// Mouse wheel over D2R's waypoint panel switches the displayed Act. See wp_scroll.cpp.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Queries the services, registers the listeners and starts the per-frame UI tick. False (with `report` saying why)
// when a service is missing; then nothing is installed and the wheel is never touched.
auto InstallScrollableWaypoints(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;

// Live state for the console command and the log.
void ScrollableWaypointsStatus(char* out, size_t size) noexcept;

// Stops acting on the wheel and removes the window subclass when nothing was chained after it.
void UninstallScrollableWaypoints() noexcept;
