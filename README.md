# Scrollable Waypoints

A [D2RLoader](https://github.com/D2RLoader) plugin for Diablo II: Resurrected. While the waypoint panel is open,
the **mouse wheel switches between the Act tabs**:

- wheel down: next Act (I → II → III → IV → V → I)
- wheel up: previous Act (I → V → IV → III → II → I)

The cursor can be anywhere on screen. That is all it adds: no new keys, no settings, no new UI.

- Scrolling only changes the Act tab. It never selects a destination and never makes you travel.
- Acts you haven't reached yet are skipped, as with a controller. Classic (non-expansion) characters cycle Acts I–IV.
- With the waypoint panel closed, the wheel works exactly as before.
- The panel's look, its tabs, its destinations and every click stay vanilla.

## Requirements

- **Diablo II: Resurrected 3.3**, run through **D2RLoader 1.3.1**.
- Works with or without a mod. Tested with Reimagined, which uses the vanilla waypoint panel.

The plugin is tied to this exact game version. On any other D2R or D2RLoader version it switches itself off and does
nothing, so a game update can't make it misbehave. It needs an update for each new game version.

## Install

1. Close the game.
2. Download `scrollable-waypoints.dll` from the [latest release](https://github.com/synpoox/scrollable-waypoints/releases/latest).
3. Copy it into `<game folder>\d2rloader\plugins\`. Create the `plugins` folder if it doesn't exist.
4. Start the game through D2RLoader. Its plugin list shows **Scrollable Waypoints** as Loaded.

To uninstall, close the game and delete the DLL.

The plugin is client-side only, and other players don't need it.

## Troubleshooting

- **Nothing happens when I scroll.** Check the plugin's log in `<game folder>\d2rloader\logs\`. A line
  `Act switching off: ...` means your game or D2RLoader version isn't the one this release supports.
- **Console:** with D2RLoader's console enabled, the command `scrollable-waypoints` prints the plugin's state.
- When reporting a problem, include the log and your D2R and D2RLoader versions.

## How it works

The plugin does to the Act tab bar exactly what a controller's shoulder buttons do. It calls the game's own
previous/next for that tab bar. It uses no screen coordinates and no simulated clicks, and the cursor never moves.

| Part | How |
|---|---|
| Wheel | Reads `WM_MOUSEWHEEL` from the game window (D2RLoader's SDK has no mouse input). The wheel is consumed only while the waypoint panel is open |
| Panel open/closed | D2R's UI messages `PanelManager:OpenPanel` / `ClosePanel` for `WaypointsPanelExpansion` / `WaypointsPanelOriginal` |
| The panel's tab bar | One checked hook on `TabBarWidget::SetActiveTab`. The waypoint panel calls it from one known place every time it opens, which identifies its tab bar |
| Switching | The tab bar's controller logic: previous/next with wrap-around, skip unavailable tabs, select with the tab sound. The panel then shows that Act, just as after a click |

The game addresses are for D2R 3.3 as run by D2RLoader 1.3.1 (image timestamp `0x6ab3782c`), and the plugin checks
the code before hooking it. [ENGINE-NOTES.md](ENGINE-NOTES.md) lists every address and offset.

## Building from source

Builds run on GitHub Actions for every push. Version tags (`v*`) publish a release with the DLL attached. To build
locally on Windows:

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/vs/community/) with the workload **Desktop
   development with C++** (it includes CMake), and [Git](https://git-scm.com/download/win).
2. In **Developer PowerShell for VS 2022**:
   ```
   git clone https://github.com/synpoox/scrollable-waypoints.git
   git clone https://github.com/D2RLoader/PluginSDK.git
   git -C PluginSDK checkout 717f727a0ec52912d1558764345f8fa3453a2bd6
   powershell -ExecutionPolicy Bypass -File scrollable-waypoints\plugin\build.ps1 -Sdk PluginSDK
   ```
3. The DLL is at `scrollable-waypoints\plugin\build\Release\scrollable-waypoints.dll`.

The platform-independent logic has unit tests:
`g++ -std=c++20 -Iplugin/src plugin/tests/wp_logic_test.cpp -o wp_test && ./wp_test`.
[TESTING.md](TESTING.md) is the in-game checklist.

### Updating for a new game version

1. Start the new game version through D2RLoader.
2. Run `tools/dump-game.ps1`. It saves the running game's code from memory, read-only, to your Desktop. The dump is
   Blizzard's code: keep it private and never commit it.
3. Find the addresses in [ENGINE-NOTES.md](ENGINE-NOTES.md) again, using the same landmarks (strings, field
   tables, call sites).
4. Update `plugin/src/wp_scroll.cpp` and the expected image in `plugin/src/build_guard.h`.

## License and credits

[MIT](LICENSE) © Synpoo. Third-party code and the SDK are listed in [NOTICE.md](NOTICE.md). Built with the
[D2RLoader PluginSDK](https://github.com/D2RLoader/PluginSDK) and
[d2rAI-toolkit](https://github.com/Nugwa628/d2rAI-toolkit).

Diablo® II: Resurrected is a trademark of Blizzard Entertainment, Inc. This project is not affiliated with or
endorsed by Blizzard Entertainment.
