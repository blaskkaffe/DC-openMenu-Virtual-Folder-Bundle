/* netprefs: see netprefs.h. File tail: "NX", version 1, iface, net, isp (mode, 16 address bytes, 3 x 16 text), the game table (count, hash u32 LE + net),
 * the starred players (count, 16 bytes each), games (count, hash u32) and events (count, hash u32). */
#include <string.h>

#include "netprefs.h"

#define NP_VERSION 1

static uint8_t iface, net = NP_NET_DCNOW;
static np_isp_t isp_cfg;
static struct { uint32_t hash; uint8_t net; } game_net[NP_GAME_NETS];
static int game_net_n;
static char star_player[NP_STAR_PLAYERS][NP_PLAYER_NAME];
static int star_player_n;
static uint32_t star_game[NP_STAR_GAMES];
static int star_game_n;
static uint32_t star_event[NP_STAR_EVENTS];
static int star_event_n;
static void (*hook)(void);

static void
changed(void) {
    if (hook) {
        hook();
    }
}

void
np_reset(void) {
    iface = 0;
    net = NP_NET_DCNOW;
    memset(&isp_cfg, 0, sizeof(isp_cfg));
    game_net_n = star_player_n = star_game_n = star_event_n = 0;
}

void
np_set_changed_hook(void (*h)(void)) {
    hook = h;
}

static uint32_t
title_hash(const char* s) { /* FNV-1a */
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h;
}

/* ---- the file tail ---- */

static void
put32(uint8_t* p, uint32_t v) {
    for (int b = 0; b < 4; b++) {
        p[b] = (uint8_t)(v >> (8 * b));
    }
}

static uint32_t
get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int
np_pack(uint8_t* out, int room) {
    int n = 0;
    if (room < NP_MAX_BYTES) {
        return 0;
    }
    out[n++] = 'N';
    out[n++] = 'X';
    out[n++] = NP_VERSION;
    out[n++] = iface;
    out[n++] = net;
    out[n++] = isp_cfg.mode;
    memcpy(out + n, isp_cfg.ip, 4), n += 4;
    memcpy(out + n, isp_cfg.mask, 4), n += 4;
    memcpy(out + n, isp_cfg.gateway, 4), n += 4;
    memcpy(out + n, isp_cfg.dns, 4), n += 4;
    memcpy(out + n, isp_cfg.number, NP_DIAL_LEN), n += NP_DIAL_LEN;
    memcpy(out + n, isp_cfg.login, NP_DIAL_LEN), n += NP_DIAL_LEN;
    memcpy(out + n, isp_cfg.password, NP_DIAL_LEN), n += NP_DIAL_LEN;
    out[n++] = (uint8_t)game_net_n;
    for (int i = 0; i < game_net_n; i++) {
        put32(out + n, game_net[i].hash);
        out[n + 4] = game_net[i].net;
        n += 5;
    }
    out[n++] = (uint8_t)star_player_n;
    for (int i = 0; i < star_player_n; i++) {
        memcpy(out + n, star_player[i], NP_PLAYER_NAME), n += NP_PLAYER_NAME;
    }
    out[n++] = (uint8_t)star_game_n;
    for (int i = 0; i < star_game_n; i++) {
        put32(out + n, star_game[i]), n += 4;
    }
    out[n++] = (uint8_t)star_event_n;
    for (int i = 0; i < star_event_n; i++) {
        put32(out + n, star_event[i]), n += 4;
    }
    return n;
}

int
np_unpack(const uint8_t* in, int len) {
    int n = 0;
    np_reset();
    if (len < 6 + 16 + 3 * NP_DIAL_LEN || in[0] != 'N' || in[1] != 'X' || in[2] != NP_VERSION) {
        return -1;
    }
    n = 3;
    iface = in[n++] <= 4 ? in[n - 1] : 0;
    net = in[n] == NP_NET_DCNET ? NP_NET_DCNET : NP_NET_DCNOW;
    n++;
    isp_cfg.mode = in[n] <= NP_ISP_STATIC ? in[n] : 0;
    n++;
    memcpy(isp_cfg.ip, in + n, 4), n += 4;
    memcpy(isp_cfg.mask, in + n, 4), n += 4;
    memcpy(isp_cfg.gateway, in + n, 4), n += 4;
    memcpy(isp_cfg.dns, in + n, 4), n += 4;
    memcpy(isp_cfg.number, in + n, NP_DIAL_LEN), n += NP_DIAL_LEN;
    memcpy(isp_cfg.login, in + n, NP_DIAL_LEN), n += NP_DIAL_LEN;
    memcpy(isp_cfg.password, in + n, NP_DIAL_LEN), n += NP_DIAL_LEN;
    isp_cfg.number[NP_DIAL_LEN - 1] = isp_cfg.login[NP_DIAL_LEN - 1] = isp_cfg.password[NP_DIAL_LEN - 1] = '\0';
    int count;
#define NEED(bytes) do { if (n + (bytes) > len) { goto bad; } } while (0)
    NEED(1);
    count = in[n++];
    if (count > NP_GAME_NETS) goto bad;
    NEED(count * 5);
    for (int i = 0; i < count; i++, n += 5) {
        game_net[i].hash = get32(in + n);
        game_net[i].net = in[n + 4];
    }
    game_net_n = count;
    NEED(1);
    count = in[n++];
    if (count > NP_STAR_PLAYERS) goto bad;
    NEED(count * NP_PLAYER_NAME);
    for (int i = 0; i < count; i++, n += NP_PLAYER_NAME) {
        memcpy(star_player[i], in + n, NP_PLAYER_NAME);
        star_player[i][NP_PLAYER_NAME - 1] = '\0';
    }
    star_player_n = count;
    NEED(1);
    count = in[n++];
    if (count > NP_STAR_GAMES) goto bad;
    NEED(count * 4);
    for (int i = 0; i < count; i++, n += 4) {
        star_game[i] = get32(in + n);
    }
    star_game_n = count;
    NEED(1);
    count = in[n++];
    if (count > NP_STAR_EVENTS) goto bad;
    NEED(count * 4);
    for (int i = 0; i < count; i++, n += 4) {
        star_event[i] = get32(in + n);
    }
    star_event_n = count;
    return 0;
bad:
    np_reset();
    return -1;
#undef NEED
}

/* ---- settings ---- */

int
np_iface(void) {
    return iface;
}

void
np_set_iface(int setting) {
    if (setting >= 0 && setting <= 4 && setting != iface) {
        iface = (uint8_t)setting;
        changed();
    }
}

int
np_net(void) {
    return net;
}

void
np_set_net(int n) {
    n = n == NP_NET_DCNET ? NP_NET_DCNET : NP_NET_DCNOW;
    if (n != net) {
        net = (uint8_t)n;
        changed();
    }
}

int
np_game_net_saved(uint32_t hash) {
    for (int i = 0; i < game_net_n; i++) {
        if (game_net[i].hash == hash) {
            return game_net[i].net;
        }
    }
    return NP_NET_NONE;
}

int
np_game_net(uint32_t hash) {
    int saved = np_game_net_saved(hash);
    return saved != NP_NET_NONE ? saved : net;
}

int
np_set_game_net(uint32_t hash, int n) {
    for (int i = 0; i < game_net_n; i++) {
        if (game_net[i].hash == hash) {
            if (n == NP_NET_NONE) {
                game_net[i] = game_net[--game_net_n];
            } else {
                game_net[i].net = (uint8_t)n;
            }
            changed();
            return 0;
        }
    }
    if (n == NP_NET_NONE) {
        return 0;
    }
    if (game_net_n >= NP_GAME_NETS) {
        return -1;
    }
    game_net[game_net_n].hash = hash;
    game_net[game_net_n++].net = (uint8_t)n;
    changed();
    return 0;
}

const np_isp_t*
np_isp(void) {
    return &isp_cfg;
}

void
np_set_isp(const np_isp_t* isp) {
    if (memcmp(&isp_cfg, isp, sizeof(isp_cfg)) != 0) {
        isp_cfg = *isp;
        isp_cfg.number[NP_DIAL_LEN - 1] = isp_cfg.login[NP_DIAL_LEN - 1] = isp_cfg.password[NP_DIAL_LEN - 1] = '\0';
        changed();
    }
}

/* ---- stars ---- */

static int
player_at(const char* name) {
    for (int i = 0; i < star_player_n; i++) {
        if (strncmp(star_player[i], name, NP_PLAYER_NAME - 1) == 0) {
            return i;
        }
    }
    return -1;
}

int
np_player_starred(const char* name) {
    return name && player_at(name) >= 0;
}

int
np_player_toggle(const char* name) {
    int at = name ? player_at(name) : -1;
    if (!name || !name[0]) {
        return -1;
    }
    if (at >= 0) {
        memmove(star_player[at], star_player[at + 1], (size_t)(star_player_n - at - 1) * NP_PLAYER_NAME);
        star_player_n--;
        changed();
        return 0;
    }
    if (star_player_n >= NP_STAR_PLAYERS) {
        return -1;
    }
    memset(star_player[star_player_n], 0, NP_PLAYER_NAME);
    strncpy(star_player[star_player_n++], name, NP_PLAYER_NAME - 1);
    changed();
    return 1;
}

int
np_star_player_count(void) {
    return star_player_n;
}

const char*
np_star_player(int i) {
    return i >= 0 && i < star_player_n ? star_player[i] : "";
}

static int
hash_toggle(uint32_t* list, int* count, int max, uint32_t h) {
    for (int i = 0; i < *count; i++) {
        if (list[i] == h) {
            memmove(list + i, list + i + 1, (size_t)(*count - i - 1) * sizeof(uint32_t));
            (*count)--;
            changed();
            return 0;
        }
    }
    if (*count >= max) {
        return -1;
    }
    list[(*count)++] = h;
    changed();
    return 1;
}

static int
hash_in(const uint32_t* list, int count, uint32_t h) {
    for (int i = 0; i < count; i++) {
        if (list[i] == h) {
            return 1;
        }
    }
    return 0;
}

int
np_game_starred(uint32_t hash) {
    return hash_in(star_game, star_game_n, hash);
}

int
np_game_toggle(uint32_t hash) {
    return hash_toggle(star_game, &star_game_n, NP_STAR_GAMES, hash);
}

int
np_star_game_count(void) {
    return star_game_n;
}

uint32_t
np_star_game(int i) {
    return i >= 0 && i < star_game_n ? star_game[i] : 0;
}

int
np_event_starred(const char* title) {
    return title && hash_in(star_event, star_event_n, title_hash(title));
}

int
np_event_toggle(const char* title) {
    return title ? hash_toggle(star_event, &star_event_n, NP_STAR_EVENTS, title_hash(title)) : -1;
}
