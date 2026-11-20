/*
 * File: kosnet_json.c
 * Project: openmenu
 * See kosnet_json.h.
 */

#include "backend/kosnet_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
kn_json_escape(char* out, size_t size, const char* in) {
    size_t n = 0;

    for (; *in; in++) {
        unsigned char c = (unsigned char)*in;
        char tmp[8];
        size_t len;

        if (c == '"' || c == '\\') {
            tmp[0] = '\\';
            tmp[1] = (char)c;
            len = 2;
        } else if (c < 0x20) {
            if (c == '\n' || c == '\r' || c == '\t') {
                tmp[0] = '\\';
                tmp[1] = c == '\n' ? 'n' : c == '\r' ? 'r' : 't';
                len = 2;
            } else {
                len = (size_t)snprintf(tmp, sizeof(tmp), "\\u%04x", c);
            }
        } else {
            tmp[0] = (char)c;
            len = 1;
        }
        if (n + len + 1 > size) {
            return -1;
        }
        memcpy(out + n, tmp, len);
        n += len;
    }
    if (n + 1 > size) {
        return -1;
    }
    out[n] = '\0';
    return (int)n;
}

int
kn_json_str(const char* json, const char* key, char* out, size_t size) {
    char pat[48];
    const char* p;
    size_t n = 0;

    if (size == 0 || snprintf(pat, sizeof(pat), "\"%s\"", key) >= (int)sizeof(pat)) {
        return -1;
    }
    for (p = json; (p = strstr(p, pat)) != NULL; p += strlen(pat)) {
        const char* q = p + strlen(pat);
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') {
            q++;
        }
        if (*q != ':') {
            continue; /* the key's text as a value somewhere */
        }
        q++;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') {
            q++;
        }
        if (*q != '"') {
            return -1; /* null or not a string */
        }
        for (q++; *q && *q != '"'; q++) {
            char c = *q;
            if (c == '\\' && q[1]) {
                q++;
                c = *q == 'n' ? '\n' : *q == 't' ? '\t' : *q;
                if (*q == 'u') {
                    int k = 0;
                    while (k < 4 && q[1] && q[1] != '"') {
                        q++;
                        k++;
                    }
                    c = '?';
                }
            }
            if (n + 1 >= size) {
                return -1;
            }
            out[n++] = c;
        }
        out[n] = '\0';
        return (int)n;
    }
    return -1;
}

int
kn_error_kind(const char* body) {
    if (strstr(body, "invalid_credentials")) {
        return KN_ERR_CREDENTIALS;
    }
    if (strstr(body, "email_not_verified")) {
        return KN_ERR_EMAIL;
    }
    if (strstr(body, "rate_limited")) {
        return KN_ERR_RATE;
    }
    return KN_ERR_OTHER;
}

int
kn_http_status(const char* resp, const char** body) {
    int code;
    const char* sep;

    if (sscanf(resp, "HTTP/%*d.%*d %d", &code) != 1) {
        return -1;
    }
    sep = strstr(resp, "\r\n\r\n");
    if (body) {
        *body = sep ? sep + 4 : NULL;
    }
    return code;
}
