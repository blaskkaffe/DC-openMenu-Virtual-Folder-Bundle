#!/usr/bin/env python3
"""Builds feature_badges.h: the Dreamcast feature logos (players, VMU, vibration pack, ...) of the three regions as small ARGB4444
pictures, cut out of scans where we have them and drawn as placeholders where we do not.  See openMenu/FEATURE_LOGOS.md.
usage: gen_badges.py OUT.h PREVIEW.png   (the scans are read from the paths below)"""
import sys, os, numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

A = '/mnt/user-data/attachments/28c3c805-6ef4-54db-97d2-c26fa8864c37/'
SRC = {
    'palsheet': A + '75793cb5-image.png',
    'usbass': A + '1b9b06c4-image.jpg',
    'usnfl': A + '62a2404b-image.webp',
    'afo': '/tmp/claude-0/-home-claude/28c3c805-6ef4-54db-97d2-c26fa8864c37/scratchpad/art_test/src/MK51117_back.png',
    'palbass': A + 'd5329494-image.jpg',
}
FONT = '/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc'
FONT_LAT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
KEYS = ['P1', 'P2', 'P12', 'P14', 'P16', 'ONLINE', 'VMU', 'VIB', 'STICK', 'GUN', 'WHEEL', 'KEYBOARD', 'VGA', 'FISHING', 'MIC', 'MOUSE',
        'MARACAS', 'DREAMEYE', 'MODEM', 'BBA', 'W5500', 'SERIAL', 'DISC']
NAMES = {'P1': '1 Player', 'P2': '2 Players', 'P12': '1-2 Players', 'P14': '1-4 Players', 'P16': '1-6 Players', 'ONLINE': 'Online Multi-play',
         'VMU': 'Visual Memory', 'VIB': 'Vibration Pack', 'STICK': 'Arcade Stick', 'GUN': 'Dreamcast Gun', 'WHEEL': 'Race Controller',
         'KEYBOARD': 'Keyboard', 'VGA': 'VGA', 'FISHING': 'Fishing Controller', 'MIC': 'Microphone', 'MOUSE': 'Mouse', 'MARACAS': 'Maracas',
         'DREAMEYE': 'Dreameye', 'MODEM': 'Modem', 'BBA': 'Broadband Adapter', 'W5500': 'W5500 Ethernet', 'SERIAL': 'Serial Link', 'DISC': 'Discs'}
H_LOGO = 34
H_PILL = 20  # height of the PAL and US logos
H_JP = 26

def load(k):
    return Image.open(SRC[k]).convert('RGB')

def cutout(img):
    """Everything outside the black outline (connected to the border, not dark) becomes transparent."""
    a = np.asarray(img.convert('L'))
    dark = a < 120
    lab, n = ndimage.label(~dark)
    border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    bg = np.isin(lab, list(border))
    rgba = np.dstack([np.asarray(img), np.where(bg, 0, 255).astype(np.uint8)])
    return Image.fromarray(rgba, 'RGBA')

def cutout_tile(img, thr=200, grow=2):
    """The logos' white tiles: the outline may have small gaps, so the barrier is widened before the outside is found; the pixels the widening
    took back from the outside are given back, so only the area outside the outline is transparent and everything inside stays."""
    a = np.asarray(img.convert('L'))
    barrier = ndimage.binary_dilation(a < thr, iterations=grow)
    lab, n = ndimage.label(~barrier)
    border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    bg = np.isin(lab, list(border))
    bg = ndimage.binary_dilation(bg, iterations=grow + 1) & (a >= thr - 40)
    rgba = np.dstack([np.asarray(img), np.where(bg, 0, 255).astype(np.uint8)])
    return Image.fromarray(rgba, 'RGBA')

def rounded(img, frac=0.18):
    img = img.convert('RGBA')
    m = Image.new('L', (img.width * 4, img.height * 4), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, img.width * 4 - 1, img.height * 4 - 1), int(img.height * 4 * frac), fill=255)
    img.putalpha(m.resize(img.size, Image.LANCZOS))
    return img

def fit_h(img, h, maxw=176):
    s = h / img.height
    w = max(8, round(img.width * s))
    if w > maxw:
        w = maxw
        h = max(8, round(img.height * maxw / img.width))
    return img.resize((w, h), Image.LANCZOS)

def crop(k, box, scale=None):
    im = load(k)
    if scale is None:
        scale = im.width / 2000.0
    return im.crop(tuple(int(v * scale) for v in box))

# ---- the cut-out logos: (source, box) ; boxes of the PAL sheet are in 2000-wide coordinates
PAL = {
    'VMU': ('palsheet', (85, 110, 540, 320)), 'STICK': ('palsheet', (682, 105, 1250, 314)), 'VIB': ('palsheet', (85, 415, 525, 622)),
    'ONLINE_NET': ('palsheet', (765, 418, 1238, 622)), 'GUN': ('palsheet', (85, 720, 655, 930)), 'VGA': ('palsheet', (764, 728, 1232, 934)),
    'WHEEL': ('palsheet', (85, 1026, 678, 1236)), 'KEYBOARD': ('palsheet', (764, 1004, 1240, 1244)),
    'P1': ('palsheet', (1348, 128, 1880, 302)), 'P2': ('palsheet', (1348, 368, 1880, 542)), 'P12': ('palsheet', (1348, 608, 1880, 782)),
    'P14': ('palsheet', (1348, 848, 1880, 1022)), 'P16': ('palsheet', (1348, 1088, 1880, 1262)), 'ONLINE': ('palsheet', (1348, 1328, 1880, 1502)),
}
LOW = set()   # logos that come from a poor scan
BLANK = {}    # US/JP badges with a number written by the menu: key -> (x, y) of the number in the picture

def build_pal():
    out = {}
    for k, (src, box) in PAL.items():
        if k == 'ONLINE_NET':
            continue
        im = crop(src, box)
        out[k] = cutout(im) if k.startswith('P') or k == 'ONLINE' else cutout_tile(im)   # everything outside the outline is transparent, the inside stays white
    # Fishing Controller: no usable PAL scan yet (the crop of the Bass Fishing back caught the CRI ADX logo): a placeholder
    return out

def build_us():
    out = {}
    bass = load('usbass').crop((10, 1140, 780, 1215))
    nfl = load('usnfl').crop((230, 1500, 1070, 1595))
    afo = load('afo')
    up = lambda im, f=4: im.resize((im.width * f, im.height * f), Image.LANCZOS)
    out['P1'] = rounded(up(bass.crop((10, 8, 152, 68))))
    out['FISHING'] = rounded(up(bass.crop((168, 8, 336, 68))))
    out['VIB'] = rounded(up(bass.crop((570, 8, 750, 68))))
    out['P14'] = rounded(up(nfl.crop((22, 8, 166, 77)), 2))
    vmu = up(nfl.crop((388, 8, 620, 77)), 2)
    w, h = vmu.size
    ImageDraw.Draw(vmu).rectangle((int(w * 0.40), int(h * 0.50), int(w * 0.93), int(h * 0.88)), fill=(255, 255, 255))   # the count is written by the menu
    out['VMU'] = rounded(vmu)
    BLANK[('US', 'VMU')] = (0.42, 0.50)
    # the small logos of the US Alien Front Online back (a 256 px wide picture: low quality)
    for key, box in {'ONLINE': (31, 192, 61, 203), 'MIC': (91, 192, 121, 203), 'VGA': (8, 205, 28, 216), 'MODEM': (58, 205, 81, 216)}.items():
        out[key] = rounded(up(afo.crop(box), 10), 0.2)
        LOW.add(('US', key))
    return out

def font(path, size, idx=0):
    return ImageFont.truetype(path, size, index=idx) if path.endswith('.ttc') else ImageFont.truetype(path, size)

def placeholder(region, key, text, h):
    fs = int(h * 0.42)
    d0 = ImageDraw.Draw(Image.new('L', (4, 4)))
    pad = int(h * 0.3) + (int(h * 0.9) if region == 'US' else 0)
    while True:
        f = font(FONT, fs)
        wtxt = int(d0.textlength(text, font=f))
        w = wtxt + pad * 2 - (int(h * 0.4) if region == 'US' else 0)
        if w <= 176 or fs <= 8:
            break
        fs -= 1
    w = min(w, 176)
    im = Image.new('RGBA', (w * 4, h * 4), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    r = int(h * 4 * 0.22)
    d.rounded_rectangle((0, 0, w * 4 - 1, h * 4 - 1), r, fill=(0, 0, 0, 255))
    d.rounded_rectangle((8, 8, w * 4 - 9, h * 4 - 9), max(2, r - 6), fill=(255, 255, 255, 255))
    tx = (w * 4 - wtxt * 4) // 2
    if region == 'US':
        d.polygon([(8, 8), (int(h * 4 * 0.95), h * 2), (8, h * 4 - 9)], fill=(214, 40, 40, 255))
        tx = int(h * 4 * 1.0) + (w * 4 - int(h * 4 * 1.0) - wtxt * 4) // 2
    f4 = font(FONT, fs * 4)
    d.text((max(tx, 16), h * 2), text, font=f4, fill=(0, 0, 0, 255), anchor='lm')
    return im.resize((w, h), Image.LANCZOS)

def jp_text(text, h=H_JP, gap=0):
    """A Japanese white box; `gap` leaves room (px) in the middle for a number the menu writes: text is split at '|'."""
    fs = int(h * 0.58)
    parts = text.split('|')
    d0 = ImageDraw.Draw(Image.new('L', (4, 4)))
    while True:
        f = font(FONT, fs)
        widths = [int(d0.textlength(p, font=f)) for p in parts]
        w = sum(widths) + gap * (len(parts) - 1) + 20
        if w <= 176 or fs <= 8:
            break
        fs -= 1
    w = min(176, w)
    im = Image.new('RGBA', (w * 4, h * 4), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w * 4 - 1, h * 4 - 1), 28, fill=(0, 0, 0, 255))
    d.rounded_rectangle((7, 7, w * 4 - 8, h * 4 - 8), 22, fill=(255, 255, 255, 255))
    f4 = font(FONT, fs * 4)
    x = (w * 4 - (sum(widths) + gap * (len(parts) - 1)) * 4) // 2
    numx = None
    for i, p in enumerate(parts):
        d.text((x, h * 2), p, font=f4, fill=(0, 0, 0, 255), anchor='lm')
        x += widths[i] * 4
        if i < len(parts) - 1:
            numx = x / 4.0
            x += gap * 4
    return im.resize((w, h), Image.LANCZOS), numx

def build_jp(english=False):
    """The Japanese logos (white boxes with black text); english: the same boxes with their text translated (a setting of the menu)."""
    out, nums = {}, {}
    if english:
        T = {'P1': '1 PLAYER', 'P2': '2 PLAYER', 'P12': '1-2 PLAYER', 'P14': '1-4 PLAYER', 'P16': '1-6 PLAYER', 'VIB': 'Puru Puru Pack',
             'VGA': 'VGA compatible', 'MIC': 'Microphone device', 'FISHING': 'Fishing controller', 'ONLINE': 'Internet compatible',
             'STICK': 'Arcade Stick', 'GUN': 'Dreamcast Gun', 'WHEEL': 'Racing Controller', 'KEYBOARD': 'Keyboard', 'MOUSE': 'Mouse',
             'MARACAS': 'Maracas Controller', 'DREAMEYE': 'Dreameye', 'MODEM': 'Modem', 'BBA': 'Broadband Adapter', 'W5500': 'W5500',
             'SERIAL': 'Serial Link'}
        vmu, disc = ('Memory backup||blocks', 28), ('Discs:||', 10)
    else:
        T = {'P1': '1 PLAYER', 'P2': '2 PLAYER', 'P12': '1-2 PLAYER', 'P14': '1-4 PLAYER', 'P16': '1-6 PLAYER', 'VIB': 'ぶるぶるぱっく対応',
             'VGA': 'VGA対応', 'MIC': 'マイクデバイス対応', 'FISHING': 'つりコントローラ対応',
             # no scans of these: the wording of the Japanese boxes ("... 対応": supports ...), in the same style
             'ONLINE': 'インターネット対応', 'STICK': 'アーケードスティック対応', 'GUN': 'ドリームキャスト・ガン対応',
             'WHEEL': 'レーシングコントローラ対応', 'KEYBOARD': 'キーボード対応', 'MOUSE': 'マウス対応', 'MARACAS': 'マラカスコントローラ対応',
             'DREAMEYE': 'ドリームアイ対応', 'MODEM': 'モデム対応', 'BBA': 'ブロードバンドアダプタ対応', 'W5500': 'W5500対応',
             'SERIAL': 'シリアル通信対応'}
        vmu, disc = ('メモリーバックアップ||ブロック', 28), ('ディスク||枚', 10)
    for k, t in T.items():
        out[k], _ = jp_text(t)
    out['VMU'], nx = jp_text(vmu[0], gap=vmu[1])
    nums['VMU'] = nx
    out['DISC'], nx = jp_text(disc[0], gap=disc[1])
    nums['DISC'] = nx
    return out, nums

def argb4444(im):
    a = np.asarray(im.convert('RGBA')).astype(np.int32)
    q = lambda v: np.minimum(15, (v + 8) >> 4)
    # transparent pixels: colour 0 (avoids fringes)
    px = (q(a[..., 3]) << 12) | (q(a[..., 0]) << 8) | (q(a[..., 1]) << 4) | q(a[..., 2])
    px[a[..., 3] < 8] = 0
    return px.astype(np.uint16)

def write_pill_mask(path):
    """The shape of the 1-4 Players banner, turned to read upwards, as the coverage of the PAL case's hinge-bar pill (30 x 212)."""
    im = build_pal()['P14']
    a = im.split()[3]
    a = a.crop(a.getbbox()).rotate(90, expand=True).resize((30, 212), Image.LANCZOS)
    v = np.asarray(a).flatten()
    with open(path, 'w') as f:
        f.write('/* Generated by tools/gen_badges.py: the banner shape of the PAL case\'s hinge-bar pill, coverage 0..255, 30 columns x 212 rows. */\n')
        f.write('#define PILL_MASK_W 30\n#define PILL_MASK_H 212\nstatic const unsigned char pill_mask[PILL_MASK_W * PILL_MASK_H] = {%s};\n' % ','.join(str(int(x)) for x in v))

def main():
    out_h, prev = sys.argv[1], sys.argv[2]
    regions = {}
    pal = build_pal()
    us = build_us()
    jp, jpnums = build_jp()
    jpen, jpennums = build_jp(english=True)
    sets = {'PAL': (pal, H_LOGO), 'US': (us, H_LOGO), 'JP': (jp, H_JP), 'JPEN': (jpen, H_JP)}
    numsets = {'JP': jpnums, 'JPEN': jpennums}
    badges = {}
    status = {}
    for rn, (have, h) in sets.items():
        for k in KEYS:
            if k == 'DISC' and rn not in ('JP', 'JPEN'):
                continue
            if k in have:
                im = fit_h(have[k], (H_PILL if (rn == 'PAL' and (k.startswith('P') and k != 'PAL' or k == 'ONLINE') and k in ('P1','P2','P12','P14','P16','ONLINE')) else h) if rn not in ('JP', 'JPEN') else have[k].height)
                st = 'low' if (rn, k) in LOW else 'ok'
            elif rn == 'PAL' and k == 'ONLINE':
                continue
            else:
                nm = NAMES[k]
                if rn in ('JP', 'JPEN'):
                    im, _ = jp_text(nm)
                else:
                    im = placeholder(rn, k, nm, h)
                st = 'placeholder'
            badges[(rn, k)] = im
            status[(rn, k)] = st
    # the US 2/1-2/1-6 players and PAL online are drawn from the nearest we have: placeholders were made above
    sheet_w, y = 760, 0
    sheet = Image.new('RGB', (sheet_w * 4, 24 * 52 + 20), (70, 78, 140))
    for ci, rn in enumerate(['PAL', 'US', 'JP', 'JPEN']):
        y = 4
        for k in KEYS:
            if (rn, k) not in badges:
                continue
            im = badges[(rn, k)]
            sheet.paste(im, (ci * sheet_w + 4, y), im)
            ImageDraw.Draw(sheet).text((ci * sheet_w + 300, y + 10), '%s %s %s' % (rn, k, status[(rn, k)]), fill=(255, 255, 255))
            y += max(im.height, 24) + 4
    sheet.save(prev)
    with open(out_h, 'w') as f:
        f.write('/* Generated by tools/gen_badges.py: the feature logos of the three regions as ARGB4444 pictures (see openMenu/FEATURE_LOGOS.md). */\n')
        f.write('#ifndef FEATURE_BADGES_H\n#define FEATURE_BADGES_H\n')
        f.write('enum { FB_P1, FB_P2, FB_P12, FB_P14, FB_P16, FB_ONLINE, FB_VMU, FB_VIB, FB_STICK, FB_GUN, FB_WHEEL, FB_KEYBOARD, FB_VGA, FB_FISHING, FB_MIC, FB_MOUSE,\n       FB_MARACAS, FB_DREAMEYE, FB_MODEM, FB_BBA, FB_W5500, FB_SERIAL, FB_DISC, FB_COUNT };\n')
        f.write('enum { FB_REGION_PAL, FB_REGION_US, FB_REGION_JP, FB_REGION_JPEN, FB_REGIONS }; /* JPEN: the Japanese logos in English */\n')
        f.write('typedef struct { unsigned short w, h; short num_x, num_y; const unsigned short* px; } feature_badge;\n')
        for ri, rn in enumerate(['PAL', 'US', 'JP', 'JPEN']):
            for k in KEYS:
                if (rn, k) not in badges:
                    continue
                im = badges[(rn, k)]
                px = argb4444(im)
                f.write('static const unsigned short fb_%s_%s[] = {%s};\n' % (rn, k, ','.join('0x%04X' % v for v in px.flatten())))
        f.write('static const feature_badge feature_badges[FB_REGIONS][FB_COUNT] = {\n')
        for rn in ['PAL', 'US', 'JP', 'JPEN']:
            f.write('  {\n')
            for k in KEYS:
                if (rn, k) not in badges:
                    f.write('    {0, 0, -1, -1, 0},\n')
                    continue
                im = badges[(rn, k)]
                nx, ny = -1, -1
                if rn in numsets and k in numsets[rn]:
                    nx, ny = int(numsets[rn][k]), int((im.height - 14) / 2)
                if (rn, k) in BLANK:
                    fx, fy = BLANK[(rn, k)]
                    nx, ny = int(fx * im.width), int(fy * im.height)
                f.write('    {%d, %d, %d, %d, fb_%s_%s},\n' % (im.width, im.height, nx, ny, rn, k))
            f.write('  },\n')
        f.write('};\n')
        # the strip's icons: the pictogram (the device drawing) at the left of the PAL tiles, 34 px tall (the logos' height on the Y screen), the same for every region
        FR = {'VMU': 0.27, 'VIB': 0.24, 'STICK': 0.45, 'GUN': 0.40, 'WHEEL': 0.41, 'KEYBOARD': 0.40, 'VGA': 0.38}
        pal_tiles = build_pal()
        f.write('typedef struct { unsigned short w, h; const unsigned short* px; } feature_icon;\n')
        icons = {}
        for k, fr in FR.items():
            t = pal_tiles[k]
            t = t.crop((0, 0, int(t.width * fr), t.height))
            bb = t.split()[3].getbbox()
            t = t.crop(bb)
            icons[k] = t.resize((max(8, round(t.width * 34 / t.height)), 34), Image.LANCZOS)
            f.write('static const unsigned short fi_%s[] = {%s};\n' % (k, ','.join('0x%04X' % v for v in argb4444(icons[k]).flatten())))
        f.write('static const feature_icon feature_icons[FB_COUNT] = {\n')
        for k in KEYS:
            if k in icons:
                f.write('    {%d, %d, fi_%s},\n' % (icons[k].width, icons[k].height, k))
            else:
                f.write('    {0, 0, 0},\n')
        f.write('};\n#endif\n')
    with open(out_h + '.status', 'w') as f:
        for (rn, k), st in sorted(status.items()):
            f.write('%s %s %s\n' % (rn, k, st))

main()
