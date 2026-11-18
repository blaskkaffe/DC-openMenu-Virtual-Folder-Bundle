/* w5500_test: the driver against a model of the chip (register file, 16 KB buffers with wrap-around, the commands it uses).
 *   gcc -Wall -Iopenmenu/src/backend -o w5500_test openmenu/test/w5500_test.c openmenu/src/backend/w5500.c && ./w5500_test */
#include <stdio.h>
#include <string.h>

#include "w5500.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); bad++; } } while (0)

/* ---- the chip ---- */
static uint8_t common[0x40], sreg[0x40], txbuf[0x4000], rxbuf[0x4000];
static int sel, phase, bsb, wrt, present = 1;
static unsigned addr;
static uint8_t sent[8][1600];
static int sent_len[8], sent_n, resets;
static unsigned rx_in; /* the chip's own write pointer into the receive buffer */

static unsigned g16(const uint8_t* r, unsigned a) { return (r[a] << 8) | r[a + 1]; }
static void p16(uint8_t* r, unsigned a, unsigned v) { r[a] = (uint8_t)(v >> 8); r[a + 1] = (uint8_t)v; }

static void command(uint8_t c) {
    if (c == 0x01) { sreg[0x03] = (sreg[0] & 0x0F) == 0x04 ? 0x42 : 0; }
    if (c == 0x10) { sreg[0x03] = 0; }
    if (c == 0x20) { /* SEND: the bytes from TX_RD to TX_WR go out */
        unsigned rd = g16(sreg, 0x22), wr = g16(sreg, 0x24), n = (wr - rd) & 0xFFFF;
        for (unsigned i = 0; i < n; i++) sent[sent_n & 7][i] = txbuf[(rd + i) & 0x3FFF];
        sent_len[sent_n & 7] = (int)n; sent_n++;
        p16(sreg, 0x22, wr);
        sreg[0x02] |= 0x10;
    }
    if (c == 0x40) { /* RECV: the host moved RX_RD; the received size follows */
        unsigned rd = g16(sreg, 0x28);
        p16(sreg, 0x26, (rx_in - rd) & 0xFFFF);
    }
    sreg[0x01] = 0;
}

static void chip_in(uint8_t v, int* out_valid, uint8_t* out) {
    (void)out_valid; (void)out;
    uint8_t* base; unsigned size;
    if (bsb == 0) { base = common; size = sizeof(common); }
    else if (bsb == 1) { base = sreg; size = sizeof(sreg); }
    else if (bsb == 2) { base = txbuf; size = sizeof(txbuf); }
    else if (bsb == 3) { base = rxbuf; size = sizeof(rxbuf); }
    else { base = NULL; size = 0; }
    unsigned a = addr;
    if (bsb >= 2 && bsb <= 3) a &= 0x3FFF;
    if (wrt && base && a < size) {
        base[a] = v;
        if (bsb == 0 && a == 0 && (v & 0x80)) { resets++; common[0] = 0; common[0x39] = 4; }
        if (bsb == 1 && a == 1) { command(v); }
    }
}

static uint8_t xfer(uint8_t out) {
    if (!sel) return 0;
    if (phase == 0) { addr = (unsigned)out << 8; phase = 1; return 0; }
    if (phase == 1) { addr |= out; phase = 2; return 0; }
    if (phase == 2) { bsb = out >> 3; wrt = (out >> 2) & 1; phase = 3; return 0; }
    uint8_t ret = 0;
    uint8_t* base = bsb == 0 ? common : bsb == 1 ? sreg : bsb == 2 ? txbuf : bsb == 3 ? rxbuf : NULL;
    unsigned size = bsb == 0 ? sizeof(common) : bsb == 1 ? sizeof(sreg) : 0x4000;
    unsigned a = addr;
    if (bsb >= 2 && bsb <= 3) a &= 0x3FFF;
    if (!present) { addr++; return 0xFF; }
    if (!wrt) { ret = base && a < size ? base[a] : 0; } else { chip_in(out, NULL, NULL); }
    addr++;
    return ret;
}
static void select_fn(int on) { sel = on; phase = 0; }
static const w5500_bus_t bus = {select_fn, xfer, NULL};

/* a frame arrives from the wire */
static void inject(const uint8_t* f, int len) {
    unsigned total = (unsigned)len + 2;
    uint8_t hdr[2] = {(uint8_t)(total >> 8), (uint8_t)total};
    for (int i = 0; i < 2; i++) rxbuf[(rx_in + (unsigned)i) & 0x3FFF] = hdr[i];
    for (int i = 0; i < len; i++) rxbuf[(rx_in + 2 + (unsigned)i) & 0x3FFF] = f[i];
    rx_in = (rx_in + total) & 0xFFFF;
    p16(sreg, 0x26, (rx_in - g16(sreg, 0x28)) & 0xFFFF);
}

int main(void) {
    static uint8_t frame[1500], back[1600];
    const uint8_t mac[6] = {2, 0x44, 0x43, 1, 2, 3};
    for (int i = 0; i < 1500; i++) frame[i] = (uint8_t)(i * 7 + 3);
    p16(sreg, 0x20, 0x4000); /* TX_FSR: all free */

    present = 0;
    CHECK(!w5500_present(&bus)); /* nothing on the bus reads as ones */
    present = 1;
    CHECK(!w5500_present(&bus)); /* before the chip's version register is set */
    common[0x39] = 4;
    CHECK(w5500_present(&bus));

    CHECK(w5500_open(&bus, mac) == 0 && resets == 1);
    CHECK(memcmp(common + 9, mac, 6) == 0);
    CHECK(sreg[0x03] == 0x42 && sreg[0x1E] == 16 && sreg[0x1F] == 16);
    common[0x2E] = 1;
    CHECK(w5500_link_up(&bus));

    /* send: frames across the end of the 16 KB buffer */
    for (int n = 0; n < 40; n++) {
        int len = 60 + n * 37 % 1400;
        frame[0] = (uint8_t)n;
        CHECK(w5500_send(&bus, frame, len, 1) == 0);
        CHECK(sent_len[(sent_n - 1) & 7] == len && memcmp(sent[(sent_n - 1) & 7], frame, (size_t)len) == 0);
    }
    CHECK(w5500_send(&bus, frame, 0, 1) == -1 && w5500_send(&bus, frame, 1600, 1) == -1);
    p16(sreg, 0x20, 10); /* the chip is still busy */
    CHECK(w5500_send(&bus, frame, 100, 0) == -1);
    p16(sreg, 0x20, 0x4000);

    /* receive: none, one, several wrapping around the buffer end */
    CHECK(w5500_recv(&bus, back, sizeof(back)) == 0);
    int ok = 1;
    for (int n = 0; n < 40; n++) {
        int len = 60 + n * 53 % 1400;
        frame[0] = (uint8_t)n;
        inject(frame, len);
        int got = w5500_recv(&bus, back, sizeof(back));
        if (got != len || memcmp(back, frame, (size_t)len) != 0) ok = 0;
    }
    CHECK(ok);
    CHECK(w5500_recv(&bus, back, sizeof(back)) == 0);
    inject(frame, 300);
    inject(frame, 400);
    CHECK(w5500_recv(&bus, back, sizeof(back)) == 300 && w5500_recv(&bus, back, sizeof(back)) == 400);
    inject(frame, 800); /* too big for the caller's buffer: dropped, not an error */
    CHECK(w5500_recv(&bus, back, 500) == 0 && w5500_recv(&bus, back, 500) == 0);
    /* a broken length is flushed */
    rxbuf[rx_in & 0x3FFF] = 0xFF; rxbuf[(rx_in + 1) & 0x3FFF] = 0xFF; rx_in = (rx_in + 20) & 0xFFFF;
    p16(sreg, 0x26, (rx_in - g16(sreg, 0x28)) & 0xFFFF);
    CHECK(w5500_recv(&bus, back, sizeof(back)) == -1 && w5500_recv(&bus, back, sizeof(back)) == 0);

    w5500_close(&bus);
    CHECK(sreg[0x03] == 0);
    printf(bad ? "%d failed\n" : "w5500 ok\n", bad);
    return bad != 0;
}
