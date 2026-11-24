#include <stdio.h>
#include <string.h>

#include <kos/genwait.h>

#include "p2m_api.h"
#include "vm2_api.h"

#define P2M_REQ_MAX 252
#define P2M_ANS_MAX 256

static uint8_t answer_buf[4 + 4 + P2M_ANS_MAX];
static uint32_t request_buf[1 + P2M_REQ_MAX / 4 + 1];

static void
p2m_reply(struct maple_state_str* st, maple_frame_t* frm) {
    (void)st;
    maple_response_t* resp = (maple_response_t*)frm->recv_buf;
    memset(answer_buf, 0, sizeof(answer_buf));
    answer_buf[0] = (uint8_t)resp->response;
    answer_buf[1] = resp->data_len;
    if (resp->response == MAPLE_RESPONSE_DATATRF) {
        size_t n = (size_t)resp->data_len * 4;
        if (n > sizeof(answer_buf) - 4) {
            n = sizeof(answer_buf) - 4;
        }
        memcpy(&answer_buf[4], resp->data, n);
    }
    maple_frame_unlock(frm);
    genwait_wake_all(frm);
}

int
p2m_request(maple_device_t* dev, const char* lines, char* out, size_t cap) {
    if (!dev || !out || !cap) {
        return -1;
    }
    out[0] = '\0';
    char text[P2M_REQ_MAX + 1];
    int n = snprintf(text, sizeof(text), "dc1\n%s", lines);
    if (n <= 0 || n > P2M_REQ_MAX) {
        return -1;
    }
    if (maple_frame_lock(&dev->frame) < 0) {
        return -1;
    }
    maple_frame_init(&dev->frame);
    memset(request_buf, 0, sizeof(request_buf));
    request_buf[0] = MAPLE_FUNC_MEMCARD;
    memcpy(&request_buf[1], text, (size_t)n);
    dev->frame.cmd = 0x22;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = (uint32_t)(1 + (n + 3) / 4);
    dev->frame.callback = p2m_reply;
    dev->frame.send_buf = request_buf;
    maple_queue_frame(&dev->frame);
    if (genwait_wait(&dev->frame, "p2m_request", 300, NULL) < 0) {
        if (dev->frame.state != MAPLE_FRAME_UNSENT) {
            dev->frame.state = MAPLE_FRAME_VACANT; /* never coming back: release the frame */
            return -1;
        }
    }
    if ((int8_t)answer_buf[0] != MAPLE_RESPONSE_DATATRF) {
        return -1;
    }
    /* answer_buf[4..7] is the function word, the text follows */
    const char* t = (const char*)&answer_buf[8];
    size_t len = strnlen(t, sizeof(answer_buf) - 8);
    if (len >= 4 && !strncmp(t, "dc1\n", 4)) {
        t += 4;
        len -= 4;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, t, len);
    out[len] = '\0';
    return 0;
}

int
p2m_answer_value(const char* answer, const char* key, char* value, size_t cap) {
    size_t kl = strlen(key);
    const char* p = answer;
    while (*p) {
        const char* eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        if (ll > kl + 3 && !strncmp(p, "ok ", 3) && !strncmp(p + 3, key, kl) && p[3 + kl] == ' ') {
            size_t vl = ll - (kl + 4);
            if (vl >= cap) {
                vl = cap - 1;
            }
            memcpy(value, p + 4 + kl, vl);
            value[vl] = '\0';
            return 1;
        }
        if (!eol) {
            break;
        }
        p = eol + 1;
    }
    return 0;
}

int
p2m_get(maple_device_t* dev, const char* key, char* value, size_t cap) {
    char req[96], ans[P2M_ANS_MAX + 1];
    snprintf(req, sizeof(req), "get %s\n", key);
    if (p2m_request(dev, req, ans, sizeof(ans)) < 0) {
        return -1;
    }
    return p2m_answer_value(ans, key, value, cap) ? 0 : -1;
}

int
p2m_set(maple_device_t* dev, const char* key, const char* value) {
    char req[128], ans[P2M_ANS_MAX + 1], got[32];
    snprintf(req, sizeof(req), "set %s %s\n", key, value);
    if (p2m_request(dev, req, ans, sizeof(ans)) < 0) {
        return -1;
    }
    return p2m_answer_value(ans, key, got, sizeof(got)) ? 0 : -1;
}

maple_device_t*
p2m_find(void) {
    for (int i = 0; i < MAPLE_PORT_COUNT * MAPLE_UNIT_COUNT; i++) {
        maple_device_t* dev = maple_enum_type(i, MAPLE_FUNC_MEMCARD);
        if (dev && !strcmp(get_vmu_type_name(dev), "Pico2Maple")) {
            return dev;
        }
    }
    return NULL;
}
