/* clock_math: calendar arithmetic for the date and time editor. */
#pragma once

/* Seconds since 1970 for a calendar date (no leap seconds); negative before 1970. */
long long clock_unix_secs(int year, int month, int day, int hour, int minute);

/* Swatch Internet Time ("beats"): 1000 a day, from midnight in Biel (UTC+1). `utc_offset_min` is the console clock's offset from UTC
 * in minutes (east positive), the minute counts as its middle second. 0..999. */
int clock_beats(int hour, int minute, int utc_offset_min);

/* The text of the main menu's clock, as the BIOS writes it: 24 hour time, the date by the BIOS language (0 Japanese, 1 English, 2 German,
 * 3 French, 4 Spanish, 5 Italian) and, for English, the console's region (pal: day first). With beats_on (this menu's .beat setting) and
 * the time zone known, the year is left out and the .beat time follows. hour12: the shared Clock setting's 12 hour choice (AM/PM, a two
 * digit year). At most 17 characters. */
void clock_header_text(char* out, unsigned size, int language, int pal, int hour12, int beats_on, int year, int month, int day, int hour, int minute,
                       int utc_offset_min, int offset_known);
