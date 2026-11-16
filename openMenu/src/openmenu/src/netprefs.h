/*
 * netprefs: the Internet settings and lists of this menu that 1.7.1 has no place for, kept in the tail of the favorites file (EXTENDED.SYS,
 * see favorites.h; OPENMENU.SYS is shared with 1.7.1 and must not grow). Pure logic, host-tested with test/netprefs_test.c.
 *   - the Connection setting (netsel.h), the preferred network (DCNow! or DCNet) and the network saved for single games
 *   - the ISP settings: how an adapter gets its address (console's own / DHCP / fixed) and what the modem and the serial link dial
 *   - the stars: players (by name), games (by identity hash) and events (by title hash), for the notifications
 * A change calls the hook (favorites.c saves the file at once).
 */
#pragma once

#include <stdint.h>

#define NP_MAX_BYTES 600
#define NP_GAME_NETS 40
#define NP_STAR_PLAYERS 8
#define NP_PLAYER_NAME 16
#define NP_STAR_GAMES 20
#define NP_STAR_EVENTS 10
#define NP_DIAL_LEN 16

/* The networks a game or the global setting can name (the DreamPi add-on's numbering). 0 as a game's value: none saved, use the global one. */
enum { NP_NET_NONE = 0, NP_NET_DCNOW = 1, NP_NET_DCNET = 2 };
/* How an adapter gets its address */
enum { NP_ISP_CONSOLE = 0, NP_ISP_DHCP = 1, NP_ISP_STATIC = 2 };

typedef struct np_isp {
    uint8_t mode;
    uint8_t ip[4], mask[4], gateway[4], dns[4];
    char number[NP_DIAL_LEN], login[NP_DIAL_LEN], password[NP_DIAL_LEN]; /* empty: DreamPi's fixed values */
} np_isp_t;

void np_reset(void);
void np_set_changed_hook(void (*hook)(void));

/* The file tail. np_pack returns the bytes written (at most NP_MAX_BYTES, 0 when room is too small); np_unpack returns 0 or -1 (then everything is at its default). */
int np_pack(uint8_t* out, int room);
int np_unpack(const uint8_t* in, int len);

int np_iface(void);
void np_set_iface(int setting);
int np_net(void);        /* the preferred network, NP_NET_DCNOW or NP_NET_DCNET */
void np_set_net(int net);

/* The network for a game: the one saved for it, else the preferred one. */
int np_game_net_saved(uint32_t hash);            /* NP_NET_NONE when none is saved */
int np_game_net(uint32_t hash);
int np_set_game_net(uint32_t hash, int net);     /* NP_NET_NONE forgets it; returns 0, or -1 when the table is full */

const np_isp_t* np_isp(void);
void np_set_isp(const np_isp_t* isp);

/* Stars: a toggle returns 1 (now starred), 0 (not any more) or -1 (the list is full). */
int np_player_starred(const char* name);
int np_player_toggle(const char* name);
int np_star_player_count(void);
const char* np_star_player(int index);
int np_game_starred(uint32_t hash);
int np_game_toggle(uint32_t hash);
int np_star_game_count(void);
uint32_t np_star_game(int index);
int np_event_starred(const char* title);
int np_event_toggle(const char* title);
