# Scrollable Waypoints — in-game tests

Status: wheel and panel detection confirmed in game. Act switching through the tab bar's native previous/next is new
and not yet run in game.

Run the rows in order. After the session, send `scrollable-waypoints.log` from `<game>\d2rloader\logs\`.

Setup: close the game, copy `scrollable-waypoints.dll` into `<game>\d2rloader\plugins\`, then start D2R through
D2RLoader. 

| # | What | How | Expected | If not |
|---|------|-----|----------|--------|
| 1 | Load | Start the game; the plugin card says Loaded | The log has `wpscroll on: the wheel switches Acts...`, with no `Act switching off` warning | Send the log |
| 2 | Wheel with the panel closed | Scroll anywhere in town | Same as without the plugin | Send the log |
| 3 | Scroll down | Open a waypoint, scroll down 5 times (cursor anywhere) | Acts go to the next one each notch, V→I wraps, with the tab click sound. The cursor never moves | Send the log |
| 4 | Scroll up | Scroll up 5 times | Previous Act each notch, I→V wraps | Send the log |
| 5 | No travel | Scroll a lot, fast and slow | You never travel and no destination is selected | **Stop and send the log right away** |
| 6 | Clicks unchanged | Click Act tabs and destinations, with and without scrolling first | As vanilla, and you travel where you click | Send the log |
| 7 | Locked Acts | Character that hasn't reached every Act | Scrolling skips the locked tabs, like a controller | Send the log |
| 8 | Reopen and new game | Close and reopen the panel; leave the game and join another; scroll each time | Works every time | Send the log |
