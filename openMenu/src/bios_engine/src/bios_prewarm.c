/* Load the models and textures of the other screens before the first frame, as bmenu_prewarm() does for the main menu.
 *
 * Without it the first visit to the game list, the Settings page, the file grid or the CD player parses their models from the
 * ROM and copies their textures into video memory in the frame where they first appear (the menu stalls for it, and the
 * frame counter shows a dip). Here each screen is opened on the menu's own scene, run for a few frames and "drawn" into
 * a sink that only loads (gfx_warm_sink), then the main menu's scripts are put back as they were. */
#include <stdlib.h>
#include <string.h>

#include "bios_cdplayer.h"
#include "bios_files.h"
#include "bios_list.h"
#include "bios_menu.h"
#include "bios_page.h"

static void
default_row(void* user, int index, bpage_row* out) {
    (void)user;
    out->icon = index < 4 ? BPAGE_ICON_BIOS(index) : BPAGE_ICON_DIGIT(index - 4);
}

/* Which frames of a screen are drawn: its objects appear over the first ones and the motions run on. */
static int
drawn(int f, int frames) {
    return f == 0 || f == frames - 1 || f == 3 || f % 8 == 0;
}

void
bmenu_prewarm_screens(bmenu* m, const bscene_sink* sink, bpage_row_fn row, void* user, int page_rows) {
    bvm* saved = (bvm*)malloc(sizeof(bvm)); /* the scripts of the screen that is up (the main menu), put back at the end */
    if (!saved) {
        return;
    }
    memcpy(saved, &m->vm, sizeof(bvm));
    const unsigned parts = m->scene.parts;
    if (!row) {
        row = default_row;
        user = NULL;
    }
    enum { FRAMES = 20 };

    static blist list; /* each screen's own state is a scratch copy */
    static bpage page;
    static bfiles files;
    static bcdplayer cd;

    blist_open(&list, m, 7, 30);
    list.disc_number = 2; /* the digit models of a multi-disc game */
    for (int f = 0; f < FRAMES; f++) {
        bvm_update(&m->vm);
        blist_sync(&list);
        if (drawn(f, FRAMES)) {
            blist_draw(&list, sink);
        }
    }
    bpage_open(&page, m, page_rows > 0 ? page_rows : 8);
    for (int f = 0; f < FRAMES; f++) {
        bvm_update(&m->vm);
        bpage_sync(&page, row, user);
        if (drawn(f, FRAMES)) {
            bpage_draw(&page, sink);
        }
    }
    bfiles_open(&files, m, 0);
    for (int f = 0; f < FRAMES; f++) {
        bvm_update(&m->vm);
        for (int i = 0; i < BFILES_SLOTS; i++) {
            files.present[i] = i != 1; /* some cards and some empty sockets */
        }
        bfiles_sync(&files);
        if (drawn(f, FRAMES)) {
            bfiles_draw(&files, sink);
        }
    }
    bcd_open(&cd, m, 1, 12, 300);
    for (int f = 0; f < FRAMES; f++) {
        bvm_update(&m->vm);
        bcd_sync(&cd);
        if (drawn(f, FRAMES)) {
            bcd_draw(&cd, sink);
        }
    }

    memcpy(&m->vm, saved, sizeof(bvm));
    m->scene.parts = parts;
    free(saved);
}
