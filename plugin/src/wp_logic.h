// Pure logic of scrollable-waypoints: no Windows, no SDK, so tests/wp_logic_test.cpp runs it on any machine.
//
//  - Act wrap: wheel down = next Act, wheel up = previous, both ends wrap (5 tabs, or 4 on a Classic character).
//  - Wheel notches: WM_MOUSEWHEEL deltas (120 per notch; precision wheels send smaller pieces) -> Act steps.
//  - Panel geometry: where the waypoint panel and its Act tabs are on screen.
//
// Why geometry: D2R's Act tabs are one TabBarWidget ("Tabs"). A click selects the tab inside the widget, which then
// sends "Waypoints:SelectTab" with no tab number, so there is no UI message that says "show Act N". The plugin
// clicks the tab instead, and only after a real tab click by the player has confirmed where the tabs are.
//
// Layout values: waypointspaneloriginalhd.json / waypointspanelexpansionhd.json and _profilehd.json (vanilla 3.3 and
// Reimagined use the same ones): panel "$LeftPanelRect" { 236, -651, 1162 x 1507 } at "$LeftPanelAnchor" { 0, 0.397 },
// "Tabs" at { 148, 201 }, tabs 70 high; Expansion 5 tabs 174 wide, padding -1; Original 4 tabs 215 wide, padding 0.
// Layout units are 4K pixels: the UI is drawn on a canvas 2160 units high.
#pragma once

#include <cstddef>

namespace wp {

constexpr int MaxActs = 5;
constexpr int WheelNotch = 120;  // WHEEL_DELTA

// Act `act` (0-based) moved by `steps` (+ next, - previous), wrapping at both ends of `count` Acts.
constexpr auto WrapAct(int act, int steps, int count = MaxActs) noexcept -> int {
	const int n = (act + steps) % count;
	return n < 0 ? n + count : n;
}

// Collects wheel deltas into whole notches. Feed() returns Act steps: wheel down (negative delta) = +1 per notch.
struct WheelAccumulator {
	int rest = 0;

	auto Feed(int delta) noexcept -> int {
		rest += delta;
		const int notches = rest / WheelNotch;  // truncates toward zero: a half notch either way waits
		rest -= notches * WheelNotch;
		return -notches;
	}
	void Reset() noexcept { rest = 0; }
};

inline auto Lower(char c) noexcept -> char {
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive substring test; `needle` is lower case. Null `hay` never matches.
inline auto Contains(const char* hay, const char* needle) noexcept -> bool {
	if (hay == nullptr || needle == nullptr) {
		return false;
	}
	for (; *hay != '\0'; ++hay) {
		size_t i = 0;
		while (needle[i] != '\0' && hay[i] != '\0' && Lower(hay[i]) == needle[i]) {
			++i;
		}
		if (needle[i] == '\0') {
			return true;
		}
	}
	return false;
}

// --- Geometry --------------------------------------------------------------------------------------------------------

struct TabBar {
	int count;    // tabs
	int width;    // one tab, layout units
	int padding;  // between tabs, layout units
};
constexpr TabBar ExpansionTabs{5, 174, -1};  // WaypointsPanelExpansion
constexpr TabBar OriginalTabs{4, 215, 0};    // WaypointsPanelOriginal (Classic characters)

constexpr double CanvasHeight = 2160.0;
constexpr double PanelAnchorY = 0.397;
constexpr int PanelX = 236, PanelY = -651, PanelW = 1162, PanelH = 1507;
constexpr int TabsX = 148, TabsY = 201, TabH = 70;

// How the UI canvas sits in the window. Not known for every aspect ratio, so the plugin tries each and keeps the one
// a real tab click confirms.
enum class Canvas : int {
	FullWidth = 0,     // anchors relative to the whole client area
	Centered16x9 = 1,  // anchors relative to a centred 16:9 area (wider windows)
	Count = 2,
};

struct Box {
	double left, top, right, bottom;

	constexpr auto Contains(double x, double y) const noexcept -> bool {
		return x >= left && x < right && y >= top && y < bottom;
	}
};

struct Mapping {
	double scale;           // client pixels per layout unit
	double panelX, panelY;  // panel's top-left corner, client pixels
};

constexpr auto MapPanel(Canvas canvas, int clientW, int clientH) noexcept -> Mapping {
	const double scale = clientH / CanvasHeight;
	double canvasW = clientW;
	if (canvas == Canvas::Centered16x9 && clientH * 16.0 / 9.0 < clientW) {
		canvasW = clientH * 16.0 / 9.0;
	}
	const double canvasX = (clientW - canvasW) / 2.0;
	return {scale, canvasX + PanelX * scale, PanelAnchorY * clientH + PanelY * scale};
}

constexpr auto PanelBox(const Mapping& m) noexcept -> Box {
	return {m.panelX, m.panelY, m.panelX + PanelW * m.scale, m.panelY + PanelH * m.scale};
}

constexpr auto TabBox(const Mapping& m, const TabBar& bar, int tab) noexcept -> Box {
	const double left = m.panelX + (TabsX + tab * (bar.width + bar.padding)) * m.scale;
	const double top = m.panelY + TabsY * m.scale;
	return {left, top, left + bar.width * m.scale, top + TabH * m.scale};
}

constexpr auto BarBox(const Mapping& m, const TabBar& bar) noexcept -> Box {
	const Box first = TabBox(m, bar, 0);
	const Box last = TabBox(m, bar, bar.count - 1);
	return {first.left, first.top, last.right, last.bottom};
}

// Middle of tab `tab`: where the plugin clicks (furthest from the destination buttons right below the tabs).
constexpr void TabCenter(const Mapping& m, const TabBar& bar, int tab, double* x, double* y) noexcept {
	const Box b = TabBox(m, bar, tab);
	*x = (b.left + b.right) / 2.0;
	*y = (b.top + b.bottom) / 2.0;
}

// Tab under client point (x, y), or -1.
constexpr auto TabAt(const Mapping& m, const TabBar& bar, double x, double y) noexcept -> int {
	for (int i = 0; i < bar.count; ++i) {
		if (TabBox(m, bar, i).Contains(x, y)) {
			return i;
		}
	}
	return -1;
}

}  // namespace wp
