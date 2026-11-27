/*
 * ctrl_editor: a generic controller mapper and editor for the adapters that speak the DC API language ("dc1": iBlueControlMod, Pico2Maple and
 * whatever comes later). It knows no project: everything it shows comes from the device's own keys (port.<A-D>.mode / modes / acc / accs /
 * layout / layouts / name, port.<A-D>.map.<OUTPUT>, sys.unsaved, call sys.commit / sys.revert), and the pages that only some devices have
 * appear when the device has the keys: the Joy-Con motion page (docs/dcapi.md of the adapter: motion.*) shows for ports whose type is "maracas"
 * and a device that has them. Logic only, no KallistiOS: the app front end (ui_ctrl.c) draws the rows as a bpage and the calibration man
 * (pose_man.h) on the motion page, and gives the transport (dcapi_request() on the adapter's Maple device).
 *
 *   Port page      Type, Memory card, [Memory card slot], [Game id], Load game id >, Rumble, Layout, Buttons >, [Joy-Con motion >],
 *                  Save, Undo (the rows in brackets only for a device that has the keys: port.<X>.vmu.slot(s), game.mode)
 *   Game page      the games of the card; A sends the game id (Maple 0x21: the adapter loads that game's memory card or settings)
 *   Pico2Maple     its own keys (display.brightness / flip, vmu.icons / slot, port.self.layout / switch.auto: each row only when the device
 *                  answered the key), Load game id >, and Save / Undo only for a device that says sys.store commit
 *   Buttons page   one row per Dreamcast output with what drives it; A picks another input for it (Pick page)
 *   Motion page    ui_motion.c: mode, status, poses; the man's arms follow the Joy-Cons (ced_man)
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ui_motion.h"

typedef uim_transport ced_transport; /* sends a request text, copies the answer text: dcapi_request() of the device */

enum { CED_PAGE_PORT, CED_PAGE_BUTTONS, CED_PAGE_PICK, CED_PAGE_MOTION, CED_PAGE_GAME, CED_PAGES };

/* The games "Load game id" offers, and how an id reaches the adapter (the front end: the card's game list, vm2_set_id() on the port's memory
 * card unit). send returns 0 when the adapter took it. */
typedef struct ced_games {
    int (*count)(void* user);
    const char* (*name)(void* user, int i);
    const char* (*id)(void* user, int i);
    int (*send)(void* user, const char* id, const char* name);
    void* user;
} ced_games;

#define CED_OUTPUTS 26
#define CED_VALUE_MAX 40

typedef struct ced {
    ced_transport send;
    void* user;
    int reachable;               /* the last request got an answer */
    int port;                    /* 0..3 = A..D */
    int page, cursor[CED_PAGES], top[CED_PAGES];
    char state[8];               /* port.X.state: empty bt wired */
    char mode[12], acc[8], layout[12], devname[32];
    char modes[96], accs[40], layouts[40];  /* the values the device accepts, separated by commas */
    char unsaved[4];             /* yes / no */
    int api;                     /* 1: the device speaks the DC API (always, the editor only opens for one) */
    int p2m;                     /* 1: a Pico2Maple: its own keys (P2M_*) instead of the port.<X>.* ones */
    int store_commit;            /* sys.store is commit: a change waits for Save (call sys.commit) */
    int p2m_has;                 /* bit k: the Pico2Maple answered key k of its list */
    char p2m_val[6][16];         /* and what it said */
    int has_slot, has_game_mode; /* the device has port.<X>.vmu.slot / game.mode */
    char slot[8], slots[64], game_mode[8];
    ced_games games;
    char map[CED_OUTPUTS][CED_VALUE_MAX];   /* what drives each output */
    int map_loaded;
    int pick_out;                /* the output the Pick page is for */
    uim motion;                  /* the Joy-Con motion page */
    int motion_known;            /* the device has the motion keys (asked once the port is a maracas port) */
    int left_deg, right_deg, heights_ok;    /* the live heights of the Joy-Cons, degrees */
    int since_heights_ms;
    char msg[56];
} ced;

void ced_init(ced* e, ced_transport send, void* user, int port);
/* A Pico2Maple (its firmware with the DC API): the page of its own settings */
void ced_init_p2m(ced* e, ced_transport send, void* user, int port);
void ced_set_games(ced* e, const ced_games* g);
/* Reads the port's keys. Returns 1, 0 when the device has no such port, negative when it did not answer. */
int ced_refresh(ced* e);

const char* ced_title(const ced* e);
int ced_rows(const ced* e);
/* The row as the page shows it; help is a constant string of up to 40 characters */
void ced_row(const ced* e, int row, char* label, size_t label_cap, char* value, size_t value_cap, const char** help);
int ced_cursor(const ced* e);
int ced_move(ced* e, int delta);                 /* up / down: returns 1 when the cursor moved */
int ced_set_cursor(ced* e, int row);
int ced_activate(ced* e);                        /* A: returns 1 when something was sent or the page changed */
int ced_change(ced* e, int dir);                 /* left (-1) / right (+1) on a row that has values: type, accessory, layout */
int ced_back(ced* e);                            /* B: goes up one page; returns 1 when the editor is left */
int ced_tick(ced* e, int ms);                    /* time passes: 1 when the rows changed */
int ced_wants_man(const ced* e);                 /* the page shows the calibration man */
int ced_man(const ced* e, float* left_deg, float* right_deg);  /* his arms: 1 when the heights are live */
