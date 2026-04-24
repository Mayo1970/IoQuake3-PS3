/*
 * ioquake3-PS3: input/ps3_osk.c
 * PS3 system on-screen keyboard via PSL1GHT's sysutil/osk API.
 *
 * The OSK is a firmware-level overlay that renders on top of the game.
 * It uses a dedicated 4 MB memory container and communicates state
 * changes through sysutil callbacks (SYSUTIL_OSK_*).
 *
 * Lifecycle:
 *   1. PS3_OSK_Open()  -> oskLoadAsync()       [overlay appears]
 *   2. sysutil callback -> SYSUTIL_OSK_DONE    [user pressed Enter/Cancel]
 *   3. oskUnloadAsync() -> SYSUTIL_OSK_UNLOADED [overlay gone]
 *   4. Result string converted to SE_CHAR events for Q3's input system
 *
 * Trigger: Triangle button when the engine is in a text-input context
 * (console, chat, UI field). Handled by ps3_input.c.
 */

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

/* OSK memory container -- firmware docs recommend 4 MB */
#define OSK_CONTAINER_SIZE  (4 * 1024 * 1024)

/* Maximum text length we support */
#define OSK_MAX_TEXT  256

/* OSK state machine */
typedef enum {
    OSK_STATE_IDLE,
    OSK_STATE_OPEN,        /* oskLoadAsync called, waiting for LOADED */
    OSK_STATE_RUNNING,     /* overlay visible, user is typing */
    OSK_STATE_DONE,        /* user finished, need to call oskUnloadAsync */
    OSK_STATE_CLOSING      /* oskUnloadAsync called, waiting for UNLOADED */
} oskState_t;

static oskState_t          osk_state = OSK_STATE_IDLE;
static sys_mem_container_t osk_container = 0;
static qboolean            osk_container_valid = qfalse;

/* Buffers for OSK input/output (UCS-2 / UTF-16LE, as PSL1GHT uses u16) */
static u16 osk_message[OSK_MAX_TEXT];  /* prompt shown to user */
static u16 osk_result[OSK_MAX_TEXT];   /* text returned by OSK */

/* Return param struct -- passed to oskUnloadAsync, filled by firmware */
static oskCallbackReturnParam osk_return;

/* If qtrue, send Enter after injecting the result (for chat auto-send) */
static qboolean osk_auto_submit = qfalse;

/* ----------------------------------------------------------------
 * UCS-2 helpers
 * ---------------------------------------------------------------- */

/* Convert ASCII C string to u16 (UCS-2). Truncates at maxlen-1. */
static void ascii_to_ucs2(const char *src, u16 *dst, int maxlen)
{
    int i;
    for (i = 0; i < maxlen - 1 && src[i] != '\0'; i++)
        dst[i] = (u16)(unsigned char)src[i];
    dst[i] = 0;
}

/* Convert UCS-2 result to ASCII and inject SE_CHAR events into Q3.
 * Non-ASCII codepoints (> 127) are skipped -- Q3 only handles ASCII.
 * If osk_auto_submit is set, sends Enter afterwards to submit (e.g. chat). */
static void osk_inject_result(const u16 *str, int len)
{
    int i;
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

/* ----------------------------------------------------------------
 * Init / Shutdown
 * ---------------------------------------------------------------- */

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

/* ----------------------------------------------------------------
 * Open the keyboard
 * ---------------------------------------------------------------- */

void PS3_OSK_Open(int maxlen, qboolean autoSubmit)
{
    oskParam param;
    oskInputFieldInfo input;
    s32 ret;

    if (osk_state != OSK_STATE_IDLE)
        return;
    if (!osk_container_valid)
        return;

    osk_auto_submit = autoSubmit;

    if (maxlen < 1)
        maxlen = 1;
    if (maxlen > OSK_MAX_TEXT - 1)
        maxlen = OSK_MAX_TEXT - 1;

    /* Clear buffers */
    memset(osk_message, 0, sizeof(osk_message));
    memset(osk_result, 0, sizeof(osk_result));

    ascii_to_ucs2("Enter text:", osk_message, OSK_MAX_TEXT);

    /* Input field setup */
    memset(&input, 0, sizeof(input));
    input.message   = osk_message;
    input.startText = osk_result;   /* empty initial text */
    input.maxLength = maxlen;

    /* Panel configuration */
    memset(&param, 0, sizeof(param));
    param.allowedPanels = OSK_PANEL_TYPE_ALPHABET |
                          OSK_PANEL_TYPE_NUMERAL  |
                          OSK_PANEL_TYPE_URL;
    param.firstViewPanel = OSK_PANEL_TYPE_ALPHABET;
    param.controlPoint.x = 0.0f;
    param.controlPoint.y = 0.0f;
    param.prohibitFlags  = OSK_PROHIBIT_RETURN; /* single-line: no newlines */

    /* Configure layout and keyboard type */
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

/* ----------------------------------------------------------------
 * Sysutil callback handler -- called from ps3_sysutil_callback
 * ---------------------------------------------------------------- */

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
        /* Intermediate event -- text was submitted but OSK may still be open
         * in continuous mode. We don't use continuous mode, so ignore. */
        break;

    case SYSUTIL_OSK_INPUT_CANCELED:
        if (osk_state == OSK_STATE_RUNNING)
            osk_state = OSK_STATE_DONE;
        break;

    case SYSUTIL_OSK_UNLOADED:
        if (osk_state == OSK_STATE_CLOSING) {
            /* Inject the result text if the user confirmed */
            if (osk_return.res == OSK_OK && osk_return.len > 0)
                osk_inject_result(osk_return.str, osk_return.len);

            osk_state = OSK_STATE_IDLE;
            ps3_log("[osk] closed");
        }
        break;
    }

    /* Transition: DONE -> begin unload */
    if (osk_state == OSK_STATE_DONE) {
        memset(&osk_return, 0, sizeof(osk_return));
        osk_return.str = osk_result;
        osk_return.len = OSK_MAX_TEXT;

        oskUnloadAsync(&osk_return);
        osk_state = OSK_STATE_CLOSING;
    }
}

/* ----------------------------------------------------------------
 * Status query
 * ---------------------------------------------------------------- */

qboolean PS3_OSK_IsActive(void)
{
    return (osk_state != OSK_STATE_IDLE) ? qtrue : qfalse;
}
