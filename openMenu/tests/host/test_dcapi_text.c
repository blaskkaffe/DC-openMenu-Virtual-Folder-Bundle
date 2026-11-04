/* Host test of the DC API client's text side (src/openmenu/src/vm2/dcapi_text.c): sh openMenu/tests/host/run.sh */
#include <stdio.h>
#include <string.h>
#include "vm2/dcapi_text.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
    uint32_t caps = 0;
    char val[64], req[300];

    /* the hello bits behind the marker, as the iBlue sends them (memory order after the word reversal) */
    CHECK(dcapi_caps_parse("dc1 00000023\0\0\0\0\0\0\0\0\0\0\0\0", &caps) && caps == 0x23);
    CHECK(dcapi_has(caps, DCAPI_CAP_API) && dcapi_has(caps, DCAPI_CAP_NET) && dcapi_has(caps, DCAPI_CAP_GAME));
    CHECK(!dcapi_has(caps, DCAPI_CAP_PORT) && !dcapi_has(caps, DCAPI_CAP_VMU));
    CHECK(dcapi_caps_parse("dc1 0000fEdC", &caps) && caps == 0xFEDC);
    CHECK(!dcapi_caps_parse("dc1 0000fEdZ", &caps));                 /* not hex */
    CHECK(!dcapi_caps_parse("dc2 00000023", &caps));                 /* another level */
    CHECK(!dcapi_caps_parse("\0\0\0\0\0\0\0\0\0\0\0\0", &caps));      /* a device that says nothing (VM2, VMU Pro ...) */
    caps = 7;
    CHECK(!dcapi_caps_parse("VM2 by Dreamware", &caps) && caps == 7); /* caps untouched on failure */

    /* building a request */
    CHECK(dcapi_build(req, sizeof(req), "get hello\n") == 14 && strcmp(req, "dc1\nget hello\n") == 0);
    CHECK(dcapi_build(req, 8, "get hello\n") == -1);                   /* does not fit the buffer */
    {
        char big[260];
        memset(big, 'a', sizeof(big) - 1);
        big[sizeof(big) - 1] = 0;
        CHECK(dcapi_build(req, sizeof(req), big) == -1);               /* does not fit DCAPI_REQ_MAX */
    }

    /* reading an answer */
    const char* ans = "dc1\nid 5\nok hello.kind adapter\nok hello.version v0.8-12-g1a2b3c4\nerr game.id unavailable no game\nok net.selected dcnet\r\nerr port.A.mode unknown\n";
    CHECK(dcapi_answer_ok(ans));
    CHECK(!dcapi_answer_ok("") && !dcapi_answer_ok("dc2\n") && !dcapi_answer_ok("dc10\n") && dcapi_answer_ok("dc1"));
    CHECK(dcapi_answer(ans, "hello.kind", val, sizeof(val)) == 1 && strcmp(val, "adapter") == 0);
    CHECK(dcapi_answer(ans, "hello.version", val, sizeof(val)) == 1 && strcmp(val, "v0.8-12-g1a2b3c4") == 0);
    CHECK(dcapi_answer(ans, "game.id", val, sizeof(val)) == 0 && strcmp(val, "unavailable no game") == 0);
    CHECK(dcapi_answer(ans, "net.selected", val, sizeof(val)) == 1 && strcmp(val, "dcnet") == 0);   /* CRLF tolerated */
    CHECK(dcapi_answer(ans, "port.A.mode", val, sizeof(val)) == 0 && strcmp(val, "unknown") == 0);
    CHECK(dcapi_answer(ans, "hello", val, sizeof(val)) == -1);          /* a longer key is not a prefix match */
    CHECK(dcapi_answer(ans, "hello.ki", val, sizeof(val)) == -1);
    CHECK(dcapi_answer(ans, "led.colour", val, sizeof(val)) == -1);
    CHECK(dcapi_answer("", "x", val, sizeof(val)) == -1);
    {
        char tiny[5];
        CHECK(dcapi_answer(ans, "hello.version", tiny, sizeof(tiny)) == 1 && strcmp(tiny, "v0.8") == 0);   /* cut, terminated */
        CHECK(dcapi_answer(ans, "hello.kind", NULL, 0) == 1);          /* only asking whether it is there */
    }
    CHECK(dcapi_answer("ok net.selected\n", "net.selected", val, sizeof(val)) == 1 && val[0] == 0);   /* an empty value */

    printf("dcapi_text: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
