#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <dc/maple.h>
#include <kos/genwait.h>

#include "vm2_api.h"

static uint8_t recv_buff[196];

/* The devices that take the game id (Maple 0x21), by the marker in the first 16 bytes of their extended device info, and the
 * name the menu gives them. iBlueControlMod's firmware answers 0x21 too and puts the "dc1" capability bits after its marker. */
static const struct {
    const char* marker;
    const char* name;
} known[] = {
    {"VM2 by Dreamware", "VM2"},
    {"8BITMODS VMUPro ", "VMUPro"},
    {"USB RP2040 EMU  ", "USB4MAPLE"},
    {"Pico2Maple USBBT", "Pico2Maple"},
    {"iBlueControlMod ", "iBlueControlMod"},
};

static const char*
known_name(const char* extended) {
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (!strncasecmp(extended, known[i].marker, 16)) {
            return known[i].name;
        }
    }
    return NULL;
}

static void
vbl_allinfo_callback(struct maple_state_str*, maple_frame_t* frm) {
    maple_response_t* resp;

    /* So.. did we get a response? */
    resp = (maple_response_t*)frm->recv_buf;

    if (resp->response == MAPLE_RESPONSE_ALLINFO) {
        /* Copy in the new buff */
        memcpy(recv_buff, resp, 196);
    }

    maple_frame_unlock(frm);
    genwait_wake_all(frm);
}

/* Send a ALLINFO command for the given port/unit */
static int
send_allinfo(maple_device_t* dev) {
    /* Reserve access. If it is not granted, give up rather than wait. */
    if (maple_frame_lock(&dev->frame) < 0) {
        return -1;
    }

    /* Set up the autodetect frame to probe a new device. */
    maple_frame_init(&dev->frame);
    dev->frame.cmd = MAPLE_COMMAND_ALLINFO;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.callback = vbl_allinfo_callback;
    maple_queue_frame(&dev->frame);

    /* Wait for the VM2 to accept it */
    if (genwait_wait(&dev->frame, "vmu_get_allinfo", 200, NULL) < 0) {
        if (dev->frame.state != MAPLE_FRAME_UNSENT) {
            /* It's probably never coming back, so just unlock the frame */
            dev->frame.state = MAPLE_FRAME_VACANT;
            dbglog(DBG_ERROR, "send_allinfo: timeout to unit %c%c\n", dev->port + 'A', dev->unit + '0');
            return MAPLE_ETIMEOUT;
        }
    }

    return MAPLE_EOK;
}

static void
vm2_reply(struct maple_state_str*, maple_frame_t* frm) {
    maple_response_t* resp;

    /* So.. did we get a response? */
    resp = (maple_response_t*)frm->recv_buf;

    if (resp->response != MAPLE_RESPONSE_OK) {
        /* printf("maple: bad response %d on device, wait ACK\n", resp->response); */
    }

    memcpy(recv_buff, resp, 4);

    maple_frame_unlock(frm);
    genwait_wake_all(frm);
}

int
vm2_set_id(maple_device_t* dev, const char* ID, const char* name) {
    maple_response_t* resp;
    uint32_t* send_buf;
    int again = 0;

wait_vm2:

    if (maple_frame_lock(&dev->frame) < 0) {
        return 0;
    }

    maple_frame_init(&dev->frame);
    send_buf = (uint32_t*)dev->frame.recv_buf;
    send_buf[0] = MAPLE_FUNC_MEMCARD;
    strncpy((char*)&dev->frame.recv_buf[4], ID, 12);
    if (name) {
        strncpy((char*)&dev->frame.recv_buf[16], name, 128);
    }
    dev->frame.cmd = 33;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = name ? 36 : 4;
    dev->frame.callback = vm2_reply;
    dev->frame.send_buf = send_buf;
    maple_queue_frame(&dev->frame);

    /* Wait for the VM2 to accept it */
    if (genwait_wait(&dev->frame, "vm2_set_id", 200, NULL) < 0) {
        if (dev->frame.state != MAPLE_FRAME_UNSENT) {
            /* It's probably never coming back, so just unlock the frame */
            dev->frame.state = MAPLE_FRAME_VACANT;
            dbglog(DBG_ERROR, "vm2_set_id: timeout to unit %c%c\n", dev->port + 'A', dev->unit + '0');
            return -1;
        }
    }

    resp = (maple_response_t*)recv_buff;

    if (resp->response == MAPLE_RESPONSE_OK) {
        return 0;
    } else if (resp->response == MAPLE_RESPONSE_AGAIN && ++again < 5) { /* a device that keeps saying so would hang the start */
        goto wait_vm2;
    }

    return -1;
}

const char*
get_last_allinfo_type_name(void) {
    const char* n = known_name(((maple_alldevinfo_t*)&recv_buff[4])->extended);
    return n ? n : "VMU";
}

int
check_vm2_present(maple_device_t* dev) {
    if (!dev || !(dev->info.functions & MAPLE_FUNC_MEMCARD)) {
        return 0;
    }
    memset(recv_buff, 0, 196);
    if (send_allinfo(dev) != MAPLE_EOK) {
        return 0;
    }
    return known_name(((maple_alldevinfo_t*)&recv_buff[4])->extended) != NULL;
}

const char*
get_vmu_type_name(maple_device_t* dev) {
    if (!dev || !(dev->info.functions & MAPLE_FUNC_MEMCARD)) {
        return "None";
    }
    memset(recv_buff, 0, 196);
    if (send_allinfo(dev) != MAPLE_EOK) {
        return "VMU"; /* can't query, assume a regular VMU */
    }
    return get_last_allinfo_type_name();
}
