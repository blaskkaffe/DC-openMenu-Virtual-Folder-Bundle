/* Host test of the calibration man (pose_man.c): the hands follow the Joy-Con angles. */
#include <math.h>
#include <stdio.h>
#include "pose_man.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* the centres of the two maracas (the translucent glow octagons, in drawing order: left, right) */
static int hands(const pm_poly* p, int n, float hx[2], float hy[2]) {
    int found = 0, last = 0;
    for (int i = 0; i < n && found < 2; i++) {
        if (p[i].n == 3 && p[i].argb == 0x302F9FE8u && !last) { /* the first triangle of a glow: its first point is the centre */
            hx[found] = p[i].x[0]; hy[found] = p[i].y[0]; found++;
            last = 8;
        }
        if (last) { last--; }
    }
    return found;
}

int main(void) {
    static pm_poly p[PM_MAX_POLYS];
    float hx[2], hy[2];
    int n;

    n = pose_man_build(320, 322, 1.0f, 0, 0, p, PM_MAX_POLYS);
    CHECK(n > 20 && n < PM_MAX_POLYS);
    CHECK(hands(p, n, hx, hy) == 2);
    CHECK(hx[0] < 320 - 60 && hx[1] > 320 + 60);            /* level: the hands out to the sides */
    CHECK(fabsf(hy[0] - hy[1]) < 1.0f && fabsf(hy[0] - (322 - 156)) < 3.0f); /* at shoulder height */

    n = pose_man_build(320, 322, 1.0f, 90, -90, p, PM_MAX_POLYS);
    CHECK(hands(p, n, hx, hy) == 2);
    CHECK(hy[0] < 322 - 156 - 60);                          /* the left one up */
    CHECK(hy[1] > 322 - 156 + 60);                          /* the right one down */

    n = pose_man_build(320, 322, 1.0f, 45, 45, p, PM_MAX_POLYS);
    hands(p, n, hx, hy);
    CHECK(hy[0] < 322 - 156 - 30 && fabsf(hy[0] - hy[1]) < 1.0f && fabsf((320 - hx[0]) - (hx[1] - 320)) < 1.0f); /* mirrored */

    for (int i = 0; i < n; i++) {
        CHECK(p[i].n >= 3 && p[i].n <= PM_MAX_PTS);
    }
    n = pose_man_guides(320, 322, 1.0f, 10, -10, p, PM_MAX_POLYS);
    CHECK(n > 10 && n < PM_MAX_POLYS);
    n = pose_man_build(320, 322, 1.0f, 0, 0, p, 5); /* a full list is cut, not overrun */
    CHECK(n == 5);

    printf("pose_man: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
