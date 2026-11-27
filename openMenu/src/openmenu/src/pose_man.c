/* pose_man: see pose_man.h. */
#include <math.h>
#include <string.h>

#include "pose_man.h"
#include "pose_man_tex.inc"

#define MATTE_R 13
#define MATTE_G 19
#define MATTE_B 28
#define CREAM 0xFFF3F0D6u
#define PI_F 3.14159265f

/* the boxes of the reference picture (see tools/pose-man/make_sprites.py) */
const pm_sprite_def pm_sprite_defs[PM_SPR_COUNT] = {
    {324, 413, 224, 440}, /* left arm: from the shoulder to the hand, hanging */
    {658, 416, 217, 437}, /* right arm */
    {295, 790, 308, 560}, /* left leg and shoe */
    {603, 790, 309, 560}, /* right leg and shoe */
};

/* geometry in the reference picture */
#define REF_TOP 142.0f   /* top of the head */
#define REF_BOTTOM 1346.0f /* bottom of the shoes */
#define REF_CX 602.5f
#define HEAD_CY 266.0f
#define HEAD_RX 67.5f
#define HEAD_RY 124.0f
#define BODY_CY 602.0f
#define BODY_RX 52.5f
#define BODY_RY 208.0f
#define RIM 8.0f
static const float shoulder_x[2] = {548.0f, 657.0f}, shoulder_y = 452.0f; /* where the arm turns */
static const float hand_x_ref[2] = {399.0f, 800.0f}, hand_y_ref = 782.0f;   /* the hand of the hanging arm */

typedef struct tr {
    float cx, base_y, f; /* screen = (ref - REF_CX) * f + cx ... */
} tr;

static float sx(const tr* t, float rx) { return t->cx + (rx - REF_CX) * t->f; }
static float sy(const tr* t, float ry) { return t->base_y + (ry - REF_BOTTOM) * t->f; }

static void
push(pm_scene* o, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c0, uint32_t c1, uint32_t c2) {
    if (o->ntri >= PM_MAX_TRIS) {
        return;
    }
    pm_tri* t = &o->tri[o->ntri++];
    t->v[0] = (pm_vert){x0, y0, c0};
    t->v[1] = (pm_vert){x1, y1, c1};
    t->v[2] = (pm_vert){x2, y2, c2};
}

/* the matte colour of a globe point whose surface looks `nz` towards us (1 = straight at us): the rim a little darker, no highlight */
static uint32_t
shade(float nz) {
    float k = 0.78f + 0.40f * nz; /* 0.78 at the rim, 1.18 in the middle */
    int r = (int)(MATTE_R * k + 0.5f), g = (int)(MATTE_G * k + 0.5f), b = (int)(MATTE_B * k + 0.5f);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* The front half of an ellipsoid (x radius rx, y radius ry, depth rx) as triangles: 3 rings from the middle to the rim, 14 points a ring (70
 * triangles: smooth enough at 640x480, where the head is 40 pixels tall). With `flat` it is a single fan of 14 triangles in the cream of the rim
 * (the larger globe behind), otherwise the matte shade. */
static void
globe(pm_scene* o, float cx, float cy, float rx, float ry, int flat) {
    enum { RINGS = 3, SEG = 14 };
    if (flat) {
        for (int k = 0; k < SEG; k++) {
            float t0 = 2.0f * PI_F * (float)k / SEG, t1 = 2.0f * PI_F * (float)(k + 1) / SEG;
            push(o, cx, cy, cx + rx * cosf(t0), cy + ry * sinf(t0), cx + rx * cosf(t1), cy + ry * sinf(t1), CREAM, CREAM, CREAM);
        }
        return;
    }
    for (int r = 0; r < RINGS; r++) {
        float a0 = (PI_F / 2.0f) * (float)r / RINGS, a1 = (PI_F / 2.0f) * (float)(r + 1) / RINGS; /* from the front pole outwards */
        float s0 = sinf(a0), s1 = sinf(a1), n0 = cosf(a0), n1 = cosf(a1);
        for (int k = 0; k < SEG; k++) {
            float t0 = 2.0f * PI_F * (float)k / SEG, t1 = 2.0f * PI_F * (float)(k + 1) / SEG;
            float x00 = cx + rx * s0 * cosf(t0), y00 = cy + ry * s0 * sinf(t0);
            float x01 = cx + rx * s0 * cosf(t1), y01 = cy + ry * s0 * sinf(t1);
            float x10 = cx + rx * s1 * cosf(t0), y10 = cy + ry * s1 * sinf(t0);
            float x11 = cx + rx * s1 * cosf(t1), y11 = cy + ry * s1 * sinf(t1);
            uint32_t c00 = flat ? CREAM : shade(n0), c10 = flat ? CREAM : shade(n1);
            if (r == 0) {
                push(o, cx, cy, x10, y10, x11, y11, c00, c10, c10); /* the pole: one triangle */
            } else {
                push(o, x00, y00, x10, y10, x11, y11, c00, c10, c10);
                push(o, x00, y00, x11, y11, x01, y01, c00, c10, c00);
            }
        }
    }
}

static void
maraca(pm_scene* o, float x, float y, float r) {
    static const uint32_t ball[6] = {0xFF9FE3FFu, 0xFF5FB8F0u, 0xFF2F86D8u, 0xFF1D5FB8u, 0xFF3F9FE8u, 0xFF7FD2FBu};
    const uint32_t glow = 0x302F9FE8u;
    for (int i = 0; i < 8; i++) { /* the glow: a soft octagon, translucent, twice the ball */
        float a0 = 2.0f * PI_F * (float)i / 8.0f, a1 = 2.0f * PI_F * (float)(i + 1) / 8.0f;
        push(o, x, y, x + 2.0f * r * cosf(a0), y + 2.0f * r * sinf(a0), x + 2.0f * r * cosf(a1), y + 2.0f * r * sinf(a1), glow, glow, glow);
    }
    for (int i = 0; i < 6; i++) { /* the ball: six facets with a white rim */
        float a0 = (15.0f + 60.0f * (float)i) * PI_F / 180.0f, a1 = (15.0f + 60.0f * (float)(i + 1)) * PI_F / 180.0f;
        push(o, x, y, x + (r + 1.6f) * cosf(a0), y + (r + 1.6f) * sinf(a0), x + (r + 1.6f) * cosf(a1), y + (r + 1.6f) * sinf(a1), CREAM, CREAM, CREAM);
    }
    for (int i = 0; i < 6; i++) {
        float a0 = (15.0f + 60.0f * (float)i) * PI_F / 180.0f, a1 = (15.0f + 60.0f * (float)(i + 1)) * PI_F / 180.0f;
        push(o, x, y, x + r * cosf(a0), y + r * sinf(a0), x + r * cosf(a1), y + r * sinf(a1), ball[i], ball[i], ball[i]);
    }
}

void
pose_man_build(float cx, float base_y, float s, float left_deg, float right_deg, pm_scene* out) {
    tr t = {cx, base_y, 200.0f * s / (REF_BOTTOM - REF_TOP)};
    const float f = t.f;

    memset(out, 0, sizeof(*out));
    /* the arms: the sprite turns about the shoulder until the hand points where the Joy-Con does */
    for (int side = 0; side < 2; side++) {
        float deg = side == 0 ? left_deg : right_deg, th = deg * PI_F / 180.0f;
        float dir = side == 0 ? -1.0f : 1.0f;
        float vx = hand_x_ref[side] - shoulder_x[side], vy = hand_y_ref - shoulder_y; /* the hanging arm, in the picture */
        float dx = dir * cosf(th), dy = -sinf(th);                                        /* where it has to point, y down */
        float phi = atan2f(dy, dx) - atan2f(vy, vx), c = cosf(phi), sn = sinf(phi);
        const pm_sprite_def* d = &pm_sprite_defs[side == 0 ? PM_SPR_ARM_L : PM_SPR_ARM_R];
        float cxr[4] = {(float)d->x, (float)(d->x + d->w), (float)(d->x + d->w), (float)d->x};
        float cyr[4] = {(float)d->y, (float)d->y, (float)(d->y + d->h), (float)(d->y + d->h)};
        float len = sqrtf(vx * vx + vy * vy);
        pm_sprite* sp = &out->spr[out->nspr++];
        float px = sx(&t, shoulder_x[side]), py = sy(&t, shoulder_y);
        sp->id = side == 0 ? PM_SPR_ARM_L : PM_SPR_ARM_R;
        for (int k = 0; k < 4; k++) {
            float rx = cxr[k] - shoulder_x[side], ry = cyr[k] - shoulder_y;
            sp->x[k] = px + (rx * c - ry * sn) * f;
            sp->y[k] = py + (rx * sn + ry * c) * f;
        }
        out->hand_x[side] = px + dx * (len + 60.0f) * f; /* the maraca sits beyond the hand */
        out->hand_y[side] = py + dy * (len + 60.0f) * f;
    }
    /* the legs, as they are */
    for (int side = 0; side < 2; side++) {
        const pm_sprite_def* d = &pm_sprite_defs[side == 0 ? PM_SPR_LEG_L : PM_SPR_LEG_R];
        pm_sprite* sp = &out->spr[out->nspr++];
        sp->id = side == 0 ? PM_SPR_LEG_L : PM_SPR_LEG_R;
        sp->x[0] = sp->x[3] = sx(&t, (float)d->x);
        sp->x[1] = sp->x[2] = sx(&t, (float)(d->x + d->w));
        sp->y[0] = sp->y[1] = sy(&t, (float)d->y);
        sp->y[2] = sp->y[3] = sy(&t, (float)(d->y + d->h));
    }
    /* the body and the head: a cream globe a little larger behind each, then the matte globe */
    globe(out, sx(&t, REF_CX), sy(&t, BODY_CY), (BODY_RX + RIM) * f, (BODY_RY + RIM) * f, 1);
    globe(out, sx(&t, REF_CX), sy(&t, BODY_CY), BODY_RX * f, BODY_RY * f, 0);
    globe(out, sx(&t, REF_CX), sy(&t, HEAD_CY), (HEAD_RX + RIM) * f, (HEAD_RY + RIM) * f, 1);
    globe(out, sx(&t, REF_CX), sy(&t, HEAD_CY), HEAD_RX * f, HEAD_RY * f, 0);
    for (int side = 0; side < 2; side++) {
        maraca(out, out->hand_x[side], out->hand_y[side], 70.0f * f);
    }
}

int
pose_man_guides(float cx, float base_y, float s, float left_deg, float right_deg, pm_tri* out, int max) {
    tr t = {cx, base_y, 200.0f * s / (REF_BOTTOM - REF_TOP)};
    int n = 0;
    for (int side = 0; side < 2; side++) {
        float dir = side == 0 ? -1.0f : 1.0f, px = sx(&t, shoulder_x[side]), py = sy(&t, shoulder_y);
        float R = (hand_y_ref - shoulder_y) * 1.2f * t.f + 6.0f; /* a little beyond the hand */
        float deg = side == 0 ? left_deg : right_deg;
        for (int a = -90; a <= 90; a += 45) {
            float th = (float)a * PI_F / 180.0f, dx = dir * cosf(th), dy = -sinf(th);
            float x0 = px + dx * (R - 5), y0 = py + dy * (R - 5), x1 = px + dx * (R + 5), y1 = py + dy * (R + 5);
            float w = a == 0 ? 1.4f : 0.8f, nx = -dy * w, ny = dx * w;
            uint32_t col = a == 0 ? 0xC0FFFFFFu : 0x60FFFFFFu;
            if (n + 2 > max) {
                return n;
            }
            out[n++] = (pm_tri){{{x0 + nx, y0 + ny, col}, {x1 + nx, y1 + ny, col}, {x1 - nx, y1 - ny, col}}};
            out[n++] = (pm_tri){{{x0 + nx, y0 + ny, col}, {x1 - nx, y1 - ny, col}, {x0 - nx, y0 - ny, col}}};
        }
        {
            float th = deg * PI_F / 180.0f, gx = px + dir * cosf(th) * R, gy = py - sinf(th) * R;
            for (int i = 0; i < 6 && n < max; i++) {
                float a0 = (float)i * PI_F / 3.0f, a1 = (float)(i + 1) * PI_F / 3.0f;
                out[n++] = (pm_tri){{{gx, gy, 0xFFFFE58Au}, {gx + 4.5f * cosf(a0), gy + 4.5f * sinf(a0), 0xFFFFE58Au}, {gx + 4.5f * cosf(a1), gy + 4.5f * sinf(a1), 0xFFFFE58Au}}};
            }
        }
    }
    return n;
}

float
pose_man_ref_scale(float scale) {
    return 200.0f * scale / (REF_BOTTOM - REF_TOP);
}

void
pose_man_shoulder(float cx, float base_y, float scale, int side, float* x, float* y) {
    tr t = {cx, base_y, pose_man_ref_scale(scale)};
    *x = sx(&t, shoulder_x[side ? 1 : 0]);
    *y = sy(&t, shoulder_y);
}

void
pose_man_leg_rect(float cx, float base_y, float scale, int side, float* x, float* y, float* w, float* h) {
    tr t = {cx, base_y, pose_man_ref_scale(scale)};
    const pm_sprite_def* d = &pm_sprite_defs[side ? PM_SPR_LEG_R : PM_SPR_LEG_L];
    *x = sx(&t, (float)d->x);
    *y = sy(&t, (float)d->y);
    *w = (float)d->w * t.f;
    *h = (float)d->h * t.f;
}

const uint16_t*
pose_man_leg_image(int side, int* w, int* h) {
    *w = side ? PM_TEX_LEG_R_W : PM_TEX_LEG_L_W;
    *h = side ? PM_TEX_LEG_R_H : PM_TEX_LEG_L_H;
    return side ? pm_tex_leg_r : pm_tex_leg_l;
}

const uint16_t*
pose_man_arm_tex(int side, int* w, int* h) {
    *w = side ? PM_TEX_ARM_R_W : PM_TEX_ARM_L_W;
    *h = side ? PM_TEX_ARM_R_H : PM_TEX_ARM_L_H;
    return side ? pm_tex_arm_r : pm_tex_arm_l;
}

/* Where the corners of the arm picture go for arm `side` turned to `deg`, as offsets from the shoulder in screen pixels: top left,
 * top right, bottom left, bottom right (a strip's order). The inverse of the turn pose_man_arm_image() undoes per pixel: the PVR then
 * turns the picture (no picture is made per angle). */
void
pose_man_arm_quad(int side, float deg, float f, float ox[4], float oy[4]) {
    const int tw = side ? PM_TEX_ARM_R_W : PM_TEX_ARM_L_W, th = side ? PM_TEX_ARM_R_H : PM_TEX_ARM_L_H;
    const pm_sprite_def* d = &pm_sprite_defs[side ? PM_SPR_ARM_R : PM_SPR_ARM_L];
    float th_rad = deg * PI_F / 180.0f, dir = side ? 1.0f : -1.0f;
    float vx = hand_x_ref[side ? 1 : 0] - shoulder_x[side ? 1 : 0], vy = hand_y_ref - shoulder_y;
    float dx = dir * cosf(th_rad), dy = -sinf(th_rad);
    float phi = atan2f(dy, dx) - atan2f(vy, vx), c = cosf(phi), s = sinf(phi);
    float shx = (shoulder_x[side ? 1 : 0] - (float)d->x) * PM_TEXF, shy = (shoulder_y - (float)d->y) * PM_TEXF;
    float k = PM_TEXF / f;
    static const float cu[4] = {0.0f, 1.0f, 0.0f, 1.0f}, cv[4] = {0.0f, 0.0f, 1.0f, 1.0f};

    for (int i = 0; i < 4; i++) {
        float du = cu[i] * (float)tw - shx, dv = cv[i] * (float)th - shy;
        ox[i] = (c * du - s * dv) / k;
        oy[i] = (s * du + c * dv) / k;
    }
}

/* the arm turned about the shoulder into a picture: for each pixel the place in the arm picture it comes from (nearest) */
void
pose_man_arm_image(int side, float deg, float f, uint16_t* out) {
    const uint16_t* tex = side ? pm_tex_arm_r : pm_tex_arm_l;
    const int tw = side ? PM_TEX_ARM_R_W : PM_TEX_ARM_L_W, th = side ? PM_TEX_ARM_R_H : PM_TEX_ARM_L_H;
    const pm_sprite_def* d = &pm_sprite_defs[side ? PM_SPR_ARM_R : PM_SPR_ARM_L];
    float th_rad = deg * PI_F / 180.0f, dir = side ? 1.0f : -1.0f;
    float vx = hand_x_ref[side ? 1 : 0] - shoulder_x[side ? 1 : 0], vy = hand_y_ref - shoulder_y;
    float dx = dir * cosf(th_rad), dy = -sinf(th_rad);
    float phi = atan2f(dy, dx) - atan2f(vy, vx), c = cosf(phi), s = sinf(phi);
    /* the shoulder in the arm picture, in texture pixels */
    float shx = (shoulder_x[side ? 1 : 0] - (float)d->x) * PM_TEXF, shy = (shoulder_y - (float)d->y) * PM_TEXF;
    float k = PM_TEXF / f; /* screen pixels to texture pixels */

    for (int y = 0; y < PM_ARM_IMG; y++) {
        for (int x = 0; x < PM_ARM_IMG; x++) {
            float ox = ((float)x + 0.5f - PM_ARM_IMG / 2) * k, oy = ((float)y + 0.5f - PM_ARM_IMG / 2) * k;
            float u = shx + (ox * c + oy * s), v = shy + (-ox * s + oy * c); /* the turn undone */
            int iu = (int)floorf(u), iv = (int)floorf(v);
            out[y * PM_ARM_IMG + x] = (iu >= 0 && iu < tw && iv >= 0 && iv < th) ? tex[iv * tw + iu] : 0;
        }
    }
}
