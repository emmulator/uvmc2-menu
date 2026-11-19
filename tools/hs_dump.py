#!/usr/bin/env python3
"""Reads the firmware's high-score table, as DIAGNOSTICS saves it (button 3 there writes menu/hiscores.bin on the card),
and says which game each record belongs to. A record's key is the CRC-32 of the game: for a built-in, the CRC in the
firmware's game list (data/firmware-catalog-1.1.13a.json); for a card .bin/.vec, the CRC of the file (of its first 16 KB
slots 0, 2, 0, 2 for a 64 KB file, as the firmware re-orders it).

    hs_dump.py menu/hiscores.bin [--card /run/media/.../UVMC2] [--catalog data/firmware-catalog-1.1.13a.json]
"""
import argparse, glob, json, os, struct, sys, zlib

REC = 16


def records(table):
    """(slot, key, score bytes) for every live record, as the firmware counts them: not free, not deleted."""
    for i in range(0, len(table) - REC + 1, REC):
        key = struct.unpack_from('<I', table, i)[0]
        if key in (0, 0xFFFFFFFE, 0xFFFFFFFF):
            continue
        yield i // REC, key, table[i + 4:i + 11]


def key_of_file(data):
    """The firmware's key for a card file, or None where it keeps no score (256 KB, streamed) or refuses the size."""
    if len(data) <= 0x8000:
        return zlib.crc32(data) & 0xFFFFFFFF
    if len(data) == 0x10000:
        # file chunk 0 and chunk 2 (16 KB each), then both again: the first 64 KB of the firmware's buffer
        return zlib.crc32(data[0:0x4000] + data[0x8000:0xC000] + data[0:0x4000] + data[0x8000:0xC000]) & 0xFFFFFFFF
    return None


def score_text(raw):
    """(text, last byte, label). The BIOS's six characters, then $80 (or blank flash where a game writes only the
    digits). The label: 'score' for a real one; 'launch only' for blank or zero, which the firmware writes for any game
    it starts; 'not a score' where the game used the slot as scratch memory and the firmware stored whatever was there."""
    chars = raw[:6]
    text = ''.join(chr(c) if 32 <= c < 127 else '?' for c in chars).strip()
    if any(c != 32 and not 48 <= c <= 57 for c in chars):
        return text, raw[6], 'not a score'
    if text.strip('0') == '':
        return (text or '(blank)'), raw[6], 'launch only'
    return text, raw[6], 'score'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('table')
    ap.add_argument('--card', help="the card's root: its .bin and .vec files are matched too")
    ap.add_argument('--catalog', default=os.path.join(os.path.dirname(__file__), '..', 'data', 'firmware-catalog-1.1.13a.json'))
    a = ap.parse_args()
    table = open(a.table, 'rb').read()
    names = {}
    for g in json.load(open(a.catalog))['games']:
        names.setdefault(g['crc'], 'built-in: ' + g['name'])
    if a.card:
        for path in sorted(glob.glob(os.path.join(a.card, '**', '*'), recursive=True)):
            if os.path.isfile(path) and path.lower().endswith(('.bin', '.vec')):
                k = key_of_file(open(path, 'rb').read())
                if k is not None:
                    names.setdefault(k, 'card: ' + os.path.relpath(path, a.card))
    live = list(records(table))
    free = sum(struct.unpack_from('<I', table, i)[0] == 0xFFFFFFFF for i in range(0, len(table), REC))
    print(f'{len(live)} live records, {free} free of {len(table) // REC}')
    seen = {}
    for slot, key, raw in live:
        seen.setdefault(key, []).append((slot, raw))
    counts = {'score': 0, 'launch only': 0, 'not a score': 0}
    for key, recs in sorted(seen.items(), key=lambda kv: kv[1][0][0]):
        slot, raw = recs[0]
        text, last, label = score_text(raw)
        counts[label] += 1
        dup = f'   (+{len(recs) - 1} more record(s) with this key; the first one counts)' if len(recs) > 1 else ''
        print(f'{slot:5d}  {key:08X}  {text:>8}  end={last:02X}  {label:<12} {names.get(key, "unknown game")}{dup}')
    print(f"{counts['score']} with a score, {counts['launch only']} launch only, {counts['not a score']} not a score "
          f'(of {len(seen)} keys); {len(live)} of {len(table) // REC} slots in use')


if __name__ == '__main__':
    sys.exit(main())
