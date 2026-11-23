/*
 * File: osk.h
 * Project: openmenu
 * The on-screen keyboard's layout and cursor (no KOS, host-testable): four rows of ten keys on three layers (lower case, upper case,
 * symbols) and a bottom row of Shift, Symbols, Space, Delete and OK. The text editor in ui_internet.c draws it and types with it.
 */

#pragma once

#define OSK_ROWS 5
#define OSK_COLS 10

enum { OSK_LOWER = 0, OSK_UPPER, OSK_SYMBOLS, OSK_LAYERS };
enum { OSK_CHAR = 0, OSK_SHIFT, OSK_SYM, OSK_SPACE, OSK_DEL, OSK_OK };

typedef struct osk {
    int layer;
    int row, col; /* the cursor: col 0..9 (on the bottom row any column of a wide key selects it) */
} osk_t;

typedef struct osk_key {
    int kind;      /* OSK_CHAR, ... */
    char ch;       /* the character of an OSK_CHAR */
    int col, span; /* where the key starts and how many columns it covers */
} osk_key_t;

void osk_init(osk_t* k);
/* The key under the cursor (or the key at row, col of the given layer). */
osk_key_t osk_key_at(int layer, int row, int col);
osk_key_t osk_key(const osk_t* k);
/* Moves one key: dr, dc in -1, 0, 1 (stops at the edges; on the bottom row left and right go from key to key). */
void osk_move(osk_t* k, int dr, int dc);
/* Shift and Symbols: lower <-> upper, and symbols on or off (back to the layer before). */
void osk_toggle_shift(osk_t* k);
void osk_toggle_symbols(osk_t* k);
/* 1 when ch is in the set of allowed characters (a set of NULL allows everything printable). */
int osk_char_ok(const char* set, char ch);
