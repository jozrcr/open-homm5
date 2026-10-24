# Ideas waiting their turn

Features the owner asked for, in the order agreed (2026-10-25: the map generator first). Each gets its own plan when its turn comes.

| Idea | From | Notes |
|---|---|---|
| **Map generator** (in progress) | owner, 2026-10-24 | [map-generator.md](map-generator.md). Step 1 is the template maker, step 2 lobby teams, step 3 decompiling the generator. **Its templates also go into the game's own list:** the DLL serves them through the game's file system. Each name is the recipe, so every PC rebuilds the same template. |
| **One language for the whole game** | owner, 2026-10-25 | The mix comes from three layers. Heroes V's texts are French (`texts.pak`, 15,349 files). The ToE 3.1 texts are English (`a2p1-texts.pak`, 253 files). MMH5.5's texts are English (`MMH55-Texts-EN.pak`, 5,805 files), and they replace many of the French ones. **The owner's choice: each player picks** French or English (a setting). The DLL swaps texts as the game reads them, as the music switch does, so no game file changes. French needs a translation of the English texts, using the game's own French texts as the glossary. English needs the English base texts, which may come from Steam's English version. |
| **Player list, always visible** | owner, 2026-10-25 | Every player with their colour and turn state (playing, waiting, done), ideally with their hero's portrait. The DLL draws it over the game. The turn state comes from the game's player and network data; `docs/subsystems/multiplayer-sync.md` covers part of it. Portraits come later, since they mean reading the game's images. |
| **Decompilation** (paused) | the plan | Paused after the 2026-10-25 night batch, at the owner's request. Resume with the generator's code (map generator step 3). |
| **Smarter AI** (deferred) | owner, 2026-10-24 | After they have played a lot. |
| **Presentation** (to study) | owner, 2026-10-24 | Better camera (farther, freer), better visuals (lighting). |
| **Installer checks MMH5.5** | owner, 2026-10-24 | [install-and-join.md](install-and-join.md), "Later". |
