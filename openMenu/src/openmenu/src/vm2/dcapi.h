/*
 * DC API client over Maple: sends a request in the language "dc1" (dcapi_text.h) to the memory card unit of an adapter
 * that said it speaks it (the capability bits behind the marker in its extended device info, see vm2_rescan() in main.c)
 * and returns the answer text. The same lines can also go to the DreamPi (POST /dcapi) later; the DreamPi hosts the
 * communication when there is one, this is the direct route (decision D-12).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <dc/maple.h>

#include "dcapi_text.h"

typedef struct dcapi_hello {
    uint32_t caps;     /* capability bits (hello.caps names them; the bits are the ones of the extended device info) */
    char name[24];     /* hello.name, "" when the device did not answer */
    char version[24];  /* hello.version */
} dcapi_hello_t;

/* Sends the request text (it must start with "dc1\n", dcapi_build() makes it) and copies the answer text, NUL terminated, into
 * ans. Returns its length, -1 when the device did not answer in time, -2 when it only acknowledged (it does not speak the
 * language), -3 when the answer is not in the language. */
int dcapi_request(maple_device_t* dev, const char* req, char* ans, size_t ans_cap);

/* "get hello" into h (caps stay as they are; they come from the extended device info). Returns 0 or a negative dcapi_request() code. */
int dcapi_get_hello(maple_device_t* dev, dcapi_hello_t* h);

/* "set <key> <value>"; the answer's text (value or "code why") goes into reply when it is not NULL. Returns 1 for ok, 0 for an
 * err line, or a negative dcapi_request() code. */
int dcapi_set(maple_device_t* dev, const char* key, const char* value, char* reply, size_t reply_cap);
