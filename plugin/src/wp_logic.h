// Pure logic of scrollable-waypoints: no Windows, no SDK, so tests/wp_logic_test.cpp runs it on any machine.
//
//  - Act wrap: I..V as 0..4, wheel down = next, wheel up = previous, both ends wrap.
//  - Wheel notches: WM_MOUSEWHEEL deltas (120 per notch; precision wheels send smaller pieces) -> Act steps.
//  - UI messages: which native messages are the waypoint panel's Act-tab messages. Only a command that contains
//    "tab" is ever recognised (and so ever sent): a waypoint destination click must never be mistaken for a tab.
#pragma once

#include <cstddef>

namespace wp {

constexpr int ActCount = 5;
constexpr int WheelNotch = 120;  // WHEEL_DELTA

// Act `act` (0-based) moved by `steps` (+ next, - previous), wrapping at both ends.
constexpr auto WrapAct(int act, int steps) noexcept -> int {
	const int n = (act + steps) % ActCount;
	return n < 0 ? n + ActCount : n;
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

// "0".."99" -> the number; anything else (empty, sign, letters, 3+ digits) -> -1.
inline auto ParseIndex(const char* text) noexcept -> int {
	if (text == nullptr || text[0] < '0' || text[0] > '9') {
		return -1;
	}
	int value = text[0] - '0';
	if (text[1] == '\0') {
		return value;
	}
	if (text[1] < '0' || text[1] > '9' || text[2] != '\0') {
		return -1;
	}
	return value * 10 + (text[1] - '0');
}

// A message is the waypoint panel's Act-tab message when it names the waypoint panel (target or command), its
// command is a tab command, and its text is a tab number (0-4, or 1-5 if the panel counts from one).
// Returns the raw tab number, or -1.
inline auto WaypointTabIndex(const char* target, const char* command, const char* text) noexcept -> int {
	if (!(Contains(target, "waypoint") || Contains(command, "waypoint")) || !Contains(command, "tab")) {
		return -1;
	}
	const int n = ParseIndex(text);
	return n >= 0 && n <= ActCount ? n : -1;
}

// Text for "show Act `act`" (0-based) when the panel numbers its tabs from `base` (0 or 1).
inline void TabText(int act, int base, char* out, size_t size) noexcept {
	if (size < 3) {
		if (size > 0) {
			out[0] = '\0';
		}
		return;
	}
	const int n = act + base;
	if (n >= 10) {
		out[0] = static_cast<char>('0' + n / 10);
		out[1] = static_cast<char>('0' + n % 10);
		out[2] = '\0';
	} else {
		out[0] = static_cast<char>('0' + n);
		out[1] = '\0';
	}
}

}  // namespace wp
