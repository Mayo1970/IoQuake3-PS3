/* ps3_input.c -- DS3 controller, USB keyboard, and USB mouse input. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io/pad.h>
#include <io/kb.h>
#include <io/mouse.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "keycodes.h"
#include "../input/ps3_input.h"
#include "../input/ps3_osk.h"

extern void ps3_log(const char *msg);

extern int Key_GetCatcher(void);
extern void Key_SetBinding(int keynum, const char *binding);
extern char *Key_GetBinding(int keynum);

static void PS3_SetDefaultBind(int keynum, const char *binding)
{
    char *existing = Key_GetBinding(keynum);
    if (!existing || !existing[0])
        Key_SetBinding(keynum, binding);
}

#define STICK_CENTER    128
#define STICK_DEADZONE  30
#define STICK_RANGE     (128 - STICK_DEADZONE)
#define MENU_CURSOR_SPEED  5.0f
#define NUM_PS3_BUTTONS 18  /* 16 digital + 2 analog triggers */

static padInfo  ps3_pad_info;
static padData  ps3_pad_data;
static qboolean ps3_pad_connected = qfalse;
static qboolean ps3_quit_pressed  = qfalse;

static float ps3_cursor_accum_x = 0.0f;
static float ps3_cursor_accum_y = 0.0f;
static int ps3_btn_prev[NUM_PS3_BUTTONS];
static int ps3_axis_prev[4];

static cvar_t *ps3_rumbleEnable = NULL;
static cvar_t *ps3_rumbleScale  = NULL;
static int     s_rumbleExpiryMs = 0;
static int     s_rumbleActive   = 0;

void PS3_SetRumble(uint8_t large, uint8_t small_motor, int durationMs)
{
    if (!ps3_rumbleEnable || !ps3_rumbleEnable->integer) return;

    float scale = ps3_rumbleScale ? ps3_rumbleScale->value : 1.0f;
    if (scale < 0.0f) scale = 0.0f;
    if (scale > 1.0f) scale = 1.0f;

    int lg = (int)(large       * scale);
    int sm = (int)(small_motor * scale);
    if (lg > 255) lg = 255;
    if (sm > 255) sm = 255;

    padActParam act;
    memset(&act, 0, sizeof(act));
    act.large_motor = (uint8_t)lg;
    act.small_motor = (sm > 0) ? 1 : 0;
    ioPadSetActDirect(0, &act);

    int now    = Sys_Milliseconds();
    int expiry = now + durationMs;
    if (expiry > s_rumbleExpiryMs) s_rumbleExpiryMs = expiry;
    s_rumbleActive = 1;
}

static void PS3_RumbleTick(void)
{
    if (!s_rumbleActive) return;
    if (Sys_Milliseconds() < s_rumbleExpiryMs) return;

    padActParam act;
    memset(&act, 0, sizeof(act));
    ioPadSetActDirect(0, &act);
    s_rumbleActive   = 0;
    s_rumbleExpiryMs = 0;
}

static void PS3_RumbleStop(void)
{
    padActParam act;
    memset(&act, 0, sizeof(act));
    ioPadSetActDirect(0, &act);
    s_rumbleActive   = 0;
    s_rumbleExpiryMs = 0;
}

/* Cross/Circle map to K_JOY*, plus K_ENTER/K_ESCAPE when UI active. */
static const int q3_key_map[NUM_PS3_BUTTONS] = {
    K_JOY1,             /* Cross */
    K_JOY2,             /* Circle */
    K_JOY3,             /* Square */
    K_JOY4,             /* Triangle */
    K_JOY5,             /* L1 */
    K_JOY6,             /* R1 */
    K_JOY7,             /* L2 */
    K_JOY8,             /* R2 */
    K_JOY9,             /* L3 */
    K_JOY10,            /* R3 */
    K_ESCAPE,           /* Start */
    K_JOY11,            /* Select */
    K_UPARROW,          /* D-Up */
    K_DOWNARROW,        /* D-Down */
    K_LEFTARROW,        /* D-Left */
    K_RIGHTARROW,       /* D-Right */
    K_JOY7,             /* L2 analog */
    K_JOY8,             /* R2 analog */
};

/* ============================================================
 * USB Keyboard
 * ============================================================ */

/* Raw USB HID usage code (0x04-0x65) → Q3 keynum (0 = unmapped).
 * Letters are always lowercase; shifted chars come via SE_CHAR. */
static const int s_raw_to_q3[256] = {
    [0x04]='a', [0x05]='b', [0x06]='c', [0x07]='d',
    [0x08]='e', [0x09]='f', [0x0A]='g', [0x0B]='h',
    [0x0C]='i', [0x0D]='j', [0x0E]='k', [0x0F]='l',
    [0x10]='m', [0x11]='n', [0x12]='o', [0x13]='p',
    [0x14]='q', [0x15]='r', [0x16]='s', [0x17]='t',
    [0x18]='u', [0x19]='v', [0x1A]='w', [0x1B]='x',
    [0x1C]='y', [0x1D]='z',
    [0x1E]='1', [0x1F]='2', [0x20]='3', [0x21]='4',
    [0x22]='5', [0x23]='6', [0x24]='7', [0x25]='8',
    [0x26]='9', [0x27]='0',
    [0x28]=K_ENTER,     [0x29]=K_ESCAPE,    [0x2A]=K_BACKSPACE,
    [0x2B]=K_TAB,       [0x2C]=K_SPACE,
    [0x2D]='-',  [0x2E]='=',  [0x2F]='[',  [0x30]=']',
    [0x31]='\\', [0x33]=';',  [0x34]='\'', [0x35]='`',
    [0x36]=',',  [0x37]='.',  [0x38]='/',
    [0x39]=K_CAPSLOCK,
    [0x3A]=K_F1,  [0x3B]=K_F2,  [0x3C]=K_F3,  [0x3D]=K_F4,
    [0x3E]=K_F5,  [0x3F]=K_F6,  [0x40]=K_F7,  [0x41]=K_F8,
    [0x42]=K_F9,  [0x43]=K_F10, [0x44]=K_F11, [0x45]=K_F12,
    [0x46]=K_PRINT, [0x47]=K_SCROLLOCK, [0x48]=K_PAUSE,
    [0x49]=K_INS,  [0x4A]=K_HOME, [0x4B]=K_PGUP,
    [0x4C]=K_DEL,  [0x4D]=K_END,  [0x4E]=K_PGDN,
    [0x4F]=K_RIGHTARROW, [0x50]=K_LEFTARROW,
    [0x51]=K_DOWNARROW,  [0x52]=K_UPARROW,
    [0x53]=K_KP_NUMLOCK,    [0x54]=K_KP_SLASH,  [0x55]=K_KP_STAR,
    [0x56]=K_KP_MINUS,      [0x57]=K_KP_PLUS,   [0x58]=K_KP_ENTER,
    [0x59]=K_KP_END,        [0x5A]=K_KP_DOWNARROW, [0x5B]=K_KP_PGDN,
    [0x5C]=K_KP_LEFTARROW,  [0x5D]=K_KP_5,      [0x5E]=K_KP_RIGHTARROW,
    [0x5F]=K_KP_HOME,       [0x60]=K_KP_UPARROW,[0x61]=K_KP_PGUP,
    [0x62]=K_KP_INS,        [0x63]=K_KP_DEL,
    [0x65]=K_MENU,
};

/* KbMkey.mkeys bitmasks (PPU big-endian; l_ctrl is bit 0 per header comment) */
#define MK_LCTRL   0x01u
#define MK_LSHIFT  0x02u
#define MK_LALT    0x04u
#define MK_RCTRL   0x10u
#define MK_RSHIFT  0x20u
#define MK_RALT    0x40u

static KbInfo   s_kb_info;
static qboolean s_kb_connected = qfalse;
static int      s_kb_port = -1;
static uint8_t  s_kb_cur[256];
static uint8_t  s_kb_prev[256];
static int      s_kb_last_seen[256];
static u32      s_kb_mkeys_prev = 0;
static KbLed    s_kb_led;

static void KB_EmitModifier(u32 cur_mk, u32 prev_mk, u32 bits, int q3key)
{
    int cur  = !!(cur_mk  & bits);
    int prev = !!(prev_mk & bits);
    if (cur != prev)
        Com_QueueEvent(0, SE_KEY, q3key, cur ? qtrue : qfalse, 0, NULL);
}

static void KB_ReleaseAll(void)
{
    int k;
    for (k = 1; k < 256; k++) {
        if (s_kb_prev[k]) {
            int q3key = s_raw_to_q3[k];
            if (q3key) Com_QueueEvent(0, SE_KEY, q3key, qfalse, 0, NULL);
        }
    }
    KB_EmitModifier(0, s_kb_mkeys_prev, MK_LCTRL|MK_RCTRL,  K_CTRL);
    KB_EmitModifier(0, s_kb_mkeys_prev, MK_LSHIFT|MK_RSHIFT, K_SHIFT);
    KB_EmitModifier(0, s_kb_mkeys_prev, MK_LALT|MK_RALT,     K_ALT);
    memset(s_kb_prev, 0, sizeof(s_kb_prev));
    s_kb_mkeys_prev = 0;
}

static void KB_Init(void)
{
    ioKbInit(MAX_KB_PORT_NUM);
    memset(s_kb_cur,       0, sizeof(s_kb_cur));
    memset(s_kb_prev,      0, sizeof(s_kb_prev));
    memset(s_kb_last_seen, 0, sizeof(s_kb_last_seen));
    memset(&s_kb_led, 0, sizeof(s_kb_led));
    s_kb_mkeys_prev = 0;
    s_kb_connected = qfalse;
    s_kb_port = -1;
}

static void KB_Shutdown(void) { ioKbEnd(); }

/* If syncOnly, update state without emitting events (for OSK). */
static void KB_Frame(qboolean syncOnly)
{
    KbData data;
    int i, k, found_port, q3key;
    qboolean pressed;
    u16 ascii;
    u32 mk;
    static int s_read_fail_logged = 0;
    static int s_key_logged = 0;

    if (ioKbGetInfo(&s_kb_info) != 0) return;

    if (s_kb_info.connected == 0) {
        if (s_kb_connected) {
            if (!syncOnly) KB_ReleaseAll();
            else { memset(s_kb_prev, 0, sizeof(s_kb_prev)); s_kb_mkeys_prev = 0; }
            s_kb_connected = qfalse;
            s_kb_port = -1;
            s_read_fail_logged = 0;
            s_key_logged = 0;
        }
        return;
    }

    found_port = -1;
    for (i = 0; i < MAX_KB_PORT_NUM; i++) {
        if (s_kb_info.status[i]) { found_port = i; break; }
    }
    if (found_port < 0) return;

    if (!s_kb_connected || found_port != s_kb_port) {
        int rc_ct;
        if (s_kb_connected && !syncOnly) KB_ReleaseAll();
        /* Skip ioKbSetReadMode; it silently breaks codetype. */
        {
            int rc_rm;
            rc_ct = ioKbSetCodeType(found_port, KB_CODETYPE_RAW);
            rc_rm = ioKbSetReadMode(found_port, KB_RMODE_PACKET);
            ioKbClearBuf(found_port);
            printf("[ps3kb] port=%d info=0x%x CodeType(RAW)=0x%x ReadMode(PACKET)=0x%x\n",
                   found_port, (unsigned)s_kb_info.info,
                   (unsigned)rc_ct, (unsigned)rc_rm);
        }
            memset(s_kb_cur,       0, sizeof(s_kb_cur));
        memset(s_kb_prev,      0, sizeof(s_kb_prev));
        memset(s_kb_last_seen, 0, sizeof(s_kb_last_seen));
        s_kb_mkeys_prev = 0;
        s_kb_port = found_port;
        s_kb_connected = qtrue;
        s_read_fail_logged = 0;
        s_key_logged = 0;
    }

    if (ioKbRead(s_kb_port, &data) != 0) {
        s_read_fail_logged++;
        if (s_read_fail_logged <= 5 || (s_read_fail_logged % 600 == 0))
            printf("[ps3kb] ioKbRead fail #%d port=%d\n",
                   s_read_fail_logged, s_kb_port);
        return;
    }
    s_read_fail_logged = 0;

    {
        int now = Sys_Milliseconds();

        if (data.nb_keycode > 0) {
            /* New snapshot: rebuild s_kb_cur from packet. Absent keys = released. */
            memset(s_kb_cur, 0, sizeof(s_kb_cur));
            for (i = 0; i < data.nb_keycode && i < MAX_KEYCODES; i++) {
                u16 raw = data.keycode[i] & ~((u16)(KB_RAWDAT | KB_KEYPAD));
                if (raw > 0 && raw < 256) {
                    s_kb_cur[(int)raw] = 1;
                    s_kb_last_seen[(int)raw] = now;
                }
            }
            if (s_key_logged < 10) {
                printf("[ps3kb] nb=%d raw[0]=0x%04x masked=0x%04x\n",
                       (int)data.nb_keycode, (unsigned)data.keycode[0],
                       (unsigned)(data.keycode[0] & ~((u16)(KB_RAWDAT | KB_KEYPAD))));
                s_key_logged++;
            }
        } else {
            /* nb_keycode==0: queue empty or all released (indistinguishable).
             * Do NOT release held keys here; multi-key transitions release via
             * the memset+rebuild path above (missing key = released).
             * Only apply a 2000ms emergency timeout so stuck keys can't
             * persist forever if the release event was somehow missed.
             * In INPUTCHAR mode auto-repeat fires every ~30ms after the
             * initial 500ms delay, keeping last_seen fresh during a real hold,
             * so the 2000ms guard should never fire during intentional holding. */
            for (i = 1; i < 256; i++) {
                if (s_kb_cur[i] && (now - s_kb_last_seen[i]) > 2000) {
                    printf("[ps3kb] emergency release raw=0x%02x (2000ms idle)\n", i);
                    s_kb_cur[i] = 0;
                }
            }
        }
    }

    if (!syncOnly) {
        for (k = 1; k < 256; k++) {
            if (s_kb_cur[k] == s_kb_prev[k]) continue;
            q3key = s_raw_to_q3[k];
            if (!q3key) continue;
            pressed = s_kb_cur[k] ? qtrue : qfalse;
            Com_QueueEvent(0, SE_KEY, q3key, pressed, 0, NULL);
            if (pressed) {
                ascii = ioKbCnvRawCode(KB_MAPPING_101, data.mkey, s_kb_led, (u16)k);
                if (ascii >= 32 && ascii < 127)
                    Com_QueueEvent(0, SE_CHAR, (int)ascii, 0, 0, NULL);
            }
        }
        mk = data.mkey._KbMkeyU.mkeys;
        KB_EmitModifier(mk, s_kb_mkeys_prev, MK_LCTRL|MK_RCTRL,  K_CTRL);
        KB_EmitModifier(mk, s_kb_mkeys_prev, MK_LSHIFT|MK_RSHIFT, K_SHIFT);
        KB_EmitModifier(mk, s_kb_mkeys_prev, MK_LALT|MK_RALT,     K_ALT);
        s_kb_mkeys_prev = mk;
    } else {
        s_kb_mkeys_prev = data.mkey._KbMkeyU.mkeys;
    }

    s_kb_led = data.led;
    memcpy(s_kb_prev, s_kb_cur, sizeof(s_kb_cur));
}

/* ============================================================
 * USB Mouse
 * ============================================================ */

static mouseInfo s_mouse_info;
static qboolean  s_mouse_connected = qfalse;
static u8        s_mouse_btns_prev = 0;

static void Mouse_Init(void)
{
    ioMouseInit(2);
    s_mouse_connected = qfalse;
    s_mouse_btns_prev = 0;
}

static void Mouse_Shutdown(void) { ioMouseEnd(); }

static void Mouse_Frame(qboolean syncOnly)
{
    mouseDataList list;
    mouseData *md;
    u32 i;
    u8 cur, diff;
    int dx = 0, dy = 0;

    if (ioMouseGetInfo(&s_mouse_info) != 0) return;

    if (s_mouse_info.connected == 0) {
        if (s_mouse_connected) {
            if (!syncOnly) {
                u8 held = s_mouse_btns_prev;
                if (held & 0x01) Com_QueueEvent(0, SE_KEY, K_MOUSE1, qfalse, 0, NULL);
                if (held & 0x02) Com_QueueEvent(0, SE_KEY, K_MOUSE2, qfalse, 0, NULL);
                if (held & 0x04) Com_QueueEvent(0, SE_KEY, K_MOUSE3, qfalse, 0, NULL);
            }
            s_mouse_btns_prev = 0;
            s_mouse_connected = qfalse;
        }
        return;
    }

    if (!s_mouse_connected) {
        ioMouseClearBuf(0);
        s_mouse_btns_prev = 0;
        s_mouse_connected = qtrue;
        if (!syncOnly) printf("[ps3] Mouse connected\n");
    }

    if (ioMouseGetDataList(0, &list) != 0) return;
    if (list.count == 0) return;

    for (i = 0; i < list.count && i < MOUSE_MAX_DATA_LIST; i++) {
        md = &list.list[i];
        if (!md->update) continue;

        dx += (int)md->x_axis;
        dy += (int)md->y_axis;

        if (!syncOnly) {
            cur  = md->buttons;
            diff = cur ^ s_mouse_btns_prev;
            if (diff & 0x01) Com_QueueEvent(0, SE_KEY, K_MOUSE1, (cur & 0x01) ? qtrue : qfalse, 0, NULL);
            if (diff & 0x02) Com_QueueEvent(0, SE_KEY, K_MOUSE2, (cur & 0x02) ? qtrue : qfalse, 0, NULL);
            if (diff & 0x04) Com_QueueEvent(0, SE_KEY, K_MOUSE3, (cur & 0x04) ? qtrue : qfalse, 0, NULL);
            if (md->wheel > 0) {
                Com_QueueEvent(0, SE_KEY, K_MWHEELUP,   qtrue,  0, NULL);
                Com_QueueEvent(0, SE_KEY, K_MWHEELUP,   qfalse, 0, NULL);
            } else if (md->wheel < 0) {
                Com_QueueEvent(0, SE_KEY, K_MWHEELDOWN, qtrue,  0, NULL);
                Com_QueueEvent(0, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL);
            }
            s_mouse_btns_prev = cur;
        } else {
            s_mouse_btns_prev = md->buttons;
        }
    }

    if (!syncOnly && (dx != 0 || dy != 0))
        Com_QueueEvent(0, SE_MOUSE, dx, dy, 0, NULL);
}

/* ============================================================ */

/* Can't reuse BTN_CROSS -- those are padData bitfield member names. */
#define PS3_BTN_IDX_CROSS     0
#define PS3_BTN_IDX_CIRCLE    1
#define PS3_BTN_IDX_TRIANGLE  3
#define PS3_BTN_IDX_L3        8
#define PS3_BTN_IDX_R3        9
#define PS3_BTN_IDX_SELECT   11

#define TRIGGER_THRESHOLD 30

static void read_buttons(const padData *pd, int out[NUM_PS3_BUTTONS])
{
    out[0]  = pd->BTN_CROSS;
    out[1]  = pd->BTN_CIRCLE;
    out[2]  = pd->BTN_SQUARE;
    out[3]  = pd->BTN_TRIANGLE;
    out[4]  = pd->BTN_L1;
    out[5]  = pd->BTN_R1;
    out[6]  = pd->BTN_L2;
    out[7]  = pd->BTN_R2;
    out[8]  = pd->BTN_L3;
    out[9]  = pd->BTN_R3;
    out[10] = pd->BTN_START;
    out[11] = pd->BTN_SELECT;
    out[12] = pd->BTN_UP;
    out[13] = pd->BTN_DOWN;
    out[14] = pd->BTN_LEFT;
    out[15] = pd->BTN_RIGHT;

    /* OR with analog pressure so triggers work even with pressure mode off. */
    out[16] = (out[6] || pd->PRE_L2 > TRIGGER_THRESHOLD) ? 1 : 0;
    out[17] = (out[7] || pd->PRE_R2 > TRIGGER_THRESHOLD) ? 1 : 0;
}

void PS3_Input_Init(void)
{
    ioPadInit(7);

    /* PRE_L2/PRE_R2 read 0 unless pressure mode is on. */
    ioPadSetPressMode(0, PAD_PRESS_MODE_ON);

    memset(ps3_btn_prev, 0, sizeof(ps3_btn_prev));
    memset(ps3_axis_prev, 0, sizeof(ps3_axis_prev));
    ps3_pad_connected = qfalse;
    ps3_quit_pressed = qfalse;
    ps3_cursor_accum_x = 0.0f;
    ps3_cursor_accum_y = 0.0f;
    s_rumbleActive   = 0;
    s_rumbleExpiryMs = 0;

    KB_Init();
    Mouse_Init();

    printf("[ps3] Input initialized (pad + keyboard + mouse)\n");
}

void PS3_Input_Shutdown(void)
{
    PS3_RumbleStop();
    ioPadEnd();
    KB_Shutdown();
    Mouse_Shutdown();
}

void PS3_Input_Frame(void)
{
    int btn_cur[NUM_PS3_BUTTONS];
    int i;
    qboolean osk_active = PS3_OSK_IsActive();

    /* Always poll keyboard/mouse to keep state in sync even during OSK. */
    KB_Frame(osk_active);
    Mouse_Frame(osk_active);

    /* OSK owns the pad while open -- only keep edge state in sync. */
    if (osk_active) {
        ioPadGetInfo(&ps3_pad_info);
        if (ps3_pad_info.status[0] && ioPadGetData(0, &ps3_pad_data) == 0) {
            read_buttons(&ps3_pad_data, ps3_btn_prev);
        }
        return;
    }

    ioPadGetInfo(&ps3_pad_info);

    if (ps3_pad_info.status[0] == 0) {
        ps3_pad_connected = qfalse;
        return;
    }
    ps3_pad_connected = qtrue;

    if (ioPadGetData(0, &ps3_pad_data) != 0)
        return;

    /* Don't bail on len==0: ps3_pad_data still holds last poll's values,
     * and skipping the analog accumulator freezes the menu cursor at 60fps. */

    PS3_RumbleTick();

    read_buttons(&ps3_pad_data, btn_cur);

    int catchers = Key_GetCatcher();
    int in_menu = (catchers & (KEYCATCH_UI | KEYCATCH_CGAME)) ? 1 : 0;
    int in_text = (catchers & (KEYCATCH_CONSOLE | KEYCATCH_MESSAGE)) ? 1 : 0;

    /* When the UI catcher turns on (menu opens), clear ui_ime_target.
     * Stock TA ui.qvm never sets this cvar, so stale values from a previous
     * menu session (e.g. baseq3 ui.qvm setting it to a field name) would cause
     * every CROSS press to open the OSK instead of confirming. */
    static int prev_in_menu = 0;
    if (in_menu && !prev_in_menu)
        Cvar_Set("ui_ime_target", "");
    prev_in_menu = in_menu;

    int triangle_pressed = (btn_cur[PS3_BTN_IDX_TRIANGLE] &&
                            !ps3_btn_prev[PS3_BTN_IDX_TRIANGLE]);
    int triangle_consumed = 0;

    /* SELECT+TRIANGLE: toggle console */
    if (triangle_pressed && btn_cur[PS3_BTN_IDX_SELECT]) {
        Com_QueueEvent(0, SE_KEY, K_CONSOLE, qtrue, 0, NULL);
        Com_QueueEvent(0, SE_KEY, K_CONSOLE, qfalse, 0, NULL);
        triangle_consumed = 1;
    }

    int cross_pressed = (btn_cur[PS3_BTN_IDX_CROSS] &&
                         !ps3_btn_prev[PS3_BTN_IDX_CROSS]);
    int cross_consumed = 0;

    if (cross_pressed) {
        if (btn_cur[PS3_BTN_IDX_SELECT] && !in_menu && !in_text) {
            /* SELECT+CROSS: open chat */
            Cbuf_ExecuteText(EXEC_APPEND, "messagemode\n");
            cross_consumed = 1;
        } else if (in_text) {
            /* CROSS in console: prepend '/' so result is treated as a command.
             * CROSS in chat: no slash, text is the message body. */
            qboolean in_console = (catchers & KEYCATCH_CONSOLE) ? qtrue : qfalse;
            PS3_OSK_Open(in_console ? "Console Command" : "Chat", 128, NULL, qtrue, in_console, qfalse);
            cross_consumed = 1;
        } else if (in_menu) {
            /* CROSS in menu: open OSK when a text field is focused (modified
             * qvm publishes ui_ime_target), otherwise fall through to K_ENTER.
             * "donothing" is the sentinel used by TA .menu files on mouseExit
             * (instead of ""); treat it as empty so it doesn't wrongly open OSK. */
            const char *ime = Cvar_VariableString("ui_ime_target");
            if (ime && ime[0] && strcmp(ime, "donothing") != 0) {
                PS3_OSK_Open("Enter text", 80, NULL, qfalse, qfalse, qtrue);
                cross_consumed = 1;
            }
        }
    }

    int circle_pressed = (btn_cur[PS3_BTN_IDX_CIRCLE] &&
                          !ps3_btn_prev[PS3_BTN_IDX_CIRCLE]);
    int circle_consumed = 0;

    /* CIRCLE closes the console when it's open */
    if (circle_pressed && !circle_consumed && (catchers & KEYCATCH_CONSOLE)) {
        Com_QueueEvent(0, SE_KEY, K_CONSOLE, qtrue, 0, NULL);
        Com_QueueEvent(0, SE_KEY, K_CONSOLE, qfalse, 0, NULL);
        circle_consumed = 1;
    }

    /* L3+R3 (edge-detected): toggle rumble enable. */
    if (btn_cur[PS3_BTN_IDX_L3] && btn_cur[PS3_BTN_IDX_R3] &&
        (!ps3_btn_prev[PS3_BTN_IDX_L3] || !ps3_btn_prev[PS3_BTN_IDX_R3])) {
        int on = ps3_rumbleEnable ? !ps3_rumbleEnable->integer : 1;
        Cvar_SetValue("ps3_rumbleEnable", on ? 1.0f : 0.0f);
        Com_Printf("Rumble: %s\n", on ? "ON" : "OFF");
        if (on) {
            PS3_SetRumble(200, 1, 200);
        } else {
            PS3_RumbleStop();
        }
        ps3_btn_prev[PS3_BTN_IDX_L3] = 1;
        ps3_btn_prev[PS3_BTN_IDX_R3] = 1;
        goto skip_buttons;
    }

    for (i = 0; i < NUM_PS3_BUTTONS; i++) {
        /* Digital L2/R2 are handled via the analog-threshold indices 16/17. */
        if (i == 6 || i == 7) continue;

        int cur  = btn_cur[i] ? 1 : 0;
        int prev = ps3_btn_prev[i];

        if (cur != prev) {
            if (i == PS3_BTN_IDX_TRIANGLE && triangle_consumed && cur) {
                ps3_btn_prev[i] = cur;
                continue;
            }

            if (i == PS3_BTN_IDX_CROSS && cross_consumed && cur) {
                ps3_btn_prev[i] = cur;
                continue;
            }

            if (i == PS3_BTN_IDX_CIRCLE && circle_consumed && cur) {
                ps3_btn_prev[i] = cur;
                continue;
            }

            if (i == PS3_BTN_IDX_CROSS && in_menu) {
                Com_QueueEvent(0, SE_KEY, K_ENTER,
                               cur ? qtrue : qfalse, 0, NULL);
            } else if (i == PS3_BTN_IDX_CIRCLE && in_menu) {
                Com_QueueEvent(0, SE_KEY, K_ESCAPE,
                               cur ? qtrue : qfalse, 0, NULL);
            } else {
                Com_QueueEvent(0, SE_KEY, q3_key_map[i],
                               cur ? qtrue : qfalse, 0, NULL);
            }
        }
        ps3_btn_prev[i] = cur;
    }
    skip_buttons:;

    {
        int lx_raw = (int)ps3_pad_data.ANA_L_H - STICK_CENTER;
        int ly_raw = (int)ps3_pad_data.ANA_L_V - STICK_CENTER;
        int rx_raw = (int)ps3_pad_data.ANA_R_H - STICK_CENTER;
        int ry_raw = (int)ps3_pad_data.ANA_R_V - STICK_CENTER;

        if (abs(lx_raw) < STICK_DEADZONE) lx_raw = 0;
        if (abs(ly_raw) < STICK_DEADZONE) ly_raw = 0;
        if (abs(rx_raw) < STICK_DEADZONE) rx_raw = 0;
        if (abs(ry_raw) < STICK_DEADZONE) ry_raw = 0;

        if (in_menu) {
            if (lx_raw != 0 || ly_raw != 0) {
                float fx = (float)lx_raw / (float)STICK_RANGE;
                float fy = (float)ly_raw / (float)STICK_RANGE;

                if (fx >  1.0f) fx =  1.0f;
                if (fx < -1.0f) fx = -1.0f;
                if (fy >  1.0f) fy =  1.0f;
                if (fy < -1.0f) fy = -1.0f;

                /* Linear; squared curves underflow accum to 0 and stutter. */
                float ax = fx * MENU_CURSOR_SPEED;
                float ay = fy * MENU_CURSOR_SPEED;

                ps3_cursor_accum_x += ax;
                ps3_cursor_accum_y += ay;

                int dx = (int)ps3_cursor_accum_x;
                int dy = (int)ps3_cursor_accum_y;
                ps3_cursor_accum_x -= (float)dx;
                ps3_cursor_accum_y -= (float)dy;

                if (dx != 0 || dy != 0)
                    Com_QueueEvent(0, SE_MOUSE, dx, dy, 0, NULL);
            }
        } else {
            /* Scaled to Q3's +/-32K joystick axis range. */
            int jlx = lx_raw * 256;
            int jly = ly_raw * 256;
            int jrx = rx_raw * 256;
            int jry = ry_raw * 256;

            if (jlx != ps3_axis_prev[0]) {
                Com_QueueEvent(0, SE_JOYSTICK_AXIS, 0, jlx, 0, NULL);
                ps3_axis_prev[0] = jlx;
            }
            if (jly != ps3_axis_prev[1]) {
                Com_QueueEvent(0, SE_JOYSTICK_AXIS, 1, jly, 0, NULL);
                ps3_axis_prev[1] = jly;
            }
            if (jrx != ps3_axis_prev[2]) {
                Com_QueueEvent(0, SE_JOYSTICK_AXIS, 4, jrx, 0, NULL);
                ps3_axis_prev[2] = jrx;
            }
            if (jry != ps3_axis_prev[3]) {
                Com_QueueEvent(0, SE_JOYSTICK_AXIS, 3, jry, 0, NULL);
                ps3_axis_prev[3] = jry;
            }
        }
    }

    /* Quit combo: Start + Select. */
    if (btn_cur[10] && btn_cur[11]) {
        ps3_quit_pressed = qtrue;
    }
}

qboolean PS3_Input_QuitPressed(void)
{
    return ps3_quit_pressed;
}

void IN_Init(void *windowData)
{
    (void)windowData;

    PS3_SetDefaultBind(K_JOY1,  "+moveup");      /* Cross    = jump */
    PS3_SetDefaultBind(K_JOY2,  "+movedown");    /* Circle   = crouch */
    PS3_SetDefaultBind(K_JOY3,  "weapprev");     /* Square */
    PS3_SetDefaultBind(K_JOY4,  "weapnext");     /* Triangle */
    PS3_SetDefaultBind(K_JOY5,  "+moveleft");    /* L1 */
    PS3_SetDefaultBind(K_JOY6,  "+moveright");   /* R1 */
    PS3_SetDefaultBind(K_JOY7,  "+zoom");        /* L2 */
    PS3_SetDefaultBind(K_JOY8,  "+attack");      /* R2 */
    PS3_SetDefaultBind(K_JOY9,  "+speed");       /* L3 */
    PS3_SetDefaultBind(K_JOY10, "+scores");      /* R3 */
    PS3_SetDefaultBind(K_JOY11, "+scores");      /* Select */

    ps3_rumbleEnable = Cvar_Get("ps3_rumbleEnable", "1",   CVAR_ARCHIVE);
    ps3_rumbleScale  = Cvar_Get("ps3_rumbleScale",  "1.0", CVAR_ARCHIVE);

    Cvar_Set("in_joystick", "1");
    Cvar_Set("in_joystickUseAnalog", "1");
    Cvar_Set("j_pitch_axis", "3");
    Cvar_Set("j_yaw_axis",   "4");
    Cvar_Set("j_forward_axis", "1");
    Cvar_Set("j_side_axis",  "0");
    Cvar_Set("j_pitch",  "0.011");
    Cvar_Set("j_yaw",   "-0.011");
    Cvar_Set("j_forward", "-0.25");
    Cvar_Set("j_side",    "0.25");

    ps3_log("IN_Init: default DS3 binds set");
}

void IN_Frame(void) {}

void IN_Shutdown(void)
{
    PS3_Input_Shutdown();
}

void IN_Restart(void)
{
    PS3_Input_Shutdown();
    PS3_Input_Init();
}
