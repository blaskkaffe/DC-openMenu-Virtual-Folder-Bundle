/* Host test of the Joy-Con motion page logic (ui_motion.c). With IBLUE_FW set to an iBlueControlMod checkout the adapter is the firmware's own
 * DC API engine (iblue_dcapi.c), so the text really is what the adapter answers; see run.sh. */
#include <stdio.h>
#include <string.h>

#include "ui_motion.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define SAME(got, want) do { checks++; if (strcmp((got), (want)) != 0) { fails++; printf("FAIL %s:%d: got \"%s\" want \"%s\"\n", __FILE__, __LINE__, got, want); } } while (0)

static void row(const uim* m, int r, char* l, char* v) {
    const char* h;
    uim_row(m, r, l, 32, v, 32, &h);
    CHECK(strlen(h) <= 40);
}

#ifdef WITH_ENGINE
#include "adapter/adapter.h"
#include "iblue/iblue_gameid.h"
#include "iblue/iblue_dcapi.h"

void adapter_q_fb(struct raw_fb* fb_data) { (void)fb_data; }
static struct { uint8_t op, a, b; } reqs[32];
static int nreq;
int iblue_dcapi_cfg_request(uint8_t op, uint8_t a, uint8_t b) { if (nreq < 32) { reqs[nreq].op = op; reqs[nreq].a = a; reqs[nreq].b = b; nreq++; } return 1; }
const struct iblue_mapent* iblue_dcapi_cfg_map(uint32_t port, size_t* count) { (void)port; *count = 0; return NULL; }
int iblue_dcapi_cfg_set_map(uint8_t port, uint8_t out, const struct iblue_mapent* e, size_t n) { (void)port; (void)out; (void)e; (void)n; return 1; }

static int engine(void* user, const char* req, char* ans, size_t cap) {
    (void)user;
    return (int)iblue_dcapi_handle(req, strlen(req), ans, cap);
}
#endif

static int none(void* user, const char* req, char* ans, size_t cap) { (void)user; (void)req; (void)ans; (void)cap; return -1; }
static int old_adapter(void* user, const char* req, char* ans, size_t cap) {
    (void)user; (void)req;
    snprintf(ans, cap, "dc1\nerr motion.* unknown\n");
    return (int)strlen(ans);
}

int main(void) {
    uim m;
    char l[32], v[32];

    /* an adapter that does not answer, or that has no motion */
    uim_init(&m, none, NULL);
    CHECK(uim_refresh(&m) < 0 && !uim_supported(&m));
    uim_init(&m, old_adapter, NULL);
    CHECK(uim_refresh(&m) == 0 && !uim_supported(&m) && m.reachable);
    CHECK(uim_activate(&m, UIM_ROW_MODE) == 0);
    SAME(m.msg, "This adapter has no Joy-Con motion");

#ifdef WITH_ENGINE
    uim_init(&m, engine, NULL);
    CHECK(uim_refresh(&m) == 0 && !uim_supported(&m)); /* the firmware has not reported motion yet: the keys do not exist */
    iblue_dcapi_motion_set(0, 0, 0);
    CHECK(uim_refresh(&m) == 1 && uim_supported(&m));
    CHECK(uim_rows() == UIM_ROWS);
    row(&m, UIM_ROW_MODE, l, v); SAME(l, "Motion mode"); SAME(v, "Off");
    row(&m, UIM_ROW_STATE, l, v); SAME(v, "Off");
    row(&m, UIM_ROW_LEFT, l, v); SAME(v, "Absent");

    /* a pose with the mode off is refused on the page, nothing is sent */
    nreq = 0;
    CHECK(uim_activate(&m, UIM_ROW_FORWARD) == 0 && nreq == 0);
    SAME(m.msg, "Switch motion mode on first");

    /* A on the mode row switches it on */
    CHECK(uim_activate(&m, UIM_ROW_MODE) == 1);
    CHECK(nreq == 1 && reqs[0].op == IBLUE_CFG_MOTION_MODE && reqs[0].a == 1);
    row(&m, UIM_ROW_MODE, l, v); SAME(v, "On");
    row(&m, UIM_ROW_STATE, l, v); SAME(v, "No Joy-Con");
    SAME(m.msg, "On: now connect the Joy-Cons again");

    /* Joy-Cons connect, not calibrated */
    iblue_dcapi_motion_set(1, 1, 1);
    CHECK(uim_tick(&m, 800) == 1); /* the state is read on its own */
    row(&m, UIM_ROW_STATE, l, v); SAME(v, "Set forward");
    row(&m, UIM_ROW_RIGHT, l, v); SAME(v, "Set forward");

    /* set forward: the adapter is asked for 5 seconds, the page counts down */
    nreq = 0;
    CHECK(uim_activate(&m, UIM_ROW_FORWARD) == 1);
    CHECK(nreq == 1 && reqs[0].op == IBLUE_CFG_MOTION_POSE && reqs[0].a == 0 && reqs[0].b == UIM_POSE_SECONDS);
    row(&m, UIM_ROW_FORWARD, l, v); SAME(v, "Hold still 7");
    CHECK(uim_activate(&m, UIM_ROW_FORWARD) == 0); /* a second press while it counts does nothing */
    CHECK(nreq == 1);
    CHECK(uim_tick(&m, 2000) == 1);
    row(&m, UIM_ROW_FORWARD, l, v); SAME(v, "Hold still 5");
    iblue_dcapi_motion_set(1, 2, 2); /* the adapter took the pose */
    CHECK(uim_tick(&m, 5100) == 1);
    row(&m, UIM_ROW_FORWARD, l, v); SAME(v, "A to start");
    SAME(m.msg, "Done: the Joy-Cons are ready");
    row(&m, UIM_ROW_STATE, l, v); SAME(v, "Ready");

    /* up and out are other poses */
    nreq = 0;
    CHECK(uim_activate(&m, UIM_ROW_UP) == 1 && reqs[0].a == 1);
    uim_tick(&m, 8000);
    CHECK(uim_activate(&m, UIM_ROW_OUT) == 1 && reqs[1].a == 3);
    uim_tick(&m, 8000);

    /* forget asks twice */
    nreq = 0;
    CHECK(uim_activate(&m, UIM_ROW_RESET) == 0 && nreq == 0);
    row(&m, UIM_ROW_RESET, l, v); SAME(v, "A again");
    CHECK(uim_activate(&m, UIM_ROW_RESET) == 1 && nreq == 1 && reqs[0].op == IBLUE_CFG_MOTION_RESET);
    row(&m, UIM_ROW_RESET, l, v); SAME(v, "A twice");

    /* another row in between cancels the question */
    uim_activate(&m, UIM_ROW_RESET);
    uim_activate(&m, UIM_ROW_STATE);
    nreq = 0;
    CHECK(uim_activate(&m, UIM_ROW_RESET) == 0 && nreq == 0);

    /* and off again */
    CHECK(uim_activate(&m, UIM_ROW_MODE) == 1);
    row(&m, UIM_ROW_MODE, l, v); SAME(v, "Off");
#else
    (void)l; (void)v; (void)row;
    printf("(IBLUE_FW not set: the part that talks to the firmware's engine was skipped)\n");
#endif
    printf("ui_motion: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
