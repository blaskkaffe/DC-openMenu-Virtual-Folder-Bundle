/* See picolink.h. */

#include "backend/picolink.h"

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

#define PL_HOST "192.168.98.1"
#define PL_PORT 80
#define PL_RESP_MAX 3200 /* a scan list is up to 24 lines of about 50 */

enum { JOB_PROBE = 1, JOB_SELECT, JOB_SCAN, JOB_JOIN };

static mutex_t lock = MUTEX_INITIALIZER;
static picolink_status_t st;
static kthread_t* worker = NULL;
static volatile int stop_flag = 0;
static int job_kind, job_arg, job_index;
static char job_name[PL_NAME_MAX + 1];
static char job_pass[72];

static void
ensure_init(void) {
    static int done;
    if (!done) {
        done = 1;
        pl_info_init(&st.info);
    }
}

static void
join_worker(void) {
    if (worker) {
        thd_join(worker, NULL);
        worker = NULL;
    }
}

/* One POST of `body` to /dcapi; the answer's body into resp. Returns the HTTP status, -1 when nothing answered, -2 when stopped. */
static int
http_post(const char* body, char* resp, size_t cap, uint32_t timeout_ms) {
    char head[220];
    size_t blen = strlen(body), got = 0;
    uint64_t deadline = timer_ms_gettime64() + timeout_ms;
    int fd = dc99_net_open(PL_HOST, PL_PORT, &stop_flag);
    int code = -1;

    if (fd < 0) {
        return fd == -2 ? -2 : -1;
    }
    snprintf(head, sizeof(head), "POST /dcapi HTTP/1.0\r\nHost: " PL_HOST "\r\nContent-Type: text/plain\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
             (unsigned)blen);
    {
        const char* parts[2] = {head, body};
        size_t lens[2] = {strlen(head), blen};
        for (int p = 0; p < 2; p++) {
            size_t sent = 0;
            while (sent < lens[p]) {
                ssize_t n = send(fd, parts[p] + sent, lens[p] - sent, 0);
                if (n > 0) {
                    sent += (size_t)n;
                } else if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) && dc99_net_wait(fd, POLLWRNORM, deadline, &stop_flag) > 0) {
                    continue;
                } else {
                    close(fd);
                    return stop_flag ? -2 : -1;
                }
            }
        }
    }
    while (got + 1 < cap) {
        ssize_t n = recv(fd, resp + got, cap - 1 - got, 0);
        if (n > 0) {
            got += (size_t)n;
        } else if (n == 0) {
            break; /* the box closed: that is the end of the answer */
        } else if ((errno == EWOULDBLOCK || errno == EAGAIN) && dc99_net_wait(fd, POLLRDNORM, deadline, &stop_flag) > 0) {
            continue;
        } else {
            break;
        }
    }
    close(fd);
    resp[got] = '\0';
    if (stop_flag) {
        return -2;
    }
    if (got > 12 && !strncmp(resp, "HTTP/", 5)) {
        char* sp = strchr(resp, ' ');
        char* body_at = strstr(resp, "\r\n\r\n");
        code = sp ? atoi(sp + 1) : -1;
        if (body_at) {
            memmove(resp, body_at + 4, strlen(body_at + 4) + 1);
        } else {
            resp[0] = '\0';
        }
    }
    return code;
}

static void
finish(int reached, const char* note, const pl_info_t* info) {
    mutex_lock(&lock);
    st.busy = 0;
    st.reached = reached;
    snprintf(st.note, sizeof(st.note), "%s", note ? note : "");
    if (info) {
        st.info = *info;
    }
    st.generation++;
    mutex_unlock(&lock);
}

static void*
worker_main(void* arg) {
    static char resp[PL_RESP_MAX];
    char req[256];
    pl_info_t info;
    size_t n = 0;
    uint32_t timeout = 6000;
    int code;
    (void)arg;
    thd_set_prio(thd_get_current(), PRIO_DEFAULT + 4);

    mutex_lock(&lock);
    info = st.info;
    mutex_unlock(&lock);
    switch (job_kind) {
        case JOB_PROBE: n = pl_req_status(req, sizeof(req)); break;
        case JOB_SELECT: n = pl_req_select(req, sizeof(req), job_arg); break;
        case JOB_SCAN: n = pl_req_scan(req, sizeof(req)); timeout = 15000; break; /* the radio is away for 2-3 s */
        default: n = pl_req_join(req, sizeof(req), job_index, job_name, job_pass); timeout = 10000; break;
    }
    memset(job_pass, 0, sizeof(job_pass));
    if (n == 0) {
        finish(st.reached, "Not a password the gateway takes", NULL);
        memset(req, 0, sizeof(req));
        return NULL;
    }
    code = http_post(req, resp, sizeof(resp), timeout);
    memset(req, 0, sizeof(req)); /* it may hold the password */
    if (code == -2) {
        finish(st.reached, "", NULL);
    } else if (code != 200) {
        finish(0, code < 0 ? "No gateway answered" : "The gateway refused the request", NULL);
    } else if (pl_parse(resp, &info) < 0) {
        finish(0, "That is not a gateway", NULL);
    } else {
        finish(1, info.err, &info);
    }
    return NULL;
}

static void
start(int kind) {
    ensure_init();
    join_worker();
    stop_flag = 0;
    job_kind = kind;
    mutex_lock(&lock);
    st.busy = 1;
    st.note[0] = '\0';
    mutex_unlock(&lock);
    worker = thd_create(0, worker_main, NULL);
}

void
picolink_probe(void) {
    if (!st.busy) {
        start(JOB_PROBE);
    }
}

void
picolink_select(int dcnet) {
    if (!st.busy) {
        job_arg = dcnet != 0;
        start(JOB_SELECT);
    }
}

void
picolink_scan(void) {
    if (!st.busy) {
        start(JOB_SCAN);
    }
}

void
picolink_join(int index, const char* name, const char* pass) {
    if (!st.busy) {
        job_index = index;
        snprintf(job_name, sizeof(job_name), "%s", name ? name : "");
        snprintf(job_pass, sizeof(job_pass), "%s", pass ? pass : "");
        start(JOB_JOIN);
    }
}

void
picolink_status(picolink_status_t* out) {
    ensure_init();
    mutex_lock(&lock);
    *out = st;
    mutex_unlock(&lock);
}

void
picolink_stop(void) {
    stop_flag = 1;
    join_worker();
    mutex_lock(&lock);
    st.busy = 0;
    mutex_unlock(&lock);
}
