"""A term glossary English -> French from the game's own texts (docs/plans/languages.md).

    python tools/lang/glossary.py [--game DIR] [--fr-pack FILE] [--out build/lang/glossary.tsv]

The output holds game texts: it stays in build/ (gitignored), never committed.

How the pairs are found:
1. Same path. MMH5.5 (and the 3.1 patch) replaced many French files with an
   English one on the same path: Text/Game/Spells/.../Name.txt in French in
   texts.pak, in English in MMH55-Texts-EN.pak. Both are the name of the same
   thing, so the pair gives the game's own French name. Only short texts
   (names and labels) are kept.
2. Same object. Where MMH5.5 gave an object (creature, spell, artifact, ...)
   a text on a new path, the object's .xdb file says so: the original
   data.pak version points at the French file, the MMH5.5 version at the new
   English one (<NameFileRef href="..."/>). The two texts pair up.
3. --fr-pack: a French text pack for MMH5.5 (its own translation, same paths
   as MMH55-Texts-EN.pak) gives the newest names directly; its pairs win.
4. Folder names. The base game's name files sit in folders named in English
   (Spells/Combat/Righteous_Might/Name.txt), so the folder name, split into
   words, pairs with the French name. Rougher (an internal name is not always
   the English shown in game), used only where 1-3 found nothing.

Single-word pairs that do not look alike ("Creatures" / "Paysans": a path
MMH5.5 reused for something else) are marked low; batch.py does not offer them.
"""
import argparse
import collections
import os
import re
import sys
import unicodedata
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import h5texts  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

CATEGORIES = [  # first match wins
    ("text/game/spells/", "spell"), ("gamemechanics/spell/", "spell"),
    ("text/game/creatures/", "creature"), ("text/game/skills/", "skill"),
    ("text/game/artifacts/", "artifact"), ("gamemechanics/rpgstats/artifactsets/", "artifact set"),
    ("text/game/heroes/", "hero"), ("gamemechanics/reftables/heroclass/", "hero class"),
    ("text/game/townbuildings/", "town building"), ("text/game/buildings/", "map building"),
    ("text/game/dwellings/", "dwelling"), ("text/game/towns/", "town / faction"),
    ("text/game/treasures/", "treasure"), ("text/game/combatstrings/", "combat"),
    ("text/combatlog/", "combat"), ("ui/", "interface"), ("text/", "other"), ("", "other"),
]
FIELD = re.compile(r"<(\w*FileRef)\s+href=\"([^\"]*)\"", re.I)


def category(key):
    for prefix, name in CATEGORIES:
        if key.startswith(prefix):
            return name
    return "other"


def is_term(text):
    t = h5texts.plain(text).strip()
    return t and len(h5texts.words(t)) <= 6 and "\n" not in t and len(t) <= 60


def clean(text):
    return re.sub(r"\s+", " ", h5texts.plain(text)).strip()


def fold(s):
    s = unicodedata.normalize("NFKD", s.lower())
    return "".join(c for c in s if not unicodedata.combining(c))


def alike(en, fr):
    """Some word of each starts the same way: Inventory / Inventaire, Regeneration / Régénération."""
    a = [w[:4] for w in h5texts.words(fold(en)) if len(w) >= 4]
    b = [w[:4] for w in h5texts.words(fold(fr)) if len(w) >= 4]
    return bool(set(a) & set(b))


def confidence(en, fr, how):
    if how == "fr-pack":
        return "high"
    if len(h5texts.words(en)) <= 1 and not alike(en, fr):
        return "low"
    return "normal"


def resolve(xdb_key, href):
    """An href as the game reads it: absolute from the data root, or next to the .xdb."""
    href = href.split("#", 1)[0]
    if not href:
        return None
    if href.startswith("/") or href.startswith("\\"):
        return h5texts.norm(href)
    return h5texts.norm(os.path.dirname(xdb_key) + "/" + href)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default=h5texts.DEFAULT_GAME)
    ap.add_argument("--fr-pack")
    ap.add_argument("--out", default=os.path.join(REPO, "build", "lang", "glossary.tsv"))
    args = ap.parse_args()

    layers = h5texts.load_layers(args.game, usermods=False)
    base = [l for l in layers if l.kind == "base"]
    english = [l for l in layers if l.kind in ("mmh55", "toe31")]
    win = h5texts.winners(layers)

    def fr_text(key):
        for b in base:
            if key in b.texts:
                return b.text(key)
        return None

    pairs = {}  # en (lower) -> (category, en, fr, how, path)

    def add(cat, en, fr, how, path):
        if not en or not fr or h5texts.guess_lang(fr) == "en":
            return
        if re.findall(r"\d+", en) != re.findall(r"\d+", fr):
            return  # "+2 Luck" / "... +1 ...": not the same thing any more
        if fr.endswith(".") != en.endswith("."):
            return  # a sentence paired with a name
        k = en.lower()
        if k in pairs and pairs[k][3] == "fr-pack" and how != "fr-pack":
            return
        pairs[k] = (cat, en, fr, how, path)

    # 1. same path
    for key, l in win.items():
        if l.kind not in ("mmh55", "toe31"):
            continue
        fr = fr_text(key)
        en = l.text(key)
        if fr is not None and is_term(en) and is_term(fr):
            add(category(key), clean(en), clean(fr), "same path", key)

    # 2. same object: .xdb files MMH5.5 changed, original vs mod text references
    data_dir = os.path.join(args.game, "data")
    originals = []
    for name in ("data.pak", "a2p1-data.pak"):
        p = os.path.join(data_dir, name)
        if os.path.exists(p):
            z = zipfile.ZipFile(p)
            originals.append((z, {h5texts.norm(i.filename): i for i in z.infolist() if i.filename.lower().endswith(".xdb")}))
    mod_xdbs = 0
    for name in sorted(os.listdir(data_dir), key=str.lower):
        if not name.lower().startswith("mmh55") or not name.lower().endswith(".pak"):
            continue
        z = zipfile.ZipFile(os.path.join(data_dir, name))
        for info in z.infolist():
            key = h5texts.norm(info.filename)
            if not key.endswith(".xdb"):
                continue
            orig = None
            for oz, idx in reversed(originals):  # the 3.1 patch's copy first
                if key in idx:
                    orig = oz.read(idx[key]).decode("utf-8", "replace")
                    break
            if orig is None:
                continue
            mod_xdbs += 1
            mod = z.read(info).decode("utf-8", "replace")
            o_refs = collections.defaultdict(list)
            for f, h in FIELD.findall(orig):
                o_refs[f.lower()].append(h)
            m_refs = collections.defaultdict(list)
            for f, h in FIELD.findall(mod):
                m_refs[f.lower()].append(h)
            for field, hs in m_refs.items():
                # only a field the object has once in both versions (its name), never lists
                if not field.endswith("namefileref") or len(hs) != 1 or len(o_refs.get(field, [])) != 1:
                    continue
                for i, h in enumerate(hs):
                    mk, ok = resolve(key, h), resolve(key, o_refs[field][i])
                    if not mk or not ok or mk == ok or mk not in win:
                        continue
                    fr = fr_text(ok)
                    if fr is None or win[mk].kind not in ("mmh55", "toe31"):
                        continue
                    en = win[mk].text(mk)
                    if is_term(en) and is_term(fr):
                        add(category(mk), clean(en), clean(fr), "same object", mk)

    # 4. folder names: the base game's French names are in folders named in English
    #    (Text/Game/Spells/Combat/Righteous_Might/Name.txt): "Righteous Might" = the French name.
    #    Wider but rougher (internal names are not always the shown English name); never
    #    replaces a pair found above.
    for b in base:
        for key, info in b.texts.items():
            parts = info.filename.replace("\\", "/").split("/")
            if len(parts) < 3 or parts[-1].lower() not in ("name.txt", "shortname.txt"):
                continue
            folder = parts[-2]
            en = re.sub(r"(?<=[a-z])(?=[A-Z])", " ", folder.replace("_", " ")).strip()
            if not re.search(r"[A-Za-z]{3}", en) or re.search(r"\d", en) or en.lower() in pairs:
                continue
            fr = b.text(key)
            if is_term(fr) and h5texts.guess_lang(fr) != "en":
                add(category(key), en, clean(fr), "folder name", key)

    # 3. a French pack for MMH5.5
    if args.fr_pack:
        z = zipfile.ZipFile(args.fr_pack)
        for info in z.infolist():
            key = h5texts.norm(info.filename)
            if key.endswith(".txt") and key in win and win[key].kind in ("mmh55", "toe31"):
                fr = h5texts.decode(z.read(info))
                en = win[key].text(key)
                if is_term(en) and is_term(fr):
                    add(category(key), clean(en), clean(fr), "fr-pack", key)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    rows = sorted(pairs.values(), key=lambda r: (r[0], r[1].lower()))
    conf = collections.Counter()
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("category\tenglish\tfrench\tconfidence\thow\tpath\n")
        for cat, en, fr, how, path in rows:
            c = confidence(en, fr, how)
            conf[c] += 1
            f.write("\t".join(x.replace("\t", " ") for x in (cat, en, fr, c, how, path)) + "\n")
    count = collections.Counter(r[0] for r in rows)
    how = collections.Counter(r[3] for r in rows)
    same = sum(1 for r in rows if r[1].lower() == r[2].lower())
    print("glossary: %d terms (%s); confidence %s; by category: %s; %d spelled the same in both; "
          "%d MMH5.5 .xdb files compared"
          % (len(rows), ", ".join("%s %d" % kv for kv in how.most_common()),
             ", ".join("%s %d" % kv for kv in conf.most_common()),
             ", ".join("%s %d" % kv for kv in count.most_common()), same, mod_xdbs))
    print("wrote", args.out)


if __name__ == "__main__":
    main()
