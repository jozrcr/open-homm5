"""Make a game text pack (.pak) from a snapshot of one of MMH5.5's text repositories.

    python -I tools/lang/build_pack.py SNAPSHOT.zip OUT.pak

SNAPSHOT.zip: a GitHub download of a repository such as
Might-Magic-Heroes-5-5/MMH55-Texts-FR at one commit. Its texts are under mod/,
in UTF-8 (so GitHub can show changes). The pack gets each mod/**/*.txt as the
game reads texts: UTF-16 LE with the FF FE mark, Windows line ends, on the same
path without mod/. This is what the repository's own prep_game_package.ps1
does (we do not run it); its other files are left out.

The snapshot is data from the internet: nothing in it is run, and only .txt
files under mod/ with plain relative paths are taken. Both files hold mod
texts: keep them in build/ (gitignored).
"""
import argparse
import os
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import h5texts  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("snapshot")
    ap.add_argument("out")
    args = ap.parse_args()
    src = zipfile.ZipFile(args.snapshot)
    n = skipped = 0
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as out:
        for info in src.infolist():
            if info.is_dir():
                continue
            parts = info.filename.split("/")
            # <repo>-<commit>/mod/<path>.txt
            if len(parts) < 3 or parts[1] != "mod" or not parts[-1].lower().endswith(".txt"):
                continue
            rel = "/".join(parts[2:])
            if any(p in ("", ".", "..") for p in parts[2:]) or ":" in rel or "\\" in rel:
                skipped += 1
                continue
            raw = src.read(info)
            try:
                text = raw.decode("utf-8-sig")
            except UnicodeDecodeError:
                skipped += 1
                print("not UTF-8, skipped:", rel)
                continue
            entry = zipfile.ZipInfo(rel, date_time=info.date_time)
            entry.compress_type = zipfile.ZIP_DEFLATED
            out.writestr(entry, h5texts.to_game_bytes(text))
            n += 1
    print("%s: %d texts%s" % (args.out, n, (", %d skipped" % skipped) if skipped else ""))


if __name__ == "__main__":
    main()
