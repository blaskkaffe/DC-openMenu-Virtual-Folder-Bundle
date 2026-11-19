/*
 * pose_man: the little black man of the Joy-Con calibration screen, drawn like the Samba de Amigo pose man but matte (no gloss).
 * One sided 3D: the head and the body are the front halves of stretched globes (triangles, no back, a cream rim made by a slightly larger
 * globe behind), the arms and the legs are sprites cut from the reference picture (pm_sprite_defs: where each piece is in it, matte version,
 * tools/pose-man/make_sprites.py makes the four pictures). Each arm swings up and down with its Joy-Con: the arm sprite turns about the
 * shoulder, and a blue maraca sits beyond the hand. Plain C, no KallistiOS: the app draws the triangles and the textured quads with gfx, a PC test
 * draws them to a picture (openMenu/tests/host/test_pose_man.c).
 */
#pragma once

#include <stdint.h>

typedef struct pm_vert {
    float x, y;
    uint32_t argb;
} pm_vert;

typedef struct pm_tri {
    pm_vert v[3];
} pm_tri;

/* The pieces cut from the reference picture (1206 x 1521). The pictures are matte copies of those boxes: the outside is transparent. */
enum { PM_SPR_ARM_L, PM_SPR_ARM_R, PM_SPR_LEG_L, PM_SPR_LEG_R, PM_SPR_COUNT };

typedef struct pm_sprite_def {
    int x, y, w, h; /* the box in the reference picture */
} pm_sprite_def;

extern const pm_sprite_def pm_sprite_defs[PM_SPR_COUNT];

typedef struct pm_sprite {
    int id;           /* PM_SPR_* */
    float x[4], y[4]; /* where the corners of the picture go on the screen: top left, top right, bottom right, bottom left */
} pm_sprite;

#define PM_MAX_TRIS 640
#define PM_MAX_SPRITES 4

typedef struct pm_scene {
    pm_sprite spr[PM_MAX_SPRITES]; /* draw these first, in this order (arms behind the body, then the legs) */
    int nspr;
    pm_tri tri[PM_MAX_TRIS];       /* then these in order: the body, the head, then the maracas (glow and ball) */
    int ntri;
    float hand_x[2], hand_y[2];    /* the centre of each maraca (left, right) */
} pm_scene;

/* The man standing with his feet at (cx, base_y) on the 640x480 picture, `scale` 1.0 = 200 pixels from head to feet. left_deg and right_deg:
 * how far above (+) or below (-) level each Joy-Con points, 0 = arm out to the side, 90 straight up, -90 straight down. */
void pose_man_build(float cx, float base_y, float scale, float left_deg, float right_deg, pm_scene* out);

/* The angle guides (ticks and the dot of each arm) as triangles. Returns the count; out holds max. */
int pose_man_guides(float cx, float base_y, float scale, float left_deg, float right_deg, pm_tri* out, int max);
