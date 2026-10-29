// Scrollable Waypoints: while D2R's waypoint panel is open, each wheel notch (cursor anywhere) shows the next
// (wheel down) or previous (wheel up) Act tab, wrapping at both ends. Scrolling only changes the tab; it never
// selects a destination. Everything else is left to the game.
//
// No pixel coordinates and no simulated input: the plugin does what a controller shoulder button does to the panel's
// tab bar. Pieces (confirmed in game and in the build-93847 image):
//  - Wheel: the SDK has no mouse input, so the plugin subclasses D2R's window procedure (class OsWindow) and reads
//    WM_MOUSEWHEEL. While the panel is open and its tab bar is known, a notch is consumed and becomes an Act step;
//    every other wheel message goes to the game unchanged.
//  - Panel open/closed: UI messages PanelManager:OpenPanel / ClosePanel / UnloadPanel with WaypointsPanelExpansion
//    (5 Acts) or WaypointsPanelOriginal (Classic characters, 4 Acts); PanelManager:UnloadAll, front-end navigation.
//  - The panel's tab bar: a TabBarWidget ("Tabs", kept at WaypointsPanel+0x170). When the panel shows an Act
//    (0x1ec820 in build 93847, on every open) it marks which Acts are available and calls TabBarWidget::SetActiveTab
//    from one place (returning to 0x1ec8d3). A hook on SetActiveTab takes the tab bar of that call: only the waypoint panel makes it.
//    It is dropped when panels are unloaded and only used while the panel is open.
//  - Switching: the tab bar's own controller code (its input handler, 0x878ed3 in build 93847): previous = current - 1 (from the
//    first tab: the last), next = current + 1 (from the last: the first), then FindAvailableTab skips tabs that are
//    not available (Acts not reached yet), then SetActiveTab(index, sound on) selects it and sends the message, so the
//    panel shows that Act exactly as after a click or a shoulder button.
//  - Finding the code: no fixed addresses and no version check. At load the plugin searches the game's code for the
//    three places by byte signature (worked out on D2R 3.3.0 build 93847, image 0x6ab3782c, with D2RLoader 1.3.1)
//    and reads the field offsets out of the code it found, so any build whose tab bar code is unchanged works. If a
//    signature is missing or not unique, or the pieces disagree, it hooks and calls nothing, logs why, and the wheel
//    always goes to the game.
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
#if defined(_MSC_VER)
#include <intrin.h>
#define RETURN_ADDRESS() _ReturnAddress()
#else
#define RETURN_ADDRESS() __builtin_return_address(0)
#endif
#include <cstdio>
#include <cstring>

namespace {


// --- Engine: found by signature (values in comments are build 93847's) ----------------------------------------------

// void TabBarWidget::SetActiveTab(this, int index, bool playSound) (0x878fb0): sets the tab (re-checking availability),
// plays the tab sound when asked, sends onSwitchTabMessage. Entry: MOV [RSP+0x18],RBX / MOV [RSP+0x10],EDX / PUSH RBP /
// PUSH RSI / PUSH RDI / SUB RSP,0x30 (16 bytes, hooked), then LEA RDI,[RCX+activeTab] and CMP EDX,[RCX+tabCount].
constexpr const char* SetActiveTabSig =
	"48 89 5C 24 18 89 54 24 10 55 56 57 48 83 EC 30 48 8D B9 ?? ?? ?? ?? 41 0F B6 F0 8B 2F 48 8B D9 "
	"83 FA FF 75 06 8B CA 89 17 EB ?? 85 D2 78 08 3B 91 ?? ?? ?? ??";
constexpr size_t SetActiveTabEntrySize = 16;
constexpr size_t SetActiveTabActiveField = 0x13;  // disp32 of LEA RDI,[RCX+activeTab] (0x16c4)
constexpr size_t SetActiveTabCountField = 0x31;   // disp32 of CMP EDX,[RCX+tabCount] (0x558)

// int TabBarWidget::FindAvailableTab(this, int index, bool forward) (0x877d20): `index` if available, else the nearest
// available tab in that direction (wrapping). Reads the availability bytes (0x1788) and tabCount (0x558).
constexpr const char* FindAvailableTabSig = "48 89 5C 24 08 4C 63 DA 48 8B D9 41 80 BC 0B ?? ?? ?? ?? 00 75 ?? 44 8B 91 ?? ?? ?? ??";
constexpr size_t FindAvailableTabCountField = 0x19;

// WaypointsPanel's show-Act routine (0x1ec820): marks each Act's tab available or not, clamps the Act to the tab count
// and calls SetActiveTab. The signature ends with that CALL; its return address (0x1ec8d3) identifies the waypoint tab
// bar, and its target must be the SetActiveTab found above.
constexpr const char* PanelCallSiteSig =
	"48 8B 8D ?? ?? ?? ?? 41 BF 01 00 00 00 41 8B FF 44 39 B9 ?? ?? ?? ?? 7E ?? 48 8D B5 ?? ?? ?? ?? 8B 1E 45 32 F6 "
	"E8 ?? ?? ?? ?? 45 33 C0 8B D3 48 8B C8 E8 ?? ?? ?? ?? 48 8B 8D ?? ?? ?? ?? 85 C0 45 0F B6 C6 8B D7 45 0F 45 C7 "
	"E8 ?? ?? ?? ?? 48 8B 8D ?? ?? ?? ?? 48 8D 76 04 FF C7 3B B9 ?? ?? ?? ?? 7C ?? 8B 81 ?? ?? ?? ?? FF C8 41 3B C5 "
	"44 0F 4C E8 45 33 C0 41 8B D5 E8 ?? ?? ?? ??";

using SetActiveTabFn = void (*)(void* tabBar, int32_t index, bool playSound) noexcept;
using FindAvailableTabFn = int32_t (*)(void* tabBar, int32_t index, bool forward) noexcept;

SetActiveTabFn g_setActiveTab = nullptr;  // trampoline to the original (set by the hook)
FindAvailableTabFn g_findAvailableTab = nullptr;
size_t g_tabCountOffset = 0;              // TabBarWidget fields, read from the code found
size_t g_activeTabOffset = 0;
bool g_engineOk = false;

// --- State -----------------------------------------------------------------------------------------------------------

const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

enum class Variant : int { None = 0, Expansion = 1, Original = 2 };

std::atomic<bool> g_active{false};    // installed and not unloading
std::atomic<bool> g_open{false};      // waypoint panel open (UI messages)
std::atomic<int> g_variant{0};        // Variant of the open panel
std::atomic<int> g_pendingSteps{0};   // Act steps from the wheel, applied by the next UI tick
std::atomic<bool> g_tickRunning{false};
bool g_tickStarted = false;          // the tick has run once (UI thread)

// The waypoint panel's tab bar, taken by the SetActiveTab hook (UI thread; the window procedure reads whether it is
// known). Used only while the panel is open; dropped when panels are unloaded.
std::atomic<void*> g_tabBar{nullptr};
uintptr_t g_panelCallSite = 0;        // return address of the panel's SetActiveTab call

// Window subclass. g_hwnd is set by the UI tick; the window procedure reads g_prevProc.
std::atomic<HWND> g_hwnd{nullptr};
std::atomic<WNDPROC> g_prevProc{nullptr};
bool g_pinned = false;
wp::WheelAccumulator g_wheel;  // window-procedure thread only

// Counters for the status command.
std::atomic<uint32_t> g_wheelSeen{0}, g_wheelUsed{0}, g_switches{0}, g_tabMessages{0};
std::atomic<uint32_t> g_opens{0};

auto VariantName(Variant v) noexcept -> const char* {
	return v == Variant::Original ? "WaypointsPanelOriginal" : v == Variant::Expansion ? "WaypointsPanelExpansion" : "none";
}

template <typename T>
auto Field(void* object, size_t offset) noexcept -> T& {
	return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// --- SetActiveTab hook: finds the waypoint panel's tab bar -------------------------------------------------------------

void SetActiveTabHook(void* tabBar, int32_t index, bool playSound) noexcept {
	const auto caller = reinterpret_cast<uintptr_t>(RETURN_ADDRESS());
	g_setActiveTab(tabBar, index, playSound);
	if (caller != g_panelCallSite || !g_active.load()) {
		return;
	}
	// The waypoint panel just showed an Act: this is its tab bar.
	const int count = Field<int32_t>(tabBar, g_tabCountOffset);
	if (count < 1 || count > wp::MaxActs) {
		plog::Warn("waypoint tab bar %p has %d tabs: not used", tabBar, count);
		return;
	}
	if (g_tabBar.exchange(tabBar) != tabBar) {
		plog::Info("waypoint tab bar %p (%d tabs, Act %d shown): scrolling on", tabBar, count, Field<int32_t>(tabBar, g_activeTabOffset) + 1);
	}
}

// --- Switching (UI tick) -----------------------------------------------------------------------------------------------

// One controller-style step on the panel's tab bar: what its input handler does for tabLeft / tabRight.
void StepTab(void* tabBar, bool forward) noexcept {
	const int32_t count = Field<int32_t>(tabBar, g_tabCountOffset);
	const int32_t current = Field<int32_t>(tabBar, g_activeTabOffset);
	if (count < 1 || count > wp::MaxActs || current < -1 || current >= count) {
		plog::Warn("tab bar %p looks wrong (%d tabs, active %d): scrolling off until the panel shows an Act again", tabBar, count, current);
		g_tabBar = nullptr;
		return;
	}
	int32_t index = 0;
	if (forward) {
		index = current >= count - 1 ? 0 : current + 1;
	} else {
		index = (current <= 0 ? count : current) - 1;
	}
	index = g_findAvailableTab(tabBar, index, forward);
	g_setActiveTab(tabBar, index, true);
	++g_switches;
}

void ApplyPendingSteps() noexcept {
	int steps = g_pendingSteps.exchange(0);
	void* const tabBar = g_tabBar.load();
	if (steps == 0 || !g_open.load() || tabBar == nullptr) {
		return;
	}
	const int count = Field<int32_t>(tabBar, g_tabCountOffset);
	if (count > 0) {
		steps %= count;  // a fast spin never goes round more than once
	}
	for (; steps > 0 && g_tabBar.load() != nullptr; --steps) {
		StepTab(tabBar, true);
	}
	for (; steps < 0 && g_tabBar.load() != nullptr; ++steps) {
		StepTab(tabBar, false);
	}
}

// --- Window procedure --------------------------------------------------------------------------------------------------

auto CALLBACK WheelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
	const WNDPROC prev = g_prevProc.load();
	if (g_active.load() && msg == WM_MOUSEWHEEL) {
		++g_wheelSeen;
		const bool open = g_open.load();
		const bool ours = open && g_engineOk && g_tabBar.load() != nullptr;
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
	plog::Info("wheel: subclassed window %p (class %s, %s)", static_cast<void*>(state.best), cls, unicode ? "unicode" : "ansi");
}

// --- UI tick -----------------------------------------------------------------------------------------------------------

void __cdecl Tick(const D2RL::PluginContext* context, void* userData) noexcept;

// Queues the next tick. False when the loader refuses; the tick then stays stopped until StartTick succeeds.
auto QueueTick() noexcept -> bool {
	const D2RL::Threads::Result r = g_threads->runOnUiThread(g_ctx, Tick, nullptr);
	if (r == D2RL::Threads::Result::Success) {
		return true;
	}
	g_tickRunning = false;
	if (r == D2RL::Threads::Result::OwnerInactive && !g_tickStarted) {
		return false;  // expected while the plugin is still loading: the next UI message retries
	}
	static std::atomic<uint32_t> s_refused{0};
	if (plog::Sample(s_refused)) {
		plog::Info("UI tick not queued (result %u, %u so far); retried on the next UI event", static_cast<unsigned>(r), s_refused.load());
	}
	return false;
}

// Starts the per-frame tick unless it runs already. D2RLoader refuses runOnUiThread while the plugin is still loading
// (OwnerInactive, seen with D2RLoader 1.3.1), so this is called from every later UI message until it succeeds.
void StartTick() noexcept {
	if (!g_active.load() || g_tickRunning.exchange(true)) {
		return;
	}
	if (QueueTick() && !g_tickStarted) {
		g_tickStarted = true;
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
	EnsureSubclass();
	ApplyPendingSteps();
	(void)QueueTick();  // runs again on the next UI update
}

// --- Listener ----------------------------------------------------------------------------------------------------------

void SetOpen(bool open, Variant variant) noexcept {
	if (open == g_open.load() && (!open || variant == static_cast<Variant>(g_variant.load()))) {
		return;
	}
	g_pendingSteps = 0;
	if (open) {
		g_variant = static_cast<int>(variant);
		++g_opens;
		g_open = true;
	} else {
		g_open = false;
	}
}

// Panels are being destroyed: the tab bar pointer must not outlive them.
void ForgetTabBar(const char* why) noexcept {
	if (g_tabBar.exchange(nullptr) != nullptr) {
		plog::Info("waypoint tab bar dropped (%s)", why);
	}
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

	if (std::strcmp(target, "PanelManager") == 0) {
		if (wp::Contains(text, "waypointspanel")) {
			const Variant variant = wp::Contains(text, "original") ? Variant::Original : Variant::Expansion;
			if (std::strcmp(command, "OpenPanel") == 0 || std::strcmp(command, "OpenExclusivePanel") == 0) {
				SetOpen(true, variant);
			} else if (std::strcmp(command, "ClosePanel") == 0) {
				SetOpen(false, variant);
			} else if (std::strcmp(command, "UnloadPanel") == 0) {
				SetOpen(false, variant);
				ForgetTabBar("panel unloaded");
			}
		} else if (std::strcmp(command, "UnloadAll") == 0) {
			SetOpen(false, Variant::None);
			ForgetTabBar("all panels unloaded");
		}
	} else if (std::strcmp(target, "FrontEndNavigation") == 0) {
		SetOpen(false, Variant::None);
		ForgetTabBar("left the game");
	} else if (std::strcmp(target, "Waypoints") == 0 && std::strcmp(command, "SelectTab") == 0) {
		++g_tabMessages;
	}
	return Continue;
}

template <typename Service>
auto Query(const D2RL::PluginContext* context, const Service** service, bool (*has)(const Service*, uint32_t), uint32_t size) noexcept -> bool {
	return context->QueryService(service) == D2RL::ServiceQueryResult::Success && has(*service, size);
}

// Executable sections of the game image, scanned for the signatures.
struct CodeRange {
	const uint8_t* start;
	size_t size;
};

auto CodeRanges(uintptr_t exeBase, CodeRange* out, int max) noexcept -> int {
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exeBase);
	if (exeBase == 0 || dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return 0;
	}
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(exeBase + static_cast<uintptr_t>(dos->e_lfanew));
	if (nt->Signature != IMAGE_NT_SIGNATURE) {
		return 0;
	}
	const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
	int n = 0;
	for (WORD i = 0; i < nt->FileHeader.NumberOfSections && n < max; ++i, ++section) {
		if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0 && section->Misc.VirtualSize > 0) {
			out[n++] = {reinterpret_cast<const uint8_t*>(exeBase + section->VirtualAddress), section->Misc.VirtualSize};
		}
	}
	return n;
}

// The one place `sig` matches in the game's code, or nullptr (and why) when it matches nowhere or more than once.
auto FindUnique(const CodeRange* ranges, int count, const char* name, const char* sig, char* why, size_t size) noexcept -> const uint8_t* {
	wp::Pattern pattern{};
	if (!wp::ParsePattern(sig, &pattern)) {
		std::snprintf(why, size, "bad signature for %s", name);
		return nullptr;
	}
	const uint8_t* found = nullptr;
	int matches = 0;
	for (int i = 0; i < count && matches < 2; ++i) {
		size_t at = 0;
		const int n = wp::FindPattern(ranges[i].start, ranges[i].size, pattern, &at);
		if (n > 0 && matches == 0) {
			found = ranges[i].start + at;
		}
		matches += n;
	}
	if (matches != 1) {
		std::snprintf(why, size, "%s %s in this game version's code", name, matches == 0 ? "not found" : "found more than once");
		return nullptr;
	}
	return found;
}

// Finds the three places by signature, checks they agree, hooks SetActiveTab. False (and why) otherwise; then nothing
// is hooked or called.
auto InstallEngine(const D2RL::PluginContext* context, char* why, size_t size) noexcept -> bool {
	CodeRange ranges[8];
	const int count = CodeRanges(context->exeBase, ranges, 8);
	if (count == 0) {
		std::snprintf(why, size, "no code sections in the game image");
		return false;
	}
	const uint8_t* set = FindUnique(ranges, count, "TabBarWidget::SetActiveTab", SetActiveTabSig, why, size);
	const uint8_t* find = set != nullptr ? FindUnique(ranges, count, "TabBarWidget::FindAvailableTab", FindAvailableTabSig, why, size) : nullptr;
	const uint8_t* site = find != nullptr ? FindUnique(ranges, count, "the waypoint panel's tab call", PanelCallSiteSig, why, size) : nullptr;
	if (site == nullptr) {
		return false;
	}
	wp::Pattern sitePattern{};
	(void)wp::ParsePattern(PanelCallSiteSig, &sitePattern);
	const uint8_t* const returnAddress = site + sitePattern.size;  // the signature ends with CALL rel32
	const uint8_t* const callTarget = returnAddress + wp::ReadI32(returnAddress - 4);
	const uint32_t countOffset = wp::ReadU32(set + SetActiveTabCountField);
	const uint32_t activeOffset = wp::ReadU32(set + SetActiveTabActiveField);
	if (callTarget != set) {
		std::snprintf(why, size, "the waypoint panel's tab call does not go to SetActiveTab");
		return false;
	}
	if (wp::ReadU32(find + FindAvailableTabCountField) != countOffset || countOffset == 0 || countOffset >= 0x10000 ||
	    activeOffset == 0 || activeOffset >= 0x10000 || activeOffset == countOffset) {
		std::snprintf(why, size, "tab bar field offsets disagree (tabCount 0x%x, active 0x%x)", countOffset, activeOffset);
		return false;
	}
	const uint64_t setRva = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(set) - context->exeBase);
	g_tabCountOffset = countOffset;
	g_activeTabOffset = activeOffset;
	g_findAvailableTab = reinterpret_cast<FindAvailableTabFn>(const_cast<uint8_t*>(find));
	g_panelCallSite = reinterpret_cast<uintptr_t>(returnAddress);
	// The entry bytes (the signature's first 16) are copied: D2RLoader checks them again before patching.
	uint8_t entry[SetActiveTabEntrySize];
	std::memcpy(entry, set, sizeof(entry));
	if (!context->InstallInlineHook(setRva, entry, sizeof(entry), reinterpret_cast<void*>(&SetActiveTabHook),
	                                reinterpret_cast<void**>(&g_setActiveTab)) ||
	    g_setActiveTab == nullptr) {
		std::snprintf(why, size, "D2RLoader refused the hook on SetActiveTab (0x%llx)", static_cast<unsigned long long>(setRva));
		return false;
	}
	plog::Info("found SetActiveTab 0x%llx, FindAvailableTab 0x%llx, panel call 0x%llx (tabCount +0x%x, active tab +0x%x)",
	           static_cast<unsigned long long>(setRva), static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(find) - context->exeBase),
	           static_cast<unsigned long long>(g_panelCallSite - context->exeBase), countOffset, activeOffset);
	return true;
}

}  // namespace

auto InstallScrollableWaypoints(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const D2RL::SharedEventService* events = nullptr;
	if (!Query(context, &g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: D2RLoader has no thread service");
		return false;
	}
	if (!Query(context, &events, D2RL::HasSharedEventServiceField, D2RL::SharedEventServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: D2RLoader has no shared-event service");
		return false;
	}
	g_ctx = context;
	char why[200] = "";
	g_engineOk = InstallEngine(context, why, sizeof(why));
	if (!g_engineOk) {
		// Nothing native is touched; the plugin only watches, and the wheel always goes to the game.
		plog::Warn("Act switching unavailable: %s", why);
	}

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

	g_active = true;
	StartTick();  // refused while loading; the listener starts it on the first UI message
	std::snprintf(report, reportSize, g_engineOk ? "on: the wheel switches Acts while the waypoint panel is open"
	                                             : "loaded, but Act switching is unavailable in this game version (see the warning above)");
	return true;
}

void ScrollableWaypointsStatus(char* out, size_t size) noexcept {
	void* const tabBar = g_tabBar.load();
	std::snprintf(out, size,
	              "%s | window %s | panel %s %s (opened %u) | tab bar %s | wheel %u seen, %u used | switches %u | tab messages %u",
	              g_active.load() ? (g_engineOk ? (g_tickRunning.load() ? "on" : "on, tick waiting for a UI event") : "Act switching unavailable") : "off",
	              g_hwnd.load() != nullptr ? "hooked" : "not found yet", VariantName(static_cast<Variant>(g_variant.load())),
	              g_open.load() ? "open" : "closed", g_opens.load(), tabBar != nullptr ? "known" : "unknown", g_wheelSeen.load(),
	              g_wheelUsed.load(), g_switches.load(), g_tabMessages.load());
}

void UninstallScrollableWaypoints() noexcept {
	g_active = false;
	g_open = false;
	g_tabBar = nullptr;
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
