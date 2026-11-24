/* dc_model.h - tiny static model format for KallistiOS / PVR.
 *
 * A model is a list of parts. A part is a set of triangle STRIPS (the PVR's native primitive); every strip is one
 * convex polygon of the model, so strip i uses strip_len[i] consecutive vertices from `verts` and the last vertex
 * of each strip must be sent with PVR_CMD_VERTEX_EOL.
 * Vertex colour already contains baked light; textured parts use it as the modulation colour.
 * Units are millimetres, +Y up, front cover faces +Z. No KOS dependency. */
#ifndef DC_MODEL_H
#define DC_MODEL_H
#include <stdint.h>

typedef struct { float x, y, z; float u, v; uint32_t argb; } dc_vertex_t;

enum { DC_TEX_RGB565 = 0, DC_TEX_ARGB4444 = 1 };

typedef struct {
    const char *file;        /* raw linear 16-bit little-endian pixels (row 0 = top), romdisk path */
    int w, h;                /* power of two, may be rectangular */
    int fmt;                 /* DC_TEX_* */
    int nearest;             /* 1: point sampling (low-res rib texture) */
} dc_texinfo_t;

typedef struct {
    const char *name;
    int texture;             /* index into model->tex, or -1 */
    int translucent;         /* 1: submit in the translucent (TR) list */
    int nstrips;
    const uint16_t *strip_len;
    const dc_vertex_t *verts;   /* all strips of this part, concatenated */
} dc_part_t;

typedef struct {
    const char *name;
    int nparts;  const dc_part_t *parts;
    int ntex;    const dc_texinfo_t *tex;
} dc_model_t;

extern const dc_model_t dc_model_cd_case_white;
extern const dc_model_t dc_model_cd_case_darkgray;
extern const dc_model_t dc_model_dreamcast_pal_case_blue;

#endif
