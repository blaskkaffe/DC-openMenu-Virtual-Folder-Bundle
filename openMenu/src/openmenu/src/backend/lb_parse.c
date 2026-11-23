/*
 * File: lb_parse.c
 * Project: openmenu
 * See lb_parse.h.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "backend/lb_parse.h"

#define CELL_LEN 40
#define COLS_MAX 12

typedef struct {
    lb_line_t* out;
    int max, count;
    char text[LB_LINE_LEN * 2]; /* the text being collected (a heading, a cell or a list item) */
    size_t tn;
    int collecting;             /* 0 none, 1 heading, 2 cell, 3 list item */
    int is_th;
    char cells[COLS_MAX][CELL_LEN];
    int ncells;
    char cols[COLS_MAX][CELL_LEN]; /* the column titles of the table */
    int ncols;
    int in_row;
} parser;

/* src into dst[cap], cut when too long */
static void
copy(char* dst, size_t cap, const char* src) {
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void
put(parser* ps, char c) {
    if (!ps->collecting) {
        return;
    }
    if (c == '\t' || c == '\r' || c == '\n') {
        c = ' ';
    }
    if (c == ' ' && (ps->tn == 0 || ps->text[ps->tn - 1] == ' ')) {
        return;
    }
    if (ps->tn + 1 < sizeof(ps->text)) {
        ps->text[ps->tn++] = c;
        ps->text[ps->tn] = '\0';
    }
}

static void
emit(parser* ps, const char* text, int kind) {
    if (ps->count >= ps->max || !text[0]) {
        return;
    }
    copy(ps->out[ps->count].text, LB_LINE_LEN, text);
    ps->out[ps->count++].kind = (unsigned char)kind;
}

static void
trim(parser* ps) {
    while (ps->tn > 0 && ps->text[ps->tn - 1] == ' ') {
        ps->text[--ps->tn] = '\0';
    }
}

static void
start(parser* ps, int what) {
    ps->collecting = what;
    ps->tn = 0;
    ps->text[0] = '\0';
}

/* The cell being collected is over (also when the page leaves out the closing tag). */
static void
end_cell(parser* ps) {
    if (ps->collecting == 2) {
        trim(ps);
        if (ps->ncells < COLS_MAX) {
            copy(ps->cells[ps->ncells++], CELL_LEN, ps->text);
        }
        ps->collecting = 0;
    }
}

/* A row is over: its cells (without the "Flag" column) are one line. */
static void
end_row(parser* ps) {
    end_cell(ps);
    char line[LB_LINE_LEN * 2];
    size_t n = 0;
    line[0] = '\0';
    for (int i = 0; i < ps->ncells; i++) {
        if (ps->is_th) {
            copy(ps->cols[i], CELL_LEN, ps->cells[i]);
        } else if (i < ps->ncols && !strcasecmp(ps->cols[i], "Flag")) {
            continue;
        }
        if (ps->is_th && !strcasecmp(ps->cells[i], "Flag")) {
            continue;
        }
        size_t cl = strlen(ps->cells[i]);
        if (n + cl + 3 >= sizeof(line)) {
            break;
        }
        if (n) {
            line[n++] = ' ';
            line[n++] = ' ';
        }
        memcpy(line + n, ps->cells[i], cl);
        n += cl;
        line[n] = '\0';
    }
    if (ps->is_th) {
        ps->ncols = ps->ncells;
    }
    emit(ps, line, ps->is_th ? LB_COLS : LB_ROW);
    ps->ncells = 0;
    ps->in_row = 0;
}

static int
ci_prefix(const char* p, const char* word) {
    for (; *word; p++, word++) {
        if (tolower((unsigned char)*p) != *word) {
            return 0;
        }
    }
    return 1;
}

static const char*
ci_find(const char* p, const char* word) {
    for (; *p; p++) {
        if (ci_prefix(p, word)) {
            return p;
        }
    }
    return NULL;
}

static int
entity(parser* ps, const char* p) {
    static const struct {
        const char* name;
        char c;
    } names[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&nbsp;", ' '}, {"&#39;", '\''},
                 {"&rsquo;", '\''}, {"&lsquo;", '\''}, {"&ldquo;", '"'}, {"&rdquo;", '"'}, {"&ndash;", '-'}, {"&mdash;", '-'}, {"&prime;", '\''}, {"&Prime;", '"'}};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        size_t n = strlen(names[i].name);
        if (!strncmp(p, names[i].name, n)) {
            put(ps, names[i].c);
            return (int)n;
        }
    }
    if (p[1] == '#') {
        char* end;
        long v = p[2] == 'x' || p[2] == 'X' ? strtol(p + 3, &end, 16) : strtol(p + 2, &end, 10);
        if (*end == ';' && end - p < 10) {
            put(ps, v >= 32 && v < 127 ? (char)v
                    : (v == 8217 || v == 8216 || v == 8242 ? '\'' : v == 8220 || v == 8221 || v == 8243 ? '"' : v == 8211 || v == 8212 ? '-' : ' '));
            return (int)(end - p) + 1;
        }
    }
    return 0;
}

/* The ASCII look-alike of a UTF-8 sequence (the font has ASCII only), and its length. */
static int
utf8(parser* ps, const unsigned char* p) {
    int n = *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : *p >= 0xC0 ? 2 : 1;
    char c = 0;
    if (p[0] == 0xE2 && p[1] == 0x80) {
        c = p[2] == 0x98 || p[2] == 0x99 || p[2] == 0xB2 ? '\'' : p[2] == 0x9C || p[2] == 0x9D || p[2] == 0xB3 ? '"' : p[2] == 0x93 || p[2] == 0x94 ? '-' : 0;
    } else if (p[0] == 0xC2 && p[1] == 0xA0) {
        c = ' ';
    }
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            n = i;
            break;
        }
    }
    if (c) {
        put(ps, c);
    }
    return n;
}

int
lb_parse_html(const char* html, lb_line_t* out, int max) {
    parser ps;
    memset(&ps, 0, sizeof(ps));
    ps.out = out;
    ps.max = max;
    for (const char* p = html; *p;) {
        if (*p == '<') {
            if (!strncmp(p, "<!--", 4)) {
                const char* e = strstr(p + 4, "-->");
                p = e ? e + 3 : p + strlen(p);
                continue;
            }
            const char* q = p + 1;
            int closing = *q == '/';
            q += closing;
            if (!isalpha((unsigned char)*q)) { /* a lone "<" in the text */
                put(&ps, '<');
                p++;
                continue;
            }
            char name[8] = "";
            int k = 0;
            while (isalnum((unsigned char)q[k]) && k < 7) {
                name[k] = (char)tolower((unsigned char)q[k]);
                k++;
            }
            name[k] = '\0';
            const char* gt = strchr(p, '>');
            if (!gt) {
                break;
            }
            if (!closing && (!strcmp(name, "script") || !strcmp(name, "style"))) {
                const char* e = ci_find(gt, closing ? "" : (name[1] == 'c' ? "</script" : "</style"));
                const char* eg = e ? strchr(e, '>') : NULL;
                p = eg ? eg + 1 : gt + strlen(gt);
                continue;
            }
            if (name[0] == 'h' && name[1] >= '1' && name[1] <= '4' && !name[2]) {
                if (closing && ps.collecting == 1) {
                    trim(&ps);
                    emit(&ps, ps.text, LB_HEAD);
                    ps.collecting = 0;
                } else if (!closing) {
                    start(&ps, 1);
                }
            } else if (!strcmp(name, "table")) {
                if (closing && ps.in_row) {
                    end_row(&ps);
                } else if (!closing) {
                    ps.ncols = 0;
                }
            } else if (!strcmp(name, "tr")) {
                if (closing) {
                    if (ps.in_row) {
                        end_row(&ps);
                    }
                } else {
                    if (ps.in_row) {
                        end_row(&ps);
                    }
                    ps.in_row = 1;
                    ps.ncells = 0;
                    ps.is_th = 0;
                }
            } else if (!strcmp(name, "td") || !strcmp(name, "th")) {
                if (closing) {
                    end_cell(&ps);
                } else if (ps.in_row) {
                    end_cell(&ps);
                    ps.is_th = name[1] == 'h';
                    start(&ps, 2);
                }
            } else if (!strcmp(name, "li")) {
                if (closing && ps.collecting == 3) {
                    trim(&ps);
                    emit(&ps, ps.text, LB_ROW);
                    ps.collecting = 0;
                } else if (!closing) {
                    start(&ps, 3);
                }
            } else if (!strcmp(name, "br") || (!strcmp(name, "p") && ps.collecting == 3)) {
                put(&ps, ' ');
            }
            p = gt + 1;
            continue;
        }
        if (*p == '&') {
            int n = entity(&ps, p);
            if (n) {
                p += n;
                continue;
            }
        }
        if ((unsigned char)*p >= 0x80) {
            p += utf8(&ps, (const unsigned char*)p);
            continue;
        }
        put(&ps, *p);
        p++;
    }
    if (ps.in_row) {
        end_row(&ps);
    }
    return ps.count;
}
