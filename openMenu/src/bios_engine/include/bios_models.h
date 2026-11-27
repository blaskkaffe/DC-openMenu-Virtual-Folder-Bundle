/*
 * bios_models: extra 3D models in the style of the BIOS menu models, built in code (no ROM data).
 * They are plain nj_objects, so the scene draws them like any ROM model. A model id is passed
 * where the scripts pass a ROM model index; ids start at BMODEL_BASE.
 *
 * Texture slots (poly->tex, i.e. slot within the object's texlist):
 *   BMODEL_TEX_FRONT (0)  the front picture, e.g. the disc artwork or box art
 *   BMODEL_TEX_BACK  (1)  the back picture (nothing is placed there yet)
 * Everything else is untextured. The mapping of a slot to a real image is up to the renderer.
 *
 * Units match the ROM icon models (about +-5 for a menu icon). The models face +z, y is up.
 */
#ifndef BIOS_MODELS_H
#define BIOS_MODELS_H

#include "nj_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BMODEL_BASE 0x100
enum {
    BMODEL_PHONE = BMODEL_BASE,  /* classic telephone, the colour of the music note, white dial disc */
    BMODEL_GLOBE,                /* low poly earth in green and blue on a small stand */
    BMODEL_CASE_WHITE,           /* CD jewel case, white */
    BMODEL_CASE_PAL,             /* the blue PAL Dreamcast case */
    BMODEL_CASE_DARK,            /* CD jewel case, dark grey */
    BMODEL_PODIUM,               /* winners' stand in Dreamcast colours; the numbers 1 2 3 are the CD player's digits (bmodel_podium_digits) */
    BMODEL_CASE_CLEAR_WHITE,     /* clear CD jewel case, white tray: a white inner label with the colour logo behind the clear front spine */
    BMODEL_CASE_CLEAR_BLACK,     /* clear CD jewel case, black tray: a black inner label with a dark grey logo */
    /* the double cases (two or more discs, 24 mm deep): ribbed dark grey top and bottom, clear front, back and sides, two spine labels
     * per side; the style picks the spine label (and blue edges for PAL) */
    BMODEL_CASE_WHITE_DOUBLE,
    BMODEL_CASE_PAL_DOUBLE,
    BMODEL_CASE_DARK_DOUBLE,
    BMODEL_CASE_CLEAR_WHITE_DOUBLE,
    BMODEL_CASE_CLEAR_BLACK_DOUBLE,
    /* the same ten cases for the game list's card (bmodel_case_list): a few boxes, only the faces the card's tilt can show (no back),
     * one picture (the front); the spine is the colour of the kind's spine label, the hinge side and the edges flat colours. The
     * game list makes no spine, back or inner label picture with them: those come with the full case in the information screen. */
    BMODEL_LCASE_WHITE,
    BMODEL_LCASE_PAL,
    BMODEL_LCASE_DARK,
    BMODEL_LCASE_CLEAR_WHITE,
    BMODEL_LCASE_CLEAR_BLACK,
    BMODEL_LCASE_WHITE_DOUBLE,
    BMODEL_LCASE_PAL_DOUBLE,
    BMODEL_LCASE_DARK_DOUBLE,
    BMODEL_LCASE_CLEAR_WHITE_DOUBLE,
    BMODEL_LCASE_CLEAR_BLACK_DOUBLE,
    BMODEL_END
};
#define BMODEL_COUNT (BMODEL_END - BMODEL_BASE)

/* a game list case's full one (any other model as it is) */
static inline int
bmodel_case_full(int id) {
    static const int full[10] = {BMODEL_CASE_WHITE, BMODEL_CASE_PAL, BMODEL_CASE_DARK, BMODEL_CASE_CLEAR_WHITE, BMODEL_CASE_CLEAR_BLACK,
                                 BMODEL_CASE_WHITE_DOUBLE, BMODEL_CASE_PAL_DOUBLE, BMODEL_CASE_DARK_DOUBLE, BMODEL_CASE_CLEAR_WHITE_DOUBLE,
                                 BMODEL_CASE_CLEAR_BLACK_DOUBLE};
    return id >= BMODEL_LCASE_WHITE && id <= BMODEL_LCASE_CLEAR_BLACK_DOUBLE ? full[id - BMODEL_LCASE_WHITE] : id;
}

/* a case's game list one (any other model as it is) */
static inline int
bmodel_case_list(int id) {
    for (int i = 0; i < 10; i++) {
        if (bmodel_case_full(BMODEL_LCASE_WHITE + i) == id) {
            return BMODEL_LCASE_WHITE + i;
        }
    }
    return id;
}

static inline int
bmodel_case_is_list(int id) {
    return id >= BMODEL_LCASE_WHITE && id <= BMODEL_LCASE_CLEAR_BLACK_DOUBLE;
}

/* a double case's single one, for a list case the full single one (any other model as it is) */
static inline int
bmodel_case_single(int id) {
    switch (id) {
        case BMODEL_CASE_WHITE_DOUBLE: case BMODEL_LCASE_WHITE: case BMODEL_LCASE_WHITE_DOUBLE: return BMODEL_CASE_WHITE;
        case BMODEL_CASE_PAL_DOUBLE: case BMODEL_LCASE_PAL: case BMODEL_LCASE_PAL_DOUBLE: return BMODEL_CASE_PAL;
        case BMODEL_CASE_DARK_DOUBLE: case BMODEL_LCASE_DARK: case BMODEL_LCASE_DARK_DOUBLE: return BMODEL_CASE_DARK;
        case BMODEL_CASE_CLEAR_WHITE_DOUBLE: case BMODEL_LCASE_CLEAR_WHITE: case BMODEL_LCASE_CLEAR_WHITE_DOUBLE: return BMODEL_CASE_CLEAR_WHITE;
        case BMODEL_CASE_CLEAR_BLACK_DOUBLE: case BMODEL_LCASE_CLEAR_BLACK: case BMODEL_LCASE_CLEAR_BLACK_DOUBLE: return BMODEL_CASE_CLEAR_BLACK;
        default: return id;
    }
}

/* a single case's double one (any other model as it is) */
static inline int
bmodel_case_double(int id) {
    switch (id) {
        case BMODEL_CASE_WHITE: return BMODEL_CASE_WHITE_DOUBLE;
        case BMODEL_CASE_PAL: return BMODEL_CASE_PAL_DOUBLE;
        case BMODEL_CASE_DARK: return BMODEL_CASE_DARK_DOUBLE;
        case BMODEL_CASE_CLEAR_WHITE: return BMODEL_CASE_CLEAR_WHITE_DOUBLE;
        case BMODEL_CASE_CLEAR_BLACK: return BMODEL_CASE_CLEAR_BLACK_DOUBLE;
        default: return id;
    }
}

static inline int
bmodel_case_is_double(int id) {
    id = bmodel_case_full(id);
    return bmodel_case_single(id) != id;
}

#define BMODEL_CASE_HEIGHT 9.3f /* height of the two cases in model units */
#define BMODEL_TEX_FRONT 0
#define BMODEL_TEX_BACK 1
#define BMODEL_TEX_SPINE 3 /* the PAL case's spine label: blue with a lighter patch and the BIOS wordmark, along the spine (made by gfx) */
#define BMODEL_TEX_PILL 4 /* the PAL case's hinge bar, or the NTSC cases' inner label behind the clear front spine (made by gfx) */
#define BMODEL_TEX_RIBS 2 /* the cases' ribbed edges: grey stripes that modulate the face colour (made by gfx) */
#define BMODEL_NOTE_COLOR 0xFF7F66FFu /* material colour of the music note (ROM model 2) */

/* Build model `id`. Returns 0, or -1 for an unknown id or out of memory. Free with nj_object_free(). */
int bmodel_build(int id, nj_object* out);

/* The podium's finishing touch, which needs the ROM: tilts the stand a little and puts the CD player's digit models 1, 2 and 3 on the
 * fronts of its blocks. `out` is what bmodel_build(BMODEL_PODIUM) made. 0, or -1 (then `out` is left as it was). */
struct bios_rom;
int bmodel_podium_digits(const struct bios_rom* rom, nj_object* out);

/* The BACK marker's frame and picture (ROM model 0) rebuilt with four identical rounded corners. `out` is the loaded ROM model. 0, or -1 (left as it was). */
int bmodel_fix_back(nj_object* out);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_MODELS_H */
