/*
 * ioquake3-PS3: sys/ps3_main.c
 * PS3 platform entry point.
 * Initialises RSX, mounts filesystem, then hands off to ioQ3's Com_Init.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>

#include <ppu-types.h>
#include <rsx/rsx.h>
#include <sysutil/sysutil.h>
#include <sysmodule/sysmodule.h>
#include <io/pad.h>
#include <sys/thread.h>
#include <sys/process.h>
#include <net/net.h>

/* ioQuake3 qcommon interface */
#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"

/* PS3-specific subsystems */
#include "../sys/ps3_glimp.h"
#include "../input/ps3_input.h"
#include "../input/ps3_osk.h"
#include "../audio/ps3_snd.h"

/* Renderer interface for pre-init */
#include "GL/gl.h"
#include "renderercommon/tr_types.h"
#include "renderercommon/tr_public.h"
extern refexport_t *GetRefAPI(int apiVersion, refimport_t *rimp);

/* PSL1GHT process parameters:
 * Priority 1001 (default), stack 1 MB, heap 128 MB */
SYS_PROCESS_PARAM(1001, 0x100000);

/* --------------------------------------------------------------------------
 * XMB system callback -- handles quit request from PS button
 * -------------------------------------------------------------------------- */
static volatile int ps3_running = 1;

static void ps3_sysutil_callback(u64 status, u64 param, void *userdata)
{
    (void)userdata;
    switch (status) {
        case SYSUTIL_EXIT_GAME:
            ps3_running = 0;
            break;
        case SYSUTIL_DRAW_BEGIN:
        case SYSUTIL_DRAW_END:
            break;
        case SYSUTIL_OSK_LOADED:
        case SYSUTIL_OSK_DONE:
        case SYSUTIL_OSK_UNLOADED:
        case SYSUTIL_OSK_INPUT_ENTERED:
        case SYSUTIL_OSK_INPUT_CANCELED:
            PS3_OSK_SysutilCallback(status, param);
            break;
        default:
            break;
    }
}

/* --------------------------------------------------------------------------
 * Debug logging -- file output to PS3 HDD
 *
 * File:  /dev_hdd0/game/IOQ3PS300/USRDIR/log.txt  (always active)
 * -------------------------------------------------------------------------- */
static const char *ps3_log_path = "/dev_hdd0/game/IOQ3PS300/USRDIR/log.txt";

void ps3_log(const char *msg)
{
    FILE *f = fopen(ps3_log_path, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

#define PS3LOG(fmt, ...) do { \
    char _lb[256]; \
    snprintf(_lb, sizeof(_lb), fmt, ##__VA_ARGS__); \
    ps3_log(_lb); \
} while (0)

/* --------------------------------------------------------------------------
 * Filesystem setup
 *
 * PS3 homebrew installed via PKG lands in:
 *   /dev_hdd0/game/TITLEID/USRDIR/
 *
 * Game data (baseq3/pak0.pk3 etc.) should be placed in:
 *   /dev_hdd0/game/IOQ3PS300/USRDIR/baseq3/
 *
 * Alternatively, data can be on USB:
 *   /dev_usb000/quake3/baseq3/
 * -------------------------------------------------------------------------- */
static const char *ps3_base_path  = "/dev_hdd0/game/IOQ3PS300/USRDIR";
static const char *ps3_usb_path   = "/dev_usb000/quake3";

static qboolean PS3_SetupFilesystem(void)
{
    /* Check if game data exists on HDD first */
    FILE *f = fopen("/dev_hdd0/game/IOQ3PS300/USRDIR/baseq3/pak0.pk3", "rb");
    if (f) {
        fclose(f);
        chdir(ps3_base_path);
        PS3LOG("Using HDD path: %s", ps3_base_path);
        return qtrue;
    }

    /* Fall back to USB */
    f = fopen("/dev_usb000/quake3/baseq3/pak0.pk3", "rb");
    if (f) {
        fclose(f);
        chdir(ps3_usb_path);
        ps3_base_path = ps3_usb_path;
        PS3LOG("Using USB path: %s", ps3_usb_path);
        return qtrue;
    }

    printf("FATAL: pak0.pk3 not found on HDD or USB\n");
    PS3LOG("FATAL: pak0.pk3 not found");
    return qfalse;
}

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* Register XMB quit callback */
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, ps3_sysutil_callback, NULL);

    printf("ioquake3-PS3 starting...\n");

    /* Truncate log from previous run */
    { FILE *f = fopen(ps3_log_path, "w"); if (f) fclose(f); }
    PS3LOG("main() reached");

    /* Filesystem -- must succeed before any file I/O */
    if (!PS3_SetupFilesystem()) {
        printf("FATAL: could not find game data. Halting.\n");
        PS3LOG("FATAL: filesystem setup failed");
        /* Spin so user can read the message */
        while (ps3_running) {
            sysUtilCheckCallback();
        }
        return 1;
    }
    PS3LOG("Filesystem ready");

    /* Input (DualShock 3) */
    PS3_Input_Init();
    printf("[ps3] Input OK\n");
    PS3LOG("Input init done");

    /* On-screen keyboard (4 MB memory container) */
    PS3_OSK_Init();
    PS3LOG("OSK init done");

    /* Network -- PS3 has BSD sockets via PSL1GHT.
     * Network init is simpler than Wii; the system handles DHCP/WiFi. */
    /* netInitialize() is called later by NET_Init via net_ip.c */
    PS3LOG("Network deferred to NET_Init");

    /* Audio */
    PS3_Snd_Init();
    printf("[ps3] Audio OK\n");
    PS3LOG("Audio init done");

    /* RSX / display init -- must be up before Com_Init because
     * Com_Init -> CL_Init -> BeginRegistration -> BeginFrame fires immediately */
    PS3_RSX_Init();
    printf("[ps3] RSX init done\n");
    PS3LOG("RSX init done");

    /* Log memory layout for BSS corruption diagnosis */
    {
        extern void *ps3gl_ptr;  /* actually ps3gl_state_t* but avoid header dep */
        extern gcmContextData *ps3gl_get_ctx(void);
        PS3LOG("MEMMAP: ps3gl_ptr=%p &ps3gl_ptr=%p get_ctx=%p",
               ps3gl_ptr, &ps3gl_ptr, (void*)ps3gl_get_ctx());
    }

    /* Pre-initialize renderer export table before Com_Init so that
     * Com_Printf -> SCR_UpdateScreen -> re.BeginFrame doesn't crash */
    extern refexport_t re;
    refexport_t *ref = GetRefAPI(REF_API_VERSION, NULL);
    if (ref) re = *ref;
    PS3LOG("GetRefAPI done");

    /* Build command line for Com_Init */
    static char cmdline[2048];
    snprintf(cmdline, sizeof(cmdline),
        "+set fs_basepath %s "
        "+set fs_homepath %s "
        "+set fs_steampath \"\" "
        "+set fs_gogpath \"\" "
        "+set fs_game baseq3 "
        "+set com_hunkMegs 80 "
        "+set com_zoneMegs 24 "
        /* --- display --- */
        "+set r_mode -1 "
        "+set r_customwidth 1280 "
        "+set r_customheight 720 "
        "+set r_picmip 1 "
        /* --- rendering quality --- */
        "+set r_dynamic 1 "
        "+set r_flares 0 "
        "+set r_fastsky 0 "
        "+set r_lodbias 1 "
        "+set r_subdivisions 12 "
        "+set r_simpleMipMaps 1 "
        "+set r_drawSun 0 "
        "+set r_primitives 2 "
        /* --- frame rate: 0 = no software limiter, vsync paces us at 60 Hz --- */
        "+set com_maxfps 0 "
        "+set pmove_fixed 1 "
        /* --- audio --- */
        "+set s_khz 48 "
        "+set com_soundMegs 8 "
        /* --- server / misc --- */
        "+set sv_pure 0 "
        "+set sv_maxclients 8 "
        "+set in_joystick 1 "
        "+set in_joystickUseAnalog 1 "
        "+set j_pitch_axis 3 "
        "+set j_yaw_axis 4 "
        "+set com_standalone 0 "
        "+set net_enabled 1 "
        "+set net_port 27960 "
        "+set fraglimit 0 "
        "+set timelimit 0 "
        "+set com_logfile 2",
        ps3_base_path, ps3_base_path
    );

    /* Create qkey file (2048 bytes) if missing */
    {
        char qkeypath[256];
        snprintf(qkeypath, sizeof(qkeypath), "%s/qkey", ps3_base_path);
        FILE *kf = fopen(qkeypath, "rb");
        if (kf) {
            fclose(kf);
        } else {
            kf = fopen(qkeypath, "wb");
            if (kf) {
                unsigned char buf[2048];
                for (int i = 0; i < 2048; i++) buf[i] = (unsigned char)(i & 0xFF);
                fwrite(buf, 1, 2048, kf);
                fclose(kf);
                printf("[ps3] Created qkey file\n");
            }
        }
    }

    /* Verify ps3gl_ptr before Com_Init */
    {
        extern void *ps3gl_ptr;
        PS3LOG("pre-ComInit: ps3gl_ptr=%p", ps3gl_ptr);
    }

    PS3LOG("Calling Com_Init");
    printf("[ps3] Calling Com_Init...\n");
    Com_Init(cmdline);
    printf("[ps3] Com_Init done\n");
    PS3LOG("Com_Init done");

    /* Initialize PSL1GHT network stack before ioq3's NET_Init.
     * Must load the net module and call netInitialize()/netCtlInit()
     * before any socket operations. */
    {
        s32 mod_ret = sysModuleLoad(SYSMODULE_NET);
        PS3LOG("sysModuleLoad(NET) returned %d", (int)mod_ret);
        int net_ret = PS3_Net_Init();
        PS3LOG("PS3_Net_Init returned %d", net_ret);
        if (net_ret < 0) {
            PS3LOG("WARNING: PS3 network init failed, networking disabled");
        }
    }

    /* Sanity check: RSX context pointer should survive Com_Init */
    {
        extern gcmContextData *ps3gl_get_ctx(void);
        PS3LOG("post-ComInit ctx check: ps3gl_get_ctx=%p", (void*)ps3gl_get_ctx());
    }

    NET_Init();
    printf("[ps3] NET_Init done\n");
    PS3LOG("NET_Init done");

    /* Main loop */
    PS3LOG("Entering main loop");
    while (ps3_running) {
        sysUtilCheckCallback();
        PS3_Input_Frame();

        if (PS3_Input_QuitPressed()) {
            Com_Quit_f();
            break;
        }

        Com_Frame();
    }

    /* Cleanup */
    PS3_OSK_Shutdown();
    PS3_Snd_Shutdown();
    PS3_Input_Shutdown();
    PS3_RSX_Shutdown();
    NET_Shutdown();
    PS3_Net_Shutdown();

    PS3LOG("Clean shutdown");
    return 0;
}
