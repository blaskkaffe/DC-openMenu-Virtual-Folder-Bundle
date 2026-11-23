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
    d = u(d, fmaxf(fabsf(y - 0.3f) - T * 0.5f, disc)); /* a parallel on each side of the equator, evenly spaced */
    d = u(d, fmaxf(fabsf(y - 0.7f) - T * 0.5f, disc));
    return d;
}

/* A polygon (points in the picture of the BIOS controller, see CT_X / CT_Y): signed distance */
static float
sd_poly(float x, float y, const float* pt, int n) {
    float d = 1e9f;
    int inside = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float ax = pt[j * 2], ay = pt[j * 2 + 1], bx = pt[i * 2], by = pt[i * 2 + 1];
        float e = sd_seg(x, y, ax, ay, bx, by, 0);
        d = e < d ? e : d;
        if (((ay > y) != (by > y)) && (x < (bx - ax) * (y - ay) / (by - ay) + ax)) {
            inside = !inside;
        }
    }
    return inside ? -d : d;
}

/* The controller of the main menu's Play icon, traced from its model seen from the front: x 28..275, y 28..245 in the trace, fitted into the square */
#define CT_X(v) (0.04f + ((v) - 28.0f) * 0.003725f)
#define CT_Y(v) (0.095f + ((v) - 28.0f) * 0.003725f)
#define CT_R(v) ((v) * 0.003725f)

static float
sd_pad(float x, float y) {
    static const float body[] = {110, 30, 150, 26, 192, 30, 196, 46, 215, 51, 245, 55, 262, 68, 271, 95, 276, 135, 275, 175, 270, 205, 258, 232, 247, 243, 236, 238,
                                 226, 215, 222, 206, 190, 212, 150, 215, 110, 212, 78, 206, 72, 228, 66, 243, 54, 238, 40, 214, 31, 185, 28, 150, 31, 115,
                                 40, 82, 55, 64, 76, 57, 96, 51, 108, 47};
    static const float slot[] = {109, 40, 194, 40, 194, 105, 188, 125, 170, 138, 150, 141, 130, 138, 112, 125, 107, 105};
    float p[sizeof(body) / sizeof(body[0])], q[sizeof(slot) / sizeof(slot[0])];
    for (unsigned i = 0; i < sizeof(body) / sizeof(body[0]); i += 2) {
        p[i] = CT_X(body[i]);
        p[i + 1] = CT_Y(body[i + 1]);
    }
    for (unsigned i = 0; i < sizeof(slot) / sizeof(slot[0]); i += 2) {
        q[i] = CT_X(slot[i]);
        q[i + 1] = CT_Y(slot[i + 1]);
    }
    float d = sd_poly(x, y, p, (int)(sizeof(body) / sizeof(body[0]) / 2));
    float h = sd_poly(x, y, q, (int)(sizeof(slot) / sizeof(slot[0]) / 2)); /* the slot for the memory card */
    h = u(h, sd_box(x, y, CT_X(74), CT_Y(147), CT_R(24), CT_R(10), 0.0f)); /* d-pad */
    h = u(h, sd_box(x, y, CT_X(74), CT_Y(147), CT_R(8), CT_R(23), 0.0f));
    h = u(h, sd_circle(x, y, CT_X(73), CT_Y(92), CT_R(17))); /* the stick */
    h = u(h, sd_circle(x, y, CT_X(230), CT_Y(93), CT_R(11))); /* the four buttons */
    h = u(h, sd_circle(x, y, CT_X(211), CT_Y(112), CT_R(11)));
    h = u(h, sd_circle(x, y, CT_X(250), CT_Y(112), CT_R(11)));
    h = u(h, sd_circle(x, y, CT_X(231), CT_Y(127), CT_R(11)));
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
