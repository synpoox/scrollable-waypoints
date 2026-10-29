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

With the game closed, copy `scrollable-waypoints.dll` into `<game>\d2rloader\plugins\` (D2RLoader's global plugin
folder), then start D2R through D2RLoader. It works with or without a mod. It has no data changes. `mod.json` is
there only so d2rAI-toolkit's `modkit install` can also ship it inside a mod
(`<game>\mods\ScrollableWaypoints\d2rloader\plugins\`). Keep only one copy.

Third-party code: see NOTICE.md.

Logic tests (any OS): `g++ -std=c++20 -Iplugin/src plugin/tests/wp_logic_test.cpp -o wp_test && ./wp_test`

## How it works

While the waypoint panel is open, the wheel does to the Act tabs exactly what a controller's shoulder buttons do.
The plugin calls the tab bar's own code. It uses no pixel coordinates and no simulated clicks, and the cursor never
moves.

| Need | Source |
|---|---|
| Wheel | Subclass of D2R's window procedure (`WM_MOUSEWHEEL`). The SDK has no mouse input |
| Panel open? | UI messages `PanelManager:OpenPanel` / `ClosePanel` with `WaypointsPanelExpansion` (or `WaypointsPanelOriginal`: Classic characters, 4 Acts) |
| The panel's tab bar | A hook on `TabBarWidget::SetActiveTab`. When the panel shows an Act, it calls that function from one known place, and the plugin takes that tab bar |
| Switch Act | The tab bar's controller logic: previous/next with wrap-around, `FindAvailableTab` (skips Acts not reached yet), then `SetActiveTab` with the tab sound. The panel then shows that Act, just as after a click |

The engine addresses are for D2R 3.3 build 93847 (image `0x6ab3782c`) only, and the entry bytes are checked. On any
other build the plugin installs no hook and the wheel always goes to the game. See `ENGINE-NOTES.md`, and
`tools/dump-game.ps1` for redoing the addresses after an update.

The plugin never consumes a UI message.
