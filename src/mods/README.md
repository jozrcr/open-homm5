# Mods: our improvements to the game

Everything here changes how the original game behaves, on purpose. It is kept apart from the decompiled code (`src/decomp/`), which must stay byte-for-byte faithful. The mods are built into the hook DLL (`src/hook/`, `dinput8.dll`) and switched on and off with a settings file, `h5-together.ini`, next to the game exe. (Until 0.2.7 the add-on was called open-homm5, and its files `open-homm5.ini`, `open-homm5\templates` and so on: the installer and the DLL move them to the new names, see [src/hook/README.md](../hook/README.md#the-old-name-old_namescpp).)

**Contents**

- Faster co-op: [LAN link tuning](#lan-link-tuning-net_tuningcpp), [LAN games over VPNs](#lan-games-over-vpns-lan_discoverycpp-on-by-default), [faster co-op segments](#faster-co-op-segments-coop_guardcpp-on-when-every-player-agrees), [quicker lockstep](#quicker-lockstep-lockstepcpp-the-drain-and-the-trace-on-by-default), [one AI decision per frame](#one-ai-decision-per-frame-ai_pacingcpp-on-by-default), [quicker autosaves](#quicker-autosaves-autosavecpp-the-trace-and-level-1-on-by-default), [fast AI pathfinding](#fast-ai-pathfinding-fast_pathscpp-on-by-default)
- Interface: ["your turn" notification](#your-turn-notification-notifycpp-on-by-default), [player list](#player-list-player_listcpp-on-by-default), [hero looks](#hero-looks-hero_skinscpp-on-but-nothing-changes-until-a-look-is-chosen), [our panels in your language](#our-panels-in-your-language-srcmodstextstxt-srchooktextscpp), [French texts for the game](#one-language-french-texts-languagescpp-off-by-default)
- Random maps: [teams](#teams-on-random-maps-rmg_teamscpp-on-by-default-only-for-recipe-maps), [templates and recipe names](#random-map-templates-rmg_templatescpp-rmg_makercpp-rmg_recipecpp-on-by-default), [template builder](#template-builder-rmg_buildercpp-rmg_screencpp-on-by-default), [start terrain that matches the factions](#start-terrain-that-matches-the-factions-rmg_factionscpp-off-by-default)
- Rules: [game rules from the host](#game-rules-from-the-host-host_rulescpp-on-and-nothing-changes-unless-the-host-changes-something), with [H5 Together's version in the waiting room](#h5-togethers-version-in-the-waiting-room-030)
- [New version available](#new-version-available-update_checkcpp-off-by-default) (opt-in)
- Online play (being built, off by default): [Steam test](#steam-test-steam_spikecpp-off-by-default-for-tests-only), [virtual LAN](#virtual-lan-virtual_lancpp-off-by-default-being-built), [the online UI](#online-play-over-steam-the-ui-online_uicpp)
- [The settings menu](#the-settings-menu-settings_cpp-options-h5-together): the add-on's settings in the game's own Options screen (Options, H5 Together; F3)
- Camera, sound and visuals: [original music](#original-music-original_musiccpp-off-by-default), [camera zoom and start distance](#farther-camera-zoom-start-distance-cameracpp-off-by-default), [the mouse wheel under Wine](#the-mouse-wheel-under-wine--proton-wheelcpp-wheel_logiccpp-on-under-wine-only), [wider volume range](#wider-volume-range-audio_volumecpp-off-by-default), [black past the map's edge](#black-past-the-maps-edge-edge_skycpp-off-by-default), [the main menu's scene and music](#the-main-menus-scene-and-music-menu_scenecpp-off-by-default)
- The freeze detector (hang watchdog), the crash reports and chain-loading another `dinput8.dll` mod are part of the DLL itself: [src/hook/README.md](../hook/README.md#the-hang-watchdog-watchdogcpp).

## LAN link tuning (`net_tuning.cpp`)

**What it fixes:** slow AI turns in co-op (LAN and GameRanger games).

In a multiplayer game only the host computes the AI, and each AI move has to reach the other PCs before the next one can start. The game's network link was tuned for 2006 modems. It sends at most one small packet every 50 ms (about 5 KB/s), and a PC with nothing to send acknowledges only once per second. During AI turns the host's link is full, every PC thinks the others are lagging and slows itself down, and each AI move waits 1 to 2.5 seconds.

The mod changes three network timings on the PC that runs it:

| Setting | Game | Mod |
|---|---|---|
| `keepalive_ms`: how often an idle link sends (and so acknowledges) | 1000 | 10 |
| `send_interval_ms`: how often a link may send a packet | 50 | 2 |
| `batched_segments`: how many "segment done" reports an idle PC holds back | 3 | 0 |

Game commands still run in the same order on every PC; only the network gets faster. **Every player should run it**: each PC only speeds up what it sends.

**Measured** with the robot (two copies of the game on one PC, 2 human players plus 1 computer player, map Trèfle), seconds per AI move:

| | per AI move | typical AI turn |
|---|---|---|
| Game's timings | 2.5 s | 15–36 s |
| With the mod | 0.6 s | 2–7 s |

(Each change alone helps less: send interval only 2.2 s, keepalive only 1.6 s, both 0.9 s, all three 0.6 s.) Over the Internet the ping between the PCs comes on top.

**Settings** (`h5-together.ini` next to the game exe; without the file the mod is on with the values above):

```ini
[net]
tuning=1             ; 0 = the game's original timings
keepalive_ms=10
send_interval_ms=2
batched_segments=0
```

The DLL's log (`h5-together.log`) says what it changed, for example `net tuning: links may send every 2 ms (game: 50)`.

**Since 0.3.1** the defaults are 10 ms and 2 ms (50 and 10 before). In two-PC tests over Steam (a Windows host and a Linux laptop, 2026-11-04), the AI's big commands filled the host's link at 10 ms, and its segment reports waited behind them for up to 2.4 s; with 2 ms the reports arrived within 0.2 s, the host's average wait per segment went from 389 ms to 124 ms and the laptop's from 682 ms to 178 ms. A busy link now sends up to about 140 KB/s, an idle one a small packet every 10 ms.

**MMH5.5 RC19k** sets the send interval to 20 ms itself (`<TransmissionInterval>` in `MMH55Stats.xml`, applied by `55u.dll` at start-up). The mod also accepts that value and makes it the mod's own (2 ms); the log then says `(game: 50, MMH5.5: 20)`. Measured on RC19k (A1M4, robot LAN run): 0.46 s per AI step with MMH5.5's 20 ms, 0.26 s with 10 ms (RC19d with 10 ms: 0.26 s). `send_interval_ms=0` keeps MMH5.5's 20 ms.

## LAN games over VPNs (`lan_discovery.cpp`, on by default)

**What it fixes:** over a virtual network such as ZeroTier (for a friend on Linux, or without GameRanger), the host's game never shows up in the LAN list.

The game finds hosted games by broadcasting a small "any games here?" message about once a second. Windows and Linux send that broadcast out of the main network card only, so it never reaches a VPN. The mod also sends it into **every network adapter**, the VPN's included. For VPNs that carry no broadcasts at all (Tailscale, for example), list the host's address in `lan_peers`; the mod then also asks the host directly. The game's own packets are not changed.

The log says where the messages go, and where games were found and joined:

```
lan: game queries also go to 192.168.1.255 (Ethernet 3), 10.21.174.255 (ZeroTier One [...])
lan: game found at 10.21.174.28:8889 (826 bytes)
lan: join request to 10.21.174.28:8888
```

**Settings** (`h5-together.ini`):

```ini
[net]
lan_all_adapters=1   ; 0 = only where the game itself sends (the original)
lan_peers=           ; e.g. 10.21.174.28 (comma-separated): also ask these hosts directly
```

Every player who looks for games needs it (the host only answers). On a Windows host the VPN's network must be marked **Private**, or the game's firewall rule blocks it.

## Faster co-op segments (`coop_guard.cpp`, on when every player agrees)

**What it fixes:** in co-op every AI move waits a few "segments" of 200 ms, the steps in which all PCs move the game forward together. Shorter segments make AI moves faster.

**Why it needs agreement:** the segment length decides when things happen in the game world, and every PC runs that world itself. So all PCs must use the same length, or the game desyncs.

**How:**
1. At the start of each multiplayer game, every PC's DLL sends a small hello to the others with the segment length it wants. A PC without our DLL ignores it.
2. Ten seconds in (segment 50), each DLL checks that **every** other PC sent the same wish.
3. If so, they all switch together at that segment. If not (someone has no DLL, an older one, or other settings), nobody switches and the game keeps its own timing.

`h5-together.log` says which:

```
co-op timing: hello from player 1 (asks for 100 ms segments, protocol 1)
co-op timing: ON from segment 50: 100 ms segments (all 2 PCs agree)
```

**Measured** (robot LAN game, 2 humans + 1 AI, 8 AI turns, network tuning on):

| | game's 200 ms | 100 ms segments |
|---|---:|---:|
| per AI move | ~0.65 s | 0.53 s |
| AI hero move | ~0.84 s | 0.58 s |
| town swap | ~0.42 s | 0.34 s |

The world checksums stayed equal on both PCs (no desync). When the copies disagree, both stay OFF.

**Settings** (`h5-together.ini`):

```ini
[coop]
guard=1           ; 0 = take no part (no hello; never switch), as without the DLL
fast_segments=1   ; 0 = ask for the game's 200 ms, so nobody switches
segment_ms=50     ; 50 or 100 (50 by default since 0.2.0)
```

## Undo End Turn (`turn_undo*.cpp`; off by default, the host's choice)

**What it does:** with simultaneous turns, a player who pressed End Turn too early can take it back while another human still plays. A round button **"Resume my turn"** ("Reprendre mon tour") shows over the game's hourglass (the End Turn button, at the centre of the wheel at the bottom right): an arrow turning back around a small hourglass, its name at the left of the wheel for a few seconds and whenever the mouse is over it. A click gives the turn back on every PC at the same moment. Once the last other human has ended the turn, the computer players start and the button goes.

**Setting:** `[coop] undo_end_turn=1` (Options, H5 Together, the host's settings). Only the host's value counts, from the next game. Every PC needs a DLL that knows it (whatever its own setting); otherwise it stays off for that game.

**How:**
1. At the start of each multiplayer game, every PC's DLL sends a small hello; the host's says whether it allows the undo. At segment 20 every PC decides the same: on only if every PC sent its hello and the host allows it.
2. A click sends "undo player P" after this PC's next segment report R. Every PC, the sender too, takes it in at the start of segment R + 10, before that segment's commands, and checks there, on the same world, that it is still allowed. Then the player's own setters put "pressed End Turn" and its turn state back to 0 (the game's banner and player list follow by themselves).
3. The button is drawn by the overlay, in the game's button art; the overlay holds back the game's clicks under it (`overlay::BlockClicksIn`), so the click does not reach the map.

`h5-together.log` says what happened:

```
turn undo: on for this game (all 2 PCs know it, the host allows it)
turn undo: request sent for player 1 (after this PC's report 156; it counts at segment 166)
turn undo: segment 166: player 1 plays again (PC 0 asked; before: 1h:1/1 2h:0/0 3ai:1/0; after: 1h:0/0 2h:0/0 3ai:1/0)
```

How turns end in the game, and the robot runs: [docs/plans/turn-features.md](../../docs/plans/turn-features.md), part 1.

## Lighter animations when the map is slow (`anim_throttle.cpp`; on, each player's choice)

**What it does:** zoomed far out on a big map, most of each frame goes into animating the 3D models on the CPU (about 1,270 skeletons a frame on a 6-player map). With this on, when the adventure map drops below about 55 frames a second, each model moves on only 1 frame in 2 to 4 (the frame rate decides, every half second); above 80 it is every frame again. Robot, 2560x1440 zoomed out to 250%: 36 -> 48 frames a second. Battles and towns are not affected (few models). Display only: each player can choose.

**Setting:** `[camera] anim_saver=1` (the default; 0 = the game's own way. Options, H5 Together, camera group; restart). `[camera] anim_every=N` (hidden, testing): a fixed 1 in N; 0 = 1, 2, 3, 4 in turn, 20 s each, with the numbers in the log. Study and addresses: [docs/plans/map-drawing.md](../../docs/plans/map-drawing.md).

## Frame rate limit and vertical sync (`frame_limit.cpp`; 180 fps by default since 0.3.5, each player's choice)

**What it does:** the game draws as many frames as it can (about 400 a second in its menus), which only heats the PC. `[video] fps_limit=N` (30 to 240; Options, H5 Together, camera group; at once) waits at each frame until 1/N s has passed: a short sleep, then a busy wait for the last half millisecond, so the pace is even (robot: 60 -> every frame 16.6 ms; changed to 120 from the Options page, 8.3 ms at once). `[video] vsync` = `game` (the game's own, its hidden `gfx_vsync`), `1` (each frame at the screen's refresh: robot, 180 Hz screen, 180 frames a second) or `0` (never wait); from the next start.

**How:** the limit is the last frame callback of our Direct3D hook, just before the game's Present (after our overlay); the clock's ticks are 1 ms while it is on. The vertical sync is the presentation interval, set by a filter our hook calls just before the game makes or resets its device (`d3dhook::SetPresentParamsFilter`). With a limit, the lighter animations count "fast" from 95% of it (else a limit of 60 would keep them light). Display only.

## A bigger vertex pool (`vertex_pool.cpp`; on)

**What it does:** the game keeps the 3D models' vertices in a shared pool that it sized for its own zoom. Zoomed far out on a big map it overflows every other frame, throws everything away and builds every model again. This makes the pool 4 times as large (about 80 MB more video memory): robot, 2560x1440 zoomed out to 250%, 36 -> 49 frames a second (70 with lighter animations). The picture is the same.

**Setting:** `[camera] vertex_pool=4` (in the file only; 1 = the game's own size, up to 16; restart). `[camera] vb_trace=1` (hidden, research): why and how often the models are built again, every 10 s. Study: [docs/plans/map-drawing.md](../../docs/plans/map-drawing.md).

## The co-op features' agreement (`coop_features*.cpp`)

Some of our changes act on the game world, so every PC of a multiplayer game must use them, or none. At the start of each multiplayer game every PC's DLL sends one hello (next to the world checksums, as undo End Turn's): the features it knows and, from the host, the ones the host wants. At segment 20 every PC decides the same: a feature is on only if every PC knows it and the host wants it. These features act only inside the multiplayer segment runner, where every PC changes its world alike; never in single player. The log:

```
co-op features: this game (2 PCs): on: script_rhythm parallel_ai
```

## The computer bonus, also in a loaded game (`coop_features.cpp`, `ai_bonus`; since 0.3.4)

**What it does:** MMH5.5 gives the computer players extra gold and creatures every week (`H55_AICheatMode`, the F2 panel's "Computer bonus": Off, 40% ... 140%). Its scripts read their settings file only when a map starts, and a save keeps the values, so a loaded game used to keep the bonus it started with (the owner's 3-player game, 2026-11-05: "the AI army multiplies x2 overnight"). MMH5.5 reads the variable again at each new week. So at the co-op features' decision (segment 20, the same point of the game on every PC) every PC sets it to the host's value with the game's own "run a line of script" (`DoString`, the scripts' object slot 0 `0x00cab4a0`, at world `+0x1d0` `+0x0c`; it answers 0 when the line ran). The value is what the host's F2 panel shows: its choice, else its own MMH5.5 file's. It counts from the next week; what the computers already got stays.

**Travels** in the co-op features' hello (the wants byte's bits 4-6; a 0.3.3 PC reads only bits 0-1, does not know `ai_bonus`, so it stays off for everyone). The log, on every PC:

```
co-op features: the host's computer bonus: H55_AICheatMode = 0 (off) set in the game on every PC
```

and MMH5.5's console: `H5 Together: H55_AICheatMode = 0`. Robot (LAN A1M4, 2026-11-05/06): with the host's `ai_cheat=0` and with no choice (its file's 4), both consoles print the value, 449 world checks all equal.

## Endless simultaneous turns on the 2008 game (`endless_turns.cpp`; off, the host's choice; since 0.3.5)

**What it does:** with "simultaneous turns" ticked in the waiting room, the original game lets the humans play at the same time only until their heroes get close (or after a battle, a dimension door, a flight...); then turns go one after the other again. MMH5.5 ships Deflaktor's **PEST** ("Permanent and Endless Simultaneous Turns", v1.6), 8 small changes that keep simultaneous turns for the whole game (things that cannot happen at once, a battle of three players, are ignored); MMH5.5's `55u.dll` makes them at start-up. The 2008 game without MMH5.5 has no `55u.dll`, so this mod makes the same 8 changes, from PEST's public description (MMH5.5's repository, `assembly/patches/production/migrated/pest.yml`, "Original EXE 3.1"): `0x009c2660` (the check that ends them returns at once), `0x00af3664` (after a battle: 5 x nop), and `je` -> `jmp` at `0x0042e072`, `0x00865ae3`, `0x009984f7` (a computer player's turn and its end), `0x008659c5` (dimension door), `0x00b4b6d4` (angel wings, levitation), `0x0098f909` (ghost mode). Each of the last six follows the world's "are turns simultaneous?" (vtable `+0x1c`) and skips its "end them" (`+0x20`).

**Setting:** `[coop] endless_turns=1` (Options, H5 Together, the host's settings; the row shows only on the 2008 game). A co-op feature (`endless_turns`, bit 3 of the wants byte): on only when every PC knows it (a PC of the 2008 game with the 8 places as expected; with MMH5.5 its own PEST does it) and the host wants it. Switched on at the decision (segment 20) on every PC, off again at the next game's start.

**Checked** (robot, LAN, the 2008 game, simultaneous turns, 2026-11-06): a line of script at segment 300 puts player 2's hero next to player 1's (`[debug] script_at=300`, `script=...SetObjectPosition...`; the 2008 game's multiplayer maps have no map script, so with `script_at` set the world gets an empty one, `host_rules.cpp` `TestScriptsWanted`). The game's console prints each turn change (`GiveTurnToNextPlayers after: C C W`: players 1 and 2 play at once, the computer waits). Off (`20261106-151426-lan`): after the move, `C W W`, `W C W`, `W W C`: one after the other, as the original game does. On (`20261106-151726-lan`): `C C W` to the end. World checks all equal (2294 and 2293).

## More time for a PC that joins (`join_time.cpp`; `[net] join_timeout_s=300`; since 0.3.5)

**What it does:** the game gives up on a PC that sends nothing for `net_timeout` seconds (20): "timeout 0.18.0" on the joining PC. A PC that joins a random map game first builds the map itself (the game's way, `rmg_join.cpp`); a big map on a slower PC took longer than that (the owner's Linux friend, the 6-player Impossible recipe map, 2026-11-06). The game makes its players' network manager with `0x0047ef60` (the constructor `0x0047ee50` is called at `0x0047ef7b` with `{ net_timeout, 20.0f }`, kept at `+0x0c` / `+0x10`; vtable `0x00e131e4`); the wrapped call sets `+0x0c` to `join_timeout_s` while players join, and the first segment of the game (coop_features.cpp, every PC) puts the game's value back, so a PC that goes silent in a running game is dropped as fast as before. The game also makes a manager with 60 s at start-up (not `net_timeout`'s): left alone.

**Checked** (robot, LAN, `C:\Games\H5_test` with the 2008 exe, the recipe map saved with `save_rmg_map.py`): with the game's 20 s the join took 20.1 s (the host makes its manager when it starts hosting, the joining PC only after it built the map: the host's limit is the one that counts); with 90 s joined, back to 20 s at the start, and a killed client was dropped 20 s after the kill.

**Since test7 (2026-11-07):** 300 s by default, and every network manager made outside a game gets it (if its own limit is lower), not only the players' one: the game also makes one with its own 60 s each time the LAN screen opens, and a joining PC talks to the host through it until its map is built (robot: that one at 14 s, Join at 31 s, the players' manager only at 46 s). All get their own limits back at the game's first segment.

**The join stages (a safety margin; 2026-11-07).** The waiting room's logic gives a joining PC's join stages three console variables: `net_multiplayer_init_timeout` (10 s, read at `0x0071e24c`), `net_ubicom_init_timeout` (10 s, `0x0071e3aa`) and `net_load_timeout` (20 s, `0x0075386b`), handed to the stage timer `0x00710380`. The three reads are pointed at our own value, `join_timeout_s` (not the variables themselves: the game saves its variables into the player's profile when it exits). Under Wine the read of `app_always_active` (`0x00c3b3dc`) is also pointed at a 1, so a long map build does not leave the game inactive behind GNOME's "not responding" box (`[video] background`). Both were first taken for the cause of the Linux laptop's join failures; they were not (below), but they stay as margins for slow PCs.

**The Linux join: the cause (test9, 2026-11-07).** The owner's laptop (Ubuntu, Proton) failed 7 of 9 hand joins of the desktop's 6-player Impossible builder map (a 25-28 s build), whatever the time limits and focus, while Windows PCs joined, and the desktop could join the laptop. Each failure: the laptop's join request (`01`, 31 bytes) went out after the build, then four more over about 1.4 s (each asks for this PC's own address twice: "10 times"), and the error box 6 s later. The host answers a join request with `04` (33 bytes) **once** and ignores repeats, then streams its link to the joiner. The virtual LAN dropped any message the game read more than 3 s after it arrived ("stale"), and on Proton that happened often (stale drops even idle in the LAN screen; Windows clients had none), so the laptop's one `04` could be thrown away; a Linux host that drops a joiner's `01` gets four more, hence only Linux to Windows failed. Since test9 the virtual LAN never drops a message for its age (a socket gets what arrives after its bind, as on a real network; `virtual_lan.cpp`), and both PCs log the `01` / `04` exchange (`vlan: join:`). First hand join on test9: the `01` at 26.1 s after Join, the `04` read at once, the game started.

## The skill wheel (`skill_wheel*.cpp`; `[ui] skill_wheel=1`, on; since 0.3.5)

**What it does:** a panel with the hero's class's **skill wheel**: the 13 skills the class can be offered at a level up (most likely first, with their chance), and for each skill the abilities this class can take (the three standard ones, the class's own, the special ones, the ultimate with a small gold dot), in the game's own pictures and words. What the hero has is framed in gold (its skills' level shows with three dots and the level's name), what it can take at a level up now in a light frame, the rest is grey. The mouse over a picture tells what it does and what it needs first. The arrows by the title show the other classes' wheels. Opened by a small wheel button next to the Look button in the hero screen's header (the original game only: MMH5.5 has its own wheel there), or a key (`[ui] skill_wheel_key`, none by default; it opens the selected hero's on the adventure map). Read only.

**How:** the data are the game's tables, read through its own files (MMH5.5's when MMH5.5 runs): `GameMechanics/RefTables/Skills.xdb` (each skill or ability in the game's order, so its place is the game's skill number: `HERO_SKILL_LOGISTICS` = 1; its kind, its skill, its pictures and texts, and per class the abilities it needs first) and `HeroClass.xdb` (each class's skills and chances). A standard ability counts for every class (unless its needs name classes), a class or ultimate ability for its own class, a special one for the classes its needs name (`skill_wheel_logic.cpp`, tested by `test/skill_wheel_test.cpp`). The hero: the hero screen's (`+0x1b0`) or this PC's player's selected one (`hero_skins.cpp`, `skins::ShownHero`); its data file (`+0xd0`, path at `+0x20`) gives its class; its mastery of each skill number is the hero's own function, vtable slot `0x174` (`0x00b4d150`, the one the script command `HasHeroSkill`, `0x005c3ee0`, uses): 0..3 for a skill, 1 for an ability it has. The pictures (64x64 each) are freed when the panel closes or shows another class (`overlay::ReleaseGameTextures`; the overlay's picture cache now holds 256).

**Checked** (robot, the 2008 game, single player A1M4, the hero screen then `panel|skillwheel open`; 2026-11-07): Dougal (Knight) read as Offence 1, Training 1, Archery, as his file and Tribes of the East's rule (every hero starts with its racial skill) say; Ylthin (Ranger) Avenger 1, Light Magic 1, Master of Blessing; Grawl (Demon Lord) Advanced Destructive Magic shown with its level's name and two dots; the tooltip of "Maître du Feu"; the Look button hidden while the panel is open; closing and opening again reloads the pictures.

## Hero details in the waiting room (`lobby_hero_tips.cpp`; `[ui] hero_tips=1`, on; since 0.3.5)

**What it does:** in the multiplayer waiting room, the tooltip of a row's hero picture (hover, or hold the right button) says "Starting hero: <name>, <specialty>" (`UI/MPWait/PlayersList/Item/HeroToolTipFormat.txt`). With this, it also gives the hero's **class**, its **starting skills and abilities** (with their mastery) and **what its specialty does**, all read from the game's own data, so in the game's language and with MMH5.5's heroes when MMH5.5 runs. The two labels ("Class:", "Starting skills:") are ours (`[lobby]` texts).

**How:** a row (`NUI::CMPWaitItem`, vftable `0x00e39ce8`) keeps its tooltips' templates in five tables at `+0x108` (`hash_map<int, wstring>`, 0x18 bytes each); the hero's is key 0 of the table whose key 0 holds `<value=heroname>`. The row's refresh `0x00713fe0` (thiscall(row, int), ret 4; called at `0x00714a55` and `0x0077e524` on every change of the row) fills that template with the hero of the row (`+0x88`, a `CDBPtr<NDb::AdvMapHeroShared>`, read through `0x004a6740`) and gives it to the hero icon (`+0x98`). Both calls are wrapped: just before the game's refresh, the row's own template becomes the game's text plus our lines for that hero (`wstring::assign`, `0x0040e960`). The hero's file is the path in its database entry (`+0x20`: the id at `+0xc`, its type then its path, as the game setup's slots hold theirs at `+0x2c` / `+0x40`). The file gives `Class`, `Editable/skills` (mastery, skill), `Editable/perkIDs` and `SpecializationDescFileRef`; `GameMechanics/RefTables/HeroClass.xdb` and `Skills.xdb` give the names' text files (a skill has one name per mastery). Each hero's lines are made once per session.

**Not from the setup:** the game setup (`NWorld::CPlayersStartInfo`, the waiting room's logic `+0x74`) also names each slot's hero (`+0x40`), but on a PC that joins it could not be read the same way (robot); the row's own hero is the same on every PC.

**Checked** (robot, LAN, the 2008 game in French, 2026-11-06, `20261106-234101-lan`): the host picks Rutger (`Brem.(AdvMapHeroShared).xdb`); on both PCs the tooltip reads "Héros de départ : Rutger, Bourlingueur / Classe : Chevalier / Compétences de départ : Notions de Logistique, Voyageur" and the specialty's description; the joining PC's screenshot shows it.

## The 2008 game in a folder with MMH5.5 (`mmh55_files.cpp`; automatic; `[video] hd_textures`, off; since 0.3.5)

**What it does:** MMH5.5 installs into the game's folder: its data are archives in `data\` (`MMH55-*.pak`) and `UserMODs\` (`MMH55-*.h5u`), and its own exes load `55u.dll`, which changes the game's code to match them. The 2008 game's own exe (`H5_GameOLD.exe` after the GameRanger guide) does not load `55u.dll` but reads every archive of those folders: a broken mix. So when the 2008 exe starts (its header's date `0x48469f9f` and an import of `um.dll`, not `55u.dll`: readable at the DLL's load, also in the Steam-wrapped exe) in a folder with MMH5.5's archives, our `FindFirstFileA` / `FindNextFileA` import hooks skip their names (`mmh55_files_logic.h`) from the DLL's load on, before the game builds its file table (`NVFS::CWinVFS`, `0x009e08d0`). Nothing in the folder changes. `[video] hd_textures=1` keeps `MMH55-HDTex.pak` (terrain, town exteriors, terrain objects, sky domes; textures only, every file it points to is in it or in the 2008 data); each PC may choose (a LAN run with it on one PC only: 858 world checks, all equal).

**Its own profile and saves:** a save of one game crashes the other when loaded (robot: an MMH5.5 save in the 2008 game, an access violation at `0x00a49280`). So, while it hides MMH5.5's files (not in robot mode, unless `[debug] own_profile_test=1`), the game's `SHGetFolderPathA(CSIDL_PERSONAL)` answers `Documents\H5 Together\Original game`, made at the game's first question (not at the DLL's load: the shell must not run under the loader's lock). At the first start, the profiles' settings files are copied there (`Profiles\*.cfg` and each player's files, not `Saves`).

**Checked** (robot, `C:\Games\H5_test`, MMH5.5 RC19d, with the 2008 exe copied in as `H5_2008.exe`): 19 archives hidden (11 paks, 8 user mods); the game's hero list has 98 heroes, fingerprint `8177`, as in a folder without MMH5.5 (MMH5.5: 179); computer turns as usual; `data\index.bin` (the game's own cache) untouched. With its own profile: 0 saves in the load list (MMH5.5's 14 stay where they were), new saves in the new folder.

**Online:** the Steam game name (`virtual_lan_steam.cpp`) is `vanilla/...` for the 2008 game in any folder (the `55u.dll` hash counts only for MMH5.5's exe), so friends who start the original game see each other's games, and only those.

## MMH5.5's scripts at their own pace (`script_rhythm.cpp`; on, the host's choice)

**What it does:** map scripts (MMH5.5's above all) count their waits in "ticks" of the game's script clock, one per segment. MMH5.5 was made for the game's 200 ms segments; with our 50 ms co-op segments its main loop checked every hero four times as often (more work on every PC, a choppier map). With this, the script clock ticks once per 200 ms of game time, as in the original game: every script keeps its original pace.

**Setting:** `[coop] script_rhythm=1` (Options, H5 Together, the host's settings; on by default). Only the host's value counts, from the next game; every PC needs a DLL that knows it.

**How:** the world's update ends with the script clock's tick; during a multiplayer world update, the tick goes through only when the update's game time is in a new 200 ms step. Every PC runs the same updates with the same times, so every PC ticks the same. Measures and the addresses: [docs/plans/performance.md](../../docs/plans/performance.md), "The script clock".

## Computer players side by side (`parallel_ai.cpp`; off, the host's choice)

**What it does:** the computer players take their turns at the same time instead of one after another (robot: their part of a day about 40% shorter with two or three). They may decide differently than in the original game, so it is off by default. Multiplayer only.

**Setting:** `[ai] parallel_turns=1` (Options, H5 Together, the host's settings). Only the host's value counts, from the next game; every PC needs a DLL that knows it. How: [docs/plans/turn-features.md](../../docs/plans/turn-features.md), part 3.

## Following a friend's view (`spectate*.cpp`; on, each player's choice)

**What it does:** in a multiplayer game, a click on an ally's name in the player list makes your camera follow theirs on the adventure map, gliding, so you can watch what they do while you wait. An eye after their name shows that you follow them, and a short line after the eye says where they are when it is not the map ("in a town", "looking at a hero"...). Your camera goes to them at once, also when they are in a town or a battle (to the place where they went in). It stops when you click the name (or the eye) again, move the camera yourself, or your own turn begins. On their screen, an eye after your name shows that you follow them. Nothing is drawn in the middle of the map.

**Only allies** (the same team), and each player sees the map through their own fog: nothing of the game itself changes, so it cannot desync a game. While an ally follows you, your PC sends where your camera looks (a tile, a few times a second) as a small message next to the game's own world checks.

**Placing the army** (the battle's tactics phase): the game sends nothing of it until the player starts the battle (one command with every creature's place), so a watcher sees the creatures where they began, then at their new places when the battle starts. It is the same for every PC, also in a battle between players.

**Watching their battle.** While you follow an ally and they fight, a small button shows right after their eye in the player list: **"Watch the battle"** (also when you start following them in the middle of their battle). It opens the game's own battle screen on their battle, live (every PC runs every battle anyway: its actions are world commands), also when the battle has begun; leave with the small **"Stop watching"** button at the bottom right (or Esc) and come back as often as you like. The battle's own buttons at the top right (auto combat, options, the white flag) show greyed out, as the game greys the others when it is not your turn. At the end, their results box shows, and **"Back to the map"** closes it. You only watch: while the battle screen is open the game gets no left click and no key (a right click, the mouse's motion and the wheel do reach it, to see a creature's information and what lies under the cursor), and your PC sends no command at all (that battle screen would act as your ally: its own settings, auto-combat moves; all dropped). If your own battle starts meanwhile, watching stops at once.

**The sword** (since 0.3.5; the owner's wish, 2026-11-06: "a small sword icon on player list to indicate when in battle (maybe clickable to directly spectate)"). While an ally fights, their row in the player list shows a small sword (the game's own attack cursor, `Textures/Interface/Cursors/Attack.dds`) in place of their turn mark, whether you follow them or not. A click on the sword follows them and opens their battle at once (when you may watch it: your own battle does not wait, `watch_battles=1`). How: every PC that shares its view (`share_view=1`) also tells every PC when it is in a battle (the "status" message, again every 4 s while it fights, once when it leaves); a sword lapses 10 s after the last. 0.3.4 takes a status only from the ally it follows, so it ignores these. Robot (the 2008 game, LAN A1M4, the host attacks a monster; `20261107-000337-lan`, `20261107-001130-lan`): the client's list showed the sword 0.1 s after the host's battle began and dropped it when it ended; the sword's action (robot: `panel|spectate sword 1`, since the robot's clicks do not reach the list) followed the host and opened the battle 0.2 s later; 2893 world checks, all equal.

**The army's placement before a battle** (since 0.3.5; the owner, 2026-11-07: "I still don't see updates during battle preparation"). During placement ("Mode tactique") nothing is in the game's world yet: each PC's battle screen keeps its own list of where the stacks stand, and the fighter's clicks change only theirs; the game sends it at Enter. So a watcher saw the automatic placement until the battle began. Now the fighter's PC sends its list to the PCs that follow it (when it changes, and every 2 s), and the watcher's PC writes it into its own battle screen's list and redraws it as the game's auto-place button does. The world is never touched (no desync possible); at Enter the game's own commands place the real stacks where the watcher saw them. A merge or a split of stacks shows only at Enter. Both PCs need 0.3.5 (0.3.4 ignores the message). Robot (LAN, A1M4): the host moved a stack, the watcher's screen showed it 60 ms later; 1689 world checks, all equal.

**Staying after a defeat** (since 0.3.4; the owner's idea, 2026-11-05: "a player should be allowed to spectate instead of being insta kicked"). When you lose the game (your last hero and no town) while your friends play on, the game shows its results ("Mission failed") and, when you close them, takes you back to the lobby: your PC leaves the game. With this, closing them keeps you on the map: you play no more (the world gives you no turn), the player list shows who still plays, and you can follow your team (the eye) and watch their battles; the land you had explored stays visible, the rest is fog. Esc, Quit leaves as usual. How: the map screen's frame calls `0x005923c0` (at `0x0059e0db`) once the mission is over, which queues `NUI::CUnwindToMPLobby`; while this PC's player is out of the game and another human still plays, that call is skipped. Allies share their view with a teammate who lost. Robot (`[debug] script_at=300` and a `script=` line that removes player 2's heroes and towns on both copies; `20261106-005819-lan`): the client got "Echec de la mission", closed it, stayed, followed the host, watched the host's battle and came back; 3175 world checks, all equal. `[coop] stay_after_defeat=1` (each player's; 0 = back to the lobby as in the original game).

**Settings:** `[coop] share_view=1` (Options, H5 Together, interface): your allies may follow your view; 0 = your view stays yours (they then read "does not share their view" after your name). `[coop] watch_battles=1`: the "Watch the battle" button; 0 = no button. Each player chooses; both apply at once. The player list must be on (`[ui] player_list=1`): its names are where you click.

`h5-together.log`:

```
spectate: following Pierre's view (a click on the player list)
spectate: Pierre's view arrives (0.1 s after the request)
spectate: Pierre's screen: a battle
spectate: stopped following Pierre (this PC's turn began)
spectate: Joz follows this PC's view
spectate: Pierre is in a battle (their PC says)
spectate: watch Pierre's battle as soon as it can (a click on the sword in the player list)
spectate: this PC's army placement sent to the PCs that follow it (3 stack(s), 3 on the field)
spectate: Pierre's army placement shown here (1 stack(s) moved)
```

How the camera is found and moved, the messages, and the robot runs: [docs/plans/spectate.md](../../docs/plans/spectate.md).

## Quick resume after a crash (`quick_resume.cpp`, `resume_transfer*.cpp`; on, the host's choice)

**What it does:** when a player's game crashes (or loses its connection), the host's game drops that player after about 20 s and plays on without them: they count as defeated, and a save made after that cannot bring them back. With this, just before that defeat, the host has every PC save the game as **"Reprise"** ("Resume"), with the player still in it; a banner on the map says so. To play on together, everyone goes to Multiplayer, LAN, Create, Load, "Reprise".

**The friend who crashed** has no copy of that save, and the game lets a PC join a loaded game only with its own copy. With online play over Steam, the add-on sends it: the friend's add-on asks the hosts it knows whether they offer a resume save, fetches it if it lacks it, checks it and writes it as `h5together-reprise.sav` in its saves folder. The LAN screen says "Your friend's resume save is here: join their loaded game". (GameRanger or a real LAN: send the file by hand, any name in the Saves folder.)

**Setting:** `[coop] resume_save=1` (the host's). It changes nothing in the game itself: one more save, at the moment a player drops.

`h5-together.log`:

```
quick resume: PC 1 left the game (this PC is 0): the host saves before the game takes its player out
quick resume: the resume save sent to every PC 94 ms after PC 1 left, just before the defeat of player 2
resume transfer: this PC offers its resume save (sauvegarde_3.sav, 1143948 bytes, CRC 6d00dc41) to players who lack it
resume transfer: the host's resume save is here (1143948 bytes, checked), written as h5together-reprise.sav: ...
```

What the game does when a player drops, and the robot runs: [docs/plans/turn-features.md](../../docs/plans/turn-features.md), part 2.

## Quicker lockstep (`lockstep.cpp`; the drain and the trace on by default)

Why multiplayer clicks feel slow, and the measurements: [docs/plans/responsiveness.md](../../docs/plans/responsiveness.md). Each setting works on this PC only, so it needs no agreement between the PCs, and none of them changes what happens in the game world.

- **Drain:** the game's message pump (transceiver slot 3, 0x007ca7c0) takes in one "segment done" report per frame. With 4 PCs or a slow frame rate, that holds everyone back. The mod calls the pump again while it still takes in a report, up to two rounds of reports per frame.
- **View delay:** the game's "now" for the screen and the AI (slot 8, 0x007c8b00) is the timer minus 400 ms, so moves start at least 400 ms after their segment.
- **Catch-up (experiment):** the segment timer moves at most 150 ms per frame (0x00475d80), so a PC whose frame froze stays behind.
- **Pacing slope:** the game slows a PC that is ahead of the slowest one by 5% per segment of lead (slot 5, 0x007c8dd0: `speed = max(1 - 0.05 * lead, 0)`, the 0.05 a float at 0x00e12f18). That was made for 200 ms segments: with 50 ms segments, the same lead in milliseconds brakes 4 times harder, and a PC 1 s ahead stops. While co-op timing is ON, the mod points the instruction that reads it (`fmul` at 0x007c8ea2) at its own value, 0.05 × segment / 200 (0.0125 at 50 ms), and raises the lead's cap of 20 segments (0x007c8e7f, 0x007c8ef3; at the cap the speed is 0, which is what stops a PC from running further ahead) to 20 × 200 / segment (80). So the pacing is the game's per millisecond, and a PC stops 4 s ahead, as in the game. (The other `fmul`, at 0x007c8e64, is for a PC that only watches; it counts its lead in 200 ms units, so it stays as it is.) In the robot's games the host then made 17 to 20 segments a second, 20 being real time (without it: 15 to 18).
- **Finer clock:** Windows wakes sleeping threads only every 15.6 ms unless a program asks for more (`timeBeginPeriod`), and the game never does. While the drain or the trace is on, the DLL asks for 1 ms once. The game's network thread (0x0047cbd0) rests `Sleep(10)` between its rounds (`push 0x0a` at 0x0047cc05); `net_sleep_ms` makes that 2 ms, so the 10 ms send interval of the link tuning really is 10 ms.
- **Trace:** every 10 s, one log line with the wait after this PC's own report (average, p80 and longest), who reported last (count per player), frames per second, the drain's extra reports, and the spread of the players' segment counts. A second line times the commands this PC sent (clicks, and the AI's moves on the host), per kind: from sending to the run of their segment (average and longest), and to being shown on this PC (the run, or the margin after this PC's own report if that is later). It ends with a suggested `view_delay_ms`: the p80 of the waits, so that 4 moves in 5 start without a jump.

Example (robot, host, with the AI moving):

```
lockstep: 197 segments in 10 s, waited 117 ms on average (p80 208, longest 557) after this PC's own report; ...
lockstep: 31 commands from this PC, ms from sending to the run of their segment (and to being shown, margin 100 ms): CSetHeroAITargetCmd 8: 137 avg/239 max, shown 174; CSetWalkPathCmd 7: 122 avg/239 max, shown 165; ...; suggested view_delay_ms=208 (p80 of the waits 208 ms)
```

**Robot results** (2 copies on one PC; one run each, the AI decides differently each run):

| Setting | AI moves | AI hero moves | Checksums |
|---|---|---|---|
| Plain | 0.50 s | 0.61 s | all equal |
| Drain plus `view_delay_ms=200` | 0.44 s | 0.46 s | all equal |
| Drain plus `view_delay_ms=250` | 0.42 s | 0.57 s | all equal |
| Drain plus `catch_up_ms=600` | 0.45 s | 0.56 s | all equal |
| 0.2.0 defaults: 50 ms segments, view 100, catch-up 600 | 0.32 s | 0.30 s | 825, all equal |
| **Now:** plus pacing slope, finer clock, `net_sleep_ms=2` (two runs) | **0.27 s, 0.25 s** | **0.20 s, 0.26 s** | 895 and 788, all equal |
| The same with `scale_pacing=0` | 0.35 s | 0.39 s | 825, all equal |
| The same with `view_delay_ms=60` | 0.27 s | 0.23 s | 891, all equal |

```ini
[net]
drain_reports=1   ; 0 = the game's one report per frame
view_delay_ms=100 ; 50..400, only while co-op timing is ON (else the game's 400)
trace_lockstep=1  ; 0 = no summary lines
catch_up_ms=600   ; 150..1000, only while co-op timing is ON (the game: 150)
scale_pacing=1    ; 0 = the game's pacing slope also with co-op timing ON
catch_up_behind=1 ; 0 = a PC behind the others never runs faster (the game's rule)
net_sleep_ms=2    ; 1..10, the network thread's rest between rounds (the game: 10)
```

The robot passes them with `run_lan_test.py --net-setting KEY=VALUE`.

**A side effect, now fixed by `ai_pacing.cpp` (below):** with 50 ms segments and `catch_up_ms=600`, the game's AI manager runs several segments in one frame and calls the AI's turn driver in each of them, so on a slow frame the AI took a second (up to a sixth) decision in the same frame, before its first decision's commands had run. In the owner's game that froze, 17% of the AI's steps came that way; with the Oct 24 builds none did.

**Hardened:** the trace follows the players' report counts from one frame to the next; a count that went back or jumped by more than its 256-segment memory now starts the trace again, instead of a long loop (it never happened in the logs, but those counts are the game's memory).

## One AI decision per frame (`ai_pacing.cpp`; on by default)

**What it is for:** the game itself never lets its AI decide twice in one frame, or before its last moves have happened in the world: with the game's own timing (200 ms segments, the segment timer moving at most 150 ms per frame) the AI manager calls the AI at most once per frame, and the AI's commands run in a later frame's network pump. Our faster co-op timing broke that on slow frames (above), and it is the leading suspect for the owner's freeze of 2026-10-25 ([docs/subsystems/hangs.md](../../docs/subsystems/hangs.md)). This puts the guarantee back.

**What it does,** in multiplayer games (single player is untouched): the AI's turn driver is called at most once between two network pumps (once per frame), and after a call that produced commands, only once the segment holding them has run (no segment record numbered up to it is left in the transceiver's list after a pump). A call held back is simply not made; the AI gets it in a later frame. Only the host runs the AI and every PC applies the commands it sends, so this cannot desync. A safety valve lets the AI go on after 5 s of waiting (logged once; never seen). How: the two calls of the segment runner `0x007c98e0` in the pump (`0x007ca957`, `0x007ca97d`) go through us, after checking the runner's first bytes; the turn driver hook (`src/hook/ai_trace.cpp`) asks before each call. One line per AI turn: `ai pacing: this AI turn: N calls held for the next frame, M until the AI's own commands had run`.

```ini
[ai]
one_decision_per_frame=1  ; 0 = as 0.2.0 to 0.2.3 (several decisions per frame on slow frames)
```

**Robot results** (LAN, 2 copies, A1M4, 6 AI turns each; world checksums equal in every run):

| Run | Same-frame AI steps | AI time per step | Checksums |
|---|---:|---:|---|
| Slow host (every frame +60 ms, 10-13 frames/s), `view_delay_ms=50` (the owner's), **off** | 23 of 58 (40%) | 0.29 s | 976, all equal |
| The same, **on** | **0 of 67** (46 calls held a frame) | 0.42 s | 1181, all equal |
| Defaults (fast frames), **on** | 0 of 66 (none held) | 0.25 s (hero moves 199 ms) | 1085, all equal |

On a fast PC nothing changes (0.25 s per step, as the 0.2.2 robot runs); on a slow frame the AI waits one frame per decision, as the game does with its own timing.

## Quicker autosaves (`autosave.cpp`; the trace and level 1 on by default)

**What it is for:** in co-op games the autosave at the start of each round freezes every PC for a long time. Findings and addresses: [docs/subsystems/save.md](../../docs/subsystems/save.md).

**What the robot found:**
- **One PC asks, every PC saves.** Each round only the PC of the first living human asks for the autosave (a network command); every PC then writes its own file, and everyone waits for the slowest one.
- **The freeze is mostly the game looking through the saves folder.** To pick the name and the file, it reads the header of every multiplayer save in the folder: 4 times on the PC that asks, twice more on every PC. Each header read of a big-map save takes about 85 ms (it rebuilds that game's hero pool).
- With the owner's 17 old multiplayer saves in the folder, a round froze for **about 10 s** (7 s asking, 3.3 s saving). With only the 3 autosaves of the small robot map: 0.1 s and 0.15 s.
- **Without the mod:** moving old multiplayer saves out of your profile's `Saves` folder helps the same way.

**What the mod does:**
- **Trace** (`trace_autosave=1`): one log line per request and per save, with each phase, and how long each PC was frozen:
  ```
  autosave: day 3 (day start): player 0 asks every PC to save (request a6820c3c; finding the oldest autosave and sending took 6763 ms)
  autosave: day 3: player 1 saved "Sauvegarde auto 1" (save 1 this round; request a6820c3c from another PC): 3299 ms in all: 2 ms preparing, 3230 ms until the first stream (disk check, file name, header), 66 ms serializing and compressing (deflate 33 ms, level 1, 5000 KB -> 815 KB in 4 streams), ...
  autosave: player 1 closed no segment for 6661 ms around the save
  ```
- **Faster compression** (`compression=1`): saves use zlib level 1 instead of the game's 6. On the owner's biggest save, the game's own zlib takes 0.60 s instead of 1.75 s, for a file 24% bigger (13.8 MB instead of 11.2). Same format: the game loads them as before (robot: a level-1 autosave loaded and played on). Only saves change, nothing else the game compresses.
- **Fewer autosaves** (`autosave_every=N`): autosave only on days 1, 1+N, 1+2N... (7 = the first day of each week). The day is part of the game, so every PC with the mod skips the same days.
- **Quick look through the saves folder** (`fast_save_list=1`): while the autosave is requested or a multiplayer save runs, the game's reads of the other saves' headers skip the step that rebuilds each one's hero pool. That step only fills a list for the lobby's hero choice, which nothing in a save uses. The names, the choice of the oldest autosave, the file names and the new save's header stay exactly the same. Details and checks in save.md.

A save does not change the game, so none of this can desync: the robot's world checksums stayed equal in every run (2 copies, 6 AI turns each, with and without the owner's saves in the folder).

**Robot results** (A1M4, small map; more in save.md):

| | Request (host) | Each PC's save | Frozen per round | zlib part |
|---|---|---|---|---|
| game's level 6, 3 autosaves in the folder | 0.1 s | 0.16–0.23 s | (not measured) | 0.10 s |
| level 1, 3 autosaves | 0.1 s | 0.12–0.15 s | ~0.3 s | 0.03 s |
| level 1, the owner's 29 saves copied in, `fast_save_list=0` | 6.4–7.2 s | 3.2–3.9 s | ~10 s | 0.03 s |
| **the defaults**, the owner's 29 saves copied in | **0.07–0.08 s** (first: 0.8 s) | **0.11–0.13 s** (first: 0.9 s) | **~0.3 s** (first: ~1 s) | 0.03 s |

```ini
[save]
trace_autosave=1   ; 0 = no autosave lines in the log
compression=1      ; zlib level of saves, 0..9 (-1 = the game's 6)
autosave_every=1   ; autosave every N days (1 = every day, as the game)
fast_save_list=1   ; 0 = the game's full read of every save in the folder
```

The robot passes them with `--save-setting KEY=VALUE`; `run_lan_test.py --autosave` ticks the lobby's autosave option.

**Not built:** making only the host ask. The game already sends one request per round, from the PC of the first living human (not always the host): skipping that PC's request would lose the autosave.

## "Your turn" notification (`notify.cpp`, on by default)

**What it is for:** while the computer players move, the friends switch to another window and miss the moment they can play again.

**What it does:** when this PC's player can play again (the computer players are done and a new day starts, or in a game without simultaneous turns the turn passes to this player) **and the game is not the window in front**:
- the game's **taskbar button flashes** until you come back to the game;
- a **short sound** plays: Windows' "System Notification" sound where Windows has one (Windows 8 and later), otherwise a short chime the mod makes in memory (Wine has no Windows sounds, so on Linux you hear the chime);
- a **notification pop-up** appears from a small icon in the notification area ("Your turn (day 5).", in the player's language: [our texts](#our-panels-in-your-language-srcmodstextstxt-srchooktextscpp)), and the icon goes away when you are back in the game. Where there is no notification area (often under Wine/Proton), nothing shows and nothing breaks.

Not on the very first turn of a game (when it starts, or after loading a save): you are at the screen then. The log has one line per turn:

```
notify: your turn (day 1): the first turn of this game, no notification
notify: your turn (day 2): taskbar flash, the system notification sound, pop-up
notify: your turn (day 3): the game is in front, no notification
```

**How it knows:** every player of the game world has a turn state (0 = playing now, 1 = waiting). After each frame of the adventure map, the mod looks at this PC's player: a living human whose state just became 0 can play. The game finds "the player whose turn it is" the same way. Details and addresses: [docs/subsystems/ai-turn.md](../../docs/subsystems/ai-turn.md), "Whose turn it is". It only reads the game: nothing in the game changes, so it cannot desync.

**Checked with the robot** (A1M4, game windows in the background, 4 AI turns per run):

| Game | Notifications | World checksums |
|---|---|---|
| LAN, 2 humans + 1 AI, turns one after another | host: days 2, 3, 4, 5; client: days 2, 4, 5 (on day 3 the client's robot ended the turn in the same frame it began) | 615, all equal |
| LAN, simultaneous turns, sound and pop-up on | both: days 2, 3, 4, 5 (sound and pop-up shown, no error) | 333, all equal |
| single player | day 2 | (one PC) |

None on day 1. Not checked by the robot: the game window in front (it would take the focus from whoever uses the PC), and Linux.

**Settings** (`h5-together.ini`):

```ini
[notify]
turn=1    ; 0 = no notification at all
sound=1   ; 0 = no sound
popup=1   ; 0 = no pop-up
trace=0   ; testing: 1 = log every change of the players' turn states
```

The robot keeps robot runs quiet (`sound=0`, `popup=0`; the flash and the log lines stay); `--notify-setting KEY=VALUE` changes that.

## Player list (`player_list*.cpp`, on by default)

**What it is for:** in co-op games with simultaneous turns, everyone wants to know who still plays and who has pressed End Turn.

**What it shows:** a small panel on the adventure map's left edge, between the resource bar and the minimap (the owner's design):
- the day and how many humans are ready ("Jour 34 - 3/4 Prêts", or "Day 34 - 3/4 ready");
- the teams, each with a numbered round button and a thin line (none in a free-for-all);
- one pill per player: the hero's portrait (a little bigger than the pill, on its left end, like the game's round buttons on its bars) in a ring of the player's colour, the player's name, and the turn: **green dot** = playing, **tick** = turn ended, **three blinking dots** = the computer player is moving, **ring** = waiting.

It looks like the game's own interface: the pills are cut from the game's resource bar picture (its metal frame, read from the game's data), round things get the game's thin bronze rims, and the text is white with a dark shadow. If that picture cannot be read, the log says so and the panel uses plain dark capsules.

It only shows the adventure map (not in towns, battles or menus). **F9** hides it or shows it again. When the mouse is over it, it turns almost invisible, so it never hides the map. It never takes a click or a key from the game. Its words follow the game's language ([our texts](#our-panels-in-your-language-srcmodstextstxt-srchooktextscpp): English, French, or a language a player added).

**Where the data comes from:** the game world's players (turn order, name, colour, team, selected hero, "my turn now", "pressed End Turn"), the same on every PC; the hero's name and portrait from its game data. Details and addresses: [docs/subsystems/players.md](../../docs/subsystems/players.md). It only reads the game, so it cannot desync a game, and each player can turn it on or off.

**How it draws:** it is one client of the DLL's shared overlay (`src/hook/overlay.cpp`, see `src/hook/README.md`), with the template builder. The overlay sees the game's Direct3D 9 device (`src/hook/d3d9_hook.cpp`: the `Direct3DCreate9` import, then entries of the Direct3D objects' vtables; data patches only) and draws just before each frame is shown, with [Dear ImGui](https://github.com/ocornut/imgui) (`src/hook/third_party/imgui`, v1.91.9b, MIT); it also holds the fonts and the pictures read from the game's data. A device reset (fullscreen switch, alt-tab) is handled. It stays plain Direct3D 9 and Win32, so it should work under Wine/Proton (with their `d3d9.dll`), not checked there yet. The player list is not a modal panel: it never takes input.

**Checked with the robot** (A1M4, LAN: 2 humans + 1 computer player; screenshots show the panel, see `tools/robot/README.md`):

| Game | What the panel showed | World checksums |
|---|---|---|
| simultaneous turns, teams humans vs AI | both humans "playing", then a tick for each one who ended, then "thinking" for the computer; the same on the client | 501, all equal |
| turns one after another, teams humans vs AI | host playing / client waiting, then the other way round, then the computer | 711, all equal |
| simultaneous turns, free-for-all | no team buttons; "Ordinateur 1" for the nameless computer player | 357, all equal |
| single player, 2560×1080 | "Player 1", "AI 2", "AI 3" (the game's names), one at a time | (one PC) |
| simultaneous turns, teams, 1920×1080 | the same, 1.4 times bigger, as the game's own interface | 267, all equal |
| 8 rows (the client's `player_list_test_rows=8`: 4 teams of 2, the tallest list), 1024×768 and 1920×1080 | the list starts higher and gets a little smaller, and stays above the chat box | 259 and 254, all equal |

**Screen sizes:** the game's interface grows with the screen height (it is laid out for 768 lines), so the list does too: its sizes and its distance to the left edge are fractions of the height, and its fonts are made again at the new size. At 1920×1080 and 2560×1080 it keeps the same size next to the resource bar and the minimap as at 1024×768, and stays clear of the minimap and the chat box.

Not checked by the robot: F9 and the fade (the robot's windows are in the background), fullscreen, and Linux.

**Settings** (`h5-together.ini`):

```ini
[ui]
player_list=1          ; 0 = no player list (nothing is installed)
player_list_key=F9     ; F1..F12, or none
player_list_top=34     ; where it starts, % of the screen height
player_list_left=2     ; its left inset, % of the screen height
language=auto          ; every panel's words: auto (the game's language), or a texts file's code: en, fr
trace_players=0        ; testing: 1 = log each change of a player's turn
player_list_test_rows=0  ; testing: show N rows (copies of the real players, teams of two)
```

## Fast AI pathfinding (`fast_paths*.cpp`, on by default)

**What it fixes:** long AI thinking late in a game on big maps (several seconds per AI move).

Late in a game, nearly all of the AI's thinking time goes into searching paths for heroes (see [docs/subsystems/ai-pathfinder.md](../../docs/subsystems/ai-pathfinder.md)). The mod keeps every step of every search and makes the steps cheaper:

- **Remembered checks.** One search can look at the same map tile over a hundred times, and each time it repeats checks that only depend on the tile ("can the hero step there?", "is there something to visit?", "what does a step onto it cost?"). The mod remembers their answers until the search ends, asking the game's own functions the first time.
- **Faster open list.** The list of tiles still to look at is sorted with the same algorithm and the same order as the game's, written out again so that it compares in place instead of copying every tile it looks at. At start-up the mod tests it against the game's own code and keeps the game's if anything differs.
- **Ported search core.** The search loop, the node expansion and the "relax a neighbour" step are rewritten from the game's code, step by step, calling the game's own functions in the same order. Inside it, the list of tiles still to look at is kept in a compact form (same order as the game's) and given back to the game when the search stops.
- **Fast "go ashore?" check.** Half of the remaining time was one check that scans every object of the map floor three times per tile. The mod builds, once per AI step, tables of which object covers which tile (same order rules as the game) and answers from them.

Because every search takes exactly the same steps, **every path stays the same**. In co-op only the host runs the AI, so the mod only matters on the host's PC, and it cannot cause a desync.

One subtlety: the original AI thinks in real-time slices, so how fast it runs can change what it decides, just as the original game decides a bit differently on a faster PC. The robot checks the mod with a virtual clock that removes that effect: then the original and the mod make the same decisions over whole turns.

**Checked** with the robot on four of the owner's saves (`tools/robot/path_check.py`, 4 whole AI turns each, virtual clock): every big path search gave exactly the same result as the game's own, node by node (leaving out only padding and flag bits the game never reads), plus the order of reached tiles and what is left of the open list.

**Measured** (owner's day-50 save, huge map, 4 AI turns, 85 AI moves):

| | AI thinking |
|---|---:|
| without the mod | 93.3 s |
| remembered checks + faster open list + ported search core | 44.0 s |
| + fast "go ashore?" check | 22.7 s |
| + cheaper steps (per-tile records, compact open list, fewer copies) | **16.7 s** (x5.6) |

**Settings** (`h5-together.ini`):

```ini
[ai]
fast_paths=1         ; 0 = the game's own pathfinding (default 1 since 2026-10-24)
remember_checks=1    ; the parts, one by one (all default 1)
fast_queue=1
port_relax=1         ; the ported search core
port_search=1
port_expand=1
fast_ashore=1        ; the fast "go ashore?" check
ashore_check=0       ; 1 = also ask the game every time and log any difference (slow, for testing)
max_expansions=0     ; EXPERIMENT, CHANGES THE AI'S PATHS: expand a tile at most N times per search (0 = off)
```

`max_expansions` is not part of the exact speed-up: it is a first test of a faster search that may change the AI's paths (see the experiment in [ai-pathfinder.md](../../docs/subsystems/ai-pathfinder.md)). With 16 on the owner's day-50 save, AI thinking went from 16.7 s to 8.3 s; only the AI hero's giant long-range searches changed. On another save the AI then chose differently after a few moves. Off by default; not for real games until it has been tested much more.

## Teams on random maps (`rmg_teams.cpp`; on by default only for recipe maps)

Step 2 of the map generator plan ([docs/plans/map-generator.md](../../docs/plans/map-generator.md)).

- **Where teams live:** in the game setup (`NWorld::CPlayersStartInfo`), one slot per player with its team at +0x1c. A random map puts every player on their own team.
- **How it works:** when the host starts the game (0x007200da → 0x0071feb0), it sends its setup to every PC, and each PC creates its players from it (0x00c9fa80). The mod changes the teams in the host's setup just before that.
- **Results:**
  - Every PC gets the same teams, including friends without our DLL.
  - Only the host's setting counts.
  - Maps with teams of their own keep them.
  - A choice that would leave a single team is not applied.

```ini
[rmg]
teams=            ; empty: the map's teams. humans_vs_ai, pairs, or a list like 1,1,2,2,3,3
lobby_teams=0     ; 1 = the lobby's team arrows pick team numbers on maps without teams
trace_teams=1     ; log the teams each PC starts with
recipe_teams=1    ; maps made from a recipe template get the template's teams; the team arrows on them move a player to another start of their team
```

**Teams of recipe maps** (`recipe_teams=1`, on by default, host only; the owner's choice, 2026-10-25). A map made from a recipe template (`OH-X1-...`, see [Random map templates](#random-map-templates-rmg_templatescpp-rmg_makercpp-rmg_recipecpp-on-by-default)) knows its teams: with 3 teams of 2, players 1+2, 3+4 and 5+6 start in the zones that belong together. **They are set as soon as the game builds the setup** (single player and multiplayer), so the setup screen and the lobby show them from the start (the owner's request, 2026-10-25), and on these maps **the team arrows move a player to another start of their own team** (since 0.3.5; the owner's wish, 2026-11-06: "being able to switch position would be nice but no clue how to do it well without moving to other team"): the game's own move (`0x00c9e660`, to the next open start the slot may take, its team's starts first, found by `0x00c9d5b0`), only when that start is a teammate's; the team numbers belong to the starts and stay the recipe's. No open start in the team that way: nothing moves (log: `player 1 (team 1) stays: the next open start (player 4) is team 2's`). Robot (the 2008 game, LAN, the 6-player 2x3 builder map, host and client on team 1 with one open start; `20261107-010048-lan`): the host's right arrow moved it from start 1 to start 3, the next right arrow stayed (team 2's), the left arrow moved it back; the client's lobby showed the host on the green start. Before 0.3.5 the arrows changed the team numbers here, as with `lobby_teams=1`. `teams=` still wins (then the recipe's teams are not set). When the host starts, it keeps the teams it has. How: the setup constructor gets the map's description, whose `sRMGProps` (`+0x254`) names the template (`+0x28`); we remember the recipe's teams per setup, as for `lobby_teams`. **Robot test** (LAN, a 2v2 recipe map, 2 humans and 2 computer players): both PCs started with the humans on team 0 and the computer players on team 1, checksums equal; with a lobby arrow pressed (`lobby_teams=1`), the arrows' teams; with `recipe_teams=0`, one team per player. Again with the teams set when the setup is built (a map made with the builder): the host's and the client's lobby showed teams 1, 1, 2, 2 from the start, an arrow changed player 3's team without `lobby_teams`, and both PCs started with 1+2 against 3+4.

**Choosing teams in the lobby** (`lobby_teams=1`, host only). On maps without teams of their own, each row's team arrows move that player's team number up or down, instead of moving the player to another colour (the game's arrows). The pieces:
- **Which maps:** the setup constructor (0x00ca0240) tells whether the map has teams. We remember that per setup.
- **The rules:** for those maps, `CanChangeTeam` (vtable cell 0x00e92770) allows every playing slot, and `SetTeam` (cell 0x00e92774) changes the slot's team.
- **The other lobbies:** at 0x0071e67d, a 6-byte jump to a small patch makes the host send a row's team to them also when only the team changed. The game itself never changes a team without the colour.
- **At the start,** the teams are renumbered 0, 1, 2…
- **Clients** need nothing: the host decides and tells them. Lobby choices win over `teams=`.
- **Maps with teams of their own** keep the game's arrows.

**Robot tests** (LAN; the host has the settings, the client has none):
- `teams=humans_vs_ai`: both PCs started with the two humans on team 0 and the computer player on team 1. 352 checksums, all equal.
- `teams=1,2,2`: both PCs started with player 1 alone against players 2 and 3. 466 checksums, all equal.
- `lobby_teams=1`, on A1M4:
  - The host pressed its own team arrow right, and the computer player's left; the client pressed its own right.
  - Both PCs started with the host and the computer player together against the client. 544 checksums, all equal.
  - AI moves were slower (1.7 s), as the AI's ally can now see its heroes walk.
- `lobby_teams=1`, on A1M2 (a map with teams): the arrow moved the host to the other team's open seat, as in the game.

The robot sets these with `run_lan_test.py --host-rmg KEY=VALUE`, and presses a team arrow with the step `"38|arrow|<row> team left|right"`.

LAN robot games on random maps came later: the recipe maps' teams (above) and the faction terrain ([below](#start-terrain-that-matches-the-factions-rmg_factionscpp-off-by-default)).

## Random map templates (`rmg_templates.cpp`, `rmg_maker.cpp`, `rmg_recipe.cpp`, on by default)

Step 1 of the map generator plan ([docs/plans/map-generator.md](../../docs/plans/map-generator.md)), and steps 1 and 2 of the in-game builder plan ([docs/plans/in-game-generator.md](../../docs/plans/in-game-generator.md)).

- **How the game finds templates:** the random map screen asks the game's file system (`NVFS::CWinVFS`) for the files in `RMG/Templates` (List, vtable slot 10), then opens each one (Open, slot 7). The mod sits on those two slots. Nothing on disk changes.
- **Template files:** the `.xdb` files of `h5-together\templates\` next to the exe (the install package puts the 24 ready ones there) are added to the list and served when the game opens them.
- **Recipe names** (new): a template named `RMG/Templates/OH-X1-<16 characters>.xdb` needs no file at all. Its name holds every choice of the template maker, for example `OH-X1-321110nnxxxxxxxa` = 2v2v2, Normal, best size, underground. When the game opens such a name, the DLL builds the template's bytes from the name with `rmg_maker.cpp`, a C++ copy of the web page's template maker. The format: [tools/rmg/README.md](../../tools/rmg/README.md#recipe-names).
  - **Why:** in multiplayer every PC builds the random map itself from the template's path. With a recipe, every PC with the DLL has the template, and they all get the same bytes.
  - **The list** shows every recipe this PC was asked to open (for example a joined game's), plus those of `recipes=`. In the list, a recipe template's name is the maker's name plus `*`, for example `OH-2v2v2-Lu-Normal*`, so it can't be mixed up with the ready file of the same layout.
  - **Same bytes as the page:** `build.ps1 -Test` checks that `rmg_maker.cpp` writes exactly what `tools/rmg/template_maker.js` writes, on the 24 ready templates and 400 random choices.
  - **Same bytes forever:** what an `X1` name builds never changes (`tools/rmg/x1_fingerprints.txt` records 302 of them; the test fails on any change). And at start, the DLL builds three recorded names itself and serves no recipe if one differs (the log says `self-check FAILED`).
  - A file in `h5-together\templates\` whose name is a recipe is skipped (the log says so): the name decides.
- **Joining a random map game** (`rmg_join.cpp`, step 3). Each PC rebuilds the host's random map from the template's name, the seed and the options, which the host's game info carries.
  - When this PC reads a host's game info, the game loads the template by its name at once: for a recipe, the Open hook builds it and puts it in the list. Just before the game builds the map, a hook on the connector (`0x00701530`) registers the recipe again from the game info, in case.
  - **The guard:** a PC without the map's template used to crash there, because the game used the missing template without a check (`0x007012b1`). Now the game shows its own "can't make the map" message (`RMG_MAP_FAIL`). This works for any missing template, also with `templates=0`.
  - Players without our DLL still crash on templates they lack: everyone should update together.
- **Teams on recipe maps:** see [Teams on random maps](#teams-on-random-maps-rmg_teamscpp-on-by-default-only-for-recipe-maps) (`recipe_teams=1`).
- **The builder panel** (steps 4 to 7): [Template builder](#template-builder-rmg_buildercpp-rmg_screencpp-on-by-default).

```ini
[rmg]
templates=1     ; 0 = off: neither the folder's files nor recipe names
recipes=        ; recipe names to offer in the list, separated by commas (for tests; the builder adds its own)
```

The log says what was served, for example:

```
rmg templates: 24 template(s) from h5-together\templates: OH-2v2-Lu-Normal.xdb, ...
rmg templates: recipe names (RMG/Templates/OH-X1-....xdb) are built when the game asks for them (maker self-check ok); in the list from recipes=: OH-X1-321110nnxxxxxxxa
rmg templates: OH-X1-321110nnxxxxxxxa.xdb built from its name: "OH-2v2v2-Lu-Normal*", 6 players, 13 zones, Large (176x176, with underground), 23121 bytes
```

**Robot tests (2026-10-25):**
- A recipe map made with the console and no file: built, generated and played.
- In a LAN game, the client rebuilt the host's recipe map, and all world checksums were equal.
- A client without recipe support got the game's message instead of a crash.

Details: [the plan's robot results](../../docs/plans/in-game-generator.md#robot-results-2026-10-25). The generator screen's template list: checked with the builder (below).

## Template builder (`rmg_builder*.cpp`, `rmg_screen.cpp`, on by default)

The plan: [docs/plans/in-game-generator.md](../../docs/plans/in-game-generator.md) (build steps 4 to 7). Every choice of the web template maker, on the game's own random map screen (`NUI::CRMGScreen`, the same screen in single player and multiplayer).

**How a player uses it:**
1. The random map screen (single player: Custom game, then "Générer une carte"; multiplayer: the same screen from the create-game screen).
2. **Modèles... / Template...**, the new button between Save and Back (or **F11**). The builder covers the screen's main panel; the game behind is dimmed and gets no input.
3. Pick a ready template in **Partir d'un modèle / Start from**, or change the choices. The zone map, the numbers and the warnings follow at once.
4. **Utiliser / Use**: the panel closes and the game's own screen is set: map size, underground level, players, "ARMG Templates" ticked and our template chosen in its list (`OH-2v2v2-Lu-Normal*`). Also, ticked, so that nothing is left to chance: **no water** (our templates are land maps; unticked, the game makes an island map half the time), **resources and experience normal** (×1 on the template's treasure and chests; unticked, the game uses the list's first item, ×0.2), and the **monster level that leaves the template's guards as written** ("Faibles"/weak in MMH5.5, whose levels are ×1.0, 1.9, 2.7, 3.5, 7.0; unticked, the game draws one of the first three). The facts: [docs/subsystems/rmg.md](../../docs/subsystems/rmg.md#the-screens-own-options-and-the-template-static-reading-2026-10-25-robot-checked). Then the game's own options if wanted, **Créer / Create**, **Sauvegarder / Save**, and play the map. In multiplayer, the other PCs rebuild the template from its name ([Random map templates](#random-map-templates-rmg_templatescpp-rmg_makercpp-rmg_recipecpp-on-by-default)).
5. **Enregistrer / Save as file** writes the template as the web page's download gives it, to `h5-together\templates\<name>.xdb` (listed by the game from its next start; a different file of the same name gets `-2`, `-3`...). **Annuler / Cancel** or **Esc** changes nothing.

**The panel** (`rmg_builder.cpp`, drawn with the game's pictures by `rmg_builder_widgets.cpp`). Its parts sit where the screen's own parts are:
- **Left, in the minimap's box:** the zone map, drawn from the maker's own zones and paths (`rmg_builder_data.cpp`: the page's `positions()`, checked against Node). Start zones in their team's colour with the player's number, each team's treasure zones paler, neutral zones grey, the centre gold ("C"); thicker paths for stronger guards, dashed when open; a small gold mark for a town.
- **Under it:** players and zones, the name the game's list will show (with its `*`), the map size.
- **Right, scrolling in the options list's box** (the wheel, or drag in the game's scroll bar track): the ready templates; teams (steppers, the teammates' path, the start mines); neutral zones (steppers, two town boxes); loot and guards, and "by distance from the bases", which shows a small table (loot and guards per distance ring, with each ring's zone count); size ("Auto" shows the size it picks) and underground; then the maker's warnings.
- **Invalid choices are not offered:** each arrow asks the maker's own check (`rmg::CheckChoices`) first. An arrow that would make more than 8 players is greyed; clicking it shows the maker's message ("Le jeu permet 8 joueurs au plus (9 demandés).").
- **Words:** in the game's language when there is a texts file for it, English otherwise (`[ui] language`, [our texts](#our-panels-in-your-language-srcmodstextstxt-srchooktextscpp)); the maker's warnings too (each has an id and its numbers, `rmg::MessageId`). Every text is measured; one too long for its place is cut with "…" (none is today, at 1024×768 and 1920×1080).
- `builder_last=` in `h5-together.ini` remembers the last choices used or saved (a recipe name); the panel opens on them.

**The native button** (`rmg_screen.cpp`). The screen's window comes from UI files of the game's data. The DLL serves, through the game's file system (nothing on disk changes):
- `UI/RMGScreen/MainPanel.(WindowSimpleShared).xdb`: the game's file with one more child, our button. **Only when the file is the one we know** (its fingerprint); otherwise the builder draws a button of its own at the same place, and the log says why.
- `UI/RMGScreen/OpenHomm5Builder.(WindowMSButton).xdb`: the game's Save button, moved to x 340 (the free slot), enabled, with our label. `OpenHomm5Builder.(UISRunReaction).xdb` sends `oh_builder` instead of `save`. `OpenHomm5Builder.txt` and its tooltip are in the panel's language.
- The screen's command handler (`0x0079a650`, vtable cell `0x00e4110c`) is wrapped: `oh_builder` opens the panel; every other command goes to the game.

**"Use"** sets the screen's options as clicks would: the size, levels and players options are ticked and their item chosen (`NUI::CRMGScreen::CComboOption`, vtable `0x00e41070`); "ARMG Templates" (EP 6) is ticked; the screen's own "change_option" (`0x00799500`) refills its template list with the templates that fit; our recipe is chosen in it. If anything is not as expected, nothing more is changed, the panel stays open with a message, and the log says what it found.

```ini
[rmg]
builder=1           ; 0 = no panel and no button (nothing is installed); needs templates=1
builder_key=F11     ; the panel's key on the random map screen (none = no key)
builder_last=       ; written by the panel: the last choices used or saved

[ui]
language=auto          ; the panels' words: auto (the game's), or a texts file's code (en, fr...)
overlay_test_panel=0   ; testing only: 1 = F10 opens the overlay's test panel on any screen
```

The log says what happened, for example:

```
rmg screen: the random map screen gets our "Template..." button (its main panel, 1641 bytes, with one more child)
rmg screen: "oh_builder" (the Template... button)
rmg builder: panel opened (the Template... button): OH-X1-321110nnxxxxxxxa, OH-2v2v2-Lu-Normal
rmg builder: Use OH-X1-221110nnxxxxxxxa ("OH-2v2-Lu-Normal*"): size L with underground, 4 players, no water, monsters item 1 (x1.0), resources and experience normal (x1), "ARMG Templates" ticked, OH-X1-221110nnxxxxxxxa chosen (14 of 19 that fit)
rmg teams: setup 139D1380 is for a random map made from /RMG/Templates/OH-X1-221110nnxxxxxxxa.xdb#xpointer(/RMGTemplate)
```

**Robot tests (2026-10-25, the French test copy):** the native button on the screen; the panel opened by the button's command and by F11, filled, at 1024×768 and 1920×1080; a preset, then Use set the screen (Large, underground, 4 players, our template 14th of the 19 that fit); Create built the map from `OH-X1-221110nnxxxxxxxa`; Save wrote it to `Maps\`; the saved map started and its AI played. Commands: [tools/robot/README.md](../../tools/robot/README.md#checking-the-template-builder-a-modal-panel-of-the-overlay).

**Checking by hand** (the robot cannot click: its game runs in the background): the drop-down lists, the arrows, the check boxes, the wheel and the scroll bar; Save as file; Esc; and that the game behind gets no click. On Linux (Proton) too. With `overlay_test_panel=1`, the overlay's test panel shows what input arrives.

## Start terrain that matches the factions (`rmg_factions.cpp`, off by default)

Plan and research: [docs/plans/faction-terrain.md](../../docs/plans/faction-terrain.md). On a random map, the generator gives each start zone the faction its player had when the map was made. Factions picked later in the lobby change only the player (or a "Random towns" town), not the ground. With this mod, the map is rebuilt at the start with the lobby's factions.

- **The lobby (host only):** on random maps, every start slot may take any of the 8 factions. The host's setup decides every PC's arrows.
- **The hello:** in a random map's lobby, each client's DLL sends the host a waiting-room request of a type the game does not use (0x4f48). A host without the mod ignores it. A client says hello whatever its own setting: it can follow a host that rebuilds.
- **The host's start:**
  1. Factions left on "Random" get the faction the map was made with (through the game's own SetRace, slot 0x38).
  2. If a faction then differs and every human client said hello, the start message names another map, `Maps/RMG/<guid>-OH1-<one hex digit per player>/`.
  3. Otherwise the map stays as it is. On fixed-town maps, the changed factions go back to the map's.
- **Every PC** that sees such a name rebuilds the map, the way a joining client builds a random map: the original map's settings, the factions from the name, the game's own generator, written as another `Maps\<name>.N.h5m`. A client does it when it reads the start message (before the game looks the map up), the host when its world starts. If a package with that name already opens (the same game played before), it is used as it is.
- **If a rebuild fails** on a PC, that PC starts on the original map and logs `REBUILD FAILED` (its world will not match the others').

```ini
[rmg]
faction_terrain=0  ; 1 = as the host, rebuild random maps with the lobby's factions
trace_factions=0   ; research: lobby setups, the start, map builds, map files, faults
```

**Robot tests** (LAN, 2026-10-25; MMH5.5's `4P-Collar-L` map `MMH55_RMG_Map_002`, large with underground, "Random towns"; the host had the setting, the client had none):
- **A faction changed** (the host picked Haven where its zone was made for Inferno):
  - both PCs rebuilt the map: 9.2 s on the host, then 8.7 s on the client (the robot's two copies share a folder, so they take turns; real PCs build at the same time);
  - the two packages were byte for byte the same;
  - the host's start zone went from Inferno ground (`Dead_Land`) with lava and dirt decorations to Haven ground (`Dark_Grass`) with grass decorations;
  - **504 world checksums, all equal.**
- **Another change** (the client picked Inferno where its zone was made for Dungeon): 507 checksums, all equal. That zone is underground, and underground zones get cave ground whatever the faction, so its ground did not change; its town did.
- **No faction changed** (setting on): no rebuild; the game started on the map as it was. 508 checksums, all equal.

**Not done yet:**
- **The lobby unlock on fixed-town maps** (every slot locked to one faction) is not robot-tested: the only such saved maps use recipe templates, which the DLL did not serve yet when this mod was tested.
- **Linux (Proton).**
- **A line on the loading screen** while the map is rebuilt: the screen just waits a few seconds.

**Each rebuild leaves a map package in the game's `Maps` folder** (`Maps\<name>.N.h5m`), like each join of a random map in the game itself. **It is hidden from the game's map lists** (on every PC with the DLL, whatever its settings): only the map it was rebuilt from shows. The lists (multiplayer, single player, random map screen) get their maps from `0x007c2ce0`, every `map-tag` resource under a folder from the game's resource index; the mod takes out those under `Maps/RMG/<guid>-OH1-<8 hex digits>/`. Log: `rmg factions: map list of ...: 1 rebuilt map(s) (-OH1-) hidden, 23 shown`.
- **The packages are not deleted:** a save of such a game names its map's path, and reading a save's setup needs the map's description (`0x00ca0c70` → the setup constructor). The start and the saves find the package by that exact path, which the lists do not touch.
- **Robot check** (LAN, 2026-11-01, `MMH55_Utility_64.exe`, map `MMH55_RMG_Map_002`, the client picked Haven where its zone was made for Dungeon): the host's map list log said `map list of Maps/RMG: 2 rebuilt map(s) (-OH1-) hidden, 55 shown`, and none of the 95 maps of the multiplayer list was an `-OH1-` one; both PCs rebuilt the map (`-OH1-838A0000`) and started on it; **496 world checksums, all equal.**
- So do not delete them by hand while you keep saves of those games. (A save's setup is read by the same code as the start message, `0x005a1430` → `0x00ca0c70`, where the mod rebuilds a missing rebuilt map; not tested with a save.)

## Original music (`original_music*.cpp`, off by default)

**What it is for:** MMH5.5 brings its own music (new tracks for terrains, the main menu, the credits, battles and hero themes). With this switch on, the game plays the **original Heroes V music** wherever MMH5.5 changed it. Off (the default), you hear MMH5.5's music, exactly as without the add-on.

Nothing on disk changes. The game finds its music through small data files in the `.pak` files (see [docs/subsystems/audio.md](../../docs/subsystems/audio.md)): one "music definition" per track, and map tiles, heroes and a faction table that point at them. When the game reads one of those files from MMH5.5's paks, the mod reads the same file from the original game's `data.pak` and gives the game the original music in it:

- **music definitions and the faction music table**: the original file (main menu, credits, the 5 terrain tracks MMH5.5 replaced, the battle music lists);
- **map tiles and heroes**: only their music line goes back to the original's. Everything else in those files (MMH5.5's hero stats, ...) stays MMH5.5's.

So the original choice is made tile by tile: on flowers you hear the original grass music, on a bog the original water music, on roads the terrain's music (MMH5.5 removed it there), even where MMH5.5 plays the same new track on all of them.

**Where the original game has no music** (heroes and tiles only MMH5.5 has, and hero themes MMH5.5 gave to heroes that had none): no hero theme, no tile music. The game then picks, as it always does, among the other music of that place: the terrain under the hero and the map's own music. MMH5.5's new tracks are never played.

The log (`h5-together.log`) says what was set back, for example:

```
original music: on; mods: MMH55-Data.pak, MMH55-Index.pak, ... (16951 data files, 3028 of them also in the original game); indexed in 32 ms; ...
original music: [5] MapObjects/Haven/Nicolai.(AdvMapHeroShared).xdb: AdventureMusic AdvMap-Heroes/Nicolai.xdb -> none
original music: [26] Sounds/_(Music)/MainMenu/a2-MainMenu.xdb: MMH5.5 track ..\Music\MMH55\MainMenu_Dynasty_002.ogg -> original ..\Music\a2\Main_Menu.ogg
original music: [40] MapObjects/_(AdvMapTile)/Grass/Flowers.xdb: AdventureMusic AdvMap-Terrain/Flowers.xdb -> AdvMap-Terrain/Grass.xdb
```

**Checked** with the robot (switch off, then on, `log_music=1` lists the tracks that start):

| Where | Off (MMH5.5) | On |
|---|---|---|
| main menu | `MMH55\MainMenu_Dynasty_002.ogg` | `a2\Main_Menu.ogg` |
| credits | `MMH55\MainMenu_Credits_002.ogg` | `Credits\Final-Credits.ogg` |
| map A1M4, hero on a stone road | `MMH55\Adventure_Terrain_SummerPlains_001.ogg` | `AdvMap-Terrain\Terrain-Grass.ogg` |
| map A2M1, hero on grass | `MMH55\Adventure_Terrain_SummerPlains_002.ogg` | `AdvMap-Terrain\Terrain-Grass.ogg` |
| random maps (other starts each time) | `MMH55\Adventure_Terrain_ElvenForest_001.ogg`, ... | `Terrain-Dirt.ogg`, `Terrain-Sand.ogg`, `a2\2ndCampaign_Stronghold.ogg`, ... |

In a LAN game with the switch on for the host and off for the client, the world checksums stayed equal (no desync): music is only sound, so each player can choose.

Only mods in the game's `data\*.pak` files are considered (not `UserMODS\`). It works with any MMH5.5 version: the original files are read when the game starts (~40 ms).

**Settings** (`h5-together.ini`):

```ini
[audio]
original_music=0   ; 1 = the original Heroes V music where MMH5.5 changed it
log_music=0        ; testing: 1 = log every music file the game starts
log_data_files=0   ; testing: log the first N files the game reads from its data
```

## Our panels in your language (`src/mods/texts/*.txt`, `src/hook/texts*.cpp`)

**What it is:** every word of our panels and messages comes from one text file per language: the player list, the "your turn" pop-up, the game rules panel and its lobby tooltips, the hero looks panel and its button, the template builder with its "Template..." button and the maker's warnings. That is 201 texts. English (`en.txt`) and French (`fr.txt`) come with the add-on, in `bin\h5-together\texts\`; a player adds a language by putting a file there ([TRANSLATING.md](texts/TRANSLATING.md), the steps for translators). This is the add-on's own words only: the game's texts are the next section's business, and the log stays English (it is for us).

**Safe by design:**
- English is built into the DLL (`build.ps1` turns `src/mods/texts/en.txt` into `build\hook\texts_en.inc`), so the panels work with no file at all.
- A file is read line by line: a line it lacks, a broken line (no `=`, before any `[section]`, not UTF-8, a key twice) and a line whose `{placeholders}` or game tags (`<color=yellow>`, `<br>`) differ from the English line show in English. The log lists each one with its line number.
- Placeholders are names, not C's `%s`/`%d`: `Computer {n}`. A translator's mistake can only show wrong words, never crash the game (a broken `%s` could).
- Accepted: UTF-8 with or without its mark, Notepad's "Unicode" (UTF-16 LE with its mark), CRLF or LF.

**The format** (`src/mods/texts/en.txt`):

```
# a comment
[players]
day = Day {n}
ready = ready
computer = Computer {n}
```

`[section]` + `key` gives the key the code asks for (`players.day`); `\n` is a new line. In the code a text is `texts::Get("rules.title")`, `texts::Format("players.computer", {{"n", "3"}})`, or a panel's table of keys (`texts::Table(kKeys)`, a struct of `const char *` written with keys: the panels' drawing code reads `w.title` as before). Sections: `[language]` (the language's own name, and the game's name of the red player in it), `[players]`, `[notify]`, `[rules]`, `[bonus]`, `[looks]`, `[builder]`, `[update]`, `[versions]`, `[online]`, `[settings]`, `[welcome]`.

**Which language** (`[ui] language=auto`, the default), the first clue that answers:

1. `[lang] texts=fr` (the game's texts in French, next section): French;
2. **Steam's language for the game:** the `language` line of `steamapps\appmanifest_15370.acf`, Steam's own file for the game (15370 is Tribes of the East's Steam id), two folders above the game folder. Used only when its `installdir` is this game folder (not for a copy of the game elsewhere). Steam's names map to codes: french to fr, german to de, russian to ru, polish to pl...;
3. **the game's own texts:** the red player's name (`Text/Game/Player/Colors/red_player.txt`, "Joueur rouge" in a French game), compared with the `red_player` line of each texts file. A name no file has means a language we have no file for: English;
4. Windows' language (only when the game's texts cannot be read yet);
5. English.

`[ui] language=<code>` chooses directly (any 2 or 3 letter code, maybe with `-xx`: `de`, `pt-br`). A language without a file shows in English. The language is chosen once, at the first text a panel asks for; the settings menu (Options, H5 Together) sets it for the next start (switching at once would leave old text pointers in other code: `texts::Get` promises they stay valid). `overlay::PanelLanguage()` still answers "French or English" for code that needs only that.

**The log** says what was chosen and why:

```
texts: fr, from Steam's language for the game (french, appmanifest_15370.acf): fr.txt, 201 of 201 texts (the others in English)
texts: fr, from the game's texts (the red player is "Joueur rouge"): fr.txt, 201 of 201 texts (the others in English)
texts: English, from [ui] language=en (built in: 201 texts)
texts: de, from [ui] language=de; but there is no de.txt in h5-together\texts: English
texts: fr.txt: 2 line(s) not used (they show in English):
texts:   line 40: no "=" (a line is "key = text"): left out
texts:   players.computer: its {names} and <tags> (none) are not the English ones ({n}): English is used
```

**Fonts:** the panels' font (Tahoma Bold, Segoe UI Bold or Arial Bold, `src/hook/overlay.cpp`) is loaded with Latin-1, Latin Extended-A, Cyrillic and the general punctuation: English, French, German, Spanish, Italian, Portuguese, Polish, Czech, Hungarian, Turkish, Russian, Ukrainian and Bulgarian are covered. Not covered yet (a letter outside the ranges shows as "?"): Greek, Romanian's ș and ț (Latin Extended-B), Vietnamese, and Chinese, Japanese and Korean (those need another font and a much bigger letter sheet). Adding a range is one line in `AddFont` when a translator comes.

**Checks:**
- `test/texts_test.cpp` (in `build.ps1 -Test`): the reader (sections, comments, the mark, CRLF/LF, UTF-16, every kind of broken line), the placeholders, the check against English and the fallback, the language clues (Steam's file, Windows' ids), and the shipped files: `en.txt` reads cleanly and is byte for byte the built-in English, `fr.txt` fits it and has every text.
- `tools/lang/ui_texts.py check` (in `build.ps1 -Test` too): every key the code uses is in `en.txt`, every key of `en.txt` is used, the translations have no other keys and the same `{names}` and `<tags>`. `ui_texts.py new de` writes a starter file, `ui_texts.py long` the long test language `xx` (every text 40% longer, with accents) for robot screenshots of what gets cut.
- `test/rmg_builder_test.cpp`: each message of the template maker has an id; in English the panel says exactly what the maker says, in French its own words with the numbers.
- The robot copies `src/mods/texts/*.txt` into the test copy with the DLL (`--texts-dir` for other files: a partial translation, the long language).

**Settings** (`h5-together.ini`):

```ini
[ui]
language=auto   ; auto (the game's language), or the code of a file in h5-together\texts: en, fr...
```

## One language: French texts (`languages*.cpp`, off by default)

**What it is for:** with MMH5.5, a French game mixes languages. The base game's texts are French, but the 3.1 patch's and MMH5.5's texts are English, and they replace many French ones. With `texts=fr`, the game takes every text it can from the French packs in `h5-together\lang\fr\` next to the exe:

- **MMH5.5's own French pack.** The MMH5.5 team publishes it on MMH5.5's ModDB page. Each player downloads the one for their MMH5.5 version, unzips it and puts its `.pak` there. Any version works, and the log names it. A `.zip` of texts is read too; a `.zip` that only wraps the `.pak` is reported in the log.
- **Our pack** of what that pack leaves out, shared within the group.

Nothing on disk changes. `game` (the default) installs nothing. The plan, with the counts and the version question, is [docs/plans/languages.md](../../docs/plans/languages.md).

**How:** the game reads each text the first time it is shown, through its file system, and keeps it until it closes ([docs/subsystems/texts.md](../../docs/subsystems/texts.md)). The mod wraps the same "open file" function as the music mod (`CWinVFS` slot 7). When the game opens a `.txt` path that one of our packs has, it gets that file instead.

- **Only `.txt` files** are ever taken from a pack. A language pack cannot change game data, only words on the screen.
- **The `FF FE` mark** that the game needs before a text (without it the game shows an empty text) is always there. A UTF-8 text in a pack is converted.
- **Between our packs,** the newest file of a path wins, as in the game. On equal dates, the pack whose name sorts last wins, the same on Windows and Linux.
- **Our panels** (`[ui] language=auto`) use French words with `texts=fr` ([our texts](#our-panels-in-your-language-srcmodstextstxt-srchooktextscpp)).

Texts are only shown, never part of the game world: the game objects keep a text's path, not its words. So each player can choose for themselves.

**Checked** with the robot (2026-10-25, MMH5.5 RC19d, its French pack and ours in the test copy):
- **Single player, a random map:**
  - 6,024 French texts indexed;
  - the version line matched ("5.69d" both);
  - 383 texts given to the game (379 from MMH5.5's pack; 4 from ours: random map settings and the Rage message);
  - the player list in French.
- **LAN on A1M4, only the host on `texts=fr`:** 8 AI turns, 1,465 world checksums, all equal on both copies (no desync).
- **The same on MMH5.5 RC19k** with its closest French pack (RC19i) and our pack for RC19k: 6,083 French texts; LAN with only the host on French, 1,557 world checksums, all equal.
- **A `.zip` that only wraps the `.pak`, and no folder at all:** the log says so, and the game keeps its own texts.

**Not seen yet:** the combat log (the robot's battles resolve at once) and the random map screen's labels.

The log (`h5-together.log`) says what was found, for example (MMH5.5 RC19k, its RC19i French pack and our pack):

```
languages: texts=fr: the game's texts come from h5-together\lang\fr\*.pak where they have them
languages: texts=fr: 6083 French texts from h5-together\lang\fr: MMH55-Texts-FR.pak (5603), open-homm5-fr.pak (480); 272 of them newer than another pack's; indexed in 16 ms
languages: version text: French pack "Version: 5.69i" (MMH55-Texts-FR.pak), the game "Version: 5.69k" (another MMH5.5 version: some French descriptions may not match the game)
```

The version line compares the MMH5.5 version written in the French pack with the one the game shows in its main menu. When they differ, some French descriptions may describe another version's numbers. The menu itself keeps showing the game's own version.

## Farther camera zoom, start distance (`camera.cpp`, off by default)

**What it is for:** the camera cannot zoom out far. `zoom_out=150` lets the adventure map's camera go 1.5 times as far from the ground as the game allows; `combat_zoom_out` does the same in battles. Off (the default, 100), the game's own limits.

**Where the limit comes from** (details in [docs/subsystems/camera.md](../../docs/subsystems/camera.md)):
- **Adventure map:** the game's `profiles\autoexec_a2.cfg` sets it at start-up: the farthest distance is 70 in every camera mode of the options screen, with or without MMH5.5 (MMH5.5 does not change this file or this code). The "vertically fixed" mode also keeps the camera looking down steeply (65°), so it shows less of the map than the "normal" mode (30° to 55°).
- **Battles:** the game picks the farthest distance by the battlefield's size. MMH5.5's DLL changes the closest zoom and the tilt of its bigger battlefields, and keeps the game's farthest distance.

**What it does:** when the game sets up the camera (map start, camera mode change, each battle), the mod hands it the game's own limits with the farthest distance (and the "soft" limit the camera springs back to) multiplied. Nothing else changes: the closest zoom, the tilt, the game's files and its own copy of the limits stay as they are. So it follows whatever the game and MMH5.5 set, in every camera mode.

The log says what it changed:

```
camera: adventure map: the camera may zoom out to 150% of the game's limit (zoom_out)
camera: adventure map, vertically fixed camera: farthest zoom 70 -> 105 (nearest 37)
camera: adventure map, vertically fixed camera, soft limit: farthest zoom 69 -> 103.5 (nearest 38)
camera: battle: farthest zoom 56 -> 84
```

**Only your screen changes:** the camera is not part of the game world, so it is not in the world checksums or in the commands the PCs send each other. Each player can choose, also in co-op games.

**Checked with the robot** (A1M4; the robot starts the camera far out, and the game stops it at its farthest allowed distance; details in camera.md):

| Run | Camera stopped at | World checksums |
|---|---|---|
| the game's limits | 70 | (one PC) |
| `zoom_out=150`, normal and "vertically fixed" camera modes | 105 | (one PC) |
| `zoom_out=300` | 200 (the start position), the sky shows past the small map's edge | (one PC) |
| `combat_zoom_out=150`, a battle the AI started | battle limit 56 -> 84, the battle ran to its end | (one PC) |
| LAN game, only the host with `zoom_out=200` and `combat_zoom_out=200` | host 140, client 25 | 698, all equal |

Not checked by the robot: the mouse wheel during play (the robot's windows are in the background), the frame rate on a big explored map, Linux.

**What to watch** far out: near the map's corners you see past its edge; on big maps more of the map is drawn, so a slow PC may lose frames; scrolling does not speed up with the distance. It works in the vanilla exe and MMH5.5's co-op exe (`MMH55_Utility`), not in MMH5.5's single-player exe (`MMH55.exe`, another build, where the DLL installs nothing); camera.md says how to get the same there by hand.

**Settings** (`h5-together.ini`):

```ini
[lang]
texts=game     ; game (the texts as installed), fr; en (English) is planned, not built yet
log_texts=0    ; testing: log the first N texts taken from the packs

[camera]
zoom_out=100          ; adventure map: % of the game's farthest zoom (100 = the game's; e.g. 150; up to 400)
combat_zoom_out=100   ; battles: the same
start_zoom=           ; adventure map: the camera's start distance (the game: 25; empty = the game's)
wheel_fix=auto        ; the mouse wheel under Wine (wheel.cpp, below); 0 = off, 1 = from the first turn
```

The robot passes them with `--camera-setting KEY=VALUE` (`run_lan_test.py`: also `--host-camera` / `--client-camera`).

**Start distance** (`start_zoom`, off by default): the adventure map's camera starts at distance 25 (the game's `mission_camera_def_pos`). With `start_zoom=70` it starts at 70 (the game keeps it within its limits, so with `zoom_out=150` up to 105). The camera set-up asks for its soft limits right before it reads the start position, so the mod's soft-limit hook writes the distance there (`0x00f339f0`). Log: `camera: adventure map: the camera starts at distance 70 (the game: 25; start_zoom)`.

## The mouse wheel under Wine / Proton (`wheel.cpp`, `wheel_logic.cpp`; on under Wine only)

**The problem** (a Linux player, 2026-11-01): the wheel zooms in once and never out; Page Up / Page Down work. It happened before our DLL too.

**Why** (static reading of the exe; [docs/subsystems/camera.md](../../docs/subsystems/camera.md), "The mouse wheel"):
- The game sets its DirectInput mouse to **absolute** axis mode and works the wheel's motion out itself: "this value minus the last one". Windows reports positions (the running sum of the notches), so that difference is the motion.
- **Wine reports the wheel as steps** (+120 or -120 a notch), even in absolute mode. The difference then turns every repeated notch into 0.
- The game also drops a "press" (bit 7 of the value's low byte) while it thinks its window is inactive or the cursor is outside it. With positions that is harmless (the next value carries the motion). A step down, -120, ends in `0x88`: it looks like a press, so wheel-down can be dropped every time.

**The fix:** the game gets positions, the running sum of Wine's steps, in its wheel events and in the mouse state's `lZ`, as on Windows. Its own "this minus the last" then gives each notch, both ways.
- `wheel_fix=auto` (the default) does it **only under Wine** (ntdll's `wine_get_version`), and only after a check: after a batch of wheel events the mouse's own state is read. With positions its `lZ` is the last event's value; with steps it is their sum. Two clear "steps" checks switch the fix on, within the first 3 or 4 notches. Positions, or nothing clear in 16 checks: left as they are.
- `wheel_fix=1` converts from the first notch (for a Wine the check misreads; never on Windows, where it breaks the wheel). `wheel_fix=0` leaves the wheel alone.
- **On Windows nothing is installed** (unless `trace_wheel=1`).
- DirectInput filter order -10: before the robot's keys (0) and the modal panels (100).
- Only this PC's camera changes: the camera is not part of the game world.

**The log:**

```
wheel: Wine 9.0: the first mouse wheel events are checked; if Wine reports them as steps, the game gets positions, as on Windows (wheel_fix=auto)
wheel: first mouse wheel event from DirectInput: -120 (20-byte events; checking)
wheel: Wine reports the mouse wheel as steps (2 of 3 checks): from now on the game gets positions, as on Windows, so the wheel zooms both ways (wheel_fix=auto)
```

**The trace** (`[debug] trace_wheel=1`): every wheel event (raw and what the game gets, and whether the game drops it as a press), the game's own `GetDeviceState` reads, each Acquire, each check, the game's checks for presses at the first event (window active, cursor inside), and the window's `WM_MOUSEWHEEL` messages (a `WH_GETMESSAGE` hook on the game's thread, watch only).

**Tests:** `test/wheel_test.cpp` models the game's reading, Windows' and Wine's mice: without the fix, 5 notches up zoom in once and 10 down do nothing; with it, every notch counts, also when the game drops presses; positions are never converted.

**Robot checks** (2026-11-01, A1M4, `MMH55_Utility_64.exe`, `builder=1`; the robot's `wheel` step gives the game's mouse notches as Windows or as Wine would, its `dik` step presses Page Down / Page Up). The robot's game window is in the background, so the game's press filter is on (`trace_wheel`: "window active 1 1, cursor in its window 0"), as it seems to be for the Linux player:

| Run | Wheel down 6 notches | Wheel up 6 notches |
|---|---|---|
| `wheel_fix=0`, Wine-like steps | **no zoom** (every -120 dropped as a press) | zooms in **one** notch, then nothing |
| `wheel_fix=0`, Windows-like positions | zooms out to the farthest | |
| `wheel_fix=1`, Wine-like steps | zooms out to the farthest | zooms in to the nearest |

Page Down (1.5 s) zoomed out to the farthest and Page Up back in, with `builder=1` (the modal panels' input hooks installed and idle). **Not checked yet:** a real Linux PC (that `auto` switches on there by itself).

**Keyboard:** Page Down zooms out and Page Up zooms in (`bind +camera_zoom 'PG_DOWN'`, `bind -camera_zoom 'PG_UP'` in the profile's `input_a2.cfg`), on every system.

## Hero looks (`hero_skins*.cpp`; on, but nothing changes until a look is chosen)

**What it is for:** the owner's request, "a way in game to select our hero's skin". A hero can wear the look of another hero **of the same faction**: its 3D figures on the adventure map, in battle and on the hero screen. The portrait, the class, the skills, the music and everything the rules use stay the hero's own. Plan and findings: [docs/plans/hero-skins.md](../../docs/plans/hero-skins.md).

**How a player chooses:** the small **Look / Apparence** button on the hero screen (in the "Armée & Stats" header, left of MMH5.5's two buttons, where the owner's sketch put it; it shows the portrait of the hero's look and a tooltip), or **F7** (`[skins] key`) on the adventure map, the hero screen or the town screen, opens a panel: on the hero screen for **the hero the screen shows**, elsewhere for this PC's player's selected hero (`hero_skins_panel.cpp`, the template builder's style, a modal overlay panel: the game gets no input while it is open). It shows the looks of the hero's faction as portraits, its own first ("son apparence"), each with the name of the hero it is named after and how many others wear it; the selected one lists them ("Aussi portée par : ..."). **Appliquer / Apply** saves the choice to `h5-together-skins.ini` (next to `h5-together.ini`, by the hero's internal name: `Godric=Freyda`; "Own look" removes the line) and makes it count: at once in single player and hot seat (the map figure is rebuilt); in multiplayer for every player a few seconds later (below). The choice is remembered for every game. The button is drawn by the DLL (ImGui) at MMH5.5's hero screen layout, scaled with the screen height: the screen's panel `UI/HeroScreen3/SingleHero` is at x 60, centred vertically, and the skill wheel's round button (`MMH55-Skillwheel.pak`, `Single/Army`, `Button_HS`) at 372,48 inside it; ours is 24×24 at 403,94 of the 1024×768 reference: the slot of MMH5.5's small square button (which MMH5.5 shows only sometimes, and which is in none of the screen's UI files), centred on the round button's line (y 106, measured in the owner's two screenshots). `[skins] hero_screen_button_x` / `_y` move it. It reads the mouse itself (the click also reaches the game, on an empty spot of the header). Not native yet: serving the screen's own UI file would need its command handler, not researched.

**The hero screen follows at once** (`[skins] live_hero_screen=1`): after Apply on the hero screen (single player), or when a changed look counts at its segment (multiplayer), the hero screen showing that hero closes and opens again, on the same page, at the next adventure-map frame. It is what the screen's own "next hero" arrow does when the player has one hero (`0x006c4880(screen, hero)`: the game's close, then a new screen with the old one's values); the new screen builds the 3D figure with the look. Only in the screen's own-hero mode, when it is shown and its close would close it (no dialog, nothing waiting for "are you sure"); otherwise nothing is done and the screen shows the look when opened again. The game never rebuilds the figure of a screen that is already shown, and making it do so crashed (DLL `9823B52E`). Log: `the hero screen closes and opens again for <hero> ...`, then `the hero screen is open again after 500 ms (...): <hero> with <look>'s look, battle model <file>`. Details: [the plan](../../docs/plans/hero-skins.md), "The hero screen's 3D figure changes at once".

**Who decides a hero's look:** its owner, if human. In single player and hot seat, the file's choices count for every human player's heroes. In multiplayer, a PC's choices count for its own player's heroes; the other players' choices arrive by message. Computer players' heroes keep their looks. A look of another faction is left out (the log says so).

**How it works** (details at the top of `hero_skins.cpp`):
- The game asks the hero object for its battle figure and its map figure (two virtual functions, `IHero` slots `0x248` and `0x24c`). For a hero with a look they answer the look hero's figures; the hero object itself, which goes into saves and world checksums, never changes.
- The look's strike figure, map animations and hero screen camera are put into the hero's data object, in memory only, with counted references; the hero's own are put back when the look goes.
- The DLL notes each map figure the game builds. In single player and hot seat it rebuilds a figure with the game's own code when its look should change (just after the map shows, when a hero changes owner). Battles and the hero screen pick up the look when they open.
- **Multiplayer: all or nothing.** Rebuilding a map figure makes new game objects, which take numbers from the world's counter, so every PC must rebuild the same figures at the same moment of the game, or the PCs' worlds part (the robot saw the world checksums split). After its second segment each PC sends its choices as world-checksum messages with check number −1, which the game ignores (the co-op guard's trick): a header with a fingerprint of the hero list (`k`), then one message per choice (`K`). At the start of segment 20 every PC applies the same rule: if every PC sent its looks in time (before its own report of segment 11) with the same game data, every PC rebuilds the same figures, in the order of their object numbers, and the game builds later figures with the looks; otherwise **nobody's looks show in that game**. So one friend without the DLL (or with another MMH5.5 version, or `show_others=0`) means normal looks for everyone in that game; the log says which PC.
- **A choice made during a multiplayer game** (the panel's Apply) is sent at this PC's next segment report R, and every PC (this one too) takes it in at the start of segment R + 10 and rebuilds that hero's figure there: the same moment everywhere, a fraction of a second to two seconds later. One rule for all PCs (`skins::ChoiceCountsAt`): a change sent before R = 10 counts with the first looks, at the decision of segment 20. In a game whose looks are off, it is only saved, for the next games (the panel says so). The hero screen showing that hero opens again at that segment, also when the hero has no map figure to rebuild (before 2026-11-04 it did not then, so the Apply seemed to do nothing).
- **Fixed 2026-11-04 (0.3.0 and before):** the panel was always for the selected hero on the map, also when the hero screen showed another hero (its own arrows, a town's hero portraits), so "hero A's change went to hero B" (the owner's games over Steam). Now the hero screen's hero; a hero of another player there gets no panel (in multiplayer its owner chooses).

The log says what happened, for example:

```
hero skins: on; 3 line(s) in h5-together-skins.ini (read when the game's hero list is); multiplayer looks when every PC takes part
hero skins: 179 heroes in the game's list (fingerprint 0518, other entries skipped); 2 look(s) chosen in h5-together-skins.ini
hero skins:   Godric wears the look of Freyda
hero skins:   Axel=Giovanni: another faction: left out (same faction only)
hero skins: Jeddite: the map figure is rebuilt with Freyda's look
hero skins: sent to the other PCs: 2 look(s) for player 1 (after this PC's report 2)
hero skins: from PC 1: player 2 sends its looks (same game data)
hero skins: looks on for this game (all 2 PCs sent theirs, with the same game data)
hero skins: no looks in this game: PC 1 sent none in time (no H5 Together DLL, an old one, other game data, or show_others=0)
```

**Checked with the robot** (A1M4; details in the plan's "Robot results"): single player (the look on the map and in a battle the AI started; the panel opened, a look picked and applied: the map figure changed at once), LAN with different looks on each PC (793 checksums, all equal), LAN with one PC taking no part (no looks, 517 checksums, all equal), LAN with a look changed through the panel during the game (both PCs rebuilt the figure at the same segment, 702 checksums, all equal).

**Settings** (`h5-together.ini`):

```ini
[skins]
skins=1            ; 0 = install nothing
key=F7             ; the panel's key (F1 to F12, or none)
hero_screen_button=1   ; 0 = no Look button on the hero screen
live_hero_screen=1     ; 0 = an open hero screen shows a new look only when opened again
test_button_on_map=0   ; testing only: the button also on the adventure map
show_others=1      ; 0 = this PC takes no part in multiplayer looks (then nobody's show there)
trace=0            ; testing: log each figure built and who asks for the figures
test_ai_heroes=0   ; testing only: computer players' heroes take this PC's looks too
```

The robot passes them with `--skins-setting KEY=VALUE` and the choices with `--skins-file PATH` (`run_lan_test.py`: `--host-skins` / `--client-skins`, `--host-skins-file` / `--client-skins-file`). `tools/robot/make_skins_file.py` writes a choices file where every hero of a faction wears one look, read from the test copy's data.

## Wider volume range (`audio_volume.cpp`, off by default)

**What it is for:** even at the lowest step of the options screen's sliders (music, sounds), the game was too loud. The sliders have 11 steps from 0 to 100%, and they are straight. The 10% step is a tenth of the sound wave's size, only 20 dB below full volume. With `volume_curve=wide` each step is about the same number of decibels: 10% becomes about −40 dB (10 times quieter), 50% −12 dB, and 100% stays as it is. Off (the default, `game`), the game's own sliders.

**How** (details in [docs/plans/lobby-audio-classes.md](../../docs/plans/lobby-audio-classes.md), item 1):
- The sliders store their value times 255 in the sound effects' manager and the music system.
- Every channel's volume then goes to FMOD through one function, `0x00b26e80`, called from two places: `0x00d0439b` (sound effects) and `0x00d047ac` (music).
- The mod redirects those two calls and multiplies the volume once more by its slider's value, so a slider value *s* counts as *s*².
- Nothing stored changes: the slider, your profile and every place that lowers the music for a while (dialogs, cutscenes) keep working as before.
- A sound the game makes audible stays audible at FMOD's quietest step.

**Only your speakers change:** sound is not part of the game world. Each player can choose, also in co-op games. The log says `audio: volume_curve=wide: ...`.

**Checked with the robot** (2026-11-01; `--audio-setting volume_curve=wide --audio-setting log_volume=200 --setvar "sound_sfxvolume = 0.1" --setvar "sound_musicvolume = 0.1"`):
- the log has `audio: volume_curve=wide: ...`;
- the main menu's music fades in from `game volume 1/255 -> 1/255` to `game volume 25/255 -> 2/255` and stays there;
- on the map, a sound effect has `audio: sound: slider 25/255, game volume 25/255 -> 2/255`. So at the lowest step FMOD gets 2 instead of 25 (about −42 dB instead of −20 dB);
- the game ran normally, with a battle.

Still to check: the owner's ears (the lowest step quiet but audible, music and sounds balanced as before). In a LAN game where only the host used it, the world checksums stayed equal (700, below).

**Settings** (`h5-together.ini`):

```ini
[audio]
volume_curve=game   ; game (the game's sliders), wide (each step about the same number of decibels)
log_volume=0        ; testing: log the first N changes of an audible channel volume (slider, game's volume, ours)
```

## Black past the map's edge (`edge_sky.cpp`, off by default)

**What it is for:** zoomed out (above), you see past the adventure map's edge. On many random maps that is a flat dark violet with a lavender corner. It is a **sky dome**, the Sylvan siege battlefield's sky: MMH5.5 gave it to `Lights/_(AmbientLight)/Arena/Dusk_light_BoatArena01(VicTest).xdb`, one of the 4 lights the random map generator picks from (`RMG/Params/Default.xdb`). The original game's copy of that light has no sky dome. With `edge_sky=none`, the game gets that light without its sky dome, so past the edge the screen stays black, like unexplored land. The same goes for the **underground**: the generator gives every random map's underground `Lights/_(AmbientLight)/Tests/underground.xdb`, which has a sky dome of its own (a dungeon backdrop, `SkyDome_Dung_Virt`); since 0.3.4 it is served without it too (the owner's game, 2026-11-05). Only adventure maps' undergrounds name that light (and an old map file among the arenas, in a list of underground lights a battlefield does not have). Off (the default, `game`), the game's sky.

**How:** when the game opens that light file, the mod hands it the same file with an empty `<SkyDome/>`, as the lights without one have. This uses the game's file system hook (the one the original music and the French texts use). Nothing on disk changes.
- In the game data only the generator's list names this light, so **battles keep their sky**.
- The other 3 random-map lights and hand-made maps keep theirs. They show the Haven sky; if that should go too, it is the same work.
- Restart the game after a change: the game keeps a light once it has read it.

**Only your screen changes:** lights are drawn, not simulated. Each player can choose. The log says `edge sky: ... served without its sky dome (edge_sky=none)` when a map uses that light.

**Checked with the robot** (2026-11-01, `zoom_out=300` and the far start position of camera.md):

| Map | `edge_sky=game` | `edge_sky=none` |
|---|---|---|
| a random map of the owner's that drew the dusk light (`MAP_JoacJoz`, started with `--map /Maps/RMG/81CD0EF2-7D11-41D7-AE71-0A74BF0872A0/map.xdb`) | past the top-left edge: the flat violet and lavender of the owner's screenshot | black; the log has `edge sky: lights/_(ambientlight)/arena/dusk_light_boatarena01(victest).xdb served without its sky dome` |
| A1M4 (another light: tested with `edge_sky_test_light=Lights/_(AmbientLight)/AdvMap/Addon/AC1M4.xdb`) | a pale sky glow above the far edge | black |

- **Battles:** a battle in the same game drew its battlefield as usual. The log showed no other light served, so only the named light files change.
- **LAN game**, only the host with `edge_sky=none`, the test light, `volume_curve=wide` and French texts: 700 world checksums, all equal on both copies.
- Give a Git Bash command line `MSYS_NO_PATHCONV=1`, or use PowerShell: Git Bash rewrites `--map /Maps/...` into a Windows path, and the game then says `Couldn't find map`.

**Settings** (`h5-together.ini`):

```ini
[camera]
edge_sky=game            ; game (the game's sky), none (no sky dome on the random maps' dusk light and the undergrounds' light: black)
edge_sky_test_light=     ; testing: one more light file to show without its sky dome
```

## The main menu's scene and music (`menu_scene*.cpp`, off by default)

Plan, research and robot checks: [docs/plans/menu-scenes.md](../../docs/plans/menu-scenes.md).

**What it is for:** the main menu is a 3D picture with the buttons over it, and a theme. The game ships two pictures of its own, and which one you see depends on what is installed: plain Tribes of the East (ToE) shows **its** picture (orcs and goblins storming a Haven town wall, with the ToE theme); with MMH5.5 you see the **Heroes V** picture (a cleric, an arch devil, a champion on a horse; MMH5.5's own music). `[menu] scene` lets each player pick one of them, or have one drawn every time the game starts.

| `scene=` | What you get |
|---|---|
| `default` (the default) | **Nothing changes**: the menu exactly as installed. The add-on installs nothing for it. |
| `toe` | The Tribes of the East picture and its theme (`music\a2\Main_Menu.ogg`). |
| `homm5` | The Heroes V picture and its theme (`music\MainMenu\MainMenu-Theme.ogg`). The picture comes with MMH5.5; on a PC without it the setting falls back to `default` (the log says why). |
| `random` | One of the pictures this PC can show, drawn once each time the game starts, with its own theme. With MMH5.5 that is `toe` or `homm5`; with plain ToE only `toe`, which is what it shows anyway. |

The logo (MMH5.5's) and the buttons stay as they are, and so does the multiplayer sub-menu's theme. A third picture, Hammers of Fate's, does not exist in the game's files, so there is no `hof`. Restart the game after a change. Nothing is written to disk and nothing from the game is in the repo: it only changes what your screen shows and your speakers play, so each player can choose (it does not matter for co-op games).

**How it works** (plain words): the game reads its menu picture from one small data file (`Arenas/Interface/MainMenu/MainMenu.(ArenaDesc).xdb`, which lists the picture's objects). Tribes of the East's copy stays in `data.pak` even under MMH5.5, which only hides it behind its own copy; the Heroes V picture's pieces are in `MMH55-Data.pak`. At the first menu file the game opens, the mod reads the directories of the game's `.pak` files (about 60 to 80 ms), finds the picture descriptions (it recognises each by the scene file it was made from, not by the pak's name), checks that every file a picture needs is there and that its theme's `.ogg` is in `music\`, and draws the pick. Then the same file hook the other mods use hands the game:

- the chosen picture's description when it asks for `MainMenu.(ArenaDesc).xdb`;
- the game's root table of music (`UI/UIGameRoot.(UIGameRoot).xdb`, 333 KB) with one change: the item `MAIN_MENU_MUSIC_ID` names `Sounds/_(Music)/MainMenu/OpenHomm5-Menu.xdb`, a name of our own;
- that name's content: the chosen theme's music definition (a 7-line file from `data.pak`).

**Why our own name for the music, not `a2-MainMenu.xdb`:** the file `a2-MainMenu.xdb` (the ToE theme, which MMH5.5 re-points at its own track) is also played by one ToE campaign dialog (`A2C3/M4/S2`), and serving other content under that name would change that dialog's music too. With a name of our own, only the main menu's item changes: that dialog, `a1-MainMenu.xdb` (Hammers of Fate's, used by a campaign dialog), the multiplayer sub-menu's theme and every other music definition stay as the game has them. (Serving the menu only "while the menu is up" would not do it: the game keeps a music definition once it has read it, so the dialog would play whatever the menu loaded.)

**One fallback:** the game reads the root table about a second after it starts, before our hook goes in on a Steam-wrapped exe (that one is hooked a few seconds later; MMH5.5's exes are hooked at once). In that case, and only then, `a2-MainMenu.xdb` itself is served with the theme, the log says so, and that one dialog changes with it. It cannot matter on plain ToE (nothing to change there: `homm5` is not available and `toe` is what is shown), and the MMH5.5 exes are not wrapped.

**With `[audio] original_music=1`:** `scene` wins whenever it is not `default`. `original_music` sets `a2-MainMenu.xdb` back to the ToE theme; the menu no longer asks for that file when `scene` is set, so the theme comes from `scene`. With `scene=default`, `original_music` does what it always did. (The two hooks wrap the same file system slot: ours is installed after `original_music`'s, so it is the outer one and sees the root table as `original_music` left it.)

**What the log says:**

```
menu scene: read 16 paks (129415 files) in 62 ms; scenes available: homm5, toe
menu scene: scene=random picked toe (available: homm5, toe): showing toe: the scene's description from data.pak (5702 bytes), the music ..\Music\a2\Main_Menu.ogg from data.pak, served as /Sounds/_(Music)/MainMenu/OpenHomm5-Menu.xdb
menu scene: the root table's main menu music /Sounds/_(Music)/MainMenu/a2-MainMenu.xdb -> /Sounds/_(Music)/MainMenu/OpenHomm5-Menu.xdb
menu scene: arenas/interface/mainmenu/mainmenu.(arenadesc).xdb served (toe's scene)
menu scene: sounds/_(music)/mainmenu/openhomm5-menu.xdb served (toe's theme)
```

A scene this PC cannot show says why (`scene=homm5 is not available on this PC (no Heroes V menu scene in the game's data): the menu as installed`).

**Checked with the robot** (2026-11-02, test copy with MMH5.5 RC19k, the menu's screenshot and `log_music=1`, which lists the music files the game starts):

| Setting | Picture | Music file the game started |
|---|---|---|
| `default` (or no `[menu]`) | Heroes V (MMH5.5's), as before; the log says nothing is installed | `MMH55\MainMenu_Dynasty_002.ogg` (MMH5.5's, as before) |
| `toe` | Tribes of the East: a cyclops and a castle wall | `a2\Main_Menu.ogg` |
| `homm5` | Heroes V: a burning arch devil in front of a bent tree | `MainMenu\MainMenu-Theme.ogg` |
| `random`, 6 launches | `toe`, `toe`, `homm5`, `toe`, `toe`, `homm5` (each picture with its own theme) | the pick's theme every time |
| `toe` / `homm5`, a game started, then the game's menu "Main menu" and OK | the same scene as at the start, drawn again | the theme starts again (`music file 4`) |
| `original_music=1` with `default` | Heroes V | `a2\Main_Menu.ogg`, as `original_music` always did |
| `original_music=1` with `homm5` / `toe` | the scene's own | `MainMenu-Theme.ogg` / `a2\Main_Menu.ogg`: **`scene` wins**; `original_music` never touches `a2-MainMenu.xdb` (no `[26]` line in the log), because the menu no longer asks for it |
| hook installed 4 s late instead of at the DLL's load (a test build, `toe`) | the scene is still served (the game reads the picture's file at 4.8 s, when the menu is built) | the root table was read before: the fallback serves `a2-MainMenu.xdb`, and the log says so |

The picture's description is read at about 4.8 s in every run and the root table about 1.2 s after the start, so the hook goes in in time on every exe that is hooked at the DLL's load. Reading the paks' directories took 62 to 79 ms, once.

**Not checked in the game:** a PC without MMH5.5 (the unit tests cover the pick for it: `homm5` falls back, `random` is `toe`), and the campaign dialog `A2C3/M4/S2` itself (the robot cannot start it; the log shows that with `scene` set nobody asks for `a2-MainMenu.xdb` at the menu, so the dialog gets the game's own file).

**LAN game** (host `scene=toe`, client `scene=homm5`, A1M4, 3 AI turns): both joined and played, each copy showed its own picture and theme at the menu, and the 516 world checksums were equal on both copies (no desync): it is only a picture and a tune, so each player can choose.

**Settings** (`h5-together.ini`):

```ini
[menu]
scene=default   ; default (as installed) | toe | homm5 | random
```

## Game rules from the host (`host_rules*.cpp`; on, and nothing changes unless the host changes something)

Plan, research and robot checks: [docs/plans/host-rules.md](../../docs/plans/host-rules.md).

**What it is for:** MMH5.5 has switches for many of its rules (governors, town conversion, the town gate, mine guards, the computer's weekly bonus, mixed neutral armies, prank artifacts, the random map extras...), in `scripts/H55-Settings.lua`. Until now a group changed them by swapping a settings pak on every PC. With this mod **the host chooses them for each game**, in a panel, and every PC plays with them.

- **The host:** on the game setup screen or in the waiting room, **F2** (or **"Rules..."** at the right of the waiting room's title) opens the panel: the game mode and ToE start bonuses, the skills of each class, 15 of MMH5.5's switches and "MMH5.5 scripts on random maps" (below), each with one line of explanation, in the player's language (English, French, or a file a player added). Gold = changed from your MMH5.5 file. **OK** keeps the choice (also for your next games, in `[rules]`); **MMH5.5's file** puts everything back.
- **The other players** see the host's rules there, read only. On the adventure map, **F2** shows the running game's rules to everyone.
- **They are used only when every player has the add-on.** Otherwise every PC uses its own MMH5.5 file, as without the add-on; the host's panel names who lacks it, and the button gets a red "!".
- **The check:** each PC tells the host a fingerprint of its MMH5.5 files (`H55-Settings.lua`, the stats paks). If a player's files differ, the host's panel and log name that player (the game could go out of step); that player's panel says so too.
- **How:** the values travel in one more part of the game's own start message, so it works on any map; when the map's scripts read `H55-Settings.lua`, every PC's add-on hands them the file with the host's values (only those numbers change). The log says which values were used, on every PC, with the same hash:
  `rules: the scripts read scripts/H55-Settings.lua: served with the host's rules (governance 1->0, ai_cheat 4->0, mixed_stacks 1->0; ...); 8206 -> 8206 bytes, hash a07c5c34`
- **MMH5.5 scripts on random maps** ("Scripts MMH5.5 sur les cartes aléatoires", `map_scripts`): in a multiplayer game the game runs MMH5.5's scripts (governors, mixed armies, the computer's bonus, the modes...) only on maps with a map script. Hand-made maps and random maps saved with a script (MMH5.5's Mapmixer, the editor: the group's usual maps) have one; **a random map straight from the game's generator has none**. With MMH5.5 RC19d the scripts then do not run there (and these switches change nothing); RC19k runs them anyway. This rule decides it for every PC of the game: **On** = the add-on gives such a map an empty map script (as the Mapmixer does), so the scripts run; **Off** = they do not run, as in the original game (also with RC19k). **The default is what your MMH5.5 does by itself** (RC19d: off, RC19k: on), so nothing changes unless the host changes it. Maps that have their own script are never changed. Logged on every PC: `rules: the world starts its scripts: this map has no map script, and this game's rules say map_scripts=1: it gets an empty one, so MMH5.5's scripts run`.
- **Skills of each class** ("Compétences de chaque classe", `class_skills`, group "Heroes and skills"): in MMH5.5 each of the 24 classes is only ever offered the 12 skills of its list. **Unlocked** = every class keeps MMH5.5's list and chances, and each other skill that MMH5.5 offers to some class can come up too, at chance 2 (the rare skills of Tribes of the East; MMH5.5's lists add up to 100), so about one new-skill offer in five is off the class's list. **MMH5.5 (locked)** is the default. The game reads its class table (`HeroClass.xdb`) once, at start-up, so every PC's add-on edits the table in memory when the game starts and puts MMH5.5's back afterwards (`host_rules_classes.cpp`; checked structure, logged: `rules: class skills: UNLOCKED for this game ...; table hash ...`, the same hash on every PC). Used only when every PC has the same class table (its hash travels in the hello; RC19d and RC19k have the same lists); otherwise MMH5.5's lock for everyone, and the panel says why. Still MMH5.5's: attribute growth, the witch huts (MMH5.5's scripts), MMH5.5's skill wheel on the hero screen (it shows the class's own tree), and the per-class requirements of the special perks.
- Single player uses your MMH5.5 file, as before (and always runs MMH5.5's scripts).
- **Saves:** a loaded game goes on with what it started with (the rules' values and whether the scripts run): a load does not start the map's scripts again. The class skills travel in the save itself (one more chunk of the saved world): a game started unlocked is unlocked again when loaded, on every PC with the add-on (F2 then says "The class skills come from the loaded save"). A PC without the add-on loads it on MMH5.5's lock.

**Game mode and ToE start bonuses** (the panel's first group; `host_rules_bonus.cpp` for the lobby's tooltips and icons, `host_rules_icons.cpp` for ToE's pictures). In MMH5.5 the lobby's bonus arrows choose the game mode (artifact = Default, gold = Savage, resources = Nightmare, random = one of them), and MMH5.5's start script then takes every human's bonus back and gives each first hero a random minor artifact instead. Two host rules change that:
- **Game mode:** "Bonus arrows (MMH5.5)" (the default: as today), or Default, Savage, Nightmare, Random for everyone. Savage and Nightmare still team the computer players against the humans, as MMH5.5 does (MMH5.5's own code runs; only its input changes). Scenarios, campaigns and games without computer players keep MMH5.5's choice (Default).
- **ToE start bonuses:** on = each player picks a start bonus with the arrows, as in Tribes of the East (gold, resources, a minor artifact, or random), and keeps it; MMH5.5's start artifact is not given (it is the same kind of bonus, so a player would get two). With this on and the mode on "Bonus arrows", the mode is Default.
- **How:** when the map's scripts read `scripts/H55-AdvMap.lua`, every PC serves it with three small edits in its "StartingBonus" part. Each edit is checked against MMH5.5's text; if MMH5.5 changes that text in an update, the file is served as it is (MMH5.5's way) and the log says why. RC19d and RC19k have the same part. The start rules are used only when every PC has the same part (its hash travels in the hello); otherwise the panel says why and MMH5.5's way stays.
- **The tooltips:** with these rules on, the bonus icons' tooltips in the waiting room describe ToE's bonuses and the game mode (in the panels' language, the `[bonus]` texts).
- **The icons:** with ToE start bonuses on, the bonus icons show Tribes of the East's own pictures (dice, a helmet, gold, resources) instead of MMH5.5's mode pictures, on every PC, as soon as the rule is on (and MMH5.5's again when it is off). Each PC reads ToE's pictures from its own game's `data.pak` when the waiting room first opens (nothing is copied to disk). If they cannot be read, the icons stay MMH5.5's and the log says why (`rules: ToE's bonus pictures ...`). For a few seconds after a player joins, before the add-on knows that player has it too, the icons and tooltips are MMH5.5's.
- The log: `rules: the scripts read scripts/H55-AdvMap.lua: served with game mode Default, each human keeps the ToE start bonus; ... hash ...` (the same hash on every PC).

### H5 Together's version in the waiting room (0.3.0)

**What it is for:** co-op works best when every PC has the same H5 Together. Before 0.3.0 a different version only showed as a silent "co-op timing OFF" in the log.

- **Where:** a line at the bottom of the waiting room, under the game's frame (between MMH5.5's "Version" text and the "Start the game" scroll), on the host and on every client with 0.3.0 or later, in the panels' language:
  - all the same: `Every player has H5 Together 0.3.0.` (dim);
  - different: `Not the same H5 Together on every PC (yours: 0.3.0): Bob 0.2.9, Ann 0.3.0 (another build), Eve: older than 0.3.0, Max: no H5 Together.` and `For co-op, every PC needs the same H5 Together version: update before you start.` (on a dark field);
  - on a client whose host has a version before 0.3.0: `The host has an older H5 Together than yours (0.3.0).` (15 s after joining: the host's rules came, but no list); no H5 Together at all (or its game rules off): `The host has no H5 Together (or its game rules are off).`
- **How:** the game rules' hello (above) carries two more requests from each client: 'V', its H5 Together version (8 bits each: major, minor, patch), and 'B', its build (the DLL's link time stamp: two builds of one version differ, e.g. a test build). The host sends each client that said its version the list of every player's, itself first, as one more lobby text (`V1`, one line per player: kind, version, build, name; `host_rules_logic.h`). A player who joined less than 12 s ago and said nothing yet is not counted.
- **Compatibility, on purpose:** a host before 0.3.0 does not know 'V' and 'B' and ignores them (as the game ignores unknown requests); a client before 0.3.0 never gets the list (only clients that said their version do). The rules' own hello and protocol (`kProtocol` 4) are unchanged, so 0.2.7 and 0.3.0 still agree on the game rules. A later change of the list gets another first line (`V2`); a client that cannot read it says the host has "another, newer" H5 Together, it never guesses.
- **Limit:** it rides on the game rules' hello, so a PC with `[rules] enabled=0` says nothing and shows as "no H5 Together".
- The log: `rules: versions: PC 1 has H5 Together 0.3.0`, `rules: versions: every player (2) has this H5 Together, 0.3.0` or `rules: versions: NOT THE SAME H5 TOGETHER on every PC (this one: 0.3.0): Bob 0.2.9 (build 6ae8aadd)`, and the line shown.
- **Robot checks** (2026-11-02, LAN, RC19k test copy; the two copies share one DLL, so `[rules] test_version=` made one of them say another version, or none): the same version on both, all 607 world checksums equal and the dim line on both; a client saying 0.2.9: both lines name the other copy's version; a host saying none (as 0.2.7): the client's line says the host's H5 Together is older, 15 s after it joined; a client with `[rules] enabled=0`: the host's line names it as having none.

**Robot checks** (2026-11-01, LAN, two copies; all world checksums equal):
- A1M4 (RC19k) with `governance=0 ai_cheat=0 mixed_stacks=0`: both PCs served the same file (hash `a07c5c34`), MMH5.5 no longer mixed the neutral stacks (`H55 Stacks Qty: 36`, no `H55 Mixing`). The client saw the host's rules in the waiting room and in game.
- The owner's `MAP_JoacJoz` (a random map with a map script): the same, plus `competitive_towns=0`.
- A client without the rules (`enabled=0`): no rules sent, both PCs used their file (and mixed the stacks, as the file says).
- A random map straight from the generator: MMH5.5's scripts did not run on either PC (no map script); the host's panel says so.
- **MMH5.5 scripts on random maps**, on generated maps: RC19d with `map_scripts=1 mixed_stacks=0`: both PCs ran MMH5.5's scripts (every `H55` start-up line twice), the same served file, no mixed stacks, 365 checksums equal; RC19d's default (off): no scripts, as before (328 equal). RC19k on a map made by RC19k: default (on) 327 equal, `map_scripts=0` no scripts (325 equal).
- **Saves:** loading a save opens no script file; the scripts go on with the values the game started with (single player, robot).
- **Skills of each class** (2026-11-02, `[rules] trace_skills=1` logs every new skill a level-up offers): unlocked, the AI's heroes were offered skills off their class's list (a Seer: Runelore, Demonic Rage, Summoning; a Runemage at chance 2: Avenger, Runelore, Invocation), the same on both PCs, all checksums equal (RC19d 1261, RC19k 2028); locked, every offer was on the list (2231 equal). A single-player autosave made unlocked kept the rule when loaded, and loaded normally with the rules off.

More in the plan.

**Settings** (`h5-together.ini`):

```ini
[rules]
enabled=1     ; 0 = never take part (no hello, always this PC's own file)
key=F2        ; the panel's key (setup screen, waiting room, adventure map); none = no key
button=1      ; the "Rules..." button in the waiting room
governance=   ; the host's choices, written by the panel (empty = MMH5.5's file); also
              ; town_conversion, extra_conversions, town_gate, mine_control, dimension_doors,
              ; ai_cheat, mixed_stacks, neutral_stack_size, bank_difficulty, negative_artifacts,
              ; rpg_potions, competitive_towns, mana_observatories, moon_disc
game_mode=    ; 0 or empty = the bonus arrows (MMH5.5), 1 Default, 2 Savage, 3 Nightmare, 4 Random
toe_bonuses=  ; 1 = each player keeps the ToE start bonus picked with the arrows (0 or empty = MMH5.5's way)
map_scripts=  ; 1 = MMH5.5's scripts also run on maps without a map script (multiplayer), 0 = not;
              ; empty = what this PC's MMH5.5 does by itself (RC19d 0, RC19k 1)
class_skills= ; 1 = unlocked: any skill MMH5.5 offers to some class, the others at chance 2 (0 or empty = MMH5.5's lock)
class_chance= ; testing: that chance, 1 to 50 (empty = 2); travels with the host's rules
trace=0       ; testing: log every script file the game opens, and print each player's start
              ; resources and minor artifacts to the game's console
trace_skills=0        ; testing: log every new skill a level-up offers (class, on the class's list or not)
class_skills_single=0 ; testing: a single-player game uses class_skills and class_chance (to check saves)
test_version=         ; testing: this PC says another H5 Together version in the waiting room ("0.2.9"),
                      ; or "old" = none, as a PC before 0.3.0 (robot runs: both copies share one DLL)
```

## New version available (`update_check.cpp`, off by default)

**What it is for:** players hear about a new version without watching the GitHub page. The owner's choice (2026-11-02): opt-in, contacting only GitHub.

- **Off by default.** With `[update] check=1`: at most once a day, when the main menu first shows, one HTTPS request to GitHub's public "latest release" address for our repo (`https://api.github.com/repos/jozrcr/h5-together/releases/latest`), on a thread of its own (WinHTTP with Windows' proxy settings; 5 s to connect, 10 s to answer; the game never waits for it). It sends nothing about the player: a User-Agent naming the add-on and its version, as GitHub asks; GitHub sees the address it comes from, as any website does. Only the answer's `tag_name` is read.
- **When a newer version is out:** two lines at the bottom left of the main menu, above MMH5.5's "Version" text, in the panels' language: `H5 Together 0.3.1 is out (you have 0.3.0).` / `Get it from github.com/jozrcr/h5-together/releases`. And the log: `update: H5 Together 0.3.1 is out (this is 0.3.0): https://github.com/jozrcr/h5-together/releases (after 297 ms; nothing is downloaded)`. Nothing is ever downloaded, installed or opened.
- **Once a day:** the time of the check and the version found are written to `[update]` (`last_check=`, `latest=`), so the notice also shows on the days without a request, and a failed request (no internet, GitHub busy) is not retried before the next day. The log says why: `update: no answer from GitHub (WinHTTP error 12007: no internet, or GitHub cannot be reached); the next check is in a day`.
- **Versions:** `v0.3.1` and `0.3.1` are the same; numbers compare as numbers (`0.3.10` after `0.3.9`); a test build `0.3.1-beta` comes before `0.3.1` (`update_check_logic.cpp`, tested by `src/hook/test/new_release_test.cpp`).
- The settings menu shows the switch (Options, H5 Together, Help), and the first start asks once ([the settings menu](#the-settings-menu-settings_cpp-options-h5-together)).
- **Robot checks** (2026-11-02): one real request (`--setting update.check=1`): `update: this is the newest version (0.2.7; GitHub's latest release: v0.2.7; after 297 ms)`, no notice. With `last_check=<now>` and `latest=0.9.0` in the file: no request ("checked less than a day ago") and the notice in the main menu, in English and French.

**Settings** (`h5-together.ini`):

```ini
[update]
check=0        ; 1 = ask GitHub at most once a day for the latest version (off by default)
last_check=    ; written by the add-on: when it last asked (seconds since 1970)
latest=        ; written by the add-on: the latest version it was told
asked=         ; written by the add-on: 1 once the first-start question was answered
```

## The settings menu (`settings_*.cpp`; Options, H5 Together)

**What it is for:** changing the add-on's settings in the game, instead of editing `h5-together.ini` in Notepad (docs/plans/community-release.md, part 2). The owner asked for it to be "as diegetic as possible": so it is not a panel of ours but a page of the game's own **Options** screen, with the game's own rows, check boxes, lists, sliders, scroll bar, tooltips and buttons.

**Where:** main menu, **Options**: the game's scroll lists `Game`, `Video & audio`, **`H5 Together`**, `Combat replays`, `Back`. From the adventure map: the game's own Options (its F4 key, or the Esc menu). **F3** opens the Options screen straight on our page (main menu, adventure map, or the Options screen itself; on the map through the map's own Options code, so, as with the game's F4, only when the game lets you open it). The game uses F4 to F8 and F10 itself (F8 is quick load), so F3 it is.

**What it shows:** like the game's categories, two pages side by side, each a list with headers:
- **On this PC:** Interface (player list, "your turn", the panels' keys and buttons, hero looks), Camera and visuals, Sound, Language, Random maps. The rows for the mouse wheel fix and the game's texts only show when they matter (under Wine; with the French packs, or `[lang] texts` already set).
- **Co-op games:** Online play (Off / Steam, and a line saying where friends' games show, or that `steam_api.dll` is missing: [the online UI](#online-play-over-steam-the-ui-online_uicpp)), Host (teams, team arrows, builder maps' teams, faction start terrain, and a line pointing to the game rules, F2), Advanced (co-op timing and steps, view delay, network timings, AI pathfinding, LAN over VPNs, autosaves, our templates, the host's rules, live hero screen, freeze detector, crash reports, another `dinput8.dll` mod), Help (the add-on's and MMH5.5's versions, the update check, where the file and the log are).

48 settings (47 until online play's row, step 4); the other 60 stay in the file (the 20 game rules have their own panel, F2; 38 are for testing or written by the add-on). `lan_peers` (addresses) is shown as a line: the game has no text box, so it is set in the file.

**When a change applies:** a name that ends in ` *` waits for a restart of the game, ` **` for the next game (the line at the top of each page says so; the tooltip says it in words). After **Apply**, a line at the bottom of the screen says, for example, "1 change applies after a restart of the game." The menu never restarts the game.

| When | Settings |
|---|---|
| At once | "your turn" sound and pop-up; the player list's key and place; the game rules key and Rules... button; the hero looks key, Look button and live hero screen; the builder's key; save compression; autosave days; LAN over VPNs; the freeze detector's wait; the camera's zooms and start distance (from the next map or battle) |
| From the next game | the host's teams |
| After a restart | everything else (the settings that put a hook in at start-up, or that the game reads once) |

Each "at once" setting is read again by its own mod (`settings_reload.cpp`, a `Reload...Settings()` in each mod: plain values the mod reads when it uses them, changed on the main thread). Checked with the robot: a new builder key opens the builder at once, the camera hook goes in for the next map, the rules key, the freeze detector's wait (log lines `... from the settings menu: ...`). The plan hoped for more (the co-op timing, the view delay, the team arrows, the language at once): those install hooks or patch the game's code at start-up, or hold texts other code keeps pointers to, so they stay "after a restart" (when in doubt, restart).

**During a multiplayer game** the Online play, Host and Advanced rows are greyed out by the game itself (its `disable_options_by_multiplayer`, set by the map's Options code, as for its own "quick combat"): changing them halfway would only make this PC disagree with the others. As a second net, Apply does not write a Host or Advanced change while that variable is on (robot check: `[coop] guard not changed: read only during a multiplayer game`).

**Buttons:** the game's own. **Apply** writes the changed settings; **Default** (after the game's own "restore the defaults?" box) puts every setting of our category back to its default; **Back** forgets the changes. Nothing is written before Apply or Default.

**How it works** (`settings_menu.cpp`):
- The Options screen (`NUI::CGameOptionsScreen`) builds its categories from the `GameOptions` part of `UI/uiconsts.(UIGameConstsH5).xdb`. When the game opens that file, we serve it with our category added at the end of the list (`settings_menu_data.cpp`, `AddCategory`; by structure, so it works with MMH5.5's file and the game's). Our words are served as the game's own text files, `UI/Consts/GameOptions/H5Together/...`, in the panels' language (`src/mods/texts/*.txt`, section `[settings]`). Nothing on disk changes. The log: `settings: the Options screen gets our "H5 Together" category (43 rows; ...)`.
- The game's pages have no scroll bar (theirs fit). Ours are longer, so the page list (`UI/GameOptions/PageCont`, shared by every category) gets the game's own vertical scroll bar, the random map screen's, hidden while there is nothing to scroll: the game's own pages look as before.
- Each row shows and sets a console variable of ours, `h5t_<section>_<key>`, holding the index of the row's choice. When a category's button is pressed (the screen's command `template`), we set them from `h5-together.ini`. **Apply** and **Default** write the category's variables and then save the profile (`0x008353e0`, called at `0x0075ca80` and `0x0075c70e`); that call goes through us first: the variables that changed are written into `h5-together.ini`, their level is put back to 0 so the game's profile file (`user_a2.cfg`) never holds them, and the "at once" ones are read again.
- **The file is changed in place, the installer's way** (`settings_ini.cpp`, the same rule as `Merge-Settings` in `install.ps1`): a changed setting fills its own commented line `;key=...` in its section; a value equal to the default puts the `;` back; every other line and comment stays as it was, with its line endings. If the file cannot be written (a protected folder), the line at the bottom says so and nothing changes. The log lists each change and what Windows then reads: `settings: h5-together.ini: 3 line(s) changed; Windows reads notify.popup=(none), camera.zoom_out=200, rmg.teams=humans_vs_ai`.
- **One table of every setting** (`settings_table.cpp`): section, key, default, kind, limits, who it matters to, when it applies, page, words. The menu is drawn from it, and `src/hook/test/settings_test.cpp` checks it against the shipped `tools/package/files/h5-together.ini` (the same 108 settings, the same defaults), every row's words in `en.txt` and `fr.txt`, the in-place writer (the cases of `tools/package/test_scripts.py`, and Windows' own reading of the result) and our category in a stand-in for the game's file. **A new setting:** one line in the table, its line in the shipped ini, and for a row its words in `en.txt` and `fr.txt`.

**The H5 Together version in the main menu:** MMH5.5's "Version: 5.69k" text (`UI/MainMenu2/Version.txt`) gets a first line, `H5 Together 0.3.0`, in the game's own font: everyone sees their version before a co-op game. It is a text, not a button: a button of ours there would not look like the game's (the game's own buttons are on the scroll, and our overlay has no game font), so the way in is the Options screen.

**The first-start question** (the update check, off by default): the first time the main menu shows, a box in the game rules panel's look asks "Check for new versions of H5 Together once a day? Yes / No". Yes writes `[update] check=1`; both answers write `[update] asked=1`, so it is asked only once; Esc (or another screen) answers nothing, and it is asked again at the next start. It is not asked when `[update] check` is already in the file, and never in robot runs (unless `[update] welcome_test=1`).

**Robot steps** (`tools/robot/README.md`, "Checking the settings menu"): `key|F3`, `panel|settings open`, `panel|settings set <section>.<key> <value>` (as a click on the row), `panel|settings scroll <page> <line>`, `panel|settings default` (the Default box's Yes), `panel|settings welcome yes|no|close`, then the game's own `ui|NUI::CGameOptionsScreen accept`. Checked 2026-11-02 on the French test copy (MMH5.5 RC19k), in French, English and the long test language: every page, Apply, Default, F3 from the main menu, a new builder key used at once, the question, and no `h5t_` line in the profile's `user_a2.cfg`.

**Settings** (`h5-together.ini`):

```ini
[update]
asked=         ; written by the add-on: 1 once the first-start question was answered
welcome_test=0 ; testing only (not in the shipped file): ask the question in robot runs too
```

## Steam test (`steam_spike.cpp`; off by default, for tests only)

A test for online play over Steam ([docs/plans/online-steam.md](../../docs/plans/online-steam.md)). It starts Steam inside the game under the game's own Steam id (15370), and logs whether Steam's relays, lobbies and messages work (lines starting with `steam:`). It needs Valve's `steam_api.dll` (32-bit, from the Steamworks SDK) in `bin\h5-together\`. Without that file, or with `spike=0`, nothing changes. It sends no game data. It stays off with `[online] mode=steam` (the virtual LAN uses Steam then). Steam is started by `steam_runtime.cpp`, shared with the virtual LAN.

**Settings** (`h5-together.ini`):

```ini
[online]
spike=0              ; 1 = solo test on one PC; host / join = the two-PC test (one PC each)
app_id=15370         ; 480 = Valve's test app "Spacewar", only to compare
spike_seconds=       ; how long it runs (solo 60, host 900, join 300)
spike_lobby=friends  ; the test lobby: friends | public | invisible
spike_pings=100      ; join: test messages sent to the host and answered
spike_ping_bytes=300
```

## Virtual LAN (`virtual_lan*.cpp`; off by default, being built)

The core of online play over Steam ([docs/plans/online-steam.md](../../docs/plans/online-steam.md), steps 2 and 3). The game keeps its own LAN play; the add-on carries its LAN packets somewhere else: **over Steam, between Steam friends** (`mode=steam`, `virtual_lan_steam.cpp`), or through a **loopback transport** for the robot's tests (two copies of the game on one PC, `virtual_lan_loopback.cpp`). With `mode=off` (the default) nothing is hooked and nothing changes. A friend's game shows up in the game's own LAN list; what else the player sees is [the online UI](#online-play-over-steam-the-ui-online_uicpp) (step 4).

How it works, in plain words:

- **Fake addresses.** Every player gets a made-up address in `198.18.0.0/15` (a range set aside for network tests, so it never clashes with a real network or ZeroTier), computed from their id (their SteamID; the loopback makes one up from its port). Every PC computes the same address for the same player. Each message also says which address its sender's game uses, and the others take that one: so two players whose ids give the same address (about 1 in 20,000 for 4 players) still agree. A PC moves off an address it knows to be taken, but only until its game has told its address to anybody.
- **The game only ever sees fake addresses.** When it asks for its own addresses (`gethostbyname` of its own host name), the add-on answers with this PC's fake address only. The game writes its addresses into its packets itself (the join request `01`, the game description `03`, the player records of the lobby; see [multiplayer-sync.md](../../docs/subsystems/multiplayer-sync.md#addresses-inside-the-packets-2026-11-02)), so they all hold the fake one: no packet is rewritten.
- **Sending** (`sendto`): a packet to a fake address goes to that player through the transport, with the game's destination port as the "channel" and a 6-byte header holding the source port and the sender's own fake address. An "any games here?" query (`02`) to a broadcast goes to every host the transport knows. Everything else (real addresses, other broadcasts, `lan_discovery`'s copies to the network adapters) is dropped: **the real network is not used** while the virtual LAN is on.
- **Receiving** (`recvfrom`): the next message for the socket's port, as if it came from the sender's fake address and source port, however long it waited (since test9 of 0.3.5; before, one older than 3 s was dropped, and that lost a Linux joiner's "accepted" answer). A new socket starts empty: what was sent to its port before its `bind` is dropped there, once. What the real network brings is read and dropped.
- **Who hosts.** Once a second the game's discovery socket gets a copy of its own last query, as if from `198.18.0.1` (an address nobody has). A game that hosts answers with its game description; the add-on swallows that answer and tells the transport (Steam: it makes a lobby; the loopback: it tells the other copies).
- The hooks sit under `lan_discovery`'s and the robot's (they go in when the DLL loads), so the robot's network log shows the packets exactly as the game sends and gets them.

**Over Steam** (`mode=steam`, `virtual_lan_steam.cpp`):

- **Steam starts** at the game's first DirectInput call (long before the LAN screen), from `bin\h5-together\steam_api.dll`, as the game's own Steam app (15370), also when Steam did not start the game (`steam_runtime.cpp`). Without that file or without Steam, the log says so and the game plays its own LAN, as with `mode=off`. It never crashes the game for that.
- **Messages** go with Steam's `ISteamNetworkingMessages`: to a friend's SteamID, on channel = the game's port, unreliable (the game resends what it needs, as on a LAN). Steam finds a direct path or uses its relays; real IP addresses are never in the game's packets.
- **The host's lobby.** When the game hosts, the add-on makes a **friends-only Steam lobby**. Its data holds our protocol number, H5 Together's version, the game's build and MMH5.5's `55u.dll` (a hash: only the same game can play together), the game's discovery port and the fake addresses in the game. It is kept while the game hosts and for a minute after.
- **Finding games.** Every 2 s the add-on looks at the Steam friends list: Steam tells which lobby a friend is in. A friend whose lobby says that friend hosts the same game is a host, and the game's "any games here?" queries go to it; its game then shows in the **game's own LAN list**. (If a lobby's data does not come, the friend's game is tried anyway at the usual port 8889.)
- **Joining.** When the game sends a host more than queries (its join request), the add-on joins the host's lobby; the lobby's members are the other players, in the lobby's order. A friend's first message is accepted only from Steam friends and members of our lobby.
- **Threads.** Steam's callbacks run on a thread of the add-on's own every 20 ms, under the same lock as the hooks; never on the game's threads.

The log says what it does (lines starting with `vlan:` and `vlan steam:`): this PC's fake address, each new peer, when this PC starts or stops hosting, the lobby, the friends' games, sessions, and every 30 to 60 s the packets carried and dropped. **No SteamID or name** goes to the log: players and lobbies show as short hashes like `#3f2a`.

**Settings** (`h5-together.ini`):

```ini
[online]
mode=off             ; off (default) | steam (Options > H5 Together > Online play; being tested) | loopback (robot tests only)
app_id=15370         ; steam: the game's Steam app (only for tests: 480 = Valve's Spacewar)
loopback_port=47100  ; loopback: this copy's own port on 127.0.0.1
loopback_peers=      ; loopback: the other copies' ports, e.g. 47102
```

`python tools\robot\run_lan_test.py --vlan` sets these for the robot's host and client (ports 47101 and 47102) and turns off the robot's own broadcast copies, so the client can only find the host through the virtual LAN. `tools/robot/packet_map.py` then shows where the addresses sit in the packets, and the robot's address scan (`--net-scan`) counts packets holding a real address of this PC (it must be 0).

## Online play over Steam: the UI (`online_ui.cpp`)

Step 4 of [docs/plans/online-steam.md](../../docs/plans/online-steam.md#step-4-the-online-ui-2026-11-03): what a player sees of online play, in the game's own look. With `[online] mode=off` (the default) none of it shows.

- **Turning it on:** Options > H5 Together, page 2, "Online play": Off (local network) / Steam. It applies after a restart of the game (the hooks and Steam start long before the LAN screen). The line under it says where friends' games show, or that `bin\h5-together\steam_api.dll` is missing (a GOG copy: Steam cannot start; nothing else changes).
- **Multiplayer > LAN:** a few lines in the empty box under the games list: whether Steam works (if not: why, and that the list shows the local network as before), how many Steam friends play with H5 Together (their rich presence `h5t_version`, set by every PC with `mode=steam`), invites, and friends' games this PC cannot join (another MMH5.5 or game build, another H5 Together protocol). The games themselves are in the game's own list.
- **The waiting room (host):** "Invite friends..." next to "Rules...". It opens Steam's invite dialog when Steam's overlay is enabled in the game (it was 5 s after Steam started, in robot runs), else, or if the overlay does not open within 2.5 s, our own list of the Steam friends who are online, each with an "Invite" button (`InviteUserToLobby`).
- **An invite** (accepted in Steam, "Join game" in Steam's friends list, or Steam starting the game with `+connect_lobby`): a banner on the main menu for 20 s ("Pierre invites you to play: open Multiplayer > LAN."), and when the game is in the background the taskbar flash, the sound and the pop-up of the "your turn" notification (`[notify] sound=`, `popup=`). The host's lobby is read without joining it, and its game shows in the LAN list.
- **Then the game joins by itself** (`online_join.cpp`): from the main menu (now, or when the player comes back to it within 10 minutes) it opens Multiplayer > LAN with the Multiplayer page's own command, waits for the host's game in the list (the row whose game data holds the host's address), chooses it and presses Join, as the player would. A line on the LAN screen says what it does; if the game does not show in 20 s, or Join fails (a message box, or 30 s), the line says what to do and the player stays there. Never in a game, the waiting room or any other screen; never under a message box; once per invite.
- **The game's message boxes:** the LAN screen's error boxes ("Timeout... Error code 0.18.0") are not screens of the game's list, so `online_join.cpp` watches them (a hook on the call sites of their constructor, `NUI::CCommonMsgBox`); while one is shown, nothing of ours is drawn and the auto-join waits.

The Steam calls are made by the Steam transport's thread (`virtual_lan_steam.cpp`); the UI reads a status under the virtual LAN's lock and only asks for the invites. The words are the `[online]` texts of `src/mods/texts/*.txt`. No Steam name or SteamID goes to the log.

**For tests** (`h5-together.ini`, not in the shipped file): `[online] ui_test=1` shows made-up friends (two with H5 Together, a game of another MMH5.5, one of another H5 Together, six friends in the list); inviting them calls no Steam function. Robot steps: `panel|online invite` (as a click on the button), `panel|online picker` (our list), `panel|online close`, `panel|online test_invite` and `panel|online pick <row>` (both only with `ui_test=1`), `panel|online test_autojoin [name]` (an invite to the first host this PC hears of: the auto-join; also with `mode=loopback`, where the online UI does only that), `panel|online games` (the LAN list's rows and where a fake address sits in their data). `[online] test_drop_join=1` (hidden) makes the virtual LAN lose this PC's join requests, for the game's "Timeout" box. Two copies: `python tools\robot\run_lan_test.py --vlan --client-autojoin 20 [--client-online test_drop_join=1]`. Example (the screenshots of step 4, with `steam_api.dll` in the test copy):

```
python tools\robot\run_ai_test.py --game C:\Games\H5_test_k --no-map --seconds 58 --shot-every 0 --online-setting mode=steam --online-setting ui_test=1 --setvar "lan_last_login = Robot-host" --step "8|panel|online test_invite" --step "10|shot|menu-invite-banner" --step "12|key|F3" --step "16|shot|options-online-row" --step "17|ui|NUI::CGameOptionsScreen options_back" --step "19|menu|multi lan" --step "26|shot|lan-status" --step "28|ui|NUI::CLobbyScreen create" --step "32|map|A1M4/" --step "34|msg|create" --step "42|shot|waiting-room-invite" --step "43|panel|online invite" --step "45|shot|invite-list" --step "46|panel|online pick 0" --step "47|panel|online pick 2" --step "49|shot|invite-list-invited" --step "50|panel|online close"
```
