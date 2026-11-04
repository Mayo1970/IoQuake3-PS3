/* ps3gl_shaders.c -- GL-to-RSX layer: shader program management. One shared vertex program (q3_vp, or q3_vp_gen
 * under ps3_vprog 1), one fragment program per texenv mode. PS3GL_SHADERS_AVAILABLE==0: every draw is a silent no-op. */

#include "ps3gl.h"
#include "ps3gl_shader_data.h"
#include <stddef.h>
#include <stdio.h>

/* Forward declarations */
extern const float *ps3gl_get_mvp(void);
extern uint32_t ps3gl_get_mvp_generation(void);

#if PS3GL_SHADERS_AVAILABLE

/* q3_vp_gen's uniforms: one table, so the names cost one TOC entry, not one each */
#define VPU(name, field, n) { name, offsetof(ps3gl_vparams_t, field), n }
static const struct { const char *name; size_t off; int len; } vp_uniforms[PS3GL_VP_UNIFORMS] = {
    VPU("cmul", cmul, 4),      VPU("cadd", cadd, 4),
    VPU("tm0", tm[0], 16),     VPU("tm1", tm[1], 16),
    VPU("tsel", tsel, 4),
    VPU("turb0", turb[0], 4),  VPU("turb1", turb[1], 4),
    VPU("turbm0", turbm[0], 4), VPU("turbm1", turbm[1], 4),
    VPU("csel", csel, 4),      VPU("lamb", lamb, 4),   VPU("lambi", lambi, 4),
    VPU("ldir", ldir, 4),      VPU("ldirc", ldirc, 4), VPU("vorg", vorg, 4), VPU("porg", porg, 4),
    VPU("dfa0", df[0][0], 4),  VPU("dfb0", df[0][4], 4), VPU("dfc0", df[0][8], 4), VPU("dfm0", df[0][12], 4),
    VPU("dfa1", df[1][0], 4),  VPU("dfb1", df[1][4], 4), VPU("dfc1", df[1][8], 4), VPU("dfm1", df[1][12], 4),
    VPU("dfa2", df[2][0], 4),  VPU("dfb2", df[2][4], 4), VPU("dfc2", df[2][8], 4), VPU("dfm2", df[2][12], 4),
};
#undef VPU

/* Shader loading helpers */

static void load_vp(ps3gl_shader_t *s, const void *data, uint32_t size)
{
    s->vp = (rsxVertexProgram *)data;
    rsxVertexProgramGetUCode(s->vp, &s->vp_ucode, &s->vp_ucode_size);
}

static void load_fp(ps3gl_shader_t *s, const void *data, uint32_t size)
{
    s->fp = (rsxFragmentProgram *)data;

    void *ucode;
    rsxFragmentProgramGetUCode(s->fp, &ucode, &s->fp_ucode_size);

    /* Fragment program ucode must reside in RSX-accessible memory */
    s->fp_ucode = rsxMemalign(64, s->fp_ucode_size);
    if (s->fp_ucode) {
        memcpy(s->fp_ucode, ucode, s->fp_ucode_size);
        rsxAddressToOffset(s->fp_ucode, &s->fp_offset);
    }
}

void ps3gl_shaders_init(void)
{
    printf("[ps3gl] Loading compiled shaders...\n");

    /* All shader slots share the same vertex program */
    for (int i = 0; i < PS3GL_TENV_COUNT; i++) {
        memset(&ps3gl.shaders[i], 0, sizeof(ps3gl_shader_t));
        load_vp(&ps3gl.shaders[i], shader_vp_data, shader_vp_data_size);
        ps3gl.shaders[i].mvp_const = rsxVertexProgramGetConst(ps3gl.shaders[i].vp, "mvp");
    }

    /* Load per-mode fragment programs */
    load_fp(&ps3gl.shaders[PS3GL_TENV_DISABLED], shader_fp_coloronly_data, shader_fp_coloronly_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_MODULATE], shader_fp_modulate_data, shader_fp_modulate_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_REPLACE],  shader_fp_replace_data,  shader_fp_replace_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_DECAL],    shader_fp_decal_data,    shader_fp_decal_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_ADD],       shader_fp_add_data,      shader_fp_add_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_BLEND],     shader_fp_blend_data,    shader_fp_blend_data_size);
    load_fp(&ps3gl.shaders[PS3GL_TENV_MODULATE2], shader_fp_modulate2_data, shader_fp_modulate2_data_size);

    /* ps3_vprog 1's VP: any missing uniform keeps ps3_vprog off rather than drawing garbage */
    {
        ps3gl_vpgen_t *g = &ps3gl.vpgen;
        int ok;

        memset(g, 0, sizeof(*g));
        g->vp = (rsxVertexProgram *)shader_vp_gen_data;
        rsxVertexProgramGetUCode(g->vp, &g->ucode, &g->ucode_size);
        g->mvp = rsxVertexProgramGetConst(g->vp, "mvp");
        ok = g->mvp != NULL;
        for (int i = 0; i < PS3GL_VP_UNIFORMS; i++) {
            g->u[i] = rsxVertexProgramGetConst(g->vp, vp_uniforms[i].name);
            ok = ok && g->u[i];
        }
        if (!ok) {
            printf("[ps3gl] WARNING: q3_vp_gen uniforms missing, ps3_vprog disabled\n");
            g->vp = NULL;
        }
    }

    ps3gl.active_shader = -1;
    ps3gl.active_vp = NULL;
    ps3gl.loaded_vp = NULL;
    ps3gl.mvp_uploaded_gen = 0;
    ps3gl.vp_up_valid = 0;
    printf("[ps3gl] Shaders loaded: %d modes\n", PS3GL_TENV_COUNT);
}

void ps3gl_shaders_shutdown(void)
{
    for (int i = 0; i < PS3GL_TENV_COUNT; i++) {
        if (ps3gl.shaders[i].fp_ucode) {
            rsxFree(ps3gl.shaders[i].fp_ucode);
            ps3gl.shaders[i].fp_ucode = NULL;
        }
    }
}

int ps3gl_shader_key(void)
{
    int tmu0 = ps3gl.tmu[0].enabled && ps3gl.tmu[0].bound && ps3gl.tmu[0].bound->data;
    int tmu1 = ps3gl.tmu[1].enabled && ps3gl.tmu[1].bound && ps3gl.tmu[1].bound->data;

    /* Both TMUs active: use dual-texture modulate (diffuse * lightmap). */
    if (tmu0 && tmu1)
        return PS3GL_TENV_MODULATE2;

    if (tmu0)
        return ps3gl.tmu[0].texenv;

    return PS3GL_TENV_DISABLED;
}

void ps3gl_apply_shader(void)
{
    int key = ps3gl_shader_key();
    if (key < 0 || key >= PS3GL_TENV_COUNT) key = PS3GL_TENV_DISABLED;

    ps3gl_shader_t *s = &ps3gl.shaders[key];
    gcmContextData *ctx = ps3gl_get_ctx();
    if (!ctx) return;

    /* All FP slots share one VP: q3_vp, or q3_vp_gen under ps3_vprog 1. Reload it only when it
     * is not bound; a different VP may keep mvp in other registers, so re-upload then. */
    rsxVertexProgram *vp = s->vp;
    void *vp_ucode = s->vp_ucode;
    rsxProgramConst *mvp_const = s->mvp_const;
    if (ps3gl_VProgOn()) {
        vp = ps3gl.vpgen.vp;
        vp_ucode = ps3gl.vpgen.ucode;
        mvp_const = ps3gl.vpgen.mvp;
    }
    if (vp != ps3gl.active_vp) {
        rsxLoadVertexProgram(ctx, vp, vp_ucode);
        if (vp != ps3gl.loaded_vp) {
            ps3gl.loaded_vp = vp;
            ps3gl.mvp_uploaded_gen = 0;
        }
        ps3gl.active_vp = vp;
        ps3gl.vp_up_valid = 0;
    }

    if (key != ps3gl.active_shader) {
        if (s->fp_ucode) {
            rsxLoadFragmentProgramLocation(ctx, s->fp, s->fp_offset,
                                           GCM_LOCATION_RSX);
        }

        ps3gl.active_shader = key;
    }

    /* MVP constant is independent of which FP is bound -- only re-patch it
     * when it changed since the last upload (patching RSX microcode isn't free). */
    uint32_t mvp_gen = ps3gl_get_mvp_generation();
    if (mvp_const && mvp_gen != ps3gl.mvp_uploaded_gen) {
        const float *mvp = ps3gl_get_mvp();
        rsxSetVertexProgramParameter(ctx, vp, mvp_const, mvp);
        ps3gl.mvp_uploaded_gen = mvp_gen;
    }

    /* Upload the world-space clip plane only if this shader binary was recompiled to support
     * it (clip_plane_const is NULL on the old pre-clip-plane binary -- software clip in ps3gl_draw.c covers that case). */
    if (s->clip_plane_const) {
        rsxSetVertexProgramParameter(ctx, s->vp, s->clip_plane_const,
                                     ps3gl.clip_plane);
    }
}

/* Identity: TEXCOORD0 = ATTR8, TEXCOORD1 = ATTR9, COLOR0 = ATTR3, every select 0: q3_vp_gen draws as q3_vp */
static const ps3gl_vparams_t vp_identity = {
    .cmul = { 1.0f, 1.0f, 1.0f, 1.0f },
    .tm = { { 1, 0, 0, 0, 0, 0, 0, 0,  0, 1, 0, 0, 0, 0, 0, 0 },
            { 0, 0, 1, 0, 0, 0, 0, 0,  0, 0, 0, 1, 0, 0, 0, 0 } },
};

/* Upload only the uniforms that differ from what the RSX holds: each one is a command-buffer write. */
void ps3gl_apply_vparams(const ps3gl_vparams_t *p)
{
    gcmContextData *ctx = ps3gl_get_ctx();
    ps3gl_vpgen_t *g = &ps3gl.vpgen;

    if (!ctx || !g->vp || ps3gl.active_vp != g->vp) return;
    if (!p) p = &vp_identity;

    for (int i = 0; i < PS3GL_VP_UNIFORMS; i++) {
        const float *src = (const float *)((const char *)p + vp_uniforms[i].off);
        float *up = (float *)((char *)&ps3gl.vp_up + vp_uniforms[i].off);
        size_t bytes = vp_uniforms[i].len * sizeof(float);

        if (ps3gl.vp_up_valid && !memcmp(up, src, bytes))
            continue;
        rsxSetVertexProgramParameter(ctx, g->vp, g->u[i], src);
        memcpy(up, src, bytes);
    }
    ps3gl.vp_up_valid = 1;
}

void ps3gl_SetVProg(int on)
{
    if ((uintptr_t)ps3gl_ptr > 0x1000)
        ps3gl.vprog = on;
}

int ps3gl_VProgOn(void)
{
    return (uintptr_t)ps3gl_ptr > 0x1000 && ps3gl.vprog && ps3gl.vpgen.vp;
}

void ps3gl_VertexParams(const ps3gl_vparams_t *p)
{
    ps3gl.vp_next = *p;
    ps3gl.vp_pending = 1;
}

#else /* PS3GL_SHADERS_AVAILABLE == 0 */

/* Stub implementations when shaders are not compiled */

void ps3gl_shaders_init(void)
{
    printf("[ps3gl] WARNING: No compiled shaders available.\n");
    printf("[ps3gl] Build ps3toolchain, then run: cd code/gl/shaders && ./compile_shaders.sh\n");
    ps3gl.active_shader = -1;
}

void ps3gl_shaders_shutdown(void) {}

int ps3gl_shader_key(void) { return PS3GL_TENV_DISABLED; }

void ps3gl_apply_shader(void)
{
    /* No shaders to apply -- draws will produce no visible output */
}

void ps3gl_apply_vparams(const ps3gl_vparams_t *p) { (void)p; }
void ps3gl_SetVProg(int on) { (void)on; }
int  ps3gl_VProgOn(void) { return 0; }
void ps3gl_VertexParams(const ps3gl_vparams_t *p) { (void)p; }

#endif /* PS3GL_SHADERS_AVAILABLE */
