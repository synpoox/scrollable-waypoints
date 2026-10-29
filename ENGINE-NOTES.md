# Engine notes (D2R 3.3, build 93847, image `0x6ab3782c`)

The addresses Scrollable Waypoints relies on. They were found in a memory dump of the running game, made with
`tools/dump-game.ps1`. RVAs are relative to the image base. **After any D2R or D2RLoader update they must be found
again.** Until then the plugin's build guard switches Act switching off.

D2R's own UI classes have no RTTI. They are reached through the game's reflection registry (type name → factory →
constructor → vtable) and its field tables (field name → object offset).

## TabBarWidget

| What | RVA / offset | Notes |
|---|---|---|
| Type getter (reflection) | `0x877df0` | Registers `TabBarWidget` (base `FocusableWidget`), type record `0x3440980` |
| Factory | `0x877960` | Allocates `0x17a8` bytes, calls the constructor |
| Constructor | `0x877130` | vtable `0x1d75e00`; defaults: `enableControllerTabSwitching` 1, `tabLeftAction` 7, `tabRightAction` 8 |
| Field table | `0x2536000` | 0x30-byte records: name, kind, offset, type |
| `tabCount` | `+0x558` | int |
| `onSwitchTabMessage` | `+0xb30` | message object |
| `enableControllerTabSwitching` / `...Indicators` | `+0x16a0` / `+0x16a1` | bool |
| `tabLeftAction` / `tabRightAction` | `+0x16a4` / `+0x16a8` | `ControllerAction` enum (controller only: select, back, option1-4, focusLeft/Right, tabLeft, tabRight, ...) |
| Active tab | `+0x16c4` | int, -1 = none |
| Tab available flags | `+0x1788 + i` | byte per tab |
| **SetActiveTab** `(this, int index, bool playSound)` | `0x878fb0` | Re-checks availability, plays the tab sound when asked, sends `onSwitchTabMessage`. Entry `48 89 5C 24 18 89 54 24 10 55 56 57 48 83 EC 30` (16 bytes). Hooked by the plugin |
| **FindAvailableTab** `(this, int index, bool forward)` | `0x877d20` | `index` if available, else the nearest available tab in that direction (wrapping). Entry `48 89 5C 24 08 4C 63 DA 48 8B D9 41 80 BC 0B 88 17 00 00 00` |
| SetTabAvailable `(this, int index, bool available)` | `0x8790d0` | |
| Controller tab switching | `0x878ed3` (in vtable `+0x20`, `0x878d30`) | tabLeft: `(current <= 0 ? count : current) - 1`, FindAvailableTab(backward); tabRight: `current >= count-1 ? 0 : current+1`, FindAvailableTab(forward); then SetActiveTab(index, true) |

## WaypointsPanel

| What | RVA / offset | Notes |
|---|---|---|
| Tabs lookup | `0x1eadf0` | Finds child `"Tabs"`, type-checks it (vtable `+0x58` = GetType, type `+0x10` = base type), stores it |
| Tab bar | `+0x170` | `TabBarWidget*` |
| Shown Act | `+0x168` | int |
| Show Act | `0x1ec820` `(this, int act)` | Marks tab 0 available and tabs 1+ by the player's progress, then calls SetActiveTab(act, false). That call returns to **`0x1ec8d3`**, which is how the plugin recognises the waypoint tab bar |
| Message | `Waypoints:SelectTab` (no tab number), sent by the tab bar on every change | |

## Input

`InputMessage` event names, in enum order: LeftMouseButtonDown, LeftMouseButtonUp, RightMouseButtonDown,
RightMouseButtonUp, BindableMouseButtonDown, BindableMouseButtonUp, MouseScroll (6), MouseMove, MouseLeave,
KeyDown, KeyUp, ControllerActionBegin (11), ControllerActionEnd, ControllerInput, BindingDown, BindingUp,
CharacterTyped, IME…

D2R gets the wheel as `WM_MOUSEWHEEL` on its `OsWindow` window (confirmed in game).

## Stash (for comparison)

`BankTabs` is a TabBarWidget (`BankPanelMessage:SelectTab`). Shared-stash pages have their own previous/next
messages, `BankPanelMessage:SharedStashLeft` / `SharedStashRight`, which is what the vanilla wheel scrolls.
