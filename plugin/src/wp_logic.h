// Pure logic of scrollable-waypoints: no Windows, no SDK, so tests/wp_logic_test.cpp runs it on any machine.
//
//  - Wheel notches: WM_MOUSEWHEEL deltas (120 per notch; precision wheels send smaller pieces) -> Act steps.
#pragma once

#include <cstddef>

namespace wp {

constexpr int MaxActs = 5;  // tabs on the waypoint panel (4 on a Classic character)
constexpr int WheelNotch = 120;  // WHEEL_DELTA

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

}  // namespace wp
