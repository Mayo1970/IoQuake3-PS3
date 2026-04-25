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

/* Avoid pulling in client.h just for these */
extern int Key_GetCatcher(void);
extern void Key_SetBinding(int keynum, const char *binding);
extern char *Key_GetBinding(int keynum);

/* Set a default bind only if the key is unbound (preserves q3config.cfg) */
static void PS3_SetDefaultBind(int keynum, const char *binding)
{
    char *existing = Key_GetBinding(keynum);
    if (!existing || !existing[0])
        Key_SetBinding(keynum, binding);
}

/* Constants */
#define STICK_CENTER    128
#define STICK_DEADZONE  30
#define STICK_RANGE     (128 - STICK_DEADZONE)

/* Pixels per frame at full stick deflection (menu cursor) */
#define MENU_CURSOR_SPEED  5.0f

#define NUM_PS3_BUTTONS 18  /* 16 digital + 2 analog triggers */

/* State */
static padInfo  ps3_pad_info;
static padData  ps3_pad_data;
static qboolean ps3_pad_connected = qfalse;
static qboolean ps3_quit_pressed  = qfalse;

static float ps3_cursor_accum_x = 0.0f;  /* sub-pixel remainder */
static float ps3_cursor_accum_y = 0.0f;
static int ps3_btn_prev[NUM_PS3_BUTTONS];
static int ps3_axis_prev[4]; /* LX, LY, RX, RY */

/* Button-to-keycode mapping.
 * Cross/Circle have dual mapping: always send K_JOY*, and additionally
 * send K_ENTER/K_ESCAPE when UI is active (see PS3_Input_Frame). */
static const int q3_key_map[NUM_PS3_BUTTONS] = {
    K_JOY1,             /*  0: Cross    -> jump (gameplay bind) */
    K_JOY2,             /*  1: Circle   -> crouch (gameplay bind) */
    K_JOY3,             /*  2: Square   -> prev weapon */
    K_JOY4,             /*  3: Triangle -> next weapon */
    K_JOY5,             /*  4: L1       -> strafe left */
    K_JOY6,             /*  5: R1       -> strafe right */
    K_JOY7,             /*  6: L2       -> zoom (digital bitfield) */
    K_JOY8,             /*  7: R2       -> attack (digital bitfield) */
    K_JOY9,             /*  8: L3       -> crouch (alt) */
    K_JOY10,            /*  9: R3       -> scoreboard */
    K_ESCAPE,           /* 10: Start    -> open menu / escape */
    K_JOY11,            /* 11: Select   -> scoreboard */
    K_UPARROW,          /* 12: D-Up     -> menu up */
    K_DOWNARROW,        /* 13: D-Down   -> menu down */
    K_LEFTARROW,        /* 14: D-Left   -> menu left (sliders) */
    K_RIGHTARROW,       /* 15: D-Right  -> menu right (sliders) */
    K_JOY7,             /* 16: L2 analog (trigger-as-button) */
    K_JOY8,             /* 17: R2 analog (trigger-as-button) */
};

/* Button indices -- can't reuse BTN_CROSS etc., those are padData bitfield names */
#define PS3_BTN_IDX_CROSS     0
#define PS3_BTN_IDX_CIRCLE    1
#define PS3_BTN_IDX_TRIANGLE  3
#define PS3_BTN_IDX_SELECT   11

/* Analog trigger threshold (0-255) */
#define TRIGGER_THRESHOLD 30

/* Read all buttons into an array. Indices 16-17 are L2/R2 via analog threshold. */
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

    /* OR digital bitfield with analog pressure so triggers work whether
     * or not pressure mode is enabled (PRE_* are 0 when it's off). */
    out[16] = (out[6] || pd->PRE_L2 > TRIGGER_THRESHOLD) ? 1 : 0;
    out[17] = (out[7] || pd->PRE_R2 > TRIGGER_THRESHOLD) ? 1 : 0;
}

/* Init / Shutdown */
void PS3_Input_Init(void)
{
    ioPadInit(7);

    /* Enable pressure mode so PRE_L2/PRE_R2 are populated (otherwise always 0) */
    ioPadSetPressMode(0, PAD_PRESS_MODE_ON);

    memset(ps3_btn_prev, 0, sizeof(ps3_btn_prev));
    memset(ps3_axis_prev, 0, sizeof(ps3_axis_prev));
    ps3_pad_connected = qfalse;
    ps3_quit_pressed = qfalse;
    ps3_cursor_accum_x = 0.0f;
    ps3_cursor_accum_y = 0.0f;
    printf("[ps3] Pad input initialized (pressure mode on)\n");
}

void PS3_Input_Shutdown(void)
{
    ioPadEnd();
}

/* Per-frame polling */
void PS3_Input_Frame(void)
{
    int btn_cur[NUM_PS3_BUTTONS];
    int i;

    /* While OSK is active, poll pad data for edge detection but don't
     * send events -- firmware owns the controller. */
    if (PS3_OSK_IsActive()) {
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

    if (ps3_pad_data.len == 0)
        return;

    /* Digital buttons -> SE_KEY (edge-detected) */
    read_buttons(&ps3_pad_data, btn_cur);

    int catchers = Key_GetCatcher();
    int in_menu = (catchers & (KEYCATCH_UI | KEYCATCH_CGAME)) ? 1 : 0;
    int in_text = (catchers & (KEYCATCH_CONSOLE | KEYCATCH_MESSAGE)) ? 1 : 0;

    int triangle_pressed = (btn_cur[PS3_BTN_IDX_TRIANGLE] &&
                            !ps3_btn_prev[PS3_BTN_IDX_TRIANGLE]);
    int triangle_consumed = 0;

    if (triangle_pressed) {
        if (btn_cur[PS3_BTN_IDX_SELECT]) {
            /* Select + Triangle = toggle console */
            Com_QueueEvent(0, SE_KEY, K_CONSOLE, qtrue, 0, NULL);
            Com_QueueEvent(0, SE_KEY, K_CONSOLE, qfalse, 0, NULL);
            triangle_consumed = 1;
        } else if (in_text) {
            /* Triangle in console or chat = open OSK.
             * Auto-submit in chat so the message sends immediately. */
            qboolean in_chat = (catchers & KEYCATCH_MESSAGE) ? qtrue : qfalse;
            PS3_OSK_Open(128, in_chat);
            triangle_consumed = 1;
        }
    }

    int cross_pressed = (btn_cur[PS3_BTN_IDX_CROSS] &&
                         !ps3_btn_prev[PS3_BTN_IDX_CROSS]);
    int cross_consumed = 0;

    if (cross_pressed && btn_cur[PS3_BTN_IDX_SELECT] && !in_menu && !in_text) {
        /* Select + Cross in gameplay = open chat */
        Cbuf_ExecuteText(EXEC_APPEND, "messagemode\n");
        cross_consumed = 1;
    }

    for (i = 0; i < NUM_PS3_BUTTONS; i++) {
        /* Skip digital L2/R2; analog threshold indices 16-17 handle them */
        if (i == 6 || i == 7) continue;

        int cur  = btn_cur[i] ? 1 : 0;
        int prev = ps3_btn_prev[i];

        if (cur != prev) {
            /* Triangle consumed by console toggle or OSK */
            if (i == PS3_BTN_IDX_TRIANGLE && triangle_consumed && cur) {
                ps3_btn_prev[i] = cur;
                continue;
            }

            /* Cross consumed by chat combo */
            if (i == PS3_BTN_IDX_CROSS && cross_consumed && cur) {
                ps3_btn_prev[i] = cur;
                continue;
            }

            Com_QueueEvent(0, SE_KEY, q3_key_map[i],
                           cur ? qtrue : qfalse, 0, NULL);

            if (i == PS3_BTN_IDX_CROSS && in_menu) {
                Com_QueueEvent(0, SE_KEY, K_ENTER,
                               cur ? qtrue : qfalse, 0, NULL);
            }
            if (i == PS3_BTN_IDX_CIRCLE && in_menu) {
                Com_QueueEvent(0, SE_KEY, K_ESCAPE,
                               cur ? qtrue : qfalse, 0, NULL);
            }
        }
        ps3_btn_prev[i] = cur;
    }

    /* Analog sticks */
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
            /* Left stick -> mouse cursor */
            if (lx_raw != 0 || ly_raw != 0) {
                float fx = (float)lx_raw / (float)STICK_RANGE;
                float fy = (float)ly_raw / (float)STICK_RANGE;

                if (fx >  1.0f) fx =  1.0f;
                if (fx < -1.0f) fx = -1.0f;
                if (fy >  1.0f) fy =  1.0f;
                if (fy < -1.0f) fy = -1.0f;

                /* Squared response curve for fine control at small deflections */
                float ax = fx * fx * MENU_CURSOR_SPEED;
                float ay = fy * fy * MENU_CURSOR_SPEED;
                if (fx < 0) ax = -ax;
                if (fy < 0) ay = -ay;

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
            /* Both sticks -> joystick axes, scaled to Q3's +/-32K range */
            int jlx = lx_raw * 256;
            int jly = ly_raw * 256;  /* positive = down = forward */
            int jrx = rx_raw * 256;
            int jry = ry_raw * 256;

            /* Only send on change */
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

    /* Check for quit combo: Select + Start */
    if (btn_cur[10] && btn_cur[11]) {  /* Start && Select */
        ps3_quit_pressed = qtrue;
    }
}

qboolean PS3_Input_QuitPressed(void)
{
    return ps3_quit_pressed;
}

/* ioQ3 input interface */
void IN_Init(void *windowData)
{
    (void)windowData;

    /* Default DS3 binds (overridable via console / q3config.cfg) */
    PS3_SetDefaultBind(K_JOY1,  "+moveup");      /* Cross    = jump */
    PS3_SetDefaultBind(K_JOY2,  "+movedown");    /* Circle   = crouch */
    PS3_SetDefaultBind(K_JOY3,  "weapprev");     /* Square   = prev weapon */
    PS3_SetDefaultBind(K_JOY4,  "weapnext");     /* Triangle = next weapon */

    PS3_SetDefaultBind(K_JOY5,  "+moveleft");    /* L1 = strafe left */
    PS3_SetDefaultBind(K_JOY6,  "+moveright");   /* R1 = strafe right */

    PS3_SetDefaultBind(K_JOY7,  "+zoom");        /* L2 = zoom */
    PS3_SetDefaultBind(K_JOY8,  "+attack");      /* R2 = fire */

    PS3_SetDefaultBind(K_JOY9,  "+speed");       /* L3 = run/walk toggle */
    PS3_SetDefaultBind(K_JOY10, "+scores");      /* R3 = scoreboard */

    PS3_SetDefaultBind(K_JOY11, "+scores");      /* Select = scoreboard */

    /* Axis cvars for twin-stick FPS (matches Xbox 360 axis numbering) */
    Cvar_Set("in_joystick", "1");
    Cvar_Set("in_joystickUseAnalog", "1");
    Cvar_Set("j_pitch_axis", "3");        /* right stick Y */
    Cvar_Set("j_yaw_axis",   "4");        /* right stick X */
    Cvar_Set("j_forward_axis", "1");      /* left stick Y */
    Cvar_Set("j_side_axis",  "0");        /* left stick X */
    Cvar_Set("j_pitch",  "0.011");        /* half of default, like Xbox 360 */
    Cvar_Set("j_yaw",   "-0.011");        /* negative = non-inverted */
    Cvar_Set("j_forward", "-0.25");
    Cvar_Set("j_side",    "0.25");

    ps3_log("IN_Init: default DS3 binds set");
}

void IN_Frame(void)
{
    /* Polling is done in PS3_Input_Frame */
}

void IN_Shutdown(void)
{
    PS3_Input_Shutdown();
}

void IN_Restart(void)
{
    PS3_Input_Shutdown();
    PS3_Input_Init();
}
