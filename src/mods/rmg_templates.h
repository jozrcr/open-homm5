// Mod: our random map templates in the game's own list (see rmg_templates.cpp).
#pragma once

namespace mods {

// With the settings of open-homm5.ini: [rmg] templates=1 (default). Reads the
// .xdb files of open-homm5\templates next to the exe, and serves recipe names
// (RMG/Templates/OH-X1-....xdb, built from the name) plus those of [rmg]
// recipes=. Only after DetectGameBuild() said the build is hookable.
void ApplyRmgTemplatesFromSettings();

}  // namespace mods
