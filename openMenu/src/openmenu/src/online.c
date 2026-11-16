/* online: see online.h. */
#include <stdbool.h>
#include <string.h>

#include <dc/maple.h>
#include <dc/maple/vmu.h>

#include <arch/timer.h>

#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include "backend/dcnow_fetch.h"
#include <backend/gd_list.h>
#include "backend/dcnow_net.h"
#include "backend/dcnow_vmu.h"
#include "backend/dreampi_link.h"
#include "backend/online_games.h"
#include "backend/online_time_sync.h"
#include <backend/gd_item.h>
#include <kos/thread.h>

#include "event_phase.h"
#include "netprefs.h"
#include "clock.h"
#include "backend/events_fetch.h"
#include "online.h"
#include "vmu_lcd_utils.h"

void
online_apply_prefs(void) {
    const np_isp_t* isp = np_isp();
    dcnow_isp_t fixed;
    memset(&fixed, 0, sizeof(fixed));
    memcpy(fixed.ip, isp->ip, 4);
    memcpy(fixed.netmask, isp->mask, 4);
    memcpy(fixed.gateway, isp->gateway, 4);
    memcpy(fixed.dns, isp->dns, 4);
    dcnow_net_set_interface(np_iface());
    dcnow_net_set_adapter_mode(isp->mode, &fixed);
    dcnow_net_set_dial(isp->number, isp->login, isp->password);
}

/* The network of a game about to start: when the console is online through a DreamPi with the add-on and that has another network selected than the game
 * wants (its own saved one, else the preferred one), ask the Pi to switch and wait a moment for the answer. With no add-on there is nothing to switch:
 * the game starts on whatever the link is. */
void
online_select_network_for(const gd_item* g) {
    dcnow_status_t c;
    if (!g || sf_dcnow[0] == DCNOW_OFF) {
        return;
    }
    dcnow_conn_poll(&c);
    if (c.state != DCNOW_CONN_ONLINE) {
        return;
    }
    int want = np_game_net(gd_item_recent_hash(g));
    int have = dreampi_link_network(); /* 0: not known */
    if (have == 0 || have == want) {
        return;
    }
    dreampi_link_select_clear();
    if (dreampi_link_select_network(want) != 0) {
        return;
    }
    for (int i = 0; i < 30; i++) { /* up to 1.5 s: 0 queued, 1 sending */
        int r = dreampi_link_select_result();
        if (r >= 2) {
            break;
        }
        thd_sleep(50);
    }
}

void
online_boot(void) {
    online_apply_prefs();
    if (sf_dcnow[0] == DCNOW_AUTO_CONNECT) {
        dcnow_conn_autostart();
    }
}

void
online_set_hangup_hook(void (*hangup)(void)) {
    dcnow_net_set_hangup_hook(hangup);
}

/* A VMU plugged in while the menu runs gets the screen it should show (the main branch's vmu_lcd_check_insertions). */
#define VMU_LCD_CHECK_INTERVAL 120 /* about two seconds */

static void
vmu_lcd_check_insertions(void) {
    static bool prev_valid[8];
    static int counter;
    static bool seeded; /* the cards present at start were drawn by savefile_init: only the ones plugged in later are new */
    if (++counter < VMU_LCD_CHECK_INTERVAL && seeded) {
        return;
    }
    counter = 0;
    for (int port = 0; port < 4; port++) {
        for (int unit = 1; unit <= 2; unit++) {
            int idx = port * 2 + (unit - 1);
            maple_device_t* dev = maple_enum_dev(port, unit);
            bool now_valid = dev && dev->valid;
            if (seeded && now_valid && !prev_valid[idx] && (dev->info.functions & MAPLE_FUNC_LCD)) {
                if (savefile_lcd_owned()) {
                    dcnow_vmu_redraw();
                } else {
                    vmu_draw_lcd_auto(dev, (void*)savefile_lcd_logo());
                }
            }
            prev_valid[idx] = now_valid;
        }
    }
    seeded = true;
}

/* The top bar shows the player count, so the list is read once a link is up even when nobody has opened the Internet screen. The
 * online games code keeps it fresh when it can; with the DreamPi add-on it does not use the list, so this keeps it fresh then. */
static void
header_fetch_tick(void) {
    static uint64_t last_start;
    dcnow_status_t conn;
    dcnow_fetch_status_t fetch;

    if (sf_dcnow[0] == DCNOW_OFF) {
        return;
    }
    dcnow_conn_poll(&conn);
    if (conn.state != DCNOW_CONN_ONLINE) {
        return;
    }
    dcnow_fetch_poll(&fetch);
    if (fetch.state == DCNOW_FETCH_RUNNING) {
        last_start = timer_ms_gettime64();
        return;
    }
    if (!fetch.list_valid && fetch.state == DCNOW_FETCH_IDLE) {
        last_start = timer_ms_gettime64();
        dcnow_fetch_start(); /* once per link: a failure leaves the state FAILED, not IDLE, until the Internet screen retries */
    } else if (fetch.list_valid && dreampi_link_playing_known()) {
        int seconds = dcnow_refresh_seconds(sf_dcnow_refresh[0]);
        if (seconds > 0 && timer_ms_gettime64() - last_start >= (uint64_t)seconds * 1000u) {
            last_start = timer_ms_gettime64();
            dcnow_fetch_start();
        }
    }
}

void
online_header_get(online_header* out) {
    dcnow_status_t conn;
    dcnow_fetch_status_t fetch;

    out->shown = sf_dcnow[0] != DCNOW_OFF;
    out->state = 0;
    out->players = -1;
    out->dcnow = out->dcnet = 0;
    out->network = 0;
    if (!out->shown) {
        return;
    }
    dcnow_conn_poll(&conn);
    switch (conn.state) {
        case DCNOW_CONN_ONLINE: out->state = 2; break;
        case DCNOW_CONN_CONNECTING: out->state = conn.hanging_up ? 0 : 1; break;
        case DCNOW_CONN_FAILED: out->state = 3; break;
        default: break;
    }
    if (out->state == 2) {
        out->network = dreampi_link_network(); /* 0 without the DreamPi add-on: plain Dreamcast Now! */
        dcnow_fetch_poll(&fetch);
        if (fetch.list_valid) {
            out->players = fetch.online_total;
            out->dcnow = fetch.online_dcnow;
            out->dcnet = fetch.online_dcnet;
        }
    }
}

/* ---- Notifications for the stars (netprefs.h): a starred player who is online, a starred game somebody plays, a starred event about to start. Shown in the
 * top bar's upper row for about 20 seconds each time one of them becomes true. The data is what the app has already fetched (the player list that keeps
 * the header's count fresh, the events list once the Internet screen has read it): no extra traffic except one events fetch per link when an event is starred. */
#define NOTICE_FRAMES 1200
#define NOTICE_EVERY 120 /* frames between looks */
#define NOTIFIED_MAX 40

static char notice_text[64];
static int notice_left;
static uint32_t notified[NOTIFIED_MAX];
static int notified_n;
static dcnow_player_t note_players[DCNOW_PLAYER_MAX];
static event_item_t note_events[EVENTS_MAX];
static int note_events_asked;

static uint32_t
key_hash(char tag, const char* s) {
    uint32_t h = 2166136261u ^ (uint8_t)tag;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h;
}

static int
notice_known(uint32_t k) {
    for (int i = 0; i < notified_n; i++) {
        if (notified[i] == k) {
            return 1;
        }
    }
    return 0;
}

static void
notice_look(void) {
    uint32_t now_keys[NOTIFIED_MAX];
    int n = 0;
    char text[64] = "";
    if (np_star_player_count() + np_star_game_count() == 0 && !np_event_starred_any()) {
        notified_n = 0;
        return;
    }
    dcnow_fetch_status_t fs;
    dcnow_fetch_poll(&fs);
    int count = fs.list_valid ? dcnow_fetch_copy(note_players, DCNOW_PLAYER_MAX) : 0;
    for (int s = 0; s < np_star_player_count() && n < NOTIFIED_MAX; s++) {
        for (int p = 0; p < count; p++) {
            if (strncmp(note_players[p].name, np_star_player(s), NP_PLAYER_NAME - 1) == 0) {
                uint32_t k = key_hash('P', np_star_player(s));
                now_keys[n++] = k;
                if (!notice_known(k)) {
                    snprintf(text, sizeof(text), "* %.20s is online", np_star_player(s));
                }
                break;
            }
        }
    }
    for (int s = 0; s < np_star_game_count() && n < NOTIFIED_MAX; s++) {
        const gd_item* it = list_find_by_hash(np_star_game(s));
        if (it && online_games_players(it) > 0) {
            uint32_t k = np_star_game(s) ^ 0x9E3779B9u;
            now_keys[n++] = k;
            if (!notice_known(k)) {
                snprintf(text, sizeof(text), "* %.24s: somebody is playing", it->name);
            }
        }
    }
    int ev = events_fetch_copy(note_events, EVENTS_MAX);
    int y, mo, d, h, mi;
    clock_get(&y, &mo, &d, &h, &mi);
    for (int e = 0; e < ev && n < NOTIFIED_MAX; e++) {
        int minutes;
        int ph = event_phase(note_events[e].sort_key, y, mo, d, h, mi, &minutes);
        if (ph == EVP_PAST || !np_event_starred(note_events[e].title) || (ph == EVP_UPCOMING && minutes > 15)) {
            continue;
        }
        uint32_t k = key_hash('E', note_events[e].title);
        now_keys[n++] = k;
        if (!notice_known(k)) {
            {
                char rel[24];
                event_phase_text(minutes, rel, sizeof(rel));
                snprintf(text, sizeof(text), "* %.24s: %s", note_events[e].title, rel);
            }
        }
    }
    memcpy(notified, now_keys, (size_t)n * sizeof(uint32_t));
    notified_n = n;
    if (text[0]) {
        snprintf(notice_text, sizeof(notice_text), "%s", text);
        notice_left = NOTICE_FRAMES;
    }
}

int
online_notice_get(char* out, size_t size) {
    if (notice_left <= 0) {
        return 0;
    }
    snprintf(out, size, "%s", notice_text);
    return 1;
}

static void
notice_tick(void) {
    static unsigned frame;
    dcnow_status_t conn;
    if (notice_left > 0) {
        notice_left--;
    }
    if (++frame % NOTICE_EVERY != 0 || sf_dcnow[0] == DCNOW_OFF) {
        return;
    }
    dcnow_conn_poll(&conn);
    if (conn.state != DCNOW_CONN_ONLINE) {
        note_events_asked = 0;
        return;
    }
    if (!note_events_asked && np_event_starred_any()) { /* the events list is read once per link for the stars */
        note_events_asked = 1;
        events_fetch_start();
    }
    notice_look();
}

void
online_tick(int menu_idle) {
    vmu_lcd_check_insertions();
    header_fetch_tick();
    dcnow_conn_tick();
    dreampi_link_ui_idle(menu_idle);
    dreampi_link_tick();
    online_games_tick();
    dcnow_vmu_tick();
    online_time_sync_tick();
    notice_tick();
}

void
online_shutdown(void) {
    dcnow_net_shutdown();
}
