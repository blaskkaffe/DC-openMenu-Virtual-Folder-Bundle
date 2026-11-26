/* clock_math: calendar arithmetic without any console dependency, see clock_math.h. */
#include <stdio.h>

#include "clock_math.h"

/* Days since 1970-01-01 of a calendar date (Howard Hinnant's days_from_civil). */
static long long
days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

long long
clock_unix_secs(int year, int month, int day, int hour, int minute) {
    return days_from_civil(year, month, day) * 86400LL + hour * 3600LL + minute * 60LL;
}

int
clock_beats(int hour, int minute, int utc_offset_min) {
    int utc = hour * 60 + minute - utc_offset_min;
    int bmt = ((utc + 60) % 1440 + 1440) % 1440;       /* minutes since midnight in Biel */
    return (bmt * 600 + 300) / 864 % 1000;             /* (seconds * 10) / 864, the middle of the minute */
}

void
clock_header_text(char* out, unsigned size, int language, int pal, int hour12, int beats_on, int year, int month, int day, int hour, int minute,
                  int utc_offset_min, int offset_known) {
    /* As the BIOS's format_datetime_string (0x8C01BAE0): always a 24 hour clock; the date by the BIOS language, English by the console's
     * region. Japanese 2026/10/03, English (America, Japan) 10/03/2026, English (PAL) 03/10/2026, German 03.10.2026, French, Spanish,
     * Italian 03/10/2026. */
    int first = month, second = day, year_first = 0; /* the two numbers before (or after) the year */
    char sep = '/';
    switch (language) {
        case 1: /* English */
            if (pal) {
                first = day, second = month;
            }
            break;
        case 2: /* German */
            first = day, second = month, sep = '.';
            break;
        case 3:
        case 4:
        case 5: /* French, Spanish, Italian */
            first = day, second = month;
            break;
        default: /* Japanese (and anything else, as the BIOS) */
            year_first = 1;
            break;
    }
    char date[24], time[16];
    const int beats = beats_on && offset_known;
    /* 12 hour (the shared Clock setting, openMenu 1.7.1's): a two digit year (with beats one letter A/P) so the line fits the header */
    const int y = hour12 ? year % 100 : year;
    const char* yf = hour12 ? "%02d%c%02d%c%02d" : "%04d%c%02d%c%02d";
    const char* yl = hour12 ? "%02d%c%02d%c%02d" : "%02d%c%02d%c%04d";
    if (beats) { /* this menu's addition: Swatch .beat time after the clock; no year, so the line fits the header */
        snprintf(date, sizeof(date), "%02d%c%02d", first, sep, second);
    } else if (year_first) {
        snprintf(date, sizeof(date), yf, y, sep, first, sep, second);
    } else {
        snprintf(date, sizeof(date), yl, first, sep, second, sep, y);
    }
    if (hour12) {
        const int h12 = hour % 12 == 0 ? 12 : hour % 12;
        if (beats) {
            snprintf(time, sizeof(time), "%d:%02d%c", h12, minute, hour < 12 ? 'A' : 'P');
        } else {
            snprintf(time, sizeof(time), "%d:%02d %s", h12, minute, hour < 12 ? "AM" : "PM");
        }
    } else {
        snprintf(time, sizeof(time), "%02d:%02d", hour, minute);
    }
    if (beats) {
        snprintf(out, size, "%s %s .%03d", date, time, clock_beats(hour, minute, utc_offset_min));
    } else {
        snprintf(out, size, "%s %s", date, time);
    }
}
