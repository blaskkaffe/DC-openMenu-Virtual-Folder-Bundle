/*
 * DC API client over Maple: sends a request in the language "dc1" (dcapi_text.h) to the unit on the bus of a port (unit 0,
 * the controller the adapter presents; never a memory card unit) and returns the answer text. The same lines can also go to the DreamPi (POST /dcapi) later; the DreamPi hosts the
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

/* The same request without waiting: dcapi_begin() queues it (-1: one is already on its way, or the bus frame is busy: try again on the
 * next frame); dcapi_poll() then gives the answer's length (> 0) once it is there, 0 while it is on its way, negative when it failed
 * or no answer came within 200 ms. One request at a time; dcapi_request() waits for one on its way first. */
int dcapi_begin(maple_device_t* dev, const char* req);
int dcapi_poll(char* ans, size_t ans_cap);
