# Decompiled source

Reconstructed C++ source of the vanilla ToE 3.1 exe, one folder per original source file ("translation unit", TU). A function counts as **done** only when `tools/verify/verify.py` says MATCH (see `tools/verify/README.md`).

## A unit

```
src/decomp/GameTimer/
    GameTimer.cpp    the reconstructed code
    unit.json        which exe address each function must match
```

`unit.json`:

```json
{
  "sources": ["GameTimer.cpp"],
  "subsystem": "",
  "functions": {
    "00401000": "?GetStepStartPtr@CGameTimer@NGameTimer@@UAEPAIXZ"
  }
}
```

The keys are exe addresses, the values the compiler's mangled names (get them with `python tools/verify/verify.py --list <Unit>`). Put a `// FUNCTION: 0x00401000` comment above each function in the source too, so humans can find it.

## Workflow for one function

1. Dump it: `tools/inventory/ghidra/DumpFunctions.java` writes Ghidra's disassembly and decompiled C to `analysis/export/dump/<address>.txt` (gitignored).
2. Write the C++ in the right unit. Declare classes with the members and virtual functions the code uses, at the right offsets and vtable slots (`analysis/classes.txt` lists each class's vtables).
3. Add the address and mangled name to `unit.json`, run the verifier, and iterate until MATCH.
4. If it calls or references something not yet decompiled, declare it and add its address to `progress/symbols.txt`.
5. `python tools/verify/verify.py --record`, then `python tools/inventory/make_progress.py`, and commit.

## Rules (from CLAUDE.md)

- Byte-matching is the definition of done. Readable but unverified code is a draft.
- No inline assembly, naked functions, embedded bytes, object patching, or `#pragma` tricks aimed at the checker.
- Keep the original behaviour, bugs included. Improvements go in `src/mods/`.
- Unit (file) names are guesses until we find evidence of the real names. Say so in the file header.

## Status

| Unit | Matched | Notes |
|---|---:|---|
| GameTimer | 13 | `NGameTimer::CGameTimer` methods at the very start of the exe; the first functions matched (2026-10-20). |
