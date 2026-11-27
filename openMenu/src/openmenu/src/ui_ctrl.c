/* ui_ctrl: see ui_ctrl.h. The Internet screen's way: a panel, rows drawn by hand, BACK and the CD buttons' objects. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <dc/maple.h>

#include <bios_audio.h>
#include <bios_scene.h>
#include <bios_vm.h>

#include <backend/gd_item.h>
#include <backend/gd_list.h>

#include "ctrl_editor.h"
#include "gfx.h"
#include "pose_man.h"
#include "sound.h"
#include "ui_ctrl.h"
#include "vm2/dcapi.h"
#include "vm2/vm2_api.h"

#define PRIO 0x1000
#define ID_BACK 0x1110
#define BACK_X (-19.53125f)
#define BACK_Y (-14.84375f)
#define BACK_Z (-351.5625f)

#define PANE_X 52.0f
#define PANE_Y 40.0f
#define PANE_W 536.0f
#define PANE_H 306.0f
#define PANE_Z 0.0028f
#define TEXT_Z 0.5f
#define PILL_Z 0.2f
#define PAD 14.0f
#define ROW_H 30.0f
#define LIST_Y (PANE_Y + 46.0f)
#define LIST_ROWS 7
#define VALUE_X (PANE_X + 250.0f)
#define MAN_Z 0.30f /* the man is in front of the panel and behind the text; his parts get a little more each, in the order they are drawn */

#define WHITE 0xFFFFFFFFu
#define GREY 0xFFE8E8E8u
#define YELLOW 0xFFF0E060u
#define PILL 0x50FFFFFFu
#define PILL_ON 0xE0F0C24Au

static bmenu* menu;
static int console_pal;
static maple_device_t* dev;
static ced ed;
static unsigned anim;
static int on_back;           /* the cursor is on BACK (up from the first row) */
static const char* why = "";
static char adapter_name[12]; /* what the last adapter opened calls itself, short ("iBlue", "Pico2Maple") */
/* the man: the arms are turned into pictures at run time (video memory), the legs are one picture each */
static int arm_tex[2] = {-1, -1}, leg_tex[2] = {-1, -1};
static float arm_uv[2][2], leg_uv[2][2]; /* how much of each padded texture the picture covers */

static int
send_text(void* user, const char* req, char* ans, size_t cap) {
    (void)user;
    return dcapi_request(dev, req, ans, cap);
}

/* the motion page's polling: sent on one frame, the answer picked up on a later one (no frame waits for the bus) */
static int
begin_text(void* user, const char* req) {
    (void)user;
    return dcapi_begin(dev, req);
}

static int
poll_text(void* user, char* ans, size_t cap) {
    (void)user;
    return dcapi_poll(ans, cap);
}

/* The adapter on a port: one "get hello" to the port's controller unit (unit 0), asked only when A is pressed on that controller. The DC API
 * never goes to a memory card unit. Returns the unit and what the adapter calls itself, or NULL when nothing there speaks the language
 * (an official pad answers that it does not know the command). */
static maple_device_t*
find_adapter(int port, dcapi_hello_t* h) {
    maple_device_t* pad = maple_enum_dev(port, 0);

    h->name[0] = h->version[0] = '\0';
    if (!pad || !pad->valid) {
        return NULL;
    }
    if (dcapi_get_hello(pad, h) != 0 || !h->name[0]) {
        return NULL;
    }
    return pad;
}

/* ---- Load game id: the games of the card that have an id (not homebrew / tools), and how an id reaches the port's adapter */
static int* game_index; /* list_all_item() indexes */
static int game_n;
static int id_port;

static void
build_games(void) {
    int all = list_all_count();

    free(game_index);
    game_index = all > 0 ? (int*)malloc(sizeof(int) * (size_t)all) : NULL;
    game_n = 0;
    for (int i = 0; game_index && i < all; i++) {
        const gd_item* g = list_all_item(i);
        if (g && g->product[0] && strcmp(g->type, "other") != 0) {
            game_index[game_n++] = i;
        }
    }
}

static int
games_count(void* user) {
    (void)user;
    return game_n;
}

static const char*
games_name(void* user, int i) {
    (void)user;
    const gd_item* g = i >= 0 && i < game_n ? list_all_item(game_index[i]) : NULL;
    return g ? g->name : "";
}

static const char*
games_id(void* user, int i) {
    (void)user;
    const gd_item* g = i >= 0 && i < game_n ? list_all_item(game_index[i]) : NULL;
    return g ? g->product : "";
}

/* the game id goes to the port's memory card unit, as at a game's start (Maple 0x21, vm2_set_id) */
static int
games_send(void* user, const char* id, const char* name) {
    (void)user;
    for (int unit = 1; unit <= 2; unit++) {
        maple_device_t* d = maple_enum_dev(id_port, unit);
        if (d && d->valid && check_vm2_present(d)) {
            return vm2_set_id(d, id, name) == 0 ? 0 : -1;
        }
    }
    return -1;
}

static const ced_games games = {games_count, games_name, games_id, games_send, NULL};

const char*
uic_why(void) {
    return why;
}

const char*
uic_adapter_name(void) {
    return adapter_name;
}

static void
place(bvm_obj* o, float x, float y, float z) {
    o->flags &= ~(uint32_t)BVM_F_ATTACHED;
    o->pos_tw[0].cur = o->pos[0] = x;
    o->pos_tw[1].cur = o->pos[1] = y;
    o->pos_tw[2].cur = o->pos[2] = z;
    o->pos_tw[0].step = o->pos_tw[1].step = o->pos_tw[2].step = 0;
}

int
uic_open(bmenu* m, int port, int pal) {
    dcapi_hello_t h;

    dev = find_adapter(port, &h);
    if (!dev) {
        why = "No adapter on this port speaks the DC API";
        return 0;
    }
    if (!strncasecmp(h.name, "Pico2Maple", 10)) {
        ced_init_p2m(&ed, send_text, NULL, port);
    } else {
        ced_init(&ed, send_text, NULL, port);
    }
    if (ced_refresh(&ed) != 1) {
        why = "The adapter did not answer";
        return 0;
    }
    ced_set_async(&ed, begin_text, poll_text);
    snprintf(adapter_name, sizeof(adapter_name), "%s", strstr(h.name, "iBlue") ? "iBlue" : h.name);
    id_port = port;
    build_games();
    ced_set_games(&ed, &games);
    menu = m;
    console_pal = pal;
    bvm_kill_all(&m->vm);
    bvm_create(&m->vm, 6, ID_BACK, PRIO);
    anim = 0;
    on_back = 0;
    why = "";
    return 1;
}

static void
free_textures(void) {
    for (int i = 0; i < 2; i++) {
        if (arm_tex[i] >= 0) gfx_dyn_free(arm_tex[i]);
        if (leg_tex[i] >= 0) gfx_dyn_free(leg_tex[i]);
        arm_tex[i] = leg_tex[i] = -1;
    }
}

int
uic_handle(button_t b) {
    switch (b) {
        case BTN_UP:
            if (on_back) break;
            if (ced_cursor(&ed) == 0) {
                on_back = 1; /* up from the first row: BACK, as on the other screens */
                sound_sfx(BAUDIO_SFX_CURSOR);
            } else if (ced_move(&ed, -1)) {
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        case BTN_DOWN:
            if (on_back) {
                on_back = 0;
                sound_sfx(BAUDIO_SFX_CURSOR);
            } else if (ced_move(&ed, 1)) {
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        case BTN_LEFT:
        case BTN_RIGHT:
            if (!on_back && ced_change(&ed, b == BTN_LEFT ? -1 : 1)) {
                sound_sfx(BAUDIO_SFX_CONFIRM);
            }
            break;
        case BTN_A:
        case BTN_START:
            if (on_back) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                free_textures();
                return ced_back(&ed) ? 1 : (ed.page == CED_PAGE_PORT ? 1 : 0);
            }
            if (ced_activate(&ed)) {
                sound_sfx(BAUDIO_SFX_ENTER);
            }
            break;
        case BTN_B:
            sound_sfx(BAUDIO_SFX_CANCEL);
            if (ced_back(&ed)) {
                free_textures();
                return 1;
            }
            break;
        default: break;
    }
    return 0;
}

void
uic_step(void) {
    ced_tick(&ed, 17);
}

void
uic_sync(void) {
    anim++;
    bvm_obj* back = bvm_find(&menu->vm, ID_BACK);
    if (back) {
        place(back, BACK_X, BACK_Y, BACK_Z);
    }
}

/* ---- drawing ---- */

static void
draw_rows(float x0, float x1, int rows, int top, int value_x, int visible)
{
    char label[40], value[40];
    const char* help;

    for (int i = 0; i < visible && top + i < rows; i++) {
        int row = top + i;
        int sel = !on_back && row == ced_cursor(&ed);
        float y = LIST_Y + (float)i * ROW_H;

        ced_row(&ed, row, label, sizeof(label), value, sizeof(value), &help);
        gfx_rrect(x0, y, x1 - x0, ROW_H - 4.0f, (ROW_H - 4.0f) / 2.0f, PILL_Z, sel ? PILL_ON : PILL);
        gfx_text(label, x0 + 12.0f, y + 2.0f, TEXT_Z, sel ? 0xFF202020u : WHITE, sel ? 0 : 1);
        gfx_text(value, (float)value_x, y + 2.0f, TEXT_Z, sel ? 0xFF202020u : GREY, sel ? 0 : 1);
    }
}

/* one triangle of the man through the sink; z grows with the order so that what is drawn later is in front */
static void
man_tri(const bscene_sink* sink, const pm_tri* t, float z) {
    bscene_vtx v[3];
    bscene_texref none = {BSCENE_TEX_NONE, 0, 0, 0};

    for (int k = 0; k < 3; k++) {
        v[k] = (bscene_vtx){t->v[k].x, t->v[k].y, z, 0.0f, 0.0f, t->v[k].argb, 0};
    }
    sink->triangle(sink->user, v, none);
}

static void
draw_man(float cx, float base_y, float scale, float left, float right) {
    static pm_scene sc;
    static pm_tri guides[48];
    const bscene_sink* sink = gfx_sink();
    float f = pose_man_ref_scale(scale);
    int ng = pose_man_guides(cx, base_y, scale, left, right, guides, 48);
    float degs[2] = {left, right};

    for (int i = 0; i < ng; i++) {
        man_tri(sink, &guides[i], MAN_Z - 0.01f);
    }
    /* the four pictures are uploaded once (padded to the PVR's power of two sizes); the arms are turned by the PVR: only their corners
     * change with the Joy-Cons, nothing is drawn or uploaded per angle */
    for (int s = 0; s < 2; s++) {
        float lx, ly, lw, lh;
        if (leg_tex[s] < 0) {
            int w, h;
            const uint16_t* px = pose_man_leg_image(s, &w, &h);
            leg_tex[s] = gfx_dyn_create_pow2(px, w, h, &leg_uv[s][0], &leg_uv[s][1]);
        }
        if (arm_tex[s] < 0) {
            int w, h;
            const uint16_t* px = pose_man_arm_tex(s, &w, &h);
            arm_tex[s] = gfx_dyn_create_pow2(px, w, h, &arm_uv[s][0], &arm_uv[s][1]);
        }
        pose_man_leg_rect(cx, base_y, scale, s, &lx, &ly, &lw, &lh);
        {
            float qx[4] = {lx, lx + lw, lx, lx + lw}, qy[4] = {ly, ly, ly + lh, ly + lh};
            gfx_image_quad(leg_tex[s], qx, qy, leg_uv[s][0], leg_uv[s][1], MAN_Z);
        }
    }
    for (int s = 0; s < 2; s++) {
        float sx, sy, ox[4], oy[4];
        pose_man_shoulder(cx, base_y, scale, s, &sx, &sy);
        pose_man_arm_quad(s, degs[s], f, ox, oy);
        for (int i = 0; i < 4; i++) {
            ox[i] += sx;
            oy[i] += sy;
        }
        gfx_image_quad(arm_tex[s], ox, oy, arm_uv[s][0], arm_uv[s][1], MAN_Z);
    }
    pose_man_build(cx, base_y, scale, left, right, &sc);
    for (int i = 0; i < sc.ntri; i++) {
        man_tri(sink, &sc.tri[i], MAN_Z + 0.0005f + 0.00005f * (float)i);
    }
}

void
uic_draw(void) {
    const bscene_sink* sink = gfx_sink();
    int rows = ced_rows(&ed), top = 0;
    char line[64];

    gfx_pane(PANE_X, PANE_Y, PANE_W, PANE_H, PANE_Z, BSCENE_PANE_ARGB);
    snprintf(line, sizeof(line), "%s  -  port %c", ced_title(&ed), 'A' + ed.port);
    gfx_text(line, PANE_X + PAD, PANE_Y + 10.0f, TEXT_Z, WHITE, 1);
    if (ed.devname[0]) {
        gfx_text(ed.devname, PANE_X + PANE_W - PAD - (float)gfx_text_width(ed.devname), PANE_Y + 10.0f, TEXT_Z, GREY, 1);
    }
    if (rows > LIST_ROWS && ced_cursor(&ed) >= LIST_ROWS / 2) {
        top = ced_cursor(&ed) - LIST_ROWS / 2;
        if (top > rows - LIST_ROWS) top = rows - LIST_ROWS;
    }
    if (ced_wants_man(&ed)) {
        float l, r;
        int live = ced_man(&ed, &l, &r);

        draw_rows(PANE_X + PAD, PANE_X + PAD + 310.0f, rows, 0, (int)(PANE_X + PAD + 170.0f), 8); /* all the rows of the motion page (8 of 30 pixels) */
        if (!live) { l = r = 0.0f; }
        draw_man(PANE_X + 445.0f, PANE_Y + PANE_H - 8.0f, 0.78f, l, r);
        if (live) {
            snprintf(line, sizeof(line), "%d deg", (int)l);
            gfx_text(line, PANE_X + 360.0f, PANE_Y + 44.0f, TEXT_Z, WHITE, 1);
            snprintf(line, sizeof(line), "%d deg", (int)r);
            gfx_text(line, PANE_X + PANE_W - PAD - (float)gfx_text_width(line), PANE_Y + 44.0f, TEXT_Z, WHITE, 1);
        }
    } else {
        draw_rows(PANE_X + PAD, PANE_X + PANE_W - PAD, rows, top, (int)VALUE_X, LIST_ROWS);
    }
    /* the help of the row and what just happened */
    {
        char label[40], value[40];
        const char* help;

        ced_row(&ed, ced_cursor(&ed), label, sizeof(label), value, sizeof(value), &help);
        gfx_text(ed.msg[0] ? ed.msg : help, 190.0f, PANE_Y + PANE_H + 18.0f, TEXT_Z, WHITE, 1);
    }
    /* BACK, lit with its frame blinking while it is selected */
    bmenu_back_style(menu, on_back, (int)anim, console_pal);
    menu->scene.parts = BSCENE_PART_ALL;
    for (int i = 0; i < menu->vm.count; i++) {
        const bvm_obj* o = &menu->vm.objs[menu->vm.order[i]];
        bscene_draw_object(&menu->scene, o, sink);
    }
    menu->scene.ovr_n = 0;
}
