#!/usr/bin/env python3
"""Makes the release archive: dist.py VERSION menu.um2 transfer.um2 OUTDIR  ->  OUTDIR/uvmc2-menu-VERSION.zip

Inside, under uvmc2-menu-VERSION/: card/ is what goes on the SD card (autostart.um2, MENU.um2, menu/menu.default.ini,
menu/transfer.um2), copied to the card's root; beside it the README, licence, notices, changelog, examples, the
README's photos, and the two user tools (gen_card_ini.py, hs_dump.py) with the catalogue they read.
Stops if CHANGELOG.md has no heading for VERSION, so a release cannot go out without its notes."""
import os, re, sys, zipfile

version, menu, transfer, outdir = sys.argv[1:5]
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
def src(p): return os.path.join(root, p)

if not re.search(r'^## ' + re.escape(version) + r'\b', open(src('CHANGELOG.md')).read(), re.M):
    sys.exit(f'CHANGELOG.md has no "## {version}" heading: write the release notes first')

top = f'uvmc2-menu-{version}'
files = [
    (menu, 'card/autostart.um2'), (menu, 'card/MENU.um2'),
    (src('card/menu/menu.default.ini'), 'card/menu/menu.default.ini'), (transfer, 'card/menu/transfer.um2'),
]
# What a user needs: the documents, the examples, the two photos the README shows, and the two tools that run with plain
# Python 3 (with the catalogue they read). The developer tools and notes stay in the source repository.
for f in ('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md',
          'docs/default-menu.jpg', 'docs/customized-menu.jpg',
          'tools/gen_card_ini.py', 'tools/hs_dump.py', 'data/firmware-catalog-1.1.13a.json'):
    files.append((src(f), f))
for f in sorted(os.listdir(src('examples'))):
    files.append((src('examples/' + f), 'examples/' + f))
os.makedirs(outdir, exist_ok=True)
out = os.path.join(outdir, top + '.zip')
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for p, name in files:
        info = zipfile.ZipInfo(f'{top}/{name}', (2026, 1, 1, 0, 0, 0))   # a fixed date: the same inputs give the same archive
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        z.writestr(info, open(p, 'rb').read())
print(f'{out}: {len(files)} files')
