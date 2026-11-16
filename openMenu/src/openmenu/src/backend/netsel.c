/* netsel: see netsel.h. */
#include "netsel.h"

static const dcnow_device_t order[] = {DCNOW_DEV_BBA, DCNOW_DEV_LAN, DCNOW_DEV_W5500, DCNOW_DEV_SERIAL, DCNOW_DEV_MODEM};
#define ORDER_N ((int)(sizeof(order) / sizeof(order[0])))

int
netsel_rank(dcnow_device_t dev) {
    for (int i = 0; i < ORDER_N; i++) {
        if (order[i] == dev) {
            return ORDER_N - i;
        }
    }
    return 0;
}

int
netsel_allows(int setting, dcnow_device_t dev) {
    switch (setting) {
        case NETSEL_AUTO: return netsel_rank(dev) > 0;
        case NETSEL_MODEM: return dev == DCNOW_DEV_MODEM;
        case NETSEL_BROADBAND: return dev == DCNOW_DEV_BBA || dev == DCNOW_DEV_LAN;
        case NETSEL_SERIAL: return dev == DCNOW_DEV_SERIAL;
        case NETSEL_W5500: return dev == DCNOW_DEV_W5500;
        default: return 0;
    }
}

unsigned
netsel_bit(dcnow_device_t dev) {
    return 1u << (unsigned)dev;
}

dcnow_device_t
netsel_next(int setting, unsigned tried) {
    for (int i = 0; i < ORDER_N; i++) {
        if (netsel_allows(setting, order[i]) && !(tried & netsel_bit(order[i]))) {
            return order[i];
        }
    }
    return DCNOW_DEV_NONE;
}

const char*
netsel_setting_name(int setting) {
    switch (setting) {
        case NETSEL_MODEM: return "Modem";
        case NETSEL_BROADBAND: return "Broadband adapter";
        case NETSEL_SERIAL: return "Serial cable";
        case NETSEL_W5500: return "W5500";
        default: return "Auto";
    }
}

const char*
netsel_speed_text(dcnow_device_t dev) {
    switch (dev) {
        case DCNOW_DEV_BBA: return "100 Mbit";
        case DCNOW_DEV_LAN: return "10 Mbit";
        case DCNOW_DEV_W5500: return "SPI ~1 Mbit";
        case DCNOW_DEV_SERIAL: return "115 kbit";
        case DCNOW_DEV_MODEM: return "56 kbit";
        default: return "";
    }
}

int
netsel_setting_for(dcnow_device_t dev) {
    for (int s = NETSEL_MODEM; s < NETSEL_COUNT; s++) {
        if (netsel_allows(s, dev)) {
            return s;
        }
    }
    return NETSEL_AUTO;
}
