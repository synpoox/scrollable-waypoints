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

## First use: click one Act tab

The first time you open a waypoint at a given window size, **click any Act tab once by hand**. That click shows the
plugin where the tabs really are on your screen. Until then the wheel goes to the game as usual. The confirmed size
is saved in `scrollable-waypoints-calibration.txt` next to the DLL. Delete that file to redo it.

## How it works

`plugin/src/wp_scroll.cpp` explains it in full. In short:

| Need | Source |
|---|---|
| Wheel | Subclass of D2R's window procedure (`WM_MOUSEWHEEL`, confirmed in game). The SDK has no mouse input |
| Panel open? | UI messages `PanelManager:OpenPanel` / `ClosePanel` with `WaypointsPanelExpansion` (or `WaypointsPanelOriginal` for Classic characters, 4 Acts) |
| Cursor over the panel | Panel rectangle computed from the layout (`$LeftPanelRect`, `$LeftPanelAnchor`) and the window size |
| Act shown on open | Lifecycle `ActChanged` (the panel opens on the player's Act) |
| Act shown after that | Your tab clicks (the tab under the cursor) and the plugin's own clicks |
| Switch Act | A click on the tab's centre. The tabs are one `TabBarWidget` whose message `Waypoints:SelectTab` has no tab number, so no message can ask for "Act N". The cursor is put back right after |

Safety:
- The plugin clicks only at a window size that one of your own tab clicks has confirmed.
- It clicks the middle of the tab, 28 layout units above the destination buttons.
- It doesn't click while a mouse button is held.
- A tab that doesn't respond (an Act not reached yet) is skipped until the panel closes.
- It never consumes a UI message and patches no game code.

## Known limits

- A real tab click per window size is needed once (see above).
- A layout mod that moves the waypoint tabs needs a plugin update. Reimagined uses the vanilla positions.
- The cursor jumps to the tab for one or two frames while the plugin clicks.
