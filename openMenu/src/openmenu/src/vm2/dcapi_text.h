/*
 * The text side of the DC API client ("dc1", the control language of the DC- projects; design: docs/04-control-channel.md in
 * https://github.com/blaskkaffe/DC-UnifiedEcosystem). Plain C without KallistiOS so a PC can test it
 * (openMenu/tests/host/run.sh). The Maple side (dcapi.c) only moves these strings.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define DCAPI_CMD 0x22          /* Maple command to the memory card unit (decision D-15) */
#define DCAPI_REQ_MAX 252       /* bytes of request text */
#define DCAPI_ANS_MAX 400       /* bytes of answer text */

#define DCAPI_CAP_API 0
#define DCAPI_CAP_NET 1
#define DCAPI_CAP_LINK 2
#define DCAPI_CAP_PORT 3
#define DCAPI_CAP_PROFILE 4
#define DCAPI_CAP_GAME 5
#define DCAPI_CAP_LED 6
#define DCAPI_CAP_PAIR 7
#define DCAPI_CAP_VMU 8
#define DCAPI_CAP_SD 9
#define DCAPI_CAP_ONLINE 10

/* The 24 bytes after the 16 byte marker in the extended device info (ALLINFO "extended" + 16): "dc1 " and eight hex digits.
 * Returns 1 and the capability bits, or 0 when the device does not say it speaks the language. */
int dcapi_caps_parse(const char* ext24, uint32_t* caps);

/* 1 when the bit (DCAPI_CAP_*) is set */
static inline int
dcapi_has(uint32_t caps, int bit) {
    return (caps >> bit) & 1u;
}

/* "dc1\n" + operations (lines like "get hello\nset net.selected dcnet\n") into out. Returns the length, or -1 when it does not fit
 * in DCAPI_REQ_MAX / out_cap. */
int dcapi_build(char* out, size_t out_cap, const char* ops);

/* Looks for the answer line of one key in an answer text. 1 = "ok <key> <value>" (value copied), 0 = "err <key> <code> [why]"
 * (everything after the key copied), -1 = no line for the key. The value is cut to val_cap - 1. */
int dcapi_answer(const char* answer, const char* key, char* val, size_t val_cap);

/* 1 when the answer starts with the language level line "dc1" (an old device answers something else or nothing) */
int dcapi_answer_ok(const char* answer);
