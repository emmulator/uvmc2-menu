#!/usr/bin/env python3
"""Finds the game catalogue in a firmware flash image by its shape, with no fixed addresses: the prototype of what the
menu would do at runtime. scan_firmware.py <flash.bin> prints what it found as JSON; --verify unpacks every game."""
import json, struct, sys, zlib
d = open(sys.argv[1], 'rb').read(); B = 0x10000000; N = len(d)
u = lambda o: struct.unpack_from('<I', d, o)[0]
LIMIT = 0x100000      # tables are in the first MB in every version seen; reads cost time at runtime
def st(v):
    if not B <= v < B + N: return None
    e = d.find(b'\0', v - B, v - B + 64)
    if e <= v - B: return None
    x = d[v - B:e]
    return x.decode('latin1') if all(32 <= c < 127 for c in x) else None
reads = 0
def find_games():
    """A game record: flash ptr, size, packed size, crc, name ptr, index in the low byte. Index 0 starts a run of them."""
    global reads
    for o in range(0, LIMIT, 4):
        reads += 1
        if u(o + 20) & 0xffffff00 and u(o + 20) & 0xff: continue
        if u(o + 20) & 0xff != 0 or not B <= u(o) < B + N or not st(u(o + 16)): continue
        k = 1
        while True:
            r = o + k * 24
            if not (B <= u(r) < B + N and u(r + 4) and u(r + 8) and st(u(r + 16)) and u(r + 20) & 0xff == k & 0xff): break
            k += 1
        if k >= 16: return o, k
    return None
def find_lists(end, ng):
    """Name-list tables: 12-byte {name, index list, count} just before the game table, split where the indexes restart."""
    tables = []; o = end - 12; run = []
    lo = max(0, end - 0x800)
    out = []; o = lo
    while o < end:
        n, p, c = st(u(o)), u(o + 4), u(o + 8)
        if n and B <= p < B + N and 0 < c <= ng and max(d[p - B:p - B + c]) < ng and len(n) >= 2:
            run.append((o, n, p, c)); o += 12
        else:
            if len(run) >= 3: out.append(run)
            run = []; o += 4
    if len(run) >= 3: out.append(run)
    return out
g = find_games()
if not g: print(json.dumps(dict(found=False))); sys.exit(1)
o, n = g
games = [dict(name=st(u(o + i*24 + 16)), crc=u(o + i*24 + 12), size=u(o + i*24 + 4), ptr=u(o + i*24), packed=u(o + i*24 + 8)) for i in range(n)]
ls = find_lists(o, n)
res = dict(found=True, table=hex(o), games=n, words_read=reads, lists=[dict(at=hex(r[0][0]), entries=len(r), names=[x[1] for x in r][:4]) for r in ls])
if '--verify' in sys.argv:
    ok = wrap = bad = 0; lost = []
    for x in games:
        raw = d[x['ptr'] - B:x['ptr'] - B + x['packed']]; r = None
        for skip, w in ((0, 15), (0, -15), (1, 15)):
            try: r = zlib.decompressobj(w).decompress(raw[skip:]); break
            except Exception: pass
        if r is None and x['packed'] == x['size']: r = raw
        if r is not None and len(r) == x['size'] and zlib.crc32(r) == x['crc']: ok += 1
        else: bad += 1; lost.append(x['name'])
    res.update(verified=ok, failed=bad, failed_names=lost[:6])
print(json.dumps(res))
