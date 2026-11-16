/* w5500_net: see w5500_net.h. Modelled on KOS's lan_adapter.c (the netif_t callbacks). The stack does not poll an interface (the BBA and
 * the LAN adapter interrupt), so this one has a thread that asks the chip for frames and hands them to net_input(). */
#include <stdbool.h>
#include <string.h>

#include <arch/timer.h>
#include <dc/scif.h>
#include <kos/dbgio.h>
#include <kos/mutex.h>
#include <kos/net.h>
#include <kos/thread.h>

#include "backend/w5500.h"
#include "backend/w5500_net.h"

static mutex_t bus_mutex = MUTEX_INITIALIZER; /* the net thread sends while the receive thread polls */
static netif_t w5500_if;
static volatile int rx_run;
static kthread_t* rx_thread;
static uint8_t rx_frame[W5500_MAX_FRAME + 16];

static void
bus_select(int on) {
    scif_spi_set_cs(on ? 0 : 1); /* active low, as the SD driver uses it */
}

static uint8_t
bus_xfer(uint8_t b) {
    return scif_spi_rw_byte(b);
}

static void
bus_delay(int ms) {
    thd_sleep(ms);
}

static const w5500_bus_t bus = {bus_select, bus_xfer, bus_delay};

/* The KOS debug output goes through the same pins: off while the chip is on them, and back (with the UART set up again) when it is not. */
static int pins_taken;

static void
give_pins_back(void) {
    if (pins_taken) {
        pins_taken = 0;
        scif_init();
        dbgio_enable();
    }
}

int
w5500_net_probe(void) {
    int found = 0;
    mutex_lock(&bus_mutex);
    if (!pins_taken) {
        dbgio_disable();
        pins_taken = 1;
    }
    if (scif_spi_init() == 0) {
        bus_select(0);
        found = w5500_present(&bus);
    }
    if (!found && !(w5500_if.flags & NETIF_INITIALIZED)) {
        give_pins_back();
    }
    mutex_unlock(&bus_mutex);
    return found;
}

static int
if_detect(netif_t* self) {
    (void)self;
    return w5500_net_probe() ? 0 : -1;
}

static void
make_mac(uint8_t mac[6]) {
    uint64_t t = timer_us_gettime64();
    mac[0] = 0x02; /* locally administered, unicast */
    mac[1] = 0x44;
    mac[2] = 0x43;
    mac[3] = (uint8_t)(t >> 8);
    mac[4] = (uint8_t)(t >> 16);
    mac[5] = (uint8_t)(t >> 24);
}

static int
if_init(netif_t* self) {
    uint8_t mac[6];
    int rc;
    if (self->flags & NETIF_INITIALIZED) {
        return 0;
    }
    make_mac(mac);
    mutex_lock(&bus_mutex);
    rc = scif_spi_init() == 0 ? w5500_open(&bus, mac) : -1;
    mutex_unlock(&bus_mutex);
    if (rc < 0) {
        return -1;
    }
    memcpy(self->mac_addr, mac, 6);
    self->ip6_lladdr.__s6_addr.__s6_addr8[0] = 0xFE; /* link-local, from the MAC as the other adapters do */
    self->ip6_lladdr.__s6_addr.__s6_addr8[1] = 0x80;
    self->ip6_lladdr.__s6_addr.__s6_addr8[8] = mac[0] ^ 0x02;
    self->ip6_lladdr.__s6_addr.__s6_addr8[9] = mac[1];
    self->ip6_lladdr.__s6_addr.__s6_addr8[10] = mac[2];
    self->ip6_lladdr.__s6_addr.__s6_addr8[11] = 0xFF;
    self->ip6_lladdr.__s6_addr.__s6_addr8[12] = 0xFE;
    self->ip6_lladdr.__s6_addr.__s6_addr8[13] = mac[3];
    self->ip6_lladdr.__s6_addr.__s6_addr8[14] = mac[4];
    self->ip6_lladdr.__s6_addr.__s6_addr8[15] = mac[5];
    self->flags |= NETIF_INITIALIZED;
    return 0;
}

static void*
rx_main(void* param) {
    netif_t* self = param;
    thd_set_prio(thd_get_current(), PRIO_DEFAULT + 4);
    while (rx_run) {
        int n;
        mutex_lock(&bus_mutex);
        n = w5500_recv(&bus, rx_frame, sizeof(rx_frame));
        mutex_unlock(&bus_mutex);
        if (n > 0) {
            net_input(self, rx_frame, n);
        } else {
            thd_sleep(n < 0 ? 5 : 3); /* nothing waiting: SPI polling costs CPU, so not tighter than this */
        }
    }
    return NULL;
}

static int
if_start(netif_t* self) {
    if (!(self->flags & NETIF_INITIALIZED)) {
        return -1;
    }
    if (self->flags & NETIF_RUNNING) {
        return 0;
    }
    for (int i = 0; i < 30 && !w5500_net_link(); i++) { /* up to 3 s for the PHY to find the link partner */
        thd_sleep(100);
    }
    rx_run = 1;
    rx_thread = thd_create(false, rx_main, self);
    if (!rx_thread) {
        rx_run = 0;
        return -1;
    }
    self->flags |= NETIF_RUNNING;
    return 0;
}

static int
if_stop(netif_t* self) {
    if (!(self->flags & NETIF_RUNNING)) {
        return -1;
    }
    rx_run = 0;
    if (rx_thread) {
        thd_join(rx_thread, NULL);
        rx_thread = NULL;
    }
    self->flags &= ~NETIF_RUNNING;
    return 0;
}

static int
if_shutdown(netif_t* self) {
    if (!(self->flags & NETIF_INITIALIZED)) {
        return 0;
    }
    if (self->flags & NETIF_RUNNING) {
        if_stop(self);
    }
    mutex_lock(&bus_mutex);
    w5500_close(&bus);
    self->flags &= ~(NETIF_DETECTED | NETIF_INITIALIZED | NETIF_RUNNING);
    give_pins_back();
    mutex_unlock(&bus_mutex);
    return 0;
}

static int
if_tx(netif_t* self, const uint8* data, int len, int blocking) {
    int rc;
    if (!(self->flags & NETIF_RUNNING)) {
        return NETIF_TX_ERROR;
    }
    mutex_lock(&bus_mutex);
    rc = w5500_send(&bus, data, len, blocking);
    mutex_unlock(&bus_mutex);
    return rc == 0 ? NETIF_TX_OK : (blocking ? NETIF_TX_ERROR : NETIF_TX_AGAIN);
}

static int
if_tx_commit(netif_t* self) {
    (void)self;
    return 0;
}

static int
if_rx_poll(netif_t* self) {
    (void)self; /* the receive thread does it */
    return 0;
}

static int
if_set_flags(netif_t* self, uint32 flags_and, uint32 flags_or) {
    self->flags = (self->flags & flags_and) | flags_or;
    return 0;
}

static int
if_set_mc(netif_t* self, const uint8* list, int count) {
    (void)self;
    (void)list;
    (void)count;
    return 0; /* no MAC filter is on: multicast arrives anyway */
}

int
w5500_net_link(void) {
    int up;
    mutex_lock(&bus_mutex);
    up = w5500_link_up(&bus);
    mutex_unlock(&bus_mutex);
    return up;
}

int
w5500_net_register(void) {
    if (!w5500_net_probe()) {
        return -1;
    }
    memset(&w5500_if, 0, sizeof(w5500_if));
    w5500_if.name = "w5500";
    w5500_if.descr = "W5500 on the serial port";
    w5500_if.flags = NETIF_NO_FLAGS;
    w5500_if.mtu = 1500;
    w5500_if.if_detect = if_detect;
    w5500_if.if_init = if_init;
    w5500_if.if_shutdown = if_shutdown;
    w5500_if.if_start = if_start;
    w5500_if.if_stop = if_stop;
    w5500_if.if_tx = if_tx;
    w5500_if.if_tx_commit = if_tx_commit;
    w5500_if.if_rx_poll = if_rx_poll;
    w5500_if.if_set_flags = if_set_flags;
    w5500_if.if_set_mc = if_set_mc;
    return net_reg_device(&w5500_if);
}

void
w5500_net_unregister(void) {
    if (w5500_if.flags & NETIF_REGISTERED) {
        net_unreg_device(&w5500_if);
    }
    mutex_lock(&bus_mutex);
    if (!(w5500_if.flags & NETIF_INITIALIZED)) {
        give_pins_back();
    }
    mutex_unlock(&bus_mutex);
}
