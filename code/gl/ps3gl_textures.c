/* ps3gl_textures.c -- GL-to-RSX: texture allocation, format conversion, binding. */

#include "ps3gl.h"
#include <stdio.h>
#include <stdlib.h>

/* Init / Shutdown */

void ps3gl_textures_init(void)
{
    for (int i = 0; i < PS3GL_MAX_TEXTURES; i++) {
        ps3gl.textures[i].glname = -1;
        ps3gl.textures[i].data   = NULL;
    }
    ps3gl.tex_next_name = 1;
}

void ps3gl_textures_shutdown(void)
{
    for (int i = 0; i < PS3GL_MAX_TEXTURES; i++) {
        if (ps3gl.textures[i].data) {
            rsxFree(ps3gl.textures[i].data);
            ps3gl.textures[i].data = NULL;
        }
        ps3gl.textures[i].glname = -1;
    }
}

/* Lookup / Allocate */

ps3gl_texture_t *ps3gl_texture_find(GLuint name)
{
    /* Simple linear scan -- 4096 slots is small enough */
    for (int i = 0; i < PS3GL_MAX_TEXTURES; i++) {
        if (ps3gl.textures[i].glname == (int)name)
            return &ps3gl.textures[i];
    }
    return NULL;
}

ps3gl_texture_t *ps3gl_texture_alloc(GLuint name)
{
    for (int i = 0; i < PS3GL_MAX_TEXTURES; i++) {
        if (ps3gl.textures[i].glname == -1) {
            ps3gl_texture_t *t = &ps3gl.textures[i];
            memset(t, 0, sizeof(*t));
            t->glname     = (int)name;
            t->wrap_s     = GCM_TEXTURE_CLAMP_TO_EDGE;
            t->wrap_t     = GCM_TEXTURE_CLAMP_TO_EDGE;
            t->min_filter = GCM_TEXTURE_LINEAR;
            t->mag_filter = GCM_TEXTURE_LINEAR;
            t->dirty      = 1;
            return t;
        }
    }
    printf("[ps3gl] WARNING: texture pool exhausted\n");
    return NULL;
}

/* GL functions */

void ps3gl_GenTextures(GLsizei n, GLuint *textures)
{
    if (!textures || n <= 0) return;
    for (int i = 0; i < n; i++) {
        textures[i] = ps3gl.tex_next_name++;
    }
}

void ps3gl_DeleteTextures(GLsizei n, const GLuint *textures)
{
    if (!textures) return;
    for (int i = 0; i < n; i++) {
        ps3gl_texture_t *t = ps3gl_texture_find(textures[i]);
        if (t) {
            /* Unbind from any TMU */
            for (int j = 0; j < PS3GL_MAX_TMUS; j++) {
                if (ps3gl.tmu[j].bound == t) {
                    ps3gl.tmu[j].bound = NULL;
                    ps3gl.tmu[j].dirty = 1;
                }
            }
            if (t->data) {
                rsxFree(t->data);
                t->data = NULL;
            }
            t->glname = -1;
        }
    }
}

void ps3gl_BindTexture(GLenum target, GLuint texture)
{
    (void)target; /* only GL_TEXTURE_2D supported */
    if (texture == 0) {
        if (ps3gl.tmu[ps3gl.active_tmu].bound != NULL)
            ps3gl.tmu[ps3gl.active_tmu].dirty = 1;
        ps3gl.tmu[ps3gl.active_tmu].bound = NULL;
        return;
    }

    ps3gl_texture_t *t = ps3gl_texture_find(texture);
    if (!t) {
        t = ps3gl_texture_alloc(texture);
    }
    if (ps3gl.tmu[ps3gl.active_tmu].bound != t)
        ps3gl.tmu[ps3gl.active_tmu].dirty = 1;
    ps3gl.tmu[ps3gl.active_tmu].bound = t;
}

/* Pixel format helpers */

static int gl_format_bpp(GLenum format)
{
    switch (format) {
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_RED:
        return 1;
    case GL_LUMINANCE_ALPHA:
        return 2;
    case GL_RGB:
    case GL_RGB8:
        return 3;
    case GL_RGBA:
    case GL_RGBA8:
    default:
        return 4;
    }
}

/* Convert source pixels to ARGB8888 for RSX. src_bpp: 1=L/A, 2=LA, 3=RGB, 4=RGBA.
 * dst_stride can exceed width*4 -- RSX mip levels share the base level's pitch, so smaller levels get padded rows instead of tight packing. */
static void convert_pixels(uint8_t *dst, uint32_t dst_stride, const uint8_t *src,
                           int width, int height, int src_bpp,
                           GLenum src_format)
{
    if (src_bpp == 4) {
        /* Fast path: Upload32() is always GL_RGBA/GL_UNSIGNED_BYTE, so one packed
         * 32-bit store matches PS3's big-endian byte order for free (see below). */
        for (int row = 0; row < height; row++) {
            uint32_t *drow = (uint32_t *)(dst + (uint32_t)row * dst_stride);
            const uint8_t *srow = src + (size_t)row * width * 4;
            for (int col = 0; col < width; col++) {
                uint8_t r = srow[col * 4 + 0];
                uint8_t g = srow[col * 4 + 1];
                uint8_t b = srow[col * 4 + 2];
                uint8_t a = srow[col * 4 + 3];
                drow[col] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
        return;
    }

    for (int row = 0; row < height; row++) {
        uint8_t *drow = dst + (uint32_t)row * dst_stride;
        const uint8_t *srow = src + (size_t)row * width * src_bpp;
        for (int col = 0; col < width; col++) {
            uint8_t r, g, b, a;
            switch (src_bpp) {
            case 1:
                if (src_format == GL_ALPHA) {
                    r = g = b = 255;
                    a = srow[col];
                } else {
                    /* Luminance */
                    r = g = b = srow[col];
                    a = 255;
                }
                break;
            case 2:
                r = g = b = srow[col * 2];
                a = srow[col * 2 + 1];
                break;
            case 3:
                r = srow[col * 3 + 0];
                g = srow[col * 3 + 1];
                b = srow[col * 3 + 2];
                a = 255;
                break;
            case 4:
            default:
                r = srow[col * 4 + 0];
                g = srow[col * 4 + 1];
                b = srow[col * 4 + 2];
                a = srow[col * 4 + 3];
                break;
            }
            /* RSX A8R8G8B8 in big-endian memory: byte order A R G B */
            drow[col * 4 + 0] = a;
            drow[col * 4 + 1] = r;
            drow[col * 4 + 2] = g;
            drow[col * 4 + 3] = b;
        }
    }
}

/* Mip chain helpers. RSX linear textures describe the whole chain from ONE width/height/
 * pitch/offset -- every level uses the BASE level's pitch, only row count shrinks. NOT tightly packed, don't "fix" it into being so. */

static uint32_t mip_level_dim(uint32_t base, int level)
{
    uint32_t d = base >> level;
    return d ? d : 1;
}

static int mip_num_levels(uint32_t w0, uint32_t h0)
{
    uint32_t maxdim = (w0 > h0) ? w0 : h0;
    int levels = 1;
    while (maxdim > 1) { maxdim >>= 1; levels++; }
    return levels;
}

/* Byte offset of `level`'s data within the packed mip buffer, given the
 * base level's row pitch (shared by every level) and height. */
static uint32_t mip_level_offset(uint32_t pitch, uint32_t h0, int level)
{
    uint32_t off = 0;
    for (int i = 0; i < level; i++)
        off += pitch * mip_level_dim(h0, i);
    return off;
}

static uint32_t mip_total_size(uint32_t pitch, uint32_t w0, uint32_t h0)
{
    return mip_level_offset(pitch, h0, mip_num_levels(w0, h0));
}

/* Build gcmTexture descriptor */
static void build_gcm_texture(ps3gl_texture_t *t)
{
    memset(&t->gcm_tex, 0, sizeof(t->gcm_tex));
    t->gcm_tex.format    = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
    t->gcm_tex.mipmap    = t->num_levels ? t->num_levels : 1;
    t->gcm_tex.dimension = GCM_TEXTURE_DIMS_2D;
    t->gcm_tex.cubemap   = GCM_FALSE;
    t->gcm_tex.remap     = PS3GL_TEX_REMAP_IDENTITY;
    t->gcm_tex.width     = t->width;
    t->gcm_tex.height    = t->height;
    t->gcm_tex.depth     = 1;
    t->gcm_tex.location  = GCM_LOCATION_RSX;
    t->gcm_tex.pitch     = t->width * 4;
    t->gcm_tex.offset    = t->offset;
}

extern void ps3_log(const char *msg);
static int ps3gl_teximg_diag_count = 0;

void ps3gl_TexImage2D(GLenum target, GLint level, GLint internalformat,
                      GLsizei width, GLsizei height, GLint border,
                      GLenum format, GLenum type, const void *pixels)
{
    (void)target; (void)border; (void)type;

    if (level < 0) return;

    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t) return;

    /* Log first 30 texture uploads for diagnostics */
    if (ps3gl_teximg_diag_count < 30) {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "PS3GL_TEXIMG: tex=%d level=%d %dx%d intfmt=0x%x fmt=0x%x type=0x%x px=%p",
                 t->glname, (int)level, (int)width, (int)height,
                 (unsigned)internalformat, (unsigned)format,
                 (unsigned)type, pixels);
        ps3_log(dbg);
        ps3gl_teximg_diag_count++;
    }

    if (level == 0) {
        /* Free old data if base dimensions changed */
        if (t->data && (t->width != (uint16_t)width || t->height != (uint16_t)height)) {
            rsxFree(t->data);
            t->data = NULL;
        }

        t->width      = (uint16_t)width;
        t->height     = (uint16_t)height;
        t->bpp        = 4; /* always store as ARGB8888 */
        t->num_levels = 0; /* becomes 1 once this level's data lands below */

        if (!t->data) {
            uint32_t pitch = (uint32_t)width * 4;
            uint32_t size  = mip_total_size(pitch, (uint32_t)width, (uint32_t)height);
            t->data = (uint8_t *)rsxMemalign(64, size);
            if (!t->data) {
                printf("[ps3gl] WARNING: failed to allocate %u bytes for texture %d\n",
                       size, t->glname);
                return;
            }
            rsxAddressToOffset(t->data, &t->offset);
        }
    }

    if (!t->data) return; /* level>0 arrived before level 0 -- ignore */
    if (level >= mip_num_levels(t->width, t->height)) return; /* beyond the allocated pyramid */

    uint32_t pitch   = (uint32_t)t->width * 4;
    uint32_t lvl_off = mip_level_offset(pitch, t->height, level);

    if (pixels) {
        int src_bpp = gl_format_bpp(format);
        convert_pixels(t->data + lvl_off, pitch, (const uint8_t *)pixels,
                       width, height, src_bpp, format);
    } else {
        memset(t->data + lvl_off, 0, pitch * mip_level_dim(t->height, level));
    }

    if ((uint8_t)(level + 1) > t->num_levels)
        t->num_levels = (uint8_t)(level + 1);

    build_gcm_texture(t);
    t->dirty = 1;
}

void ps3gl_TexSubImage2D(GLenum target, GLint level, GLint xoff, GLint yoff,
                         GLsizei w, GLsizei h, GLenum format, GLenum type,
                         const void *pixels)
{
    (void)target; (void)type;
    if (level != 0 || !pixels) return;

    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t || !t->data) return;

    int src_bpp = gl_format_bpp(format);
    const uint8_t *src = (const uint8_t *)pixels;

    for (int row = 0; row < h; row++) {
        int dy = yoff + row;
        if (dy < 0 || dy >= t->height) continue;
        for (int col = 0; col < w; col++) {
            int dx = xoff + col;
            if (dx < 0 || dx >= t->width) continue;

            uint8_t r, g, b, a;
            int si = (row * w + col) * src_bpp;
            switch (src_bpp) {
            case 1:
                if (format == GL_ALPHA) { r = g = b = 255; a = src[si]; }
                else { r = g = b = src[si]; a = 255; }
                break;
            case 3:
                r = src[si]; g = src[si+1]; b = src[si+2]; a = 255;
                break;
            case 4:
            default:
                r = src[si]; g = src[si+1]; b = src[si+2]; a = src[si+3];
                break;
            }

            int di = (dy * t->width + dx) * 4;
            t->data[di + 0] = a;
            t->data[di + 1] = r;
            t->data[di + 2] = g;
            t->data[di + 3] = b;
        }
    }

    t->dirty = 1;
}

/* Texture parameters */

static uint8_t gl_to_gcm_wrap(GLint param)
{
    switch (param) {
    case GL_REPEAT:        return GCM_TEXTURE_REPEAT;
    case GL_CLAMP:         return GCM_TEXTURE_CLAMP;
    case GL_CLAMP_TO_EDGE: return GCM_TEXTURE_CLAMP_TO_EDGE;
    default:               return GCM_TEXTURE_REPEAT;
    }
}

static uint8_t gl_to_gcm_filter(GLint param)
{
    switch (param) {
    case GL_NEAREST:                return GCM_TEXTURE_NEAREST;
    case GL_LINEAR:                 return GCM_TEXTURE_LINEAR;
    case GL_NEAREST_MIPMAP_NEAREST: return GCM_TEXTURE_NEAREST_MIPMAP_NEAREST;
    case GL_LINEAR_MIPMAP_NEAREST:  return GCM_TEXTURE_LINEAR_MIPMAP_NEAREST;
    case GL_NEAREST_MIPMAP_LINEAR:  return GCM_TEXTURE_NEAREST_MIPMAP_LINEAR;
    case GL_LINEAR_MIPMAP_LINEAR:   return GCM_TEXTURE_LINEAR_MIPMAP_LINEAR;
    default:                        return GCM_TEXTURE_LINEAR;
    }
}

static void tex_param(GLenum pname, GLint param)
{
    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t) return;

    switch (pname) {
    case GL_TEXTURE_WRAP_S:     t->wrap_s     = gl_to_gcm_wrap(param); break;
    case GL_TEXTURE_WRAP_T:     t->wrap_t     = gl_to_gcm_wrap(param); break;
    case GL_TEXTURE_MIN_FILTER: t->min_filter = gl_to_gcm_filter(param); break;
    case GL_TEXTURE_MAG_FILTER: t->mag_filter = gl_to_gcm_filter(param); break;
    case GL_TEXTURE_MAX_ANISOTROPY_EXT:
    case GL_GENERATE_MIPMAP:
        break;
    default: break;
    }
    t->dirty = 1;
}

void ps3gl_TexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    (void)target;
    tex_param(pname, (GLint)param);
}

void ps3gl_TexParameteri(GLenum target, GLenum pname, GLint param)
{
    (void)target;
    tex_param(pname, param);
}

/* TexEnv */

void ps3gl_TexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    (void)target;
    if (pname != GL_TEXTURE_ENV_MODE) return;

    int mode;
    GLenum p = (GLenum)(int)param;
    switch (p) {
    case GL_MODULATE:       mode = PS3GL_TENV_MODULATE; break;
    case GL_REPLACE:        mode = PS3GL_TENV_REPLACE; break;
    case GL_DECAL:          mode = PS3GL_TENV_DECAL; break;
    case GL_ADD:            mode = PS3GL_TENV_ADD; break;
    default:                mode = PS3GL_TENV_MODULATE; break;
    }
    if (ps3gl.tmu[ps3gl.active_tmu].texenv != mode)
        ps3gl.tmu[ps3gl.active_tmu].dirty = 1;
    ps3gl.tmu[ps3gl.active_tmu].texenv = mode;
}

void ps3gl_TexEnvi(GLenum target, GLenum pname, GLint param)
{
    ps3gl_TexEnvf(target, pname, (GLfloat)param);
}

/* Multitexture */

void ps3gl_ActiveTextureARB(GLenum texture)
{
    int tmu = (int)(texture - GL_TEXTURE0_ARB);
    if (tmu >= 0 && tmu < PS3GL_MAX_TMUS)
        ps3gl.active_tmu = tmu;
}

void ps3gl_ClientActiveTextureARB(GLenum texture)
{
    int tmu = (int)(texture - GL_TEXTURE0_ARB);
    if (tmu >= 0 && tmu < PS3GL_MAX_TMUS)
        ps3gl.client_active_tmu = tmu;
}

void ps3gl_PixelStorei(GLenum pname, GLint param)
{
    (void)pname; (void)param;
}

void ps3gl_CopyTexSubImage2D(GLenum target, GLint level, GLint xoff,
                              GLint yoff, GLint x, GLint y,
                              GLsizei w, GLsizei h)
{
    (void)target; (void)level; (void)xoff; (void)yoff;
    (void)x; (void)y; (void)w; (void)h;
}

/* Apply textures to RSX before draw */

void ps3gl_apply_textures(void)
{
    gcmContextData *ctx = ps3gl_get_ctx();
    if (!ctx) return;

    for (int i = 0; i < PS3GL_MAX_TMUS; i++) {
        ps3gl_tmu_t *tmu = &ps3gl.tmu[i];
        ps3gl_texture_t *t = tmu->bound;

        if (!tmu->enabled || !t || !t->data) {
            if (tmu->dirty) {
                rsxTextureControl(ctx, i, GCM_FALSE, 0, 0, 0);
                tmu->dirty = 0;
            }
            continue;
        }

        /* Skip re-bind if nothing changed on this TMU */
        if (!tmu->dirty && !t->dirty) continue;

        rsxLoadTexture(ctx, i, &t->gcm_tex);
        /* maxlod is 4.8 fixed point, NOT an integer level count -- get this wrong and the
         * texture binds but renders nothing. maxlod = (num_levels-1)<<8 so the sampler can reach every uploaded mip. */
        {
            uint16_t maxlod = (uint16_t)(((t->num_levels ? t->num_levels : 1) - 1) << 8);
            rsxTextureControl(ctx, i, GCM_TRUE, 0, maxlod, GCM_TEXTURE_MAX_ANISO_1);
        }
        rsxTextureFilter(ctx, i, 0, t->min_filter, t->mag_filter,
                         GCM_TEXTURE_CONVOLUTION_QUINCUNX);
        rsxTextureWrapMode(ctx, i, t->wrap_s, t->wrap_t,
                           GCM_TEXTURE_CLAMP_TO_EDGE,
                           GCM_TEXTURE_UNSIGNED_REMAP_NORMAL,
                           GCM_TEXTURE_ZFUNC_NEVER, 0);

        tmu->dirty = 0;
        t->dirty = 0;
    }
}
