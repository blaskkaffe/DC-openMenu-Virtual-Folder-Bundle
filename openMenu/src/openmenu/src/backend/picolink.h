/*
 * File: picolink.h
 * Project: openmenu
 * Talks to the gateway the console is connected through (DC-PicoLink; a DC-DreamDash DreamPi answers the same language without the wifi
 * part): POST http://192.168.98.1/dcapi with the dc1 text of picolink_parse. One request at a time on a worker thread below the menu's
 * priority; the menu polls picolink_status(). Nothing here runs unless the Internet screen's Settings ask for it.
 */

#pragma once

#include "backend/picolink_parse.h"

typedef struct picolink_status {
    int busy;       /* a request is on its way */
    int generation; /* grows with every finished request */
    int reached;    /* the last request got a dc1 answer */
    char note[64];  /* what went wrong or the box's own "err" text, "" when all is well */
    pl_info_t info; /* what the gateway said so far */
} picolink_status_t;

/* Ask hello, net.* and wifi.* (also the way to find out whether a gateway is there). Ignored while busy. */
void picolink_probe(void);
/* Switch the gateway's network (DCNow! or DCNet) from the next call on. */
void picolink_select(int dcnet);
/* Search for Wi-Fi networks (the box leaves its channel for 2-3 s). */
void picolink_scan(void);
/* Join scan result `index` (name used when index < 0) with the password ("" for an open network). */
void picolink_join(int index, const char* name, const char* pass);
void picolink_status(picolink_status_t* out);
/* Stops the worker and waits for it; the last answers are kept. */
void picolink_stop(void);
