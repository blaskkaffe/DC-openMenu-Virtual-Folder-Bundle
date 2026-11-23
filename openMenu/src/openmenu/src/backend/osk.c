/*
 * File: osk.c
 * Project: openmenu
 * See osk.h.
 */

#include "backend/osk.h"

#include <string.h>

static const char* const rows[OSK_LAYERS][OSK_ROWS - 1] = {
    {"1234567890", "qwertyuiop", "asdfghjkl@", "zxcvbnm._-"},
    {"1234567890", "QWERTYUIOP", "ASDFGHJKL@", "ZXCVBNM._-"},
    {"!@#$%^&*()", "~`|<>[]{}/", "\"'+=_-:;,.", ",.?!-_/\\:;"},
};
/* the bottom row: the key, where it starts, how many columns it covers */
static const struct {
    int kind, col, span;
} bottom[] = {{OSK_SHIFT, 0, 2}, {OSK_SYM, 2, 2}, {OSK_SPACE, 4, 3}, {OSK_DEL, 7, 2}, {OSK_OK, 9, 1}};
#define NBOTTOM ((int)(sizeof(bottom) / sizeof(bottom[0])))

void
osk_init(osk_t* k) {
    k->layer = OSK_LOWER;
    k->row = 1;
    k->col = 0;
}

osk_key_t
osk_key_at(int layer, int row, int col) {
    osk_key_t r = {OSK_CHAR, 0, col, 1};

    if (layer < 0 || layer >= OSK_LAYERS || row < 0 || row >= OSK_ROWS || col < 0 || col >= OSK_COLS) {
        r.kind = OSK_SPACE;
        return r;
    }
    if (row == OSK_ROWS - 1) {
        for (int i = 0; i < NBOTTOM; i++) {
            if (col >= bottom[i].col && col < bottom[i].col + bottom[i].span) {
                r.kind = bottom[i].kind;
                r.col = bottom[i].col;
                r.span = bottom[i].span;
                return r;
            }
        }
    }
    r.ch = rows[layer][row][col];
    return r;
}

osk_key_t
osk_key(const osk_t* k) {
    return osk_key_at(k->layer, k->row, k->col);
}

void
osk_move(osk_t* k, int dr, int dc) {
    osk_key_t cur = osk_key(k);

    if (dr) {
        int r = k->row + dr;
        if (r >= 0 && r < OSK_ROWS) {
            k->row = r; /* the column stays: a wide key below covers it */
        }
    }
    if (dc) {
        if (k->row == OSK_ROWS - 1) { /* from key to key */
            int c = dc > 0 ? cur.col + cur.span : cur.col - 1;
            if (c >= 0 && c < OSK_COLS) {
                k->col = osk_key_at(k->layer, k->row, c).col;
            }
        } else {
            int c = k->col + dc;
            if (c >= 0 && c < OSK_COLS) {
                k->col = c;
            }
        }
    }
}

void
osk_toggle_shift(osk_t* k) {
    k->layer = k->layer == OSK_LOWER ? OSK_UPPER : OSK_LOWER;
}

void
osk_toggle_symbols(osk_t* k) {
    static int before = OSK_LOWER;
    if (k->layer == OSK_SYMBOLS) {
        k->layer = before;
    } else {
        before = k->layer;
        k->layer = OSK_SYMBOLS;
    }
}

int
osk_char_ok(const char* set, char ch) {
    if (ch == 0) {
        return 0;
    }
    return set ? strchr(set, ch) != NULL : ch >= 0x20 && ch < 0x7F;
}
