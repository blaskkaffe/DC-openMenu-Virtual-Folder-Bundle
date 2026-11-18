/*
 * File: w5500_net.h
 * Project: openmenu
 * The W5500 as a KOS network interface (the serial port's SPI pins, as an SD adapter uses them). The chip driver is w5500.c.
 */
#pragma once

/* Is a W5500 on the serial port? Touches only the SPI pins and reads one register; leaves the port as the serial driver needs it
 * (serial_port_open sets everything up again). Not tried while an SD card is mounted there. Returns 1 or 0. */
int w5500_net_probe(void);

/* Registers the interface with KOS (before net_init). Returns 0, or -1 when the chip is not there. */
int w5500_net_register(void);

/* Takes the interface out of KOS again (after net_shutdown, or when a connect is given up). */
void w5500_net_unregister(void);

/* The cable has a link partner (for the interface test). Only while registered and running. */
int w5500_net_link(void);
