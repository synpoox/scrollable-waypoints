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

	// Byte signatures.
	wp::Pattern pat{};
	CHECK(wp::ParsePattern("48 8B ?? 05", &pat) && pat.size == 4 && pat.wild[2] && pat.bytes[1] == 0x8b);
	CHECK(!wp::ParsePattern("?? 8B", &pat));   // leading wildcard
	CHECK(!wp::ParsePattern("4 8B", &pat));    // half byte
	CHECK(!wp::ParsePattern("48 8G", &pat));   // not hex
	CHECK(!wp::ParsePattern("", &pat));
	const uint8_t code[] = {0x90, 0x48, 0x8b, 0x11, 0x05, 0x48, 0x8b, 0x22, 0x06, 0x48, 0x8b, 0x33, 0x05};
	size_t at = 0;
	CHECK(wp::ParsePattern("48 8B ?? 05", &pat));
	CHECK(wp::FindPattern(code, sizeof(code), pat, &at) == 2 && at == 1);   // two matches: not unique
	CHECK(wp::ParsePattern("48 8B 22 ??", &pat));
	CHECK(wp::FindPattern(code, sizeof(code), pat, &at) == 1 && at == 5);
	CHECK(wp::ParsePattern("48 8B 33 05 00", &pat));
	CHECK(wp::FindPattern(code, sizeof(code), pat, &at) == 0);              // runs past the end
	const uint8_t le[] = {0xc4, 0x16, 0x00, 0x00, 0xfc, 0xff, 0xff, 0xff};
	CHECK(wp::ReadU32(le) == 0x16c4 && wp::ReadI32(le + 4) == -4);

	std::printf(g_failed == 0 ? "all tests passed\n" : "%d failed\n", g_failed);
	return g_failed == 0 ? 0 : 1;
}
