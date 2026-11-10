/* Host stand-in for KOS's dc/pvr.h (gfx_host_test): the types gfx.c fills, with KOS 2.1.1's layouts for what goes to the
 * TA (pvr_vertex_t), and a pvr_poly_compile() that packs every field of the context so a changed header shows. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef void* pvr_ptr_t;
typedef uint32_t pvr_list_t;

#define PVR_LIST_OP_POLY 0
#define PVR_LIST_TR_POLY 2
#define PVR_BINSIZE_0  0
#define PVR_BINSIZE_32 32
#define PVR_CMD_VERTEX     0xE0000000u
#define PVR_CMD_VERTEX_EOL 0xF0000000u
#define PVR_TXRFMT_ARGB1555    (0 << 27)
#define PVR_TXRFMT_RGB565      (1 << 27)
#define PVR_TXRFMT_ARGB4444    (2 << 27)
#define PVR_TXRFMT_VQ_ENABLE   (1 << 30)
#define PVR_TXRFMT_TWIDDLED    (0 << 26)
#define PVR_TXRFMT_NONTWIDDLED (1 << 26)
#define PVR_FILTER_NONE     0
#define PVR_FILTER_BILINEAR 2
#define PVR_CULLING_NONE 0
#define PVR_CULLING_CCW  3
#define PVR_SPECULAR_DISABLE 0
#define PVR_SPECULAR_ENABLE  1
#define PVR_UVFLIP_NONE 0
#define PVR_UVFLIP_V    1
#define PVR_UVFLIP_U    2
#define PVR_UVCLAMP_NONE 0
#define PVR_UVCLAMP_V    1
#define PVR_UVCLAMP_U    2
#define PVR_BLEND_ZERO        0
#define PVR_BLEND_ONE         1
#define PVR_BLEND_SRCALPHA    4
#define PVR_BLEND_INVSRCALPHA 5
#define PVR_DEPTHWRITE_ENABLE  0
#define PVR_DEPTHWRITE_DISABLE 1
#define PVR_RAM_BASE 0

typedef struct {
    uint32_t flags;
    float x, y, z, u, v;
    uint32_t argb, oargb;
} pvr_vertex_t;

typedef struct {
    uint32_t w[8];
} pvr_poly_hdr_t;

typedef struct {
    int list_type;
    struct {
        int alpha, shading, fog_type, culling, color_clamp, clip_mode, modifier_mode, specular, alpha2, fog_type2, color_clamp2;
    } gen;
    struct {
        int src, dst, src_enable, dst_enable, src2, dst2, src_enable2, dst_enable2;
    } blend;
    struct {
        int color, uv, modifier;
    } fmt;
    struct {
        int comparison, write;
    } depth;
    struct {
        int enable, filter, mipmap, mipmap_bias, uv_flip, uv_clamp, alpha, env, width, height, format;
        pvr_ptr_t base;
    } txr;
} pvr_poly_cxt_t;

typedef struct {
    int opb_sizes[5];
    int vertex_buf_size;
    int dma_enabled;
    int fsaa_enabled;
    int autosort_disabled;
    int opb_overflow_count;
} pvr_init_params_t;

typedef struct {
    uint64_t frame_last_time, reg_last_time, rnd_last_time, buf_last_time;
    size_t frame_count, vbl_count, vtx_buffer_used, vtx_buffer_used_max;
    float frame_rate;
    uint32_t enabled_list_mask;
} pvr_stats_t;

int pvr_init(pvr_init_params_t* params);
int pvr_shutdown(void);
void* pvr_set_vertbuf(pvr_list_t list, void* buffer, size_t len);
pvr_ptr_t pvr_mem_malloc(size_t size);
void pvr_mem_free(pvr_ptr_t chunk);
void pvr_txr_load(const void* src, pvr_ptr_t dst, size_t count);
void pvr_poly_cxt_col(pvr_poly_cxt_t* dst, pvr_list_t list);
void pvr_poly_cxt_txr(pvr_poly_cxt_t* dst, pvr_list_t list, int textureformat, int tw, int th, pvr_ptr_t textureaddr, int filtering);
void pvr_poly_compile(pvr_poly_hdr_t* dst, const pvr_poly_cxt_t* src);
int pvr_prim(const void* data, size_t size);
int pvr_wait_ready(void);
int pvr_get_stats(pvr_stats_t* stat);
void pvr_set_bg_color(float r, float g, float b);
void pvr_scene_begin(void);
int pvr_list_begin(pvr_list_t list);
int pvr_list_finish(void);
int pvr_scene_finish(void);
int pvr_set_presort_mode(int presort);
