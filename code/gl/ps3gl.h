/* ps3gl.h -- OpenGL 1.1 fixed-function subset backed by RSX/GCM. */

#ifndef PS3GL_H
#define PS3GL_H

#include <stdint.h>
#include <string.h>

#include <ppu-types.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>

#include "GL/gl.h"

/* Constants */
#define PS3GL_MAX_TMUS          2
#define PS3GL_MAX_MATRIX_STACK  32
#define PS3GL_MAX_TEXTURES      4096
#define PS3GL_MAX_VERTS         16384   /* per-batch immediate mode */

/* Vertex ring buffer size (RSX memory). Split into two fenced segments;
 * a single frame normally stays inside the first 8 MB segment. */
#define PS3GL_VRING_SIZE        (16 * 1024 * 1024)

/* GCM label index for the vertex ring fences. Indices below 64 are
 * reserved for the system / libgcm internals. */
#define PS3GL_LABEL_VRING       64

/* GCM label index for the tess arena fences (Session 4 Stage B). */
#define PS3GL_LABEL_TARENA      65

/* Bytes reserved at the start of the tess arena for carve-once static
 * buffers (never reset per frame). Currently holds the renderer's
 * constantColor255 array. */
#define PS3GL_TARENA_STATIC     4096

/* Identity ARGB remap. Fallback for varying PSL1GHT macro definitions. */
#define PS3GL_TEX_REMAP_IDENTITY  0x00AAE4

/* Max mip levels: 4096x4096 -> 13 (gcmTexture.mipmap is the level COUNT) */
#define PS3GL_TEX_MAX_LEVELS    13

/* Dirty flags */
#define PS3GL_DIRTY_BLEND       0x0001u
#define PS3GL_DIRTY_ALPHA       0x0002u
#define PS3GL_DIRTY_DEPTH       0x0004u
#define PS3GL_DIRTY_STENCIL     0x0008u
#define PS3GL_DIRTY_VIEWPORT    0x0010u
#define PS3GL_DIRTY_CULL        0x0020u
#define PS3GL_DIRTY_SCISSOR     0x0040u
#define PS3GL_DIRTY_COLORMASK   0x0080u
#define PS3GL_DIRTY_POLYOFFSET  0x0100u
#define PS3GL_DIRTY_SHADE       0x0200u
#define PS3GL_DIRTY_ALL         0xFFFFu

/* Texenv mode IDs (shader key) */
#define PS3GL_TENV_DISABLED     0
#define PS3GL_TENV_MODULATE     1
#define PS3GL_TENV_REPLACE      2
#define PS3GL_TENV_DECAL        3
#define PS3GL_TENV_ADD          4
#define PS3GL_TENV_BLEND        5
#define PS3GL_TENV_MODULATE2    6   /* tex0 * tex1: dual-texture lightmap pass */
#define PS3GL_TENV_COUNT        7

/* 36-byte interleaved vertex: pos(16) + tc0(8) + tc1(8) + color(4) */
#pragma pack(push, 1)
typedef struct {
    float x, y, z, w;
    float u0, v0;
    float u1, v1;
    uint32_t color;
} ps3gl_vertex_t;
#pragma pack(pop)

#define PS3GL_VERTEX_SIZE       sizeof(ps3gl_vertex_t)

/* Vertex attribute offsets */
#define PS3GL_VATTR_POS_OFF     0
#define PS3GL_VATTR_TC0_OFF     16
#define PS3GL_VATTR_TC1_OFF     24
#define PS3GL_VATTR_COLOR_OFF   32

/* Texture slot */
typedef struct {
    int             glname;     /* -1 = free */
    uint8_t        *data;
    uint32_t        offset;     /* RSX offset */
    uint16_t        width;
    uint16_t        height;
    uint8_t         bpp;
    uint8_t         wrap_s, wrap_t;
    uint8_t         min_filter, mag_filter;
    uint8_t         num_levels;     /* mip levels uploaded so far (>=1) */
    uint8_t         swizzled;       /* SWZ layout (pow2 + mip chain), else LIN */
    uint8_t         sub_updated;    /* got TexSubImage2D: must stay linear */
    uint32_t        level_offset[PS3GL_TEX_MAX_LEVELS];
    uint32_t        total_size;     /* full allocation size (all levels) */
    gcmTexture      gcm_tex;
    int             dirty;
} ps3gl_texture_t;

/* TMU state */
typedef struct {
    ps3gl_texture_t *bound;
    int              enabled;
    int              texenv;
    int              dirty;
} ps3gl_tmu_t;

/* Matrix stack (column-major 4x4) */
typedef struct {
    float   stack[PS3GL_MAX_MATRIX_STACK][16];
    int     depth;
    int     dirty;
} ps3gl_matstack_t;

/* Vertex array pointers (glDrawElements path). enabled tracks
 * gl{Enable,Disable}ClientState; the pointer itself is never cleared,
 * matching real GL semantics (disable+re-enable keeps the pointer). */
typedef struct {
    const void *ptr;
    GLint       size;
    GLenum      type;
    GLsizei     stride;
    int         enabled;
} ps3gl_array_ptr_t;

/* Vertex ring buffer in RSX memory. Two fenced segments: when the
 * current segment fills mid-frame, a GCM label fence is written after
 * the draws that used it, and the other segment is reused only after
 * the GPU reports that fence — overwriting unread vertices is no
 * longer possible. */
typedef struct {
    uint8_t    *base;
    uint32_t    base_off;
    uint32_t    capacity;
    uint32_t    head;
    uint32_t    seg_size;       /* capacity / 2 */
    int         cur_seg;        /* segment being filled (0 or 1) */
    uint32_t    fence_counter;  /* monotonic fence value source */
    uint32_t    seg_fence[2];   /* fence the GPU must pass before reuse; 0 = free */
    uint32_t    label_index;    /* GCM label backing this ring's fences */
    volatile uint32_t *label_addr; /* CPU view of that label */
    uint32_t   *stat_bytes;     /* per-ring counters in ps3gl_stats_cur */
    uint32_t   *stat_wraps;
} ps3gl_vring_t;

/* Shader program pair (vertex + fragment) */
typedef struct {
    rsxVertexProgram   *vp;
    void               *vp_ucode;
    uint32_t            vp_ucode_size;
    rsxProgramConst    *mvp_const;
    rsxProgramConst    *clip_plane_const; /* world-space clip plane uniform; NULL if not in binary */

    rsxFragmentProgram *fp;
    void               *fp_ucode;
    uint32_t            fp_ucode_size;
    uint32_t            fp_offset;
} ps3gl_shader_t;

/* Global GL state */
typedef struct {
    gcmContextData     *ctx;

    uint32_t            screen_w;
    uint32_t            screen_h;

    uint32_t            dirty;

    struct {
        int     blend_enable;
        uint16_t blend_src, blend_dst;
        int     alpha_test_enable;
        uint32_t alpha_func, alpha_ref;
        int     depth_test_enable;
        int     depth_mask;
        uint32_t depth_func;
        int     cull_enable;
        uint32_t cull_face, front_face;
        int     scissor_enable;
        int16_t scissor_x, scissor_y;
        uint16_t scissor_w, scissor_h;
        int16_t vp_x, vp_y;
        uint16_t vp_w, vp_h;
        float    depth_near, depth_far;
        int     color_mask_r, color_mask_g, color_mask_b, color_mask_a;
        int     polyoffset_fill;
        float   polyoffset_factor, polyoffset_units;
        uint32_t shade_model;
        int     stencil_enable;
        uint32_t stencil_func, stencil_ref, stencil_mask;
        uint32_t stencil_fail, stencil_zfail, stencil_zpass;
        uint32_t stencil_writemask;
        uint32_t clear_color;
        float    clear_depth;
        uint32_t clear_stencil;
    } rs;

    /* GL_CLIP_PLANE0: clip plane for portal/mirror rendering.
     * clip_plane[] stores the world-space plane (normal.xyz, dist).
     * Evaluated in software in DrawElements against world-space vertex positions. */
    int                 clip_plane_enabled;
    float               clip_plane[4];     /* (nx,ny,nz,dist): dot(n,v)>=dist keeps */

    int                 active_tmu;
    ps3gl_tmu_t         tmu[PS3GL_MAX_TMUS];
    int                 client_active_tmu;

    GLenum              matrix_mode;
    ps3gl_matstack_t    mv;
    ps3gl_matstack_t    proj;

    struct {
        ps3gl_vertex_t  buf[PS3GL_MAX_VERTS];
        int             count;
        GLenum          prim;
        float           u0, v0;
        float           u1, v1;
        uint32_t        color;
    } imm;

    ps3gl_array_ptr_t   va_vertex;
    ps3gl_array_ptr_t   va_color;
    ps3gl_array_ptr_t   va_texcoord[PS3GL_MAX_TMUS];
    int                 va_locked;
    GLint               va_lock_first;
    GLsizei             va_lock_count;

    ps3gl_vring_t       vring;

    /* Tess vertex arena (Session 4 Stage B): RSX-mapped XDR region the
     * renderer's tess arrays live in, so the GPU fetches vertex data
     * directly from main memory (GCM_LOCATION_CELL) with no PPE copy.
     * tarena.base is the ring portion; tarena_lo/span cover the whole
     * region including the static carve-once header. */
    ps3gl_vring_t       tarena;
    uint8_t            *tarena_lo;       /* region start (static header) */
    uint32_t            tarena_lo_off;   /* RSX IO offset of tarena_lo */
    uint32_t            tarena_span;     /* total region size in bytes */
    uint32_t            tarena_static_used;

    ps3gl_texture_t     textures[PS3GL_MAX_TEXTURES];
    GLuint              tex_next_name;

    ps3gl_shader_t      shaders[PS3GL_TENV_COUNT];
    int                 active_shader;
    int                 mvp_uploaded;   /* current MVP already in VP constants */

} ps3gl_state_t;

/* Heap-allocated singleton. Macro allows `ps3gl.field` access. */
extern ps3gl_state_t *ps3gl_ptr;
#define ps3gl (*ps3gl_ptr)

/* Per-frame renderer counters (Session 0 instrumentation).
 * Accumulated into ps3gl_stats_cur during the frame; snapshotted to
 * ps3gl_stats_last and reset in ps3gl_begin_frame. Lives in .data
 * (static storage outside the heap state) so it survives state resets. */
typedef struct {
    uint32_t draw_calls;       /* DrawElements + immediate-mode flushes */
    uint32_t draws_elements;   /* DrawElements draws */
    uint32_t streams_direct;   /* attribute streams bound straight from the arena */
    uint32_t streams_copied;   /* attribute streams copied through the vring */
    uint32_t verts_copied;     /* vertices that needed at least one stream copy */
    uint32_t vring_bytes;      /* bytes allocated from the vertex ring */
    uint32_t vring_wraps;      /* mid-frame segment switches (fenced, safe) */
    uint32_t vring_drops;      /* draws dropped: single draw > one segment */
    uint32_t tarena_bytes;     /* bytes carved from the tess arena */
    uint32_t tarena_wraps;     /* tess arena segment switches */
    uint32_t tex_binds;        /* rsxLoadTexture calls */
    uint32_t tris_clipculled;  /* triangles dropped by sw clip plane */
    uint32_t idx_bytes;        /* index bytes written to the ring */
} ps3gl_stats_t;

extern ps3gl_stats_t ps3gl_stats_cur;   /* frame in progress */
extern ps3gl_stats_t ps3gl_stats_last;  /* last completed frame */
extern uint32_t      ps3gl_stats_frames; /* frames since init */

gcmContextData *ps3gl_get_ctx(void);

/* Module init/shutdown */
void ps3gl_init(gcmContextData *ctx, uint32_t w, uint32_t h);
void ps3gl_shutdown(void);
void ps3gl_begin_frame(void);
void ps3gl_end_frame(void);

/* Subsystem init */
void ps3gl_states_init(void);
void ps3gl_matrices_init(void);
void ps3gl_textures_init(void);
void ps3gl_vring_init(void);
void ps3gl_shaders_init(void);

void ps3gl_states_shutdown(void);
void ps3gl_textures_shutdown(void);
void ps3gl_vring_shutdown(void);
void ps3gl_shaders_shutdown(void);

/* State application (before draw) */
void ps3gl_apply_states(void);
void ps3gl_apply_matrices(void);
void ps3gl_apply_textures(void);
void ps3gl_apply_shader(void);

/* Vertex ring buffer. Index data shares the same ring and fences;
 * a draw that allocates both vertices and indices must pre-reserve the
 * combined size with ps3gl_vring_ensure so the segment switch (and its
 * fence) cannot fall between the two allocations. */
ps3gl_vertex_t *ps3gl_vring_alloc(int count, uint32_t *out_offset);
uint16_t *ps3gl_iring_alloc(int count, uint32_t *out_offset);
void *ps3gl_vring_alloc_bytes(uint32_t bytes, uint32_t align, uint32_t *out_offset);
int ps3gl_vring_ensure(uint32_t bytes);
void ps3gl_vring_frame_reset(void);  /* call at frame start (after WaitFlip) */
void ps3gl_vring_frame_end(void);    /* call at frame end (before gcmSetFlip) */

/* Tess vertex arena (Session 4 Stage B). The renderer carves its tess
 * vertex arrays from here so DrawElements can bind them directly with
 * GCM_LOCATION_CELL. Same fenced-segment discipline as the vring; a
 * caller making multiple carves that one draw will read must pre-reserve
 * the combined size with ps3gl_tess_ensure (tr_shade.c does this once
 * per surface in RB_BeginSurface). The frame reset/end hooks are driven
 * by the vring frame functions above. */
void ps3gl_tess_arena_init(void *base, uint32_t size);
int  ps3gl_tess_ensure(uint32_t bytes);
void *ps3gl_tess_alloc(uint32_t bytes);
void *ps3gl_tess_static_alloc(uint32_t bytes); /* carve-once, never reset */
void ps3gl_tess_trim(void *new_head);          /* give back the ring tail */
int  ps3gl_tess_owns(const void *p);
uint32_t ps3gl_tess_offset(const void *p);

/* Texture helpers */
ps3gl_texture_t *ps3gl_texture_find(GLuint name);
ps3gl_texture_t *ps3gl_texture_alloc(GLuint name);

/* Shader helpers */
int ps3gl_shader_key(void);
void ps3gl_shader_select(int key);

/* GL function implementations */
void ps3gl_Enable(GLenum cap);
void ps3gl_Disable(GLenum cap);
void ps3gl_BlendFunc(GLenum sfactor, GLenum dfactor);
void ps3gl_AlphaFunc(GLenum func, GLclampf ref);
void ps3gl_DepthFunc(GLenum func);
void ps3gl_DepthMask(GLboolean flag);
void ps3gl_DepthRange(GLclampd n, GLclampd f);
void ps3gl_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void ps3gl_CullFace(GLenum mode);
void ps3gl_FrontFace(GLenum mode);
void ps3gl_Scissor(GLint x, GLint y, GLsizei w, GLsizei h);
void ps3gl_Viewport(GLint x, GLint y, GLsizei w, GLsizei h);
void ps3gl_ShadeModel(GLenum mode);
void ps3gl_PolygonOffset(GLfloat factor, GLfloat units);
void ps3gl_PolygonMode(GLenum face, GLenum mode);
void ps3gl_StencilFunc(GLenum func, GLint ref, GLuint mask);
void ps3gl_StencilMask(GLuint mask);
void ps3gl_StencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void ps3gl_Clear(GLbitfield mask);
void ps3gl_ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void ps3gl_ClearDepth(GLclampd depth);
void ps3gl_ClearStencil(GLint s);
void ps3gl_LineWidth(GLfloat width);
void ps3gl_ClipPlane(GLenum plane, const GLdouble *equation);

/* PS3-specific: set clip plane directly in world space (normal.xyz, dist).
 * Avoids the eye-space transform complexity of glClipPlane.
 * Called from tr_backend.c before the portal/mirror render pass. */
void ps3gl_SetWorldClipPlane(float nx, float ny, float nz, float dist);
void ps3gl_GetIntegerv(GLenum pname, GLint *params);
void ps3gl_GetBooleanv(GLenum pname, GLboolean *params);
const GLubyte *ps3gl_GetString(GLenum name);
GLenum ps3gl_GetError(void);

void ps3gl_MatrixMode(GLenum mode);
void ps3gl_LoadIdentity(void);
void ps3gl_LoadMatrixf(const GLfloat *m);
void ps3gl_MultMatrixf(const GLfloat *m);
void ps3gl_PushMatrix(void);
void ps3gl_PopMatrix(void);
void ps3gl_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t,
                 GLdouble n, GLdouble f);
void ps3gl_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t,
                   GLdouble n, GLdouble f);
void ps3gl_Translatef(GLfloat x, GLfloat y, GLfloat z);
void ps3gl_Scalef(GLfloat x, GLfloat y, GLfloat z);
void ps3gl_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void ps3gl_GetFloatv(GLenum pname, GLfloat *params);

void ps3gl_Begin(GLenum mode);
void ps3gl_End(void);
void ps3gl_Vertex2f(GLfloat x, GLfloat y);
void ps3gl_Vertex3f(GLfloat x, GLfloat y, GLfloat z);
void ps3gl_Vertex3fv(const GLfloat *v);
void ps3gl_TexCoord2f(GLfloat s, GLfloat t);
void ps3gl_TexCoord2fv(const GLfloat *v);
void ps3gl_MultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t);

void ps3gl_Color3f(GLfloat r, GLfloat g, GLfloat b);
void ps3gl_Color3fv(const GLfloat *v);
void ps3gl_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void ps3gl_Color4fv(const GLfloat *v);
void ps3gl_Color4ubv(const GLubyte *v);
void ps3gl_Color4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void ps3gl_Color3ubv(const GLubyte *v);

void ps3gl_BindTexture(GLenum target, GLuint texture);
void ps3gl_GenTextures(GLsizei n, GLuint *textures);
void ps3gl_DeleteTextures(GLsizei n, const GLuint *textures);
void ps3gl_TexImage2D(GLenum target, GLint level, GLint internalformat,
                      GLsizei width, GLsizei height, GLint border,
                      GLenum format, GLenum type, const void *pixels);
void ps3gl_TexSubImage2D(GLenum target, GLint level, GLint xoff, GLint yoff,
                         GLsizei w, GLsizei h, GLenum format, GLenum type,
                         const void *pixels);
void ps3gl_TexParameterf(GLenum target, GLenum pname, GLfloat param);
void ps3gl_TexParameteri(GLenum target, GLenum pname, GLint param);
void ps3gl_TexEnvf(GLenum target, GLenum pname, GLfloat param);
void ps3gl_TexEnvi(GLenum target, GLenum pname, GLint param);
void ps3gl_ActiveTextureARB(GLenum texture);
void ps3gl_ClientActiveTextureARB(GLenum texture);
void ps3gl_PixelStorei(GLenum pname, GLint param);
void ps3gl_CopyTexSubImage2D(GLenum target, GLint level, GLint xoff,
                              GLint yoff, GLint x, GLint y,
                              GLsizei w, GLsizei h);

void ps3gl_VertexPointer(GLint size, GLenum type, GLsizei stride,
                         const void *ptr);
void ps3gl_TexCoordPointer(GLint size, GLenum type, GLsizei stride,
                           const void *ptr);
void ps3gl_ColorPointer(GLint size, GLenum type, GLsizei stride,
                        const void *ptr);
void ps3gl_EnableClientState(GLenum cap);
void ps3gl_DisableClientState(GLenum cap);
void ps3gl_LockArraysEXT(GLint first, GLsizei count);
void ps3gl_UnlockArraysEXT(void);
void ps3gl_DrawElements(GLenum mode, GLsizei count, GLenum type,
                        const void *indices);
void ps3gl_DrawArrays(GLenum mode, GLint first, GLsizei count);
void ps3gl_ArrayElement(GLint i);

/* No-ops */
void ps3gl_Finish(void);
void ps3gl_Flush(void);
void ps3gl_DrawBuffer(GLenum mode);
void ps3gl_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h,
                      GLenum format, GLenum type, void *pixels);

/* Pack RGBA floats to uint32 (big-endian RGBA byte order for RSX) */
static inline uint32_t ps3gl_pack_color(float r, float g, float b, float a)
{
    uint32_t ri = (uint32_t)(r * 255.0f + 0.5f);
    uint32_t gi = (uint32_t)(g * 255.0f + 0.5f);
    uint32_t bi = (uint32_t)(b * 255.0f + 0.5f);
    uint32_t ai = (uint32_t)(a * 255.0f + 0.5f);
    if (ri > 255) ri = 255;
    if (gi > 255) gi = 255;
    if (bi > 255) bi = 255;
    if (ai > 255) ai = 255;
    return (ri << 24) | (gi << 16) | (bi << 8) | ai;
}

/* Pack RGBA bytes to uint32 */
static inline uint32_t ps3gl_pack_color_ub(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
}

/* Identity matrix */
static const float ps3gl_identity[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
};

#endif /* PS3GL_H */
