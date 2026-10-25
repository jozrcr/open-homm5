# Camera: how far it may zoom out

Status: **checked in a running game** (2026-10-25: Ghidra on the vanilla 3.1 exe, the game's config files and MMH5.5's files, then robot runs of `MMH55_Utility_64.exe` with the camera mod, see "Robot checks" at the end). Addresses are in the vanilla 3.1 exe; they are the same in `MMH55_Utility(_64).exe` (the co-op exe: MMH5.5 changes nothing here, see below).

The mod that uses this: `src/mods/camera.cpp` (`[camera] zoom_out`, `combat_zoom_out`; [src/mods/README.md](../../src/mods/README.md)).

## Big picture

The game has several cameras: the **adventure map** ("mission" camera in the code), the campaign's **conquest map**, **battles** (the "arena"), the **town** screens and cut-scenes. Each one is set up by a small *configurator* object that gives the camera its limits: the nearest and farthest distance from the point it looks at, the tilt range and the turn range.

Distances are in world units. A map tile is 2 units (the configurators double the map size to get the camera's bounds).

## Adventure map

### Where the limits come from

From a text file in the game folder, **`profiles\autoexec_a2.cfg`**, which the game runs at start-up (the exe names it at `0x00e493e0`). It is a list of console commands. The camera lines:

```
//mission_camera_def_pos Fov Pitch Yaw fRod(Distance)
mission_camera_def_pos 50 -53 16 25
// mission_camera_limits fov minDistance maxDistance minPitch maxPitch minYaw maxYaw
mission_camera_limits 50 10 70 -55 -30 0 0
mission_camera_softlimits 50 11 69 -53 -32 0 0
//No camera limits
//mission_camera_limits 50 -100 1000 -100 100 0 0
// verfical fixed camera
mission_camera_vertical_fixed_limits 50 37 70 -65 -65 0 0
mission_camera_vertical_fixed_softlimits 50 38 69 -65 -65 0 0
// classic like camera
mission_camera_classic_like_limits 50 37 70 -65 -65 0 0.001
mission_camera_classic_like_softlimits 50 38 69 -65 -65 0 0.001
// mini-dialog camera
mission_mini_dialog_camera_limits 50 10 70 -55 -30 0 0
mission_mini_dialog_camera_softlimits 50 11 69 -53 -32 0 0
```

| Camera mode (options screen, "Mode de caméra") | `adv_camera_mode` | Distance | Tilt (degrees below the horizon) |
|---|---|---|---|
| Normal | 0 | 10 to 70 (soft 11 to 69) | 30 to 55 |
| Vertically fixed ("Fixe verticalement") | 1 | 37 to 70 (soft 38 to 69) | 65, fixed |
| Classic HoMM view ("Vue HOMM classique") | 2 | 37 to 70 (soft 38 to 69) | 65, fixed |
| (mini dialogs) | 3 | 10 to 70 (soft 11 to 69) | 30 to 55 |

All modes have a field of view of 50 and a **farthest distance of 70**. The soft limits are where the camera springs back to after you push it past them. The camera starts at distance 25 (`mission_camera_def_pos`), tilted 53°, turned 16°. The configurator also gives the camera the pair 1 and 1000 (`0x007d33be`), probably the near and far clip planes, far beyond 70.

**Vanilla and MMH5.5 are the same here.** `autoexec_a2.cfg` is the Steam file of 2021 (same date as the other Steam files), MMH5.5 does not ship one, no `.pak` has a `.cfg`, and neither MMH5.5's exe patches nor its DLL (`55u.dll`) touch this code (checked: no address of it in `55u.dll`). The owner's profile uses the **vertically fixed** mode (`setvar adv_camera_mode = 1` in `Profiles\Joz\user_a2.cfg`): the camera always looks down steeply (65°), which shows less of the map than the normal mode's flatter angles. Together with the farthest distance of 70, that is probably the "very zoomed in" feeling.

### Code

| What | Address | Notes |
|---|---|---|
| `mission_camera_limits` (and the other 9 limit commands) | `0x0058a8d0` ... `0x0058b410` | Check for 7 arguments, then the parser. Registered at `0x0058b470`. |
| Limit parser | `0x0058a790` | `EAX` = the arguments, `EDI` = the record, one pushed pointer for the field of view. Stores distance, distance, then tilt and turn × π/180 (`0x00e21210`). |
| Limit records | `0x00f33900` normal, `0x00f33a18` vertically fixed, `0x00f33a90` classic, `0x00f33b08` mini dialog; each soft twin `0x3c` later (`0x00f3393c`, `0x00f33a54`, `0x00f33acc`, `0x00f33b44`) | 15 floats: [0] nearest, [1] farthest, [2] [3] tilt range, [4] [5] turn range (radians), [6] attenuation (`mission_camera_attenuation`), [7] lowest height (`mission_camera_minheight`), the rest unknown. |
| Field of view | `0x00f338f8` (normal mode), `0x00f33b80` / `b84` / `b88` (modes 1–3) | Read by `0x0058a680`. |
| Default position | `0x00f339f0` | distance, tilt `+4`, turn `+8`, field of view `+0x10` (`mission_camera_def_pos`, `0x0058a930`). |
| "The record of this mode" | `0x0058a5e0` (limits), `0x0058a630` (soft limits) | `__fastcall(int mode)`, returns the record; mode 1000 = the current `adv_camera_mode` (`0x00fd7158`). Their only callers are the configurator below. |
| `CAdvMapCameraConfigurator` | vftable `0x00e44688`; slot 7 `0x007d3180` (the camera operator), slot 8 `0x007d3310` (the camera) | Byte `+0x14` set = a mini dialog (mode 3), otherwise mode 1000. Slot 8 also sets the default position, the field of view, the bounds (map size × 2) and probably the clip planes (1, 1000). Calls: `0x007d31fa`, `0x007d3332` (limits), `0x007d321b`, `0x007d3353` (soft limits). |
| Camera mode change | `adv_camera_mode_changed` `0x00593340` → `0x00593240` | Makes a new configurator (`0x007d3950`) and runs its slot 7 again. |

## Battles

The battle camera's limits are not in a config file. `CArenaCameraConfigurator` (vftable `0x00e446b0`, slot 7 `0x007d3730`) builds them each time a battle starts: it copies the adventure map's normal record as a template, then sets

- nearest distance: `0x007d35a0`; farthest distance: `0x007d3650` (called once, at `0x007d386b`);
- tilt 35° to 60° (`-1.0471976`, `-0.61086524` rad, immediates at `0x007d3887`, `0x007d3895`), turn ±90° (`0x007d389d`, `0x007d38a5`),

and gives the same record to the camera as limits and as soft limits.

The two distance functions ask the arena for its size (vtable slot `0x44`), look the first number up in a small table and double the value (tiles to world units):

| Arena size code | Nearest | Farthest |
|---|---|---|
| 11 | 2 × 6.6 | 2 × 11 |
| 14 | 2 × 7 | 2 × 17.5 |
| 16 | 2 × 5 | 2 × 21 |
| 18, 20 | 2 × 14.4 | 2 × 28 |
| 24 and all others | 2 × 18 | 2 × 30 |

The size code is probably the battlefield's width plus 4 (the configurator's bounds use `size − 4`): 16 for the game's 12×10 field, 18 for MMH5.5's 14×12 field (below). In a robot battle with MMH5.5 the game's farthest distance was 56 (2 × 28), the row of codes 18 and 20, which fits. On screens taller than 4:3 the farthest distance also grows by √(height/width × 4/3); not on wide screens.

**MMH5.5 changes the battle camera.** Its `55u.dll` (loaded by the co-op exe in place of Ubisoft's `um.dll`) reads `<CombatCameraLimits>` from `MMH55Stats.xml` (in `MMH55-Stats.pak`) and patches, at run time, the operands at `0x007d3602` (nearest distance of the code-18/20 row), `0x007d36a7` (farthest distance of that row) and the four angle immediates above. MMH5.5's values: nearest 8 (the game: 14.4), farthest 28 (the game's own), tilt −1.6 to −0.3 rad (about 17° to 92°). MMH5.5 also makes the battlefield 14×12 (`<BattlefieldSize>`). So in MMH5.5's battles the camera may come closer and tilt more, and goes as far as the game's own value for that field size.

## Is it safe to zoom out farther?

**Desync:** the limits are only read by the camera configurators above, which hand them to the camera objects. Cameras are not part of the game world (`NWorld`): they are not in the world checksum (the world's own slot 4, [multiplayer-sync.md](multiplayer-sync.md)), and no world command carries a camera position (the only camera-related world class is `NWorld::CScriptDialogEntry_MoveCamera`, a map script moving the camera, the other way round). So each PC may use its own zoom. The robot's LAN game agrees (below).

**What to watch** when the camera goes farther than the game allows:

- **Map edges:** near a corner you see past the end of the map (as you already do a little at the game's 70). At 200 (`zoom_out=300` with a far start position) the top of the screen showed the sky beyond the small test map's edge.
- **Performance:** the visible ground grows with the square of the distance (150% → about 2.25 times as much map on screen), so more objects are drawn. Big maps on slow PCs may lose frames. Frames do not change the game, only how smooth it is. Not measured (the robot's test map is small and mostly unexplored).
- **Scrolling:** `adventure_scroll_speed_zoom_factor = 0` (autoexec) means scrolling does not speed up with the distance, so far out the map seems to scroll slowly.
- **Fog of war:** drawn as before (unexplored land stays black around the explored part); nothing else looked wrong in the robot's screenshots up to 200.

## Without the add-on

The same farther zoom can be had by editing the `mission_camera_*limits` lines of `profiles\autoexec_a2.cfg` (the farthest distance is the third number). That changes a game file (Steam's "verify files" puts the original back) and works in every exe, MMH5.5's single-player exe (`MMH55_64.exe`, 3.1j) included, where the add-on installs no hooks. The battle camera has no such file.

## Robot checks (2026-10-25, `MMH55_Utility_64.exe`, map A1M4, 1024×768)

The robot cannot turn the mouse wheel (its windows are in the background), so it moves the camera's start position far out before the map starts (`mission_camera_def_pos 50 -53 16 200`). The game puts the camera at the farthest distance its limits allow, so the screenshots show exactly that limit. Commands: [tools/robot/README.md](../../tools/robot/README.md), "Checking the camera zoom".

| Run | Camera at | What the screenshots show |
|---|---|---|
| no change (start position 25) | 25 | the game's usual close start view |
| far start, the game's limits | 70 | the farthest the game allows |
| far start, `zoom_out=150` | 105 | the same place (day 2, the same town) from half as far again: everything smaller, the minimap's view frame bigger |
| far start, "vertically fixed" mode (`adv_camera_mode = 1`), the game's limits / `zoom_out=150` | 70 / 105 | the same, with the fixed steep angle; log: `vertically fixed camera: farthest zoom 70 -> 105 (nearest 37)` |
| far start, `zoom_out=300` | 200 (the start position, under the limit of 210) | works; the sky shows above the small map's far edge |
| 24 AI turns, `combat_zoom_out=150`, one battle (the AI attacked the robot's hero) | battle limit 84 | log `camera: battle: farthest zoom 56 -> 84`; the battle ran to its end with auto-combat. The battle camera starts at its usual place, so the farther limit itself is not visible without a player's mouse wheel. |
| **LAN game** (2 copies, 2 humans + 1 AI, 4 AI turns): only the host with `zoom_out=200`, `combat_zoom_out=200` and the far start | host 140, client 25 | **698 world checksums, all equal on both copies (no desync)** |

Not checked by the robot: zooming with the mouse wheel during play (a player's test), the frame rate on a big explored map, Linux.
