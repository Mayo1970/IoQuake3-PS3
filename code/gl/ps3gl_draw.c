/* GL-to-RSX layer: vertex array / glDrawElements path */

#include "ps3gl.h"
#include <stdio.h>

extern void ps3gl_inc_draw_count(void);
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

static void ps3gl_set_client_state(GLenum cap, int enabled)
{
    switch (cap) {
    case GL_VERTEX_ARRAY:        ps3gl.va_vertex.enabled = enabled; break;
    case GL_COLOR_ARRAY:         ps3gl.va_color.enabled  = enabled; break;
    case GL_TEXTURE_COORD_ARRAY:
        ps3gl.va_texcoord[ps3gl.client_active_tmu].enabled = enabled;
        break;
    default: break;
    }
}

void ps3gl_EnableClientState(GLenum cap)  { ps3gl_set_client_state(cap, 1); }
void ps3gl_DisableClientState(GLenum cap) { ps3gl_set_client_state(cap, 0); }

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
    if (!ps3gl.va_vertex.enabled || !ps3gl.va_vertex.ptr) return;
    ps3gl_inc_draw_count();
    ps3gl_stats_cur.draw_calls++;

    /* Apply all deferred state */
    ps3gl_apply_states();
    ps3gl_apply_matrices();
    ps3gl_apply_textures();
    ps3gl_apply_shader();

    gcmContextData *ctx = ps3gl_get_ctx();
    if (!ctx) return;

    /* Q3 uses glLockArraysEXT; fall back to index scan if not locked. */
    int num_verts;
    if (ps3gl.va_locked && ps3gl.va_lock_count > 0) {
        num_verts = ps3gl.va_lock_count;
    } else {
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

    /* Disabled arrays are absent even if pointers remain set (GL semantics). */
    const uint8_t *tp0 = ps3gl.va_texcoord[0].enabled
                       ? (const uint8_t *)ps3gl.va_texcoord[0].ptr : NULL;
    int ts0 = ps3gl.va_texcoord[0].stride;
    if (ts0 == 0) ts0 = 2 * sizeof(float);

    const uint8_t *tp1 = ps3gl.va_texcoord[1].enabled
                       ? (const uint8_t *)ps3gl.va_texcoord[1].ptr : NULL;
    int ts1 = ps3gl.va_texcoord[1].stride;
    if (ts1 == 0) ts1 = 2 * sizeof(float);

    const uint8_t *cp  = ps3gl.va_color.enabled
                       ? (const uint8_t *)ps3gl.va_color.ptr : NULL;
    int cs = ps3gl.va_color.stride;
    if (cs == 0) cs = 4;

    const int has_z = (ps3gl.va_vertex.size >= 3);

    /* Zero-copy fetch: tess in arena binds GCM_LOCATION_CELL (direct); others copy to vring. */
    {
        int pos_size = ps3gl.va_vertex.size;
        if (pos_size < 2) pos_size = 2;
        else if (pos_size > 4) pos_size = 4;

        const int pos_direct = ps3gl_tess_owns(vp);
        const int tc0_direct = tp0 && ps3gl_tess_owns(tp0);
        const int tc1_direct = tp1 && ps3gl_tess_owns(tp1);
        const int col_direct = cp  && ps3gl_tess_owns(cp);

        /* Copy (n-1)*stride + elem_size: covers all verts without over-reading. */
        uint32_t esz_pos   = (uint32_t)pos_size * sizeof(float);
        uint32_t pos_bytes = pos_direct ? 0
                           : (uint32_t)(num_verts - 1) * (uint32_t)vs + esz_pos;
        uint32_t tc0_bytes = tc0_direct ? 0
                           : tp0 ? (uint32_t)(num_verts - 1) * (uint32_t)ts0 + 8
                                 : (uint32_t)num_verts * 8;
        uint32_t tc1_bytes = (tp1 && !tc1_direct)
                           ? (uint32_t)(num_verts - 1) * (uint32_t)ts1 + 8
                           : 0;
        uint32_t col_bytes = col_direct ? 0
                           : cp  ? (uint32_t)(num_verts - 1) * (uint32_t)cs + 4
                                 : (uint32_t)num_verts * 4;

        /* Reserve all carves together: segment switch between them would fence in-flight data. */
        uint32_t reserve = 128 + (uint32_t)count * sizeof(uint16_t);
        if (pos_bytes) reserve += pos_bytes + 16;
        if (tc0_bytes) reserve += tc0_bytes + 16;
        if (tc1_bytes) reserve += tc1_bytes + 16;
        if (col_bytes) reserve += col_bytes + 16;
        if (!ps3gl_vring_ensure(reserve)) return;

        ps3gl_stats_cur.draws_elements++;
        ps3gl_stats_cur.streams_direct +=
            pos_direct + tc0_direct + tc1_direct + col_direct;
        if (pos_bytes || tc0_bytes || tc1_bytes || col_bytes) {
            ps3gl_stats_cur.verts_copied += (uint32_t)num_verts;
            ps3gl_stats_cur.streams_copied +=
                (pos_bytes != 0) + (tc0_bytes != 0) +
                (tc1_bytes != 0) + (col_bytes != 0);
        }

        uint32_t off_pos, off_tc0, off_tc1, off_col;
        uint8_t  loc_pos, loc_tc0, loc_tc1, loc_col;
        uint8_t *dst;

        if (pos_direct) {
            off_pos = ps3gl_tess_offset(vp);
            loc_pos = GCM_LOCATION_CELL;
        } else {
            dst = ps3gl_vring_alloc_bytes(pos_bytes, 16, &off_pos);
            if (!dst) return;
            memcpy(dst, vp, pos_bytes);
            loc_pos = GCM_LOCATION_RSX;
        }

        /* Missing TEX0 array: splat zeros (vertex fetch needs valid memory). */
        int bind_ts0 = tp0 ? ts0 : 8;
        if (tc0_direct) {
            off_tc0 = ps3gl_tess_offset(tp0);
            loc_tc0 = GCM_LOCATION_CELL;
        } else {
            dst = ps3gl_vring_alloc_bytes(tc0_bytes, 16, &off_tc0);
            if (!dst) return;
            if (tp0) memcpy(dst, tp0, tc0_bytes);
            else     memset(dst, 0, tc0_bytes);
            loc_tc0 = GCM_LOCATION_RSX;
        }

        /* Missing TEX1 (single-texture case): re-bind TEX0 (zero extra copy). */
        int bind_ts1;
        if (tp1) {
            if (tc1_direct) {
                off_tc1 = ps3gl_tess_offset(tp1);
                loc_tc1 = GCM_LOCATION_CELL;
            } else {
                dst = ps3gl_vring_alloc_bytes(tc1_bytes, 16, &off_tc1);
                if (!dst) return;
                memcpy(dst, tp1, tc1_bytes);
                loc_tc1 = GCM_LOCATION_RSX;
            }
            bind_ts1 = ts1;
        } else {
            off_tc1  = off_tc0;
            loc_tc1  = loc_tc0;
            bind_ts1 = bind_ts0;
        }

        /* Colors are r,g,b,a in memory; bind raw on big-endian (no repack needed). */
        int bind_cs = cp ? cs : 4;
        if (col_direct) {
            off_col = ps3gl_tess_offset(cp);
            loc_col = GCM_LOCATION_CELL;
        } else {
            dst = ps3gl_vring_alloc_bytes(col_bytes, 16, &off_col);
            if (!dst) return;
            if (cp) {
                memcpy(dst, cp, col_bytes);
            } else {
                const uint32_t imm_c = ps3gl.imm.color;
                uint32_t *cd = (uint32_t *)dst;
                for (int i = 0; i < num_verts; i++) cd[i] = imm_c;
            }
            loc_col = GCM_LOCATION_RSX;
        }

        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_POS, 0,
                                 off_pos, vs, pos_size,
                                 GCM_VERTEX_DATA_TYPE_F32, loc_pos);
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX0, 0,
                                 off_tc0, bind_ts0, 2,
                                 GCM_VERTEX_DATA_TYPE_F32, loc_tc0);
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX1, 0,
                                 off_tc1, bind_ts1, 2,
                                 GCM_VERTEX_DATA_TYPE_F32, loc_tc1);
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR0, 0,
                                 off_col, bind_cs, 4,
                                 GCM_VERTEX_DATA_TYPE_U8, loc_col);
    }

    /* Indices in ring buffer by offset; avoids inline FIFO injection. */
    uint32_t gcm_prim = GCM_TYPE_TRIANGLES; /* Q3 always uses GL_TRIANGLES here */
    (void)mode; /* mode is always GL_TRIANGLES in practice */

    int n = count;

    /* Software clip plane culling (RSX has no fixed-function clip plane). */
    if (ps3gl.clip_plane_enabled && n >= 3) {
        /* World-space test: BSP surfaces are object==world space (no MV transform needed). */
        float nx   = ps3gl.clip_plane[0];
        float ny   = ps3gl.clip_plane[1];
        float nz   = ps3gl.clip_plane[2];
        float dist = ps3gl.clip_plane[3];
        static float clip_dist[PS3GL_MAX_VERTS];
        for (int i = 0; i < num_verts; i++) {
            const float *pos = (const float *)(vp + i * vs);
            float wx = pos[0], wy = pos[1], wz = has_z ? pos[2] : 0.0f;
            clip_dist[i] = nx*wx + ny*wy + nz*wz - dist;
        }

        uint32_t idx_off;
        uint16_t *idst = ps3gl_iring_alloc(n, &idx_off);
        if (!idst) return;

        int out = 0;
        const uint16_t *src16 = (const uint16_t *)indices;
        for (int i = 0; i + 2 < n; i += 3) {
            uint16_t i0 = src16[i], i1 = src16[i+1], i2 = src16[i+2];
            if (clip_dist[i0] < 0.0f && clip_dist[i1] < 0.0f && clip_dist[i2] < 0.0f)
                continue;
            idst[out++] = i0;
            idst[out++] = i1;
            idst[out++] = i2;
        }
        ps3gl_stats_cur.tris_clipculled += (uint32_t)((n - out) / 3);
        if (out > 0)
            rsxDrawIndexArray(ctx, gcm_prim, idx_off, out,
                              GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);
        return;
    }

    /* Q3 indices are always uint16_t; straight memcpy, no conversion. */
    uint32_t idx_off;
    uint16_t *idst = ps3gl_iring_alloc(n, &idx_off);
    if (!idst) return;
    memcpy(idst, indices, n * sizeof(uint16_t));
    rsxDrawIndexArray(ctx, gcm_prim, idx_off, n,
                      GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);
}
