"""Read the game's text layers (the .pak and .h5u zip archives), never change them.

Shared by inventory.py, glossary.py and batch.py (docs/plans/languages.md).

The game keeps every text in its own small .txt file (UTF-16 LE with a byte
order mark), inside zip archives: data/*.pak, and UserMODS/*.h5u for mods,
campaigns and single maps. When two archives hold the same path, the game keeps
the entry with the newest date inside the zip (NVFS::CWinVFS, 0x009df790 and
0x009def50 in the vanilla 3.1 exe): that is how MMH5.5's English files replace
the French ones. On equal dates the file met first stays (the folder's listing
order).
"""
import collections
import os
import re
import zipfile

DEFAULT_GAME = r"C:\Program Files (x86)\Steam\steamapps\common\Heroes of Might and Magic 5 Tribes of the East"


def norm(name):
    """'/Text\\Game/X.txt' -> 'text/game/x.txt' (the game ignores case and slash style)."""
    return name.replace("\\", "/").lstrip("/").lower()


def kind_of(file_name):
    n = file_name.lower()
    if n == "texts.pak":
        return "base"      # Heroes V + Tribes of the East texts, in the Steam language
    if n == "a2p1-texts.pak":
        return "toe31"     # the ToE 3.1 patch's texts
    if n.startswith("mmh55"):
        return "mmh55"
    if n in ("data.pak", "a2p1-data.pak", "sound.pak", "soundsfx.pak"):
        return "game"
    return "other"


class Layer:
    """One archive: its .txt files (normalized path -> ZipInfo)."""

    def __init__(self, path, group):
        self.path = path
        self.name = os.path.basename(path)
        self.group = group  # "data" (data\*.pak) or "usermods" (UserMODS\*.h5u)
        self.kind = kind_of(self.name) if group == "data" else "usermods"
        self.zip = zipfile.ZipFile(path)
        self.texts = {}
        for info in self.zip.infolist():
            if not info.is_dir() and info.filename.lower().endswith(".txt"):
                self.texts[norm(info.filename)] = info

    def raw(self, key):
        return self.zip.read(self.texts[key])

    def text(self, key):
        return decode(self.raw(key))


def load_layers(game=DEFAULT_GAME, usermods=True):
    """Every archive, in the order the game meets them (case-insensitive name order)."""
    layers = []
    folders = [("data", os.path.join(game, "data"), ".pak")]
    if usermods:
        folders.append(("usermods", os.path.join(game, "UserMODS"), ".h5u"))
    for group, folder, ext in folders:
        if not os.path.isdir(folder):
            continue
        for name in sorted(os.listdir(folder), key=str.lower):
            if name.lower().endswith(ext):
                try:
                    layers.append(Layer(os.path.join(folder, name), group))
                except zipfile.BadZipFile:
                    pass
    return layers


def winners(layers):
    """path -> the Layer whose file the game uses (newest date inside the zip wins)."""
    best = {}
    for layer in layers:
        for key, info in layer.texts.items():
            cur = best.get(key)
            if cur is None or info.date_time > cur.texts[key].date_time:
                best[key] = layer
    return best


def decode(raw):
    if raw[:2] == b"\xff\xfe":
        return raw[2:].decode("utf-16-le", "replace")
    if raw[:3] == b"\xef\xbb\xbf":
        return raw[3:].decode("utf-8", "replace")
    if len(raw) > 1 and raw[1:2] == b"\x00":
        return raw.decode("utf-16-le", "replace")
    return raw.decode("utf-8", "replace")


def encoding_of(raw):
    if not raw:
        return "empty"
    if raw[:2] == b"\xff\xfe":
        return "utf-16le+bom"
    if raw[:3] == b"\xef\xbb\xbf":
        return "utf-8+bom"
    if len(raw) > 1 and raw[1:2] == b"\x00":
        return "utf-16le"
    return "other"


# ---- markup ----

TAG = re.compile(r"<[^<>\r\n]{1,80}>")
# printf-style fields: one in 3.1 + MMH5.5 RC19d (a %s in the Witch Hut text); other "%" are percent signs
PRINTF = re.compile(r"%[-+#0]?\d*[sdiuxc]\b|%\d\b")


def tags(text):
    """Every markup tag, in order: <br>, <color=FF0000>, <value=damage>, ..."""
    return TAG.findall(text)


def tag_name(tag):
    """'<color=FF0000>' -> 'color=', '<font size=20>' -> 'font size=', '<br>' -> 'br'."""
    inner = tag[1:-1].strip().lower()
    if "=" in inner:
        return inner.split("=", 1)[0].strip() + "="
    return inner


def placeholders(text):
    """What the game fills in or reads: <value=...> names and printf-style fields."""
    found = [t for t in tags(text) if tag_name(t) in ("value=", "hotkey=")]
    return found + PRINTF.findall(text)


def plain(text):
    return TAG.sub(" ", text)


WORD = re.compile(r"[^\W\d_]+(?:['\u2019-][^\W\d_]+)*")


def words(text):
    return WORD.findall(plain(text))


# ---- language (a rough guess, good enough for counting) ----

FR = set("le la les des du un une et est pour avec vous votre vos sur dans au aux qui que ne pas plus "
         "ses son sa ce cette ces leur leurs par tous toutes chaque peut sont être niveau sort sorts "
         "créature créatures héros dégâts attaque défense ville joueur tour jour".split())
EN = set("the and of to is for with you your on in at which that not more his her its this these their "
         "by all each can are be level spell spells creature creatures hero heroes damage attack "
         "defense defence town player turn day".split())
ACCENTS = set("éèêëàâçùûôîïœÉÈÀÇ")


def guess_lang(text):
    """'fr', 'en' or 'neutral' (names, numbers, too short to tell)."""
    ws = [w.lower() for w in words(text)]
    fr = sum(w in FR for w in ws) + 2 * sum(c in ACCENTS for c in text)
    en = sum(w in EN for w in ws)
    if fr >= 2 and fr > en:
        return "fr"
    if en >= 2 and en > fr:
        return "en"
    if fr == 1 and en == 0 and any(c in ACCENTS for c in text):
        return "fr"
    return "neutral"


# ---- batches: many small texts in one plain file, easy to read and write ----
#
#   @@@ text/game/spells/.../name.txt
#   the text, as many lines as it has
#   @@@ next/path.txt
#   ...
# Line breaks inside a text are kept; the file is UTF-8.

SEP = "@@@ "


def write_batch(path, items):
    """items: [(key, text)]"""
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for key, text in items:
            f.write(SEP + key + "\n")
            f.write(text.replace("\r\n", "\n").rstrip("\n") + "\n")


def read_batch(path):
    items = collections.OrderedDict()
    key = None
    lines = []
    with open(path, encoding="utf-8-sig") as f:
        for line in f:
            if line.startswith(SEP):
                if key is not None:
                    items[key] = "".join(lines).rstrip("\n")
                key = line[len(SEP):].strip()
                lines = []
            elif key is not None:
                lines.append(line)
    if key is not None:
        items[key] = "".join(lines).rstrip("\n")
    return items


def to_game_bytes(text):
    """A text as the game stores it: UTF-16 LE with a BOM, Windows line ends."""
    return b"\xff\xfe" + text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-16-le")
