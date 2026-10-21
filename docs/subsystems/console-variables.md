# Console variables

The game has a console variable system (like Quake's cvars). Variables are set with `setvar <name> = <value>` in `.cfg` files (for example `profiles/default_profile/user_a2.cfg` has `setvar ai_time_limit = 180000`) and presumably from the in-game console. Many debug and tuning switches are variables, so they are cheap levers for experiments.

## How variables are registered (for finding the global behind a name)

Each variable is registered by a small global-setup function (no callers, in the init region of the exe) with this pattern:

```c
FUN_00401b20("ai_time_limit", "");          // makes the name string
FUN_00c11400(&DAT_00f440d0, &default, 2);   // binds it to a global; last argument = type (0, 2 seen)
```

To find the global for a name: find the function that references the name string (`analysis/export/func_strings.tsv`, or `tools/inventory/ghidra/XrefsTo.java` on the string), dump it (`tools/inventory/dump.ps1 <address>`), and read the first argument of the `FUN_00c11400` call that follows. Ghidra cannot see writes to these globals (they go through the pointer), only reads.

## Known variables and their globals

| Variable | Global | Registered at | Used by |
|---|---|---|---|
| `ai_time_limit` | `0x00f440d0` (type 2) | `0x0086eb60` | AI turn driver `0x0086dd40`: whole-turn limit in ms |
| `aai_step_limit` | `0x00f440d4` (type 0) | `0x0086eb60` | AI turn driver: per-call thinking budget in ms |
| `mp_check_crc` | `0x00fe4d18` | `0x007cb230` | desync check `0x007c9410` |
| `mp_check_object_crc` | `0x00fe4d19` | `0x007cb230` | desync check `0x007c9410` (per-object report) |
| `mp_observer_mode_for_player2` | `0x00fe4d1a` | `0x007cb230` | |
| `net_timeout` | `0x00f27394`, default 20 | `0x004039a0` | `0x00402a80`, `0x00402c60` (network driver), seconds |
| `net_batched_segments` | `0x00f27398`, default 3 | `0x004039a0` | `0x00402440`: how many empty segments a PC may hold back before reporting them (see multiplayer-sync.md) |

Names whose globals are not looked up yet: `aai_proceed`, `aai_testmap`, `adventure_ai_timing` (`0x00609910`), `town_ai_log`, `show_ai_fog`, `show_ai_geometry`, `combat_ai_lookahead`, `combat_ai_retreat_value`, `combat_ai_use_magic_for_human`, `log_random_calls`, `log_game_calcs`, `log_client`, `net_game_port`, `net_load_timeout`, `net_multiplayer_init_timeout`, `net_lan_match_maker_port`, `net_tournament_active`. Console commands (not variables): `self_adventure_ai`, `aai_play_for_humans` (`0x00590830`).
