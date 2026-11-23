/*
 * File: lb_parse.h
 * Project: openmenu
 * Leaderboard pages (DreamcastLive and the boards it embeds) as lines of text for the Internet screen: every heading (h1..h4) is a heading
 * line, every table row is one line (its cells joined, the "Flag" column left out) and every list item is one line. Everything else on the
 * page (menus, prose) is left out. No console dependencies, host-tested (test/lb_parse_test.c).
 */
#pragma once

#define LB_LINES_MAX 320
#define LB_LINE_LEN  72

enum { LB_ROW = 0, LB_HEAD = 1, LB_COLS = 2 }; /* a score line, a heading, the column titles of a table */

typedef struct lb_line {
    char text[LB_LINE_LEN];
    unsigned char kind;
} lb_line_t;

/* Parses `html` (NUL terminated) into out[0..max). Returns the number of lines. */
int lb_parse_html(const char* html, lb_line_t* out, int max);
