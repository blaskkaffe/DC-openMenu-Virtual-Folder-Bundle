/*
 * File: kosnet_post.h
 * Project: openmenu
 * Posting to the KosNet global chat with the user's own KosNet account. KosNet's API is HTTPS only
 * (https://kosnet.psyops.studio/api/v1: POST /login, then POST /chat/messages with the bearer token), so this needs mbedTLS from
 * kos-ports. Without it (OM_HAVE_MBEDTLS not defined) everything reports "TLS is not in this build". Reading the chat stays with
 * dc99_chat (DC99's plain-HTTP feed). The password is only held in RAM for the login call; the token stays in RAM until the
 * screen is left.
 */

#pragma once

#include <stddef.h>

enum { KOSNET_OUT = 0, KOSNET_BUSY, KOSNET_IN };

typedef struct kosnet_post_status {
    int state;
    char name[32]; /* the display name, when KOSNET_IN */
    char note[64]; /* the last result or error, "" when none */
    int generation;
} kosnet_post_status_t;

/* 1 when this build can do TLS at all. */
int kosnet_tls_available(void);
/* One HTTPS GET of a page (HTTP/1.0) into out: headers, then the body. Returns the status code, -1 on a failure (note says why) and -2 when
 * *stop became 1. Port 443, the certificate is not checked (see above). */
int https_get_page(const char* host, const char* path, char* out, size_t cap, volatile int* stop, const char** note);
/* Signs in (on a worker thread). Ignored while busy. */
void kosnet_login(const char* user, const char* pass);
/* Posts one line (1-240 bytes); needs KOSNET_IN. Returns 0 when started, -1 when not signed in or busy. */
int kosnet_post(const char* text);
void kosnet_logout(void);
void kosnet_post_status(kosnet_post_status_t* out);
/* Stops the worker and forgets the token. */
void kosnet_post_stop(void);
