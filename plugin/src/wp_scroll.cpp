// Scrollable Waypoints: while D2R's waypoint panel is open and the cursor is over it, each wheel notch shows the next
// (wheel down) or previous (wheel up) Act tab, wrapping at both ends. Scrolling only changes the tab; it never
// selects a destination. Everything else (clicks, tabs, destinations, the wheel elsewhere or with the panel closed) is
// left to the game.
//
// No game address is used. The pieces (names confirmed in game with D2R 3.3 + Reimagined, and in the layouts):
//  - Wheel: the SDK has no mouse input, so the plugin subclasses D2R's window procedure (class OsWindow) and reads
//    WM_MOUSEWHEEL. A notch over the open panel is consumed and turned into Act steps; any other wheel message goes to
//    the game unchanged.
//  - Panel open/closed: UI messages PanelManager:OpenPanel / ClosePanel / UnloadPanel with WaypointsPanelExpansion
//    (5 Acts) or WaypointsPanelOriginal (Classic characters, 4 Acts); PanelManager:UnloadAll and front-end navigation
//    close it too.
//  - Switching: the Act tabs are one TabBarWidget ("Tabs"); a click selects a tab inside the widget, which then sends
//    "Waypoints:SelectTab" without a tab number, so no message can ask for Act N. The plugin clicks the tab: it moves
//    the cursor to the tab's centre, posts a left click there, and puts the cursor back once the tab switched.
//  - Where the tabs are: wp_logic.h computes them from the layout and the window size. Two canvas models are possible
//    (full width, centred 16:9), so a real tab click by the player confirms which one is right for this window size
//    before the plugin ever clicks (saved in "<dll name>-calibration.txt" next to the DLL). Until then the wheel goes
//    to the game.
//  - Which Act is shown: the panel opens on the player's current Act (Lifecycle ActChanged); the player's own tab
//    clicks (the tab under the cursor) and the plugin's clicks keep it in step.
//  - Locked Acts: a click on a tab that does not answer with SelectTab marks it locked until the panel closes, and
//    the step moves on to the next tab.
#include "wp_scroll.h"

#include "log.h"
#include "wp_logic.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

constexpr uint32_t MaxLoggedMessages = 300;  // UI-message diagnosis lines per load (the log must stay readable)
constexpr uint32_t MaxLoggedWheel = 20;      // wheel events logged one by one (then only the counters)
constexpr uint64_t RestoreAfterTicks = 2;    // earliest restore of the cursor after a click (the game must see it)
constexpr uint64_t ClickTimeoutTicks = 15;   // no SelectTab by then: the tab is locked
constexpr int MaxCalibrations = 16;

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

enum class Variant : int { None = 0, Expansion = 1, Original = 2 };

std::atomic<bool> g_active{false};     // installed and not unloading
std::atomic<bool> g_open{false};       // waypoint panel open (UI messages), read by the window procedure
std::atomic<int> g_variant{0};         // Variant of the open panel
std::atomic<int> g_pendingSteps{0};    // Act steps from the wheel, applied by the next UI tick
std::atomic<int> g_shownAct{0};        // 0-based Act the panel shows
std::atomic<int> g_playerAct{-1};      // 0-based Act the player is in (-1 unknown)
std::atomic<bool> g_tickRunning{false};
std::atomic<uint64_t> g_ticks{0};

// Window subclass. g_hwnd is set by the UI tick; the window procedure reads g_prevProc.
std::atomic<HWND> g_hwnd{nullptr};
std::atomic<WNDPROC> g_prevProc{nullptr};
bool g_pinned = false;
wp::WheelAccumulator g_wheel;  // window-procedure thread only

// Calibration: which canvas model a real tab click confirmed, per client size. Guarded by g_calLock.
struct Calibration {
	int width, height, canvas;
};
SRWLOCK g_calLock = SRWLOCK_INIT;
Calibration g_cal[MaxCalibrations] = {};
int g_calCount = 0;
wchar_t g_calPath[600] = {};

// Plugin click in flight (UI thread).
struct Click {
	bool active;
	uint64_t tick;     // g_ticks when posted
	int tab;           // tab clicked
	int previous;      // tab shown before
	int direction;     // +1 / -1: where a locked tab moves the step
	bool answered;     // SelectTab arrived
	POINT savedCursor; // screen position to put back
};
Click g_click = {};
unsigned g_lockedTabs = 0;  // bit per tab that did not answer, since the panel opened

// Counters for the status command.
std::atomic<uint32_t> g_wheelSeen{0}, g_wheelUsed{0}, g_switches{0}, g_locked{0}, g_realTabClicks{0}, g_skipped{0};
std::atomic<uint32_t> g_loggedMessages{0}, g_opens{0}, g_loggedWheel{0};

auto TabsOf(Variant v) noexcept -> const wp::TabBar& {
	return v == Variant::Original ? wp::OriginalTabs : wp::ExpansionTabs;
}

auto VariantName(Variant v) noexcept -> const char* {
	return v == Variant::Original ? "WaypointsPanelOriginal" : v == Variant::Expansion ? "WaypointsPanelExpansion" : "none";
}

// --- Calibration -----------------------------------------------------------------------------------------------------

// Confirmed canvas model for this client size, or -1.
auto CalibratedCanvas(int width, int height) noexcept -> int {
	int canvas = -1;
	AcquireSRWLockShared(&g_calLock);
	for (int i = 0; i < g_calCount; ++i) {
		if (g_cal[i].width == width && g_cal[i].height == height) {
			canvas = g_cal[i].canvas;
		}
	}
	ReleaseSRWLockShared(&g_calLock);
	return canvas;
}

void SaveCalibration() noexcept {
	if (g_calPath[0] == L'\0') {
		return;
	}
	FILE* f = _wfopen(g_calPath, L"w");
	if (f == nullptr) {
		plog::Warn("could not write the calibration file (%ls): the tab click is asked for again next start", g_calPath);
		return;
	}
	AcquireSRWLockShared(&g_calLock);
	std::fprintf(f, "# scrollable-waypoints: confirmed tab positions (client width, height, canvas model). Delete to redo.\n");
	for (int i = 0; i < g_calCount; ++i) {
		std::fprintf(f, "%d %d %d\n", g_cal[i].width, g_cal[i].height, g_cal[i].canvas);
	}
	ReleaseSRWLockShared(&g_calLock);
	std::fclose(f);
}

void LoadCalibration(const wchar_t* pluginPath) noexcept {
	if (pluginPath == nullptr) {
		return;
	}
	std::swprintf(g_calPath, sizeof(g_calPath) / sizeof(g_calPath[0]), L"%ls", pluginPath);
	wchar_t* dot = std::wcsrchr(g_calPath, L'.');
	if (dot == nullptr) {
		dot = g_calPath + std::wcslen(g_calPath);
	}
	const size_t room = sizeof(g_calPath) / sizeof(g_calPath[0]) - static_cast<size_t>(dot - g_calPath);
	std::swprintf(dot, room, L"-calibration.txt");
	FILE* f = _wfopen(g_calPath, L"r");
	if (f == nullptr) {
		plog::Info("no calibration yet (%ls): click any Act tab once to confirm where the tabs are", g_calPath);
		return;
	}
	char line[128];
	while (std::fgets(line, sizeof(line), f) != nullptr && g_calCount < MaxCalibrations) {
		Calibration c{};
		if (line[0] != '#' && std::sscanf(line, "%d %d %d", &c.width, &c.height, &c.canvas) == 3 && c.canvas >= 0 &&
		    c.canvas < static_cast<int>(wp::Canvas::Count)) {
			g_cal[g_calCount++] = c;
		}
	}
	std::fclose(f);
	plog::Info("calibration loaded: %d window size(s)", g_calCount);
}

void RememberCalibration(int width, int height, int canvas) noexcept {
	AcquireSRWLockExclusive(&g_calLock);
	int i = 0;
	while (i < g_calCount && !(g_cal[i].width == width && g_cal[i].height == height)) {
		++i;
	}
	const bool changed = i == g_calCount || g_cal[i].canvas != canvas;
	if (i == g_calCount && g_calCount < MaxCalibrations) {
		++g_calCount;
	}
	if (i < MaxCalibrations) {
		g_cal[i] = {width, height, canvas};
	}
	ReleaseSRWLockExclusive(&g_calLock);
	if (changed) {
		plog::Info("tabs confirmed at %d x %d (canvas model %d): scrolling is on for this window size", width, height, canvas);
		SaveCalibration();
	}
}

auto ClientSize(HWND hwnd, int* width, int* height) noexcept -> bool {
	RECT rc{};
	if (hwnd == nullptr || !GetClientRect(hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0) {
		return false;
	}
	*width = rc.right;
	*height = rc.bottom;
	return true;
}

// The player clicked a tab: find which canvas model puts a tab under the cursor, confirm it, and follow the tab.
void OnRealTabClick() noexcept {
	const HWND hwnd = g_hwnd.load();
	int w = 0, h = 0;
	POINT pt{};
	if (!ClientSize(hwnd, &w, &h) || !GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt)) {
		return;
	}
	++g_realTabClicks;
	const wp::TabBar& tabs = TabsOf(static_cast<Variant>(g_variant.load()));
	int hitCanvas = -1, hitTab = -1, hits = 0;
	for (int c = 0; c < static_cast<int>(wp::Canvas::Count); ++c) {
		const int tab = wp::TabAt(wp::MapPanel(static_cast<wp::Canvas>(c), w, h), tabs, pt.x, pt.y);
		if (tab >= 0) {
			if (hits == 0 || tab != hitTab) {
				++hits;
			}
			if (hitCanvas < 0) {
				hitCanvas = c;
				hitTab = tab;
			}
		}
	}
	if (hits == 1) {
		g_shownAct = hitTab;
		RememberCalibration(w, h, hitCanvas);
		return;
	}
	const wp::Mapping full = wp::MapPanel(wp::Canvas::FullWidth, w, h);
	const wp::Box bar = wp::BarBox(full, tabs);
	plog::Warn("tab click at %ld,%ld (window %d x %d) is not on a predicted tab (%d models hit; full-width bar %.0f,%.0f-%.0f,%.0f): "
	           "scrolling stays off for this size",
	           pt.x, pt.y, w, h, hits, bar.left, bar.top, bar.right, bar.bottom);
}

// --- Window procedure ------------------------------------------------------------------------------------------------

void LogStatus(const char* why) noexcept {
	char state[700];
	ScrollableWaypointsStatus(state, sizeof(state));
	plog::Info("status (%s): %s", why, state);
}

// Cursor over the open, calibrated panel. `why` says why not, for the log.
auto WheelIsOurs(HWND hwnd, LPARAM lParam, const char** why) noexcept -> bool {
	if (!g_open.load()) {
		*why = "panel closed";
		return false;
	}
	int w = 0, h = 0;
	POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};  // WM_MOUSEWHEEL carries screen coordinates
	if (!ClientSize(hwnd, &w, &h) || !ScreenToClient(hwnd, &pt)) {
		*why = "no client area";
		return false;
	}
	const int canvas = CalibratedCanvas(w, h);
	if (canvas < 0) {
		*why = "tabs not confirmed for this window size yet (click an Act tab once)";
		return false;
	}
	if (!wp::PanelBox(wp::MapPanel(static_cast<wp::Canvas>(canvas), w, h)).Contains(pt.x, pt.y)) {
		*why = "cursor not over the panel";
		return false;
	}
	*why = "used";
	return true;
}

auto CALLBACK WheelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
	const WNDPROC prev = g_prevProc.load();
	if (g_active.load() && msg == WM_MOUSEWHEEL) {
		++g_wheelSeen;
		const char* why = "";
		const bool ours = WheelIsOurs(hwnd, lParam, &why);
		if (g_loggedWheel.load() < MaxLoggedWheel) {
			++g_loggedWheel;
			plog::Info("wheel delta %d at screen %d,%d: %s", static_cast<int>(GET_WHEEL_DELTA_WPARAM(wParam)),
			           static_cast<int>(GET_X_LPARAM(lParam)), static_cast<int>(GET_Y_LPARAM(lParam)), why);
		}
		if (ours) {
			const int steps = g_wheel.Feed(GET_WHEEL_DELTA_WPARAM(wParam));
			if (steps != 0) {
				g_pendingSteps += steps;
			}
			++g_wheelUsed;
			return 0;  // consumed: the game does not also act on this notch
		}
		g_wheel.Reset();
	}
	return prev != nullptr ? CallWindowProcW(prev, hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

struct FindWindowState {
	DWORD pid;
	HWND best;
	LONG bestArea;
	bool bestIsGame;
};

auto CALLBACK FindGameWindow(HWND hwnd, LPARAM param) -> BOOL {
	auto* state = reinterpret_cast<FindWindowState*>(param);
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (pid != state->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
		return TRUE;
	}
	char cls[64] = {};
	GetClassNameA(hwnd, cls, sizeof(cls));
	const bool isGame = std::strcmp(cls, "OsWindow") == 0;  // D2R's main window class
	RECT rc{};
	GetClientRect(hwnd, &rc);
	const LONG area = rc.right * rc.bottom;
	if ((isGame && !state->bestIsGame) || (isGame == state->bestIsGame && area > state->bestArea)) {
		state->best = hwnd;
		state->bestArea = area;
		state->bestIsGame = isGame;
	}
	return TRUE;
}

// Keeps the subclass on D2R's window (finds it once it exists, again if it is recreated).
void EnsureSubclass() noexcept {
	const HWND current = g_hwnd.load();
	if (current != nullptr && IsWindow(current)) {
		return;
	}
	if (current != nullptr) {
		plog::Info("game window %p is gone; looking for the new one", static_cast<void*>(current));
		g_hwnd = nullptr;
		g_prevProc = nullptr;
	}
	FindWindowState state{GetCurrentProcessId(), nullptr, 0, false};
	EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&state));
	if (state.best == nullptr) {
		return;  // not created yet: next tick
	}
	if (!g_pinned) {
		// The window may outlive this plugin (or someone may chain after us), so keep the DLL loaded for good: the
		// procedure stays callable and passes everything through once g_active is false.
		HMODULE self = nullptr;
		g_pinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
		                              reinterpret_cast<LPCWSTR>(&WheelWndProc), &self) != FALSE;
	}
	const bool unicode = IsWindowUnicode(state.best) != FALSE;
	g_prevProc = reinterpret_cast<WNDPROC>(unicode ? GetWindowLongPtrW(state.best, GWLP_WNDPROC) : GetWindowLongPtrA(state.best, GWLP_WNDPROC));
	const LONG_PTR old = unicode ? SetWindowLongPtrW(state.best, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WheelWndProc))
	                             : SetWindowLongPtrA(state.best, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WheelWndProc));
	if (old == 0) {
		plog::Warn("could not subclass window %p (error %lu): the wheel is not read", static_cast<void*>(state.best), GetLastError());
		return;
	}
	g_prevProc = reinterpret_cast<WNDPROC>(old);
	g_hwnd = state.best;
	char cls[64] = {};
	GetClassNameA(state.best, cls, sizeof(cls));
	int w = 0, h = 0;
	ClientSize(state.best, &w, &h);
	plog::Info("wheel: subclassed window %p (class %s, client %d x %d, %s; tabs %s for this size)", static_cast<void*>(state.best), cls,
	           w, h, unicode ? "unicode" : "ansi", CalibratedCanvas(w, h) >= 0 ? "confirmed" : "not confirmed yet");
}

// --- Clicking a tab (UI tick) ----------------------------------------------------------------------------------------

auto MakePoint(LONG x, LONG y) noexcept -> LPARAM {
	return MAKELPARAM(static_cast<WORD>(static_cast<SHORT>(x)), static_cast<WORD>(static_cast<SHORT>(y)));
}

void RestoreCursor() noexcept {
	SetCursorPos(g_click.savedCursor.x, g_click.savedCursor.y);
	const HWND hwnd = g_hwnd.load();
	POINT client = g_click.savedCursor;
	if (hwnd != nullptr && ScreenToClient(hwnd, &client)) {
		PostMessageW(hwnd, WM_MOUSEMOVE, 0, MakePoint(client.x, client.y));
	}
	g_click.active = false;
}

// Next tab from `from` by `steps`, skipping tabs found locked since the panel opened.
auto NextTab(int from, int steps, int count) noexcept -> int {
	const int dir = steps > 0 ? 1 : -1;
	int tab = from;
	for (int moved = 0; moved < (steps > 0 ? steps : -steps); ++moved) {
		int guard = 0;
		do {
			tab = wp::WrapAct(tab, dir, count);
		} while ((g_lockedTabs & (1U << tab)) != 0 && tab != from && ++guard < count);
	}
	return tab;
}

void ClickTab(int tab, int previous, int direction) noexcept {
	const HWND hwnd = g_hwnd.load();
	int w = 0, h = 0;
	if (!ClientSize(hwnd, &w, &h)) {
		return;
	}
	const int canvas = CalibratedCanvas(w, h);
	if (canvas < 0) {
		return;
	}
	const wp::TabBar& tabs = TabsOf(static_cast<Variant>(g_variant.load()));
	double cx = 0, cy = 0;
	wp::TabCenter(wp::MapPanel(static_cast<wp::Canvas>(canvas), w, h), tabs, tab, &cx, &cy);
	POINT client{static_cast<LONG>(cx), static_cast<LONG>(cy)};
	POINT screen = client;
	POINT saved{};
	if (!ClientToScreen(hwnd, &screen) || !GetCursorPos(&saved)) {
		return;
	}
	g_click = {true, g_ticks.load(), tab, previous, direction, false, saved};
	// The game may read the cursor or the message position: both point at the tab's centre while it handles the click.
	SetCursorPos(screen.x, screen.y);
	const LPARAM at = MakePoint(client.x, client.y);
	PostMessageW(hwnd, WM_MOUSEMOVE, 0, at);
	PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, at);
	PostMessageW(hwnd, WM_LBUTTONUP, 0, at);
	g_shownAct = tab;
	++g_switches;
}

void ApplyPendingSteps() noexcept {
	if (g_click.active) {
		const uint64_t age = g_ticks.load() - g_click.tick;
		if (age < RestoreAfterTicks || (!g_click.answered && age < ClickTimeoutTicks)) {
			return;  // the game has not handled the click yet
		}
		const bool answered = g_click.answered;
		RestoreCursor();
		if (!answered) {
			// No SelectTab: the tab did not switch (an Act not reached yet). Skip it until the panel closes.
			g_lockedTabs |= 1U << g_click.tab;
			g_shownAct = g_click.previous;
			++g_locked;
			plog::Info("Act %d tab did not answer: treated as locked until the panel closes", g_click.tab + 1);
			g_pendingSteps += g_click.direction;  // the notch still moves on, to the next open Act
		}
	}
	const int steps = g_pendingSteps.exchange(0);
	if (steps == 0 || !g_open.load()) {
		return;
	}
	if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) {
		++g_skipped;  // a button is held (dragging, casting): never click into that
		return;
	}
	const int count = TabsOf(static_cast<Variant>(g_variant.load())).count;
	const int from = g_shownAct.load();
	const int tab = NextTab(from, steps, count);
	if (tab != from) {
		ClickTab(tab, from, steps > 0 ? 1 : -1);
	}
}

// --- UI tick ---------------------------------------------------------------------------------------------------------

void __cdecl Tick(const D2RL::PluginContext* context, void* userData) noexcept;

// Queues the next tick. False when the loader refuses; the tick then stays stopped until StartTick succeeds.
auto QueueTick() noexcept -> bool {
	const D2RL::Threads::Result r = g_threads->runOnUiThread(g_ctx, Tick, nullptr);
	if (r == D2RL::Threads::Result::Success) {
		return true;
	}
	g_tickRunning = false;
	static std::atomic<uint32_t> s_refused{0};
	if (plog::Sample(s_refused)) {
		plog::Info("UI tick not queued (result %u, %u so far); retried on the next UI event", static_cast<unsigned>(r), s_refused.load());
	}
	return false;
}

// Starts the per-frame tick unless it runs already. D2RLoader refuses runOnUiThread while the plugin is still loading
// (OwnerInactive, seen with D2RLoader 1.3.1), so this is called from every later callback (UI messages, gameplay
// events) until it succeeds.
void StartTick() noexcept {
	if (!g_active.load() || g_tickRunning.exchange(true)) {
		return;
	}
	if (QueueTick()) {
		plog::Info("UI tick started");
	}
}

void __cdecl Tick(const D2RL::PluginContext* context, void* userData) noexcept {
	(void)context;
	(void)userData;
	if (!g_active.load()) {
		g_tickRunning = false;
		return;
	}
	++g_ticks;
	EnsureSubclass();
	ApplyPendingSteps();
	(void)QueueTick();  // runs again on the next UI update
}

// --- Listeners -------------------------------------------------------------------------------------------------------

void SetOpen(bool open, Variant variant) noexcept {
	if (open == g_open.load() && (!open || variant == static_cast<Variant>(g_variant.load()))) {
		return;
	}
	if (open) {
		g_variant = static_cast<int>(variant);
		const int count = TabsOf(variant).count;
		const int act = g_playerAct.load();
		g_shownAct = act >= 0 && act < count ? act : 0;
		g_pendingSteps = 0;
		g_lockedTabs = 0;
		++g_opens;
		g_open = true;
		plog::Info("waypoint panel open: %s, Act %d shown", VariantName(variant), g_shownAct.load() + 1);
	} else {
		g_open = false;
		g_pendingSteps = 0;
		plog::Info("waypoint panel closed");
	}
	LogStatus(open ? "panel opened" : "panel closed");
}

auto __cdecl OnUiMessage(const D2RL::PluginContext* context, const D2RL::SharedEvents::UiMessageEvent* event, void* userData) noexcept
	-> D2RL::SharedEvents::UiMessageAction {
	(void)context;
	(void)userData;
	constexpr auto Continue = D2RL::SharedEvents::UiMessageAction::Continue;  // never consumed: the game always handles it
	if (event == nullptr || event->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize || !g_active.load()) {
		return Continue;
	}
	StartTick();
	const char* target = event->target != nullptr ? event->target : "";
	const char* command = event->command != nullptr ? event->command : "";
	const char* text = event->text != nullptr ? event->text : "";

	// Diagnosis: messages naming waypoints, and every message while the panel is open.
	const bool mentionsWaypoint = wp::Contains(target, "waypoint") || wp::Contains(text, "waypoint");
	if ((mentionsWaypoint || g_open.load()) && g_loggedMessages.load() < MaxLoggedMessages) {
		const uint32_t n = ++g_loggedMessages;
		plog::Info("ui message %s:%s:%s%s", target, command, text, n == MaxLoggedMessages ? " (last one logged)" : "");
	}

	if (std::strcmp(target, "PanelManager") == 0) {
		if (wp::Contains(text, "waypointspanel")) {
			const Variant variant = wp::Contains(text, "original") ? Variant::Original : Variant::Expansion;
			if (std::strcmp(command, "OpenPanel") == 0 || std::strcmp(command, "OpenExclusivePanel") == 0) {
				SetOpen(true, variant);
			} else if (std::strcmp(command, "ClosePanel") == 0 || std::strcmp(command, "UnloadPanel") == 0) {
				SetOpen(false, variant);
			}
		} else if (std::strcmp(command, "UnloadAll") == 0) {
			SetOpen(false, Variant::None);
		}
	} else if (std::strcmp(target, "FrontEndNavigation") == 0) {
		SetOpen(false, Variant::None);
	} else if (std::strcmp(target, "Waypoints") == 0 && std::strcmp(command, "SelectTab") == 0) {
		if (g_click.active) {
			g_click.answered = true;  // our own click switched the tab
		} else {
			OnRealTabClick();
		}
	}
	return Continue;
}

void __cdecl OnGameplayEvent(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent* event, void* userData) noexcept {
	(void)context;
	(void)userData;
	if (event == nullptr || !D2RL::Lifecycle::HasGameplayEventField(event, D2RL::Lifecycle::GameplayEventRequiredSize)) {
		return;
	}
	StartTick();
	if (event->kind == D2RL::Lifecycle::GameplayEventKind::ActChanged) {
		g_playerAct = event->currentValue;
	} else if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) {
		g_playerAct = -1;
		SetOpen(false, Variant::None);
	}
}

template <typename Service>
auto Query(const D2RL::PluginContext* context, const Service** service, bool (*has)(const Service*, uint32_t), uint32_t size) noexcept -> bool {
	return context->QueryService(service) == D2RL::ServiceQueryResult::Success && has(*service, size);
}

}  // namespace

auto InstallScrollableWaypoints(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const D2RL::SharedEventService* events = nullptr;
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (!Query(context, &g_widgets, D2RL::HasWidgetServiceField, D2RL::WidgetServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: D2RLoader has no widget service");
		return false;
	}
	if (!Query(context, &g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: D2RLoader has no thread service");
		return false;
	}
	if (!Query(context, &events, D2RL::HasSharedEventServiceField, D2RL::SharedEventServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: D2RLoader has no shared-event service");
		return false;
	}
	if (!Query(context, &lifecycle, D2RL::HasLifecycleServiceField, D2RL::LifecycleServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: D2RLoader has no lifecycle service");
		return false;
	}
	g_ctx = context;
	LoadCalibration(context->pluginPath);

	const D2RL::SharedEvents::UiMessageListener messages{
		.structSize = D2RL::SharedEvents::UiMessageListenerSize,
		.flags = 0,
		.priority = 0,
		.reserved = 0,
		.callback = OnUiMessage,
		.userData = nullptr,
	};
	D2RL::SharedEvents::ListenerHandle messageHandle = D2RL::SharedEvents::InvalidHandle;
	if (events->registerUiMessageListener(context, &messages, &messageHandle) != D2RL::SharedEvents::Result::Success) {
		std::snprintf(report, reportSize, "off: UI message listener refused");
		return false;
	}
	for (const auto kind : {D2RL::Lifecycle::GameplayEventKind::ActChanged, D2RL::Lifecycle::GameplayEventKind::GameLeft,
	                        D2RL::Lifecycle::GameplayEventKind::GameJoined, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady}) {
		const D2RL::Lifecycle::GameplayEventListener listener{
			.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
			.flags = 0,
			.kind = kind,
			.reserved = 0,
			.callback = OnGameplayEvent,
			.userData = nullptr,
		};
		D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
		if (lifecycle->registerGameplayEventListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
			// Not fatal: without the player's Act the panel is assumed to open on Act I until a tab click says otherwise.
			plog::Warn("gameplay listener %u refused: the Act shown on open is a guess", static_cast<unsigned>(kind));
		}
	}

	g_active = true;
	StartTick();  // refused while loading; the listeners start it on the first UI event
	std::snprintf(report, reportSize, "on: wheel over the open waypoint panel switches Acts");
	return true;
}

void ScrollableWaypointsStatus(char* out, size_t size) noexcept {
	const HWND hwnd = g_hwnd.load();
	int w = 0, h = 0;
	const bool sized = ClientSize(hwnd, &w, &h);
	const int canvas = sized ? CalibratedCanvas(w, h) : -1;
	std::snprintf(out, size,
	              "%s | window %s %d x %d, tabs %s | panel %s %s (opened %u) | Act shown %d, player %d | wheel %u seen, %u used | "
	              "tab clicks: player %u, plugin %u, locked %u, skipped %u",
	              g_active.load() ? (g_tickRunning.load() ? "on" : "on, tick waiting for a UI event") : "off",
	              hwnd != nullptr ? "hooked" : "not found yet", w, h,
	              canvas >= 0 ? (canvas == 0 ? "confirmed (full width)" : "confirmed (centred 16:9)") : "NOT CONFIRMED (click an Act tab once)",
	              VariantName(static_cast<Variant>(g_variant.load())), g_open.load() ? "open" : "closed", g_opens.load(),
	              g_shownAct.load() + 1, g_playerAct.load() + 1, g_wheelSeen.load(), g_wheelUsed.load(), g_realTabClicks.load(),
	              g_switches.load(), g_locked.load(), g_skipped.load());
}

void UninstallScrollableWaypoints() noexcept {
	g_active = false;
	g_open = false;
	const HWND hwnd = g_hwnd.load();
	if (hwnd == nullptr || !IsWindow(hwnd)) {
		return;
	}
	const bool unicode = IsWindowUnicode(hwnd) != FALSE;
	const LONG_PTR current = unicode ? GetWindowLongPtrW(hwnd, GWLP_WNDPROC) : GetWindowLongPtrA(hwnd, GWLP_WNDPROC);
	if (current == reinterpret_cast<LONG_PTR>(&WheelWndProc) && g_prevProc.load() != nullptr) {
		if (unicode) {
			SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_prevProc.load()));
		} else {
			SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_prevProc.load()));
		}
	}
	// Otherwise something subclassed after us: stay in the chain as a pass-through (the DLL is pinned).
}
