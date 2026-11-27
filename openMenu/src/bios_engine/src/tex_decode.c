/*
 * tex_decode: PVR texture payloads to ARGB8888, see tex_decode.h.
 */
#include "tex_decode.h"

/* the bits of v (16 of them) spread to the even bits */
static inline uint32_t
spread(uint32_t v) {
    v &= 0xFFFFu;
    v = (v | (v << 8)) & 0x00FF00FFu;
    v = (v | (v << 4)) & 0x0F0F0F0Fu;
    v = (v | (v << 2)) & 0x33333333u;
    v = (v | (v << 1)) & 0x55555555u;
    return v;
}

uint32_t
bios_untwiddle(uint32_t x, uint32_t y) {
    return spread(y) | (spread(x) << 1); /* y's bits in the even places, x's in the odd ones */
}

/* 5 and 6 bit channels as 0..255 (v * 255 / 31 or 63, rounded down): tables, as the SH4 has no divide instruction */
static const uint8_t lut5[32] = {0, 8, 16, 24, 32, 41, 49, 57, 65, 74, 82, 90, 98, 106, 115, 123, 131, 139, 148, 156, 164, 172, 180, 189, 197, 205, 213, 222, 230, 238, 246, 255};
static const uint8_t lut6[64] = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 85, 89, 93, 97, 101, 105, 109, 113, 117, 121, 125, 129, 133, 137, 141, 145, 149, 153, 157, 161, 165, 170, 174, 178, 182, 186, 190, 194, 198, 202, 206, 210, 214, 218, 222, 226, 230, 234, 238, 242, 246, 250, 255};

static uint32_t
convert(uint16_t v, int fmt) {
    uint32_t a, r, g, b;
    switch (fmt) {
        case BIOS_PVR_ARGB1555:
            a = (v & 0x8000) ? 255 : 0;
            r = lut5[(v >> 10) & 31];
            g = lut5[(v >> 5) & 31];
            b = lut5[v & 31];
            break;
        case BIOS_PVR_RGB565:
            a = 255;
            r = lut5[(v >> 11) & 31];
            g = lut6[(v >> 5) & 63];
            b = lut5[v & 31];
            break;
        default: /* ARGB4444 */
            a = ((v >> 12) & 15) * 17;
            r = ((v >> 8) & 15) * 17;
            g = ((v >> 4) & 15) * 17;
            b = (v & 15) * 17;
            break;
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static uint16_t
px16(const uint8_t* p, size_t i) {
    return (uint16_t)(p[i * 2] | (p[i * 2 + 1] << 8));
}

int
bios_texture_decode(const bios_texture* t, uint32_t* argb) {
    return t ? bios_texture_decode_rows(t, argb, 0, t->height) : -1;
}

int
bios_texture_decode_rows(const bios_texture* t, uint32_t* argb, uint32_t y0, uint32_t y1) {
    if (!t || !argb || !t->data || t->data_size == 0) {
        return -1;
    }
    uint32_t w = t->width, h = t->height;
    y1 = y1 > h ? h : y1;
    y0 = y0 > y1 ? y1 : y0;
    switch (t->data_type) {
        case BIOS_PVR_TWIDDLED:
            for (uint32_t y = y0; y < y1; y++) {
                const uint32_t ty = spread(y);
                for (uint32_t x = 0; x < w; x++) {
                    argb[y * w + x] = convert(px16(t->data, ty | (spread(x) << 1)), t->pixel_format);
                }
            }
            return 0;
        case BIOS_PVR_RECTANGLE:
            for (uint32_t i = y0 * w; i < y1 * w; i++) {
                argb[i] = convert(px16(t->data, i), t->pixel_format);
            }
            return 0;
        case BIOS_PVR_VQ: {
            const uint8_t* index = t->data + 2048;
            for (uint32_t y = y0 / 2; y < (y1 + 1) / 2; y++) {
                const uint32_t ty = spread(y);
                for (uint32_t x = 0; x < w / 2; x++) {
                    uint32_t e = index[ty | (spread(x) << 1)];
                    for (uint32_t k = 0; k < 4; k++) {
                        argb[(2 * y + (k & 1)) * w + 2 * x + (k >> 1)] = convert(px16(t->data, e * 4 + k), t->pixel_format);
                    }
                }
            }
            return 0;
        }
        default: return -1;
    }
}
