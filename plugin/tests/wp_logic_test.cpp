// Host-side tests of wp_logic.h (no Windows needed):  g++ -std=c++20 -I../src wp_logic_test.cpp && ./a.out
#include "wp_logic.h"

#include <cstdio>
#include <cstring>

static int g_failed = 0;

#define CHECK(expr)                                                       \
	do {                                                                  \
		if (!(expr)) {                                                    \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);   \
			++g_failed;                                                   \
		}                                                                 \
	} while (0)

int main() {
	// Wrap: I->II->III->IV->V->I and back.
	CHECK(wp::WrapAct(0, 1) == 1);
	CHECK(wp::WrapAct(3, 1) == 4);
	CHECK(wp::WrapAct(4, 1) == 0);
	CHECK(wp::WrapAct(0, -1) == 4);
	CHECK(wp::WrapAct(4, -1) == 3);
	CHECK(wp::WrapAct(2, 7) == 4);
	CHECK(wp::WrapAct(2, -8) == 4);
	int act = 0;
	for (int i = 0; i < 5; ++i) act = wp::WrapAct(act, 1);
	CHECK(act == 0);
	for (int i = 0; i < 5; ++i) act = wp::WrapAct(act, -1);
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
	CHECK(wheel.rest == 0);
	wheel.Feed(-100);
	wheel.Reset();
	CHECK(wheel.Feed(-20) == 0);

	// Message recognition.
	CHECK(wp::WaypointTabIndex("WaypointsPanelMessage", "ActivateTab", "2") == 2);
	CHECK(wp::WaypointTabIndex("waypointspanel", "SwitchTab", "0") == 0);
	CHECK(wp::WaypointTabIndex("WaypointsPanelMessage", "ActivateTab", "5") == 5);  // 1-based panels
	CHECK(wp::WaypointTabIndex("WaypointsPanelMessage", "ActivateTab", "6") == -1);
	CHECK(wp::WaypointTabIndex("WaypointsPanelMessage", "ActivateTab", "") == -1);
	CHECK(wp::WaypointTabIndex("WaypointsPanelMessage", "ActivateTab", nullptr) == -1);
	CHECK(wp::WaypointTabIndex("WaypointsPanelMessage", "ActivateTab", "-1") == -1);
	CHECK(wp::WaypointTabIndex("SkillsTreePanelMessage", "ActivateTab", "1") == -1);   // other panel
	CHECK(wp::WaypointTabIndex("WaypointsPanelMessage", "SelectWaypoint", "3") == -1);  // destination, never a tab
	CHECK(wp::WaypointTabIndex("WaypointsPanelMessage", "Travel", "1") == -1);
	CHECK(wp::WaypointTabIndex(nullptr, nullptr, "1") == -1);

	CHECK(wp::ParseIndex("07") == 7);
	CHECK(wp::ParseIndex("123") == -1);
	CHECK(wp::ParseIndex("1a") == -1);

	char text[4];
	wp::TabText(0, 0, text, sizeof(text));
	CHECK(std::strcmp(text, "0") == 0);
	wp::TabText(4, 1, text, sizeof(text));
	CHECK(std::strcmp(text, "5") == 0);

	std::printf(g_failed == 0 ? "all tests passed\n" : "%d failed\n", g_failed);
	return g_failed == 0 ? 0 : 1;
}
