/* ui_motion: see ui_motion.h. */
#include <stdio.h>
#include <string.h>

#include "vm2/dcapi_text.h"
#include "ui_motion.h"

static void
copy(char* dst, size_t cap, const char* src) {
    snprintf(dst, cap, "%s", src);
}

static void
say(uim* m, const char* text) {
    copy(m->msg, sizeof(m->msg), text);
}

/* one request of one operation line; the answer text into ans */
static int
ask(uim* m, const char* op, char* ans, size_t cap) {
    char ops[64], req[96];

    if (snprintf(ops, sizeof(ops), "%s\n", op) >= (int)sizeof(ops) || dcapi_build(req, sizeof(req), ops) < 0) {
        return -1;
    }
    return m->send(m->user, req, ans, cap);
}

void
uim_init(uim* m, uim_transport send, void* user) {
    memset(m, 0, sizeof(*m));
    m->send = send;
    m->user = user;
    copy(m->mode, sizeof(m->mode), "?");
    copy(m->state, sizeof(m->state), "?");
    copy(m->left, sizeof(m->left), "?");
    copy(m->right, sizeof(m->right), "?");
}

int
uim_refresh(uim* m) {
    char ans[DCAPI_ANS_MAX + 1], v[16];
    int r = ask(m, "get motion.*", ans, sizeof(ans));

    m->since_refresh_ms = 0;
    m->reachable = r >= 0;
    if (r < 0) {
        m->supported = 0;
        return r;
    }
    if (dcapi_answer(ans, "motion.mode", v, sizeof(v)) != 1) {
        m->supported = 0;
        return 0;
    }
    m->supported = 1;
    copy(m->mode, sizeof(m->mode), v);
    if (dcapi_answer(ans, "motion.state", v, sizeof(v)) == 1) {
        copy(m->state, sizeof(m->state), v);
    }
    if (dcapi_answer(ans, "motion.left", v, sizeof(v)) == 1) {
        copy(m->left, sizeof(m->left), v);
    }
    if (dcapi_answer(ans, "motion.right", v, sizeof(v)) == 1) {
        copy(m->right, sizeof(m->right), v);
    }
    return 1;
}

int
uim_supported(const uim* m) {
    return m->supported;
}

int
uim_rows(void) {
    return UIM_ROWS;
}

static const char*
state_text(const char* s) {
    if (!strcmp(s, "off")) return "Off";
    if (!strcmp(s, "on")) return "On";
    if (!strcmp(s, "waiting")) return "No Joy-Con";
    if (!strcmp(s, "uncalibrated")) return "Set forward";
    if (!strcmp(s, "ready")) return "Ready";
    if (!strcmp(s, "absent")) return "Absent";
    return "?";
}

void
uim_row(const uim* m, int row, char* label, size_t label_cap, char* value, size_t value_cap, const char** help) {
    static const char* const labels[UIM_ROWS] = {"Motion mode", "Status", "Left Joy-Con", "Right Joy-Con", "Set forward", "Up and down", "Sideways reach", "Forget"};
    static const char* const helps[UIM_ROWS] = {
        "A: on or off. Connect the Joy-Cons again", /* 0 */
        "Both Joy-Cons need to be set forward",     /* 1 */
        "The left Joy-Con is the left maraca",      /* 2 */
        "The right Joy-Con is the right maraca",    /* 3 */
        "A: point both at the TV, level, still", /* 4 */
        "A: point both straight up, hold still",    /* 5: the first time only */
        "A: turn out left and right, level, hold",  /* 6 */
        "A twice: forget what the adapter learned", /* 7 */
    };
    const char* v = "";

    if (row < 0 || row >= UIM_ROWS) {
        label[0] = value[0] = 0;
        *help = "";
        return;
    }
    copy(label, label_cap, labels[row]);
    *help = helps[row];
    switch (row) {
        case UIM_ROW_MODE: v = state_text(m->mode); break;
        case UIM_ROW_STATE: v = state_text(m->state); break;
        case UIM_ROW_LEFT: v = state_text(m->left); break;
        case UIM_ROW_RIGHT: v = state_text(m->right); break;
        case UIM_ROW_FORWARD:
        case UIM_ROW_UP:
        case UIM_ROW_OUT:
            if (m->pose_ms > 0 && m->pose == row) {
                char t[24];
                snprintf(t, sizeof(t), "Hold still %d", (m->pose_ms + 999) / 1000);
                copy(value, value_cap, t);
                return;
            }
            v = "A to start";
            break;
        case UIM_ROW_RESET: v = m->confirm_reset ? "A again" : "A twice"; break;
    }
    copy(value, value_cap, v);
}

static int
pose_name(int row, char* op, size_t cap) {
    switch (row) {
        case UIM_ROW_FORWARD: return snprintf(op, cap, "call motion.pose.forward %d", UIM_POSE_SECONDS) > 0;
        case UIM_ROW_UP: return snprintf(op, cap, "call motion.pose.up %d", UIM_POSE_SECONDS) > 0;
        case UIM_ROW_OUT: return snprintf(op, cap, "call motion.pose.out %d", UIM_POSE_SECONDS) > 0;
    }
    return 0;
}

int
uim_activate(uim* m, int row) {
    char op[48], ans[DCAPI_ANS_MAX + 1], v[48];
    int r;

    if (row != UIM_ROW_RESET) {
        m->confirm_reset = 0;
    }
    if (!m->supported) {
        say(m, "This adapter has no Joy-Con motion");
        return 0;
    }
    if (row == UIM_ROW_MODE) {
        int on = strcmp(m->mode, "on") != 0;

        snprintf(op, sizeof(op), "set motion.mode %s", on ? "on" : "off");
        r = ask(m, op, ans, sizeof(ans));
        if (r < 0 || dcapi_answer(ans, "motion.mode", v, sizeof(v)) != 1) {
            say(m, r < 0 ? "No answer from the adapter" : "The adapter did not accept it");
            return 1;
        }
        copy(m->mode, sizeof(m->mode), v);
        say(m, on ? "On: now connect the Joy-Cons again" : "Off");
        uim_refresh(m);
        return 1;
    }
    if (row == UIM_ROW_FORWARD || row == UIM_ROW_UP || row == UIM_ROW_OUT) {
        if (strcmp(m->mode, "on") != 0) {
            say(m, "Switch motion mode on first");
            return 0;
        }
        if (m->pose_ms > 0) {
            return 0;
        }
        pose_name(row, op, sizeof(op));
        r = ask(m, op, ans, sizeof(ans));
        {
            char key[32];
            snprintf(key, sizeof(key), "%s", op + 5); /* the action name up to the space */
            for (char* p = key; *p; p++) {
                if (*p == ' ') {
                    *p = 0;
                    break;
                }
            }
            if (r < 0 || dcapi_answer(ans, key, v, sizeof(v)) != 1) {
                say(m, r < 0 ? "No answer from the adapter" : "The adapter did not take it");
                return 1;
            }
        }
        m->pose = row;
        m->pose_ms = (UIM_POSE_SECONDS + 2) * 1000; /* the pose is taken at the end of the delay; the last seconds are for the result */
        say(m, "Hold the Joy-Cons still");
        return 1;
    }
    if (row == UIM_ROW_RESET) {
        if (!m->confirm_reset) {
            m->confirm_reset = 1;
            say(m, "Press A again to forget");
            return 0;
        }
        m->confirm_reset = 0;
        r = ask(m, "call motion.reset", ans, sizeof(ans));
        if (r < 0 || dcapi_answer(ans, "motion.reset", v, sizeof(v)) != 1) {
            say(m, "The adapter did not do it");
            return 1;
        }
        say(m, "Forgotten: set up and down again");
        uim_refresh(m);
        return 1;
    }
    return 0; /* the read only rows */
}

int
uim_tick(uim* m, int ms) {
    int changed = 0;

    if (m->pose_ms > 0) {
        int before = (m->pose_ms + 999) / 1000;

        m->pose_ms -= ms;
        if (m->pose_ms <= 0) {
            m->pose_ms = 0;
            uim_refresh(m);
            say(m, !strcmp(m->state, "ready") ? "Done: the Joy-Cons are ready" : "Look at the status: try again if it needs forward");
            changed = 1;
        }
        else if ((m->pose_ms + 999) / 1000 != before) {
            changed = 1;
        }
    }
    m->since_refresh_ms += ms;
    if (m->since_refresh_ms >= UIM_REFRESH_MS && m->supported) {
        char s[16], l[16], r[16];

        copy(s, sizeof(s), m->state);
        copy(l, sizeof(l), m->left);
        copy(r, sizeof(r), m->right);
        uim_refresh(m);
        if (strcmp(s, m->state) || strcmp(l, m->left) || strcmp(r, m->right)) {
            changed = 1;
        }
    }
    return changed;
}
