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
    e->api = 1;
    uim_init(&e->motion, e->send, e->user);
}

void
ced_init_plain(ced* e, const char* kind, int port) {
    memset(e, 0, sizeof(*e));
    e->port = port < 0 ? 0 : (port > 3 ? 3 : port);
    e->page = CED_PAGE_PORT;
    copy(e->devkind, sizeof(e->devkind), kind);
    e->reachable = 1;
}

void
ced_set_games(ced* e, const ced_games* g) {
    e->games = *g;
}

int
ced_refresh(ced* e) {
    char ops[160], ans[DCAPI_ANS_MAX + 1], key[32], v[96];
    char p = (char)('A' + e->port);
    int r;

    if (!e->api) {
        return 1;
    }
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
    /* memory card slots and the game id switch: only some devices have them (proposed keys, docs/dcapi.md) */
    snprintf(ops, sizeof(ops), "get port.%c.vmu.slot\nget port.%c.vmu.slots\nget game.mode\n", p, p);
    e->has_slot = e->has_game_mode = 0;
    if (ask(e, ops, ans, sizeof(ans)) >= 0) {
        snprintf(key, sizeof(key), "port.%c.vmu.slot", p);
        if (dcapi_answer(ans, key, v, sizeof(v)) == 1) {
            copy(e->slot, sizeof(e->slot), v);
            snprintf(key, sizeof(key), "port.%c.vmu.slots", p);
            e->has_slot = dcapi_answer(ans, key, v, sizeof(v)) == 1;
            if (e->has_slot) copy(e->slots, sizeof(e->slots), v);
        }
        if (dcapi_answer(ans, "game.mode", v, sizeof(v)) == 1) {
            copy(e->game_mode, sizeof(e->game_mode), v);
            e->has_game_mode = 1;
        }
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
        case CED_PAGE_GAME: return "Load game id";
        default: return e->api ? "Controller" : (e->devkind[0] ? e->devkind : "Controller");
    }
}

/* the rows of the port page */
enum { R_TYPE, R_VMU, R_SLOT, R_GAMEID, R_LOADID, R_RUMBLE, R_LAYOUT, R_BUTTONS, R_MOTION, R_SAVE, R_UNDO,
       /* the page without the DC API: hints, then Load game id */
       R_HINT_LAYOUT, R_HINT_SLOT, R_HINT_GAMEID, R_HINT_RUMBLE, R_HINT_MODE };

static int
port_row_list(const ced* e, int* ids) {
    int n = 0;

    if (!e->api) {
        ids[n++] = R_HINT_MODE;
        ids[n++] = R_HINT_LAYOUT;
        ids[n++] = R_HINT_SLOT;
        ids[n++] = R_HINT_GAMEID;
        ids[n++] = R_LOADID;
        ids[n++] = R_HINT_RUMBLE;
        return n;
    }
    ids[n++] = R_TYPE;
    ids[n++] = R_VMU;
    if (e->has_slot) ids[n++] = R_SLOT;
    if (e->has_game_mode) ids[n++] = R_GAMEID;
    ids[n++] = R_LOADID;
    ids[n++] = R_RUMBLE;
    ids[n++] = R_LAYOUT;
    ids[n++] = R_BUTTONS;
    if (e->motion_known) ids[n++] = R_MOTION;
    ids[n++] = R_SAVE;
    ids[n++] = R_UNDO;
    return n;
}

static int
port_row_id(const ced* e, int row) {
    int ids[16], n = port_row_list(e, ids);

    return row >= 0 && row < n ? ids[row] : -1;
}

/* the accessory value as two switches: memory card and rumble */
static int
acc_vmu(const char* a) {
    return !strcmp(a, "vmu") || !strcmp(a, "both");
}

static int
acc_rumble(const char* a) {
    return !strcmp(a, "rumble") || !strcmp(a, "both");
}

static const char*
acc_of(int vmu, int rumble) {
    return vmu ? (rumble ? "both" : "vmu") : (rumble ? "rumble" : "none");
}

/* `v` is one of the comma separated values of `list` */
static int
in_list(const char* list, const char* v) {
    size_t l = strlen(v);

    for (const char* s = list; *s;) {
        size_t n = strcspn(s, ",");
        if (n == l && !strncmp(s, v, l)) return 1;
        s += n;
        if (*s == ',') s++;
    }
    return 0;
}

static int
game_count(const ced* e) {
    return e->games.count ? e->games.count(e->games.user) : 0;
}

int
ced_rows(const ced* e) {
    int ids[16];

    switch (e->page) {
        case CED_PAGE_BUTTONS: return CED_OUTPUTS;
        case CED_PAGE_PICK: return INPUTS;
        case CED_PAGE_MOTION: return uim_rows();
        case CED_PAGE_GAME: return game_count(e);
        default: return port_row_list(e, ids);
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
        case CED_PAGE_GAME: {
            const char* n = e->games.name ? e->games.name(e->games.user, row) : "";
            const char* id = e->games.id ? e->games.id(e->games.user, row) : "";
            copy(label, label_cap, n ? n : "");
            copy(value, value_cap, id ? id : "");
            *help = "A: send this game's id to the adapter";
            return;
        }
        default: break;
    }
    switch (port_row_id(e, row)) {
        case R_TYPE: copy(label, label_cap, "Type"); copy(value, value_cap, nice_mode(e->mode)); *help = "Left / right: what the console sees"; break;
        case R_VMU:
            copy(label, label_cap, "Memory card");
            copy(value, value_cap, acc_vmu(e->acc) ? "On" : "Off");
            *help = "Left / right: the adapter's memory card";
            break;
        case R_SLOT: copy(label, label_cap, "Memory card slot"); copy(value, value_cap, e->slot); *help = "Left / right: which card the adapter shows"; break;
        case R_GAMEID:
            copy(label, label_cap, "Game id");
            copy(value, value_cap, !strcmp(e->game_mode, "on") ? "On" : "Off");
            *help = "Left / right: follow the game started";
            break;
        case R_LOADID: copy(label, label_cap, "Load game id"); copy(value, value_cap, "Choose"); *help = "A: pick a game, its id goes to the adapter"; break;
        case R_RUMBLE:
            copy(label, label_cap, "Rumble");
            copy(value, value_cap, acc_rumble(e->acc) ? "On" : "Off");
            *help = "Left / right: the rumble pack";
            break;
        case R_LAYOUT: copy(label, label_cap, "Layout"); copy(value, value_cap, nice_layout(e->layout)); *help = "Left / right: a ready made button layout"; break;
        case R_BUTTONS: copy(label, label_cap, "Buttons"); copy(value, value_cap, "Edit"); *help = "A: what drives each button"; break;
        case R_MOTION: copy(label, label_cap, "Joy-Con motion"); { char l2[8]; const char* h2; uim_row(&e->motion, UIM_ROW_STATE, l2, sizeof(l2), value, value_cap, &h2); } *help = "A: maracas from the Joy-Cons"; break;
        case R_SAVE: copy(label, label_cap, "Save"); copy(value, value_cap, !strcmp(e->unsaved, "yes") ? "Unsaved changes" : "Saved"); *help = "A: keep the changes for the next start"; break;
        case R_UNDO: copy(label, label_cap, "Undo"); copy(value, value_cap, ""); *help = "A: go back to the saved settings"; break;
        /* Pico2Maple: its own controls (its README), the console cannot change them yet */
        case R_HINT_MODE: copy(label, label_cap, "Settings"); copy(value, value_cap, "On the controller"); *help = "Hold Select and press a button"; break;
        case R_HINT_LAYOUT: copy(label, label_cap, "Layout"); copy(value, value_cap, "Select + A B X Y"); *help = "Standard, all, twin stick, fight stick"; break;
        case R_HINT_SLOT: copy(label, label_cap, "Memory card slot"); copy(value, value_cap, "Select + LB / RB"); *help = "Cycles the 10 memory cards"; break;
        case R_HINT_GAMEID: copy(label, label_cap, "Game id"); copy(value, value_cap, "Past slot 10 / 1"); *help = "The display says GID: one card per game"; break;
        case R_HINT_RUMBLE: copy(label, label_cap, "Rumble"); copy(value, value_cap, "Select + D-pad down"); *help = "Racing wheel: Select + D-pad left"; break;
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

    if (e->page != CED_PAGE_PORT || !e->api) {
        return 0;
    }
    switch (port_row_id(e, e->cursor[CED_PAGE_PORT])) {
        case R_TYPE:
            if (!list_step(e->modes, e->mode, dir, v, sizeof(v))) return 0;
            snprintf(key, sizeof(key), "port.%c.mode", p);
            set_key(e, key, v);
            break;
        case R_VMU:
        case R_RUMBLE: {
            int row = port_row_id(e, e->cursor[CED_PAGE_PORT]);
            int vmu = acc_vmu(e->acc), rumble = acc_rumble(e->acc);
            const char* want;

            if (row == R_VMU) vmu = !vmu; else rumble = !rumble; /* a switch: left and right both flip it */
            want = acc_of(vmu, rumble);
            if (!in_list(e->accs, want)) {
                say(e, "Not on this device");
                return 1;
            }
            snprintf(key, sizeof(key), "port.%c.acc", p);
            set_key(e, key, want);
            break;
        }
        case R_SLOT:
            if (!list_step(e->slots, e->slot, dir, v, sizeof(v))) return 0;
            snprintf(key, sizeof(key), "port.%c.vmu.slot", p);
            set_key(e, key, v);
            break;
        case R_GAMEID:
            set_key(e, "game.mode", !strcmp(e->game_mode, "on") ? "off" : "on");
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
                case R_LOADID:
                    if (!game_count(e)) {
                        say(e, "No games with an id on the card");
                        return 1;
                    }
                    if (e->api && !acc_vmu(e->acc)) {
                        say(e, "Turn the memory card on first"); /* the id goes to the memory card unit */
                        return 1;
                    }
                    e->page = CED_PAGE_GAME;
                    return 1;
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
        case CED_PAGE_GAME: {
            int row = e->cursor[CED_PAGE_GAME];
            const char* id = e->games.id ? e->games.id(e->games.user, row) : NULL;
            const char* n = e->games.name ? e->games.name(e->games.user, row) : "";

            if (!id || !id[0] || !e->games.send) {
                return 0;
            }
            if (e->games.send(e->games.user, id, n) == 0) {
                snprintf(e->msg, sizeof(e->msg), "Sent %.16s", id);
            } else {
                say(e, "The adapter did not take it");
            }
            e->page = CED_PAGE_PORT;
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
        case CED_PAGE_GAME:
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
