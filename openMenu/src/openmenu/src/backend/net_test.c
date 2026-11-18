/* net_test: see net_test.h. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arch/timer.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "backend/dcnow_net.h"
#include "backend/net_test.h"
#include "backend/netsel.h"

#define TEST_HOST "dreamcast.online" /* the Dreamcast Now! server the player list comes from */
#define STEP_MS 8000

static mutex_t lines_mutex = MUTEX_INITIALIZER;
static nt_line_t lines[NT_LINES];
static int count;
static volatile int running;
static kthread_t* worker;
static volatile int run_online, run_busy; /* what the screen saw of the connection when the test was asked for */

static void
push(int level, const char* fmt, const char* a, int n) {
    mutex_lock(&lines_mutex);
    if (count < NT_LINES) {
        snprintf(lines[count].text, NT_TEXT, fmt, a ? a : "", n);
        lines[count].level = level;
        count++;
    }
    mutex_unlock(&lines_mutex);
}

/* A TCP connect with a timer. Returns the milliseconds, or -1. */
static int
connect_ms(const struct sockaddr_in* addr) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int rc;
    uint64_t t0 = timer_ms_gettime64();
    if (fd < 0) {
        return -1;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    rc = connect(fd, (const struct sockaddr*)addr, sizeof(*addr));
    if (rc < 0 && errno != EWOULDBLOCK && errno != EINPROGRESS && errno != EAGAIN) {
        close(fd);
        return -1;
    }
    while (timer_ms_gettime64() - t0 < STEP_MS) {
        struct pollfd pfd = {fd, POLLWRNORM, 0};
        if (poll(&pfd, 1, 100) > 0) {
            close(fd);
            return (pfd.revents & (POLLHUP | POLLERR)) ? -1 : (int)(timer_ms_gettime64() - t0);
        }
    }
    close(fd);
    return -1;
}

static void*
test_main(void* param) {
    int online = run_online;
    int busy = run_busy;
    (void)param;
    thd_set_prio(thd_get_current(), PRIO_DEFAULT + 4);
    static const dcnow_device_t ways[] = {DCNOW_DEV_BBA, DCNOW_DEV_LAN, DCNOW_DEV_W5500, DCNOW_DEV_SERIAL, DCNOW_DEV_MODEM};

    if (!busy) { /* local: is the hardware there */
        unsigned mask = dcnow_net_probe_all();
        for (unsigned i = 0; i < sizeof(ways) / sizeof(ways[0]); i++) {
            int found = (mask & netsel_bit(ways[i])) != 0;
            char name[40];
            snprintf(name, sizeof(name), "%s", dcnow_device_name(ways[i]));
            push(found ? NT_OK : NT_INFO, found ? "Found: %s" : "None: %s", name, 0);
        }
        if (!mask) {
            push(NT_WARN, "Nothing found: see the cable and the seating", NULL, 0);
        }
        push(NT_INFO, "Connect, then run the test again for the link", NULL, 0);
    } else {
        dcnow_device_t used = dcnow_device_hint();
        push(NT_OK, "In use: %s", dcnow_device_name(used), 0);
        if (!online) {
            push(NT_INFO, "Still connecting: wait, then test again", NULL, 0);
        } else {
            uint8_t ip[4];
            if (dcnow_conn_address(ip)) { /* local: the address the stack has */
                char a[24];
                snprintf(a, sizeof(a), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
                push(NT_OK, "Address %s", a, 0);
            } else {
                push(NT_FAIL, "No address on the link", NULL, 0);
            }
            struct sockaddr_in addr;
            uint64_t t0 = timer_ms_gettime64();
            if (dcnow_resolve(TEST_HOST, 80, &addr)) { /* server: the DNS server answers */
                push(NT_OK, "DNS lookup %d ms", NULL, (int)(timer_ms_gettime64() - t0));
                int ms = connect_ms(&addr);
                if (ms >= 0) { /* server: a TCP connect to Dreamcast Now! */
                    push(ms < 400 ? NT_OK : NT_WARN, "Server connect %d ms", NULL, ms);
                } else {
                    push(NT_FAIL, "The server did not answer", NULL, 0);
                }
            } else {
                push(NT_FAIL, "DNS lookup failed: check the DNS server", NULL, 0);
            }
        }
    }
    push(NT_INFO, "Port forwarding: not tested (needs an outside service)", NULL, 0);
    running = 0;
    return NULL;
}

void
net_test_start(int busy, int online) {
    run_busy = busy;
    run_online = online;
    if (running) {
        return;
    }
    if (worker) {
        thd_join(worker, NULL);
        worker = NULL;
    }
    mutex_lock(&lines_mutex);
    count = 0;
    mutex_unlock(&lines_mutex);
    running = 1;
    worker = thd_create(false, test_main, NULL);
    if (!worker) {
        running = 0;
        push(NT_FAIL, "Could not start the test", NULL, 0);
    }
}

int
net_test_running(void) {
    return running;
}

int
net_test_lines(nt_line_t* out, int max) {
    int n;
    mutex_lock(&lines_mutex);
    n = count < max ? count : max;
    memcpy(out, lines, (size_t)n * sizeof(lines[0]));
    mutex_unlock(&lines_mutex);
    return n;
}
