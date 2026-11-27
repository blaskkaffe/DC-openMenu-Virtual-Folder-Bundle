#include <stdio.h>
#include <string.h>

#include <dc/maple.h>
#include <kos/dbglog.h>
#include <kos/genwait.h>
#include <kos/thread.h>
#include <arch/timer.h>

#include "dcapi.h"

/* The DC API is spoken to the unit on the bus of a port (unit 0: the controller, or whatever the adapter presents there), never to a
 * memory card unit. */

/* The reply is copied here by the Maple callback: 4 byte header, the function code, the text */
static uint8_t resp_buf[4 + 4 + DCAPI_ANS_MAX + 8];

static void
dcapi_reply(struct maple_state_str*, maple_frame_t* frm) {
    maple_response_t* resp = (maple_response_t*)frm->recv_buf;
    size_t n = 4;

    if (resp->response == MAPLE_RESPONSE_DATATRF) {
        n += (size_t)resp->data_len * 4;
    }
    if (n > sizeof(resp_buf)) {
        n = sizeof(resp_buf);
    }
    memcpy(resp_buf, resp, n);

    maple_frame_unlock(frm);
    genwait_wake_all(frm);
}

static volatile int async_state; /* the request without waiting (dcapi_begin): 0 idle, 1 sent, 2 answered */
static maple_device_t* async_dev;

/* The text of an answer in `buf` (header, function code, text): its length, or negative */
static int
answer_text(const uint8_t* buf, char* ans, size_t ans_cap) {
    const maple_response_t* resp = (const maple_response_t*)buf;
    size_t len;

    if (resp->response != MAPLE_RESPONSE_DATATRF) {
        return resp->response == MAPLE_RESPONSE_OK ? -2 : -1;
    }
    len = (size_t)resp->data_len * 4;
    if (len < 4) {
        return -3;
    }
    len -= 4; /* the function code */
    if (len > ans_cap - 1) {
        len = ans_cap - 1;
    }
    memcpy(ans, &buf[8], len);
    ans[len] = 0;
    /* NUL padding to the word: the text ends at the first NUL */
    len = strlen(ans);
    /* "dc1" first; an answer that starts with its ok / err lines straight away is taken too */
    return dcapi_answer_ok(ans) || !strncmp(ans, "ok ", 3) || !strncmp(ans, "err ", 4) ? (int)len : -3;
}

int
dcapi_request(maple_device_t* dev, const char* req, char* ans, size_t ans_cap) {
    maple_response_t* resp;
    uint32_t* send_buf;
    size_t n = strlen(req);
    size_t words = (n + 3) / 4;

    if (!dev || !ans || !ans_cap || n > DCAPI_REQ_MAX) {
        return -1;
    }
    ans[0] = 0;
    if (async_state == 1 && async_dev == dev) { /* an answer still on its way on this frame: wait for it (it is dropped) */
        for (int i = 0; i < 40 && async_state == 1; i++) {
            thd_sleep(5);
        }
        char drop[8];
        dcapi_poll(drop, sizeof(drop));
    }

    int again_n = 0;
again:
    /* the frame of a controller unit is also the console's own poll of it, busy about once a frame: wait a little for it instead of failing */
    for (int tries = 0; maple_frame_lock(&dev->frame) < 0; tries++) {
        if (tries >= 20) {
            return -1;
        }
        thd_sleep(5);
    }

    maple_frame_init(&dev->frame);
    send_buf = (uint32_t*)dev->frame.recv_buf;
    send_buf[0] = dev->info.functions; /* the unit on the bus (the controller, keyboard, mouse ...): what it is, the firmware echoes it */
    memset(&dev->frame.recv_buf[4], 0, words * 4); /* text NUL padded to whole words, like the game id */
    memcpy(&dev->frame.recv_buf[4], req, n);
    dev->frame.cmd = DCAPI_CMD;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 1 + words;
    dev->frame.callback = dcapi_reply;
    dev->frame.send_buf = send_buf;
    memset(resp_buf, 0, 4);
    maple_queue_frame(&dev->frame);

    if (genwait_wait(&dev->frame, "dcapi_request", 200, NULL) < 0) {
        if (dev->frame.state != MAPLE_FRAME_UNSENT) {
            /* It's probably never coming back, so just unlock the frame */
            dev->frame.state = MAPLE_FRAME_VACANT;
            dbglog(DBG_ERROR, "dcapi_request: timeout to unit %c%c\n", dev->port + 'A', dev->unit + '0');
            return -1;
        }
    }

    resp = (maple_response_t*)resp_buf;
    if (resp->response == MAPLE_RESPONSE_AGAIN && ++again_n < 5) { /* a device that keeps saying so would hang the menu */
        goto again;
    }
    return answer_text(resp_buf, ans, ans_cap);
}

/* ---- the same request without waiting (dcapi_begin / dcapi_poll): the answer is picked up on a later frame, so a screen that polls
 * the adapter (the Joy-Con heights) does not stand still for the bus round trip each time */
static uint8_t async_buf[4 + 4 + DCAPI_ANS_MAX + 8];
static uint64_t async_t0;

static void
async_reply(struct maple_state_str*, maple_frame_t* frm) {
    maple_response_t* resp = (maple_response_t*)frm->recv_buf;
    size_t n = 4;

    if (resp->response == MAPLE_RESPONSE_DATATRF) {
        n += (size_t)resp->data_len * 4;
    }
    if (n > sizeof(async_buf)) {
        n = sizeof(async_buf);
    }
    memcpy(async_buf, resp, n);
    maple_frame_unlock(frm);
    async_state = 2;
}

int
dcapi_begin(maple_device_t* dev, const char* req) {
    size_t n = strlen(req), words = (n + 3) / 4;
    uint32_t* send_buf;

    if (async_state == 2) {
        async_state = 0; /* an answer nobody picked up (its screen was left): dropped */
    } else if (async_state == 1) {
        char drop[8];
        dcapi_poll(drop, sizeof(drop)); /* gives up on one that is overdue */
    }
    if (!dev || n > DCAPI_REQ_MAX || async_state != 0 || maple_frame_lock(&dev->frame) < 0) {
        return -1; /* one at a time; the frame busy with the console's own poll: next frame */
    }
    maple_frame_init(&dev->frame);
    send_buf = (uint32_t*)dev->frame.recv_buf;
    send_buf[0] = dev->info.functions;
    memset(&dev->frame.recv_buf[4], 0, words * 4);
    memcpy(&dev->frame.recv_buf[4], req, n);
    dev->frame.cmd = DCAPI_CMD;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 1 + words;
    dev->frame.callback = async_reply;
    dev->frame.send_buf = send_buf;
    memset(async_buf, 0, 4);
    async_dev = dev;
    async_t0 = timer_ms_gettime64();
    async_state = 1;
    maple_queue_frame(&dev->frame);
    return 0;
}

int
dcapi_poll(char* ans, size_t ans_cap) {
    if (async_state == 2) {
        async_state = 0;
        return answer_text(async_buf, ans, ans_cap);
    }
    if (async_state == 1 && timer_ms_gettime64() - async_t0 > 200) {
        if (async_dev->frame.state != MAPLE_FRAME_UNSENT) {
            async_dev->frame.state = MAPLE_FRAME_VACANT; /* no answer is coming: the frame is the console's again */
            async_state = 0;
            return -1;
        }
    }
    return async_state == 1 ? 0 : -1;
}

int
dcapi_get_hello(maple_device_t* dev, dcapi_hello_t* h) {
    char req[64], ans[DCAPI_ANS_MAX + 1];
    int r;

    h->name[0] = h->version[0] = 0;
    if (dcapi_build(req, sizeof(req), "get hello.name\nget hello.version\n") < 0) { /* the keys by name: every device has them */
        return -1;
    }
    r = dcapi_request(dev, req, ans, sizeof(ans));
    if (r < 0) {
        return r;
    }
    dcapi_answer(ans, "hello.name", h->name, sizeof(h->name));
    dcapi_answer(ans, "hello.version", h->version, sizeof(h->version));
    return 0;
}

int
dcapi_set(maple_device_t* dev, const char* key, const char* value, char* reply, size_t reply_cap) {
    char ops[96], req[128], ans[DCAPI_ANS_MAX + 1], val[64];
    int r;

    if (snprintf(ops, sizeof(ops), "set %s %s\n", key, value) >= (int)sizeof(ops) || dcapi_build(req, sizeof(req), ops) < 0) {
        return -1;
    }
    r = dcapi_request(dev, req, ans, sizeof(ans));
    if (r < 0) {
        return r;
    }
    r = dcapi_answer(ans, key, val, sizeof(val));
    if (reply && reply_cap) {
        snprintf(reply, reply_cap, "%s", r < 0 ? "no answer" : val);
    }
    return r == 1 ? 1 : 0;
}
