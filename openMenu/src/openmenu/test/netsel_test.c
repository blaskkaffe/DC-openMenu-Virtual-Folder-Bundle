/* netsel_test: the order Auto probes in and what each forced setting allows.
 *   gcc -Wall -Iopenmenu/src -o netsel_test openmenu/test/netsel_test.c openmenu/src/backend/netsel.c -Iopenmenu/src/backend && ./netsel_test */
#include <stdio.h>
#include "netsel.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); bad++; } } while (0)

int main(void) {
    /* Auto walks BBA, LAN, W5500, serial, modem */
    unsigned tried = 0;
    dcnow_device_t want[] = {DCNOW_DEV_BBA, DCNOW_DEV_LAN, DCNOW_DEV_W5500, DCNOW_DEV_SERIAL, DCNOW_DEV_MODEM};
    for (int i = 0; i < 5; i++) {
        dcnow_device_t d = netsel_next(NETSEL_AUTO, tried);
        CHECK(d == want[i]);
        tried |= netsel_bit(d);
    }
    CHECK(netsel_next(NETSEL_AUTO, tried) == DCNOW_DEV_NONE);
    /* the fastest one that is there: BBA missing, W5500 and modem present -> W5500 */
    tried = netsel_bit(DCNOW_DEV_BBA) | netsel_bit(DCNOW_DEV_LAN);
    CHECK(netsel_next(NETSEL_AUTO, tried) == DCNOW_DEV_W5500);
    /* forced */
    CHECK(netsel_next(NETSEL_MODEM, 0) == DCNOW_DEV_MODEM);
    CHECK(netsel_next(NETSEL_MODEM, netsel_bit(DCNOW_DEV_MODEM)) == DCNOW_DEV_NONE);
    CHECK(netsel_next(NETSEL_BROADBAND, 0) == DCNOW_DEV_BBA);
    CHECK(netsel_next(NETSEL_BROADBAND, netsel_bit(DCNOW_DEV_BBA)) == DCNOW_DEV_LAN);
    CHECK(netsel_next(NETSEL_SERIAL, 0) == DCNOW_DEV_SERIAL);
    CHECK(netsel_next(NETSEL_W5500, 0) == DCNOW_DEV_W5500);
    CHECK(!netsel_allows(NETSEL_SERIAL, DCNOW_DEV_MODEM) && !netsel_allows(NETSEL_AUTO, DCNOW_DEV_NONE));
    CHECK(netsel_rank(DCNOW_DEV_BBA) > netsel_rank(DCNOW_DEV_LAN) && netsel_rank(DCNOW_DEV_SERIAL) > netsel_rank(DCNOW_DEV_MODEM));
    for (int d = DCNOW_DEV_MODEM; d <= DCNOW_DEV_W5500; d++) {
        CHECK(netsel_allows(netsel_setting_for((dcnow_device_t)d), (dcnow_device_t)d));
    }
    printf(bad ? "%d failed\n" : "netsel ok\n", bad);
    return bad != 0;
}
