"""Translation batches: extract, check, pack (docs/plans/languages.md).

    python tools/lang/batch.py make  --paths LIST.txt --out build/lang/b001   [--game DIR]
    python tools/lang/batch.py check build/lang/b001                           (en.txt vs fr.txt)
    python tools/lang/batch.py pack  build/lang/b001 --to build/lang/fr        (game files, UTF-16)

make: LIST.txt has one game path per line (text/game/.../name.txt). Writes into
      the batch folder:
        en.txt       the English text the game uses for each path
        context.txt  for each path: the old French text on the same path (when
                     the base game has one) and the glossary terms found in it
      Both hold game texts: batch folders stay in build/ (gitignored).
fr.txt: the translation, same format as en.txt (written by a person or a worker).
check: every path translated, tags and <value=...> placeholders unchanged, same
      number of lines, no English left, length warnings. Exit code 1 on errors.
pack: one file per path as the game reads it (UTF-16 LE + BOM, CRLF), ready to
      zip into a .pak for open-homm5\\lang\\fr.
"""
import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import h5texts  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GLOSSARY = os.path.join(REPO, "build", "lang", "glossary.tsv")


def load_glossary(path=GLOSSARY):
    terms = []
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            next(f, None)
            for line in f:
                parts = line.rstrip("\n").split("\t")
                if len(parts) >= 4 and parts[3] != "low":
                    terms.append((parts[1], parts[2]))
    return terms


def make(args):
    layers = h5texts.load_layers(args.game, usermods=False)
    win = h5texts.winners(layers)
    base = [l for l in layers if l.kind == "base"]
    terms = load_glossary()
    keys = []
    with open(args.paths, encoding="utf-8") as f:
        for line in f:
            k = h5texts.norm(line.strip())
            if k and k not in keys:
                keys.append(k)
    os.makedirs(args.out, exist_ok=True)
    en_items, ctx_items, missing = [], [], []
    words = 0
    for k in keys:
        if k not in win:
            missing.append(k)
            continue
        layer = win[k]
        name = layer.texts[k].filename.replace("\\", "/").lstrip("/")
        text = layer.text(k)
        words += len(h5texts.words(text))
        en_items.append((name, text))
        notes = ["from: " + layer.name]
        old = next((b.text(k) for b in base if k in b.texts and b is not layer), None)
        if old is not None:
            notes.append("old French on the same path (may describe an older version):")
            notes.append(old.replace("\r\n", "\n"))
        low = text.lower()
        hits = [(e, fr) for e, fr in terms if len(e) > 3 and re.search(r"\b" + re.escape(e.lower()) + r"\b", low)]
        if hits:
            notes.append("glossary (the French game's names; check the meaning still matches):")
            notes += ["  %s = %s" % h for h in hits[:20]]
        ctx_items.append((name, "\n".join(notes)))
    h5texts.write_batch(os.path.join(args.out, "en.txt"), en_items)
    h5texts.write_batch(os.path.join(args.out, "context.txt"), ctx_items)
    print("batch %s: %d texts, %d words%s" % (args.out, len(en_items), words,
                                              ("; not found: " + ", ".join(missing)) if missing else ""))


def check(args):
    en = h5texts.read_batch(os.path.join(args.batch, "en.txt"))
    fr = h5texts.read_batch(os.path.join(args.batch, "fr.txt"))
    errors, warnings = [], []
    stats = collections.Counter()
    for key, e in en.items():
        if key not in fr:
            errors.append((key, "not translated"))
            continue
        f = fr[key]
        stats["texts"] += 1
        stats["en words"] += len(h5texts.words(e))
        stats["fr words"] += len(h5texts.words(f))
        stats["en chars"] += len(h5texts.plain(e).strip())
        stats["fr chars"] += len(h5texts.plain(f).strip())
        if not f.strip() and e.strip():
            errors.append((key, "empty"))
            continue
        te, tf = h5texts.tags(e), h5texts.tags(f)
        if collections.Counter(te) != collections.Counter(tf):
            lost = collections.Counter(te) - collections.Counter(tf)
            added = collections.Counter(tf) - collections.Counter(te)
            errors.append((key, "tags differ: missing %s, extra %s" % (dict(lost), dict(added))))
        elif te != tf:
            warnings.append((key, "tags in another order (fine if the sentence order changed)"))
        if sorted(h5texts.placeholders(e)) != sorted(h5texts.placeholders(f)):
            errors.append((key, "placeholders differ"))
        if e.count("\n") != f.count("\n"):
            warnings.append((key, "line count %d -> %d" % (e.count("\n") + 1, f.count("\n") + 1)))
        if h5texts.guess_lang(f) == "en":
            errors.append((key, "still reads as English"))
        le, lf = len(h5texts.plain(e).strip()), len(h5texts.plain(f).strip())
        if le and le <= 40 and lf > 1.35 * le and lf - le > 4:
            warnings.append((key, "short label %d -> %d characters: check it fits" % (le, lf)))
            stats["long labels"] += 1
        elif le > 40 and lf > 1.5 * le:
            warnings.append((key, "%.2f times longer" % (lf / le)))
    for key in fr:
        if key not in en:
            errors.append((key, "not in en.txt"))
    for key, msg in errors:
        print("ERROR   %s: %s" % (key, msg))
    for key, msg in warnings:
        print("warning %s: %s" % (key, msg))
    ratio = stats["fr chars"] / stats["en chars"] if stats["en chars"] else 0
    print("%d texts, %d English words -> %d French words, length x%.2f; %d errors, %d warnings"
          % (stats["texts"], stats["en words"], stats["fr words"], ratio, len(errors), len(warnings)))
    return 1 if errors else 0


def pack(args):
    en = h5texts.read_batch(os.path.join(args.batch, "en.txt"))
    fr = h5texts.read_batch(os.path.join(args.batch, "fr.txt"))
    n = 0
    for key, text in fr.items():
        if key not in en:
            continue
        path = os.path.join(args.to, *key.split("/"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(h5texts.to_game_bytes(text))
        n += 1
    print("packed %d files into %s" % (n, args.to))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make")
    m.add_argument("--paths", required=True)
    m.add_argument("--out", required=True)
    m.add_argument("--game", default=h5texts.DEFAULT_GAME)
    c = sub.add_parser("check")
    c.add_argument("batch")
    p = sub.add_parser("pack")
    p.add_argument("batch")
    p.add_argument("--to", required=True)
    args = ap.parse_args()
    if args.cmd == "make":
        make(args)
    elif args.cmd == "check":
        sys.exit(check(args))
    else:
        pack(args)


if __name__ == "__main__":
    main()
