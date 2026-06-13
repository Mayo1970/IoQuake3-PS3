/* ps3gl_vertices.c -- GL-to-RSX layer: vertex accumulation and ring buffer. */

#include "ps3gl.h"
#include <stdio.h>
#include <unistd.h>

/* Forward declaration from ps3gl_matrices.c */
extern const float *ps3gl_get_mvp(void);

/* Ring buffer management for vring and tess arena (both use label fences). */

void ps3gl_vring_init(void)
{
    ps3gl.vring.capacity = PS3GL_VRING_SIZE;
    ps3gl.vring.base = (uint8_t *)rsxMemalign(128, PS3GL_VRING_SIZE);
    if (!ps3gl.vring.base) {
        printf("[ps3gl] FATAL: failed to allocate vertex ring buffer (%u bytes)\n",
               PS3GL_VRING_SIZE);
        return;
    }
    rsxAddressToOffset(ps3gl.vring.base, &ps3gl.vring.base_off);
    ps3gl.vring.head = 0;
    ps3gl.vring.seg_size = PS3GL_VRING_SIZE / 2;
    ps3gl.vring.cur_seg = 0;
    ps3gl.vring.fence_counter = 0;
    ps3gl.vring.seg_fence[0] = 0;
    ps3gl.vring.seg_fence[1] = 0;
    ps3gl.vring.label_index = PS3GL_LABEL_VRING;
    ps3gl.vring.label_addr = (vu32 *)gcmGetLabelAddress(PS3GL_LABEL_VRING);
    if (ps3gl.vring.label_addr)
        *ps3gl.vring.label_addr = 0;
    ps3gl.vring.stat_bytes = &ps3gl_stats_cur.vring_bytes;
    ps3gl.vring.stat_wraps = &ps3gl_stats_cur.vring_wraps;

    printf("[ps3gl] Vertex ring buffer: %u bytes at %p (offset 0x%08x), "
           "2 x %u KB fenced segments\n",
           PS3GL_VRING_SIZE, ps3gl.vring.base, ps3gl.vring.base_off,
           ps3gl.vring.seg_size / 1024);
}

/* Wait for GPU to complete draws up to fence; bounded to avoid hard hang. */
static void ring_wait_fence(ps3gl_vring_t *vr, uint32_t fence)
{
    int spins = 0;

    if (!fence || !vr->label_addr) return;
    while ((int32_t)(*vr->label_addr - fence) < 0) {
        usleep(100);
        if (++spins > 5000) {   /* ~0.5 s */
            printf("[ps3gl] WARNING: ring fence %u timed out (label %u=%u)\n",
                   fence, vr->label_index, *vr->label_addr);
            break;
        }
    }
}

/* Frame start: rewind to segment 0 after GPU completes previous frame. */
static void ring_frame_reset(ps3gl_vring_t *vr)
{
    if (!vr->base) return;
    ring_wait_fence(vr, vr->seg_fence[0]);
    vr->seg_fence[0] = 0;
    vr->cur_seg = 0;
    vr->head = 0;
}

/* Frame end: fence the segment after last draw (backend label, not command label). */
static void ring_frame_end(ps3gl_vring_t *vr)
{
    gcmContextData *ctx = ps3gl_get_ctx();

    if (!ctx || !vr->base) return;
    vr->fence_counter++;
    rsxSetWriteBackendLabel(ctx, vr->label_index, vr->fence_counter);
    vr->seg_fence[vr->cur_seg] = vr->fence_counter;
}

void ps3gl_vring_frame_reset(void)
{
    ring_frame_reset(&ps3gl.vring);
    ring_frame_reset(&ps3gl.tarena);
}

void ps3gl_vring_frame_end(void)
{
    ring_frame_end(&ps3gl.vring);
    ring_frame_end(&ps3gl.tarena);
}

void ps3gl_vring_shutdown(void)
{
    if (ps3gl.vring.base) {
        rsxFree(ps3gl.vring.base);
        ps3gl.vring.base = NULL;
    }
    /* Tess arena lives in rsxInit IO buffer; no separate free. */
}

/* Ensure `needed` bytes available; switch segments if needed. */
static int ring_ensure(ps3gl_vring_t *vr, uint32_t needed)
{
    if (!vr->base) return 0;

    /* Drop oversized allocs (bigger than one segment = memory corruption risk). */
    if (needed > vr->seg_size) {
        ps3gl_stats_cur.vring_drops++;
        printf("[ps3gl] WARNING: ring carve dropped (%u bytes > segment %u bytes)\n",
               needed, vr->seg_size);
        return 0;
    }

    /* Segment full: switch halves and wait for GPU to release it. */
    if (vr->head + needed > (uint32_t)vr->cur_seg * vr->seg_size + vr->seg_size) {
        gcmContextData *ctx = ps3gl_get_ctx();
        if (ctx) {
            vr->fence_counter++;
            rsxSetWriteBackendLabel(ctx, vr->label_index, vr->fence_counter);
            rsxFlushBuffer(ctx);
            vr->seg_fence[vr->cur_seg] = vr->fence_counter;
        }
        vr->cur_seg ^= 1;
        ring_wait_fence(vr, vr->seg_fence[vr->cur_seg]);
        vr->seg_fence[vr->cur_seg] = 0;
        vr->head = (uint32_t)vr->cur_seg * vr->seg_size;
        (*vr->stat_wraps)++;
    }
    return 1;
}

/* Pre-reserve for multi-alloc draws (vertices + indices). Switch must happen before ANY data. */
int ps3gl_vring_ensure(uint32_t needed)
{
    return ring_ensure(&ps3gl.vring, needed);
}

/* Byte carve with self-alignment (misaligned F32 vertex offset = GPU hang). */
static void *ring_alloc_bytes(ps3gl_vring_t *vr, uint32_t bytes, uint32_t align,
                              uint32_t *out_offset)
{
    uint32_t mask = align - 1;
    uint32_t pad;

    if (!vr->base) return NULL;

    pad = ((vr->head + mask) & ~mask) - vr->head;
    if (!ring_ensure(vr, pad + bytes)) return NULL;

    /* Realign after segment switch (segment starts are already 128-aligned). */
    vr->head = (vr->head + mask) & ~mask;
    *vr->stat_bytes += bytes;

    void *ptr = vr->base + vr->head;
    *out_offset = vr->base_off + vr->head;
    vr->head += bytes;
    return ptr;
}

void *ps3gl_vring_alloc_bytes(uint32_t bytes, uint32_t align, uint32_t *out_offset)
{
    return ring_alloc_bytes(&ps3gl.vring, bytes, align, out_offset);
}

/* ----------------------------------------------------------------
 * Tess vertex arena (Session 4 Stage B)
 *
 * Lives in the top half of the 32 MB rsxInit IO buffer: that whole
 * buffer is mapped into the RSX's IO space at init (the FIFO only uses
 * the first RSX_CB_SIZE bytes), so vertex data written here by the
 * renderer is fetched directly by the RSX DMA engine with
 * GCM_LOCATION_CELL — no PPE copy, no extra allocation, and the CPU
 * side stays normal cached XDR (reads in the deform/fog code are free).
 *
 * Layout: [PS3GL_TARENA_STATIC carve-once header][fenced ring].
 * ---------------------------------------------------------------- */

void ps3gl_tess_arena_init(void *base, uint32_t size)
{
    ps3gl_vring_t *ar = &ps3gl.tarena;
    uint32_t off;

    ps3gl.tarena_lo = NULL;
    ar->base = NULL;

    if (!base || size < PS3GL_TARENA_STATIC + 2 * 128) {
        printf("[ps3gl] tess arena disabled (no usable region)\n");
        return;
    }

    /* The region must be RSX-mapped (it is, when rsxInit succeeded on
     * this buffer). If we got here through a recovered GCM context the
     * mapping may not exist — disable and fall back to vring copies. */
    if (rsxAddressToOffset(base, &off) != 0) {
        printf("[ps3gl] tess arena disabled (region not RSX-mapped)\n");
        return;
    }

    ps3gl.tarena_lo = (uint8_t *)base;
    ps3gl.tarena_lo_off = off;
    ps3gl.tarena_span = size;
    ps3gl.tarena_static_used = 0;

    ar->base = (uint8_t *)base + PS3GL_TARENA_STATIC;
    ar->base_off = off + PS3GL_TARENA_STATIC;
    ar->capacity = size - PS3GL_TARENA_STATIC;
    ar->seg_size = (ar->capacity / 2) & ~127u;
    ar->capacity = ar->seg_size * 2;
    ar->head = 0;
    ar->cur_seg = 0;
    ar->fence_counter = 0;
    ar->seg_fence[0] = 0;
    ar->seg_fence[1] = 0;
    ar->label_index = PS3GL_LABEL_TARENA;
    ar->label_addr = (vu32 *)gcmGetLabelAddress(PS3GL_LABEL_TARENA);
    if (ar->label_addr)
        *ar->label_addr = 0;
    ar->stat_bytes = &ps3gl_stats_cur.tarena_bytes;
    ar->stat_wraps = &ps3gl_stats_cur.tarena_wraps;

    printf("[ps3gl] Tess arena: %u bytes at %p (offset 0x%08x), "
           "2 x %u KB fenced segments\n",
           ar->capacity, ar->base, ar->base_off, ar->seg_size / 1024);
}

int ps3gl_tess_ensure(uint32_t bytes)
{
    return ring_ensure(&ps3gl.tarena, bytes);
}

void *ps3gl_tess_alloc(uint32_t bytes)
{
    uint32_t off;
    return ring_alloc_bytes(&ps3gl.tarena, bytes, 16, &off);
}

/* Carve-once allocation from the static header (never reset per frame).
 * For buffers with constant contents that live for the whole run. */
void *ps3gl_tess_static_alloc(uint32_t bytes)
{
    uint32_t used = (ps3gl.tarena_static_used + 15) & ~15u;

    if (!ps3gl.tarena_lo || used + bytes > PS3GL_TARENA_STATIC)
        return NULL;
    ps3gl.tarena_static_used = used + bytes;
    return ps3gl.tarena_lo + used;
}

/* Rewind the ring head to `new_head`, releasing the tail of the most
 * recent carve once its real size is known (the renderer carves tess
 * geometry at SHADER_MAX_VERTEXES size before the vertex count exists).
 * Only valid while nothing has been carved after it; out-of-range
 * pointers are ignored. */
void ps3gl_tess_trim(void *new_head)
{
    ps3gl_vring_t *ar = &ps3gl.tarena;
    uint32_t pos, seg_start;

    if (!ar->base) return;
    pos = (uint32_t)((uint8_t *)new_head - ar->base);
    seg_start = (uint32_t)ar->cur_seg * ar->seg_size;
    if (pos >= seg_start && pos <= ar->head) {
        ps3gl_stats_cur.tarena_bytes -= ar->head - pos;
        ar->head = pos;
    }
}

int ps3gl_tess_owns(const void *p)
{
    return ps3gl.tarena_lo &&
           (const uint8_t *)p >= ps3gl.tarena_lo &&
           (const uint8_t *)p < ps3gl.tarena_lo + ps3gl.tarena_span;
}

uint32_t ps3gl_tess_offset(const void *p)
{
    return ps3gl.tarena_lo_off + (uint32_t)((const uint8_t *)p - ps3gl.tarena_lo);
}

ps3gl_vertex_t *ps3gl_vring_alloc(int count, uint32_t *out_offset)
{
    return (ps3gl_vertex_t *)ps3gl_vring_alloc_bytes(
        (uint32_t)count * PS3GL_VERTEX_SIZE, 16, out_offset);
}

/* Carve index storage from the same ring (same segments, same fence).
 * The returned offset is 128-byte aligned: rsxDrawIndexArray splits a
 * misaligned draw into extra FIFO batches, alignment keeps it to the
 * minimal ~7-word command sequence. */
uint16_t *ps3gl_iring_alloc(int count, uint32_t *out_offset)
{
    uint32_t needed = (uint32_t)count * sizeof(uint16_t);
    uint16_t *ptr = (uint16_t *)ps3gl_vring_alloc_bytes(needed, 128, out_offset);
    if (ptr)
        ps3gl_stats_cur.idx_bytes += needed;
    return ptr;
}

/* ----------------------------------------------------------------
 * GL primitive type -> GCM primitive type
 * ---------------------------------------------------------------- */

static uint32_t gl_to_gcm_prim(GLenum mode)
{
    switch (mode) {
    case GL_POINTS:         return GCM_TYPE_POINTS;
    case GL_LINES:          return GCM_TYPE_LINES;
    case GL_LINE_LOOP:      return GCM_TYPE_LINE_LOOP;
    case GL_LINE_STRIP:     return GCM_TYPE_LINE_STRIP;
    case GL_TRIANGLES:      return GCM_TYPE_TRIANGLES;
    case GL_TRIANGLE_STRIP: return GCM_TYPE_TRIANGLE_STRIP;
    case GL_TRIANGLE_FAN:   return GCM_TYPE_TRIANGLE_FAN;
    case GL_QUADS:          return GCM_TYPE_QUADS;
    case GL_QUAD_STRIP:     return GCM_TYPE_QUAD_STRIP;
    case GL_POLYGON:        return GCM_TYPE_TRIANGLE_FAN;
    default:                return GCM_TYPE_TRIANGLES;
    }
}

/* ----------------------------------------------------------------
 * Submit accumulated vertices to RSX
 * ---------------------------------------------------------------- */

static void flush_immediate(void)
{
    int count = ps3gl.imm.count;
    if (count == 0) return;
    ps3gl_stats_cur.draw_calls++;
    ps3gl_stats_cur.verts_copied += (uint32_t)count;

    /* Apply all deferred state */
    ps3gl_apply_states();
    ps3gl_apply_matrices();
    ps3gl_apply_textures();
    ps3gl_apply_shader();

    /* Copy vertices into ring buffer */
    uint32_t vb_offset;
    ps3gl_vertex_t *dst = ps3gl_vring_alloc(count, &vb_offset);
    if (!dst) return;
    memcpy(dst, ps3gl.imm.buf, count * PS3GL_VERTEX_SIZE);

    gcmContextData *ctx = ps3gl_get_ctx();
    if (!ctx) return;

    /* Bind vertex attributes from ring buffer */
    /* Position: attr 0, 4 floats */
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_POS, 0,
                             vb_offset + PS3GL_VATTR_POS_OFF,
                             PS3GL_VERTEX_SIZE, 4,
                             GCM_VERTEX_DATA_TYPE_F32,
                             GCM_LOCATION_RSX);
    /* Texcoord 0: attr 8, 2 floats */
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX0, 0,
                             vb_offset + PS3GL_VATTR_TC0_OFF,
                             PS3GL_VERTEX_SIZE, 2,
                             GCM_VERTEX_DATA_TYPE_F32,
                             GCM_LOCATION_RSX);
    /* Texcoord 1: attr 9, 2 floats */
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX1, 0,
                             vb_offset + PS3GL_VATTR_TC1_OFF,
                             PS3GL_VERTEX_SIZE, 2,
                             GCM_VERTEX_DATA_TYPE_F32,
                             GCM_LOCATION_RSX);
    /* Color: attr 3, 4 unsigned bytes */
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR0, 0,
                             vb_offset + PS3GL_VATTR_COLOR_OFF,
                             PS3GL_VERTEX_SIZE, 4,
                             GCM_VERTEX_DATA_TYPE_U8,
                             GCM_LOCATION_RSX);

    /* Draw */
    rsxDrawVertexArray(ctx, gl_to_gcm_prim(ps3gl.imm.prim), 0, count);
}

/* ----------------------------------------------------------------
 * GL immediate mode functions
 * ---------------------------------------------------------------- */

void ps3gl_Begin(GLenum mode)
{
    ps3gl.imm.count = 0;
    ps3gl.imm.prim  = mode;
}

void ps3gl_End(void)
{
    flush_immediate();
    ps3gl.imm.count = 0;
}

void ps3gl_Vertex2f(GLfloat x, GLfloat y)
{
    if (ps3gl.imm.count >= PS3GL_MAX_VERTS) return;
    ps3gl_vertex_t *v = &ps3gl.imm.buf[ps3gl.imm.count++];
    v->x  = x;  v->y  = y;  v->z = 0.0f; v->w = 1.0f;
    v->u0 = ps3gl.imm.u0; v->v0 = ps3gl.imm.v0;
    v->u1 = ps3gl.imm.u1; v->v1 = ps3gl.imm.v1;
    v->color = ps3gl.imm.color;
}

void ps3gl_Vertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (ps3gl.imm.count >= PS3GL_MAX_VERTS) return;
    ps3gl_vertex_t *v = &ps3gl.imm.buf[ps3gl.imm.count++];
    v->x  = x;  v->y  = y;  v->z = z; v->w = 1.0f;
    v->u0 = ps3gl.imm.u0; v->v0 = ps3gl.imm.v0;
    v->u1 = ps3gl.imm.u1; v->v1 = ps3gl.imm.v1;
    v->color = ps3gl.imm.color;
}

void ps3gl_Vertex3fv(const GLfloat *vv)
{
    ps3gl_Vertex3f(vv[0], vv[1], vv[2]);
}

void ps3gl_TexCoord2f(GLfloat s, GLfloat t)
{
    ps3gl.imm.u0 = s;
    ps3gl.imm.v0 = t;
}

void ps3gl_TexCoord2fv(const GLfloat *v)
{
    ps3gl.imm.u0 = v[0];
    ps3gl.imm.v0 = v[1];
}

void ps3gl_MultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t)
{
    int tmu = (int)(target - GL_TEXTURE0_ARB);
    if (tmu == 0) {
        ps3gl.imm.u0 = s;
        ps3gl.imm.v0 = t;
    } else if (tmu == 1) {
        ps3gl.imm.u1 = s;
        ps3gl.imm.v1 = t;
    }
}

/* Frame draw count tracking */
static int ps3gl_frame_draw_count = 0;

void ps3gl_Finish(void) { ps3gl_frame_draw_count = 0; }
void ps3gl_inc_draw_count(void) { ps3gl_frame_draw_count++; }
void ps3gl_Flush(void) {}
void ps3gl_DrawBuffer(GLenum mode) { (void)mode; }
void ps3gl_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h,
                      GLenum format, GLenum type, void *pixels)
{
    (void)x; (void)y; (void)w; (void)h;
    (void)format; (void)type; (void)pixels;
}
