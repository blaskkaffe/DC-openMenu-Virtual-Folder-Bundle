#include "uicons.h"

#include <math.h>

/* Shapes are signed distances in a 0..1 square (negative inside, y down). A glyph is a union of shapes with holes cut out of it. */

static float
sd_circle(float x, float y, float cx, float cy, float r) {
    return sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) - r;
}

static float
sd_box(float x, float y, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(x - cx) - (hw - r);
    float qy = fabsf(y - cy) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float inner = (qx > qy ? qx : qy);
    return sqrtf(ox * ox + oy * oy) + (inner < 0 ? inner : 0) - r;
}

static float
sd_seg(float x, float y, float ax, float ay, float bx, float by, float r) {
    float px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay;
    float h = (px * dx + py * dy) / (dx * dx + dy * dy);
    h = h < 0 ? 0 : (h > 1 ? 1 : h);
    return sqrtf((px - dx * h) * (px - dx * h) + (py - dy * h) * (py - dy * h)) - r;
}

static float
sd_tri(float x, float y, float ax, float ay, float bx, float by, float cx, float cy) {
    float e0x = bx - ax, e0y = by - ay, e1x = cx - bx, e1y = cy - by, e2x = ax - cx, e2y = ay - cy;
    float d0 = sd_seg(x, y, ax, ay, bx, by, 0), d1 = sd_seg(x, y, bx, by, cx, cy, 0), d2 = sd_seg(x, y, cx, cy, ax, ay, 0);
    float d = d0 < d1 ? d0 : d1;
    d = d < d2 ? d : d2;
    float s0 = e0x * (y - ay) - e0y * (x - ax), s1 = e1x * (y - by) - e1y * (x - bx), s2 = e2x * (y - cy) - e2y * (x - cx);
    int inside = (s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0);
    return inside ? -d : d;
}

static float
u(float a, float b) {
    return a < b ? a : b;
}

static float
cut(float a, float hole) { /* a with `hole` taken out */
    return a > -hole ? a : -hole;
}

static float
ring(float d, float t) { /* the outline of a shape, t thick */
    return fabsf(d) - t * 0.5f;
}

static float
sd_globe(float x, float y) {
    const float R = 0.42f, T = 0.055f;
    float disc = sd_circle(x, y, 0.5f, 0.5f, R - T * 0.5f);
    float ex = (x - 0.5f) / 0.19f, ey = (y - 0.5f) / R;
    float meridian = ring((sqrtf(ex * ex + ey * ey) - 1.0f) * 0.19f, T);
    float d = ring(sd_circle(x, y, 0.5f, 0.5f, R), T);
    d = u(d, fmaxf(meridian, disc));
    d = u(d, fmaxf(fabsf(x - 0.5f) - T * 0.5f, disc));
    d = u(d, fmaxf(fabsf(y - 0.5f) - T * 0.5f, disc));
    return d;
}

static float
sd_pad(float x, float y) {
    float d = sd_box(x, y, 0.5f, 0.58f, 0.44f, 0.27f, 0.17f);
    d = u(d, sd_box(x, y, 0.5f, 0.32f, 0.22f, 0.13f, 0.07f)); /* the VMU bump */
    float h = u(sd_box(x, y, 0.25f, 0.58f, 0.115f, 0.035f, 0.0f), sd_box(x, y, 0.25f, 0.58f, 0.035f, 0.115f, 0.0f)); /* d-pad */
    h = u(h, sd_circle(x, y, 0.73f, 0.47f, 0.05f)); /* four face buttons */
    h = u(h, sd_circle(x, y, 0.83f, 0.58f, 0.05f));
    h = u(h, sd_circle(x, y, 0.73f, 0.69f, 0.05f));
    h = u(h, sd_circle(x, y, 0.63f, 0.58f, 0.05f));
    h = u(h, sd_box(x, y, 0.5f, 0.32f, 0.12f, 0.06f, 0.03f)); /* the window */
    return cut(d, h);
}

static float
sd_calendar(float x, float y) {
    float d = sd_box(x, y, 0.5f, 0.56f, 0.4f, 0.34f, 0.07f);
    d = u(d, sd_seg(x, y, 0.3f, 0.14f, 0.3f, 0.26f, 0.045f)); /* the binder rings */
    d = u(d, sd_seg(x, y, 0.7f, 0.14f, 0.7f, 0.26f, 0.045f));
    float h = sd_box(x, y, 0.5f, 0.395f, 0.4f, 0.016f, 0.0f); /* the header's line */
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 3; c++) {
            h = u(h, sd_box(x, y, 0.3f + 0.2f * (float)c, 0.55f + 0.17f * (float)r, 0.065f, 0.055f, 0.012f));
        }
    }
    return cut(d, h);
}

static float
sd_chat(float x, float y) {
    float d = sd_box(x, y, 0.5f, 0.44f, 0.43f, 0.29f, 0.15f);
    d = u(d, sd_tri(x, y, 0.22f, 0.62f, 0.2f, 0.92f, 0.5f, 0.68f));
    float h = sd_circle(x, y, 0.3f, 0.44f, 0.06f);
    h = u(h, sd_circle(x, y, 0.5f, 0.44f, 0.06f));
    h = u(h, sd_circle(x, y, 0.7f, 0.44f, 0.06f));
    return cut(d, h);
}

static float
sd_wrench(float x, float y) {
    float dx = x - 0.5f, dy = y - 0.5f;
    float uu = (dx - dy) * 0.70710678f; /* along the wrench, towards the head at the top right */
    float vv = (dx + dy) * 0.70710678f;
    float d = sd_seg(uu, vv, -0.34f, 0.0f, 0.2f, 0.0f, 0.095f); /* the handle */
    d = u(d, sd_circle(uu, vv, 0.28f, 0.0f, 0.17f)); /* the head */
    float h = sd_box(uu, vv, 0.44f, 0.0f, 0.17f, 0.08f, 0.0f); /* the jaw */
    h = u(h, sd_circle(uu, vv, -0.34f, 0.0f, 0.035f)); /* the hanging hole */
    return cut(d, h);
}

static float
shape(int id, float x, float y) {
    switch (id) {
        case UICON_ONLINE: return sd_globe(x, y);
        case UICON_PLAY: return sd_pad(x, y);
        case UICON_EVENTS: return sd_calendar(x, y);
        case UICON_CHAT: return sd_chat(x, y);
        default: return sd_wrench(x, y);
    }
}

static float
cover(float d) { /* anti-aliased: half a pixel either side of the edge */
    float a = 0.5f - d * (float)UICON_SIZE;
    return a < 0 ? 0 : (a > 1 ? 1 : a);
}

void
uicon_render(int id, uint32_t argb, uint16_t* out) {
    const float S = (float)UICON_SIZE;
    int r = (int)((argb >> 16) & 0xFF) / 17, g = (int)((argb >> 8) & 0xFF) / 17, b = (int)(argb & 0xFF) / 17;

    for (int py = 0; py < UICON_SIZE; py++) {
        for (int px = 0; px < UICON_SIZE; px++) {
            float a = cover(shape(id, ((float)px + 0.5f) / S, ((float)py + 0.5f) / S));
            out[py * UICON_SIZE + px] = (uint16_t)(((int)(a * 15.0f + 0.5f) << 12) | (r << 8) | (g << 4) | b);
        }
    }
}
