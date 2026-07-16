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
extern void Com_WriteConfiguration(void);

#ifdef CLASSIC
#include "zpack_classic_embedded.h"
#elif defined(ELITEFORCE)
/* No fix-pak yet for this flavor (v1). */
#else
#include "pak9_ps3_embedded.h"
#ifdef STANDALONETA
#include "pak4_ps3_embedded.h"
#endif
#endif

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

#if defined(STANDALONEOA)
#  define PS3_GAMEDIR       "baseoa"
#  define PS3_LOG_SUFFIX    "log_oa.txt"
#  define PS3_TITLE         "openarena-PS3"
#elif defined(STANDALONETA)
#  define PS3_GAMEDIR       "missionpack"
#  define PS3_LOG_SUFFIX    "log_ta.txt"
#  define PS3_TITLE         "teamarena-PS3"
#elif defined(ELITEFORCE)
#  define PS3_GAMEDIR       "baseEF"
#  define PS3_LOG_SUFFIX    "log_ef.txt"
#  define PS3_TITLE         "eliteforce-PS3"
#else
#  define PS3_GAMEDIR       "baseq3"
#  define PS3_LOG_SUFFIX    "log.txt"
#  define PS3_TITLE         "ioquake3-PS3"
#endif

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

/* Try HDD, fall back to USB (any of usb000..usb006 -- a stick can enumerate
 * on any port depending on how many devices are plugged in). */
static char ps3_base_path[64] = "/dev_hdd0/data/ioq3";

static qboolean PS3_SetupFilesystem(void)
{
    char probe[128];
    char usb_path[64];

    snprintf(probe, sizeof(probe), "/dev_hdd0/data/ioq3/" PS3_GAMEDIR "/pak0.pk3");
    FILE *f = fopen(probe, "rb");
    if (f) {
        fclose(f);
        chdir(ps3_base_path);
        PS3LOG("Using HDD path: %s", ps3_base_path);
        return qtrue;
    }

    for (int usb = 0; usb < 7; usb++) {
        snprintf(usb_path, sizeof(usb_path), "/dev_usb%03d/quake3", usb);
        snprintf(probe, sizeof(probe), "%s/" PS3_GAMEDIR "/pak0.pk3", usb_path);

        f = fopen(probe, "rb");
        if (f) {
            fclose(f);
            chdir(usb_path);
            snprintf(ps3_base_path, sizeof(ps3_base_path), "%s", usb_path);
            PS3LOG("Using USB path: %s", ps3_base_path);
            return qtrue;
        }
    }

    printf("FATAL: " PS3_GAMEDIR "/pak0.pk3 not found on HDD or USB\n");
    PS3LOG("FATAL: " PS3_GAMEDIR "/pak0.pk3 not found");
    return qfalse;
}

static unsigned int PS3_FileChecksum(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0xFFFFFFFFu;
    unsigned int csum = 0;
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) csum += buf[i];
    }
    fclose(f);
    return csum;
}

#if !defined(STANDALONEOA) && !defined(CLASSIC) && !defined(ELITEFORCE)
static void PS3_ExtractBundledPak9(void)
{
    char destpath[256];
    snprintf(destpath, sizeof(destpath), "%s/baseq3/pak9-ps3.pk3", ps3_base_path);

    if (PS3_FileChecksum(destpath) == pak9_ps3_data_csum) {
        PS3LOG("pak9-ps3.pk3 up to date, skipping");
        return;
    }

    FILE *f = fopen(destpath, "wb");
    if (!f) {
        printf("[ps3] WARNING: could not write %s\n", destpath);
        return;
    }
    size_t written = fwrite(pak9_ps3_data, 1, pak9_ps3_data_len, f);
    fclose(f);
    if (written == pak9_ps3_data_len)
        printf("[ps3] Extracted pak9-ps3.pk3 -> baseq3\n");
    else
        printf("[ps3] WARNING: pak9-ps3.pk3 write incomplete (%u/%u)\n",
               (unsigned)written, pak9_ps3_data_len);
}
#endif /* !STANDALONEOA && !CLASSIC && !ELITEFORCE */

#ifdef STANDALONETA
static void PS3_ExtractBundledPak4(void)
{
    char destpath[256];
    snprintf(destpath, sizeof(destpath), "%s/missionpack/pak4-ps3.pk3", ps3_base_path);

    if (PS3_FileChecksum(destpath) == pak4_ps3_data_csum) {
        PS3LOG("pak4-ps3.pk3 up to date, skipping");
        return;
    }

    FILE *f = fopen(destpath, "wb");
    if (!f) {
        printf("[ps3] WARNING: could not write %s\n", destpath);
        return;
    }
    size_t written = fwrite(pak4_ps3_data, 1, pak4_ps3_data_len, f);
    fclose(f);
    if (written == pak4_ps3_data_len)
        printf("[ps3] Extracted pak4-ps3.pk3 -> missionpack\n");
    else
        printf("[ps3] WARNING: pak4-ps3.pk3 write incomplete (%u/%u)\n",
               (unsigned)written, pak4_ps3_data_len);
}
#endif

#ifdef CLASSIC
static void PS3_ExtractBundledZpackClassic(void)
{
    char destpath[256];
    snprintf(destpath, sizeof(destpath), "%s/baseq3/zpack-classic.pk3", ps3_base_path);

    if (PS3_FileChecksum(destpath) == zpack_classic_data_csum) {
        PS3LOG("zpack-classic.pk3 up to date, skipping");
        return;
    }

    FILE *f = fopen(destpath, "wb");
    if (!f) {
        printf("[ps3] WARNING: could not write %s\n", destpath);
        return;
    }
    size_t written = fwrite(zpack_classic_data, 1, zpack_classic_data_len, f);
    fclose(f);
    if (written == zpack_classic_data_len)
        printf("[ps3] Extracted zpack-classic.pk3 -> baseq3\n");
    else
        printf("[ps3] WARNING: zpack-classic.pk3 write incomplete (%u/%u)\n",
               (unsigned)written, zpack_classic_data_len);
}
#endif

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

#if !defined(STANDALONEOA) && !defined(CLASSIC) && !defined(ELITEFORCE)
    PS3_ExtractBundledPak9();
#endif
#ifdef STANDALONETA
    PS3_ExtractBundledPak4();
#endif
#ifdef CLASSIC
    PS3_ExtractBundledZpackClassic();
#endif

    PS3_Input_Init();
    printf("[ps3] Input OK\n");
    PS3_OSK_Init();

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

    /* Cmdline wins over Cvar_Get defaults. Cvar_Set in Sys_Init gets clobbered
     * by config load, so don't bother. Strip quotes/ctrl so we don't break parsing. */
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
        "+set com_hunkMegs 96 "
        "+set com_zoneMegs 24 "
        "+set max_routingcache 6144 "
        "+set r_mode -1 "
        "+set r_customwidth 1280 "
        "+set r_customheight 720 "
        "+set r_picmip 1 "
        "+set r_dynamic 1 "
        "+set r_flares 0 "
        "+set r_fastsky 0 "
        "+set r_lodbias 0 "
        "+set r_subdivisions 4 "
        "+set r_drawSun 0 "
        "+set r_primitives 2 "
        "+set com_maxfps 60 "         /* uncapped floods the GCM buffer and stalls mid-frame, keep the cap */
        "+set pmove_fixed 1 "
        "+set s_khz 48 "
        "+set com_soundMegs 8 "
        "+set sv_pure 0 "
        "+set cl_allowDownload 1 "
        "+set g_doWarmup 0 "          /* warmup map_restart on slow cgame reload = serverId race, don't turn this on */
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
        ps3_base_path, ps3_base_path
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

    /* Only apply the XMB nick if name is still the hardcoded default —
       otherwise we'd stomp an in-game rename. */
    {
        cvar_t *cv = Cvar_Get("name", "UnnamedPlayer", CVAR_USERINFO | CVAR_ARCHIVE);
        if (Q_stricmp(cv->string, "UnnamedPlayer") == 0) {
            Cvar_Set("name", ps3_nick);
        }
    }

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

    /* XMB exit path: sysutil callback set ps3_running=0, Com_Quit_f never ran,
       so flush config and shut down manually here. */
    if (!ps3_running) {
        Com_WriteConfiguration();
        SV_Shutdown("Server quit");
        CL_Shutdown("Client quit", qtrue, qtrue);
        Com_Shutdown();
        FS_Shutdown(qtrue);
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
