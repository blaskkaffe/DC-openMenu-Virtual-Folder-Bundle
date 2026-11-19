/*
 * pose_man: the little black man of the Joy-Con calibration screen, as flat polygons (low poly, in the style of the Samba de Amigo pose
 * man: black tubes with a white edge, a round head, a glowing blue maraca in each hand). Standing straight, each arm swings up and
 * down with the elevation of its Joy-Con: 0 = level (arms out sideways), +90 straight up, -90 straight down. Plain C (no KallistiOS):
 * the app draws the polygons with gfx, a PC test draws them to a picture (openMenu/tests/host/test_pose_man.c).
 */
#pragma once

#include <stdint.h>

#define PM_MAX_PTS 8
#define PM_MAX_POLYS 128

typedef struct pm_poly {
    float x[PM_MAX_PTS], y[PM_MAX_PTS];
    int n;
    uint32_t argb;
} pm_poly;

/* Builds the man standing with his feet at (cx, base_y) on the 640x480 picture, `scale` 1.0 = 200 pixels tall (head to feet). The polygons are
 * in drawing order (back to front), each a convex polygon of 3 to PM_MAX_PTS points. Returns the count. */
int pose_man_build(float cx, float base_y, float scale, float left_deg, float right_deg, pm_poly* out, int max);

/* The angle guides: tick marks and a dot per arm, as polygons too (thin quads). */
int pose_man_guides(float cx, float base_y, float scale, float left_deg, float right_deg, pm_poly* out, int max);
