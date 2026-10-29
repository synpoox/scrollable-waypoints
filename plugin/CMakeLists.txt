# scrollable-waypoints: mouse wheel over D2R's waypoint panel switches the displayed Act (D2RLoader plugin).
#
# Build:  powershell -ExecutionPolicy Bypass -File build.ps1 -Sdk <PluginSDK checkout>
# Output: build/Release/scrollable-waypoints.dll -> copy it to <game>/d2rloader/plugins/
cmake_minimum_required(VERSION 3.29)
project(scrollable_waypoints LANGUAGES CXX RC)

set(D2RL_SDK_DIR "" CACHE PATH "D2RLoader PluginSDK directory (github.com/D2RLoader/PluginSDK at 717f727, SDK 0.3.0)")
if(NOT D2RL_SDK_DIR)
	message(FATAL_ERROR "Pass -DD2RL_SDK_DIR=<PluginSDK checkout>")
endif()

set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${D2RL_SDK_DIR} PluginSDK EXCLUDE_FROM_ALL)

add_library(scrollable_waypoints SHARED
	src/plugin.cpp
	src/wp_scroll.cpp
	src/log.cpp
	src/scrollable-waypoints.rc
)
target_link_libraries(scrollable_waypoints PRIVATE D2RLPlugin::D2RLPlugin user32)
set_target_properties(scrollable_waypoints PROPERTIES OUTPUT_NAME "scrollable-waypoints")
if(MSVC)
	target_compile_options(scrollable_waypoints PRIVATE /W4 /permissive- /EHsc /utf-8)
	set_property(TARGET scrollable_waypoints PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")  # static CRT: no VC++ runtime dependency
endif()
