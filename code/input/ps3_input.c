/* ps3_input.c -- DS3 controller input via libpad. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io/pad.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "keycodes.h"
#include "../input/ps3_input.h"
#include "../input/ps3_osk.h"

extern void ps3_log(const char *msg);

extern int Key_GetCatcher(void);
extern void Key_SetBinding(int keynum, const char *binding);
extern char *Key_GetBinding(int keynum);

/* Preserves any binding loaded from q3config.cfg. */
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

static float ps3_cursor_accum_x = 0.0f;  /* sub-pixel remainder */
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

/* Cross/Circle dual-map: always K_JOY*, plus K_ENTER/K_ESCAPE when UI active. */
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

    /* Analog pressure OR with digital L2/R2; triggers work with pressure mode off. */
    out[16] = (out[6] || pd->PRE_L2 > TRIGGER_THRESHOLD) ? 1 : 0;
    out[17] = (out[7] || pd->PRE_R2 > TRIGGER_THRESHOLD) ? 1 : 0;
}

void PS3_Input_Init(void)
{
    ioPadInit(7);
    ioPadSetPressMode(0, PAD_PRESS_MODE_ON);

    memset(ps3_btn_prev, 0, sizeof(ps3_btn_prev));
    memset(ps3_axis_prev, 0, sizeof(ps3_axis_prev));
    ps3_pad_connected = qfalse;
    ps3_quit_pressed = qfalse;
    ps3_cursor_accum_x = 0.0f;
    ps3_cursor_accum_y = 0.0f;
    s_rumbleActive   = 0;
    s_rumbleExpiryMs = 0;
    printf("[ps3] Pad input initialized (pressure mode on, rumble enabled)\n");
}

void PS3_Input_Shutdown(void)
{
    PS3_RumbleStop();
    ioPadEnd();
}

void PS3_Input_Frame(void)
{
    int btn_cur[NUM_PS3_BUTTONS];
    int i;

    /* OSK owns pad; only update edge state while active. */
    if (PS3_OSK_IsActive()) {
        ioPadGetInfo(&ps3_pad_info);
        if (ps3_pad_info.status[0] && ioPadGetData(0, &ps3_pad_data) == 0)
            read_buttons(&ps3_pad_data, ps3_btn_prev);
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

    /* Process analog even on len==0; otherwise menu cursor freezes. */
    PS3_RumbleTick();

    read_buttons(&ps3_pad_data, btn_cur);

    int catchers = Key_GetCatcher();
    int in_menu = (catchers & (KEYCATCH_UI | KEYCATCH_CGAME)) ? 1 : 0;
    int in_text = (catchers & (KEYCATCH_CONSOLE | KEYCATCH_MESSAGE)) ? 1 : 0;

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
            /* OSK in console: prepend '/' for command; in chat: no slash. */
            qboolean in_console = (catchers & KEYCATCH_CONSOLE) ? qtrue : qfalse;
            PS3_OSK_Open(128, qtrue, in_console);
            cross_consumed = 1;
        }
    }

    int circle_pressed = (btn_cur[PS3_BTN_IDX_CIRCLE] &&
                          !ps3_btn_prev[PS3_BTN_IDX_CIRCLE]);
    int circle_consumed = 0;

    /* CIRCLE closes the console when it's open */
    if (circle_pressed && (catchers & KEYCATCH_CONSOLE)) {
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
        /* Skip L2/R2 digital; handled via analog-threshold indices 16/17. */
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

                /* Linear response; squared curves underflow accumulator. */
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
            /* Scale to Q3 axis range (32K). */
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

    PS3_SetDefaultBind(K_JOY1,  "+moveup");
    PS3_SetDefaultBind(K_JOY2,  "+movedown");
    PS3_SetDefaultBind(K_JOY3,  "weapprev");
    PS3_SetDefaultBind(K_JOY4,  "weapnext");
    PS3_SetDefaultBind(K_JOY5,  "+moveleft");
    PS3_SetDefaultBind(K_JOY6,  "+moveright");
    PS3_SetDefaultBind(K_JOY7,  "+zoom");
    PS3_SetDefaultBind(K_JOY8,  "+attack");
    PS3_SetDefaultBind(K_JOY9,  "+speed");
    PS3_SetDefaultBind(K_JOY10, "+scores");
    PS3_SetDefaultBind(K_JOY11, "+scores");

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
