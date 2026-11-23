/*
 * File: kosnet_post.c
 * Project: openmenu
 * See kosnet_post.h. One worker thread per call (login or post), below the menu's priority. TLS 1.2 with mbedTLS over the same
 * poll-based sockets as the chats. The server certificate is NOT verified yet (the CA is not in the build and the Dreamcast's clock is
 * often wrong): the traffic is encrypted against listeners, but not against somebody who sits in the middle.
 */

#include "backend/kosnet_post.h"

#include <stdbool.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arch/timer.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include <sys/socket.h>

#include "backend/dc99_chat.h"
#include "backend/kosnet_json.h"

#define KN_API_HOST "kosnet.psyops.studio"
#define KN_API_PORT 443
#define KN_RESP_MAX 6144
#define KN_TIMEOUT_MS 40000 /* a handshake on the SH-4 takes a few seconds */

static mutex_t lock = MUTEX_INITIALIZER;
static kosnet_post_status_t st;
static kthread_t* worker = NULL;
static volatile int stop_flag = 0;

static char token[512];
static char job_user[48], job_pass[48], job_text[260];
static int job_kind; /* 1 login, 2 post */

static void
set_state(int state, const char* name, const char* note) {
    mutex_lock(&lock);
    st.state = state;
    if (name) {
        snprintf(st.name, sizeof(st.name), "%s", name);
    }
    snprintf(st.note, sizeof(st.note), "%s", note ? note : "");
    st.generation++;
    mutex_unlock(&lock);
}

void
kosnet_post_status(kosnet_post_status_t* out) {
    mutex_lock(&lock);
    *out = st;
    mutex_unlock(&lock);
}

static void
join_worker(void) {
    if (worker) {
        thd_join(worker, NULL);
        worker = NULL;
    }
}

#ifdef OM_HAVE_MBEDTLS

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>

typedef struct net {
    int fd;
} net_t;

static int
net_send(void* ctx, const unsigned char* buf, size_t len) {
    ssize_t n = send(((net_t*)ctx)->fd, buf, len, 0);
    if (n >= 0) {
        return (int)n;
    }
    return errno == EWOULDBLOCK || errno == EAGAIN ? MBEDTLS_ERR_SSL_WANT_WRITE : (-0x0044);
}

static int
net_recv(void* ctx, unsigned char* buf, size_t len) {
    ssize_t n = recv(((net_t*)ctx)->fd, buf, len, 0);
    if (n >= 0) {
        return (int)n; /* 0: the server closed */
    }
    return errno == EWOULDBLOCK || errno == EAGAIN ? MBEDTLS_ERR_SSL_WANT_READ : (-0x004C);
}

/* Entropy: the timer's jitter, the clock and the addresses of things, mixed. Not strong, and the only source the Dreamcast has. */
static unsigned entropy_calls;
static int
entropy(void* arg, unsigned char* out, size_t len) {
    uint64_t acc = timer_ns_gettime64() ^ ((uint64_t)(uintptr_t)&acc << 17) ^ ((uint64_t)++entropy_calls << 40);
    (void)arg;
    for (size_t i = 0; i < len; i++) {
        for (int k = 0; k < 7; k++) { /* each byte waits a jittery moment */
            uint64_t t = timer_ns_gettime64();
            acc = acc * 6364136223846793005ULL + 1442695040888963407ULL + (t ^ (t >> 11));
            if ((t & 3) == 0) {
                thd_pass();
            }
        }
        out[i] = (unsigned char)(acc >> 33);
    }
    return 0;
}

/* One HTTPS request. The whole response (headers and body) goes into resp. Returns the status code, -1 on a failure (note set), -2 when stopped. */
static int
https_core(const char* host, int port, volatile int* stop, int plain_get, const char* method, const char* path, const char* bearer, const char* body, char* resp, size_t resp_size, const char** note) {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context drbg;
    net_t net;
    char req[1024];
    int rc, ret = -1;
    size_t used = 0;
    uint64_t deadline = timer_ms_gettime64() + KN_TIMEOUT_MS;
    const char* sess_note = "The server did not answer";

    *note = "Could not set up TLS";
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ctr_drbg_init(&drbg);
    net.fd = dc99_net_open(host, port, stop);
    if (net.fd < 0) {
        *note = net.fd == -2 ? "Stopped" : "Could not reach the server";
        ret = net.fd == -2 ? -2 : -1;
        goto done;
    }
    if (mbedtls_ctr_drbg_seed(&drbg, entropy, NULL, (const unsigned char*)"openMenu", 8) != 0 ||
        mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        goto closed;
    }
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE); /* see the top of the file */
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
    if (mbedtls_ssl_setup(&ssl, &conf) != 0 || mbedtls_ssl_set_hostname(&ssl, host) != 0) {
        goto closed;
    }
    mbedtls_ssl_set_bio(&ssl, &net, net_send, net_recv, NULL);

    *note = "The TLS handshake failed";
    while ((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            goto closed;
        }
        rc = dc99_net_wait(net.fd, rc == MBEDTLS_ERR_SSL_WANT_READ ? POLLRDNORM : POLLWRNORM, deadline, stop);
        if (rc <= 0) {
            *note = rc < 0 ? "Stopped" : "The server did not answer";
            ret = rc < 0 ? -2 : -1;
            goto closed;
        }
    }

    if (plain_get) { /* a page: HTTP/1.0, so the answer is never chunked */
        snprintf(req, sizeof(req), "GET %.600s HTTP/1.0\r\nHost: %.120s\r\nUser-Agent: openMenu\r\nAccept: text/html\r\nConnection: close\r\n\r\n", path, host);
    } else
    snprintf(req, sizeof(req),
             "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: openMenu\r\nAccept: application/json\r\nConnection: close\r\n%s%s%s"
             "Content-Type: application/json\r\nContent-Length: %d\r\n\r\n%s",
             method, path, host, bearer ? "Authorization: Bearer " : "", bearer ? bearer : "", bearer ? "\r\n" : "",
             body ? (int)strlen(body) : 0, body ? body : "");
    for (size_t sent = 0, total = strlen(req); sent < total;) {
        rc = mbedtls_ssl_write(&ssl, (const unsigned char*)req + sent, total - sent);
        if (rc > 0) {
            sent += (size_t)rc;
        } else if (rc == MBEDTLS_ERR_SSL_WANT_WRITE || rc == MBEDTLS_ERR_SSL_WANT_READ) {
            rc = dc99_net_wait(net.fd, rc == MBEDTLS_ERR_SSL_WANT_READ ? POLLRDNORM : POLLWRNORM, deadline, stop);
            if (rc <= 0) {
                *note = rc < 0 ? "Stopped" : sess_note;
                ret = rc < 0 ? -2 : -1;
                goto closed;
            }
        } else {
            *note = "Sending failed";
            goto closed;
        }
    }
    for (;;) { /* until the server closes */
        rc = mbedtls_ssl_read(&ssl, (unsigned char*)resp + used, resp_size - 1 - used);
        if (rc > 0) {
            used += (size_t)rc;
            if (used >= resp_size - 1) {
                break;
            }
        } else if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            rc = dc99_net_wait(net.fd, rc == MBEDTLS_ERR_SSL_WANT_READ ? POLLRDNORM : POLLWRNORM, deadline, stop);
            if (rc <= 0) {
                if (used == 0) {
                    *note = rc < 0 ? "Stopped" : sess_note;
                    ret = rc < 0 ? -2 : -1;
                    goto closed;
                }
                break;
            }
        } else {
            break; /* closed, or an error after the answer */
        }
    }
    resp[used] = '\0';
    {
        const char* b;
        ret = kn_http_status(resp, &b);
        if (ret < 0) {
            *note = "The server gave an unexpected answer";
        }
    }
closed:
    mbedtls_ssl_close_notify(&ssl);
    close(net.fd);
done:
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&drbg);
    return ret;
}

static int
https_call(const char* method, const char* path, const char* bearer, const char* body, char* resp, size_t resp_size, const char** note) {
    return https_core(KN_API_HOST, KN_API_PORT, &stop_flag, 0, method, path, bearer, body, resp, resp_size, note);
}

int
https_get_page(const char* host, const char* path, char* out, size_t cap, volatile int* stop, const char** note) {
    return https_core(host, 443, stop, 1, "GET", path, NULL, NULL, out, cap, note);
}

int
kosnet_tls_available(void) {
    return 1;
}

#else /* no TLS library in this build */

static int
https_call(const char* method, const char* path, const char* bearer, const char* body, char* resp, size_t resp_size, const char** note) {
    (void)method, (void)path, (void)bearer, (void)body, (void)resp, (void)resp_size;
    *note = "TLS is not in this build";
    return -1;
}

int
https_get_page(const char* host, const char* path, char* out, size_t cap, volatile int* stop, const char** note) {
    (void)host, (void)path, (void)out, (void)cap, (void)stop;
    *note = "TLS is not in this build";
    return -1;
}

int
kosnet_tls_available(void) {
    return 0;
}

#endif

static char resp[KN_RESP_MAX];

static void
do_login(void) {
    static const char* const platforms[] = {"dreamcast", "web"}; /* the server's list of platforms is not known: "web" is the one the website sends */
    char user[100], pass[100], body[320], name[32] = "", tok[512];
    const char* note = "";
    const char* json;
    int code = -1;

    if (kn_json_escape(user, sizeof(user), job_user) < 0 || kn_json_escape(pass, sizeof(pass), job_pass) < 0) {
        set_state(KOSNET_OUT, "", "Login too long");
        return;
    }
    for (size_t p = 0; p < sizeof(platforms) / sizeof(platforms[0]); p++) {
        snprintf(body, sizeof(body), "{\"username\":\"%s\",\"password\":\"%s\",\"platform\":\"%s\"}", user, pass, platforms[p]);
        code = https_call("POST", "/api/v1/login", NULL, body, resp, sizeof(resp), &note);
        if (code != 400 && code != 422) {
            break; /* anything but "the request was not understood" ends the tries */
        }
    }
    memset(body, 0, sizeof(body));
    memset(pass, 0, sizeof(pass));
    memset(job_pass, 0, sizeof(job_pass));
    if (code == -2) {
        set_state(KOSNET_OUT, "", "");
        return;
    }
    if (code < 0) {
        set_state(KOSNET_OUT, "", note);
        return;
    }
    json = strstr(resp, "\r\n\r\n");
    json = json ? json + 4 : "";
    if (code == 200 && kn_json_str(json, "token", tok, sizeof(tok)) > 0) {
        mutex_lock(&lock);
        snprintf(token, sizeof(token), "%s", tok);
        mutex_unlock(&lock);
        if (kn_json_str(json, "display_name", name, sizeof(name)) <= 0) {
            snprintf(name, sizeof(name), "%.31s", job_user);
        }
        set_state(KOSNET_IN, name, "Signed in");
        return;
    }
    switch (kn_error_kind(json)) {
        case KN_ERR_CREDENTIALS: note = "Wrong username or password"; break;
        case KN_ERR_EMAIL: note = "Verify your email on KosNet first"; break;
        case KN_ERR_RATE: note = "Too many tries: wait a while"; break;
        default: note = code == 401 ? "Wrong username or password" : "KosNet refused the login"; break;
    }
    set_state(KOSNET_OUT, "", note);
}

static void
do_post(void) {
    char text[600], body[640], tok[512];
    const char* note = "";
    const char* json;
    int code;

    mutex_lock(&lock);
    snprintf(tok, sizeof(tok), "%s", token);
    mutex_unlock(&lock);
    if (kn_json_escape(text, sizeof(text), job_text) < 0) {
        set_state(KOSNET_IN, NULL, "Message too long");
        return;
    }
    snprintf(body, sizeof(body), "{\"body\":\"%s\"}", text);
    code = https_call("POST", "/api/v1/chat/messages", tok, body, resp, sizeof(resp), &note);
    memset(tok, 0, sizeof(tok));
    if (code == -2) {
        set_state(KOSNET_IN, NULL, "");
    } else if (code == 201 || code == 200) {
        set_state(KOSNET_IN, NULL, "Sent");
    } else if (code == 401) {
        mutex_lock(&lock);
        token[0] = '\0';
        mutex_unlock(&lock);
        set_state(KOSNET_OUT, "", "Session over: sign in again");
    } else if (code < 0) {
        set_state(KOSNET_IN, NULL, note);
    } else {
        json = strstr(resp, "\r\n\r\n");
        set_state(KOSNET_IN, NULL, kn_error_kind(json ? json + 4 : "") == KN_ERR_RATE ? "Too fast: wait a moment" : "KosNet did not take the message");
    }
}

static void*
worker_main(void* arg) {
    (void)arg;
    thd_set_prio(thd_get_current(), PRIO_DEFAULT + 4);
    if (job_kind == 1) {
        do_login();
    } else {
        do_post();
    }
    return NULL;
}

void
kosnet_login(const char* user, const char* pass) {
    if (st.state == KOSNET_BUSY) {
        return;
    }
    join_worker();
    stop_flag = 0;
    snprintf(job_user, sizeof(job_user), "%s", user);
    snprintf(job_pass, sizeof(job_pass), "%s", pass);
    job_kind = 1;
    set_state(KOSNET_BUSY, "", "Signing in...");
    worker = thd_create(0, worker_main, NULL);
}

int
kosnet_post(const char* text) {
    if (st.state != KOSNET_IN || !text[0]) {
        return -1;
    }
    join_worker();
    stop_flag = 0;
    snprintf(job_text, sizeof(job_text), "%.240s", text);
    job_kind = 2;
    {
        char name[32];
        mutex_lock(&lock);
        snprintf(name, sizeof(name), "%s", st.name);
        mutex_unlock(&lock);
        set_state(KOSNET_BUSY, name, "Sending...");
    }
    worker = thd_create(0, worker_main, NULL);
    return 0;
}

void
kosnet_logout(void) {
    kosnet_post_stop();
    set_state(KOSNET_OUT, "", "");
}

void
kosnet_post_stop(void) {
    stop_flag = 1;
    join_worker();
    mutex_lock(&lock);
    token[0] = '\0';
    mutex_unlock(&lock);
    if (st.state != KOSNET_OUT) {
        set_state(KOSNET_OUT, "", "");
    }
}
