#!/usr/bin/env python3
"""Pixel-diff the C port's output against the golden frames.

Exit code is non-zero if any frame differs, so this drops straight into CI
or a pre-commit hook. Writes a side-by-side sheet for anything that fails.
"""
import sys, os, glob
from PIL import Image

PPM_DIR = sys.argv[1] if len(sys.argv) > 1 else "out"
GOLD_DIR = sys.argv[2] if len(sys.argv) > 2 else "../golden-frames"

fails, oks, missing = [], [], []
sheets = []

for ppm in sorted(glob.glob(os.path.join(PPM_DIR, "*.ppm"))):
    name = os.path.basename(ppm)[:-4]
    gold = os.path.join(GOLD_DIR, name + ".png")
    if not os.path.exists(gold):
        missing.append(name)
        continue
    a = Image.open(ppm).convert("RGB")
    b = Image.open(gold).convert("RGB")
    if a.size != b.size:
        fails.append((name, -1, 0))
        continue
    pa, pb = a.load(), b.load()
    bad, first = 0, None
    diff = Image.new("RGB", a.size, (0, 0, 0))
    pd = diff.load()
    for y in range(a.size[1]):
        for x in range(a.size[0]):
            if pa[x, y] != pb[x, y]:
                bad += 1
                pd[x, y] = (255, 0, 0)
                if first is None:
                    first = (x, y, pa[x, y], pb[x, y])
    if bad:
        fails.append((name, bad, first))
        row = Image.new("RGB", (64 * 3 + 8, 64), (20, 20, 20))
        row.paste(b, (0, 0)); row.paste(a, (68, 0)); row.paste(diff, (136, 0))
        sheets.append((name, row))
    else:
        oks.append(name)

S = 5
if sheets:
    sheet = Image.new("RGB", ((64 * 3 + 8) * S, 64 * S * len(sheets)), (20, 20, 20))
    for i, (_, row) in enumerate(sheets):
        sheet.paste(row.resize(((64 * 3 + 8) * S, 64 * S), Image.NEAREST), (0, i * 64 * S))
    sheet.save("diff.png")

print(f"match   {len(oks)}")
for n in oks:
    print(f"  ok    {n}")
if missing:
    print(f"no reference for {len(missing)}: {', '.join(missing)}")
if fails:
    print(f"DIFFER  {len(fails)}   (gold | ours | diff  ->  diff.png)")
    for n, bad, first in fails:
        if bad == -1:
            print(f"  SIZE  {n}")
        else:
            x, y, got, want = first
            print(f"  {bad:5d}px {n}  first ({x},{y}) ours={got} gold={want}")
sys.exit(1 if fails else 0)
