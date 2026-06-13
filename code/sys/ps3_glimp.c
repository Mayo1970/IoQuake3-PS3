/* ps3_glimp.c -- RSX/GCM display and frame management. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include <ppu-types.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <sysutil/video.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "renderercommon/tr_types.h"
#include "renderercommon/tr_public.h"
#include "../sys/ps3_glimp.h"
#include "../gl/ps3gl.h"

extern void ps3_log(const char *msg);

/* ----------------------------------------------------------------
 * RSX state
 * ---------------------------------------------------------------- */
/* Triple-buffered: third buffer absorbs missed vblank spikes (vsync double-buffering hard-drops to 30 FPS). */
#define RSX_FB_COUNT 3

static gcmContextData *ps3_gcm_context = NULL;
static u32 ps3_display_width  = 1280;
static u32 ps3_display_height = 720;
static u32 ps3_color_pitch    = 0;
static u32 ps3_color_offset[RSX_FB_COUNT];
static u32 *ps3_color_buffer[RSX_FB_COUNT];
static u32 ps3_depth_offset;
static u32 *ps3_depth_buffer = NULL;
static int ps3_current_fb = 0;
static int ps3_current_rt = -1;   /* last render target set on RSX; -1 = none */

#define RSX_FB_ALIGN    64
#define RSX_DEPTH_ALIGN 64

static void PS3_RSX_AllocFramebuffers(void)
{
    ps3_color_pitch = ps3_display_width * 4; /* ARGB8888 */

    u32 color_size = ps3_color_pitch * ps3_display_height;
    u32 depth_size = ps3_display_width * ps3_display_height * 4; /* 32-bit depth */

    for (int i = 0; i < RSX_FB_COUNT; i++) {
        ps3_color_buffer[i] = (u32 *)rsxMemalign(RSX_FB_ALIGN, color_size);
        if (!ps3_color_buffer[i]) {
            printf("[ps3] FATAL: rsxMemalign failed for color buffer %d\n", i);
            return;
        }
        rsxAddressToOffset(ps3_color_buffer[i], &ps3_color_offset[i]);

        gcmSetDisplayBuffer(i, ps3_color_offset[i],
                            ps3_color_pitch, ps3_display_width, ps3_display_height);
    }

    ps3_depth_buffer = (u32 *)rsxMemalign(RSX_DEPTH_ALIGN, depth_size);
    if (ps3_depth_buffer) {
        rsxAddressToOffset(ps3_depth_buffer, &ps3_depth_offset);
    }
}

static void PS3_RSX_SetRenderTarget(int index)
{
    if (index == ps3_current_rt) return;
    ps3_current_rt = index;

    gcmSurface sf;
    memset(&sf, 0, sizeof(sf));

    sf.colorFormat    = GCM_SURFACE_A8R8G8B8;
    sf.colorTarget    = GCM_SURFACE_TARGET_0;
    sf.colorLocation[0] = GCM_LOCATION_RSX;
    sf.colorOffset[0]   = ps3_color_offset[index];
    sf.colorPitch[0]    = ps3_color_pitch;

    /* Unused color targets */
    for (int i = 1; i < 4; i++) {
        sf.colorLocation[i] = GCM_LOCATION_RSX;
        sf.colorOffset[i]   = ps3_color_offset[index];
        sf.colorPitch[i]    = 64;
    }

    sf.depthFormat    = GCM_SURFACE_ZETA_Z24S8;
    sf.depthLocation  = GCM_LOCATION_RSX;
    sf.depthOffset    = ps3_depth_offset;
    sf.depthPitch     = ps3_display_width * 4;

    sf.type           = GCM_SURFACE_TYPE_LINEAR;
    sf.antiAlias      = GCM_SURFACE_CENTER_1;

    sf.width          = ps3_display_width;
    sf.height         = ps3_display_height;
    sf.x              = 0;
    sf.y              = 0;

    rsxSetSurface(ps3_gcm_context, &sf);
}

/* Flip tracking via GCM interrupt. */
static volatile u32 ps3_flips_completed = 0;
static u32 ps3_flips_queued = 0;

static void PS3_RSX_FlipHandler(const u32 head)
{
    (void)head;
    ps3_flips_completed++;
}

static void PS3_RSX_WaitFlips(void)
{
    /* Wait until a back buffer becomes available; bounded spin. */
    int spins = 0;
    while ((s32)(ps3_flips_queued - ps3_flips_completed) > RSX_FB_COUNT - 2) {
        usleep(100);
        if (++spins > 20000) {  /* ~2 s */
            printf("[ps3] WARNING: flip wait timed out (queued=%u done=%u)\n",
                   ps3_flips_queued, ps3_flips_completed);
            ps3_flips_completed = ps3_flips_queued;
            break;
        }
    }
}

/* ----------------------------------------------------------------
 * Public interface
 * ---------------------------------------------------------------- */

#define RSX_CB_SIZE     (1 * 1024 * 1024)   /* 1 MB command buffer */
#define RSX_HOST_SIZE   (32 * 1024 * 1024)  /* 32 MB IO buffer (PSL1GHT standard) */

void PS3_RSX_Init(void)
{
    ps3_log("PS3_RSX_Init: entered");

    /* Allocate IO buffer (1 MB aligned, 32 MB total). */
    void *host_addr = memalign(1024 * 1024, RSX_HOST_SIZE);
    if (!host_addr) {
        ps3_log("PS3_RSX_Init: FATAL memalign failed for 32MB IO buffer");
        return;
    }

    s32 ret = rsxInit(&ps3_gcm_context, RSX_CB_SIZE, RSX_HOST_SIZE, host_addr);
    if (ret != 0 || !ps3_gcm_context) {
        ps3_log("PS3_RSX_Init: rsxInit failed, trying fallbacks");
        rsxSetDefaultCommandBuffer(&ps3_gcm_context);
        if (!ps3_gcm_context) {
            /* Fallback: gcmInitBody when default context unavailable. */
            ret = gcmInitBody(&ps3_gcm_context, RSX_CB_SIZE, RSX_HOST_SIZE, host_addr);
            if (ret == 0 && ps3_gcm_context) {
                rsxHeapInit();
                ps3_log("PS3_RSX_Init: gcmInitBody fallback succeeded");
            } else {
                ps3_log("PS3_RSX_Init: FATAL all GCM init attempts failed");
                return;
            }
        }
    }

    /* Try 720p; fall back to TV default if unavailable. */
    s32 vid_res = VIDEO_RESOLUTION_720;
    u8  vid_aspect = VIDEO_ASPECT_16_9;

    if (!videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720,
                                        VIDEO_ASPECT_16_9, 0)) {
        videoState state;
        videoGetState(0, 0, &state);
        vid_res = state.displayMode.resolution;
        vid_aspect = state.displayMode.aspect;
        ps3_log("PS3_RSX_Init: 720p unavailable, using TV default");
    }

    videoResolution res;
    videoGetResolution(vid_res, &res);

    ps3_display_width  = res.width;
    ps3_display_height = res.height;
    printf("[ps3] Display: %ux%u\n", ps3_display_width, ps3_display_height);

    videoConfiguration vconfig;
    memset(&vconfig, 0, sizeof(vconfig));
    vconfig.resolution  = vid_res;
    vconfig.format      = VIDEO_BUFFER_FORMAT_XRGB;
    vconfig.pitch       = ps3_display_width * 4;
    vconfig.aspect      = vid_aspect;
    videoConfigure(0, &vconfig, NULL, 0);

    PS3_RSX_AllocFramebuffers();

    ps3_current_fb = 0;
    PS3_RSX_SetRenderTarget(ps3_current_fb);

    gcmResetFlipStatus();

    /* Flip tracking via interrupt (required for triple buffering). */
    ps3_flips_completed = 0;
    ps3_flips_queued = 0;
    gcmSetFlipHandler(PS3_RSX_FlipHandler);

    ps3gl_init(ps3_gcm_context, ps3_display_width, ps3_display_height);

    /* Tess arena in top half of IO buffer; GPU fetches XDR directly. */
    ps3gl_tess_arena_init((uint8_t *)host_addr + RSX_HOST_SIZE / 2,
                          RSX_HOST_SIZE / 2);

}

void PS3_RSX_Shutdown(void)
{
    ps3_current_rt = -1;
    gcmSetFlipHandler(NULL);
    ps3gl_shutdown();

    rsxFinish(ps3_gcm_context, 1);

    for (int i = 0; i < RSX_FB_COUNT; i++) {
        if (ps3_color_buffer[i]) {
            rsxFree(ps3_color_buffer[i]);
            ps3_color_buffer[i] = NULL;
        }
    }
    if (ps3_depth_buffer) {
        rsxFree(ps3_depth_buffer);
        ps3_depth_buffer = NULL;
    }
}

void PS3_RSX_BeginFrame(void)
{
    if (!ps3_gcm_context) return;

    PS3_RSX_WaitFlips();
    PS3_RSX_SetRenderTarget(ps3_current_fb);
    ps3gl_begin_frame();
}

void PS3_RSX_EndFrame(void)
{
    if (!ps3_gcm_context) return;

    ps3gl_end_frame();

    /* Queue flip + flush (no WaitFlip here to avoid double V-Sync stall). */
    gcmSetFlip(ps3_gcm_context, ps3_current_fb);
    rsxFlushBuffer(ps3_gcm_context);

    ps3_flips_queued++;
    ps3_current_fb = (ps3_current_fb + 1) % RSX_FB_COUNT;
}

/* Renderer stats command. */
static int ps3_stats_auto = 0;

static void PS3GL_PrintStats(const ps3gl_stats_t *s)
{
    Com_Printf("ps3gl frame %u: draws=%u (elements=%u) streams direct=%u "
               "copied=%u (verts=%u)\n",
               ps3gl_stats_frames, s->draw_calls, s->draws_elements,
               s->streams_direct, s->streams_copied, s->verts_copied);
    Com_Printf("  vring: %u KB (%u KB idx), %u wraps, %u dropped | "
               "arena: %u KB, %u wraps | texbinds=%u clipculled=%u\n",
               s->vring_bytes / 1024, s->idx_bytes / 1024,
               s->vring_wraps, s->vring_drops,
               s->tarena_bytes / 1024, s->tarena_wraps,
               s->tex_binds, s->tris_clipculled);
}

static void PS3GL_Stats_f(void)
{
    if (Cmd_Argc() >= 2) {
        ps3_stats_auto = atoi(Cmd_Argv(1));
        Com_Printf("ps3gl_stats: auto-print %s\n",
                   ps3_stats_auto > 0 ? va("every %d frames", ps3_stats_auto)
                                      : "off");
        return;
    }
    PS3GL_PrintStats(&ps3gl_stats_last);
}

static void PS3_Perf_f(void)
{
    int on = 1;
    if (Cmd_Argc() >= 2)
        on = atoi(Cmd_Argv(1));

    if (on) {
        Cvar_Set("r_dynamic", "0");
        Cvar_Set("r_picmip", "1");
        Cvar_Set("cg_marks", "0");
        Com_Printf("ps3_perf: ON (r_dynamic 0, r_picmip 1, cg_marks 0)\n");
    } else {
        Cvar_Set("r_dynamic", "1");
        Cvar_Set("r_picmip", "0");
        Cvar_Set("cg_marks", "1");
        Com_Printf("ps3_perf: OFF (r_dynamic 1, r_picmip 0, cg_marks 1)\n");
    }
    Com_Printf("ps3_perf: r_picmip is latched; vid_restart to apply\n");
}

void GLimp_Init(qboolean fixedFunction)
{
    (void)fixedFunction;
    extern glconfig_t glConfig;

    Cmd_AddCommand("ps3gl_stats", PS3GL_Stats_f);
    Cmd_AddCommand("ps3_perf", PS3_Perf_f);

    IN_Init(NULL);

    glConfig.vidWidth           = ps3_display_width;
    glConfig.vidHeight          = ps3_display_height;
    glConfig.colorBits          = 32;
    glConfig.depthBits          = 24;
    glConfig.stencilBits        = 8;
    glConfig.isFullscreen       = qtrue;
    glConfig.windowAspect       = (float)ps3_display_width / (float)ps3_display_height;
    glConfig.stereoEnabled      = qfalse;
    glConfig.smpActive          = qfalse;
    glConfig.displayFrequency   = 60;
    glConfig.deviceSupportsGamma = qfalse;
    glConfig.textureCompression = TC_NONE;
    glConfig.numTextureUnits    = 2;
    glConfig.textureEnvAddAvailable = qtrue;

    Q_strncpyz(glConfig.vendor_string, "Sony", sizeof(glConfig.vendor_string));
    Q_strncpyz(glConfig.renderer_string, "RSX Reality Synthesizer",
               sizeof(glConfig.renderer_string));
    Q_strncpyz(glConfig.version_string, "1.1 PSL1GHT",
               sizeof(glConfig.version_string));
    Q_strncpyz(glConfig.extensions_string, "GL_ARB_multitexture",
               sizeof(glConfig.extensions_string));
}

void GLimp_Shutdown(qboolean unloadDLL)
{
    (void)unloadDLL;
    Cmd_RemoveCommand("ps3gl_stats");
    Cmd_RemoveCommand("ps3_perf");
}

void GLimp_EndFrame(void)
{
    PS3_RSX_EndFrame();

    if (ps3_stats_auto > 0 &&
        (ps3gl_stats_frames % (uint32_t)ps3_stats_auto) == 0)
        PS3GL_PrintStats(&ps3gl_stats_cur);
}

void GLimp_Minimize(void) { }

void GLimp_SetGamma(unsigned char red[256], unsigned char green[256],
                     unsigned char blue[256])
{
    (void)red; (void)green; (void)blue;
}

void GLimp_LogComment(char *comment)
{
    (void)comment;
}

void *GLimp_RendererSleep(void)
{
    return NULL;
}

qboolean GLimp_SpawnRenderThread(void (*function)(void))
{
    (void)function;
    return qfalse;
}

void GLimp_FrontEndSleep(void)
{
}

void GLimp_WakeRenderer(void *data)
{
    (void)data;
}
