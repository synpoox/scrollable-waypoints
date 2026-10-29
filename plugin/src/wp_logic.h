// Pure logic of scrollable-waypoints: no Windows, no SDK, so tests/wp_logic_test.cpp runs it on any machine.
//
//  - Wheel notches: WM_MOUSEWHEEL deltas (120 per notch; precision wheels send smaller pieces) -> Act steps.
//  - Byte signatures: finding game functions by their code ("48 8B ?? ..."), so the plugin is not tied to one build.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

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

// --- Byte signatures ---------------------------------------------------------------------------------------------------

constexpr size_t MaxPattern = 192;

// "48 8B ?? 05": hex bytes and ?? wildcards separated by spaces.
struct Pattern {
	uint8_t bytes[MaxPattern];
	bool wild[MaxPattern];
	size_t size;
};

inline auto HexDigit(char c) noexcept -> int {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// False on a malformed signature, an empty one, one longer than MaxPattern or one starting with a wildcard.
inline auto ParsePattern(const char* text, Pattern* out) noexcept -> bool {
	out->size = 0;
	for (const char* p = text; *p != '\0';) {
		if (*p == ' ') {
			++p;
			continue;
		}
		if (out->size == MaxPattern || p[1] == '\0' || (p[2] != ' ' && p[2] != '\0')) {
			return false;
		}
		if (p[0] == '?' && p[1] == '?') {
			out->wild[out->size] = true;
			out->bytes[out->size] = 0;
		} else {
			const int hi = HexDigit(p[0]), lo = HexDigit(p[1]);
			if (hi < 0 || lo < 0) {
				return false;
			}
			out->wild[out->size] = false;
			out->bytes[out->size] = static_cast<uint8_t>(hi * 16 + lo);
		}
		++out->size;
		p += 2;
	}
	return out->size > 0 && !out->wild[0];
}

// Matches of `pat` in data[0, size): returns how many (counting stops at 2) and the first one's offset in *first.
inline auto FindPattern(const uint8_t* data, size_t size, const Pattern& pat, size_t* first) noexcept -> int {
	int count = 0;
	if (pat.size == 0 || size < pat.size) {
		return 0;
	}
	const uint8_t lead = pat.bytes[0];
	const uint8_t* const end = data + size - pat.size + 1;
	for (const uint8_t* p = data; p < end;) {
		p = static_cast<const uint8_t*>(std::memchr(p, lead, static_cast<size_t>(end - p)));
		if (p == nullptr) {
			break;
		}
		size_t i = 1;
		while (i < pat.size && (pat.wild[i] || p[i] == pat.bytes[i])) {
			++i;
		}
		if (i == pat.size) {
			if (count == 0) {
				*first = static_cast<size_t>(p - data);
			}
			if (++count == 2) {
				break;
			}
		}
		++p;
	}
	return count;
}

inline auto ReadU32(const uint8_t* p) noexcept -> uint32_t {
	uint32_t v = 0;
	std::memcpy(&v, p, sizeof(v));
	return v;
}

inline auto ReadI32(const uint8_t* p) noexcept -> int32_t {
	int32_t v = 0;
	std::memcpy(&v, p, sizeof(v));
	return v;
}

}  // namespace wp
