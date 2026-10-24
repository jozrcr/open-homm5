# The oracle (byte-match verifier)

`verify.py` decides whether a decompiled function is **done**. It compiles each unit in `src/decomp/` with the game's original compiler (Visual C++ 2003, `cl` 13.10.3077, see `docs/toolchain.md`) and compares the machine code of every listed function with the reference exe.

```
python tools/verify/verify.py                 # all units
python tools/verify/verify.py GameTimer       # one unit
python tools/verify/verify.py --list GameTimer  # functions the unit's .obj defines, with mangled names
python tools/verify/verify.py --record        # all units, and rewrite progress/matched.txt and progress/fingerprints.json
python tools/verify/verify.py --fingerprints  # check every recorded function without the game's exe (what GitHub runs)
```

Needs: `ref/H5_Game_31_vanilla.exe` (checked by hash), the compiler in `C:\tools\vctoolkit2003` (or set `VCTOOLKIT2003`), Python 3 with `pefile`. Build output goes to `build/decomp/` (gitignored).

## What "match" means

For each function listed in a unit's `unit.json`:

1. **Bytes.** The function's bytes in the `.obj` must equal the exe's bytes at the listed address. The only exception is the 4-byte address fields that the linker fills in later ("relocations"). In the `.obj` they are placeholders, so they are skipped in this comparison.
2. **Size.** The `.obj` function must be exactly as long as the exe function (`progress/inventory.csv`), so a correct beginning of a longer function does not count.
3. **Relocations.** Each skipped address field must point to the right place in the exe:
   - a function or global with a known address, from any `unit.json` or from `progress/symbols.txt`;
   - an imported DLL function (by its import-table slot);
   - data defined in the same `.obj` (string literals, constant tables): the exe bytes at the target must equal the `.obj` data.

   If the target symbol's address is unknown, the function does not match until someone adds the address to `progress/symbols.txt`.

A wrong address is reported like this:
`reloc at +0x10 to ?Func00401330@...: exe has 00401330, expected 00401334`.

## Checking without the game's exe (GitHub)

The game's exe can never be uploaded, so GitHub checks against `progress/fingerprints.json` instead. `--record` writes it from the exe. For every matched function, it holds:
- the original size;
- a SHA-256 of the original bytes with the relocation fields zeroed;
- for each relocation, the address the exe points to (or a SHA-256 of the data there);
- the import-table addresses those relocations use.

`--fingerprints` compiles the units and applies the same three conditions to those values. Hashes and addresses are allowed in the repo (CLAUDE.md).

`.github/workflows/decomp.yml` runs `--fingerprints` on every push and pull request:
- **The compiler:** it gets the 2003 compiler from archive.org (`tools/toolchain/fetch_vctoolkit2003.py`, checked by SHA-256, cached between runs).
- **The verifier:** it checks the SHA-256 of `verify.py` and `coff.py`. A change to either fails the build until their hashes in the workflow are updated in the same, reviewed change.

## Rules (from CLAUDE.md)

- Never edit this folder to make a function pass. CI will hash these files.
- Default flags live in `DEFAULT_FLAGS`. A unit may override them in `unit.json` (`"flags"`) only when the original file was evidently built differently, with a comment in the unit's source saying why.

## Files

- `verify.py`: build, compare, report, record.
- `coff.py`: minimal reader for the compiler's `.obj` files (sections, relocations, symbols).
