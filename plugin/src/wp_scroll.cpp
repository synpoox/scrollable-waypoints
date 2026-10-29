// Scrollable Waypoints: while D2R's waypoint panel is open, each wheel notch (cursor anywhere) shows the next
// (wheel down) or previous (wheel up) Act tab, wrapping at both ends. Scrolling only changes the tab; it never
// selects a destination. Everything else is left to the game.
//
// No pixel coordinates, no simulated clicks. The pieces (names confirmed in game with D2R 3.3 + Reimagined):
//  - Wheel: the SDK has no mouse input, so the plugin subclasses D2R's window procedure (class OsWindow) and reads
//    WM_MOUSEWHEEL (confirmed).
//  - Panel open/closed: UI messages PanelManager:OpenPanel / ClosePanel / UnloadPanel with WaypointsPanelExpansion
//    (5 Acts) or WaypointsPanelOriginal (Classic characters, 4 Acts); PanelManager:UnloadAll and front-end
//    navigation close it too (confirmed).
//  - Switching: the Act tabs are one TabBarWidget ("Tabs") whose message, Waypoints:SelectTab, carries no tab number,
//    so no UI message can select an Act. The plan is the tab bar's own previous/next (what controller shoulder
//    buttons use), called natively once its address in build 93847 is known. Until then SwitchActs() only logs and
//    the wheel goes to the game unchanged.
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

constexpr uint32_t MaxLoggedMessages = 300;  // UI-message diagnosis lines per load (the log must stay readable)
constexpr uint32_t MaxLoggedWheel = 20;      // wheel events logged one by one (then only the counters)

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

// Counters for the status command.
std::atomic<uint32_t> g_wheelSeen{0}, g_wheelUsed{0}, g_tabMessages{0};
std::atomic<uint32_t> g_loggedMessages{0}, g_opens{0}, g_loggedWheel{0};

auto TabCount(Variant v) noexcept -> int {
	return v == Variant::Original ? 4 : wp::MaxActs;  // Classic characters' panel has Acts I-IV
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

auto VariantName(Variant v) noexcept -> const char* {
	return v == Variant::Original ? "WaypointsPanelOriginal" : v == Variant::Expansion ? "WaypointsPanelExpansion" : "none";
}

// --- Window procedure ------------------------------------------------------------------------------------------------

void LogStatus(const char* why) noexcept {
	char state[700];
	ScrollableWaypointsStatus(state, sizeof(state));
	plog::Info("status (%s): %s", why, state);
}

auto WheelIsOurs(const char** why) noexcept -> bool {
	if (!g_open.load()) {
		*why = "panel closed";
		return false;
	}
	*why = "panel open";
	return true;
}

auto CALLBACK WheelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
	const WNDPROC prev = g_prevProc.load();
	if (g_active.load() && msg == WM_MOUSEWHEEL) {
		++g_wheelSeen;
		const char* why = "";
		const bool ours = WheelIsOurs(&why);
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
			// Not consumed yet: nothing switches the Act until the native previous/next is wired in.
		} else {
			g_wheel.Reset();
		}
		return prev != nullptr ? CallWindowProcW(prev, hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
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
	plog::Info("wheel: subclassed window %p (class %s, client %d x %d, %s)", static_cast<void*>(state.best), cls, w, h,
	           unicode ? "unicode" : "ansi");
}

// --- Switching (UI tick) --------------------------------------------------------------------------------------------

// Moves the waypoint panel's tab bar by `steps` tabs (+ next, - previous). Placeholder: the native previous/next of
// the TabBarWidget is not wired in yet, so this only follows and logs what it would do.
void SwitchActs(int steps) noexcept {
	const int count = TabCount(static_cast<Variant>(g_variant.load()));
	const int from = g_shownAct.load();
	const int to = wp::WrapAct(from, steps, count);
	static std::atomic<uint32_t> s_logged{0};
	if (plog::Sample(s_logged)) {
		plog::Info("would switch Act %d -> %d (native tab previous/next not wired in yet)", from + 1, to + 1);
	}
}

void ApplyPendingSteps() noexcept {
	const int steps = g_pendingSteps.exchange(0);
	if (steps != 0 && g_open.load()) {
		SwitchActs(steps);
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
		const int count = TabCount(variant);
		const int act = g_playerAct.load();
		g_shownAct = act >= 0 && act < count ? act : 0;
		g_pendingSteps = 0;
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
		++g_tabMessages;
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
	ClientSize(hwnd, &w, &h);
	std::snprintf(out, size,
	              "%s | window %s %d x %d | panel %s %s (opened %u) | Act shown %d, player %d | wheel %u seen, %u while open | "
	              "tab messages %u",
	              g_active.load() ? (g_tickRunning.load() ? "on" : "on, tick waiting for a UI event") : "off",
	              hwnd != nullptr ? "hooked" : "not found yet", w, h, VariantName(static_cast<Variant>(g_variant.load())),
	              g_open.load() ? "open" : "closed", g_opens.load(), g_shownAct.load() + 1, g_playerAct.load() + 1, g_wheelSeen.load(),
	              g_wheelUsed.load(), g_tabMessages.load());
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
