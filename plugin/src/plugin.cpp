// scrollable-waypoints: D2RLoader plugin entry point.
//
// Target: D2R 3.3 under D2RLoader 1.3.1, PluginSDK 0.3.0. Client-only (UI behaviour, nothing shared with other
// players), global (installed in <game>/d2rloader/plugins/, runs with or without a mod; not ModScopedOnly, which makes
// D2RLoader refuse it in the global folder). It patches no game code except one hook on the tab bar's SetActiveTab,
// found by byte signature on any game version; if the code isn't found it hooks nothing and only watches.
// See wp_scroll.cpp.
//
// Guards (from d2rAI-toolkit's template; they change nothing in the game, they say what runs):
//  - build_guard.h: the game image, and whether it is the verified one (reported only: see build_guard.h);
//  - env_guard.h: a second loaded copy of this plugin, and the other plugins, at load and again 15 s later.
//
// Console command "scrollable-waypoints" prints the live state (image, guards, window, panel, tab bar, counters).
#include <D2RLPlugin/api.h>

#include "build_guard.h"
#include "env_guard.h"
#include "log.h"
#include "wp_scroll.h"

#include <cstdio>

namespace {

constexpr const char* PluginName = "scrollable-waypoints";
constexpr const char* PluginVersion = "1.0.0";  // PluginInfo::version too; CHANGELOG.md

const D2RL::PluginContext* g_context = nullptr;
uint32_t g_stamp = 0;

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
	char image[200];
	build_guard::Describe(image, sizeof(image), g_stamp, g_context != nullptr ? g_context->buildVersion : nullptr);
	char state[700];
	ScrollableWaypointsStatus(state, sizeof(state));
	char line[1100];
	std::snprintf(line, sizeof(line), "%s %s | %s | %s", PluginName, PluginVersion, image, state);
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);  // also in the plugin log, for reports
	char env[600];
	EnvironmentStatus(env, sizeof(env));
	std::snprintf(line, sizeof(line), "environment %s", env);
	command->plugin->WriteConsoleMessage(line);
	OtherPlugins(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
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
	g_stamp = build_guard::ImageStamp(context->exeBase);
	char image[200];
	build_guard::Describe(image, sizeof(image), g_stamp, context->buildVersion);
	plog::Info("%s loaded (mod %s, %s)", PluginVersion, context->activeMod != nullptr ? context->activeMod : "none", image);
	if (!build_guard::Matches(g_stamp)) {
		plog::Warn("untested game image: if anything goes wrong, report it with this log");
	}
	StartEnvironmentGuard(context, "");  // global plugin: no mod folder to check; catches a second copy, lists others
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
