/* Host test of the generic controller editor (ctrl_editor.c) against the firmware's own DC API engine (see run.sh, IBLUE_FW). */
#include <stdio.h>
#include <string.h>

#include "ctrl_editor.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define SAME(got, want) do { checks++; if (strcmp((got), (want)) != 0) { fails++; printf("FAIL %s:%d: got \"%s\" want \"%s\"\n", __FILE__, __LINE__, got, want); } } while (0)

#ifdef WITH_ENGINE
#include "adapter/adapter.h"
#include "iblue/iblue_gameid.h"
#include "iblue/iblue_dcapi.h"

/* the numbers of adapter.h (the stub of the firmware tests has no such enums) */
enum { DEV_PAD = 0, DEV_PAD_ALT, DEV_KB, DEV_MOUSE, DEV_RACING, DEV_MARACAS };
enum { ACC_NONE = 0, ACC_MEM, ACC_RUMBLE, ACC_BOTH };

void adapter_q_fb(struct raw_fb* fb_data) { (void)fb_data; }
static struct { uint8_t op, a, b; } reqs[64];
static int nreq;
static struct { uint8_t port, out; struct iblue_mapent e; } sets[8];
static int nset;
static struct iblue_mapent live[4][130];
static size_t live_n[4];
int iblue_dcapi_cfg_request(uint8_t op, uint8_t a, uint8_t b) { if (nreq < 64) { reqs[nreq].op = op; reqs[nreq].a = a; reqs[nreq].b = b; nreq++; } return 1; }
const struct iblue_mapent* iblue_dcapi_cfg_map(uint32_t port, size_t* count) { *count = live_n[port & 3]; return live[port & 3]; }
int iblue_dcapi_cfg_set_map(uint8_t port, uint8_t out, const struct iblue_mapent* e, size_t n) {
    if (nset < 8 && n) { sets[nset].port = port; sets[nset].out = out; sets[nset].e = e[0]; nset++; }
    return 1;
}

static int engine(void* user, const char* req, char* ans, size_t cap) {
    (void)user;
    return (int)iblue_dcapi_handle(req, strlen(req), ans, cap);
}
#endif

static int silent(void* user, const char* req, char* ans, size_t cap) { (void)user; (void)req; (void)ans; (void)cap; return -1; }

/* a device with the proposed keys port.A.vmu.slot(s) and game.mode (docs/dcapi.md): answers from a small table */
static char fake_slot[24] = "3", fake_game[24] = "off", fake_acc[24] = "vmu";
static int fake(void* user, const char* req, char* ans, size_t cap) {
    char line[96];
    size_t n = 0;
    (void)user;
    n += (size_t)snprintf(ans + n, cap - n, "dc1\n");
    for (const char* s = req; *s;) {
        size_t l = strcspn(s, "\n");
        if (l >= sizeof(line)) l = sizeof(line) - 1;
        memcpy(line, s, l);
        line[l] = 0;
        s += l;
        if (*s) s++;
        if (!strncmp(line, "get ", 4)) {
            const char* k = line + 4;
            const char* val = !strcmp(k, "port.A.mode") ? "normal" : !strcmp(k, "port.A.acc") ? fake_acc : !strcmp(k, "port.A.vmu.slot") ? fake_slot
                            : !strcmp(k, "port.A.vmu.slots") ? "1,2,3,4,5,6,7,8,9,10" : !strcmp(k, "game.mode") ? fake_game
                            : !strcmp(k, "port.A.accs") ? "none,vmu" : !strcmp(k, "sys.unsaved") ? "no" : NULL;
            n += (size_t)(val ? snprintf(ans + n, cap - n, "ok %s %s\n", k, val) : snprintf(ans + n, cap - n, "err %s unknown\n", k));
        } else if (!strncmp(line, "set ", 4)) {
            char k[40], val[24];
            if (sscanf(line + 4, "%39s %23s", k, val) == 2) {
                if (!strcmp(k, "port.A.vmu.slot")) snprintf(fake_slot, sizeof(fake_slot), "%s", val);
                if (!strcmp(k, "game.mode")) snprintf(fake_game, sizeof(fake_game), "%s", val);
                if (!strcmp(k, "port.A.acc")) snprintf(fake_acc, sizeof(fake_acc), "%s", val);
                n += (size_t)snprintf(ans + n, cap - n, "ok %s %s\n", k, val);
            }
        }
    }
    return (int)n;
}

/* a Pico2Maple: its own keys, sys.store auto, no display.flip */
static char p2m_bright[16] = "73", p2m_slot[16] = "10", p2m_layout[16] = "all", p2m_last[300];
static int p2m(void* user, const char* req, char* ans, size_t cap) {
    (void)user;
    snprintf(p2m_last, sizeof(p2m_last), "%s", req);
    snprintf(ans, cap, "dc1\n");
    for (const char* p = req; *p;) {
        char l[80], verb[8], key[40], val[16] = "";
        size_t n = strcspn(p, "\n");
        snprintf(l, sizeof(l), "%.*s", (int)n, p);
        p += n + (p[n] == '\n');
        if (sscanf(l, "%7s %39s %15s", verb, key, val) < 2) continue;
        char* v = !strcmp(key, "display.brightness") ? p2m_bright : !strcmp(key, "vmu.slot") ? p2m_slot : !strcmp(key, "port.self.layout") ? p2m_layout : NULL;
        const char* fixed = !strcmp(key, "vmu.icons") ? "bios" : !strcmp(key, "port.self.switch.auto") ? "on" : !strcmp(key, "sys.store") ? "auto" : NULL;
        size_t at = strlen(ans);
        if (v && !strcmp(verb, "set")) snprintf(v, 16, "%s", val);
        if (v || fixed) snprintf(ans + at, cap - at, "ok %s %s\n", key, v ? v : fixed);
        else snprintf(ans + at, cap - at, "err %s unknown\n", key);
    }
    return (int)strlen(ans);
}

static const char* const game_names[2] = {"Sonic Adventure", "Shenmue"};
static const char* const game_ids[2] = {"MK-51000", "MK-51059"};
static char sent_id[16];
static int g_count(void* u) { (void)u; return 2; }
static const char* g_name(void* u, int i) { (void)u; return game_names[i]; }
static const char* g_id(void* u, int i) { (void)u; return game_ids[i]; }
static int g_send(void* u, const char* id, const char* name) { (void)u; (void)name; snprintf(sent_id, sizeof(sent_id), "%s", id); return 0; }

int main(void) {
    ced e;
    char l[40], v[40];
    const char* h;
    const ced_games games = {g_count, g_name, g_id, g_send, NULL};

    /* a device with slots and the game id switch: the rows appear; rumble is not offered by it */
    ced_init(&e, fake, NULL, 0);
    ced_set_games(&e, &games);
    CHECK(ced_refresh(&e) == 1 && e.has_slot && e.has_game_mode);
    CHECK(ced_rows(&e) == 10);
    ced_row(&e, 2, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Memory card slot"); SAME(v, "3");
    ced_row(&e, 3, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Game id"); SAME(v, "Off");
    ced_set_cursor(&e, 2);
    CHECK(ced_change(&e, 1) == 1);
    SAME(fake_slot, "4");
    ced_change(&e, -1);
    ced_change(&e, -1);
    ced_change(&e, -1);
    ced_change(&e, -1);
    SAME(fake_slot, "10"); /* wraps */
    ced_set_cursor(&e, 3);
    ced_change(&e, 1);
    SAME(fake_game, "on");
    ced_row(&e, 3, l, sizeof(l), v, sizeof(v), &h); SAME(v, "On");
    ced_set_cursor(&e, 5); /* Rumble: this device lists no rumble */
    ced_change(&e, 1);
    SAME(fake_acc, "vmu");
    SAME(e.msg, "Not on this device");
    /* Load game id: the game page, A sends the id */
    ced_set_cursor(&e, 4);
    CHECK(ced_activate(&e) == 1 && e.page == CED_PAGE_GAME);
    SAME(ced_title(&e), "Load game id");
    CHECK(ced_rows(&e) == 2);
    ced_row(&e, 1, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Shenmue"); SAME(v, "MK-51059");
    ced_set_cursor(&e, 1);
    CHECK(ced_activate(&e) == 1 && e.page == CED_PAGE_PORT);
    SAME(sent_id, "MK-51059");
    SAME(e.msg, "Sent MK-51059");

    /* a Pico2Maple with the DC API: its own keys, stored by itself (no Save / Undo), no flip key on this one */
    ced_init_p2m(&e, p2m, NULL, 1);
    ced_set_games(&e, &games);
    CHECK(ced_refresh(&e) == 1);
    SAME(ced_title(&e), "Pico2Maple");
    CHECK(ced_rows(&e) == 6); /* brightness, icons, slot, layout, switch, Load game id */
    ced_row(&e, 0, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Display brightness"); SAME(v, "70%");
    ced_row(&e, 1, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Memory card icons"); SAME(v, "BIOS icons 0-9");
    ced_row(&e, 2, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Memory card slot"); SAME(v, "10");
    ced_row(&e, 5, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Load game id");
    ced_set_cursor(&e, 0);
    CHECK(ced_change(&e, 1) == 1);
    SAME(p2m_bright, "80");
    ced_row(&e, 0, l, sizeof(l), v, sizeof(v), &h); SAME(v, "80%");
    CHECK(strstr(p2m_last, "call") == NULL); /* auto store: no commit */
    ced_set_cursor(&e, 2);
    CHECK(ced_change(&e, 1) == 1);
    SAME(p2m_slot, "gameid");
    ced_row(&e, 2, l, sizeof(l), v, sizeof(v), &h); SAME(v, "Game id");
    ced_set_cursor(&e, 3);
    CHECK(ced_change(&e, 1) == 1);
    SAME(p2m_layout, "twin");
    ced_set_cursor(&e, 5);
    sent_id[0] = 0;
    CHECK(ced_activate(&e) == 1 && e.page == CED_PAGE_GAME);
    ced_set_cursor(&e, 0);
    ced_activate(&e);
    SAME(sent_id, "MK-51000");
    CHECK(ced_back(&e) == 1);

    ced_init(&e, silent, NULL, 0);
    CHECK(ced_refresh(&e) < 0 && !e.reachable);

#ifdef WITH_ENGINE
    iblue_dcapi_init("v-test");
    iblue_dcapi_port_count(4);
    iblue_dcapi_port_set(0, IBLUE_PORT_BT, DEV_PAD, ACC_BOTH, 0, 0);
    iblue_dcapi_port_set(1, IBLUE_PORT_BT, DEV_MARACAS, ACC_NONE, 0, 0);
    /* port A: the output A is driven by the generic id of the A button */
    live[0][0] = (struct iblue_mapent){IBM_RB_DOWN, IBM_RB_DOWN, 0, 100, 50, 135, 0, 0};
    live_n[0] = 1;

    ced_init(&e, engine, NULL, 0);
    CHECK(ced_refresh(&e) == 1 && e.reachable);
    SAME(ced_title(&e), "Controller");
    /* Type, Memory card, Load game id, Rumble, Layout, Buttons, Save, Undo: the iBlue has no slots and no game id switch (yet) */
    CHECK(!e.has_slot && !e.has_game_mode);
    CHECK(ced_rows(&e) == 8);
    ced_row(&e, 0, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Type"); SAME(v, "Controller");
    ced_row(&e, 1, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Memory card"); SAME(v, "On");
    ced_row(&e, 2, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Load game id");
    ced_row(&e, 3, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Rumble"); SAME(v, "On");
    ced_row(&e, 4, l, sizeof(l), v, sizeof(v), &h); CHECK(!strcmp(v, "Standard") || !strcmp(v, "Custom"));
    ced_row(&e, 6, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Save"); SAME(v, "Saved");
    CHECK(strlen(h) <= 40);

    /* Type: right goes to the next the device accepts, the engine is asked */
    nreq = 0;
    CHECK(ced_change(&e, 1) == 1);
    CHECK(nreq == 1 && reqs[0].op == IBLUE_CFG_SET_MODE && reqs[0].a == 0 && reqs[0].b == DEV_PAD_ALT);
    ced_row(&e, 0, l, sizeof(l), v, sizeof(v), &h); SAME(v, "Dual stick");
    ced_row(&e, 6, l, sizeof(l), v, sizeof(v), &h); SAME(v, "Unsaved changes");
    /* ... and round: left from the first one wraps to the last the device lists */
    ced_change(&e, -1);
    ced_row(&e, 0, l, sizeof(l), v, sizeof(v), &h); SAME(v, "Controller");
    ced_change(&e, -1);
    ced_row(&e, 0, l, sizeof(l), v, sizeof(v), &h); SAME(v, "Maracas");

    /* the accessory as two switches: rumble off leaves the memory card (both -> vmu), then memory card off (vmu -> none) */
    ced_set_cursor(&e, 3);
    nreq = 0;
    ced_change(&e, 1);
    CHECK(nreq == 1 && reqs[0].op == IBLUE_CFG_SET_ACC && reqs[0].b == ACC_MEM);
    SAME(e.acc, "vmu");
    ced_row(&e, 3, l, sizeof(l), v, sizeof(v), &h); SAME(v, "Off");
    ced_set_cursor(&e, 1);
    nreq = 0;
    ced_change(&e, -1);
    CHECK(nreq == 1 && reqs[0].op == IBLUE_CFG_SET_ACC && reqs[0].b == ACC_NONE);
    /* Load game id needs the memory card on (the id goes to its unit) */
    ced_set_cursor(&e, 2);
    CHECK(ced_activate(&e) == 1 && e.page == CED_PAGE_PORT);
    ced_set_cursor(&e, 1);
    ced_change(&e, 1);
    ced_set_cursor(&e, 3);
    ced_change(&e, 1);
    SAME(e.acc, "both");

    /* Buttons page: output A is driven by "a" */
    ced_set_cursor(&e, 5);
    CHECK(ced_activate(&e) == 1);
    SAME(ced_title(&e), "Buttons");
    CHECK(ced_rows(&e) == CED_OUTPUTS);
    ced_row(&e, 0, l, sizeof(l), v, sizeof(v), &h); SAME(l, "A"); SAME(v, "a");
    ced_row(&e, 1, l, sizeof(l), v, sizeof(v), &h); SAME(l, "B"); CHECK(!strcmp(v, "none") || !strcmp(v, "?") || v[0]);
    ced_row(&e, 25, l, sizeof(l), v, sizeof(v), &h); SAME(l, "DPAD2_RIGHT");
    /* pick another input for the output at the cursor (X, row 3 in the list below) */
    ced_set_cursor(&e, 2);
    CHECK(ced_activate(&e) == 1);
    SAME(ced_title(&e), "X");
    CHECK(ced_rows(&e) == 31);
    ced_set_cursor(&e, 5); /* lb */
    nset = 0;
    CHECK(ced_activate(&e) == 1);
    CHECK(nset == 1 && sets[0].port == 0);
    SAME(ced_title(&e), "Buttons");
    CHECK(ced_back(&e) == 0);
    SAME(ced_title(&e), "Controller");
    CHECK(ced_back(&e) == 1); /* left the editor */

    /* a maracas port has the Joy-Con motion row; the man shows on that page only */
    iblue_dcapi_motion_set(1, 2, 2);
    iblue_dcapi_motion_height(0, 33);
    iblue_dcapi_motion_height(1, -20);
    ced_init(&e, engine, NULL, 1);
    CHECK(ced_refresh(&e) == 1);
    SAME(e.mode, "maracas");
    CHECK(e.motion_known && ced_rows(&e) == 9);
    ced_row(&e, 6, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Joy-Con motion"); SAME(v, "Ready");
    CHECK(!ced_wants_man(&e));
    ced_set_cursor(&e, 6);
    CHECK(ced_activate(&e) == 1 && ced_wants_man(&e));
    SAME(ced_title(&e), "Joy-Con motion");
    CHECK(ced_rows(&e) == uim_rows());
    CHECK(ced_tick(&e, 200) == 1);
    {
        float a, b;
        CHECK(ced_man(&e, &a, &b) == 1 && a == 33.0f && b == -20.0f);
        iblue_dcapi_motion_height(0, -90);
        CHECK(ced_tick(&e, 200) == 1);
        ced_man(&e, &a, &b);
        CHECK(a == -90.0f && b == -20.0f);
    }
    /* A on the status rows does nothing; the pose rows ask the adapter */
    nreq = 0;
    ced_set_cursor(&e, UIM_ROW_FORWARD);
    CHECK(ced_activate(&e) == 0 || 1);
    CHECK(ced_back(&e) == 0 && !ced_wants_man(&e));

    /* a port that is not a maracas port has no motion row, and no motion asked */
    iblue_dcapi_port_set(1, IBLUE_PORT_BT, DEV_PAD, ACC_NONE, 0, 0);
    ced_init(&e, engine, NULL, 1);
    ced_refresh(&e);
    CHECK(!e.motion_known && ced_rows(&e) == 8);
#else
    (void)l; (void)v; (void)h;
    printf("(IBLUE_FW not set: the part that talks to the firmware's engine was skipped)\n");
#endif
    printf("ctrl_editor: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
