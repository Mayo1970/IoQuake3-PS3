/* GL-to-RSX layer: vertex array / glDrawElements path */

#include "ps3gl.h"
#include <stdio.h>

extern void ps3gl_inc_draw_count(void);
extern const float *ps3gl_get_mvp(void);

/* GL vertex array functions */

void ps3gl_VertexPointer(GLint size, GLenum type, GLsizei stride,
                         const void *ptr)
{
    ps3gl.va_vertex.ptr    = ptr;
    ps3gl.va_vertex.size   = size;
    ps3gl.va_vertex.type   = type;
    ps3gl.va_vertex.stride = stride;
}

void ps3gl_TexCoordPointer(GLint size, GLenum type, GLsizei stride,
                           const void *ptr)
{
    int tmu = ps3gl.client_active_tmu;
    ps3gl.va_texcoord[tmu].ptr    = ptr;
    ps3gl.va_texcoord[tmu].size   = size;
    ps3gl.va_texcoord[tmu].type   = type;
    ps3gl.va_texcoord[tmu].stride = stride;
}

void ps3gl_ColorPointer(GLint size, GLenum type, GLsizei stride,
                        const void *ptr)
{
    ps3gl.va_color.ptr    = ptr;
    ps3gl.va_color.size   = size;
    ps3gl.va_color.type   = type;
    ps3gl.va_color.stride = stride;
}

void ps3gl_EnableClientState(GLenum cap)  { (void)cap; }

void ps3gl_DisableClientState(GLenum cap)
{
    switch (cap) {
    case GL_VERTEX_ARRAY:        ps3gl.va_vertex.ptr = NULL; break;
    case GL_COLOR_ARRAY:         ps3gl.va_color.ptr  = NULL; break;
    case GL_TEXTURE_COORD_ARRAY:
        ps3gl.va_texcoord[ps3gl.client_active_tmu].ptr = NULL;
        break;
    default: break;
    }
}

void ps3gl_LockArraysEXT(GLint first, GLsizei count)
{
    ps3gl.va_locked     = 1;
    ps3gl.va_lock_first = first;
    ps3gl.va_lock_count = count;
}

void ps3gl_UnlockArraysEXT(void)
{
    ps3gl.va_locked = 0;
}

void ps3gl_ArrayElement(GLint i) { (void)i; }

void ps3gl_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
    (void)mode; (void)first; (void)count;
}

void ps3gl_DrawElements(GLenum mode, GLsizei count, GLenum type,
                        const void *indices)
{
    if (count <= 0) return;
    if (!ps3gl.va_vertex.ptr) return;
    ps3gl_inc_draw_count();

    /* Apply all deferred state */
    ps3gl_apply_states();
    ps3gl_apply_matrices();
    ps3gl_apply_textures();
    ps3gl_apply_shader();

    gcmContextData *ctx = ps3gl_get_ctx();
    if (!ctx) return;

    /* Determine vertex count from glLockArraysEXT or scan indices. */
    int num_verts;
    if (ps3gl.va_locked && ps3gl.va_lock_count > 0) {
        num_verts = ps3gl.va_lock_count;
    } else {
        /* Fallback: scan indices for max (shouldn't happen in practice) */
        int max_idx = 0;
        const uint16_t *idx = (const uint16_t *)indices;
        for (int i = 0; i < count; i++) {
            if ((int)idx[i] > max_idx) max_idx = (int)idx[i];
        }
        num_verts = max_idx + 1;
    }

    const uint8_t *vp  = (const uint8_t *)ps3gl.va_vertex.ptr;
    int vs = ps3gl.va_vertex.stride;
    if (vs == 0) vs = ps3gl.va_vertex.size * sizeof(float);

    const uint8_t *tp0 = (const uint8_t *)ps3gl.va_texcoord[0].ptr;
    int ts0 = ps3gl.va_texcoord[0].stride;
    if (ts0 == 0) ts0 = 2 * sizeof(float);

    const uint8_t *tp1 = (const uint8_t *)ps3gl.va_texcoord[1].ptr;
    int ts1 = ps3gl.va_texcoord[1].stride;
    if (ts1 == 0) ts1 = 2 * sizeof(float);

    const uint8_t *cp  = (const uint8_t *)ps3gl.va_color.ptr;
    int cs = ps3gl.va_color.stride;
    if (cs == 0) cs = 4;

    const int has_z = (ps3gl.va_vertex.size >= 3);

    /* Direct-bind XDR arrays to RSX, or copy to vring on failure. */
    {
        uint32_t off_pos  = 0;
        uint32_t off_tc0  = 0;
        uint32_t off_tc1  = 0;
        uint32_t off_col  = 0;

        int direct =
            (rsxAddressToOffset((void *)vp,  &off_pos) == 0) &&
            (!tp0 || rsxAddressToOffset((void *)tp0, &off_tc0) == 0) &&
            (!tp1 || rsxAddressToOffset((void *)tp1, &off_tc1) == 0) &&
            (!cp  || rsxAddressToOffset((void *)cp,  &off_col) == 0);

        if (direct) {
            /* Bind Q3's arrays straight from main memory — zero PPE copy. */
            rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_POS, 0,
                                     off_pos, vs,
                                     ps3gl.va_vertex.size >= 4 ? 4 : 4,
                                     GCM_VERTEX_DATA_TYPE_F32,
                                     GCM_LOCATION_CELL);
            if (tp0)
                rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX0, 0,
                                         off_tc0, ts0, 2,
                                         GCM_VERTEX_DATA_TYPE_F32,
                                         GCM_LOCATION_CELL);
            if (tp1)
                rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX1, 0,
                                         off_tc1, ts1, 2,
                                         GCM_VERTEX_DATA_TYPE_F32,
                                         GCM_LOCATION_CELL);
            if (cp)
                rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR0, 0,
                                         off_col, cs, 4,
                                         GCM_VERTEX_DATA_TYPE_U8,
                                         GCM_LOCATION_CELL);
        } else {
            /* Fallback: interleave-copy into RSX ring buffer. */
            uint32_t vb_offset;
            ps3gl_vertex_t *verts = ps3gl_vring_alloc(num_verts, &vb_offset);
            if (!verts) return;

            const uint32_t imm_c = ps3gl.imm.color;

            if (tp0 && tp1 && cp) {
                for (int i = 0; i < num_verts; i++) {
                    const float   *pos = (const float *)(vp  + i * vs);
                    const float   *tc0 = (const float *)(tp0 + i * ts0);
                    const float   *tc1 = (const float *)(tp1 + i * ts1);
                    const uint8_t *c   = cp + i * cs;
                    ps3gl_vertex_t *v  = &verts[i];
                    v->x = pos[0]; v->y = pos[1]; v->z = has_z ? pos[2] : 0.0f; v->w = 1.0f;
                    v->u0 = tc0[0]; v->v0 = tc0[1];
                    v->u1 = tc1[0]; v->v1 = tc1[1];
                    v->color = ps3gl_pack_color_ub(c[0], c[1], c[2], c[3]);
                }
            } else if (tp0 && cp) {
                for (int i = 0; i < num_verts; i++) {
                    const float   *pos = (const float *)(vp  + i * vs);
                    const float   *tc0 = (const float *)(tp0 + i * ts0);
                    const uint8_t *c   = cp + i * cs;
                    ps3gl_vertex_t *v  = &verts[i];
                    v->x = pos[0]; v->y = pos[1]; v->z = has_z ? pos[2] : 0.0f; v->w = 1.0f;
                    v->u0 = tc0[0]; v->v0 = tc0[1];
                    v->u1 = 0.0f;  v->v1 = 0.0f;
                    v->color = ps3gl_pack_color_ub(c[0], c[1], c[2], c[3]);
                }
            } else {
                for (int i = 0; i < num_verts; i++) {
                    const float   *pos = (const float *)(vp + i * vs);
                    ps3gl_vertex_t *v  = &verts[i];
                    v->x = pos[0]; v->y = pos[1]; v->z = has_z ? pos[2] : 0.0f; v->w = 1.0f;
                    if (tp0) { const float *tc = (const float *)(tp0 + i * ts0); v->u0 = tc[0]; v->v0 = tc[1]; }
                    else     { v->u0 = 0.0f; v->v0 = 0.0f; }
                    if (tp1) { const float *tc = (const float *)(tp1 + i * ts1); v->u1 = tc[0]; v->v1 = tc[1]; }
                    else     { v->u1 = 0.0f; v->v1 = 0.0f; }
                    if (cp)  { const uint8_t *c = cp + i * cs; v->color = ps3gl_pack_color_ub(c[0], c[1], c[2], c[3]); }
                    else     { v->color = imm_c; }
                }
            }

            rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_POS, 0,
                                     vb_offset + PS3GL_VATTR_POS_OFF,
                                     PS3GL_VERTEX_SIZE, 4,
                                     GCM_VERTEX_DATA_TYPE_F32,
                                     GCM_LOCATION_RSX);
            rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX0, 0,
                                     vb_offset + PS3GL_VATTR_TC0_OFF,
                                     PS3GL_VERTEX_SIZE, 2,
                                     GCM_VERTEX_DATA_TYPE_F32,
                                     GCM_LOCATION_RSX);
            rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX1, 0,
                                     vb_offset + PS3GL_VATTR_TC1_OFF,
                                     PS3GL_VERTEX_SIZE, 2,
                                     GCM_VERTEX_DATA_TYPE_F32,
                                     GCM_LOCATION_RSX);
            rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR0, 0,
                                     vb_offset + PS3GL_VATTR_COLOR_OFF,
                                     PS3GL_VERTEX_SIZE, 4,
                                     GCM_VERTEX_DATA_TYPE_U8,
                                     GCM_LOCATION_RSX);
        }
    }

    /* Submit indexed draw with inline indices. */
    uint32_t gcm_prim = GCM_TYPE_TRIANGLES; /* Q3 always uses GL_TRIANGLES here */
    (void)mode; /* mode is always GL_TRIANGLES in practice */

    /* RSX wants 16-bit indices; static 65536 buffer avoids a stack alloc. Q3's own
     * tessellator never gets this big (SHADER_MAX_VERTEXES caps it well below), so this is a safety net -- if it ever fires, warn loudly, don't silently eat triangles. */
    static uint16_t idx16[65536];
    if (count > 65536) {
        static int warned = 0;
        if (!warned) {
            printf("[ps3gl] WARNING: DrawElements count %d exceeds 65536 -- "
                   "truncating, geometry will be missing\n", count);
            warned = 1;
        }
    }
    int n = (count > 65536) ? 65536 : count;

    /* Software clip plane: cull triangles where all 3 verts are behind plane. */
    if (ps3gl.clip_plane_enabled && n >= 3) {
        float nx   = ps3gl.clip_plane[0];
        float ny   = ps3gl.clip_plane[1];
        float nz   = ps3gl.clip_plane[2];
        float dist = ps3gl.clip_plane[3];

        /* World-space clip test: BSP surfaces, no modelview transform needed. */
        static float clip_dist[PS3GL_MAX_VERTS];
        for (int i = 0; i < num_verts; i++) {
            const float *pos = (const float *)(vp + i * vs);
            float wx = pos[0], wy = pos[1], wz = has_z ? pos[2] : 0.0f;
            clip_dist[i] = nx*wx + ny*wy + nz*wz - dist;
        }

        int out = 0;
        const uint16_t *src16 = (const uint16_t *)indices;
        for (int i = 0; i + 2 < n; i += 3) {
            uint16_t i0 = src16[i], i1 = src16[i+1], i2 = src16[i+2];
            if (clip_dist[i0] < 0.0f && clip_dist[i1] < 0.0f && clip_dist[i2] < 0.0f)
                continue;
            idx16[out++] = i0;
            idx16[out++] = i1;
            idx16[out++] = i2;
        }
        if (out > 0)
            rsxDrawInlineIndexArray16(ctx, gcm_prim, 0, out, idx16);
        return;
    }

    /* Q3 uses 16-bit indices; direct memcpy, no conversion needed. */
    memcpy(idx16, indices, n * sizeof(uint16_t));
    rsxDrawInlineIndexArray16(ctx, gcm_prim, 0, n, idx16);
}
