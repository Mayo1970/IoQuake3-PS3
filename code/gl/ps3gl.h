/*
 * ps3gl.h -- GL-to-RSX translation layer for ioquake3-PS3.
 *
 * Provides a minimal OpenGL 1.1 fixed-function subset backed by the
 * RSX Reality Synthesizer via PSL1GHT's librsx/GCM.
 *
 * Architecture mirrors the Xbox 360 port's GL translation layer:
 *   - Single global state struct (ps3gl)
 *   - Dirty-flag bitmask for deferred state application
 *   - 36-byte interleaved vertex format
 *   - Vertex ring buffer in RSX memory
 *   - Pre-compiled Cg vertex/fragment programs selected by texenv key
 *   - 32-deep matrix stacks for modelview/projection
 *
 * All ps3gl_* functions are internal. The public GL API is exposed
 * through qgl_ps3.c which wires qgl* pointers to these implementations.
 */

#ifndef PS3GL_H
#define PS3GL_H

#include <stdint.h>
#include <string.h>

#include <ppu-types.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>

#include "GL/gl.h"

/* ----------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------- */
#define PS3GL_MAX_TMUS          2
#define PS3GL_MAX_MATRIX_STACK  32
#define PS3GL_MAX_TEXTURES      4096
#define PS3GL_MAX_VERTS         16384   /* per-batch immediate mode */

/* Vertex ring buffer: 2 MB in RSX memory */
#define PS3GL_VRING_SIZE        (2 * 1024 * 1024)

/*
 * Texture remap: identity ARGB -> ARGB.
 * The GCM_TEXTURE_REMAP_* macros vary across PSL1GHT versions.
 * We compute the remap value at init time in ps3gl_textures.c.
 * Fallback constant: 0x00AAE4 is the standard identity remap
 * for the NV40/RSX (remap each channel to itself, type = remap).
 */
#define PS3GL_TEX_REMAP_IDENTITY  0x00AAE4

/* ----------------------------------------------------------------
 * Dirty flags for deferred state application
 * ---------------------------------------------------------------- */
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

/* ----------------------------------------------------------------
 * Texenv mode IDs (used as shader cache key components)
 * ---------------------------------------------------------------- */
#define PS3GL_TENV_DISABLED     0
#define PS3GL_TENV_MODULATE     1
#define PS3GL_TENV_REPLACE      2
#define PS3GL_TENV_DECAL        3
#define PS3GL_TENV_ADD          4
#define PS3GL_TENV_BLEND        5
#define PS3GL_TENV_COUNT        6

/* ----------------------------------------------------------------
 * Vertex format: 36 bytes, interleaved
 *   float x, y, z, w;       -- 16 bytes (offset  0)
 *   float u0, v0;            --  8 bytes (offset 16)
 *   float u1, v1;            --  8 bytes (offset 24)
 *   uint32_t color;          --  4 bytes (offset 32) RGBA packed
 * ---------------------------------------------------------------- */
#pragma pack(push, 1)
typedef struct {
    float x, y, z, w;
    float u0, v0;
    float u1, v1;
    uint32_t color;             /* packed RGBA for RSX (R in high byte on BE) */
} ps3gl_vertex_t;
#pragma pack(pop)

#define PS3GL_VERTEX_SIZE       sizeof(ps3gl_vertex_t) /* 36 */

/* Vertex attribute offsets for rsxBindVertexArrayAttrib */
#define PS3GL_VATTR_POS_OFF     0
#define PS3GL_VATTR_TC0_OFF     16
#define PS3GL_VATTR_TC1_OFF     24
#define PS3GL_VATTR_COLOR_OFF   32

/* ----------------------------------------------------------------
 * Texture slot
 * ---------------------------------------------------------------- */
typedef struct {
    int             glname;     /* GL texture name, -1 = free */
    uint8_t        *data;       /* RSX-allocated pixel data */
    uint32_t        offset;     /* RSX memory offset */
    uint16_t        width;
    uint16_t        height;
    uint8_t         bpp;        /* bytes per pixel (1 or 4) */
    uint8_t         wrap_s;     /* GCM_TEXTURE_* wrap mode */
    uint8_t         wrap_t;
    uint8_t         min_filter; /* GCM_TEXTURE_* filter */
    uint8_t         mag_filter;
    gcmTexture      gcm_tex;    /* pre-built GCM texture descriptor */
    int             dirty;      /* needs re-upload to RSX tex unit */
} ps3gl_texture_t;

/* ----------------------------------------------------------------
 * TMU (texture mapping unit) state
 * ---------------------------------------------------------------- */
typedef struct {
    ps3gl_texture_t *bound;     /* currently bound texture */
    int              enabled;   /* GL_TEXTURE_2D enabled on this TMU */
    int              texenv;    /* PS3GL_TENV_* mode */
    int              dirty;     /* needs re-bind to RSX */
} ps3gl_tmu_t;

/* ----------------------------------------------------------------
 * Matrix stack
 * ---------------------------------------------------------------- */
typedef struct {
    float   stack[PS3GL_MAX_MATRIX_STACK][16];  /* column-major 4x4 */
    int     depth;                              /* current stack index */
    int     dirty;                              /* needs re-upload */
} ps3gl_matstack_t;

/* ----------------------------------------------------------------
 * Vertex array pointers (for glDrawElements path)
 * ---------------------------------------------------------------- */
typedef struct {
    const void *ptr;
    GLint       size;       /* components: 2,3,4 */
    GLenum      type;
    GLsizei     stride;
} ps3gl_array_ptr_t;

/* ----------------------------------------------------------------
 * Vertex ring buffer in RSX memory
 * ---------------------------------------------------------------- */
typedef struct {
    uint8_t    *base;       /* RSX-allocated base address */
    uint32_t    base_off;   /* RSX memory offset of base */
    uint32_t    capacity;   /* total size in bytes */
    uint32_t    head;       /* write offset (bytes from base) */
} ps3gl_vring_t;

/* ----------------------------------------------------------------
 * Shader program pair (vertex + fragment)
 * ---------------------------------------------------------------- */
typedef struct {
    rsxVertexProgram   *vp;
    void               *vp_ucode;
    uint32_t            vp_ucode_size;
    rsxProgramConst    *mvp_const;      /* cached MVP constant (string lookup once) */

    rsxFragmentProgram *fp;
    void               *fp_ucode;      /* RSX-allocated */
    uint32_t            fp_ucode_size;
    uint32_t            fp_offset;      /* RSX offset for fp ucode */
} ps3gl_shader_t;

/* ----------------------------------------------------------------
 * Global GL state
 * ---------------------------------------------------------------- */
typedef struct {
    gcmContextData     *ctx;            /* RSX command context */

    /* Display */
    uint32_t            screen_w;
    uint32_t            screen_h;

    /* Dirty tracking */
    uint32_t            dirty;

    /* Render state */
    struct {
        /* Blend */
        int     blend_enable;
        uint16_t blend_src, blend_dst;

        /* Alpha test */
        int     alpha_test_enable;
        uint32_t alpha_func;
        uint32_t alpha_ref;             /* 0-255 */

        /* Depth */
        int     depth_test_enable;
        int     depth_mask;             /* write enable */
        uint32_t depth_func;

        /* Cull */
        int     cull_enable;
        uint32_t cull_face;             /* GL_FRONT / GL_BACK */
        uint32_t front_face;            /* GL_CW / GL_CCW */

        /* Scissor */
        int     scissor_enable;
        int16_t scissor_x, scissor_y;
        uint16_t scissor_w, scissor_h;

        /* Viewport */
        int16_t vp_x, vp_y;
        uint16_t vp_w, vp_h;
        float    depth_near, depth_far;

        /* Color mask */
        int     color_mask_r, color_mask_g, color_mask_b, color_mask_a;

        /* Polygon offset */
        int     polyoffset_fill;
        float   polyoffset_factor, polyoffset_units;

        /* Shade model */
        uint32_t shade_model;

        /* Stencil */
        int     stencil_enable;
        uint32_t stencil_func, stencil_ref, stencil_mask;
        uint32_t stencil_fail, stencil_zfail, stencil_zpass;
        uint32_t stencil_writemask;

        /* Clear values */
        uint32_t clear_color;           /* packed ARGB */
        float    clear_depth;
        uint32_t clear_stencil;
    } rs;

    /* TMU state */
    int                 active_tmu;     /* 0 or 1 */
    ps3gl_tmu_t         tmu[PS3GL_MAX_TMUS];

    /* Client-side TMU selector (for vertex array tex coord pointers) */
    int                 client_active_tmu;

    /* Matrix stacks */
    GLenum              matrix_mode;    /* GL_MODELVIEW etc. */
    ps3gl_matstack_t    mv;             /* modelview */
    ps3gl_matstack_t    proj;           /* projection */

    /* Immediate-mode vertex accumulation */
    struct {
        ps3gl_vertex_t  buf[PS3GL_MAX_VERTS];
        int             count;
        GLenum          prim;           /* GL_TRIANGLES etc. */
        float           u0, v0;        /* current texcoord TMU 0 */
        float           u1, v1;        /* current texcoord TMU 1 */
        uint32_t        color;         /* current packed color */
    } imm;

    /* Vertex array pointers (glDrawElements path) */
    ps3gl_array_ptr_t   va_vertex;
    ps3gl_array_ptr_t   va_color;
    ps3gl_array_ptr_t   va_texcoord[PS3GL_MAX_TMUS];
    int                 va_locked;
    GLint               va_lock_first;
    GLsizei             va_lock_count;

    /* Vertex ring buffer */
    ps3gl_vring_t       vring;

    /* Textures */
    ps3gl_texture_t     textures[PS3GL_MAX_TEXTURES];
    GLuint              tex_next_name;  /* monotonic name allocator */

    /* Shaders */
    ps3gl_shader_t      shaders[PS3GL_TENV_COUNT]; /* per texenv combo */
    int                 active_shader;

} ps3gl_state_t;

/* Global singleton -- heap-allocated to avoid BSS corruption.
 * The macro lets all existing code continue to use `ps3gl.field`. */
extern ps3gl_state_t *ps3gl_ptr;
#define ps3gl (*ps3gl_ptr)

/* Returns the GCM context, with fallback to backup copy if ps3gl.ctx is NULL */
gcmContextData *ps3gl_get_ctx(void);

/* Restore ps3gl_ptr from .data backup if BSS corruption zeroed it */
void ps3gl_restore_if_needed(void);

/* ----------------------------------------------------------------
 * Module init/shutdown (called from ps3_glimp.c)
 * ---------------------------------------------------------------- */
void ps3gl_init(gcmContextData *ctx, uint32_t w, uint32_t h);
void ps3gl_shutdown(void);
void ps3gl_begin_frame(void);
void ps3gl_end_frame(void);

/* ----------------------------------------------------------------
 * Subsystem init (called by ps3gl_init)
 * ---------------------------------------------------------------- */
void ps3gl_states_init(void);
void ps3gl_matrices_init(void);
void ps3gl_textures_init(void);
void ps3gl_vring_init(void);
void ps3gl_shaders_init(void);

void ps3gl_states_shutdown(void);
void ps3gl_textures_shutdown(void);
void ps3gl_vring_shutdown(void);
void ps3gl_shaders_shutdown(void);

/* ----------------------------------------------------------------
 * State application (called before draw)
 * ---------------------------------------------------------------- */
void ps3gl_apply_states(void);
void ps3gl_apply_matrices(void);
void ps3gl_apply_textures(void);
void ps3gl_apply_shader(void);

/* ----------------------------------------------------------------
 * Vertex ring buffer
 * ---------------------------------------------------------------- */
ps3gl_vertex_t *ps3gl_vring_alloc(int count, uint32_t *out_offset);

/* ----------------------------------------------------------------
 * Texture helpers
 * ---------------------------------------------------------------- */
ps3gl_texture_t *ps3gl_texture_find(GLuint name);
ps3gl_texture_t *ps3gl_texture_alloc(GLuint name);

/* ----------------------------------------------------------------
 * Shader helpers
 * ---------------------------------------------------------------- */
int ps3gl_shader_key(void);
void ps3gl_shader_select(int key);

/* ----------------------------------------------------------------
 * GL function implementations (wired from qgl_ps3.c)
 * Each file provides a group of GL functions.
 * ---------------------------------------------------------------- */

/* ps3gl_states.c */
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
void ps3gl_GetIntegerv(GLenum pname, GLint *params);
void ps3gl_GetBooleanv(GLenum pname, GLboolean *params);
const GLubyte *ps3gl_GetString(GLenum name);
GLenum ps3gl_GetError(void);

/* ps3gl_matrices.c */
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

/* ps3gl_vertices.c */
void ps3gl_Begin(GLenum mode);
void ps3gl_End(void);
void ps3gl_Vertex2f(GLfloat x, GLfloat y);
void ps3gl_Vertex3f(GLfloat x, GLfloat y, GLfloat z);
void ps3gl_Vertex3fv(const GLfloat *v);
void ps3gl_TexCoord2f(GLfloat s, GLfloat t);
void ps3gl_TexCoord2fv(const GLfloat *v);
void ps3gl_MultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t);

/* ps3gl_colors.c */
void ps3gl_Color3f(GLfloat r, GLfloat g, GLfloat b);
void ps3gl_Color3fv(const GLfloat *v);
void ps3gl_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void ps3gl_Color4fv(const GLfloat *v);
void ps3gl_Color4ubv(const GLubyte *v);
void ps3gl_Color4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void ps3gl_Color3ubv(const GLubyte *v);

/* ps3gl_textures.c */
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

/* ps3gl_draw.c */
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

/* No-ops kept for completeness */
void ps3gl_Finish(void);
void ps3gl_Flush(void);
void ps3gl_DrawBuffer(GLenum mode);
void ps3gl_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h,
                      GLenum format, GLenum type, void *pixels);

/* ----------------------------------------------------------------
 * Utility
 * ---------------------------------------------------------------- */

/*
 * Pack RGBA floats [0,1] to uint32 for RSX vertex color.
 *
 * RSX GCM_VERTEX_DATA_TYPE_U8 reads 4 bytes at ascending addresses as
 * (x, y, z, w). The shader's COLOR0 semantic maps to (R, G, B, A).
 * On big-endian PS3, (R<<24)|(G<<16)|(B<<8)|A stores as bytes R,G,B,A
 * at ascending addresses -- matching the shader's expected RGBA order.
 */
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

/* Pack RGBA bytes to uint32 (RGBA byte order in memory on big-endian) */
static inline uint32_t ps3gl_pack_color_ub(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
}

/* Identity matrix constant */
static const float ps3gl_identity[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
};

#endif /* PS3GL_H */
