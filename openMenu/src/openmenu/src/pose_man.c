/* pose_man: see pose_man.h. */
#include <math.h>
#include <string.h>

#include "pose_man.h"

#define EDGE 0xFFFFFFFFu
#define BLACK 0xFF0A0A10u
#define DARK 0xFF1B1B27u
#define GREY 0xFF30303Fu

typedef struct ctx {
    pm_poly* out;
    int n, max;
} ctx;

static void
add(ctx* c, const float* x, const float* y, int n, uint32_t argb) {
    if (c->n >= c->max || n < 3 || n > PM_MAX_PTS) {
        return;
    }
    pm_poly* p = &c->out[c->n++];
    p->n = n;
    p->argb = argb;
    for (int i = 0; i < n; i++) {
        p->x[i] = x[i];
        p->y[i] = y[i];
    }
}

/* a tapered tube from (ax, ay) to (bx, by), half widths w0 and w1: white edge first (wider), then two facets */
static void
tube(ctx* c, float ax, float ay, float bx, float by, float w0, float w1, uint32_t lit, uint32_t shade, float edge) {
    float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy);
    float nx, ny;
    if (l < 1e-3f) {
        return;
    }
    nx = -dy / l;
    ny = dx / l;
    {
        float e0 = w0 + edge, e1 = w1 + edge;
        float x[4] = {ax + nx * e0, bx + nx * e1, bx - nx * e1, ax - nx * e0};
        float y[4] = {ay + ny * e0, by + ny * e1, by - ny * e1, ay - ny * e0};
        add(c, x, y, 4, EDGE);
    }
    {
        float x[4] = {ax + nx * w0, bx + nx * w1, bx, ax};
        float y[4] = {ay + ny * w0, by + ny * w1, by, ay};
        add(c, x, y, 4, lit);
    }
    {
        float x[4] = {ax, bx, bx - nx * w1, ax - nx * w0};
        float y[4] = {ay, by, by - ny * w1, ay - ny * w0};
        add(c, x, y, 4, shade);
    }
}

/* a regular polygon of n points (a facet fan is made of triangles: the centre and two neighbours) */
static void
fan(ctx* c, float cx, float cy, float r, int n, float rot_deg, const uint32_t* cols, int ncols, float edge) {
    float x[PM_MAX_PTS], y[PM_MAX_PTS];
    if (n > PM_MAX_PTS) {
        n = PM_MAX_PTS;
    }
    for (int i = 0; i < n; i++) {
        float a = (rot_deg + 360.0f * (float)i / (float)n) * 0.0174532925f;
        x[i] = cx + (r + edge) * cosf(a);
        y[i] = cy + (r + edge) * sinf(a);
    }
    if (edge > 0.0f) {
        add(c, x, y, n, EDGE);
    }
    for (int i = 0; i < n; i++) {
        float a0 = (rot_deg + 360.0f * (float)i / (float)n) * 0.0174532925f, a1 = (rot_deg + 360.0f * (float)(i + 1) / (float)n) * 0.0174532925f;
        float tx[3] = {cx, cx + r * cosf(a0), cx + r * cosf(a1)};
        float ty[3] = {cy, cy + r * sinf(a0), cy + r * sinf(a1)};
        add(c, tx, ty, 3, cols[i % ncols]);
    }
}

static void
maraca(ctx* c, float x, float y, float r) {
    static const uint32_t glow[1] = {0x302F9FE8u};
    static const uint32_t ball[6] = {0xFF9FE3FFu, 0xFF5FB8F0u, 0xFF2F86D8u, 0xFF1D5FB8u, 0xFF3F9FE8u, 0xFF7FD2FBu};
    fan(c, x, y, r * 2.0f, 8, 0.0f, glow, 1, 0.0f); /* the glow: a soft octagon, translucent */
    fan(c, x, y, r, 6, 15.0f, ball, 6, 1.6f);
}

int
pose_man_build(float cx, float base_y, float s, float left_deg, float right_deg, pm_poly* out, int max) {
    ctx c = {out, 0, max};
    const float k = s; /* the drawing below is for a man 200 pixels tall */
    const float edge = 1.8f * (k < 0.6f ? 0.6f : k);

    /* legs and feet */
    tube(&c, cx - 7 * k, base_y - 92 * k, cx - 11 * k, base_y - 6 * k, 3.6f * k, 3.0f * k, GREY, BLACK, edge);
    tube(&c, cx + 7 * k, base_y - 92 * k, cx + 11 * k, base_y - 6 * k, 3.6f * k, 3.0f * k, DARK, BLACK, edge);
    {
        float lx[4] = {cx - 15 * k, cx - 7 * k, cx - 3 * k, cx - 26 * k}, ly[4] = {base_y - 8 * k, base_y - 8 * k, base_y, base_y};
        float rx[4] = {cx + 15 * k, cx + 7 * k, cx + 3 * k, cx + 26 * k};
        add(&c, lx, ly, 4, EDGE);
        add(&c, rx, ly, 4, EDGE);
        for (int i = 0; i < 4; i++) {
            lx[i] += (i < 2 ? 0.0f : 0.0f);
        }
        {
            float ix[4] = {cx - 13.5f * k, cx - 8.5f * k, cx - 5.5f * k, cx - 23.5f * k}, iy[4] = {base_y - 6.6f * k, base_y - 6.6f * k, base_y - 1.4f * k, base_y - 1.4f * k};
            float jx[4] = {cx + 13.5f * k, cx + 8.5f * k, cx + 5.5f * k, cx + 23.5f * k};
            add(&c, ix, iy, 4, GREY);
            add(&c, jx, iy, 4, BLACK);
        }
    }
    /* body: a tapered torso, two facets, with a thin neck */
    {
        float x[4] = {cx - 21 * k, cx + 21 * k, cx + 11 * k, cx - 11 * k}, y[4] = {base_y - 160 * k, base_y - 160 * k, base_y - 88 * k, base_y - 88 * k};
        float ex[4], ey[4];
        for (int i = 0; i < 4; i++) {
            ex[i] = cx + (x[i] - cx) * (1.0f + edge / (22.0f * k)) ;
            ey[i] = y[i] + (i < 2 ? -edge : edge);
        }
        add(&c, ex, ey, 4, EDGE);
        {
            float lx[4] = {x[0], cx, cx, x[3]}, ly[4] = {y[0], y[0], y[3], y[3]};
            float rx[4] = {cx, x[1], x[2], cx};
            add(&c, lx, ly, 4, GREY);
            add(&c, rx, ly, 4, BLACK);
        }
    }
    tube(&c, cx, base_y - 160 * k, cx, base_y - 172 * k, 3.2f * k, 3.0f * k, DARK, BLACK, edge);
    /* the head: an octagon of facets */
    {
        static const uint32_t cols[8] = {GREY, DARK, BLACK, DARK, BLACK, DARK, GREY, DARK};
        fan(&c, cx, base_y - 184 * k, 13.0f * k, 8, 22.5f, cols, 8, edge);
    }
    /* the arms: from the shoulders (+-21, 156), 78 long, the Joy-Con's elevation; the maraca at the hand */
    for (int side = -1; side <= 1; side += 2) {
        float deg = side < 0 ? left_deg : right_deg, t = deg * 0.0174532925f;
        float sx = cx + (float)side * 21 * k, sy = base_y - 156 * k, L = 76 * k;
        float ex = sx + (float)side * cosf(t) * L, ey = sy - sinf(t) * L;
        float mx = (sx + ex) * 0.5f, my = (sy + ey) * 0.5f;
        tube(&c, sx, sy, mx, my, 3.4f * k, 2.9f * k, side < 0 ? GREY : DARK, BLACK, edge);
        tube(&c, mx, my, ex, ey, 2.9f * k, 2.4f * k, DARK, BLACK, edge);
        fan(&c, mx, my, 3.8f * k, 6, 0.0f, (const uint32_t[]){GREY, BLACK}, 2, edge * 0.7f); /* the elbow */
        maraca(&c, ex + (float)side * cosf(t) * 13.0f * k, ey - sinf(t) * 13.0f * k, 11.0f * k);
    }
    return c.n;
}

int
pose_man_guides(float cx, float base_y, float s, float left_deg, float right_deg, pm_poly* out, int max) {
    ctx c = {out, 0, max};
    for (int side = -1; side <= 1; side += 2) {
        float sx = cx + (float)side * 21 * s, sy = base_y - 156 * s, R = 108.0f * s;
        float deg = side < 0 ? left_deg : right_deg;
        for (int a = -90; a <= 90; a += 45) {
            float t = (float)a * 0.0174532925f, dx = (float)side * cosf(t), dy = -sinf(t);
            float x0 = sx + dx * (R - 5), y0 = sy + dy * (R - 5), x1 = sx + dx * (R + 5), y1 = sy + dy * (R + 5);
            float nx = -dy * (a == 0 ? 1.4f : 0.8f), ny = dx * (a == 0 ? 1.4f : 0.8f);
            float xs[4] = {x0 + nx, x1 + nx, x1 - nx, x0 - nx}, ys[4] = {y0 + ny, y1 + ny, y1 - ny, y0 - ny};
            add(&c, xs, ys, 4, a == 0 ? 0xC0FFFFFFu : 0x60FFFFFFu);
        }
        {
            float t = deg * 0.0174532925f, px = sx + (float)side * cosf(t) * R, py = sy - sinf(t) * R;
            static const uint32_t gold[1] = {0xFFFFE58Au};
            fan(&c, px, py, 4.5f, 6, 0.0f, gold, 1, 0.0f);
        }
    }
    return c.n;
}
