/* ps3_sys.c -- Sys_* / CON_* / mmap / minizip / linker wraps for PS3.
 * Replaces upstream sys_main.c + sys_unix.c. */

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
#include <sys/memory.h>
#include <sysutil/sysutil.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "sys/sys_local.h"
#include "keycodes.h"

#include "../sys/ps3_glimp.h"
#include "../input/ps3_input.h"
#include "../audio/ps3_snd.h"

#include "renderercommon/tr_types.h"
#include "renderercommon/tr_public.h"

extern void ps3_log(const char *msg);

void Sys_SetFloatEnv(void);

/* MAX_FOUND_FILES is defined in upstream sys_unix.c, not in any header. */
#ifndef MAX_FOUND_FILES
#define MAX_FOUND_FILES 0x1000
#endif

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

static const char *ps3_basepath = "/dev_hdd0/data/ioq3";

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

    /* PSL1GHT's mkdir returns EACCES/EPERM for existing dirs; fall back to stat(). */
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
    return NULL;
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

int Sys_PID(void) { return 1; }
qboolean Sys_PIDIsRunning(int pid) { (void)pid; return qtrue; }
void Sys_InitPIDFile(const char *gamedir) { (void)gamedir; }
void Sys_RemovePIDFile(const char *gamedir) { (void)gamedir; }

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
            char dirbuf[MAX_OSPATH * 2];
            snprintf(dirbuf, sizeof(dirbuf), "%s/%s", directory, d->d_name);
            struct stat st;
            if (stat(dirbuf, &st) == -1) continue;
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

/* Local XMB user -> PSN nickname -> "player". Buffer sizes matter; wrong size returns INVALID_VALUE. */
char *Sys_GetCurrentUser(void)
{
    static char username[SYSUTIL_SYSTEMPARAM_NICKNAME_SIZE];
    static int  resolved = 0;

    if (!resolved) {
        char line[200];
        int  ret_u, ret_n;

        resolved = 1;
        username[0] = '\0';

        ret_u = sysUtilGetSystemParamString(
            SYSUTIL_SYSTEMPARAM_ID_CURRENT_USERNAME,
            username, SYSUTIL_SYSTEMPARAM_CURRENT_USERNAME_SIZE);
        username[SYSUTIL_SYSTEMPARAM_CURRENT_USERNAME_SIZE - 1] = '\0';
        snprintf(line, sizeof(line),
                 "[user] CURRENT_USERNAME(0x131) ret=%d name='%s'",
                 ret_u, username);
        ps3_log(line);

        ret_n = 0;
        if (ret_u != 0 || username[0] == '\0') {
            username[0] = '\0';
            ret_n = sysUtilGetSystemParamString(
                SYSUTIL_SYSTEMPARAM_ID_NICKNAME,
                username, SYSUTIL_SYSTEMPARAM_NICKNAME_SIZE);
            username[SYSUTIL_SYSTEMPARAM_NICKNAME_SIZE - 1] = '\0';
            snprintf(line, sizeof(line),
                     "[user] NICKNAME(0x113) ret=%d name='%s'",
                     ret_n, username);
            ps3_log(line);
            if (ret_n != 0)
                username[0] = '\0';
        }
    }

    if (username[0] == '\0')
        return "player";
    return username;
}

cpuFeatures_t Sys_GetProcessorFeatures(void)
{
    return (cpuFeatures_t)CF_ALTIVEC;
}

/* No free-memory syscall in PSL1GHT; binary search is the only probe. */
static unsigned ps3_probe_free_user_mem_mb(void)
{
    unsigned lo = 1, hi = 200, best = 0;

    while (lo <= hi) {
        unsigned mid = (lo + hi) / 2;
        sys_mem_container_t c;
        size_t sz = (size_t)mid * 1024u * 1024u;

        if (sysMemContainerCreate(&c, sz) == 0) {
            sysMemContainerDestroy(c);
            best = mid;
            lo = mid + 1;
        } else {
            if (mid == 0) break;
            hi = mid - 1;
        }
    }
    return best;
}

/* QVM JIT exec-from-heap probe REMOVED (2026-06-13): a verified-correct
 * probe (instructions written + cache-flushed + called via a proper ELFv1
 * descriptor — disassembly-checked) hard-froze the console at the bctrl.
 * lv2 has no executable-page protection in its user API (sys/memory.h:
 * only PROT_READ_ONLY/READ_WRITE); instruction fetch from heap faults.
 * Because failure is a freeze, not an error, the probe cannot gate
 * anything at runtime — do not reinstate it. See CLAUDE.md "PPC JIT". */

void Sys_PlatformInit(void)
{
    char line[160];
    unsigned free_mb;

    Sys_SetFloatEnv();

    /* Memory ceiling before hunk init. Keep free_mb > hunk+zone to avoid OOM. */
    free_mb = ps3_probe_free_user_mem_mb();
    snprintf(line, sizeof(line),
             "[mem] free user memory at Sys_PlatformInit: ~%u MB "
             "(current DEF_COMHUNKMEGS=%d, DEF_COMZONEMEGS=%d)",
             free_mb, DEF_COMHUNKMEGS, DEF_COMZONEMEGS);
    ps3_log(line);
    printf("%s\n", line);
}

void Sys_PlatformExit(void) {}

void Sys_SetFloatEnv(void)
{
#ifdef __GNUC__
    /* PPC FPSCR -> round-to-nearest, no exceptions. */
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

qboolean Sys_SetMaxFileLimit(void) { return qtrue; }

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

void Sys_In_Restart_f(void) {}

void Sys_Init(void)
{
    const char *user = Sys_GetCurrentUser();

    Cmd_AddCommand("in_restart", Sys_In_Restart_f);

    Cvar_Set("username", user);
    Cvar_Set("arch", ARCH_STRING);

    /* Saved q3config.cfg still wins on next boot via CVAR_ARCHIVE. */
    if (user && *user && Q_stricmp(user, "player") != 0) {
        Cvar_Set("name", user);
    }
}

char *Sys_ConsoleInput(void) { return NULL; }
char *Sys_GetClipboardData(void) { return NULL; }

qboolean Sys_DllExtension(const char *name)
{
    const char *p;
    if (!name || !*name) return qfalse;
    p = strrchr(name, '.');
    if (p && !Q_stricmp(p, DLL_EXT))
        return qtrue;
    return qfalse;
}

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

void Sys_ParseArgs(int argc, char **argv) { (void)argc; (void)argv; }

void CON_Shutdown(void) {}
void CON_Init(void) {}
char *CON_Input(void) { return NULL; }
void CON_Print(const char *msg) { printf("%s", msg); }

unsigned int CON_LogSize(void) { return 0; }
unsigned int CON_LogWrite(const char *in) { (void)in; return 0; }
unsigned int CON_LogRead(char *out, unsigned int outSize) { (void)out; (void)outSize; return 0; }

void Sys_SigHandler(int signal)
{
    (void)signal;
    Sys_Quit();
}

/* Real RSX bring-up is in PS3_RSX_Init() called from ps3_main.c. */
void Sys_GLimpInit(void) {}
void Sys_GLimpSafeInit(void) {}

/* No mmap on PS3; use memalign + memset for QVM/hunk. */
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

qboolean CL_VideoRecording(void)               { return qfalse; }
qboolean CL_OpenAVIForWriting(const char *f)   { (void)f; return qfalse; }
qboolean CL_CloseAVI(void)                     { return qfalse; }
void     CL_TakeVideoFrame(void)               { }
void     CL_WriteAVIVideoFrame(const byte *d, int s) { (void)d; (void)s; }
void     CL_WriteAVIAudioFrame(const byte *d, int s) { (void)d; (void)s; }

/* qkey created in ps3_main.c; suppress CD key dialog. */
void __wrap_CL_GenerateQKey(void) {}

extern void QDECL __real_Com_Printf(const char *fmt, ...) __attribute__((format(printf,1,2)));
void QDECL __wrap_Com_Printf(const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __real_Com_Printf("%s", buf);
}

