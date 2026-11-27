/* ctrl_adapter: the controller adapter's own settings as rows of the Settings list, for an adapter that speaks the DC API ("dc1",
 * vm2/dcapi.h; iBlueControlMod today). Nothing here knows a project: the ports, their modes, accessories and layouts are what the
 * adapter answers (port.<A-D>.mode / modes, .acc / accs, .layout / layouts, .state, sys.store; docs/04-control-channel.md of
 * DC-UnifiedEcosystem). A device is only asked when its extended device info carries "dc1" and the api and port capability bits
 * behind its marker, so a plain controller, a VMU, a VM2 or a Pico2Maple (which takes the game id but has no DC API) is never sent
 * the command. The search runs when the Settings screen opens and only again when the devices on the ports changed.
 *
 * The text side (parsing, the rows, the requests) is plain C; the Maple side is dcapi_request() and vm2_extended_info(). The host test
 * (test/ctrl_adapter_test.c) builds with -DCTA_HOST_TEST and gives its own transport. */
#pragma once

#include <stddef.h>

#define CTA_PORTS 4
enum { CTA_MODE, CTA_ACC, CTA_LAYOUT, CTA_KINDS };
#define CTA_ROWS (CTA_PORTS * CTA_KINDS)

/* Find the adapter (or keep the one found while the devices are the same) and read its ports. Call when the Settings screen opens. */
void cta_refresh(void);

/* The adapter's name ("iBlueControlMod"), "" when there is none. */
const char* cta_name(void);

/* The rows: one per port the adapter has and per setting it offers there, port A first. */
int cta_rows(void);
const char* cta_label(int row);   /* "Port A type" */
const char* cta_help(int row);    /* "Bluetooth pad: what the console sees" */
int cta_choice_count(int row);
const char* cta_choice_name(int row, int choice); /* shown: "Dual stick" */
int cta_get(int row);
/* Change it on the adapter, and have the adapter store it (sys.commit) when it says changes wait for that. 0 when the adapter took it. */
int cta_set(int row, int choice);

#ifdef CTA_HOST_TEST
/* the host test's adapter: sends a request text, copies the answer text; returns its length or a negative number */
typedef int (*cta_transport)(const char* req, char* ans, size_t cap);
void cta_host_attach(const char* name, cta_transport send);
#endif
