#!/usr/bin/env python3
"""Play the USA build in French, German or Italian, with no port change: the
text, speech and cut-scenes of that language are taken from the PAL Europe
disc and added to the extracted USA game.

    python3 tools/add_language.py --game sd/switch/nfsu2x/game \\
        --pal "Need for Speed - Underground 2 (Europe) (En,Fr,De,It,Nl,Sv).iso" --language fr

writes into game/NFSUNDER/ (Python 3 only, nothing else needed):
  ZZDATA10.BIN  the language's text (LANGUAGES/FRENCH.BIN ...), speech
                (sound/speech/UG2_Speech_FR.*) and cut-scenes (MOVIES/*_FR.vp6),
                as on the PAL disc
  ZZDATA11.BIN  Global/FrontEndMemoryFile.bin and InGameMemoryFile.bin with the
                language's speech index, events and script patched in (same
                sizes): the game preloads its own copy of them, and with the
                English one the career froze after its first cut-scene
  ZDIR.BIN      every entry of the English names pointed at that data
The USA ZDIR.BIN is kept as ZDIR_english.BIN: copy it back over ZDIR.BIN for
English. The game opens NFSUNDER/ZZDATA0..15 when they exist.

The PAL disc has the speech and cut-scenes of French (_FR), German (_GR) and
Italian (_IT) only; Spanish, Dutch and Swedish have text but no speech.
"""
import argparse
import hashlib
import os
import shutil
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
NTSC_U_XBE_SHA256 = "c8cab1bfe7cf26553e84eb4ef2a26d16f6b06260f7494c583802bc00fd81dd31"
LANGS = {"fr": ("FRENCH", "FR"), "de": ("GERMAN", "GR"), "it": ("ITALIAN", "IT")}
MOVIES = ["ealogo", "PSA", "DriftTut", "DragTut", "StreetXTut"] + \
         ["Scene%02d" % i for i in [1] + list(range(5, 19)) + [20]]
UNNAMED_MOVIE = 0xA420FE8B      # MOVIES\<?>.vp6: a fourth tutorial movie, its name is not known
MEMFILES = ["Global\\FrontEndMemoryFile.bin", "Global\\InGameMemoryFile.bin"]
SPEECH = ["evt", "idx", "csi"]
INV33 = pow(33, -1, 1 << 32)


def bhash(name):
    """The game's file name hash (bStringHash of the upper-case path)."""
    h = 0xFFFFFFFF
    for c in name.replace("/", "\\").upper().encode("latin-1"):
        h = (h * 33 + c) & 0xFFFFFFFF
    return h


def rename_ext(h, old, new):
    """Hash of the same name with another ending, from the hash alone."""
    for c in reversed(old.encode()):
        h = ((h - c) * INV33) & 0xFFFFFFFF
    for c in new.encode():
        h = (h * 33 + c) & 0xFFFFFFFF
    return h


def zdir_rows(data):
    return [list(struct.unpack_from("<6I", data, i)) for i in range(0, len(data) - 23, 24)]


class PalDisc:
    """NFSUNDER/ZDIR.BIN and ZZDATA*.BIN of the PAL disc image."""

    def __init__(self, iso):
        self.f = open(iso, "rb")
        for base in (0, 0x18300000):
            self.f.seek(base + 32 * SECTOR)
            if self.f.read(len(MAGIC)) == MAGIC:
                break
        else:
            sys.exit("%s is not an Xbox disc image" % iso)
        root, size = struct.unpack("<II", self.f.read(8))
        self.files = {}
        self._walk(base, root, size, "")
        z = self.files.get("NFSUNDER/ZDIR.BIN")
        if not z:
            sys.exit("no NFSUNDER/ZDIR.BIN: not an Underground 2 disc")
        self.entries = {r[0]: r for r in zdir_rows(self._read(*z))}

    def _walk(self, base, sector, size, prefix):
        self.f.seek(base + sector * SECTOR)
        data = self.f.read(size)
        stack, seen = [0], set()
        while stack:
            off = stack.pop()
            if off in seen or off + 14 > len(data):
                continue
            seen.add(off)
            left, right, start, length, attr, n = struct.unpack_from("<HHIIBB", data, off)
            if left == 0xFFFF and right == 0xFFFF:
                continue
            name = data[off + 14:off + 14 + n].decode("latin-1").upper()
            if attr & 0x10:
                if length:
                    self._walk(base, start, length, prefix + name + "/")
            else:
                self.files[prefix + name] = (length, base + start * SECTOR)
            for k in (left, right):
                if k:
                    stack.append(k * 4)

    def _read(self, size, off):
        self.f.seek(off)
        return self.f.read(size)

    def read(self, h):
        _, num, sec, _, length, _ = self.entries[h]
        _, base = self.files["NFSUNDER/ZZDATA%d.BIN" % num]
        self.f.seek(base + sec * SECTOR)
        return self.f.read(length)


def plan(lang, pal):
    """[(label, hash the USA build opens, hash of the language's data on the PAL disc)]"""
    text, c = LANGS[lang]
    p = [("LANGUAGES\\%s.BIN" % text, bhash("LANGUAGES\\ENGLISH.BIN"), bhash("LANGUAGES\\%s.BIN" % text))]
    for ext in ("big", "idx", "evt", "csi"):
        p.append(("sound\\speech\\UG2_Speech_%s.%s" % (c, ext), bhash("sound\\speech\\UG2_Speech." + ext),
                  bhash("sound\\speech\\UG2_Speech_%s.%s" % (c, ext))))
    for m in MOVIES:
        p.append(("MOVIES\\%s_%s.vp6" % (m, c), bhash("MOVIES\\%s.vp6" % m), bhash("MOVIES\\%s_%s.vp6" % (m, c))))
    p.append(("MOVIES\\<?>_%s.vp6" % c, UNNAMED_MOVIE, rename_ext(UNNAMED_MOVIE, ".VP6", "_%s.VP6" % c)))
    thx = bhash("MOVIES\\THX_logo_%s.vp6" % c)
    if thx in pal.entries:                           # French only
        p.append(("MOVIES\\THX_logo_%s.vp6" % c, bhash("MOVIES\\THX_logo.vp6"), thx))
    missing = [label for label, _, src in p if src not in pal.entries]
    if missing:
        sys.exit("not on this disc: %s (is it the PAL Europe one?)" % ", ".join(missing))
    return p


def bundle_entries(b):
    """{hash: (offset, size, size2)} of a preloaded memory bundle."""
    n = struct.unpack_from("<I", b, 12)[0]
    return {h: (off, size, size2) for h, off, size, size2, _ in
            (struct.unpack_from("<5I", b, 16 + 20 * i) for i in range(n))}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", required=True, help="the extracted USA game (has default.xbe, NFSUNDER/)")
    ap.add_argument("--pal", required=True, help="PAL Europe disc image")
    ap.add_argument("--language", required=True, choices=sorted(LANGS))
    a = ap.parse_args()
    with open(os.path.join(a.game, "default.xbe"), "rb") as f:
        if hashlib.sha256(f.read()).hexdigest() != NTSC_U_XBE_SHA256:
            sys.exit("%s is not the extracted NTSC-U game this port is built from" % a.game)
    nfs = os.path.join(a.game, "NFSUNDER")
    english = os.path.join(nfs, "ZDIR_english.BIN")
    if not os.path.exists(english):
        shutil.copyfile(os.path.join(nfs, "ZDIR.BIN"), english)
    rows = zdir_rows(open(english, "rb").read())
    base10 = sum(os.path.getsize(os.path.join(nfs, "ZZDATA%d.BIN" % i)) // SECTOR for i in range(10))
    pal = PalDisc(a.pal)
    items = plan(a.language, pal)
    text, c = LANGS[a.language]

    placed = {}                     # USA hash -> (pack, sector, size, checksum or None, label)
    sec = 0
    with open(os.path.join(nfs, "ZZDATA10.BIN"), "wb") as f:
        for label, usa, src in items:
            data = pal.read(src)
            placed[usa] = (10, sec, len(data), pal.entries[src][5], label)
            f.write(data + bytes(-len(data) % SECTOR))
            sec += (len(data) + SECTOR - 1) // SECTOR
    print("ZZDATA10.BIN: %d files, %d MB" % (len(items), (sec * SECTOR) >> 20))

    def usa_read(name):
        r = next(r for r in rows if r[0] == bhash(name))
        with open(os.path.join(nfs, "ZZDATA%d.BIN" % r[1]), "rb") as f:
            f.seek(r[2] * SECTOR)
            return f.read(r[4])

    speech = {ext: pal.read(bhash("sound\\speech\\UG2_Speech_%s.%s" % (c, ext))) for ext in SPEECH}
    zz11 = bytearray()
    for name in MEMFILES:
        b = bytearray(usa_read(name))
        ents = bundle_entries(b)
        for ext in SPEECH:
            off, size, size2 = ents[bhash("sound\\speech\\UG2_Speech." + ext)]
            if size != len(speech[ext]) or size2 != size:
                sys.exit("%s: UG2_Speech.%s is %d bytes, the %s one %d" % (name, ext, size, text, len(speech[ext])))
            b[off:off + size] = speech[ext]
        placed[bhash(name)] = (11, len(zz11) // SECTOR, len(b), None, name)
        zz11 += b + bytes(-len(b) % SECTOR)
    with open(os.path.join(nfs, "ZZDATA11.BIN"), "wb") as f:
        f.write(zz11)
    print("ZZDATA11.BIN: the two preloaded bundles with the %s speech index" % text)

    base11 = base10 + sec
    for h, (num, s, size, ck, label) in placed.items():
        hit = 0
        for r in rows:
            if r[0] == h:
                # last field: a checksum the game does not check; the PAL one for the PAL data
                r[1:] = [num, s, (base10 if num == 10 else base11) + s, size, r[5] if ck is None else ck]
                hit += 1
        if not hit:
            sys.exit("%s is not in the USA ZDIR.BIN" % label)
    with open(os.path.join(nfs, "ZDIR.BIN"), "wb") as f:
        for r in rows:
            f.write(struct.pack("<6I", *r))
    with open(os.path.join(nfs, "ZZDATA10.BIN"), "rb") as f:
        for label, usa, src in items:
            _, s, size, _, _ = placed[usa]
            f.seek(s * SECTOR)
            if f.read(size) != pal.read(src):
                sys.exit("read back failed: " + label)
    print("ZDIR.BIN: done, %s (ZDIR_english.BIN brings English back)" % text)


if __name__ == "__main__":
    main()
