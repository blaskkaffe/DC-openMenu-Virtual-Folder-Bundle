/*
 * File: net_test.h
 * Project: openmenu
 * The Internet screen's network test: which interfaces answer, and (when online) the address, the DNS lookup and the server round trip, on a worker
 * thread. Every line says where it was measured: local to this console, or against a server (then it only means something while online). Port
 * forwarding and anything that needs an outside service or a DreamPi add-on is listed as not tested, never guessed.
 */
#pragma once

#define NT_LINES 14
#define NT_TEXT 52

enum { NT_INFO = 0, NT_OK, NT_WARN, NT_FAIL };

typedef struct nt_line {
    char text[NT_TEXT];
    int level;
} nt_line_t;

/* Starts a test run on the worker. Does nothing while one runs. busy: a connection is up or coming up (then the hardware is not probed, it would
 * take the serial port from it); online: it is up (then the link is tested). The screen knows both from its connection poll. */
void net_test_start(int busy, int online);
/* 1 while it runs. */
int net_test_running(void);
/* Copies the lines so far; returns how many. */
int net_test_lines(nt_line_t* out, int max);
