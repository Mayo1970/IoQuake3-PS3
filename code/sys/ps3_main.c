/* ps3_main.c -- PS3 entry point. */

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

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"

#include "../sys/ps3_glimp.h"
#include "../input/ps3_input.h"
#include "../input/ps3_osk.h"
#include "../audio/ps3_snd.h"

#include "GL/gl.h"
#include "renderercommon/tr_types.h"
#include "renderercommon/tr_public.h"
extern refexport_t *GetRefAPI(int apiVersion, refimport_t *rimp);

SYS_PROCESS_PARAM(1001, 0x100000);

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

/* All three variants share /dev_hdd0/data/ioq3 with per-mod subdirs. */
#if defined(STANDALONEOA)
#  define PS3_GAMEDIR       "baseoa"
#  define PS3_LOG_SUFFIX    "log_oa.txt"
#  define PS3_TITLE         "openarena-PS3"
#elif defined(STANDALONETA)
#  define PS3_GAMEDIR       "missionpack"
#  define PS3_LOG_SUFFIX    "log_ta.txt"
#  define PS3_TITLE         "teamarena-PS3"
#else
#  define PS3_GAMEDIR       "baseq3"
#  define PS3_LOG_SUFFIX    "log.txt"
#  define PS3_TITLE         "ioquake3-PS3"
#endif

/* ps3_log() must stay defined in release too -- other TUs link it via extern. */
#ifdef PS3_DEBUG
static const char *ps3_log_path = "/dev_hdd0/data/ioq3/" PS3_LOG_SUFFIX;
#endif

void ps3_log(const char *msg)
{
#ifdef PS3_DEBUG
    FILE *f = fopen(ps3_log_path, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
#else
    (void)msg;
#endif
}

#ifdef PS3_DEBUG
#define PS3LOG(fmt, ...) do { \
    char _lb[256]; \
    snprintf(_lb, sizeof(_lb), fmt, ##__VA_ARGS__); \
    ps3_log(_lb); \
} while (0)
#else
#define PS3LOG(fmt, ...) ((void)0)
#endif

/* Try HDD, fall back to USB. Each variant probes its own pak0.pk3. */
static const char *ps3_base_path  = "/dev_hdd0/data/ioq3";
static const char *ps3_usb_path   = "/dev_usb000/quake3";

static qboolean PS3_SetupFilesystem(void)
{
    const char *probe_hdd = "/dev_hdd0/data/ioq3/" PS3_GAMEDIR "/pak0.pk3";
    const char *probe_usb = "/dev_usb000/quake3/"  PS3_GAMEDIR "/pak0.pk3";

    FILE *f = fopen(probe_hdd, "rb");
    if (f) {
        fclose(f);
        chdir(ps3_base_path);
        PS3LOG("Using HDD path: %s", ps3_base_path);
        return qtrue;
    }

    f = fopen(probe_usb, "rb");
    if (f) {
        fclose(f);
        chdir(ps3_usb_path);
        ps3_base_path = ps3_usb_path;
        PS3LOG("Using USB path: %s", ps3_usb_path);
        return qtrue;
    }

    printf("FATAL: " PS3_GAMEDIR "/pak0.pk3 not found on HDD or USB\n");
    PS3LOG("FATAL: " PS3_GAMEDIR "/pak0.pk3 not found");
    return qfalse;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, ps3_sysutil_callback, NULL);

    printf(PS3_TITLE " starting...\n");

#ifdef PS3_DEBUG
    { FILE *f = fopen(ps3_log_path, "w"); if (f) fclose(f); }
    PS3LOG("main() reached");
#endif

    if (!PS3_SetupFilesystem()) {
        printf("FATAL: could not find game data. Halting.\n");
        PS3LOG("FATAL: filesystem setup failed");
        while (ps3_running) {
            sysUtilCheckCallback();
        }
        return 1;
    }
    PS3LOG("Filesystem ready");

    PS3_Input_Init();
    printf("[ps3] Input OK\n");
    PS3_OSK_Init();
    PS3_Snd_Init();
    printf("[ps3] Audio OK\n");

    /* RSX must be up before Com_Init -- CL_Init issues BeginFrame immediately. */
    PS3_RSX_Init();
    printf("[ps3] RSX init done\n");

    /* Pre-init renderer so Com_Printf -> SCR_UpdateScreen is safe. */
    extern refexport_t re;
    refexport_t *ref = GetRefAPI(REF_API_VERSION, NULL);
    if (ref) re = *ref;

    /* Upstream sys_main.c's main() is replaced, so do this ourselves. */
    extern void Sys_PlatformInit(void);
    Sys_PlatformInit();

    /* Cmdline values beat Cvar_Get defaults; Cvar_Set from Sys_Init is
     * overwritten by config load. Strip quotes/ctrl so we don't break parsing. */
    extern char *Sys_GetCurrentUser(void);
    static char ps3_nick[64];
    {
        const char *src = Sys_GetCurrentUser();
        size_t o = 0;
        if (src) {
            for (size_t i = 0; src[i] && o < sizeof(ps3_nick) - 1; i++) {
                unsigned char c = (unsigned char)src[i];
                if (c == '"' || c == '\\' || c == ';' || c < 0x20) continue;
                ps3_nick[o++] = (char)c;
            }
        }
        ps3_nick[o] = '\0';
        if (ps3_nick[0] == '\0')
            snprintf(ps3_nick, sizeof(ps3_nick), "%s", "player");
        PS3LOG("[user] cmdline nick='%s'", ps3_nick);
    }

    static char cmdline[2048];
    snprintf(cmdline, sizeof(cmdline),
        "+set fs_basepath %s "
        "+set fs_homepath %s "
        "+set fs_steampath \"\" "
        "+set fs_gogpath \"\" "
        "+set fs_game " PS3_GAMEDIR " "
        "+set name \"%s\" "
        "+set com_hunkMegs 96 "
        "+set com_zoneMegs 24 "
        "+set r_mode -1 "
        "+set r_customwidth 1280 "
        "+set r_customheight 720 "
        "+set r_picmip 1 "
        "+set r_dynamic 1 "
        "+set r_flares 0 "
        "+set r_fastsky 0 "
        "+set r_lodbias 1 "
        "+set r_subdivisions 12 "
        "+set r_simpleMipMaps 1 "
        "+set r_drawSun 0 "
        "+set r_primitives 2 "
        "+set com_maxfps 0 "          /* vsync paces at 60 Hz */
        "+set pmove_fixed 1 "
        "+set s_khz 48 "
        "+set com_soundMegs 8 "
        "+set sv_pure 0 "
        "+set g_doWarmup 0 "          /* Q3 warmup loops on PS3 due to slow cgame load */
        "+set sv_maxclients 8 "
        "+set in_joystick 1 "
        "+set in_joystickUseAnalog 1 "
        "+set j_pitch_axis 3 "
        "+set j_yaw_axis 4 "
        "+set net_enabled 1 "
        "+set net_port 27960 "
        "+set fraglimit 0 "
        "+set timelimit 0 "
#ifdef PS3_DEBUG
        "+set com_logfile 2",
#else
        "+set com_logfile 0",
#endif
        ps3_base_path, ps3_base_path, ps3_nick
    );

    /* Hashed by CL_UpdateGUID into cl_guid; OA QVM rejects empty guid. */
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

    printf("[ps3] Calling Com_Init...\n");
    Com_Init(cmdline);
    printf("[ps3] Com_Init done\n");

    /* PSL1GHT net module must load before ioq3's NET_Init. */
    {
        sysModuleLoad(SYSMODULE_NET);
        PS3_Net_Init();
    }

    NET_Init();
    printf("[ps3] NET_Init done\n");

    while (ps3_running) {
        sysUtilCheckCallback();
        PS3_Input_Frame();

        if (PS3_Input_QuitPressed()) {
            Com_Quit_f();
            break;
        }

        Com_Frame();
    }

    PS3_OSK_Shutdown();
    PS3_Snd_Shutdown();
    PS3_Input_Shutdown();
    PS3_RSX_Shutdown();
    NET_Shutdown();
    PS3_Net_Shutdown();

    PS3LOG("Clean shutdown");
    return 0;
}
