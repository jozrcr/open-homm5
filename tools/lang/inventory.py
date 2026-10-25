"""Inventory of the game's texts, by layer and language (docs/plans/languages.md).

    python tools/lang/inventory.py [--game DIR] [--fr-pack FILE]

Reads the game folder (never writes there) and writes:
  tools/lang/inventory.md     counts only (committed)
  build/lang/inventory.csv    one line per text path (local, gitignored)

--fr-pack: a French text pack (a zip, e.g. MMH5.5's MMH55-Texts-FR) to measure
how much of the English the player sees it covers. Nothing is copied from it.
"""
import argparse
import collections
import csv
import datetime
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import h5texts  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def area(key):
    parts = key.split("/")
    n = 3 if parts[0] in ("text", "ui", "gamemechanics") else 2
    return "/".join(parts[:min(n, len(parts) - 1)]) or parts[0]


def pct(a, b):
    return "%d%%" % round(100.0 * a / b) if b else "-"


def table(rows, head):
    out = ["| " + " | ".join(head) + " |", "|" + "|".join("---" if i == 0 else "---:" for i in range(len(head))) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(c) for c in r) + " |")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default=h5texts.DEFAULT_GAME)
    ap.add_argument("--fr-pack", help="a French text pack (.pak/.zip) to measure its coverage")
    ap.add_argument("--summary", default=os.path.join(REPO, "tools", "lang", "inventory.md"))
    ap.add_argument("--detail", default=os.path.join(REPO, "build", "lang"))
    args = ap.parse_args()

    layers = h5texts.load_layers(args.game)
    data = [l for l in layers if l.group == "data"]
    user = [l for l in layers if l.group == "usermods"]
    win = h5texts.winners(data)

    info = {}  # (layer name, key) -> (lang, words)

    def look(layer, key):
        k = (layer.name, key)
        if k not in info:
            t = layer.text(key)
            info[k] = (h5texts.guess_lang(t), len(h5texts.words(t)), t)
        return info[k]

    md = []
    md.append("# Text inventory (counts only)")
    md.append("")
    md.append("Made by `python tools/lang/inventory.py` on %s from the owner's install (read only). "
              "No game text is in this file. Plan: [../../docs/plans/languages.md](../../docs/plans/languages.md)."
              % datetime.date.today().isoformat())
    md.append("")
    md.append("Words are counted without the markup tags. The language is a guess from common words and accents; "
              "\"neutral\" means names, numbers or too short to tell (a name like a hero's is often the same in both).")
    md.append("")

    # ---- 1. layers ----
    md.append("## 1. The layers")
    md.append("")
    rows = []
    for l in layers:
        if not l.texts:
            continue
        c = collections.Counter()
        w = collections.Counter()
        for key in l.texts:
            lang, n, _ = look(l, key)
            c[lang] += 1
            w[lang] += n
        dates = sorted(i.date_time for i in l.texts.values())
        rows.append([l.name, l.group, l.kind, len(l.texts), sum(w.values()), c["fr"], c["en"], c["neutral"],
                     "%d-%d" % (dates[0][0], dates[-1][0])])
    md += table(rows, ["archive", "folder", "kind", "text files", "words", "French", "English", "neutral", "dates"])
    md.append("")

    # ---- 2. what the player sees ----
    md.append("## 2. What the player sees today (data\\*.pak, newest date wins)")
    md.append("")
    by_kind = collections.Counter()
    by_lang = collections.Counter()
    words_lang = collections.Counter()
    for key, l in win.items():
        lang, n, _ = look(l, key)
        by_kind[l.kind] += 1
        by_lang[lang] += 1
        words_lang[lang] += n
    md.append("- **%d** different text paths." % len(win))
    md.append("- The file used comes from: " + ", ".join("%s %d" % (k, v) for k, v in by_kind.most_common()) + ".")
    md.append("- Its language: French %d files (%d words), English %d files (%d words), neutral %d files (%d words)."
              % (by_lang["fr"], words_lang["fr"], by_lang["en"], words_lang["en"], by_lang["neutral"], words_lang["neutral"]))
    ties = 0
    for key, l in win.items():
        d = l.texts[key].date_time
        ties += sum(1 for o in data if o is not l and key in o.texts and o.texts[key].date_time == d)
    md.append("- Paths where two archives have the same date (the folder order decides, which may differ on Linux): %d." % ties)
    md.append("")

    # ---- 3. overlaps ----
    md.append("## 3. Which English files replace a French one")
    md.append("")
    base = [l for l in data if l.kind == "base"]
    base_keys = set().union(*(l.texts for l in base)) if base else set()
    rows = []
    for kind in ("toe31", "mmh55"):
        ls = [l for l in data if l.kind == kind]
        keys = set().union(*(l.texts for l in ls)) if ls else set()
        same = keys & base_keys
        newer = 0
        for k in same:
            mine = max(l.texts[k].date_time for l in ls if k in l.texts)
            newer += all(mine > b.texts[k].date_time for b in base if k in b.texts)
        wins = sum(1 for k in keys if win[k].kind == kind)
        rows.append([kind + " (" + ", ".join(l.name for l in ls if l.texts) + ")", len(keys), len(same), newer,
                     len(keys - base_keys), wins])
    md += table(rows, ["layer", "text files", "same path as a base file", "... newer than it",
                       "new paths", "used by the game (not replaced again)"])
    md.append("")
    md.append("The ToE 3.1 files that are not used are replaced again by MMH5.5's.")
    md.append("")

    # ---- 4. French needs ----
    md.append("## 4. For French: the English the player sees")
    md.append("")
    md.append("The files the game uses that are English: every file of an English layer (ToE 3.1, MMH5.5) that has "
              "a word and does not read as French, and the base files that read as English. \"Sentences\" read as "
              "English; \"short\" are names and labels too short for the guess (\"Peasant\", \"Spell power\"): "
              "most need French too, and the names must match the game's French names (glossary). "
              "\"Replaces\" = a French base file exists on the same path (an older text to start from).")
    md.append("")

    def english(l, key):
        lang, n, _ = look(l, key)
        if n == 0 or lang == "fr":
            return None
        if l.kind in ("mmh55", "toe31"):
            return "sentences" if lang == "en" else "short"
        return "sentences" if lang == "en" else None

    need = collections.defaultdict(lambda: [0, 0])  # (kind, area) -> files, words
    kinds = collections.defaultdict(lambda: collections.Counter())
    for key, l in win.items():
        e = english(l, key)
        if not e:
            continue
        n = look(l, key)[1]
        k = kinds[l.kind]
        k[e + " files"] += 1
        k[e + " words"] += n
        if key in base_keys:
            k["replaces"] += 1
        need[(l.kind, area(key))][0] += 1
        need[(l.kind, area(key))][1] += n
    rows = []
    tot = collections.Counter()
    for kind, k in sorted(kinds.items()):
        tot.update(k)
        rows.append([kind, k["sentences files"], k["sentences words"], k["short files"], k["short words"],
                     k["replaces"], k["sentences words"] + k["short words"]])
    rows.append(["**total**", tot["sentences files"], tot["sentences words"], tot["short files"], tot["short words"],
                 tot["replaces"], "**%d**" % (tot["sentences words"] + tot["short words"])])
    md += table(rows, ["layer", "sentences: files", "words", "short: files", "words", "files that replace a French one",
                       "all words"])
    md.append("")
    rows = sorted(([k[0], k[1], v[0], v[1]] for k, v in need.items()), key=lambda r: -r[3])
    md.append("Biggest areas (all layers):")
    md.append("")
    md += table(rows[:25], ["layer", "area", "files", "words"])
    md.append("")

    # ---- 5. English needs ----
    md.append("## 5. For English: the French the player sees")
    md.append("")
    fr_need = collections.Counter()
    fr_words = collections.Counter()
    for key, l in win.items():
        lang, n, _ = look(l, key)
        if lang == "fr":
            fr_need[area(key)] += 1
            fr_words[area(key)] += n
    md.append("**%d** files, **%d** words, all from `%s`: an English `texts.pak` of the same game version replaces "
              "them path for path. Biggest areas:" % (sum(fr_need.values()), sum(fr_words.values()),
                                                      ", ".join(sorted({win[k].name for k in win if look(win[k], k)[0] == "fr"}))))
    md.append("")
    md += table([[a, fr_need[a], fr_words[a]] for a, _ in fr_words.most_common(15)], ["area", "files", "words"])
    md.append("")

    # ---- 6. format ----
    md.append("## 6. The format")
    md.append("")
    enc = collections.Counter()
    ends = collections.Counter()
    tagc = collections.Counter()
    tag_files = collections.Counter()
    ph_files = 0
    printf_files = 0
    to_tr = [(l, k) for k, l in win.items() if english(l, k)]
    for l in layers:
        for key in l.texts:
            raw = l.raw(key)
            enc[h5texts.encoding_of(raw)] += 1
            t = look(l, key)[2]
            ends["CRLF" if "\r\n" in t else ("LF only" if "\n" in t else "one line")] += 1
            ends["ends with a line break" if t.endswith("\n") else "no final line break"] += 1
    for l, key in to_tr:
        t = look(l, key)[2]
        names = [h5texts.tag_name(x) for x in h5texts.tags(t)]
        tagc.update(names)
        tag_files.update(set(names))
        ph = h5texts.placeholders(t)
        ph_files += bool(ph)
        printf_files += bool(h5texts.PRINTF.search(t))
    md.append("- Encodings (all layers): " + ", ".join("%s %d" % kv for kv in enc.most_common()) + ".")
    md.append("- Line breaks: " + ", ".join("%s %d" % kv for kv in ends.most_common()) + ".")
    md.append("- In the English files to translate (%d): %d have a placeholder (`<value=...>`, `<hotkey=...>`, "
              "printf `%%s`), %d of them printf-style." % (len(to_tr), ph_files, printf_files))
    md.append("")
    md.append("Tags in the English files to translate (each must come out unchanged):")
    md.append("")
    md += table([["`<%s...>`" % n if n.endswith("=") else "`<%s>`" % n, c, tag_files[n]] for n, c in tagc.most_common(30)],
                ["tag", "times", "files"])
    md.append("")

    # ---- 7. length ----
    ratios = []
    short = 0
    pairs = 0
    for l in [x for x in data if x.kind in ("mmh55", "toe31")]:
        for key in l.texts:
            if key not in base_keys:
                continue
            b = next(x for x in base if key in x.texts)
            fr_t = h5texts.plain(look(b, key)[2]).strip()
            en_t = h5texts.plain(look(l, key)[2]).strip()
            if look(b, key)[0] != "fr" or look(l, key)[0] != "en" or len(en_t) < 20:
                continue
            pairs += 1
            ratios.append(len(fr_t) / len(en_t))
            short += len(en_t) <= 40
    md.append("## 7. Length")
    md.append("")
    if ratios:
        ratios.sort()
        md.append("Same path, old French base text vs the MMH5.5/3.1 English text that replaced it (%d pairs, 20+ "
                  "characters). MMH5.5 often rewrote and lengthened the text, so this is not a translation ratio: "
                  "French/English length median %.2f, 90%% under %.2f. %d of the pairs are short (40 characters or "
                  "less), the labels where a fixed-size box can cut the text. For a real translation ratio see "
                  "docs/plans/languages.md (MMH5.5's own French: about 1.17)." % (pairs, statistics.median(ratios),
                                                                                  ratios[int(0.9 * len(ratios))], short))
    md.append("")

    # ---- 8. UserMODS ----
    md.append("## 8. UserMODS (campaigns, single maps)")
    md.append("")
    md.append("Not needed for co-op on multiplayer or random maps. Their archives compete with data\\ by date too.")
    md.append("")
    rows = []
    for l in user:
        if not l.texts:
            continue
        c = collections.Counter()
        w = 0
        newer = 0
        for key in l.texts:
            lang, n, _ = look(l, key)
            c[lang] += 1
            w += n
            if key in win and l.texts[key].date_time > win[key].texts[key].date_time:
                newer += 1
        rows.append([l.name, len(l.texts), w, c["fr"], c["en"], c["neutral"], newer])
    md += table(rows, ["archive", "text files", "words", "French", "English", "neutral", "newer than data\\"])
    md.append("")

    # ---- 9. a French pack ----
    if args.fr_pack:
        import zipfile
        z = zipfile.ZipFile(args.fr_pack)
        fr = {}
        for i in z.infolist():
            if not i.is_dir() and i.filename.lower().endswith(".txt"):
                fr[h5texts.norm(i.filename)] = i
        keys = [k for _l, k in to_tr]
        covered = [k for k in keys if k in fr]
        same = [k for k in covered if look(win[k], k)[1] >= 2  # (a one-word label may be the same in French)
                and h5texts.decode(z.read(fr[k])).strip() == look(win[k], k)[2].strip()]
        gaps = sorted([k for k in keys if k not in fr] + same, key=lambda k: (area(k), k))
        gap_words = sum(look(win[k], k)[1] for k in gaps)
        md.append("## 9. The French pack %s" % os.path.basename(args.fr_pack))
        md.append("")
        md.append("- %d text files; it has %d of the %d English files the player sees; %d of those are still the "
                  "English text. Left to translate: %d files, %d words (list: build/lang/gaps.txt)."
                  % (len(fr), len(covered), len(keys), len(same), len(gaps), gap_words))
        md.append("")
        os.makedirs(args.detail, exist_ok=True)
        with open(os.path.join(args.detail, "gaps.txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(gaps) + "\n")

    os.makedirs(os.path.dirname(args.summary), exist_ok=True)
    with open(args.summary, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(md) + "\n")
    os.makedirs(args.detail, exist_ok=True)
    with open(os.path.join(args.detail, "inventory.csv"), "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow(["path", "used_from", "kind", "lang", "words", "in_base", "layers"])
        for key in sorted(win):
            l = win[key]
            lang, n, _ = look(l, key)
            w.writerow([key, l.name, l.kind, lang, n, int(key in base_keys),
                        ";".join(x.name for x in layers if key in x.texts)])
    print("wrote", args.summary, "and", os.path.join(args.detail, "inventory.csv"))


if __name__ == "__main__":
    main()
