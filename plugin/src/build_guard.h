// Build guard (from d2rAI-toolkit's plugin template): which game image is running, and is it the one this plugin was
// verified on? The test is the image itself, not a version label: the PE header TimeDateStamp of the running D2R image
// (context->exeBase). Steam and Battle.net ship the same 3.3 program under different build numbers (94995 / 93847,
// both stamp 0x6ab3782c under D2RLoader 1.3.1), so a build-number check would wrongly refuse one of them.
//
// This plugin reports, it does not gate: it uses no fixed addresses (wp_scroll.cpp finds the code by signature and
// checks it), so on another image it still tries, and the log and the status command say the build is untested. A
// crash report then shows at once whether it ran on the verified image.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace build_guard {

constexpr uint32_t ExpectedImage = 0x6ab3782cU;  // verified: D2R 3.3.0 under D2RLoader 1.3.1 (Battle.net 93847 / Steam 94995)

// TimeDateStamp of the PE image at `exeBase` (0 when there is none).
inline auto ImageStamp(uintptr_t exeBase) noexcept -> uint32_t {
	if (exeBase == 0) {
		return 0;
	}
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exeBase);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return 0;
	}
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(exeBase + static_cast<uintptr_t>(dos->e_lfanew));
	return nt->Signature == IMAGE_NT_SIGNATURE ? nt->FileHeader.TimeDateStamp : 0;
}

constexpr auto Matches(uint32_t stamp) noexcept -> bool {
	return stamp == ExpectedImage;
}

// The status / log line: "image 0x6ab3782c (verified build)" or "... (untested build: game code found by signature)".
inline void Describe(char* out, size_t size, uint32_t stamp, const char* buildVersion) noexcept {
	std::snprintf(out, size, "D2R %s, image 0x%08x (%s)", buildVersion != nullptr ? buildVersion : "?", stamp,
	              Matches(stamp) ? "verified build" : "untested build: verified on 0x6ab3782c, game code found by signature");
}

}  // namespace build_guard
