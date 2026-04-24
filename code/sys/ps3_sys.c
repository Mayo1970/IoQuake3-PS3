/*
 * ioquake3-PS3: sys/ps3_sys.c
 *
 * Implements the complete Sys_* interface required by ioQ3 for the PS3
 * platform.  This file replaces upstream sys_main.c + sys_unix.c.
 * Also provides CON_*, mmap/munmap, fill_fopen_filefunc, and linker wraps.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <malloc.h>
#include <math.h>

#include <ppu-types.h>
#include <sys/systime.h>
#include <sysutil/sysutil.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "sys/sys_local.h"
#include "keycodes.h"

/* PS3-specific subsystems */
#include "../sys/ps3_glimp.h"
#include "../input/ps3_input.h"
#include "../audio/ps3_snd.h"

/* Renderer public interface */
#include "renderercommon/tr_types.h"
#include "renderercommon/tr_public.h"

/* External logging from ps3_main.c */
extern void ps3_log(const char *msg);

/* Forward declarations */
void Sys_SetFloatEnv(void);

/* MAX_FOUND_FILES -- defined in upstream sys_unix.c, not in a header */
#ifndef MAX_FOUND_FILES
#define MAX_FOUND_FILES 0x1000
#endif

/* ==================================================================
 * Binary path / install path
 * ================================================================== */
static char binaryPath[MAX_OSPATH]  = { 0 };
static char installPath[MAX_OSPATH] = { 0 };

void Sys_SetBinaryPath(const char *path)
{
    Q_strncpyz(binaryPath, path, sizeof(binaryPath));
}

char *Sys_BinaryPath(void)
{
    return binaryPath;
}

void Sys_SetDefaultInstallPath(const char *path)
{
    Q_strncpyz(installPath, path, sizeof(installPath));
}

char *Sys_DefaultInstallPath(void)
{
    if (*installPath)
        return installPath;
    return Sys_Cwd();
}

char *Sys_DefaultAppPath(void)
{
    return Sys_BinaryPath();
}

/* ==================================================================
 * Filesystem paths -- all point to USRDIR on PS3 HDD
 * ================================================================== */
static const char *ps3_basepath = "/dev_hdd0/game/IOQ3PS300/USRDIR";

char *Sys_Cwd(void)
{
    static char cwd[MAX_OSPATH];
    if (getcwd(cwd, sizeof(cwd) - 1) == NULL) {
        Q_strncpyz(cwd, ps3_basepath, sizeof(cwd));
    }
    return cwd;
}

qboolean Sys_Mkdir(const char *path)
{
    if (mkdir(path, 0777) == 0)
        return qtrue;

    /* mkdir failed -- check if directory already exists.
     * PSL1GHT's mkdir on PS3 mount points (e.g. /dev_hdd0) may return
     * EACCES or EPERM instead of EEXIST, so a simple errno check is
     * not sufficient. Fall back to stat(). */
    if (errno == EEXIST)
        return qtrue;

    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return qtrue;

    return qfalse;
}

FILE *Sys_FOpen(const char *ospath, const char *mode)
{
    return fopen(ospath, mode);
}

FILE *Sys_Mkfifo(const char *ospath)
{
    (void)ospath;
    return NULL; /* no FIFOs on PS3 */
}

char *Sys_DefaultHomePath(void)
{
    return Sys_Cwd();
}

char *Sys_DefaultHomeConfigPath(void) { return Sys_Cwd(); }
char *Sys_DefaultHomeDataPath(void)   { return Sys_Cwd(); }
char *Sys_DefaultHomeStatePath(void)  { return Sys_Cwd(); }

char *Sys_SteamPath(void)          { return ""; }
char *Sys_GogPath(void)            { return ""; }
char *Sys_MicrosoftStorePath(void) { return ""; }

/* ==================================================================
 * Time
 * ================================================================== */
static u64 ps3_time_base = 0;

int Sys_Milliseconds(void)
{
    u64 now;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    now = (u64)tv.tv_sec * 1000000ULL + (u64)tv.tv_usec;

    if (ps3_time_base == 0) {
        ps3_time_base = now;
    }
    return (int)((now - ps3_time_base) / 1000ULL);
}

int Sys_FileTime(char *path)
{
    struct stat st;
    if (stat(path, &st) == -1)
        return -1;
    return (int)st.st_mtime;
}

/* ==================================================================
 * Process management
 * ================================================================== */
int Sys_PID(void)
{
    return 1; /* single process on PS3 */
}

qboolean Sys_PIDIsRunning(int pid)
{
    (void)pid;
    return qtrue;
}

void Sys_InitPIDFile(const char *gamedir)
{
    (void)gamedir;
    /* No PID files on PS3 */
}

void Sys_RemovePIDFile(const char *gamedir)
{
    (void)gamedir;
}

/* ==================================================================
 * Path utilities
 * ================================================================== */
const char *Sys_Basename(char *path)
{
    char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

const char *Sys_Dirname(char *path)
{
    static char dir[MAX_OSPATH];
    char *p;

    Q_strncpyz(dir, path, sizeof(dir));
    p = strrchr(dir, '/');
    if (p) {
        *p = '\0';
    } else {
        dir[0] = '.';
        dir[1] = '\0';
    }
    return dir;
}

/* ==================================================================
 * Directory listing
 * ================================================================== */
void Sys_ListFilteredFiles(const char *basedir, char *subdirs,
                           char *filter, char **list, int *numfiles)
{
    char search[MAX_OSPATH];
    char newsubdirs[MAX_OSPATH];
    char filename[MAX_OSPATH];
    DIR *fdir;
    struct dirent *d;
    struct stat st;

    if (*subdirs) {
        Com_sprintf(search, sizeof(search), "%s/%s", basedir, subdirs);
    } else {
        Com_sprintf(search, sizeof(search), "%s", basedir);
    }

    fdir = opendir(search);
    if (!fdir) return;

    while ((d = readdir(fdir)) != NULL) {
        if (*numfiles >= MAX_FOUND_FILES - 1) break;
        Com_sprintf(filename, sizeof(filename), "%s/%s", search, d->d_name);
        if (stat(filename, &st) == -1) continue;

        if (S_ISDIR(st.st_mode)) {
            if (d->d_name[0] == '.') continue;
            if (*subdirs) {
                Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s/%s", subdirs, d->d_name);
            } else {
                Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s", d->d_name);
            }
            Sys_ListFilteredFiles(basedir, newsubdirs, filter, list, numfiles);
        }

        if (*numfiles >= MAX_FOUND_FILES - 1) break;

        Com_sprintf(filename, sizeof(filename), "%s/%s",
                    *subdirs ? subdirs : "", d->d_name);
        if (!Com_FilterPath(filter, filename, qfalse)) continue;

        list[*numfiles] = CopyString(filename);
        (*numfiles)++;
    }
    closedir(fdir);
}

char **Sys_ListFiles(const char *directory, const char *extension,
                     char *filter, int *numfiles, qboolean wantsubs)
{
    struct dirent *d;
    DIR *fdir;
    qboolean dironly = wantsubs;
    char search[MAX_OSPATH];
    int nfiles = 0;
    char **listCopy;
    char *list[MAX_FOUND_FILES];
    int extLen;

    if (filter) {
        nfiles = 0;
        Sys_ListFilteredFiles(directory, "", filter, list, &nfiles);
        *numfiles = nfiles;
        if (!nfiles) return NULL;
        listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
        for (int i = 0; i < nfiles; i++) {
            listCopy[i] = list[i];
        }
        listCopy[nfiles] = NULL;
        return listCopy;
    }

    if (!extension) extension = "";
    if (extension[0] == '/' && extension[1] == 0) {
        extension = "";
        dironly = qtrue;
    }
    extLen = strlen(extension);

    fdir = opendir(directory);
    if (!fdir) {
        *numfiles = 0;
        return NULL;
    }

    while ((d = readdir(fdir)) != NULL) {
        if (nfiles >= MAX_FOUND_FILES - 1) break;
        if (d->d_name[0] == '.' &&
            (d->d_name[1] == '\0' ||
             (d->d_name[1] == '.' && d->d_name[2] == '\0'))) {
            continue;
        }

        if (*extension) {
            int nameLen = strlen(d->d_name);
            if (nameLen < extLen ||
                Q_stricmp(d->d_name + nameLen - extLen, extension)) {
                continue;
            }
        }

        if (dironly) {
            snprintf(search, sizeof(search), "%s/%s", directory, d->d_name);
            struct stat st;
            if (stat(search, &st) == -1) continue;
            if (!S_ISDIR(st.st_mode)) continue;
        }

        list[nfiles] = CopyString(d->d_name);
        nfiles++;
    }
    closedir(fdir);

    *numfiles = nfiles;
    if (!nfiles) return NULL;

    listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
    for (int i = 0; i < nfiles; i++) {
        listCopy[i] = list[i];
    }
    listCopy[nfiles] = NULL;

    return listCopy;
}

void Sys_FreeFileList(char **list)
{
    int i;
    if (!list) return;
    for (i = 0; list[i]; i++) {
        Z_Free(list[i]);
    }
    Z_Free(list);
}

/* ==================================================================
 * Error / Print / Dialog
 * ================================================================== */
void Sys_Print(const char *msg)
{
    fputs(msg, stdout);
    fflush(stdout);
    ps3_log(msg);
}

void Sys_Error(const char *error, ...)
{
    va_list ap;
    char msg[4096];

    va_start(ap, error);
    vsnprintf(msg, sizeof(msg), error, ap);
    va_end(ap);

    printf("\n\n=============================\n");
    printf("[FATAL ERROR]\n");
    printf("%s\n", msg);
    printf("=============================\n");
    fflush(stdout);
    ps3_log(msg);

    Sys_PlatformExit();
    exit(1);
}

void Sys_Quit(void)
{
    CON_Shutdown();
    Sys_PlatformExit();
    exit(0);
}

void Sys_AnsiColorPrint(const char *msg)
{
    /* No ANSI terminals on PS3; just print the raw text */
    Sys_Print(msg);
}

dialogResult_t Sys_Dialog(dialogType_t type, const char *message, const char *title)
{
    (void)type; (void)title;
    printf("[ps3] DIALOG: %s\n", message);
    ps3_log(message);
    return DR_OK;
}

void Sys_ErrorDialog(const char *error)
{
    printf("[ps3] ERROR: %s\n", error);
    ps3_log(error);
}

/* ==================================================================
 * Random bytes
 * ================================================================== */
qboolean Sys_RandomBytes(byte *string, int len)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned int seed = (unsigned int)(tv.tv_sec ^ tv.tv_usec);
    for (int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        string[i] = (byte)((seed >> 16) & 0xFF);
    }
    return qtrue;
}

/* ==================================================================
 * User info
 * ================================================================== */
char *Sys_GetCurrentUser(void)
{
    return "player";
}

/* ==================================================================
 * CPU features
 * ================================================================== */
cpuFeatures_t Sys_GetProcessorFeatures(void)
{
    return (cpuFeatures_t)CF_ALTIVEC;
}

/* ==================================================================
 * Platform init / exit / misc
 * ================================================================== */
void Sys_PlatformInit(void)
{
    Sys_SetFloatEnv();
}

void Sys_PlatformExit(void)
{
    /* Cleanup handled in ps3_main.c */
}

void Sys_SetFloatEnv(void)
{
    /* Ensure FPU is in a known state */
#ifdef __GNUC__
    /* PPC: set FPSCR to default (round-to-nearest, no exceptions) */
    union { unsigned long long u; double d; } fpscr;
    fpscr.u = 0;
    __asm__ __volatile__("mtfsf 255,%0" :: "f"(fpscr.d));
#endif
}

void Sys_SetEnv(const char *name, const char *value)
{
    if (value && *value)
        setenv(name, value, 1);
    else
        unsetenv(name);
}

qboolean Sys_LowPhysicalMemory(void)
{
    return qfalse;
}

void Sys_Sleep(int msec)
{
    if (msec <= 0) return;
    usleep(msec * 1000);
}

qboolean Sys_SetMaxFileLimit(void)
{
    return qtrue; /* not applicable on PS3 */
}

qboolean Sys_OpenFolderInPlatformFileManager(const char *path)
{
    (void)path;
    return qfalse;
}

qboolean Sys_OpenFolderInFileManager(const char *path, qboolean create)
{
    (void)path; (void)create;
    return qfalse;
}

/* ==================================================================
 * Sys_Init -- called from Com_Init
 * ================================================================== */
void Sys_In_Restart_f(void)
{
    /* No input restart on PS3 */
}

void Sys_Init(void)
{
    Cmd_AddCommand("in_restart", Sys_In_Restart_f);

    Cvar_Set("username", Sys_GetCurrentUser());
    Cvar_Set("arch", ARCH_STRING);
}

/* ==================================================================
 * Console input / clipboard
 * ================================================================== */
char *Sys_ConsoleInput(void)
{
    return NULL; /* no TTY on PS3 */
}

char *Sys_GetClipboardData(void)
{
    return NULL; /* no clipboard on PS3 */
}

/* ==================================================================
 * DLL extension check
 * ================================================================== */
qboolean Sys_DllExtension(const char *name)
{
    const char *p;
    if (!name || !*name) return qfalse;
    p = strrchr(name, '.');
    if (p && !Q_stricmp(p, DLL_EXT))
        return qtrue;
    return qfalse;
}

/* ==================================================================
 * Dynamic library loading -- disabled on PS3.
 * ioQ3 uses these for renderer DLL and game DLLs.
 * We link everything statically.
 * ================================================================== */
void *Sys_LoadDll(const char *name, qboolean useSystemLib)
{
    (void)name; (void)useSystemLib;
    return NULL;
}

void * QDECL Sys_LoadGameDll(const char *name,
                              vmMainProc *entryPoint,
                              intptr_t (*systemcalls)(intptr_t, ...))
{
    (void)name; (void)entryPoint; (void)systemcalls;
    return NULL;
}

void Sys_UnloadDll(void *dllHandle)
{
    (void)dllHandle;
}

void *Sys_LoadFunction(void *dllHandle, const char *name)
{
    (void)dllHandle; (void)name;
    return NULL;
}

char *Sys_GetDLLName(const char *name)
{
    (void)name;
    return NULL;
}

/* ==================================================================
 * Command-line parsing (N/A on PS3)
 * ================================================================== */
void Sys_ParseArgs(int argc, char **argv)
{
    (void)argc; (void)argv;
}

/* ==================================================================
 * Console stubs (no TTY on PS3)
 * ================================================================== */
void CON_Shutdown(void) {}
void CON_Init(void) {}
char *CON_Input(void) { return NULL; }
void CON_Print(const char *msg) { printf("%s", msg); }

/* con_log.c functions -- provide stubs if not linking con_log.c */
unsigned int CON_LogSize(void) { return 0; }
unsigned int CON_LogWrite(const char *in) { (void)in; return 0; }
unsigned int CON_LogRead(char *out, unsigned int outSize) { (void)out; (void)outSize; return 0; }

/* ==================================================================
 * Signal handling -- PS3 homebrew doesn't use Unix signals
 * ================================================================== */
void Sys_SigHandler(int signal)
{
    (void)signal;
    Sys_Quit();
}

/* ==================================================================
 * GLimp stubs -- Sys_ wrappers called from sys_main.c
 * Real RSX implementation is in ps3_glimp.c
 * ================================================================== */
void Sys_GLimpInit(void)
{
    /* RSX init happens in PS3_RSX_Init() called from ps3_main.c */
}

void Sys_GLimpSafeInit(void)
{
    /* Same as above */
}

/* ==================================================================
 * mmap / munmap for QVM / hunk allocation
 * PS3 homebrew: just use memalign.
 * ================================================================== */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;
    void *p = memalign(16, length);
    if (p) memset(p, 0, length);
    return p ? p : (void *)(intptr_t)-1;
}

int munmap(void *addr, size_t length)
{
    (void)length;
    if (addr && addr != (void *)(intptr_t)-1)
        free(addr);
    return 0;
}

/* ==================================================================
 * fill_fopen_filefunc -- minizip file callback interface
 * Used by unzip.c for pk3 reading.
 * ================================================================== */
#include "qcommon/unzip.h"

static voidpf ZCALLBACK fopen_file_func(voidpf opaque, const char *filename, int mode)
{
    (void)opaque;
    const char *fmode = "rb";
    if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) == ZLIB_FILEFUNC_MODE_READ)
        fmode = "rb";
    else if (mode & ZLIB_FILEFUNC_MODE_EXISTING)
        fmode = "r+b";
    else if (mode & ZLIB_FILEFUNC_MODE_CREATE)
        fmode = "wb";
    return (voidpf)fopen(filename, fmode);
}

static uLong ZCALLBACK fread_file_func(voidpf opaque, voidpf stream, void *buf, uLong size)
{
    (void)opaque;
    return (uLong)fread(buf, 1, (size_t)size, (FILE *)stream);
}

static uLong ZCALLBACK fwrite_file_func(voidpf opaque, voidpf stream, const void *buf, uLong size)
{
    (void)opaque;
    return (uLong)fwrite(buf, 1, (size_t)size, (FILE *)stream);
}

static long ZCALLBACK ftell_file_func(voidpf opaque, voidpf stream)
{
    (void)opaque;
    return ftell((FILE *)stream);
}

static long ZCALLBACK fseek_file_func(voidpf opaque, voidpf stream, uLong offset, int origin)
{
    (void)opaque;
    int whence;
    switch (origin) {
        case ZLIB_FILEFUNC_SEEK_CUR: whence = SEEK_CUR; break;
        case ZLIB_FILEFUNC_SEEK_END: whence = SEEK_END; break;
        case ZLIB_FILEFUNC_SEEK_SET: whence = SEEK_SET; break;
        default: return -1;
    }
    return (long)fseek((FILE *)stream, (long)offset, whence);
}

static int ZCALLBACK fclose_file_func(voidpf opaque, voidpf stream)
{
    (void)opaque;
    return fclose((FILE *)stream);
}

static int ZCALLBACK ferror_file_func(voidpf opaque, voidpf stream)
{
    (void)opaque;
    return ferror((FILE *)stream);
}

void fill_fopen_filefunc(zlib_filefunc_def *pzlib_filefunc_def)
{
    pzlib_filefunc_def->zopen_file  = fopen_file_func;
    pzlib_filefunc_def->zread_file  = fread_file_func;
    pzlib_filefunc_def->zwrite_file = fwrite_file_func;
    pzlib_filefunc_def->ztell_file  = ftell_file_func;
    pzlib_filefunc_def->zseek_file  = fseek_file_func;
    pzlib_filefunc_def->zclose_file = fclose_file_func;
    pzlib_filefunc_def->zerror_file = ferror_file_func;
    pzlib_filefunc_def->opaque      = NULL;
}

/* ==================================================================
 * AVI stubs -- no video recording on PS3
 * ================================================================== */
qboolean CL_VideoRecording(void)               { return qfalse; }
qboolean CL_OpenAVIForWriting(const char *f)   { (void)f; return qfalse; }
qboolean CL_CloseAVI(void)                     { return qfalse; }
void     CL_TakeVideoFrame(void)               { }
void     CL_WriteAVIVideoFrame(const byte *d, int s) { (void)d; (void)s; }
void     CL_WriteAVIAudioFrame(const byte *d, int s) { (void)d; (void)s; }

/* ==================================================================
 * MD5 stub -- no GUID/md5 verification needed on PS3
 * ================================================================== */
char *Com_MD5File(const char *filename, int length,
                  const char *prefix, int pLen)
{
    (void)filename; (void)length; (void)prefix; (void)pLen;
    return "";
}

/* ==================================================================
 * Linker wraps -- intercept specific ioQ3 functions
 *
 * __wrap_CL_GenerateQKey: bypass CD key dialog
 * __wrap_Com_Printf: mirror output to log file
 * ================================================================== */

/* Bypass CD key generation dialog */
void __wrap_CL_GenerateQKey(void)
{
    /* qkey file is created in ps3_main.c, nothing to do */
}

/* Mirror Com_Printf to log file.
 * The real Com_Printf calls Sys_Print, which already calls ps3_log.
 * We only need to forward to __real_Com_Printf here. The wrap exists
 * so we can intercept the call if needed (e.g., filtering, redirection). */
extern void QDECL __real_Com_Printf(const char *fmt, ...) __attribute__((format(printf,1,2)));
void QDECL __wrap_Com_Printf(const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* Call real Com_Printf (which calls Sys_Print -> ps3_log) */
    __real_Com_Printf("%s", buf);
}

