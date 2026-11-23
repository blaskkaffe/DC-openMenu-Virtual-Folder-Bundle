/* Host test of the dc1 requests and answers of the gateways (backend/picolink_parse). */
#include <stdio.h>
#include <string.h>

#include "backend/picolink_parse.h"

static int failures;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("FAIL line %d: %s\n", __LINE__, #c);           \
            failures++;                                           \
        }                                                         \
    } while (0)

int
main(void) {
    char out[400];
    pl_info_t in;

    CHECK(pl_req_status(out, sizeof(out)) > 0 && !strncmp(out, "dc1\nget hello\n", 14));
    CHECK(pl_req_status(out, 10) == 0);
    pl_req_select(out, sizeof(out), 1);
    CHECK(!strcmp(out, "dc1\ncall net.select dcnet\nget net.*\n"));
    pl_req_select(out, sizeof(out), 0);
    CHECK(strstr(out, "net.select dcnow"));
    pl_req_scan(out, sizeof(out));
    CHECK(!strcmp(out, "dc1\ncall wifi.scan\nget wifi.*\n"));

    /* joins */
    CHECK(pl_req_join(out, sizeof(out), 2, NULL, "hunter2hunter2") > 0);
    CHECK(!strcmp(out, "dc1\nset wifi.pass hunter2hunter2\ncall wifi.join #2\nget wifi.*\n"));
    CHECK(pl_req_join(out, sizeof(out), -1, "Home Net", "") > 0 && strstr(out, "set wifi.pass \ncall wifi.join Home Net\n"));
    CHECK(pl_req_join(out, sizeof(out), 0, NULL, "short") == 0);              /* under 8 */
    CHECK(pl_req_join(out, sizeof(out), 0, NULL, " leadingblank") == 0);      /* blank at the start */
    CHECK(pl_req_join(out, sizeof(out), 0, NULL, "trailingblank ") == 0);
    CHECK(pl_req_join(out, sizeof(out), 0, NULL, "new\nline1234") == 0);      /* would split the request */
    CHECK(pl_req_join(out, sizeof(out), -1, "", "") == 0);                    /* no name */
    CHECK(pl_req_join(out, sizeof(out), -1, "012345678901234567890123456789012", "") == 0); /* 33 characters */
    CHECK(pl_pass_ok("") && pl_pass_ok("12345678") && !pl_pass_ok("1234567"));
    {
        char longpass[80];
        memset(longpass, 'a', 64);
        longpass[64] = '\0';
        CHECK(!pl_pass_ok(longpass));
        longpass[63] = '\0';
        CHECK(pl_pass_ok(longpass));
    }

    /* the answer of a gateway with wifi */
    pl_info_init(&in);
    CHECK(in.dcnet == -1 && in.scan_n == -1 && !in.present);
    CHECK(pl_parse("dc1\nok hello.api 1\nok hello.caps api,net,wifi\nok hello.kind gateway\nok hello.name DC-PicoLink\nok hello.version 0.9\n"
                   "ok net.dcnet ok\nok net.selected dcnet\nok wifi.ip 192.168.1.50\nok wifi.ssid Home Net\nok wifi.state connected\nok wifi.up 1\n",
                   &in) == 11);
    CHECK(in.present && in.has_net && in.has_wifi && !strcmp(in.name, "DC-PicoLink"));
    CHECK(in.dcnet == 1 && in.dcnet_ok);
    CHECK(!strcmp(in.wifi_state, "connected") && !strcmp(in.wifi_ssid, "Home Net") && !strcmp(in.wifi_ip, "192.168.1.50"));

    /* a later full read without ssid / ip: they are gone */
    CHECK(pl_parse("dc1\nok wifi.state none\nok wifi.up 0\n", &in) == 2);
    CHECK(!strcmp(in.wifi_state, "none") && in.wifi_ssid[0] == '\0' && in.wifi_ip[0] == '\0');

    /* a scan, CRLF line ends, a name with spaces, a hidden-name oddity */
    CHECK(pl_parse("dc1\r\nok wifi.scan 3\r\nok wifi.scan.n 3\r\nok wifi.scan.0 -48 0 Home Net\r\nok wifi.scan.1 -71 1 Cafe\r\nok wifi.scan.2 -90 0 x\r\nok wifi.state none\r\n", &in) == 6);
    CHECK(in.scan_n == 3 && in.scan_have == 3);
    CHECK(in.scan[0].rssi == -48 && !in.scan[0].open && !strcmp(in.scan[0].name, "Home Net"));
    CHECK(in.scan[1].open && !strcmp(in.scan[1].name, "Cafe"));
    CHECK(pl_bars(-48) == 4 && pl_bars(-60) == 3 && pl_bars(-70) == 2 && pl_bars(-85) == 1 && pl_bars(-95) == 0);

    /* more networks than we keep */
    {
        char big[2000] = "dc1\nok wifi.scan.n 24\n";
        for (int i = 0; i < 24; i++) {
            char l[64];
            snprintf(l, sizeof(l), "ok wifi.scan.%d -60 1 N%d\n", i, i);
            strcat(big, l);
        }
        pl_parse(big, &in);
        CHECK(in.scan_n == 24 && in.scan_have == PL_SCAN_MAX && !strcmp(in.scan[15].name, "N15"));
    }

    /* errors */
    CHECK(pl_parse("dc1\nerr wifi.scan unavailable busy\nok net.selected dcnow\n", &in) == 1);
    CHECK(!strcmp(in.err, "wifi.scan unavailable busy") && in.dcnet == 0);
    CHECK(pl_parse("dc1\nok net.selected dcnow\n", &in) == 1 && in.err[0] == '\0'); /* cleared by the next answer */

    /* not dc1: an HTML page of something else on that address */
    CHECK(pl_parse("<html>hello</html>", &in) == -1);
    CHECK(pl_parse("", &in) == -1);

    /* a DreamPi: no wifi */
    pl_info_init(&in);
    pl_parse("dc1\nok hello.api 1\nok hello.caps api,net\nok hello.name DreamPi\n", &in);
    CHECK(in.present && in.has_net && !in.has_wifi);
    pl_parse("dc1\nok hello.caps api,network\n", &in);
    CHECK(!in.has_net); /* "network" is not "net" */

    if (failures) {
        printf("%d failed\n", failures);
        return 1;
    }
    puts("picolink_parse ok");
    return 0;
}
