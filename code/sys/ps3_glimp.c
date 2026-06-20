/* ps3_glimp.c -- RSX/GCM display init and frame management. */

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

/* RSX state */
#define RSX_FB_COUNT 3

static gcmContextData *ps3_gcm_context = NULL;
static gcmContextData *ps3_gcm_context_backup = (gcmContextData *)0xDEAD;
static u32 ps3_display_width  = 1280;
static u32 ps3_display_height = 720;
static u32 ps3_color_pitch    = 0;
static u32 ps3_color_offset[RSX_FB_COUNT];
static u32 *ps3_color_buffer[RSX_FB_COUNT];
static u32 ps3_depth_offset;
static u32 *ps3_depth_buffer = NULL;
static int ps3_current_fb = 0;
static int ps3_current_rt = -1;

static volatile u32 ps3_flip_queued    = 0;
static volatile u32 ps3_flip_completed = 0;

static void ps3_flip_handler(const u32 head)
{
    (void)head;
    ps3_flip_completed++;
}

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

/* Public interface */

/* GCM IO buffer: 1 MB cmd + 32 MB IO (standard PSL1GHT pattern). */
#define RSX_CB_SIZE     (1 * 1024 * 1024)   /* 1 MB command buffer */
#define RSX_HOST_SIZE   (32 * 1024 * 1024)  /* 32 MB IO buffer     */

void PS3_RSX_Init(void)
{
    ps3_log("PS3_RSX_Init: entered");

    /* Allocate 1 MB-aligned IO buffer (32 MB). */
    void *host_addr = memalign(1024 * 1024, RSX_HOST_SIZE);
    if (!host_addr) {
        ps3_log("PS3_RSX_Init: FATAL memalign failed for 32MB IO buffer");
        return;
    }

    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "PS3_RSX_Init: host_addr=%p cmdSize=0x%x ioSize=0x%x",
                 host_addr, RSX_CB_SIZE, RSX_HOST_SIZE);
        ps3_log(dbg);
    }

    s32 ret = rsxInit(&ps3_gcm_context, RSX_CB_SIZE, RSX_HOST_SIZE, host_addr);
    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg), "PS3_RSX_Init: rsxInit ret=%d ctx=%p",
                 (int)ret, (void*)ps3_gcm_context);
        ps3_log(dbg);
    }
    if (ret != 0 || !ps3_gcm_context) {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "PS3_RSX_Init: rsxInit FAILED (ret=0x%08x), trying fallbacks",
                 (unsigned)(ret & 0xFFFFFFFF));
        ps3_log(dbg);

        /* Try to recover existing GCM context. */
        rsxSetDefaultCommandBuffer(&ps3_gcm_context);
        snprintf(dbg, sizeof(dbg),
                 "PS3_RSX_Init: rsxSetDefaultCommandBuffer ctx=%p",
                 (void*)ps3_gcm_context);
        ps3_log(dbg);

        if (ps3_gcm_context) {
            ps3_log("PS3_RSX_Init: recovered existing GCM context");
        } else {
            /* Try gcmInitBody as final fallback. */
            ret = gcmInitBody(&ps3_gcm_context, RSX_CB_SIZE, RSX_HOST_SIZE, host_addr);
            snprintf(dbg, sizeof(dbg),
                     "PS3_RSX_Init: gcmInitBody ret=%d ctx=%p",
                     (int)ret, (void*)ps3_gcm_context);
            ps3_log(dbg);

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
        ps3_log("PS3_RSX_Init: 720p not available, using TV default");
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
    ps3_flip_queued    = 0;
    ps3_flip_completed = 0;
    gcmSetFlipHandler(ps3_flip_handler);
    PS3_RSX_SetRenderTarget(ps3_current_fb);

    ps3gl_init(ps3_gcm_context, ps3_display_width, ps3_display_height);

    {
        char dbg[256];
        snprintf(dbg, sizeof(dbg),
                 "PS3_RSX_Init: after ps3gl_init ps3gl_ptr=%p ctx=%p",
                 (void*)ps3gl_ptr, (void*)ps3_gcm_context);
        ps3_log(dbg);
    }

    /* Backup context pointer. */
    ps3_gcm_context_backup = ps3_gcm_context;

    {
        char dbg[256];
        snprintf(dbg, sizeof(dbg),
                 "PS3_RSX_Init: ctx=%p &ctx=%p backup=%p ps3gl_ptr=%p sizeof=%u",
                 (void*)ps3_gcm_context,
                 (void*)&ps3_gcm_context,
                 (void*)ps3_gcm_context_backup,
                 (void*)ps3gl_ptr,
                 (unsigned)sizeof(ps3gl_state_t));
        ps3_log(dbg);
    }
}

void PS3_RSX_Shutdown(void)
{
    ps3_current_rt = -1;
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

/* Block only if both non-displayed buffers are in flight (queued - completed > 1).
 * With 3 buffers one buffer is always free for the CPU to render into. */
static void PS3_RSX_WaitFlips(void)
{
    int waited = 0;
    while ((int)(ps3_flip_queued - ps3_flip_completed) > RSX_FB_COUNT - 2) {
        usleep(100);
        if (++waited > 20000) {
            printf("[ps3] flip fence timed out, force-syncing\n");
            ps3_flip_completed = ps3_flip_queued;
            break;
        }
    }
}

void PS3_RSX_BeginFrame(void)
{
    /* Restore from .data backup if corrupted */
    if ((uintptr_t)ps3_gcm_context <= 0x1000
        && (uintptr_t)ps3_gcm_context_backup > 0x1000)
        ps3_gcm_context = ps3_gcm_context_backup;

    if ((uintptr_t)ps3_gcm_context <= 0x1000) return;

    /* Block until a buffer is free to render into. */
    PS3_RSX_WaitFlips();

    PS3_RSX_SetRenderTarget(ps3_current_fb);
    ps3gl_begin_frame();
}

void PS3_RSX_EndFrame(void)
{
    /* Restore from .data backup if corrupted */
    if ((uintptr_t)ps3_gcm_context <= 0x1000
        && (uintptr_t)ps3_gcm_context_backup > 0x1000)
        ps3_gcm_context = ps3_gcm_context_backup;

    if ((uintptr_t)ps3_gcm_context <= 0x1000) return;

    ps3gl_end_frame();

    /* Queue flip + flush (non-blocking). Wait happens in next BeginFrame. */
    gcmSetWaitFlip(ps3_gcm_context);
    gcmSetFlip(ps3_gcm_context, ps3_current_fb);
    rsxFlushBuffer(ps3_gcm_context);

    ps3_flip_queued++;
    ps3_current_fb = (ps3_current_fb + 1) % RSX_FB_COUNT;
}

/* GLimp interface -- called by ioQ3's renderer */
void GLimp_Init(qboolean fixedFunction)
{
    (void)fixedFunction;
    extern glconfig_t glConfig;

    /* Call IN_Init for default controller binds (upstream calls from sdl_glimp.c) */
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
    glConfig.numTextureUnits    = 2;    /* RSX supports multiple texture units */
    glConfig.textureEnvAddAvailable = qtrue; /* We handle GL_ADD texenv mode */

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
}

void GLimp_EndFrame(void)
{
    PS3_RSX_EndFrame();
}

void GLimp_Minimize(void)
{
    /* No minimize on PS3 */
}

void GLimp_SetGamma(unsigned char red[256], unsigned char green[256],
                     unsigned char blue[256])
{
    (void)red; (void)green; (void)blue;
    /* PS3 gamma could be set via RSX but we skip it for now */
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
