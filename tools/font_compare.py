#!/usr/bin/env python3
"""Compares the three fonts. font_compare.py <uvm2_font.h> <vpy.c> <out.html> [out.png]"""
import re, sys
fh, vc, out = open(sys.argv[1]).read(), open(sys.argv[2]).read(), sys.argv[3]
nums = lambda n: [int(x) for x in re.search(n + r'\[\] = \{(.*?)\};', fh, re.S)[1].replace('\n', ' ').split(',') if x.strip()]
data, off, ln = nums('uvm2_font_data'), nums('uvm2_font_offset'), nums('uvm2_font_length')
ours = {m[1]: m[2] for m in re.finditer(r'/\* (.) \*/ "([^"]*)"', open('src/font_data.h').read())}
arrs = {m[1]: [int(x) for x in m[2].split(',')] for m in re.finditer(r'static const signed char (F_\w+)\[\]\s*=\s*\{([^}]*)\}', vc)}
names = re.findall(r'F_\w+', re.search(r'FONT_ABC\[[^\]]*\]\s*=\s*\{(.*?)\};', vc, re.S)[1])
def g_ours(c):
    return [[(int(p[i]), int(p[i+1])) for i in range(0, len(p), 2)] for p in ours.get(c, '').split()]
def g_sdk2(c):
    i = ord(c) - 32
    if not 0 <= i < len(ln): return []
    t = data[off[i]:off[i] + ln[i]]; r = []
    for k in range(0, len(t), 3):
        if t[k] == 1: r.append([(t[k+1], t[k+2])])
        elif r: r[-1].append((t[k+1], t[k+2]))
    return [p for p in r if len(p) > 1]
def g_port(c):
    a = arrs.get(names[ord(c) - 32]); r = []; x = y = 0; cur = None
    if not a: return []
    i = 0
    while i + 2 < len(a) and (i == 0 or a[i] <= 0):
        nx, ny = x + a[i+2] / 15, y + a[i+1] / 15
        if a[i] != 0:
            if cur is None: cur = [(x, y)]; r.append(cur)
            cur.append((nx, ny))
        else: cur = None
        x, y = nx, ny; i += 3
    return r
FONTS = [('Ours (src/font_data.h)', g_ours, 6), ('SDK uvm2_font.h (uvm2_print_text)', g_sdk2, 6), ('SDK vpy.c port of the PiTrex font', g_port, 12)]
core = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'
def counts(f): 
    st = sum(len(p) - 1 for c in core for p in f(c)); ju = sum(max(len(f(c)) - 1, 0) for c in core); return st, ju
def path(t, f, adv, k, ox=0):
    return ''.join('M' + ' L'.join(f'{(i*adv+x)*k+ox:.1f} {(7-y)*k:.1f}' for x, y in p) for i, c in enumerate(t) for p in f(c))
def line(t, f, adv, k, w=1.4):
    return f'<svg width="{len(t)*adv*k+8:.0f}" height="{9*k:.0f}" style="display:block"><path d="{path(t,f,adv,k)}" fill="none" stroke="#b8ffd8" stroke-width="{w}" stroke-linecap="round" stroke-linejoin="round"/></svg>'
samples = ['ABCDEFGHIJKLM', 'NOPQRSTUVWXYZ', '0123456789', 'STAR WARS 1983', 'VECTORBLADE-SJ']
h = '<!doctype html><meta charset=utf-8><title>Font comparison</title><style>body{background:#050807;color:#cfe;font:15px system-ui;margin:24px}h2{margin:24px 0 4px;font-size:17px}p{margin:0 0 10px;color:#9bb}.m{background:#000;padding:14px;display:inline-block;border:1px solid #1d2b26}</style><h1>The three fonts</h1>'
for n, f, adv in FONTS:
    s, j = counts(f)
    h += f'<h2>{n}</h2><p>A-Z and 0-9: {s} strokes, {j} jumps. A: {sum(len(p)-1 for p in f("A"))} strokes.</p><div class=m>' + ''.join(line(t, f, adv, 6 if adv == 6 else 3) for t in samples) + '</div>'
    print(n, s, j, 'A', sum(len(p)-1 for p in f('A')))
open(out, 'w').write(h)
if len(sys.argv) > 4:
    from PIL import Image, ImageDraw
    im = Image.new('RGB', (1000, 3 * 330), 'black'); d = ImageDraw.Draw(im); y0 = 0
    for n, f, adv in FONTS:
        d.text((8, y0 + 4), n, fill='white'); k = 7 if adv == 6 else 3.5
        for r, t in enumerate(samples):
            for i, c in enumerate(t):
                for p in f(c):
                    d.line([(8 + (i * adv + x) * k, y0 + 24 + r * 60 + (6 - yy) * k) for x, yy in p], fill=(180, 255, 216), width=2)
        y0 += 330
    im.save(sys.argv[4])
