# The template builder inside the game: plan

Status: **approved by the owner on 2026-10-25**, for the next version. The owner asked: "for next version can we prioritize in-game map building tools (to have parity with our web tool)". This is the first part of step C in [map-generator.md](map-generator.md). Background: [../subsystems/rmg.md](../subsystems/rmg.md).

Everything below comes from static reading (the reference exe through Ghidra, and the game's data in the test copy's paks), done on 2026-10-25. The robot was not used: the PC was busy with a real game. So nothing here has been tried in a running game yet. The first build step is a robot check of the open points.

## In short

- **What we build:** a "Template..." button on the game's own random map screen. It opens a panel with every choice of the web template maker, a live zone map, and the same numbers and warnings. "Use template" fills in the game's own screen (map size, underground, players, template); then you press the game's Create and Save, as today.
- **Multiplayer:** nothing new is sent over the network. The template's **file name is the recipe** (for example `OH-X1-321110nnxxxxxxxa.xdb`). The game already sends the template's path to every PC when they join. Every PC's DLL rebuilds the same file from that name, byte for byte, and the game on each PC rebuilds the same map, as it does today with any template.
- **How:** the template maker is ported to C++ inside the DLL, about 450 lines, checked byte for byte against the JavaScript. The panel is Dear ImGui, which the player list already uses, styled with the game's own pictures. While the panel is open it takes the whole mouse and keyboard, so input can't reach the game behind it.
- **Effort:** about 1,800 lines of new C++, in 6 to 8 sessions, each ending with something testable.

## What we found

### 1. The random map screen

**One screen serves both modes.** It is `NUI::CRMGScreen` (vtable `0x00e41114`), screen ID `ID_RMG_SCREEN`. It is created in `0x0079acc0` and set up in `0x00799b60`. Two places open it:
- **Single player:** the custom game screen (`ID_CUSTOM_GAME_CREATE_SCREEN`). Its command `rmg_screen` is registered in `0x0077acf0`.
- **Multiplayer:** the "create game" screen (`ID_MP_CREATE_SCREEN`). The command is registered in `0x00703c90`, and its handler `0x00703170` calls `0x00796be0`, which creates `NUI::CCreateRMGScreen` (vtable `0x00e40e30`).

**How the player uses it today:**
1. Pick options, then press **Create**. The map is generated and shown on the screen's minimap.
2. Press **Save**. The map is saved as a normal map under `Maps/` (`0x00798790`; the file name pattern is `rmg_map_<number>`).
3. Press **Back**, then pick the map in the map list. The list has a filter button for random maps: `RMGFilterBtn`, console variable `custom_game_show_rmg_maps`.

**Layout** (`UI/RMGScreen/` in `data.pak`; the screen file itself comes from `a2p1-data.pak`). The units are those of the game's 1024×768 layout, scaled with the screen height:

| Part | Window | Position, size |
|---|---|---|
| The panel, centred | `MainPanel`, background `MainBg.(Texture).dds` | 649×508 |
| Title | `Title` | 41,36; 572×36 |
| Minimap and progress bar | `MinimapPanel` | 44,76; 263×263 |
| Options list (scrolls) | `OptionsContainer` | 315,76; 294×334 |
| Map name | `MapNameEdit` | 58,372; 234×16 |
| Buttons, 122×34 each (`UI/Common/DlgButtons/Button_122_34`) | `CreateBtn`, `SaveBtn`, `BackBtn` | x 41, 188 and 491; y 430 |

The bottom row has **a free slot between Save and Back** (x 340 to 462), the right size for one more button.

**The options list is built from data.** The list is `InfoRMGScreen.Controls` in `UI/UIGameRoot.(UIGameRoot).xdb` (MMH5.5's copy in `MMH55-Frame.pak` wins). For each item, `0x00799b60` copies a template window into the list, by its `ControlType`:

| ControlType | Option object | Template window |
|---|---|---|
| 1 `ECT_COMBO` | `CComboOption` | `ComboTemplate` |
| 2 `ECT_SLIDER` | `CSliderOption` | `SliderTemplate` |
| 3 `ECT_DIVIDER` | `CDividerOption` | `DividerTemplate` |
| 4 `ECT_CHECKBOX` | `CCheckBoxOption` | `CheckBoxTemplate` |

The slider's template window holds only a check box, no slider: don't count on sliders.

**What each option means is code.** Each item has a `Param` from a closed list of 20 values:

| Values | Meaning |
|---|---|
| 0 | `EP_NONE` |
| 1 to 5 | `EP_MAP_SIZE`, `EP_AMOUNT_OF_WATER`, `EP_MONSTER_LEVEL`, `EP_NUMBER_OF_PLAYERS`, `EP_NUMBER_OF_MAP_LEVELS` |
| 6 | `EP_CUSTOM_TEMPLATE` |
| 7, 8 | `EP_GENERATE_MINIMAP`, `EP_TRUE_RANDOM_TOWNS` |
| 9 to 11 | `EP_RESOURCE_LEVEL`, `EP_EXP_LEVEL`, `EP_GRAAL_ON_MAP` |
| 12 to 19 | `EP_RACE_PLAYER_1` to `_8` |

In MMH5.5's list, in order: size (7 sizes), levels (with or without underground), monster level (5 steps), water (none or islands), players (2 to 8), a divider, then the rest:
- **"ARMG Templates"**, MMH5.5's name for the custom template option. It is a check box plus a list of templates. Ticking the box shows MMH5.5's tips (`0x007995b0`).
- Check boxes for the minimap, truly random towns and the grail.
- Resources and experience (5 steps each).
- One faction per player.

**The code of the screen:**

| What | Where |
|---|---|
| Commands from its buttons: `create`, `save`, `change_option`, `change_option_check`, `map_name_changed`; returns 0 for unknown ones | `0x0079a650` (vtable cell `0x00e4110c`) |
| Per-frame state machine (generate, close the "please wait" guard screen, error boxes) | `0x0079a760` |
| Generate | `0x00799780` |
| Read every control into the screen's value block (screen `+0x264`) | `0x00797520` |
| Turn the values into the generator's parameters | `0x00799230` and `0x007993a0` |
| Refill the template list | `0x00798160` |
| Save | `0x00798790`, `0x00798530` |

The **value block** at screen `+0x264` holds one 0x20-byte entry per option. Byte +0 of an entry says whether the option is "on" (for a combo: set, not random), and +4 holds the chosen index:

| Offset | Option |
|---|---|
| `+0x00` | size |
| `+0x20` | levels |
| `+0x40` | water |
| `+0x60` | monsters |
| `+0x80` | players (index + 2) |
| `+0xa0` | custom template (on, the chosen index, and the list of template paths at `+0xc0`) |
| `+0xec`, `+0xed`, `+0xee` | minimap, random towns, grail |
| `+0xf0` | resources |
| `+0x110` | experience |
| `+0x130` | the factions |

**The generator's parameters** (written by `0x00799230` and `0x007993a0`):

| Offset | Parameter |
|---|---|
| `+0x10` | size index |
| `+0x0d` | underground |
| `+0x18` | players |
| `+0x24` | the template link |
| `+0x48` | water |
| `+0x50` | monsters |
| `+0x54` | factions |
| `+0x80` | **seed = `time(NULL)`** |
| `+0x94` | minimap |
| `+0x95` | random towns |
| `+0x98` | resources |
| `+0xa0` | experience |
| `+0xa5` | grail |

**How the template is chosen.** The generator object is `NRMG::CRandomMapGenerator`, interface vtable `0x00e55f28`:

| Slot | Function | What it does |
|---|---|---|
| 1 | `0x0090e7e0` | Generate |
| 2 | `0x0090c710` | List all templates |
| 3 | `0x0090c880` | List the templates that fit the options |
| 4 | `0x00908da0` | Write the map |
| 7 | | The generator's version |

A template **fits** when three things are true:
- MinPlayers ≤ players ≤ MaxPlayers;
- the map area lies between MinMapSize and MaxMapSize (doubled with underground);
- it isn't a test template.

The choice then depends on the custom template box:
- **Box off:** the screen picks a random template among those that fit.
- **Box on:** it takes the one picked in the list. **That list only shows templates that fit the current size, levels and players**, and `0x00798160` refills it when they change. That's why the web page tells players to pick the size first.

**The option objects** (a vector at screen `+0x1c0`/`+0x1c4`):
- Fields: `+0xc` the `EP_*` value; `+0x10` a short name (`water`, `monster`, `player`, `levels`, `custom`, `minimap`, `random_towns`, `resources`, `experience`, `graal`, ...).
- Vtable: `+0x20` "is on", `+0x24` "set on", `+0x28` the chosen index, `+0x30` "replace the list" (combos, `0x0079e900`).
- Controls: a combo's list control is at `+0x28` (its slot `+0x14` gets the selection, `+0x18` sets it); its check box is at `+0x38`.

**What we can change:**
- **Set the existing controls from our code**, using the objects above. The game then builds its parameters as if the player had clicked.
- **Add windows to the screen**, such as one more button, by serving changed UI files through our file system hook. Nothing on disk changes.
- **Catch a new command**, by wrapping the command handler `0x0079a650`.
- **Not:** give a new meaning to a game option without decompiling the generator (step 3 of the generator plan). Options added to `InfoRMGScreen` would be drawn by the game, but only our DLL would read them.

### 2. Multiplayer: what travels, and why a missing template crashes

**The host sends a recipe, not a map:**
- The game info that the host advertises (`NUI::CGameData` `+0xc`, vtable `0x00e3816c`; on a LAN, the game info packet that GameRanger relays) has a "random map" flag at `+0x104`. The lobby list then shows a red " RMG" (`0x006fbf20`).
- From `+0x108` it carries the map's random map settings (`sRMGProps`): **the template's path, the seed, the size, water, monsters, underground, resources, experience, the options, the factions**, and at `+0x110` the generator version.

**When a client joins** (`0x006f7960`, called from `0x006f7c20` and `0x006f8ac0`) a random map game:
1. It creates `CCreateRMGConnectorCmd` (`0x007014e0`, vtable `0x00e38718`), handing it that game info.
2. The command's Execute (`0x00701530`) shows "Loading RMG map" and runs `NUI::CRMGConnector` (constructor `0x00701950`, work in `0x007011d0`).
3. The connector checks the generator version (slot 7), lists all templates (slot 2), and looks for the map's template in that list (`0x00701670`).
4. It rebuilds the parameters from the game info (`0x00701090`), generates the map (slot 1) and writes it (slot 4).

Errors go back to the lobby as codes 1 to 4, shown as `RMG_MAP_FAIL` (`0x006f6ac0`).

**Other maps are not sent either:**
- Normal maps get a checksum test instead (`0x006f77c0`, "CalculatingChecksum").
- A client without the map gets `ERROR_NO_MAP` (`0x006950e0`).
- The game has no map transfer at all.

**Why a client without the template crashes:** the template link is resolved (`0x004a69d0`) and used without checking that it was found:
- in the connector's lookup at `0x007012b1`..`0x007012c4`, which reads the template's path string at `+0xc`;
- in Generate at `0x0090e8db`, `cmp [edi+0x84],1` with `edi` the template.

A missing file gives a null pointer, so the game crashes instead of showing `RMG_MAP_FAIL`. This is static reading; the robot hasn't reproduced it.

**So a custom template needs exactly this:** every PC can open the same path (`RMG/Templates/<name>.xdb`) and gets the same bytes. Then each PC builds the same map from the same seed, as the game already does for its own templates.

**Existing messages we could use:**
- **Lobby chat.** On a LAN it is `SGameChatPacket`; in game, `NMP::CChatCommand`, with a chat command parser `NMPChat::CMPCommandsParser`.
- **The co-op guard's hello.** Only after the game has started, which is too late: the map is rebuilt when the client joins.

**We don't need either.** The template path already reaches every PC inside the game info. If the path itself is the recipe, there is nothing else to send.

### 3. Building the template inside the DLL

- **Port, not engine.** `tools/rmg/template_maker.js` is 361 lines, about 250 of them logic: the zone graph, distances, zone fields, guards, size choice and the XML writer. Ported to C++ that is about 450 lines, plus about 60 for the zone map preview (the page's `positions()`).
  - Running the JavaScript itself would need a script engine: QuickJS or Duktape, tens of thousands of lines, and a download. That is the kind of big dependency the brief rules out.
- **Byte for byte.** The C++ must write exactly the bytes the JavaScript writes: the BOM, CRLF line ends, and the same rounding.
  - The maker only uses `Math.round` on positive numbers, so rounding is the same in C++.
  - It uses `Math.log` only to compare sizes.
  - A test proves it: Node writes the expected files for the 24 ready templates and 400 random choices, and a small test exe (`src/hook/test/`, like `original_music_test.cpp`) compares.
- **Serving.** `src/mods/rmg_templates.cpp` already serves files through the game's file system: Open (slot 7, `0x009e0020`) and List (slot 10, `0x009e02b0`) of `NVFS::CWinVFS`. The game lists the folder again each time it needs the templates (`0x0090c710` and `0x0090c880` → `0x00910b30`). A template added while the game runs should therefore show up on the next refresh; step 0 checks this.

### 4. The three ways to draw the panel

**How the game reads input** (static reading; checked in step 4):
- **The mouse in menus comes from window messages.** The interface's event names are `win_left_button_down`, `win_mouse_move`, `win_char`, `win_key`, ..., registered in `0x00489550`. The exe also uses `GetCursorPos` and `SetCursorPos`.
- **Hotkeys come from DirectInput.** The robot found that the keyboard is read through DirectInput 8. Our DLL *is* `dinput8.dll`, and `src/hook/robot_input.cpp` already wraps `GetDeviceState`/`GetDeviceData`.
- **The player list takes no input today.** It runs with `ImGuiConfigFlags_NoMouse`, and only F9 is read, through `GetAsyncKeyState`.

| | (a) ImGui panel next to the game's controls | (b) The game's own widgets | (c) Our button + an ImGui panel over the screen (**recommended**) |
|---|---|---|---|
| What | A panel beside the options list. Clicks go to the panel or the game depending on where the mouse is. | New options in `InfoRMGScreen`, served by our file system hook. The game draws them; our DLL reads them. | A native-looking "Template..." button in the free slot. It opens a modal panel that covers the screen's main panel. |
| Effort | Medium-high: per-pixel input routing, plus sync with the game's controls while both are live | Medium: patch MMH5.5's 333 KB UI root file on the fly, serve about 40 text files, read the option objects | Medium: one input rule, ImGui drawing, a small UI file patch for the button |
| Look | Two styles side by side | Fully native | Native button, and the panel drawn with the game's own background, buttons and check boxes |
| Parity | Full | **Partial.** Only combos and check boxes: no zone map, no stats, no warnings, no presets row. About 15 combos make a long scrolling list. | **Full**: zone map, stats, warnings, presets, steppers |
| Input | Hard: route each message by position; the game's edit line and hotkeys still need care | None: the game does it | Simple: **while the panel is open, the game gets nothing**. Mouse and keyboard messages are swallowed in the window procedure, DirectInput keyboard and mouse data are zeroed. When it's closed, everything goes to the game. |
| Wine/Proton | Most risky: hover and routing timing | Safest | Low risk: the subclassed window procedure and DirectInput wrapper work under Wine. The ImGui drawing itself is not yet checked on Linux (the player list isn't either). |
| Risks | Clicks that leak through; double handling | MMH5.5 updates its UI root file; options the game itself ignores; the list gets long | Must restore input on close and on alt-tab; a UI file served wrongly breaks the screen (we check its bytes first and fall back to an ImGui-drawn button) |

**Recommendation: (c).** It is the only one with full parity, and the modal rule keeps the input work small and safe. The button can be native:
- serve a changed `UI/RMGScreen/MainPanel.(WindowSimpleShared).xdb` with one more child, a copy of `Save.(WindowMSButton).xdb` at x 340, y 430, with label `OpenHomm5Builder.txt`;
- give it a `UISRunReaction` sending `oh_builder`;
- wrap the command handler (`0x00e4110c`) to catch that command.

If the original file isn't the one we expect, we skip the patch and draw the button with ImGui at the same place.

### 5. Parity: every web option in the game

| Web page | In the game | Limits |
|---|---|---|
| Start from (8 presets) | A row of buttons at the top | The same 8 presets as the page |
| Teams (2 to 8) | Stepper `[-] 3 [+]` | At most 8 players in all (teams × players per team) |
| Players per team (1 to 4) | Stepper | 1 = free-for-all |
| Teammates' path (weak guard / open) | Two-button switch | |
| Start zone mines (all six / wood and ore) | Two-button switch | |
| Zones between neighbouring teams (0 to 4) | Stepper | |
| Zones behind each team (0 to 3) | Stepper | |
| Zones in the centre (0 to 3) | Stepper | |
| A town in each zone between teams | Check box | |
| A town in each team's treasure zone | Check box | |
| Loot (poor / normal / rich / very rich) | Four-button switch | Above the richest shipped template: the same warning as the page |
| Guards on paths (weak / normal / strong) | Three-button switch | |
| By distance from the bases (loot ×4 tiers, guards ×3 tiers) | A section that folds open, one switch per tier, "Same" first | |
| Size (best fit / M / L / XL / H / I) | Switch | "Use template" sets the game's own size to match |
| Underground | Check box | "Use template" sets the game's own levels to match |
| Name in the game's list | Shown, not typed: made from the choices, with a `*` for "built in game" | No typing, so no keyboard handling. A typed name would make the recipe long. |
| Zone map, legend | Drawn live with ImGui: circles, lines thicker for stronger guards, dashed when open | |
| Stats (players, zones, neutral zones, size, tiles per zone, zones by distance) and warnings | Text under the map | |
| Download / copy XML | **Not needed.** The template exists in the game as soon as it is used. "Save as file" writes it to `open-homm5\templates\` for the map editor or for sharing. | |

The game's own options stay on the game's screen: monster level, water, resources, experience, minimap, random towns, grail and the factions. The panel doesn't duplicate them.

**Presets:**
- **Built in:** the page's 8 presets.
- **Remembered:** the last choices, kept in `open-homm5.ini` (`[rmg] builder_last=<recipe>`).
- **Not needed:** "save as template file" for multiplayer, since the name already is the template. It stays only as a convenience for the map editor or sharing.
- **Later, if wanted:** named favourites, a list of label + recipe (Q2).

## The recommended design

### What the player sees

1. Single player or multiplayer, as today: Random map. The random map screen now has a fourth button, **Template...**, between Save and Back.
2. **Template...** opens the builder over the screen's main panel. The game is dimmed and gets no input while it is open.
3. Choose a preset or change choices. The zone map, numbers and warnings follow at once.
4. **Use template**: the panel closes and the game's own screen now shows:
   - the right size, levels and players;
   - "ARMG Templates" ticked;
   - our template picked in its list.

   **Cancel** changes nothing.
5. Set the game's own options if wanted (monsters, resources, factions...), press **Create**, look at the minimap, **Save**, **Back**, and pick the map. In multiplayer, the clients rebuild it when they join.

### The panel (mockup)

It covers the game's main panel exactly: 649×508 in the 1024×768 layout, centred, scaled with the screen height like the rest of the interface. Its background is the screen's own `UI/RMGScreen/MainBg.(Texture).dds`. Its buttons use `Button_122_34_{Normal,Highlight,Click,Disabled}` and its check boxes `UI/Common/CheckBox/CheckBox_{0,1}_*`, all loaded with the player list's texture code. Text is Tahoma Bold, like the player list, in the game's gold and parchment colours.

```
+------------------------------------------ 649 x 508 ----------------------------------------+
|  Template builder                                                         (title, y 36)    |
|  +---- zone map, 263 x 263 (where the minimap is) ----+  +---- choices, 294 x 334, scrolls -+ |
|  |              (1)  (2)                               |  | Start from: [2v2v2] [2 vs 2] ... | |
|  |        (B)            (B)                          |  | PLAYERS                          | |
|  |            (T)  (C)  (T)                           |  |  Teams            [-]  3  [+]    | |
|  |   (5) (6)      (B)       (3) (4)                    |  |  Players per team [-]  2  [+]    | |
|  |   colours = teams, gold = centre, grey = neutral    |  |  Teammates' path  [Weak][Open]   | |
|  +-----------------------------------------------------+  |  Start mines   [All six][Wood+ore]| |
|  6 players, 13 zones (7 neutral)                          | NEUTRAL ZONES  steppers, 2 boxes | |
|  Large 176x176 with underground, 4,766 tiles per zone     | LOOT AND GUARDS  switches        | |
|  ! warnings, if any                                       |   > By distance from the bases   | |
|  OH-2v2v2-Lu-Normal*                     (y 372)          | MAP  [Best fit][M][L][XL][H][I]  | |
|                                                           |      [x] Underground             | |
|                                                           +----------------------------------+ |
|  [ Use template ]   [ Save as file ]                                        [ Cancel ]        |
|   (x 41, y 430)      (x 188)                                                 (x 491)          |
+---------------------------------------------------------------------------------------------+
```

The parts sit where the game's own parts are: the zone map on the minimap, the choices on the options list, the buttons on the button row. Opening the panel feels like the screen flipping to another page.

### The parts of the code

| File | What it does |
|---|---|
| `src/mods/rmg_maker.cpp/.h` | The C++ template maker: the same choices, the same XML bytes as `template_maker.js`. Pure code, no game access. |
| `src/mods/rmg_recipe.cpp/.h` | Choices ↔ file name, for example `OH-X1-321110nnxxxxxxxa.xdb` (below) |
| `src/mods/rmg_templates.cpp` (extended) | Also serves any recipe name it is asked for: Open builds the bytes on the fly; List adds every recipe this PC has seen or built. Clients register the game's recipe when they join (step 3). |
| `src/mods/rmg_builder.cpp` | The panel (ImGui), the zone map and its numbers |
| `src/mods/rmg_screen.cpp` | The game's random map screen: the native button (served UI files), the `oh_builder` command, "Use template" (sets the game's controls) |
| `src/hook/overlay.cpp/.h` | One shared ImGui context, fonts and textures for the player list and the builder (moved out of `player_list.cpp`), plus modal input: the subclassed window procedure, and the DirectInput mute, made from the robot's wrappers |

**The recipe in the name.** `OH-X1-` followed by 16 characters, one per choice:

| Position | Choice | Values |
|---|---|---|
| 1 | teams | 2 to 8 |
| 2 | players per team | 1 to 4 |
| 3 | team zones | 0 to 3 |
| 4 | border zones | 0 to 4 |
| 5 | centre zones | 0 to 3 |
| 6 | the five yes/no choices | one character, 0–9 and a–v (border towns, team towns, open ally path, wood and ore only, no underground) |
| 7 | loot | `p` / `n` / `r` / `v` |
| 8 | guards | `w` / `n` / `s` |
| 9 to 12 | loot by distance | `x` = same, or a loot letter |
| 13 to 15 | guards by distance | `x` = same, or a guard letter |
| 16 | size | `a` = best fit, or `T` `S` `M` `L` `X` `H` `I` |

- **Example:** 2v2v2 Normal with underground is `OH-X1-321110nnxxxxxxxa.xdb`.
- **Path length:** the full path, `RMG/Templates/OH-X1-....xdb`, is 40 characters.
- **The name in the game's list** is the maker's own name plus `*`.
- **`X1` is the recipe version.** What an `X1` name builds must never change, the same rule as the ready templates. New features get `X2`, and the DLL keeps building `X1` forever.

**Settings** (`open-homm5.ini` `[rmg]`):
- `builder=1` (default): 0 means no button, no panel; recipe names are still served, so this PC can still join.
- `builder_last=`: the last recipe, filled in by the DLL.

## How multiplayer stays in sync

1. The host builds a recipe template. The game generates the map from it with a seed and saves it. The map's settings hold the recipe path.
2. The host creates the game with that map. The game info, which the game sends to every PC, carries the path, the seed and every option.
3. A joining client's game asks its file system for that path. Our DLL sees an `OH-X1-...` name, builds the XML from it with the same code as the host, and serves it. The game then rebuilds the same map.

**Nothing new travels, and nothing has to agree.** The same DLL version means the same bytes; this is tested against the JavaScript and between builds.

**The one known wrinkle.** The client's game lists the templates (to check the map's template exists) *before* it opens the template. So the client must know the recipe a moment earlier. The plan is to wrap the connector command's Execute (vtable cell `0x00e38734` → `0x00701530`): read the template path from the game info copy it holds (connector `+0x14`, settings at `+0x108`, template link at `+0x28` of those) and register the recipe before the game lists. Step 0 confirms where the path string sits. *(Step 3, 2026-10-25: the link is read by name when the game info arrives, which already makes the client open, and so register, the recipe before it lists; the Execute wrapper is kept as a second chance. See [Notes from step 3](#notes-from-step-3).)*

**Players without our DLL, or with an older one,** cannot join a game made from a recipe template. The same is already true for every `OH-` template. We'll add two things:
- with our DLL: a clean `RMG_MAP_FAIL` message instead of the crash when a template is missing (a mod in `src/mods`, not a change to the original code's files);
- a line in the install guide: everyone updates together.

**Teams:** unchanged. They come from the host's lobby (`rmg_teams.cpp`), not the map. *(Owner's answer 1, step 3: on a map made from a recipe, the host now sets the recipe's teams itself, `[rmg] recipe_teams=1`; the lobby arrows and `teams=` still win.)*

## Build steps (each one testable)

0. **Robot check of the open points**, when the PC is free (static reading can't settle these):
   - The full path: the random map screen opens from the custom game screen and from the multiplayer create screen; a served template shows in "ARMG Templates"; Create, then Save.
   - Whether a template added *while the game runs* shows after the list refreshes.
   - On a LAN robot run with an `OH-` template: the client's order of List and Open, and where the template path sits in the game info.
   - Screenshots of the screen at 1024×768 and 1920×1080 for the mockup.

   *Test:* the robot report and screenshots.

   **[x] Mostly done (2026-10-25)**, see [Robot results](#robot-results-2026-10-25):
   - **Served templates load:** the game's database loads a template we serve with no file, through the file system's Open. **Yes.**
   - **Added while the game runs:** a recipe the game opened shows in the next listing. **Yes.**
   - **The client's order and the path in the game info:** the client reads the template when the host's game info arrives, before it lists. The path sits as in the static reading.
   - **[x] The generator screen itself** (done with steps 5 to 7): the robot opens it with its step `rmg|`; our recipe template is in "ARMG Templates" (14th of the 19 that fit Large, underground, 4 players), and Create, then Save, work. Screenshots: [Notes from steps 5 to 7](#notes-from-steps-5-to-7).
1. **C++ maker and its parity test.** Add `rmg_maker.cpp` and a test exe that compares its output with Node's for the 24 ready templates and 400 random choices. `test_template_maker.js` gets a switch that writes the expected files.

   *Test:* `build.ps1 -Test` passes; the bytes are equal.

   **[x] Done (2026-10-25).** 435 of 435 templates equal byte for byte (24 ready, 400 random, 11 special), and the 12 refused choices give the same message. See [Notes from steps 1 and 2](#notes-from-steps-1-and-2).
2. **Recipe names served by the DLL.** Add `rmg_recipe.cpp`, plus Open and List of recipe names in `rmg_templates.cpp`. A setting can list recipes to offer, for example `[rmg] recipes=OH-X1-...`, before any panel exists.

   *Test:* the robot generates a map with the console command `rmg RMG/Templates/OH-X1-....xdb ...`, then through the screen.

   **[x] Code and unit tests done (2026-10-25).** `rmg_recipe_test.cpp`: the round trips, the bad names and paths, the 24 ready templates built from their recipe names, and 302 recorded `X1` names. **[x] Robot test done (2026-10-25)**, except the generator screen (see step 0): [Robot results](#robot-results-2026-10-25).

   *The robot test, when the PC is free:*
   - **At start**, the log says `recipe names ... are built when the game asks for them (maker self-check ok)`: the DLL's own build of the maker gives the recorded bytes (on Wine/Proton too, later).
   - **Console, no file** (no file has a recipe name, and no `recipes=`): the step `console|rmg RMG/Templates/OH-X1-321110nnxxxxxxxa.xdb -players 6 -size 3 -underground 1 start`. The log says `OH-X1-321110nnxxxxxxxa.xdb built from its name: "OH-2v2v2-Lu-Normal*" ...`; the generator's log names the template; the map has 6 start towns; 2 AI turns play.
   - **The same in small letters** (`rmg rmg/templates/oh-x1-321110nnxxxxxxxa.xdb ...`): served too.
   - **The list:** `recipes=OH-X1-321110nnxxxxxxxa` and `templates_selftest=1`: the self-test line counts 1 recipe. On the random map screen (Large, underground, 6 players, "ARMG Templates" ticked), `OH-2v2v2-Lu-Normal*` is in the list next to `OH-2v2v2-Lu-Normal`, and the `*` shows in the game's font; Create, then Save, work.
   - **Opened, then listed:** without `recipes=`, after the console test above, the screen's list also shows `OH-2v2v2-Lu-Normal*`.
   - **Off:** `templates=0`: the console test above finds no template (expect the game's own behaviour for a missing template, maybe the crash of §2; do it last).
3. **Clients join recipe games.** The connector hook, and the clean "template missing" message.

   *Test:* a LAN robot run on a recipe template: the client rebuilds the map, plays 4 AI turns, and every world checksum is equal on both copies. With the client's recipe support off: the message, no crash.

   **[x] Code and unit tests done (2026-10-25), with automatic teams for recipe maps (the owner's answer 1). [x] Robot test done (2026-10-25)**: every check below passed, after one fix ([Robot results](#robot-results-2026-10-25)). `src/mods/rmg_join.cpp`, and `rmg_teams.cpp` for the teams. What the static reading found: [Notes from step 3](#notes-from-step-3).

   *The robot test, when the PC is free* (LAN, `run_lan_test.py`; the host generates and saves a map from `OH-X1-321110nnxxxxxxxa` first, as in step 2, and starts the game on that saved map):
   - **Client rebuilds the map.** The client's log says `rmg templates: OH-X1-321110nnxxxxxxxa.xdb built from its name` while the lobby reads the host's game info, then `rmg join: joining a random map game made from the recipe OH-X1-321110nnxxxxxxxa`; the game starts on both PCs, 4 AI turns play, every world checksum is equal.
   - **Teams set automatically.** No `teams=` on the host: the host's log says `rmg teams: a map made from the recipe OH-X1-321110nnxxxxxxxa: 3 teams of 2, in player order`, and both logs' `this PC starts with` lines show players 1+2, 3+4, 5+6 on teams 0, 1, 2.
   - **The lobby arrows still win.** With `lobby_teams=1` and one arrow pressed on the host: `teams already set ... keeping them`, the arrows' teams on both PCs.
   - **`recipe_teams=0`** on the host: every player on their own team, as before.
   - **The client without recipe support** (`templates=0` on the client): the client's log says `rmg join: the map's template is missing on this PC`, the lobby shows the game's "can't make the map" message (`RMG_MAP_FAIL`), and the client doesn't crash. If it crashes anyway, before or after this line, note where: the static reading may have missed a second place that uses the template.
4. [x] **Shared overlay and modal input.** Move ImGui setup out of the player list into `overlay.cpp`. Add the window procedure subclass, our own small mouse and keyboard feed to ImGui (we don't add ImGui's Win32 backend, which would be a download), and the DirectInput mute while a modal panel is open.

   *Test:* the player list unchanged in a robot run. By hand (the robot can't click a background window): a test panel takes clicks and Esc, and the game behind it doesn't react. **Also by the owner on Linux (Proton).**

   **Done (2026-10-25)**, see `src/hook/README.md` ("The overlay"). What the build found:
   - The game draws **its own cursor** by default (`ui_use_hwcursor = 0`), kept at `0x00fe6da0`/`0x00fe6da4` by `0x00835ce0`, moved from Windows' cursor or from DirectInput's mouse axes. So the mute keeps DirectInput's mouse motion (the cursor must move over the panel) and holds back keys, buttons and the wheel; ImGui follows the game's cursor.
   - One set of DirectInput hooks (`dinput_hook.cpp`) now serves the robot's keyboard and the mute.
   - Safety nets: the mute holds only while the panel's screen is on top (checked every frame) and a frame was drawn in the last 2 s; Esc closes; keys held across the change are handled (`input_mute.cpp`, tested by `test/input_mute_test.cpp`).
   - The test panel: `[ui] overlay_test_panel=1`, F10. On the adventure map F10 is also the game's menu key (`input_a2.cfg`), so the panel closes at once there: test on the main menu or the random map screen.
   - Robot check: the player list on A1M4 with 8 test rows, old DLL vs new: same layout, header, rows and statuses; the text inside the pills is pixel-identical.
   - Still to do: the hand test on Windows and on Linux (Proton).
5. [x] **The native button and an empty panel.** Serve the changed MainPanel and the new button files; catch `oh_builder`; draw the panel's frame from the game's pictures.

   *Test:* robot screenshot with the button; the `ui|NUI::CRMGScreen oh_builder` step opens the panel; with a wrong file, the fallback button.

   **First part done (2026-10-25):** the empty panel (`src/mods/rmg_builder.cpp`, `[rmg] builder=1`): the screen's `MainBg`, its title, a Cancel button made of `Button_122_34_*`, over the main panel (649×508, centred, scaled with the height; the screen's window is `ALIGN_CENTER`). "Screen on top" works by the RTTI class `.?AVCRMGScreen@NUI@@`. For now **F11** opens it (robot step `key|F11`). Robot-checked at 1024×768 and 1920×1080: the panel covers the main panel exactly. The robot reaches the screen with the new step `rmg|` (the screen's observer ignores `msg|rmg_screen`, which has no sender; see `tools/robot/README.md`). **Rest done (2026-10-25):** the native "Template..." button (served UI files, only when the game's file is the one we know), `oh_builder`, the fallback button drawn by the panel otherwise (`src/mods/rmg_screen.cpp`). Robot-checked: the button on the screen at 1024×768 and 1920×1080, and `ui|NUI::CRMGScreen oh_builder` opens the panel. The fallback button is not robot-checked (it needs a changed game file): by hand, if ever.
6. [x] **The panel itself.** Every choice in the parity table, presets, the zone map, numbers and warnings, English and French (`[ui] language`).

   *Test:* by hand, the same choices on the page and in the game give the same name and the same numbers.

   **Done (2026-10-25)**, the owner's approved design (version 2 of the mockup: drop-down lists like the game's own, steppers inline, check boxes; nothing overflows). `src/mods/rmg_builder.cpp`, `rmg_builder_widgets.cpp`, `rmg_builder_data.cpp` (tested by `src/hook/test/rmg_builder_test.cpp`). Robot-checked filled at 1024×768 and 1920×1080 (French). Still by hand: the clicks, and the same choices on the page and in the game.
7. [x] **"Use template".** Set the game's size, levels and players, tick "ARMG Templates", refill and select. Also "Save as file", and remember the last choices.

   *Test:* robot: open, apply a preset (a robot step), Create, Save, then start the saved map; the generator log names our template.

   **Done (2026-10-25)**, robot-checked: every point of the test above passed ([Notes from steps 5 to 7](#notes-from-steps-5-to-7)). "Save as file" and `builder_last=` are checked by hand only (the robot's settings file is made fresh for each run).
8. **Release.** Docs (`src/mods/README.md`, `tools/rmg/README.md`, the install guide), and a full test by the owner's group: three PCs on GameRanger, a recipe map.

**Effort:**
- **Steps 0 to 3** give working recipe templates in multiplayer, even before the panel: 3 sessions.
- **Steps 4 to 7** give the panel: 3 to 4 sessions.
- **Step 8** is release and testing: 1 session.

## Notes from steps 1 and 2

Written on 2026-10-25, while building the C++ maker (`src/mods/rmg_maker.cpp`) and the recipe names (`src/mods/rmg_recipe.cpp`).

**Decisions.** They hold for every `X1` name, so they cannot change later:
- **The name in the game's list is the maker's name plus `*`.** Otherwise the served file is the maker's file for the same choices. So `OH-X1-321110nnxxxxxxxa` and the ready `OH-2v2v2-Lu-Normal.xdb` differ in one line only, `<Name>OH-2v2v2-Lu-Normal*</Name>`. The ready templates' file names never look like recipe names (one is made from the layout, the other from every choice): the table in [tools/rmg/README.md](../../tools/rmg/README.md) pairs them.
- **"X1 never changes" is enforced:** `tools/rmg/x1_fingerprints.txt` records what 302 `X1` names build; `build.ps1 -Test` fails if one changes. The DLL also checks itself at start: it builds three of those names and serves no recipe if one differs (a compiler or runtime surprise would otherwise give one PC different bytes).
- **The format is two tables** at the top of `rmg_recipe.cpp`, by the JavaScript choice names, so the page can copy them (the owner's answer 3).
- **A recipe name is always built from the name**, even when a file of that name is in `open-homm5\templates\` (skipped, with a log line) or in the game's data.
- **`templates=0` turns off both** the folder's files and the recipes. With `templates=1` the file system hooks now go in even when there is no template file, so a PC without files can still serve a joined game's recipe.
- **The list adds at most 64 recipes** (`recipes=` first, then those the game opened). Any recipe is still served when asked for.

**JavaScript behaviours kept as they are in C++** (the page and the DLL must agree):
1. `allyLink` and `startMines` are not checked: anything but "open" is a weak guard, anything but "woodore" is all six mines. The C++ maker does the same. Recipe names only hold the two words.
2. "Towns" is in the name when `borderTowns` or `teamTowns` is on, even when no zone can hold a town (0 border or 0 team zones).
3. A tier set to the overall level (for example `lootByDistance` "normal" with Normal loot) is not "Tiers": it builds the same template as "Same". So two recipe names build the same bytes, as do a size equal to the best fit and `a`.
4. `guardsByDistance` 0 only changes the paths that join neighbouring teams' start zones directly, when there are no border zones (they join two distance-0 zones; teammates' paths keep their fixed guard). The page has no control for it, and `X1` names can't hold it.
5. The guard table has a `teamBorder` strength (8) that the layout never uses.
6. Counts are not checked: a negative number of zones acts as 0. Recipe names only hold 0 to 3 (or 4).
7. A typed name's file name turns each run of characters other than A-Z, a-z, 0-9, `.` and `-` into one `_`. That gives the same on UTF-8 bytes (C++) as on UTF-16 (JavaScript); tested with accents.
8. Best fit compares \|log(tiles per zone / 5,800)\| between sizes. C's `log` and JavaScript's may differ in the last bit, but two sizes never tie: a tie needs side1 × side2 × (1 or 2) = 5,800 × zones, and 5,800 = 2³ × 5² × 29 while no side has the factor 29. Near ties are about 1e-6 apart or more, far above the last bit. So both pick the same size.

**One difference, for input that nobody can give:** JavaScript looks the loot and guard words up in plain objects, so words like `toString` or `constructor` pass its check and then build a template full of `NaN`. C++ refuses them as unknown words. The page, the ready list and recipe names never produce them.

**A bug in the JavaScript test, fixed:** the random generator of `test_template_maker.js` (`seed * 1103515245 + 12345`, then `% n`) barely changed its low bits. Of its 400 "random" cases, 399 were free-for-all games with poor loot, no underground and open ally paths. It is now xorshift32. Its 400 cases, and the parity test's 400 wider cases (every value a recipe can hold, every size), pass every check.

## Notes from step 3

Static reading of the reference exe on 2026-10-25 (the MMH5.5 exes have the same bytes at every place below). Nothing here has run in a game yet.

**Where the template's name is.**
- The map's `sRMGProps` (in `map.xdb`, and in `NDb::SAdvMapDesc` at `+0x254`) holds `RMGmap` (`+4`), `RMGversion` (`+8`), `RMGstartseed` (`+0xc`), `RMGguid` (`+0x10`, a string) and `InitialParams` (`+0x1c`). The template is `InitialParams.Template`, at `sRMGProps +0x28`: a database link, an NDb pointer and a flag.
- The game info that the host advertises (0x194 bytes) holds a copy of `sRMGProps` at `+0x108`, so its template link is at `+0x130`. The connector command (`CCreateRMGConnectorCmd`, 0x1a4 bytes) keeps a copy of the game info at `+0x10`, the connector (0x1b8 bytes) at `+0x14`.
- Every database object has its database id at `+0xc`: the path (a string), a flag, a hash and the path in a canonical case. The connector compares templates by that id (`0x00701670`).

**The link travels by name.** `0x004a6a50` serializes the link. Between PCs it writes the path, `/RMG/Templates/<file>.xdb#xpointer(/RMGTemplate)`. When it reads one, it asks the database for that object at once (`0x009b0f60`), and the database loads the file through the file system.
- So **the client's Open hook builds a recipe template as soon as the client reads the host's game info**, and registers the name. That is before the connector lists the templates: the "known wrinkle" above solves itself, as long as the database's loading goes through the file system's Open (the robot checks it).
- The connector hook (`0x00701530`, cell `0x00e38734`) registers it again from the game info copy, in case the database had the template already.
- **A missing template leaves the link empty.** `0x004a69d0` then returns 0, and the connector reads `[0 + 0xc]` (`0x007012be`): the crash of §2. The guard redirects that one call (`0x007012b1`): for an empty link, it returns a stand-in whose database id matches no template, so the connector takes its own way for "template not in the list" (code 1, `RMG_MAP_FAIL`). The connector reads nothing else of the stand-in on that way, and the guard checks 14 bytes around the call before it goes in.
- The guard is on even with `templates=0`: it is a crash fix for any missing template, ours or MMH5.5's.

**Automatic teams (`rmg_teams.cpp`).** The game setup's constructor (`0x00ca0240`) gets the map's `SAdvMapDesc`. We read the template link at `+0x27c` and remember the recipe's teams per setup. At the host's start (`0x0071feb0`), when `teams=` is empty and no two players share a team yet, players 1 to N get team 1, the next N team 2, and so on. The host sends its setup to every PC, so clients need nothing. `recipe_teams=0` turns it off.

## Robot results (2026-10-25)

All runs used the test copy (MMH5.5, `MMH55_Utility_64.exe`). The commands are in [tools/robot/README.md](../../tools/robot/README.md#checking-random-maps-from-recipe-templates-in-game-builder-steps-2-and-3).

**Step 2, single player, console, no file:**
- `rmg rmg/templates/oh-x1-22001gnnxxxxxxxm.xdb -players 4 -size 2 -underground 0 start`, in small letters, with no `recipes=`:
  - the log says `OH-X1-22001gnnxxxxxxxM.xdb built from its name: "OH-2v2-M-Normal*"`;
  - the generator says "template loaded" and builds 5 zones;
  - the map has 4 towns, one per player, and 2 AI turns play.
- **So the game's database loads a template we serve with no file** (step 0's main question).
- The same with `OH-X1-321110nnxxxxxxxa` (2v2v2, Large with underground):
  - it is built as `OH-2v2v2-Lu-Normal*`, 23,121 bytes, the recorded size;
  - the map has 6 towns, and 2 AI turns play.
- With `recipes=OH-X1-321110nnxxxxxxxa` and `templates_selftest=1`, the game's own listing of `RMG/Templates` has 104 templates: 94 of MMH5.5's, our 9 files and the 1 recipe.
- At start the log says `maker self-check ok`.

**Step 3, LAN** (host and client on this PC; the 2v2 map above saved with `tools/robot/save_rmg_map.py`; a computer player on slots 3 and 4):
- **The client rebuilds the map.**
  - The client builds the recipe when its lobby reads the host's game info (22.5 s, before it joins). It logs `joining a random map game made from the recipe OH-X1-22001gnnxxxxxxxM` at 32.6 s, and its listing has the recipe.
  - The game starts on both PCs, 2 AI turns play, and all 317 world checksums are equal (no desync).
  - The host never opens the template: the template link of a map read from its file is only loaded when used.
- **Automatic teams:** the first run did not set them. The host's map holds the link differently from a loaded template (below).
  - **Fixed:** the database id of a link read from a map file has the xpointer's name ("RMGTemplate") where a loaded template has its path. The path is in the id's key. `TemplateLinkReference` now takes whichever of the two names a file (unit-tested).
  - Then both PCs started with players 1+2 (the two humans) on team 0 and players 3+4 (computer) on team 1, with equal checksums.
- **The lobby arrows win:** with `lobby_teams=1`, the host pressed player 3's team arrow. The log says `teams already set ... keeping them`, and the arrows' teams were used on both PCs (0, 1, 1, 2).
- **`recipe_teams=0`:** every player on their own team, as before.
- **Client with `templates=0`:**
  - the client logs `the map's template is missing on this PC`: the database link really was empty there, as the static reading said;
  - the lobby shows the game's message (MMH5.5's text: "RMG Map failed to load ...") with OK and Cancel, and the client keeps running.
  - The crash itself was not reproduced, since the guard is always on. But the empty link at `0x007012b1` is exactly what the connector used to read.

**Not done by the robot:** the generator screen (step 0). Its button sends `rmg_screen` as a `UISSendGameMessage`, and the robot's `msg|rmg_screen` is ignored by both the single-player and the multiplayer screen. So these are still to check by hand or with a new robot step:
- that our templates show in "ARMG Templates", with the `*`;
- that Create and Save work from the screen;
- screenshots at 1024×768 and 1920×1080 for the mockup.

## Notes from steps 5 to 7

Written on 2026-10-25, while building the native button, the panel and "Use" (`src/mods/rmg_builder*.cpp`, `src/mods/rmg_screen.cpp`).

**The native button.** The screen's window is made of UI files from `data.pak`, read through `NVFS::CWinVFS` slot 7, the same Open that reads the paks (its entries are `CWinFileEntry` and `CZipFileEntry`), so one more hook on that slot serves changed files for any path:
- `UI/RMGScreen/MainPanel.(WindowSimpleShared).xdb` (1,559 bytes, CRLF, fingerprint `0x92c8eafe13c2abaa`) gets one more `<Item>` after the Save button. Any other file: no native button, and the panel draws its own at the same place.
- Our button is the game's Save button (`Save.(WindowMSButton).xdb`, fingerprint checked) with six replacements, each of a text found exactly once: the name, the tooltip, x 188 → 340, `Enabled` true (Save starts disabled until a map exists), the reaction, the label. Its reaction is Save's with `oh_builder` for `save`. The label and tooltip are UTF-16 text files like the game's (`Modèles...` / `Template...`).
- The command reaches the screen's `IProgrammedReactionsAndChecks` (at +0x174; its one slot, cell `0x00e4110c`, holds `0x0079a650`), which returns 0 for commands it does not know. Our wrapper takes `oh_builder`; the robot's `ui|` step goes through the same cell.

**"Use".** What the static reading said held, with one correction:
- The screen list's entry (`[0x00fe719c]`, the overlay's "screen on top") points to a **part** of the screen object, not its start: the option vector at +0x1c0 is relative to the start. `overlay::TopScreenObject` now subtracts the part's offset, read from its RTTI locator (+4). The first robot run failed on this (the log listed nothing), the second worked.
- The size, levels and players options are `CComboOption`s (vtable `0x00e41070`): slot 9 ticks the check box (one argument), the list control at +0x28, slot 6, chooses an item (one argument); their item lists are the screen's own (7 sizes, "Non"/"Oui", 2 to 8 players).
- After "change_option" (`0x00799500`), screen +0x324/+0x328 holds one 0x20-byte database id per fitting template, in the list's order; the recipe is found by its path. Choosing it and running "change_option" again keeps the choice (the same list: the combo's "replace list", `0x0079e900`, keeps the selection when the list did not change).
- The game then generates with our template: the generated and the saved map name `/RMG/Templates/OH-X1-221110nnxxxxxxxa.xdb`.

**Robot results** (`tools/robot/README.md`, "Checking the template builder"; three runs in all):
- The button: `rmg screen: the random map screen gets our "Template..." button (its main panel, 1641 bytes, with one more child)`; the screen shows "Modèles..." between Sauvegarder and Fermer.
- The panel, filled, French: `builder-filled-1024.png` and `builder-filled-1920.png` (kept with the session's screenshots, not in the repository). Nothing overflows; the longest French label ("Ville en plus par équipe", "Auto : Grande (176×176)") fits.
- `panel|preset 1` (2v2, Large with underground), `panel|use`: `size L with underground, 4 players, "ARMG Templates" ticked, OH-X1-221110nnxxxxxxxa chosen (14 of 19 that fit)`; the game's screen shows Grande (176x176), Oui, Quatre, ticked.
- Create: `rmg teams: setup ... is for a random map made from /RMG/Templates/OH-X1-221110nnxxxxxxxa.xdb`. Save: `Maps\MMH55_RMG_Map_010.h5m`, whose `map.xdb` names the same template.
- The saved map, started with `advmap /Maps/RMG/<guid>/map.xdb`: the same log line, and the computer players took their turns. (The robot's test maps were removed afterwards.)

**Still to check by hand** (the robot cannot click): the drop-down lists, arrows, check boxes, wheel and scroll bar; Esc; "Save as file"; that the game behind gets no click; the same choices on the page and in the game give the same name and numbers; all of it on Linux (Proton) too.

## Risks

- **Input under Wine/Proton.** The modal design keeps it simple, but ImGui drawing and the window procedure subclass haven't been checked on Linux yet (the player list hasn't either). That's why step 4 asks for an early Linux test.
- **Input left muted.** If the panel closes in an odd way (alt-tab, the device lost, a disconnect), input must come back. The mute is tied to "the panel is open and the random map screen is on top", and is checked every frame.
- **Client timing.** If the template path can't be read from the game info, the fallback is to register recipes in the List hook from the paths that our Open hook has seen. Step 0 decides.
- **The game info's size.** A longer path makes the advertised game info bigger. We keep paths at 40 characters; step 3 checks that it arrives whole.
- **Byte drift between the page and the DLL.** If the page and the DLL disagree, two players build different templates. The parity test runs on every build. The page will also show the recipe name, so a template made on the page works in the game with no file (Q3).
- **UI files served wrongly.** The changed MainPanel file is only served when the original's bytes are the ones we know; otherwise we fall back to the ImGui button.
- **Old DLLs and players without the DLL** crash on recipe templates, as on any template they lack. We handle this with the message, the docs, and "everyone updates together".
- **Not solved here:** teammates still don't start next to each other. That is the generator's zone placement, step 3 of [map-generator.md](map-generator.md).

## Questions for the owner

**Answered 2026-10-25:**
1. **Yes, automatic teams.** A setting, on by default, for recipe maps only; the lobby arrows still win.
2. **Favourites later.** This version has the presets and "remember my last choices" only.
3. **Yes, recipe names on the web page.** The page shows each template's recipe name, so a template made on the page works in the game by name alone.

The questions as asked:

1. **Teams from the template:** when the host starts a map made from a team recipe (for example 2v2v2), should the DLL set the teams to match (players 1+2, 3+4, 5+6) unless the lobby arrows changed them? It would be a setting; the proposal is to have it on for recipe maps only.
2. **Presets:** the page's 8 presets and "remember my last choices" are planned. Do you also want named favourites (save under a name, pick from a list) in this version, or later?
3. **The web page:** should it also show the recipe name (and the panel's "Save as file" become rare)? Then a template made on the page works in the game for everyone just by its name, with no file to share.
