// Host-side tests of wp_logic.h (no Windows needed):  g++ -std=c++20 -I../src wp_logic_test.cpp && ./a.out
#include "wp_logic.h"

#include <cstdio>

static int g_failed = 0;

#define CHECK(expr)                                                       \
	do {                                                                  \
		if (!(expr)) {                                                    \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);   \
			++g_failed;                                                   \
		}                                                                 \
	} while (0)

int main() {
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

	std::printf(g_failed == 0 ? "all tests passed\n" : "%d failed\n", g_failed);
	return g_failed == 0 ? 0 : 1;
}
