/* event_phase_test.   gcc -Wall -Iopenmenu/src -o event_phase_test openmenu/test/event_phase_test.c openmenu/src/event_phase.c && ./event_phase_test */
#include <stdio.h>
#include <string.h>
#include "event_phase.h"
static int bad;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); bad++; } } while (0)
int main(void) {
    int m;
    CHECK(event_phase("2026-11-16 21:00", 2026, 11, 16, 20, 0, &m) == EVP_UPCOMING && m == 60);
    CHECK(event_phase("2026-11-16 20:00", 2026, 11, 16, 20, 0, &m) == EVP_CURRENT && m == 0);
    CHECK(event_phase("2026-11-16 18:00", 2026, 11, 16, 20, 0, &m) == EVP_CURRENT && m == -120);
    CHECK(event_phase("2026-11-16 17:00", 2026, 11, 16, 20, 0, &m) == EVP_PAST && m == -180);
    CHECK(event_phase("2026-11-17 00:30", 2026, 11, 16, 23, 30, &m) == EVP_UPCOMING && m == 60); /* over midnight */
    CHECK(event_phase("2027-01-01 00:00", 2026, 12, 31, 23, 0, &m) == EVP_UPCOMING && m == 60);   /* over the year */
    CHECK(event_phase("2028-03-01 00:00", 2028, 2, 29, 23, 0, &m) == EVP_UPCOMING && m == 60);   /* leap day */
    CHECK(event_phase("garbage", 2026, 11, 16, 20, 0, &m) == EVP_UPCOMING);
    char t[32];
    event_phase_text(25, t, sizeof(t)); CHECK(strcmp(t, "in 25 min") == 0);
    event_phase_text(0, t, sizeof(t)); CHECK(strcmp(t, "starting now") == 0);
    event_phase_text(-40, t, sizeof(t)); CHECK(strcmp(t, "started 40 min ago") == 0);
    event_phase_text(3 * 1440, t, sizeof(t)); CHECK(strcmp(t, "in 3 days") == 0);
    printf(bad ? "%d failed\n" : "event_phase ok\n", bad);
    return bad != 0;
}
