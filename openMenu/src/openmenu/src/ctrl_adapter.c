/* ctrl_adapter: see ctrl_adapter.h. */
#include <stdio.h>
#include <string.h>

#include "ctrl_adapter.h"
#include "vm2/dcapi_text.h"

#ifndef CTA_HOST_TEST
#include <dc/maple.h>
#include <kos/thread.h>

#include "vm2/dcapi.h"
#include "vm2/vm2_api.h"
#endif

#define CHOICES 8
#define WORD 16

typedef struct {
    int n;                       /* 0: the adapter does not offer this setting on the port */
    char value[WORD];            /* what the adapter says it is now */
    char choice[CHOICES][WORD];  /* the values it takes, in its spelling */
} setting;

static const char* const key_of[CTA_KINDS] = {"mode", "acc", "layout"};

static struct {
    int found;
    char name[24];
    int commit;                  /* sys.store is "commit": a change is kept only after "call sys.commit" */
    char state[CTA_PORTS][WORD]; /* empty, bt, wired */
    setting set[CTA_PORTS][CTA_KINDS];
    int nrows;
    unsigned char row_port[CTA_ROWS], row_kind[CTA_ROWS];
} a;

/* ---- the transport ------------------------------------------------------------------------------------------------------ */

#ifdef CTA_HOST_TEST
static cta_transport host_send;

void
cta_host_attach(const char* name, cta_transport send) {
    memset(&a, 0, sizeof(a));
    host_send = send;
    a.found = send != NULL;
    snprintf(a.name, sizeof(a.name), "%s", name ? name : "");
}

static int
request(const char* ops, char* ans, size_t cap) {
    char req[DCAPI_REQ_MAX + 1];
    if (!host_send || dcapi_build(req, sizeof(req), ops) < 0) {
        return -1;
    }
    return host_send(req, ans, cap);
}
#else
static int dev_port = -1, dev_unit = -1;
static int known_port = -1; /* the port the adapter was found on: its controller unit answers too (when the port is set to no memory card) */
static unsigned last_sig;
static int scanned;

static int
request(const char* ops, char* ans, size_t cap) {
    char req[DCAPI_REQ_MAX + 1];
    maple_device_t* dev = dev_port >= 0 ? maple_enum_dev(dev_port, dev_unit) : NULL;
    if ((!dev || !dev->valid) && dev_port >= 0 && dev_unit != 0) {
        dev = maple_enum_dev(dev_port, 0); /* its memory card was just switched off: the controller unit of the same adapter port */
    }
    if (!dev || !dev->valid || dcapi_build(req, sizeof(req), ops) < 0) {
        return -1;
    }
    return dcapi_request(dev, req, ans, cap);
}

/* What is on the ports: a new search only when this changes (a device plugged in, pulled out, or restarted in another mode). */
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

/* The memory card units only (the marker and the capability bits are in their device info): the first that says dc1 with the api
 * and port bits is the adapter. A device info request is what the BIOS sends every card anyway. */
static void
search(void) {
    a.found = 0;
    dev_port = dev_unit = -1;
    for (int p = 0; p < MAPLE_PORT_COUNT && !a.found; p++) {
        for (int u = 1; u < MAPLE_UNIT_COUNT && !a.found; u++) {
            maple_device_t* d = maple_enum_dev(p, u);
            char ext[40];
            uint32_t caps = 0;
            if (!d || !d->valid || !(d->info.functions & MAPLE_FUNC_MEMCARD) || vm2_extended_info(d, ext) != 0) {
                continue;
            }
            if (!dcapi_caps_parse(ext + 16, &caps) || !dcapi_has(caps, DCAPI_CAP_API) || !dcapi_has(caps, DCAPI_CAP_PORT)) {
                continue;
            }
            int n = 16;
            while (n > 0 && (ext[n - 1] == ' ' || ext[n - 1] == '\0')) {
                n--;
            }
            snprintf(a.name, sizeof(a.name), "%.*s", n, ext);
            dev_port = known_port = p;
            dev_unit = u;
            a.found = 1;
        }
    }
    /* No memory card says it: the adapter found before may have been set to no memory card on its port. Its controller unit takes
     * the same requests (the first read shows whether it still answers; if not, it is forgotten). */
    if (!a.found && known_port >= 0) {
        maple_device_t* d = maple_enum_dev(known_port, 0);
        if (d && d->valid) {
            dev_port = known_port;
            dev_unit = 0;
            a.found = 1;
        } else {
            known_port = -1;
        }
    }
}
#endif

/* ---- the text --------------------------------------------------------------------------------------------------------- */

/* "normal,dual,keyboard" into the choices; returns how many */
static int
split(const char* list, char out[CHOICES][WORD]) {
    int n = 0;
    while (*list && n < CHOICES) {
        const char* end = strchr(list, ',');
        size_t len = end ? (size_t)(end - list) : strlen(list);
        while (len && list[len - 1] == ' ') {
            len--;
        }
        if (len > 0 && len < WORD) {
            memcpy(out[n], list, len);
            out[n][len] = '\0';
            n++;
        }
        if (!end) {
            break;
        }
        list = end + 1;
        while (*list == ' ') {
            list++;
        }
    }
    return n;
}

static int
find(const setting* s, const char* v) {
    for (int i = 0; i < s->n; i++) {
        if (!strcmp(s->choice[i], v)) {
            return i;
        }
    }
    return -1;
}

/* The values a row offers: what the adapter takes, without "custom" (a layout of the user's own: it cannot be set, only kept),
 * and the value it has now when that is not among them, so it can be shown. */
static void
fill(setting* s, int kind, const char* list, const char* now) {
    char tmp[CHOICES][WORD];
    int n = split(list, tmp);
    s->n = 0;
    for (int i = 0; i < n; i++) {
        if (kind == CTA_LAYOUT && !strcmp(tmp[i], "custom")) {
            continue;
        }
        memcpy(s->choice[s->n++], tmp[i], WORD);
    }
    snprintf(s->value, sizeof(s->value), "%s", now);
    if (s->n > 0 && now[0] && find(s, now) < 0 && s->n < CHOICES) {
        snprintf(s->choice[s->n++], WORD, "%s", now);
    }
}

/* One request per port: its state and all three settings with their values. A port the board does not have answers err. */
static void
read_port(int p) {
    char ops[256], ans[DCAPI_ANS_MAX + 1], v[WORD], list[96];
    char X = (char)('A' + p);
    memset(a.set[p], 0, sizeof(a.set[p]));
    a.state[p][0] = '\0';
    snprintf(ops, sizeof(ops), "get port.%c.state\nget port.%c.mode\nget port.%c.modes\nget port.%c.acc\nget port.%c.accs\n"
             "get port.%c.layout\nget port.%c.layouts\n%s", X, X, X, X, X, X, X, p == 0 ? "get sys.store\n" : "");
    if (request(ops, ans, sizeof(ans)) < 0) {
        return;
    }
    char key[32];
    if (p == 0) {
        a.commit = dcapi_answer(ans, "sys.store", v, sizeof(v)) == 1 && !strcmp(v, "commit");
    }
    snprintf(key, sizeof(key), "port.%c.state", X);
    if (dcapi_answer(ans, key, v, sizeof(v)) == 1) {
        snprintf(a.state[p], sizeof(a.state[p]), "%s", v);
    }
    for (int k = 0; k < CTA_KINDS; k++) {
        char now[WORD];
        snprintf(key, sizeof(key), "port.%c.%s", X, key_of[k]);
        if (dcapi_answer(ans, key, now, sizeof(now)) != 1) {
            continue; /* no such port, or not this setting */
        }
        snprintf(key, sizeof(key), "port.%c.%ss", X, key_of[k]);
        if (dcapi_answer(ans, key, list, sizeof(list)) != 1) {
            continue;
        }
        fill(&a.set[p][k], k, list, now);
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
#endif
    a.nrows = 0;
    if (!a.found) {
        return;
    }
    for (int p = 0; p < CTA_PORTS; p++) {
        read_port(p);
        for (int k = 0; k < CTA_KINDS; k++) {
            if (a.set[p][k].n > 1) { /* a setting with one value is nothing to choose */
                a.row_port[a.nrows] = (unsigned char)p;
                a.row_kind[a.nrows] = (unsigned char)k;
                a.nrows++;
            }
        }
    }
#ifndef CTA_HOST_TEST
    if (a.nrows == 0) {
        scanned = 0; /* it did not answer: look again next time */
        known_port = -1;
        a.found = 0;
    }
#endif
}

const char*
cta_name(void) {
    return a.found ? a.name : "";
}

int
cta_rows(void) {
    return a.nrows;
}

#define ROW_OK(r) ((r) >= 0 && (r) < a.nrows)
#define SET(r) (&a.set[a.row_port[r]][a.row_kind[r]])

const char*
cta_label(int row) {
    static char text[CTA_ROWS][24];
    static const char* const what[CTA_KINDS] = {"type", "accessory", "layout"};
    if (!ROW_OK(row)) {
        return "";
    }
    snprintf(text[row], sizeof(text[row]), "Port %c %s", 'A' + a.row_port[row], what[a.row_kind[row]]);
    return text[row];
}

const char*
cta_help(int row) {
    static char text[CTA_ROWS][48];
    static const char* const what[CTA_KINDS] = {"what the console sees", "memory card and vibration", "button layout"};
    if (!ROW_OK(row)) {
        return "";
    }
    const char* st = a.state[a.row_port[row]];
    const char* who = !strcmp(st, "bt") ? "Bluetooth pad" : !strcmp(st, "wired") ? "Wired pad" : !strcmp(st, "empty") ? "Nothing on it" : "";
    snprintf(text[row], sizeof(text[row]), "%s%s%s", who, who[0] ? ": " : "", what[a.row_kind[row]]);
    if (!who[0]) {
        text[row][0] = (char)(text[row][0] - 'a' + 'A');
    }
    return text[row];
}

int
cta_choice_count(int row) {
    return ROW_OK(row) ? SET(row)->n : 0;
}

const char*
cta_choice_name(int row, int choice) {
    static const char* const names[][2] = {
        {"normal", "Controller"}, {"dual", "Dual stick"}, {"keyboard", "Keyboard"}, {"mouse", "Mouse"}, {"racing", "Racing wheel"},
        {"maracas", "Maracas"}, {"none", "None"}, {"vmu", "Memory card"}, {"rumble", "Vibration"}, {"both", "Card + vibration"},
        {"standard", "Standard"}, {"all", "All buttons"}, {"custom", "Custom"}, {"twin", "Twin stick"}, {"fight", "Fight stick"},
    };
    static char other[WORD];
    if (!ROW_OK(row) || choice < 0 || choice >= SET(row)->n) {
        return "?";
    }
    const char* v = SET(row)->choice[choice];
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

int
cta_get(int row) {
    if (!ROW_OK(row)) {
        return 0;
    }
    int i = find(SET(row), SET(row)->value);
    return i < 0 ? 0 : i;
}

int
cta_set(int row, int choice) {
    char ops[96], ans[DCAPI_ANS_MAX + 1], key[32], got[48];
    if (!ROW_OK(row) || choice < 0 || choice >= SET(row)->n) {
        return -1;
    }
    setting* s = SET(row);
    const char* v = s->choice[choice];
    if (!strcmp(v, s->value) || (a.row_kind[row] == CTA_LAYOUT && !strcmp(v, "custom"))) {
        return 0; /* nothing to change (a custom layout is made on the adapter's own page) */
    }
    snprintf(key, sizeof(key), "port.%c.%s", 'A' + a.row_port[row], key_of[a.row_kind[row]]);
    /* the set and the commit in one request: a mode change restarts the adapter's wired side, which a second request could miss */
    snprintf(ops, sizeof(ops), "set %s %s\n%s", key, v, a.commit ? "call sys.commit\n" : "");
    if (request(ops, ans, sizeof(ans)) < 0 || dcapi_answer(ans, key, got, sizeof(got)) != 1) {
        return -1;
    }
    memmove(s->value, v, strlen(v) + 1); /* both WORD long */
    if (a.commit && dcapi_answer(ans, "sys.commit", got, sizeof(got)) == 1 && strstr(got, "busy")) {
#ifndef CTA_HOST_TEST
        thd_sleep(100);
#endif
        request("call sys.commit\n", ans, sizeof(ans)); /* once more; if it is still busy the change stays live until the next one */
    }
    return 0;
}
