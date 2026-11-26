/*
 * bios_models: procedural models in the style of the BIOS menu, see bios_models.h.
 * Flat shaded solids made of boxes, cylinders and a low poly sphere; the winding of every face is
 * fixed up against an outward direction, so the primitives only have to get the geometry right.
 */
#include "bios_models.h"

#include "bios_rom.h"
#include "case_models/dc_model.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VERTS 4096
#define MAX_POLYS 64
#define MAX_TRIS 1600
#define PI_F 3.14159265f

#define STRIP_IGNORE_LIGHT 0x01
#define STRIP_IGNORE_SPECULAR 0x02
#define STRIP_INNER 0x80 /* inside the case (the tray): left out of the semi-transparent second copy */
#define STRIP_DOUBLE_SIDED 0x10

typedef struct {
    int tex;
    uint32_t diffuse;
    uint8_t flags;
    int ntris;
    nj_corner* corners;
} bpoly;

typedef struct {
    nj_vertex* verts;
    int nverts;
    bpoly polys[MAX_POLYS];
    int npolys;
    int err;
} builder;

typedef nj_vec3 v3;

static v3
v3m(float x, float y, float z) {
    v3 r = {x, y, z};
    return r;
}
static v3
sub(v3 a, v3 b) {
    return v3m(a.x - b.x, a.y - b.y, a.z - b.z);
}
static v3
add(v3 a, v3 b) {
    return v3m(a.x + b.x, a.y + b.y, a.z + b.z);
}
static v3
mul(v3 a, float f) {
    return v3m(a.x * f, a.y * f, a.z * f);
}
static float
dot(v3 a, v3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static v3
cross(v3 a, v3 b) {
    return v3m(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static v3
norm(v3 a) {
    float l = sqrtf(dot(a, a));
    return l > 1e-9f ? mul(a, 1.0f / l) : v3m(0, 0, 1);
}

static int
poly_new(builder* b, int tex, uint32_t diffuse, uint8_t flags) {
    if (b->npolys >= MAX_POLYS) {
        b->err = 1;
        return 0;
    }
    bpoly* p = &b->polys[b->npolys];
    p->tex = tex;
    p->diffuse = diffuse;
    p->flags = flags;
    p->ntris = 0;
    p->corners = (nj_corner*)calloc(3 * MAX_TRIS, sizeof(nj_corner));
    if (!p->corners) {
        b->err = 1;
    }
    return b->npolys++;
}

/* One triangle. `out` is a direction the face should look toward; nrm is NULL for a flat face. */
static void
tri(builder* b, int poly, const v3 p[3], const v3* nrm, const float uv[3][2], v3 out) {
    bpoly* bp = &b->polys[poly];
    if (b->err || !bp->corners || bp->ntris >= MAX_TRIS || b->nverts + 3 > MAX_VERTS) {
        b->err = 1;
        return;
    }
    int order[3] = {0, 1, 2};
    v3 n = cross(sub(p[1], p[0]), sub(p[2], p[0]));
    if (dot(n, n) < 1e-12f) {
        return; /* degenerate, e.g. at a pole */
    }
    if (dot(n, out) < 0.0f) {
        order[1] = 2;
        order[2] = 1;
        n = mul(n, -1.0f);
    }
    n = norm(n);
    for (int k = 0; k < 3; k++) {
        int s = order[k];
        nj_vertex* v = &b->verts[b->nverts];
        memset(v, 0, sizeof(*v));
        v->pos = p[s];
        v->nrm = nrm ? nrm[s] : n;
        v->valid = v->has_nrm = 1;
        nj_corner* c = &bp->corners[bp->ntris * 3 + k];
        c->idx = (uint16_t)b->nverts++;
        c->u = uv ? uv[s][0] : 0.0f;
        c->v = uv ? uv[s][1] : 0.0f;
    }
    bp->ntris++;
}

static void
quad(builder* b, int poly, v3 a, v3 bb, v3 c, v3 d, const float uv[4][2], v3 out) {
    v3 t0[3] = {a, bb, c}, t1[3] = {a, c, d};
    float u0[3][2], u1[3][2];
    if (uv) {
        memcpy(u0[0], uv[0], 8);
        memcpy(u0[1], uv[1], 8);
        memcpy(u0[2], uv[2], 8);
        memcpy(u1[0], uv[0], 8);
        memcpy(u1[1], uv[2], 8);
        memcpy(u1[2], uv[3], 8);
    }
    tri(b, poly, t0, NULL, uv ? u0 : NULL, out);
    tri(b, poly, t1, NULL, uv ? u1 : NULL, out);
}

/* Rectangular frustum standing on y: bottom half sizes (w0, d0) at y0, top (w1, d1) at y1, centred on (cx, cz). */
static void
frustum(builder* b, int poly, float cx, float cz, float y0, float y1, float w0, float d0, float w1, float d1, int bottom) {
    v3 bt[4] = {v3m(cx - w0, y0, cz - d0), v3m(cx + w0, y0, cz - d0), v3m(cx + w0, y0, cz + d0), v3m(cx - w0, y0, cz + d0)};
    v3 tp[4] = {v3m(cx - w1, y1, cz - d1), v3m(cx + w1, y1, cz - d1), v3m(cx + w1, y1, cz + d1), v3m(cx - w1, y1, cz + d1)};
    v3 mid = v3m(cx, (y0 + y1) / 2, cz);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        v3 fc = mul(add(add(bt[i], bt[j]), add(tp[i], tp[j])), 0.25f);
        quad(b, poly, bt[i], bt[j], tp[j], tp[i], NULL, sub(fc, mid));
    }
    quad(b, poly, tp[0], tp[1], tp[2], tp[3], NULL, v3m(0, 1, 0));
    if (bottom) {
        quad(b, poly, bt[0], bt[1], bt[2], bt[3], NULL, v3m(0, -1, 0));
    }
}

#define CAP_BOTTOM 1
#define CAP_TOP 2

/* Cylinder (or cone frustum) from base centre `c` along `axis`; smooth radial normals on the side. */
static void
cylinder(builder* b, int side_poly, int cap_poly, int caps, int seg, v3 c, v3 axis, float r0, float r1, float len) {
    v3 a = norm(axis);
    v3 ref = fabsf(a.y) < 0.9f ? v3m(0, 1, 0) : v3m(1, 0, 0);
    v3 u = norm(cross(a, ref)), v = cross(a, u);
    v3 top = add(c, mul(a, len));
    for (int i = 0; i < seg; i++) {
        float t0 = 2 * PI_F * i / seg, t1 = 2 * PI_F * (i + 1) / seg;
        v3 d0 = add(mul(u, cosf(t0)), mul(v, sinf(t0))), d1 = add(mul(u, cosf(t1)), mul(v, sinf(t1)));
        v3 b0 = add(c, mul(d0, r0)), b1 = add(c, mul(d1, r0));
        v3 t0p = add(top, mul(d0, r1)), t1p = add(top, mul(d1, r1));
        v3 q1[3] = {b0, b1, t1p}, q2[3] = {b0, t1p, t0p};
        v3 n1[3] = {d0, d1, d1}, n2[3] = {d0, d1, d0};
        v3 out = add(d0, d1);
        tri(b, side_poly, q1, n1, NULL, out);
        tri(b, side_poly, q2, n2, NULL, out);
        if ((caps & CAP_TOP) && r1 > 0.0f) {
            v3 f[3] = {top, t0p, t1p};
            tri(b, cap_poly, f, NULL, NULL, a);
        }
        if ((caps & CAP_BOTTOM) && r0 > 0.0f) {
            v3 f[3] = {c, b0, b1};
            tri(b, cap_poly, f, NULL, NULL, mul(a, -1.0f));
        }
    }
}

/* Solid of revolution about the y axis through (cx, cy, cz): rings of (radius, height above cy), bottom to
 * top, z squeezed by zs. Normals point away from the inner point (cx, cy + inner_y, cz), which makes domes
 * look round. A cap closes the top (and the bottom if bottom_cap). */
static void
lathe(builder* b, int poly, int seg, const float (*ring)[2], int n, v3 c, float zs, float inner_y, int top_cap, int bottom_cap) {
    v3 in = v3m(c.x, c.y + inner_y, c.z);
    for (int i = 0; i + 1 < n; i++) {
        for (int k = 0; k < seg; k++) {
            float t0 = 2 * PI_F * k / seg, t1 = 2 * PI_F * (k + 1) / seg;
            v3 p[4] = {v3m(c.x + ring[i][0] * cosf(t0), c.y + ring[i][1], c.z + ring[i][0] * sinf(t0) * zs),
                       v3m(c.x + ring[i][0] * cosf(t1), c.y + ring[i][1], c.z + ring[i][0] * sinf(t1) * zs),
                       v3m(c.x + ring[i + 1][0] * cosf(t1), c.y + ring[i + 1][1], c.z + ring[i + 1][0] * sinf(t1) * zs),
                       v3m(c.x + ring[i + 1][0] * cosf(t0), c.y + ring[i + 1][1], c.z + ring[i + 1][0] * sinf(t0) * zs)};
            v3 nr[4];
            for (int q = 0; q < 4; q++) {
                nr[q] = norm(sub(p[q], in));
            }
            v3 out = add(nr[0], nr[2]);
            v3 t1v[3] = {p[0], p[1], p[2]}, t2v[3] = {p[0], p[2], p[3]};
            v3 n1[3] = {nr[0], nr[1], nr[2]}, n2[3] = {nr[0], nr[2], nr[3]};
            tri(b, poly, t1v, n1, NULL, out);
            tri(b, poly, t2v, n2, NULL, out);
        }
    }
    for (int cap = 0; cap < 2; cap++) {
        if ((cap == 0 && !top_cap) || (cap == 1 && !bottom_cap)) {
            continue;
        }
        int r = cap == 0 ? n - 1 : 0;
        float y = c.y + ring[r][1];
        for (int k = 0; k < seg; k++) {
            float t0 = 2 * PI_F * k / seg, t1 = 2 * PI_F * (k + 1) / seg;
            v3 f[3] = {v3m(c.x, y, c.z), v3m(c.x + ring[r][0] * cosf(t0), y, c.z + ring[r][0] * sinf(t0) * zs),
                       v3m(c.x + ring[r][0] * cosf(t1), y, c.z + ring[r][0] * sinf(t1) * zs)};
            tri(b, poly, f, NULL, NULL, v3m(0, cap == 0 ? 1.0f : -1.0f, 0));
        }
    }
}

/* Bent bar with a rectangular section (hy x hz half sizes) through `n` centres in the x-y plane. */
static void
bar(builder* b, int poly, const v3* c, int n, float hy, float hz) {
    v3 ring[8][4];
    for (int i = 0; i < n; i++) {
        v3 t = norm(sub(c[i + (i + 1 < n)], c[i - (i > 0)]));
        v3 nn = v3m(-t.y, t.x, 0.0f);
        ring[i][0] = add(add(c[i], mul(nn, hy)), v3m(0, 0, hz));
        ring[i][1] = add(add(c[i], mul(nn, -hy)), v3m(0, 0, hz));
        ring[i][2] = add(add(c[i], mul(nn, -hy)), v3m(0, 0, -hz));
        ring[i][3] = add(add(c[i], mul(nn, hy)), v3m(0, 0, -hz));
    }
    for (int i = 0; i + 1 < n; i++) {
        v3 mid = mul(add(c[i], c[i + 1]), 0.5f);
        for (int k = 0; k < 4; k++) {
            int j = (k + 1) & 3;
            v3 fc = mul(add(add(ring[i][k], ring[i][j]), add(ring[i + 1][k], ring[i + 1][j])), 0.25f);
            quad(b, poly, ring[i][k], ring[i][j], ring[i + 1][j], ring[i + 1][k], NULL, sub(fc, mid));
        }
    }
}

/* ---- finishing ----------------------------------------------------------------------------- */

static void
builder_free(builder* b) {
    for (int i = 0; i < b->npolys; i++) {
        free(b->polys[i].corners);
    }
    free(b->verts);
}

static int
builder_finish(builder* b, nj_object* out) {
    int ok = !b->err && b->nverts > 0;
    nj_node* node = ok ? (nj_node*)calloc(1, sizeof(nj_node)) : NULL;
    nj_mesh* mesh = node ? (nj_mesh*)calloc(1, sizeof(nj_mesh)) : NULL;
    if (mesh) {
        mesh->verts = (nj_vertex*)calloc((size_t)b->nverts, sizeof(nj_vertex));
        mesh->polys = (nj_poly*)calloc((size_t)b->npolys, sizeof(nj_poly));
    }
    if (!mesh || !mesh->verts || !mesh->polys) {
        if (mesh) {
            free(mesh->verts);
            free(mesh->polys);
        }
        free(mesh);
        free(node);
        builder_free(b);
        return -1;
    }
    memcpy(mesh->verts, b->verts, (size_t)b->nverts * sizeof(nj_vertex));
    mesh->nverts = b->nverts;
    for (int i = 0; i < b->npolys; i++) {
        const bpoly* s = &b->polys[i];
        nj_poly* d = &mesh->polys[mesh->npolys];
        if (s->ntris == 0) {
            continue;
        }
        d->tex = s->tex;
        d->diffuse = s->diffuse;
        d->has_diffuse = 1;
        d->has_uv = s->tex >= 0;
        d->strip_flags = s->flags;
        d->ntris = s->ntris;
        d->corners = (nj_corner*)malloc((size_t)s->ntris * 3 * sizeof(nj_corner));
        if (!d->corners) {
            continue; /* leaves the poly empty; drop it */
        }
        memcpy(d->corners, s->corners, (size_t)s->ntris * 3 * sizeof(nj_corner));
        mesh->npolys++;
    }
    node->scl.x = node->scl.y = node->scl.z = 1.0f;
    node->parent = -1;
    node->mesh = mesh;
    out->count = 1;
    out->nodes = node;
    builder_free(b);
    return 0;
}

static int
builder_start(builder* b) {
    memset(b, 0, sizeof(*b));
    b->verts = (nj_vertex*)calloc(MAX_VERTS, sizeof(nj_vertex));
    return b->verts ? 0 : -1;
}

/* ---- the models ---------------------------------------------------------------------------- */

static void
build_phone(builder* b) {
    int body = poly_new(b, -1, BMODEL_NOTE_COLOR, 0);
    int light = poly_new(b, -1, 0xFFB299FFu, 0);
    int white = poly_new(b, -1, 0xFFFFFFFFu, 0);
    int hub = poly_new(b, -1, 0xFFD2D2E6u, 0);

    /* square plinth, then the rounded, domed body of an old bakelite set */
    frustum(b, body, 0.0f, 0.0f, 0.0f, 0.8f, 4.3f, 3.5f, 4.0f, 3.2f, 0);
    static const float shell[4][2] = {{3.7f, 0.0f}, {3.6f, 0.9f}, {2.9f, 1.8f}, {1.6f, 2.4f}};
    lathe(b, body, 10, shell, 4, v3m(0.0f, 0.8f, 0.0f), 0.85f, 0.0f, 1, 0);
    /* the dial: one white disc with a small hub, on the sloped front */
    v3 tilt = norm(v3m(0.0f, cosf(0.85f), sinf(0.85f)));
    v3 centre = v3m(0.0f, 2.0f, 1.9f);
    cylinder(b, white, white, CAP_TOP, 12, centre, tilt, 1.75f, 1.75f, 0.3f);
    cylinder(b, hub, hub, CAP_TOP, 6, add(centre, mul(tilt, 0.3f)), tilt, 0.6f, 0.5f, 0.2f);
    /* handset: two round cups joined by an arched grip */
    static const float cup[3][2] = {{1.15f, 0.0f}, {1.3f, 0.8f}, {0.7f, 1.7f}};
    lathe(b, light, 7, cup, 3, v3m(-2.9f, 2.5f, 0.0f), 1.0f, -0.6f, 1, 0);
    lathe(b, light, 7, cup, 3, v3m(2.9f, 2.5f, 0.0f), 1.0f, -0.6f, 1, 0);
    const v3 grip[4] = {{-2.9f, 3.9f, 0.0f}, {-1.0f, 4.75f, 0.0f}, {1.0f, 4.75f, 0.0f}, {2.9f, 3.9f, 0.0f}};
    bar(b, light, grip, 4, 0.42f, 0.42f);
}

/* A winners' stand: three blocks in the Dreamcast's colours, the winner (light grey) in the middle, the second (orange) on its left and the third
 * (blue) on its right, about 8 units wide and 6 tall, centred on the origin. The numbers come later: bmodel_podium_digits. */
#define POD_BOTTOM (-3.2f)
#define POD_BACK (-1.4f) /* the backs line up: the deeper the block, the further it sticks out to the front */
/* The winner (1) is the widest and the deepest, the second (2) in between, the third (3) the smallest, the blocks touching: the 1 sticks
 * out a little to the front. */
static const float pod_h[3] = {6.0f, 4.2f, 3.0f};
static const float pod_w[3] = {3.2f, 2.8f, 2.4f};
static const float pod_d[3] = {2.8f, 2.4f, 2.0f};
static const float pod_cx[3] = {0.2f, -2.8f, 3.0f}; /* the 1 in the middle, the 2 to its left and the 3 to its right, 0.04 into each other */

#define POD_TAPER 0.95f /* the foot of a block is this much of its top's width and depth: the top is a tiny bit bigger */

static void
build_podium(builder* b) {
    static const uint32_t col[3] = {0xFFE4E6EEu, 0xFFE8681Cu, 0xFF1E5AB4u}; /* light grey (the winner), Dreamcast orange, Dreamcast blue */
    /* the faces are not depth sorted: the second (behind the winner) first, then the winner, then the third (its edge over the winner's) */
    static const int order[3] = {1, 0, 2};
    for (int k = 0; k < 3; k++) {
        const int i = order[k];
        int poly = poly_new(b, -1, col[i], 0);
        const float hw = pod_w[i] / 2.0f, hd = pod_d[i] / 2.0f, cz = POD_BACK + hd, fw = hw * POD_TAPER, fd = hd * POD_TAPER;
        const float y0 = POD_BOTTOM, y1 = POD_BOTTOM + pod_h[i], x = pod_cx[i];
        v3 b00 = v3m(x - fw, y0, cz - fd), b10 = v3m(x + fw, y0, cz - fd), b11 = v3m(x + fw, y0, cz + fd), b01 = v3m(x - fw, y0, cz + fd);
        v3 t00 = v3m(x - hw, y1, cz - hd), t10 = v3m(x + hw, y1, cz - hd), t11 = v3m(x + hw, y1, cz + hd), t01 = v3m(x - hw, y1, cz + hd);
        quad(b, poly, t00, t01, t11, t10, NULL, v3m(0, 1, 0));  /* top */
        quad(b, poly, b00, b10, b11, b01, NULL, v3m(0, -1, 0)); /* bottom */
        quad(b, poly, b01, b11, t11, t01, NULL, v3m(0, 0, 1));  /* front */
        quad(b, poly, b10, b00, t00, t10, NULL, v3m(0, 0, -1)); /* back */
        quad(b, poly, b11, b10, t10, t11, NULL, v3m(1, 0, 0));  /* right */
        quad(b, poly, b00, b01, t01, t00, NULL, v3m(-1, 0, 0)); /* left */
    }
}

#define GLOBE_R 4.6f
#define GLOBE_FRONT_LON (-75.0f) /* the Americas face the viewer */
#define GLOBE_LEAN 0.35f           /* the north pole leans toward the viewer (radians) */
#ifndef GLOBE_SUBDIV
#define GLOBE_SUBDIV 2 /* 20 * 4^2 = 320 triangles; 3 gives 1280 */
#endif
#define GLOBE_MAX_FACES (20 << (2 * GLOBE_SUBDIV))

static v3
globe_orient(v3 p) {
    /* turn the earth so GLOBE_FRONT_LON faces +z, then lean the north pole toward the viewer */
    float a = -GLOBE_FRONT_LON * (PI_F / 180.0f);
    v3 q = v3m(p.x * cosf(a) + p.z * sinf(a), p.y, -p.x * sinf(a) + p.z * cosf(a));
    return v3m(q.x, q.y * cosf(GLOBE_LEAN) - q.z * sinf(GLOBE_LEAN), q.y * sinf(GLOBE_LEAN) + q.z * cosf(GLOBE_LEAN));
}

/* Continents and big islands as closed (lon, lat) loops in degrees. */
#define LOOP_END 999.0f
static const float globe_coast[][2] = {
    /* North America */
    {-168, 66}, {-162, 70}, {-156, 71.5}, {-141, 69.5}, {-128, 70}, {-115, 68}, {-95, 72}, {-85, 69}, {-82, 63}, {-93, 59}, {-94, 57}, {-80, 52}, {-79, 57}, {-77, 62}, {-70, 60}, {-62, 58}, {-56, 52}, {-60, 47}, {-66, 45}, {-70, 42}, {-74, 40}, {-76, 35}, {-81, 31}, {-80, 26}, {-82, 26}, {-84, 30}, {-90, 29.5}, {-94, 29}, {-97, 26}, {-97.5, 22}, {-95, 19}, {-91, 19}, {-90, 21}, {-87, 21}, {-88, 16}, {-84, 15}, {-83, 10}, {-79, 8.5}, {-83, 8}, {-86, 11}, {-92, 14.5}, {-96, 16}, {-105, 20}, {-109, 26}, {-114, 31}, {-117, 33}, {-121, 35}, {-124, 40}, {-124, 46}, {-125, 49}, {-130, 54}, {-135, 58}, {-140, 60}, {-148, 60}, {-152, 58}, {-158, 56}, {-163, 55}, {-158, 58}, {-162, 61}, {-165, 63}, {LOOP_END, 0},
    /* Greenland */
    {-73, 78}, {-60, 82}, {-30, 83}, {-20, 80}, {-18, 76}, {-22, 70}, {-32, 68}, {-43, 60}, {-48, 61}, {-53, 67}, {-56, 70}, {-67, 76}, {LOOP_END, 0},
    /* South America */
    {-77.5, 8.5}, {-72, 12}, {-65, 10.5}, {-60, 8}, {-52, 5}, {-50, 0}, {-44, -2}, {-35, -5}, {-35, -9}, {-39, -13}, {-39, -18}, {-41, -22}, {-48, -26}, {-53, -34}, {-58, -35}, {-57, -38}, {-62, -39}, {-65, -45}, {-68, -50}, {-69, -54}, {-72, -53}, {-75, -47}, {-73, -40}, {-71, -30}, {-70, -18}, {-76, -14}, {-81, -6}, {-80, -2}, {-78, 2}, {LOOP_END, 0},
    /* Africa */
    {-17, 21}, {-16, 28}, {-9, 32}, {-6, 36}, {10, 37}, {11, 33}, {20, 31}, {32, 31}, {34, 28}, {38, 20}, {43, 12}, {51, 11.5}, {48, 5}, {40, -3}, {40, -11}, {35, -20}, {33, -26}, {27, -34}, {20, -35}, {18, -30}, {12, -17}, {13, -8}, {9, -1}, {9, 4}, {5, 6}, {-2, 5}, {-8, 4.5}, {-13, 8}, {-17, 13}, {LOOP_END, 0},
    /* Eurasia */
    {-9, 37}, {-9, 43}, {-2, 44}, {-5, 48}, {2, 51}, {5, 53}, {8, 54}, {8, 57}, {10, 57}, {12, 54}, {21, 55}, {24, 59}, {30, 60}, {23, 60}, {22, 65}, {17, 61}, {19, 59}, {16, 56}, {12, 56}, {11, 59}, {5, 59}, {5, 62}, {14, 67}, {25, 71}, {33, 70}, {41, 67}, {40, 64}, {45, 68}, {60, 69}, {70, 73}, {80, 73}, {100, 77}, {113, 74}, {130, 71}, {140, 72}, {160, 70}, {170, 70}, {180, 68}, {180, 65}, {178, 63}, {163, 60}, {156, 51}, {155, 58}, {142, 59}, {137, 54}, {141, 52}, {140, 48}, {135, 43}, {130, 42}, {129, 35}, {126, 35}, {125, 40}, {121, 40}, {122, 37}, {119, 35}, {122, 31}, {121, 28}, {115, 23}, {110, 21}, {108, 17}, {106, 10}, {105, 9}, {101, 13}, {100, 7}, {103, 1.5}, {100, 3}, {98, 10}, {98, 16}, {94, 16}, {92, 22}, {87, 21.5}, {80, 15}, {77, 8}, {73, 17}, {72, 21}, {67, 24.5}, {61, 25}, {57, 26}, {56, 27}, {51, 30}, {49, 30}, {51, 25}, {56, 24}, {59, 22}, {52, 16}, {44, 12.5}, {43, 15}, {39, 21}, {35, 28}, {35, 31}, {36, 36.5}, {27, 37}, {26, 40.5}, {23, 40}, {24, 37}, {21, 37}, {19, 42}, {13, 45}, {12, 44}, {18, 40}, {16, 38}, {12, 41}, {9, 44}, {3, 43}, {0, 39}, {-2, 37}, {-6, 36}, {LOOP_END, 0},
    /* Australia */
    {114, -22}, {122, -18}, {130, -12}, {137, -12}, {136, -16}, {141, -17}, {142, -11}, {146, -19}, {153, -26}, {151, -33}, {147, -38}, {141, -38}, {135, -35}, {131, -31}, {124, -33}, {115, -34}, {114, -28}, {LOOP_END, 0},
    /* UK */
    {-5, 50}, {1, 51}, {2, 53}, {-3, 56}, {-5, 58.5}, {-6, 56}, {-3, 54}, {-5, 52}, {LOOP_END, 0},
    /* Japan */
    {130, 31}, {136, 34}, {140, 36}, {142, 40}, {142, 44}, {140, 42}, {137, 37}, {131, 34}, {LOOP_END, 0},
    /* Madagascar */
    {44, -25}, {47, -25}, {50, -15}, {49, -12}, {44, -17}, {LOOP_END, 0},
    /* Borneo */
    {109, 1}, {117, 7}, {119, 5}, {116, -4}, {110, -3}, {LOOP_END, 0},
    /* Sumatra */
    {95, 5}, {106, -6}, {104, -6}, {98, 0}, {LOOP_END, 0},
    /* New Guinea */
    {131, -1}, {141, -3}, {150, -10}, {142, -9}, {138, -8}, {LOOP_END, 0},
    /* New Zealand */
    {172, -34}, {178, -38}, {175, -41}, {167, -46}, {172, -41}, {LOOP_END, 0},
    {LOOP_END, LOOP_END},
};

/* Land test on the coarse continent loops (lon, lat in degrees), plus ice at the poles. */
static int
globe_surface(float lon, float lat) {
    if (lat > 78.0f || lat < -70.0f) {
        return 2; /* ice */
    }
    int start = 0, inside = 0;
    for (int i = 0;; i++) {
        if (globe_coast[i][0] == LOOP_END) {
            if (globe_coast[i][1] == LOOP_END) {
                break;
            }
            int n = i - start, in = 0;
            for (int k = 0, j = n - 1; k < n; j = k++) {
                const float* pk = globe_coast[start + k];
                const float* pj = globe_coast[start + j];
                if (((pk[1] > lat) != (pj[1] > lat)) && (lon < (pj[0] - pk[0]) * (lat - pk[1]) / (pj[1] - pk[1]) + pk[0])) {
                    in = !in;
                }
            }
            inside |= in;
            start = i + 1;
        }
    }
    return inside ? 1 : 0;
}

static void
build_globe(builder* b) {
    /* BIOS palette: the green of the menu icons, a deep blue, a light ice colour */
    int sea = poly_new(b, -1, 0xFF2A5FD8u, 0);
    int land = poly_new(b, -1, 0xFF1CB257u, 0);
    int ice = poly_new(b, -1, 0xFFEAF2FFu, 0);

    /* icosahedron, subdivided twice: a faceted ball */
    static v3 vert[12 + 2 * GLOBE_MAX_FACES]; /* corners of the ball, with the midpoints added by the subdivision */
    static int face[GLOBE_MAX_FACES][3];
    int nv = 12, nf = 20;
    const float t = (1.0f + sqrtf(5.0f)) / 2.0f;
    {
        const float a = 1.0f, c = t;
        const float pts[12][3] = {{-a, c, 0}, {a, c, 0}, {-a, -c, 0}, {a, -c, 0}, {0, -a, c}, {0, a, c},
                                  {0, -a, -c}, {0, a, -c}, {c, 0, -a}, {c, 0, a}, {-c, 0, -a}, {-c, 0, a}};
        for (int i = 0; i < 12; i++) {
            vert[i] = norm(v3m(pts[i][0], pts[i][1], pts[i][2]));
        }
    }
    static const int f0[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                  {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    memcpy(face, f0, sizeof(f0));
    for (int level = 0; level < GLOBE_SUBDIV; level++) {
        static int nface[GLOBE_MAX_FACES][3];
        int nn = 0;
        for (int f = 0; f < nf; f++) {
            int m[3];
            for (int k = 0; k < 3; k++) {
                v3 mid = norm(add(vert[face[f][k]], vert[face[f][(k + 1) % 3]]));
                int found = -1;
                for (int q = 12; q < nv; q++) {
                    if (fabsf(vert[q].x - mid.x) + fabsf(vert[q].y - mid.y) + fabsf(vert[q].z - mid.z) < 1e-5f) {
                        found = q;
                        break;
                    }
                }
                if (found < 0) {
                    vert[nv] = mid;
                    found = nv++;
                }
                m[k] = found;
            }
            int a = face[f][0], bb = face[f][1], c = face[f][2];
            int quads[4][3] = {{a, m[0], m[2]}, {m[0], bb, m[1]}, {m[2], m[1], c}, {m[0], m[1], m[2]}};
            for (int k = 0; k < 4; k++) {
                memcpy(nface[nn++], quads[k], sizeof(int) * 3);
            }
        }
        nf = nn;
        memcpy(face, nface, sizeof(int) * (size_t)nf * 3);
    }
    for (int f = 0; f < nf; f++) {
        v3 corner[3], c = v3m(0, 0, 0);
        for (int k = 0; k < 3; k++) {
            corner[k] = vert[face[f][k]];
            c = add(c, corner[k]);
        }
        c = norm(c);
        float lat = asinf(c.y) * (180.0f / PI_F), lon = atan2f(c.x, c.z) * (180.0f / PI_F); /* unit sphere, lon 0 toward +z */
        /* land when at least two of four sample points (the middle and three points toward the corners) are land */
        int kind = globe_surface(lon, lat), votes = kind == 1;
        if (kind != 2) {
            for (int k = 0; k < 3; k++) {
                v3 q = norm(add(mul(c, 0.5f), mul(corner[k], 0.5f)));
                float qlat = asinf(q.y) * (180.0f / PI_F), qlon = atan2f(q.x, q.z) * (180.0f / PI_F);
                votes += globe_surface(qlon, qlat) == 1;
            }
            kind = votes >= 2;
        }
        v3 p[3];
        for (int k = 0; k < 3; k++) {
            p[k] = add(v3m(0.0f, 5.0f, 0.0f), mul(globe_orient(corner[k]), GLOBE_R));
        }
        tri(b, kind == 1 ? land : (kind == 2 ? ice : sea), p, NULL, NULL, sub(p[0], v3m(0, 5.0f, 0)));
    }
}

/* The jewel cases: the models made for the menu (case_models/, generated: triangle strips in millimetres, the light baked into the vertex colours),
 * 125 mm tall, scaled to BMODEL_CASE_HEIGHT. The front and rear labels are the picture slots (texture 0 and 1); the spine label and the ribs are plain
 * colours (the average of their pictures). Parts are drawn opaque first, then the clear ones. Every polygon of one colour in a part is one poly. */
static uint32_t
mul_argb(uint32_t c, uint32_t t) {
    uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        r |= ((((c >> sh) & 255u) * ((t >> sh) & 255u) + 127u) / 255u) << sh;
    }
    return r;
}

/* The label quads of the cases are 120 mm tall (the pictures have a 1:1 pixel ratio: front 120 x 120, back about 139 x 120, spine 7 / 15.5 x 120):
 * the model's labels are moved to those sizes. The front of the NTSC cases is the 120 mm square at the right edge; the hinge side
 * beside it is a plain quad (below). */
/* where the NTSC cases' 120 mm front cover starts: 14.5 mm in from the hinge edge, as on the real jewel cases (the clear
 * front spine beside it shows the inner label) */
#define NTSC_FRONT_X0 -56.5f
#define NTSC_FRONT_X1 69.6f /* the front cover reaches the right edge of the lid (inside its rim), as on the real cases */

static float
remap_f(float v, float o0, float o1, float n0, float n1) {
    return o1 > o0 ? n0 + (v - o0) / (o1 - o0) * (n1 - n0) : v;
}

static void
fit_label(dc_vertex_t v[4], const char* name, int pal) {
    float x0 = v[0].x, x1 = v[0].x, z0 = v[0].z, z1 = v[0].z;
    for (int i = 1; i < 4; i++) {
        x0 = v[i].x < x0 ? v[i].x : x0;
        x1 = v[i].x > x1 ? v[i].x : x1;
        z0 = v[i].z < z0 ? v[i].z : z0;
        z1 = v[i].z > z1 ? v[i].z : z1;
    }
    for (int i = 0; i < 4; i++) {
        const float hh = strstr(name, "label_rear") ? 59.5f : 60.0f; /* Sega's guidelines: front inlay 120 x 120 mm, back inlay 119 mm tall */
        v[i].y = v[i].y > 0.0f ? hh : -hh;
        if (strstr(name, "label_front")) {
            v[i].x = remap_f(v[i].x, x0, x1, pal ? -53.0f : NTSC_FRONT_X0, pal ? 67.0f : NTSC_FRONT_X1);
        } else if (strstr(name, "label_spine")) {
            v[i].z = remap_f(v[i].z, z0, z1, pal ? -8.5f : -3.77f, pal ? 7.0f : 3.27f);
            v[i].u = 1.0f - v[i].u; /* the spine labels read the other way round (turned 180 degrees) */
            v[i].v = 1.0f - v[i].v;
            if (pal) {
                v[i].x = -71.03f; /* on the outside of the side wall, from the back edge to where the hinge's curve starts (it covered the front band) */
            }
        }
    }
}

/* The PAL case's hinge bar: the blue pill and the carrier it belongs to made one flat box, as wide as the pill (8 mm), over the whole
 * height of the front's hinge side; its front is a picture (BMODEL_TEX_PILL) with the lighter rounded pill and the swirl. */
static void
build_pal_bar(builder* b, float S) {
    const float x0 = -66.0f, x1 = -58.0f, yt = 61.5f, yb = -61.5f, zb = 8.5f, zf = 8.9f;
    const v3 A = v3m(x0 * S, yt * S, zf * S), B = v3m(x1 * S, yt * S, zf * S), C = v3m(x1 * S, yb * S, zf * S), D = v3m(x0 * S, yb * S, zf * S);
    static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    { /* the carrier: a plain lit face of the same blue as the hinge sections beside it (the picture above it is clear except the pill and its text) */
        int c0 = poly_new(b, -1, 0xFF2A4D9Cu, 0);
        const float zc = zf - 0.15f;
        quad(b, c0, v3m(x0 * S, yt * S, zc * S), v3m(x1 * S, yt * S, zc * S), v3m(x1 * S, yb * S, zc * S), v3m(x0 * S, yb * S, zc * S), NULL, v3m(0, 0, 1));
    }
    int p = poly_new(b, BMODEL_TEX_PILL, 0xFFFFFFFFu, STRIP_IGNORE_SPECULAR); /* lit like the menu's icons, but no shine: it is the same blue as the sections beside it */
    quad(b, p, A, B, C, D, uv, v3m(0, 0, 1));
    int q = poly_new(b, -1, 0xFF2A4D9Cu, 0);
    const v3 A0 = v3m(x0 * S, yt * S, zb * S), B0 = v3m(x1 * S, yt * S, zb * S), C0 = v3m(x1 * S, yb * S, zb * S), D0 = v3m(x0 * S, yb * S, zb * S);
    quad(b, q, A0, A, D, D0, NULL, v3m(-1, 0, 0));
    quad(b, q, B, B0, C0, C, NULL, v3m(1, 0, 0));
    quad(b, q, A0, B0, B, A, NULL, v3m(0, 1, 0));
    quad(b, q, D, C, C0, D0, NULL, v3m(0, -1, 0));
}

/* The PAL case's inside: the blue tray fills the space between the front and rear labels (and shows beside the hinge bar), so the clear shell
 * is not see-through. A box slightly inside the shell; the faces at the top, bottom and right edges are a little darker, as seen at a slant. */
static void
build_tray(builder* b, float S, float x0, float x1, float yt, float yb, float zf, float zr, uint32_t blue, float front_x1) {
    int f = poly_new(b, -1, blue, STRIP_INNER), e = poly_new(b, -1, blue, STRIP_INNER); /* lit */
    const v3 A = v3m(x0 * S, yt * S, zf * S), B = v3m(x1 * S, yt * S, zf * S), C = v3m(x1 * S, yb * S, zf * S), D = v3m(x0 * S, yb * S, zf * S);
    const v3 A0 = v3m(x0 * S, yt * S, zr * S), B0 = v3m(x1 * S, yt * S, zr * S), C0 = v3m(x1 * S, yb * S, zr * S), D0 = v3m(x0 * S, yb * S, zr * S);
    if (front_x1 > x0) { /* the front face only where the cover does not hide it (a big face behind the cover can cut through it in the
                          * per-triangle sort) */
        const v3 Bf = v3m(front_x1 * S, yt * S, zf * S), Cf = v3m(front_x1 * S, yb * S, zf * S);
        quad(b, f, A, Bf, Cf, D, NULL, v3m(0, 0, 1));
    }
    (void)C;
    quad(b, e, A0, B0, B, A, NULL, v3m(0, 1, 0));
    quad(b, e, D, C, C0, D0, NULL, v3m(0, -1, 0));
    quad(b, e, B, B0, C0, C, NULL, v3m(1, 0, 0));
}

#define RIB_MM 9.1f

/* The double CD case (two to four discs, the "fat box"): 142 x 125 x 24 mm. Its top and bottom edges are dark grey and ribbed over the
 * whole depth (blue and plain for PAL); the front, back and both sides are clear. Under the front the cover (the whole width: no hinge
 * strip), under the back the back cover, and on each side two spine labels, one per half, with a dark gap between the halves. */
#define DOUBLE_HZ 12.0f   /* half the depth */
#define DOUBLE_SPINE0 1.0f  /* a spine label from here (the gap's edge) ... */
#define DOUBLE_SPINE1 11.0f /* ... to here, on each half (10 mm: the spine pictures of the double kinds are made for that) */
static void
build_double_case(builder* b, int pal) {
    const float S = BMODEL_CASE_HEIGHT / 125.0f;
    const float X = 71.0f, Y = 62.5f, Z = DOUBLE_HZ;
    const uint32_t frame = pal ? 0xFF2A4D9Cu : 0xFF3C3E42u;
    /* the inside, dark, behind the clear sides (so they are not see-through) */
    {
        int in = poly_new(b, -1, frame, STRIP_INNER | STRIP_IGNORE_LIGHT);
        const float xi = X - 0.6f, zi = Z - 0.5f;
        quad(b, in, v3m(-xi * S, Y * S, -zi * S), v3m(-xi * S, Y * S, zi * S), v3m(-xi * S, -Y * S, zi * S), v3m(-xi * S, -Y * S, -zi * S), NULL, v3m(-1, 0, 0));
        quad(b, in, v3m(xi * S, Y * S, zi * S), v3m(xi * S, Y * S, -zi * S), v3m(xi * S, -Y * S, -zi * S), v3m(xi * S, -Y * S, zi * S), NULL, v3m(1, 0, 0));
    }
    /* the top and bottom edges */
    {
        int e = poly_new(b, pal ? -1 : BMODEL_TEX_RIBS, frame, 0);
        const float u0 = -X / RIB_MM, u1 = X / RIB_MM;
        const float v0 = 0.25f - 0.8f / (2 * Z - 1.6f) * 0.5f, v1 = 0.75f + 0.8f / (2 * Z - 1.6f) * 0.5f;
        const float uvt[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        quad(b, e, v3m(-X * S, Y * S, -Z * S), v3m(X * S, Y * S, -Z * S), v3m(X * S, Y * S, Z * S), v3m(-X * S, Y * S, Z * S), pal ? NULL : uvt, v3m(0, 1, 0));
        quad(b, e, v3m(-X * S, -Y * S, -Z * S), v3m(X * S, -Y * S, -Z * S), v3m(X * S, -Y * S, Z * S), v3m(-X * S, -Y * S, Z * S), pal ? NULL : uvt, v3m(0, -1, 0));
        /* the gap between the halves: a dark band round the sides */
        int g = poly_new(b, -1, 0xFF18191Bu, STRIP_IGNORE_LIGHT);
        const float gz = DOUBLE_SPINE0 - 0.2f, xg = X - 0.3f;
        quad(b, g, v3m(-xg * S, Y * S, -gz * S), v3m(-xg * S, Y * S, gz * S), v3m(-xg * S, -Y * S, gz * S), v3m(-xg * S, -Y * S, -gz * S), NULL, v3m(-1, 0, 0));
        quad(b, g, v3m(xg * S, Y * S, gz * S), v3m(xg * S, Y * S, -gz * S), v3m(xg * S, -Y * S, -gz * S), v3m(xg * S, -Y * S, gz * S), NULL, v3m(1, 0, 0));
    }
    /* the labels: the cover, the back cover, the spines */
    {
        static const float uvf[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        int f = poly_new(b, BMODEL_TEX_FRONT, 0xFFD4D4D4u, STRIP_IGNORE_LIGHT);
        const float fx = 69.6f, fy = 60.0f, fz = Z - 0.4f;
        quad(b, f, v3m(-fx * S, fy * S, fz * S), v3m(fx * S, fy * S, fz * S), v3m(fx * S, -fy * S, fz * S), v3m(-fx * S, -fy * S, fz * S), uvf, v3m(0, 0, 1));
        int r = poly_new(b, BMODEL_TEX_BACK, 0xFFEAEAEAu, STRIP_IGNORE_LIGHT);
        const float ry = 59.5f;
        quad(b, r, v3m(fx * S, ry * S, -fz * S), v3m(-fx * S, ry * S, -fz * S), v3m(-fx * S, -ry * S, -fz * S), v3m(fx * S, -ry * S, -fz * S), uvf, v3m(0, 0, -1));
        int sp = poly_new(b, BMODEL_TEX_SPINE, 0xFFB4B4B4u, STRIP_IGNORE_LIGHT);
        const float sx = X - 0.4f, sy = 60.0f;
        for (int half = 0; half < 2; half++) {
            const float z0 = half ? DOUBLE_SPINE0 : -DOUBLE_SPINE1, z1 = half ? DOUBLE_SPINE1 : -DOUBLE_SPINE0;
            /* as the single case's: on the left side u runs from 1 at the back edge to 0 at the front, v from 1 at the top; on the right
             * side the other way across, so it reads the same from outside */
            const float ul[4][2] = {{1, 1}, {0, 1}, {0, 0}, {1, 0}};
            const float ur[4][2] = {{1, 1}, {0, 1}, {0, 0}, {1, 0}};
            quad(b, sp, v3m(-sx * S, sy * S, z0 * S), v3m(-sx * S, sy * S, z1 * S), v3m(-sx * S, -sy * S, z1 * S), v3m(-sx * S, -sy * S, z0 * S), ul,
                 v3m(-1, 0, 0));
            quad(b, sp, v3m(sx * S, sy * S, z1 * S), v3m(sx * S, sy * S, z0 * S), v3m(sx * S, -sy * S, z0 * S), v3m(sx * S, -sy * S, z1 * S), ur,
                 v3m(1, 0, 0));
        }
    }
    /* the clear shell: front, back and sides */
    {
        int c = poly_new(b, -1, 0x40C4CCD4u, STRIP_IGNORE_LIGHT);
        quad(b, c, v3m(-X * S, Y * S, Z * S), v3m(X * S, Y * S, Z * S), v3m(X * S, -Y * S, Z * S), v3m(-X * S, -Y * S, Z * S), NULL, v3m(0, 0, 1));
        quad(b, c, v3m(X * S, Y * S, -Z * S), v3m(-X * S, Y * S, -Z * S), v3m(-X * S, -Y * S, -Z * S), v3m(X * S, -Y * S, -Z * S), NULL, v3m(0, 0, -1));
        quad(b, c, v3m(-X * S, Y * S, -Z * S), v3m(-X * S, Y * S, Z * S), v3m(-X * S, -Y * S, Z * S), v3m(-X * S, -Y * S, -Z * S), NULL, v3m(-1, 0, 0));
        quad(b, c, v3m(X * S, Y * S, Z * S), v3m(X * S, Y * S, -Z * S), v3m(X * S, -Y * S, -Z * S), v3m(X * S, -Y * S, Z * S), NULL, v3m(1, 0, 0));
    }
}

static void
build_case(builder* b, const dc_model_t* m, uint32_t spine_tint, int clear) {
    const float S = BMODEL_CASE_HEIGHT / 125.0f;
    const int ntsc = m != &dc_model_dreamcast_pal_case_blue;
    const int ribbed = ntsc && !clear; /* the white and dark grey NTSC cases; the clear ones (1 white tray, 2 black) have no ribs */
    if (!ntsc) {
        build_tray(b, S, -69.6f, 66.8f, 61.0f, -61.0f, 6.0f, -7.8f, 0xFF2A4D9Cu, -52.0f); /* its front well behind the cover's (8.0): small on the list's card, nearer let it cut through the art */
    } else if (clear) {
        /* the clear cases: the tray (white or black) inside, behind the front cover, seen through the clear edges */
        build_tray(b, S, NTSC_FRONT_X0, 69.5f, 60.8f, -60.8f, 3.0f, -4.0f, clear == 2 ? 0xFF141416u : 0xFFE6E6E2u, NTSC_FRONT_X0);
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int pi = 0; pi < m->nparts; pi++) {
            const dc_part_t* part = &m->parts[pi];
            if (part->translucent != pass) {
                continue;
            }
            if (clear && !strcmp(part->name, "ribs_front_strip")) {
                continue; /* clear plastic: no ribs */
            }
            if (!strcmp(part->name, "hinge_pill")) {
                build_pal_bar(b, S); /* the pill and the carrier: one flat box */
                continue;
            }
            dc_vertex_t lab[4];
            const dc_vertex_t* part_verts = part->verts;
            if (strstr(part->name, "label_") && part->nstrips == 1 && part->strip_len[0] == 4) {
                memcpy(lab, part->verts, sizeof(lab));
                fit_label(lab, part->name, !ntsc);
                part_verts = lab;
                if (ntsc && strstr(part->name, "label_front")) {
                    const float zf = lab[0].z;
                    if (ribbed) { /* the hinge side beside the front: plain, the case's colour */
                        const uint32_t fill = m == &dc_model_cd_case_white ? 0xFFD8D8D8u : 0xFF2E2E2Eu;
                        int hp = poly_new(b, -1, fill, STRIP_IGNORE_LIGHT);
                        quad(b, hp, v3m(-70.5f * S, 60.0f * S, zf * S), v3m(NTSC_FRONT_X0 * S, 60.0f * S, zf * S),
                             v3m(NTSC_FRONT_X0 * S, -60.0f * S, zf * S), v3m(-70.5f * S, -60.0f * S, zf * S), NULL, v3m(0, 0, 1));
                    } else {
                        /* the clear cases: the inner label right behind the clear front spine, touching the front cover's edge as
                         * the real ones do (white with the colour logo, or black with a dark grey one; the picture is made by gfx,
                         * BMODEL_TEX_PILL) */
                        static const float huv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                        int hp = poly_new(b, BMODEL_TEX_PILL, 0xFFFFFFFFu, STRIP_IGNORE_LIGHT);
                        const float zb = zf - 0.05f, wy = 60.8f;
                        quad(b, hp, v3m(-70.4f * S, wy * S, zb * S), v3m(NTSC_FRONT_X0 * S, wy * S, zb * S),
                             v3m(NTSC_FRONT_X0 * S, -wy * S, zb * S), v3m(-70.4f * S, -wy * S, zb * S), huv, v3m(0, 0, 1));
                        const uint32_t wc = clear == 2 ? 0xFF0E0E10u : 0xFFEEEEEAu;
                        int w = poly_new(b, -1, wc, STRIP_IGNORE_LIGHT);
                        /* seen from the spine side: the label wraps round the hinge edge inside the clear shell, from the back to the
                         * front, so the case is not see-through beside the spine label (behind it at x -71.03) */
                        const float sx = -70.8f, sz0 = -5.0f, sz1 = 5.2f;
                        quad(b, w, v3m(sx * S, wy * S, sz0 * S), v3m(sx * S, wy * S, sz1 * S), v3m(sx * S, -wy * S, sz1 * S),
                             v3m(sx * S, -wy * S, sz0 * S), NULL, v3m(-1, 0, 0));
                    }
                }
            }
            int tex = -1;
            uint32_t tint = 0xFFFFFFFFu;
            if (part->texture >= 0) {
                const char* f = m->tex[part->texture].file;
                if (strstr(f, "label_front")) {
                    tex = BMODEL_TEX_FRONT;
                } else if (strstr(f, "label_rear")) {
                    tex = BMODEL_TEX_BACK;
                } else if (strstr(f, "spine")) {
                    tex = BMODEL_TEX_SPINE; /* the spine label of the case's region, with the BIOS's Dreamcast logo (made by gfx) */
                    (void)spine_tint;
                } else {
                    tint = 0x24414141u; /* the ribs: faint dark lines, as their average */
                }
            }
            int ntri = 0;
            for (int st = 0; st < part->nstrips; st++) {
                ntri += part->strip_len[st] - 2;
            }
            const int first = b->npolys;
            const dc_vertex_t* src = part_verts;
            for (int st = 0; st < part->nstrips; st++) {
                const int n = part->strip_len[st];
                /* the NTSC cases' ribs: the outer top and bottom edges and the hinge strip (white or black, the spine's) carry the ribbed
                 * texture, its u running across the ribs (one repeat of the picture is RIB_MM wide) */
                int rib_axis = -1; /* 0: u from x, 1: u from y */
                if (ribbed && !strcmp(part->name, "case_shell") && st <= 1) {
                    rib_axis = 0; /* the top and bottom edges only: the right side stays smooth */
                } else if (ribbed && !strcmp(part->name, "hinge_strip") && (st == 0 || st == 2)) {
                    rib_axis = st == 0 ? 0 : 1;
                }
                const int stex = rib_axis >= 0 ? BMODEL_TEX_RIBS : tex;
                /* across the ribbed band: 0.8 mm at each side stay plain (the texture's rows 0..3 and 12..15 are plain, the ribs in between) */
                int cax = -1;
                float cmin = 0.0f, cext = 0.0f;
                if (rib_axis >= 0) {
                    float mn[3] = {1e9f, 1e9f, 1e9f}, mxv[3] = {-1e9f, -1e9f, -1e9f};
                    for (int i = 0; i < n; i++) {
                        const float c3[3] = {src[i].x, src[i].y, src[i].z};
                        for (int k = 0; k < 3; k++) {
                            mn[k] = c3[k] < mn[k] ? c3[k] : mn[k];
                            mxv[k] = c3[k] > mxv[k] ? c3[k] : mxv[k];
                        }
                    }
                    for (int k = 0; k < 3; k++) {
                        if (k != rib_axis && mxv[k] - mn[k] > cext) {
                            cext = mxv[k] - mn[k];
                            cmin = mn[k];
                            cax = k;
                        }
                    }
                    if (cext < 1.7f) {
                        cax = -1;
                    }
                }
                uint32_t srcc = src[0].argb;
                if (!ntsc && (srcc >> 24) == 0xFFu && ((srcc >> 16) & 0xFF) < 0x40 && (srcc & 0xFF) > 0x40) {
                    /* the PAL case's opaque blues (the curved hinge sections and the like): the case's blue, shaded as the original was */
                    srcc = srcc == 0xFF1E48ADu ? 0xFF4268B4u : 0xFF2A4D9Cu; /* the pill's blue and the case's: flat, the light shades them */
                }
                uint32_t col = tex == BMODEL_TEX_BACK ? 0xFFEAEAEAu /* the back is white paper */ : mul_argb(srcc, rib_axis >= 0 ? 0xFFFFFFFFu : tint);
                if (clear && (!strcmp(part->name, "case_shell") || !strcmp(part->name, "hinge_strip")) && tex < 0) {
                    col = 0x40C4CCD4u; /* clear plastic */
                }
                int g = first;
                while (g < b->npolys && (b->polys[g].diffuse != col || b->polys[g].tex != stex)) {
                    g++;
                }
                if (g == b->npolys) {
                    if (b->npolys >= MAX_POLYS) {
                        b->err = 1;
                        return;
                    }
                    bpoly* np = &b->polys[b->npolys++];
                    np->tex = stex;
                    np->diffuse = col;
                    np->flags = (!ntsc && !strcmp(part->name, "hinge_strip")) ? 0 : STRIP_IGNORE_LIGHT; /* the PAL hinge sections are lit; one-sided: a back face (the rear label seen from the front) must not take part in the PVR's per-triangle sort */
                    np->ntris = 0;
                    np->corners = (nj_corner*)calloc((size_t)ntri * 3, sizeof(nj_corner));
                    if (!np->corners) {
                        b->err = 1;
                        return;
                    }
                }
                bpoly* bp = &b->polys[g];
                for (int t = 0; t + 2 < n; t++) {
                    if (b->nverts + 3 > MAX_VERTS) {
                        b->err = 1;
                        return;
                    }
                    /* a strip alternates its winding: odd triangles are turned so all of them face the same way */
                    const int ord[3] = {(t & 1) ? 1 : 0, (t & 1) ? 0 : 1, 2};
                    v3 p[3];
                    for (int k = 0; k < 3; k++) {
                        const dc_vertex_t* sv = &src[t + ord[k]];
                        p[k] = v3m(sv->x * S, sv->y * S, sv->z * S);
                    }
                    const v3 fn = norm(cross(sub(p[1], p[0]), sub(p[2], p[0])));
                    for (int k = 0; k < 3; k++) {
                        const dc_vertex_t* sv = &src[t + ord[k]];
                        nj_vertex* v = &b->verts[b->nverts];
                        memset(v, 0, sizeof(*v));
                        v->pos = p[k];
                        v->nrm = fn;
                        v->valid = v->has_nrm = 1;
                        nj_corner* c = &bp->corners[bp->ntris * 3 + k];
                        c->idx = (uint16_t)b->nverts++;
                        c->u = rib_axis < 0 ? sv->u : (rib_axis == 0 ? sv->x : sv->y) / RIB_MM;
                        if (rib_axis < 0) {
                            c->v = sv->v;
                        } else if (cax < 0) {
                            c->v = 0.5f;
                        } else {
                            const float cc = cax == 0 ? sv->x : (cax == 1 ? sv->y : sv->z);
                            c->v = 0.25f + ((cc - cmin) - 0.8f) / (cext - 1.6f) * 0.5f;
                        }
                    }
                    bp->ntris++;
                }
                src += n;
            }
        }
    }
}

int
bmodel_build(int id, nj_object* out) {
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    builder b;
    if (builder_start(&b) != 0) {
        return -1;
    }
    switch (id) {
        case BMODEL_PHONE: build_phone(&b); break;
        case BMODEL_CASE_WHITE_DOUBLE:
        case BMODEL_CASE_PAL_DOUBLE:
        case BMODEL_CASE_DARK_DOUBLE:
        case BMODEL_CASE_CLEAR_WHITE_DOUBLE:
        case BMODEL_CASE_CLEAR_BLACK_DOUBLE: build_double_case(&b, bmodel_case_single(id) == BMODEL_CASE_PAL); break;
        case BMODEL_GLOBE: build_globe(&b); break;
        case BMODEL_PODIUM: build_podium(&b); break;
        case BMODEL_CASE_WHITE: build_case(&b, &dc_model_cd_case_white, 0xFF292D31u, 0); break;
        case BMODEL_CASE_DARK: build_case(&b, &dc_model_cd_case_darkgray, 0xFF292D31u, 0); break;
        case BMODEL_CASE_CLEAR_WHITE: build_case(&b, &dc_model_cd_case_white, 0xFF292D31u, 1); break;
        case BMODEL_CASE_CLEAR_BLACK: build_case(&b, &dc_model_cd_case_white, 0xFF292D31u, 2); break;
        case BMODEL_CASE_PAL: build_case(&b, &dc_model_dreamcast_pal_case_blue, 0xFF20419Bu, 0); break;
        default:
            builder_free(&b);
            return -1;
    }
    return builder_finish(&b, out);
}

/* The numbers sit on the blocks' fronts, shrunk to fit, in colours that stand out from the block under them. */
int
bmodel_podium_digits(const struct bios_rom* rom, nj_object* out) {
    static const int digit_model[3] = {22, 23, 24}; /* the CD player's 1, 2 and 3 (ROM models 21 + digit) */
    static const uint32_t digit_col[3] = {0xFF1E5AB4u, 0xFFFFFFFFu, 0xFFFFFFFFu};
    static const float digit_cx[3] = {-0.66f, 0.0f, 0.0f}; /* the 1 is off-centre in its model */
    const float s = 0.38f;
    if (!rom || !out || out->count != 1 || !out->nodes[0].mesh) {
        return -1;
    }
    nj_object d[3];
    for (int i = 0; i < 3; i++) {
        uint32_t addr = bios_model_addr(rom, digit_model[i]);
        if (!addr || nj_object_load(rom, addr, &d[i]) != 0 || d[i].count != 1 || !d[i].nodes[0].mesh) {
            for (int j = 0; j < i; j++) {
                nj_object_free(&d[j]);
            }
            if (addr && i >= 0 && d[i].nodes) {
                nj_object_free(&d[i]);
            }
            return -1;
        }
    }
    nj_node* nodes = (nj_node*)calloc(5, sizeof(nj_node));
    if (!nodes) {
        for (int i = 0; i < 3; i++) {
            nj_object_free(&d[i]);
        }
        return -1;
    }
    /* 0: the tilt (a little turned and leaning back, so the tops show), 1: the stand, 2..4: the numbers */
    nodes[0].scl.x = nodes[0].scl.y = nodes[0].scl.z = 1.0f;
    nodes[0].parent = -1;
    nodes[0].ang[0] = 0x08E4;  /* about 12 degrees: the tops lean toward the viewer */
    nodes[0].ang[1] = -0x038E; /* about -5 degrees: turned, so the side of the stand shows */
    nodes[1] = out->nodes[0];
    nodes[1].parent = 0;
    nodes[1].eval = 0;
    for (int i = 0; i < 3; i++) {
        nj_node n = d[i].nodes[0];
        for (int p = 0; p < n.mesh->npolys; p++) {
            n.mesh->polys[p].diffuse = digit_col[i];
            n.mesh->polys[p].has_diffuse = 1;
            n.mesh->polys[p].tex = -1;
            n.mesh->polys[p].has_uv = 0;
        }
        n.parent = 0;
        n.eval = 0; /* the ROM's own flags (a root's) would ignore the placement */
        n.ang[0] = n.ang[1] = n.ang[2] = 0;
        n.scl.x = n.scl.y = n.scl.z = s;
        n.pos.x = pod_cx[i] - digit_cx[i] * s;
        n.pos.y = POD_BOTTOM + pod_h[i] * 0.5f;
        n.pos.z = POD_BACK + pod_d[i] / 2.0f * (1.0f + (1.0f + POD_TAPER) / 2.0f) - 2.0f * s + 0.02f; /* the digit's back plane on the block's front face */
        nodes[2 + i] = n;
        free(d[i].nodes); /* the node's mesh now belongs to `nodes` */
    }
    free(out->nodes);
    out->nodes = nodes;
    out->count = 5;
    return 0;
}


/* The BACK marker (ROM model 0, node 0): the BIOS's own frame has corners cut differently (the picture's by one amount, the frame's by another),
 * which looks wonky. Here the frame and the picture are rebuilt with four identical rounded corners, same size, same depth, same texture. */
#define BK_SEG 6 /* segments per corner */
#define BK_N (4 * (BK_SEG + 1))
static void
bk_ring(float hx, float hy, float r, float z, float nz, float out_n, nj_vertex* v) {
    for (int k = 0; k < 4; k++) {
        const float cx = (k == 0 || k == 3) ? hx - r : -(hx - r), cy = (k == 0 || k == 1) ? hy - r : -(hy - r);
        for (int j = 0; j <= BK_SEG; j++) {
            const float a = ((float)k + (float)j / (float)BK_SEG) * 1.5707963f, c = cosf(a), sn = sinf(a);
            nj_vertex* q = &v[k * (BK_SEG + 1) + j];
            memset(q, 0, sizeof(*q));
            q->pos = v3m(cx + r * c, cy + r * sn, z);
            q->nrm = v3m(out_n * c, out_n * sn, nz);
            q->valid = q->has_nrm = 1;
        }
    }
}

int
bmodel_fix_back(nj_object* obj) {
    if (!obj || obj->count < 1 || !obj->nodes[0].mesh) {
        return -1;
    }
    nj_mesh* m = obj->nodes[0].mesh;
    if (m->npolys != 2 || m->polys[0].has_uv || !m->polys[1].has_uv || m->polys[1].ntris != 6) {
        return -1; /* not the ROM model this was made for */
    }
    const int nv = 5 * BK_N + 1; /* back, wall front, bevel outer, bevel inner, picture ring, picture centre */
    nj_vertex* verts = (nj_vertex*)calloc((size_t)nv, sizeof(nj_vertex));
    nj_corner* fr = (nj_corner*)calloc((size_t)BK_N * 4 * 3, sizeof(nj_corner)); /* wall + bevel: 2 quads per segment each */
    nj_corner* pic = (nj_corner*)calloc((size_t)BK_N * 3, sizeof(nj_corner));
    if (!verts || !fr || !pic) {
        free(verts);
        free(fr);
        free(pic);
        return -1;
    }
    const float OX = 2.60f, OY = 2.51f, OR = 0.62f, IX = 2.37f, IY = 2.30f, IR = 0.40f;
    bk_ring(OX, OY, OR, -0.36f, 0.0f, 1.0f, verts);
    bk_ring(OX, OY, OR, 0.04f, 0.0f, 1.0f, verts + BK_N);
    bk_ring(OX, OY, OR, 0.04f, 0.6f, 0.8f, verts + 2 * BK_N);
    bk_ring(IX, IY, IR, 0.21f, 0.6f, 0.8f, verts + 3 * BK_N);
    bk_ring(IX, IY, IR, 0.21f, 1.0f, 0.0f, verts + 4 * BK_N);
    nj_vertex* ctr = &verts[5 * BK_N];
    ctr->pos = v3m(0, 0, 0.21f);
    ctr->nrm = v3m(0, 0, 1);
    ctr->valid = ctr->has_nrm = 1;
    int nc = 0;
    for (int i = 0; i < BK_N; i++) {
        const int j = (i + 1) % BK_N;
        const int w[6] = {i, j, BK_N + j, i, BK_N + j, BK_N + i};
        for (int k = 0; k < 6; k++) {
            fr[nc].idx = (uint16_t)w[k];
            fr[nc++].u = 0;
        }
        const int b[6] = {2 * BK_N + i, 2 * BK_N + j, 3 * BK_N + i, 2 * BK_N + j, 3 * BK_N + j, 3 * BK_N + i};
        for (int k = 0; k < 6; k++) {
            fr[nc].idx = (uint16_t)b[k];
            fr[nc++].u = 0;
        }
    }
    int pc = 0;
    for (int i = 0; i < BK_N; i++) {
        const int j = (i + 1) % BK_N;
        const int t[3] = {5 * BK_N, 4 * BK_N + i, 4 * BK_N + j};
        for (int k = 0; k < 3; k++) {
            const nj_vertex* v = &verts[t[k]];
            pic[pc].idx = (uint16_t)t[k];
            pic[pc].u = (v->pos.x + IX) / (2.0f * IX) * 0.98f;
            pic[pc].v = (IY - v->pos.y) / (2.0f * IY) * 0.99f + 0.01f;
            pc++;
        }
    }
    free(m->verts);
    free(m->polys[0].corners);
    free(m->polys[1].corners);
    m->verts = verts;
    m->nverts = nv;
    m->polys[0].corners = fr;
    m->polys[0].ntris = nc / 3;
    m->polys[1].corners = pic;
    m->polys[1].ntris = pc / 3;
    return 0;
}
