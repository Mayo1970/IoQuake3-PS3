/* ps3_renderer.c -- Renderer glue layer. */

#include <stdio.h>
#include <string.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "renderercommon/tr_types.h"
#include "renderercommon/tr_public.h"
#include "renderergl1/tr_local.h"
#include "../sys/ps3_glimp.h"
#include "../gl/ps3gl_vprog.h"

extern void ps3_log(const char *msg);

/* Global refimport_t -- tr_main.c's definition is renamed by our patches. */
refimport_t ri;

/* 1 (default): q3_vp_gen (VERTEX_OFFLOAD_TODO.md V1-V5). 0: the old path (q3_vp, ComputeColors/ComputeTexCoords). */
static cvar_t *ps3_vprog;

/* Pre-boot stubs -- no-ops to prevent crashes before Com_Init. */
static void stub_Shutdown(qboolean destroyWindow)       { (void)destroyWindow; }
static void stub_BeginRegistration(glconfig_t *config)   { (void)config; }
static qhandle_t stub_RegisterModel(const char *n)       { (void)n; return 0; }
static qhandle_t stub_RegisterSkin(const char *n)        { (void)n; return 0; }
static qhandle_t stub_RegisterShader(const char *n)      { (void)n; return 0; }
static qhandle_t stub_RegisterShaderNoMip(const char *n) { (void)n; return 0; }
static void stub_LoadWorld(const char *n)                { (void)n; }
static void stub_SetWorldVisData(const byte *d, int l)   { (void)d; (void)l; }
static void stub_EndRegistration(void) {}
static void stub_ClearScene(void) {}
static void stub_AddRefEntityToScene(const refEntity_t *e) { (void)e; }
static void stub_AddPolyToScene(qhandle_t h, int n, const polyVert_t *v, int nv)
    { (void)h; (void)n; (void)v; (void)nv; }
static int  stub_LightForPoint(vec3_t p, vec3_t a, vec3_t d, vec3_t di)
    { (void)p; (void)a; (void)d; (void)di; return 0; }
static void stub_AddLightToScene(const vec3_t o, float i, float r, float g, float b)
    { (void)o; (void)i; (void)r; (void)g; (void)b; }
static void stub_AddAdditiveLightToScene(const vec3_t o, float i, float r, float g, float b)
    { (void)o; (void)i; (void)r; (void)g; (void)b; }
static void stub_RenderScene(const refdef_t *fd) { (void)fd; }
static void stub_SetColor(const float *rgba) { (void)rgba; }
static void stub_DrawStretchPic(float x, float y, float w, float h,
    float s1, float t1, float s2, float t2, qhandle_t sh)
    { (void)x; (void)y; (void)w; (void)h; (void)s1; (void)t1; (void)s2; (void)t2; (void)sh; }
static void stub_DrawStretchRaw(int x, int y, int w, int h, int c, int r, const byte *d, int cl, qboolean dirty)
    { (void)x; (void)y; (void)w; (void)h; (void)c; (void)r; (void)d; (void)cl; (void)dirty; }
static void stub_UploadCinematic(int w, int h, int c, int r, const byte *d, int cl, qboolean dirty)
    { (void)w; (void)h; (void)c; (void)r; (void)d; (void)cl; (void)dirty; }
static void stub_BeginFrame(stereoFrame_t s) { (void)s; }
static void stub_EndFrame(int *f, int *b) { (void)f; (void)b; }
static int  stub_MarkFragments(int n, const vec3_t *p, const vec3_t pr,
    int mf, vec3_t mp, int mm, markFragment_t *mfr)
    { (void)n; (void)p; (void)pr; (void)mf; (void)mp; (void)mm; (void)mfr; return 0; }
static int  stub_LerpTag(orientation_t *t, qhandle_t m, int sf, int ef, float f, const char *tn)
    { (void)t; (void)m; (void)sf; (void)ef; (void)f; (void)tn; return 0; }
static void stub_ModelBounds(qhandle_t m, vec3_t mn, vec3_t mx)
    { (void)m; (void)mn; (void)mx; }
static void stub_RegisterFont(const char *fn, int ps, fontInfo_t *fi)
    { (void)fn; (void)ps; (void)fi; }
static void stub_RemapShader(const char *o, const char *n, const char *t)
    { (void)o; (void)n; (void)t; }
static qboolean stub_GetEntityToken(char *b, int s)
    { (void)b; (void)s; return qfalse; }
static qboolean stub_inPVS(const vec3_t a, const vec3_t b)
    { (void)a; (void)b; return qfalse; }
static void stub_TakeVideoFrame(int w, int h, byte *c, byte *d, qboolean m)
    { (void)w; (void)h; (void)c; (void)d; (void)m; }

static refexport_t ps3_stub_re = {0};

static refexport_t *PS3_StubRefExport(void)
{
    ps3_stub_re.Shutdown           = stub_Shutdown;
    ps3_stub_re.BeginRegistration  = stub_BeginRegistration;
    ps3_stub_re.RegisterModel      = stub_RegisterModel;
    ps3_stub_re.RegisterSkin       = stub_RegisterSkin;
    ps3_stub_re.RegisterShader     = stub_RegisterShader;
    ps3_stub_re.RegisterShaderNoMip = stub_RegisterShaderNoMip;
    ps3_stub_re.LoadWorld          = stub_LoadWorld;
    ps3_stub_re.SetWorldVisData    = stub_SetWorldVisData;
    ps3_stub_re.EndRegistration    = stub_EndRegistration;
    ps3_stub_re.ClearScene         = stub_ClearScene;
    ps3_stub_re.AddRefEntityToScene = stub_AddRefEntityToScene;
    ps3_stub_re.AddPolyToScene     = stub_AddPolyToScene;
    ps3_stub_re.LightForPoint      = stub_LightForPoint;
    ps3_stub_re.AddLightToScene    = stub_AddLightToScene;
    ps3_stub_re.AddAdditiveLightToScene = stub_AddAdditiveLightToScene;
    ps3_stub_re.RenderScene        = stub_RenderScene;
    ps3_stub_re.SetColor           = stub_SetColor;
    ps3_stub_re.DrawStretchPic     = stub_DrawStretchPic;
    ps3_stub_re.DrawStretchRaw     = stub_DrawStretchRaw;
    ps3_stub_re.UploadCinematic    = stub_UploadCinematic;
    ps3_stub_re.BeginFrame         = stub_BeginFrame;
    ps3_stub_re.EndFrame           = stub_EndFrame;
    ps3_stub_re.MarkFragments      = stub_MarkFragments;
    ps3_stub_re.LerpTag            = stub_LerpTag;
    ps3_stub_re.ModelBounds        = stub_ModelBounds;
    ps3_stub_re.RegisterFont       = stub_RegisterFont;
    ps3_stub_re.RemapShader        = stub_RemapShader;
    ps3_stub_re.GetEntityToken     = stub_GetEntityToken;
    ps3_stub_re.inPVS              = stub_inPVS;
    ps3_stub_re.TakeVideoFrame     = stub_TakeVideoFrame;
    return &ps3_stub_re;
}

/* GetRefAPI -- real entry point is renamed to tr_init_GetRefAPI_unused. */
extern refexport_t *tr_init_GetRefAPI_unused(int apiVersion, refimport_t *rimp);

extern void QGL_Init(void);
extern void PS3_RSX_BeginFrame(void);

static void (*upstream_BeginFrame)(stereoFrame_t stereoFrame) = NULL;

static void PS3_RE_BeginFrame(stereoFrame_t stereoFrame)
{
    PS3_RSX_BeginFrame();
    ps3gl_SetVProg(ps3_vprog && ps3_vprog->integer);
    if (upstream_BeginFrame)
        upstream_BeginFrame(stereoFrame);
}

refexport_t *GetRefAPI(int apiVersion, refimport_t *rimp)
{
    if (!rimp) {
        ps3_log("GetRefAPI: pre-boot stub");
        return PS3_StubRefExport();
    }

    ri = *rimp;
    ps3_log("GetRefAPI: real init");
    ps3_vprog = ri.Cvar_Get("ps3_vprog", "1", 0);

    QGL_Init();

    refexport_t *re = tr_init_GetRefAPI_unused(apiVersion, rimp);

    /* Inject PS3_RSX_BeginFrame before each frame */
    upstream_BeginFrame = re->BeginFrame;
    re->BeginFrame = PS3_RE_BeginFrame;

    return re;
}

/* ps3_vprog 1 (VERTEX_OFFLOAD_TODO.md V1-V5): surface deforms and stage colors / texcoords folded into
 * q3_vp_gen's uniforms. Each helper mirrors tr_shade.c / tr_shade_calc.c; qfalse leaves the PPE path. */

/* The current surface's deforms: armed by RB_PS3_SurfaceDeforms, sent with every stage of it */
static struct {
    qboolean active;
    qboolean normal;            /* a slot moves along tess.normal */
    float    df[3][16];
} ps3Deform;

/* EvalWaveForm (static in tr_shade_calc.c); qfalse on a bad func, which EvalWaveForm drops on */
static qboolean PS3_WaveValue(const waveForm_t *wf, float *out)
{
    const float *table;

    if (wf->func == GF_NOISE) {
        *out = wf->base + R_NoiseGet4f(0, 0, 0, (tess.shaderTime + wf->phase) * wf->frequency) * wf->amplitude;
        return qtrue;
    }
    switch (wf->func) {
    case GF_SIN:              table = tr.sinTable; break;
    case GF_TRIANGLE:         table = tr.triangleTable; break;
    case GF_SQUARE:           table = tr.squareTable; break;
    case GF_SAWTOOTH:         table = tr.sawToothTable; break;
    case GF_INVERSE_SAWTOOTH: table = tr.inverseSawToothTable; break;
    default:                  return qfalse;
    }
    *out = wf->base + table[((int64_t)((wf->phase + tess.shaderTime * wf->frequency) * FUNCTABLE_SIZE))
                            & FUNCTABLE_MASK] * wf->amplitude;
    return qtrue;
}

/* q3_vp_gen's waveval() weights: b = (c, sin, square, triangle), c = (saw, invsaw, on) */
static qboolean PS3_WaveWeights(genFunc_t func, float *b, float *c)
{
    switch (func) {
    case GF_SIN:              b[1] = 1.0f; return qtrue;
    case GF_SQUARE:           b[2] = 1.0f; return qtrue;
    case GF_TRIANGLE:         b[3] = 1.0f; return qtrue;
    case GF_SAWTOOTH:         c[0] = 1.0f; return qtrue;
    case GF_INVERSE_SAWTOOTH: c[1] = 1.0f; return qtrue;
    default:                  return qfalse;   /* noise varies per vertex */
    }
}

/* RB_CalcDiffuseColor's inputs; the unlit side uses the byte-packed ambient, as it does */
static void PS3_DiffuseParams(const trRefEntity_t *ent, ps3gl_vparams_t *p)
{
    int i;

    p->csel[0] = 1.0f;
    for (i = 0; i < 3; i++) {
        p->lamb[i] = ent->ambientLight[i] / 255.0f;
        p->lambi[i] = ((const byte *)&ent->ambientLightInt)[i] / 255.0f;
        p->ldir[i] = ent->lightDir[i];
        p->ldirc[i] = ent->directedLight[i] / 255.0f;
    }
}

/* COLOR0 channel = vertex * mul + add (+ the csel terms). Constants are bytes, as in ComputeColors, then b / 255. */
static qboolean PS3_ColorParams(const shaderStage_t *pStage, ps3gl_vparams_t *p)
{
    enum { K_CONST, K_VERTEX, K_ONE_MINUS_VERTEX } kind = K_CONST;
    const trRefEntity_t *ent = backEnd.currentEntity;
    float *mul = p->cmul, *add = p->cadd;
    byte k[4] = { 255, 255, 255, 255 };
    float scale = 1.0f, glow;
    int i, v;

    switch (pStage->rgbGen) {
    case CGEN_IDENTITY:
        break;
    case CGEN_EXACT_VERTEX:
        kind = K_VERTEX;
        break;
    case CGEN_VERTEX:
        kind = K_VERTEX;
        scale = tr.identityLight;
        break;
    case CGEN_ONE_MINUS_VERTEX:
        kind = K_ONE_MINUS_VERTEX;
        scale = tr.identityLight;
        break;
    case CGEN_CONST:
        Com_Memcpy(k, pStage->constantColor, 4);
        break;
    case CGEN_FOG:
        Com_Memcpy(k, &tr.world->fogs[tess.fogNum].colorInt, 4);
        break;
    case CGEN_WAVEFORM:
        /* RB_CalcWaveColor: identityLight scales every func but noise */
        if (!PS3_WaveValue(&pStage->rgbWave, &glow))
            return qfalse;
        if (pStage->rgbWave.func != GF_NOISE)
            glow *= tr.identityLight;
        glow = glow < 0 ? 0 : (glow > 1 ? 1 : glow);
        k[0] = k[1] = k[2] = ri.ftol(255 * glow);
        break;
    case CGEN_ENTITY:
    case CGEN_ONE_MINUS_ENTITY:
        if (!ent)
            return qfalse;
        for (i = 0; i < 4; i++) {
            k[i] = ent->e.shaderRGBA[i];
            if (pStage->rgbGen == CGEN_ONE_MINUS_ENTITY)
                k[i] = 255 - k[i];
        }
        break;
    case CGEN_LIGHTING_DIFFUSE:
        /* rgb comes from the VP's lighting term, alpha is 255 */
        if (!ent)
            return qfalse;
        k[0] = k[1] = k[2] = 0;
        PS3_DiffuseParams(ent, p);
        break;
    default:            /* CGEN_IDENTITY_LIGHTING, CGEN_BAD: ComputeColors memsets all four bytes */
        k[0] = k[1] = k[2] = k[3] = tr.identityLightByte;
        break;
    }

    for (i = 0; i < 4; i++) {
        if (kind == K_CONST) {
            mul[i] = 0.0f;
            add[i] = k[i] / 255.0f;
        } else if (kind == K_VERTEX) {
            mul[i] = i < 3 ? scale : 1.0f;  /* alpha is copied unscaled */
            add[i] = 0.0f;
        } else {
            mul[i] = -scale;
            add[i] = scale;
        }
    }

    switch (pStage->alphaGen) {
    case AGEN_SKIP:
        /* one-minus-vertex leaves the previous stage's alpha in svars, which the VP cannot see */
        if (kind == K_ONE_MINUS_VERTEX)
            return qfalse;
        break;
    case AGEN_IDENTITY:
        /* ComputeColors keeps the memcpy'd vertex alpha for rgbGen vertex at identityLight 1 */
        if (pStage->rgbGen == CGEN_VERTEX && tr.identityLight == 1)
            break;
        mul[3] = 0.0f;
        add[3] = 1.0f;
        break;
    case AGEN_CONST:
        mul[3] = 0.0f;
        add[3] = pStage->constantColor[3] / 255.0f;
        break;
    case AGEN_WAVEFORM:
        /* RB_CalcWaveAlpha: clamped, then truncated to an int */
        if (!PS3_WaveValue(&pStage->alphaWave, &glow))
            return qfalse;
        glow = glow < 0 ? 0 : (glow > 1 ? 1 : glow);
        v = 255 * glow;
        mul[3] = 0.0f;
        add[3] = v / 255.0f;
        break;
    case AGEN_ENTITY:
    case AGEN_ONE_MINUS_ENTITY:
        if (!ent)
            return qfalse;
        v = ent->e.shaderRGBA[3];
        if (pStage->alphaGen == AGEN_ONE_MINUS_ENTITY)
            v = 255 - v;
        mul[3] = 0.0f;
        add[3] = v / 255.0f;
        break;
    case AGEN_VERTEX:
        mul[3] = 1.0f;
        add[3] = 0.0f;
        break;
    case AGEN_ONE_MINUS_VERTEX:
        mul[3] = -1.0f;
        add[3] = 1.0f;
        break;
    case AGEN_LIGHTING_SPECULAR:
        mul[3] = add[3] = 0.0f;
        p->csel[1] = 1.0f;
        VectorCopy(backEnd.or.viewOrigin, p->vorg);
        break;
    case AGEN_PORTAL:
        /* the PPE path divides by portalRange */
        if (tess.shader->portalRange <= 0)
            return qfalse;
        mul[3] = add[3] = 0.0f;
        p->csel[2] = 1.0f;
        VectorCopy(backEnd.viewParms.or.origin, p->porg);
        p->porg[3] = 1.0f / tess.shader->portalRange;
        break;
    default:
        return qfalse;
    }

    p->csel[3] = p->csel[0] + p->csel[1] + p->csel[2];
    return qtrue;
}

/* (s, t) -> (s * m00 + t * m10 + tx, s * m01 + t * m11 + ty), on rows s[8], t[8] over (st, lm, xyz, 1).
 * L, once a turb is seen, is the 2x2 that the turb offset still goes through. */
static void PS3_TexAffine(float *s, float *t, float *L, float m00, float m10, float tx, float m01, float m11, float ty)
{
    float ns, nt, l0, l1;
    int k;

    for (k = 0; k < 8; k++) {
        ns = s[k] * m00 + t[k] * m10;
        nt = s[k] * m01 + t[k] * m11;
        s[k] = ns;
        t[k] = nt;
    }
    s[7] += tx;
    t[7] += ty;
    if (L) {
        l0 = L[0];
        l1 = L[1];
        L[0] = m00 * l0 + m10 * L[2];
        L[1] = m00 * l1 + m10 * L[3];
        L[2] = m01 * l0 + m11 * L[2];
        L[3] = m01 * l1 + m11 * L[3];
    }
}

/* RB_CalcScrollTexCoords: the offset wraps to [0, 1) in double */
static void PS3_TexScroll(float *s, float *t, const float speed[2])
{
    double ds = speed[0] * tess.shaderTime, dt = speed[1] * tess.shaderTime;

    s[7] += ds - floor(ds);
    t[7] += dt - floor(dt);
}

/* tcGen as rows, then every tcMod composed onto them in shader order; one turb per bundle */
static qboolean PS3_BundleParams(const textureBundle_t *bundle, int b, ps3gl_vparams_t *p)
{
    float *s = p->tm[b], *t = p->tm[b] + 8, *L = NULL, q;
    double now;
    int i;

    switch (bundle->tcGen) {
    case TCGEN_IDENTITY:
        break;
    case TCGEN_TEXTURE:
        s[0] = 1.0f;
        t[1] = 1.0f;
        break;
    case TCGEN_LIGHTMAP:
        s[2] = 1.0f;
        t[3] = 1.0f;
        break;
    case TCGEN_VECTOR:
        VectorCopy(bundle->tcGenVectors[0], s + 4);
        VectorCopy(bundle->tcGenVectors[1], t + 4);
        break;
    case TCGEN_ENVIRONMENT_MAPPED:
        /* the VP's env coords take lm's place for this bundle */
        s[2] = 1.0f;
        t[3] = 1.0f;
        p->tsel[b] = 1.0f;
        VectorCopy(backEnd.or.viewOrigin, p->vorg);
        break;
    default:            /* fog (V6); TCGEN_BAD stops ComputeTexCoords */
        return qfalse;
    }

    for (i = 0; i < bundle->numTexMods; i++) {
        const texModInfo_t *tmi = &bundle->texMods[i];

        switch (tmi->type) {
        case TMOD_NONE:
            i = TR_MAX_TEXMODS;
            break;
        case TMOD_TURBULENT:
            /* RB_CalcTurbulentTexCoords: func and base unused; a second turb stays on the PPE */
            if (L)
                return qfalse;
            L = p->turbm[b];
            L[0] = L[3] = 1.0f;
            p->tsel[2 + b] = 1.0f;
            now = tmi->wave.phase + tess.shaderTime * tmi->wave.frequency;
            p->turb[b][0] = tmi->wave.amplitude;
            p->turb[b][1] = now - floor(now);
            break;
        case TMOD_ENTITY_TRANSLATE:
            PS3_TexScroll(s, t, backEnd.currentEntity->e.shaderTexCoord);
            break;
        case TMOD_SCROLL:
            PS3_TexScroll(s, t, tmi->scroll);
            break;
        case TMOD_SCALE:
            PS3_TexAffine(s, t, L, tmi->scale[0], 0.0f, 0.0f, 0.0f, tmi->scale[1], 0.0f);
            break;
        case TMOD_STRETCH:
            if (!PS3_WaveValue(&tmi->wave, &q))
                return qfalse;
            q = 1.0f / q;
            PS3_TexAffine(s, t, L, q, 0.0f, 0.5f - 0.5f * q, 0.0f, q, 0.5f - 0.5f * q);
            break;
        case TMOD_TRANSFORM:
            PS3_TexAffine(s, t, L, tmi->matrix[0][0], tmi->matrix[1][0], tmi->translate[0],
                          tmi->matrix[0][1], tmi->matrix[1][1], tmi->translate[1]);
            break;
        case TMOD_ROTATE: {
            /* RB_CalcRotateTexCoords, sinTable lookup included */
            double degs = -tmi->rotateSpeed * tess.shaderTime;
            int64_t index = degs * (FUNCTABLE_SIZE / 360.0f);
            float sinValue = tr.sinTable[index & FUNCTABLE_MASK];
            float cosValue = tr.sinTable[(index + FUNCTABLE_SIZE / 4) & FUNCTABLE_MASK];

            PS3_TexAffine(s, t, L, cosValue, -sinValue, 0.5 - 0.5 * cosValue + 0.5 * sinValue,
                          sinValue, cosValue, 0.5 - 0.5 * sinValue - 0.5 * cosValue);
            break;
        }
        default:        /* a bad type ComputeTexCoords drops on */
            return qfalse;
        }
    }
    return qtrue;
}

/* One stage, no side effects: the dry run for RB_PS3_SurfaceDeforms and the real one share it */
static qboolean PS3_StageParams(const shaderStage_t *pStage, ps3gl_vparams_t *p)
{
    Com_Memset(p, 0, sizeof(*p));
    /* greyscale and fog-modulated colors are per-vertex table work: they stay on the PPE */
    if (r_greyscale->value || (tess.fogNum && pStage->adjustColorsForFog != ACFF_NONE))
        return qfalse;
    if (!PS3_ColorParams(pStage, p) || !PS3_BundleParams(&pStage->bundle[0], 0, p))
        return qfalse;
    /* a single-texture stage's bundle 1 is TCGEN_BAD, and its TEXCOORD1 is never sampled */
    if (pStage->bundle[1].image[0] && !PS3_BundleParams(&pStage->bundle[1], 1, p))
        return qfalse;
    return qtrue;
}

static void PS3_SendParams(ps3gl_vparams_t *p)
{
    qboolean normal = p->csel[0] || p->csel[1] || p->tsel[0] || p->tsel[1];

    if (ps3Deform.active) {
        Com_Memcpy(p->df, ps3Deform.df, sizeof(p->df));
        normal = normal || ps3Deform.normal;
    }
    p->color = tess.vertexColors;
    p->st = tess.texCoords[0][0];
    p->lm = tess.texCoords[0][1];
    p->tc_stride = sizeof(tess.texCoords[0]);
    p->normal = normal ? tess.normal : NULL;
    ps3gl_VertexParams(p);
}

/* RB_IterateStagesGeneric calls this in place of ComputeColors / ComputeTexCoords */
qboolean RB_PS3_StageParams(shaderStage_t *pStage)
{
    ps3gl_vparams_t p;

    if (!ps3gl_VProgOn() || !PS3_StageParams(pStage, &p))
        return qfalse;
    PS3_SendParams(&p);
    return qtrue;
}

/* RB_StageIteratorVertexLitTexture calls this in place of RB_CalcDiffuseColor: it binds raw st, no tcMods */
qboolean RB_PS3_VertexLitParams(void)
{
    ps3gl_vparams_t p;

    /* this iterator never deforms; the last generic surface's slots must not leak in */
    ps3Deform.active = qfalse;
    if (!ps3gl_VProgOn() || !backEnd.currentEntity)
        return qfalse;
    Com_Memset(&p, 0, sizeof(p));
    PS3_DiffuseParams(backEnd.currentEntity, &p);
    p.csel[3] = 1.0f;
    p.cadd[3] = 1.0f;
    p.tm[0][0] = 1.0f;
    p.tm[0][9] = 1.0f;
    PS3_SendParams(&p);
    return qtrue;
}

/* V5, RB_StageIteratorGeneric's call in place of RB_DeformTessGeometry. The VP deforms only if nothing on
 * the PPE reads tess.xyz / tess.normal afterwards and every stage goes through the VP. */
qboolean RB_PS3_SurfaceDeforms(void)
{
    const shader_t *shader = tess.shader;
    ps3gl_vparams_t p;
    int i, n = 0;

    ps3Deform.active = qfalse;
    ps3Deform.normal = qfalse;
    if (!shader->numDeforms || !ps3gl_VProgOn())
        return qfalse;
    /* dlights, the fog pass, stencil shadows and the debug draws read the deformed arrays */
    if (tess.dlightBits || tess.fogNum || r_shadows->integer == 2 || r_showtris->integer || r_shownormals->integer)
        return qfalse;

    Com_Memset(ps3Deform.df, 0, sizeof(ps3Deform.df));
    for (i = 0; i < shader->numDeforms && n < 3; i++) {
        const deformStage_t *ds = &shader->deforms[i];
        float *a = ps3Deform.df[n], *b = a + 4, *c = a + 8, *m = a + 12, scale;
        double now;

        switch (ds->deformation) {
        case DEFORM_NONE:
            continue;
        case DEFORM_WAVE:
            if (ds->deformationWave.frequency == 0) {
                /* RB_CalcDeformVertexes: one scale for the whole surface */
                if (!PS3_WaveValue(&ds->deformationWave, &scale))
                    return qfalse;
                a[0] = scale;
            } else {
                if (!PS3_WaveWeights(ds->deformationWave.func, b, c))
                    return qfalse;
                a[0] = ds->deformationWave.base;
                a[1] = ds->deformationWave.amplitude;
                a[2] = ds->deformationSpread;
                now = ds->deformationWave.phase + tess.shaderTime * ds->deformationWave.frequency;
                b[0] = now - floor(now);
            }
            ps3Deform.normal = qtrue;
            break;
        case DEFORM_BULGE:
            /* RB_CalcBulgeVertexes: sinTable over st.s * width + now, one period per 2 pi */
            now = backEnd.refdef.time * 0.001 * ds->bulgeSpeed / (2 * M_PI);
            a[1] = ds->bulgeHeight;
            a[3] = ds->bulgeWidth / (2 * M_PI);
            b[0] = now - floor(now);
            b[1] = 1.0f;
            ps3Deform.normal = qtrue;
            break;
        case DEFORM_MOVE:
            if (!PS3_WaveValue(&ds->deformationWave, &scale))
                return qfalse;
            VectorScale(ds->moveVector, scale, m);
            break;
        default:        /* normals (per-vertex noise), autosprite, text, projection shadow */
            return qfalse;
        }
        c[2] = 1.0f;
        n++;
    }
    if (i < shader->numDeforms)
        return qfalse;

    for (i = 0; i < MAX_SHADER_STAGES && tess.xstages[i]; i++) {
        shaderStage_t st = *tess.xstages[i];

#ifdef ELITEFORCE
        /* RB_IterateStagesGeneric's override */
        if (backEnd.currentEntity->e.renderfx & RF_FORCE_ENT_ALPHA)
            st.alphaGen = AGEN_ENTITY;
#endif
        if (!PS3_StageParams(&st, &p))
            return qfalse;
    }
    ps3Deform.active = n > 0;
    return ps3Deform.active;
}
