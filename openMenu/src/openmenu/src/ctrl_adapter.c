/* ctrl_adapter: see ctrl_adapter.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ctrl_adapter.h"
#include "vm2/dcapi_text.h"

#ifndef CTA_HOST_TEST
#include <dc/maple.h>
#include <kos/thread.h>

#include "vm2/dcapi.h"
#endif

#define PORTS 4
#define CHOICES 12
#define WORD 16

enum { KIND_NONE, KIND_P2M, KIND_IBLUE };

typedef struct {
    int port;                    /* the port whose controller unit is asked */
    char key[32];                /* "display.brightness", "port.B.mode" */
    char label[24], help[48];
    int numeric;                 /* the value is a number: the nearest choice is the current one */
    int n;
    char value[WORD];            /* what the adapter says it is now */
    char choice[CHOICES][WORD];  /* the values it takes, in its spelling */
    char name[CHOICES][WORD];    /* shown */
} row_t;

static struct {
    int kind[PORTS];             /* what answered on each port's controller unit */
    int commit[PORTS];           /* the adapter stores a change only on "call sys.commit" */
    int nrows;
    row_t row[CTA_ROWS];
} a;

/* ---- the transport: always the unit on the bus of the port, never a memory card ----------------------------------------- */

#ifdef CTA_HOST_TEST
static cta_transport host_send;

void
cta_host_attach(cta_transport send) {
    memset(&a, 0, sizeof(a));
    host_send = send;
}

static int
request(int port, const char* ops, char* ans, size_t cap) {
    char req[DCAPI_REQ_MAX + 1];
    if (!host_send || dcapi_build(req, sizeof(req), ops) < 0) {
        return -1;
    }
    return host_send(port, req, ans, cap);
}

static int
port_present(int port) {
    (void)port;
    return host_send != NULL;
}
#else
static unsigned last_sig;
static int scanned;

static int
request(int port, const char* ops, char* ans, size_t cap) {
    char req[DCAPI_REQ_MAX + 1];
    maple_device_t* dev = maple_enum_dev(port, 0);
    if (!dev || !dev->valid || dcapi_build(req, sizeof(req), ops) < 0) {
        return -1;
    }
    return dcapi_request(dev, req, ans, cap);
}

static int
port_present(int port) {
    maple_device_t* dev = maple_enum_dev(port, 0);
    return dev && dev->valid;
}

/* What is on the ports: the search runs again only when this changes (plugged in, pulled out, restarted in another mode). */
static unsigned
devices_sig(void) {
    unsigned sig = 2166136261u;
    for (int p = 0; p < MAPLE_PORT_COUNT; p++) {
        for (int u = 0; u < MAPLE_UNIT_COUNT; u++) {
            maple_device_t* d = maple_enum_dev(p, u);
            sig = (sig ^ (d && d->valid ? d->info.functions + 1u : 0u)) * 16777619u;
        }
    }
    return sig;
}
#endif

/* Which adapter answers on a port's controller unit: one "get hello.name". */
static void
search(void) {
    int iblue = 0;
    for (int p = 0; p < PORTS; p++) {
        char ans[DCAPI_ANS_MAX + 1], name[32];
        a.kind[p] = KIND_NONE;
        if (!port_present(p) || request(p, "get hello.name\n", ans, sizeof(ans)) < 0 ||
            dcapi_answer(ans, "hello.name", name, sizeof(name)) != 1) {
            continue;
        }
        if (!strncasecmp(name, "Pico2Maple", 10)) {
            a.kind[p] = KIND_P2M;
        } else if (!strncasecmp(name, "iBlueControlMod", 15) && !iblue) {
            a.kind[p] = KIND_IBLUE; /* one adapter for all its ports: asked on the first only */
            iblue = 1;
        }
    }
}

/* ---- rows -------------------------------------------------------------------------------------------------------------- */

static row_t*
add_row(int port, const char* key, const char* label, const char* help) {
    if (a.nrows >= CTA_ROWS) {
        return NULL;
    }
    row_t* r = &a.row[a.nrows++]; /* take() gives it back when the adapter has no such key */
    memset(r, 0, sizeof(*r));
    r->port = port;
    snprintf(r->key, sizeof(r->key), "%s", key);
    snprintf(r->label, sizeof(r->label), "%s", label);
    snprintf(r->help, sizeof(r->help), "%s", help);
    return r;
}

static void
add_choice(row_t* r, const char* value, const char* name) {
    if (r->n < CHOICES) {
        snprintf(r->choice[r->n], WORD, "%s", value);
        snprintf(r->name[r->n], WORD, "%s", name);
        r->n++;
    }
}

static const char*
shown(const char* v) {
    static const char* const names[][2] = {
        {"normal", "Controller"}, {"dual", "Dual stick"}, {"keyboard", "Keyboard"}, {"mouse", "Mouse"}, {"racing", "Racing wheel"},
        {"maracas", "Maracas"}, {"none", "None"}, {"vmu", "Memory card"}, {"rumble", "Vibration"}, {"both", "Card + vibration"},
        {"standard", "Standard"}, {"all", "All buttons"}, {"custom", "Custom"}, {"twin", "Twin stick"}, {"fight", "Fight stick"},
    };
    static char other[WORD];
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (!strcmp(v, names[i][0])) {
            return names[i][1];
        }
    }
    snprintf(other, sizeof(other), "%s", v); /* a value this menu does not know: as the adapter writes it, first letter up */
    if (other[0] >= 'a' && other[0] <= 'z') {
        other[0] = (char)(other[0] - 'a' + 'A');
    }
    return other;
}

static int
find(const row_t* r, const char* v) {
    if (r->numeric) {
        int best = -1, dist = 1 << 30, x = atoi(v);
        for (int i = 0; i < r->n; i++) {
            int d = abs(atoi(r->choice[i]) - x);
            if (d < dist) {
                dist = d;
                best = i;
            }
        }
        return best;
    }
    for (int i = 0; i < r->n; i++) {
        if (!strcasecmp(r->choice[i], v)) {
            return i;
        }
    }
    return -1;
}

/* the row's value from the answer; the row is dropped when the adapter has no such key */
static void
take(const char* ans, row_t* r) {
    if (!r) {
        return;
    }
    if (dcapi_answer(ans, r->key, r->value, sizeof(r->value)) != 1 || r->n < 2) {
        a.nrows--; /* it was the last one added */
        return;
    }
    if (find(r, r->value) < 0 && r->n < CHOICES) {
        add_choice(r, r->value, shown(r->value)); /* a value this menu does not offer, so it can be shown */
    }
}

static int
store_by_commit(const char* ans) {
    char v[WORD];
    return !(dcapi_answer(ans, "sys.store", v, sizeof(v)) == 1 && !strcmp(v, "auto")); /* unknown: commit, a device that stores by itself says "nothing" */
}

/* Pico2Maple (unified firmware): its own settings, one request. */
static void
read_p2m(int p) {
    char ans[DCAPI_ANS_MAX + 1];
    if (request(p, "get display.brightness\nget display.flip\nget vmu.icons\nget vmu.slot\nget port.self.layout\nget port.self.switch.auto\n"
                   "get sys.store\n", ans, sizeof(ans)) < 0) {
        return;
    }
    a.commit[p] = store_by_commit(ans);
    row_t* r = add_row(p, "display.brightness", "Display brightness", "Brightness of the OLED screen");
    if (r) {
        r->numeric = 1;
        for (int i = 0; i <= 100; i += 10) {
            char v[8], n[8];
            snprintf(v, sizeof(v), "%d", i);
            snprintf(n, sizeof(n), "%d%%", i);
            add_choice(r, v, n);
        }
    }
    take(ans, r);
    r = add_row(p, "display.flip", "Flip display", "Turn the OLED screen 180 degrees");
    if (r) {
        add_choice(r, "normal", "Normal");
        add_choice(r, "flipped", "Flipped");
    }
    take(ans, r);
    r = add_row(p, "vmu.icons", "Memory card icons", "BIOS icons 0-9 per slot, or the card image");
    if (r) {
        add_choice(r, "bios", "BIOS icons 0-9");
        add_choice(r, "image", "Card image");
    }
    take(ans, r);
    r = add_row(p, "vmu.slot", "Memory card slot", "The slot the console sees (or the game's)");
    if (r) {
        for (int i = 1; i <= 10; i++) {
            char v[4];
            snprintf(v, sizeof(v), "%d", i);
            add_choice(r, v, v);
        }
        add_choice(r, "gameid", "Game ID");
    }
    take(ans, r);
    r = add_row(p, "port.self.layout", "Pad layout", "Button layout of the USB pad");
    if (r) {
        static const char* const l[] = {"standard", "all", "twin", "fight", "custom"};
        for (int i = 0; i < 5; i++) {
            add_choice(r, l[i], shown(l[i]));
        }
    }
    take(ans, r);
    r = add_row(p, "port.self.switch.auto", "Keyboard/mouse switch", "Switch by itself to the one being used");
    if (r) {
        add_choice(r, "on", "On");
        add_choice(r, "off", "Off");
    }
    take(ans, r);
}

/* iBlueControlMod: type, accessory and layout of each port it has, with the values it says it takes; one request per port. */
static void
read_iblue(int p) {
    static const char* const key[3] = {"mode", "acc", "layout"};
    static const char* const what[3] = {"type", "accessory", "layout"};
    static const char* const help[3] = {"what the console sees", "memory card and vibration", "button layout"};
    for (int x = 0; x < PORTS; x++) {
        char ops[256], ans[DCAPI_ANS_MAX + 1], k[32], st[WORD] = "", list[96];
        char X = (char)('A' + x);
        snprintf(ops, sizeof(ops), "get port.%c.state\nget port.%c.mode\nget port.%c.modes\nget port.%c.acc\nget port.%c.accs\n"
                 "get port.%c.layout\nget port.%c.layouts\n%s", X, X, X, X, X, X, X, x == 0 ? "get sys.store\n" : "");
        if (request(p, ops, ans, sizeof(ans)) < 0) {
            return;
        }
        if (x == 0) {
            a.commit[p] = store_by_commit(ans);
        }
        snprintf(k, sizeof(k), "port.%c.state", X);
        dcapi_answer(ans, k, st, sizeof(st));
        const char* who = !strcmp(st, "bt") ? "Bluetooth pad: " : !strcmp(st, "wired") ? "Wired pad: " : !strcmp(st, "empty") ? "Nothing on it: " : "";
        for (int i = 0; i < 3; i++) {
            char label[24], h[48];
            snprintf(k, sizeof(k), "port.%c.%ss", X, key[i]);
            if (dcapi_answer(ans, k, list, sizeof(list)) != 1) {
                continue; /* no such port, or not this setting */
            }
            snprintf(k, sizeof(k), "port.%c.%s", X, key[i]);
            snprintf(label, sizeof(label), "Port %c %s", X, what[i]);
            snprintf(h, sizeof(h), "%s%s", who, help[i]);
            if (!who[0]) {
                h[0] = (char)(h[0] - 'a' + 'A');
            }
            row_t* r = add_row(p, k, label, h);
            for (char* v = list; r && *v;) {
                char* end = strchr(v, ',');
                if (end) {
                    *end = '\0';
                }
                if (!(i == 2 && !strcmp(v, "custom"))) { /* a custom layout is made on the adapter's own page; shown only while it is the one */
                    add_choice(r, v, shown(v));
                }
                v = end ? end + 1 : v + strlen(v);
            }
            take(ans, r);
        }
    }
}

void
cta_refresh(void) {
#ifndef CTA_HOST_TEST
    unsigned sig = devices_sig();
    if (!scanned || sig != last_sig) {
        search();
        scanned = 1;
        last_sig = sig;
    }
#else
    search();
#endif
    a.nrows = 0;
    for (int p = 0; p < PORTS; p++) {
        int before = a.nrows;
        if (a.kind[p] == KIND_P2M) {
            read_p2m(p);
        } else if (a.kind[p] == KIND_IBLUE) {
            read_iblue(p);
        }
        if (a.kind[p] != KIND_NONE && a.nrows == before) {
#ifndef CTA_HOST_TEST
            scanned = 0; /* it did not answer this time: look again next time */
#endif
        }
    }
}

int
cta_rows(void) {
    return a.nrows;
}

#define ROW_OK(r) ((r) >= 0 && (r) < a.nrows)

const char*
cta_group(int row) {
    static char text[CTA_ROWS][24];
    if (!ROW_OK(row)) {
        return "";
    }
    int p = a.row[row].port;
    if (a.kind[p] == KIND_IBLUE) {
        return "iBlueControlMod";
    }
    snprintf(text[row], sizeof(text[row]), "Pico2Maple (port %c)", 'A' + p);
    return text[row];
}

const char*
cta_label(int row) {
    return ROW_OK(row) ? a.row[row].label : "";
}

const char*
cta_help(int row) {
    return ROW_OK(row) ? a.row[row].help : "";
}

int
cta_choice_count(int row) {
    return ROW_OK(row) ? a.row[row].n : 0;
}

const char*
cta_choice_name(int row, int choice) {
    return ROW_OK(row) && choice >= 0 && choice < a.row[row].n ? a.row[row].name[choice] : "?";
}

int
cta_get(int row) {
    int i = ROW_OK(row) ? find(&a.row[row], a.row[row].value) : -1;
    return i < 0 ? 0 : i;
}

int
cta_set(int row, int choice) {
    char ops[96], ans[DCAPI_ANS_MAX + 1], got[48];
    if (!ROW_OK(row) || choice < 0 || choice >= a.row[row].n) {
        return -1;
    }
    row_t* r = &a.row[row];
    const char* v = r->choice[choice];
    if (choice == cta_get(row) || (a.kind[r->port] == KIND_IBLUE && strstr(r->key, ".layout") && !strcmp(v, "custom"))) {
        return 0; /* nothing to change (iBlue's custom layout cannot be set, only kept) */
    }
    /* the set and the commit in one request: a type change restarts the adapter's wired side, which a second request could miss */
    snprintf(ops, sizeof(ops), "set %s %s\n%s", r->key, v, a.commit[r->port] ? "call sys.commit\n" : "");
    if (request(r->port, ops, ans, sizeof(ans)) < 0 || dcapi_answer(ans, r->key, got, sizeof(got)) != 1) {
        return -1;
    }
    memmove(r->value, v, strlen(v) + 1); /* both WORD long */
    if (a.commit[r->port] && dcapi_answer(ans, "sys.commit", got, sizeof(got)) == 1 && strstr(got, "busy")) {
#ifndef CTA_HOST_TEST
        thd_sleep(100);
#endif
        request(r->port, "call sys.commit\n", ans, sizeof(ans)); /* once more; still busy: the change stays live until the next one */
    }
    return 0;
}
