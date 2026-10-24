"""Download the Visual C++ Toolkit 2003 v1.01 installer from archive.org (for CI).

    python tools/toolchain/fetch_vctoolkit2003.py VCToolkitSetup.exe

Looks the file up by its SHA-1 in two independent archive.org uploads (see
docs/toolchain.md), downloads it, and checks its size and SHA-256 before
writing it. Then unpack it with extract_vctoolkit2003.py.
"""
import hashlib
import json
import sys
import urllib.request

ITEMS = ["vc-toolkit-2003-setup", "20260623_202606"]
SHA1 = "50dc6cca1503f08a41d56816f3f1abcaf347b0bd"
SHA256 = "c040f6ffbc2259426dd523b7561575fe1704b58bc37092fbfe27b64aefc449f6"
SIZE = 32948392


def fetch(url):
    with urllib.request.urlopen(url, timeout=300) as r:
        return r.read()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    for item in ITEMS:
        try:
            meta = json.loads(fetch("https://archive.org/metadata/%s" % item))
        except OSError as e:
            print("%s: %s" % (item, e))
            continue
        for f in meta.get("files", []):
            if f.get("sha1") != SHA1:
                continue
            url = "https://archive.org/download/%s/%s" % (item, urllib.request.quote(f["name"]))
            print("downloading", url)
            try:
                data = fetch(url)
            except OSError as e:
                print("  %s" % e)
                continue
            if len(data) == SIZE and hashlib.sha256(data).hexdigest() == SHA256:
                with open(sys.argv[1], "wb") as out:
                    out.write(data)
                print("ok: %d bytes, sha256 %s" % (len(data), SHA256))
                return 0
            print("  wrong file (%d bytes, sha256 %s)" % (len(data), hashlib.sha256(data).hexdigest()))
    sys.exit("could not download the Toolkit 2003 v1.01 installer")


if __name__ == "__main__":
    sys.exit(main())
