#!/usr/bin/env python3
"""A preview page of the font, with stroke counts against the SDK font. font_preview.py <vpy.c> <out.html>"""
import re, sys
src = open('src/font_data.h').read()
gl = {}
for m in re.finditer(r'/\* (.) \*/ "([^"]*)"', src): gl[m[1]] = m[2]
vpy = open(sys.argv[1]).read()
arrs = {m[1]: [int(x) for x in m[2].split(',')] for m in re.finditer(r'static const signed char (F_\w+)\[\] = \{([^}]*)\}', vpy)}
tab = re.search(r'FONT_ABC\[[^\]]*\] = \{(.*?)\};', vpy, re.S)[1]
names = re.findall(r'F_\w+', tab)
def sdk_counts(c):
    """Strokes, and blanked jumps between strokes (not the move to the start or the advance after the end)."""
    a = arrs.get(names[ord(c) - 0x20])
    if a is None: return 0, 0
    ops = []; i = 0
    while i + 2 < len(a) and (i == 0 or a[i] <= 0):
        if a[i] != 0: ops.append('s')
        elif a[i+1] or a[i+2]: ops.append('m')
        i += 3
    t = ''.join(ops).strip('m')
    return t.count('s'), len(re.findall('m+', t))
def ours(g): return sum(len(p)//2 - 1 for p in g.split()), g.count(' ')
def svg(g, k=14, w=2.2):
    d = ''
    for p in g.split():
        pts = [(int(p[i])*k + 8, (6-int(p[i+1]))*k + 8) for i in range(0, len(p), 2)]
        d += 'M' + ' L'.join(f'{x} {y}' for x, y in pts)
    return f'<svg width="{4*k+16}" height="{6*k+16}"><path d="{d}" fill="none" stroke="#b8ffd8" stroke-width="{w}" stroke-linecap="round" stroke-linejoin="round"/></svg>'
def line(t, k=4, w=1.3):
    out = ''
    for i, c in enumerate(t):
        g = gl.get(c)
        if g:
            for p in g.split():
                pts = [(int(p[j])*k + i*6*k, (6-int(p[j+1]))*k + 2) for j in range(0, len(p), 2)]
                out += 'M' + ' L'.join(f'{x} {y}' for x, y in pts)
    return f'<svg width="{len(t)*6*k}" height="{6*k+4}" style="display:block"><path d="{out}" fill="none" stroke="#b8ffd8" stroke-width="{w}" stroke-linecap="round"/></svg>'
core = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'
to = tj = so = sj = 0
cells = ''
for c, g in gl.items():
    o, oj = ours(g); s, sjj = sdk_counts(c)
    if c in core: to += o; tj += oj; so += s; sj += sjj
    cells += f'<div class=c><b>{c}</b>{svg(g)}<small>{o} <i>{'was ' + str(s) if s else ''}</i></small></div>'
menu = ['UVMC2 MENU', '>ALL GAMES', ' ARCADE / ATARI', ' STAR WARS 1983', ' VECTORBLADE-SJ', ' ASTEROIDS 0 7A', ' DEFENDER', ' BATTLEZONE 0 3A', ' SYSTEM']
html = f'''<!doctype html><meta charset=utf-8><title>UVMC2 menu font</title><style>
body{{background:#050807;color:#cfe;font:15px system-ui;margin:24px}}.g{{display:flex;flex-wrap:wrap;gap:8px}}
.c{{border:1px solid #1d2b26;padding:6px;text-align:center;width:88px}}.c b{{display:block;color:#7a9}}small{{color:#bfe}}i{{color:#678;font-style:normal}}
.m{{background:#000;padding:18px;display:inline-block;border:1px solid #1d2b26}}</style>
<h1>Our font</h1><p><b>A-Z and 0-9: {to} strokes and {tj} jumps, against {so} strokes and {sj} jumps for the SDK font.</b> Under each glyph: its strokes, and the old count.</p>
<h2>In the menu</h2><div class=m>{''.join(line(l) for l in menu)}</div><h2>All glyphs</h2><div class=g>{cells}</div>'''
open(sys.argv[2], 'w').write(html)
print(f'A-Z 0-9: ours {to} strokes {tj} jumps; SDK {so} strokes {sj} jumps; A: {ours(gl["A"])} vs {sdk_counts("A")}')
