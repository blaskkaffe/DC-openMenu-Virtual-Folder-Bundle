/* ctrl_editor: see ctrl_editor.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm2/dcapi_text.h"
#include "ctrl_editor.h"

static const char* const output_names[CED_OUTPUTS] = {
    "A", "B", "X", "Y", "START", "D", "Z", "C", "LT", "RT", "A1_UP", "A1_DOWN", "A1_LEFT", "A1_RIGHT", "A2_UP", "A2_DOWN", "A2_LEFT", "A2_RIGHT",
    "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT", "DPAD2_UP", "DPAD2_DOWN", "DPAD2_LEFT", "DPAD2_RIGHT",
};
#define INPUTS 31
static const char* const input_names[INPUTS] = {
    "none", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "back", "start", "guide", "share",
    "lstick.up", "lstick.down", "lstick.left", "lstick.right", "lstick.click", "rstick.up", "rstick.down", "rstick.left", "rstick.right",
    "rstick.click", "dpad.up", "dpad.down", "dpad.left", "dpad.right", "dpad2.up", "dpad2.down", "dpad2.left", "dpad2.right",
};
static const char* const default_modes = "normal,dual,keyboard,mouse";
static const char* const default_accs = "none,vmu,rumble,both";

static void
copy(char* dst, size_t cap, const char* src) {
    snprintf(dst, cap, "%s", src);
}

static void
say(ced* e, const char* t) {
    copy(e->msg, sizeof(e->msg), t);
}

static int
ask(ced* e, const char* ops, char* ans, size_t cap) {
    char req[DCAPI_REQ_MAX + 1];

    if (dcapi_build(req, sizeof(req), ops) < 0) {
        return -1;
    }
    {
        int r = e->send(e->user, req, ans, cap);
        e->reachable = r >= 0;
        return r;
    }
}

static const char*
nice_mode(const char* m) {
    if (!strcmp(m, "normal")) return "Controller";
    if (!strcmp(m, "dual")) return "Dual stick";
    if (!strcmp(m, "keyboard")) return "Keyboard";
    if (!strcmp(m, "mouse")) return "Mouse";
    if (!strcmp(m, "racing")) return "Racing";
    if (!strcmp(m, "maracas")) return "Maracas";
    return m;
}

static const char*
nice_acc(const char* a) {
    if (!strcmp(a, "none")) return "None";
    if (!strcmp(a, "vmu")) return "Memory card";
    if (!strcmp(a, "rumble")) return "Rumble";
    if (!strcmp(a, "both")) return "Both";
    return a;
}

static const char*
nice_layout(const char* l) {
    if (!strcmp(l, "standard")) return "Standard";
    if (!strcmp(l, "all")) return "All buttons";
    if (!strcmp(l, "custom")) return "Custom";
    return l;
}

void
ced_init(ced* e, ced_transport send, void* user, int port) {
    memset(e, 0, sizeof(*e));
    e->send = send;
    e->user = user;
    e->port = port < 0 ? 0 : (port > 3 ? 3 : port);
    copy(e->modes, sizeof(e->modes), default_modes);
    copy(e->accs, sizeof(e->accs), default_accs);
    copy(e->layouts, sizeof(e->layouts), "standard,all");
    copy(e->mode, sizeof(e->mode), "?");
    copy(e->acc, sizeof(e->acc), "?");
    copy(e->layout, sizeof(e->layout), "?");
    copy(e->unsaved, sizeof(e->unsaved), "no");
    e->page = CED_PAGE_PORT;
    uim_init(&e->motion, e->send, e->user);
}

int
ced_refresh(ced* e) {
    char ops[160], ans[DCAPI_ANS_MAX + 1], key[32], v[96];
    char p = (char)('A' + e->port);
    int r;

    snprintf(ops, sizeof(ops), "get port.%c.state\nget port.%c.mode\nget port.%c.acc\nget port.%c.layout\nget port.%c.name\nget sys.unsaved\n", p, p, p, p, p);
    r = ask(e, ops, ans, sizeof(ans));
    if (r < 0) {
        return r;
    }
    snprintf(key, sizeof(key), "port.%c.mode", p);
    if (dcapi_answer(ans, key, v, sizeof(v)) != 1) {
        return 0; /* the device has no such port */
    }
    copy(e->mode, sizeof(e->mode), v);
    snprintf(key, sizeof(key), "port.%c.state", p);
    if (dcapi_answer(ans, key, v, sizeof(v)) == 1) copy(e->state, sizeof(e->state), v);
    snprintf(key, sizeof(key), "port.%c.acc", p);
    if (dcapi_answer(ans, key, v, sizeof(v)) == 1) copy(e->acc, sizeof(e->acc), v);
    snprintf(key, sizeof(key), "port.%c.layout", p);
    if (dcapi_answer(ans, key, v, sizeof(v)) == 1) copy(e->layout, sizeof(e->layout), v);
    snprintf(key, sizeof(key), "port.%c.name", p);
    if (dcapi_answer(ans, key, v, sizeof(v)) == 1) copy(e->devname, sizeof(e->devname), v);
    if (dcapi_answer(ans, "sys.unsaved", v, sizeof(v)) == 1) copy(e->unsaved, sizeof(e->unsaved), v);
    /* the values the device accepts (a device that does not list them keeps the defaults) */
    snprintf(ops, sizeof(ops), "get port.%c.modes\nget port.%c.accs\nget port.%c.layouts\n", p, p, p);
    if (ask(e, ops, ans, sizeof(ans)) >= 0) {
        snprintf(key, sizeof(key), "port.%c.modes", p);
        if (dcapi_answer(ans, key, v, sizeof(v)) == 1) copy(e->modes, sizeof(e->modes), v);
        snprintf(key, sizeof(key), "port.%c.accs", p);
        if (dcapi_answer(ans, key, v, sizeof(v)) == 1) copy(e->accs, sizeof(e->accs), v);
        snprintf(key, sizeof(key), "port.%c.layouts", p);
        if (dcapi_answer(ans, key, v, sizeof(v)) == 1) copy(e->layouts, sizeof(e->layouts), v);
    }
    /* Joy-Con motion: only worth asking of a maracas port */
    if (!strcmp(e->mode, "maracas")) {
        e->motion_known = uim_refresh(&e->motion) == 1;
    } else {
        e->motion_known = 0;
    }
    return 1;
}

/* ---- the lists: "a,b,c" -> the item after / before `cur` */
static int
list_step(const char* list, const char* cur, int dir, char* out, size_t cap) {
    char items[12][24];
    int n = 0, at = -1;
    const char* s = list;

    while (*s && n < 12) {
        size_t l = strcspn(s, ",");
        if (l >= sizeof(items[0])) l = sizeof(items[0]) - 1;
        memcpy(items[n], s, l);
        items[n][l] = 0;
        if (!strcmp(items[n], cur)) at = n;
        n++;
        s += strcspn(s, ",");
        if (*s == ',') s++;
    }
    if (n == 0) {
        return 0;
    }
    {
        int to = at < 0 ? 0 : ((at + (dir > 0 ? 1 : -1) + n) % n);
        /* "custom" cannot be set: skip it */
        if (!strcmp(items[to], "custom")) to = (to + (dir > 0 ? 1 : -1) + n) % n;
        copy(out, cap, items[to]);
    }
    return 1;
}

/* ---- the Buttons page: what drives each output */
static void
load_map(ced* e) {
    char p = (char)('A' + e->port);

    e->map_loaded = 0;
    for (int i = 0; i < CED_OUTPUTS; i += 8) {
        char ops[DCAPI_REQ_MAX + 1], ans[DCAPI_ANS_MAX + 1];
        size_t n = 0;

        ops[0] = 0;
        for (int k = i; k < i + 8 && k < CED_OUTPUTS; k++) {
            n += (size_t)snprintf(ops + n, sizeof(ops) - n, "get port.%c.map.%s\n", p, output_names[k]);
        }
        if (ask(e, ops, ans, sizeof(ans)) < 0) {
            say(e, "No answer from the adapter");
            return;
        }
        for (int k = i; k < i + 8 && k < CED_OUTPUTS; k++) {
            char key[40], v[CED_VALUE_MAX];
            snprintf(key, sizeof(key), "port.%c.map.%s", p, output_names[k]);
            if (dcapi_answer(ans, key, v, sizeof(v)) == 1) {
                copy(e->map[k], sizeof(e->map[k]), v);
            } else {
                copy(e->map[k], sizeof(e->map[k]), "?");
            }
        }
    }
    e->map_loaded = 1;
}

const char*
ced_title(const ced* e) {
    switch (e->page) {
        case CED_PAGE_BUTTONS: return "Buttons";
        case CED_PAGE_PICK: return output_names[e->pick_out];
        case CED_PAGE_MOTION: return "Joy-Con motion";
        default: return "Controller";
    }
}

/* the rows of the port page */
enum { R_TYPE, R_ACC, R_LAYOUT, R_BUTTONS, R_MOTION, R_SAVE, R_UNDO };

static int
port_row_id(const ced* e, int row) {
    static const int plain[] = {R_TYPE, R_ACC, R_LAYOUT, R_BUTTONS, R_SAVE, R_UNDO};
    static const int with_motion[] = {R_TYPE, R_ACC, R_LAYOUT, R_BUTTONS, R_MOTION, R_SAVE, R_UNDO};

    if (e->motion_known) {
        return row >= 0 && row < 7 ? with_motion[row] : -1;
    }
    return row >= 0 && row < 6 ? plain[row] : -1;
}

int
ced_rows(const ced* e) {
    switch (e->page) {
        case CED_PAGE_BUTTONS: return CED_OUTPUTS;
        case CED_PAGE_PICK: return INPUTS;
        case CED_PAGE_MOTION: return uim_rows();
        default: return e->motion_known ? 7 : 6;
    }
}

void
ced_row(const ced* e, int row, char* label, size_t label_cap, char* value, size_t value_cap, const char** help) {
    *help = "";
    label[0] = value[0] = 0;
    if (row < 0 || row >= ced_rows(e)) {
        return;
    }
    switch (e->page) {
        case CED_PAGE_BUTTONS:
            copy(label, label_cap, output_names[row]);
            copy(value, value_cap, e->map[row][0] ? e->map[row] : "?");
            *help = "A: choose what drives this button";
            return;
        case CED_PAGE_PICK:
            copy(label, label_cap, input_names[row]);
            copy(value, value_cap, !strcmp(e->map[e->pick_out], input_names[row]) || (row == 0 && !strcmp(e->map[e->pick_out], "none")) ? "Now" : "");
            *help = "A: use this input";
            return;
        case CED_PAGE_MOTION:
            uim_row(&e->motion, row, label, label_cap, value, value_cap, help);
            return;
        default: break;
    }
    switch (port_row_id(e, row)) {
        case R_TYPE: copy(label, label_cap, "Type"); copy(value, value_cap, nice_mode(e->mode)); *help = "Left / right: what the console sees"; break;
        case R_ACC: copy(label, label_cap, "Accessory"); copy(value, value_cap, nice_acc(e->acc)); *help = "Left / right: memory card, rumble pack"; break;
        case R_LAYOUT: copy(label, label_cap, "Layout"); copy(value, value_cap, nice_layout(e->layout)); *help = "Left / right: a ready made button layout"; break;
        case R_BUTTONS: copy(label, label_cap, "Buttons"); copy(value, value_cap, "Edit"); *help = "A: what drives each button"; break;
        case R_MOTION: copy(label, label_cap, "Joy-Con motion"); { char l2[8]; const char* h2; uim_row(&e->motion, UIM_ROW_STATE, l2, sizeof(l2), value, value_cap, &h2); } *help = "A: maracas from the Joy-Cons"; break;
        case R_SAVE: copy(label, label_cap, "Save"); copy(value, value_cap, !strcmp(e->unsaved, "yes") ? "Unsaved changes" : "Saved"); *help = "A: keep the changes for the next start"; break;
        case R_UNDO: copy(label, label_cap, "Undo"); copy(value, value_cap, ""); *help = "A: go back to the saved settings"; break;
        default: break;
    }
}

int
ced_cursor(const ced* e) {
    return e->cursor[e->page];
}

int
ced_set_cursor(ced* e, int row) {
    if (row < 0 || row >= ced_rows(e)) {
        return 0;
    }
    e->cursor[e->page] = row;
    return 1;
}

int
ced_move(ced* e, int delta) {
    int n = ced_rows(e), c = e->cursor[e->page] + delta;

    if (c < 0 || c >= n) {
        return 0;
    }
    e->cursor[e->page] = c;
    return 1;
}

static void
set_key(ced* e, const char* key, const char* value) {
    char ops[96], ans[DCAPI_ANS_MAX + 1], reply[64];

    snprintf(ops, sizeof(ops), "set %s %s\n", key, value);
    if (ask(e, ops, ans, sizeof(ans)) < 0) {
        say(e, "No answer from the adapter");
        return;
    }
    if (dcapi_answer(ans, key, reply, sizeof(reply)) == 1) {
        say(e, "");
    } else {
        snprintf(e->msg, sizeof(e->msg), "Not accepted: %.40s", reply);
    }
}

int
ced_change(ced* e, int dir) {
    char key[32], v[24];
    char p = (char)('A' + e->port);

    if (e->page != CED_PAGE_PORT) {
        return 0;
    }
    switch (port_row_id(e, e->cursor[CED_PAGE_PORT])) {
        case R_TYPE:
            if (!list_step(e->modes, e->mode, dir, v, sizeof(v))) return 0;
            snprintf(key, sizeof(key), "port.%c.mode", p);
            set_key(e, key, v);
            break;
        case R_ACC:
            if (!list_step(e->accs, e->acc, dir, v, sizeof(v))) return 0;
            snprintf(key, sizeof(key), "port.%c.acc", p);
            set_key(e, key, v);
            break;
        case R_LAYOUT:
            if (!list_step(e->layouts, !strcmp(e->layout, "custom") ? "standard" : e->layout, dir, v, sizeof(v))) return 0;
            snprintf(key, sizeof(key), "port.%c.layout", p);
            set_key(e, key, v);
            e->map_loaded = 0;
            break;
        default:
            return 0;
    }
    ced_refresh(e);
    /* the rows may have changed (a maracas port has the motion row) */
    if (e->cursor[CED_PAGE_PORT] >= ced_rows(e)) {
        e->cursor[CED_PAGE_PORT] = ced_rows(e) - 1;
    }
    return 1;
}

int
ced_activate(ced* e) {
    char ops[96], ans[DCAPI_ANS_MAX + 1];

    switch (e->page) {
        case CED_PAGE_PORT:
            switch (port_row_id(e, e->cursor[CED_PAGE_PORT])) {
                case R_BUTTONS:
                    load_map(e);
                    e->page = CED_PAGE_BUTTONS;
                    return 1;
                case R_MOTION:
                    e->page = CED_PAGE_MOTION;
                    uim_refresh(&e->motion);
                    return 1;
                case R_SAVE:
                    ask(e, "call sys.commit\n", ans, sizeof(ans));
                    say(e, "Saved");
                    ced_refresh(e);
                    return 1;
                case R_UNDO:
                    ask(e, "call sys.revert\n", ans, sizeof(ans));
                    say(e, "Back to the saved settings");
                    e->map_loaded = 0;
                    ced_refresh(e);
                    return 1;
                default:
                    return 0;
            }
        case CED_PAGE_BUTTONS:
            e->pick_out = e->cursor[CED_PAGE_BUTTONS];
            e->page = CED_PAGE_PICK;
            return 1;
        case CED_PAGE_PICK: {
            char key[40];
            int row = e->cursor[CED_PAGE_PICK];

            snprintf(key, sizeof(key), "port.%c.map.%s", 'A' + e->port, output_names[e->pick_out]);
            set_key(e, key, input_names[row]);
            snprintf(ops, sizeof(ops), "get %s\nget sys.unsaved\n", key);
            if (ask(e, ops, ans, sizeof(ans)) >= 0) {
                char v[CED_VALUE_MAX];
                if (dcapi_answer(ans, key, v, sizeof(v)) == 1) copy(e->map[e->pick_out], sizeof(e->map[e->pick_out]), v);
                if (dcapi_answer(ans, "sys.unsaved", v, sizeof(v)) == 1) copy(e->unsaved, sizeof(e->unsaved), v);
            }
            e->page = CED_PAGE_BUTTONS;
            return 1;
        }
        case CED_PAGE_MOTION: {
            int r = uim_activate(&e->motion, e->cursor[CED_PAGE_MOTION]);
            copy(e->msg, sizeof(e->msg), e->motion.msg);
            return r;
        }
        default: return 0;
    }
}

int
ced_back(ced* e) {
    switch (e->page) {
        case CED_PAGE_PICK: e->page = CED_PAGE_BUTTONS; return 0;
        case CED_PAGE_BUTTONS:
        case CED_PAGE_MOTION: e->page = CED_PAGE_PORT; return 0;
        default: return 1;
    }
}

int
ced_wants_man(const ced* e) {
    return e->page == CED_PAGE_MOTION && e->motion_known;
}

int
ced_man(const ced* e, float* l, float* r) {
    *l = (float)e->left_deg;
    *r = (float)e->right_deg;
    return e->heights_ok;
}

int
ced_tick(ced* e, int ms) {
    int changed = 0;

    if (e->page == CED_PAGE_MOTION) {
        changed |= uim_tick(&e->motion, ms);
        copy(e->msg, sizeof(e->msg), e->motion.msg);
        e->since_heights_ms += ms;
        if (e->since_heights_ms >= 150) {
            char ans[DCAPI_ANS_MAX + 1], a[12], b[12];
            int ok;

            e->since_heights_ms = 0;
            ok = ask(e, "get motion.left.height\nget motion.right.height\n", ans, sizeof(ans)) >= 0;
            if (ok) {
                int l = dcapi_answer(ans, "motion.left.height", a, sizeof(a)), r = dcapi_answer(ans, "motion.right.height", b, sizeof(b));
                int nl = l == 1 ? atoi(a) : e->left_deg, nr = r == 1 ? atoi(b) : e->right_deg;

                e->heights_ok = l == 1 || r == 1;
                if (nl != e->left_deg || nr != e->right_deg) {
                    e->left_deg = nl;
                    e->right_deg = nr;
                    changed = 1;
                }
            }
        }
    }
    return changed;
}
