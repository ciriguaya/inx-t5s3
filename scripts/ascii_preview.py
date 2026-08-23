#!/usr/bin/env python3
"""Render a BMP screenshot as coarse ASCII art so the UI layout can be verified
without viewing the image. Dense-darks become '#', sparse become ' '."""
import struct
import sys

from PIL import Image


def ascii_art(path, cols=64, rows=44):
    im = Image.open(path).convert('L')
    w, h = im.size
    cw = max(1, w // cols)
    ch = max(1, h // rows)
    out = []
    for r in range(rows):
        line = []
        for c in range(cols):
            box = im.crop((c * cw, r * ch, min(w, (c + 1) * cw), min(h, (r + 1) * ch)))
            px = list(box.getdata())
            # E-ink: paper=255 (white), ink=0 (black). Fraction of dark pixels.
            dark = sum(1 for v in px if v < 128) / max(1, len(px))
            line.append('#' if dark > 0.5 else '+' if dark > 0.2 else '.' if dark > 0.05 else ' ')
        out.append(''.join(line))
    return '\n'.join(out)


if __name__ == '__main__':
    for path in sys.argv[1:]:
        print('=' * 64)
        print(path)
        print(ascii_art(path))
