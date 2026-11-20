/*
 * File: dc99_chat_parse.c
 * Project: openmenu
 * See dc99_chat_parse.h. The scanning follows the DC99 maintainer's reference clients.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/dc99_chat_parse.h"

static void
copy(char* dst, size_t cap, const char* src) {
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

/* ---- text ---- */

void
dc99_chat_clean(char* s) {
    char* w = s;
    for (const char* r = s; *r; r++) {
        unsigned char ch = (unsigned char)*r;
        if (ch == 0x03) { /* colour: ^C[fg[,bg]] */
            int d = 0;
            while (r[1] >= '0' && r[1] <= '9' && d < 2) {
                r++;
                d++;
            }
            if (r[1] == ',' && r[2] >= '0' && r[2] <= '9') {
                r += 2;
                if (r[1] >= '0' && r[1] <= '9') {
                    r++;
                }
            }
        } else if (ch == 0x02 || ch == 0x0f || ch == 0x16 || ch == 0x1d || ch == 0x1e || ch == 0x1f) {
            /* bold, reset, reverse, italic, strike, underline: dropped */
        } else if (ch >= 0x80) {
            *w++ = '?';
            while (((unsigned char)r[1] & 0xC0) == 0x80) { /* the rest of the sequence */
                r++;
            }
        } else if (ch < 0x20 || ch == 0x7f) {
            *w++ = ' ';
        } else {
            *w++ = (char)ch;
        }
    }
    *w = '\0';
}

int
dc99_chat_wrap(const char* prefix, const char* text, int width, char* lines, int max, int cap) {
    char all[400];
    int n = 0;
    size_t len;
    size_t at = 0;

    snprintf(all, sizeof(all), "%s%s", prefix, text);
    len = strlen(all);
    if (width >= cap) {
        width = cap - 1;
    }
    while (at < len && n < max) {
        size_t take = len - at;
        int lead = n > 0 ? 2 : 0;
        char* out = lines + n * cap;

        if ((int)take > width - lead) {
            size_t cut = (size_t)(width - lead);
            size_t sp = cut;
            while (sp > 0 && all[at + sp] != ' ') { /* the last blank that fits */
                sp--;
            }
            take = sp > 0 ? sp : cut;
        }
        memset(out, ' ', (size_t)lead);
        memcpy(out + lead, all + at, take);
        out[lead + (int)take] = '\0';
        at += take;
        while (all[at] == ' ') {
            at++;
        }
        n++;
    }
    return n;
}

/* ---- KosNet feed ---- */

static int
hex4(const char* p, unsigned* out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

/* The value of "key" inside obj[0..objlen): strings unescaped to ASCII ('?' for what is not), other tokens as text, null as "". 1 when found. */
static int
field(const char* obj, size_t objlen, const char* key, char* out, size_t cap) {
    char pat[40];
    const char* end = obj + objlen;
    const char* p = NULL;
    char* w = out;
    char* wend = out + cap;
    size_t pl;

    snprintf(pat, sizeof(pat), "\"%s\":", key);
    pl = strlen(pat);
    out[0] = '\0';
    for (const char* s = obj; s + pl <= end; s++) {
        if (!strncmp(s, pat, pl)) {
            p = s + pl;
            break;
        }
    }
    if (!p) {
        return 0;
    }
    while (p < end && *p == ' ') {
        p++;
    }
    if (p < end && *p == '"') {
        p++;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                p++;
                switch (*p) {
                    case 'n': case 'r': case 't': if (w + 1 < wend) *w++ = ' '; p++; break;
                    case 'b': case 'f': p++; break;
                    case 'u': {
                        unsigned cp;
                        if (p + 4 < end && hex4(p + 1, &cp)) {
                            p += 5;
                            if (cp >= 0xD800 && cp < 0xDC00 && p + 5 < end && p[0] == '\\' && p[1] == 'u') {
                                unsigned lo;
                                if (hex4(p + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) {
                                    p += 6; /* a surrogate pair: one character */
                                }
                            }
                            if (w + 1 < wend) *w++ = cp >= 0x20 && cp < 0x7f ? (char)cp : '?';
                        } else {
                            p++;
                        }
                        break;
                    }
                    default: if (w + 1 < wend) *w++ = *p; p++; break; /* \" \\ \/ */
                }
            } else {
                if (w + 1 < wend) *w++ = *p;
                p++;
            }
        }
    } else {
        while (p < end && *p != ',' && *p != '}' && *p != ' ') {
            if (w + 1 < wend) *w++ = *p;
            p++;
        }
        if (!strcmp(out, "null")) {
            w = out;
        }
    }
    *w = '\0';
    dc99_chat_clean(out);
    return 1;
}

int
dc99_kn_parse(const char* json, dc99_kn_msg_t* out, int max, char* cursor, size_t cursor_cap) {
    const char* p;
    int count = 0;
    char tmp[32];

    if (cursor_cap > 0) {
        cursor[0] = '\0';
    }
    if (strstr(json, "\"ok\":false") != NULL) {
        return -2;
    }
    if (cursor_cap > 0 && field(json, strlen(json), "next_cursor", tmp, sizeof(tmp))) {
        copy(cursor, cursor_cap, tmp);
    }
    p = strstr(json, "\"messages\":[");
    if (p == NULL) {
        return -1;
    }
    p += 12;
    while (*p && count < max) {
        const char* start;
        size_t len;
        int in_str = 0;
        dc99_kn_msg_t* m = &out[count];

        while (*p && *p != '{' && *p != ']') {
            p++;
        }
        if (*p != '{') {
            break;
        }
        start = p;
        for (; *p; p++) { /* a "}" inside a message is part of the text */
            if (in_str) {
                if (*p == '\\' && p[1]) p++;
                else if (*p == '"') in_str = 0;
            } else if (*p == '"') {
                in_str = 1;
            } else if (*p == '}') {
                break;
            }
        }
        if (*p != '}') {
            break; /* cut off in the middle of one: keep what is there */
        }
        len = (size_t)(p - start) + 1;
        p++;
        memset(m, 0, sizeof(*m));
        field(start, len, "id", m->id, sizeof(m->id));
        field(start, len, "kind", tmp, sizeof(tmp));
        m->join = strcmp(tmp, "join") == 0;
        field(start, len, "display_name", m->name, sizeof(m->name));
        field(start, len, "game_name", m->game, sizeof(m->game));
        field(start, len, "body", m->body, sizeof(m->body));
        count++;
    }
    return count;
}

void
dc99_kn_path(const char* cursor, char* out, size_t cap) {
    int digits = cursor != NULL && *cursor != '\0' && strlen(cursor) < 20;
    for (const char* c = cursor; digits && *c; c++) {
        digits = *c >= '0' && *c <= '9';
    }
    if (digits) {
        snprintf(out, cap, "/online/kosnet_chat_status.php?cursor=%s", cursor);
    } else {
        snprintf(out, cap, "/online/kosnet_chat_status.php");
    }
}

/* ---- IRC ---- */

int
dc99_irc_say_line(const char* text, char* out, size_t cap) {
    char clean[400];
    size_t j = 0;

    for (const char* p = text; *p && j < sizeof(clean) - 1; p++) {
        clean[j++] = *p == '\r' || *p == '\n' ? ' ' : *p;
    }
    clean[j] = '\0';
    while (j > 0 && clean[j - 1] == ' ') {
        clean[--j] = '\0';
    }
    if (j == 0) {
        return -1;
    }
    snprintf(out, cap, "PRIVMSG #dc99 :%s", clean);
    return 0;
}

int
dc99_irc_parse(char* line, dc99_irc_ev_t* ev) {
    char* prefix = NULL;
    char* cmd;
    char* params[6] = {0};
    int np = 0;
    char* p = line;
    char* sp;

    memset(ev, 0, sizeof(*ev));
    if (*p == ':') {
        prefix = ++p;
        sp = strchr(p, ' ');
        if (!sp) {
            return 0;
        }
        *sp = '\0';
        p = sp + 1;
    }
    while (*p == ' ') {
        p++;
    }
    cmd = p;
    sp = strchr(p, ' ');
    if (sp) {
        *sp = '\0';
        p = sp + 1;
    } else {
        p = cmd + strlen(cmd);
    }
    while (*p && np < 6) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (*p == ':') {
            params[np++] = p + 1; /* the rest of the line */
            break;
        }
        params[np++] = p;
        sp = strchr(p, ' ');
        if (!sp) {
            break;
        }
        *sp = '\0';
        p = sp + 1;
    }
    if (prefix) {
        char* bang = strchr(prefix, '!');
        size_t n = bang ? (size_t)(bang - prefix) : strlen(prefix);
        if (n >= sizeof(ev->nick)) {
            n = sizeof(ev->nick) - 1;
        }
        memcpy(ev->nick, prefix, n);
        ev->nick[n] = '\0';
        dc99_chat_clean(ev->nick);
    }

    if (!strcmp(cmd, "PING")) {
        snprintf(ev->reply, sizeof(ev->reply), "PONG :%.60s", np ? params[0] : "");
        return 1;
    }
    if (!strcmp(cmd, "001")) {
        ev->type = DC99_IRCE_READY;
        copy(ev->reply, sizeof(ev->reply), "JOIN #dc99");
        return 1;
    }
    if (!strcmp(cmd, "433") || !strcmp(cmd, "464") || !strcmp(cmd, "432") || !strcmp(cmd, "465")) {
        ev->type = DC99_IRCE_ERROR;
        copy(ev->text, sizeof(ev->text),
             !strcmp(cmd, "433") ? "That nickname is already in use." : !strcmp(cmd, "464") ? "Wrong password for that nickname."
             : !strcmp(cmd, "432") ? "That nickname is not allowed." : "You cannot connect to this server.");
        return 1;
    }
    if (!strcmp(cmd, "ERROR")) {
        ev->type = DC99_IRCE_ERROR;
        copy(ev->text, sizeof(ev->text), np ? params[0] : "The server closed the connection.");
        dc99_chat_clean(ev->text);
        return 1;
    }
    if (!strcmp(cmd, "PRIVMSG") && np >= 2) {
        char* text = params[1];
        if (params[0][0] != '#') {
            return 0; /* a private message is not for the channel view */
        }
        if (!strcmp(ev->nick, "dc99status")) {
            return 0; /* the counting bot */
        }
        if (text[0] == 0x01) {
            if (strncmp(text + 1, "ACTION ", 7) != 0) {
                return 0; /* VERSION, PING... */
            }
            text += 8;
            char* end = strchr(text, 0x01);
            if (end) {
                *end = '\0';
            }
            ev->type = DC99_IRCE_ACTION;
        } else {
            ev->type = DC99_IRCE_MSG;
        }
        copy(ev->text, sizeof(ev->text), text);
        dc99_chat_clean(ev->text);
        return 1;
    }
    if (!strcmp(cmd, "NOTICE") && np >= 2) {
        if (params[1][0] == 0x01) {
            return 0; /* the website's colour hints */
        }
        ev->type = DC99_IRCE_NOTICE;
        copy(ev->text, sizeof(ev->text), params[1]);
        dc99_chat_clean(ev->text);
        return 1;
    }
    if ((!strcmp(cmd, "JOIN") || !strcmp(cmd, "PART")) && np >= 1) {
        if (!strcmp(ev->nick, "dc99status")) {
            return 0;
        }
        ev->type = cmd[0] == 'J' ? DC99_IRCE_JOIN : DC99_IRCE_PART;
        return 1;
    }
    if (!strcmp(cmd, "QUIT")) {
        if (!strcmp(ev->nick, "dc99status")) {
            return 0;
        }
        ev->type = DC99_IRCE_QUIT;
        copy(ev->text, sizeof(ev->text), np ? params[0] : "");
        dc99_chat_clean(ev->text);
        return 1;
    }
    return 0;
}
