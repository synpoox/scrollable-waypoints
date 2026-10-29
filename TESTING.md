# Scrollable Waypoints — in-game tests

Build 0.1.1 has **not been run in the game yet**. It compiles against PluginSDK 0.3.0 and the logic tests pass;
nothing below has been checked in D2R. Run the rows in order. After the session, send me the plugin log from
`<game>\d2rloader\logs\` and the output of the `scrollable-waypoints` console command (rows 2 and 9).

Setup: close the game, copy `scrollable-waypoints.dll` into `<game>\d2rloader\plugins\`, then start D2R through
D2RLoader. The plugin card should say **Loaded** (0.1.0 showed "Errored" in the global folder because it was
mod-scoped only). Use a
character that has reached all five Acts.

| # | What | How | Expected | If not |
|---|------|-----|----------|--------|
| 1 | Wheel with the panel closed | In town, wheel on the left and right halves of the screen (bind a skill to the wheel if you can) | Same as without the mod | Send the log. The plugin probably thinks the panel is open: `findPanel` still resolves while it's closed |
| 2 | Status at load | Console: `scrollable-waypoints` | `on`, `window hooked`, `panel ... closed` | The status line says which part is missing |
| 3 | Panel found | Click a waypoint | The log has `waypoint panel found as "..."` and `waypoint panel open (Act N shown ...)`, where N is your current Act | No "found" line: the panel's name isn't in my list. Send the log, which has every `ui message` line |
| 4 | Scroll down | Panel open, cursor over it, wheel down 5 times | Acts go I→II→III→IV→V→I (starting from your current Act and wrapping the same way) | Nothing changes: check the `learned tab message` / `dispatch ... failed` lines. Click one Act tab, then retry; if that fixes it, the default message was wrong and the log has the real one |
| 5 | Scroll up | Wheel up 5 times | V→IV→III→II→I→V order, wrapping | As row 4 |
| 6 | No travel | Scroll a lot | You never travel and no destination gets selected | Stop and send the log right away |
| 7 | Clicks unchanged | Click Act tabs and destinations, both with and without scrolling first | Same as vanilla, and you travel where you click | Send the log |
| 8 | Right half | Panel open, cursor over the game world on the right | Wheel behaves as vanilla | Hover-area problem: send your resolution |
| 9 | Counters | `scrollable-waypoints` after rows 3-8 | `wheel N seen, M used` with M > 0, and `switches` > 0 | `seen 0` while you scrolled means D2R reads the wheel through raw input (`raw` > 0). The plugin then needs a different wheel source |
| 10 | Locked Acts | Character that hasn't reached Act V (or a Classic character) | Note what scrolling onto a locked Act does | Tell me what you saw |
| 11 | Close paths | Close with Esc, by travelling, and by walking away | Log shows `waypoint panel closed` each time, and row 1 still holds afterwards | Send the log |
