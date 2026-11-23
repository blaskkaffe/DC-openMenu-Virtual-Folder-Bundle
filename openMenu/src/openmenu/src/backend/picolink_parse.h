/*
 * File: picolink_parse.h
 * Project: openmenu
 * The `dc1` text language of the gateways (DC-PicoLink, DC-DreamDash on a DreamPi): the requests openMenu sends to POST /dcapi and the
 * parsing of the answers. No KOS, host-tested with test/picolink_parse_test.c. The transport is picolink.c.
 *
 *   hello.kind / hello.name / hello.caps ("api,net,wifi")       net.selected "dcnow"|"dcnet", net.dcnet "ok"|"off:..."
 *   wifi.state none|connecting|connected|failed, wifi.ssid, wifi.ip, wifi.error, wifi.scan.n, wifi.scan.<i> "<rssi> <1 open|0 locked> <name>"
 *   call net.select dcnow|dcnet     call wifi.scan     set wifi.pass <8..63 chars>     call wifi.join <name | #<i>>
 */

#pragma once

#include <stddef.h>

#define PL_SCAN_MAX 16
#define PL_NAME_MAX 33

typedef struct pl_net {
    int rssi;               /* dBm, about -30 (strong) to -90 */
    int open;               /* 1: no password */
    char name[PL_NAME_MAX]; /* the network name */
} pl_net_t;

typedef struct pl_info {
    int present;      /* a hello with the api came back */
    int has_net;      /* "net" among the caps */
    int has_wifi;     /* "wifi" among the caps */
    char name[24];    /* hello.name */
    int dcnet;        /* net.selected: 1 DCNet, 0 DCNow!, -1 not known */
    int dcnet_ok;     /* net.dcnet is "ok" */
    char wifi_state[12];
    char wifi_ssid[PL_NAME_MAX];
    char wifi_ip[20];
    char wifi_error[48];
    int scan_n;       /* the number the box found, -1 no scan yet */
    int scan_have;    /* how many of them are in scan[] (at most PL_SCAN_MAX) */
    pl_net_t scan[PL_SCAN_MAX];
    char err[64];     /* the first "err" line of the last answer, "" when none */
} pl_info_t;

void pl_info_init(pl_info_t* info);

/* Requests (each one a complete body for POST /dcapi); return the length, 0 when it does not fit or the argument is not allowed. */
size_t pl_req_status(char* out, size_t cap);                         /* hello, net.*, wifi.* */
size_t pl_req_select(char* out, size_t cap, int dcnet);              /* call net.select, then net.* */
size_t pl_req_scan(char* out, size_t cap);                           /* call wifi.scan, then wifi.* (the list) */
/* Join: the password first (write only), then the join of scan result `index` (or of `name` when index < 0), then wifi.*. An empty
 * password is for an open network; otherwise 8 to 63 printable characters with no blank at either end (the box trims lines). */
size_t pl_req_join(char* out, size_t cap, int index, const char* name, const char* pass);

/* Is this a password the box takes (the range it states, printable, no blank at either end)? Empty is allowed (an open network). */
int pl_pass_ok(const char* pass);

/* Reads an answer into info (the fields the answer carries are changed, the others stay). Returns the number of "ok" lines, -1 when the
 * answer is not dc1 at all. */
int pl_parse(const char* resp, pl_info_t* info);

/* Signal strength as 0..4 bars from the dBm. */
int pl_bars(int rssi);
