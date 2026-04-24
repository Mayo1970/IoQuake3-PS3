/*
 * ps3gl_draw.c -- GL-to-RSX layer: vertex array / glDrawElements path.
 *
 * Handles the batch rendering path used by Q3's main renderer:
 * glVertexPointer + glTexCoordPointer + glColorPointer + glDrawElements.
 *
 * Converts vertex arrays to the interleaved ps3gl_vertex_t format,
 * copies into the RSX vertex ring buffer, and issues indexed draws.
 */

#include "ps3gl.h"
#include <stdio.h>

extern void ps3gl_inc_draw_count(void);

/* Forward declaration from ps3gl_matrices.c */
extern const float *ps3gl_get_mvp(void);

/* ----------------------------------------------------------------
 * GL vertex array functions
 * ---------------------------------------------------------------- */

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

/* ----------------------------------------------------------------
 * glDrawElements -- main batch draw path
 *
 * Q3 calls this with:
 *   mode = GL_TRIANGLES
 *   type = GL_UNSIGNED_INT
 *   indices = pointer to index array
 * ---------------------------------------------------------------- */

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

    /*
     * Determine vertex count.
     * Q3 always calls glLockArraysEXT before glDrawElements, giving us
     * the exact vertex count without scanning all indices.
     */
    int num_verts;
    if (ps3gl.va_locked && ps3gl.va_lock_count > 0) {
        num_verts = ps3gl.va_lock_count;
    } else {
        /* Fallback: scan indices for max (shouldn't happen in practice) */
        int max_idx = 0;
        if (type == GL_UNSIGNED_INT) {
            const uint32_t *idx = (const uint32_t *)indices;
            for (int i = 0; i < count; i++) {
                if ((int)idx[i] > max_idx) max_idx = (int)idx[i];
            }
        } else {
            const uint16_t *idx = (const uint16_t *)indices;
            for (int i = 0; i < count; i++) {
                if ((int)idx[i] > max_idx) max_idx = (int)idx[i];
            }
        }
        num_verts = max_idx + 1;
    }

    /* Allocate interleaved vertices in ring buffer */
    uint32_t vb_offset;
    ps3gl_vertex_t *verts = ps3gl_vring_alloc(num_verts, &vb_offset);
    if (!verts) return;

    /* Source pointers and strides */
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
    if (cs == 0) cs = 4; /* 4 bytes RGBA */

    /* Convert vertex arrays to interleaved format */
    for (int i = 0; i < num_verts; i++) {
        ps3gl_vertex_t *v = &verts[i];

        /* Position */
        const float *pos = (const float *)(vp + i * vs);
        v->x = pos[0];
        v->y = pos[1];
        v->z = (ps3gl.va_vertex.size >= 3) ? pos[2] : 0.0f;
        v->w = 1.0f;

        /* Texcoord 0 */
        if (tp0) {
            const float *tc = (const float *)(tp0 + i * ts0);
            v->u0 = tc[0];
            v->v0 = tc[1];
        } else {
            v->u0 = 0.0f;
            v->v0 = 0.0f;
        }

        /* Texcoord 1 */
        if (tp1) {
            const float *tc = (const float *)(tp1 + i * ts1);
            v->u1 = tc[0];
            v->v1 = tc[1];
        } else {
            v->u1 = 0.0f;
            v->v1 = 0.0f;
        }

        /* Color */
        if (cp) {
            const uint8_t *c = cp + i * cs;
            v->color = ps3gl_pack_color_ub(c[0], c[1], c[2], c[3]);
        } else {
            v->color = ps3gl.imm.color;
        }
    }

    /* Bind vertex attributes */
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

    /*
     * Issue indexed draw.
     * For RSX we use inline index submission (indices live in main memory,
     * pushed into command buffer). This avoids needing index data in RSX memory.
     */
    uint32_t gcm_prim = GCM_TYPE_TRIANGLES; /* Q3 always uses GL_TRIANGLES here */
    (void)mode; /* mode is always GL_TRIANGLES in practice */

    /*
     * RSX inline index draw expects 16-bit indices.
     * Q3's indices fit in 16 bits (max ~SHADER_MAX_VERTEXES = 4096).
     * Static buffer avoids 128KB stack allocation per draw call.
     */
    static uint16_t idx16[65536];
    int n = (count > 65536) ? 65536 : count;

    /* Issue indexed draw */

    if (type == GL_UNSIGNED_INT) {
        const uint32_t *idx32 = (const uint32_t *)indices;
        for (int i = 0; i < n; i++)
            idx16[i] = (uint16_t)idx32[i];
        rsxDrawInlineIndexArray16(ctx, gcm_prim, 0, n, idx16);
    } else {
        const uint16_t *src16 = (const uint16_t *)indices;
        memcpy(idx16, src16, n * sizeof(uint16_t));
        rsxDrawInlineIndexArray16(ctx, gcm_prim, 0, n, idx16);
    }
}
