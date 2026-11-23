/*
 * File: lb_fetch.h
 * Project: openmenu
 * Leaderboards for the Internet screen, read on a worker thread over plain HTTP and cut into lines by lb_parse.
 */
#pragma once

#include "backend/lb_parse.h"

typedef struct lb_source {
    const char* game;
    const char* label;
    const char* host;
    const char* path;
} lb_source_t;

typedef enum lb_state { LB_IDLE = 0, LB_RUNNING, LB_DONE, LB_FAILED } lb_state_t;

typedef struct lb_status {
    lb_state_t state;
    int generation; /* grows by one for every board that arrived */
    int count;      /* lines of the board in the store */
    int source;     /* the board they belong to */
    char line[64];  /* what is going on, or why it failed */
} lb_status_t;

int lb_source_count(void);
const lb_source_t* lb_source(int i);

/* Starts reading board `source`. An earlier read still running is ended first. */
void lb_fetch_start(int source);
/* Asks a running read to stop (it never blocks the menu). */
void lb_fetch_abort(void);
/* Snapshot of the state, once per frame. */
void lb_fetch_poll(lb_status_t* out);
/* Copies the lines of the newest board. Returns how many were copied. */
int lb_fetch_copy(lb_line_t* out, int max);
