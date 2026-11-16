/*
 * event_phase: where an event stands against the clock: coming up, going on, or over. Pure logic (host-tested, test/event_phase_test.c).
 * The sources give a start time and no end, so an event counts as going on for EVENT_LENGTH_MIN after its start (an assumption, not data).
 * Times are the sort key of events_fetch.h ("YYYY-MM-DD HH:MM") against the console's clock, which the menu treats as local time.
 */
#pragma once

#define EVENT_LENGTH_MIN 180

enum { EVP_PAST = 0, EVP_CURRENT, EVP_UPCOMING };

/* Minutes from the clock to the start (negative: started that long ago), or a very large value (INT_MAX) when the key cannot be read. */
int event_minutes_to(const char* sort_key, int y, int mo, int d, int h, int mi);
/* EVP_*, and the minutes to the start through `minutes` (may be NULL). */
int event_phase(const char* sort_key, int y, int mo, int d, int h, int mi, int* minutes);
/* "in 25 min", "in 3 h", "tomorrow", "in 4 days", "started 40 min ago", "starting now" */
void event_phase_text(int minutes, char* out, int size);
