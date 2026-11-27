/* ctrl_adapter: the settings of controller adapters that speak the DC API ("dc1", vm2/dcapi.h) as rows of the Settings list:
 * Pico2Maple (its unified firmware: display, memory card icons and slot, pad layout, keyboard/mouse switch) and iBlueControlMod (type,
 * accessory and layout of each of its ports).
 *
 * Every request goes to the unit on the bus of a port (unit 0, the controller the adapter presents), never to a memory card. A port is
 * asked "get hello.name" once when the Settings screen opens and only again when what is plugged into the ports changed; a port whose
 * device does not answer as a known adapter is not asked anything else. One iBlueControlMod answers for all its ports, so it is asked
 * on the first port it answered on only.
 *
 * The text side is plain C; the host test (test/ctrl_adapter_test.c) builds with -DCTA_HOST_TEST and gives its own transport. */
#pragma once

#include <stddef.h>

#define CTA_ROWS 24

/* Look at the ports (or keep what was found while the devices are the same) and read the adapters' settings. Call when Settings opens. */
void cta_refresh(void);

/* The rows: per adapter, the settings it answered for, in a fixed order. */
int cta_rows(void);
const char* cta_group(int row);   /* "Pico2Maple (port A)", "iBlueControlMod" */
const char* cta_label(int row);
const char* cta_help(int row);
int cta_choice_count(int row);
const char* cta_choice_name(int row, int choice);
int cta_get(int row);
/* Change it on the adapter and have it stored ("call sys.commit" unless the adapter says it stores by itself). 0 when it took it. */
int cta_set(int row, int choice);

#ifdef CTA_HOST_TEST
/* the host test's ports: send a request text to the device on a port, copy the answer text; length, or negative when nothing answers */
typedef int (*cta_transport)(int port, const char* req, char* ans, size_t cap);
void cta_host_attach(cta_transport send);
#endif
