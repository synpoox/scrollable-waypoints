// Host-side tests of wp_logic.h (no Windows needed):  g++ -std=c++20 -I../src wp_logic_test.cpp && ./a.out
#include "wp_logic.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

static int g_failed = 0;

#define CHECK(expr)                                                       \
	do {                                                                  \
		if (!(expr)) {                                                    \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);   \
			++g_failed;                                                   \
		}                                                                 \
	} while (0)

static bool Near(double a, double b) {
	return std::fabs(a - b) < 0.01;
}

int main() {
	// Wrap: I->II->III->IV->V->I and back; Classic panels wrap over 4.
	CHECK(wp::WrapAct(0, 1) == 1);
	CHECK(wp::WrapAct(4, 1) == 0);
	CHECK(wp::WrapAct(0, -1) == 4);
	CHECK(wp::WrapAct(2, 7) == 4);
	CHECK(wp::WrapAct(2, -8) == 4);
	CHECK(wp::WrapAct(3, 1, 4) == 0);
	CHECK(wp::WrapAct(0, -1, 4) == 3);
	int act = 0;
	for (int i = 0; i < 5; ++i) act = wp::WrapAct(act, 1);
	CHECK(act == 0);

	// Wheel: down (negative) = next Act, up = previous; partial notches wait.
	wp::WheelAccumulator wheel;
	CHECK(wheel.Feed(-120) == 1);
	CHECK(wheel.Feed(120) == -1);
	CHECK(wheel.Feed(-240) == 2);
	CHECK(wheel.Feed(-60) == 0);
	CHECK(wheel.Feed(-60) == 1);
	CHECK(wheel.Feed(40) == 0);
	CHECK(wheel.Feed(40) == 0);
	CHECK(wheel.Feed(40) == -1);
	wheel.Feed(-100);
	wheel.Reset();
	CHECK(wheel.Feed(-20) == 0);

	CHECK(wp::Contains("WaypointsPanelExpansion", "waypointspanel"));
	CHECK(!wp::Contains("SkillsTreePanel", "waypointspanel"));
	CHECK(!wp::Contains(nullptr, "x"));

	// Geometry at 4K 16:9: layout units are pixels, panel at x 236, y 0.397*2160-651.
	const wp::Mapping k4 = wp::MapPanel(wp::Canvas::FullWidth, 3840, 2160);
	CHECK(Near(k4.scale, 1.0));
	CHECK(Near(k4.panelX, 236.0));
	CHECK(Near(k4.panelY, 0.397 * 2160 - 651));
	const wp::Box tab0 = wp::TabBox(k4, wp::ExpansionTabs, 0);
	CHECK(Near(tab0.left, 236 + 148) && Near(tab0.right, 236 + 148 + 174));
	const wp::Box tab1 = wp::TabBox(k4, wp::ExpansionTabs, 1);
	CHECK(Near(tab1.left, 236 + 148 + 173));  // padding -1: tabs overlap by one unit
	const wp::Box bar = wp::BarBox(k4, wp::ExpansionTabs);
	CHECK(Near(bar.right - bar.left, 5 * 174 - 4));
	// The panel lies inside the canvas.
	const wp::Box panel = wp::PanelBox(k4);
	CHECK(panel.top > 0 && panel.bottom < 2160);

	// 1080p is 4K halved; both canvases agree at 16:9.
	const wp::Mapping hd = wp::MapPanel(wp::Canvas::FullWidth, 1920, 1080);
	const wp::Mapping hdc = wp::MapPanel(wp::Canvas::Centered16x9, 1920, 1080);
	CHECK(Near(hd.scale, 0.5) && Near(hd.panelX, 118) && Near(hdc.panelX, hd.panelX));

	// Ultrawide 3440x1440: the centred canvas is 2560 wide, 440 px in.
	const wp::Mapping uwFull = wp::MapPanel(wp::Canvas::FullWidth, 3440, 1440);
	const wp::Mapping uwMid = wp::MapPanel(wp::Canvas::Centered16x9, 3440, 1440);
	CHECK(Near(uwMid.panelX - uwFull.panelX, 440));

	// Tab centres hit their own tab, inside the bar, every layout and mapping.
	for (const wp::TabBar* tabs : {&wp::ExpansionTabs, &wp::OriginalTabs}) {
		for (const wp::Mapping* m : {&k4, &hd, &uwFull, &uwMid}) {
			for (int i = 0; i < tabs->count; ++i) {
				double x = 0, y = 0;
				wp::TabCenter(*m, *tabs, i, &x, &y);
				CHECK(wp::TabAt(*m, *tabs, x, y) == i);
				CHECK(wp::BarBox(*m, *tabs).Contains(x, y));
				// The click is at least 28 layout units above the destination buttons (layout y 264).
				CHECK(m->panelY + 264 * m->scale - y >= 28 * m->scale - 0.01);
			}
		}
	}
	CHECK(wp::TabAt(k4, wp::ExpansionTabs, 10, 10) == -1);
	CHECK(wp::OriginalTabs.count == 4);

	std::printf(g_failed == 0 ? "all tests passed\n" : "%d failed\n", g_failed);
	return g_failed == 0 ? 0 : 1;
}
