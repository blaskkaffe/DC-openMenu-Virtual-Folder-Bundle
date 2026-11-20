/* Host test of kosnet_json.c */
#include <assert.h>
#include <string.h>

#include "backend/kosnet_json.h"

int
main(void) {
    char out[64];
    const char* body;

    assert(kn_json_escape(out, sizeof(out), "hi \"you\" \\ ok") == 16 && !strcmp(out, "hi \\\"you\\\" \\\\ ok"));
    assert(kn_json_escape(out, sizeof(out), "a\nb\tc\x01") > 0 && !strcmp(out, "a\\nb\\tc\\u0001"));
    assert(kn_json_escape(out, 4, "abcd") == -1);
    assert(kn_json_escape(out, 5, "abcd") == 4);
    assert(kn_json_escape(out, sizeof(out), "") == 0);

    assert(kn_json_str("{\"token\":\"abc.def\",\"x\":1}", "token", out, sizeof(out)) == 7 && !strcmp(out, "abc.def"));
    assert(kn_json_str("{\"user\": {\"display_name\" : \"Ad\\\"am\\u00e9\"}}", "display_name", out, sizeof(out)) == 6 && !strcmp(out, "Ad\"am?"));
    assert(kn_json_str("{\"a\":\"token\",\"token\":\"t\"}", "token", out, sizeof(out)) == 1 && !strcmp(out, "t")); /* a value that looks like the key */
    assert(kn_json_str("{\"token\":null}", "token", out, sizeof(out)) == -1);
    assert(kn_json_str("{}", "token", out, sizeof(out)) == -1);
    assert(kn_json_str("{\"token\":\"abcdefgh\"}", "token", out, 4) == -1); /* too small */
    assert(kn_json_str("{\"token\":\"abcdefgh", "token", out, sizeof(out)) >= 0); /* cut off: no crash */

    assert(kn_error_kind("{\"error\":\"invalid_credentials\"}") == KN_ERR_CREDENTIALS);
    assert(kn_error_kind("{\"code\":\"email_not_verified\"}") == KN_ERR_EMAIL);
    assert(kn_error_kind("rate_limited") == KN_ERR_RATE);
    assert(kn_error_kind("{}") == KN_ERR_OTHER);

    assert(kn_http_status("HTTP/1.1 201 Created\r\nA: b\r\n\r\n{\"ok\":1}", &body) == 201 && !strcmp(body, "{\"ok\":1}"));
    assert(kn_http_status("HTTP/1.0 401 X\r\n", &body) == 401 && body == NULL);
    assert(kn_http_status("nonsense", &body) == -1);
    return 0;
}
