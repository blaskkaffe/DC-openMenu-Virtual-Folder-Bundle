/* Host test of ctrl_adapter.c against an adapter that answers like iBlueControlMod's firmware (docs/dcapi.md there):
 *   gcc -Wall -DCTA_HOST_TEST -Isrc test/ctrl_adapter_test.c src/ctrl_adapter.c src/vm2/dcapi_text.c && ./a.out */
#include <stdio.h>
#include <string.h>

#include "ctrl_adapter.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* the fake adapter: two ports (A, B), store by commit */
static char mode[2][16] = {"normal", "dual"}, acc[2][16] = {"vmu", "none"}, layout[2][16] = {"all", "custom"};
static int unsaved, commits, requests;
static char last_req[300];
static int dead;

static void
line(char* out, size_t cap, const char* fmt, const char* a, const char* b) {
    size_t n = strlen(out);
    snprintf(out + n, cap - n, fmt, a, b);
}

static int
fake(const char* req, char* ans, size_t cap) {
    requests++;
    snprintf(last_req, sizeof(last_req), "%s", req);
    if (dead) {
        return -1;
    }
    CHECK(strncmp(req, "dc1\n", 4) == 0);
    snprintf(ans, cap, "dc1\n");
    const char* p = req + 4;
    while (*p) {
        char l[96];
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        snprintf(l, sizeof(l), "%.*s", (int)n, p);
        p += n + (e ? 1 : 0);
        char verb[8], key[48], val[48] = "";
        if (sscanf(l, "%7s %47s %47s", verb, key, val) < 2) {
            continue;
        }
        if (!strcmp(verb, "call")) {
            if (!strcmp(key, "sys.commit")) {
                commits++;
                line(ans, cap, "ok %s %s\n", key, unsaved ? "done" : "nothing");
                unsaved = 0;
            }
            continue;
        }
        if (!strcmp(key, "sys.store")) {
            line(ans, cap, "ok %s %s\n", key, "commit");
            continue;
        }
        char X;
        char what[16];
        if (sscanf(key, "port.%c.%15s", &X, what) != 2) {
            line(ans, cap, "err %s %s\n", key, "unknown");
            continue;
        }
        int i = X - 'A';
        if (i < 0 || i > 3 || i >= 2) {
            line(ans, cap, "err %s %s\n", key, "unavailable no such port");
            continue;
        }
        char* v = !strcmp(what, "mode") ? mode[i] : !strcmp(what, "acc") ? acc[i] : !strcmp(what, "layout") ? layout[i] : NULL;
        if (!strcmp(verb, "set")) {
            if (!v || (v == layout[i] && !strcmp(val, "custom"))) {
                line(ans, cap, "err %s %s\n", key, "bad");
                continue;
            }
            snprintf(v, 16, "%s", val);
            unsaved = 1;
            line(ans, cap, "ok %s %s\n", key, val);
            continue;
        }
        if (v) {
            line(ans, cap, "ok %s %s\n", key, v);
        } else if (!strcmp(what, "state")) {
            line(ans, cap, "ok %s %s\n", key, i == 0 ? "bt" : "empty");
        } else if (!strcmp(what, "modes")) {
            line(ans, cap, "ok %s %s\n", key, "normal,dual,keyboard,mouse,racing,maracas");
        } else if (!strcmp(what, "accs")) {
            line(ans, cap, "ok %s %s\n", key, "none,vmu,rumble,both");
        } else if (!strcmp(what, "layouts")) {
            line(ans, cap, "ok %s %s\n", key, "standard,all,custom");
        }
    }
    CHECK(strlen(ans) < 400); /* the firmware cuts its answer at 400 bytes: everything must fit */
    return (int)strlen(ans);
}

int
main(void) {
    /* no adapter: no rows, nothing asked */
    cta_host_attach(NULL, NULL);
    cta_refresh();
    CHECK(cta_rows() == 0 && !strcmp(cta_name(), ""));

    cta_host_attach("iBlueControlMod", fake);
    requests = 0;
    cta_refresh();
    CHECK(requests == 4); /* one request per port */
    CHECK(!strcmp(cta_name(), "iBlueControlMod"));
    CHECK(cta_rows() == 6); /* ports A and B, three settings each */
    CHECK(!strcmp(cta_label(0), "Port A type"));
    CHECK(!strcmp(cta_label(1), "Port A accessory"));
    CHECK(!strcmp(cta_label(2), "Port A layout"));
    CHECK(!strcmp(cta_label(3), "Port B type"));
    CHECK(!strcmp(cta_help(0), "Bluetooth pad: what the console sees"));
    CHECK(!strcmp(cta_help(4), "Nothing on it: memory card and vibration"));
    CHECK(cta_choice_count(0) == 6 && !strcmp(cta_choice_name(0, cta_get(0)), "Controller"));
    CHECK(!strcmp(cta_choice_name(3, cta_get(3)), "Dual stick"));
    CHECK(!strcmp(cta_choice_name(1, cta_get(1)), "Memory card"));
    /* layout: "custom" is only offered where it is the current one, and cannot be chosen into */
    CHECK(cta_choice_count(2) == 2 && !strcmp(cta_choice_name(2, cta_get(2)), "All buttons"));
    CHECK(cta_choice_count(5) == 3 && !strcmp(cta_choice_name(5, cta_get(5)), "Custom"));

    /* a change: set and commit in one request */
    requests = commits = 0;
    CHECK(cta_set(0, 1) == 0);
    CHECK(requests == 1 && commits == 1 && !strcmp(mode[0], "dual"));
    CHECK(strstr(last_req, "set port.A.mode dual\ncall sys.commit\n") != NULL);
    CHECK(!strcmp(cta_choice_name(0, cta_get(0)), "Dual stick"));
    /* the same value again: nothing sent */
    requests = 0;
    CHECK(cta_set(0, 1) == 0 && requests == 0);
    /* choosing "custom" sends nothing */
    CHECK(cta_set(5, cta_get(5)) == 0 && requests == 0);
    /* leaving custom for standard works */
    CHECK(cta_set(5, 0) == 0 && !strcmp(layout[1], "standard"));
    /* the adapter gone: the row keeps its value, the set says so */
    dead = 1;
    CHECK(cta_set(1, 0) == -1 && !strcmp(cta_choice_name(1, cta_get(1)), "Memory card"));
    cta_refresh();
    CHECK(cta_rows() == 0);
    dead = 0;
    /* bad rows */
    CHECK(cta_set(99, 0) == -1 && !strcmp(cta_label(-1), "") && cta_choice_count(42) == 0);

    printf("ctrl_adapter: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
