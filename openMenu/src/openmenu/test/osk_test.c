/* Host test of osk.c */
#include <assert.h>
#include <string.h>

#include "backend/osk.h"

int
main(void) {
    osk_t k;
    char seen[128] = {0};

    /* every printable character the editors allow (letters, digits and the symbol keys) is on some layer */
    for (int l = 0; l < OSK_LAYERS; l++) {
        for (int r = 0; r < OSK_ROWS - 1; r++) {
            for (int c = 0; c < OSK_COLS; c++) {
                osk_key_t key = osk_key_at(l, r, c);
                assert(key.kind == OSK_CHAR && key.span == 1 && key.ch >= 0x20);
                seen[(int)key.ch] = 1;
            }
        }
    }
    for (const char* p = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@._-!#$%^&*()~`|\\/<>[]{}\"'+=:;,?"; *p; p++) {
        assert(seen[(int)*p]);
    }

    osk_init(&k);
    assert(osk_key(&k).ch == 'q');
    osk_move(&k, 0, -1); /* the edges hold */
    assert(k.col == 0);
    osk_move(&k, -1, 0);
    osk_move(&k, -1, 0);
    assert(k.row == 0 && osk_key(&k).ch == '1');
    osk_move(&k, -1, 0);
    assert(k.row == 0);
    for (int i = 0; i < 12; i++) {
        osk_move(&k, 0, 1);
    }
    assert(k.col == 9 && osk_key(&k).ch == '0');

    /* down to the bottom row: the wide key covering the column */
    for (int i = 0; i < 4; i++) {
        osk_move(&k, 1, 0);
    }
    assert(k.row == 4 && osk_key(&k).kind == OSK_OK);
    osk_move(&k, 0, 1);
    assert(osk_key(&k).kind == OSK_OK); /* no further */
    osk_move(&k, 0, -1);
    assert(osk_key(&k).kind == OSK_DEL && k.col == 7);
    osk_move(&k, 0, -1);
    assert(osk_key(&k).kind == OSK_SPACE && k.col == 4);
    osk_move(&k, 0, -1);
    assert(osk_key(&k).kind == OSK_SYM);
    osk_move(&k, 0, -1);
    assert(osk_key(&k).kind == OSK_SHIFT && k.col == 0);
    osk_move(&k, 0, -1);
    assert(osk_key(&k).kind == OSK_SHIFT);
    osk_move(&k, -1, 0); /* up from a wide key keeps its first column */
    assert(osk_key(&k).ch == 'z');

    /* layers */
    osk_toggle_shift(&k);
    assert(osk_key(&k).ch == 'Z' && k.layer == OSK_UPPER);
    osk_toggle_symbols(&k);
    assert(k.layer == OSK_SYMBOLS);
    osk_toggle_symbols(&k);
    assert(k.layer == OSK_UPPER); /* back where it was */
    osk_toggle_shift(&k);
    assert(k.layer == OSK_LOWER);

    assert(osk_char_ok("abc", 'b') && !osk_char_ok("abc", 'z') && !osk_char_ok("abc", 0));
    assert(osk_char_ok(NULL, '~') && !osk_char_ok(NULL, 0x01));
    return 0;
}
