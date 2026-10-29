# Scrollable Waypoints

A D2RLoader plugin mod for Diablo II: Resurrected 3.3 (D2RLoader 1.3.1, PluginSDK 0.3.0), built with d2rAI-toolkit.
While the waypoint panel is open and the cursor is over it, the mouse wheel switches the Act tab:

- wheel down: next Act (I → II → III → IV → V → I);
- wheel up: previous Act (I → V → IV → III → II → I).

That is all it adds. It has no keys or settings, never selects a destination and never travels. With the panel
closed, or with the cursor elsewhere, the wheel goes to the game untouched. The panel's look, its tabs, its
destinations and its clicks stay vanilla.

## Download a build

Every push builds the DLL on GitHub: open the **Actions** tab, click the newest green "Build DLL" run, and download
**scrollable-waypoints-dll** under *Artifacts* (a zip with `scrollable-waypoints.dll` inside). Being signed in to
GitHub is required to download artifacts.

## Build it yourself on Windows

1. Install [Visual Studio 2022 Community](https://visualstudio.microsoft.com/vs/community/) with the workload
   **"Desktop development with C++"** (it includes CMake), and [Git](https://git-scm.com/download/win).
2. Open **"Developer PowerShell for VS 2022"** from the Start menu.
3. Run, one line at a time:
   ```
   cd $HOME\Documents
   git clone https://github.com/synpoox/scrollable-waypoints.git
   git clone https://github.com/D2RLoader/PluginSDK.git
   git -C PluginSDK checkout 717f727a0ec52912d1558764345f8fa3453a2bd6
   powershell -ExecutionPolicy Bypass -File scrollable-waypoints\plugin\build.ps1 -Sdk PluginSDK
   ```
4. The last line prints `built: ...\scrollable-waypoints.dll`: that is the plugin.

## Build and install

```
powershell -ExecutionPolicy Bypass -File plugin\build.ps1 -Sdk <d2rAI-toolkit>\third_party\PluginSDK
python -m modkit install <this folder>        (game closed)
```

The mod has no data changes (`patches/` is empty). It ships only `scrollable-waypoints.dll`, which goes in
`mods\ScrollableWaypoints\d2rloader\plugins\`.

Third-party code: see NOTICE.md.

Logic tests (any OS): `g++ -std=c++20 -Iplugin/src plugin/tests/wp_logic_test.cpp -o wp_test && ./wp_test`

## How it works

`plugin/src/wp_scroll.cpp` explains it in full. In short:

| Need | Source |
|---|---|
| Wheel | Subclass of D2R's window procedure (`WM_MOUSEWHEEL`). The SDK has no mouse input |
| Panel open? | Per-frame UI tick: the panel resolves through `WidgetService::findPanel` / `getWidgetRect` |
| Cursor over the panel | Left half of the client area (the waypoint panel is a left-side panel) |
| Act shown on open | Lifecycle `ActChanged` (the panel opens on the player's Act) |
| Act shown after that | UI message listener follows every waypoint tab message, yours and the plugin's |
| Switch Act | `WidgetService::dispatchUiAction` with the panel's tab message. Only a command containing "Tab" is ever sent |

The plugin patches no game code and uses no game address, so it has no image check. It never consumes a UI message.

## Not verified yet

No game files and no game run were available when this was written. See TESTING.md.

- The panel's root name (candidates: `WaypointsPanel`, `WaypointPanel`, `WaypointsPanelHD`, `Waypoints`).
- The tab message. The default is `WaypointsPanelMessage:ActivateTab:<0-4>`, by analogy with the skill tree. One
  click on an Act tab teaches the plugin the real message, and the log records it.
- That `findPanel` stops resolving while the panel is closed.
- That D2R gets the wheel as `WM_MOUSEWHEEL`, not only as raw input.
- What happens on a locked Act (one not reached yet, or Act V on a Classic character). The plugin cycles all five
  Acts, as specified.

Once the first test session confirms the names, they get hard-coded and the guesses go away.
