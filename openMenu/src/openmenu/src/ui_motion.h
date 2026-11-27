/*
 * ui_motion: the Joy-Con motion page (Samba de Amigo maracas): switch the adapter's motion mode on and off, see which Joy-Cons it follows,
 * and take the calibration poses. It talks to the iBlueControlMod adapter in the DC API language (vm2/dcapi_text.h, keys motion.*, docs/dcapi.md
 * of the adapter). Logic only, no KallistiOS: the front end (ui_bios.c, a bpage of these rows) calls uim_row() for each row, uim_activate()
 * for A, uim_tick() every frame, and gives the transport that sends a request text to the adapter and returns its answer (dcapi_request()).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Sends the request, copies the NUL terminated answer into ans. Returns the answer's length or a negative number (no answer). */
typedef int (*uim_transport)(void* user, const char* req, char* ans, size_t ans_cap);

enum { UIM_ROW_MODE, UIM_ROW_STATE, UIM_ROW_LEFT, UIM_ROW_RIGHT, UIM_ROW_FORWARD, UIM_ROW_UP, UIM_ROW_OUT, UIM_ROW_RESET, UIM_ROWS };

#define UIM_POSE_SECONDS 5   /* time to get the Joy-Cons still after the button */
#define UIM_REFRESH_MS 700   /* how often the state is read while the page is open */

typedef struct uim {
    uim_transport send;
    void* user;
    int supported;        /* 1 when the adapter has the motion keys */
    int reachable;        /* the last request got an answer */
    char mode[8];         /* off / on */
    char state[16];       /* off waiting uncalibrated ready */
    char left[16], right[16]; /* absent uncalibrated ready */
    char msg[48];         /* a line for the info bar: what just happened, or what to do next */
    int pose_ms;          /* > 0: a pose is counting down (milliseconds left, then a moment of looking at the result) */
    int pose;             /* which one: UIM_ROW_FORWARD ... */
    int confirm_reset;    /* the first A on "Forget" asked, the second does it */
    int since_refresh_ms;
} uim;

void uim_init(uim* m, uim_transport send, void* user);
/* Reads motion.mode, motion.state, motion.left, motion.right. Returns 1 when the adapter answered with them, 0 when it did not (no
 * motion: the page says so), negative when it did not answer. */
int uim_refresh(uim* m);
/* 1 when the page is of any use (the adapter has motion) */
int uim_supported(const uim* m);

int uim_rows(void);
/* The row as the page shows it: label and value (what the A button would do is in the help). help is a constant string, up to 40 characters. */
void uim_row(const uim* m, int row, char* label, size_t label_cap, char* value, size_t value_cap, const char** help);
/* A on a row: toggles the mode, starts a pose, forgets what was learned. Returns 1 when something was sent. */
int uim_activate(uim* m, int row);
/* Time passes (ms since the last call, a frame). Counts a pose down, and reads the state again when it is due. Returns 1 when the rows changed. */
int uim_tick(uim* m, int ms);
