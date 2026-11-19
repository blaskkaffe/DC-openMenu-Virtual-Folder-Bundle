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

/* the glossy blue-black of the pose man: body, a blue facet, the rim light, and the cream edge of the originals */
#define BODY 0xFF0B1118u
#define FACET 0xFF182632u
#define RIM 0xFF23394Du
#define CREAM 0xFFF3F0D6u
#define SPEC 0xE8FFFFFFu

/* an ellipse (rx, ry, rotated by rot_deg) as a cream edge and a fan of facets: dark all round, the rim light on the lower right, and a
 * small white sliver (the shine) near the upper left */
static void
blob(ctx* c, float cx, float cy, float rx, float ry, float rot_deg, float edge, int shine) {
    enum { N = 8 };
    float ca = cosf(rot_deg * 0.0174532925f), sa = sinf(rot_deg * 0.0174532925f);
    float px[N], py[N], ex[N], ey[N];
    for (int i = 0; i < N; i++) {
        float a = (22.5f + 45.0f * (float)i) * 0.0174532925f;
        float ux = cosf(a), uy = sinf(a);
        px[i] = cx + (ux * rx) * ca - (uy * ry) * sa;
        py[i] = cy + (ux * rx) * sa + (uy * ry) * ca;
        ex[i] = cx + (ux * (rx + edge)) * ca - (uy * (ry + edge)) * sa;
        ey[i] = cy + (ux * (rx + edge)) * sa + (uy * (ry + edge)) * ca;
    }
    add(c, ex, ey, N, CREAM);
    for (int i = 0; i < N; i++) {
        float tx[3] = {cx, px[i], px[(i + 1) % N]}, ty[3] = {cy, py[i], py[(i + 1) % N]};
        /* i 0..3 are the lower half (y down), 5..7 the upper left */
        add(c, tx, ty, 3, (i == 0 || i == 1) ? RIM : ((i == 2 || i == 7) ? FACET : BODY));
    }
    if (shine) {
        float sx = cx - rx * 0.35f, sy = cy - ry * 0.35f, hx = rx * 0.14f + 0.6f, hy = ry * 0.34f + 0.6f;
        float x[4] = {sx, sx + hx, sx, sx - hx}, y[4] = {sy - hy, sy, sy + hy, sy};
        add(c, x, y, 4, SPEC);
    }
}

/* a tapered tube with the cream edge, a dark and a blue-black half */
static void
limb(ctx* c, float ax, float ay, float bx, float by, float w0, float w1, float edge) {
    tube(c, ax, ay, bx, by, w0, w1, FACET, BODY, edge);
}

int
pose_man_build(float cx, float base_y, float s, float left_deg, float right_deg, pm_poly* out, int max) {
    ctx c = {out, 0, max};
    const float k = s; /* the drawing is for a man 200 pixels tall */
    const float edge = 1.7f * (k < 0.6f ? 0.6f : k);

    /* legs: thin at the hip, a little wider at the ankle, and big rounded shoes pointing outwards */
    for (int side = -1; side <= 1; side += 2) {
        float sd = (float)side;
        limb(&c, cx + sd * 6 * k, base_y - 92 * k, cx + sd * 20 * k, base_y - 22 * k, 6.5f * k, 9.5f * k, edge);
        blob(&c, cx + sd * 27 * k, base_y - 9 * k, 21.0f * k, 10.0f * k, sd * -8.0f, edge, 1);
    }
    /* the arms, behind the body: from the shoulders (+-10, 156) to a round hand, then the maraca beyond it */
    for (int side = -1; side <= 1; side += 2) {
        float sd = (float)side, deg = side < 0 ? left_deg : right_deg, t = deg * 0.0174532925f;
        float sx = cx + sd * 17 * k, sy = base_y - 154 * k, L = 62 * k;
        float ex = sx + sd * cosf(t) * L, ey = sy - sinf(t) * L;
        limb(&c, sx, sy, ex, ey, 4.4f * k, 3.0f * k, edge);
        blob(&c, ex, ey, 8.0f * k, 9.5f * k, 0.0f, edge, 1);
        maraca(&c, ex + sd * cosf(t) * 15.0f * k, ey - sinf(t) * 15.0f * k, 10.0f * k);
    }
    /* the body: a long narrow lozenge, the neck, and the oval head */
    {
        float x[6] = {cx - 9 * k, cx + 9 * k, cx + 12 * k, cx + 6 * k, cx - 6 * k, cx - 12 * k};
        float y[6] = {base_y - 158 * k, base_y - 158 * k, base_y - 124 * k, base_y - 90 * k, base_y - 90 * k, base_y - 124 * k};
        float ex[6], ey[6];
        for (int i = 0; i < 6; i++) {
            ex[i] = cx + (x[i] - cx) * (1.0f + edge / (10.0f * k));
            ey[i] = y[i] + (i < 2 ? -edge : (i > 2 && i < 5 ? edge : 0.0f));
        }
        add(&c, ex, ey, 6, CREAM);
        {
            float lx[5] = {x[0], cx, cx, x[4], x[5]}, ly[5] = {y[0], y[0], y[3], y[4], y[5]};
            float rx2[5] = {cx, x[1], x[2], x[3], cx}, ry2[5] = {y[0], y[1], y[2], y[3], y[3]};
            add(&c, lx, ly, 5, BODY);
            add(&c, rx2, ry2, 5, FACET);
        }
        {
            float x2[4] = {cx + 1.5f * k, cx + 3.5f * k, cx + 3.5f * k, cx + 1.5f * k}, y2[4] = {base_y - 150 * k, base_y - 150 * k, base_y - 112 * k, base_y - 112 * k};
            add(&c, x2, y2, 4, SPEC);
        }
    }
    limb(&c, cx, base_y - 156 * k, cx, base_y - 166 * k, 3.4f * k, 3.0f * k, edge);
    blob(&c, cx, base_y - 184 * k, 10.5f * k, 17.0f * k, 0.0f, edge, 1);
    return c.n;
}

int
pose_man_guides(float cx, float base_y, float s, float left_deg, float right_deg, pm_poly* out, int max) {
    ctx c = {out, 0, max};
    for (int side = -1; side <= 1; side += 2) {
        float sx = cx + (float)side * 17 * s, sy = base_y - 154 * s, R = 98.0f * s;
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
