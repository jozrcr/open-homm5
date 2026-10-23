// Redirecting single call instructions of the game ("call rel32") to our code.
// Several hooks may stack on one call site (a mod and a robot trace, say): each
// one calls the target that was there before it.
#pragma once
#include <windows.h>

namespace hookpatch {

// Redirect the call at `site` to `replacement` if it calls `original`, or a hook
// that was put over `original` with this function. Returns the previous target,
// which `replacement` must call in turn, or 0 when the site is not as expected
// (another build: then nothing is changed).
DWORD RedirectCall(DWORD site, DWORD original, const void *replacement);

// The previous target recorded when `replacement` was put on `site` (0 = none).
// For a replacement installed on several sites: call it with the site from
// _ReturnAddress() - 5.
DWORD PreviousTarget(DWORD site, const void *replacement);

}  // namespace hookpatch
