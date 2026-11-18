/*
 * File: netsel.h
 * Project: openmenu
 * Which way to the network is used: "Auto" takes the fastest one that is there, or one is forced in Settings. Pure logic (no KOS),
 * host-tested with test/netsel_test.c. The probing itself is in dcnow_net.c, in the order this module gives.
 *
 * Speed order, fastest first (the hardware's own rate, not a measurement):
 *   Broadband Adapter 100 Mbit, LAN Adapter 10 Mbit, W5500 on the serial port (SPI, bit-banged by KOS: about 1 Mbit at best),
 *   serial cable to a DreamPi (PPP at 115200), the modem (33.6k, 56k from a V.90 line).
 */
#pragma once

#include "dcnow_net.h"

/* The Settings value ("Connection"): 0 is Auto, the others force one way. Broadband covers both Ethernet adapters of the expansion port. */
enum { NETSEL_AUTO = 0, NETSEL_MODEM, NETSEL_BROADBAND, NETSEL_SERIAL, NETSEL_W5500, NETSEL_COUNT };

/* Rank of a way (higher is faster), 0 for none. */
int netsel_rank(dcnow_device_t dev);

/* Does this Settings value allow that way? Auto allows all of them. */
int netsel_allows(int setting, dcnow_device_t dev);

/* The fastest way the setting allows that is not in `tried` (a mask of netsel_bit()); DCNOW_DEV_NONE when none is left. Probe it: if it
 * is not there add its bit to `tried` and ask again. */
dcnow_device_t netsel_next(int setting, unsigned tried);
unsigned netsel_bit(dcnow_device_t dev);

/* "Auto", "Modem", "Broadband adapter", "Serial cable", "W5500" */
const char* netsel_setting_name(int setting);
/* The nominal speed of a way for the lists ("100 Mbit", "10 Mbit", "SPI ~1 Mbit", "115 kbit", "56 kbit"). */
const char* netsel_speed_text(dcnow_device_t dev);
/* The setting that forces this way (NETSEL_AUTO for none). */
int netsel_setting_for(dcnow_device_t dev);
