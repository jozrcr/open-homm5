// All hooks of the DLL. Only call InstallAll() after DetectGameBuild() said the
// build is hookable.
#pragma once

namespace hooks {

void InstallAll();

// When the DLL is loaded, before the game runs: the few hooks that must be in
// place before the game reads its data. Checks the build itself.
void InstallEarly();

}  // namespace hooks
