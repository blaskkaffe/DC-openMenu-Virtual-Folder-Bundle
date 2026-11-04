#include <stdio.h>
#include <string.h>

#include <dc/maple.h>
#include <kos/genwait.h>

#include "dcapi.h"

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

again:
    if (maple_frame_lock(&dev->frame) < 0) {
        return -1;
    }

    maple_frame_init(&dev->frame);
    send_buf = (uint32_t*)dev->frame.recv_buf;
    send_buf[0] = MAPLE_FUNC_MEMCARD;
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
    if (resp->response == MAPLE_RESPONSE_AGAIN) {
        goto again;
    }
    if (resp->response != MAPLE_RESPONSE_DATATRF) {
        return resp->response == MAPLE_RESPONSE_OK ? -2 : -1;
    }
    {
        size_t len = (size_t)resp->data_len * 4;

        if (len < 4) {
            return -3;
        }
        len -= 4; /* the function code */
        if (len > ans_cap - 1) {
            len = ans_cap - 1;
        }
        memcpy(ans, &resp_buf[8], len);
        ans[len] = 0;
        /* NUL padding to the word: the text ends at the first NUL */
        len = strlen(ans);
        return dcapi_answer_ok(ans) ? (int)len : -3;
    }
}

int
dcapi_get_hello(maple_device_t* dev, dcapi_hello_t* h) {
    char req[32], ans[DCAPI_ANS_MAX + 1];
    int r;

    h->name[0] = h->version[0] = 0;
    if (dcapi_build(req, sizeof(req), "get hello\n") < 0) {
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
