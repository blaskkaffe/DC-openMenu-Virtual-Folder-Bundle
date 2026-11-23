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
sd_poly(float x, float y, const float* v, int n) { /* v: x,y pairs; signed distance to a closed polygon */
    float d = (x - v[0]) * (x - v[0]) + (y - v[1]) * (y - v[1]);
    float sgn = 1.0f;
    for (int i = 0, j = n - 1; i < n; j = i, i++) {
        float ex = v[j * 2] - v[i * 2], ey = v[j * 2 + 1] - v[i * 2 + 1];
        float wx = x - v[i * 2], wy = y - v[i * 2 + 1];
        float h = (wx * ex + wy * ey) / (ex * ex + ey * ey);
        h = h < 0 ? 0 : (h > 1 ? 1 : h);
        float bx = wx - ex * h, by = wy - ey * h;
        float dd = bx * bx + by * by;
        d = dd < d ? dd : d;
        int c1 = y >= v[i * 2 + 1], c2 = y < v[j * 2 + 1], c3 = ex * wy > ey * wx;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) {
            sgn = -sgn;
        }
    }
    return sgn * sqrtf(d);
}

static void
rot(float* x, float* y, float deg) { /* turn the picture about its middle: the glyphs sit a little crooked, like the main menu's */
    float a = deg * 0.017453293f, c = cosf(a), s = sinf(a), dx = *x - 0.5f, dy = *y - 0.5f;
    *x = 0.5f + dx * c + dy * s;
    *y = 0.5f - dx * s + dy * c;
}

static float
sd_globe(float x, float y) {
    const float R = 0.43f, T = 0.075f;
    float disc = sd_circle(x, y, 0.5f, 0.5f, R - T * 0.25f);
    float ex = (x - 0.5f) / 0.2f, ey = (y - 0.5f) / R;
    float meridian = ring((sqrtf(ex * ex + ey * ey) - 1.0f) * 0.2f, T);
    float d = ring(sd_circle(x, y, 0.5f, 0.5f, R), T);
    d = u(d, fmaxf(meridian, disc));
    d = u(d, fmaxf(fabsf(x - 0.5f) - T * 0.5f, disc));
    for (int i = -1; i <= 1; i++) { /* the equator and one line either side, evenly spaced */
        d = u(d, fmaxf(fabsf(y - (0.5f + 0.2f * (float)i)) - T * 0.5f, disc));
    }
    return d;
}

/* The controller as the main menu's model has it, traced from a render of it: the outline, then the parts that are see-through. Picture
 * coordinates of the render (285,330 is the middle, 535 px the width of the square). */
#define PX(px) (((px) - 285.0f) / 535.0f + 0.5f)
#define PY(py) (((py) - 330.0f) / 535.0f + 0.5f)
#define PL(l) ((l) / 535.0f)

static float
sd_pad(float x, float y) {
    static const float outline[][2] = {
        {70, 185}, {160, 158}, {200, 130}, {285, 122}, {370, 130}, {410, 158}, {480, 175}, {520, 225}, {531, 330}, {520, 430},
        {478, 540}, {440, 536}, {412, 495}, {350, 488}, {285, 486}, {220, 488}, {158, 495}, {130, 536}, {92, 540}, {55, 500},
        {40, 400}, {40, 250}};
    float v[22 * 2];
    for (int i = 0; i < 22; i++) {
        v[i * 2] = PX(outline[i][0]);
        v[i * 2 + 1] = PY(outline[i][1]);
    }
    float d = sd_poly(x, y, v, 22) - 0.022f; /* chunkier, softened corners */
    float h = sd_box(x, y, PX(285), PY(238), PL(70), PL(88), PL(46)); /* the VMU slot */
    h = u(h, sd_circle(x, y, PX(128), PY(245), PL(30))); /* the stick */
    h = u(h, sd_box(x, y, PX(128), PY(355), PL(46), PL(16), PL(4))); /* the d-pad */
    h = u(h, sd_box(x, y, PX(128), PY(355), PL(16), PL(46), PL(4)));
    h = u(h, sd_circle(x, y, PX(443), PY(240), PL(17))); /* the four buttons */
    h = u(h, sd_circle(x, y, PX(400), PY(284), PL(17)));
    h = u(h, sd_circle(x, y, PX(486), PY(284), PL(17)));
    h = u(h, sd_circle(x, y, PX(443), PY(322), PL(17)));
    h = u(h, sd_tri(x, y, PX(281), PY(392), PX(256), PY(436), PX(306), PY(436))); /* start */
    return cut(d, h);
}

/* The alarm clock of the main menu's Settings icon, traced from a render of it: the body with its face cut out and the two hands left in,
 * the two bells on top and the knob at the side. Picture coordinates of the render (292,330 is the middle, 620 px the width of the square). */
#define CX(px) (((px) - 292.0f) / 620.0f + 0.5f)
#define CY(py) (((py) - 330.0f) / 620.0f + 0.5f)
#define CL(l) ((l) / 620.0f)

static float
poly_img(float x, float y, const float (*pts)[2], int n) {
    float v[16 * 2];
    for (int i = 0; i < n; i++) {
        v[i * 2] = CX(pts[i][0]);
        v[i * 2 + 1] = CY(pts[i][1]);
    }
    return sd_poly(x, y, v, n);
}

static float
sd_clock(float x, float y) {
    static const float bell_l[][2] = {{38, 232}, {58, 150}, {130, 84}, {212, 62}, {252, 92}, {242, 128}, {150, 152}, {92, 200}, {58, 238}};
    static const float bell_r[][2] = {{547, 228}, {527, 150}, {455, 84}, {373, 60}, {330, 72}, {336, 112}, {432, 152}, {497, 200}, {532, 234}};
    static const float hand_a[][2] = {{190, 215}, {238, 186}, {338, 308}, {296, 354}}; /* the long hand, up to the left */
    static const float hand_b[][2] = {{318, 298}, {402, 224}, {444, 270}, {346, 338}}; /* the short hand, up to the right */
    float body = sd_circle(x, y, CX(290), CY(355), CL(218)) - 0.012f;
    float d = body;
    d = u(d, poly_img(x, y, bell_l, 9) - 0.016f);
    d = u(d, poly_img(x, y, bell_r, 9) - 0.016f);
    d = u(d, sd_box(x, y, CX(548), CY(345), CL(30), CL(44), CL(8)));
    d = cut(d, sd_circle(x, y, CX(292), CY(345), CL(148))); /* the face */
    d = u(d, poly_img(x, y, hand_a, 4) - 0.02f);
    d = u(d, poly_img(x, y, hand_b, 4) - 0.02f);
    return d;
}

static float
sd_chat(float x, float y) {
    float d = sd_box(x, y, 0.5f, 0.44f, 0.45f, 0.34f, 0.33f); /* an oval, not a box */
    d = u(d, sd_tri(x, y, 0.2f, 0.58f, 0.16f, 0.94f, 0.52f, 0.72f) - 0.025f);
    float h = sd_circle(x, y, 0.3f, 0.44f, 0.075f);
    h = u(h, sd_circle(x, y, 0.5f, 0.44f, 0.075f));
    h = u(h, sd_circle(x, y, 0.7f, 0.44f, 0.075f));
    return cut(d, h);
}

static float
sd_wrench(float x, float y) {
    float dx = x - 0.5f, dy = y - 0.5f;
    float uu = (dx - dy) * 0.70710678f; /* along the wrench, towards the head at the top right */
    float vv = (dx + dy) * 0.70710678f;
    float d = sd_seg(uu, vv, -0.3f, 0.0f, 0.18f, 0.0f, 0.13f); /* the handle, fat */
    d = u(d, sd_circle(uu, vv, -0.33f, 0.0f, 0.17f)); /* a bulb at the end */
    d = u(d, sd_circle(uu, vv, 0.27f, 0.0f, 0.23f)); /* the head */
    float h = sd_box(uu, vv, 0.5f, 0.0f, 0.2f, 0.095f, 0.03f); /* the jaw */
    h = u(h, sd_circle(uu, vv, -0.33f, 0.0f, 0.055f)); /* the hanging hole */
    return cut(d, h);
}

static float
shape(int id, float x, float y) {
    /* a little crooked and wobbly, as the main menu's models are */
    static const float tilt[UICON_COUNT] = {-8.0f, 0.0f, 0.0f, -1.5f, 0.0f};
    rot(&x, &y, tilt[id]);
    x += 0.004f * sinf(9.0f * y + (float)id);
    y += 0.004f * sinf(8.0f * x + (float)id * 2.0f);
    switch (id) {
        case UICON_ONLINE: return sd_globe(x, y);
        case UICON_PLAY: return sd_pad(x, y);
        case UICON_EVENTS: return sd_clock(x, y);
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
