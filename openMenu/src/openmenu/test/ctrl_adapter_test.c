/* Host test of ctrl_adapter.c against an iBlueControlMod (docs/dcapi.md there) on port A and a Pico2Maple (unified) on port C:
 *   gcc -Wall -DCTA_HOST_TEST -Isrc test/ctrl_adapter_test.c src/ctrl_adapter.c src/vm2/dcapi_text.c && ./a.out */
#include <stdio.h>
#include <string.h>

#include "ctrl_adapter.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* port A: an iBlueControlMod (two ports A, B), port C: a Pico2Maple (unified firmware), port B and D: nothing that speaks the language */
static char mode[2][16] = {"normal", "dual"}, acc[2][16] = {"vmu", "none"}, layout[2][16] = {"all", "custom"};
static char bright[8] = "70", slot[8] = "3";
static int unsaved, commits, requests, asked_port[8], nasked;
static char last_req[300];
static int dead;

static void
add(char* out, size_t cap, const char* kind, const char* key, const char* val) {
    size_t n = strlen(out);
    snprintf(out + n, cap - n, "%s %s %s\n", kind, key, val);
}

static int
fake(int port, const char* req, char* ans, size_t cap) {
    requests++;
    asked_port[nasked++ & 7] = port;
    snprintf(last_req, sizeof(last_req), "%s", req);
    if (dead || (port != 0 && port != 2)) {
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
                add(ans, cap, "ok", key, unsaved ? "done" : "nothing");
                unsaved = 0;
            }
            continue;
        }
        if (!strcmp(key, "hello.name")) {
            add(ans, cap, "ok", key, port == 0 ? "iBlueControlMod" : "Pico2Maple");
            continue;
        }
        if (port == 2) { /* the Pico2Maple: stores by itself */
            if (!strcmp(key, "sys.store")) {
                add(ans, cap, "ok", key, "auto");
            } else if (!strcmp(key, "display.brightness")) {
                if (!strcmp(verb, "set")) snprintf(bright, sizeof(bright), "%s", val);
                add(ans, cap, "ok", key, bright);
            } else if (!strcmp(key, "vmu.slot")) {
                if (!strcmp(verb, "set")) snprintf(slot, sizeof(slot), "%s", val);
                add(ans, cap, "ok", key, slot);
            } else if (!strcmp(key, "display.flip")) {
                add(ans, cap, "ok", key, "normal");
            } else if (!strcmp(key, "vmu.icons")) {
                add(ans, cap, "ok", key, "bios");
            } else if (!strcmp(key, "port.self.layout")) {
                add(ans, cap, "ok", key, "standard");
            } else {
                add(ans, cap, "err", key, "unknown");
            }
            continue;
        }
        if (!strcmp(key, "sys.store")) {
            add(ans, cap, "ok", key, "commit");
            continue;
        }
        char X;
        char what[16];
        if (sscanf(key, "port.%c.%15s", &X, what) != 2) {
            add(ans, cap, "err", key, "unknown");
            continue;
        }
        int i = X - 'A';
        if (i < 0 || i > 1) {
            add(ans, cap, "err", key, "unavailable no such port");
            continue;
        }
        char* v = !strcmp(what, "mode") ? mode[i] : !strcmp(what, "acc") ? acc[i] : !strcmp(what, "layout") ? layout[i] : NULL;
        if (!strcmp(verb, "set")) {
            if (!v || (v == layout[i] && !strcmp(val, "custom"))) {
                add(ans, cap, "err", key, "bad");
                continue;
            }
            snprintf(v, 16, "%s", val);
            unsaved = 1;
            add(ans, cap, "ok", key, val);
            continue;
        }
        if (v) {
            add(ans, cap, "ok", key, v);
        } else if (!strcmp(what, "state")) {
            add(ans, cap, "ok", key, i == 0 ? "bt" : "empty");
        } else if (!strcmp(what, "modes")) {
            add(ans, cap, "ok", key, "normal,dual,keyboard,mouse,racing,maracas");
        } else if (!strcmp(what, "accs")) {
            add(ans, cap, "ok", key, "none,vmu,rumble,both");
        } else if (!strcmp(what, "layouts")) {
            add(ans, cap, "ok", key, "standard,all,custom");
        }
    }
    CHECK(strlen(ans) < 400); /* the firmware cuts its answer at 400 bytes: everything must fit */
    return (int)strlen(ans);
}

int
main(void) {
    /* nothing on the ports: no rows */
    cta_host_attach(NULL);
    cta_refresh();
    CHECK(cta_rows() == 0);

    cta_host_attach(fake);
    cta_refresh();
    /* only ports A and C answered as adapters: 4 hello probes, then A (iBlue: one request per port) and C (one request) */
    CHECK(cta_rows() == 6 + 5); /* iBlue: two ports x three; Pico2Maple: five (this one has no keyboard/mouse switch key: its row is dropped) */
    CHECK(!strcmp(cta_group(0), "iBlueControlMod") && !strcmp(cta_group(6), "Pico2Maple (port C)"));
    CHECK(!strcmp(cta_label(0), "Port A type") && !strcmp(cta_label(3), "Port B type"));
    CHECK(!strcmp(cta_help(0), "Bluetooth pad: what the console sees") && !strcmp(cta_help(4), "Nothing on it: memory card and vibration"));
    CHECK(!strcmp(cta_choice_name(0, cta_get(0)), "Controller") && !strcmp(cta_choice_name(3, cta_get(3)), "Dual stick"));
    CHECK(cta_choice_count(2) == 2 && !strcmp(cta_choice_name(2, cta_get(2)), "All buttons")); /* custom only where it is the current one */
    CHECK(cta_choice_count(5) == 3 && !strcmp(cta_choice_name(5, cta_get(5)), "Custom"));
    CHECK(!strcmp(cta_label(6), "Display brightness") && !strcmp(cta_choice_name(6, cta_get(6)), "70%") && cta_choice_count(6) == 11);
    CHECK(!strcmp(cta_label(9), "Memory card slot") && !strcmp(cta_choice_name(9, cta_get(9)), "3") && cta_choice_count(9) == 11);

    /* iBlue: set and commit in one request, to port A's controller unit */
    requests = commits = nasked = 0;
    CHECK(cta_set(0, 1) == 0);
    CHECK(requests == 1 && commits == 1 && asked_port[0] == 0 && !strcmp(mode[0], "dual"));
    CHECK(strstr(last_req, "set port.A.mode dual\ncall sys.commit\n") != NULL);
    requests = 0;
    CHECK(cta_set(0, 1) == 0 && requests == 0);                 /* same value: nothing sent */
    CHECK(cta_set(5, cta_get(5)) == 0 && requests == 0);        /* "custom": nothing sent */
    CHECK(cta_set(5, 0) == 0 && !strcmp(layout[1], "standard"));
    /* Pico2Maple: stores by itself, no commit; the request goes to port C's controller unit only */
    requests = commits = nasked = 0;
    CHECK(cta_set(6, 10) == 0 && !strcmp(bright, "100"));
    CHECK(requests == 1 && commits == 0 && asked_port[0] == 2 && strstr(last_req, "call") == NULL);
    CHECK(cta_set(9, 10) == 0 && !strcmp(slot, "gameid"));
    /* the adapter gone */
    dead = 1;
    CHECK(cta_set(1, 2) == -1 && !strcmp(cta_choice_name(1, cta_get(1)), "Memory card"));
    cta_refresh();
    CHECK(cta_rows() == 0);
    dead = 0;
    CHECK(cta_set(99, 0) == -1 && !strcmp(cta_label(-1), "") && cta_choice_count(42) == 0);

    printf("ctrl_adapter: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
