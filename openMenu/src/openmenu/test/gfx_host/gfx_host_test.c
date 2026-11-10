/*
 * gfx_host_test: runs the PVR backend (openmenu/src/gfx.c) on a PC against stand-ins for KOS and writes everything it
 * would send to the PVR: the bytes of every pvr_prim() (polygon headers and vertices), the lists, the texture uploads.
 * Needs no BIOS ROM (the engine's built-in models, the background, text, panels). It does not check the picture; it
 * makes a change to gfx.c that should not alter the output provable: run it before and after and compare.
 *
 *   cd openMenu/src
 *   gcc -O2 -Wall -Wextra -DBIOS_FONT_ROM=host_font -DGFX_REGION_BYTE=host_region -include openmenu/test/gfx_host/host_font.h -o gfx_host_test \
 *       openmenu/test/gfx_host/gfx_host_test.c openmenu/src/gfx.c \
 *       bios_engine/src/[a-z]*.c bios_rom/src/[a-z]*.c -Iopenmenu/test/gfx_host/kos -I../external/uthash/include \
 *       -Ibios_engine/include -Ibios_rom/include -Iopenmenu_shared/include -Iopenmenu/src -lm
 *   ./gfx_host_test > after.txt ; cmp before.txt after.txt
 *
 * With a BIOS dump it also measures the screens: `./gfx_host_test --survey dc_boot.bin [frames]` builds main menu, game list,
 * settings, card grid and CD player as ui_bios.c does and prints per screen the host time of a frame, triangles, TA vertices
 * and headers. Time is x86: compare two builds of the code (alternate the runs, pin one core: taskset -c 1), never read it as
 * console fps. The dump stays outside the repo (git-ignored).
 *
 * The split of the stream into pvr_prim() calls is not part of the output (only the bytes are), so the batching may change.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/timer.h>
#include <dc/biosfont.h>
#include <dc/pvr.h>
#include <kos/fs.h>
#include <kos/sem.h>
#include <kos/thread.h>

#include <time.h>

#include <backend/dat_format.h>
#include <texture/serial_sanitize.h>

#include "gfx.h"
#include <bios_cdplayer.h>
#include <bios_files.h>
#include <bios_list.h>
#include <bios_menu.h>
#include <bios_page.h>
#include <bios_rom.h>

/* the engine's test scene (models at rest, turning, tinted, the grid, the scene switches) */
#define main scene_dump_main
#include "../../../bios_engine/tools/scene_dump.c"
#undef main

static int quiet; /* survey mode: no listing */
static uint32_t out_hash = 2166136261u;
static unsigned long prim_bytes, prim_calls, uploads;

/* Video memory events (allocations, uploads) are written at the end of the frame they happen in: when they fall between
 * two pvr_prim() calls depends on the batching, which is not part of the output. */
static char events[1 << 16];
static size_t events_n;

static void
event(const char* fmt, unsigned long a, unsigned long b, unsigned long c) {
    if (events_n < sizeof(events) - 96) {
        if (!quiet) events_n += (size_t)snprintf(events + events_n, 96, fmt, a, b, c);
    }
}

static void
events_write(void) {
    fwrite(events, 1, events_n, stdout);
    events_n = 0;
}

static void
hash_bytes(const void* p, size_t n) {
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; i++) {
        out_hash = (out_hash ^ b[i]) * 16777619u;
    }
}

/* ---- KOS ---- */

uint64_t
timer_us_gettime64(void) {
    static uint64_t t;
    return t += 7;
}

/* gfx.c reads the BIOS font at its address in the console's memory: built with -DBIOS_FONT_ROM=host_font it reads this */
unsigned char host_font[0x100000];
unsigned char host_region = 1; /* USA */

uint8_t*
syscall_font_address(void) {
    return host_font;
}

file_t
fs_open(const char* path, int mode) {
    (void)path;
    (void)mode;
    return FILEHND_INVALID;
}

int
fs_close(file_t fd) {
    (void)fd;
    return 0;
}

ssize_t
fs_read(file_t fd, void* buf, size_t n) {
    (void)fd;
    (void)buf;
    (void)n;
    return -1;
}

size_t
fs_total(file_t fd) {
    (void)fd;
    return 0;
}

int
sem_init(semaphore_t* s, int value) {
    s->count = value;
    return 0;
}

int
sem_wait(semaphore_t* s) {
    (void)s;
    return 0;
}

int
sem_signal(semaphore_t* s) {
    s->count++;
    return 0;
}

kthread_t*
thd_create(int detach, void* (*fn)(void*), void* arg) {
    (void)detach;
    (void)fn;
    (void)arg;
    return NULL; /* the art loader never runs: no art, the placeholders are drawn */
}

int
thd_set_prio(kthread_t* t, int prio) {
    (void)t;
    (void)prio;
    return 0;
}

int
DAT_init(dat_file* bin) {
    memset(bin, 0, sizeof(*bin));
    return 0;
}

int
DAT_load_parse(dat_file* bin, const char* path) {
    (void)bin;
    (void)path;
    return 0;
}

uint32_t
DAT_get_offset_by_ID(const dat_file* bin, const char* ID) {
    (void)bin;
    (void)ID;
    return 0;
}

int
DAT_read_file_by_ID(const dat_file* bin, const char* ID, void* buf) {
    (void)bin;
    (void)ID;
    (void)buf;
    return 0;
}

const char*
serial_santize_art(const char* id) {
    return id;
}

/* ---- PVR ---- */

int
pvr_init(pvr_init_params_t* params) {
    if (!quiet) printf("init dma %d presort %d\n", params->dma_enabled, params->autosort_disabled);
    return 0;
}

int
pvr_shutdown(void) {
    return 0;
}

void*
pvr_set_vertbuf(pvr_list_t list, void* buffer, size_t len) {
    (void)buffer;
    if (!quiet) printf("vertbuf %u %zu\n", (unsigned)list, len);
    return NULL;
}

/* Video memory: addresses handed out in order and never reused, so the same allocations give the same headers. */
pvr_ptr_t
pvr_mem_malloc(size_t size) {
    static uintptr_t next = 0x04000000u;
    uintptr_t p = next;
    next += (size + 31u) & ~(uintptr_t)31u;
    event("vram alloc %08lx %lu\n", (unsigned long)p, (unsigned long)size, 0);
    return (pvr_ptr_t)p;
}

void
pvr_mem_free(pvr_ptr_t chunk) {
    event("vram free %08lx\n", (unsigned long)(uintptr_t)chunk, 0, 0);
}

void
pvr_txr_load(const void* src, pvr_ptr_t dst, size_t count) {
    uint32_t h = 2166136261u;
    const unsigned char* b = (const unsigned char*)src;
    for (size_t i = 0; i < count; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    event("upload %08lx %lu %08lx\n", (unsigned long)(uintptr_t)dst, (unsigned long)count, (unsigned long)h);
    uploads++;
}

void
pvr_poly_cxt_col(pvr_poly_cxt_t* dst, pvr_list_t list) {
    memset(dst, 0, sizeof(*dst));
    dst->list_type = (int)list;
    dst->blend.src = list == PVR_LIST_OP_POLY ? PVR_BLEND_ONE : PVR_BLEND_SRCALPHA;
    dst->blend.dst = list == PVR_LIST_OP_POLY ? PVR_BLEND_ZERO : PVR_BLEND_INVSRCALPHA;
    dst->gen.culling = PVR_CULLING_CCW;
}

void
pvr_poly_cxt_txr(pvr_poly_cxt_t* dst, pvr_list_t list, int textureformat, int tw, int th, pvr_ptr_t textureaddr, int filtering) {
    pvr_poly_cxt_col(dst, list);
    dst->txr.enable = 1;
    dst->txr.filter = filtering;
    dst->txr.width = tw;
    dst->txr.height = th;
    dst->txr.format = textureformat;
    dst->txr.base = textureaddr;
}

/* Not the PVR's packing: one line per header with every field gfx.c sets, and a stand-in of 32 bytes in the stream. */
void
pvr_poly_compile(pvr_poly_hdr_t* dst, const pvr_poly_cxt_t* c) {
    memset(dst, 0, sizeof(*dst));
    dst->w[0] = 0x80000000u | (uint32_t)c->list_type << 24 | (uint32_t)c->txr.enable << 3 | (uint32_t)c->gen.specular << 2;
    dst->w[1] = (uint32_t)c->gen.culling << 27 | (uint32_t)c->depth.write << 26 | (uint32_t)c->depth.comparison << 29;
    dst->w[2] = (uint32_t)c->blend.src << 29 | (uint32_t)c->blend.dst << 26 | (uint32_t)c->txr.uv_flip << 17
                | (uint32_t)c->txr.uv_clamp << 15 | (uint32_t)c->txr.filter << 13 | (uint32_t)c->txr.width << 4;
    dst->w[3] = (uint32_t)c->txr.format ^ (uint32_t)(uintptr_t)c->txr.base;
    dst->w[4] = (uint32_t)c->txr.height;
    dst->w[5] = (uint32_t)c->gen.alpha | (uint32_t)c->gen.shading << 1 | (uint32_t)c->txr.alpha << 2 | (uint32_t)c->txr.env << 3
                | (uint32_t)c->txr.mipmap << 5;
}

int
pvr_prim(const void* data, size_t size) {
    const uint32_t* w = (const uint32_t*)data;
    if (quiet) {
        prim_bytes += size;
        prim_calls++;
        return 0;
    }
    for (size_t i = 0; i + 32 <= size; i += 32, w += 8) {
        if ((w[0] >> 28) >= 0xE) {
            const pvr_vertex_t* v = (const pvr_vertex_t*)w;
            printf("v%c %.9g %.9g %.9g %.9g %.9g %08x %08x\n", w[0] == PVR_CMD_VERTEX_EOL ? '.' : ' ', (double)v->x, (double)v->y,
                   (double)v->z, (double)v->u, (double)v->v, (unsigned)v->argb, (unsigned)v->oargb);
        } else {
            printf("hdr %08x %08x %08x %08x %08x %08x\n", (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3],
                   (unsigned)w[4], (unsigned)w[5]);
        }
    }
    hash_bytes(data, size);
    prim_bytes += size;
    prim_calls++;
    return 0;
}

int
pvr_wait_ready(void) {
    return 0;
}

int
pvr_get_stats(pvr_stats_t* stat) {
    memset(stat, 0, sizeof(*stat));
    return 0;
}

void
pvr_set_bg_color(float r, float g, float b) {
    (void)r;
    (void)g;
    (void)b;
}

void
pvr_scene_begin(void) {
    if (!quiet) printf("scene\n");
}

int
pvr_list_begin(pvr_list_t list) {
    if (!quiet) printf("list %u\n", (unsigned)list);
    return 0;
}

int
pvr_list_finish(void) {
    if (!quiet) printf("list end\n");
    return 0;
}

int
pvr_scene_finish(void) {
    events_write();
    if (!quiet) printf("scene end\n");
    return 0;
}

int
pvr_set_presort_mode(int presort) {
    (void)presort;
    return 0;
}

/* ---- the frames ---- */

static void
overlay(int f) {
    static const char* const words[] = {"Play", "Settings", "A long title that is cut: The Game of the Year Edition (Disc 2)",
                                        "Files", "12:34", "", "Shenmue II", "x"};
    const float z = 0.6f;
    gfx_pane(40.0f, 60.0f + (float)f, 300.0f, 120.0f, z, 0xC0283848u);
    gfx_rrect(360.0f, 80.0f, 200.0f + (float)(f % 7), 60.0f, 12.0f, z, 0x80FFFFFFu);
    gfx_rrect(360.0f, 160.0f, 10.0f, 80.0f, 30.0f, z, 0xFF102030u); /* radius larger than the box */
    gfx_rect(10.0f, 400.0f, 620.0f, 2.0f, z, 0xFFFF0000u);
    for (int i = 0; i < 8; i++) {
        gfx_text(words[(i + f) % 8], 50.0f, 70.0f + 26.0f * (float)i, z + 0.01f, 0xFFFFFFFFu, i & 1);
    }
    gfx_text_scaled(words[f % 8], 300.0f, 300.0f, z, 0xFF80C0FFu, 0.5f + 0.25f * (float)(f % 3));
    gfx_text(NULL, 0.0f, 0.0f, z, 0xFFFFFFFFu, 0);
    char unique[24];
    snprintf(unique, sizeof(unique), "frame %d", f / 3); /* new strings: the text slots are reused in time */
    gfx_text(unique, 400.0f, 420.0f, z, 0xFFFFFF00u, 0);
    gfx_art_box("T-1234N", 420.0f, 260.0f, 128.0f, 128.0f, z);
    gfx_logo_badge();
}

/* ---- survey: the screens of the menu with the real ROM, what each one costs a frame ----
 *   gfx_host_test --survey dc_boot.bin [frames]
 * Each screen is built and drawn through gfx.c as ui_bios.c does (background, objects, text labels), frame after frame; the
 * time is the host's (x86), good for comparing two builds of the code, not for the console's frame rate. The counts are
 * what the console's TA gets. Art pictures (ICON.DAT, BOX.DAT) and the case panel are not part of it. */
static double
now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}

static void
page_row(void* user, int index, bpage_row* out) {
    (void)user;
    out->icon = index < 4 ? BPAGE_ICON_BIOS(index) : BPAGE_ICON_DIGIT(index - 4);
}

static const char* const titles[] = {"Shenmue II", "Crazy Taxi 2 (Disc 1)", "Sonic Adventure 2: The Trial", "Jet Set Radio",
                                     "Skies of Arcadia (Disc 2)", "Power Stone 2", "Soul Calibur", "Rez", "Ikaruga",
                                     "Marvel vs. Capcom 2", "Grandia II", "Space Channel 5: Part 2"};
enum { SC_MAIN, SC_MAIN_MOVE, SC_LIST, SC_LIST_SCROLL, SC_SETTINGS, SC_FILES, SC_CD, SC_N };
static const char* const sc_name[SC_N] = {"main menu, at rest", "main menu, changing icon", "game list, at rest",
                                          "game list, scrolling", "settings page", "memory card grid", "CD player"};

static void
survey_one(const bios_rom* rom, int sc, int frames) {
    static bmenu menu;
    static blist list;
    static bpage page;
    static bfiles files;
    static bcdplayer cd;
    bmenu_init(&menu, rom, NULL);
    menu.hw_autosort = gfx_set_autosort(1); /* every screen of ui_bios.c: the PVR sorts, not the CPU */
    switch (sc) {
        case SC_MAIN:
        case SC_MAIN_MOVE:
            bmenu_show_main(&menu, 0);
            break;
        case SC_LIST:
        case SC_LIST_SCROLL:
            blist_open(&list, &menu, 7, 300);
            blist_goto(&list, 10);
            break;
        case SC_SETTINGS:
            bpage_open(&page, &menu, 10);
            break;
        case SC_FILES:
            bfiles_open(&files, &menu, 0);
            files.present[0] = files.present[2] = files.present[3] = 1;
            break;
        case SC_CD:
            bcd_open(&cd, &menu, 1, 12, 3725);
            break;
    }
    gfx_stats st;
    unsigned long tris = 0, calls_before = prim_calls, bytes_before = prim_bytes;
    unsigned text_uploads = 0, headers = 0, vertices = 0;
    double total = 0, worst = 0, setup_frames = 90;
    int measured = 0;
    gfx_stats_take(&st);
    for (int f = 0; f < frames + (int)setup_frames; f++) {
        double t0 = now_us();
        if (sc == SC_MAIN_MOVE && f % 45 == 0) {
            bmenu_select(&menu, (f / 45) % 4);
        }
        if (sc == SC_LIST_SCROLL && f % 5 == 0) {
            blist_move(&list, 1);
        }
        bmenu_update(&menu);
        for (int s = 0; s < 7; s++) {
            gfx_set_label((uint16_t)BLIST_TEXT_ID(s), titles[(s + (sc == SC_LIST_SCROLL ? f / 5 : 0)) % 12]);
            gfx_set_label((uint16_t)BPAGE_TEXT_ID(s), titles[s % 12]);
        }
        if (sc <= SC_MAIN_MOVE) {
            static const char* const icons[] = {"Play", "File", "Music", "Settings"};
            for (int i = 0; i < 4; i++) {
                gfx_set_label((uint16_t)BMENU_ID_ICON(i), icons[i]);
            }
            gfx_set_label(BMENU_ID_HEADER, "10/10/2026 10:27");
        }
        gfx_begin_frame(0xFF204080u, 0xFF80C0FFu);
        bscene_draw_background(&menu.bg, gfx_sink());
        switch (sc) {
            case SC_MAIN:
            case SC_MAIN_MOVE:
                bmenu_draw_objects(&menu, gfx_sink());
                break;
            case SC_LIST:
            case SC_LIST_SCROLL:
                list.disc_number = 2;
                blist_sync(&list);
                blist_draw(&list, gfx_sink());
                break;
            case SC_SETTINGS:
                bpage_sync(&page, page_row, NULL);
                bpage_draw(&page, gfx_sink());
                break;
            case SC_FILES:
                bfiles_sync(&files);
                bfiles_draw(&files, gfx_sink());
                break;
            case SC_CD:
                bcd_sync(&cd);
                bcd_draw(&cd, gfx_sink());
                break;
        }
        gfx_end_frame();
        double dt = now_us() - t0;
        gfx_stats_take(&st);
        if (f >= (int)setup_frames) { /* the first frames fill the caches: not counted */
            total += dt;
            if (dt > worst) {
                worst = dt;
            }
            tris += gfx_triangles();
            text_uploads += st.text_uploads;
            headers += st.headers;
            vertices += st.vertices;
            measured++;
        }
    }
    (void)calls_before;
    (void)bytes_before;
    bmenu_free(&menu);
    printf("%-26s %7.0f us/frame (worst %6.0f)  %5lu tris  %5u TA vertices  %3u headers  %4.1f text pictures\n", sc_name[sc],
           total / measured, worst, tris / (unsigned long)measured, vertices / (unsigned)measured, headers / (unsigned)measured,
           (double)text_uploads / measured);
}

static int
survey(const char* rom_path, int frames) {
    static uint8_t data[BIOS_ROM_SIZE];
    FILE* f = fopen(rom_path, "rb");
    size_t got = f ? fread(data, 1, sizeof(data), f) : 0;
    if (f) {
        fclose(f);
    }
    static bios_rom rom;
    if (bios_rom_init(&rom, data, got) != 0) {
        fprintf(stderr, "not a usable BIOS: %s\n", rom_path);
        return 1;
    }
    quiet = 1;
    memcpy(host_font, data + 0x100020, sizeof(host_font) - 0x20);
    if (gfx_init(&rom) != 0) {
        return 1;
    }
    for (int sc = 0; sc < SC_N; sc++) {
        survey_one(&rom, sc, frames);
    }
    return 0;
}

int
main(int argc, char** argv) {
    if (argc > 2 && !strcmp(argv[1], "--survey")) {
        return survey(argv[2], argc > 3 ? atoi(argv[3]) : 600);
    }
    int frames = argc > 1 ? atoi(argv[1]) : 60;
    uint32_t r = 12345u; /* the font: any bytes, the same every run */
    for (size_t i = 0; i < sizeof(host_font); i++) {
        r = r * 1664525u + 1013904223u;
        host_font[i] = (unsigned char)(r >> 24);
    }

    static bscene scene;
    static dcbg_state bg;
    if (gfx_init(NULL) != 0) {
        return 1;
    }
    bscene_init(&scene, NULL);
    dcbg_init(&bg, 0, 0);
    gfx_load_logo_texts();
    unsigned short icon[32 * 32];
    for (int i = 0; i < 32 * 32; i++) {
        icon[i] = (unsigned short)(0xF000 | i);
    }
    int img = gfx_dyn_create(icon, 32, 32);
    bscene_draw_background(&bg, gfx_warm_sink());

    for (int f = 0; f < frames; f++) {
        printf("F %d\n", f);
        gfx_set_aspect((f / 10) & 1);
        gfx_set_dark((f / 20) & 1);
        gfx_set_logo_mode(f % 3);
        gfx_set_autosort(f & 1);
        gfx_begin_frame(0xFF204080u, 0xFF80C0FFu);
        dcbg_step(&bg);
        bscene_draw_background(&bg, gfx_sink());
        if (f % 4 == 3) {
            const bscene_sink* sorted = bscene_sort_begin(gfx_sink());
            draw_set(&scene, f, sorted);
            bscene_sort_end();
        } else {
            draw_set(&scene, f, gfx_sink());
        }
        overlay(f);
        gfx_image(img, 500.0f, 20.0f, 32.0f, 32.0f, 0.7f);
        gfx_image(img + 1, 0.0f, 0.0f, 1.0f, 1.0f, 0.7f); /* not there: nothing */
        gfx_end_frame();
        printf("tris %u\n", gfx_triangles());
    }
    gfx_dyn_free_all();
    events_write();
    printf("# %lu bytes, %lu uploads, hash %08x\n", prim_bytes, uploads, (unsigned)out_hash);
    fprintf(stderr, "%lu bytes in %lu pvr_prim calls, %lu uploads, hash %08x\n", prim_bytes, prim_calls, uploads, (unsigned)out_hash);
    return 0;
}
