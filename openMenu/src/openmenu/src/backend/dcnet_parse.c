/*
 * File: dcnet_parse.c
 * Project: openmenu
 * The DCNet status endpoints: see dcnet_parse.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/dcnet_parse.h"

static const char*
ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

/* Reads a string at p (on the opening quote) into out (cut to cap-1, \" \\ \/ and \uXXXX as '?' for anything above 0x7F). Returns the position
 * after the closing quote, NULL when not a string. */
static const char*
str_read(const char* p, char* out, size_t cap) {
    size_t n = 0;

    if (*p != '"') {
        return NULL;
    }
    for (p++; *p && *p != '"'; p++) {
        char c = *p;
        if (c == '\\') {
            p++;
            if (*p == 'u') {
                unsigned v = 0;
                for (int i = 1; i <= 4 && p[i]; i++) {
                    char h = p[i];
                    v = v * 16 + (unsigned)(h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0');
                }
                c = v < 0x80 && v >= 0x20 ? (char)v : '?';
                p += 4;
            } else if (*p == 'n' || *p == 't' || *p == 'r') {
                c = ' ';
            } else if (*p == '\0') {
                return NULL;
            } else {
                c = *p;
            }
        }
        if (out && n + 1 < cap) {
            out[n++] = c;
        }
    }
    if (*p != '"') {
        return NULL;
    }
    if (out && cap) {
        out[n] = '\0';
    }
    return p + 1;
}

/* Skips one value (any type). Returns the position after it, NULL when malformed. */
static const char*
skip_value(const char* p) {
    p = ws(p);
    if (*p == '"') {
        return str_read(p, NULL, 0);
    }
    if (*p == '{' || *p == '[') {
        char open = *p, close = open == '{' ? '}' : ']';
        p = ws(p + 1);
        while (*p != close) {
            if (open == '{') {
                p = str_read(p, NULL, 0);
                if (p == NULL || *(p = ws(p)) != ':') {
                    return NULL;
                }
                p++;
            }
            p = skip_value(p);
            if (p == NULL) {
                return NULL;
            }
            p = ws(p);
            if (*p == ',') {
                p = ws(p + 1);
            } else if (*p != close) {
                return NULL;
            }
        }
        return p + 1;
    }
    if (*p == '\0' || *p == ',' || *p == '}' || *p == ']') {
        return NULL;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') {
        p++;
    }
    return p;
}

/* a flat view of one object: the values we care about, whatever they are called */
typedef struct rec {
    char name[DCNOW_NAME_LEN];
    char country[3];
    char game[DCNOW_TITLE_LEN];
    char code[16];
    char id[16];
    int online; /* 1 unless "online": false */
    int players, games;
} rec_t;

static void
rec_key(rec_t* r, const char* key, const char* p, int depth, const char** end);

/* Reads the object at p into r (nested objects are read too: geoloc.country). Returns the position after it, NULL when malformed. */
static const char*
rec_read(const char* p, rec_t* r, int depth) {
    char key[24];

    p = ws(p);
    if (*p != '{') {
        return NULL;
    }
    p = ws(p + 1);
    while (*p != '}') {
        const char* e = NULL;
        p = str_read(p, key, sizeof(key));
        if (p == NULL || *(p = ws(p)) != ':') {
            return NULL;
        }
        p = ws(p + 1);
        rec_key(r, key, p, depth, &e);
        if (e == NULL) {
            return NULL;
        }
        p = ws(e);
        if (*p == ',') {
            p = ws(p + 1);
        } else if (*p != '}') {
            return NULL;
        }
    }
    return p + 1;
}

static void
rec_key(rec_t* r, const char* key, const char* p, int depth, const char** end) {
    char* dst = NULL;
    size_t cap = 0;

    if (!strcmp(key, "userName") || !strcmp(key, "username") || !strcmp(key, "player")) {
        dst = r->name, cap = sizeof(r->name);
    } else if (!strcmp(key, "name") || !strcmp(key, "game") || !strcmp(key, "gameName") || !strcmp(key, "title")) {
        dst = r->game, cap = sizeof(r->game);
    } else if (!strcmp(key, "gameId")) {
        dst = r->id, cap = sizeof(r->id);
    } else if (!strcmp(key, "country")) {
        dst = r->country, cap = sizeof(r->country);
    }
    if (dst && *p == '"') {
        *end = str_read(p, dst, cap);
    } else if (!strcmp(key, "geoloc") && *p == '{' && depth == 0) {
        *end = rec_read(p, r, 1);
    } else if (!strcmp(key, "online")) {
        r->online = strncmp(p, "false", 5) != 0;
        *end = skip_value(p);
    } else if (!strcmp(key, "playerCount") || !strcmp(key, "gameCount")) {
        int v = (int)strtol(p, NULL, 10);
        *(key[0] == 'p' ? &r->players : &r->games) = v;
        *end = skip_value(p);
    } else {
        *end = skip_value(p);
    }
}

/* Walks the array at json: calls cb for every object. Returns 0, -1 when not an array / malformed before the first entry; a cut array keeps
 * what was read. */
static int
each(const char* json, void (*cb)(const rec_t*, void*), void* arg) {
    const char* p = ws(json);
    int first = 1;

    if (*p != '[') {
        return -1;
    }
    p = ws(p + 1);
    while (*p && *p != ']') {
        rec_t r;
        memset(&r, 0, sizeof(r));
        r.online = 1;
        p = rec_read(p, &r, 0);
        if (p == NULL) {
            return first ? -1 : 0;
        }
        first = 0;
        cb(&r, arg);
        p = ws(p);
        if (*p == ',') {
            p = ws(p + 1);
        } else if (*p != ']') {
            return 0;
        }
    }
    return 0;
}

typedef struct users_arg {
    dcnow_player_t* out;
    int* n;
    int max;
    int* total;
} users_arg;

static void
users_cb(const rec_t* r, void* a) {
    users_arg* u = a;
    dcnow_player_t* e;

    if (!r->name[0] || !r->online) {
        return;
    }
    (*u->total)++;
    if (*u->n >= u->max) {
        return;
    }
    e = &u->out[(*u->n)++];
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", r->name);
    snprintf(e->country, sizeof(e->country), "%s", r->country);
    snprintf(e->network, sizeof(e->network), "DCNET");
}

int
dcnet_parse_users(const char* json, dcnow_player_t* out, int* n, int max, int* total) {
    users_arg u = {out, n, max, total};

    return each(json, users_cb, &u);
}

static void
players_cb(const rec_t* r, void* a) {
    users_arg* u = a;

    if (!r->name[0]) {
        return;
    }
    for (int i = 0; i < *u->n; i++) {
        if (!strcmp(u->out[i].name, r->name) && !strcmp(u->out[i].network, "DCNET")) {
            snprintf(u->out[i].title, sizeof(u->out[i].title), "%s", r->game);
            snprintf(u->out[i].code, sizeof(u->out[i].code), "%s", r->id);
            return;
        }
    }
    users_cb(r, a);
    if (*u->n > 0 && !strcmp(u->out[*u->n - 1].name, r->name)) {
        snprintf(u->out[*u->n - 1].title, sizeof(u->out[*u->n - 1].title), "%s", r->game);
        snprintf(u->out[*u->n - 1].code, sizeof(u->out[*u->n - 1].code), "%s", r->id);
    }
}

int
dcnet_apply_players(const char* json, dcnow_player_t* out, int n, int max, int* total) {
    users_arg u = {out, &n, max, total};

    if (each(json, players_cb, &u) < 0) {
        return -1;
    }
    return n;
}

typedef struct games_arg {
    dcnet_game_t* out;
    int n, max;
} games_arg;

static void
games_cb(const rec_t* r, void* a) {
    games_arg* g = a;

    if (!r->online || !r->id[0] || g->n >= g->max) {
        return;
    }
    snprintf(g->out[g->n].id, sizeof(g->out[g->n].id), "%s", r->id);
    snprintf(g->out[g->n].name, sizeof(g->out[g->n].name), "%s", r->game[0] ? r->game : r->id);
    g->out[g->n].players = r->players;
    g->out[g->n].games = r->games;
    g->n++;
}

int
dcnet_parse_games(const char* json, dcnet_game_t* out, int max) {
    games_arg g = {out, 0, max};

    if (each(json, games_cb, &g) < 0) {
        return -1;
    }
    return g.n;
}
