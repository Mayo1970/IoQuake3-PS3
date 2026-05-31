/* ps3_renderer.c -- Renderer glue layer. */

#include <stdio.h>
#include <string.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "renderercommon/tr_types.h"
#include "renderercommon/tr_public.h"
#include "../sys/ps3_glimp.h"

extern void ps3_log(const char *msg);

/* Global refimport_t -- tr_main.c's definition is renamed by our patches. */
refimport_t ri;

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

    QGL_Init();

    refexport_t *re = tr_init_GetRefAPI_unused(apiVersion, rimp);

    /* Inject PS3_RSX_BeginFrame before each frame */
    upstream_BeginFrame = re->BeginFrame;
    re->BeginFrame = PS3_RE_BeginFrame;

    return re;
}
