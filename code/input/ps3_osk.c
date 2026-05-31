/* ps3_osk.c -- PS3 system on-screen keyboard via sysutil/osk. */

#include <stdio.h>
#include <string.h>

#include <ppu-types.h>
#include <sysutil/osk.h>
#include <sysutil/sysutil.h>
#include <sys/memory.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "keycodes.h"
#include "../input/ps3_osk.h"

extern void ps3_log(const char *msg);

/* Firmware docs recommend 4 MB for OSK container */
#define OSK_CONTAINER_SIZE  (4 * 1024 * 1024)

#define OSK_MAX_TEXT  256

typedef enum {
    OSK_STATE_IDLE,
    OSK_STATE_OPEN,
    OSK_STATE_RUNNING,
    OSK_STATE_DONE,
    OSK_STATE_CLOSING
} oskState_t;

static oskState_t          osk_state = OSK_STATE_IDLE;
static sys_mem_container_t osk_container = 0;
static qboolean            osk_container_valid = qfalse;

static u16 osk_message[OSK_MAX_TEXT];  /* UCS-2 prompt */
static u16 osk_result[OSK_MAX_TEXT];   /* UCS-2 result */
static oskCallbackReturnParam osk_return;
static qboolean osk_auto_submit   = qfalse;  /* send Enter after result */
static qboolean osk_prepend_slash = qfalse;  /* prepend '/' so console treats it as command */

static void ascii_to_ucs2(const char *src, u16 *dst, int maxlen)
{
    int i;
    for (i = 0; i < maxlen - 1 && src[i] != '\0'; i++)
        dst[i] = (u16)(unsigned char)src[i];
    dst[i] = 0;
}

/* Inject UCS-2 result as SE_CHAR events (non-ASCII skipped, Q3 is ASCII-only) */
static void osk_inject_result(const u16 *str, int len)
{
    int i;
    if (osk_prepend_slash)
        Com_QueueEvent(0, SE_CHAR, '/', 0, 0, NULL);
    for (i = 0; i < len; i++) {
        int ch = (int)str[i];
        if (ch >= 32 && ch <= 126)
            Com_QueueEvent(0, SE_CHAR, ch, 0, 0, NULL);
    }
    if (osk_auto_submit) {
        Com_QueueEvent(0, SE_KEY, K_ENTER, qtrue, 0, NULL);
        Com_QueueEvent(0, SE_KEY, K_ENTER, qfalse, 0, NULL);
    }
}

void PS3_OSK_Init(void)
{
    s32 ret;

    osk_state = OSK_STATE_IDLE;
    osk_container_valid = qfalse;

    ret = sysMemContainerCreate(&osk_container, OSK_CONTAINER_SIZE);
    if (ret != 0) {
        ps3_log("[osk] sysMemContainerCreate failed");
        return;
    }
    osk_container_valid = qtrue;
    ps3_log("[osk] init OK, 4 MB container allocated");
}

void PS3_OSK_Shutdown(void)
{
    if (osk_state != OSK_STATE_IDLE) {
        oskAbort();
        /* Best-effort: the firmware will clean up on process exit */
    }

    if (osk_container_valid) {
        sysMemContainerDestroy(osk_container);
        osk_container_valid = qfalse;
    }

    osk_state = OSK_STATE_IDLE;
    ps3_log("[osk] shutdown");
}

void PS3_OSK_Open(int maxlen, qboolean autoSubmit, qboolean prependSlash)
{
    oskParam param;
    oskInputFieldInfo input;
    s32 ret;

    if (osk_state != OSK_STATE_IDLE)
        return;
    if (!osk_container_valid)
        return;

    osk_auto_submit   = autoSubmit;
    osk_prepend_slash = prependSlash;

    if (maxlen < 1)
        maxlen = 1;
    if (maxlen > OSK_MAX_TEXT - 1)
        maxlen = OSK_MAX_TEXT - 1;

    memset(osk_message, 0, sizeof(osk_message));
    memset(osk_result, 0, sizeof(osk_result));

    ascii_to_ucs2("Enter text:", osk_message, OSK_MAX_TEXT);

    memset(&input, 0, sizeof(input));
    input.message   = osk_message;
    input.startText = osk_result;
    input.maxLength = maxlen;

    memset(&param, 0, sizeof(param));
    param.allowedPanels = OSK_PANEL_TYPE_ALPHABET |
                          OSK_PANEL_TYPE_NUMERAL  |
                          OSK_PANEL_TYPE_URL;
    param.firstViewPanel = OSK_PANEL_TYPE_ALPHABET;
    param.controlPoint.x = 0.0f;
    param.controlPoint.y = 0.0f;
    param.prohibitFlags  = OSK_PROHIBIT_RETURN; /* single-line only */
    oskSetKeyLayoutOption(OSK_FULLKEY_PANEL);
    oskSetInitialKeyLayout(OSK_INITIAL_FULLKEY_PANEL);
    oskSetInitialInputDevice(OSK_DEVICE_PAD);
    oskDisableDimmer();

    ret = oskLoadAsync(osk_container, &param, &input);
    if (ret != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[osk] oskLoadAsync failed: %d", (int)ret);
        ps3_log(buf);
        return;
    }

    osk_state = OSK_STATE_OPEN;
    ps3_log("[osk] opening keyboard");
}

void PS3_OSK_SysutilCallback(u64 status, u64 param)
{
    (void)param;

    switch (status) {
    case SYSUTIL_OSK_LOADED:
        if (osk_state == OSK_STATE_OPEN)
            osk_state = OSK_STATE_RUNNING;
        break;

    case SYSUTIL_OSK_DONE:
        if (osk_state == OSK_STATE_RUNNING)
            osk_state = OSK_STATE_DONE;
        break;

    case SYSUTIL_OSK_INPUT_ENTERED:
        /* Unused -- we don't use continuous mode */
        break;

    case SYSUTIL_OSK_INPUT_CANCELED:
        if (osk_state == OSK_STATE_RUNNING)
            osk_state = OSK_STATE_DONE;
        break;

    case SYSUTIL_OSK_UNLOADED:
        if (osk_state == OSK_STATE_CLOSING) {
            if (osk_return.res == OSK_OK && osk_return.len > 0)
                osk_inject_result(osk_return.str, osk_return.len);

            osk_state = OSK_STATE_IDLE;
            ps3_log("[osk] closed");
        }
        break;
    }

    if (osk_state == OSK_STATE_DONE) {
        memset(&osk_return, 0, sizeof(osk_return));
        osk_return.str = osk_result;
        osk_return.len = OSK_MAX_TEXT;

        oskUnloadAsync(&osk_return);
        osk_state = OSK_STATE_CLOSING;
    }
}

qboolean PS3_OSK_IsActive(void)
{
    return (osk_state != OSK_STATE_IDLE) ? qtrue : qfalse;
}
