# Matching notes: what makes this compiler produce the original bytes

Lessons from matched functions, for whoever decompiles the next ones. Add to it when something new works. The compiler is Visual C++ 7.1 (`cl.exe` 13.10.3077, see [../toolchain.md](../toolchain.md)); the verifier is [tools/verify/verify.py](../../tools/verify/verify.py).

First collected in the pilot of 2026-10-24 (three Sonnet workers on the pathfinder area, 0x00c07000..0x00c0b000: 54 matches out of 63 tried in about an hour).

## Build flags

- **Base:** `/O2 /G7 /GR /W3` everywhere so far. `/G7` is required (`/G6`, `/G5`, `/GB` break most functions).
- **`/GX` (C++ exceptions) is not used everywhere.** Between 0x00bf9957 and 0x00c0d797 there is no exception-frame prologue (`mov eax, fs:[0]`) at all, so that code was built without `/GX`. With `/GX`, placement `new` of a class with a constructor gains an exception frame and no longer matches. Leave `/GX` out of a unit's `flags` when its neighbourhood has no `fs:[0]` prologues. Code that needs no unwinding compiles the same either way.
- **`/Ow` (assume aliasing across calls) is real for some files.** 0x00c0a200..0x00c0a9b0 only matches with `/O2 /G7 /Ow /GR`; the functions right after it (0x00c0abc0 on) only match without it. A change of needed flags is a strong sign of a **translation unit boundary**: start a new unit folder there.
- `wchar_t` is not a built-in type without `/Zc:wchar_t`: write `unsigned short` for the game's wide characters.
- **`/Ob1`** (inline only what is marked `inline`) is needed where the exe *calls* small free templates that `/O2` (`/Ob2`) would inline. Seen in the single-player transceiver, 0x007c80a0 on.
- The network transceiver area (0x007c7xxx..0x007ccxxx) has no `fs:[0]` prologue: no `/GX`.
- From Git Bash, a leading `/` in a `cl` flag gets path-converted: write `-O2` style there.

## Shape of the source

Statement order matters more than anything. Small things that changed the bytes:

- **Bit fields:** the compiler merges neighbouring constant bit-field writes into one read-modify-write. Wrapping each group in a small inline member (of the struct that holds the bits) keeps them as separate `and`/`or` instructions, as in the exe. Any call or load through `this` also acts as a barrier.
- **Sub-object constructors vs plain statements:** a 64-byte node only matched when built as a base struct (`NodeBase`) plus two `Cost` members with their own constructors, not with one constructor doing all the stores.
- **Layered helpers inline differently:** the original STL-like code calls through several small inline layers (`push_heap` → aux → `PushHeap`, `allocate_n`, `ucopy`, `ufill_n`). Flattening them changes which loops get inlined.
- **Pass-by-value comparators** (an empty struct) still cost a stack slot (`ret 0x48` in the heap code). Keep them by value.
- **Temporaries vs named locals:** `T()` at the use site and a named local put the zero stores in different places. `SFmtA(...).Conv()` reuses `eax`; a named local re-takes its address.
- **Stack slots** go to locals in the order their address is first used in the generated code. Call arguments are evaluated right to left.
- **Block order:** `do { if (bad) break; ...; return x; } while (0); <fallback>` gives the original layout (fallback first, return last) where a plain `if`/`else` does not.
- **`const` member functions** let the compiler keep member loads across calls to them.
- **A pointer used twice** (`tile->kind == 2 || tile->kind == 3`) keeps the exe's separate `lea`; `int kind = tile->kind` folds it away.
- **A leftover `lea` after a test** of a virtual base: go through an explicit base pointer (`CRefBase *b = o;`) or a helper taking the base pointer.
- **`switch`:** write the cases in the order the code is laid out in the exe.
- **Clamps:** `t = (t < 1.0) ? t : 1.0` gives the original `test ah,5; jnp`. `int m = (n > 0) ? n : 1; n = m;` gives a `test edx,edx` prologue.
- **Small non-inline functions** may still be inlined, but only inside loops.
- **`m_total = SBonus();`** compiles to the original's reset stores; `new (&m_total) SBonus` adds a null check.
- **Loop bounds:** a named reference (`vec<int> &v = n->cmds[me];`) lets the compiler hoist the loop bound. Writing `n->cmds[me].size()` each time keeps the exe's per-iteration recompute.
- **Bool returns:**
  - `x ? true : false` gives `test; setnz`, and `x != 0` gives `and/neg/sbb/neg`.
  - `return a < b;` on unsigned bit fields gives `sbb/neg`, and `bool r = a < b; return r;` gives `setb`.
- **Operand order** decides the instruction: `id == v[i]` gives `cmp reg,[mem]`, and `v[i] == id` gives `cmp [mem],reg`.
- **`switch` versus `if` chain:**
  - An `if`/`else if` chain on one variable gives a `cmp 1/2/3` chain; a `switch` gives the `sub eax,1` chain.
  - A `goto` changes register allocation in earlier parts of the function.
- **Signed versus unsigned** division and multiply follow the field's type: a time that must be `int` gives `imul`.
- **Arithmetic form matters:** `time - (n*200 + 30000)` gives the exe's `lea` chain, and `(n+150)*200` does not.
- **Macros:** a `max(a, call())` macro evaluates the call twice. Match the exe's order with `(cur > call()) ? cur : call()`.
- **Inlined bounds checks** only matched as one `&&` chain (`p->f < size && p->x < w && p->y < h`), not nested `if`s.
- **Statement order and the zero register:** where you put an assignment relative to a local's declaration (`m_settings.i20 = limit;` before `Node cur;`) decides where the zero register for that local's stores is allocated.
- **Positions:** compare positions as a plain bit-field struct, field by field. The same field inside a union with an int gets shift/and extraction instead.
- **Taking a parameter's address** (`Chunk(&f, ...)`): copy the parameter to a local first (`IBinSaver *f = pf;`). Otherwise the compiler re-reads it from the stack after every call, where the exe keeps it in `esi`.
- **Temporaries vs locals for call arguments:** a temporary like `SLevel(1)` with an inline constructor and a `const &` parameter puts its store right before the call, as in the exe.

## Classes, virtual bases and destructors

- The game's ref-counted objects share a virtual base, `CObjectBase` (vtable 0x00e0a7a0, 0xc bytes: vptr, strong count at +4 (low 20 bits), weak count at +8). Its destructor must be inline (`{}`).
- **Leave destructors implicit** where the exe's have no vptr stores; a declared destructor always starts with them. Call it as `this->X::~X();` (the unqualified call is virtual).
- To get a class's vtable and its deleting destructor (`??_G`) emitted, the unit must define a constructor. An empty stand-in works.
- A class that overrides virtuals of its virtual base only gets a **vtordisp** if it also *declares* a virtual destructor. Without it, every offset is 4 bytes off.
- A class with no virtual function of its own (only overrides of its virtual base) has its vbptr at +0. Give it a virtual of its own to get vfptr at +0 and vbptr at +4.
- **The game's `Reset()` idiom:** `this->X::~X(); a = m_weak; b = m_strong; new (this) X; m_weak += a; m_strong += b;`.
- **Base-class constructors** that zero members run first, then the vptr store, then the derived members, as in the exe.
- **`dynamic_cast` fast paths** are plain pointer arithmetic on `vtable[-1]` (the RTTI locator); `&typeid(T)` gives the type descriptor.

## The game's library code is not Microsoft's

- Vectors are `{start, finish, end_of_storage}` (12 bytes, no allocator member): **STLport-style**, not Dinkumware. `priority_queue` is `{vector, comparator}`.
- Free functions and templates in that code are `__fastcall` (write it explicitly; don't use `/Gr`). `operator new`/`delete` stay cdecl.
- Growth: new capacity = `old_size + max(old_size, n)`.

## Sizes, loop alignment and jump tables (verifier fix, Q8)

- **Loop alignment:** Ghidra's size in `progress/inventory.csv` leaves out the alignment bytes a loop jumps over (`eb 03 8d 49 00`, `eb 06 8d 9b 00 00 00 00`).
- **Jump tables:** the compiler puts a `switch`'s table at the end of the function, and the table entries point inside the function itself.
- Since Q8, the verifier treats Ghidra's size as a lower bound. It checks instead that the function ends where the exe's does (only `0xCC` padding follows), and that jump-table entries point to the same place in the exe. A size message now means a real difference.
- **Held** functions (`progress/held.txt`) are for any new verifier gap found later: identical bytes that the verifier can't confirm yet. Keep them in the source, out of `unit.json`; they are not failures.

## Working practice

- One unit folder per guessed translation unit; names are guesses and say so.
- Drafts that don't match yet go in a `drafts.cpp` that includes the unit's source (not listed in `sources`).
- `progress/symbols.txt`: append under your own heading, never rewrite the file (merges keep both sides).
- Keep files LF. Python's `write_text` on Windows writes CRLF; pass `newline="\n"`.
- `next.py` reads failures from `progress/attempts.txt` (one line per attempt); the unit's own `attempts.txt` holds the details.
