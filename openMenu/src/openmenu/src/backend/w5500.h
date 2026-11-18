/*
 * File: w5500.h
 * Project: openmenu
 * WIZnet W5500 Ethernet chip in MACRAW mode (socket 0 carries raw Ethernet frames, the network stack does IP/TCP itself), over a SPI
 * bus the caller provides. Portable C: on the console the bus is KOS's scif_spi_* (the serial port's pins, as an SD adapter uses
 * them; w5500_net.c), in test/w5500_test.c it is a model of the chip.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct w5500_bus {
    void (*select)(int on);       /* chip select: on = 1 pulls it low */
    uint8_t (*xfer)(uint8_t out); /* one byte out and the byte that came in at the same time */
    void (*delay_ms)(int ms);     /* may be NULL (the polling loops then just count) */
} w5500_bus_t;

#define W5500_VERSION 0x04
#define W5500_MAX_FRAME 1518

/* Looks for the chip: reads its version register. Returns 1 when a W5500 answers, 0 when not (an empty bus reads all ones or zeros). Does not change
 * anything on the chip. */
int w5500_present(const w5500_bus_t* bus);

/* Soft reset, MAC address, socket 0 as a MACRAW socket with the whole 16 KB of both buffers. Returns 0 or -1. */
int w5500_open(const w5500_bus_t* bus, const uint8_t mac[6]);
void w5500_close(const w5500_bus_t* bus);

/* The PHY sees a cable with a link partner. */
int w5500_link_up(const w5500_bus_t* bus);

/* Sends one frame. blocking: wait for the chip to take it (up to about 50 ms). Returns 0, or -1 when it did not go. */
int w5500_send(const w5500_bus_t* bus, const uint8_t* frame, int len, int blocking);

/* Copies the next received frame into out (max bytes). Returns its length, 0 when there is none, -1 on a broken buffer (it is then
 * flushed). A frame longer than max is dropped and 0 returned. */
int w5500_recv(const w5500_bus_t* bus, uint8_t* out, int max);
