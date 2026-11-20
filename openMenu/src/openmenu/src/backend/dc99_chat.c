/*
 * File: dc99_chat.c
 * Project: openmenu
 * See dc99_chat.h. Two worker threads below the menu's priority, plain sockets with a poll loop that looks at the stop flag every 100 ms.
 */

#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arch/timer.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "backend/dc99_chat.h"
#include "backend/dc99_chat_parse.h"
#include "backend/dcnow_net.h"

#define KN_HOST "dc99.net"
#define IRC_HOST "irc.dc99.net"
#define IRC_PORT 6667
#define KN_BODY_MAX 24576
#define KN_POLL_MS 6000
#define IO_TIMEOUT_MS 15000

typedef struct chan {
    mutex_t lock;
    dc99_row_t rows[DC99_ROWS];
    int head;  /* index of the next row to write */
    int count; /* rows held */
    dc99_chat_status_t st;
} chan_t;

static chan_t chans[DC99_CHATS] = {
    {.lock = MUTEX_INITIALIZER},
    {.lock = MUTEX_INITIALIZER},
};

static kthread_t* kn_thread = NULL;
static kthread_t* irc_thread = NULL;
static volatile int kn_stop = 0;
static volatile int irc_stop = 0;

/* the IRC thread's input and output */
static char irc_nick[24], irc_pass[24];
static mutex_t out_lock = MUTEX_INITIALIZER;
static char out_line[420];
static int out_pending = 0;

static char kn_body[KN_BODY_MAX + 1];
static dc99_kn_msg_t kn_msgs[DC99_KN_MAX];

/* ---- rows ---- */

static void
set_state(chan_t* c, int state, const char* note) {
    mutex_lock(&c->lock);
    c->st.state = state;
    snprintf(c->st.note, sizeof(c->st.note), "%s", note ? note : "");
    c->st.generation++;
    mutex_unlock(&c->lock);
}

/* Adds text as wrapped rows. */
static void
push(chan_t* c, int kind, const char* prefix, const char* text) {
    char lines[6][DC99_ROW_CHARS + 4];
    int n = dc99_chat_wrap(prefix, text, DC99_ROW_CHARS, &lines[0][0], 6, DC99_ROW_CHARS + 4);

    mutex_lock(&c->lock);
    for (int i = 0; i < n; i++) {
        dc99_row_t* r = &c->rows[c->head];
        snprintf(r->text, sizeof(r->text), "%s", lines[i]);
        r->kind = (uint8_t)kind;
        c->head = (c->head + 1) % DC99_ROWS;
        if (c->count < DC99_ROWS) {
            c->count++;
        }
    }
    c->st.rows = c->count;
    c->st.generation++;
    mutex_unlock(&c->lock);
}

void
dc99_chat_status(int which, dc99_chat_status_t* out) {
    chan_t* c = &chans[which];
    mutex_lock(&c->lock);
    *out = c->st;
    mutex_unlock(&c->lock);
}

int
dc99_chat_copy(int which, dc99_row_t* out, int max) {
    chan_t* c = &chans[which];
    int n;

    mutex_lock(&c->lock);
    n = c->count < max ? c->count : max;
    for (int i = 0; i < n; i++) { /* the newest n, oldest first */
        out[i] = c->rows[(c->head - n + i + DC99_ROWS * 2) % DC99_ROWS];
    }
    mutex_unlock(&c->lock);
    return n;
}

/* ---- sockets ---- */

/* The events that came (POLLIN / POLLOUT bits), 0 at the deadline, -1 when stop was asked. */
static int
wait_fd(int fd, short events, uint64_t deadline, volatile int* stop) {
    struct pollfd pfd;

    while (timer_ms_gettime64() < deadline) {
        if (*stop) {
            return -1;
        }
        pfd.fd = fd;
        pfd.events = events;
        pfd.revents = 0;
        if (poll(&pfd, 1, 100) > 0) {
            return pfd.revents;
        }
    }
    return 0;
}

/* A connected non-blocking TCP socket, or -1 (-2 when stop was asked). */
static int
open_tcp(const char* host, int port, volatile int* stop) {
    struct sockaddr_in addr;
    int fd;
    int rc;

    if (!dcnow_resolve(host, port, &addr)) {
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    rc = connect(fd, (const struct sockaddr*)&addr, sizeof(addr));
    if (rc < 0 && errno != EWOULDBLOCK && errno != EINPROGRESS && errno != EAGAIN) {
        close(fd);
        return -1;
    }
    rc = wait_fd(fd, POLLWRNORM, timer_ms_gettime64() + IO_TIMEOUT_MS, stop);
    if (rc <= 0 || (rc & (POLLHUP | POLLERR))) {
        close(fd);
        return rc < 0 ? -2 : -1;
    }
    return fd;
}

/* The same helpers for kosnet_post.c (TLS over the same sockets). */
int
dc99_net_open(const char* host, int port, volatile int* stop) {
    return open_tcp(host, port, stop);
}

int
dc99_net_wait(int fd, short events, uint64_t deadline, volatile int* stop) {
    return wait_fd(fd, events, deadline, stop);
}

static int
send_all(int fd, const char* data, size_t len, volatile int* stop) {
    size_t sent = 0;
    uint64_t deadline = timer_ms_gettime64() + IO_TIMEOUT_MS;

    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            if (wait_fd(fd, POLLWRNORM, deadline, stop) > 0) {
                continue;
            }
        }
        return -1;
    }
    return 0;
}

/* One HTTP GET into kn_body. Returns the status code, -1 on a failure, -2 when stop was asked. */
static int
http_get(const char* host, const char* path) {
    char req[200];
    size_t used = 0;
    int code = -1;
    int fd = open_tcp(host, 80, &kn_stop);

    if (fd < 0) {
        return fd == -2 ? -2 : -1;
    }
    snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: openMenu\r\nConnection: close\r\n\r\n", path, host);
    if (send_all(fd, req, strlen(req), &kn_stop) < 0) {
        close(fd);
        return -1;
    }
    uint64_t deadline = timer_ms_gettime64() + 30000;
    for (;;) {
        int rc = wait_fd(fd, POLLRDNORM, deadline, &kn_stop);
        if (rc < 0) {
            close(fd);
            return -2;
        }
        if (rc == 0) {
            close(fd);
            return -1;
        }
        ssize_t got = recv(fd, kn_body + used, KN_BODY_MAX - used, 0);
        if (got > 0) {
            used += (size_t)got;
            if (used >= KN_BODY_MAX) {
                break;
            }
            continue;
        }
        if (got < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && !(rc & POLLHUP)) {
            continue;
        }
        break;
    }
    close(fd);
    kn_body[used] = '\0';
    if (sscanf(kn_body, "HTTP/%*d.%*d %d", &code) != 1) {
        return -1;
    }
    return code;
}

/* ---- KosNet ---- */

static void*
kn_main(void* arg) {
    chan_t* c = &chans[DC99_CHAT_KOSNET];
    char cursor[32] = "";
    long last_id = 0;
    (void)arg;

    thd_set_prio(thd_get_current(), PRIO_DEFAULT + 4);
    set_state(c, DC99_CHAT_CONNECTING, "");
    while (!kn_stop) {
        char path[96];
        int code;

        dc99_kn_path(cursor, path, sizeof(path));
        code = http_get(KN_HOST, path);
        if (code == -2) {
            break;
        }
        if (code == 200) {
            const char* json = strstr(kn_body, "\r\n\r\n");
            char next[32];
            int n = json ? dc99_kn_parse(json + 4, kn_msgs, DC99_KN_MAX, next, sizeof(next)) : -1;

            if (n >= 0) {
                if (next[0]) {
                    snprintf(cursor, sizeof(cursor), "%s", next);
                }
                for (int i = 0; i < n; i++) {
                    long id = atol(kn_msgs[i].id);
                    char prefix[40];

                    if (id > 0 && id <= last_id) {
                        continue; /* seen */
                    }
                    last_id = id > last_id ? id : last_id;
                    if (kn_msgs[i].join) {
                        push(c, DC99_ROW_SYS, "", kn_msgs[i].body);
                    } else {
                        snprintf(prefix, sizeof(prefix), "%s: ", kn_msgs[i].name);
                        push(c, DC99_ROW_MSG, prefix, kn_msgs[i].body);
                    }
                }
                set_state(c, DC99_CHAT_ON, "");
            } else if (n == -2) {
                set_state(c, DC99_CHAT_FAILED, "KosNet is not reachable right now.");
            } else {
                set_state(c, DC99_CHAT_FAILED, "dc99.net gave an unexpected answer.");
            }
        } else {
            set_state(c, DC99_CHAT_FAILED, code > 0 ? "dc99.net did not give the chat." : "dc99.net did not answer.");
        }
        for (int ms = 0; ms < KN_POLL_MS && !kn_stop; ms += 100) {
            thd_sleep(100);
        }
    }
    if (c->st.state != DC99_CHAT_FAILED) {
        set_state(c, DC99_CHAT_OFF, "");
    }
    return NULL;
}

void
dc99_kosnet_start(void) {
    if (kn_thread != NULL) {
        return;
    }
    kn_stop = 0;
    kn_thread = thd_create(false, kn_main, NULL);
    if (kn_thread == NULL) {
        set_state(&chans[DC99_CHAT_KOSNET], DC99_CHAT_FAILED, "Could not start the chat.");
    }
}

/* ---- IRC ---- */

static int
irc_send_line(int fd, const char* line) {
    char buf[520];
    int n = snprintf(buf, sizeof(buf), "%s\r\n", line);

    if (n <= 0 || n >= (int)sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    return send_all(fd, buf, (size_t)n, &irc_stop);
}

static void
irc_row(chan_t* c, const dc99_irc_ev_t* ev) {
    char prefix[48];

    switch (ev->type) {
        case DC99_IRCE_MSG:
            snprintf(prefix, sizeof(prefix), "%s: ", ev->nick);
            push(c, DC99_ROW_MSG, prefix, ev->text);
            break;
        case DC99_IRCE_ACTION:
            snprintf(prefix, sizeof(prefix), "* %s ", ev->nick);
            push(c, DC99_ROW_MSG, prefix, ev->text);
            break;
        case DC99_IRCE_JOIN:
            snprintf(prefix, sizeof(prefix), "%s joined", ev->nick);
            push(c, DC99_ROW_SYS, "", prefix);
            break;
        case DC99_IRCE_PART:
        case DC99_IRCE_QUIT:
            snprintf(prefix, sizeof(prefix), "%s left", ev->nick);
            push(c, DC99_ROW_SYS, "", prefix);
            break;
        case DC99_IRCE_NOTICE: push(c, DC99_ROW_SYS, "", ev->text); break;
        default: break;
    }
}

static void*
irc_main(void* arg) {
    chan_t* c = &chans[DC99_CHAT_IRC];
    static char rx[1536];
    int rxlen = 0;
    int fd;
    char line[160];
    int failed = 0;
    (void)arg;

    thd_set_prio(thd_get_current(), PRIO_DEFAULT + 4);
    set_state(c, DC99_CHAT_CONNECTING, "");
    fd = open_tcp(IRC_HOST, IRC_PORT, &irc_stop);
    if (fd < 0) {
        set_state(c, fd == -2 ? DC99_CHAT_OFF : DC99_CHAT_FAILED, fd == -2 ? "" : "Could not reach irc.dc99.net.");
        return NULL;
    }
    snprintf(line, sizeof(line), "PASS %s:%s", irc_nick, irc_pass);
    failed = irc_send_line(fd, line) < 0;
    snprintf(line, sizeof(line), "NICK %s", irc_nick);
    failed |= irc_send_line(fd, line) < 0;
    snprintf(line, sizeof(line), "USER %s 0 * :%s", irc_nick, irc_nick);
    failed |= irc_send_line(fd, line) < 0;
    if (failed) {
        close(fd);
        set_state(c, DC99_CHAT_FAILED, "The server did not take the login.");
        return NULL;
    }
    memset(irc_pass, 0, sizeof(irc_pass)); /* not kept longer than the login needs it */

    while (!irc_stop && !failed) {
        int rc = wait_fd(fd, POLLRDNORM, timer_ms_gettime64() + 200, &irc_stop);

        if (rc < 0) {
            break;
        }
        if (rc > 0) {
            ssize_t got;

            if (rxlen >= (int)sizeof(rx) - 1) {
                rxlen = 0; /* an absurdly long line: dropped */
            }
            got = recv(fd, rx + rxlen, sizeof(rx) - 1 - (size_t)rxlen, 0);
            if (got > 0) {
                rxlen += (int)got;
            } else if (!(got < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))) {
                set_state(c, DC99_CHAT_FAILED, "The server closed the connection.");
                failed = 1;
                break;
            }
        }
        for (;;) { /* whole lines */
            char* nl = memchr(rx, '\n', (size_t)rxlen);
            char text[1536];
            dc99_irc_ev_t ev;
            int len;

            if (nl == NULL) {
                break;
            }
            len = (int)(nl - rx);
            if (len > 0 && rx[len - 1] == '\r') {
                len--;
            }
            if (len > (int)sizeof(text) - 1) {
                len = (int)sizeof(text) - 1;
            }
            memcpy(text, rx, (size_t)len);
            text[len] = '\0';
            memmove(rx, nl + 1, (size_t)(rxlen - (int)(nl - rx) - 1));
            rxlen -= (int)(nl - rx) + 1;
            if (!dc99_irc_parse(text, &ev)) {
                continue;
            }
            if (ev.reply[0]) {
                irc_send_line(fd, ev.reply);
            }
            if (ev.type == DC99_IRCE_READY) {
                set_state(c, DC99_CHAT_ON, "");
                push(c, DC99_ROW_SYS, "", "Joined #dc99");
            } else if (ev.type == DC99_IRCE_ERROR) {
                push(c, DC99_ROW_ERR, "", ev.text);
                set_state(c, DC99_CHAT_FAILED, ev.text);
                failed = 1;
                break;
            } else {
                irc_row(c, &ev);
            }
        }
        mutex_lock(&out_lock); /* a line the user typed */
        if (out_pending && !failed) {
            char say[480];
            char own[420];
            snprintf(own, sizeof(own), "%s", out_line);
            out_pending = 0;
            mutex_unlock(&out_lock);
            if (dc99_irc_say_line(own, say, sizeof(say)) == 0) {
                if (irc_send_line(fd, say) < 0) {
                    set_state(c, DC99_CHAT_FAILED, "The message did not go out.");
                    failed = 1;
                } else { /* the server does not repeat it to the sender */
                    char prefix[40];
                    char shown[420];
                    snprintf(shown, sizeof(shown), "%s", own);
                    dc99_chat_clean(shown);
                    snprintf(prefix, sizeof(prefix), "%s: ", irc_nick);
                    push(c, DC99_ROW_SELF, prefix, shown);
                }
            }
        } else {
            mutex_unlock(&out_lock);
        }
    }
    if (!failed) {
        irc_send_line(fd, "QUIT :bye");
    }
    close(fd);
    if (!failed) {
        set_state(c, DC99_CHAT_OFF, "");
    }
    return NULL;
}

void
dc99_irc_start(const char* nick, const char* pass) {
    if (irc_thread != NULL) {
        dc99_chat_status_t st;
        dc99_chat_status(DC99_CHAT_IRC, &st);
        if (st.state == DC99_CHAT_CONNECTING || st.state == DC99_CHAT_ON) {
            return;
        }
        dc99_irc_stop(); /* the last one ended: collect it */
    }
    snprintf(irc_nick, sizeof(irc_nick), "%s", nick);
    snprintf(irc_pass, sizeof(irc_pass), "%s", pass);
    mutex_lock(&out_lock);
    out_pending = 0;
    mutex_unlock(&out_lock);
    irc_stop = 0;
    irc_thread = thd_create(false, irc_main, NULL);
    if (irc_thread == NULL) {
        set_state(&chans[DC99_CHAT_IRC], DC99_CHAT_FAILED, "Could not start the chat.");
    }
}

int
dc99_irc_send(const char* text) {
    dc99_chat_status_t st;
    int rc = -1;

    dc99_chat_status(DC99_CHAT_IRC, &st);
    if (st.state != DC99_CHAT_ON) {
        return -1;
    }
    mutex_lock(&out_lock);
    if (!out_pending) {
        snprintf(out_line, sizeof(out_line), "%s", text);
        out_pending = 1;
        rc = 0;
    }
    mutex_unlock(&out_lock);
    return rc;
}

void
dc99_irc_stop(void) {
    if (irc_thread != NULL) {
        irc_stop = 1;
        thd_join(irc_thread, NULL);
        irc_thread = NULL;
    }
}

void
dc99_chat_stop(void) {
    dc99_irc_stop();
    if (kn_thread != NULL) {
        kn_stop = 1;
        thd_join(kn_thread, NULL);
        kn_thread = NULL;
    }
    set_state(&chans[DC99_CHAT_KOSNET], DC99_CHAT_OFF, "");
    set_state(&chans[DC99_CHAT_IRC], DC99_CHAT_OFF, "");
}
