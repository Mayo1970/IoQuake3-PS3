/*
 * ps3gl_textures.c -- GL-to-RSX layer: texture management.
 *
 * Manages texture allocation in RSX memory, format conversion,
 * and binding to RSX texture units via GCM commands.
 *
 * Each texture slot holds pixel data in RSX VRAM. When bound,
 * the gcmTexture descriptor is configured and loaded via rsxLoadTexture.
 */

#include "ps3gl.h"
#include <stdio.h>
#include <stdlib.h>

/* ----------------------------------------------------------------
 * Init / Shutdown
 * ---------------------------------------------------------------- */

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

/* ----------------------------------------------------------------
 * Lookup / Allocate
 * ---------------------------------------------------------------- */

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

/* ----------------------------------------------------------------
 * GL functions
 * ---------------------------------------------------------------- */

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

/* ----------------------------------------------------------------
 * Pixel format helpers
 * ---------------------------------------------------------------- */

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

/*
 * Convert source pixels to ARGB8888 for RSX.
 * src_bpp: 1 (L or A), 2 (LA), 3 (RGB), 4 (RGBA)
 */
static void convert_pixels(uint8_t *dst, const uint8_t *src,
                           int width, int height, int src_bpp,
                           GLenum src_format)
{
    int npix = width * height;

    if (src_bpp == 4) {
        /* RGBA -> ARGB on big-endian is a rotate of the 32-bit word:
         * (R<<24|G<<16|B<<8|A) -> (A<<24|R<<16|G<<8|B). One load, one
         * rotate, one store per pixel -- this is the hot path for
         * cinematic frames (30 full-texture uploads per second). */
        const uint32_t *s32 = (const uint32_t *)src;
        uint32_t *d32 = (uint32_t *)dst;
        for (int i = 0; i < npix; i++) {
            uint32_t p = s32[i];
            d32[i] = (p >> 8) | (p << 24);
        }
        return;
    }

    for (int i = 0; i < npix; i++) {
        uint8_t r, g, b, a;
        switch (src_bpp) {
        case 1:
            if (src_format == GL_ALPHA) {
                r = g = b = 255;
                a = src[i];
            } else {
                /* Luminance */
                r = g = b = src[i];
                a = 255;
            }
            break;
        case 2:
            r = g = b = src[i * 2];
            a = src[i * 2 + 1];
            break;
        case 3:
            r = src[i * 3 + 0];
            g = src[i * 3 + 1];
            b = src[i * 3 + 2];
            a = 255;
            break;
        case 4:
        default:
            r = src[i * 4 + 0];
            g = src[i * 4 + 1];
            b = src[i * 4 + 2];
            a = src[i * 4 + 3];
            break;
        }
        /* RSX A8R8G8B8 in big-endian memory: byte order A R G B */
        dst[i * 4 + 0] = a;
        dst[i * 4 + 1] = r;
        dst[i * 4 + 2] = g;
        dst[i * 4 + 3] = b;
    }
}

/* ----------------------------------------------------------------
 * Mip chain / swizzle helpers
 * ---------------------------------------------------------------- */

static int is_pow2(int v)
{
    return v > 0 && (v & (v - 1)) == 0;
}

static int log2i(int v)
{
    int l = 0;
    while (v > 1) { v >>= 1; l++; }
    return l;
}

/* Number of levels in a full mip chain down to 1x1 */
static int mip_chain_levels(int w, int h)
{
    int l = 1;
    while (w > 1 || h > 1) {
        w = (w > 1) ? (w >> 1) : 1;
        h = (h > 1) ? (h >> 1) : 1;
        l++;
    }
    return (l > PS3GL_TEX_MAX_LEVELS) ? PS3GL_TEX_MAX_LEVELS : l;
}

/*
 * Morton/Z-order address of pixel (x,y) in a w x h pow2 image
 * (NV40 swizzle: x and y bits interleaved, x in the lowest position;
 * once the smaller dimension's bits are exhausted, the remaining bits
 * of the larger dimension continue linearly).
 */
static uint32_t swizzle_offset(uint32_t x, uint32_t y, int lw, int lh)
{
    uint32_t offset = 0;
    int shift = 0;
    while (lw | lh) {
        if (lw) { offset |= (x & 1) << shift; x >>= 1; shift++; lw--; }
        if (lh) { offset |= (y & 1) << shift; y >>= 1; shift++; lh--; }
    }
    return offset;
}

/* Swizzle a 32bpp pow2 image from a linear source into dst */
static void swizzle_argb8(uint32_t *dst, const uint32_t *src, int w, int h)
{
    int lw = log2i(w), lh = log2i(h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            dst[swizzle_offset(x, y, lw, lh)] = src[y * w + x];
        }
    }
}

/* Deswizzle level 0 of a 32bpp pow2 image into a linear dst */
static void deswizzle_argb8(uint32_t *dst, const uint32_t *src, int w, int h)
{
    int lw = log2i(w), lh = log2i(h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            dst[y * w + x] = src[swizzle_offset(x, y, lw, lh)];
        }
    }
}

/* Queue an RSX texture cache invalidation (data changed under a live bind) */
static void invalidate_texture_cache(void)
{
    gcmContextData *ctx = ps3gl_get_ctx();
    if (ctx)
        rsxInvalidateTextureCache(ctx, GCM_INVALIDATE_TEXTURE);
}

/* Build gcmTexture descriptor */
static void build_gcm_texture(ps3gl_texture_t *t)
{
    memset(&t->gcm_tex, 0, sizeof(t->gcm_tex));
    t->gcm_tex.format    = GCM_TEXTURE_FORMAT_A8R8G8B8 |
                           (t->swizzled ? GCM_TEXTURE_FORMAT_SWZ
                                        : GCM_TEXTURE_FORMAT_LIN);
    t->gcm_tex.mipmap    = (t->num_levels > 0) ? t->num_levels : 1;
    t->gcm_tex.dimension = GCM_TEXTURE_DIMS_2D;
    t->gcm_tex.cubemap   = GCM_FALSE;
    t->gcm_tex.remap     = PS3GL_TEX_REMAP_IDENTITY;
    t->gcm_tex.width     = t->width;
    t->gcm_tex.height    = t->height;
    t->gcm_tex.depth     = 1;
    t->gcm_tex.location  = GCM_LOCATION_RSX;
    t->gcm_tex.pitch     = t->swizzled ? 0 : (uint32_t)t->width * 4;
    t->gcm_tex.offset    = t->offset;
}

extern void ps3_log(const char *msg);
static int ps3gl_teximg_diag_count = 0;

void ps3gl_TexImage2D(GLenum target, GLint level, GLint internalformat,
                      GLsizei width, GLsizei height, GLint border,
                      GLenum format, GLenum type, const void *pixels)
{
    (void)target; (void)border; (void)type;

    ps3gl_texture_t *t = ps3gl.tmu[ps3gl.active_tmu].bound;
    if (!t || width <= 0 || height <= 0) return;

    if (level == 0) {
        /* Log first 30 texture uploads for diagnostics */
        if (ps3gl_teximg_diag_count < 30) {
            char dbg[128];
            snprintf(dbg, sizeof(dbg),
                     "PS3GL_TEXIMG: tex=%d %dx%d intfmt=0x%x fmt=0x%x type=0x%x px=%p",
                     t->glname, (int)width, (int)height,
                     (unsigned)internalformat, (unsigned)format,
                     (unsigned)type, pixels);
            ps3_log(dbg);
            ps3gl_teximg_diag_count++;
        }

        /* Pow2 textures get a swizzled full mip chain; non-pow2 (and
         * anything that has received TexSubImage2D, i.e. cinematics)
         * stays linear, base level only. */
        int want_swz = is_pow2(width) && is_pow2(height) && !t->sub_updated;
        int levels   = want_swz ? mip_chain_levels(width, height) : 1;

        uint32_t lvl_off[PS3GL_TEX_MAX_LEVELS];
        uint32_t total = 0;
        int w = width, h = height;
        for (int i = 0; i < levels; i++) {
            lvl_off[i] = total;
            total += (uint32_t)(w * h * 4);
            w = (w > 1) ? (w >> 1) : 1;
            h = (h > 1) ? (h >> 1) : 1;
        }

        /* Free old data if dimensions or layout changed */
        if (t->data && (t->width != (uint16_t)width ||
                        t->height != (uint16_t)height ||
                        (int)t->swizzled != want_swz ||
                        t->total_size != total)) {
            rsxFree(t->data);
            t->data = NULL;
        }

        if (!t->data) {
            t->data = (uint8_t *)rsxMemalign(128, total);
            if (!t->data && want_swz) {
                /* Mip chain didn't fit -- degrade to linear base level */
                want_swz = 0;
                levels = 1;
                total = (uint32_t)(width * height * 4);
                lvl_off[0] = 0;
                t->data = (uint8_t *)rsxMemalign(128, total);
            }
            if (!t->data) {
                printf("[ps3gl] WARNING: failed to allocate %u bytes for texture %d\n",
                       total, t->glname);
                return;
            }
            rsxAddressToOffset(t->data, &t->offset);
        }

        t->width      = (uint16_t)width;
        t->height     = (uint16_t)height;
        t->bpp        = 4; /* always store as ARGB8888 */
        t->swizzled   = (uint8_t)want_swz;
        t->total_size = total;
        memcpy(t->level_offset, lvl_off, sizeof(lvl_off));
        t->num_levels = 1; /* restart the chain; grows as levels arrive */
    } else {
        /* Mip level upload: only valid for swizzled (pow2) textures and
         * only when dimensions match the expected chain level. */
        if (!t->data || !t->swizzled) return;
        if (level >= mip_chain_levels(t->width, t->height)) return;
        int ew = (t->width  >> level) > 1 ? (t->width  >> level) : 1;
        int eh = (t->height >> level) > 1 ? (t->height >> level) : 1;
        if (width != ew || height != eh) return;
    }

    uint8_t *dst = t->data + t->level_offset[level];
    uint32_t lvl_size = (uint32_t)(width * height * 4);

    if (pixels) {
        int src_bpp = gl_format_bpp(format);
        if (t->swizzled) {
            /* Convert into a temporary linear buffer, then swizzle into
             * VRAM (sequential VRAM writes; no read-modify-write). */
            uint8_t *tmp = (uint8_t *)malloc(lvl_size);
            if (!tmp) {
                if (level > 0) return; /* skip this mip, chain stays short */
                /* Base level must not be lost: degrade to linear in place
                 * (allocation is chain-sized, base level always fits). */
                t->swizzled   = 0;
                t->num_levels = 1;
                convert_pixels(dst, (const uint8_t *)pixels,
                               width, height, src_bpp, format);
            } else {
                convert_pixels(tmp, (const uint8_t *)pixels,
                               width, height, src_bpp, format);
                swizzle_argb8((uint32_t *)dst, (const uint32_t *)tmp,
                              width, height);
                free(tmp);
                if (level + 1 > t->num_levels)
                    t->num_levels = (uint8_t)(level + 1);
            }
        } else {
            convert_pixels(dst, (const uint8_t *)pixels,
                           width, height, src_bpp, format);
        }
    } else {
        memset(dst, 0, lvl_size);
        if (t->swizzled && level + 1 > t->num_levels)
            t->num_levels = (uint8_t)(level + 1);
    }

    build_gcm_texture(t);
    invalidate_texture_cache();
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

    /* Sub-updates into a swizzled layout would scatter writes Morton-order
     * through write-combined VRAM every frame (cinematics). Convert the
     * texture to linear once; the chain-sized allocation is reused since
     * the base level always fits at offset 0. */
    if (t->swizzled) {
        if (xoff != 0 || yoff != 0 || w != t->width || h != t->height) {
            /* Partial update: preserve existing texels by deswizzling
             * level 0 through a temporary linear buffer (one-time cost). */
            uint32_t base_size = (uint32_t)t->width * t->height * 4;
            uint32_t *tmp = (uint32_t *)malloc(base_size);
            if (!tmp) return; /* can't convert safely; drop the update */
            deswizzle_argb8(tmp, (const uint32_t *)t->data,
                            t->width, t->height);
            memcpy(t->data, tmp, base_size);
            free(tmp);
        }
        /* Full-rect update overwrites everything; just flip the layout. */
        t->swizzled   = 0;
        t->num_levels = 1;
        build_gcm_texture(t);
    }
    t->sub_updated = 1;

    int src_bpp = gl_format_bpp(format);
    const uint8_t *src = (const uint8_t *)pixels;

    /* Fast path: full-width, in-bounds update (cinematics upload the
     * whole frame). Rows are contiguous in source and dest, so the whole
     * rect converts in one sequential pass -- no per-pixel bounds checks. */
    if (xoff == 0 && w == t->width && h > 0 &&
        yoff >= 0 && yoff + h <= t->height) {
        convert_pixels(t->data + (uint32_t)yoff * t->width * 4,
                       src, w, h, src_bpp, format);
        invalidate_texture_cache();
        t->dirty = 1;
        return;
    }

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

    /* RSX texture cache can hold stale tiles after an in-place update */
    invalidate_texture_cache();
    t->dirty = 1;
}

/* ----------------------------------------------------------------
 * Texture parameters
 * ---------------------------------------------------------------- */

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

/* ----------------------------------------------------------------
 * TexEnv
 * ---------------------------------------------------------------- */

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

/* ----------------------------------------------------------------
 * Multitexture
 * ---------------------------------------------------------------- */

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

/* ----------------------------------------------------------------
 * Apply textures to RSX before draw
 * ---------------------------------------------------------------- */

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

        ps3gl_stats_cur.tex_binds++;
        rsxLoadTexture(ctx, i, &t->gcm_tex);
        /* min/max LOD clamps are 4.8 fixed point; maxlod must cover the
         * mip chain or the RSX never samples below level 0 */
        uint16_t maxlod = (uint16_t)(((t->num_levels > 0 ? t->num_levels : 1) - 1) << 8);
        rsxTextureControl(ctx, i, GCM_TRUE, 0, maxlod, GCM_TEXTURE_MAX_ANISO_1);
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
