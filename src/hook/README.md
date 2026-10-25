# Hook DLL (`dinput8.dll` proxy)

A DLL the game loads at start-up. It lets our code run inside the game, so improvements can ship before the decompilation is complete (the "hook track" in CLAUDE.md).

**What it does today:** forwards DirectInput to the real Windows DLL (so the game works normally). If the exe is a build we know, it also:

- logs every step of every AI turn with its timing to `open-homm5.log` next to the exe;
- applies our mods from `src/mods/`. Today that's one: **LAN link tuning** (`net_tuning.cpp`), modern network timings that make co-op AI turns about 4× faster. It's on by default; `open-homm5.ini` next to the exe can turn it off (`[net]`, `tuning=0`). See `src/mods/README.md`.

Robot mode (`robot*.cpp`, used by `tools/robot/`) only turns on when an `open-homm5-robot.ini` file sits next to the exe, i.e. in the test copy during robot runs.

## How it gets into the game

The game imports one function from `DINPUT8.dll` (`DirectInput8Create`). Windows looks for DLLs in the exe's own folder first, so a `dinput8.dll` placed next to the exe is loaded instead of the system one. Ours loads the real `dinput8.dll` from the Windows system folder and passes every call through.

It **adds** one file and replaces none, and it does not collide with MMH5.5 (which uses `55u.dll` in the `um.dll` import slot). It does not replace `d3d9.dll` either (on Linux, Proton/DXVK supply their own): to draw the player list over the game, `d3d9_hook.cpp` only replaces the exe's `Direct3DCreate9` import entry and entries of the Direct3D objects' vtables, whichever `d3d9.dll` made them.

## Drawing over the game (`d3d9_hook.cpp`)

The overlay (below) and the robot's screenshots share one set of Direct3D 9 hooks: `Direct3DCreate9` (the exe's import), `IDirect3D9::CreateDevice`, `IDirect3DDevice9::Present` and `Reset` (vtable entries). Each user registers a callback that runs before each frame is shown, in order: the overlay draws first, then the robot takes its screenshot, so the screenshots show the overlay. Windows' `d3d9.dll` gives **each device a vtable of its own** (on the heap): every new device's entries are checked and hooked, not only the first one's.

## The overlay (`overlay*.cpp`): our panels over the game

Every panel we draw over the game is a **client** of one shared overlay: the player list (`src/mods/player_list.cpp`), the template builder (`src/mods/rmg_builder.cpp`) and a test panel (`overlay_test_panel.cpp`). The overlay owns what they share:

- one **Dear ImGui** context. Dear ImGui v1.91.9b (MIT, <https://github.com/ocornut/imgui>, tag `v1.91.9b`) is in `third_party/imgui/`: its core files and its DirectX 9 renderer (`backends/imgui_impl_dx9.*`), unchanged, with its `LICENSE.txt`. Its Win32 backend is not used: it would change the mouse cursor and wants every window message. We feed it ourselves (below);
- the **fonts** (Tahoma Bold, else Segoe UI Bold, else Arial Bold; Latin and Cyrillic). A client asks for the sizes it needs each frame; the font atlas is made again only when a new size is asked for;
- the **pictures** from the game's data (`overlay_textures.cpp`, moved from the player list): `.dds` files read through the game's own file system (so MMH5.5's paks count), turned into Direct3D textures that survive a device reset;
- the **screen on top** (`overlay::TopScreen`: the game's screen list at `[0x00fe719c]`, its last entry, its RTTI class name), the panels' **language** (`[ui] language`) and our **keys** (F1..F12 read with `GetAsyncKeyState`, so the game still gets them);
- the Direct3D device's life: on a reset (fullscreen switch, alt-tab) ImGui's objects are released before and made again after; after 10 s with nothing drawn, everything of ours on the device goes.

Each frame, the overlay asks every client "do you draw now?", lets those that do load pictures and ask for fonts, then runs one ImGui frame in which they draw in the order they registered (the player list first). A client that crashes is caught, turned off and logged.

### Modal panels: the game gets no input (`overlay_input.cpp`, `input_mute.cpp`, `dinput_hook.cpp`)

A modal panel (the template builder, the test panel) takes the whole mouse and keyboard while it is open. **When no modal panel is open, everything passes through untouched** (the player list is display-only, as before).

How the game reads its input (static reading: the interface's event names in `0x00489550`, the cursor code `0x00835ce0`, the game's own `profiles/default_profile/input_a2.cfg` and `user_a2.cfg`; not yet checked in a running game, which is why both paths below are covered):

- **Mouse clicks and keys in menus** come from window messages (`win_left_button_down`, `win_key`, ... in `0x00489550`), **hotkeys** (Esc, F10, ...) from DirectInput 8.
- **The cursor is the game's own** by default (`ui_use_hwcursor = 0` in the default profile): the game keeps it in client pixels at `0x00fe6da0`/`0x00fe6da4`. It moves it either from Windows' cursor (`GetCursorPos`, `ScreenToClient`) or from DirectInput's mouse axes (`bind +cursor_x 'MOUSE_AXIS_X'`, 1.2 times the motion), and then puts Windows' cursor there (`SetCursorPos`).

What a modal panel does:

1. **Window messages.** The game window's procedure (class `"H5"`) is subclassed when a modal client is installed. While muted, its mouse and keyboard messages go into a small queue for ImGui (buttons, wheel, keys, characters) and are not given to the game. Alt+F4 still closes the game.
2. **DirectInput.** Our DLL is the `dinput8.dll` proxy, so `dinput_hook.cpp` wraps the keyboard's and mouse's `GetDeviceState` / `GetDeviceData` (vtable entries of the real DirectInput objects, one set of hooks shared with the robot's keyboard, `robot_input.cpp`). While muted, the game sees no key, no button and no wheel. **The mouse's motion still reaches the game**, so its cursor keeps moving over our panel; ImGui follows that same cursor (`0x00fe6da0`), so what the player sees under the cursor is what gets the click. If the game takes the mouse for itself (DirectInput exclusive mode: Windows then sends no mouse messages), the buttons for ImGui come from DirectInput instead.
3. **Esc** closes the panel (seen in the messages or in DirectInput).
4. **Keys held across the change** are handled (`input_mute.cpp`): a key the game saw go down before the panel opened still gets its release; a key pressed while the panel was open (Esc, for one) stays hidden from the game until it is released, also after the panel closed.

The safety nets, so that input always comes back:

- "muted" means: **a panel is open, its screen is on top, and a frame was drawn with it in the last 2 s**. The input hooks check the 2 s themselves, so input comes back even when no frame is drawn at all (alt-tab in fullscreen, a lost device, a panel that cannot draw);
- every frame, the panel **closes if its screen is no longer on top** (another screen, a message box, a lost connection), and after the 2 s without a frame;
- a client that crashes is turned off and its panel closes.

The rules of point 4 and of what is held back are pure code, tested without the game by `test/input_mute_test.cpp`.

**The test panel** (`[ui] overlay_test_panel=1`, testing only): F10 (or `[ui] overlay_test_key`) opens a small modal panel over any screen, with three buttons, a check box and a text line, showing the mouse position, the wheel, the keys down and where the input comes from. Each click is written to the log. On the adventure map, F10 is also the game's own menu key: the game sees the press before the panel opens, opens its menu, and the panel closes at once (its screen left the top). Test on another screen (main menu, random map screen) or choose another key.

## Which exes it hooks

On the first `DirectInput8Create` call (after start-up, when even the Steam-wrapped exe has decrypted its code) it checks the exe: load address, link timestamp, image size, a vtable entry and the first bytes of the AI turn driver. It installs hooks only for:

- vanilla ToE 3.1 (2008): the unwrapped reference exe or the Steam `H5_GameOLD.exe`;
- `MMH55_Utility.exe` / `MMH55_Utility_64.exe` (the co-op exe), which has the same code.

In any other exe (3.1j `MMH55(_64).exe`, the 2025 `H5_Game.exe`, …) it only forwards and logs "unknown build".

One hook goes in earlier, when the DLL is loaded (`hooks::InstallEarly`), if the exe can already be recognized then (not the Steam-wrapped one): the "original music" mod's file system hook, only when that mod is on. The game reads its heroes during its splash screen, before the first `DirectInput8Create` call.

If the log says "unknown build" or "could not hook" in `MMH55_Utility`, MMH5.5's own DLL (`55u.dll`) may have changed the same vtable entry or code at run time. Our DLL then stays out of the way instead of guessing. Tell me, and we'll look at what `55u.dll` does.

## The AI trace

The AI turn driver (`IPlayerAI` slot 0, `0x0086dd40`, see `docs/subsystems/ai-turn.md`) is a virtual method, so we hook it by replacing one pointer in `CPlayerAI`'s vtable (`0x00e4c8bc`). No code is patched. Each call produces one log line:

```
  123456 [ 4321] ai-step ai=0A1B2C30 flag=0 stage 3->4 calls=17 cmds=1 turn_age=850ms think=12.34ms gap=180.5ms
```

| Field | Meaning |
|---|---|
| first number | ms since the DLL loaded |
| `[4321]` | thread id |
| `ai=` | address of the `CPlayerAI` object (one per AI player) |
| `stage a->b` | the turn's state-machine stage before and after this call |
| `calls=` | the AI's own call counter |
| `cmds=` | commands in the output list after the call |
| `turn_age=` | ms since this AI's turn started (its own `GetTickCount()` stamp) |
| `think=` | how long this call took (ms): the AI's thinking time |
| `gap=` | time between this AI's previous call returning and this call starting (ms), i.e. the time the AI waited. **This is the number to look at in co-op**: if hypothesis H1 in `docs/subsystems/ai-turn.md` is right, it is about one network round trip after each command in multiplayer, and near zero in single player. |

## Build

Needs Visual Studio 2019 or 2022 (C++ build tools, x86). Already on the owner's PC.

```powershell
powershell -File src\hook\build.ps1 -Test
```

This builds `build\hook\dinput8.dll` and runs these tests outside the game:

- `test/proxy_test.cpp`: loads the DLL, calls `DirectInput8Create`, and checks that a real DirectInput object comes back.
- `test/ai_trace_test.cpp`: calls the AI hook exactly the way the game calls the AI (`__thiscall`, command list then flag) on a fake AI object, built with the compiler's runtime stack check (`/RTCs`). It checks that the arguments reach the original function unchanged and that the log line is right. A calling-convention mistake here would crash the game, so this test matters.
- `test/original_music_test.cpp`: the "original music" mod's text work (`src/mods/original_music_xdb.cpp`) on small made-up data files: which music lines change and which stay.
- `test/rmg_maker_test.cpp`: the C++ template maker (`src/mods/rmg_maker.cpp`) against the JavaScript one. First `node tools/rmg/test_template_maker.js --expected build/rmg_parity` writes what the page's maker builds for the 24 ready templates, 400 random choices and a few special ones; the test must build the same bytes, names, warnings and numbers, and refuse bad choices with the same message. Needs Node.js.
- `test/rmg_recipe_test.cpp`: recipe names (`src/mods/rmg_recipe.cpp`): choices → `OH-X1-...` → the same choices, every bad name and path refused, reading a template link the way the game keeps it (`src/mods/rmg_join.cpp`), the teams of a recipe map, and each ready template built from its recipe name, compared with the file in `src/mods/rmg/templates/`. It also checks that 302 recorded `X1` names still build what they built when recorded (`tools/rmg/x1_fingerprints.txt`): what an `X1` name builds must never change.
- `test/input_mute_test.cpp`: the modal panels' input rules (`input_mute.cpp`): what the game gets of its window messages and of DirectInput while a panel is open and just after (held keys, Alt+F4, the mouse's motion, buffered events).

The build runs in `src\hook` with relative source paths: with full paths, a deep folder (a git worktree) made the command line longer than `cmd.exe` accepts (8191 characters).

## Owner test (phase 0 gate: "DLL loads and logs AI turns; game runs normally")

Use a **separate copy** of the game, never the Steam folder (CLAUDE.md).

1. Copy `build\hook\dinput8.dll` into the copy's `bin\` folder, next to `MMH55_Utility_64.exe`.
2. Start a single-player skirmish **with `MMH55_Utility_64.exe`** (the 3.1j `MMH55_64.exe` is not hooked) against 1–2 AI players, and play 2–3 turns.
3. Check `bin\open-homm5.log`. It should say `game build: ToE 3.1 + MMH5.5 patches (MMH55_Utility)`, then `hooked AI turn driver`, then many `ai-step` lines during AI turns.
4. If anything goes wrong: delete `dinput8.dll` (or create an empty file `open-homm5.nohooks` in `bin\` to keep only forwarding), and send me the log.

On **Linux** (Wine/Proton) Wine prefers its own built-in `dinput8.dll`. Tell it to use ours first: Steam launch option `WINEDLLOVERRIDES="dinput8=n,b" %command%`, or in `winecfg` → Libraries → add `dinput8` as "native, builtin" (for GameRanger's Wine prefix).

## Files

| File | What |
|---|---|
| `dllmain.cpp` | DLL entry, the forwarded exports, lazy hook installation |
| `dinput8.def` | export names (same as the real `dinput8.dll`) |
| `game_build.cpp/.h` | which exe are we in? |
| `hooks.cpp/.h` | installs all hooks (honours `open-homm5.nohooks`); `InstallEarly` for the few that must be in before the game reads its data |
| `ai_trace.cpp` | the AI turn trace |
| `log.cpp/.h` | `open-homm5.log` writer |
| `d3d9_hook.cpp/.h` | the shared Direct3D 9 hooks (overlay, robot screenshots) |
| `dinput_hook.cpp/.h` | the shared DirectInput 8 hooks (the robot's keyboard, the modal panels' mute) |
| `overlay.cpp/.h`, `overlay_internal.h` | the overlay: clients, the ImGui frame, fonts, the screen on top, the panels' language, our keys |
| `overlay_input.cpp` | modal panels: the window procedure, the ImGui feed, the DirectInput mute, the safety nets |
| `input_mute.cpp/.h` | the rules of what is held back from the game while a modal panel is open (pure code, tested) |
| `overlay_textures.cpp` | pictures from the game's data, as textures |
| `overlay_test_panel.cpp` | the test panel (`[ui] overlay_test_panel=1`) |
| `third_party/imgui/` | Dear ImGui v1.91.9b (MIT): core + DirectX 9 renderer, for the overlay |
| `robot.cpp/.h`, `robot_endturn.cpp`, `robot_input.cpp` | robot mode for unattended test runs; only active when `open-homm5-robot.ini` is next to the exe (see `tools/robot/README.md`) |
| `test/proxy_test.cpp` | smoke test |
| `test/input_mute_test.cpp` | the modal input rules |
| `build.ps1` | build script |
