#!/usr/bin/env python3
"""Make the SD card folder from a release zip and the Xbox disc image, so the
disc does not have to be extracted by hand:

    python3 tools/make_sd.py --iso "Need for Speed - Underground 2 (USA).iso" \\
                             --release nfsu2x.zip --out sd

writes sd/switch/nfsu2x/ (the release's NRO(s) and nfsu2x_env.txt, and game/
with every file of the disc), ready to copy to the root of the card. No
dependencies beyond Python 3.

- The names keep the disc's case: the port opens them on the card's file
  system, which is case-sensitive.
- The disc must be the Xbox NTSC-U one (default.xbe checked): the port is
  lifted from that executable.
- The release's save/ folder is not copied: copied over a card that already
  has the game, its empty images would replace the ones there (the port
  makes them on its first run). Saves themselves are in game/UDATA/, so
  --out must be an empty folder, never the card itself.
- --set KEY=VALUE (repeatable) adds a line to nfsu2x_env.txt.
"""
import argparse
import hashlib
import os
import struct
import sys
import zipfile

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# file system start: plain XISO, redump image of an Xbox disc
PARTITIONS = (0, 0x18300000)
NTSC_U_XBE_SHA256 = "c8cab1bfe7cf26553e84eb4ef2a26d16f6b06260f7494c583802bc00fd81dd31"


def disc_files(f):
    """{path as on the disc: (size, offset in the image)}"""
    for base in PARTITIONS:
        f.seek(base + 32 * SECTOR)
        if f.read(len(MAGIC)) == MAGIC:
            break
    else:
        sys.exit("not an Xbox disc image (no XDVDFS file system found)")
    root, root_size = struct.unpack("<II", f.read(8))
    out = {}

    def walk(sector, size, prefix):
        f.seek(base + sector * SECTOR)
        data = f.read(size)
        stack, seen = [0], set()
        while stack:
            off = stack.pop()
            if off in seen or off + 14 > len(data):
                continue
            seen.add(off)
            left, right, start, length, attr, n = struct.unpack_from("<HHIIBB", data, off)
            if left == 0xFFFF and right == 0xFFFF:
                continue
            name = data[off + 14:off + 14 + n].decode("latin-1")
            if attr & 0x10:
                out[prefix + name + "/"] = None
                if length:
                    walk(start, length, prefix + name + "/")
            else:
                out[prefix + name] = (length, base + start * SECTOR)
            for k in (left, right):
                if k:
                    stack.append(k * 4)
    walk(root, root_size, "")
    return out


def copy(f, entry, dst):
    size, off = entry
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    f.seek(off)
    with open(dst, "wb") as o:
        left = size
        while left:
            b = f.read(min(left, 8 << 20))
            if not b:
                sys.exit("the image is truncated (%s)" % dst)
            o.write(b)
            left -= len(b)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--iso", required=True, help="Xbox NTSC-U disc image")
    ap.add_argument("--release", required=True, help="release zip (nfsu2x.zip)")
    ap.add_argument("--out", required=True, help="empty folder: becomes the SD card root")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE",
                    help="extra nfsu2x_env.txt line")
    a = ap.parse_args()
    dest = os.path.join(a.out, "switch", "nfsu2x")
    game = os.path.join(dest, "game")
    if os.path.isdir(game) and os.listdir(game):
        sys.exit("%s already holds a game: use an empty folder (saves live in game/UDATA)" % game)
    for s in a.set:
        if "=" not in s:
            sys.exit("--set wants KEY=VALUE, got %r" % s)

    with zipfile.ZipFile(a.release) as z:
        nros = [n for n in z.namelist() if n.endswith(".nro")]
        if not nros:
            sys.exit("%s holds no .nro: not a release zip" % a.release)
        for i in z.infolist():
            if i.is_dir() or i.filename.startswith("save/"):
                continue
            dst = os.path.join(dest, *i.filename.split("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as o:
                o.write(z.read(i))
    print("release: %s" % ", ".join(nros))

    with open(a.iso, "rb") as f:
        files = disc_files(f)
        xbe = next((k for k in files if k.lower() == "default.xbe"), None)
        if not xbe:
            sys.exit("no default.xbe on this disc")
        size, off = files[xbe]
        f.seek(off)
        if hashlib.sha256(f.read(size)).hexdigest() != NTSC_U_XBE_SHA256:
            sys.exit("this is not the Xbox NTSC-U (USA) disc the port is built from")
        names = sorted(k for k, v in files.items() if v)
        total = sum(files[k][0] for k in names)
        done = 0
        for i, k in enumerate(names):
            copy(f, files[k], os.path.join(game, *k.split("/")))
            done += files[k][0]
            if i % 50 == 0 or i == len(names) - 1:
                print("\rdisc: %d/%d files, %d/%d MB" % (i + 1, len(names), done >> 20, total >> 20),
                      end="", flush=True)
        for k, v in files.items():
            if v is None:
                os.makedirs(os.path.join(game, *k.rstrip("/").split("/")), exist_ok=True)
    print()

    if a.set:
        env = os.path.join(dest, "nfsu2x_env.txt")
        text = open(env, encoding="utf-8").read() if os.path.exists(env) else ""
        keys = {s.split("=")[0] for s in a.set}
        kept = [l for l in text.splitlines() if l.split("=")[0].strip() not in keys]
        with open(env, "w", encoding="utf-8", newline="\n") as o:
            o.write("\n".join(kept).rstrip("\n") + "\n\n# tools/make_sd.py\n" + "\n".join(a.set) + "\n")
    print("done: copy %s to the root of the SD card" % os.path.join(a.out, "switch"))


if __name__ == "__main__":
    main()
