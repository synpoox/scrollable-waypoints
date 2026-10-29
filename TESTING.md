# Scrollable Waypoints — in-game tests

Status: loads, reads the wheel and detects the panel (confirmed in game). Switching Acts is not wired in yet; it waits for
the tab bar's native previous/next (memory dump + analysis).

Run the rows in order. After the session, send `scrollable-waypoints.log` from `<game>\d2rloader\logs\`.

Setup: close the game, copy `scrollable-waypoints.dll` into `<game>\d2rloader\plugins\`, then start D2R through
D2RLoader. 

| # | What | How | Expected | If not |
|---|------|-----|----------|--------|
| 1 | Wheel unchanged | Scroll anywhere, with the waypoint panel open and closed | Same as without the plugin. Nothing switches yet | Send the log |
| 2 | Panel detected | Open a waypoint, scroll, close it | The log has `waypoint panel open`, `would switch Act ...`, `waypoint panel closed` | Send the log |
