#include <string.h>

#include "dcapi_text.h"

int
dcapi_caps_parse(const char* ext24, uint32_t* caps) {
    uint32_t v = 0;

    if (memcmp(ext24, "dc1 ", 4) != 0) {
        return 0;
    }
    for (int i = 0; i < 8; i++) {
        char c = ext24[4 + i];
        uint32_t d;

        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            d = (uint32_t)(c - 'A' + 10);
        } else {
            return 0;
        }
        v = (v << 4) | d;
    }
    *caps = v;
    return 1;
}

int
dcapi_build(char* out, size_t out_cap, const char* ops) {
    size_t n = strlen(ops);

    if (4 + n > DCAPI_REQ_MAX || 4 + n + 1 > out_cap) {
        return -1;
    }
    memcpy(out, "dc1\n", 4);
    memcpy(out + 4, ops, n + 1);
    return (int)(4 + n);
}

int
dcapi_answer_ok(const char* answer) {
    return strncmp(answer, "dc1", 3) == 0 && (answer[3] == '\n' || answer[3] == '\r' || answer[3] == 0);
}

int
dcapi_answer(const char* answer, const char* key, char* val, size_t val_cap) {
    size_t klen = strlen(key);
    const char* p = answer;

    while (*p) {
        const char* end = p;
        int ok;

        while (*end && *end != '\n') {
            end++;
        }
        ok = strncmp(p, "ok ", 3) == 0;
        if ((ok || strncmp(p, "err ", 4) == 0)) {
            const char* k = p + (ok ? 3 : 4);

            if ((size_t)(end - k) >= klen && memcmp(k, key, klen) == 0 && (k[klen] == ' ' || k + klen == end)) {
                const char* v = k + klen;
                size_t n;

                if (v < end && *v == ' ') {
                    v++;
                }
                n = (size_t)(end - v);
                while (n && (v[n - 1] == '\r')) {
                    n--;
                }
                if (val_cap) {
                    if (n > val_cap - 1) {
                        n = val_cap - 1;
                    }
                    memcpy(val, v, n);
                    val[n] = 0;
                }
                return ok;
            }
        }
        p = *end ? end + 1 : end;
    }
    return -1;
}
