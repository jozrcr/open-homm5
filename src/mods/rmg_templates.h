// Mod: our random map templates in the game's own list (see rmg_templates.cpp).
#pragma once

#include <string>

namespace mods {

// With the settings of open-homm5.ini: [rmg] templates=1 (default). Reads the
// .xdb files of open-homm5\templates next to the exe, and serves recipe names
// (RMG/Templates/OH-X1-....xdb, built from the name) plus those of [rmg]
// recipes=. Also sets up joining random map games (rmg_join.cpp). Only after
// DetectGameBuild() said the build is hookable.
void ApplyRmgTemplatesFromSettings();

// Adds a recipe name (as rmg::CanonicalRecipe gives it) to those the game's
// template list offers; true when it was not there yet. Any thread.
bool RegisterRecipe(const std::string &recipe);

// Are recipe names served (templates=1 and the maker's self-check passed)?
bool RecipesServed();

// A game file stream (CMemoryStream, the game's allocator) holding `data`, as
// the file system's Open returns one; nullptr when out of memory.
void *NewGameStream(const std::string &data);

}  // namespace mods
