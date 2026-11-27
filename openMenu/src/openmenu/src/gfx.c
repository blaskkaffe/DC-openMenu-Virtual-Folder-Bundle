/*
 * gfx: PVR backend for the BIOS-style menu, see gfx.h.
 */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include <arch/timer.h>
#include <dc/biosfont.h>
#include <dc/pvr.h>
#include <fcntl.h>
#include <kos/fs.h>
#include <kos/sem.h>
#include <kos/thread.h>

#include <bios_menu.h>
#include <bios_list.h>
#include <bios_text.h>
#include <bios_surface.h>
#include <bios_models.h>
#include <tex_decode.h>
#include <bios_page.h>
#include <backend/dat_format.h>
#include <texture/serial_sanitize.h>

#include "gfx.h"

#ifndef GFX_REGION_BYTE /* the console's region byte, set by the BIOS (gfx_host_test has its own) */
#define GFX_REGION_BYTE (*(volatile uint8_t*)0x8C000072)
#endif

#ifndef BIOS_FONT_ROM /* gfx_host_test puts its own bytes here */
#define BIOS_FONT_ROM ((const uint8_t*)0xA0100020u) /* what syscall_font_address() returns */
#endif

#define MAX_LOGO_FILE (256 * 1024)
#define MAX_TEXTURES 96
#define MAX_TEXT_ENTRIES 80 /* each key of the on-screen keyboard is a string of its own: 48 were too few (a slot is 2 KB for a single character) */
/* TEST ONLY: where the time of a frame's drawing goes, for the emulator measurement */
unsigned gfx_prof_us[8];
#define MAX_TEXT_CHARS 42
#define MAX_TEXT_W 512
#define MAX_LABELS 32 /* the main menu icons (4), the header clock (1), the game list rows (BLIST_MAX_SLOTS 13) and the Settings page rows (BPAGE_SLOTS 5) all keep theirs: 16 left the last two Settings rows blank once the list had been opened (it is, at start-up: bmenu_prewarm_screens) */
#define LABEL_CHARS 120 /* a label may hold several lines / columns, see sink_text */

/* Settings page text lines (bios_page): left aligned, label then a value column */
#define BG_Z 0.001f /* 1/w of the gradient quad: behind everything (the clouds are at ~0.0026) */
#define TEXT_Z_BIAS 0.00002f

typedef struct {
    int kind, a, b;
    pvr_ptr_t ptr;
    int w, h, fmt;
    int valid;
    int nearest; /* drawn pixel for pixel: no filtering (the text surfaces) */
} rom_tex;

typedef struct {
    char str[MAX_TEXT_CHARS + 1];
    pvr_ptr_t ptr;
    int w;    /* of the picture, a power of two */
    int used; /* columns of it that hold the text (btext_width) */
    uint32_t last_frame;
} text_tex;

static const bios_rom* g_rom;
static rom_tex rom_texes[MAX_TEXTURES];
static int num_rom_texes;
static rom_tex logo_tex;
static rom_tex logo_dcnet_tex;  /* the BIOS logo cropped to its swirl (mode 2: swirl + the D; "CNET" is drawn after it) */
static float logo_ox, logo_oy; /* top left of the header logo's quad this frame (it slides in with the bar) */
static int logo_seen;
static uint32_t logo_argb = 0xFFFFFFFFu; /* the colour the header logo's polygon is drawn with this frame (shading and the bar's fade) */
static int logo_mode;           /* see gfx_set_logo_mode */
static int logo_variants_tried;
static text_tex text_texes[MAX_TEXT_ENTRIES];
static uint32_t frame_no;
static unsigned tri_count;
static gfx_stats stats; /* since the last gfx_stats_take() */
/* Translucent polygons: 1 = drawn in the order they are submitted (no hardware sorting, much
 * cheaper; the menu submits back to front already), 0 = the PVR sorts them per pixel. */
#ifndef GFX_PRESORT
#define GFX_PRESORT 1
#endif
static int autosort = !GFX_PRESORT; /* see gfx_set_autosort */

/* Switching the sort mode per frame.
 * KOS 2.1.1's pvr_set_presort_mode() must not be used: it re-runs the whole tile-matrix set-up, which moves the
 * buffer's matrix 0x48 bytes further on each call (the start-up code skips the matrix header that way). Called every
 * frame, the matrix walks through video RAM: tiles in the lower right break into tall stripes and the PVR hangs on
 * the first frame. Later KOS fixed it by rewriting only the control words of the tiles; this is that fix, reaching
 * the buffer through KOS's internal PVR state (pvr_internal.h, found by CMake in the KOS source tree). Without that
 * header the mode stays what pvr_init set (GFX_PRESORT). */
#ifdef GFX_PVR_INTERNAL
#include <pvr_internal.h>
#define TILE_PRESORT (1u << 29)
static int tile_mode[2] = {-1, -1}; /* presort bit currently in each TA buffer's tile matrix, -1 unknown */
static int tile_mode_broken;        /* the matrix did not look as expected: never touch it */

static int
set_tile_presort(int presort) {
    int target = pvr_state.ta_target;
    if (tile_mode_broken || target < 0 || target > 1) {
        return 0;
    }
    if (tile_mode[target] == presort) {
        return 1;
    }
    /* tile_matrix points at the start-up tile; the screen tiles follow, 6 words each, column by column */
    volatile uint32_t* vr = (volatile uint32_t*)PVR_RAM_BASE + (pvr_state.ta_buffers[target].tile_matrix >> 2) + 6;
    const int tw = pvr_state.tw, th = pvr_state.th;
    if ((vr[0] & ~TILE_PRESORT) != 0 || (th > 1 && (vr[6] & ~TILE_PRESORT) != 0x100u)) {
        tile_mode_broken = 1;
        return 0;
    }
    for (int x = 0; x < tw; x++) {
        for (int y = 0; y < th; y++) {
            uint32_t w = ((uint32_t)y << 8) | ((uint32_t)x << 2) | (presort ? TILE_PRESORT : 0u);
            if (x == tw - 1 && y == th - 1) {
                w |= 1u << 31; /* last tile */
            }
            vr[0] = w;
            vr += 6;
        }
    }
    tile_mode[target] = presort;
    return 1;
}
#endif

static struct {
    uint16_t id;
    char text[LABEL_CHARS + 1];
} labels[MAX_LABELS];
static int num_labels;

/* Last polygon header sent in the translucent list; avoids re-sending identical state. */
static pvr_ptr_t last_ptr;
static int last_untextured;
static int last_uv;  /* NJ_TEX_* flip / clamp bits of the current header */
static int hdr_valid;

static uint16_t text_canvas[MAX_TEXT_W * GFX_LINE_H] __attribute__((aligned(32)));

#ifndef GFX_VERTEX_DMA
#define GFX_VERTEX_DMA 1
#endif
#define OP_DMA_BYTES (128 * 1024)  /* the gradient quad only */
#define TR_DMA_BYTES (1024 * 1024) /* two halves of 512 KB: 16000 vertices a frame */
static void *op_dma_buf, *tr_dma_buf;
static int cur_list_op = 1; /* the list pvr_prim() sends to now: 1 = OP, 0 = TR */
static unsigned frame_bytes[2]; /* sent so far this frame, per list: never past a buffer half (KOS only asserts) */

/* ---- Setup -------------------------------------------------------------------- */

int
gfx_init(const bios_rom* rom) {
    g_rom = rom;
    bsurf_init(rom);
    pvr_init_params_t params = {
        /* OP, OP modifier, TR, TR modifier, punch-through */
        {PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_0},
        512 * 1024, /* vertex buffer */
        GFX_VERTEX_DMA, /* the BIOS builds each list in RAM and sends it to the TA by DMA (0x8C0DA614): the CPU is free meanwhile */
        0,          /* no FSAA */
        GFX_PRESORT, /* 1 = translucent autosort disabled */
        1};         /* one extra set of object pointer blocks */
    int r = pvr_init(&params);
#if GFX_VERTEX_DMA
    if (r == 0) {
        /* KOS's vertex DMA mode: pvr_prim() copies into these (each buffer is split in two, one half per frame in flight) */
        op_dma_buf = memalign(32, OP_DMA_BYTES);
        tr_dma_buf = memalign(32, TR_DMA_BYTES);
        if (op_dma_buf && tr_dma_buf) {
            pvr_set_vertbuf(PVR_LIST_OP_POLY, op_dma_buf, OP_DMA_BYTES);
            pvr_set_vertbuf(PVR_LIST_TR_POLY, tr_dma_buf, TR_DMA_BYTES);
        } else {
            free(op_dma_buf);
            free(tr_dma_buf);
            op_dma_buf = tr_dma_buf = NULL;
            pvr_shutdown();
            params.dma_enabled = 0; /* no memory for the buffers: the store queues as before */
            r = pvr_init(&params);
        }
    }
#endif
    return r;
}

/* ---- ROM textures ----------------------------------------------------------------- */

static int
pvr_format(const bios_texture* t) {
    int fmt = 0;
    switch (t->pixel_format) {
        case BIOS_PVR_ARGB1555: fmt = PVR_TXRFMT_ARGB1555; break;
        case BIOS_PVR_RGB565: fmt = PVR_TXRFMT_RGB565; break;
        default: fmt = PVR_TXRFMT_ARGB4444; break;
    }
    if (t->data_type == BIOS_PVR_RECTANGLE) {
        fmt |= PVR_TXRFMT_NONTWIDDLED;
    } else {
        fmt |= PVR_TXRFMT_TWIDDLED;
    }
    if (t->data_type == BIOS_PVR_VQ) {
        fmt |= PVR_TXRFMT_VQ_ENABLE;
    }
    return fmt;
}

/* Copy a texture payload to video memory and fill in `g`. */
static void
upload_texture(const bios_texture* t, rom_tex* g) {
    size_t size = (t->data_size + 31) & ~(size_t)31;
    void* staging = memalign(32, size);
    if (!staging) {
        return;
    }
    memset(staging, 0, size);
    memcpy(staging, t->data, t->data_size);

    g->ptr = pvr_mem_malloc(size);
    if (g->ptr) {
        pvr_txr_load(staging, g->ptr, size);
        g->w = t->width;
        g->h = t->height;
        g->fmt = pvr_format(t);
        g->valid = 1;
    }
    free(staging);
}

/* ---- Game art (DISC.DAT / ICON.DAT / BOX.DAT on the menu disc) ---------------------------------- */

#define ART_SLOTS 20
#define ART_CHUNK_MAX (160 * 1024)

typedef struct {
    char key[20]; /* sanitized product id; box art entries start with '#' */
    rom_tex tex;
    int present; /* an entry exists (tex.valid tells if the picture could be loaded) */
    int loading; /* a job of the loader thread is reading it: do not evict */
    uint32_t last_use;
    uint32_t retry_at; /* the disc could not be read: ask again from this frame on (0: nothing to retry) */
    int band;          /* box fronts: the curved band at the cover's left edge (the US covers'): 0 not known, 1 white, 2 black */
} art_entry;

/* Pictures are read from the DATs by a thread of their own (reading the disc takes tens of
 * milliseconds and used to stall the frame); the main thread only uploads a finished one to video
 * memory, at most ART_UPLOADS_PER_FRAME per frame. */
#define ART_JOBS 3
#define ART_UPLOADS_PER_FRAME 1
enum { JOB_FREE = 0, JOB_QUEUED, JOB_READING, JOB_DONE, JOB_FAILED, JOB_ERROR };
#define ART_RETRY_FRAMES 60       /* a read error: try that picture again a second later */
#define DAT_RETRY_FRAMES 300      /* a DAT that could not be opened: try again five seconds later ... */
#define DAT_RETRIES 3             /* ... at most this often (a DAT that is not on the disc stays missing) */
typedef struct {
    volatile int state;
    char id[20];
    int box; /* 0 disc label / icon, 1 box front, 2 back, 3 spine */
    int entry;
    uint8_t* buf;
    uint32_t size; /* bytes of pixel data after the 0x20 byte header */
    int w, h, fmt;
    uint32_t us; /* time the thread needed */
} art_job;
static art_job art_jobs[ART_JOBS];
static semaphore_t art_sem;
static kthread_t* art_thread;
static int art_started;

static dat_file dat_icon, dat_icon_ex, dat_box, dat_box_ex;
static dat_file dat_back, dat_back_ex, dat_spine, dat_spine_ex; /* BACK.DAT, SPINE.DAT: the case's back and spine pictures (BOX.DAT's format; _EX wins) */
static dat_file dat_disc; /* DISC.DAT: the games' own disc labels (their 0GDTEX.PVR), collected by the card manager */
static volatile int dats_loaded;
static volatile int dats_wanted; /* the loader thread is to load (again) the DATs that are not there */
static int dat_ok[9], dat_tries[9];
static uint32_t dat_retry_at;
static art_entry art_cache[ART_SLOTS];
static char row_product[BLIST_MAX_SLOTS][16];
static int row_pal[BLIST_MAX_SLOTS]; /* the game of the row is a PAL release: the blue BIOS disc, not the red one */
static int row_window[BLIST_MAX_SLOTS]; /* title scroll: visible width in px (0 = no scrolling) */
static int row_offset[BLIST_MAX_SLOTS];


/* The DATs, parsed once; one that could not be opened or parsed is tried again a few times (the disc may not have
 * been ready), but only while the loader thread is not reading from them. */
static void
art_load_dats(void) {
    static dat_file* const dats[9] = {&dat_disc, &dat_icon, &dat_icon_ex, &dat_box, &dat_box_ex, &dat_back, &dat_back_ex, &dat_spine, &dat_spine_ex};
    static const char* const names[9] = {"DISC.DAT", "ICON.DAT", "ICON_EX.DAT", "BOX.DAT", "BOX_EX.DAT", "BACK.DAT", "BACK_EX.DAT", "SPINE.DAT", "SPINE_EX.DAT"};
    if (dats_loaded == 2 || (dats_loaded && frame_no < dat_retry_at)) {
        return;
    }
    int pending = 0;
    for (int i = 0; i < 9; i++) {
        if (dat_ok[i] || dat_tries[i] >= DAT_RETRIES) {
            continue;
        }
        DAT_init(dats[i]);
        dat_ok[i] = DAT_load_parse(dats[i], names[i]) == 0;
        if (!dat_ok[i]) {
            DAT_init(dats[i]); /* nothing half set up: the readers see no table */
            pending |= ++dat_tries[i] < DAT_RETRIES;
        }
    }
    dats_loaded = pending ? 1 : 2;
    dat_retry_at = frame_no + DAT_RETRY_FRAMES;
}

/* The PVR files in the DATs have a fixed 0x20 byte header (GBIX + PVRT): colour format, layout,
 * then width and height. Returns the size of the pixel data, 0 if unusable. */
static uint32_t
art_parse(const uint8_t* b, int* w, int* h, int* fmt) {
    int color = b[0x18], layout = b[0x19];
    *w = b[0x1C] | (b[0x1D] << 8);
    *h = b[0x1E] | (b[0x1F] << 8);
    int f = 0;
    switch (color) {
        case 0: f = PVR_TXRFMT_ARGB1555; break;
        case 2: f = PVR_TXRFMT_ARGB4444; break;
        default: f = PVR_TXRFMT_RGB565; break;
    }
    switch (layout) {
        case 0x01: f |= PVR_TXRFMT_TWIDDLED; break;
        case 0x03: f |= PVR_TXRFMT_VQ_ENABLE; break;
        case 0x09: f |= PVR_TXRFMT_NONTWIDDLED; break;
        case 0x0D: f |= PVR_TXRFMT_TWIDDLED; break;
        case 0x10: f |= PVR_TXRFMT_VQ_ENABLE | PVR_TXRFMT_NONTWIDDLED; break;
        default: return 0;
    }
    if (*w <= 0 || *h <= 0 || *w > 1024 || *h > 1024) {
        return 0;
    }
    *fmt = f;
    return (uint32_t)(*w) * (uint32_t)(*h) * 2u;
}

/* Runs in the loader thread: read the picture `id` of the DAT into the job's buffer. 1 when it is there, 0 when the
 * DAT does not have it (or it is unusable), -1 when the disc could not be read. */
static int
art_read_job(const dat_file* d, art_job* j) {
    if (!d->hash || d->chunk_size > ART_CHUNK_MAX || d->chunk_size <= 0x20 || !DAT_get_offset_by_ID(d, j->id)) {
        return 0;
    }
    if (!DAT_read_file_by_ID(d, j->id, j->buf)) {
        return -1;
    }
    int w, h, fmt;
    uint32_t size = art_parse(j->buf, &w, &h, &fmt);
    if (!size) {
        return 0; /* not a picture the PVR can take: no art rather than garbage */
    }
    if (fmt & PVR_TXRFMT_VQ_ENABLE) {
        size = 2048u + (uint32_t)w * (uint32_t)h / 4u; /* the code book, then one byte per 2x2 texels */
    }
    if (size + 0x20 > d->chunk_size) {
        return 0; /* the chunk cannot hold that much */
    }
    j->w = w;
    j->h = h;
    j->fmt = fmt;
    j->size = size;
    return 1;
}

static void*
art_worker(void* param) {
    (void)param;
    for (;;) {
        sem_wait(&art_sem);
        if (dats_wanted) { /* the retry of a DAT that could not be opened: here, never on the menu's thread (opening the disc's files
                            * took the frame it happened in, 200 ms and more, as the list opened or scrolled) */
            dats_wanted = 0;
            art_load_dats();
        }
        for (int i = 0; i < ART_JOBS; i++) {
            art_job* j = &art_jobs[i];
            if (j->state != JOB_QUEUED) {
                continue;
            }
            j->state = JOB_READING;
            uint64_t t0 = timer_us_gettime64();
            /* disc labels: the game's own label (DISC.DAT) first; then the add-on file wins over the main one */
            const dat_file* order[3] = {j->box ? NULL : &dat_disc,
                                        j->box == 1 ? &dat_box_ex : j->box == 2 ? &dat_back_ex : j->box == 3 ? &dat_spine_ex : &dat_icon_ex,
                                        j->box == 1 ? &dat_box : j->box == 2 ? &dat_back : j->box == 3 ? &dat_spine : &dat_icon};
            int got = 0, error = 0;
            for (int k = 0; k < 3 && got != 1; k++) {
                if (order[k]) {
                    got = art_read_job(order[k], j);
                    error |= got < 0;
                }
            }
            j->us = (uint32_t)(timer_us_gettime64() - t0);
            j->state = got == 1 ? JOB_DONE : (error ? JOB_ERROR : JOB_FAILED);
        }
    }
    return NULL;
}

/* The loader thread and its buffers; if they cannot be had now, it is tried again on a later request. */
static int
art_start(void) {
    if (art_started) {
        return 1;
    }
    for (int i = 0; i < ART_JOBS; i++) {
        if (!art_jobs[i].buf) {
            art_jobs[i].buf = (uint8_t*)memalign(32, ART_CHUNK_MAX);
        }
        if (!art_jobs[i].buf) {
            return 0; /* the buffers already had are kept for the next try */
        }
    }
    static int sem_ready;
    if (!sem_ready) {
        sem_init(&art_sem, 0);
        sem_ready = 1;
    }
    art_thread = thd_create(1, art_worker, NULL);
    if (!art_thread) {
        return 0;
    }
    thd_set_prio(art_thread, PRIO_DEFAULT + 4); /* lower than the menu */
    art_started = 1;
    return 1;
}

/* A DAT that could not be opened at the start is tried again a few times, 5 s apart, by the loader thread */
static void
art_want_dats(void) {
    if (dats_loaded == 2 || (dats_loaded && frame_no < dat_retry_at) || dats_wanted || !art_start()) {
        return;
    }
    dats_wanted = 1;
    sem_signal(&art_sem);
}

/* Video memory is full: give back the picture used longest ago (not `keep`, not one being loaded). 1 if one went. */
static int
art_evict_one(const art_entry* keep) {
    art_entry* oldest = NULL;
    for (int i = 0; i < ART_SLOTS; i++) {
        art_entry* e = &art_cache[i];
        if (e != keep && e->present && !e->loading && e->tex.valid && (!oldest || e->last_use < oldest->last_use)) {
            oldest = e;
        }
    }
    if (!oldest) {
        return 0;
    }
    pvr_mem_free(oldest->tex.ptr);
    memset(oldest, 0, sizeof(*oldest)); /* asked for again, it is read again */
    return 1;
}

static art_entry* art_get(const char* product, int box);

/* One texel of a picture in the loader's buffer (after the 0x20 byte header) as luminance 0..255, -1 if the format is not known */
static int
art_texel_luma(const art_job* j, int x, int y) {
    const uint8_t* d = j->buf + 0x20;
    const int col = j->fmt & (7 << 27); /* PVR_TXRFMT_ARGB1555 / RGB565 / ARGB4444 */
    uint32_t idx;
    uint16_t c;
    /* twiddled: the bits of y and x interleaved, y's in the even places (the smaller side's bits only) */
    const int tw = !(j->fmt & PVR_TXRFMT_NONTWIDDLED);
    if (j->fmt & PVR_TXRFMT_VQ_ENABLE) {
        if (!tw) {
            return -1;
        }
        const int bx = x / 2, by = y / 2, bw = j->w / 2, bh = j->h / 2, m = bw < bh ? bw : bh;
        idx = 0;
        int bit = 0;
        for (int i = 1; i < m; i <<= 1) {
            idx |= ((by & i) ? 1u : 0u) << bit++;
            idx |= ((bx & i) ? 1u : 0u) << bit++;
        }
        idx |= (uint32_t)((bw < bh ? by : bx) / m) * (uint32_t)(m * m);
        const uint8_t code = d[2048 + idx];
        const uint8_t* e = d + code * 8 + (((x & 1) * 2 + (y & 1)) * 2);
        c = (uint16_t)(e[0] | (e[1] << 8));
    } else {
        if (tw) {
            const int m = j->w < j->h ? j->w : j->h;
            idx = 0;
            int bit = 0;
            for (int i = 1; i < m; i <<= 1) {
                idx |= ((y & i) ? 1u : 0u) << bit++;
                idx |= ((x & i) ? 1u : 0u) << bit++;
            }
            idx |= (uint32_t)((j->w < j->h ? y : x) / m) * (uint32_t)(m * m);
        } else {
            idx = (uint32_t)y * (uint32_t)j->w + (uint32_t)x;
        }
        c = (uint16_t)(d[idx * 2] | (d[idx * 2 + 1] << 8));
    }
    int r, g, b;
    if (col == PVR_TXRFMT_RGB565) {
        r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
    } else if (col == PVR_TXRFMT_ARGB4444) {
        r = ((c >> 8) & 15) * 17, g = ((c >> 4) & 15) * 17, b = (c & 15) * 17;
    } else {
        r = ((c >> 10) & 31) << 3, g = ((c >> 5) & 31) << 3, b = (c & 31) << 3;
    }
    return (r * 77 + g * 151 + b * 28) >> 8;
}

/* The US covers have a curved band down their left edge with the Dreamcast logo: black on most (Sonic Adventure 2), white on others.
 * Samples down the left 2..8 % of the cover (the band, above and below its logo); the median says which: 1 white, 2 black, 0 neither
 * clearly (another cover). */
static int
art_band_of(const art_job* j) {
    enum { N = 7 * 9 };
    int v[N], n = 0;
    for (int a = 0; a < 7; a++) {
        for (int k = 0; k < 9; k++) {
            const int x = j->w * (2 + a) / 100, y = j->h * (12 + k * 9) / 100;
            const int l = art_texel_luma(j, x, y);
            if (l < 0) {
                return 0;
            }
            v[n++] = l;
        }
    }
    for (int i = 1; i < n; i++) { /* insertion sort: 63 values */
        for (int k = i; k > 0 && v[k - 1] > v[k]; k--) {
            const int t = v[k];
            v[k] = v[k - 1], v[k - 1] = t;
        }
    }
    const int lo = v[n / 4], mid = v[n / 2], hi = v[n * 3 / 4];
    if (hi < 70) {
        return 2;
    }
    if (lo > 190) {
        return 1;
    }
    return mid < 50 ? 2 : mid > 215 ? 1 : 0;
}

int
gfx_art_front_band(const char* product) {
    if (!product || !product[0]) {
        return -1;
    }
    art_entry* e = art_get(product, 1);
    if (!e || e->loading) {
        return -1; /* not known yet */
    }
    return e->band;
}

/* Main thread, once per frame: upload the pictures the loader has finished. */
static void
art_poll_impl(void) {
    int uploads = 0;
    for (int i = 0; i < ART_JOBS; i++) {
        art_job* j = &art_jobs[i];
        if (j->state == JOB_FAILED || j->state == JOB_ERROR) {
            art_entry* e = &art_cache[j->entry];
            e->loading = 0;
            /* not in any DAT: no art, asked no more. The disc could not be read: ask again a second later */
            e->retry_at = j->state == JOB_ERROR ? frame_no + ART_RETRY_FRAMES : 0;
            j->state = JOB_FREE;
        } else if (j->state == JOB_DONE && uploads < ART_UPLOADS_PER_FRAME) {
            art_entry* e = &art_cache[j->entry];
            uint32_t padded = (j->size + 31u) & ~31u;
            e->tex.ptr = pvr_mem_malloc(padded);
            if (!e->tex.ptr) {
                /* video memory is full: make room and upload it on the next frame; with nothing to give back, it
                 * stays without art (and is read again when asked for later) */
                if (!art_evict_one(e)) {
                    memset(e, 0, sizeof(*e));
                    j->state = JOB_FREE;
                }
                continue;
            }
            pvr_txr_load(j->buf + 0x20, e->tex.ptr, padded);
            e->band = j->box == 1 ? art_band_of(j) : 0;
            e->tex.w = j->w;
            e->tex.h = j->h;
            e->tex.fmt = j->fmt;
            e->tex.valid = 1;
            uploads++;
            stats.art_loads++;
            stats.art_us += j->us;
            e->loading = 0;
            j->state = JOB_FREE;
        }
    }
}

static void
art_poll(void) {
    (void)0;
    uint64_t _p0 = timer_us_gettime64();
    art_poll_impl();
    gfx_prof_us[6] += (unsigned)(timer_us_gettime64() - _p0);
    
}

static art_entry*
art_get(const char* product, int box) {
    char key[20];
    if (!product || !product[0]) {
        return NULL;
    }
    art_want_dats();
    const char* id = serial_santize_art(product);
    snprintf(key, sizeof(key), "%s%.17s", box == 1 ? "#" : box == 2 ? "%" : box == 3 ? "^" : "", id);
    art_entry* free_slot = NULL;
    art_entry* oldest = NULL;
    for (int i = 0; i < ART_SLOTS; i++) {
        art_entry* e = &art_cache[i];
        if (e->present && !strcmp(e->key, key)) {
            e->last_use = frame_no;
            if (!e->retry_at || e->loading || frame_no < e->retry_at) {
                return e;
            }
            memset(e, 0, sizeof(*e)); /* a read error earlier: read it again */
            free_slot = e;
            break;
        }
        if (!e->present) {
            free_slot = free_slot ? free_slot : e;
        } else if (!e->loading && (!oldest || e->last_use < oldest->last_use)) {
            oldest = e;
        }
    }
    if (!art_start()) {
        return NULL;
    }
    art_job* job = NULL;
    for (int i = 0; i < ART_JOBS && !job; i++) {
        job = art_jobs[i].state == JOB_FREE ? &art_jobs[i] : NULL;
    }
    if (!job) {
        return NULL; /* the loader is busy: ask again on a later frame */
    }
    art_entry* e = free_slot;
    if (!e) {
        e = oldest;
        if (!e || e->last_use + 2 > frame_no) {
            return NULL; /* every slot is in use: wait */
        }
        if (e->tex.valid) {
            pvr_mem_free(e->tex.ptr);
        }
    }
    memset(e, 0, sizeof(*e));
    strncpy(e->key, key, sizeof(e->key) - 1);
    e->present = 1;
    e->loading = 1;
    e->last_use = frame_no;
    strncpy(job->id, id, sizeof(job->id) - 1);
    job->id[sizeof(job->id) - 1] = '\0';
    job->box = box;
    job->entry = (int)(e - art_cache);
    job->state = JOB_QUEUED;
    sem_signal(&art_sem);
    return e;
}

int
gfx_art_prefetch(const char* product, int box) {
    if (!product || !product[0]) {
        return 1;
    }
    art_entry* e = art_get(product, box);
    if (!e) {
        return 0; /* loader busy or cache full this frame: not there yet */
    }
    return !e->loading;
}

void
gfx_art_bind_row(int slot, const char* product, int pal) {
    if (slot >= 0 && slot < BLIST_MAX_SLOTS) {
        row_pal[slot] = pal != 0;
        strncpy(row_product[slot], product ? product : "", sizeof(row_product[slot]) - 1);
        row_product[slot][sizeof(row_product[slot]) - 1] = '\0';
    }
}

/* The BIOS has two pictures for the face of a disc, both 256x256 with the GBIX 0: the first in the
 * ROM is blue (what the BIOS shows on European consoles), the second red (Japan and America). */
static rom_tex*
disc_face(int pal) {
    static rom_tex face[2];
    static int tried[2];
    int i = pal ? 1 : 0;
    if (!tried[i] && g_rom) {
        tried[i] = 1;
        int found = 0;
        for (int n = 0; n < bios_texture_count(g_rom); n++) {
            bios_texture t;
            if (bios_texture_get(g_rom, n, &t) == 0 && t.gbix == 0 && t.width == 256 && t.height == 256) {
                if (found == (pal ? 0 : 1)) {
                    upload_texture(&t, &face[i]);
                    break;
                }
                found++;
            }
        }
    }
    return face[i].valid ? &face[i] : NULL;
}

/* Plain white picture for the texture slots of the built-in models (bios_models.h) that have no art. */
static rom_tex*
white_tex(void) {
    static rom_tex white;
    static int tried;
    if (!tried) {
        tried = 1;
        unsigned short px[8 * 8];
        for (int i = 0; i < 64; i++) {
            px[i] = 0xFFFF;
        }
        white.ptr = pvr_mem_malloc(sizeof(px));
        if (white.ptr) {
            pvr_txr_load(px, white.ptr, sizeof(px));
            white.w = white.h = 8;
            white.fmt = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED;
            white.valid = 1;
        }
    }
    return white.valid ? &white : NULL;
}

static char model_front_product[16];
static int memo_ok;

void
gfx_model_bind_front(const char* product) {
    strncpy(model_front_product, product ? product : "", sizeof(model_front_product) - 1);
    model_front_product[sizeof(model_front_product) - 1] = '\0';
    memo_ok = 0; /* the same texture reference now means another picture */
}

/* The last texture looked up: a model draws many triangles with the same one, and finding a game picture
 * costs a string search. Forgotten whenever pictures come or go. */
static bscene_texref memo_ref;
static rom_tex* memo_tex;

static rom_tex* get_rom_texture_uncached(bscene_texref ref);

/* ---- The header logo and the online mode ---- */

#define LOGO_W 128
#define LOGO_H 32
#define LOGO_SWIRL_W 29 /* columns of the logo texture that are the swirl; the wordmark starts at 30 */
#define LOGO_D_END 46 /* the wordmark's D ends here; "CNET" starts (the D is kept from the BIOS logo) */

static void
upload_argb4444_impl(const unsigned short* px, int w, int h, rom_tex* g) {
    size_t size = ((size_t)w * (size_t)h * 2u + 31u) & ~(size_t)31u;
    void* staging = memalign(32, size);
    pvr_ptr_t ptr = staging ? pvr_mem_malloc(size) : NULL;
    if (!ptr) {
        free(staging);
        return;
    }
    memset(staging, 0, size);
    memcpy(staging, px, (size_t)w * (size_t)h * 2u);
    pvr_txr_load(staging, ptr, size);
    free(staging);
    g->ptr = ptr;
    g->w = w;
    g->h = h;
    g->fmt = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED;
    g->valid = 1;
}

/* The same for a picture already in a buffer from memalign(32, ...) whose size (w * h * 2) is a multiple of 32: no copy. Frees it. */
static void
upload_argb4444_staged(unsigned short* staging, int w, int h, rom_tex* g) {
    const size_t size = (size_t)w * (size_t)h * 2u;
    pvr_ptr_t ptr = (size & 31u) == 0 ? pvr_mem_malloc(size) : NULL;
    if (ptr) {
        pvr_txr_load(staging, ptr, size);
        g->ptr = ptr;
        g->w = w;
        g->h = h;
        g->fmt = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED;
        g->valid = 1;
    }
    free(staging);
}

static void
upload_argb4444(const unsigned short* px, int w, int h, rom_tex* g) {
    (void)0;
    uint64_t _p0 = timer_us_gettime64();
    upload_argb4444_impl(px, w, h, g);
    gfx_prof_us[5] += (unsigned)(timer_us_gettime64() - _p0);
    
}

/* The online logos' pictures: "Now!" orange / blue and "CNET" for the light bar, and flat copies for the dark bar (dark mode), made by
 * gfx_load_logo_texts from the masks of logo_letters.inc; and the ROM logo with a light wordmark. */
static rom_tex logo_now_tex[2]; /* [0] orange, [1] blue */
static rom_tex logo_dcnet_text_tex;
static rom_tex logo_now_dark_tex[2], logo_cnet_dark_tex, logo_dark_tex, logo_dcnet_dark_tex;
static int logo_dark;
static unsigned logo_swirl_rgb = 0xFFFFFF; /* the swirl's colour, 0xRRGGBB: picks the orange or the blue "Now!" */

#include "logo_letters.inc"

static void build_logo_variants(void);

/* One 64x32 ARGB4444 picture from a coverage mask. Light bar (`dark` 0): the colour mixed with white by (1 - coverage), as the BIOS
 * logo's own edge pixels are (c*colour + (1-c)*255). Dark bar: the colour as it is, the coverage as the alpha. */
static void
make_letters(const uint8_t* mask, unsigned rgb, int dark, rom_tex* out) {
    unsigned short px[64 * 32];
    for (int i = 0; i < 64 * 32; i++) {
        unsigned a = mask[i], v = 0;
        if (a) {
            v = a << 12;
            for (int ch = 0; ch < 3; ch++) {
                unsigned t = (rgb >> (16 - 8 * ch)) & 0xFF;
                unsigned n = dark ? (t + 8) / 17 : (a * t + (15 - a) * 255 + 127) / 255; /* the nearest of the 16 levels */
                v |= n << (8 - 4 * ch);
            }
        }
        px[i] = (unsigned short)v;
    }
    upload_argb4444(px, 64, 32, out);
}

/* The letters of the online logos are in the program (logo_letters.inc, hand-drawn by tools/gen_logo_text.py): "CNET" after the
 * BIOS logo's own D, and "Now!" behind it, orange or blue as the swirl, each for the light and the dark header bar. They hold nothing
 * of the BIOS logo. */
void
gfx_load_logo_texts(void) {
    if (logo_dcnet_text_tex.valid) {
        return;
    }
    make_letters(logo_mask_cnet, 0x444444u, 0, &logo_dcnet_text_tex);
    make_letters(logo_mask_cnet, 0xCDCDD0u, 1, &logo_cnet_dark_tex);
    make_letters(logo_mask_now, 0xEE4400u, 0, &logo_now_tex[0]);
    make_letters(logo_mask_now, 0x4477BBu, 0, &logo_now_tex[1]);
    make_letters(logo_mask_now, 0xEE4400u, 1, &logo_now_dark_tex[0]);
    make_letters(logo_mask_now, 0x6096E1u, 1, &logo_now_dark_tex[1]);
    build_logo_variants(); /* the cropped and dark logos now, not in the frame that first needs them */
}

int
gfx_logo_texts_loaded(void) {
    return (logo_now_tex[0].valid ? 1 : 0) | (logo_now_tex[1].valid ? 2 : 0) | (logo_dcnet_text_tex.valid ? 4 : 0);
}

/* Pictures made from the ROM's logo while the menu runs (nothing of it is kept anywhere): the swirl with the wordmark's D, the
 * wordmark's D (and the colour of the swirl). Nothing is made when a LOGO.PVR of the user's own is in use. */
static void
build_logo_variants(void) {
    if (logo_variants_tried || logo_tex.valid || !g_rom) {
        return;
    }
    logo_variants_tried = 1;
    bios_texture t;
    uint32_t* argb = malloc(sizeof(uint32_t) * LOGO_W * LOGO_H);
    unsigned short* swirl_d = calloc((size_t)LOGO_W * LOGO_H, sizeof(unsigned short));
    unsigned short* full_dark = calloc((size_t)LOGO_W * LOGO_H, sizeof(unsigned short));
    unsigned short* swirl_d_dark = calloc((size_t)LOGO_W * LOGO_H, sizeof(unsigned short));
    if (argb && swirl_d && full_dark && swirl_d_dark && bios_header_logo(g_rom, GFX_REGION_BYTE & 0xF, &t) == 0 && t.width == LOGO_W
        && t.height == LOGO_H && bios_texture_decode(&t, argb) == 0) {
        int best = -1;
        for (int y = 0; y < LOGO_H; y++) {
            for (int x = 0; x < LOGO_W; x++) { /* the whole logo, light-wordmark copy for dark mode: the wordmark's pixels become light grey, same alpha */
                uint32_t c = argb[y * LOGO_W + x];
                uint32_t dc = x < LOGO_SWIRL_W ? c : (c & 0xFF000000u) | 0xCDCDD0u;
                full_dark[y * LOGO_W + x] = (unsigned short)((((dc >> 24) & 0xFF) >> 4) << 12 | (((dc >> 16) & 0xFF) >> 4) << 8
                                                             | (((dc >> 8) & 0xFF) >> 4) << 4 | ((dc & 0xFF) >> 4));
                if (x < LOGO_D_END) {
                    swirl_d_dark[y * LOGO_W + x] = full_dark[y * LOGO_W + x];
                }
            }
            for (int x = 0; x < LOGO_D_END; x++) { /* the swirl and the D */
                uint32_t c = argb[y * LOGO_W + x];
                swirl_d[y * LOGO_W + x] = (unsigned short)((((c >> 24) & 0xFF) >> 4) << 12 | (((c >> 16) & 0xFF) >> 4) << 8
                                                          | (((c >> 8) & 0xFF) >> 4) << 4 | ((c & 0xFF) >> 4));
                if (x < LOGO_SWIRL_W) {
                    /* the swirl's colour: the most saturated opaque pixel (orange on NTSC and PAL-J, blue on PAL) */
                    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                    int hi = r > g ? (r > b ? r : b) : (g > b ? g : b), lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
                    if ((c >> 24) >= 0xF0 && hi - lo > best) {
                        best = hi - lo;
                        logo_swirl_rgb = (unsigned)(((r + 8) / 17) * 17) << 16 | (unsigned)(((g + 8) / 17) * 17) << 8 | (unsigned)(((b + 8) / 17) * 17);
                    }
                }
            }
        }
        upload_argb4444(swirl_d, LOGO_W, LOGO_H, &logo_dcnet_tex);
        upload_argb4444(full_dark, LOGO_W, LOGO_H, &logo_dark_tex);
        upload_argb4444(swirl_d_dark, LOGO_W, LOGO_H, &logo_dcnet_dark_tex);
    }
    free(argb);
    free(swirl_d);
    free(full_dark);
    free(swirl_d_dark);
}

void
gfx_set_dark(int dark) {
    if (dark != logo_dark) {
        logo_dark = dark;
        memo_ok = 0; /* the header's texture may have been remembered */
    }
}

void
gfx_set_logo_mode(int mode) {
    if (mode != logo_mode) {
        logo_mode = mode;
        memo_ok = 0; /* the header's texture may have been remembered */
    }
}


#define DYN_MAX 64
static rom_tex dyn_tex[DYN_MAX];
static struct {
    int texlist, dyn;
} decal_ovr[16];
static int decal_n;

static rom_tex*
get_rom_texture(bscene_texref ref) {
    if (memo_ok && memo_ref.kind == ref.kind && memo_ref.a == ref.a && memo_ref.b == ref.b) {
        return memo_tex;
    }
    rom_tex* t = get_rom_texture_uncached(ref);
    memo_ok = 1;
    memo_ref = ref;
    memo_tex = t;
    return t;
}

/* The ribs of the NTSC cases' edges (BMODEL_TEX_RIBS): grey stripes (groove, ridge, flat) that modulate the face's colour, 64x8,
 * the pattern of the ribs picture of the case models. */
static rom_tex ribs_tex;
static rom_tex*
ribs_texture(void) {
    /* 8 even ribs across 64 columns (a repeat is 9.1 mm: 1.14 mm per rib); rows 0..3 and 12..15 are plain white (the 0.8 mm at the
     * sides of the band stay untextured), the ribs are in rows 4..11 */
    static const unsigned char rib[8] = {0xF, 0xF, 0xE, 0xC, 0xB, 0xB, 0xC, 0xE};
    if (!ribs_tex.valid) {
        unsigned short px[64 * 16];
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 64; x++) {
                unsigned v = (y < 4 || y >= 12) ? 0xFu : rib[x & 7];
                px[y * 64 + x] = (unsigned short)(0xF000u | (v << 8) | (v << 4) | v);
            }
        }
        upload_argb4444(px, 64, 16, &ribs_tex);
    }
    return ribs_tex.valid ? &ribs_tex : NULL;
}

/* An opaque ARGB4444 pixel from 0..255 colour values. */
static unsigned short
pack4444(float r, float g, float b) {
    unsigned c[3] = {(unsigned)(r + 8.0f) >> 4, (unsigned)(g + 8.0f) >> 4, (unsigned)(b + 8.0f) >> 4};
    for (int i = 0; i < 3; i++) {
        c[i] = c[i] > 15u ? 15u : c[i];
    }
    return (unsigned short)(0xF000u | c[0] << 8 | c[1] << 4 | c[2]);
}

/* The 4 x 4 ordered dither of the pictures made here (pack4444_dither) */
static const unsigned char bayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static const float bayer4_t[16] = {0.5f / 16, 1.5f / 16, 2.5f / 16, 3.5f / 16, 4.5f / 16, 5.5f / 16, 6.5f / 16, 7.5f / 16,
                                   8.5f / 16, 9.5f / 16, 10.5f / 16, 11.5f / 16, 12.5f / 16, 13.5f / 16, 14.5f / 16, 15.5f / 16};

/* The same for a colour given as it should look on screen (0..255 per channel; a 4-bit channel shows as level * 17): the levels are ordered-dithered
 * (4 x 4) over the picture, so a flat area shows its colour on average instead of the nearest of 16 levels. */
static unsigned short
pack4444_dither(float r, float g, float b, int x, int y) {
    const float t = bayer4_t[bayer4[y & 3][x & 3]];
    const float v[3] = {r, g, b};
    unsigned c[3];
    for (int i = 0; i < 3; i++) {
        float l = v[i] * (1.0f / 17.0f);
        unsigned lo = (unsigned)l;
        c[i] = lo + ((l - (float)lo) > t ? 1u : 0u);
        c[i] = c[i] > 15u ? 15u : c[i];
    }
    return (unsigned short)(0xF000u | c[0] << 8 | c[1] << 4 | c[2]);
}

/* pack4444_dither with an alpha (0..1). */
static unsigned short
pack4444_dither_a(float r, float g, float b, float a, int x, int y) {
    const unsigned short c = pack4444_dither(r, g, b, x, y);
    unsigned al = (unsigned)(a * 15.0f + 0.5f);
    al = al > 15u ? 15u : al;
    return (unsigned short)((c & 0x0FFFu) | al << 12);
}

/* The 4-bit level of a 0..255 value, for whole-number colours: lvl_round as pack4444, lvl_dither[cell] as pack4444_dither in the cell
 * of the 4 x 4 pattern (the same results, without the floats: level lo = v / 17 rounds up when the rest / 17 > (cell + 0.5) / 16). */
static uint8_t lvl_round[256], lvl_dither[16][256];
static int lvl_ready;
static void
lvl_init(void) {
    if (lvl_ready) {
        return;
    }
    for (int v = 0; v < 256; v++) {
        lvl_round[v] = (uint8_t)((v + 8) >> 4 > 15 ? 15 : (v + 8) >> 4);
        for (int c = 0; c < 16; c++) {
            const int l = v / 17 + (32 * (v % 17) > 34 * c + 17 ? 1 : 0);
            lvl_dither[c][v] = (uint8_t)(l > 15 ? 15 : l);
        }
    }
    lvl_ready = 1;
}

/* x / 255 rounded toward zero, as C's division, for |x| <= 255 * 255 (the SH4 has no divide instruction) */
static inline int
div255(int x) {
    return x >= 0 ? (x + 1 + (x >> 8)) >> 8 : -((-x + 1 + (-x >> 8)) >> 8);
}

/* The cases' fronts and backs for a game without box art, from the BIOS's own disc picture (it has the stacked Dreamcast logo with the
 * word under the swirl): the front is the disc's blue (PAL) or red (NTSC) with the logo in white, the back is white with the logo in its
 * own colours. 128 x 128, the logo's width is `frac` of the picture. */
static rom_tex generic_tex[2][2]; /* [pal][back] */
static int generic_tried[2][2];
static rom_tex*
generic_cover_impl(int pal, int back) {
    enum { N = 128 };
    rom_tex* out = &generic_tex[pal ? 1 : 0][back ? 1 : 0];
    if (out->valid || generic_tried[pal ? 1 : 0][back ? 1 : 0] || !g_rom) {
        return out->valid ? out : NULL;
    }
    generic_tried[pal ? 1 : 0][back ? 1 : 0] = 1;
    uint32_t* d = malloc(sizeof(uint32_t) * 256 * 256);
    unsigned short* px = calloc((size_t)N * N, sizeof(unsigned short));
    if (!d || !px) {
        free(d);
        free(px);
        return NULL;
    }
    int found = 0, ok = 0;
    for (int n = 0; n < bios_texture_count(g_rom) && !ok; n++) {
        bios_texture t;
        if (bios_texture_get(g_rom, n, &t) == 0 && t.gbix == 0 && t.width == 256 && t.height == 256) {
            if (found == (pal ? 0 : 1)) {
                ok = bios_texture_decode(&t, d) == 0;
            }
            found++;
        }
    }
    if (!ok) {
        free(d);
        free(px);
        return NULL;
    }
    const uint32_t bgc = d[128 * 256 + 40]; /* the disc's coloured half */
    /* where the logo is (on the disc's light half, right of the hole): the pixels that differ from the light grey */
    #define DISC_GREY 204.0f
    int x0 = 256, x1 = 0, y0 = 256, y1 = 0;
    for (int y = 90; y < 175; y++) {
        for (int x = 165; x < 250; x++) {
            const float rr0 = (float)(x - 128), rr1 = (float)(y - 128);
            if (rr0 * rr0 + rr1 * rr1 > 112.0f * 112.0f) {
                continue; /* the disc's rim is not part of the logo */
            }
            uint32_t c = d[y * 256 + x];
            float df = fabsf((float)((c >> 16) & 255) - DISC_GREY);
            float dg = fabsf((float)((c >> 8) & 255) - DISC_GREY);
            float db = fabsf((float)(c & 255) - DISC_GREY);
            if ((df > dg ? (df > db ? df : db) : (dg > db ? dg : db)) > 60.0f) {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    if (x1 <= x0 || y1 <= y0) {
        free(d);
        free(px);
        return NULL;
    }
    const float lw = (float)(x1 - x0 + 1), lh = (float)(y1 - y0 + 1);
    const float want = back ? 66.0f : 88.0f; /* the logo's width on the picture */
    const float sc = want / lw;              /* picture pixels per disc pixel, vertically */
    /* the label is lw_mm x 120 mm: the picture is square, so the logo is made narrower to look right once stretched over it */
    const float lw_mm = pal ? (back ? 138.3f : 120.0f) : (back ? 139.6f : 120.0f);
    const float scx = sc * (back ? 119.0f : 120.0f) / lw_mm;
    const float cx = (float)N / 2.0f, cy = (float)N / 2.0f;
    /* The logo's pixels around its box, once, in place: the top byte becomes the pixel's distance from the light grey (its coverage
     * is that / 150), or 255 outside the disc's rim (paper). The picture's pixels then only look up the four around them; where each
     * falls on the disc depends on its column or on its row alone, so those are worked out once per column and once per row. */
    const int bx0 = x0 - 1 < 0 ? 0 : x0 - 1, bx1 = x1 + 2 > 255 ? 255 : x1 + 2;
    const int by0 = y0 - 1 < 0 ? 0 : y0 - 1, by1 = y1 + 2 > 255 ? 255 : y1 + 2;
    for (int yy = by0; yy <= by1; yy++) {
        for (int xx = bx0; xx <= bx1; xx++) {
            uint32_t* c = &d[yy * 256 + xx];
            unsigned m = 255;
            if ((xx - 128) * (xx - 128) + (yy - 128) * (yy - 128) <= 112 * 112) {
                const int dr = abs((int)((*c >> 16) & 255) - 204), dg = abs((int)((*c >> 8) & 255) - 204), db = abs((int)(*c & 255) - 204);
                m = (unsigned)(dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db)); /* at most 204 */
            }
            *c = (*c & 0xFFFFFFu) | m << 24;
        }
    }
    float cov_of[256];
    for (int m = 0; m < 256; m++) {
        cov_of[m] = (float)m / 150.0f > 1.0f ? 1.0f : (float)m / 150.0f;
    }
    int cix[N], ciy[N];     /* the logo pixel left of / above the picture pixel; INT_MIN: off the logo */
    float cax[N], cay[N];   /* the weight of the one right of / below it */
    for (int i = 0; i < N; i++) {
        const float fx = (float)x0 + (i + 0.5f - cx) / scx + lw / 2.0f - 0.5f, fy = (float)y0 + (i + 0.5f - cy) / sc + lh / 2.0f - 0.5f;
        cix[i] = fx >= (float)x0 - 1 && fx <= (float)x1 + 1 ? (int)floorf(fx) : INT_MIN;
        ciy[i] = fy >= (float)y0 - 1 && fy <= (float)y1 + 1 ? (int)floorf(fy) : INT_MIN;
        cax[i] = cix[i] != INT_MIN ? fx - (float)cix[i] : 0.0f;
        cay[i] = ciy[i] != INT_MIN ? fy - (float)ciy[i] : 0.0f;
    }
    const float r0 = back ? 255.0f : (float)((bgc >> 16) & 255), g0 = back ? 255.0f : (float)((bgc >> 8) & 255), b0 = back ? 255.0f : (float)(bgc & 255);
    const unsigned short plain = pack4444(r0, g0, b0);
    for (int y = 0; y < N; y++) {
        const int iy = ciy[y];
        for (int x = 0; x < N; x++) {
            const int ix = cix[x];
            if (iy == INT_MIN || ix == INT_MIN) {
                px[y * N + x] = plain;
                continue;
            }
            const float ax = cax[x], ay = cay[y];
            float cr = 0, cg = 0, cb = 0, ca = 0;
            for (int k = 0; k < 4; k++) {
                const int xx = ix + (k & 1), yy = iy + (k >> 1);
                if (xx < 0 || xx > 255 || yy < 0 || yy > 255) {
                    continue;
                }
                const float w = ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay);
                const uint32_t c = d[yy * 256 + xx];
                if ((c >> 24) == 255) {
                    cr += w * 255.0f * 1.25f, cg += w * 255.0f * 1.25f, cb += w * 255.0f * 1.25f; /* outside the logo: paper */
                    continue;
                }
                cr += w * (float)((c >> 16) & 255) * 1.25f, cg += w * (float)((c >> 8) & 255) * 1.25f, cb += w * (float)(c & 255) * 1.25f;
                ca += w * cov_of[c >> 24];
            }
            float r = r0, g = g0, b = b0;
            if (back) { /* its own colours, the light grey of the disc becoming the white of the paper */
                r = cr > 255 ? 255 : cr, g = cg > 255 ? 255 : cg, b = cb > 255 ? 255 : cb;
            } else { /* white ink */
                r += (255.0f - r) * ca, g += (255.0f - g) * ca, b += (255.0f - b) * ca;
            }
            px[y * N + x] = pack4444(r, g, b);
        }
    }
    upload_argb4444(px, N, N, out);
    free(px);
    free(d);
    return out->valid ? out : NULL;
}

static rom_tex*
generic_cover(int pal, int back) {
    rom_tex* _r;
    uint64_t _p0 = timer_us_gettime64();
    _r = generic_cover_impl(pal, back);
    gfx_prof_us[1] += (unsigned)(timer_us_gettime64() - _p0);
    return _r;
}

/* The PAL case's hinge bar (BMODEL_TEX_PILL): 8 x 123 mm, muted blue; the lighter rounded pill (full width, 51 mm tall, corners of 3.2 mm) in
 * the middle with the BIOS logo's "Dreamcast" wordmark in dark lettering on it (the PAL cases have the text, not the swirl). 32 x 512. */
static rom_tex pill_tex;
static rom_tex*
pill_texture(void) {
    enum { W = 32, H = 512 };
    static int tried;
    if (pill_tex.valid || tried || !g_rom) {
        return pill_tex.valid ? &pill_tex : NULL;
    }
    tried = 1;
    bios_texture t;
    uint32_t* logo = malloc(sizeof(uint32_t) * LOGO_W * LOGO_H);
    unsigned short* px = calloc((size_t)W * H, sizeof(unsigned short));
    if (!px || !logo || bios_header_logo(g_rom, 0, &t) != 0 || t.width != LOGO_W || t.height != LOGO_H || bios_texture_decode(&t, logo) != 0) {
        free(logo);
        free(px);
        return NULL;
    }
    /* the wordmark "Dreamcast" of the BIOS's logo (right of the swirl), found by its opaque pixels */
    int wx0 = LOGO_W, wx1 = 0, wy0 = LOGO_H, wy1 = 0;
    for (int y = 0; y < LOGO_H; y++) {
        for (int x = LOGO_SWIRL_W + 1; x < LOGO_W; x++) {
            if ((logo[y * LOGO_W + x] >> 24) > 0x60) {
                wx0 = x < wx0 ? x : wx0, wx1 = x > wx1 ? x : wx1;
                wy0 = y < wy0 ? y : wy0, wy1 = y > wy1 ? y : wy1;
            }
        }
    }
    if (wx1 <= wx0) {
        free(logo);
        free(px);
        return NULL;
    }
    const float ww = (float)(wx1 - wx0 + 1), wh = (float)(wy1 - wy0 + 1);
    const float ppm_x = (float)W / 8.0f, ppm_y = (float)H / 123.0f; /* picture pixels per mm */
    const float text_mm = 36.0f;            /* the text's length along the pill: it reads from the bottom up, like the real case's spine */
    const float s = text_mm / ww;           /* mm per logo pixel */
    const float cx = (float)W / 2.0f, cy = 61.5f * ppm_y; /* the middle of the bar */
    const float pt = (61.5f - 25.5f) * ppm_y;
    /* 3 x 3 samples per pixel. A sample's place along the pill (and on the wordmark's columns) depends on its row only, across on
     * its column only: worked out once per sample row and column. */
    const float PW = 7.25f, PH = 51.0f, R = 3.0f; /* the pill: the menu's "1-4 Players" banner turned to read upwards: a rectangle 7.25 x 51 mm
                                                   * whose top right and bottom left corners are round with one radius (circular in mm, so not
                                                   * stretched), the other two square */
    float su[3 * W], sv[3 * H];
    int16_t slx[3 * H]; /* the wordmark's column (-1: off it) */
    int8_t sly[3 * W];  /* its row */
    for (int i = 0; i < 3 * W; i++) {
        const float fx = (float)(i / 3) + 0.5f, px_ = fx + ((float)(i % 3) - 1.0f) / 3.0f;
        su[i] = (px_ - 1.5f) / ppm_x;
        const float across = (px_ - cx) / ppm_x, ly = (float)wy0 + wh / 2.0f + across / s;
        sly[i] = (int8_t)(ly >= (float)wy0 && ly < (float)wy1 + 1.0f ? (int)ly : -1);
    }
    for (int j = 0; j < 3 * H; j++) {
        const float fy = (float)(j / 3) + 0.5f, py_ = fy + ((float)(j % 3) - 1.0f) / 3.0f;
        sv[j] = (py_ - pt) / ppm_y;
        const float along = (cy - py_) / ppm_y, lx = (float)wx0 + ww / 2.0f + along / s;
        slx[j] = (int16_t)(lx >= (float)wx0 && lx < (float)wx1 + 1.0f ? (int)lx : -1);
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float r = 47, g = 86, b = 175; /* the pill: the bar's blue a little lighter; the rest of the picture is clear (the bar's face under it is plain) */
            float cov = 0.0f;
            int at = 0;
            for (int sy = 0; sy < 3; sy++) {
                const float v = sv[3 * y + sy];
                const int lx = slx[3 * y + sy];
                for (int sx = 0; sx < 3; sx++) {
                    const float u = su[3 * x + sx];
                    int in = u >= 0.0f && u <= PW && v >= 0.0f && v <= PH;
                    if (in && u > PW - R && v < R) {
                        in = (u - (PW - R)) * (u - (PW - R)) + (v - R) * (v - R) <= R * R;
                    } else if (in && u < R && v > PH - R) {
                        in = (u - R) * (u - R) + (v - (PH - R)) * (v - (PH - R)) <= R * R;
                    }
                    cov += in ? 1.0f / 9.0f : 0.0f;
                    const int ly = sly[3 * x + sx];
                    if (lx >= 0 && ly >= 0) {
                        at += (int)(logo[ly * LOGO_W + lx] >> 24);
                    }
                }
            }
            const float a = (float)at / 255.0f / 9.0f; /* the text's coverage */
            r += (17 - r) * a, g += (34 - g) * a, b += (85 - b) * a; /* dark navy lettering */
            if (a > cov) {
                cov = a;
            }
            px[y * W + x] = pack4444_dither_a(r, g, b, cov, x, y);
        }
    }
    upload_argb4444(px, W, H, &pill_tex);
    free(px);
    free(logo);
    return pill_tex.valid ? &pill_tex : NULL;
}

/* The cases' spines (BMODEL_TEX_SPINE): a generic label per region with the game's own title and product code on it, like the real ones:
 * the BIOS's Dreamcast logo (the swirl one) at the bottom end, the title in the middle and the product code at the top end, all reading
 * upwards. PAL: blue with white ink. NTSC-J: white with black ink and a coral band with the logo in white. American (as the cases on a
 * shelf): black with white ink and the swirl in orange, or, for the clear case with the white tray, white with black ink and "Sega"
 * before the logo in grey. 64 x 512: the picture's width is the case's depth, its height the case's height. The BIOS holds no SEGA logo as a
 * picture (the boot animation's is a model), so only the Dreamcast one is used. */
enum { SPINE_PAL, SPINE_J, SPINE_US, SPINE_USW, SPINE_KINDS };
static char spine_title[64], spine_code[16];

void
gfx_model_bind_title(const char* title, const char* product) {
    char t[sizeof(spine_title)], c[sizeof(spine_code)];
    strncpy(t, title ? title : "", sizeof(t) - 1);
    t[sizeof(t) - 1] = '\0';
    strncpy(c, product ? product : "", sizeof(c) - 1);
    c[sizeof(c) - 1] = '\0';
    if (strcmp(t, spine_title) || strcmp(c, spine_code)) {
        strcpy(spine_title, t);
        strcpy(spine_code, c);
        memo_ok = 0; /* the spine's picture is another one now */
    }
}

/* One string in the BIOS font as a coverage picture (0..255 per pixel, glyph pixels only), tw x 24. Returns its buffer (malloc) or NULL. */
static uint8_t*
text_coverage(const char* str, int* tw) {
    int w = btext_width(str) + 4;
    if (w < 8 || w > 1024) {
        return NULL;
    }
    uint16_t* cv = calloc((size_t)w * GFX_LINE_H, 2);
    uint8_t* cov = calloc((size_t)w * GFX_LINE_H, 1);
    if (!cv || !cov) {
        free(cv);
        free(cov);
        return NULL;
    }
    btext_draw(cv, w, GFX_LINE_H, 0, 0, BIOS_FONT_ROM, str, 0xFFFF);
    for (int i = 0; i < w * GFX_LINE_H; i++) {
        cov[i] = ((cv[i] >> 12) == 15 && ((cv[i] >> 8) & 15) >= 12) ? 255 : 0; /* the glyph, not its shade */
    }
    free(cv);
    *tw = w;
    return cov;
}

/* The NTSC cases' hinge side beside the front (BMODEL_TEX_PILL on those cases): the inner label seen through the clear
 * front spine, 14 x 120 mm. White cases: a white label with the logo in colour (SEGA, the orange swirl, Dreamcast)
 * reading upwards; dark cases: a black label with the logo repeated in dark grey along it. 64 x 512. */
static rom_tex hinge_tex[2];
static rom_tex*
hinge_texture_impl(int dark) {
    enum { W = 64, H = 512 };
    static int tried[2];
    dark = dark ? 1 : 0;
    if (hinge_tex[dark].valid || tried[dark] || !g_rom) {
        return hinge_tex[dark].valid ? &hinge_tex[dark] : NULL;
    }
    tried[dark] = 1;
    bios_texture t;
    uint32_t* logo = malloc(sizeof(uint32_t) * LOGO_W * LOGO_H);
    unsigned short* px = calloc((size_t)W * H, sizeof(unsigned short));
    if (!px || !logo || bios_header_logo(g_rom, 0, &t) != 0 || t.width != LOGO_W || t.height != LOGO_H || bios_texture_decode(&t, logo) != 0) {
        free(logo);
        free(px);
        return NULL;
    }
    int x0 = LOGO_W, x1 = -1, y0 = LOGO_H, y1 = -1; /* the whole logo: swirl and wordmark */
    for (int y = 0; y < LOGO_H; y++) {
        for (int x = 0; x < LOGO_W; x++) {
            if ((logo[y * LOGO_W + x] >> 24) > 0x60) {
                x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
            }
        }
    }
    int tw = 0;
    uint8_t* sega = dark ? NULL : text_coverage("SEGA", &tw);
    static const float bgs[2][3] = {{238, 238, 234}, {14, 14, 16}};
    static const float inks[2][3] = {{40, 40, 44}, {62, 62, 66}};
    static const float swirls[2][3] = {{232, 104, 28}, {62, 62, 66}};
    const float* bg = bgs[dark];
    const float* ink = inks[dark];
    const float* sw = swirls[dark];
    const float ppm_x = (float)W / 14.0f, ppm_y = (float)H / 120.0f;
    const float s = x1 >= x0 ? (dark ? 10.0f : 7.0f) / (float)(y1 - y0 + 1) : 1.0f; /* mm per logo pixel: the logo is 7 mm across (10 on the black label, as the real one) */
    const float L = x1 >= x0 ? (float)(x1 - x0 + 1) * s : 0.0f;
    const float sg = 5.0f / 18.0f, Ls = sega ? (float)tw * sg : 0.0f, gap = sega ? 2.0f : 0.0f; /* SEGA: 5 mm capitals */
    const float a0 = 60.0f - (Ls + gap + L) / 2.0f, b0 = a0 + Ls + gap; /* where SEGA and the logo start, mm from the bottom */
    const float lyc = (float)(y0 + y1 + 1) / 2.0f;
    /* 2 x 2 samples per pixel. How far up the label a sample is depends only on its row and how far across only on its column, so
     * what each sample row crosses (SEGA's letter column or the logo's column) and each sample column's row in them are worked out
     * once: the pixels only look them up. */
    int16_t rs[2 * H], rl[2 * H]; /* per sample row: SEGA's column (-1: none), the logo's column (-1: none); -2 in rs: on SEGA's rows, off its letters */
    int8_t cs[2 * W], cl[2 * W];  /* per sample column: SEGA's row, the logo's row (-1: none) */
    for (int j = 0; j < 2 * H; j++) {
        const float fy = (float)j * 0.5f + 0.25f, along = ((float)H - fy) / ppm_y;
        rs[j] = -1, rl[j] = -1;
        if (sega && along >= a0 && along < a0 + Ls) {
            const int tx = (int)((along - a0) / sg);
            rs[j] = (int16_t)(tx >= 0 && tx < tw ? tx : -2);
        } else if (x1 >= x0 && (dark ? along >= 3.0f : (along >= b0 && along < b0 + L))) {
            const float kk = dark ? fmodf(along - 3.0f, L + 6.0f) : along - b0;
            const int lx = kk < L ? x0 + (int)(kk / s) : -1;
            rl[j] = (int16_t)(lx >= 0 && lx < LOGO_W ? lx : -1);
        }
    }
    for (int i = 0; i < 2 * W; i++) {
        const float fx = (float)i * 0.5f + 0.25f, across = fx / ppm_x - 7.0f;
        const int ty = (int)(12.0f + across / sg), ly = x1 >= x0 ? (int)(lyc + across / s) : -1;
        cs[i] = (int8_t)(ty >= 0 && ty < GFX_LINE_H ? ty : -1);
        cl[i] = (int8_t)(ly >= 0 && ly < LOGO_H ? ly : -1);
    }
    unsigned short plain[16]; /* the label without anything on it, per cell of the dither */
    for (int c = 0; c < 16; c++) {
        plain[c] = pack4444_dither(bg[0], bg[1], bg[2], c & 3, c >> 2);
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float r = bg[0], g = bg[1], b = bg[2];
            int any = 0;
            for (int k = 0; k < 4; k++) {
                const int j = 2 * y + (k >> 1), i = 2 * x + (k & 1);
                int al = 0;
                const float* c = ink;
                if (rs[j] != -1) {
                    if (rs[j] >= 0 && cs[i] >= 0) {
                        al = sega[cs[i] * tw + rs[j]];
                    }
                } else if (rl[j] >= 0 && cl[i] >= 0) {
                    al = (int)(logo[cl[i] * LOGO_W + rl[j]] >> 24);
                    c = rl[j] < LOGO_SWIRL_W ? sw : ink;
                }
                if (al) {
                    const float a = (float)al / 255.0f;
                    r += (c[0] - bg[0]) * a / 4.0f, g += (c[1] - bg[1]) * a / 4.0f, b += (c[2] - bg[2]) * a / 4.0f;
                    any = 1;
                }
            }
            px[y * W + x] = any ? pack4444_dither(r, g, b, x, y) : plain[(y & 3) * 4 + (x & 3)];
        }
    }
    upload_argb4444(px, W, H, &hinge_tex[dark]);
    free(sega);
    free(px);
    free(logo);
    return hinge_tex[dark].valid ? &hinge_tex[dark] : NULL;
}

static rom_tex*
hinge_texture(int dark) {
    rom_tex* _r;
    uint64_t _p0 = timer_us_gettime64();
    _r = hinge_texture_impl(dark);
    gfx_prof_us[0] += (unsigned)(timer_us_gettime64() - _p0);
    return _r;
}

/* The spines are made in two steps. The part every game of a kind shares (background, patch, the logo) is drawn once per kind into
 * spine_base and also kept as a picture of its own (spine_plain): what a case shows while its own spine is not made yet. The title
 * and the product code are drawn over a copy of it, only over the rows and columns the letters cover, with integer blending. At
 * most one spine is made per frame, and the last SPINE_SLOTS made are kept, so a case scrolled back to, and the old and new case
 * drawn in the same frame while they fly, do not make theirs again. A slot used in this frame or the previous one (the PVR may
 * still be drawing that one) is never replaced. */
#define SPINE_SLOTS 4
typedef struct {
    rom_tex tex;
    char key[64 + 16 + 4];
    uint32_t used;
} spine_slot;
static spine_slot spine_slots[SPINE_SLOTS];
static uint8_t* spine_base[SPINE_KINDS * 2]; /* RGB, 64 x 512 */
static unsigned short* spine_base_px[SPINE_KINDS * 2]; /* ... packed for the PVR */
static rom_tex spine_plain[SPINE_KINDS * 2];
static int spine_base_tried[SPINE_KINDS * 2];
static uint32_t spine_made_frame = 0xFFFFFFFFu;
enum { SPINE_W = 64, SPINE_H = 512 };
/* background, patch, ink (title), band, the logo's swirl, its wordmark, the product code */
static const uint8_t spine_col[SPINE_KINDS][7][3] = {
    {{40, 73, 149}, {45, 82, 167}, {0xF8, 0xF8, 0xFF}, {40, 73, 149}, {0xF8, 0xF8, 0xFF}, {0xF8, 0xF8, 0xFF}, {0xF8, 0xF8, 0xFF}},
    {{0xF2, 0xF2, 0xEE}, {0xF2, 0xF2, 0xEE}, {0x14, 0x14, 0x18}, {0xE4, 0x5E, 0x58}, {0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF}, {0x30, 0x30, 0x34}},
    {{0x10, 0x10, 0x12}, {0x10, 0x10, 0x12}, {0xF4, 0xF4, 0xF4}, {0x10, 0x10, 0x12}, {0xE8, 0x5A, 0x24}, {0xF4, 0xF4, 0xF4}, {0xC8, 0xC8, 0xC8}},
    {{0xF4, 0xF4, 0xF0}, {0xF4, 0xF4, 0xF0}, {0x16, 0x16, 0x1A}, {0xF4, 0xF4, 0xF0}, {0xE8, 0x5A, 0x24}, {0x70, 0x70, 0x74}, {0x50, 0x50, 0x54}}};

/* The label is depth x 120 mm (7 mm on the NTSC cases, 15.4 on the PAL one): picture pixels per mm across over those along. The logo
 * and the letters are drawn that much wider across than along, so they are not stretched on the case. */
static float
spine_ra(int kind) {
    /* kinds from SPINE_KINDS on: the same for the double case, whose two spine labels (one per half) are 10 mm across */
    const float depth_mm = kind >= SPINE_KINDS ? 10.0f : kind % SPINE_KINDS == SPINE_PAL ? 15.5f : 7.03f;
    return ((float)SPINE_W / depth_mm) / ((float)SPINE_H / 120.0f);
}

/* rgb into px, the box x0..x1, y0..y1 (exclusive ends) */
static void
spine_pack(const uint8_t* rgb, int kind, unsigned short* px, int x0, int y0, int x1, int y1) {
    lvl_init();
    const int pal = kind % SPINE_KINDS == SPINE_PAL;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const int i = y * SPINE_W + x;
            const uint8_t* l = pal ? lvl_dither[bayer4[y & 3][x & 3]] : lvl_round;
            px[i] = (unsigned short)(0xF000u | (unsigned)l[rgb[i * 3]] << 8 | (unsigned)l[rgb[i * 3 + 1]] << 4 | l[rgb[i * 3 + 2]]);
        }
    }
}

static void spine_text(uint8_t* rgb, int kind, const char* str, float avail, float mid, float best, const uint8_t* ink, int box[4]);

/* The shared part of a kind's spine; NULL when it cannot be made. */
static const uint8_t*
spine_base_of_impl(int kind) {
    if (spine_base[kind] || spine_base_tried[kind] || !g_rom) {
        return spine_base[kind];
    }
    spine_base_tried[kind] = 1;
    enum { W = SPINE_W, H = SPINE_H };
    uint8_t* rgb = malloc((size_t)W * H * 3);
    uint32_t* logo = malloc(sizeof(uint32_t) * LOGO_W * LOGO_H);
    bios_texture t;
    if (!rgb) {
        free(logo);
        return NULL;
    }
    int lx0 = LOGO_W, lx1 = 0, ly0 = LOGO_H, ly1 = 0;
    if (!logo || bios_header_logo(g_rom, 0, &t) != 0 || t.width != LOGO_W || t.height != LOGO_H || bios_texture_decode(&t, logo) != 0) {
        free(logo);
        logo = NULL;
    } else {
        for (int y = 0; y < LOGO_H; y++) {
            for (int x = 0; x < LOGO_W; x++) {
                if ((logo[y * LOGO_W + x] >> 24) > 0x60) {
                    lx0 = x < lx0 ? x : lx0, lx1 = x > lx1 ? x : lx1;
                    ly0 = y < ly0 ? y : ly0, ly1 = y > ly1 ? y : ly1;
                }
            }
        }
    }
    const uint8_t* bg = spine_col[kind % SPINE_KINDS][0];
    const uint8_t* patch = spine_col[kind % SPINE_KINDS][1];
    const uint8_t* band = spine_col[kind % SPINE_KINDS][3];
    const uint8_t* swirl = spine_col[kind % SPINE_KINDS][4];
    const uint8_t* word = spine_col[kind % SPINE_KINDS][5];
    const float ra = spine_ra(kind);
    const float cr = 10.0f, mx = 5.0f, my = 18.0f; /* the patch: corner radius, margins */
    const float pw = W - 2 * mx, ph = H - 2 * my;
    float ddx[W], ddy[H]; /* how far into a rounded corner each column / row is, squared; -1: off the patch */
    for (int x = 0; x < W; x++) {
        const float fx = (float)x + 0.5f - mx;
        const float dx = fx < cr ? cr - fx : ((pw - fx) < cr ? cr - (pw - fx) : 0.0f);
        ddx[x] = fx >= 0 && fx <= pw ? dx * dx : -1.0f;
    }
    for (int y = 0; y < H; y++) {
        const float fy = (float)y + 0.5f - my;
        const float dy = fy < cr ? cr - fy : ((ph - fy) < cr ? cr - (ph - fy) : 0.0f);
        ddy[y] = fy >= 0 && fy <= ph ? dy * dy : -1.0f;
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t* o = &rgb[(y * W + x) * 3];
            const uint8_t* c = bg;
            if (ddx[x] >= 0.0f && ddy[y] >= 0.0f && ddx[x] + ddy[y] <= cr * cr) {
                c = patch;
            }
            if (kind % SPINE_KINDS == SPINE_J && (float)y > H - 150.0f) {
                c = band; /* across the whole label */
            }
            o[0] = c[0], o[1] = c[1], o[2] = c[2];
        }
    }
    /* the logo, at the bottom end, turned a quarter to the left (reading upwards): its x runs up the spine */
    if (logo && lx1 > lx0) {
        const float sw = (float)(lx1 - lx0 + 1), sh = (float)(ly1 - ly0 + 1);
        float s = 118.0f / sw;
        if (sh * s * ra > 0.6f * (float)W) {
            s = 0.6f * (float)W / (sh * ra);
        }
        const float cy = (float)H - (kind % SPINE_KINDS == SPINE_USW ? 62.0f : 30.0f) - sw * s / 2.0f, cx = (float)W / 2.0f; /* its centre on the picture */
        int cly[W]; /* per column: the logo's row (-1: off it); its column per picture row below */
        for (int x = 0; x < W; x++) {
            const float ly = (float)ly0 + sh / 2.0f + ((float)x + 0.5f - cx) / (s * ra);
            cly[x] = ly >= (float)ly0 && (int)ly <= ly1 ? (int)ly : -1;
        }
        for (int y = 0; y < H; y++) {
            const float lx = (float)lx0 + sw / 2.0f + (cy - ((float)y + 0.5f)) / s;
            const int ix = (int)lx;
            if (!(lx >= (float)lx0 && ix <= lx1)) {
                continue;
            }
            const uint8_t* ink = ix <= LOGO_SWIRL_W ? swirl : word;
            for (int x = 0; x < W; x++) {
                if (cly[x] < 0) {
                    continue;
                }
                const int a = (int)(logo[cly[x] * LOGO_W + ix] >> 24);
                if (a) {
                    uint8_t* o = &rgb[(y * W + x) * 3];
                    for (int k = 0; k < 3; k++) {
                        o[k] = (uint8_t)(o[k] + div255(((int)ink[k] - (int)o[k]) * a));
                    }
                }
            }
        }
    }
    free(logo);
    if (kind % SPINE_KINDS == SPINE_USW) { /* "Sega" before the swirl, as on the shelf */
        int box[4] = {W, H, 0, 0};
        spine_text(rgb, kind, "Sega", 30.0f, (float)H - 44.0f, 0.62f, word, box);
    }
    unsigned short* px = malloc(sizeof(unsigned short) * W * H);
    if (!px) {
        free(rgb);
        return NULL;
    }
    spine_pack(rgb, kind, px, 0, 0, W, H);
    upload_argb4444(px, W, H, &spine_plain[kind]);
    spine_base[kind] = rgb;
    spine_base_px[kind] = px;
    return rgb;
}

static const uint8_t*
spine_base_of(int kind) {
    const uint8_t* _r;
    uint64_t _p0 = timer_us_gettime64();
    _r = spine_base_of_impl(kind);
    gfx_prof_us[2] += (unsigned)(timer_us_gettime64() - _p0);
    return _r;
}

/* The kind's spine without title: shown until the game's own is made. */
static rom_tex*
spine_plain_texture(int kind) {
    spine_base_of(kind);
    return spine_plain[kind].valid ? &spine_plain[kind] : NULL;
}

/* A string over the picture, reading upwards: its middle at picture row `mid`, in at most `avail` rows, glyphs at best `best`
 * picture pixels per font pixel, centred across. Only the rows and columns the letters can cover are visited. */
static void
spine_text(uint8_t* rgb, int kind, const char* str, float avail, float mid, float best, const uint8_t* ink, int box[4]) {
    enum { W = SPINE_W, H = SPINE_H };
    int tw = 0;
    uint8_t* cov = str[0] ? text_coverage(str, &tw) : NULL;
    if (!cov) {
        return;
    }
    const float ra = spine_ra(kind);
    float sl = avail / (float)tw; /* along the spine */
    if (sl > best) {
        sl = best;
    }
    if (sl * ra * 24.0f > 0.55f * (float)W) {
        sl = 0.55f * (float)W / (24.0f * ra); /* the letters fit across the label */
    }
    const float st = sl * ra; /* across it: in proportion */
    const float cxp = (float)W / 2.0f;
    int ya = (int)(mid - (float)tw / 2.0f * sl) - 2, yb = (int)(mid + (float)tw / 2.0f * sl) + 2;
    int xa = (int)(cxp - 12.5f * st) - 2, xb = (int)(cxp + 12.5f * st) + 2;
    ya = ya < 0 ? 0 : ya, yb = yb > H ? H : yb;
    xa = xa < 0 ? 0 : xa, xb = xb > W ? W : xb;
    if (xa < xb && ya < yb) { /* what has to be packed again */
        box[0] = xa < box[0] ? xa : box[0], box[1] = ya < box[1] ? ya : box[1];
        box[2] = xb > box[2] ? xb : box[2], box[3] = yb > box[3] ? yb : box[3];
    }
    const float isl = 1.0f / sl, ist = 1.0f / st;
    /* text y runs to the right: per column, the coverage rows of its two samples (offsets into cov; -1: off the glyphs) */
    int jo[W][2];
    for (int x = xa; x < xb; x++) {
        const float ty = 12.0f + ((float)x + 0.5f - cxp) * ist;
        const int in = !(ty < -0.25f || ty >= (float)GFX_LINE_H + 0.25f);
        const int jy0 = (int)(ty - 0.25f), jy1 = (int)(ty + 0.25f);
        jo[x][0] = in && jy0 >= 0 && jy0 < GFX_LINE_H ? jy0 * tw : -1;
        jo[x][1] = in && jy1 >= 0 && jy1 < GFX_LINE_H ? jy1 * tw : -1;
    }
    for (int y = ya; y < yb; y++) {
        /* text x runs up the picture; the glyph rows 2..22 are centred */
        const float tx = (float)tw / 2.0f + (mid - ((float)y + 0.5f)) * isl;
        if (tx < -0.25f || tx >= (float)tw + 0.25f) {
            continue;
        }
        const int jx0 = (int)(tx - 0.25f), jx1 = (int)(tx + 0.25f);
        const int ok0 = jx0 >= 0 && jx0 < tw, ok1 = jx1 >= 0 && jx1 < tw;
        for (int x = xa; x < xb; x++) {
            /* two by two samples, so a squeezed text does not break up; each is 0 or 255, so the sum is n * 255 */
            int n = 0;
            for (int k = 0; k < 2; k++) {
                if (jo[x][k] >= 0) {
                    n += (ok0 && cov[jo[x][k] + jx0]) + (ok1 && cov[jo[x][k] + jx1]);
                }
            }
            if (n) {
                uint8_t* o = &rgb[(y * W + x) * 3];
                for (int k = 0; k < 3; k++) {
                    o[k] = (uint8_t)(o[k] + (((int)ink[k] - (int)o[k]) * n) / 4); /* as (d * n * 255) / 1020 */
                }
            }
        }
    }
    free(cov);
}

static rom_tex*
spine_texture_impl(int kind) {
    enum { W = SPINE_W, H = SPINE_H };
    char key[sizeof(spine_slots[0].key)];
    snprintf(key, sizeof(key), "%d|%s|%s", kind, spine_title, spine_code);
    for (int i = 0; i < SPINE_SLOTS; i++) {
        if (spine_slots[i].tex.valid && !strcmp(spine_slots[i].key, key)) {
            spine_slots[i].used = frame_no;
            return &spine_slots[i].tex;
        }
    }
    const uint8_t* base = spine_base_of(kind);
    if (!base) {
        return NULL;
    }
    if (spine_made_frame == frame_no) {
        return spine_plain_texture(kind); /* one made this frame already: this one on the next */
    }
    spine_slot* victim = NULL;
    for (int i = 0; i < SPINE_SLOTS; i++) {
        spine_slot* sl = &spine_slots[i];
        if (!sl->tex.valid) {
            victim = sl;
            break;
        }
        if (sl->used + 1 < frame_no && (!victim || sl->used < victim->used)) {
            victim = sl;
        }
    }
    if (!victim) {
        return spine_plain_texture(kind); /* all in use on the screen: not now */
    }
    uint8_t* rgb = malloc((size_t)W * H * 3);
    unsigned short* px = rgb ? memalign(32, sizeof(unsigned short) * W * H) : NULL; /* uploaded as it is */
    if (!px) {
        free(rgb);
        return spine_plain_texture(kind);
    }
    spine_made_frame = frame_no;
    memcpy(rgb, base, (size_t)W * H * 3);
    memcpy(px, spine_base_px[kind], sizeof(unsigned short) * W * H);
    int box[4] = {W, H, 0, 0};
    spine_text(rgb, kind, spine_title, 296.0f, 212.0f, 0.95f, spine_col[kind % SPINE_KINDS][2], box);
    spine_text(rgb, kind, spine_code, 84.0f, 52.0f, 0.62f, spine_col[kind % SPINE_KINDS][6], box);
    spine_pack(rgb, kind, px, box[0], box[1], box[2], box[3]);
    free(rgb);
    if (victim->tex.valid) {
        pvr_mem_free(victim->tex.ptr);
    }
    memset(victim, 0, sizeof(*victim));
    upload_argb4444_staged(px, W, H, &victim->tex);
    if (!victim->tex.valid) {
        return spine_plain_texture(kind);
    }
    strcpy(victim->key, key);
    victim->used = frame_no;
    return &victim->tex;
}

static rom_tex*
spine_texture(int kind) {
    rom_tex* _r;
    uint64_t _p0 = timer_us_gettime64();
    _r = spine_texture_impl(kind);
    gfx_prof_us[3] += (unsigned)(timer_us_gettime64() - _p0);
    return _r;
}

static rom_tex*
get_rom_texture_uncached(bscene_texref ref) {
    if (ref.kind == BSCENE_TEX_TEXLIST && ref.b == 0) {
        for (int i = 0; i < decal_n; i++) {
            if (decal_ovr[i].texlist == ref.a && decal_ovr[i].dyn >= 0 && decal_ovr[i].dyn < DYN_MAX && dyn_tex[decal_ovr[i].dyn].valid) {
                return &dyn_tex[decal_ovr[i].dyn];
            }
        }
    }
    /* built-in models: slot 0 shows the box art of the bound game, everything else is plain white */
    if (ref.kind == BSCENE_TEX_TEXLIST && ref.a >= BMODEL_BASE && ref.a < BMODEL_END) {
        const int ca = bmodel_case_single(ref.a); /* a double case: the single one's pictures */
        if (ref.b == BMODEL_TEX_FRONT) {
            art_entry* e = art_get(model_front_product, 1);
            if (e && e->tex.valid) {
                return &e->tex;
            }
            rom_tex* r = generic_cover(ca == BMODEL_CASE_PAL, 0); /* no box art: the generic front of the region */
            if (r) {
                return r;
            }
        }
        if (ref.b == BMODEL_TEX_BACK) {
            art_entry* e = art_get(model_front_product, 2); /* BACK.DAT */
            if (e && e->tex.valid) {
                return &e->tex;
            }
            rom_tex* r = generic_cover(ca == BMODEL_CASE_PAL, 1);
            if (r) {
                return r;
            }
        }
        if (ref.b == BMODEL_TEX_PILL) { /* the PAL hinge bar, or the NTSC cases' inner label behind the clear front spine */
            rom_tex* r = ca == BMODEL_CASE_PAL ? pill_texture() : hinge_texture(ca == BMODEL_CASE_CLEAR_BLACK);
            if (r) {
                return r;
            }
        }
        if (ref.b == BMODEL_TEX_SPINE) {
            art_entry* e = art_get(model_front_product, 3); /* SPINE.DAT */
            if (e && e->tex.valid) {
                return &e->tex;
            }
            const int kind = (ca == BMODEL_CASE_PAL ? SPINE_PAL : ca == BMODEL_CASE_WHITE ? SPINE_J : ca == BMODEL_CASE_CLEAR_WHITE ? SPINE_USW : SPINE_US)
                             + (bmodel_case_is_double(ref.a) ? SPINE_KINDS : 0);
            if (e && e->loading) {
                rom_tex* r = spine_plain_texture(kind); /* SPINE.DAT may have one: none is made until it is known */
                if (r) {
                    return r;
                }
            }
            rom_tex* r = spine_texture(kind);
            if (r) {
                return r;
            }
        }
        if (ref.b == BMODEL_TEX_RIBS) {
            rom_tex* r = ribs_texture();
            if (r) {
                return r;
            }
        }
        return white_tex();
    }
    /* A row disc: texture 0 is the label (the game's art); the rest is the BIOS disc, texlist 61 */
    if (ref.kind == BSCENE_TEX_TEXLIST && ref.a >= BLIST_TEXLIST_BASE) {
        int slot = ref.a - BLIST_TEXLIST_BASE;
        if (ref.b == 0 && slot < BLIST_MAX_SLOTS) {
            art_entry* e = art_get(row_product[slot], 0);
            if (e && e->tex.valid) {
                return &e->tex;
            }
            rom_tex* face = disc_face(row_pal[slot]); /* no picture: the BIOS disc of the game's region */
            if (face) {
                return face;
            }
        }
        ref.a = BLIST_DISC_MODEL;
    }
    if (logo_tex.valid && ref.kind == BSCENE_TEX_TEXLIST && ref.a == BMENU_HEADER_MODEL && ref.b == BMENU_LOGO_SLOT) {
        return &logo_tex;
    }
    if ((logo_mode == 2 || logo_dark) && ref.kind == BSCENE_TEX_TEXLIST && ref.a == BMENU_HEADER_MODEL && ref.b == BMENU_LOGO_SLOT) {
        build_logo_variants();
        if (logo_mode == 2) {
            rom_tex* word = logo_dark ? &logo_cnet_dark_tex : &logo_dcnet_text_tex;
            rom_tex* cropped = logo_dark ? &logo_dcnet_dark_tex : &logo_dcnet_tex;
            if (cropped->valid && word->valid) {
                return cropped;
            }
        } else if (logo_dark_tex.valid) {
            return &logo_dark_tex;
        }
    }
    for (int i = 0; i < num_rom_texes; i++) {
        if (rom_texes[i].kind == ref.kind && rom_texes[i].a == ref.a && rom_texes[i].b == ref.b) {
            return rom_texes[i].valid ? &rom_texes[i] : NULL;
        }
    }
    if (num_rom_texes >= MAX_TEXTURES || !g_rom) {
        return NULL;
    }

    rom_tex* g = &rom_texes[num_rom_texes++];
    memset(g, 0, sizeof(*g));
    g->kind = ref.kind;
    g->a = ref.a;
    g->b = ref.b;

    bios_texture t;
    int ok;
    if (ref.kind == BSCENE_TEX_GBIX) {
        ok = bios_texture_find(g_rom, (uint32_t)ref.a, &t) == 0;
    } else if (ref.a == BMENU_HEADER_MODEL && ref.b == BMENU_LOGO_SLOT) {
        /* the logo with the swirl, or the plain one on a European console, as the BIOS picks it */
        ok = bios_header_logo(g_rom, GFX_REGION_BYTE & 0xF, &t) == 0;
    } else {
        ok = bios_texlist_texture(g_rom, ref.a, ref.b, &t) == 0;
    }
    if (!ok || t.data_size == 0) {
        return NULL;
    }

    upload_texture(&t, g);
    return g->valid ? g : NULL;
}

int
gfx_load_logo(const char* path) {
    if (logo_tex.valid) {
        return 0;
    }
    file_t fd = fs_open(path, O_RDONLY);
    if (fd == FILEHND_INVALID) {
        return -1;
    }
    ssize_t size = fs_total(fd);
    uint8_t* buf = (size > 0 && size <= MAX_LOGO_FILE) ? (uint8_t*)malloc((size_t)size) : NULL;
    if (buf && fs_read(fd, buf, (size_t)size) != size) {
        free(buf);
        buf = NULL;
    }
    fs_close(fd);
    if (!buf) {
        return -1;
    }

    bios_texture t;
    if (bios_texture_parse(buf, (size_t)size, &t) == 0) {
        upload_texture(&t, &logo_tex);
    }
    free(buf);
    return logo_tex.valid ? 0 : -1;
}

/* ---- Submission --------------------------------------------------------------------- */

/* Headers and vertices are collected here and sent with a few large pvr_prim() calls: each call
 * locks the store queues, which cost more than the triangles themselves when done per polygon. */
#define CMD_SLOTS 2048
static pvr_vertex_t cmdbuf[CMD_SLOTS] __attribute__((aligned(32)));
static int cmd_n;

static void
cmd_flush(void) {
    if (cmd_n) {
        uint64_t t0 = timer_us_gettime64();
#if GFX_VERTEX_DMA
        if (tr_dma_buf) {
            unsigned bytes = (unsigned)cmd_n * (unsigned)sizeof(pvr_vertex_t);
            int op = cur_list_op;
            unsigned room = op ? OP_DMA_BYTES / 2 : TR_DMA_BYTES / 2;
            if (frame_bytes[op] + bytes + 128u > room) {
                cmd_n = 0; /* this frame's list is full: drop the rest rather than run over the buffer */
                return;
            }
            frame_bytes[op] += bytes;
        }
#endif
        pvr_prim(cmdbuf, cmd_n * (int)sizeof(pvr_vertex_t));
        stats.ta_us += (unsigned)(timer_us_gettime64() - t0);
        stats.vertices += (unsigned)cmd_n;
        cmd_n = 0;
    }
}

static void
cmd_reserve(int slots) {
    if (cmd_n + slots > CMD_SLOTS) {
        cmd_flush();
    }
}

static void
submit_header(pvr_poly_cxt_t* cxt) {
    cmd_reserve(1);
    pvr_poly_compile((pvr_poly_hdr_t*)&cmdbuf[cmd_n++], cxt);
}

/* `uv`: the NJ_TEX_* flip / clamp bits of a model's texture (used with `tex` only). */
static void
send_header_tr(const rom_tex* tex, pvr_ptr_t text_ptr, int text_w, int uv) {
    pvr_poly_cxt_t cxt;

    if (tex) {
        pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, tex->fmt, tex->w, tex->h, tex->ptr,
                         tex->nearest ? PVR_FILTER_NONE : PVR_FILTER_BILINEAR);
        cxt.gen.specular = PVR_SPECULAR_ENABLE; /* the offset colour: the BIOS' specular on textured strips */
        /* the model's texture chunk: mirror or hold u and v past 0..1 (the disc's hub is a quarter, mirrored) */
        cxt.txr.uv_flip = ((uv & NJ_TEX_FLIP_U) ? PVR_UVFLIP_U : 0) | ((uv & NJ_TEX_FLIP_V) ? PVR_UVFLIP_V : 0);
        cxt.txr.uv_clamp = ((uv & NJ_TEX_CLAMP_U) ? PVR_UVCLAMP_U : 0) | ((uv & NJ_TEX_CLAMP_V) ? PVR_UVCLAMP_V : 0);
    } else if (text_ptr) {
        pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED, text_w, GFX_LINE_H, text_ptr,
                         PVR_FILTER_NONE);
    } else {
        pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    }
    cxt.gen.culling = PVR_CULLING_NONE;
    cxt.blend.src = PVR_BLEND_SRCALPHA;
    cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
    if (text_ptr || !tex) {
        /* Text and flat panels: their transparent parts must not hide what is drawn later. */
        cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    }
    submit_header(&cxt);
    stats.headers++;
}

static void strip_close(void);

/* The polygon header for what is drawn next: a ROM / art picture (`tex`, with the model's uv bits), a text picture
 * (`text_ptr`, `text_w` wide) or flat colour (both NULL). Sent only when it differs from the one in force; ends a
 * pending strip of sink_triangle(). */
static void
ensure_header_uv(const rom_tex* tex, pvr_ptr_t text_ptr, int text_w, int uv) {
    strip_close();
    pvr_ptr_t key = tex ? tex->ptr : text_ptr;
    int untextured = (key == NULL);
    if (!tex) {
        uv = 0;
    }
    if (hdr_valid && key == last_ptr && untextured == last_untextured && uv == last_uv) {
        return;
    }
    send_header_tr(tex, text_ptr, text_w, uv);
    last_ptr = key;
    last_untextured = untextured;
    last_uv = uv;
    hdr_valid = 1;
}

static void
ensure_header(const rom_tex* tex, pvr_ptr_t text_ptr, int text_w) {
    ensure_header_uv(tex, text_ptr, text_w, 0);
}

/* 16:9: the picture is squeezed to 3/4 of its width around the centre; a wide TV stretches it back. */
static float aspect_x = 1.0f;
static int aspect_bypass; /* the background gradient always fills the screen */

void
gfx_set_aspect(int wide) {
    aspect_x = wide ? 0.75f : 1.0f;
}

/* One vertex into the command buffer. `oargb` is the PVR offset colour (the specular of textured strips, 0 otherwise).
 * The buffer may fill up in the middle of a strip: the TA takes a strip in pieces. */
static inline void
put_vertex(float x, float y, float z, float u, float v, uint32_t argb, uint32_t oargb, int last) {
    if (!aspect_bypass) {
        x = 320.0f + (x - 320.0f) * aspect_x;
    }
    cmd_reserve(1);
    pvr_vertex_t* vert = &cmdbuf[cmd_n++];
    vert->flags = last ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
    vert->x = x;
    vert->y = y;
    vert->z = z;
    vert->u = u;
    vert->v = v;
    vert->argb = argb;
    vert->oargb = oargb;
}

/* The scene sends separate triangles, but most come from the models' triangle strips: a triangle that shares the
 * last two vertices sent is sent as one more vertex of the same strip (culling is off, so the order does not
 * matter). A third of the vertices for the TA, the store queues and the PVR's object lists. The strip's last vertex
 * waits here until it is known whether the strip goes on. */
static bscene_vtx strip_l1, strip_l2;
static const rom_tex* strip_tex;
static int strip_uv;
static int strip_pending;

/* the same corner of the same strip: position, colours and picture coordinates equal (word compares, no memcmp call) */
static inline int
vtx_same(const bscene_vtx* a, const bscene_vtx* b) {
    return a->x == b->x && a->y == b->y && a->argb == b->argb && a->u == b->u && a->v == b->v && a->invw == b->invw
           && a->oargb == b->oargb;
}

static inline void
strip_vertex(const bscene_vtx* p, const rom_tex* tex, int last) {
    put_vertex(p->x, p->y, p->invw, p->u, p->v, p->argb, tex ? p->oargb : 0, last);
}

static void
strip_close(void) {
    if (strip_pending) {
        strip_pending = 0;
        strip_vertex(&strip_l2, strip_tex, 1);
    }
}

static void
send_vertex(float x, float y, float z, float u, float v, uint32_t argb, int last) {
    strip_close();
    put_vertex(x, y, z, u, v, argb, 0, last);
}

/* A rectangle of the picture of the header in force (u0..u1, v0..v1), one colour. */
static void
send_quad(float x, float y, float w, float h, float z, float u0, float v0, float u1, float v1, uint32_t argb) {
    send_vertex(x, y, z, u0, v0, argb, 0);
    send_vertex(x + w, y, z, u1, v0, argb, 0);
    send_vertex(x, y + h, z, u0, v1, argb, 0);
    send_vertex(x + w, y + h, z, u1, v1, argb, 1);
}

/* The whole picture `tex` in a rectangle. */
static void
picture_quad(const rom_tex* tex, float x, float y, float w, float h, float z, uint32_t argb) {
    ensure_header(tex, NULL, 0);
    send_quad(x, y, w, h, z, 0.0f, 0.0f, 1.0f, 1.0f, argb);
}

/* The header logo: where it is and how it is shaded this frame, for the "Now!" / "CNET" letters drawn after it (gfx_logo_badge). Called
 * for every triangle and strip before anything is skipped, so the letters slide and fade with the bar, invisible ones included. */
static void
note_logo(const bscene_vtx* v, int n, bscene_texref ref) {
    if (ref.kind != BSCENE_TEX_TEXLIST || ref.a != BMENU_HEADER_MODEL || ref.b != BMENU_LOGO_SLOT) {
        return;
    }
    for (int i = 0; i < n; i++) {
        if (!logo_seen || v[i].x < logo_ox) logo_ox = v[i].x;
        if (!logo_seen || v[i].y < logo_oy) logo_oy = v[i].y;
        logo_seen = 1;
    }
    logo_argb = v[0].argb;
}

static void
sink_triangle(void* user, const bscene_vtx v[3], bscene_texref ref) {
    (void)user;
    note_logo(v, 3, ref);
    if (!(v[0].argb >> 24) && !(v[1].argb >> 24) && !(v[2].argb >> 24)) {
        return; /* fully transparent */
    }
    rom_tex* tex = NULL;
    if (ref.kind != BSCENE_TEX_NONE) {
        tex = get_rom_texture(ref);
        if (!tex) {
            return; /* texture missing from this ROM: draw nothing rather than garbage */
        }
    }
    tri_count++;
    if (strip_pending && tex == strip_tex && ref.uv == strip_uv) {
        int a = -1, b = -1;
        for (int i = 0; i < 3; i++) {
            if (a < 0 && vtx_same(&v[i], &strip_l1)) {
                a = i;
            } else if (b < 0 && vtx_same(&v[i], &strip_l2)) {
                b = i;
            }
        }
        if (a >= 0 && b >= 0) {
            strip_vertex(&strip_l2, tex, 0); /* the strip goes on */
            strip_l1 = strip_l2;
            strip_l2 = v[3 - a - b];
            return;
        }
    }
    ensure_header_uv(tex, NULL, 0, ref.uv); /* also ends a pending strip */
    strip_uv = ref.uv;
    strip_vertex(&v[0], tex, 0);
    strip_vertex(&v[1], tex, 0);
    strip_l1 = v[1];
    strip_l2 = v[2];
    strip_tex = tex;
    strip_pending = 1;
}

static const char*
find_label(uint16_t id) {
    for (int i = 0; i < num_labels; i++) {
        if (labels[i].id == id) {
            return labels[i].text;
        }
    }
    return NULL;
}

static void text_windowed(const char* str, float x, float y, float z, uint32_t argb, int window, int offset);

/* ---- Text surfaces (bios_surface): the pictures the screens print into, kept in video memory ---------------- */

typedef struct {
    uint16_t id;
    rom_tex tex;
    uint32_t serial;
} surf_tex;
static surf_tex surf_texes[BSURF_MAX];

static rom_tex*
surface_texture(bsurf* sf) {
    surf_tex* st = NULL;
    for (int i = 0; i < BSURF_MAX; i++) {
        if (surf_texes[i].tex.valid && surf_texes[i].id == sf->id) {
            st = &surf_texes[i];
            break;
        }
    }
    if (st && (st->tex.w != sf->w || st->tex.h != sf->h)) {
        pvr_mem_free(st->tex.ptr);
        memset(st, 0, sizeof(*st));
        st = NULL;
    }
    if (!st) {
        for (int i = 0; i < BSURF_MAX && !st; i++) {
            if (!surf_texes[i].tex.valid) {
                st = &surf_texes[i];
            }
        }
        if (!st) {
            return NULL;
        }
        st->tex.ptr = pvr_mem_malloc((size_t)sf->w * (size_t)sf->h * 2u);
        if (!st->tex.ptr) {
            return NULL;
        }
        st->id = sf->id;
        st->tex.w = sf->w;
        st->tex.h = sf->h;
        st->tex.fmt = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED;
        st->tex.nearest = 1;
        st->tex.valid = 1;
        bsurf_touch(sf, 0, sf->h - 1); /* all of it the first time */
    }
    int y0, y1;
    if (bsurf_take_dirty(sf, &y0, &y1)) {
        size_t row = (size_t)sf->w * 2u;
        pvr_txr_load(sf->px + (size_t)y0 * sf->w, (uint8_t*)st->tex.ptr + (size_t)y0 * row, (size_t)(y1 - y0 + 1) * row);
        stats.text_uploads++;
    }
    return &st->tex;
}

/* Free the video memory of surfaces that no longer exist (after a screen freed them). */
static void
surface_gc(void) {
    for (int i = 0; i < BSURF_MAX; i++) {
        if (surf_texes[i].tex.valid && !bsurf_find(surf_texes[i].id)) {
            pvr_mem_free(surf_texes[i].tex.ptr);
            memset(&surf_texes[i], 0, sizeof(surf_texes[i]));
        }
    }
}

static int
draw_surface(const bvm_obj* obj, float x, float y, float invw) {
    bsurf* sf = bsurf_find(obj->id);
    if (!sf) {
        return 0;
    }
    if (sf->hidden) {
        return 1;
    }
    rom_tex* t = surface_texture(sf);
    if (!t) {
        return 1;
    }
    float a = bscene_text_alpha(obj);
    if (a <= 0.0f) {
        return 1;
    }
    uint32_t argb = ((uint32_t)(a * 255.0f) << 24) | 0x00FFFFFFu;
    float x0 = x - (float)sf->w / 2.0f, y0 = y - (float)sf->h / 2.0f, z = invw + TEXT_Z_BIAS;
    ensure_header(t, NULL, 0);
    send_quad(x0, y0, (float)sf->w, (float)sf->h, z, 0.0f, 0.0f, 1.0f, 1.0f, argb);
    return 1;
}

static void
sink_text(void* user, const bvm_obj* obj, float x, float y, float invw) {
    (void)user;
    if (draw_surface(obj, x, y, invw)) {
        return;
    }
    const char* label = find_label(obj->id);
    if (label && obj->id >= BPAGE_TEXT_ID(0) && obj->id <= BPAGE_TEXT_ID(BPAGE_SLOTS - 1)) {
        /* Settings page, as the original: "label\tvalue", the label centred in its column and the value left aligned 10 px
         * inside the pill (x as in bios_page.h), both centred on the row's height. (The information under the box is written by
         * ui_bios.c.) */
        float y0 = y - (float)obj->text_h / 2.0f;
        char part[LABEL_CHARS + 1];
        const char* p = label;
        for (int line = 0; *p && line < 3; line++) {
            size_t n = strcspn(p, "\n");
            if (n > LABEL_CHARS) {
                n = LABEL_CHARS;
            }
            memcpy(part, p, n);
            part[n] = '\0';
            char* tab = strchr(part, '\t');
            float ly = y0 + (float)(line * GFX_LINE_H);
            if (tab) {
                *tab = '\0';
                gfx_text(tab + 1, BPAGE_VALUE_X_PX, ly, invw + TEXT_Z_BIAS, 0xFFFFFFFFu, 1);
            }
            gfx_text(part, BPAGE_LABEL_CX_PX - (float)gfx_text_width(part) / 2.0f, ly, invw + TEXT_Z_BIAS, 0xFFFFFFFFu, 1);
            p += n;
            if (*p == '\n') {
                p++;
            }
        }
    } else if (label && obj->id >= BLIST_TEXT_FIRST && obj->id <= BLIST_TEXT_LAST) {
        /* Game list row: one line, left aligned in the text surface; a long selected title scrolls */
        int slot = obj->id - BLIST_TEXT_FIRST;
        float lx = x - (float)obj->text_w / 2.0f, ly = y - (float)obj->text_h / 2.0f;
        if (slot >= 0 && slot < BLIST_MAX_SLOTS && row_window[slot] > 0 && gfx_text_width(label) > row_window[slot]) {
            text_windowed(label, lx, ly, invw + TEXT_Z_BIAS, 0xFFFFFFFFu, row_window[slot], row_offset[slot]);
        } else {
            gfx_text(label, lx, ly, invw + TEXT_Z_BIAS, 0xFFFFFFFFu, 1);
        }
    } else if (label) {
        /* The anchor of a text surface is its centre (checked against the BIOS layout:
         * the caption pills line up with it), so centre the string on it. */
        float w = (float)gfx_text_width(label);
        int header = obj->id == BMENU_ID_HEADER; /* dark text on the light header bar, as in the BIOS */
        gfx_text(label, x - w / 2.0f, y - (float)GFX_LINE_H / 2.0f, invw + TEXT_Z_BIAS, header ? 0xFF303030u : 0xFFFFFFFFu,
                 !header);
    }
}

/* A strip from the scene (the BIOS' strip handlers send these too): one header if the picture changes, then the vertices. */
static void
sink_strip(void* user, const bscene_vtx* v, int n, bscene_texref ref) {
    (void)user;
    note_logo(v, n, ref);
    int any = 0;
    for (int i = 0; i < n; i++) {
        any |= v[i].argb >> 24 ? 1 : 0;
    }
    if (!any) {
        return; /* fully transparent */
    }
    rom_tex* tex = NULL;
    if (ref.kind != BSCENE_TEX_NONE) {
        tex = get_rom_texture(ref);
        if (!tex) {
            return; /* texture missing from this ROM: draw nothing rather than garbage */
        }
    }
    tri_count += (unsigned)(n - 2);
    ensure_header_uv(tex, NULL, 0, ref.uv); /* also ends a pending strip of sink_triangle() */
    /* straight into the command buffer, as many as fit at a time */
    const uint32_t offset_mask = tex ? 0xFFFFFFFFu : 0u;
    while (n > 0) {
        cmd_reserve(1);
        int k = CMD_SLOTS - cmd_n;
        if (k > n) {
            k = n;
        }
        pvr_vertex_t* vert = &cmdbuf[cmd_n];
        for (int i = 0; i < k; i++, v++, vert++) {
            vert->flags = PVR_CMD_VERTEX;
            vert->x = aspect_bypass ? v->x : 320.0f + (v->x - 320.0f) * aspect_x;
            vert->y = v->y;
            vert->z = v->invw;
            vert->u = v->u;
            vert->v = v->v;
            vert->argb = v->argb;
            vert->oargb = v->oargb & offset_mask;
        }
        cmd_n += k;
        n -= k;
    }
    cmdbuf[cmd_n - 1].flags = PVR_CMD_VERTEX_EOL;
}

static const bscene_sink the_sink = {NULL, sink_triangle, sink_text, sink_strip};

/* Loads the textures a drawing uses, draws nothing (bmenu_prewarm before the first frame). */
static void
warm_triangle(void* user, const bscene_vtx v[3], bscene_texref ref) {
    (void)user;
    (void)v;
    if (ref.kind != BSCENE_TEX_NONE) {
        get_rom_texture(ref);
    }
}

static void
warm_strip(void* user, const bscene_vtx* v, int n, bscene_texref ref) {
    (void)n;
    warm_triangle(user, v, ref);
}

static const bscene_sink warm_sink = {NULL, warm_triangle, NULL, warm_strip};

const bscene_sink*
gfx_warm_sink(void) {
    return &warm_sink;
}

void
gfx_preload_art_index(void) {
    art_load_dats();
}

const bscene_sink*
gfx_sink(void) {
    return &the_sink;
}

unsigned
gfx_triangles(void) {
    return tri_count;
}

void
gfx_stats_take(gfx_stats* out) {
    *out = stats;
    memset(&stats, 0, sizeof(stats));
}

void
gfx_set_label(uint16_t obj_id, const char* text) {
    for (int i = 0; i < num_labels; i++) {
        if (labels[i].id == obj_id) {
            strncpy(labels[i].text, text, LABEL_CHARS);
            labels[i].text[LABEL_CHARS] = '\0';
            return;
        }
    }
    _Static_assert(MAX_LABELS >= 4 + 1 + BLIST_MAX_SLOTS + BPAGE_SLOTS, "every label the screens set at the same time");
    if (num_labels < MAX_LABELS) {
        labels[num_labels].id = obj_id;
        strncpy(labels[num_labels].text, text, LABEL_CHARS);
        labels[num_labels].text[LABEL_CHARS] = '\0';
        num_labels++;
    }
}

/* ---- Text ------------------------------------------------------------------------------- */


int
gfx_text_width(const char* str) {
    return btext_width(str);
}

static int
pow2_at_least(int v) {
    int p = 32;
    while (p < v) {
        p <<= 1;
    }
    return p;
}

static text_tex*
get_text_texture_impl(const char* str) {
    text_tex* victim = NULL;
    for (int i = 0; i < MAX_TEXT_ENTRIES; i++) {
        if (text_texes[i].ptr && !strcmp(text_texes[i].str, str)) {
            text_texes[i].last_frame = frame_no;
            return &text_texes[i];
        }
    }

    /* Reuse an empty slot, else the least recently used one. A slot only becomes
     * available two frames after its last use: the PVR may still be rendering the
     * previous frame with its texture. */
    for (int i = 0; i < MAX_TEXT_ENTRIES; i++) {
        text_tex* t = &text_texes[i];
        if (!t->ptr) {
            victim = t;
            break;
        }
        if (t->last_frame + 2 <= frame_no && (!victim || t->last_frame < victim->last_frame)) {
            victim = t;
        }
    }
    if (!victim) {
        return NULL;
    }

    int w = pow2_at_least((int)strlen(str) * GFX_CHAR_W);
    if (w > MAX_TEXT_W) {
        w = MAX_TEXT_W;
    }
    if (victim->ptr && victim->w != w) {
        pvr_mem_free(victim->ptr);
        victim->ptr = NULL;
    }
    if (!victim->ptr) {
        victim->ptr = pvr_mem_malloc((size_t)w * GFX_LINE_H * 2);
        if (!victim->ptr) {
            return NULL;
        }
    }
    victim->w = w;
    strncpy(victim->str, str, MAX_TEXT_CHARS);
    victim->str[MAX_TEXT_CHARS] = '\0';
    victim->used = btext_width(victim->str);
    victim->last_frame = frame_no;

    memset(text_canvas, 0, (size_t)w * GFX_LINE_H * 2);
    /* the BIOS way: glyphs drawn with double thickness and a built-in shade (no drop shadow needed) */
    btext_draw(text_canvas, w, GFX_LINE_H, 0, 0, BIOS_FONT_ROM, victim->str, 0xFFFF);
    pvr_txr_load(text_canvas, victim->ptr, (size_t)w * GFX_LINE_H * 2);
    stats.text_uploads++;
    return victim;
}

static text_tex*
get_text_texture(const char* str) {
    text_tex* _r;
    uint64_t _p0 = timer_us_gettime64();
    _r = get_text_texture_impl(str);
    gfx_prof_us[4] += (unsigned)(timer_us_gettime64() - _p0);
    return _r;
}

void
gfx_text_scaled(const char* str, float x, float y, float z, uint32_t argb, float scale) {
    if (!str || !str[0]) {
        return;
    }
    char clipped[MAX_TEXT_CHARS + 1];
    strncpy(clipped, str, MAX_TEXT_CHARS);
    clipped[MAX_TEXT_CHARS] = '\0';

    text_tex* t = get_text_texture(clipped);
    if (!t) {
        return;
    }
    ensure_header(NULL, t->ptr, t->w);

    /* only the part of the picture that holds text: less to blend, and nothing sticks out over its neighbours */
    int used = t->used + 3;
    if (used > t->w) used = t->w;
    send_quad(x, y, (float)used * scale, 26.0f * scale, z, 0.0f, 0.0f, (float)used / (float)t->w, 26.0f / (float)GFX_LINE_H, argb);
}

void
gfx_text(const char* str, float x, float y, float z, uint32_t argb, int shadow) {
    (void)shadow; /* the glyphs carry their own shade, see btext_blit() */
    gfx_text_scaled(str, x, y, z, argb, 1.0f);
}

/* A string shown through a window of `window` pixels, moved `offset` pixels to the left. */
static void
text_windowed(const char* str, float x, float y, float z, uint32_t argb, int window, int offset) {
    char clipped[MAX_TEXT_CHARS + 1];
    strncpy(clipped, str, MAX_TEXT_CHARS);
    clipped[MAX_TEXT_CHARS] = '\0';
    text_tex* t = get_text_texture(clipped);
    if (!t) {
        return;
    }
    int full = t->used;
    if (offset < 0) offset = 0;
    if (offset > full - window) offset = full > window ? full - window : 0;
    int win = window < full ? window : full;
    float u0 = (float)offset / (float)t->w, u1 = (float)(offset + win) / (float)t->w;
    ensure_header(NULL, t->ptr, t->w);
    send_quad(x, y, (float)win, (float)GFX_LINE_H, z, u0, 0.0f, u1, 1.0f, argb);
}

void
gfx_set_row_scroll(int slot, int window_px, int offset_px) {
    if (slot >= 0 && slot < BLIST_MAX_SLOTS) {
        row_window[slot] = window_px;
        row_offset[slot] = offset_px;
    }
}

void
gfx_rect(float x, float y, float w, float h, float z, uint32_t argb) {
    ensure_header(NULL, NULL, 0);
    send_quad(x, y, w, h, z, 0.0f, 0.0f, 0.0f, 0.0f, argb);
}

/* ---- Small pictures made at run time (file icons of the memory cards) ----------------------------- */

void
gfx_decal_override(int texlist, int dyn) {
    for (int i = 0; i < decal_n; i++) {
        if (decal_ovr[i].texlist == texlist) {
            if (decal_ovr[i].dyn != dyn) {
                decal_ovr[i].dyn = dyn;
                memo_ok = 0; /* the remembered picture of that texlist is no longer the one to show */
            }
            return;
        }
    }
    if (decal_n < 16) {
        decal_ovr[decal_n].texlist = texlist;
        decal_ovr[decal_n++].dyn = dyn;
        memo_ok = 0;
    }
}

int
gfx_dyn_create(const unsigned short* argb4444, int w, int h) {
    for (int i = 0; i < DYN_MAX; i++) {
        if (dyn_tex[i].valid) {
            continue;
        }
        upload_argb4444(argb4444, w, h, &dyn_tex[i]);
        return dyn_tex[i].valid ? i : -1;
    }
    return -1;
}

void
gfx_dyn_free(int id) {
    if (id >= 0 && id < DYN_MAX && dyn_tex[id].valid) {
        pvr_mem_free(dyn_tex[id].ptr);
        memset(&dyn_tex[id], 0, sizeof(dyn_tex[id]));
    }
}

void
gfx_dyn_free_all(void) {
    for (int i = 0; i < DYN_MAX; i++) {
        gfx_dyn_free(i);
    }
}

void
gfx_image(int id, float x, float y, float w, float h, float z) {
    if (id < 0 || id >= DYN_MAX || !dyn_tex[id].valid) {
        return;
    }
    picture_quad(&dyn_tex[id], x, y, w, h, z, 0xFFFFFFFFu);
}

int
gfx_dyn_create_pow2(const unsigned short* argb4444, int w, int h, float* u1, float* v1) {
    int pw = 8, ph = 8;
    while (pw < w) pw <<= 1;
    while (ph < h) ph <<= 1;
    unsigned short* buf = (unsigned short*)calloc((size_t)pw * (size_t)ph, 2);
    if (!buf) {
        return -1;
    }
    for (int y = 0; y < h; y++) {
        memcpy(&buf[y * pw], &argb4444[y * w], (size_t)w * 2u);
    }
    int id = gfx_dyn_create(buf, pw, ph);
    free(buf);
    *u1 = (float)w / (float)pw;
    *v1 = (float)h / (float)ph;
    return id;
}

void
gfx_image_quad(int id, const float x[4], const float y[4], float u1, float v1, float z) {
    if (id < 0 || id >= DYN_MAX || !dyn_tex[id].valid) {
        return;
    }
    ensure_header(&dyn_tex[id], NULL, 0);
    send_vertex(x[0], y[0], z, 0.0f, 0.0f, 0xFFFFFFFFu, 0);
    send_vertex(x[1], y[1], z, u1, 0.0f, 0xFFFFFFFFu, 0);
    send_vertex(x[2], y[2], z, 0.0f, v1, 0xFFFFFFFFu, 0);
    send_vertex(x[3], y[3], z, u1, v1, 0xFFFFFFFFu, 1);
}

void
gfx_image_tint(int id, float x, float y, float w, float h, float z, uint32_t argb) {
    if (id < 0 || id >= DYN_MAX || !dyn_tex[id].valid) {
        return;
    }
    picture_quad(&dyn_tex[id], x, y, w, h, z, argb);
}

int
gfx_dyn_valid(int id) {
    return id >= 0 && id < DYN_MAX && dyn_tex[id].valid;
}

/* Where the header logo is on the screen (measured on the preview: the 128x32 texture covers x 27..165, y 28..61). */
void
gfx_logo_badge(void) {
    const float sx = 139.0f / 128.0f, sy = 34.0f / 32.0f;
    if (!logo_seen) {
        return; /* the logo is not drawn this frame (the bar is off the screen or not there): neither are its letters */
    }
    const float ox = logo_ox, oy = logo_oy; /* follows the header bar while it slides */
    /* "Now!" after the logo, or "CNET" after the D of the logo (cropped to swirl + D then): pictures with the edges baked in */
    build_logo_variants();
    int blue = (logo_swirl_rgb & 0xFF) > ((logo_swirl_rgb >> 16) & 0xFF);
    rom_tex* text = logo_mode == 1 ? (logo_dark ? &logo_now_dark_tex[blue] : &logo_now_tex[blue])
                                   : logo_mode == 2 ? (logo_dark ? &logo_cnet_dark_tex : &logo_dcnet_text_tex) : NULL;
    if (!text || !text->valid) {
        return;
    }
    /* drawn with the colour of the logo's own polygon, so the letters are shaded and fade exactly as the logo is */
    const uint32_t tint = logo_argb;
    float x = logo_mode == 1 ? ox + 127.0f * sx - 2.0f : ox + (float)LOGO_D_END * sx, y = oy;
    float w = (float)text->w * sx, h = (float)LOGO_H * sy;
    picture_quad(text, x, y, w, h, 0.5f, tint);
}

void
gfx_art_box(const char* product, float x, float y, float w, float h, float z) {
    art_entry* e = art_get(product, 1);
    if (e && e->tex.valid) {
        picture_quad(&e->tex, x, y, w, h, z, 0xFFFFFFFFu);
    } else {
        gfx_rect(x, y, w, h, z, 0x40FFFFFFu); /* no picture (yet) */
    }
}

/* Flat translucent rectangle with rounded corners of radius r: three bands and four corner fans
 * that do not overlap, so the translucent colour is even. */
void
gfx_rrect(float x, float y, float w, float h, float r, float z, uint32_t argb) {
    enum { SEGMENTS = 4 };
    static const float cs[SEGMENTS + 1] = {1.0f, 0.9239f, 0.7071f, 0.3827f, 0.0f}; /* cos of 0, 22.5, 45, 67.5, 90 degrees */
    static const float sn[SEGMENTS + 1] = {0.0f, 0.3827f, 0.7071f, 0.9239f, 1.0f};
    if (r * 2.0f > w) r = w / 2.0f;
    if (r * 2.0f > h) r = h / 2.0f;
    ensure_header(NULL, NULL, 0);
    /* top, middle and bottom bands */
    float xs[3] = {x + r, x, x + r};
    float ys[3] = {y, y + r, y + h - r};
    float ws[3] = {w - 2.0f * r, w, w - 2.0f * r};
    float hs[3] = {r, h - 2.0f * r, r};
    for (int i = 0; i < 3; i++) {
        send_quad(xs[i], ys[i], ws[i], hs[i], z, 0.0f, 0.0f, 0.0f, 0.0f, argb);
    }
    /* corners: centre of each arc and the direction of its quarter */
    float cx[4] = {x + w - r, x + r, x + r, x + w - r};
    float cy[4] = {y + r, y + r, y + h - r, y + h - r};
    float dx[4] = {1.0f, -1.0f, -1.0f, 1.0f};
    float dy[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    for (int c = 0; c < 4; c++) {
        for (int i = 0; i < SEGMENTS; i++) {
            send_vertex(cx[c], cy[c], z, 0, 0, argb, 0);
            send_vertex(cx[c] + dx[c] * r * cs[i], cy[c] + dy[c] * r * sn[i], z, 0, 0, argb, 0);
            send_vertex(cx[c] + dx[c] * r * cs[i + 1], cy[c] + dy[c] * r * sn[i + 1], z, 0, 0, argb, 1);
        }
    }
}

/* The app's standard panel: flat, the colour of the row bars (BSCENE_PANE_ARGB), and corners shaped exactly as the
 * right end of a row bar (model 33 as the Settings screen shows it, traced pixel by pixel): about 9 px, a little
 * squarer than a circle. */
static const float pane_corner[][2] = { /* inset from the side, distance from the top/bottom edge */
    {9.5f, 0.0f}, {6.5f, 1.0f}, {4.5f, 2.0f}, {3.5f, 3.0f}, {2.5f, 4.0f}, {1.5f, 5.0f}, {0.8f, 6.5f}, {0.3f, 8.0f}, {0.0f, 9.0f}};
#define PANE_CORNER_N ((int)(sizeof(pane_corner) / sizeof(pane_corner[0])))
#define PANE_R 9.5f /* width of the corner */
#define PANE_RV 9.0f /* height of the corner */

void
gfx_pane(float x, float y, float w, float h, float z, uint32_t argb) {
    ensure_header(NULL, NULL, 0);
    /* top and bottom bands between the corners, and the middle band */
    float xs[3] = {x + PANE_R, x, x + PANE_R};
    float ys[3] = {y, y + PANE_RV, y + h - PANE_RV};
    float ws[3] = {w - 2.0f * PANE_R, w, w - 2.0f * PANE_R};
    float hs[3] = {PANE_RV, h - 2.0f * PANE_RV, PANE_RV};
    for (int i = 0; i < 3; i++) {
        send_quad(xs[i], ys[i], ws[i], hs[i], z, 0.0f, 0.0f, 0.0f, 0.0f, argb);
    }
    /* each corner: a fan from its inner point (x side + PANE_R, y side + PANE_RV) */
    for (int c = 0; c < 4; c++) {
        int right = c == 0 || c == 3, bottom = c >= 2;
        float ex = right ? x + w : x, ey = bottom ? y + h : y; /* the box corner */
        float sx = right ? -1.0f : 1.0f, sy = bottom ? -1.0f : 1.0f;
        float ix = ex + sx * PANE_R, iy = ey + sy * PANE_RV;
        for (int i = 0; i + 1 < PANE_CORNER_N; i++) {
            send_vertex(ix, iy, z, 0, 0, argb, 0);
            send_vertex(ex + sx * pane_corner[i][0], ey + sy * pane_corner[i][1], z, 0, 0, argb, 0);
            send_vertex(ex + sx * pane_corner[i + 1][0], ey + sy * pane_corner[i + 1][1], z, 0, 0, argb, 1);
        }
    }
}

/* ---- Frames ------------------------------------------------------------------------------ */

void
gfx_begin_frame(uint32_t top, uint32_t bottom) {
    frame_no++;
    tri_count = 0;
    logo_seen = 0;
    {
        uint64_t t0 = timer_us_gettime64();
        pvr_wait_ready();
        stats.wait_us += (unsigned)(timer_us_gettime64() - t0);
        pvr_stats_t ps;
        if (pvr_get_stats(&ps) == 0) {
            stats.render_us += (unsigned)(ps.rnd_last_time / 1000u);
        }
    }
    art_poll();
    surface_gc();
    memo_ok = 0;
    /* The tile matrix of the buffer the TA fills next carries the sort mode (see set_tile_presort). */
#ifdef GFX_PVR_INTERNAL
    if (!set_tile_presort(!autosort)) {
        autosort = !GFX_PRESORT;
    }
#endif
    /* Where the gradient quad does not draw, show a mid blue instead of black. */
    pvr_set_bg_color(0.45f, 0.60f, 0.80f);
    pvr_scene_begin();

    pvr_poly_cxt_t cxt;
    frame_bytes[0] = frame_bytes[1] = 0;
    cur_list_op = 1;
    pvr_list_begin(PVR_LIST_OP_POLY);
    pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
    cxt.gen.culling = PVR_CULLING_NONE;
    submit_header(&cxt);
    aspect_bypass = 1;
    send_vertex(0.0f, 0.0f, BG_Z, 0, 0, top, 0);
    send_vertex(640.0f, 0.0f, BG_Z, 0, 0, top, 0);
    send_vertex(0.0f, 480.0f, BG_Z, 0, 0, bottom, 0);
    send_vertex(640.0f, 480.0f, BG_Z, 0, 0, bottom, 1);
    aspect_bypass = 0;
    cmd_flush();
    pvr_list_finish();

    cur_list_op = 0;
    pvr_list_begin(PVR_LIST_TR_POLY);
    hdr_valid = 0;
}

int
gfx_set_autosort(int on) {
#ifdef GFX_PVR_INTERNAL
    autosort = on ? 1 : 0;
#else
    (void)on; /* cannot switch safely: keep the mode pvr_init set */
#endif
    return autosort;
}

int
gfx_autosort(void) {
    return autosort;
}

void
gfx_end_frame(void) {
    strip_close();
    cmd_flush();
    pvr_list_finish();
    pvr_scene_finish();
}
