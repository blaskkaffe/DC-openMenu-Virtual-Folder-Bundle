#!/usr/bin/env python3
"""Cuts the matte arm and leg pieces of the calibration man (src/openmenu/src/pose_man.c) out of the reference picture.

Usage: make_sprites.py reference.jpg outdir [pose_man_tex.inc]
With a third argument also writes the four pieces, scaled to PM_TEXF of their size, as ARGB4444 arrays for pose_man.c.
Writes arm_l.png arm_r.png leg_l.png leg_r.png (transparent outside, the inside flat matte, the cream rim kept) and prints the boxes
that pm_sprite_defs in pose_man.c holds. The reference picture is not part of the repository.
"""
import sys
from collections import deque

import numpy as np
from PIL import Image, ImageFilter

MATTE = np.array([13, 19, 28], np.float32)
SHOULDER_L, SHOULDER_R, HIPS, CENTRE = 548, 657, 790, 602
HAND_L, HAND_R = 500, 705  # below the elbows the arm is cut further out, so that nothing of the legs comes with it


TEXF = 0.17  # the pieces in the program are this much of the reference's size (the man is about 0.15 on screen)


def main(ref, outdir, inc=None):
    a = np.asarray(Image.open(ref).convert('RGB')).astype(np.float32)
    h, w, _ = a.shape
    lum = a.mean(axis=2)
    dark = lum < 60
    out = np.zeros((h, w), bool)  # the outside: dark pixels reachable from the border
    dq = deque()
    for x in range(w):
        for y in (0, h - 1):
            if dark[y, x] and not out[y, x]:
                out[y, x] = True
                dq.append((y, x))
    for y in range(h):
        for x in (0, w - 1):
            if dark[y, x] and not out[y, x]:
                out[y, x] = True
                dq.append((y, x))
    while dq:
        y, x = dq.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < h and 0 <= nx < w and dark[ny, nx] and not out[ny, nx]:
                out[ny, nx] = True
                dq.append((ny, nx))
    inside = ~out
    m = Image.fromarray((inside * 255).astype(np.uint8))
    core = np.asarray(m.filter(ImageFilter.MinFilter(25))) > 0  # 12 pixels in from the rim
    keep = inside & ~core & (lum > 120)                          # the cream rim stays, everything else (shine, blue) goes flat
    img = a.copy()
    img[inside & ~keep] = MATTE
    rgba = np.zeros((h, w, 4), np.uint8)
    rgba[..., :3] = img.clip(0, 255).astype(np.uint8)
    rgba[..., 3] = np.asarray(m.filter(ImageFilter.GaussianBlur(1.2)))
    full = Image.fromarray(rgba, 'RGBA')
    yy, xx = np.mgrid[0:h, 0:w]
    pieces = {
        'arm_l': ((250, 380, 620, 900), (yy > 400) & (yy < 900) & (((yy <= 780) & (xx < SHOULDER_L)) | ((yy > 780) & (xx < HAND_L)))),
        'arm_r': ((590, 380, 960, 900), (yy > 400) & (yy < 900) & (((yy <= 780) & (xx > SHOULDER_R)) | ((yy > 780) & (xx > HAND_R)))),
        'leg_l': ((250, 780, 640, 1380), (xx <= CENTRE) & (yy >= HIPS) & ((yy >= 860) | (xx >= HAND_L + 5))),
        'leg_r': ((560, 780, 960, 1380), (xx > CENTRE) & (yy >= HIPS) & ((yy >= 860) | (xx <= HAND_R - 5))),
    }
    arrays = []
    for name, (box, mask) in pieces.items():
        sub = np.array(full.crop(box))
        sub[..., 3] = (sub[..., 3] * mask[box[1]:box[3], box[0]:box[2]]).astype(np.uint8)
        piece = Image.fromarray(sub, 'RGBA')
        bb = piece.getbbox()
        piece = piece.crop(bb)
        piece.save(f'{outdir}/{name}.png')
        print(name, (box[0] + bb[0], box[1] + bb[1], bb[2] - bb[0], bb[3] - bb[1]))
        if inc:
            small = piece.resize((max(1, round(piece.width * TEXF)), max(1, round(piece.height * TEXF))), Image.LANCZOS)
            px = np.asarray(small).astype(int)
            alpha = (px[..., 3] * 15 + 127) // 255
            word = (alpha << 12) | ((px[..., 0] * 15 + 127) // 255 << 8) | ((px[..., 1] * 15 + 127) // 255 << 4) | ((px[..., 2] * 15 + 127) // 255)
            arrays.append((name, small.width, small.height, word.flatten()))
    if inc:
        with open(inc, 'w') as f:
            f.write('/* made by tools/pose-man/make_sprites.py: the matte pieces of the calibration man, ARGB4444, PM_TEXF of the reference size */\n')
            f.write(f'#define PM_TEXF {TEXF}f\n')
            for name, w, h, data in arrays:
                up = name.upper()
                f.write(f'#define PM_TEX_{up}_W {w}\n#define PM_TEX_{up}_H {h}\n')
                f.write(f'static const uint16_t pm_tex_{name}[{w * h}] = {{\n')
                for i in range(0, len(data), 12):
                    f.write('    ' + ', '.join(f'0x{int(v):04X}' for v in data[i:i + 12]) + ',\n')
                f.write('};\n')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
