/* event_phase: see event_phase.h. */
#include <limits.h>
#include <stdio.h>

#include "event_phase.h"

static long
days_from_civil(int y, int m, int d) { /* days since 1970-01-01 (proleptic Gregorian) */
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int
event_minutes_to(const char* key, int y, int mo, int d, int h, int mi) {
    int ky, kmo, kd, kh, kmi;
    if (!key || sscanf(key, "%d-%d-%d %d:%d", &ky, &kmo, &kd, &kh, &kmi) != 5 || kmo < 1 || kmo > 12 || kd < 1 || kd > 31) {
        return INT_MAX;
    }
    long a = days_from_civil(ky, kmo, kd) * 1440L + kh * 60L + kmi;
    long b = days_from_civil(y, mo, d) * 1440L + h * 60L + mi;
    long diff = a - b;
    return diff > INT_MAX / 2 ? INT_MAX / 2 : diff < -(INT_MAX / 2) ? -(INT_MAX / 2) : (int)diff;
}

int
event_phase(const char* key, int y, int mo, int d, int h, int mi, int* minutes) {
    int m = event_minutes_to(key, y, mo, d, h, mi);
    if (minutes) {
        *minutes = m;
    }
    if (m == INT_MAX) {
        return EVP_UPCOMING; /* unreadable time: keep it in the list rather than lose it */
    }
    if (m > 0) {
        return EVP_UPCOMING;
    }
    return -m < EVENT_LENGTH_MIN ? EVP_CURRENT : EVP_PAST;
}

void
event_phase_text(int m, char* out, int size) {
    if (m == INT_MAX) {
        snprintf(out, (size_t)size, "time unknown");
    } else if (m > 2 * 1440) {
        snprintf(out, (size_t)size, "in %d days", m / 1440);
    } else if (m > 1440) {
        snprintf(out, (size_t)size, "in %d h", m / 60);
    } else if (m >= 120) {
        snprintf(out, (size_t)size, "in %d h", m / 60);
    } else if (m > 0) {
        snprintf(out, (size_t)size, "in %d min", m);
    } else if (m == 0) {
        snprintf(out, (size_t)size, "starting now");
    } else if (m > -120) {
        snprintf(out, (size_t)size, "started %d min ago", -m);
    } else {
        snprintf(out, (size_t)size, "started %d h ago", -m / 60);
    }
}
