/*
 * File: lb_fetch.c
 * Project: openmenu
 * See lb_fetch.h. The HTTP helpers follow news_fetch.c.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arch/timer.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "backend/dcnow_net.h"
#include "backend/lb_fetch.h"

#define BODY_MAX       (128 * 1024)
#define WEB_TIMEOUT_MS 30000

/* The boards: DreamcastLive's page of PSO, and the boards of Vanishing Point and Quake III that its pages show in frames. The hosts are asked
 * on plain HTTP (the console has no TLS here): a site that sends the console to https is reported as that. */
static const lb_source_t sources[] = {
    {"Phantasy Star Online", "Challenge and Attack", "dreamcastlive.net", "/pso-arena/"},
    {"Vanishing Point", "Weekly Challenge", "shumania.ddns.net", "/vp_challenges.html"},
    {"Vanishing Point", "Track Records", "shumania.ddns.net", "/vp_track_board.html"},
    {"Vanishing Point", "Rankings 2026-now", "shumania.ddns.net", "/vp_top100.html"},
    {"Vanishing Point", "Rankings 2003-now", "shumania.ddns.net", "/vp_combined_top100.html"},
    {"Quake III Arena", "Player Rankings", "www.uefa.dreampipe.net", "/Quake3/rankings.php"},
};
#define SOURCES ((int)(sizeof(sources) / sizeof(sources[0])))

static mutex_t lb_mutex = MUTEX_INITIALIZER;
static lb_status_t status;
static lb_line_t lines[LB_LINES_MAX];
static lb_line_t parsed[LB_LINES_MAX];
static kthread_t* worker = NULL;
static volatile int abort_requested = 0;
static volatile int worker_done = 0;
static int want_source, restart_pending;
static char body[BODY_MAX + 1];

int
lb_source_count(void) {
    return SOURCES;
}

const lb_source_t*
lb_source(int i) {
    return i >= 0 && i < SOURCES ? &sources[i] : NULL;
}

static void
set_line(lb_state_t state, const char* text) {
    mutex_lock(&lb_mutex);
    status.state = state;
    snprintf(status.line, sizeof(status.line), "%s", text);
    mutex_unlock(&lb_mutex);
}

static int
wait_socket(int fd, short events, uint64_t deadline) {
    struct pollfd pfd;
    while (timer_ms_gettime64() < deadline) {
        if (abort_requested) {
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

/* One plain HTTP GET into body[]. Returns the status code, -1 on any failure and -2 when aborted. */
static int
http_get(const struct sockaddr_in* addr, const char* host, const char* path, uint64_t timeout_ms, size_t* used) {
    char request[192];
    uint64_t deadline = timer_ms_gettime64() + timeout_ms;
    size_t sent = 0, len;
    int code = -1, fd, rc;

    *used = 0;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    rc = connect(fd, (const struct sockaddr*)addr, sizeof(*addr));
    if (rc < 0 && errno != EWOULDBLOCK && errno != EINPROGRESS && errno != EAGAIN) {
        close(fd);
        return -1;
    }
    rc = wait_socket(fd, POLLWRNORM, deadline);
    if (rc <= 0 || (rc & (POLLHUP | POLLERR))) {
        close(fd);
        return rc < 0 ? -2 : -1;
    }
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: openMenu\r\nAccept: text/html\r\nConnection: close\r\n\r\n",
             path, host);
    len = strlen(request);
    while (sent < len) {
        ssize_t n = send(fd, request + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        rc = (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) ? wait_socket(fd, POLLWRNORM, deadline) : 0;
        if (rc <= 0) {
            close(fd);
            return rc < 0 ? -2 : -1;
        }
    }
    for (;;) {
        rc = wait_socket(fd, POLLRDNORM, deadline);
        if (rc <= 0) {
            close(fd);
            return rc < 0 ? -2 : -1;
        }
        ssize_t got = recv(fd, body + *used, BODY_MAX - *used, 0);
        if (got > 0) {
            *used += (size_t)got;
            if (*used == BODY_MAX) {
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
    body[*used] = '\0';
    if (sscanf(body, "HTTP/%*d.%*d %d", &code) != 1) {
        return -1;
    }
    return code;
}

static void
run_fetch(int src) {
    struct sockaddr_in addr;
    size_t used;
    char line[48];
    const lb_source_t* s = &sources[src];

    snprintf(line, sizeof(line), "Reading %.30s...", s->host);
    set_line(LB_RUNNING, line);
    if (!dcnow_resolve(s->host, 80, &addr)) {
        snprintf(line, sizeof(line), "%.30s could not be found", s->host);
        set_line(LB_FAILED, line);
        return;
    }
    int code = http_get(&addr, s->host, s->path, WEB_TIMEOUT_MS, &used);
    if (code == -2 || abort_requested) {
        set_line(LB_IDLE, "");
        return;
    }
    if (code != 200) {
        snprintf(line, sizeof(line), code >= 300 && code < 400 ? "%.30s wants https" : "%.30s did not answer", s->host);
        set_line(LB_FAILED, line);
        return;
    }
    const char* html = strstr(body, "\r\n\r\n");
    int n = lb_parse_html(html ? html + 4 : body, parsed, LB_LINES_MAX);
    if (n == 0) {
        set_line(LB_FAILED, "No scores found on the page");
        return;
    }
    mutex_lock(&lb_mutex);
    memcpy(lines, parsed, sizeof(parsed[0]) * (size_t)n);
    status.count = n;
    status.source = src;
    status.generation++;
    status.state = LB_DONE;
    status.line[0] = '\0';
    mutex_unlock(&lb_mutex);
}

static void*
lb_main(void* param) {
    (void)param;
    thd_set_prio(thd_get_current(), PRIO_DEFAULT + 4); /* below the menu: a fetch never takes the frame's time */
    run_fetch(want_source);
    worker_done = 1;
    return NULL;
}

static void
join_worker(void) {
    if (worker != NULL) {
        thd_join(worker, NULL);
        worker = NULL;
    }
}

void
lb_fetch_start(int source) {
    if (source < 0 || source >= SOURCES) {
        return;
    }
    if (worker != NULL && !worker_done) {
        restart_pending = 1; /* one is still ending (aborted or not): this one starts when it has */
        want_source = source;
        abort_requested = 1;
        return;
    }
    restart_pending = 0;
    join_worker();
    abort_requested = 0;
    worker_done = 0;
    want_source = source;
    mutex_lock(&lb_mutex);
    status.count = 0; /* the lines of another board are not this one's */
    status.source = source;
    mutex_unlock(&lb_mutex);
    set_line(LB_RUNNING, "Fetching...");
    worker = thd_create(0, lb_main, NULL);
    if (worker == NULL) {
        set_line(LB_FAILED, "Could not start the fetch");
    }
}

void
lb_fetch_abort(void) {
    abort_requested = 1;
    restart_pending = 0;
    mutex_lock(&lb_mutex);
    if (status.state == LB_RUNNING) {
        status.state = LB_IDLE;
    }
    mutex_unlock(&lb_mutex);
}

void
lb_fetch_poll(lb_status_t* out) {
    if (worker != NULL && worker_done) {
        join_worker();
        if (restart_pending) {
            restart_pending = 0;
            lb_fetch_start(want_source);
        }
    }
    mutex_lock(&lb_mutex);
    *out = status;
    mutex_unlock(&lb_mutex);
}

int
lb_fetch_copy(lb_line_t* out, int max) {
    mutex_lock(&lb_mutex);
    int n = status.count < max ? status.count : max;
    memcpy(out, lines, sizeof(lines[0]) * (size_t)n);
    mutex_unlock(&lb_mutex);
    return n;
}
