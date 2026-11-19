#!/usr/bin/env python3
"""Write a stress menu ini: N sections, one built-in lookup every 50.

Usage: gen_stress_ini.py [N] OUT     (default N 900)

Each section has the shape of the large parser test, but compactly: a distinct
name, a game file, a category and a note. The menu reads an ini file into a
32 KB buffer, so 900 sections with long names would not fit; this keeps the
same four keys with short values. Every 50th is [R<i>] with file = @fw/Rip Off
and category = ORIGINAL, which exercises the built-in lookup.

Each game gets its own g<i> file, because the menu keeps one entry per image
in a category: the same image repeated would collapse to one row. The preview
proof puts empty g<i> files on the card fixture, as the menu hides an entry
whose image is missing.
"""

import sys

DEFAULT_N = 900


def main(argv):
    args = argv[1:]
    if len(args) == 0 or len(args) > 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    if len(args) == 1:
        n, out = DEFAULT_N, args[0]
    else:
        n, out = int(args[0]), args[1]
    if n < 0:
        print("N must not be negative", file=sys.stderr)
        return 2

    text = []
    for i in range(n):
        if i % 50 == 0:
            text.append(f"[R{i}]\nfile=@fw/Rip Off\ncategory=ORIGINAL\ntext=N\n")
        else:
            text.append(f"[G{i}]\nimage=g{i}\ncategory={chr(ord('A') + i % 20)}\ntext=N\n")
    with open(out, "w", encoding="ascii") as f:
        f.write("".join(text))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
