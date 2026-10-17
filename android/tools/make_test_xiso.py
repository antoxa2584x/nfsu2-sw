#!/usr/bin/env python3
"""Build a small XDVDFS image from a directory (for testing the Android ISO
extractor on a desktop JVM). Usage: make_test_xiso.py <dir> <out.iso> [base_offset]"""
import os, struct, sys

SECTOR = 2048

def align(n, a): return (n + a - 1) // a * a

def build_dir(path, alloc):
    """Returns (table_bytes, files_to_place). `alloc(size)` returns the first sector
    for `size` bytes of data and reserves it."""
    items = sorted(os.listdir(path), key=lambda s: s.lower())
    recs = []   # (name, is_dir, sector, size, payload)
    for name in items:
        full = os.path.join(path, name)
        if os.path.isdir(full):
            sub, subsize = build_dir(full, alloc)
            sec = alloc(len(sub)) if sub else 0
            recs.append((name, True, sec, len(sub), sub))
        else:
            data = open(full, 'rb').read()
            sec = alloc(len(data)) if data else 0
            recs.append((name, False, sec, len(data), data))
    if not recs:
        return b'', 0
    # balanced BST over the sorted records
    order = []
    def layout(lo, hi):
        if lo > hi: return None
        mid = (lo + hi) // 2
        node = {'rec': recs[mid]}
        node['l'] = layout(lo, mid - 1)
        node['r'] = layout(mid + 1, hi)
        return node
    root = layout(0, len(recs) - 1)
    offs = {}
    pos = 0
    def place(n):
        nonlocal pos
        if n is None: return
        offs[id(n)] = pos
        pos += align(14 + len(n['rec'][0]), 4)
        place(n['l']); place(n['r'])
    place(root)
    table = bytearray(b'\xff' * align(pos, SECTOR))
    def emit(n):
        if n is None: return
        name, isdir, sec, size, _ = n['rec']
        l = offs[id(n['l'])] // 4 if n['l'] else 0
        r = offs[id(n['r'])] // 4 if n['r'] else 0
        o = offs[id(n)]
        struct.pack_into('<HHIIBB', table, o, l, r, sec, size, 0x10 if isdir else 0x20, len(name))
        table[o + 14:o + 14 + len(name)] = name.encode('latin-1')
        emit(n['l']); emit(n['r'])
    emit(root)
    # attach payloads for writing
    build_dir.payloads = getattr(build_dir, 'payloads', [])
    for name, isdir, sec, size, payload in recs:
        if size and payload: build_dir.payloads.append((sec, payload))
    return bytes(table), len(table)

def main():
    src, out = sys.argv[1], sys.argv[2]
    base = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0
    nxt = [33]                      # sector 32 is the volume descriptor
    def alloc(size):
        s = nxt[0]; nxt[0] += max(1, align(size, SECTOR) // SECTOR); return s
    build_dir.payloads = []
    table, size = build_dir(src, alloc)
    root_sec = alloc(len(table))
    build_dir.payloads.append((root_sec, table))
    with open(out, 'wb') as f:
        f.seek(base + 32 * SECTOR)
        vd = bytearray(SECTOR)
        vd[0:20] = b'MICROSOFT*XBOX*MEDIA'
        struct.pack_into('<II', vd, 0x14, root_sec, len(table))
        vd[0x7EC:0x7EC + 20] = b'MICROSOFT*XBOX*MEDIA'
        f.write(vd)
        for sec, payload in build_dir.payloads:
            f.seek(base + sec * SECTOR); f.write(payload)
        f.truncate(base + nxt[0] * SECTOR)
main()
