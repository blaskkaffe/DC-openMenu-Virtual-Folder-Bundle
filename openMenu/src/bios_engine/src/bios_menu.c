/*
 * bios_menu: the BIOS main menu, see bios_menu.h.
 * Object scripts and ids are those of main_menu_update in the original.
 */
#include "bios_menu.h"

#include <stdlib.h>
#include <string.h>

#define PRIO_ICON 0x2000
#define PRIO_HEADER 0x1000

/* The 2x2 cursor table of the original (0x8C038724): {up, down, left, right} per icon */
static const uint8_t cursor_table[BMENU_ICONS][4] = {
    {0, 2, 0, 1}, /* Play */
    {1, 3, 0, 1}, /* File */
    {0, 2, 2, 3}, /* Music */
    {1, 3, 2, 3}, /* Settings */
};

void
bmenu_init(bmenu* m, const bios_rom* rom, const bvm_host* host) {
    memset(m, 0, sizeof(*m));
    m->rom = rom;
    bvm_init(&m->vm, rom, host);
    bscene_init(&m->scene, rom);
    dcbg_init(&m->bg, 0, 0);
}

void
bmenu_free(bmenu* m) {
    bscene_free(&m->scene);
}

static void
apply_selection(bmenu* m) {
    for (int i = 0; i < BMENU_ICONS; i++) {
        bvm_obj* icon = bvm_find(&m->vm, BMENU_ID_ICON(i));
        bvm_obj* caption = bvm_find(&m->vm, BMENU_ID_CAPTION(i));
        int on = (i == m->selected);
        if (icon) {
            icon->var[0] = on;
        }
        if (caption) {
            caption->var[0] = on;
        }
    }
}

void
bmenu_show_main(bmenu* m, int selected) {
    bvm_kill_all(&m->vm);
    m->selected = (selected >= 0 && selected < BMENU_ICONS) ? selected : 0;
    for (int i = 0; i < BMENU_ICONS; i++) {
        bvm_create(&m->vm, 8 + i, (uint16_t)BMENU_ID_ICON(i), PRIO_ICON);
    }
    bvm_create(&m->vm, 0x48, 0x204, PRIO_ICON);
    bvm_create(&m->vm, 1, BMENU_ID_HEADER, PRIO_HEADER);
    for (int i = 0; i < BMENU_ICONS; i++) {
        bvm_create(&m->vm, 0x53 + i, (uint16_t)BMENU_ID_CAPTION(i), PRIO_ICON);
    }
    apply_selection(m);
}

int
bmenu_select(bmenu* m, int i) {
    if (i < 0 || i >= BMENU_ICONS || i == m->selected) {
        return 0;
    }
    m->selected = i;
    apply_selection(m);
    return 1;
}

int
bmenu_move(bmenu* m, bmenu_dir dir) {
    int next = cursor_table[m->selected][dir];
    if (next == m->selected) {
        return 0;
    }
    m->selected = next;
    apply_selection(m);
    return 1;
}

void
bmenu_set_icon_model(bmenu* m, int icon, uint16_t model, float scale, float dx, float dy, float dz) {
    if (icon >= 0 && icon < BMENU_ICONS) {
        m->icon_model[icon].model = model;
        m->icon_model[icon].scale = scale;
        m->icon_model[icon].dx = dx;
        m->icon_model[icon].dy = dy;
        m->icon_model[icon].dz = dz;
    }
}

/* The icons with another model: both layers of the icon take it, without the script's motion; it turns about its vertical axis while selected. */
static void
apply_icon_models(bmenu* m) {
    for (int i = 0; i < BMENU_ICONS; i++) {
        if (!m->icon_model[i].model) {
            continue;
        }
        const int age = m->selected == i ? ++m->icon_model[i].selected_frames : (m->icon_model[i].selected_frames = 0);
        const int32_t turn = (int32_t)((age % 180) * (65536.0f / 180.0f)); /* once in three seconds */
        for (int layer = 0; layer < 2; layer++) {
            bvm_obj* o = bvm_find(&m->vm, (uint16_t)(BMENU_ID_ICON(i) + layer * 0x100));
            if (!o) {
                continue;
            }
            o->model = o->texlist = m->icon_model[i].model;
            o->motion = 0;
            o->flags &= ~(uint32_t)BVM_F_MOTION;
            o->model_scale = m->icon_model[i].scale;
            o->model_off[0] = m->icon_model[i].dx; /* the model moves, its caption does not */
            o->model_off[1] = m->icon_model[i].dy;
            o->model_off[2] = -m->icon_model[i].dz; /* more negative = farther */
            o->rot_tw[1].step = 0;
            o->rot_tw[1].cur = o->rot[1] = age ? turn : 0;
        }
    }
}

/* The caption pills (object 0x204, model 5) lie only 0.6 to 4 units behind the icons, and the PVR's sorting of translucent
 * polygons does not always keep the icon in front (the controller, VMU and clock were seen behind their pills on a console).
 * Their depth is lowered by a few percent, not moved: the pills keep their place and size on the screen.
 * BMENU_PILL_DEPTH 1.0f gives the BIOS's own depth back. */
#define BMENU_PILL_DEPTH 0.95f
static void
apply_pill_depth(bmenu* m) {
    bvm_obj* pills = bvm_find(&m->vm, 0x204);
    if (pills) {
        pills->depth_mul = BMENU_PILL_DEPTH;
    }
}

void
bmenu_prewarm(bmenu* m, const bscene_sink* sink, int frames) {
    bvm* vm = (bvm*)malloc(sizeof(bvm)); /* a copy of the scripts' state (about 77 KB), for this only */
    if (!vm) {
        return;
    }
    memcpy(vm, &m->vm, sizeof(bvm));
    const unsigned parts = m->scene.parts;
    m->scene.parts = BSCENE_PART_MODEL;
    for (int f = 0; f < frames; f++) {
        bvm_update(vm);
        for (int i = 0; i < BMENU_ICONS; i++) { /* the icons drawn with another model (the globe) */
            for (int layer = 0; m->icon_model[i].model && layer < 2; layer++) {
                bvm_obj* o = bvm_find(vm, (uint16_t)(BMENU_ID_ICON(i) + layer * 0x100));
                if (o) {
                    o->model = o->texlist = m->icon_model[i].model;
                    o->flags &= ~(uint32_t)BVM_F_MOTION;
                    o->model_scale = m->icon_model[i].scale;
                }
            }
        }
        if (f == 0 || f == frames - 1 || f % 8 == 0) { /* the objects appear over the first frames, the motions run on */
            bscene_draw_objects(&m->scene, vm, sink);
        }
    }
    bscene_draw_background(&m->bg, sink);
    m->scene.parts = parts;
    free(vm);
}

void
bmenu_update(bmenu* m) {
    bvm_update(&m->vm);
    apply_icon_models(m);
    apply_pill_depth(m);
    dcbg_step(&m->bg);
    m->frames++;
}

/* Each icon is drawn twice (0x200 + i and 0x300 + i): the same model, motion frame, rotation and colour, the second
 * one 0.117 units higher and 0.039 farther. The lighting depends on the normals only, so the second copy is the
 * first one's triangles projected again from the shifted position: no second transform and lighting. */
#define TWIN_VMAX 4096 /* vertices of the kept strips */
#define TWIN_RMAX 1024 /* kept strips (a single triangle is a strip of 3) */
typedef struct {
    int first, n;
    bscene_texref tex;
} twin_run;
static bscene_vtx twin_v[TWIN_VMAX];
static twin_run twin_runs[TWIN_RMAX];
static int twin_vn, twin_n, twin_overflow;
static const bscene_sink* twin_out;

/* a strip (or a triangle) to a sink: as a strip if it takes them */
static void
twin_emit(const bscene_sink* sink, const bscene_vtx* v, int n, bscene_texref tex) {
    if (sink->strip) {
        sink->strip(sink->user, v, n, tex);
    } else {
        for (int k = 0; k + 2 < n; k++) {
            sink->triangle(sink->user, &v[k], tex);
        }
    }
}

static void
twin_keep(const bscene_vtx* v, int n, bscene_texref tex) {
    if (twin_n < TWIN_RMAX && twin_vn + n <= TWIN_VMAX) {
        memcpy(&twin_v[twin_vn], v, sizeof(bscene_vtx) * (size_t)n);
        twin_runs[twin_n].first = twin_vn;
        twin_runs[twin_n].n = n;
        twin_runs[twin_n].tex = tex;
        twin_vn += n;
        twin_n++;
    } else {
        twin_overflow = 1;
    }
}

static void
twin_triangle(void* user, const bscene_vtx v[3], bscene_texref tex) {
    (void)user;
    twin_keep(v, 3, tex);
    twin_out->triangle(twin_out->user, v, tex);
}

static void
twin_strip(void* user, const bscene_vtx* v, int n, bscene_texref tex) {
    (void)user;
    twin_keep(v, n, tex);
    twin_emit(twin_out, v, n, tex);
}

static int
twins(const bvm_obj* a, const bvm_obj* b) {
    if (!a->active || !b->active || ((a->flags ^ b->flags) & ~(uint32_t)(BVM_F_ATTACHED | BVM_F_TEXT)) || a->model != b->model ||
        a->texlist != b->texlist || (a->flags & (BVM_F_HIDE | BVM_F_PANEL)) || !(a->flags & BVM_F_MODEL)) {
        return 0;
    }
    if ((a->flags & BVM_F_MOTION) && (a->motion != b->motion || a->motion_tw.cur != b->motion_tw.cur)) {
        return 0;
    }
    if ((a->flags & BVM_F_COLOUR) && memcmp(a->color, b->color, sizeof(a->color))) {
        return 0;
    }
    for (int k = 0; k < 3; k++) {
        if (a->rot[k] != b->rot[k] || a->scale_tw[k].cur != b->scale_tw[k].cur) {
            return 0;
        }
    }
    return 1;
}

/* the captured triangles of `a`, seen from where `b` is */
static void
twin_replay(const bvm_obj* a, const bvm_obj* b, const bscene_sink* sink) {
    const float dx = b->pos[0] - a->pos[0], dy = b->pos[1] - a->pos[1], dz = b->pos[2] - a->pos[2];
    const float half_w = BSCENE_SCREEN_W / 2, half_h = BSCENE_SCREEN_H / 2;
    static bscene_vtx v[TWIN_VMAX];
    static uint8_t ok[TWIN_VMAX];
    /* every kept vertex once: back to view space (z = -1 / invw), shift, project again */
    for (int i = 0; i < twin_vn; i++) {
        const bscene_vtx* s = &twin_v[i];
        v[i] = *s;
        float w = 1.0f / s->invw;
        float x = (s->x - half_w) * w + BSCENE_FOCAL * dx, y = (half_h - s->y) * w + BSCENE_FOCAL * dy;
        float nw = w - dz;
        ok[i] = nw >= 1.0f;
        if (ok[i]) {
            float iw = 1.0f / nw;
            v[i].x = half_w + x * iw;
            v[i].y = half_h - y * iw;
            v[i].invw = iw;
        }
    }
    for (int r = 0; r < twin_n; r++) {
        const twin_run* run = &twin_runs[r];
        int all = 1;
        for (int k = 0; k < run->n; k++) {
            all &= ok[run->first + k];
        }
        if (all) {
            twin_emit(sink, &v[run->first], run->n, run->tex);
            continue;
        }
        for (int k = 0; k + 2 < run->n; k++) { /* a triangle with a vertex behind the eye is left out, the rest of the strip stays */
            const int f = run->first + k;
            if (ok[f] && ok[f + 1] && ok[f + 2]) {
                sink->triangle(sink->user, &v[f], run->tex);
            }
        }
    }
}

void
bmenu_draw_objects(bmenu* m, const bscene_sink* sink) {
    static const unsigned passes[2] = {BSCENE_PART_MODEL, BSCENE_PART_TEXT};
    for (int pass = 0; pass < 2; pass++) {
        m->scene.parts = passes[pass];
        /* the BIOS has the PVR sort its translucent polygons per pixel (autosort); without it the models go
         * through the CPU sorter */
        const int cpu_sort = pass == 0 && !m->hw_autosort;
        const bscene_sink* to = cpu_sort ? bscene_sort_begin(sink) : sink;
        uint8_t done[BVM_MAX_OBJECTS] = {0};
        for (int i = 0; i < m->vm.count; i++) {
            const int oi = m->vm.order[i];
            const bvm_obj* o = &m->vm.objs[oi];
            if (done[oi]) {
                continue;
            }
            /* an icon of the first layer: draw it, then its twin of the second layer from the same triangles */
            const bvm_obj* twin = NULL;
            int twin_index = -1;
            /* only for an icon that moves: one at rest comes out of the scene's triangle cache already */
            if (pass == 0 && (o->flags & BVM_F_MOTION) && o->id >= BMENU_ID_ICON(0) && o->id < BMENU_ID_ICON(BMENU_ICONS)) {
                for (int k = 0; k < m->vm.count; k++) {
                    const bvm_obj* c = &m->vm.objs[m->vm.order[k]];
                    if (c->id == o->id + 0x100 && twins(o, c)) {
                        twin = c;
                        twin_index = m->vm.order[k];
                        break;
                    }
                }
            }
            if (!twin) {
                bscene_draw_object(&m->scene, o, to);
                continue;
            }
            bscene_sink cap = {NULL, twin_triangle, to->text, twin_strip};
            twin_out = to;
            twin_n = 0;
            twin_vn = 0;
            twin_overflow = 0;
            bscene_draw_object(&m->scene, o, &cap);
            if (twin_overflow) {
                bscene_draw_object(&m->scene, twin, to);
            } else {
                twin_replay(o, twin, to);
            }
            done[twin_index] = 1;
        }
        if (cpu_sort) {
            bscene_sort_end();
        }
    }
    m->scene.parts = BSCENE_PART_ALL;
}

void
bmenu_draw(bmenu* m, const bscene_sink* sink) {
    bscene_draw_background(&m->bg, sink);
    bmenu_draw_objects(m, sink);
}

void
bmenu_back_style(bmenu* m, int selected, int anim, int pal) {
    const uint32_t arrow = selected ? (pal ? 0xD02020F0u : 0xD0F02000u) : 0u;
    const uint32_t frame = selected && ((anim / 16) & 1) == 0 ? 0xFFFFFF00u : 0xC0404040u;
    m->scene.ovr[0].model = 0, m->scene.ovr[0].node = 1, m->scene.ovr[0].poly = 0, m->scene.ovr[0].argb = arrow;
    m->scene.ovr[0].lit = 0;
    m->scene.ovr[1].model = 0, m->scene.ovr[1].node = 2, m->scene.ovr[1].poly = 0, m->scene.ovr[1].argb = frame;
    m->scene.ovr[1].lit = 0;
    m->scene.ovr_n = 2;
}

int
bmenu_back_hit(float x, float y, float px, float py) {
    const float px_per_unit = 11.32f; /* 4000 / 353.5: pixels per world unit at the BACK marker's depth */
    float cx = 320.0f + x * px_per_unit, cy = 240.0f - y * px_per_unit;
    return px >= cx - 34.0f && px <= cx + 34.0f && py >= cy - 34.0f && py <= cy + 34.0f;
}

int
bmenu_pill_colours(bmenu* m, int icon, uint32_t* body, uint32_t* border) {
    static const int node[BMENU_ICONS] = {1, 3, 2, 4}; /* the corner of model 5 each caption pill takes (2x2 as the icons) */
    static const uint32_t accent[BMENU_ICONS] = {BMENU_ACCENT_PLAY, BMENU_ACCENT_FILE, BMENU_ACCENT_MUSIC, BMENU_ACCENT_SETTINGS};
    nj_node* n = icon >= 0 && icon < BMENU_ICONS ? bscene_node(&m->scene, 5, node[icon]) : NULL;
    if (n && n->mesh && n->mesh->npolys >= 2 && n->mesh->polys[0].has_diffuse && n->mesh->polys[1].has_diffuse) {
        *body = n->mesh->polys[0].diffuse;
        *border = n->mesh->polys[1].diffuse;
        return 1;
    }
    *body = (accent[icon & 3] & 0x00FFFFFFu) | 0xB2000000u;
    *border = (accent[icon & 3] & 0x00FFFFFFu) | 0xCC000000u;
    return 0;
}
