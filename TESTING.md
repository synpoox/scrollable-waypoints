# Scrollable Waypoints — in-game tests

Status: loads and reads the wheel (confirmed in game). Switching tabs by clicking is new and not yet checked in game.

Run the rows in order. After the session, send `scrollable-waypoints.log` from `<game>\d2rloader\logs\`.

Setup: close the game, copy `scrollable-waypoints.dll` into `<game>\d2rloader\plugins\`, then start D2R through
D2RLoader. Use a character that has reached all five Acts, plus one that hasn't for row 9.

| # | What | How | Expected | If not |
|---|------|-----|----------|--------|
| 1 | Wheel with the panel closed | In town, wheel anywhere | Same as without the plugin | Send the log |
| 2 | Before confirmation | Open a waypoint and scroll over the panel *before* clicking any tab (first time only) | Nothing happens. The log says "tabs not confirmed" | |
| 3 | Confirm | Click any Act tab by hand | The log says `tabs confirmed at W x H` | The log says `tab click ... is not on a predicted tab`: send it |
| 4 | Scroll down | Cursor over the panel, wheel down 5 times | Acts go I→II→III→IV→V→I. The cursor flicks to the tab and back | Send the log |
| 5 | Scroll up | Wheel up 5 times | V→IV→III→II→I→V order | Send the log |
| 6 | No travel | Scroll a lot, fast and slow | You never travel and no destination gets selected | **Stop and send the log right away** |
| 7 | Clicks unchanged | Click Act tabs and destinations, with and without scrolling first | Same as vanilla, and you travel where you click | Send the log |
| 8 | Outside the panel | Panel open, cursor over the game world | Wheel behaves as vanilla | Send the log |
| 9 | Locked Acts | Character that hasn't reached every Act | Scrolling skips the locked tabs | Send the log |
| 10 | Next start | Restart the game, open a waypoint, scroll without clicking a tab | Works straight away (the confirmation was saved) | Send the log |
