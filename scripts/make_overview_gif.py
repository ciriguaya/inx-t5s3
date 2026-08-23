#!/usr/bin/env python3
"""Assemble the inx-t5s3 interface-overview GIF from simulator screenshots.

Screenshots come from scripts/sim_tour.sh at the device's native 540x960
portrait logical resolution (1080x1920 px on a 2x Retina SDL surface). Frames
are downscaled to 540x960 and reduced to a clean 4-level gray so the e-ink look
survives GIF quantization.

Usage: python3 scripts/make_overview_gif.py [out.gif]
"""
import os
import sys

from PIL import Image

FRAME_MS = 1200          # per-frame display time
HOLD_MS = 2400           # extra hold on the first frame (cover)
LOOP = 0                 # 0 = loop forever


def eink_frame(path):
    im = Image.open(path).convert('L')
    im = im.resize((540, 960), Image.LANCZOS)
    # Map to 4 gray levels: paper white, light gray, dark gray, ink black.
    im = im.point(lambda v: 255 if v >= 192 else (170 if v >= 128 else (85 if v >= 64 else 0)))
    return im.convert('P', palette=Image.ADAPTIVE, colors=4)


def main():
    shots_dir = sys.argv[1] if len(sys.argv) > 1 else '/tmp/simshots'
    out = sys.argv[2] if len(sys.argv) > 2 else 'docs/overview.gif'
    names = [f'{i:02d}_{n}.bmp' for i, n in enumerate(
        ['home', 'library', 'reader', 'bookmenu', 'quickmenu', 'quotes'], 1)]
    frames = [eink_frame(os.path.join(shots_dir, n)) for n in names]

    os.makedirs(os.path.dirname(out), exist_ok=True)
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=FRAME_MS,
                   loop=LOOP, disposal=2, optimize=True)
    print(f'wrote {out} ({frames[0].size[0]}x{frames[0].size[1]}, '
          f'{len(frames)} frames @ {FRAME_MS}ms)')


if __name__ == '__main__':
    main()
