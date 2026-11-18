/*
 * scene_dump: prints every triangle bios_scene emits for a fixed set of drawings that need no BIOS ROM (the
 * background meshes, the built-in models of bios_models.h, a grid mesh, text anchors, the triangle cache and the
 * sorter). For refactoring the scene code: build it before and after a change and diff the two outputs. They must be
 * identical, to the last bit of every float.
 *
 *   cd openMenu/src
 *   gcc -O2 -o scene_dump bios_engine/tools/scene_dump.c (all of bios_engine/src and bios_rom/src) \
 *       -Ibios_engine/include -Ibios_rom/include -lm
 *   ./scene_dump > after.txt        (and the same from the commit before the change: before.txt)
 *   cmp before.txt after.txt
 *
 * Strips are printed as their triangles, and the three corners of a triangle in a fixed order: neither the winding
 * (culling is done before the sink) nor the cut into strips changes what the PVR draws. Triangles whose three corners
 * are fully transparent are left out (gfx.c does not send them). The last lines count what a
 * frame costs the backend: triangles, and the vertices the TA gets (gfx.c joins triangles that share an edge with the
 * one before into a strip; a strip from the scene costs its vertices).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bios_scene.h"

static int use_strip_sink;
static unsigned long n_tris, n_ta_vertices, n_calls, n_text;
static uint32_t fnv = 2166136261u;

static void
mix(const void* p, size_t n) {
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; i++) {
        fnv = (fnv ^ b[i]) * 16777619u;
    }
}

static int
vtx_cmp(const void* a, const void* b) {
    return memcmp(a, b, sizeof(bscene_vtx));
}

/* gfx.c sink_triangle: a triangle that shares the last two vertices sent goes on the same strip */
static bscene_vtx l1, l2;
static bscene_texref ltex;
static int pending;

static int
same(const bscene_vtx* a, const bscene_vtx* b) {
    return !memcmp(a, b, sizeof(*a));
}

static void
print_tri(const bscene_vtx v[3], bscene_texref tex) {
    bscene_vtx s[3];
    if (!(v[0].argb >> 24) && !(v[1].argb >> 24) && !(v[2].argb >> 24)) {
        return; /* fully transparent: the backend drops it, so the scene may as well not send it */
    }
    /* copy field by field: the struct has no padding, but make sure nothing uninitialised is compared */
    memset(s, 0, sizeof(s));
    for (int i = 0; i < 3; i++) {
        s[i].x = v[i].x;
        s[i].y = v[i].y;
        s[i].invw = v[i].invw;
        s[i].u = v[i].u;
        s[i].v = v[i].v;
        s[i].argb = v[i].argb;
        s[i].oargb = v[i].oargb;
    }
    qsort(s, 3, sizeof(s[0]), vtx_cmp);
    printf("T %d %d %d %d", tex.kind, tex.a, tex.b, tex.uv);
    for (int i = 0; i < 3; i++) {
        uint32_t w[5];
        memcpy(w, &s[i].x, sizeof(w));
        printf(" %08x %08x %08x %08x %08x %08x %08x", w[0], w[1], w[2], w[3], w[4], s[i].argb, s[i].oargb);
    }
    printf("\n");
    mix(s, sizeof(s));
    mix(&tex, sizeof(tex));
    n_tris++;
}

static void
sink_tri(void* user, const bscene_vtx v[3], bscene_texref tex) {
    (void)user;
    n_calls++;
    print_tri(v, tex);
    if (!(v[0].argb >> 24) && !(v[1].argb >> 24) && !(v[2].argb >> 24)) {
        return; /* gfx.c drops it */
    }
    if (pending && !memcmp(&tex, &ltex, sizeof(tex))) {
        int a = -1, b = -1;
        for (int i = 0; i < 3; i++) {
            if (a < 0 && same(&v[i], &l1)) {
                a = i;
            } else if (b < 0 && same(&v[i], &l2)) {
                b = i;
            }
        }
        if (a >= 0 && b >= 0) {
            n_ta_vertices++;
            l1 = l2;
            l2 = v[3 - a - b];
            return;
        }
    }
    n_ta_vertices += 3;
    l1 = v[1];
    l2 = v[2];
    ltex = tex;
    pending = 1;
}

static void
sink_strip(void* user, const bscene_vtx* v, int n, bscene_texref tex) {
    (void)user;
    n_calls++;
    int any = 0;
    for (int i = 0; i < n; i++) {
        any |= (v[i].argb >> 24) != 0;
    }
    for (int k = 0; k + 2 < n; k++) {
        print_tri(&v[k], tex);
    }
    if (any) {
        n_ta_vertices += (unsigned long)n;
    }
    pending = 0;
}

static void
sink_text(void* user, const bvm_obj* obj, float x, float y, float invw) {
    (void)user;
    uint32_t w[3];
    float f[3] = {x, y, invw};
    memcpy(w, f, sizeof(w));
    printf("X %u %08x %08x %08x %.6f\n", (unsigned)obj->id, w[0], w[1], w[2], (double)bscene_text_alpha(obj));
    mix(w, sizeof(w));
    n_text++;
}

static const bscene_sink*
the_sink(void) {
    static bscene_sink s;
    s.user = NULL;
    s.triangle = sink_tri;
    s.text = sink_text;
    s.strip = use_strip_sink ? sink_strip : NULL;
    return &s;
}

static void
section(const char* name) {
    printf("# %s\n", name);
    pending = 0;
}

static bvm_obj
object(int model, float x, float y, float z, int rx, int ry, int rz, float scale) {
    bvm_obj o;
    memset(&o, 0, sizeof(o));
    o.active = 1;
    o.id = (uint16_t)(0x400 + model);
    o.flags = BVM_F_MODEL;
    o.model = (uint16_t)model;
    o.texlist = (uint16_t)model;
    o.pos[0] = x;
    o.pos[1] = y;
    o.pos[2] = z;
    o.rot[0] = rx;
    o.rot[1] = ry;
    o.rot[2] = rz;
    o.scale_tw[0].cur = o.scale_tw[1].cur = o.scale_tw[2].cur = scale;
    return o;
}

static void
draw_set(bscene* s, int frame, const bscene_sink* sink) {
    static const unsigned passes[3] = {BSCENE_PART_MODEL, BSCENE_PART_TEXT, BSCENE_PART_ALL};
    for (int pass = 0; pass < 3; pass++) {
        s->parts = passes[pass];
        for (int model = BMODEL_BASE; model < BMODEL_END; model++) {
            /* one at rest (kept by the cache from its second frame), one turning, one coloured and fading */
            bvm_obj rest = object(model, -8.0f + 5.0f * (float)(model - BMODEL_BASE), 4.0f, -300.0f, 0x800, 0x1800, 0, 1.0f);
            bvm_obj turn = object(model, -8.0f + 5.0f * (float)(model - BMODEL_BASE), -4.0f, -350.0f, frame * 0x180, frame * 0x2C0,
                                  frame * 0x90, 1.3f);
            bvm_obj tint = rest;
            tint.pos[1] = 0.0f;
            tint.flags |= BVM_F_COLOUR | BVM_F_FLAG17 | BVM_F_TEXT;
            tint.color[0] = -0.25f;
            tint.color[1] = 0.1f;
            tint.color[2] = -0.05f;
            tint.color[3] = 0.2f;
            tint.text_w = 120;
            tint.text_h = 26;
            tint.text_off[0] = 1.5f;
            tint.text_off[1] = -2.0f;
            bvm_obj moved = turn; /* a model drawn away from its caption, resized */
            moved.flags |= BVM_F_TEXT;
            moved.text_w = 64;
            moved.text_h = 26;
            moved.model_off[0] = 2.0f;
            moved.model_off[2] = -40.0f;
            moved.model_scale = 0.7f;
            bvm_obj hidden = rest;
            hidden.flags |= BVM_F_HIDE;
            bvm_obj text_only = rest;
            text_only.flags = BVM_F_TEXT;
            text_only.text_w = 200;
            text_only.text_h = 52;
            bscene_draw_object(s, &rest, sink);
            bscene_draw_object(s, &turn, sink);
            bscene_draw_object(s, &tint, sink);
            bscene_draw_object(s, &moved, sink);
            bscene_draw_object(s, &hidden, sink);
            bscene_draw_object(s, &text_only, sink);
        }
        /* the copy box's progress bar: a grid mesh with no model */
        bvm_obj grid = object(0, 0.0f, -6.0f, -300.0f, 0, 0, 0, 1.0f);
        grid.flags = 0;
        grid.mesh_cols = 12;
        grid.mesh_rows = 4;
        grid.mesh_cw = 20;
        grid.mesh_ch = 8;
        grid.mesh_mode = 5;
        grid.mesh_tex = 7;
        grid.var[0] = frame * 97;
        bscene_draw_object(s, &grid, sink);
    }
    s->parts = BSCENE_PART_ALL;
    /* scene switches that go into the cache key */
    s->fullbright = 1;
    bvm_obj bright = object(BMODEL_CASE_PAL, 6.0f, 6.0f, -320.0f, 0x400, 0x2000, 0, 1.0f);
    bscene_draw_object(s, &bright, sink);
    s->fullbright = 0;
    s->stretch_on = 1;
    s->stretch_a = -1.0f;
    s->stretch_b = 1.0f;
    s->stretch_f = 0.5f;
    s->stretch_yc = 1.0f;
    s->stretch_yd = 0.75f;
    s->stretch_yx = -3.0f;
    bscene_draw_object(s, &bright, sink);
    s->stretch_on = 0;
    s->stretch_yc = s->stretch_yd = s->stretch_yx = 0.0f;
    s->no_decals = 1;
    bscene_draw_object(s, &bright, sink);
    s->no_decals = 0;
    s->ovr[0].model = BMODEL_CASE_PAL;
    s->ovr[0].node = 0;
    s->ovr[0].poly = 1;
    s->ovr[0].argb = 0xC0FF8000u;
    s->ovr[0].lit = frame & 1;
    s->ovr_n = 1;
    bscene_draw_object(s, &bright, sink);
    s->ovr_n = 0;
    s->fade = 0.25f * (float)(frame % 4);
    bvm_obj fading = bright;
    fading.flags |= BVM_F_FLAG17 | BVM_F_TEXT;
    fading.text_w = 80;
    fading.text_h = 26;
    bscene_draw_object(s, &fading, sink);
    s->fade = 0.0f;
    const float rot[3] = {10.0f, (float)(frame * 7), 0.0f};
    bscene_draw_model(s, BMODEL_GLOBE, 500.0f, 120.0f, 1.5f, rot, sink);
}

int
main(int argc, char** argv) {
    int frames = argc > 1 ? atoi(argv[1]) : 40;
    static bscene scene;
    static dcbg_state bg;

    for (use_strip_sink = 0; use_strip_sink < 2; use_strip_sink++) {
        printf("## sink %s strips\n", use_strip_sink ? "with" : "without");
        /* the background: the intro, then steady state; plain and tinted (dark mode) */
        for (int tint = 0; tint < 2; tint++) {
            section(tint ? "background, tinted" : "background");
            bscene_set_background_tint(tint ? 0x6040C0u : 0);
            dcbg_init(&bg, 0, 0);
            unsigned long t0 = n_tris, v0 = n_ta_vertices, c0 = n_calls;
            for (int f = 0; f < frames; f++) {
                dcbg_step(&bg);
                printf("F %d\n", f);
                bscene_draw_background(&bg, the_sink());
                pending = 0;
            }
            fprintf(stderr, "%s%s: %lu triangles, %lu TA vertices, %lu sink calls a frame\n", tint ? "background tinted" : "background",
                    use_strip_sink ? " (strips)" : " (triangles)", (n_tris - t0) / (unsigned long)frames,
                    (n_ta_vertices - v0) / (unsigned long)frames, (n_calls - c0) / (unsigned long)frames);
        }
        bscene_set_background_tint(0);

        for (int cache = 0; cache < 2; cache++) {
            section(cache ? "models, cache on" : "models, cache off");
            bscene_init(&scene, NULL);
            bscene_cache_enable(cache);
            unsigned long t0 = n_tris, v0 = n_ta_vertices, c0 = n_calls;
            for (int f = 0; f < frames; f++) {
                printf("F %d\n", f);
                draw_set(&scene, f, the_sink());
                pending = 0;
            }
            fprintf(stderr, "models cache %s%s: %lu triangles, %lu TA vertices, %lu sink calls a frame\n", cache ? "on" : "off",
                    use_strip_sink ? " (strips)" : " (triangles)", (n_tris - t0) / (unsigned long)frames,
                    (n_ta_vertices - v0) / (unsigned long)frames, (n_calls - c0) / (unsigned long)frames);
            bscene_free(&scene);
        }
        bscene_cache_enable(1);

        /* the sorter: far to near, ties in the order they came */
        section("sorted");
        bscene_init(&scene, NULL);
        for (int f = 0; f < 4; f++) {
            printf("F %d\n", f);
            const bscene_sink* sorted = bscene_sort_begin(the_sink());
            draw_set(&scene, f, sorted);
            bscene_sort_end();
        }
        bscene_free(&scene);
    }
    printf("# %lu triangles, %lu texts, hash %08x\n", n_tris, n_text, (unsigned)fnv);
    fprintf(stderr, "hash %08x\n", (unsigned)fnv);
    return 0;
}
