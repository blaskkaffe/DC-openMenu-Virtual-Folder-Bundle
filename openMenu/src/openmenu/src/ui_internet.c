/* ui_internet: see ui_internet.h. The connection, the player list and the online games are the main branch's engine
 * (backend/dcnow_*, dcnet_parse, online_games); the events are backend/events_fetch, the news backend/news_fetch, the way to the network
 * backend/netsel + dcnow_net, the saved settings and stars netprefs.
 * The Online page also holds the two DC99 chats (backend/dc99_chat). Four pages on the CD player's buttons: Online (who is online / the games being played), Play (the card's online games and the
 * browsers), Events (going on and coming up / the news), Settings (connection, ISP settings, network test). The connection status is
 * drawn above the panel. A acts, Y stars, START switches the list of a page; connecting and hanging up is the Connection row of Settings. */
#include <ctype.h>
#include <math.h>
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
#include <bios_page.h>
#include <openmenu_settings.h>

#include "backend/dc99_chat.h"
#include "backend/dcnow_fetch.h"
#include "backend/dcnow_net.h"
#include "backend/events_fetch.h"
#include "backend/kosnet_post.h"
#include "backend/lb_fetch.h"
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
#define ID_KEY(i) ((uint16_t)(0x1200 + (i))) /* the keyboard's keys: the same oval as the Say button */
#define KEYS_MAX 48
#define ID_SAY 0x1120 /* the oval of the chat bar: script 0x3f, the Select button of the BIOS clock screen */
#define ID_PANEL 0x1140 /* the popups' panel: script 5, the popup of the BIOS file (VMU) screen */
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

/* A popup in the middle of the screen, as the keyboard's: coloured border, dark fill. The lists draw inside it. */
#define POP_W 520.0f
#define POP_H 330.0f

/* Where the buttons of the popup on the screen are (set when they are drawn): what a click on one does is what its button does. */
static struct {
    float x0, x1, y0, y1;
    button_t b;
} pbtn[4];
static int npbtn;

#define ITEM_H 46.0f /* events and news: two lines per item */
#define ITEM_ROWS 4
#define POPUP_ROWS 10 /* rows of a list in a popup */
#define POPUP_ROW_H 24.0f
#define STATUS_Y 10.0f /* the connection status, above the panel */
#define CHAR_W 11 /* the BIOS font's advance */

enum { PAGE_ONLINE, PAGE_PLAY, PAGE_EVENTS, PAGE_CHAT, PAGE_SETTINGS };
static const char* const page_names[PAGES] = {"Online", "Play", "Events", "Chat", "Settings"};
/* A page can show more than one list: START switches (Online: players / games, Events: events / news) */
enum { VIEW_A = 0, VIEW_B = 1, VIEW_SET_ISP = 1, VIEW_SET_TEST = 2, VIEW_SET_WIFI = 3 }; /* Chat: A = the DC99 IRC chat, B = the KosNet chat */

/* The lists behind the pages */
enum { LIST_PLAYERS, LIST_OGAMES, LIST_PLAY, LIST_EVENTS, LIST_NEWS, LIST_SETTINGS, LIST_ISP, LIST_TEST, LIST_WIFI, LIST_ONLINE, LIST_BOARD, LISTS };
/* The Online page is four rows like the BIOS settings; A opens a popup with the content (view[PAGE_ONLINE]: 0 = the rows, else the row + 1) */
enum { ON_MENU = 0, ON_PLAYERS, ON_GAMES, ON_EVENT, ON_BOARD, ON_ROWS = 4 };
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

static bpage orows; /* the Online page's four rows: the BIOS Settings screen's own bars, pills and 3D icons */
static int say_on, say_dead; /* the chat bar has its oval button this frame, and it cannot be used now */
static float say_cx, say_cy;
static void key_oval(float cx, float cy, float sx, float sy, int lit, uint32_t col);
#define BTN_OVAL_PX 60.0f /* the width of the oval at scale 1, in pixels (measured from the keyboard) */
static uint32_t key_col[48]; /* a forced colour of the oval in slot i (0: its own green) */
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
static lb_status_t lbst; /* the leaderboard being read */
static int lb_gen = -1;
static lb_line_t lb_lines[LB_LINES_MAX];
static int nlb;
static int lb_open = -1; /* the board shown (an index of lb_source), -1: the list of boards */
static int lb_top;      /* its first line shown */
static void lb_layout(void);
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
int
uin_is_browser(const char* name) {
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
        if (uin_is_browser(g->name)) {
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
        if (n > 0 || (have_meta && !db_get_meta(g->product, &meta) && meta && db_item_connections(meta))) {
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
        case PAGE_ONLINE: return view[p] == ON_MENU ? LIST_ONLINE : view[p] == ON_PLAYERS ? LIST_PLAYERS : view[p] == ON_GAMES ? LIST_OGAMES : view[p] == ON_BOARD ? LIST_BOARD : -1;
        case PAGE_CHAT: return -1; /* the chats scroll on their own */
        case PAGE_PLAY: return LIST_PLAY;
        case PAGE_EVENTS: return view[p] == VIEW_A ? LIST_EVENTS : LIST_NEWS;
        case PAGE_SETTINGS: return view[p] == VIEW_A ? LIST_SETTINGS : view[p] == VIEW_SET_ISP ? LIST_ISP : view[p] == VIEW_SET_WIFI ? LIST_WIFI : LIST_TEST;
        default: return -1;
    }
}

/* The browsers on the card: the way to a game's online leaderboard */
static int
board_count(void) {
    int n = 0;
    for (int i = 0; i < ngames; i++) {
        n += games[i].browser;
    }
    return n;
}

static const gd_item*
board_item(int k) {
    for (int i = 0; i < ngames; i++) {
        if (games[i].browser && k-- == 0) {
            return games[i].item;
        }
    }
    return NULL;
}

static int
list_rows(int l) {
    switch (l) {
        case LIST_PLAYERS: return nplayers;
        case LIST_OGAMES: return nogames;
        case LIST_ONLINE: return ON_ROWS;
        case LIST_BOARD: return lb_source_count() + board_count();
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
    return l == LIST_EVENTS || l == LIST_NEWS ? ITEM_ROWS : l == LIST_PLAYERS || l == LIST_OGAMES || l == LIST_BOARD ? POPUP_ROWS : ROWS;
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
    bvm_create(&m->vm, 5, ID_PANEL, PRIO);
    for (int i = 0; i < KEYS_MAX; i++) {
        bvm_create(&m->vm, 0x3f, ID_KEY(i), PRIO);
    }
    for (int i = 0; i < PAGES; i++) {
        bvm_create(&m->vm, 0x17 + (i == 1 ? 2 : (i == 2 ? 1 : i)), ID_BUTTON(i), PRIO); /* left to right, as bios_list */
    }
    bpage_open_rows(&orows, m, 1); /* with the buttons of this screen; its BACK marker stays */
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
                say("Not connected: Settings, Connection");
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
    if (page == PAGE_ONLINE && view[page] == ON_PLAYERS) {
        if (sel[LIST_PLAYERS] >= nplayers) {
            return 0;
        }
        r = np_player_toggle(players[sel[LIST_PLAYERS]].name);
    } else if (page == PAGE_ONLINE) {
        if (view[page] != ON_GAMES || sel[LIST_OGAMES] >= nogames || !ogames[sel[LIST_OGAMES]].item) {
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
    lb_fetch_abort();
    dc99_chat_stop();
    kosnet_post_stop();
    picolink_stop();
}

/* A in the IRC view: sets the login up, connects, or opens the line to say. */
static void
kosnet_activate(int online) {
    if (!online) {
        sound_sfx(BAUDIO_SFX_ERROR);
        say("Not connected: Settings, Connection");
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
        say("Not connected: Settings, Connection");
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
                view[PAGE_ONLINE] = ON_MENU;
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
                view[PAGE_ONLINE] = ON_MENU;
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
            if (page == PAGE_ONLINE && view[page] == ON_BOARD && lb_open >= 0) { /* a board: scroll its lines */
                int step = b == BTN_UP ? -1 : b == BTN_DOWN ? 1 : b == BTN_PAGE_UP ? -POPUP_ROWS : POPUP_ROWS;
                int most = nlb > POPUP_ROWS ? nlb - POPUP_ROWS : 0;
                lb_top = lb_top + step < 0 ? 0 : lb_top + step > most ? most : lb_top + step;
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
                    if (view[page] == ON_MENU) { /* a row: its popup */
                        view[page] = sel[LIST_ONLINE] + 1;
                        lb_open = -1;
                        sound_sfx(BAUDIO_SFX_ENTER);
                    } else if (view[page] == ON_BOARD) { /* open a board, read it again, or start a browser */
                        int row = sel[LIST_BOARD];
                        if (lb_open >= 0) {
                            lb_fetch_start(lb_open);
                            sound_sfx(BAUDIO_SFX_CONFIRM);
                        } else if (row < lb_source_count()) {
                            if (!online) {
                                sound_sfx(BAUDIO_SFX_ERROR);
                                say("Not connected: Settings, Connection");
                            } else {
                                lb_open = row;
                                lb_top = 0;
                                nlb = 0;
                                lb_fetch_start(row);
                                sound_sfx(BAUDIO_SFX_ENTER);
                            }
                        } else {
                            launch_row(board_item(row - lb_source_count()));
                        }
                    } else if (view[page] == ON_EVENT) {
                        sound_sfx(BAUDIO_SFX_CANCEL);
                        view[page] = ON_MENU;
                    } else if (view[page] == ON_PLAYERS) { /* the player list: refresh it */
                        if (online) {
                            sound_sfx(BAUDIO_SFX_CONFIRM);
                            start_fetches();
                        } else {
                            sound_sfx(BAUDIO_SFX_ERROR);
                            say("Not connected: Settings, Connection");
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
                        say("Not connected: Settings, Connection");
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
            if (page == PAGE_EVENTS || page == PAGE_CHAT) { /* events / news, IRC / KosNet */
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
        case BTN_B:
            sound_sfx(BAUDIO_SFX_CANCEL);
            if (detail) {
                detail = 0;
                break;
            }
            if (page == PAGE_ONLINE && view[page] == ON_BOARD && lb_open >= 0) { /* a board back to the list of boards */
                lb_open = -1;
                lb_fetch_abort();
                break;
            }
            if (page == PAGE_ONLINE && view[page] != ON_MENU) { /* a popup closes */
                view[page] = ON_MENU;
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
                lb_fetch_abort();
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
        view[PAGE_ONLINE] = ON_MENU;
        sound_sfx(BAUDIO_SFX_CURSOR);
        cursor = page = bt;
        return;
    }
    if (detail) {
        return;
    }
    if (page == PAGE_ONLINE && view[PAGE_ONLINE] == ON_MENU) { /* the four rows: the one under the pointer is the selected one */
        for (int r = 0; r < ON_ROWS; r++) {
            if (x >= 36.0f && x <= 624.0f && y >= bpage_rows_y_px(r) - 34.0f && y < bpage_rows_y_px(r) + 34.0f) {
                if (sel[LIST_ONLINE] != r || cursor < 0) {
                    sel[LIST_ONLINE] = r;
                    cursor = page; /* off BACK */
                    sound_sfx(BAUDIO_SFX_CURSOR);
                }
                return;
            }
        }
        return;
    }
    const int l = page_list(page);
    if (l < 0 || (page == PAGE_ONLINE && lb_open >= 0)) {
        return;
    }
    /* a list: the row under the pointer (the popups' lists and the lists of the other pages, which sit in their own panes) */
    const int popup = page == PAGE_ONLINE;
    const float lx = popup ? 320.0f - POP_W / 2.0f + 8.0f : PANE_X, rx = popup ? 320.0f + POP_W / 2.0f - 8.0f : PANE_X + PANE_W;
    const float y0 = popup ? 240.0f - POP_H / 2.0f + 40.0f : LIST_Y, h = popup ? POPUP_ROW_H : ROW_H;
    if (l == LIST_EVENTS || l == LIST_NEWS || x < lx || x > rx || y < y0 - 2.0f) {
        return;
    }
    int row = (int)((y - y0 + 2.0f) / h);
    if (row >= list_visible(l) || top[l] + row >= list_rows(l)) {
        return;
    }
    if (sel[l] != top[l] + row || cursor < 0) {
        sel[l] = top[l] + row;
        cursor = page; /* off BACK */
        sound_sfx(BAUDIO_SFX_CURSOR);
    }
}

/* A click: on the button of a popup it is what that button does (A, Y or B), anywhere else it is A. */
button_t
uin_click(float x, float y, button_t b) {
    if (ed.active || b != BTN_A) {
        return b;
    }
    for (int i = 0; i < npbtn; i++) {
        if (x >= pbtn[i].x0 && x <= pbtn[i].x1 && y >= pbtn[i].y0 && y <= pbtn[i].y1) {
            return pbtn[i].b;
        }
    }
    return b;
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

/* The icon of an Online row: the same models as the main menu's (controller, clock, globe) and a person, at the BIOS icons' size */
static void
online_icon(void* user, int index, bpage_row* out) {
    static const int icon[ON_ROWS] = {BPAGE_ICON_GLOBE, BPAGE_ICON_MAIN_PAD, BPAGE_ICON_MAIN_CLOCK, BPAGE_ICON_PODIUM};
    (void)user;
    out->icon = icon[index < 0 || index >= ON_ROWS ? 0 : index];
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
         * for X, since a dial takes the phone line and the others probe the serial port. Only once per start: after a hang-up the Connection row connects again. */
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
    lb_fetch_poll(&lbst);
    if (lbst.generation != lb_gen) {
        lb_gen = lbst.generation;
        nlb = lb_fetch_copy(lb_lines, LB_LINES_MAX);
        lb_layout();
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
    bpage_rows_show(&orows, page == PAGE_ONLINE && view[PAGE_ONLINE] == ON_MENU && !ed.active);
    orows.cursor = sel[LIST_ONLINE];
    bpage_sync(&orows, online_icon, NULL);
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
    switch (conn.state) {
        case DCNOW_CONN_ONLINE: return; /* nothing to say while all is well: Settings has the Connection row to hang up */
        case DCNOW_CONN_CONNECTING:
            snprintf(line, sizeof(line), "%s", conn.count > 0 ? conn.lines[conn.count - 1] : "Connecting...");
            break;
        case DCNOW_CONN_COOLDOWN: snprintf(line, sizeof(line), "Wait %d s before calling again", conn.cooldown_seconds); break;
        case DCNOW_CONN_FAILED:
            /* the engine's failure lines are "Connection failed.", the reason, and advice when there is any: show the reason */
            snprintf(line, sizeof(line), "%s  (Settings: Connection)", conn.count > 1 ? conn.lines[1] : conn.count > 0 ? conn.lines[0] : "Connection failed");
            break;
        default:
            snprintf(line, sizeof(line), "%s", !probed ? "Looking for an adapter..." : "Not connected  (Settings: Connection)");
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

static int lay_rows = ROWS;
static float lay_row_h = ROW_H;

/* One row's highlight bar over `w` pixels from x */
static void
row_bar(float x, float w, float y) {
    gfx_rect(x - 8.0f, y - 2.0f, w + 8.0f, lay_row_h, 0.45f, 0x40FFFFFFu);
}

#define STAR_W 22.0f /* the column of the star mark */

/* Where the lists draw: in the panel, or in a popup (draw_popup_frame moves them) */
static float lay_x = PANE_X + PAD, lay_head = HEAD_Y, lay_list = LIST_Y, lay_right = PANE_X + PANE_W - PAD, lay_foot = PANE_Y + PANE_H - 34.0f;
#define LAY_W (lay_right - lay_x - 12.0f)

/* The Online page, players: who is online, the game they are in, the network. A starred player has a star. */
static void
draw_players(void) {
    char line[64];
    int online = conn.state == DCNOW_CONN_ONLINE;
    float x = lay_x;
    if (pstat.list_valid) {
        snprintf(line, sizeof(line), "Players online: %d", pstat.online_total);
    } else {
        snprintf(line, sizeof(line), "Players online%s", pstat.state == DCNOW_FETCH_RUNNING ? ": loading" : pstat.state == DCNOW_FETCH_FAILED ? ": failed" : "");
    }
    gfx_text(line, x, lay_head, TEXT_Z, WHITE, 1);

    if (nplayers == 0) {
        char note[DCNOW_STATUS_WIDTH + 16];
        text_cut(!online ? "Connect to see who is online" : pstat.list_valid ? "Nobody is online" : fetch_note(note, sizeof(note)), 44, x, lay_list, GREY);
    }
    for (int r = 0; r < lay_rows && top[LIST_PLAYERS] + r < nplayers; r++) {
        int i = top[LIST_PLAYERS] + r;
        float y = lay_list + (float)r * lay_row_h;
        int is_sel = i == sel[LIST_PLAYERS];
        if (is_sel) {
            row_bar(x, LAY_W, y);
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
    float last = l == LIST_EVENTS || l == LIST_NEWS ? (float)(vis - 1) * ITEM_H : (float)(vis - 1) * lay_row_h;
    if (top[l] > 0) {
        gfx_text("^", lay_right - 11.0f, lay_list - 4.0f, TEXT_Z, GREY, 1);
    }
    if (top[l] + vis < n) {
        gfx_text("v", lay_right - 11.0f, lay_list + last, TEXT_Z, GREY, 1);
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
    float x = lay_x;
    char line[64];
    snprintf(line, sizeof(line), "Games being played: %d", nogames);
    gfx_text(line, x, lay_head, TEXT_Z, WHITE, 1);
    if (nogames == 0) {
        text_cut(conn.state != DCNOW_CONN_ONLINE ? "Connect to see what is being played" : "Nobody is playing right now", 44, x, lay_list, GREY);
    }
    for (int r = 0; r < lay_rows && top[LIST_OGAMES] + r < nogames; r++) {
        int i = top[LIST_OGAMES] + r;
        float y = lay_list + (float)r * lay_row_h;
        int is_sel = i == sel[LIST_OGAMES];
        const ogame_row* g = &ogames[i];
        if (is_sel) {
            row_bar(x, LAY_W, y);
        }
        if (g->item && np_game_starred(item_hash(g->item))) {
            gfx_text("*", x, y, TEXT_Z, YELLOW, 1);
        }
        snprintf(line, sizeof(line), "%2d", g->players);
        gfx_text(line, x + STAR_W, y, TEXT_Z, YELLOW, 1);
        text_cut(g->title, 34, x + STAR_W + 34.0f, y, is_sel ? YELLOW : g->item ? WHITE : DIM);
        if (!g->item) {
            gfx_text("not on card", lay_right - 11.0f * CHAR_W - 4.0f, y, TEXT_Z, DIM, 1);
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
            float bw = (float)gfx_text_width(btn) + 44.0f; /* the word, and the oval under its first letter */
            gfx_rect(x, CHAT_BAR_Y, right - x - bw - 8.0f, CHAT_BAR_H, 0.45f, 0x30FFFFFFu);
            const int dead = w == DC99_CHAT_KOSNET && (!kosnet_tls_available() || kn_st.state == KOSNET_BUSY);
            if (notice_frames > 0) { /* what just happened takes the bar's place for a moment */
                hint = notice;
                hcol = WHITE;
            }
            text_cut(hint, (int)((right - x - bw - 24.0f) / CHAR_W), x + 8.0f, CHAT_BAR_Y + 1.0f, hcol);
            say_on = 0; /* the button is a steady 3D oval in the A button's red (grey when it cannot be used): no blinking green one */
            say_dead = dead;
            key_oval(right - (float)gfx_text_width(btn) - 9.0f, CHAT_BAR_Y + 15.0f, 0.68f, 0.68f, 1, dead ? 0xF0606878u : 0xF0E03C3Cu); /* as the BIOS: 9 px left of the word */
            gfx_text(btn, right - (float)gfx_text_width(btn), CHAT_BAR_Y + 1.0f, TEXT_Z, dead ? DIM : WHITE, 1);
        }
    }
    if (st->state == DC99_CHAT_FAILED && st->note[0] && n > 0) {
        text_cut(st->note, 44, x + 8.0f, CHAT_HEAD_Y + 30.0f, RED);
    }
}

/* The on-screen keyboard: a popup with a coloured border, the text with its cursor, and a grid of keys. */
#define KB_W 520.0f
#define KB_H 290.0f
#define KB_X (320.0f - KB_W / 2.0f) /* popups are in the middle of the screen */
#define KB_Y (240.0f - KB_H / 2.0f)
#define KB_CELL_W 38.0f
#define KB_CELL_H 26.0f
#define KB_GAP 4.0f /* the keys are 42 apart across and 37 down: the same space between the ovals both ways */
#define KB_ROW_PITCH 37.0f
#define KB_KEY 0x7030804Cu
#define KB_KEY_SEL 0xFF48C860u
#define KB_KEY_OK 0xB02E9A4Au
#define KB_SHOWN 38

/* A key: the Say button's oval (script 0x3f of the BIOS clock screen, 45 x 38 px at scale 1) placed under the key, lit when selected. */
#define OVAL_W 45.5f
static int kb_slot;

/* The popup panel: the BIOS file screen's (script 5), in the middle of the screen as that screen puts its popups, see through
 * the same way. Its size in var 0 and 1 comes out 0.978 times as many pixels (a 460 x 212 popup is 450 x 207 on the screen). */
#define PANEL_Z (-85000.0f / 256.0f)
#define PANEL_PX 0.978f
static int panel_on;
static float panel_w, panel_h;
static void
popup_panel(float w, float h) {
    panel_on = 1;
    panel_w = w;
    panel_h = h;
}
/* A flat button, cheap to draw: the ends and the middle of a grey, glossy pill as three small pictures, multiplied by a colour when they are drawn
 * (one rectangle each, where a 3D oval is hundreds of triangles). The selected key of the keyboard is the one 3D oval. */
enum { PILL_LEFT, PILL_MID, PILL_RIGHT, PILL_PARTS };
#define PILL_TEX 32 /* the pictures are 32 tall (a circle of 16 at each end) */
#define PILL_H 26.0f
#define PILL_CAP 13.0f
static int pill_id[PILL_PARTS] = {-1, -1, -1};

static unsigned short
pill_texel(int part, int x, int y) {
    const float r = PILL_TEX / 2.0f;
    const float dy = (float)y + 0.5f - r;
    float cov = r - fabsf(dy) + 0.5f; /* the straight middle */
    if (part != PILL_MID) {
        /* the round end: a circle centred on the picture's right edge (left end) or left edge (right end) */
        const float dx = (part == PILL_LEFT) ? (float)x + 0.5f - r : (float)x + 0.5f;
        cov = r - sqrtf(dx * dx + dy * dy) + 0.5f;
    }
    cov = cov < 0.0f ? 0.0f : cov > 1.0f ? 1.0f : cov;
    const float t = ((float)y + 0.5f) / (float)PILL_TEX; /* the gloss: light at the top, a bright band, darker toward the bottom */
    float g = t < 0.22f ? 0.84f + 0.16f * (t / 0.22f) : t < 0.42f ? 1.0f - 0.16f * ((t - 0.22f) / 0.2f) : 0.84f - 0.34f * ((t - 0.42f) / 0.58f);
    unsigned al = (unsigned)(cov * 15.0f + 0.5f), v = (unsigned)(g * 15.0f + 0.5f);
    return (unsigned short)((al << 12) | (v << 8) | (v << 4) | v);
}

static int
pill_ready(void) {
    int ok = 1;
    for (int i = 0; i < PILL_PARTS; i++) {
        ok = ok && gfx_dyn_valid(pill_id[i]);
    }
    if (ok) {
        return 1;
    }
    static const int width[PILL_PARTS] = {16, 8, 16};
    for (int i = 0; i < PILL_PARTS; i++) {
        gfx_dyn_free(pill_id[i]); /* a half made set (or one freed with the other pictures) starts again */
        unsigned short px[16 * PILL_TEX];
        for (int y = 0; y < PILL_TEX; y++) {
            for (int x = 0; x < width[i]; x++) {
                px[y * width[i] + x] = pill_texel(i, x, y);
            }
        }
        pill_id[i] = gfx_dyn_create(px, width[i], PILL_TEX);
        if (pill_id[i] < 0) {
            return 0;
        }
    }
    return 1;
}

/* A pill from x, y, w wide (at least a circle), in `argb` (the grey of the pictures times it). */
static void __attribute__((unused))
pill(float x, float y, float w, float z, uint32_t argb) {
    if (!pill_ready()) {
        return;
    }
    if (w < 2.0f * PILL_CAP) {
        w = 2.0f * PILL_CAP;
    }
    gfx_image_tint(pill_id[PILL_LEFT], x, y, PILL_CAP, PILL_H, z, argb);
    gfx_image_tint(pill_id[PILL_MID], x + PILL_CAP, y, w - 2.0f * PILL_CAP, PILL_H, z, argb);
    gfx_image_tint(pill_id[PILL_RIGHT], x + w - PILL_CAP, y, PILL_CAP, PILL_H, z, argb);
}

/* the shape of a keyboard key as seen at 640x480 (42 x 35, one hex digit of coverage each): cut from the 3D oval it replaces */
#define KEY_TEX_W 42
#define KEY_TEX_H 35
static const char* const key_shape[KEY_TEX_H] = {
    "00000000000000000048BFFFFFFFFFB84000000000",
    "00000000000000008BFFFFFFFFFFFFFFFB40000000",
    "00000000000008FFFFFFFFFFFFFFFFFFFFFF400000",
    "000000000004BFFFFFFFFFFFFFFFFFFFFFFFF40000",
    "00000000004BFFFFFFFFFFFFFFFFFFFFFFFFFF4000",
    "000000000BFFFFFFFFFFFFFFFFFFFFFFFFFFFFF400",
    "00000004BFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00",
    "0000004FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFB0",
    "000004FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF0",
    "00000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF8",
    "0000BFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF8",
    "0004FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
    "004FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
    "00BFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
    "08FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
    "0BFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
    "0FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFB",
    "4FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF4",
    "8FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF0",
    "8FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF80",
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF80",
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00",
    "8FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF000",
    "8FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF8000",
    "8FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFB0000",
    "4FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF40000",
    "0FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF400000",
    "0BFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF8000000",
    "00BFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF40000000",
    "004BFFFFFFFFFFFFFFFFFFFFFFFFFFFF8000000000",
    "0000FFFFFFFFFFFFFFFFFFFFFFFFFFF40000000000",
    "00000BFFFFFFFFFFFFFFFFFFFFFFFB000000000000",
    "000004BFFFFFFFFFFFFFFFFFFFF880000000000000",
    "00000000BFFFFFFFFFFFFFFFF80000000000000000",
    "00000000004BFFFFFFFFFB40000000000000000000",
};
static int key_tex = -1;

/* A flat key the same size and colour as the 3D oval (but a single picture): w wide (a wide key stretches the picture, as it stretched the oval). */
static void
key_flat(float cx, float cy, float w, uint32_t argb) {
    if (!gfx_dyn_valid(key_tex)) {
        unsigned short px[KEY_TEX_W * KEY_TEX_H];
        for (int y = 0; y < KEY_TEX_H; y++) {
            for (int x = 0; x < KEY_TEX_W; x++) {
                char ch = key_shape[y][x];
                unsigned a = ch <= '9' ? (unsigned)(ch - '0') : (unsigned)(ch - 'A' + 10);
                px[y * KEY_TEX_W + x] = (unsigned short)((a << 12) | 0x0FFF);
            }
        }
        gfx_dyn_free(key_tex);
        key_tex = gfx_dyn_create(px, KEY_TEX_W, KEY_TEX_H);
    }
    gfx_image_tint(key_tex, cx - w / 2.0f, cy - (float)KEY_TEX_H / 2.0f, w, (float)KEY_TEX_H, 0.45f, argb);
}

static void
key_oval(float cx, float cy, float sx, float sy, int lit, uint32_t col) {
    bvm_obj* o = bvm_find(&menu->vm, ID_KEY(kb_slot));
    if (!o || kb_slot >= KEYS_MAX) {
        return;
    }
    key_col[kb_slot] = col;
    kb_slot++;
    const float z = PANEL_Z + 0.1953125f; /* in front of the panel, as the buttons of the file screen's popups */
    const float ppu = 11.9f * (335.9375f - 0.39f) / -z;   /* 11.9 pixels per unit at the clock screen's depth */
    place(o, (cx - 320.0f) / ppu, (240.0f - cy) / ppu, z);
    o->scale_tw[0].cur = sx;
    o->scale_tw[1].cur = sy;
    o->scale_tw[2].cur = 1.0f;
    o->var[1] = lit;
    o->flags &= ~(uint32_t)BVM_F_HIDE;
}

static void
kb_label(const char* t, float x, float w, float y, uint32_t col) {
    gfx_text(t, x + (w - (float)(strlen(t) * CHAR_W)) / 2.0f, y, TEXT_Z, col, 1);
}

static void
draw_keyboard(const char* title) {
    const float gx = KB_X + (KB_W - (10.0f * KB_CELL_W + 9.0f * KB_GAP)) / 2.0f; /* the keys, in the middle */
    const float fx = KB_X + 18.0f; /* the text field */
    const float gy = KB_Y + 72.0f;
    const osk_key_t cur = osk_key(&kb);
    const int hide = ed.kind == ED_KN_PASS || ed.kind == ED_CHAT_PASS;
    int from = ed.pos > KB_SHOWN - 2 ? ed.pos - (KB_SHOWN - 2) : 0; /* the text scrolls along with the cursor */

    const int grid = !input_keyboard_present(); /* a plugged-in keyboard types the text: only the field */
    const float h = grid ? KB_H : 118.0f;
    const float top = 240.0f - h / 2.0f;

    popup_panel(KB_W, h);
    gfx_text(title, KB_X + 14.0f, top + 6.0f, TEXT_Z, YELLOW, 1);

    /* the text */
    gfx_rect(fx, top + 36.0f, KB_W - 36.0f, 30.0f, 0.4f, 0xC0050810u); /* in front of the panel */
    {
        char shown[KB_SHOWN + 1];
        char before[KB_SHOWN + 1];
        int n = 0;
        for (int i = from; i < ed.len && n < KB_SHOWN; i++) {
            shown[n++] = hide && i != ed.pos - 1 ? '*' : ed.text[i]; /* a password shows only the character just typed */
        }
        shown[n] = '\0';
        snprintf(before, sizeof(before), "%.*s", ed.pos - from, shown);
        gfx_text(shown, fx + 8.0f, top + 37.0f, TEXT_Z, WHITE, 1);
        gfx_rect(fx + 8.0f + (float)gfx_text_width(before), top + 40.0f, 2.0f, 22.0f, 0.46f, YELLOW);
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
            float ky = gy + (float)r * KB_ROW_PITCH;
            float kw = (float)key.span * KB_CELL_W + (float)(key.span - 1) * KB_GAP;
            int sel = r == kb.row && key.col == cur.col;
            uint32_t col = WHITE;
            char t[2] = {key.ch, 0};

            if (key.kind == OSK_CHAR && !osk_char_ok(ed.set, key.ch)) {
                col = 0xFF606878u; /* this editor does not take it */
            }
            if (sel) {
                key_oval(kx + kw / 2.0f, ky + KB_CELL_H / 2.0f, key.span > 1 ? (kw - 13.0f) / (OVAL_W * 1.16f) : 0.68f /* a stretched oval comes out about 16% wider than its scale says; a wide key leaves the same 3.5 px each side as a single one: 11 px between all ovals */, 0.68f, 1, 0);
            } else {
                key_flat(kx + kw / 2.0f, ky + KB_CELL_H / 2.0f, key.span > 1 ? kw + 2.0f : 42.0f, 0xFF0A5421u); /* flat, the same look: only the one under the cursor is a 3D oval (the keyboard stays fast) */
            }
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
    gfx_text("A key  X space  Y del  START done  B back", KB_X + 20.0f, KB_Y + KB_H - 34.0f, TEXT_Z, DIM, 1); /* one row (a text is cut at 42 characters) */
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

/* ---- the Online page: four rows, each opens a popup ------------------------------------------------------------------ */

static const char* const online_names[ON_ROWS] = {"Players", "Games", "Next event", "Leaderboards"};

/* The short value at the end of a row */
static void
online_value(int i, char* out, size_t size) {
    int online = conn.state == DCNOW_CONN_ONLINE;
    out[0] = '\0';
    if (i == ON_PLAYERS - 1) {
        if (pstat.list_valid) {
            snprintf(out, size, "%d online", pstat.online_total);
        } else {
            snprintf(out, size, "%s", !online ? "not connected" : pstat.state == DCNOW_FETCH_FAILED ? "failed" : "loading...");
        }
    } else if (i == ON_GAMES - 1) {
        if (pstat.list_valid) {
            snprintf(out, size, "%d being played", nogames);
        } else {
            snprintf(out, size, "%s", !online ? "not connected" : "loading...");
        }
    } else if (i == ON_EVENT - 1) {
        if (nev_rows > 0) {
            char rel[24];
            event_phase_text(ev_minutes[0], rel, sizeof(rel));
            snprintf(out, size, "%s", rel);
        } else {
            snprintf(out, size, "%s", !online ? "not connected" : estat.state == EVENTS_RUNNING ? "loading..." : "none coming");
        }
    } else {
        snprintf(out, size, "%d boards", lb_source_count());
    }
}

/* A popup in the middle of the screen, as the keyboard's: coloured border, dark fill. The lists draw inside it. */
/* The buttons of a popup, the way the Say button is: a green oval of the clock screen under the first letter of the word, the rest of it
 * beside it. `words` are separated by '|'. A notice takes their place for a moment. */
#define BTN_LEFT 22.0f /* the oval sticks out this far left of its word */
#define BTN_GAP 34.0f  /* from a word to the next oval */
static button_t
popup_button_for(const char* word) {
    if (!strcmp(word, "Close") || !strcmp(word, "Back")) {
        return BTN_B;
    }
    return !strcmp(word, "Star") ? BTN_Y : BTN_A; /* Refresh, Join, Open, Reload */
}

static void
draw_popup_buttons(const char* words) {
    npbtn = 0;
    if (notice_frames > 0) {
        text_cut(notice, 44, lay_x, lay_foot, WHITE);
        return;
    }
    char word[24];
    float total = -BTN_GAP; /* the row of buttons, from the left of the first oval to the end of the last word: in the middle */
    for (const char* p = words; p && *p;) {
        const char* bar = strchr(p, '|');
        int n = bar ? (int)(bar - p) : (int)strlen(p);
        snprintf(word, sizeof(word), "%.*s", n, p);
        total += BTN_LEFT + (float)gfx_text_width(word) + BTN_GAP;
        p += n + (bar ? 1 : 0);
    }
    float x = 320.0f - total / 2.0f + BTN_LEFT;
    for (const char* p = words; p && *p;) {
        const char* bar = strchr(p, '|');
        int n = bar ? (int)(bar - p) : (int)strlen(p);
        snprintf(word, sizeof(word), "%.*s", n, p);
        {
            button_t pb = popup_button_for(word); /* the colour of the controller button that does it: A red, B blue, X yellow, Y green */
            uint32_t pc = pb == BTN_A ? 0xF0E03C3Cu : pb == BTN_B ? 0xF02F6FE0u : pb == BTN_X ? 0xF0E0C030u : 0xF02FB050u;
            key_oval(x - 6.0f, lay_foot + 14.0f, 0.68f, 0.68f, 1, pc); /* the oval as it always was (never stretched), beside its word */
            gfx_text(word, x, lay_foot, TEXT_Z, WHITE, 1);
        }
        if (npbtn < 4) {
            pbtn[npbtn].x0 = x - BTN_LEFT;
            pbtn[npbtn].x1 = x + (float)gfx_text_width(word) + BTN_GAP / 2.0f;
            pbtn[npbtn].y0 = lay_foot - 6.0f;
            pbtn[npbtn].y1 = lay_foot + 32.0f;
            pbtn[npbtn++].b = popup_button_for(word);
        }
        x += (float)gfx_text_width(word) + BTN_GAP + BTN_LEFT;
        p += n + (bar ? 1 : 0);
    }
}

static void
draw_popup_frame(const char* title, const char* buttons) {
    const float px = 320.0f - POP_W / 2.0f, py = 240.0f - POP_H / 2.0f; /* in the middle, as the file screen's popups */
    popup_panel(POP_W, POP_H);
    lay_x = px + 16.0f;
    lay_head = py + 8.0f;
    lay_list = py + 40.0f;
    lay_right = px + POP_W - 14.0f;
    lay_foot = py + POP_H - 42.0f;
    lay_rows = POPUP_ROWS;
    lay_row_h = POPUP_ROW_H;
    if (title) {
        gfx_text(title, lay_x, lay_head, TEXT_Z, YELLOW, 1);
    }
    draw_popup_buttons(buttons);
}

static void
reset_layout(void) {
    lay_x = PANE_X + PAD;
    lay_head = HEAD_Y;
    lay_list = LIST_Y;
    lay_right = PANE_X + PANE_W - PAD;
    lay_foot = PANE_Y + PANE_H - 34.0f;
    lay_rows = ROWS;
    lay_row_h = ROW_H;
}

/* Next event popup: the one that is on or next, with what is known, then the ones after it */
static void
draw_next_event(void) {
    char line[96], rel[24];
    float x = lay_x, y = lay_list - 4.0f;
    if (nev_rows == 0) {
        text_cut(conn.state != DCNOW_CONN_ONLINE ? "Connect to see the events" : "No events coming", 44, x, lay_list, GREY);
        return;
    }
    const event_item_t* e = &events[ev_row[0]];
    event_phase_text(ev_minutes[0], rel, sizeof(rel));
    int tl = wrap_lines(e->title, 44, -1, NULL, NULL);
    for (int i = 0; i < tl && i < 2; i++) {
        const char* st = NULL;
        int len = 0;
        wrap_lines(e->title, 44, i, &st, &len);
        snprintf(line, sizeof(line), "%.*s", len < 46 ? len : 46, st);
        gfx_text(line, x, y, TEXT_Z, WHITE, 1);
        y += ROW_H;
    }
    snprintf(line, sizeof(line), "%s%s", ev_phase[0] == EVP_CURRENT ? "LIVE  " : "", rel);
    gfx_text(line, x, y, TEXT_Z, GREEN, 1);
    y += ROW_H;
    snprintf(line, sizeof(line), "%s  %s", e->when, e->source);
    text_cut(line, 44, x, y, GREY);
    y += ROW_H * 1.5f;
    for (int i = 1; i < nev_rows && i < 4; i++) {
        event_phase_text(ev_minutes[i], rel, sizeof(rel));
        snprintf(line, sizeof(line), "%s  %s", rel, events[ev_row[i]].title);
        text_cut(line, 44, x, y, DIM);
        y += ROW_H;
    }
}

/* The text of the four rows: the bars, pills and icons are objects of the scene (bpage rows mode), placed as the BIOS Settings screen has
 * them. The name is centred between the icon and the pill, the value starts 10 px inside the pill, both centred on the row's height. */
static void
draw_online_menu(void) {
    for (int r = 0; r < ON_ROWS; r++) {
        char val[40];
        float cy = bpage_rows_y_px(r);
        float lw = (float)gfx_text_width(online_names[r]);
        gfx_text(online_names[r], BPAGE_ROWS_LABEL_CX_PX - lw / 2.0f, cy - 12.0f, TEXT_Z, WHITE, 1);
        online_value(r, val, sizeof(val));
        text_cut(val, 30, BPAGE_ROWS_VALUE_X_PX, cy - 12.0f, WHITE);
    }
}

/* Leaderboards popup: the boards (and the card's browsers after them), then the lines of the one opened. */
#define BOARD_W 476.0f /* the width of the lines in the popup */

/* Where the columns of every table of the board start (x from the popup's left edge): each column as wide as its widest cell, the last takes
 * what is left; when they do not fit, the widest are cut to one width. Run when a board has arrived. */
static short lb_colx[LB_LINES_MAX][LB_CELLS];

static void
lb_layout(void) {
    for (int i = 0; i < nlb;) {
        if (lb_lines[i].kind == LB_HEAD) {
            lb_colx[i][0] = 0;
            i++;
            continue;
        }
        int j = i, ncol = 0;
        float w[LB_CELLS] = {0};
        for (; j < nlb && lb_lines[j].kind != LB_HEAD; j++) {
            for (int c = 0; c < lb_lines[j].ncell; c++) {
                float tw = (float)gfx_text_width(lb_lines[j].cell[c]);
                w[c] = tw > w[c] ? tw : w[c];
            }
            ncol = lb_lines[j].ncell > ncol ? lb_lines[j].ncell : ncol;
        }
        const float gap = 18.0f;
        /* every column as wide as its widest cell; when they do not all fit, only the widest are cut, all to the same
         * width (the largest that lets the line fit) */
        float cap = BOARD_W, need = 0.0f;
        for (int c = 0; c < ncol; c++) {
            need += w[c] + gap;
        }
        if (need > BOARD_W + gap) {
            float lo = 0.0f, hi = BOARD_W;
            for (int it = 0; it < 24; it++) {
                float mid = (lo + hi) / 2.0f, sum = 0.0f;
                for (int c = 0; c < ncol; c++) {
                    sum += w[c] + gap > mid ? mid : w[c] + gap;
                }
                if (sum <= BOARD_W + gap) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            cap = lo;
        }
        float x = 0.0f;
        short at[LB_CELLS];
        for (int c = 0; c < ncol; c++) {
            at[c] = (short)x;
            x += (w[c] + gap > cap ? cap : w[c] + gap);
        }
        for (int k = i; k < j; k++) {
            for (int c = 0; c < LB_CELLS; c++) {
                lb_colx[k][c] = c < ncol ? at[c] : 0;
            }
        }
        i = j;
    }
}

static void
draw_board(void) {
    char line[96];
    if (lb_open < 0) {
        draw_popup_frame("Leaderboards", "Open|Close");
        int n = lb_source_count() + board_count();
        for (int r = 0; r < lay_rows && top[LIST_BOARD] + r < n; r++) {
            int i = top[LIST_BOARD] + r;
            float y = lay_list + (float)r * lay_row_h;
            int is_sel = i == sel[LIST_BOARD];
            if (is_sel) {
                row_bar(lay_x, LAY_W, y);
            }
            if (i < lb_source_count()) {
                snprintf(line, sizeof(line), "%s", lb_source(i)->game);
                text_cut(line, 24, lay_x, y, is_sel ? YELLOW : WHITE);
                text_cut(lb_source(i)->label, 22, lay_x + 270.0f, y, is_sel ? YELLOW : GREY);
            } else {
                text_cut(board_item(i - lb_source_count())->name, 24, lay_x, y, is_sel ? YELLOW : WHITE);
                text_cut("browser", 22, lay_x + 270.0f, y, DIM);
            }
        }
        draw_arrows(LIST_BOARD);
        return;
    }
    snprintf(line, sizeof(line), "%s: %s", lb_source(lb_open)->game, lb_source(lb_open)->label);
    draw_popup_frame(NULL, "Reload|Back");
    text_cut(line, 42, lay_x, lay_head, YELLOW);
    if (lbst.source != lb_open || (lbst.state != LB_DONE && lbst.state != LB_FAILED)) {
        text_cut(lbst.state == LB_RUNNING ? lbst.line : "Loading...", 42, lay_x, lay_list, GREY);
        return;
    }
    if (lbst.state == LB_FAILED || nlb == 0) {
        text_cut(lbst.state == LB_FAILED ? lbst.line : "No scores on the page", 42, lay_x, lay_list, RED);
        text_cut("Reload to try again", 42, lay_x, lay_list + lay_row_h, DIM);
        return;
    }
    for (int r = 0; r < POPUP_ROWS && lb_top + r < nlb; r++) {
        const lb_line_t* l = &lb_lines[lb_top + r];
        float y = lay_list + (float)r * lay_row_h;
        uint32_t col = l->kind == LB_HEAD ? YELLOW : l->kind == LB_COLS ? DIM : WHITE;
        for (int c = 0; c < l->ncell; c++) {
            int last = c == l->ncell - 1 || l->kind == LB_HEAD;
            float x0 = (float)lb_colx[lb_top + r][c];
            float x1 = last ? BOARD_W : (float)lb_colx[lb_top + r][c + 1];
            text_cut(l->cell[c], (int)((x1 - x0 - (last ? 0.0f : 6.0f)) / CHAR_W), lay_x + x0, y, col);
        }
    }
    if (lb_top > 0) {
        gfx_text("^", lay_right - 11.0f, lay_list - 4.0f, TEXT_Z, GREY, 1);
    }
    if (lb_top + POPUP_ROWS < nlb) {
        gfx_text("v", lay_right - 11.0f, lay_list + (float)(POPUP_ROWS - 1) * lay_row_h, TEXT_Z, GREY, 1);
    }
}

static void
draw_online(void) {
    switch (view[PAGE_ONLINE]) {
        case ON_PLAYERS:
            draw_popup_frame(NULL, "Refresh|Star|Close");
            draw_players();
            break;
        case ON_GAMES:
            draw_popup_frame(NULL, "Join|Star|Close");
            draw_ogames();
            break;
        case ON_EVENT:
            draw_popup_frame("Next event", "Close");
            draw_next_event();
            break;
        case ON_BOARD:
            draw_board();
            break;
        default:
            draw_online_menu();
            break;
    }
    reset_layout();
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
            draw_online();
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
    if (ed.active || page == PAGE_CHAT || (page == PAGE_ONLINE && view[page] != ON_MENU)) { /* the chat page has its own bar down to the panel's edge, a popup its own hint */
        return;
    }
    if (notice_frames > 0) {
        text = notice;
    } else if (conn.state == DCNOW_CONN_FAILED && conn.count > 2) {
        text = conn.lines[2]; /* the advice that goes with the reason */
    } else if (detail) {
        text = page == PAGE_EVENTS && view[page] == VIEW_A ? "Up/Down: scroll  Y: star  B: back" : "Up/Down: scroll  B: back";
    } else if (page == PAGE_ONLINE) {
        if (view[page] != ON_MENU) {
            return; /* a popup has its own hint */
        }
        return; /* the help box says what the row is */
    } else if (page == PAGE_CHAT && view[page] == VIEW_A) {
        text = chat_st[DC99_CHAT_IRC].state == DC99_CHAT_ON ? "A: say  Y: login  START: KosNet  Up: back" : "A: join  Y: login  START: KosNet";
    } else if (page == PAGE_CHAT) {
        text = "Up/Down: scroll back  START: IRC";
    } else if (page == PAGE_PLAY && sel[LIST_PLAY] < ngames) {
        const game_row* g = &games[sel[LIST_PLAY]];
        struct db_item* meta = NULL;
        if (g->browser) {
            text = "A: start the browser";
        } else if (have_meta && !db_get_meta(g->item->product, &meta) && meta && db_item_connections(meta)) { /* what META.DAT says about its connections */
            static const char* const how[4] = {"modem", "BBA", "W5500", "serial"};
            const unsigned c = db_item_connections(meta);
            size_t at = (size_t)snprintf(line, sizeof(line), "Connects by:");
            for (int k = 0; k < 4 && at < sizeof(line); k++) {
                if (c & (1u << k)) {
                    at += (size_t)snprintf(line + at, sizeof(line) - at, " %s", how[k]);
                }
            }
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
            enum { ICON_PAD = 128, ICON_GLYPH = 86 }; /* a power of two: the glyph (two thirds of it) in the middle of a clear picture; the button's picture area is large */
            static uint16_t big[ICON_PAD * ICON_PAD];
            for (int l = 0; l < 2; l++) {
                uicon_render(k, l ? 0xFF000000u : 0xFF1C2850u, px);
                memset(big, 0, sizeof(big));
                const int off = (ICON_PAD - ICON_GLYPH) / 2;
                for (int y = 0; y < ICON_GLYPH; y++) {
                    for (int x = 0; x < ICON_GLYPH; x++) {
                        big[(y + off) * ICON_PAD + off + x] = px[(y * UICON_SIZE / ICON_GLYPH) * UICON_SIZE + x * UICON_SIZE / ICON_GLYPH];
                    }
                }
                ids[l][k] = gfx_dyn_create(big, ICON_PAD, ICON_PAD);
            }
        }
        made = 1;
    }
    return ids[lit][i];
}

void
uin_draw(void) {
    npbtn = 0; /* the popups that are drawn set their buttons again */
    const bscene_sink* sink = gfx_sink();
    if (!(page == PAGE_ONLINE && view[PAGE_ONLINE] == ON_MENU && !ed.active)) { /* the Online rows are bars of their own */
        gfx_pane(PANE_X, PANE_Y, PANE_W, PANE_H, PANE_Z, BSCENE_PANE_ARGB);
    }
    draw_status();
    say_on = 0;
    kb_slot = 0;
    panel_on = 0;
    for (int i = 0; i < KEYS_MAX; i++) {
        bvm_obj* k = bvm_find(&menu->vm, ID_KEY(i));
        if (k) {
            k->flags |= BVM_F_HIDE;
        }
    }
    draw_page();
    draw_footer();
    {
        bvm_obj* po = bvm_find(&menu->vm, ID_PANEL);
        if (po) {
            if (panel_on) {
                place(po, 0.0f, 0.0f, PANEL_Z);
                po->var[0] = (int)(panel_w / PANEL_PX + 0.5f);
                po->var[1] = (int)(panel_h / PANEL_PX + 0.5f);
                po->var[2] = po->var[3] = 256;
                po->text_w = po->text_h = 0; /* no text of its own: the popup's text is drawn over it */
                po->panel_w = po->var[0]; /* the script takes the size once, when it starts: the size of this frame's popup */
                po->panel_h = po->var[1];
                po->flags = (po->flags | BVM_F_PANEL) & ~(uint32_t)BVM_F_HIDE;
                menu->scene.object_panel_accent = BMENU_ACCENT_MUSIC; /* the Internet screen's blue frame: a popup takes the colour of its main-screen icon */
            } else {
                po->flags |= BVM_F_HIDE;
            }
        }
    }
    {
        bvm_obj* so = bvm_find(&menu->vm, ID_SAY);
        if (so) {
            if (say_on && !ed.active) {
                const float px = 11.9f, z = -335.9375f + 0.39f; /* pixels per unit and depth of the clock screen's buttons */
                place(so, (say_cx - 320.0f) / px, (240.0f - say_cy) / px, z);
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
        menu->scene.ovr_n = 0;
        if (o->id >= ID_KEY(0) && o->id < ID_KEY(KEYS_MAX) && key_col[o->id - ID_KEY(0)]) { /* a button of its own colour: the oval's one polygon */
            menu->scene.ovr[0].model = o->model;
            menu->scene.ovr[0].node = 0;
            menu->scene.ovr[0].poly = 0;
            menu->scene.ovr[0].argb = key_col[o->id - ID_KEY(0)];
            menu->scene.ovr[0].lit = 0;
            menu->scene.ovr_n = 1;
        }
        if (o->id >= ID_BUTTON(0) && o->id < ID_BUTTON(PAGES)) {
            /* the glyph is the button's own picture (texture 0), so it sits in the model and moves with it */
            gfx_decal_override(o->texlist, page_icon((int)(o->id - ID_BUTTON(0)), (int)(o->id - ID_BUTTON(0)) == page));
        }
        bscene_draw_object(&menu->scene, o, sink);
    }
    menu->scene.no_decals = 0;
    menu->scene.ovr_n = 0;
}
