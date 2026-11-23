#include "backend/picolink_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
pl_info_init(pl_info_t* info) {
    memset(info, 0, sizeof(*info));
    info->dcnet = -1;
    info->scan_n = -1;
}

static void
copy(char* dst, size_t cap, const char* src, size_t n) {
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int
printable(const char* s) {
    for (; *s; s++) {
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E) {
            return 0;
        }
    }
    return 1;
}

int
pl_pass_ok(const char* pass) {
    size_t n = strlen(pass);
    if (n == 0) {
        return 1;
    }
    return n >= 8 && n <= 63 && printable(pass) && pass[0] != ' ' && pass[n - 1] != ' ';
}

size_t
pl_req_status(char* out, size_t cap) {
    int n = snprintf(out, cap, "dc1\nget hello\nget net.*\nget wifi.*\n");
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

size_t
pl_req_select(char* out, size_t cap, int dcnet) {
    int n = snprintf(out, cap, "dc1\ncall net.select %s\nget net.*\n", dcnet ? "dcnet" : "dcnow");
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

size_t
pl_req_scan(char* out, size_t cap) {
    int n = snprintf(out, cap, "dc1\ncall wifi.scan\nget wifi.*\n");
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

size_t
pl_req_join(char* out, size_t cap, int index, const char* name, const char* pass) {
    int n;
    if (!pl_pass_ok(pass)) {
        return 0;
    }
    if (index >= 0) {
        n = snprintf(out, cap, "dc1\nset wifi.pass %s\ncall wifi.join #%d\nget wifi.*\n", pass, index);
    } else {
        size_t l = name ? strlen(name) : 0;
        if (l == 0 || l > 32 || !printable(name)) {
            return 0;
        }
        n = snprintf(out, cap, "dc1\nset wifi.pass %s\ncall wifi.join %s\nget wifi.*\n", pass, name);
    }
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

static int
has_cap(const char* caps, const char* cap) {
    size_t l = strlen(cap);
    for (const char* p = caps; *p;) {
        const char* e = strchr(p, ',');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n == l && !strncmp(p, cap, l)) {
            return 1;
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    return 0;
}

static void
apply(pl_info_t* info, const char* key, const char* val) {
    if (!strcmp(key, "hello.api")) {
        info->present = 1;
    } else if (!strcmp(key, "hello.caps")) {
        info->has_net = has_cap(val, "net");
        info->has_wifi = has_cap(val, "wifi");
    } else if (!strcmp(key, "hello.name")) {
        copy(info->name, sizeof(info->name), val, strlen(val));
    } else if (!strcmp(key, "net.selected")) {
        info->dcnet = !strcmp(val, "dcnet") ? 1 : !strcmp(val, "dcnow") ? 0 : -1;
    } else if (!strcmp(key, "net.dcnet")) {
        info->dcnet_ok = !strcmp(val, "ok");
    } else if (!strcmp(key, "wifi.state")) {
        copy(info->wifi_state, sizeof(info->wifi_state), val, strlen(val));
    } else if (!strcmp(key, "wifi.ssid")) {
        copy(info->wifi_ssid, sizeof(info->wifi_ssid), val, strlen(val));
    } else if (!strcmp(key, "wifi.ip")) {
        copy(info->wifi_ip, sizeof(info->wifi_ip), val, strlen(val));
    } else if (!strcmp(key, "wifi.error")) {
        copy(info->wifi_error, sizeof(info->wifi_error), val, strlen(val));
    } else if (!strcmp(key, "wifi.scan.n")) {
        info->scan_n = atoi(val);
        info->scan_have = 0;
    } else if (!strncmp(key, "wifi.scan.", 10)) {
        int i = atoi(key + 10);
        int rssi, open, used = 0;
        if (i >= 0 && i < PL_SCAN_MAX && sscanf(val, "%d %d %n", &rssi, &open, &used) >= 2 && used > 0) {
            pl_net_t* n = &info->scan[i];
            n->rssi = rssi;
            n->open = open != 0;
            copy(n->name, sizeof(n->name), val + used, strlen(val + used));
            if (i + 1 > info->scan_have) {
                info->scan_have = i + 1;
            }
        }
    }
}

int
pl_parse(const char* resp, pl_info_t* info) {
    int oks = 0;
    const char* p = resp;

    if (strncmp(resp, "dc1", 3) != 0) {
        return -1;
    }
    info->err[0] = '\0';
    if (strstr(resp, "ok wifi.state ")) { /* a full read of wifi.*: what it leaves out (no network saved, no address, no error) is gone */
        info->wifi_ssid[0] = info->wifi_ip[0] = info->wifi_error[0] = '\0';
    }
    while (*p) {
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char line[160];
        copy(line, sizeof(line), p, n);
        for (size_t l = strlen(line); l && line[l - 1] == '\r'; l--) {
            line[l - 1] = '\0';
        }
        if (!strncmp(line, "ok ", 3)) {
            char* key = line + 3;
            char* sp = strchr(key, ' ');
            const char* val = "";
            if (sp) {
                *sp = '\0';
                val = sp + 1;
            }
            oks++;
            apply(info, key, val);
        } else if (!strncmp(line, "err ", 4) && !info->err[0]) {
            copy(info->err, sizeof(info->err), line + 4, strlen(line + 4));
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    return oks;
}

int
pl_bars(int rssi) {
    return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : rssi >= -88 ? 1 : 0;
}
