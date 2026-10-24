# A better random map generator: plan

Status: **agreed on 2026-10-25** (Q9 in [../questions.md](../questions.md)). Findings behind it: [../subsystems/rmg.md](../subsystems/rmg.md).

**The owner's choices:**
- **Route:** A + B now, C later.
- **First maps:**
  - 2v2v2 with neutral zones, without and with extra towns in them;
  - 2 vs 2;
  - more neutral zones;
  - bigger and richer maps.
- **The template maker** can be a separate page.
- **Teams are chosen in the lobby**, like on normal maps, not in an ini setting. Step 2 starts by finding how the lobby handles teams on random maps, and what it takes to allow them there.

## What we want

The owner's pain points with the random map generator (2026-10-24):
- **No team maps.** For example, the three of us against the AI, or 2 vs 2.
- **Almost no control.** You can't choose how many neutral zones there are, how much loot there is, and so on.
- **Almost no way to make your own.**

Heroes III players solved this with templates and template editors (HotA's editor, VCMI). We want the same, ideally inside the game.

## What we found

1. **The generator follows templates.**
   - A template is a list of zones and their connections:
     - start zones, and neutral zones with their size;
     - each zone's towns, mines, dwellings and treasure (total value and densities), plus shops, shrines and bonus buildings;
     - how strongly each connection is guarded.
   - MMH5.5 already ships 94 templates, including some team-shaped ones.
   - **The number of neutral zones, the loot and the guards are all template data.** No code is needed for them, only better templates.
2. **Teams are the one big hole.** The game fully supports teams, but the generator never sets them: every player of a random map is alone.
   - Today's workaround: open the map in the editor, set the teams, save it as a normal map.
   - The cost: in multiplayer, every PC normally rebuilds the random map itself from the template and a seed. A map edited this way can't be rebuilt, so it has to be sent around as a file.
3. **New options on the generator screen need code.** The screen's list of options comes from data, but what each option *does* is fixed in the exe: map size, monster level, resources, experience, water, players.
4. **The generator's code is small:** about 400 functions, 1.5% of the game. That's within reach of our decompilation pipeline: roughly 8 to 15 runs like tonight's.
5. **Multiplayer rule:** since every PC builds the map itself, every player needs the same templates. If we change anything, every player needs our DLL too, with the same settings. That's the same idea as the co-op timing guard.

## The options

| | What it is | Gets you | Effort |
|---|---|---|---|
| **A. Template maker** | A page where you pick players, teams, size, number of neutral zones, loot level and guard strength; it writes a template. Plus a set of ready templates for our group. | Neutral zones, loot, guards, layouts for teams | Small (1–2 sessions) |
| **B. DLL features on the game's generator** | Teams on random maps (for example "humans vs AI", or "1,1,1,2,2,2"), applied the same way on every PC. Templates loaded from our own folder, so the game folder is never touched. A check that every player has the same templates. | **Real team games** that still rebuild on every PC; easy sharing | Medium (2–4 sessions, some research first) |
| **C. Decompile and extend the generator** | Make the generator ours, function by function, then add native options: loot level, neutral zones, monster strength per zone, new map sizes; better team placement; fixes. | Everything, inside the game | Large (the pipeline for the code, then the features) |
| D. Our own generator writing map files | A new generator outside the game, writing `.h5m` maps | Total freedom | Very large; one file format still unknown; nobody has done it for Heroes V. **Not recommended.** |

## Recommendation: A + B now, C after

1. **Step 1, the template maker (A), with ready templates:**
   - 3 allied humans vs 3 AI;
   - 2 vs 2;
   - a free-for-all with more neutral zones;
   - each in a few sizes and loot levels.

   You test them, and we adjust.
2. **Step 2, teams on random maps (B), chosen in the lobby** (the owner's choice):
   - First find how the lobby handles teams, and why it doesn't offer them on random maps. Use the exe, the UI data, and the robot on the test copy.
   - Then make the DLL allow team choice there, keeping multiplayer rebuilding working on every PC. Every player needs the DLL, checked as the co-op guard does.
3. **Step 3, the generator's code (C):**
   - Point the decompilation pipeline at the generator instead of the network code.
   - Then add options to the generator screen, such as sliders for loot and neutral zones.
   - This is where it becomes a "proper" generator, like HotA's.

**Why this order:** steps 1 and 2 answer the pain points within days, and with no risk to the game. Step 3 takes weeks, but makes everything possible later, and reuses what we just built.

## Not yet known (first things to test)

- That a new template file shows up in the game's template list, and where it can live.
- Whether the host can change teams in the lobby for a random map, and whether multiplayer rebuilding survives a team change.
- What MMH5.5's "monster / resources / experience" steps really multiply.
