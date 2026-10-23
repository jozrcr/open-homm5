// All hooks of the DLL. Only call InstallAll() after DetectGameBuild() said the
// build is hookable.
#pragma once

namespace hooks {

void InstallAll();

}  // namespace hooks
