/* Host test of the calibration man (pose_man.c): the arms follow the Joy-Con angles, the rest stands still. */
#include <math.h>
#include <stdio.h>
#include "pose_man.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static pm_scene sc;

int main(void) {
    const float cx = 320, by = 322, shoulder_y = 322 + (452.0f - 1346.0f) * 200.0f / (1346.0f - 142.0f);

    pose_man_build(cx, by, 1.0f, 0, 0, &sc);
    CHECK(sc.nspr == 4 && sc.ntri > 100 && sc.ntri < PM_MAX_TRIS);
    CHECK(sc.spr[0].id == PM_SPR_ARM_L && sc.spr[1].id == PM_SPR_ARM_R && sc.spr[2].id == PM_SPR_LEG_L && sc.spr[3].id == PM_SPR_LEG_R);
    CHECK(sc.hand_x[0] < cx - 50 && sc.hand_x[1] > cx + 50);                 /* level: the hands out to the sides */
    CHECK(fabsf(sc.hand_y[0] - shoulder_y) < 3.0f && fabsf(sc.hand_y[1] - shoulder_y) < 3.0f);
    CHECK(fabsf((cx - sc.hand_x[0]) - (sc.hand_x[1] - cx)) < 3.0f);          /* roughly mirrored (the shoulders are 5 pixels off the middle) */

    pose_man_build(cx, by, 1.0f, 90, -90, &sc);
    CHECK(sc.hand_y[0] < shoulder_y - 60);                                    /* the left one up */
    CHECK(sc.hand_y[1] > shoulder_y + 60);                                    /* the right one down */
    CHECK(fabsf(sc.hand_x[0] - cx) < 40 && fabsf(sc.hand_x[1] - cx) < 40);

    pose_man_build(cx, by, 1.0f, 45, 45, &sc);
    CHECK(sc.hand_y[0] < shoulder_y - 30 && fabsf(sc.hand_y[0] - sc.hand_y[1]) < 2.0f);

    /* the arm sprites keep their shape (a rotation: the sides stay the same length) */
    for (int a = 0; a < 2; a++) {
        const pm_sprite* s = &sc.spr[a];
        float w = hypotf(s->x[1] - s->x[0], s->y[1] - s->y[0]), w2 = hypotf(s->x[2] - s->x[3], s->y[2] - s->y[3]);
        float h = hypotf(s->x[3] - s->x[0], s->y[3] - s->y[0]);
        CHECK(fabsf(w - w2) < 0.01f && w > 20 && h > 50);
    }
    /* the legs are not turned: straight rectangles under the body, feet on the base line */
    for (int l = 2; l < 4; l++) {
        const pm_sprite* s = &sc.spr[l];
        CHECK(s->x[0] == s->x[3] && s->y[0] == s->y[1] && fabsf(s->y[2] - by) < 1.5f);
    }
    /* the globes sit on the middle and stay inside the picture */
    for (int i = 0; i < sc.ntri; i++) {
        for (int k = 0; k < 3; k++) {
            CHECK(sc.tri[i].v[k].x > 0 && sc.tri[i].v[k].x < 640 && sc.tri[i].v[k].y > 0 && sc.tri[i].v[k].y < 480);
        }
    }
    {
        static pm_tri g[64];
        int n = pose_man_guides(cx, by, 1.0f, 10, -10, g, 64);
        CHECK(n > 10 && n <= 64);
        CHECK(pose_man_guides(cx, by, 1.0f, 0, 0, g, 3) <= 3);
    }

    /* the arm picture: the arm is in it, turned the right way (the hand end follows the angle), the shoulder is in the middle */
    {
        static uint16_t img[PM_ARM_IMG * PM_ARM_IMG];
        float f = pose_man_ref_scale(0.9f);
        for (int side = 0; side < 2; side++) {
            for (int deg = -90; deg <= 90; deg += 45) {
                long sx_ = 0, sy_ = 0, n = 0;
                pose_man_arm_image(side, (float)deg, f, img);
                for (int y = 0; y < PM_ARM_IMG; y++) {
                    for (int x = 0; x < PM_ARM_IMG; x++) {
                        if ((img[y * PM_ARM_IMG + x] >> 12) > 8) { sx_ += x; sy_ += y; n++; }
                    }
                }
                CHECK(n > 150);                                         /* something is drawn, and not all of the picture */
                CHECK(n < PM_ARM_IMG * PM_ARM_IMG / 3);
                {
                    float mx = (float)sx_ / (float)n - PM_ARM_IMG / 2, my = (float)sy_ / (float)n - PM_ARM_IMG / 2;
                    float out = side ? mx : -mx;                         /* outwards of the body is positive */
                    if (deg == 90) { CHECK(my < -10); }                  /* up: the arm is above the shoulder */
                    if (deg == -90) { CHECK(my > 10); }
                    if (deg == 0) { CHECK(out > 10 && fabsf(my) < 12); } /* level: out to the side, at shoulder height */
                }
            }
        }
        {
            int w, h;
            CHECK(pose_man_leg_image(0, &w, &h) != NULL && w > 40 && h > 80);
            CHECK(pose_man_leg_image(1, &w, &h) != NULL && w > 40 && h > 80);
        }
    }
    /* a low polygon count: under 250 triangles for the whole man at any angle */
    pose_man_build(cx, by, 1.0f, 30, -60, &sc);
    CHECK(sc.ntri < 250);

    printf("pose_man: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
