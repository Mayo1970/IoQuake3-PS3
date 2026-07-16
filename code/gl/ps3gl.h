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

/* Vertex ring buffer size (RSX memory) */
#define PS3GL_VRING_SIZE        (2 * 1024 * 1024)

/* Identity ARGB remap. Fallback for varying PSL1GHT macro definitions. */
#define PS3GL_TEX_REMAP_IDENTITY  0x00AAE4

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
    uint16_t        width;      /* level 0 (base) dimensions */
    uint16_t        height;
    uint8_t         bpp;
    uint8_t         num_levels; /* contiguous mip levels uploaded so far, >=1 */
    uint8_t         wrap_s, wrap_t;
    uint8_t         min_filter, mag_filter;
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

/* Vertex array pointers (glDrawElements path) */
typedef struct {
    const void *ptr;
    GLint       size;
    GLenum      type;
    GLsizei     stride;
} ps3gl_array_ptr_t;

/* Double-buffered: command submission runs ahead of GPU execution, so without this a
 * same-frame wrap would stomp data the GPU hasn't fetched yet. Labels <64 are system-reserved; 65 is saved for an unimplemented tess-arena fence -- don't reuse it. */
#define PS3GL_VRING_SEGMENTS    2
#define PS3GL_LABEL_VRING_SEG0  64
#define PS3GL_LABEL_VRING_SEG1  66

/* Vertex ring buffer in RSX memory */
typedef struct {
    uint8_t    *base;
    uint32_t    base_off;
    uint32_t    seg_capacity;  /* bytes per segment */
    uint32_t    head;          /* offset within the current segment */
    int         cur_seg;       /* 0 or 1 */
    volatile uint32_t *fence_label[PS3GL_VRING_SEGMENTS]; /* GCM label addr; RSX writes here when pipeline drains */
    uint32_t    fence_val[PS3GL_VRING_SEGMENTS];           /* last value written to each label */
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

    /* GL_CLIP_PLANE0: world-space plane (normal.xyz, dist) for portal/mirror clipping.
     * Must stay world-space -- eye-space flips the sign and blacks out the mirror. Software-evaluated in DrawElements. */
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

    ps3gl_texture_t     textures[PS3GL_MAX_TEXTURES];
    GLuint              tex_next_name;

    ps3gl_shader_t      shaders[PS3GL_TENV_COUNT];
    int                 active_shader;
    rsxVertexProgram   *active_vp;      /* physical VP currently loaded on RSX; all shader slots share one VP, so this is tracked separately from active_shader (the FP key) */
    uint32_t            mvp_uploaded_gen; /* last ps3gl_get_mvp_generation() value uploaded to active_vp */

} ps3gl_state_t;

/* Heap-allocated singleton. Macro allows `ps3gl.field` access. */
extern ps3gl_state_t *ps3gl_ptr;
#define ps3gl (*ps3gl_ptr)

gcmContextData *ps3gl_get_ctx(void);
void ps3gl_restore_if_needed(void);

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

/* Vertex ring buffer */
ps3gl_vertex_t *ps3gl_vring_alloc(int count, uint32_t *out_offset);

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

/* PS3-specific: sets the clip plane directly in world space (normal.xyz, dist),
 * skipping glClipPlane's eye-space transform. Called from tr_backend.c before the portal/mirror pass. */
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
