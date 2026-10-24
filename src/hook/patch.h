// Redirecting call instructions of the game ("call rel32") to our code.
// Several hooks may stack on one call site (a mod, then a robot trace): each
// one calls the target that was there before it.
#pragma once
#include <windows.h>

namespace hookpatch {

// Redirect the calls at `sites` (all of them, or none) to `replacement`. Each
// site must call `original`, or a hook that was put over `original` with this
// function, and all must call the same target. Returns that previous target,
// which `replacement` must call in turn, or 0 when the sites are not as
// expected (another build: then nothing is changed).
//
// The replacement gets one "next" for all its sites, so it must not use its
// return address to pick one: when another hook is stacked on top, the return
// address is in that hook, not at the game's call site.
DWORD RedirectCalls(const DWORD *sites, int count, DWORD original, const void *replacement);

inline DWORD RedirectCall(DWORD site, DWORD original, const void *replacement)
{
	return RedirectCalls(&site, 1, original, replacement);
}

// The same for a pointer to code in a table, such as a vtable slot: the slot
// must hold `original`, or a hook put over `original` with this function.
// Returns what it held (to be called in turn), or 0 when it is not as expected.
DWORD PatchPointer(DWORD slot_address, DWORD original, const void *replacement);

}  // namespace hookpatch
