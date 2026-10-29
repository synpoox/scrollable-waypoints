// Scrollable Waypoints: while D2R's waypoint panel is open and the cursor is over it, each wheel notch shows the next
// (wheel down) or previous (wheel up) Act tab, wrapping I..V. Scrolling only changes the tab; it never selects a
// destination. Everything else (clicks, tabs, destinations, the wheel elsewhere or with the panel closed) is left to
// the game.
//
// No game address is used, so the part does not depend on the game image. The pieces:
//  - Wheel: D2RLoader's SDK has no mouse input (InputService is keyboard actions only), so the plugin subclasses D2R's
//    window procedure and reads WM_MOUSEWHEEL. A wheel notch over the open panel is consumed (the game does not also
//    see it) and turned into Act steps; any other wheel message goes to the game unchanged.
//  - Panel open/closed: a UI-thread tick, once per frame, resolves the panel by name (WidgetService::findPanel +
//    getWidgetRect). Resolving = open. The tick also applies the queued Act steps and keeps the subclass attached.
//  - Which Act is shown: the panel opens on the player's current Act (Lifecycle ActChanged); after that, every
//    waypoint tab message (the player's own tab clicks and ours) is followed through the UI message listener.
//  - Switching: WidgetService::dispatchUiAction with the panel's own tab message (target/command/text, the shape of
//    the layout's onClickMessage). Only a command containing "tab" is ever sent (wp_logic.h).
//
// UNVERIFIED (no game files or game run when this was written; TESTING.md has the checks):
//  - the panel's name (PanelNames below), the tab message (DefaultTabTarget/DefaultTabCommand, 0-based text). A tab
//    click on the panel teaches the real message (logged as "learned tab message"), so a wrong default only means
//    the first scroll of a game needs one tab click first;
//  - that findPanel stops resolving while the panel is closed (the SDK says handles survive closing and reopening);
//  - that D2R receives the wheel as WM_MOUSEWHEEL (WM_INPUT wheel events are counted for the diagnosis);
//  - the hover area: the left half of the window's client area (D2R's waypoint panel is a left-side panel; the
//    SDK's widget rects are parent-local, with no mapping to screen pixels). The open panel's rect is logged.
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

namespace {

// Candidate root names of waypointspanelhd.json, first match wins (UNVERIFIED).
constexpr const char* PanelNames[] = {"WaypointsPanel", "WaypointPanel", "WaypointsPanelHD", "WaypointPanelHD", "Waypoints",
                                      "WaypointsMenu", "WaypointMenu", "WaypointsPanelLayout", "waypointspanelhd"};
constexpr int PanelNameCount = static_cast<int>(sizeof(PanelNames) / sizeof(PanelNames[0]));

// Tab message until a real tab click teaches it, by analogy with SkillsTreePanelMessage:ActivateTab:<n> (UNVERIFIED).
constexpr const char* DefaultTabTarget = "WaypointsPanelMessage";
constexpr const char* DefaultTabCommand = "ActivateTab";

constexpr uint32_t MaxLoggedMessages = 300;  // UI-message diagnosis lines per load (the log must stay readable)
constexpr uint32_t MaxLoggedWheel = 20;      // wheel events logged one by one (then only the counters)
constexpr int MaxDistinctMessages = 512;     // distinct target:command pairs logged once each

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

std::atomic<bool> g_active{false};    // installed and not unloading
std::atomic<bool> g_open{false};      // waypoint panel open (written by the UI tick, read by the window procedure)
std::atomic<int> g_pendingSteps{0};   // Act steps from the wheel, applied by the next UI tick
std::atomic<int> g_shownAct{0};       // 0-based Act the panel shows
std::atomic<int> g_playerAct{-1};     // 0-based Act the player is in (-1 unknown)
std::atomic<bool> g_tickRunning{false};

// Window subclass. g_hwnd is touched by the UI tick only; the window procedure reads g_prevProc.
HWND g_hwnd = nullptr;
std::atomic<WNDPROC> g_prevProc{nullptr};
bool g_pinned = false;
wp::WheelAccumulator g_wheel;  // window-procedure thread only

// Panel lookup (UI tick only).
D2RL::Widgets::WidgetHandle g_panel = D2RL::Widgets::InvalidHandle;
int g_panelName = -1;

// Tab message: the default, or the one learned from a click. Guarded by g_tabLock (listener vs tick).
SRWLOCK g_tabLock = SRWLOCK_INIT;
char g_tabTarget[96] = {};
char g_tabCommand[96] = {};
int g_tabBase = 0;  // panel counts tabs from 0 (or 1 when a "5" was seen)
bool g_tabLearned = false;

// Counters for the status command.
std::atomic<uint32_t> g_wheelSeen{0}, g_wheelUsed{0}, g_rawWheel{0}, g_switches{0}, g_switchFails{0}, g_tabClicks{0};
std::atomic<uint32_t> g_loggedMessages{0}, g_opens{0}, g_loggedWheel{0};

// Diagnosis state (UI thread).
uint32_t g_findResult[sizeof(PanelNames) / sizeof(PanelNames[0])];  // last findPanel result per name, logged on change
uint64_t g_seenMessages[MaxDistinctMessages];
int g_seenMessageCount = 0;
uint64_t g_ticks = 0;

// --- Window procedure ------------------------------------------------------------------------------------------------

void LogStatus(const char* why) noexcept {
	char state[700];
	ScrollableWaypointsStatus(state, sizeof(state));
	plog::Info("status (%s): %s", why, state);
}

// Cursor (client coordinates) over the waypoint panel: the left half of the client area (see the header comment).
auto OverPanel(HWND hwnd, LPARAM lParam) noexcept -> bool {
	POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};  // WM_MOUSEWHEEL carries screen coordinates
	RECT rc{};
	if (!ScreenToClient(hwnd, &pt) || !GetClientRect(hwnd, &rc)) {
		return false;
	}
	return pt.x >= 0 && pt.x < rc.right / 2 && pt.y >= 0 && pt.y < rc.bottom;
}

void CountRawWheel(LPARAM lParam) noexcept {
	RAWINPUT raw{};
	UINT size = sizeof(raw);
	if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
		return;
	}
	if (raw.header.dwType == RIM_TYPEMOUSE && (raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) != 0) {
		++g_rawWheel;
	}
}

auto CALLBACK WheelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
	const WNDPROC prev = g_prevProc.load();
	if (g_active.load() && msg == WM_MOUSEWHEEL) {
		++g_wheelSeen;
		const bool open = g_open.load();
		const bool over = open && OverPanel(hwnd, lParam);
		if (g_loggedWheel.load() < MaxLoggedWheel) {
			++g_loggedWheel;
			plog::Info("wheel delta %d at screen %d,%d: panel %s%s", static_cast<int>(GET_WHEEL_DELTA_WPARAM(wParam)),
			           static_cast<int>(GET_X_LPARAM(lParam)), static_cast<int>(GET_Y_LPARAM(lParam)), open ? "open" : "closed",
			           open ? (over ? ", cursor over it: used" : ", cursor elsewhere: passed on") : ": passed on");
		}
		static std::atomic<uint32_t> s_seen{0};
		if (plog::Sample(s_seen)) {
			LogStatus("wheel");
		}
		if (over) {
			const int steps = g_wheel.Feed(GET_WHEEL_DELTA_WPARAM(wParam));
			if (steps != 0) {
				g_pendingSteps += steps;
			}
			++g_wheelUsed;
			return 0;  // consumed: the game does not also act on this notch
		}
		g_wheel.Reset();
	} else if (msg == WM_INPUT && g_open.load()) {
		CountRawWheel(lParam);  // diagnosis only: tells whether D2R reads the wheel through raw input
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
	if (g_hwnd != nullptr && IsWindow(g_hwnd)) {
		return;
	}
	if (g_hwnd != nullptr) {
		plog::Info("game window %p is gone; looking for the new one", static_cast<void*>(g_hwnd));
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
	GetClassNameA(g_hwnd, cls, sizeof(cls));
	RECT rc{};
	GetClientRect(g_hwnd, &rc);
	plog::Info("wheel: subclassed window %p (class %s, client %ld x %ld, %s)", static_cast<void*>(g_hwnd), cls,
	           static_cast<long>(rc.right), static_cast<long>(rc.bottom), unicode ? "unicode" : "ansi");
}

// --- Tab message -----------------------------------------------------------------------------------------------------

struct TabMessage {
	char target[96];
	char command[96];
	int base;
};

auto CurrentTabMessage() noexcept -> TabMessage {
	TabMessage m{};
	AcquireSRWLockShared(&g_tabLock);
	std::snprintf(m.target, sizeof(m.target), "%s", g_tabTarget);
	std::snprintf(m.command, sizeof(m.command), "%s", g_tabCommand);
	m.base = g_tabBase;
	ReleaseSRWLockShared(&g_tabLock);
	return m;
}

// --- UI tick ---------------------------------------------------------------------------------------------------------

// Resolves the panel: true while it is open. Logs the name that matched once.
auto PanelResolves(D2RL::Widgets::Rect* rect) noexcept -> bool {
	const D2RL::PluginContext* ctx = g_ctx;
	if (g_panel != D2RL::Widgets::InvalidHandle) {
		return g_widgets->getWidgetRect(ctx, g_panel, rect) == D2RL::Widgets::Result::Success;
	}
	for (int i = 0; i < PanelNameCount; ++i) {
		D2RL::Widgets::WidgetHandle handle = D2RL::Widgets::InvalidHandle;
		const D2RL::Widgets::Result found = g_widgets->findPanel(ctx, PanelNames[i], &handle);
		if (static_cast<uint32_t>(found) != g_findResult[i]) {
			g_findResult[i] = static_cast<uint32_t>(found);
			plog::Info("findPanel(\"%s\") -> result %u", PanelNames[i], static_cast<unsigned>(found));  // 0 found, 4 not found
		}
		if (found == D2RL::Widgets::Result::Success && handle != D2RL::Widgets::InvalidHandle) {
			g_panel = handle;
			g_panelName = i;
			plog::Info("waypoint panel found as \"%s\"", PanelNames[i]);
			return g_widgets->getWidgetRect(ctx, g_panel, rect) == D2RL::Widgets::Result::Success;
		}
	}
	return false;
}

void PollPanel() noexcept {
	D2RL::Widgets::Rect rect{};
	const bool open = PanelResolves(&rect);
	if (open == g_open.load()) {
		return;
	}
	if (open) {
		const int act = g_playerAct.load();
		g_shownAct = act >= 0 && act < wp::ActCount ? act : 0;
		g_pendingSteps = 0;
		++g_opens;
		RECT client{};
		if (g_hwnd != nullptr) {
			GetClientRect(g_hwnd, &client);
		}
		plog::Info("waypoint panel open (Act %d shown, player Act %d; panel rect %d,%d %dx%d; window %ld x %ld)",
		           g_shownAct.load() + 1, act + 1, rect.x, rect.y, rect.width, rect.height, client.right, client.bottom);
	} else {
		plog::Info("waypoint panel closed");
	}
	g_open = open;
	LogStatus(open ? "panel opened" : "panel closed");
}

void ApplyPendingSteps() noexcept {
	const int steps = g_pendingSteps.exchange(0);
	if (steps == 0 || !g_open.load()) {
		return;
	}
	const int next = wp::WrapAct(g_shownAct.load(), steps);
	const TabMessage m = CurrentTabMessage();
	char text[4];
	wp::TabText(next, m.base, text, sizeof(text));
	const D2RL::Widgets::UiAction action{
		.structSize = D2RL::Widgets::UiActionSize,
		.flags = 0,
		.target = m.target,
		.command = m.command,
		.text = text,
	};
	const D2RL::Widgets::Result result = g_widgets->dispatchUiAction(g_ctx, &action);
	if (result == D2RL::Widgets::Result::Success) {
		g_shownAct = next;  // the echo through the message listener says the same
		++g_switches;
	} else {
		static std::atomic<uint32_t> s_failed{0};
		++g_switchFails;
		if (plog::Sample(s_failed)) {
			plog::Warn("dispatch %s:%s:%s failed (result %u, %u so far)", m.target, m.command, text, static_cast<unsigned>(result), s_failed.load());
		}
	}
}

void __cdecl Tick(const D2RL::PluginContext* context, void* userData) noexcept;

// Queues the next tick. False when the loader refuses; the tick then stays stopped until StartTick succeeds.
auto QueueTick() noexcept -> bool {
	const D2RL::Threads::Result r = g_threads->runOnUiThread(g_ctx, Tick, nullptr);
	if (r == D2RL::Threads::Result::Success) {
		return true;
	}
	g_tickRunning = false;
	g_open = false;
	static std::atomic<uint32_t> s_refused{0};
	if (plog::Sample(s_refused)) {
		plog::Warn("UI tick not queued (result %u, %u so far); retried on the next UI event", static_cast<unsigned>(r), s_refused.load());
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
	if (g_ticks++ == 0) {
		plog::Info("UI tick running");
	}
	EnsureSubclass();
	if (g_hwnd == nullptr && g_ticks == 600) {
		plog::Warn("no game window found after 600 UI ticks: the wheel cannot be read");
	}
	PollPanel();
	ApplyPendingSteps();
	(void)QueueTick();  // runs again on the next UI update
}

// --- Listeners -------------------------------------------------------------------------------------------------------

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

	// Diagnosis: each distinct target:command once (shows the real panel and tab message names).
	const uint64_t key = event->targetHash * 31U ^ event->commandHash;
	bool seen = false;
	for (int i = 0; i < g_seenMessageCount && !seen; ++i) {
		seen = g_seenMessages[i] == key;
	}
	if (!seen && g_seenMessageCount < MaxDistinctMessages) {
		g_seenMessages[g_seenMessageCount++] = key;
		plog::Info("ui first %s:%s:%s", target, command, text);
	}

	// Diagnosis: every message naming the waypoint panel, and every message while it is open.
	const bool mentionsWaypoint = wp::Contains(target, "waypoint") || wp::Contains(command, "waypoint") || wp::Contains(text, "waypoint");
	if ((mentionsWaypoint || g_open.load()) && g_loggedMessages.load() < MaxLoggedMessages) {
		const uint32_t n = ++g_loggedMessages;
		plog::Info("ui message %s:%s:%s%s", target, command, text, n == MaxLoggedMessages ? " (last one logged)" : "");
	}

	const int tab = wp::WaypointTabIndex(target, command, text);
	if (tab < 0) {
		return Continue;
	}
	++g_tabClicks;
	AcquireSRWLockExclusive(&g_tabLock);
	if (tab == 0) {
		g_tabBase = 0;
	} else if (tab == wp::ActCount) {
		g_tabBase = 1;
	}
	const bool changed = std::strcmp(g_tabTarget, target) != 0 || std::strcmp(g_tabCommand, command) != 0;
	if (changed) {
		std::snprintf(g_tabTarget, sizeof(g_tabTarget), "%s", target);
		std::snprintf(g_tabCommand, sizeof(g_tabCommand), "%s", command);
	}
	if (changed || !g_tabLearned) {
		plog::Info("learned tab message %s:%s:<n> (this one: %s)", g_tabTarget, g_tabCommand, text);
	}
	g_tabLearned = true;
	const int base = g_tabBase;
	ReleaseSRWLockExclusive(&g_tabLock);
	const int act = tab - base;
	if (act >= 0 && act < wp::ActCount) {
		g_shownAct = act;
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
	for (uint32_t& r : g_findResult) {
		r = 0xffffffffU;  // "not tried yet": the first result of every name is logged
	}
	std::snprintf(g_tabTarget, sizeof(g_tabTarget), "%s", DefaultTabTarget);
	std::snprintf(g_tabCommand, sizeof(g_tabCommand), "%s", DefaultTabCommand);

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
			// Not fatal: without the player's Act the panel is assumed to open on Act I until a tab message says otherwise.
			plog::Warn("gameplay listener %u refused: the Act shown on open is a guess", static_cast<unsigned>(kind));
		}
	}

	g_active = true;
	StartTick();  // usually refused while loading; the listeners start it on the first UI event
	std::snprintf(report, reportSize, "on: wheel over the open waypoint panel switches Acts (tab message %s:%s, default)",
	              DefaultTabTarget, DefaultTabCommand);
	return true;
}

void ScrollableWaypointsStatus(char* out, size_t size) noexcept {
	const TabMessage m = CurrentTabMessage();
	AcquireSRWLockShared(&g_tabLock);
	const bool learned = g_tabLearned;
	ReleaseSRWLockShared(&g_tabLock);
	std::snprintf(out, size,
	              "%s | window %s | panel %s, %s (opened %u) | Act shown %d, player %d | tab %s:%s:<n> from %d (%s) | "
	              "wheel %u seen, %u used, raw %u | switches %u, failed %u | tab messages %u",
	              g_active.load() ? (g_tickRunning.load() ? "on" : "on, tick waiting for a UI event") : "off",
	              g_hwnd != nullptr ? "hooked" : "not found yet", g_panelName >= 0 ? PanelNames[g_panelName] : "not found yet",
	              g_open.load() ? "open" : "closed", g_opens.load(), g_shownAct.load() + 1, g_playerAct.load() + 1, m.target,
	              m.command, m.base, learned ? "learned" : "default", g_wheelSeen.load(), g_wheelUsed.load(), g_rawWheel.load(),
	              g_switches.load(), g_switchFails.load(), g_tabClicks.load());
}

void UninstallScrollableWaypoints() noexcept {
	g_active = false;
	g_open = false;
	if (g_hwnd == nullptr || !IsWindow(g_hwnd)) {
		return;
	}
	const bool unicode = IsWindowUnicode(g_hwnd) != FALSE;
	const LONG_PTR current = unicode ? GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC) : GetWindowLongPtrA(g_hwnd, GWLP_WNDPROC);
	if (current == reinterpret_cast<LONG_PTR>(&WheelWndProc) && g_prevProc.load() != nullptr) {
		if (unicode) {
			SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_prevProc.load()));
		} else {
			SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_prevProc.load()));
		}
	}
	// Otherwise something subclassed after us: stay in the chain as a pass-through (the DLL is pinned).
}
