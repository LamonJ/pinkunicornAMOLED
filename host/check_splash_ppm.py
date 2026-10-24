#!/usr/bin/env python3
"""Compare a splash PPM from test_splash_png to the PNG it was decoded from.

The board stores RGB565 and masks the round glass. This applies that same
conversion to a 466x466 PNG and requires every pixel to match.
"""
import sys

from PIL import Image

PANEL = 466


def unpack(p):
    r = (p >> 11) & 31
    g = (p >> 5) & 63
    b = p & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def pack(r, g, b, a):
    if a == 0:
        return 0
    if a != 255:
        r = (r * a) // 255
        g = (g * a) // 255
        b = (b * a) // 255
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)


def main():
    if len(sys.argv) != 3:
        print("usage: check_splash_ppm.py in.png out.ppm", file=sys.stderr)
        return 2
    im = Image.open(sys.argv[1]).convert("RGBA")
    if im.size != (PANEL, PANEL):
        print("expected %dx%d, got %s" % (PANEL, PANEL, im.size), file=sys.stderr)
        return 1
    raw = open(sys.argv[2], "rb").read()
    header, body = raw.split(b"\n", 1)
    dims, body = body.split(b"\n", 1)
    depth, body = body.split(b"\n", 1)
    if header != b"P6" or dims != b"466 466" or depth != b"255":
        print("bad ppm header", file=sys.stderr)
        return 1
    if len(body) != PANEL * PANEL * 3:
        print("bad ppm length", file=sys.stderr)
        return 1
    src = im.load()
    center = PANEL // 2
    limit = center * center
    mismatch = 0
    for y in range(PANEL):
        for x in range(PANEL):
            r, g, b, a = src[x, y]
            pixel = pack(r, g, b, a)
            dx, dy = x - center, y - center
            if dx * dx + dy * dy > limit:
                pixel = 0
            got = (body[(y * PANEL + x) * 3],
                   body[(y * PANEL + x) * 3 + 1],
                   body[(y * PANEL + x) * 3 + 2])
            if got != unpack(pixel):
                mismatch += 1
    print("splash ppm mismatch %d" % mismatch)
    return 0 if mismatch == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
