/* ps3gl_vertices.c -- GL-to-RSX: vertex accumulation and ring buffer. */

#include "ps3gl.h"
#include <stdio.h>

/* Forward declaration from ps3gl_matrices.c */
extern const float *ps3gl_get_mvp(void);

/* Ring buffer management */

void ps3gl_vring_init(void)
{
    static const int seg_labels[PS3GL_VRING_SEGMENTS] = {
        PS3GL_LABEL_VRING_SEG0, PS3GL_LABEL_VRING_SEG1
    };

    ps3gl.vring.seg_capacity = PS3GL_VRING_SIZE / PS3GL_VRING_SEGMENTS;
    ps3gl.vring.base = (uint8_t *)rsxMemalign(128, PS3GL_VRING_SIZE);
    if (!ps3gl.vring.base) {
        printf("[ps3gl] FATAL: failed to allocate vertex ring buffer (%u bytes)\n",
               PS3GL_VRING_SIZE);
        return;
    }
    rsxAddressToOffset(ps3gl.vring.base, &ps3gl.vring.base_off);
    ps3gl.vring.head    = 0;
    ps3gl.vring.cur_seg = 0;

    /* One backend label per segment -- RSX only writes fence_val here once the pipeline
     * fully drains. begin_frame waits on it before reusing that segment's head. */
    for (int i = 0; i < PS3GL_VRING_SEGMENTS; i++) {
        ps3gl.vring.fence_label[i] = (volatile uint32_t *)gcmGetLabelAddress(seg_labels[i]);
        ps3gl.vring.fence_val[i]   = 0;
        if (ps3gl.vring.fence_label[i])
            *ps3gl.vring.fence_label[i] = 0;
    }

    printf("[ps3gl] Vertex ring buffer: %u bytes (%d x %u) at %p (offset 0x%08x)\n",
           PS3GL_VRING_SIZE, PS3GL_VRING_SEGMENTS, ps3gl.vring.seg_capacity,
           ps3gl.vring.base, ps3gl.vring.base_off);
}

void ps3gl_vring_shutdown(void)
{
    if (ps3gl.vring.base) {
        rsxFree(ps3gl.vring.base);
        ps3gl.vring.base = NULL;
    }
}

ps3gl_vertex_t *ps3gl_vring_alloc(int count, uint32_t *out_offset)
{
    uint32_t needed = (uint32_t)(count * PS3GL_VERTEX_SIZE);

    if (ps3gl.vring.head + needed > ps3gl.vring.seg_capacity) {
        /* Segment full for this frame -- refuse to wrap rather than stomp data the GPU hasn't
         * fetched yet (submission runs ahead of GPU execution). Should never trip; if it does, drop the batch, don't silently corrupt the frame. */
        static int warned = 0;
        if (!warned) {
            printf("[ps3gl] WARNING: vertex ring segment exhausted this frame "
                   "(need %u, have %u of %u) -- dropping draw\n",
                   needed, ps3gl.vring.seg_capacity - ps3gl.vring.head,
                   ps3gl.vring.seg_capacity);
            warned = 1;
        }
        return NULL;
    }

    uint32_t seg_base = (uint32_t)ps3gl.vring.cur_seg * ps3gl.vring.seg_capacity;
    ps3gl_vertex_t *ptr = (ps3gl_vertex_t *)(ps3gl.vring.base + seg_base + ps3gl.vring.head);
    *out_offset = ps3gl.vring.base_off + seg_base + ps3gl.vring.head;

    /* Round up to 16-byte alignment -- every carve must self-align its own start or
     * attribute sub-offsets (TC0/TC1/COLOR) drift and RSX vertex fetch wedges the whole console. */
    ps3gl.vring.head = (ps3gl.vring.head + needed + 15u) & ~15u;

    return ptr;
}

/* GL primitive type -> GCM primitive type */

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

/* Submit accumulated vertices to RSX */

static void flush_immediate(void)
{
    int count = ps3gl.imm.count;
    if (count == 0) return;

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

/* GL immediate mode functions */

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
