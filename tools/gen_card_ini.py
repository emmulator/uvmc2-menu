#!/usr/bin/env python3
"""Writes the catalogue's names, categories and developers for the games found on a card, matched by CRC-32, then by
name. gen_card_ini.py <card root> [catalogue.json] prints menu/catalog.ini (python3 tools/gen_card_ini.py E:/ > E:/menu/catalog.ini);
it runs from anywhere and needs only Python 3 and data/firmware-catalog-1.1.13a.json. Only .bin and .vec files in the card's root
are looked at. The file starts with a folder rule for the root, because the default menu lists no card files by itself:
a matched game goes in its firmware categories, the others in HOMEBREW. A rule for the same folder in menu.ini (read
later) changes this one rather than adding a second."""
import json, os, re, sys, zlib
root = sys.argv[1]
cat = json.load(open(sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data', 'firmware-catalog-1.1.13a.json')))
slug = lambda s: re.sub('[^a-z0-9]', '', s.lower())
by_crc = {g['crc']: g for g in cat['games']}
by_name = {slug(g['name']): g for g in cat['games']}
print('; catalog.ini: made by tools/gen_card_ini.py for the games on this card. DO NOT EDIT: tools/gen_card_ini.py (make card-ini) replaces it.')
print('; Put your changes in menu.ini, which is read after this file.')
print('\n; the .bin and .vec files in the root: each in its category below, or in HOMEBREW')
print('[CATALOGUED FILES]\nfolder   = .\nfiles    = bin vec\nimage    = {dir}/{file}\ncategory = HOMEBREW')
n = 0
for f in sorted(os.listdir(root), key=str.lower):
    p = os.path.join(root, f)
    if not os.path.isfile(p) or not f.lower().endswith(('.bin', '.vec')): continue
    g = by_crc.get(zlib.crc32(open(p, 'rb').read())) or by_name.get(slug(f.rsplit('.', 1)[0]))
    if not g or not (g['categories'] or g['developers'] or g['name']): continue
    print(f"\n[{g['name'].upper()[:31]}]\nfile     = {f}")
    if g['categories']:
        print(f"category = {g['categories'][0].upper()}")
        if len(g['categories']) > 1: print('also     = ' + ', '.join(c.upper() for c in g['categories'][1:]))
    if g['developers']:
        line = 'BY ' + ', '.join(g['developers']).upper()
        print('text     = ' + line[:47])
    n += 1
print(f'matched {n} games', file=sys.stderr)
