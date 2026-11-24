/* p2m_api: the "dc1" text API of the Pico2Maple firmware (2.5 and later), spoken over Maple command 0x22 to its memory card unit.
 * A request is "dc1\n" and lines "get <key>", "set <key> <value>", "call <action>"; the answer is "dc1\n" and one "ok <key> <value>" or
 * "err <key> <why>" line each. Keys: display.brightness (0-100), display.flip (normal|flipped|toggle), vmu.icons (bios|image),
 * vmu.slot (1-10|gameid), port.self.layout, port.self.switch.auto (on|off), port.self.mode, sys.commit ... */
#pragma once

#include <stddef.h>

#include <dc/maple.h>

/* The first memory card unit that answers as a Pico2Maple, or NULL. Asks each memory card (a blocking Maple round trip). */
maple_device_t* p2m_find(void);

/* Send the request lines (without the "dc1" line) and copy the answer text (without its "dc1" line) into out. 0 on success. */
int p2m_request(maple_device_t* dev, const char* lines, char* out, size_t cap);

/* One key: the value of "ok <key> <value>" into value; 0 on success, -1 when the device did not answer or said err. */
int p2m_get(maple_device_t* dev, const char* key, char* value, size_t cap);
int p2m_set(maple_device_t* dev, const char* key, const char* value);

/* Parse the value out of an answer text for a key; 1 when there is an "ok <key> " line. Exposed for the host test. */
int p2m_answer_value(const char* answer, const char* key, char* value, size_t cap);
