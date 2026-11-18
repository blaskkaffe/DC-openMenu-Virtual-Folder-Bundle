/* w5500: see w5500.h. Register addresses and the frame format are from the W5500 datasheet (v1.0.x): a SPI frame is a 16 bit address, a control
 * byte (block select in bits 7..3, bit 2 = write, bits 1..0 = 0 for the variable length mode) and the data; socket n's registers are block 4n+1, its transmit
 * buffer 4n+2, its receive buffer 4n+3. */
#include <string.h>

#include "w5500.h"

#define BSB_COMMON 0x00
#define BSB_S0_REG 0x01
#define BSB_S0_TX 0x02
#define BSB_S0_RX 0x03

#define R_MR 0x0000     /* bit 7 resets the chip */
#define R_SHAR 0x0009   /* MAC address, 6 bytes */
#define R_PHYCFGR 0x002E /* bit 0 link up */
#define R_VERSIONR 0x0039

#define S_MR 0x0000     /* 0x04 = MACRAW */
#define S_CR 0x0001
#define S_IR 0x0002     /* bit 4 SEND_OK, bit 3 TIMEOUT */
#define S_SR 0x0003     /* 0x42 = MACRAW open */
#define S_RXBUF_SIZE 0x001E /* in KB */
#define S_TXBUF_SIZE 0x001F
#define S_TX_FSR 0x0020
#define S_TX_WR 0x0024
#define S_RX_RSR 0x0026
#define S_RX_RD 0x0028

#define CMD_OPEN 0x01
#define CMD_CLOSE 0x10
#define CMD_SEND 0x20
#define CMD_RECV 0x40

#define BUF_SIZE 0x4000u /* socket 0 gets all 16 KB of each direction */
#define BUF_MASK (BUF_SIZE - 1)
#define MR_MACRAW 0x04

static void
head(const w5500_bus_t* b, unsigned addr, unsigned bsb, int write) {
    b->select(1);
    b->xfer((uint8_t)(addr >> 8));
    b->xfer((uint8_t)addr);
    b->xfer((uint8_t)((bsb << 3) | (write ? 0x04 : 0x00)));
}

static uint8_t
rd8(const w5500_bus_t* b, unsigned addr, unsigned bsb) {
    head(b, addr, bsb, 0);
    uint8_t v = b->xfer(0);
    b->select(0);
    return v;
}

static void
wr8(const w5500_bus_t* b, unsigned addr, unsigned bsb, uint8_t v) {
    head(b, addr, bsb, 1);
    b->xfer(v);
    b->select(0);
}

static unsigned
rd16(const w5500_bus_t* b, unsigned addr, unsigned bsb) {
    head(b, addr, bsb, 0);
    unsigned hi = b->xfer(0);
    unsigned lo = b->xfer(0);
    b->select(0);
    return (hi << 8) | lo;
}

/* A 16 bit register that changes while it is read (the free size, the received size) is read until two reads agree. */
static unsigned
rd16_stable(const w5500_bus_t* b, unsigned addr, unsigned bsb) {
    unsigned a = rd16(b, addr, bsb);
    for (int i = 0; i < 4; i++) {
        unsigned c = rd16(b, addr, bsb);
        if (c == a) {
            return c;
        }
        a = c;
    }
    return a;
}

static void
wr16(const w5500_bus_t* b, unsigned addr, unsigned bsb, unsigned v) {
    head(b, addr, bsb, 1);
    b->xfer((uint8_t)(v >> 8));
    b->xfer((uint8_t)v);
    b->select(0);
}

static void
delay(const w5500_bus_t* b, int ms) {
    if (b->delay_ms) {
        b->delay_ms(ms);
    }
}

/* Waits for the command register to read zero (the chip took the command). */
static int
cmd(const w5500_bus_t* b, uint8_t c) {
    wr8(b, S_CR, BSB_S0_REG, c);
    for (int i = 0; i < 2000; i++) {
        if (rd8(b, S_CR, BSB_S0_REG) == 0) {
            return 0;
        }
    }
    return -1;
}

int
w5500_present(const w5500_bus_t* b) {
    return rd8(b, R_VERSIONR, BSB_COMMON) == W5500_VERSION && rd8(b, R_VERSIONR, BSB_COMMON) == W5500_VERSION;
}

int
w5500_open(const w5500_bus_t* b, const uint8_t mac[6]) {
    wr8(b, R_MR, BSB_COMMON, 0x80);
    int done = 0;
    for (int i = 0; i < 100 && !done; i++) {
        delay(b, 2);
        done = !(rd8(b, R_MR, BSB_COMMON) & 0x80) && rd8(b, R_VERSIONR, BSB_COMMON) == W5500_VERSION;
    }
    if (!done) {
        return -1;
    }
    head(b, R_SHAR, BSB_COMMON, 1);
    for (int i = 0; i < 6; i++) {
        b->xfer(mac[i]);
    }
    b->select(0);
    wr8(b, S_RXBUF_SIZE, BSB_S0_REG, 16);
    wr8(b, S_TXBUF_SIZE, BSB_S0_REG, 16);
    for (int s = 1; s < 8; s++) { /* the other sockets get none */
        wr8(b, S_RXBUF_SIZE, (unsigned)(4 * s + 1), 0);
        wr8(b, S_TXBUF_SIZE, (unsigned)(4 * s + 1), 0);
    }
    wr8(b, S_MR, BSB_S0_REG, MR_MACRAW); /* no MAC filter: the stack wants broadcasts (ARP, DHCP) and its own unicast */
    if (cmd(b, CMD_OPEN) < 0 || rd8(b, S_SR, BSB_S0_REG) != 0x42) {
        return -1;
    }
    return 0;
}

void
w5500_close(const w5500_bus_t* b) {
    cmd(b, CMD_CLOSE);
}

int
w5500_link_up(const w5500_bus_t* b) {
    return rd8(b, R_PHYCFGR, BSB_COMMON) & 1;
}

/* Writes into the transmit buffer from `at`, wrapping at the end of the 16 KB. */
static void
tx_put(const w5500_bus_t* b, unsigned at, const uint8_t* data, int len) {
    while (len > 0) {
        unsigned off = at & BUF_MASK;
        int chunk = (int)(BUF_SIZE - off) < len ? (int)(BUF_SIZE - off) : len;
        head(b, off, BSB_S0_TX, 1);
        for (int i = 0; i < chunk; i++) {
            b->xfer(data[i]);
        }
        b->select(0);
        data += chunk;
        at += (unsigned)chunk;
        len -= chunk;
    }
}

static void
rx_get(const w5500_bus_t* b, unsigned at, uint8_t* out, int len) {
    while (len > 0) {
        unsigned off = at & BUF_MASK;
        int chunk = (int)(BUF_SIZE - off) < len ? (int)(BUF_SIZE - off) : len;
        head(b, off, BSB_S0_RX, 0);
        for (int i = 0; i < chunk; i++) {
            out[i] = b->xfer(0);
        }
        b->select(0);
        out += chunk;
        at += (unsigned)chunk;
        len -= chunk;
    }
}

int
w5500_send(const w5500_bus_t* b, const uint8_t* frame, int len, int blocking) {
    if (len <= 0 || len > W5500_MAX_FRAME) {
        return -1;
    }
    int waited = 0;
    while (rd16_stable(b, S_TX_FSR, BSB_S0_REG) < (unsigned)len) { /* the previous frame is still going out */
        if (!blocking || waited++ > 50) {
            return -1;
        }
        delay(b, 1);
    }
    unsigned wr = rd16(b, S_TX_WR, BSB_S0_REG);
    tx_put(b, wr, frame, len);
    wr16(b, S_TX_WR, BSB_S0_REG, (wr + (unsigned)len) & 0xFFFF);
    wr8(b, S_IR, BSB_S0_REG, 0x18); /* clear SEND_OK and TIMEOUT before the new send */
    if (cmd(b, CMD_SEND) < 0) {
        return -1;
    }
    return 0;
}

int
w5500_recv(const w5500_bus_t* b, uint8_t* out, int max) {
    unsigned rsr = rd16_stable(b, S_RX_RSR, BSB_S0_REG);
    if (rsr < 2) {
        return 0;
    }
    unsigned rd = rd16(b, S_RX_RD, BSB_S0_REG);
    uint8_t lenb[2];
    rx_get(b, rd, lenb, 2);
    unsigned total = ((unsigned)lenb[0] << 8) | lenb[1]; /* the frame's size plus these two bytes */
    if (total < 2 + 14 || total - 2 > W5500_MAX_FRAME || total > rsr) { /* not a frame: drop what is in the buffer */
        wr16(b, S_RX_RD, BSB_S0_REG, (rd + rsr) & 0xFFFF);
        cmd(b, CMD_RECV);
        return -1;
    }
    int len = (int)total - 2;
    int got = 0;
    if (len <= max) {
        rx_get(b, rd + 2, out, len);
        got = len;
    }
    wr16(b, S_RX_RD, BSB_S0_REG, (rd + total) & 0xFFFF);
    cmd(b, CMD_RECV);
    return got;
}
