/* ui_internet: see ui_internet.h. The connection, the player list and the online games are the main branch's engine
 * (backend/dcnow_*, dcnet_parse, online_games); the events are backend/events_fetch, the news backend/news_fetch, the way to the network
 * backend/netsel + dcnow_net, the saved settings and stars netprefs.
 * The Online page also holds the two DC99 chats (backend/dc99_chat). Four pages on the CD player's buttons: Online (who is online / the games being played), Play (the card's online games and the
 * browsers), Events (going on and coming up / the news), Settings (connection, ISP settings, network test). The connection status is
 * drawn above the panel. A acts, Y stars, START switches the list of a page, X connects or hangs up. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arch/timer.h>

#include <backend/gd_item.h>
#include <backend/db_list.h>
#include <backend/gd_list.h>
#include <bios_audio.h>
#include <bios_list.h>
#include <openmenu_settings.h>

#include "backend/dc99_chat.h"
#include "backend/dcnow_fetch.h"
#include "backend/dcnow_net.h"
#include "backend/events_fetch.h"
#include "backend/kosnet_post.h"
#include "backend/osk.h"
#include "backend/picolink.h"
#include "backend/uicons.h"
#include "backend/net_test.h"
#include "backend/netsel.h"
#include "backend/news_fetch.h"
#include "backend/online_games.h"
#include "clock.h"
#include "event_phase.h"
#include "gfx.h"
#include "input.h"
#include "launch.h"
#include "netprefs.h"
#include "online.h"
#include "sound.h"
#include "ui_internet.h"
#include "ui_settings.h"

/* The bottom row, as the game list has it: the CD player's BACK marker and buttons (scripts 6, 0x17..0x1A) */
#define PRIO 0x1000
#define ID_BACK 0x1110
#define ID_SAY 0x1120 /* the oval of the chat bar: script 0x3f, the Select button of the BIOS clock screen */
#define SAY_W 112.0f
#define ID_BUTTON(i) ((uint16_t)(0x1301 + (i)))
#define BACK_X (-19.53125f)
#define BACK_Y (-14.84375f)
#define BACK_Z (-351.5625f)
#define PAGES 5
#define CUR_BACK (-1)

/* The panel: from where the rows start to the right edge of the rows' area, down to just above the buttons */
#define PANE_X 52.0f /* the clouds show all round the box: 52 px left and right, 40 above, 36 above the buttons */
#define PANE_Y 40.0f
#define PANE_W 536.0f
#define PANE_H 306.0f
#define PANE_Z 0.0028f /* just in front of the clouds (see ui_bios.c) */
#define TEXT_Z 0.5f
#define PAD 14.0f
#define ROW_H 26.0f
#define HEAD_Y (PANE_Y + 10.0f)
#define LIST_Y (PANE_Y + 44.0f)
#define ROWS 8 /* LIST_Y + 8 rows ends above the footer line */
#define ITEM_H 46.0f /* events and news: two lines per item */
#define ITEM_ROWS 4
#define STATUS_Y 10.0f /* the connection status, above the panel */
#define CHAR_W 11 /* the BIOS font's advance */

enum { PAGE_ONLINE, PAGE_PLAY, PAGE_EVENTS, PAGE_CHAT, PAGE_SETTINGS };
static const char* const page_names[PAGES] = {"Online", "Play", "Events", "Chat", "Settings"};
/* A page can show more than one list: START switches (Online: players / games, Events: events / news) */
enum { VIEW_A = 0, VIEW_B = 1, VIEW_SET_ISP = 1, VIEW_SET_TEST = 2, VIEW_SET_WIFI = 3 }; /* Chat: A = the DC99 IRC chat, B = the KosNet chat */

/* The lists behind the pages */
enum { LIST_PLAYERS, LIST_OGAMES, LIST_PLAY, LIST_EVENTS, LIST_NEWS, LIST_SETTINGS, LIST_ISP, LIST_TEST, LIST_WIFI, LISTS };
/* Settings page rows; the Internet rows of ui_settings.c follow (uis_net_first) */
enum { SET_STATUS, SET_WAY, SET_ISP, SET_TEST, SET_GW_NET, SET_GW_WIFI, SET_SOURCES, SET_ROWS };
/* ISP view rows */
enum { ISP_PRESET, ISP_ADDR, ISP_IP, ISP_MASK, ISP_GW, ISP_DNS, ISP_NUMBER, ISP_LOGIN, ISP_PASS, ISP_RESET, ISP_ROWS };

#define WHITE 0xFFFFFFFFu
#define GREY 0xFFB0B0B0u
#define DIM 0xFF909090u
#define YELLOW 0xFFF0E060u
#define GREEN 0xFF70E070u
#define RED 0xFFF07070u

#define GAMES_MAX 256
#define OGAMES_MAX 48

static bmenu* menu;
static int console_pal;
static int cursor = PAGE_ONLINE; /* CUR_BACK or the page button */
static int page = PAGE_ONLINE;
static int view[PAGES];
static int top[LISTS], sel[LISTS];
static int detail;     /* an event or a news item is open */
static int detail_top; /* the first line of it shown */
static int have_meta;  /* META.DAT is loaded: db_get_meta may be asked */
static unsigned games_built; /* frame of the last rebuild of the games lists */
static int (*start_game_fn)(const gd_item*); /* the game list's start (Serial VMU first); NULL: launch directly */
static unsigned anim;
static int probed;
static int adapter_tried; /* the adapter was connected on its own once since the start */
static dcnow_device_t device = DCNOW_DEV_NONE;
static dcnow_isp_t isp;
static dcnow_status_t conn;
static int was_online;
static uint64_t last_players_fetch, last_events_fetch, last_news_fetch;
static char notice[64];
static int notice_frames;

static int say_on, say_dead; /* the chat bar has its oval button this frame, and it cannot be used now */
static float say_cy;
static picolink_status_t gw; /* the gateway (DC-PicoLink): what it said last */
static uint64_t last_gw_ask;
static int wifi_pick; /* the scan result whose password is being typed */

static dcnow_fetch_status_t pstat;
static int players_gen = -1;
static dcnow_player_t players[DCNOW_PLAYER_MAX];
static int nplayers;

static events_status_t estat;
static int events_gen = -1;
static event_item_t events[EVENTS_MAX];
static int nevents;
/* The events still on: going on first, then coming up (the list is soonest first); hours-old ones are left out. */
static int ev_row[EVENTS_MAX];
static int ev_phase[EVENTS_MAX];
static int ev_minutes[EVENTS_MAX];
static int nev_rows;
static unsigned ev_built = ~0u; /* frame of the last rebuild (the clock moves on) */

static news_status_t nstat;
static int news_gen = -1;
static news_item_t news[NEWS_MAX];
static int nnews;

/* The two chats: the rows of each (copied when they change), and how far back the view is scrolled (rows from the newest) */
static dc99_row_t chat_rows[DC99_CHATS][DC99_ROWS];
static int chat_n[DC99_CHATS];
static int chat_gen[DC99_CHATS] = {-1, -1};
static int chat_back[DC99_CHATS];
static dc99_chat_status_t chat_st[DC99_CHATS];

static nt_line_t test_lines[NT_LINES];
static int ntest;

/* A row of the Play page: a game of the card (with the players in it now) or a browser disc. */
typedef struct {
    const gd_item* item;
    int players;
    int browser;
} game_row;
static game_row games[GAMES_MAX];
static int ngames;

/* A row of Online > Games: a game being played now; item is NULL when it is not on this card */
typedef struct {
    char title[40];
    int players;
    const gd_item* item;
} ogame_row;
static ogame_row ogames[OGAMES_MAX];
static int nogames;

static void
say(const char* text) {
    snprintf(notice, sizeof(notice), "%s", text);
    notice_frames = 180;
}

/* ---- the lists -------------------------------------------------------------------------------------------------------- */

/* Browser discs on the card, by their titles. */
static int
is_browser(const char* name) {
    static const char* const keys[] = {"BROWSER", "PASSPORT", "PLANETWEB", "PLANET WEB", "DREAMKEY", "DREAM KEY", "DREAMEXPLORER"};
    char up[128];
    size_t n = 0;
    for (; name[n] && n + 1 < sizeof(up); n++) {
        up[n] = (char)toupper((unsigned char)name[n]);
    }
    up[n] = '\0';
    for (unsigned k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
        if (strstr(up, keys[k])) {
            return 1;
        }
    }
    return 0;
}

static int
name_cmp(const char* a, const char* b) {
    for (; *a && tolower((unsigned char)*a) == tolower((unsigned char)*b); a++, b++) {
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int
strncasecmp_n(const char* hay, const char* needle, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!hay[i] || tolower((unsigned char)hay[i]) != tolower((unsigned char)needle[i])) {
            return 0;
        }
    }
    return 1;
}


static uint32_t
item_hash(const gd_item* g) {
    return (uint32_t)gd_item_recent_hash(g);
}

/* Games first by players online, then by name; the browsers last. */
static int
game_cmp(const void* x, const void* y) {
    const game_row* a = x;
    const game_row* b = y;
    if (a->browser != b->browser) {
        return a->browser - b->browser;
    }
    if (a->players != b->players) {
        return b->players - a->players;
    }
    return name_cmp(a->item->name, b->item->name);
}

static int
ogame_cmp(const void* x, const void* y) {
    const ogame_row* a = x;
    const ogame_row* b = y;
    if (a->players != b->players) {
        return b->players - a->players;
    }
    return name_cmp(a->title, b->title);
}

/* The games being played now: the card's games with players (by the Pi's account or the player list), then the titles the player list names that
 * the card does not have. */
static void
build_ogames(void) {
    nogames = 0;
    for (int i = 0; i < ngames && nogames < OGAMES_MAX; i++) {
        if (!games[i].browser && games[i].players > 0) {
            snprintf(ogames[nogames].title, sizeof(ogames[nogames].title), "%s", games[i].item->name);
            ogames[nogames].players = games[i].players;
            ogames[nogames++].item = games[i].item;
        }
    }
    int on_card = nogames;
    for (int p = 0; p < nplayers && nogames < OGAMES_MAX; p++) {
        const char* t = players[p].title;
        int known = !t[0];
        for (int k = 0; !known && k < nogames; k++) {
            known = name_cmp(ogames[k].title, t) == 0;
        }
        for (int k = 0; !known && k < on_card; k++) { /* "Phantasy Star Online Ver.2" against the card's "Phantasy Star Online Ver.2 (NTSC)" */
            size_t n = strlen(t);
            known = n >= 6 && strncasecmp_n(ogames[k].title, t, n);
        }
        if (known) {
            continue;
        }
        snprintf(ogames[nogames].title, sizeof(ogames[nogames].title), "%s", t);
        ogames[nogames].players = 0;
        ogames[nogames].item = NULL;
        for (int q = p; q < nplayers; q++) {
            ogames[nogames].players += name_cmp(players[q].title, t) == 0;
        }
        nogames++;
    }
    /* DCNET's games list: a game it counts players in that the list has not shown yet, or with more players than the list names */
    dcnet_game_t dg[DCNET_GAME_MAX];
    int ng = dcnow_fetch_copy_games(dg, DCNET_GAME_MAX);
    for (int d = 0; d < ng; d++) {
        int at = -1;
        for (int k = 0; at < 0 && k < nogames; k++) {
            size_t n = strlen(dg[d].name);
            if (name_cmp(ogames[k].title, dg[d].name) == 0 || (n >= 6 && strncasecmp_n(ogames[k].title, dg[d].name, n))) {
                at = k;
            }
        }
        if (at >= 0) {
            if (dg[d].players > ogames[at].players) {
                ogames[at].players = dg[d].players;
            }
        } else if (nogames < OGAMES_MAX) {
            const gd_item* card = online_games_find(dg[d].name);
            snprintf(ogames[nogames].title, sizeof(ogames[nogames].title), "%s", card ? card->name : dg[d].name);
            ogames[nogames].players = dg[d].players;
            ogames[nogames++].item = card;
        }
    }
    qsort(ogames, (size_t)nogames, sizeof(ogames[0]), ogame_cmp);
}

/* The Play page: every game of the card that has online play (META.DAT) or is being played online now, once per game (not every
 * disc of a set), and the browser discs. The cursor stays on the same game when the order changes. */
static void
build_games(void) {
    const gd_item* keep = sel[LIST_PLAY] < ngames ? games[sel[LIST_PLAY]].item : NULL;
    ngames = 0;
    for (int i = 1; i < list_all_count() && ngames < GAMES_MAX; i++) {
        const gd_item* g = list_all_item(i);
        if (!g || strncmp(g->disc, "DIR", 3) == 0) {
            continue;
        }
        if (is_browser(g->name)) {
            games[ngames].item = g;
            games[ngames].players = 0;
            games[ngames++].browser = 1;
            continue;
        }
        if (!g->product[0] || (gd_item_disc_total(g->disc) > 1 && gd_item_disc_num(g->disc) > 1)) {
            continue;
        }
        int n = online_games_players(g);
        struct db_item* meta = NULL;
        if (n > 0 || (have_meta && !db_get_meta(g->product, &meta) && meta && (meta->network & 3))) {
            games[ngames].item = g;
            games[ngames].players = n;
            games[ngames++].browser = 0;
        }
    }
    qsort(games, (size_t)ngames, sizeof(games[0]), game_cmp);
    for (int i = 0; keep && i < ngames; i++) {
        if (games[i].item == keep) {
            sel[LIST_PLAY] = i;
        }
    }
    build_ogames();
}

/* The events that are still on, with where each stands against the clock. */
static void
build_events(void) {
    int y, mo, d, h, mi;
    clock_get(&y, &mo, &d, &h, &mi);
    nev_rows = 0;
    for (int i = 0; i < nevents; i++) {
        int minutes;
        int ph = event_phase(events[i].sort_key, y, mo, d, h, mi, &minutes);
        if (ph == EVP_PAST) {
            continue;
        }
        ev_row[nev_rows] = i;
        ev_phase[nev_rows] = ph;
        ev_minutes[nev_rows++] = minutes;
    }
    ev_built = anim;
}

/* The list the cursor is in on a page, -1 when the page has none. */
static int
page_list(int p) {
    switch (p) {
        case PAGE_ONLINE: return view[p] == VIEW_A ? LIST_PLAYERS : LIST_OGAMES;
        case PAGE_CHAT: return -1; /* the chats scroll on their own */
        case PAGE_PLAY: return LIST_PLAY;
        case PAGE_EVENTS: return view[p] == VIEW_A ? LIST_EVENTS : LIST_NEWS;
        case PAGE_SETTINGS: return view[p] == VIEW_A ? LIST_SETTINGS : view[p] == VIEW_SET_ISP ? LIST_ISP : view[p] == VIEW_SET_WIFI ? LIST_WIFI : LIST_TEST;
        default: return -1;
    }
}

static int
list_rows(int l) {
    switch (l) {
        case LIST_PLAYERS: return nplayers;
        case LIST_OGAMES: return nogames;
        case LIST_PLAY: return ngames;
        case LIST_EVENTS: return nev_rows;
        case LIST_NEWS: return nnews;
        case LIST_ISP: return ISP_ROWS;
        case LIST_TEST: return ntest;
        case LIST_WIFI: return gw.info.scan_have;
        default: return SET_ROWS + uis_net_count();
    }
}

/* How many rows of a list fit on its page */
static int
list_visible(int l) {
    return l == LIST_EVENTS || l == LIST_NEWS ? ITEM_ROWS : ROWS;
}

static void
clamp_list(int l) {
    int n = list_rows(l);
    int vis = list_visible(l);
    if (sel[l] > n - 1) {
        sel[l] = n - 1;
    }
    if (sel[l] < 0) {
        sel[l] = 0;
    }
    if (sel[l] < top[l]) {
        top[l] = sel[l];
    }
    if (sel[l] >= top[l] + vis) {
        top[l] = sel[l] - vis + 1;
    }
}

/* Starts a game of the card (through the game list's start when it has one). Returns 1 if that took over. */
static int
start_item(const gd_item* g) {
    if (start_game_fn) {
        return start_game_fn(g);
    }
    launch_disc(g); /* only returns if it could not be started */
    return 0;
}

void
uin_set_launch(int (*start)(const gd_item*)) {
    start_game_fn = start;
}

/* ---- connection and fetches ------------------------------------------------------------------------------------------- */

static void
start_fetches(void) {
    last_players_fetch = last_events_fetch = last_news_fetch = timer_ms_gettime64();
    dcnow_fetch_start();
    events_fetch_start();
    news_fetch_start();
}

void
uin_set_meta(int loaded) {
    have_meta = loaded;
}

static void
connect_or_hang_up(void) {
    dcnow_conn_poll(&conn);
    switch (conn.state) {
        case DCNOW_CONN_ONLINE:
            dcnow_conn_disconnect();
            sound_sfx(BAUDIO_SFX_CANCEL);
            break;
        case DCNOW_CONN_CONNECTING:
            if (conn.can_cancel) {
                dcnow_conn_cancel();
                sound_sfx(BAUDIO_SFX_CANCEL);
            }
            break;
        case DCNOW_CONN_COOLDOWN: sound_sfx(BAUDIO_SFX_ERROR); break; /* the DreamPi needs a moment between calls */
        default:
            /* the engine looks for the ways in speed order (or the one the Connection setting names) and reads an adapter's address settings itself;
             * a flash ROM read runs with interrupts off: the engine's own copy when it has one (as the main branch) */
            if ((device == DCNOW_DEV_BBA || device == DCNOW_DEV_LAN) && !dcnow_conn_isp(&isp)) {
                dcnow_read_isp(&isp);
            }
            if (dcnow_conn_start(device, &isp) == 0) {
                sound_sfx(BAUDIO_SFX_CONFIRM);
            } else {
                sound_sfx(BAUDIO_SFX_ERROR);
            }
            break;
    }
}

void
uin_open(bmenu* m, int pal) {
    menu = m;
    console_pal = pal;
    bvm_kill_all(&m->vm);
    bvm_create(&m->vm, 6, ID_BACK, PRIO);
    bvm_create(&m->vm, 0x3f, ID_SAY, PRIO);
    for (int i = 0; i < PAGES; i++) {
        bvm_create(&m->vm, 0x17 + (i == 1 ? 2 : (i == 2 ? 1 : i)), ID_BUTTON(i), PRIO); /* left to right, as bios_list */
    }
    cursor = page;
    anim = 0;
    probed = 0;
    notice_frames = 0;
    detail = 0;
    for (int i = 0; i < PAGES; i++) {
        view[i] = VIEW_A; /* the settings sub-views are left too */
    }
    build_games();
    build_events();
    dcnow_conn_poll(&conn);
    was_online = conn.state == DCNOW_CONN_ONLINE;
    if (was_online) {
        start_fetches();
    }
}

/* ---- the editor for an address or a short text (the ISP settings) ------------------------------------------------------- */

enum { ED_ISP = 0, ED_CHAT_NICK, ED_CHAT_PASS, ED_CHAT_MSG, ED_KN_USER, ED_KN_PASS, ED_KN_MSG, ED_WIFI_PASS };
static char kn_user[48]; /* the KosNet login typed this visit (nothing is saved) */
static kosnet_post_status_t kn_st;
static int kn_gen = -1;
#define ED_MAX 120
#define ED_SHOWN 36 /* characters of the text on the panel at once */

typedef struct {
    int active;
    int kind;       /* ED_ISP, or one of the chat texts */
    int max;        /* characters the text may have, with the end mark */
    int row;        /* the ISP row being edited */
    int is_address; /* four numbers, else a text */
    uint8_t oct[4];
    int octi;
    char text[ED_MAX];
    int len, pos;
    const char* set; /* the characters UP and DOWN go through */
} editor_t;
static editor_t ed;
static osk_t kb; /* the on-screen keyboard of the text editors */

#define DIAL_SET "0123456789*#,W"
#define TEXT_SET " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-@!"
#define CHAT_SET " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,?!'-_:;()/@#+="
#define NICK_SET "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-[]{}^`|"
#define KN_PASS_SET "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ "
#define PASS_SET "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-@!#+=:;,?"

static void
isp_save(const np_isp_t* v) {
    np_set_isp(v);
    online_apply_prefs();
}

static uint8_t*
isp_field(np_isp_t* v, int row) {
    return row == ISP_IP ? v->ip : row == ISP_MASK ? v->mask : row == ISP_GW ? v->gateway : v->dns;
}

static char*
isp_text_field(np_isp_t* v, int row) {
    return row == ISP_NUMBER ? v->number : row == ISP_LOGIN ? v->login : v->password;
}

static void
editor_open(int row) {
    np_isp_t cur = *np_isp();
    memset(&ed, 0, sizeof(ed));
    ed.active = 1;
    ed.kind = ED_ISP;
    ed.max = NP_DIAL_LEN;
    ed.row = row;
    if (row >= ISP_IP && row <= ISP_DNS) {
        ed.is_address = 1;
        memcpy(ed.oct, isp_field(&cur, row), 4);
    } else {
        snprintf(ed.text, sizeof(ed.text), "%s", isp_text_field(&cur, row));
        ed.len = (int)strlen(ed.text);
        ed.set = row == ISP_NUMBER ? DIAL_SET : TEXT_SET;
        ed.len = (int)strlen(ed.text);
        ed.pos = ed.len;
    }
    osk_init(&kb);
}

/* The chat's texts: the nick and the password of the IRC login, or a line to say. */
static void
editor_open_chat(int kind, const char* start) {
    memset(&ed, 0, sizeof(ed));
    ed.active = 1;
    ed.kind = kind;
    ed.max = kind == ED_CHAT_MSG || kind == ED_KN_MSG ? ED_MAX : kind == ED_KN_USER || kind == ED_KN_PASS ? 41 : kind == ED_WIFI_PASS ? 64 : NP_CHAT_LEN;
    ed.set = kind == ED_CHAT_NICK ? NICK_SET : kind == ED_CHAT_PASS ? PASS_SET : kind == ED_KN_PASS || kind == ED_WIFI_PASS ? KN_PASS_SET : kind == ED_KN_USER ? PASS_SET : CHAT_SET;
    snprintf(ed.text, sizeof(ed.text), "%s", start);
    ed.len = (int)strlen(ed.text);
    ed.pos = ed.len;
    osk_init(&kb);
}

/* A character typed at the cursor (when the editor's set allows it and there is room). */
static int
editor_type(char c) {
    if (!osk_char_ok(ed.set, c) || ed.len >= ed.max - 1) {
        return 0;
    }
    memmove(ed.text + ed.pos + 1, ed.text + ed.pos, (size_t)(ed.len - ed.pos + 1));
    ed.text[ed.pos++] = c;
    ed.len++;
    return 1;
}

/* The character before the cursor goes. */
static int
editor_backspace(void) {
    if (ed.pos <= 0) {
        return 0;
    }
    memmove(ed.text + ed.pos - 1, ed.text + ed.pos, (size_t)(ed.len - ed.pos + 1));
    ed.pos--;
    ed.len--;
    return 1;
}

/* A or START in a chat text: the nick leads on to the password, the password starts the chat, a line goes out. */
static void
chat_editor_done(void) {
    char text[ED_MAX];
    int kind = ed.kind;

    snprintf(text, sizeof(text), "%s", ed.text);
    for (int n = (int)strlen(text); n > 0 && text[n - 1] == ' '; n--) {
        text[n - 1] = '\0';
    }
    if (kind == ED_WIFI_PASS) {
        if (text[0] == '\0' || !pl_pass_ok(text)) { /* the box takes 8 to 63 characters with no blank at either end */
            sound_sfx(BAUDIO_SFX_ERROR);
            say("8 to 63 characters, no blank at either end");
            return;
        }
        ed.active = 0;
        sound_sfx(BAUDIO_SFX_CONFIRM);
        picolink_join(wifi_pick, gw.info.scan[wifi_pick].name, ed.text); /* ed.text, not the trimmed copy: the box trims on its own */
        memset(ed.text, 0, sizeof(ed.text));
        memset(text, 0, sizeof(text));
        say("Joining the network...");
        last_gw_ask = timer_ms_gettime64();
        return;
    }
    if (kind == ED_KN_MSG) {
        ed.active = 0;
        if (text[0] && kosnet_post(text) == 0) {
            sound_sfx(BAUDIO_SFX_CONFIRM);
            chat_back[DC99_CHAT_KOSNET] = 0;
        } else if (text[0]) {
            sound_sfx(BAUDIO_SFX_ERROR);
            say("Not sent: sign in first");
        }
        return;
    }
    if (kind == ED_KN_USER || kind == ED_KN_PASS) {
        if (text[0] == '\0') {
            sound_sfx(BAUDIO_SFX_ERROR);
            say(kind == ED_KN_USER ? "A username is needed" : "A password is needed");
            return;
        }
        if (kind == ED_KN_USER) {
            snprintf(kn_user, sizeof(kn_user), "%s", text);
            sound_sfx(BAUDIO_SFX_CONFIRM);
            editor_open_chat(ED_KN_PASS, "");
            return;
        }
        ed.active = 0;
        sound_sfx(BAUDIO_SFX_CONFIRM);
        kosnet_login(kn_user, text);
        memset(ed.text, 0, sizeof(ed.text));
        return;
    }
    if (kind == ED_CHAT_MSG) {
        ed.active = 0;
        if (text[0] == '\0') {
            return;
        }
        if (dc99_irc_send(text) == 0) {
            sound_sfx(BAUDIO_SFX_CONFIRM);
            chat_back[DC99_CHAT_IRC] = 0;
        } else {
            sound_sfx(BAUDIO_SFX_ERROR);
            say("Not sent: wait a moment or reconnect");
        }
        return;
    }
    if (text[0] == '\0') {
        sound_sfx(BAUDIO_SFX_ERROR);
        say(kind == ED_CHAT_NICK ? "A nick is needed" : "A password is needed");
        return;
    }
    if (kind == ED_CHAT_NICK) {
        np_set_chat(text, np_chat_pass());
        sound_sfx(BAUDIO_SFX_CONFIRM);
        editor_open_chat(ED_CHAT_PASS, np_chat_pass());
        return;
    }
    np_set_chat(np_chat_nick(), text);
    ed.active = 0;
    sound_sfx(BAUDIO_SFX_CONFIRM);
    dc99_irc_stop(); /* a changed login starts over */
    if (conn.state == DCNOW_CONN_ONLINE) {
        dc99_irc_start(np_chat_nick(), np_chat_pass());
        say("Connecting to the DC99 chat");
    } else {
        say("Saved. Connect to chat");
    }
}

/* The text or address is done: saved (ISP rows) or handed on (the chats). */
static void
editor_finish(void) {
    if (ed.kind != ED_ISP) {
        chat_editor_done();
        return;
    }
    np_isp_t cur = *np_isp();
    if (ed.is_address) {
        memcpy(isp_field(&cur, ed.row), ed.oct, 4);
    } else {
        while (ed.len > 0 && ed.text[ed.len - 1] == ' ') { /* trailing blanks are not part of a login */
            ed.text[--ed.len] = '\0';
        }
        snprintf(isp_text_field(&cur, ed.row), NP_DIAL_LEN, "%s", ed.text);
    }
    isp_save(&cur);
    ed.active = 0;
    sound_sfx(BAUDIO_SFX_CONFIRM);
    say("Saved. Used at the next connect");
}

/* A on the keyboard: the key under the cursor. */
static void
editor_press(void) {
    osk_key_t key = osk_key(&kb);
    int ok = 1;

    switch (key.kind) {
        case OSK_CHAR: ok = editor_type(key.ch); break;
        case OSK_SPACE: ok = editor_type(' '); break;
        case OSK_DEL: ok = editor_backspace(); break;
        case OSK_SHIFT: osk_toggle_shift(&kb); break;
        case OSK_SYM: osk_toggle_symbols(&kb); break;
        case OSK_OK: editor_finish(); return;
        default: break;
    }
    sound_sfx(ok ? BAUDIO_SFX_CURSOR : BAUDIO_SFX_ERROR);
}

/* A keyboard is plugged in: no key grid, the text is typed. Enter is done, Escape cancels, Backspace deletes. A controller still has START, B, X (space) and Y (delete). */
static void
editor_handle_typing(button_t b) {
    int c = input_typed_ascii();
    int kbd = input_from_keyboard();

    if (c == 8) {
        sound_sfx(editor_backspace() ? BAUDIO_SFX_CURSOR : BAUDIO_SFX_ERROR);
        return;
    }
    if (c) {
        sound_sfx(editor_type((char)c) ? BAUDIO_SFX_CURSOR : BAUDIO_SFX_ERROR);
        return;
    }
    switch (b) {
        case BTN_A:
        case BTN_START: editor_finish(); return;
        case BTN_B: ed.active = 0; sound_sfx(BAUDIO_SFX_CANCEL); return;
        case BTN_LEFT: ed.pos = ed.pos > 0 ? ed.pos - 1 : 0; break;
        case BTN_RIGHT: ed.pos = ed.pos < ed.len ? ed.pos + 1 : ed.len; break;
        case BTN_X:
            if (!kbd) {
                sound_sfx(editor_type(' ') ? BAUDIO_SFX_CURSOR : BAUDIO_SFX_ERROR);
            }
            return;
        case BTN_Y:
            if (!kbd) {
                sound_sfx(editor_backspace() ? BAUDIO_SFX_CURSOR : BAUDIO_SFX_ERROR);
            }
            return;
        default: return;
    }
    sound_sfx(BAUDIO_SFX_CURSOR);
}

/* A button while the editor is open */
static void
editor_handle(button_t b) {
    if (!ed.is_address && input_keyboard_present()) {
        editor_handle_typing(b);
        return;
    }
    switch (b) {
        case BTN_B: ed.active = 0; sound_sfx(BAUDIO_SFX_CANCEL); return;
        case BTN_START: editor_finish(); return;
        case BTN_A:
            if (!ed.is_address) {
                editor_press();
            } else {
                editor_finish();
            }
            return;
        default: break;
    }
    if (ed.is_address) {
        switch (b) {
            case BTN_LEFT: ed.octi = ed.octi > 0 ? ed.octi - 1 : 0; break;
            case BTN_RIGHT: ed.octi = ed.octi < 3 ? ed.octi + 1 : 3; break;
            case BTN_UP: ed.oct[ed.octi]++; break;
            case BTN_DOWN: ed.oct[ed.octi]--; break;
            case BTN_PAGE_UP:
            case BTN_X: ed.oct[ed.octi] = (uint8_t)(ed.oct[ed.octi] + 16); break;
            case BTN_PAGE_DOWN:
            case BTN_Y: ed.oct[ed.octi] = (uint8_t)(ed.oct[ed.octi] - 16); break;
            default: return;
        }
    } else {
        switch (b) {
            case BTN_LEFT: osk_move(&kb, 0, -1); break;
            case BTN_RIGHT: osk_move(&kb, 0, 1); break;
            case BTN_UP: osk_move(&kb, -1, 0); break;
            case BTN_DOWN: osk_move(&kb, 1, 0); break;
            case BTN_PAGE_UP: ed.pos = ed.pos > 0 ? ed.pos - 1 : 0; break; /* the cursor in the text */
            case BTN_PAGE_DOWN: ed.pos = ed.pos < ed.len ? ed.pos + 1 : ed.len; break;
            case BTN_X: /* a space */
                if (!editor_type(' ')) {
                    sound_sfx(BAUDIO_SFX_ERROR);
                    return;
                }
                break;
            case BTN_Y: /* delete the character before the cursor */
                if (!editor_backspace()) {
                    sound_sfx(BAUDIO_SFX_ERROR);
                    return;
                }
                break;
            default: return;
        }
    }
    sound_sfx(BAUDIO_SFX_CURSOR);
}

/* ---- input -------------------------------------------------------------------------------------------------------------- */

/* "DreamPi (default)", "Home network (DHCP)" or "Custom" from what is saved */
static const char*
isp_preset_name(const np_isp_t* v) {
    int dial_default = !v->number[0] && !v->login[0] && !v->password[0];
    if (v->mode == NP_ISP_CONSOLE && dial_default) {
        return "DreamPi (default)";
    }
    if (v->mode == NP_ISP_DHCP && dial_default) {
        return "Home network (DHCP)";
    }
    return "Custom";
}

static void
ip_text(const uint8_t* a, char* out, size_t size) {
    snprintf(out, size, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
}

static const char*
isp_row_text(int row, char* out, size_t size, uint32_t* colour) {
    const np_isp_t* v = np_isp();
    char a[24];
    switch (row) {
        case ISP_PRESET: snprintf(out, size, "Preset  %s", isp_preset_name(v)); break;
        case ISP_ADDR: snprintf(out, size, "Adapter address  %s", v->mode == NP_ISP_DHCP ? "DHCP" : v->mode == NP_ISP_STATIC ? "Fixed" : "Console's own"); break;
        case ISP_IP:
        case ISP_MASK:
        case ISP_GW:
        case ISP_DNS: {
            static const char* const names[] = {"IP address", "Netmask", "Gateway", "DNS server"};
            ip_text(isp_field((np_isp_t*)v, row), a, sizeof(a));
            snprintf(out, size, "%s  %s", names[row - ISP_IP], a);
            if (v->mode != NP_ISP_STATIC) {
                *colour = DIM; /* only used with a fixed address */
            }
            break;
        }
        case ISP_NUMBER: snprintf(out, size, "Dial number  %s", v->number[0] ? v->number : "(DreamPi's)"); break;
        case ISP_LOGIN: snprintf(out, size, "Login  %s", v->login[0] ? v->login : "(DreamPi's)"); break;
        case ISP_PASS: {
            char dots[NP_DIAL_LEN];
            size_t n = strlen(v->password);
            memset(dots, '*', n);
            dots[n] = '\0';
            snprintf(out, size, "Password  %s", n ? dots : "(DreamPi's)");
            break;
        }
        default: snprintf(out, size, "Back to the defaults"); break;
    }
    return out;
}

/* A on an ISP row */
static void
isp_activate(int row) {
    np_isp_t cur = *np_isp();
    switch (row) {
        case ISP_PRESET: { /* DreamPi (default) -> Home network (DHCP) -> back; Custom goes to the first */
            int to_dhcp = strcmp(isp_preset_name(&cur), "DreamPi (default)") == 0;
            memset(cur.number, 0, sizeof(cur.number));
            memset(cur.login, 0, sizeof(cur.login));
            memset(cur.password, 0, sizeof(cur.password));
            cur.mode = to_dhcp ? NP_ISP_DHCP : NP_ISP_CONSOLE;
            isp_save(&cur);
            sound_sfx(BAUDIO_SFX_CONFIRM);
            break;
        }
        case ISP_ADDR:
            cur.mode = (uint8_t)((cur.mode + 1) % 3);
            isp_save(&cur);
            sound_sfx(BAUDIO_SFX_CONFIRM);
            break;
        case ISP_RESET:
            memset(&cur, 0, sizeof(cur));
            isp_save(&cur);
            sound_sfx(BAUDIO_SFX_CONFIRM);
            say("ISP settings reset");
            break;
        default:
            if (row >= ISP_IP && row <= ISP_DNS && cur.mode != NP_ISP_STATIC) {
                sound_sfx(BAUDIO_SFX_ERROR);
                say("Set the adapter address to Fixed first");
            } else {
                editor_open(row);
                sound_sfx(BAUDIO_SFX_ENTER);
            }
            break;
    }
}

/* Settings page rows */
static const char*
setting_text(int row, char* out, size_t size) {
    dcnow_device_t used = dcnow_device_hint();
    int online = conn.state == DCNOW_CONN_ONLINE;
    if (row >= SET_ROWS) {
        int r = uis_net_first() + row - SET_ROWS;
        snprintf(out, size, "%s  %s", uis_label(r), uis_value(r));
        return out;
    }
    switch (row) {
        case SET_STATUS:
            snprintf(out, size, "Connection  %s", online ? "Online (A: hang up)" : conn.state == DCNOW_CONN_CONNECTING ? "Connecting..." : "Off (A: connect)");
            break;
        case SET_WAY:
            snprintf(out, size, "Connect by  %s%s", netsel_setting_name(np_iface()),
                     np_iface() == NETSEL_AUTO ? (used != DCNOW_DEV_NONE ? " (last: " : "") : "");
            if (np_iface() == NETSEL_AUTO && used != DCNOW_DEV_NONE) {
                size_t n = strlen(out);
                snprintf(out + n, size - n, "%s)", dcnow_device_short_name(used));
            }
            break;
        case SET_ISP: snprintf(out, size, "ISP settings  %s >", isp_preset_name(np_isp())); break;
        case SET_TEST: snprintf(out, size, "Network test >"); break;
        case SET_GW_NET:
            if (gw.reached && gw.info.has_net) {
                snprintf(out, size, "Gateway network  %s%s", gw.info.dcnet == 1 ? "DCNet" : "DCNow!", gw.busy ? " ..." : " (A: switch)");
            } else {
                snprintf(out, size, "Gateway  %s", gw.busy ? "looking..." : online ? (gw.reached ? "has no network switch" : "none found (A: look again)") : "connect first");
            }
            break;
        case SET_GW_WIFI:
            if (gw.reached && gw.info.has_wifi) {
                const char* state = gw.info.wifi_state;
                snprintf(out, size, "Gateway Wi-Fi  %s%s%s >", !strcmp(state, "connected") ? "" : state[0] && strcmp(state, "none") ? state : "not set up",
                         gw.info.wifi_ssid[0] ? "  " : "", gw.info.wifi_ssid);
            } else {
                snprintf(out, size, "Gateway Wi-Fi  %s", gw.reached ? "not on this gateway" : "needs the gateway");
            }
            break;
        default: snprintf(out, size, "Players  %s", pstat.multi_network ? "DCNow! and DCNET" : "DCNow!"); break;
    }
    return out;
}

/* A on a Settings row */
static void
settings_activate(int row) {
    if (row >= SET_ROWS) { /* a choice: the next value, saved at once */
        int r = uis_net_first() + row - SET_ROWS;
        uis_change(r, 1);
        sound_sfx(BAUDIO_SFX_CONFIRM);
        say(uis_commit() == 0 ? uis_help(r) : "Could not save settings");
        return;
    }
    switch (row) {
        case SET_STATUS: connect_or_hang_up(); break;
        case SET_WAY:
            np_set_iface((np_iface() + 1) % NETSEL_COUNT);
            online_apply_prefs();
            sound_sfx(BAUDIO_SFX_CONFIRM);
            say(conn.state == DCNOW_CONN_ONLINE ? "Used at the next connect" : np_iface() == NETSEL_AUTO ? "Auto: the fastest one that is there" : "Only this one is tried");
            break;
        case SET_ISP:
            view[PAGE_SETTINGS] = VIEW_SET_ISP;
            sound_sfx(BAUDIO_SFX_ENTER);
            break;
        case SET_TEST:
            view[PAGE_SETTINGS] = VIEW_SET_TEST;
            net_test_start(conn.state == DCNOW_CONN_CONNECTING || conn.state == DCNOW_CONN_ONLINE, conn.state == DCNOW_CONN_ONLINE);
            sound_sfx(BAUDIO_SFX_ENTER);
            break;
        case SET_GW_NET:
            if (conn.state != DCNOW_CONN_ONLINE) {
                sound_sfx(BAUDIO_SFX_ERROR);
                say("Not connected: X connects");
            } else if (gw.busy) {
                sound_sfx(BAUDIO_SFX_ERROR);
            } else if (gw.reached && gw.info.has_net) {
                picolink_select(gw.info.dcnet != 1); /* the other one, from the gateway's next call */
                last_gw_ask = timer_ms_gettime64();
                sound_sfx(BAUDIO_SFX_CONFIRM);
            } else {
                picolink_probe();
                last_gw_ask = timer_ms_gettime64();
                sound_sfx(BAUDIO_SFX_CONFIRM);
                say("Looking for the gateway");
            }
            break;
        case SET_GW_WIFI:
            if (gw.reached && gw.info.has_wifi && !gw.busy) {
                view[PAGE_SETTINGS] = VIEW_SET_WIFI;
                picolink_scan();
                last_gw_ask = timer_ms_gettime64();
                sound_sfx(BAUDIO_SFX_ENTER);
            } else {
                sound_sfx(BAUDIO_SFX_ERROR);
                say(gw.busy ? "Wait a moment" : "Only a DC-PicoLink gateway has Wi-Fi to set up");
            }
            break;
        default: sound_sfx(BAUDIO_SFX_CURSOR); say("Nothing to change here"); break;
    }
}

/* A on a network of the Wi-Fi view: an open one is joined at once, a locked one asks for its password first. */
static void
wifi_activate(int i) {
    if (gw.busy) {
        sound_sfx(BAUDIO_SFX_ERROR);
        say("Wait a moment");
    } else if (i >= gw.info.scan_have) {
        picolink_scan(); /* nothing listed: look again */
        last_gw_ask = timer_ms_gettime64();
        sound_sfx(BAUDIO_SFX_CONFIRM);
    } else if (gw.info.scan[i].open) {
        picolink_join(i, gw.info.scan[i].name, "");
        last_gw_ask = timer_ms_gettime64();
        sound_sfx(BAUDIO_SFX_CONFIRM);
        say("Joining the network...");
    } else {
        wifi_pick = i;
        editor_open_chat(ED_WIFI_PASS, "");
        sound_sfx(BAUDIO_SFX_ENTER);
    }
}

/* Y: star or unstar what the cursor is on. Returns 1 when something was there to star. */
static int
star_selected(void) {
    int r;
    if (page == PAGE_ONLINE && view[page] == VIEW_A) {
        if (sel[LIST_PLAYERS] >= nplayers) {
            return 0;
        }
        r = np_player_toggle(players[sel[LIST_PLAYERS]].name);
    } else if (page == PAGE_ONLINE) {
        if (view[page] != VIEW_B || sel[LIST_OGAMES] >= nogames || !ogames[sel[LIST_OGAMES]].item) {
            return 0;
        }
        r = np_game_toggle(item_hash(ogames[sel[LIST_OGAMES]].item));
    } else if (page == PAGE_PLAY) {
        if (sel[LIST_PLAY] >= ngames || games[sel[LIST_PLAY]].browser) {
            return 0;
        }
        r = np_game_toggle(item_hash(games[sel[LIST_PLAY]].item));
    } else if (page == PAGE_EVENTS && view[page] == VIEW_A) {
        if (sel[LIST_EVENTS] >= nev_rows) {
            return 0;
        }
        r = np_event_toggle(events[ev_row[sel[LIST_EVENTS]]].title);
    } else {
        return 0;
    }
    if (r < 0) {
        sound_sfx(BAUDIO_SFX_ERROR);
        say("The star list is full");
    } else {
        sound_sfx(BAUDIO_SFX_CONFIRM);
        say(r ? "Starred: you are told when it is on" : "Star removed");
    }
    return 1;
}

/* A launch from the Online or Play list */
static void
launch_row(const gd_item* g) {
    if (!g) {
        sound_sfx(BAUDIO_SFX_ERROR);
        say("That game is not on this card");
        return;
    }
    sound_sfx(BAUDIO_SFX_ENTER);
    if (!start_item(g)) {
        sound_sfx(BAUDIO_SFX_ERROR);
        say("Could not start it");
    }
}

static void
leave_screen(void) {
    events_fetch_abort();
    news_fetch_abort();
    dc99_chat_stop();
    kosnet_post_stop();
    picolink_stop();
}

/* A in the IRC view: sets the login up, connects, or opens the line to say. */
static void
kosnet_activate(int online) {
    if (!online) {
        sound_sfx(BAUDIO_SFX_ERROR);
        say("Not connected: X connects");
    } else if (!kosnet_tls_available()) {
        sound_sfx(BAUDIO_SFX_ERROR);
        say("Posting needs TLS: not in this build");
    } else if (kn_st.state == KOSNET_BUSY) {
        sound_sfx(BAUDIO_SFX_ERROR);
    } else if (kn_st.state == KOSNET_IN) {
        editor_open_chat(ED_KN_MSG, "");
        sound_sfx(BAUDIO_SFX_ENTER);
    } else {
        editor_open_chat(ED_KN_USER, kn_user);
        sound_sfx(BAUDIO_SFX_ENTER);
    }
}

static void
chat_irc_activate(int online) {
    if (!online) {
        sound_sfx(BAUDIO_SFX_ERROR);
        say("Not connected: X connects");
    } else if (!np_chat_nick()[0] || !np_chat_pass()[0]) {
        editor_open_chat(ED_CHAT_NICK, np_chat_nick());
        sound_sfx(BAUDIO_SFX_ENTER);
    } else if (chat_st[DC99_CHAT_IRC].state == DC99_CHAT_ON) {
        editor_open_chat(ED_CHAT_MSG, "");
        sound_sfx(BAUDIO_SFX_ENTER);
    } else if (chat_st[DC99_CHAT_IRC].state == DC99_CHAT_CONNECTING) {
        sound_sfx(BAUDIO_SFX_ERROR);
    } else {
        dc99_irc_start(np_chat_nick(), np_chat_pass());
        sound_sfx(BAUDIO_SFX_CONFIRM);
    }
}

int
uin_handle(button_t b) {
    if (ed.active) {
        editor_handle(b);
        return 0;
    }
    switch (b) {
        case BTN_LEFT:
            if (cursor > CUR_BACK) {
                detail = 0;
                cursor--;
                if (cursor >= 0) {
                    page = cursor;
                }
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        case BTN_RIGHT:
            if (cursor < PAGES - 1) {
                detail = 0;
                cursor++;
                page = cursor;
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        case BTN_UP:
        case BTN_DOWN:
        case BTN_PAGE_UP:
        case BTN_PAGE_DOWN: {
            int l = page_list(page);
            if (page == PAGE_CHAT) { /* a chat: up goes back in time */
                int w = view[page] == VIEW_A ? DC99_CHAT_IRC : DC99_CHAT_KOSNET;
                int step = b == BTN_UP ? 1 : b == BTN_DOWN ? -1 : b == BTN_PAGE_UP ? 4 : -4;
                int most = chat_n[w] > 1 ? chat_n[w] - 1 : 0;
                int to = chat_back[w] + step;
                chat_back[w] = to < 0 ? 0 : to > most ? most : to;
                break;
            }
            if (l < 0) {
                break;
            }
            if (detail) { /* an open item: scroll its text */
                int step = b == BTN_UP ? -1 : b == BTN_DOWN ? 1 : b == BTN_PAGE_UP ? -4 : 4;
                detail_top = detail_top + step < 0 ? 0 : detail_top + step;
                break;
            }
            int step = b == BTN_UP ? -1 : b == BTN_DOWN ? 1 : b == BTN_PAGE_UP ? -list_visible(l) : list_visible(l);
            int before = sel[l];
            sel[l] += step;
            clamp_list(l);
            if (sel[l] != before) {
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        }
        case BTN_A: {
            if (cursor == CUR_BACK) {
                sound_sfx(BAUDIO_SFX_CANCEL);
                leave_screen();
                return 1;
            }
            bvm_obj* o = bvm_find(&menu->vm, ID_BUTTON(cursor));
            if (o) {
                o->var[1] = 1; /* the button jumps, as in the CD player */
            }
            int online = conn.state == DCNOW_CONN_ONLINE;
            switch (page) {
                case PAGE_CHAT:
                    if (view[page] == VIEW_A) {
                        chat_irc_activate(online);
                    } else {
                        kosnet_activate(online);
                    }
                    break;
                case PAGE_ONLINE:
                    if (view[page] == VIEW_A) { /* the player list: refresh it */
                        if (online) {
                            sound_sfx(BAUDIO_SFX_CONFIRM);
                            start_fetches();
                        } else {
                            sound_sfx(BAUDIO_SFX_ERROR);
                            say("Not connected: X connects");
                        }
                    } else if (sel[LIST_OGAMES] < nogames) { /* join or start the game */
                        launch_row(ogames[sel[LIST_OGAMES]].item);
                    } else {
                        sound_sfx(BAUDIO_SFX_ERROR);
                        say("Nobody is playing right now");
                    }
                    break;
                case PAGE_PLAY:
                    if (sel[LIST_PLAY] < ngames) {
                        launch_row(games[sel[LIST_PLAY]].item);
                    } else {
                        sound_sfx(BAUDIO_SFX_ERROR);
                        say("No online games on this card");
                    }
                    break;
                case PAGE_EVENTS: {
                    int l = page_list(page);
                    int n = l == LIST_EVENTS ? nev_rows : nnews;
                    if (detail) {
                        detail = 0;
                    } else if (n > 0) {
                        detail = 1;
                        detail_top = 0;
                        sound_sfx(BAUDIO_SFX_ENTER);
                    } else if (online) {
                        sound_sfx(BAUDIO_SFX_CONFIRM);
                        start_fetches();
                    } else {
                        sound_sfx(BAUDIO_SFX_ERROR);
                        say("Not connected: X connects");
                    }
                    break;
                }
                default:
                    if (view[page] == VIEW_A) {
                        settings_activate(sel[LIST_SETTINGS]);
                    } else if (view[page] == VIEW_SET_ISP) {
                        isp_activate(sel[LIST_ISP]);
                    } else if (view[page] == VIEW_SET_WIFI) {
                        wifi_activate(sel[LIST_WIFI]);
                    } else { /* the test view: run it again */
                        net_test_start(conn.state == DCNOW_CONN_CONNECTING || conn.state == DCNOW_CONN_ONLINE, conn.state == DCNOW_CONN_ONLINE);
                        sound_sfx(BAUDIO_SFX_CONFIRM);
                    }
                    break;
            }
            break;
        }
        case BTN_START:
            if (cursor == CUR_BACK) {
                break;
            }
            detail = 0;
            if (page == PAGE_ONLINE || page == PAGE_EVENTS || page == PAGE_CHAT) { /* players / games, events / news, IRC / KosNet */
                view[page] = view[page] == VIEW_A ? VIEW_B : VIEW_A;
                sound_sfx(BAUDIO_SFX_CURSOR);
            }
            break;
        case BTN_Y:
            if (cursor == CUR_BACK) {
                break;
            }
            if (page == PAGE_CHAT && view[page] == VIEW_A) {
                editor_open_chat(ED_CHAT_NICK, np_chat_nick()); /* the login: nick, then password */
                sound_sfx(BAUDIO_SFX_ENTER);
            } else if (page == PAGE_CHAT) { /* KosNet: the login (or out again) */
                if (kn_st.state == KOSNET_IN) {
                    kosnet_logout();
                    sound_sfx(BAUDIO_SFX_CONFIRM);
                    say("Signed out of KosNet");
                } else if (conn.state == DCNOW_CONN_ONLINE && kosnet_tls_available() && kn_st.state != KOSNET_BUSY) {
                    editor_open_chat(ED_KN_USER, kn_user);
                    sound_sfx(BAUDIO_SFX_ENTER);
                } else {
                    sound_sfx(BAUDIO_SFX_ERROR);
                }
            } else if (page == PAGE_SETTINGS && view[page] == VIEW_SET_WIFI) { /* search again */
                if (gw.busy) {
                    sound_sfx(BAUDIO_SFX_ERROR);
                } else {
                    picolink_scan();
                    last_gw_ask = timer_ms_gettime64();
                    sound_sfx(BAUDIO_SFX_CONFIRM);
                }
            } else if (detail && page == PAGE_EVENTS && page_list(page) == LIST_EVENTS) {
                star_selected();
            } else if (!detail && !star_selected()) {
                sound_sfx(BAUDIO_SFX_ERROR);
            }
            break;
        case BTN_X: connect_or_hang_up(); break;
        case BTN_B:
            sound_sfx(BAUDIO_SFX_CANCEL);
            if (detail) {
                detail = 0;
                break;
            }
            if (page == PAGE_SETTINGS && view[page] != VIEW_A) { /* back from the ISP settings or the test */
                view[page] = VIEW_A;
                break;
            }
            {
                uint64_t t0 = timer_ms_gettime64();
                printf("OM B: Internet screen leaves, stopping the fetches\n");
                events_fetch_abort();
                uint64_t t1 = timer_ms_gettime64();
                news_fetch_abort();
                dc99_chat_stop();
                kosnet_post_stop();
                picolink_stop();
                printf("OM B: events stopped in %u ms, news in %u ms\n", (unsigned)(t1 - t0), (unsigned)(timer_ms_gettime64() - t1));
            }
            return 1;
        default: break;
    }
    return 0;
}

void
uin_hover(float x, float y) {
    if (ed.active) {
        return;
    }
    if (blist_back_at_px(x, y)) {
        if (cursor != CUR_BACK) sound_sfx(BAUDIO_SFX_CURSOR);
        cursor = CUR_BACK;
        return;
    }
    int bt = blist_button_at_px(x, y);
    if (bt >= 0 && bt < PAGES && bt != cursor) {
        sound_sfx(BAUDIO_SFX_CURSOR);
        cursor = page = bt;
    }
}

/* ---- per frame ------------------------------------------------------------------------------------------------------- */

static void
place(bvm_obj* o, float x, float y, float z) {
    o->flags &= ~(uint32_t)BVM_F_ATTACHED;
    o->pos_tw[0].cur = o->pos[0] = x;
    o->pos_tw[1].cur = o->pos[1] = y;
    o->pos_tw[2].cur = o->pos[2] = z;
    o->pos_tw[0].step = o->pos_tw[1].step = o->pos_tw[2].step = 0;
}

void
uin_sync(void) {
    anim++;
    if (notice_frames > 0) {
        notice_frames--;
    }
    if (!probed && anim > 1) {
        /* the expansion port is probed once the screen shows (it can take a moment), as the main branch's window */
        probed = 1;
        device = dcnow_detect_device();
        /* An adapter needs no Connect step (the main branch's window connects it at once); a modem, the serial cable or the W5500 wait
         * for X, since a dial takes the phone line and the others probe the serial port. Only once per start: after a hang-up X connects again. */
        dcnow_conn_poll(&conn);
        if (!adapter_tried && conn.state == DCNOW_CONN_IDLE && (device == DCNOW_DEV_BBA || device == DCNOW_DEV_LAN)) {
            adapter_tried = 1;
            connect_or_hang_up();
        }
    }
    dcnow_conn_poll(&conn);
    int online = conn.state == DCNOW_CONN_ONLINE;
    if (online && !was_online) {
        start_fetches(); /* just connected */
    }
    if (!online && was_online) {
        nplayers = 0; /* the lists go with the connection, as in the main branch */
        players_gen = -1;
        build_games();
    }
    if (!online && was_online) {
        dc99_chat_stop(); /* the sockets went with the connection */
        kosnet_post_stop();
        picolink_stop();
    }
    if (online && page == PAGE_CHAT && view[page] == VIEW_B) {
        dc99_kosnet_start(); /* reading starts when the view is looked at; stays until the screen is left */
    }
    kosnet_post_status(&kn_st);
    if (kn_st.generation != kn_gen) {
        kn_gen = kn_st.generation;
        if (kn_st.note[0] && page == PAGE_CHAT) {
            say(kn_st.note); /* signed in, sent, or what went wrong */
        }
    }
    for (int w = 0; w < DC99_CHATS; w++) {
        dc99_chat_status(w, &chat_st[w]);
        if (chat_st[w].generation != chat_gen[w]) {
            int before = chat_n[w];
            chat_gen[w] = chat_st[w].generation;
            chat_n[w] = dc99_chat_copy(w, chat_rows[w], DC99_ROWS);
            if (chat_back[w] > 0 && chat_n[w] > before) {
                chat_back[w] += chat_n[w] - before; /* the view stays where it is while lines arrive */
            }
            if (chat_back[w] > chat_n[w]) {
                chat_back[w] = chat_n[w];
            }
        }
    }
    was_online = online;

    dcnow_fetch_poll(&pstat);
    if (pstat.generation != players_gen && pstat.list_valid) {
        players_gen = pstat.generation;
        nplayers = dcnow_fetch_copy(players, DCNOW_PLAYER_MAX);
        build_games();
        games_built = anim;
    }
    if (page <= PAGE_PLAY && anim - games_built >= 300) { /* the Pi's own account of the games changes without a new list */
        build_games();
        games_built = anim;
    }
    news_fetch_poll(&nstat);
    if (nstat.generation != news_gen) {
        news_gen = nstat.generation;
        nnews = news_fetch_copy(news, NEWS_MAX);
    }
    events_fetch_poll(&estat);
    if (estat.generation != events_gen) {
        events_gen = estat.generation;
        nevents = events_fetch_copy(events, EVENTS_MAX);
        build_events();
    } else if (anim - ev_built >= 600) { /* the clock moves on: going on / coming up / over */
        build_events();
    }
    ntest = net_test_lines(test_lines, NT_LINES);
    /* The gateway: asked about when the Settings page is open and the console is online (again every half minute, or every 15 seconds while
     * none answered), and every two seconds while it joins a Wi-Fi network */
    if (online && page == PAGE_SETTINGS && !ed.active) {
        picolink_status_t g;
        uint64_t now = timer_ms_gettime64();
        picolink_status(&g);
        gw = g;
        {
            int joining = !strcmp(gw.info.wifi_state, "connecting");
            uint64_t every = joining ? 2000u : gw.reached ? 30000u : 15000u;
            if (!gw.busy && now - last_gw_ask >= every) {
                last_gw_ask = now;
                picolink_probe();
            }
        }
    } else if (page == PAGE_SETTINGS) {
        picolink_status(&gw);
    }
    /* Auto-Refresh of the player list (the VMU screen refreshes on its own while it is on), as the main branch */
    if (online && sf_dcnow_vmu[0] != DCNOW_VMU_ON && pstat.state != DCNOW_FETCH_RUNNING && dcnow_refresh_seconds(sf_dcnow_refresh[0]) > 0
        && timer_ms_gettime64() - last_players_fetch >= (uint64_t)dcnow_refresh_seconds(sf_dcnow_refresh[0]) * 1000u) {
        last_players_fetch = timer_ms_gettime64();
        dcnow_fetch_start();
    }
    /* Events and news refresh on their own as well: every minute while their page is open (and a failed fetch is tried again then),
     * every ten minutes otherwise; not while an item is open for reading. */
    if (online && !detail) {
        uint64_t now = timer_ms_gettime64();
        uint64_t every = (page == PAGE_EVENTS ? 60u : 600u) * 1000u;
        if (estat.state != EVENTS_RUNNING && now - last_events_fetch >= every) {
            last_events_fetch = now;
            events_fetch_start();
        }
        if (nstat.state != NEWS_RUNNING && now - last_news_fetch >= every) {
            last_news_fetch = now;
            news_fetch_start();
        }
    }
    for (int l = 0; l < LISTS; l++) {
        clamp_list(l);
    }

    bvm_obj* back = bvm_find(&menu->vm, ID_BACK);
    if (back) {
        place(back, BACK_X, BACK_Y, BACK_Z);
    }
    for (int i = 0; i < PAGES; i++) {
        bvm_obj* o = bvm_find(&menu->vm, ID_BUTTON(i));
        if (o) {
            o->var[0] = i == cursor; /* lit, as the CD player's selected button */
        }
    }
}

/* ---- drawing --------------------------------------------------------------------------------------------------------- */

/* `text` cut to `chars` characters ("..." when cut) */
static void
text_cut(const char* text, int chars, float x, float y, uint32_t argb) {
    char line[96];
    int n = (int)strlen(text);
    if (chars > (int)sizeof(line) - 1) {
        chars = (int)sizeof(line) - 1;
    }
    if (n <= chars) {
        gfx_text(text, x, y, TEXT_Z, argb, 1);
        return;
    }
    snprintf(line, sizeof(line), "%.*s..", chars > 2 ? chars - 2 : 0, text);
    gfx_text(line, x, y, TEXT_Z, argb, 1);
}

static void
draw_status(void) {
    char line[64];
    dcnow_device_t used = dcnow_device_hint();
    switch (conn.state) {
        case DCNOW_CONN_ONLINE: {
            snprintf(line, sizeof(line), "Online%s%s  X: hang up", used != DCNOW_DEV_NONE ? " via " : "", used != DCNOW_DEV_NONE ? dcnow_device_short_name(used) : "");
            break;
        }
        case DCNOW_CONN_CONNECTING:
            snprintf(line, sizeof(line), "%s", conn.count > 0 ? conn.lines[conn.count - 1] : "Connecting...");
            break;
        case DCNOW_CONN_COOLDOWN: snprintf(line, sizeof(line), "Wait %d s before calling again", conn.cooldown_seconds); break;
        case DCNOW_CONN_FAILED:
            /* the engine's failure lines are "Connection failed.", the reason, and advice when there is any: show the reason */
            snprintf(line, sizeof(line), "%s  X: retry", conn.count > 1 ? conn.lines[1] : conn.count > 0 ? conn.lines[0] : "Connection failed");
            break;
        default:
            snprintf(line, sizeof(line), "%s", !probed ? "Looking for an adapter..." : "Not connected  X: connect (by the Connection setting)");
            break;
    }
    int chars = (int)strlen(line) < 46 ? (int)strlen(line) : 46;
    text_cut(line, 46, PANE_X + PANE_W - PAD - (float)(chars * CHAR_W), STATUS_Y, conn.state == DCNOW_CONN_ONLINE ? GREY : YELLOW);
}

/* What the player fetch is doing or why it failed (its last status line), for the empty lists: "Loading..." alone hides where
 * it stops (DNS, the connection to the server, the answer). */
static const char*
fetch_note(char* out, size_t size) {
    if (pstat.state == DCNOW_FETCH_FAILED && pstat.count > 0) {
        snprintf(out, size, "%s  A: retry", pstat.lines[pstat.count - 1]);
        return out;
    }
    if (pstat.state == DCNOW_FETCH_RUNNING && pstat.count > 0) {
        return pstat.lines[pstat.count - 1];
    }
    return "Loading...";
}

/* One row's highlight bar over `w` pixels from x */
static void
row_bar(float x, float w, float y) {
    gfx_rect(x - 8.0f, y - 2.0f, w + 8.0f, ROW_H, 0.45f, 0x40FFFFFFu);
}

#define STAR_W 22.0f /* the column of the star mark */

/* The Online page, players: who is online, the game they are in, the network. A starred player has a star. */
static void
draw_players(void) {
    char line[64];
    int online = conn.state == DCNOW_CONN_ONLINE;
    float x = PANE_X + PAD;
    if (pstat.list_valid) {
        snprintf(line, sizeof(line), "Players online: %d", pstat.online_total);
    } else {
        snprintf(line, sizeof(line), "Players online%s", pstat.state == DCNOW_FETCH_RUNNING ? ": loading" : pstat.state == DCNOW_FETCH_FAILED ? ": failed" : "");
    }
    gfx_text(line, x, HEAD_Y, TEXT_Z, WHITE, 1);

    if (nplayers == 0) {
        char note[DCNOW_STATUS_WIDTH + 16];
        text_cut(!online ? "Connect to see who is online" : pstat.list_valid ? "Nobody is online" : fetch_note(note, sizeof(note)), 44, x, LIST_Y, GREY);
    }
    for (int r = 0; r < ROWS && top[LIST_PLAYERS] + r < nplayers; r++) {
        int i = top[LIST_PLAYERS] + r;
        float y = LIST_Y + (float)r * ROW_H;
        int is_sel = i == sel[LIST_PLAYERS];
        if (is_sel) {
            row_bar(x, PANE_W - PAD * 2.0f - 12.0f, y);
        }
        if (np_player_starred(players[i].name)) {
            gfx_text("*", x, y, TEXT_Z, YELLOW, 1);
        }
        text_cut(players[i].name, 11, x + STAR_W, y, is_sel ? YELLOW : WHITE);
        text_cut(players[i].title[0] ? players[i].title : "in the lobby", 21, x + 145.0f + STAR_W, y, players[i].title[0] ? GREY : DIM);
        text_cut(players[i].network, 6, x + 410.0f, y, DIM);
    }
}

typedef const char* (*row_text_fn)(int index, char* out, size_t size, uint32_t* colour);

static void
draw_arrows(int l) {
    int n = list_rows(l);
    int vis = list_visible(l);
    float last = l == LIST_EVENTS || l == LIST_NEWS ? (float)(vis - 1) * ITEM_H : (float)(vis - 1) * ROW_H;
    if (top[l] > 0) {
        gfx_text("^", PANE_X + PANE_W - PAD - 11.0f, LIST_Y - 4.0f, TEXT_Z, GREY, 1);
    }
    if (top[l] + vis < n) {
        gfx_text("v", PANE_X + PANE_W - PAD - 11.0f, LIST_Y + last, TEXT_Z, GREY, 1);
    }
}

static void
draw_list(int l, row_text_fn row_text) {
    int n = list_rows(l);
    char line[96];
    for (int r = 0; r < ROWS && top[l] + r < n; r++) {
        int i = top[l] + r;
        float y = LIST_Y + (float)r * ROW_H;
        int is_sel = i == sel[l];
        uint32_t col = is_sel ? YELLOW : WHITE;
        const char* text = row_text(i, line, sizeof(line), &col);
        if (is_sel) {
            row_bar(PANE_X + PAD, PANE_W - PAD * 2.0f - 12.0f, y);
        }
        text_cut(text, 44, PANE_X + PAD, y, is_sel && col == WHITE ? YELLOW : col);
    }
    draw_arrows(l);
}

/* Online > Games: the games being played now, the player count first. A game that is not on the card cannot be started. */
static void
draw_ogames(void) {
    float x = PANE_X + PAD;
    char line[64];
    snprintf(line, sizeof(line), "Games being played: %d", nogames);
    gfx_text(line, x, HEAD_Y, TEXT_Z, WHITE, 1);
    if (nogames == 0) {
        text_cut(conn.state != DCNOW_CONN_ONLINE ? "Connect to see what is being played" : "Nobody is playing right now", 44, x, LIST_Y, GREY);
    }
    for (int r = 0; r < ROWS && top[LIST_OGAMES] + r < nogames; r++) {
        int i = top[LIST_OGAMES] + r;
        float y = LIST_Y + (float)r * ROW_H;
        int is_sel = i == sel[LIST_OGAMES];
        const ogame_row* g = &ogames[i];
        if (is_sel) {
            row_bar(x, PANE_W - PAD * 2.0f - 12.0f, y);
        }
        if (g->item && np_game_starred(item_hash(g->item))) {
            gfx_text("*", x, y, TEXT_Z, YELLOW, 1);
        }
        snprintf(line, sizeof(line), "%2d", g->players);
        gfx_text(line, x + STAR_W, y, TEXT_Z, YELLOW, 1);
        text_cut(g->title, 34, x + STAR_W + 34.0f, y, is_sel ? YELLOW : g->item ? WHITE : DIM);
        if (!g->item) {
            gfx_text("not on card", x + PANE_W - PAD * 2.0f - 11.0f * CHAR_W - 16.0f, y, TEXT_Z, DIM, 1);
        }
    }
    draw_arrows(LIST_OGAMES);
}

/* The Play page: the card's online games (the ones being played now highlighted), each with the network it will use, the browsers last. */
static void
draw_play(void) {
    float x = PANE_X + PAD;
    char line[64];
    int playing = 0;
    for (int i = 0; i < ngames; i++) {
        playing += games[i].players > 0;
    }
    snprintf(line, sizeof(line), "Online games: %d  being played: %d", ngames, playing);
    gfx_text(line, x, HEAD_Y, TEXT_Z, WHITE, 1);
    if (ngames == 0) {
        text_cut(have_meta ? "No online games on this card" : "No game information (META.DAT)", 44, x, LIST_Y, GREY);
    }
    for (int r = 0; r < ROWS && top[LIST_PLAY] + r < ngames; r++) {
        int i = top[LIST_PLAY] + r;
        float y = LIST_Y + (float)r * ROW_H;
        int is_sel = i == sel[LIST_PLAY];
        const game_row* g = &games[i];
        if (is_sel) {
            row_bar(x, PANE_W - PAD * 2.0f - 12.0f, y);
        }
        if (g->players > 0) {
            gfx_rect(x - 8.0f, y - 2.0f, PANE_W - PAD * 2.0f - 4.0f, ROW_H, 0.44f, 0x3040C040u); /* green tint: somebody plays it now */
            snprintf(line, sizeof(line), "%2d", g->players);
            gfx_text(line, x + STAR_W, y, TEXT_Z, YELLOW, 1);
        }
        if (!g->browser && np_game_starred(item_hash(g->item))) {
            gfx_text("*", x, y, TEXT_Z, YELLOW, 1);
        }
        text_cut(g->item->name, 34, x + STAR_W + 34.0f, y, is_sel ? YELLOW : g->players > 0 ? WHITE : g->browser ? GREY : 0xFFD0D0D0u);
        float tag_x = x + PANE_W - PAD * 2.0f - 7.0f * CHAR_W - 16.0f;
        if (g->browser) {
            gfx_text("browser", tag_x, y, TEXT_Z, DIM, 1);
        }
    }
    draw_arrows(LIST_PLAY);
}

/* Event and news items: two lines each, four to a page. */
static void
draw_items(int l, int n, const char* (*first)(int), const char* (*second)(int)) {
    float x = PANE_X + PAD;
    for (int r = 0; r < ITEM_ROWS && top[l] + r < n; r++) {
        int i = top[l] + r;
        float y = LIST_Y + (float)r * ITEM_H;
        int is_sel = i == sel[l];
        if (is_sel) {
            gfx_rect(x - 8.0f, y - 2.0f, PANE_W - PAD * 2.0f - 4.0f, ITEM_H - 4.0f, 0.45f, 0x40FFFFFFu);
        }
        text_cut(first(i), 44, x, y, is_sel ? YELLOW : WHITE);
        text_cut(second(i), 44, x, y + 22.0f, DIM);
    }
    draw_arrows(l);
}

static const char*
event_first(int i) {
    static char line[96];
    const event_item_t* e = &events[ev_row[i]];
    snprintf(line, sizeof(line), "%s%s%s", np_event_starred(e->title) ? "* " : "", ev_phase[i] == EVP_CURRENT ? "LIVE  " : "", e->title);
    return line;
}

static const char*
event_second(int i) {
    static char line[96];
    char rel[24];
    event_phase_text(ev_minutes[i], rel, sizeof(rel));
    snprintf(line, sizeof(line), "%s  %s  %s", rel, events[ev_row[i]].when, events[ev_row[i]].source);
    return line;
}

static const char*
news_first(int i) {
    return news[i].title;
}

static const char*
news_second(int i) {
    return news[i].when[0] ? news[i].when : news[i].body[0] ? news[i].body : news[i].source;
}

/* Text cut into lines of `width` characters at spaces. Returns the number of lines, writes the start/length of line `want`. */
static int
wrap_lines(const char* text, int width, int want, const char** start, int* len) {
    int lines = 0;
    const char* p = text;
    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        int n = (int)strlen(p);
        int cut = n <= width ? n : width;
        if (n > width) {
            while (cut > 0 && p[cut] != ' ') {
                cut--;
            }
            if (cut == 0) {
                cut = width;
            }
        }
        if (lines == want) {
            *start = p;
            *len = cut;
        }
        lines++;
        p += cut;
    }
    return lines;
}

/* An open event or news item: the heading in yellow, then the details; Up/Down scroll, B closes. */
static void
draw_detail(void) {
    char text[NEWS_BODY_LEN + NEWS_TITLE_LEN + 8];
    const char *title, *facts, *body;
    char fact_line[96];
    char rel[24];
    if (page == PAGE_EVENTS && view[page] == VIEW_A) {
        int i = sel[LIST_EVENTS];
        const event_item_t* e = &events[ev_row[i]];
        event_phase_text(ev_minutes[i], rel, sizeof(rel));
        snprintf(fact_line, sizeof(fact_line), "%s  %s  %s%s", e->when, e->source, rel, np_event_starred(e->title) ? "  *" : "");
        title = e->title;
        facts = fact_line;
        body = ev_phase[i] == EVP_CURRENT ? "Going on now (the sources give no end time: shown for three hours)." : "Y stars it. A starred event is shown in the top bar shortly before it starts.";
    } else {
        const news_item_t* n = &news[sel[LIST_NEWS]];
        snprintf(fact_line, sizeof(fact_line), "%s%s%s", n->when, n->when[0] ? "  " : "", n->source);
        title = n->title;
        facts = fact_line;
        body = n->body;
    }
    snprintf(text, sizeof(text), "%s", title);
    int tl = wrap_lines(text, 44, -1, NULL, NULL);
    /* lines: the title (wrapped), the facts, a gap, the body (wrapped) */
    int bl = wrap_lines(body, 44, -1, NULL, NULL);
    int total = tl + 2 + bl;
    int vis = ROWS;
    if (detail_top > total - vis) {
        detail_top = total - vis > 0 ? total - vis : 0;
    }
    for (int r = 0; r < vis; r++) {
        int li = detail_top + r;
        float y = LIST_Y + (float)r * ROW_H - 4.0f;
        const char* st = NULL;
        int len = 0;
        uint32_t col = WHITE;
        if (li < tl) {
            wrap_lines(text, 44, li, &st, &len);
            col = YELLOW;
        } else if (li == tl) {
            st = facts;
            len = (int)strlen(facts);
            col = DIM;
        } else if (li > tl + 1 && li - tl - 2 < bl) {
            wrap_lines(body, 44, li - tl - 2, &st, &len);
        }
        if (st && len > 0) {
            char line[48];
            snprintf(line, sizeof(line), "%.*s", len < 46 ? len : 46, st);
            gfx_text(line, PANE_X + PAD, y, TEXT_Z, col, 1);
        }
    }
    if (detail_top > 0) {
        gfx_text("^", PANE_X + PANE_W - PAD - 11.0f, LIST_Y - 4.0f, TEXT_Z, GREY, 1);
    }
    if (detail_top + vis < total) {
        gfx_text("v", PANE_X + PANE_W - PAD - 11.0f, LIST_Y + (float)(vis - 1) * ROW_H, TEXT_Z, GREY, 1);
    }
}

static const char*
settings_row(int i, char* out, size_t size, uint32_t* colour) {
    (void)colour;
    return setting_text(i, out, size);
}

static const char*
isp_row(int i, char* out, size_t size, uint32_t* colour) {
    return isp_row_text(i, out, size, colour);
}

/* The Wi-Fi view: the networks the gateway found, strongest first, with their signal and a lock. */
static void
draw_wifi(void) {
    float x = PANE_X + PAD;
    char line[96];
    int n = gw.info.scan_have;
    const char* state = gw.info.wifi_state;

    if (!strcmp(state, "connected")) {
        snprintf(line, sizeof(line), "Wi-Fi: %.20s  %s", gw.info.wifi_ssid, gw.info.wifi_ip);
    } else if (!strcmp(state, "connecting")) {
        snprintf(line, sizeof(line), "Wi-Fi: joining %.20s ...", gw.info.wifi_ssid);
    } else if (!strcmp(state, "failed")) {
        snprintf(line, sizeof(line), "Wi-Fi: %s", gw.info.wifi_error[0] ? gw.info.wifi_error : "could not join");
    } else {
        snprintf(line, sizeof(line), "Wi-Fi: %s", gw.busy ? "searching..." : "not set up");
    }
    text_cut(line, 44, x, HEAD_Y, !strcmp(state, "failed") ? RED : !strcmp(state, "connected") ? GREEN : WHITE);
    if (n == 0) {
        text_cut(gw.busy ? "Searching for networks..." : gw.note[0] ? gw.note : "No networks found  Y: search again", 44, x, LIST_Y, GREY);
    }
    for (int r = 0; r < ROWS && top[LIST_WIFI] + r < n; r++) {
        int i = top[LIST_WIFI] + r;
        float y = LIST_Y + (float)r * ROW_H;
        int is_sel = i == sel[LIST_WIFI];
        const pl_net_t* w = &gw.info.scan[i];
        float bx = PANE_X + PANE_W - PAD - 92.0f;
        if (is_sel) {
            row_bar(x, PANE_W - PAD * 2.0f - 12.0f, y);
        }
        text_cut(w->name, 30, x, y, is_sel ? YELLOW : WHITE);
        for (int b = 0; b < 4; b++) { /* the signal: four bars, the strong ones lit */
            float bh = 6.0f + (float)b * 4.0f;
            gfx_rect(bx + (float)b * 7.0f, y + 18.0f - bh, 5.0f, bh, 0.46f, b < pl_bars(w->rssi) ? (is_sel ? YELLOW : WHITE) : 0x40FFFFFFu);
        }
        gfx_text(w->open ? "open" : "lock", bx + 36.0f, y, TEXT_Z, w->open ? GREEN : DIM, 1);
    }
    draw_arrows(LIST_WIFI);
}

/* The network test: its lines, coloured by how it went. */
static void
draw_test(void) {
    float x = PANE_X + PAD;
    gfx_text(net_test_running() ? "Network test: running..." : "Network test", x, HEAD_Y, TEXT_Z, WHITE, 1);
    for (int r = 0; r < ROWS && top[LIST_TEST] + r < ntest; r++) {
        const nt_line_t* t = &test_lines[top[LIST_TEST] + r];
        text_cut(t->text, 44, x, LIST_Y + (float)r * ROW_H, t->level == NT_OK ? GREEN : t->level == NT_FAIL ? RED : t->level == NT_WARN ? YELLOW : GREY);
    }
    if (ntest == 0) {
        text_cut(net_test_running() ? "Looking..." : "A: run the test", 44, x, LIST_Y, GREY);
    }
    draw_arrows(LIST_TEST);
}

/* A chat, laid out like DCPlaya's: the two chats as tabs, a status line (dot, LIVE, the title, who you are), the messages as the
 * sender's name over the text, the newest at the bottom (up scrolls back), and an input bar with its button. */
#define CHAT_TABS_Y (PANE_Y + 6.0f)
#define CHAT_HEAD_Y (PANE_Y + 36.0f)
#define CHAT_LIST_Y (PANE_Y + 68.0f)
#define CHAT_LINE_H 25.0f
#define CHAT_LINES 7
#define CHAT_BAR_Y (PANE_Y + PANE_H - 36.0f) /* the chat fills the panel: no hint line under it */
#define CHAT_BAR_H 26.0f

/* Does the row show as a name line and a text line? Returns the length of the name, 0 for a one-line row. */
static int
chat_name_len(const dc99_row_t* r) {
    const char* c;
    if (r->kind != DC99_ROW_MSG && r->kind != DC99_ROW_SELF) {
        return 0;
    }
    c = strstr(r->text, ": ");
    return c && c - r->text > 0 && c - r->text <= 20 ? (int)(c - r->text) : 0;
}

static void
draw_chat(int w) {
    char line[96];
    const float x = PANE_X + PAD;
    const float right = PANE_X + PANE_W - PAD;
    const int online = conn.state == DCNOW_CONN_ONLINE;
    const dc99_chat_status_t* st = &chat_st[w];
    const int n = chat_n[w];
    const int login = np_chat_nick()[0] && np_chat_pass()[0];
    static const char* const tab_names[DC99_CHATS] = {"IRC #DC99", "KosNet Global"};
    static const float tab_w[DC99_CHATS] = {124.0f, 154.0f};
    float tx = x;

    for (int t = 0; t < DC99_CHATS; t++) { /* the tabs: the open one is lit */
        gfx_rect(tx, CHAT_TABS_Y, tab_w[t], 26.0f, 0.45f, t == w ? 0x60FFFFFFu : 0x20FFFFFFu);
        gfx_text(tab_names[t], tx + 8.0f, CHAT_TABS_Y + 1.0f, TEXT_Z, t == w ? YELLOW : GREY, 1);
        tx += tab_w[t] + 8.0f;
    }

    /* the status line */
    {
        const char* word = st->state == DC99_CHAT_ON ? "LIVE" : st->state == DC99_CHAT_CONNECTING ? "WAIT" : st->state == DC99_CHAT_FAILED ? "FAIL" : "OFF";
        uint32_t col = st->state == DC99_CHAT_ON ? GREEN : st->state == DC99_CHAT_CONNECTING ? YELLOW : st->state == DC99_CHAT_FAILED ? RED : DIM;
        const char* who = w == DC99_CHAT_KOSNET ? (kn_st.state == KOSNET_IN ? kn_st.name : "NOT SIGNED IN") : login ? np_chat_nick() : "NOT SIGNED IN";
        gfx_rect(x, CHAT_HEAD_Y + 9.0f, 9.0f, 9.0f, 0.45f, col);
        gfx_text(word, x + 16.0f, CHAT_HEAD_Y, TEXT_Z, col, 1);
        gfx_text(w == DC99_CHAT_IRC ? "DC99 chat" : "KosNet global chat", x + 16.0f + 5.0f * CHAR_W, CHAT_HEAD_Y, TEXT_Z, WHITE, 1);
        gfx_text(who, right - (float)(strlen(who) * CHAR_W), CHAT_HEAD_Y, TEXT_Z, DIM, 1);
        gfx_rect(x, CHAT_HEAD_Y + 26.0f, right - x, 1.0f, 0.45f, 0x50FFFFFFu);
    }

    if (n == 0) {
        const char* msg;
        if (!online) {
            msg = "Connect to chat";
        } else if (st->state == DC99_CHAT_FAILED) {
            msg = st->note[0] ? st->note : "The chat did not answer";
        } else if (w == DC99_CHAT_IRC && st->state != DC99_CHAT_CONNECTING) {
            msg = login ? "A: join the chat" : "A: set up your chat login";
        } else {
            msg = "Waiting for the chat...";
        }
        text_cut(msg, 44, x + 8.0f, CHAT_LIST_Y, GREY);
    } else {
        /* the rows that fit, from the newest shown backwards, then drawn from the top */
        int end = n - chat_back[w]; /* one past the last shown */
        int from = end, used = 0;
        float y;
        while (from > 0) {
            int need = chat_name_len(&chat_rows[w][from - 1]) ? 2 : 1;
            if (used + need > CHAT_LINES) {
                break;
            }
            used += need;
            from--;
        }
        y = CHAT_LIST_Y + (float)(CHAT_LINES - used) * CHAT_LINE_H;
        for (int i = from; i < end; i++) {
            const dc99_row_t* r = &chat_rows[w][i];
            int nl = chat_name_len(r);
            if (nl) {
                snprintf(line, sizeof(line), "%.*s", nl, r->text);
                gfx_text(line, x + 8.0f, y, TEXT_Z, r->kind == DC99_ROW_SELF ? GREEN : YELLOW, 1);
                y += CHAT_LINE_H;
                text_cut(r->text + nl + 2, 42, x + 8.0f + 2.0f * CHAR_W, y, WHITE);
            } else {
                text_cut(r->text, DC99_ROW_CHARS, x + 8.0f, y, r->kind == DC99_ROW_ERR ? RED : DIM);
            }
            y += CHAT_LINE_H;
        }
        if (chat_back[w] > 0) {
            gfx_text("v", right - 11.0f, CHAT_LIST_Y + (float)(CHAT_LINES - 1) * CHAT_LINE_H, TEXT_Z, GREY, 1);
        }
        if (from > 0) {
            gfx_text("^", right - 11.0f, CHAT_LIST_Y, TEXT_Z, GREY, 1);
        }
    }

    /* the input bar and its button */
    {
        const char* hint;
        const char* btn;
        uint32_t hcol = GREY;
        if (w == DC99_CHAT_KOSNET) {
            if (!kosnet_tls_available()) {
                hint = "Posting needs TLS: not in this build";
                btn = "NO TLS";
            } else if (kn_st.state == KOSNET_BUSY) {
                hint = kn_st.note[0] ? kn_st.note : "Working...";
                btn = "WAIT";
            } else if (kn_st.state == KOSNET_IN) {
                hint = "Say something to KosNet";
                btn = "SAY";
            } else {
                hint = "Sign in to KosNet to post";
                btn = "SIGN IN";
                hcol = YELLOW;
            }
        } else if (!login) {
            hint = "Sign in to IRC to post";
            btn = "SIGN IN";
            hcol = YELLOW;
        } else if (st->state != DC99_CHAT_ON) {
            hint = "Join #dc99 to post";
            btn = "JOIN";
        } else {
            hint = "Say something in #dc99";
            btn = "SAY";
        }
        {
            float bw = SAY_W;
            gfx_rect(x, CHAT_BAR_Y, right - x - bw - 8.0f, CHAT_BAR_H, 0.45f, 0x30FFFFFFu);
            const int dead = w == DC99_CHAT_KOSNET && (!kosnet_tls_available() || kn_st.state == KOSNET_BUSY);
            if (notice_frames > 0) { /* what just happened takes the bar's place for a moment */
                hint = notice;
                hcol = WHITE;
            }
            text_cut(hint, (int)((right - x - bw - 24.0f) / CHAR_W), x + 8.0f, CHAT_BAR_Y + 1.0f, hcol);
            say_on = 1;
            say_dead = dead;
            say_cy = CHAT_BAR_Y + CHAT_BAR_H / 2.0f;
            gfx_text(btn, right - bw / 2.0f - (float)gfx_text_width(btn) / 2.0f, CHAT_BAR_Y + 1.0f, TEXT_Z, dead ? DIM : WHITE, 1);
        }
    }
    if (st->state == DC99_CHAT_FAILED && st->note[0] && n > 0) {
        text_cut(st->note, 44, x + 8.0f, CHAT_HEAD_Y + 30.0f, RED);
    }
}

/* The on-screen keyboard: a popup with a coloured border, the text with its cursor, and a grid of keys. */
#define KB_X (PANE_X + 22.0f)
#define KB_Y (PANE_Y + 4.0f)
#define KB_W 492.0f
#define KB_H 292.0f
#define KB_CELL_W 42.0f
#define KB_CELL_H 26.0f
#define KB_GAP 4.0f
#define KB_BORDER 0xFFE8A030u
#define KB_FILL 0xFF101A38u
#define KB_KEY 0x50FFFFFFu
#define KB_KEY_SEL 0xFFB08040u
#define KB_KEY_OK 0x7030A050u
#define KB_SHOWN 38

static void
kb_label(const char* t, float x, float w, float y, uint32_t col) {
    gfx_text(t, x + (w - (float)(strlen(t) * CHAR_W)) / 2.0f, y, TEXT_Z, col, 1);
}

static void
draw_keyboard(const char* title) {
    const float gx = KB_X + 18.0f;
    const float gy = KB_Y + 76.0f;
    const osk_key_t cur = osk_key(&kb);
    const int hide = ed.kind == ED_KN_PASS || ed.kind == ED_CHAT_PASS;
    int from = ed.pos > KB_SHOWN - 2 ? ed.pos - (KB_SHOWN - 2) : 0; /* the text scrolls along with the cursor */

    const int grid = !input_keyboard_present(); /* a plugged-in keyboard types the text: only the field */
    const float top = grid ? KB_Y : KB_Y + 92.0f;
    const float h = grid ? KB_H : 118.0f;

    gfx_rect(KB_X, top, KB_W, h, 0.43f, KB_BORDER);
    gfx_rect(KB_X + 3.0f, top + 3.0f, KB_W - 6.0f, h - 6.0f, 0.44f, KB_FILL);
    gfx_text(title, KB_X + 14.0f, top + 6.0f, TEXT_Z, YELLOW, 1);

    /* the text */
    gfx_rect(gx, top + 36.0f, KB_W - 36.0f, 30.0f, 0.45f, 0xFF050810u);
    {
        char shown[KB_SHOWN + 1];
        char before[KB_SHOWN + 1];
        int n = 0;
        for (int i = from; i < ed.len && n < KB_SHOWN; i++) {
            shown[n++] = hide && i != ed.pos - 1 ? '*' : ed.text[i]; /* a password shows only the character just typed */
        }
        shown[n] = '\0';
        snprintf(before, sizeof(before), "%.*s", ed.pos - from, shown);
        gfx_text(shown, gx + 8.0f, top + 37.0f, TEXT_Z, WHITE, 1);
        gfx_rect(gx + 8.0f + (float)gfx_text_width(before), top + 40.0f, 2.0f, 22.0f, 0.46f, YELLOW);
    }

    if (!grid) {
        gfx_text("Enter: done   Esc: cancel", KB_X + 18.0f, top + h - 34.0f, TEXT_Z, DIM, 1);
        return;
    }

    /* the keys */
    for (int r = 0; r < OSK_ROWS; r++) {
        for (int c = 0; c < OSK_COLS;) {
            osk_key_t key = osk_key_at(kb.layer, r, c);
            float kx = gx + (float)key.col * (KB_CELL_W + KB_GAP);
            float ky = gy + (float)r * (KB_CELL_H + KB_GAP);
            float kw = (float)key.span * KB_CELL_W + (float)(key.span - 1) * KB_GAP;
            int sel = r == kb.row && key.col == cur.col;
            uint32_t col = WHITE;
            char t[2] = {key.ch, 0};

            if (key.kind == OSK_CHAR && !osk_char_ok(ed.set, key.ch)) {
                col = 0xFF606878u; /* this editor does not take it */
            }
            gfx_rect(kx, ky, kw, KB_CELL_H, 0.45f, sel ? KB_KEY_SEL : key.kind == OSK_OK ? KB_KEY_OK : KB_KEY);
            switch (key.kind) {
                case OSK_CHAR: kb_label(t, kx, kw, ky, col); break;
                case OSK_SHIFT: kb_label("SHIFT", kx, kw, ky, kb.layer == OSK_UPPER ? YELLOW : col); break;
                case OSK_SYM: kb_label(kb.layer == OSK_SYMBOLS ? "ABC" : "#+=", kx, kw, ky, kb.layer == OSK_SYMBOLS ? YELLOW : col); break;
                case OSK_SPACE: kb_label("SPACE", kx, kw, ky, col); break;
                case OSK_DEL: kb_label("DEL", kx, kw, ky, col); break;
                default: kb_label("OK", kx, kw, ky, GREEN); break;
            }
            c = key.col + key.span;
        }
    }
    gfx_text("A: key  X: space  Y: delete", KB_X + 18.0f, KB_Y + KB_H - 56.0f, TEXT_Z, DIM, 1);
    gfx_text("L/R: cursor  START: done  B: cancel", KB_X + 18.0f, KB_Y + KB_H - 32.0f, TEXT_Z, DIM, 1);
}

/* The editor over the panel: the value, the cursor, the keys. */
static void
draw_editor(void) {
    static const char* const names[] = {"IP address", "Netmask", "Gateway", "DNS server", "Dial number", "Login", "Password"};
    float x = PANE_X + PAD;
    char line[64];
    if (ed.kind == ED_KN_USER || ed.kind == ED_KN_PASS || ed.kind == ED_KN_MSG) {
        snprintf(line, sizeof(line), "%s", ed.kind == ED_KN_USER ? "KosNet username" : ed.kind == ED_KN_PASS ? "KosNet password" : "Say in the KosNet chat");
    } else if (ed.kind == ED_WIFI_PASS) {
        snprintf(line, sizeof(line), "Wi-Fi password for %.24s", gw.info.scan[wifi_pick].name);
    } else if (ed.kind != ED_ISP) {
        snprintf(line, sizeof(line), "%s", ed.kind == ED_CHAT_NICK ? "Chat nick (1-20 characters)"
                                           : ed.kind == ED_CHAT_PASS ? "Chat password (sent as typed)" : "Say in #dc99");
    } else {
        snprintf(line, sizeof(line), "Edit: %s", names[ed.row - ISP_IP]);
    }
    if (!ed.is_address) {
        draw_keyboard(line);
        return;
    }
    gfx_text(line, x, HEAD_Y, TEXT_Z, WHITE, 1);
    for (int i = 0; i < 4; i++) {
        float ox = x + 40.0f + (float)i * 110.0f;
        snprintf(line, sizeof(line), "%3d", ed.oct[i]);
        gfx_text(line, ox, LIST_Y + 20.0f, TEXT_Z, i == ed.octi ? YELLOW : WHITE, 1);
        if (i == ed.octi) {
            gfx_text("^^^", ox, LIST_Y + 46.0f, TEXT_Z, YELLOW, 1);
        }
        if (i < 3) {
            gfx_text(".", ox + 52.0f, LIST_Y + 20.0f, TEXT_Z, GREY, 1);
        }
    }
    gfx_text("Left/Right: part   Up/Down: +-1   X/Y: +-16", x, LIST_Y + 110.0f, TEXT_Z, DIM, 1);
    gfx_text("A: save   B: cancel", x, PANE_Y + PANE_H - 34.0f, TEXT_Z, DIM, 1);
}

static void
draw_page(void) {
    int online = conn.state == DCNOW_CONN_ONLINE;
    char line[64];
    if (ed.active) {
        draw_editor();
        return;
    }
    if (detail && ((page == PAGE_EVENTS && view[page] == VIEW_A && nev_rows > 0) || (page == PAGE_EVENTS && view[page] == VIEW_B && nnews > 0))) {
        draw_detail();
        return;
    }
    detail = 0;
    switch (page) {
        case PAGE_ONLINE:
            if (view[page] == VIEW_A) {
                draw_players();
            } else if (view[page] == VIEW_B) {
                draw_ogames();
            }
            break;
        case PAGE_CHAT: draw_chat(view[page] == VIEW_A ? DC99_CHAT_IRC : DC99_CHAT_KOSNET); break;
        case PAGE_PLAY: draw_play(); break;
        case PAGE_EVENTS:
            if (view[page] == VIEW_A) {
                int live = 0;
                for (int i = 0; i < nev_rows; i++) {
                    live += ev_phase[i] == EVP_CURRENT;
                }
                snprintf(line, sizeof(line), "Events: %d going on, %d coming up%s", live, nev_rows - live, nevents || estat.state != EVENTS_RUNNING ? "" : "  loading");
                gfx_text(line, PANE_X + PAD, HEAD_Y, TEXT_Z, WHITE, 1);
                if (nev_rows == 0) {
                    text_cut(!online ? "Connect to see the events" : estat.state == EVENTS_FAILED ? estat.line : "No events coming", 44, PANE_X + PAD, LIST_Y, GREY);
                } else {
                    draw_items(LIST_EVENTS, nev_rows, event_first, event_second);
                }
            } else {
                snprintf(line, sizeof(line), "News%s", nnews ? "" : nstat.state == NEWS_RUNNING ? ": loading" : "");
                gfx_text(line, PANE_X + PAD, HEAD_Y, TEXT_Z, WHITE, 1);
                if (nnews == 0) {
                    text_cut(!online ? "Connect to read the news" : nstat.state == NEWS_FAILED ? nstat.line : nstat.state == NEWS_RUNNING ? nstat.line : "No news yet  A: retry",
                             44, PANE_X + PAD, LIST_Y, GREY);
                } else {
                    draw_items(LIST_NEWS, nnews, news_first, news_second);
                }
            }
            break;
        default:
            if (view[page] == VIEW_A) {
                gfx_text("Network", PANE_X + PAD, HEAD_Y, TEXT_Z, WHITE, 1);
                draw_list(LIST_SETTINGS, settings_row);
            } else if (view[page] == VIEW_SET_ISP) {
                gfx_text("ISP settings", PANE_X + PAD, HEAD_Y, TEXT_Z, WHITE, 1);
                draw_list(LIST_ISP, isp_row);
            } else if (view[page] == VIEW_SET_WIFI) {
                draw_wifi();
            } else {
                draw_test();
            }
            break;
    }
}

static void
draw_footer(void) {
    const char* text = NULL;
    char line[96];
    if (ed.active || page == PAGE_CHAT) { /* the chat page has its own bar down to the panel's edge */
        return;
    }
    if (notice_frames > 0) {
        text = notice;
    } else if (conn.state == DCNOW_CONN_FAILED && conn.count > 2) {
        text = conn.lines[2]; /* the advice that goes with the reason */
    } else if (detail) {
        text = page == PAGE_EVENTS && view[page] == VIEW_A ? "Up/Down: scroll  Y: star  B: back" : "Up/Down: scroll  B: back";
    } else if (page == PAGE_ONLINE && view[page] == VIEW_A) {
        snprintf(line, sizeof(line), "A: refresh  Y: star  START: games  X: %s", conn.state == DCNOW_CONN_ONLINE ? "hang up" : "connect");
        text = line;
    } else if (page == PAGE_ONLINE && view[page] == VIEW_B) {
        text = "A: join or start  Y: star  START: players";
    } else if (page == PAGE_CHAT && view[page] == VIEW_A) {
        text = chat_st[DC99_CHAT_IRC].state == DC99_CHAT_ON ? "A: say  Y: login  START: KosNet  Up: back" : "A: join  Y: login  START: KosNet";
    } else if (page == PAGE_CHAT) {
        text = "Up/Down: scroll back  START: IRC";
    } else if (page == PAGE_PLAY && sel[LIST_PLAY] < ngames) {
        const game_row* g = &games[sel[LIST_PLAY]];
        struct db_item* meta = NULL;
        if (g->browser) {
            text = "A: start the browser";
        } else if (have_meta && !db_get_meta(g->item->product, &meta) && meta && (meta->network & 3)) { /* what META.DAT says about its connections */
            static const char* const how[] = {"", "modem", "broadband adapter", "modem or broadband adapter"};
            snprintf(line, sizeof(line), "Connects by: %s", how[meta->network & 3]);
            text = line;
        } else {
            text = "A: start  Y: star";
        }
    } else if (page == PAGE_EVENTS) {
        text = view[page] == VIEW_A ? (nev_rows > 0 ? "A: open  Y: star  START: news" : "START: news") : (nnews > 0 ? "A: read  START: events" : "START: events");
    } else if (page == PAGE_SETTINGS) {
        text = view[page] == VIEW_A ? "A: change or open" : view[page] == VIEW_SET_ISP ? "A: change  B: back" : view[page] == VIEW_SET_WIFI ? "A: join  Y: search again  B: back" : "A: run again  B: back";
    }
    if (text) {
        text_cut(text, 44, PANE_X + PAD, PANE_Y + PANE_H - 34.0f, DIM);
    }
}

/* The glyph of a page button, drawn once: black when selected like the BIOS marks, dark navy when not. */
static int
page_icon(int i, int lit) {
    static int ids[2][PAGES];
    static int made;
    if (!made) {
        for (int k = 0; k < PAGES; k++) {
            uint16_t px[UICON_SIZE * UICON_SIZE];
            for (int l = 0; l < 2; l++) {
                uicon_render(k, l ? 0xFF000000u : 0xFF1C2850u, px);
                ids[l][k] = gfx_dyn_create(px, UICON_SIZE, UICON_SIZE);
            }
        }
        made = 1;
    }
    return ids[lit][i];
}

void
uin_draw(void) {
    const bscene_sink* sink = gfx_sink();
    gfx_pane(PANE_X, PANE_Y, PANE_W, PANE_H, PANE_Z, BSCENE_PANE_ARGB);
    draw_status();
    say_on = 0;
    draw_page();
    draw_footer();
    {
        bvm_obj* so = bvm_find(&menu->vm, ID_SAY);
        if (so) {
            if (say_on && !ed.active) {
                const float px = 11.9f, z = -335.9375f + 0.39f; /* pixels per unit and depth of the clock screen's buttons */
                place(so, (PANE_X + PANE_W - PAD - SAY_W / 2.0f - 320.0f) / px, (240.0f - say_cy) / px, z);
                so->scale_tw[0].cur = 2.6f; /* the clock screen's oval, wider for the word */
                so->scale_tw[1].cur = 1.25f;
                so->scale_tw[2].cur = 1.0f;
                so->var[1] = !say_dead; /* green when it can be used, as the selected button of the clock screen */
                so->flags &= ~(uint32_t)BVM_F_HIDE;
            } else {
                so->flags |= BVM_F_HIDE;
            }
        }
    }

    /* BACK and the buttons: BACK lit with its frame blinking while it is selected, the buttons without their pictures */
    bmenu_back_style(menu, cursor == CUR_BACK, (int)anim, console_pal);
    menu->scene.parts = BSCENE_PART_ALL;
    for (int i = 0; i < menu->vm.count; i++) {
        const bvm_obj* o = &menu->vm.objs[menu->vm.order[i]];
        menu->scene.no_decals = o->id >= ID_BUTTON(0) && o->id < ID_BUTTON(PAGES);
        bscene_draw_object(&menu->scene, o, sink);
    }
    menu->scene.no_decals = 0;
    menu->scene.ovr_n = 0;
    for (int i = 0; i < PAGES; i++) {
        float x, y;
        blist_button_center_px(i, &x, &y);
        gfx_image(page_icon(i, i == page), x - (float)UICON_DRAW / 2.0f, y - (float)UICON_DRAW / 2.0f, (float)UICON_DRAW, (float)UICON_DRAW,
                  TEXT_Z);
    }
}
