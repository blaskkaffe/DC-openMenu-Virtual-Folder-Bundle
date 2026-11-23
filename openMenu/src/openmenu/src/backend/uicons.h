/* The glyphs of the Internet screen's five buttons (globe, controller, calendar, speech bubble, wrench), drawn at run time as flat dark
 * shapes, like the CD player's play and pause marks. Pure code, no console needed: test/uicons_test.c renders them. */
#pragma once

#include <stdint.h>

enum { UICON_ONLINE, UICON_PLAY, UICON_EVENTS, UICON_CHAT, UICON_SETTINGS, UICON_COUNT };

#define UICON_SIZE 64 /* a power of two and a multiple of 32: the picture is not twiddled */
#define UICON_DRAW 30 /* as drawn on a button */

/* Draw glyph `id` into w*h ARGB4444 pixels (w = h = UICON_SIZE): the glyph in `argb` (alpha ignored) on a clear background. */
void uicon_render(int id, uint32_t argb, uint16_t* out);
