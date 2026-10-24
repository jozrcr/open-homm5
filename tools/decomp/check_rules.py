"""Check the decompiled source against CLAUDE.md's rules (run by the coordinator and CI).

    python tools/decomp/check_rules.py

Forbidden in decomp source (CLAUDE.md): inline assembly, naked functions,
embedded bytes, object patching, #pragma tricks aimed at the checker. This
script flags anything that could be one of those, for a human to look at:
  - inline assembly (__asm, _asm, __emit), naked functions;
  - any #pragma, and section tricks (code_seg, data_seg, alloc_text, allocate);
  - long runs of byte literals (machine code pasted in as data);
  - files that are not source or notes (objects, binaries);
  - compiler flags outside the known list, and sources outside the unit folder.
Exit code 1 if anything is flagged.
"""
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
DECOMP = REPO / "src" / "decomp"

SOURCE = {".cpp", ".c", ".h", ".hpp", ".inl"}
NOTES = {".json", ".txt", ".md"}
CODE = [
    (re.compile(r"\b_{1,2}asm\b|\b__emit\b|\b_emit\b"), "inline assembly"),
    (re.compile(r"\bnaked\b"), "naked function"),
    (re.compile(r"^\s*#\s*pragma\b", re.M), "#pragma"),
    (re.compile(r"\b(code_seg|data_seg|const_seg|bss_seg|alloc_text|allocate)\b"), "section trick"),
    (re.compile(r"(?:0x[0-9a-fA-F]{1,2}\s*,\s*){16,}"), "a long run of byte literals"),
    (re.compile(r'"(?:\\x[0-9a-fA-F]{2}){8,}'), "machine code in a string"),
]
# The compiler flags a unit may set (unit.json "flags").
FLAG = re.compile(r"^/(nologo|c|O[12dgistwxy]|Oy-|Ob[012]|G[3-7B]|G[RX]-?|G[ysFrzd]|W[0-4]|Zp(1|2|4|8|16)|J"
                  r"|Zc:(wchar_t|forScope)-?|D[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_]*)?|EH[sca]+|arch:SSE2?)$")


def strip_comments(text):
    """Source without comments and string contents, so notes don't trip the checks."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def main():
    problems = []
    for path in sorted(DECOMP.rglob("*")):
        if not path.is_file() or path.name == ".gitkeep":
            continue
        rel = path.relative_to(REPO).as_posix()
        if path.suffix.lower() not in SOURCE | NOTES:
            problems.append("%s: not a source or notes file" % rel)
            continue
        if path.suffix.lower() in SOURCE:
            raw = path.read_text(encoding="utf-8", errors="replace")
            code = strip_comments(raw)
            for pattern, what in CODE:
                m = pattern.search(code if what != "machine code in a string" else raw)
                if m:
                    line = code[:m.start()].count("\n") + 1
                    problems.append("%s:%d: %s" % (rel, line, what))
    for unit in sorted(DECOMP.glob("*/unit.json")):
        rel = unit.relative_to(REPO).as_posix()
        try:
            cfg = json.loads(unit.read_text(encoding="utf-8"))
        except ValueError as e:
            problems.append("%s: not valid JSON (%s)" % (rel, e))
            continue
        for flag in cfg.get("flags", []):
            if not FLAG.match(flag):
                problems.append("%s: unknown compiler flag %s" % (rel, flag))
        for src in cfg.get("sources", []):
            p = (unit.parent / src).resolve()
            if unit.parent.resolve() not in p.parents or not p.is_file():
                problems.append("%s: source %s is not a file in the unit folder" % (rel, src))
    for p in problems:
        print(p)
    print("%d problem(s) in src/decomp" % len(problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
