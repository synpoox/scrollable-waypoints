// scrollable-waypoints: D2RLoader plugin entry point.
//
// Target: D2R 3.3 under D2RLoader 1.3.1, PluginSDK 0.3.0. Client-only (UI behaviour, nothing shared with other
// players), global (installed in <game>/d2rloader/plugins/, runs with or without a mod; not ModScopedOnly, which makes
// D2RLoader refuse it in the global folder). It patches no game code except one checked hook on the tab bar's
// SetActiveTab (build 93847 only; on any other image it stays off and only watches). See wp_scroll.cpp.
//
// Console command "scrollable-waypoints" prints the live state (window, panel, tab bar, wheel and switch counters).
#include <D2RLPlugin/api.h>

#include "log.h"
#include "wp_scroll.h"

#include <cstdio>

namespace {

constexpr const char* PluginName = "scrollable-waypoints";
constexpr const char* PluginVersion = "1.0.0";  // PluginInfo::version too; CHANGELOG.md

const D2RL::PluginContext* g_context = nullptr;

constexpr D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "scrollable-waypoints",
	.name        = "Scrollable Waypoints",
	.version     = PluginVersion,
	.author      = "Synpoo",
	.description = "Mouse wheel switches the waypoint panel's Act tabs.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

auto StatusCommand(D2R::Game::Client* client, const D2RL::ConsoleCommandContext* command, void* userData) noexcept
	-> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;
	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	char state[700];
	ScrollableWaypointsStatus(state, sizeof(state));
	char line[800];
	std::snprintf(line, sizeof(line), "%s %s | %s", PluginName, PluginVersion, state);
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);  // also in the plugin log, for reports
	return D2RL::ConsoleCommandResult::Handled;
}

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	g_context = context;
	plog::Init(context, PluginName);
	plog::Info("%s loaded (mod %s, D2R %s)", PluginVersion, context->activeMod != nullptr ? context->activeMod : "?",
	           context->buildVersion != nullptr ? context->buildVersion : "?");
	char report[300];
	(void)InstallScrollableWaypoints(context, report, sizeof(report));
	plog::Info("wpscroll %s", report);
	// Stay loaded even when the part is off, so the status command can say why.
	return context->RegisterConsoleCommand(PluginName, StatusCommand, "Status of Scrollable Waypoints.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	UninstallScrollableWaypoints();
	g_context = nullptr;
}
