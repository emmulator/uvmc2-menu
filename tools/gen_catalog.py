#!/usr/bin/env python3
"""Reads the game catalogue out of a UVMC2 firmware flash image (made from the .uf2) and prints it as JSON.
gen_catalog.py <flash.bin> > catalog.json   Layout is for firmware 1.1.13a; it stops if the tables do not look right."""
import json, struct, sys
d = open(sys.argv[1], 'rb').read(); B = 0x10000000
u = lambda o: struct.unpack_from('<I', d, o)[0]
def st(v):
    if not B <= v < B + len(d): return None
    x = d[v - B:d.find(b'\0', v - B)]
    return x.decode('latin1') if 0 < len(x) < 64 and all(32 <= c < 127 for c in x) else None
GAMES, NG = 0x54554, 226
games = []
for i in range(NG):
    o = GAMES + i * 24
    n = st(u(o + 16)); assert n and u(o + 20) & 0xff == i, (i, n)
    games.append(dict(name=n, size=u(o + 4), crc=u(o + 12), flags=u(o + 20) >> 8, developers=[], categories=[]))
def lists(lo, hi, key):
    out = []
    for o in range(lo, hi, 12):
        n, p, c = st(u(o)), u(o + 4), u(o + 8)
        assert n and 0 < c <= NG and B <= p < B + len(d), hex(o)
        out.append(n)
        for g in d[p - B:p - B + c]: games[g][key].append(n)
    return out
devs = lists(0x54164, 0x54314, 'developers')
cats = lists(0x54438, 0x544c8, 'categories')
print(json.dumps(dict(games=games, developers=devs, categories=cats), indent=0))
