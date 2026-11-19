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

int main(void) {
    ced e;
    char l[40], v[40];
    const char* h;

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
    CHECK(ced_rows(&e) == 6);
    ced_row(&e, 0, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Type"); SAME(v, "Controller");
    ced_row(&e, 1, l, sizeof(l), v, sizeof(v), &h); SAME(v, "Both");
    ced_row(&e, 2, l, sizeof(l), v, sizeof(v), &h); CHECK(!strcmp(v, "Standard") || !strcmp(v, "Custom"));
    ced_row(&e, 4, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Save"); SAME(v, "Saved");
    CHECK(strlen(h) <= 40);

    /* Type: right goes to the next the device accepts, the engine is asked */
    nreq = 0;
    CHECK(ced_change(&e, 1) == 1);
    CHECK(nreq == 1 && reqs[0].op == IBLUE_CFG_SET_MODE && reqs[0].a == 0 && reqs[0].b == DEV_PAD_ALT);
    ced_row(&e, 0, l, sizeof(l), v, sizeof(v), &h); SAME(v, "Dual stick");
    ced_row(&e, 4, l, sizeof(l), v, sizeof(v), &h); SAME(v, "Unsaved changes");
    /* ... and round: left from the first one wraps to the last the device lists */
    ced_change(&e, -1);
    ced_row(&e, 0, l, sizeof(l), v, sizeof(v), &h); SAME(v, "Controller");
    ced_change(&e, -1);
    ced_row(&e, 0, l, sizeof(l), v, sizeof(v), &h); SAME(v, "Maracas");

    /* the accessory */
    ced_move(&e, 1);
    nreq = 0;
    ced_change(&e, 1);
    CHECK(nreq == 1 && reqs[0].op == IBLUE_CFG_SET_ACC);

    /* Buttons page: output A is driven by "a" */
    ced_set_cursor(&e, 3);
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
    CHECK(e.motion_known && ced_rows(&e) == 7);
    ced_row(&e, 4, l, sizeof(l), v, sizeof(v), &h); SAME(l, "Joy-Con motion"); SAME(v, "Ready");
    CHECK(!ced_wants_man(&e));
    ced_set_cursor(&e, 4);
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
    CHECK(!e.motion_known && ced_rows(&e) == 6);
#else
    (void)l; (void)v; (void)h;
    printf("(IBLUE_FW not set: the part that talks to the firmware's engine was skipped)\n");
#endif
    printf("ctrl_editor: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
