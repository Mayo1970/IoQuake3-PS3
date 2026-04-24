/*
 * ioquake3-PS3: input/ps3_input.c
 * DualShock 3 (SIXAXIS) controller input via PSL1GHT's libpad.
 *
 * Button mapping follows the Xbox 360 port's approach:
 *   - D-pad sends classic keycodes (K_UPARROW etc.) for menu navigation
 *   - Cross = K_ENTER (confirm), Circle = K_ESCAPE (back)
 *   - Face/shoulder buttons send K_JOY* for bindable gameplay actions
 *   - Left stick: SE_MOUSE in menus (cursor), SE_JOYSTICK_AXIS in gameplay
 *   - Right stick: SE_JOYSTICK_AXIS in gameplay (mouselook via j_* cvars)
 *
 * Default gameplay binds are set in IN_Init:
 *   RT (R2) = attack, LT (L2) = zoom, Cross = jump, Circle = crouch,
 *   Square = prev weapon, Triangle = next weapon, L1/R1 = strafe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io/pad.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "keycodes.h"
#include "../input/ps3_input.h"

extern void ps3_log(const char *msg);

/* Key_GetCatcher is declared in client.h but we avoid pulling that in */
extern int Key_GetCatcher(void);

/* Key_SetBinding / Key_GetBinding for default binds */
extern void Key_SetBinding(int keynum, const char *binding);
extern char *Key_GetBinding(int keynum);

/* Set a default bind only if the key is not already bound by q3config.cfg */
static void PS3_SetDefaultBind(int keynum, const char *binding)
{
    char *existing = Key_GetBinding(keynum);
    if (!existing || !existing[0])
        Key_SetBinding(keynum, binding);
}

/* ----------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------- */
#define STICK_CENTER    128
#define STICK_DEADZONE  30
#define STICK_RANGE     (128 - STICK_DEADZONE)

/* Mouse cursor speed for analog stick in menus (pixels per frame at full tilt).
 * Xbox 360 port uses >>13 on 16-bit range = ~4 px/frame.
 * We scale from our 0-98 range to similar values. */
#define MENU_CURSOR_SPEED  5.0f

/* Number of buttons we track */
#define NUM_PS3_BUTTONS 18  /* 16 digital + 2 triggers */

/* ----------------------------------------------------------------
 * State
 * ---------------------------------------------------------------- */
static padInfo  ps3_pad_info;
static padData  ps3_pad_data;
static qboolean ps3_pad_connected = qfalse;
static qboolean ps3_quit_pressed  = qfalse;

/* Float accumulator for sub-pixel analog precision (menu cursor) */
static float ps3_cursor_accum_x = 0.0f;
static float ps3_cursor_accum_y = 0.0f;

/* Previous button states for edge detection */
static int ps3_btn_prev[NUM_PS3_BUTTONS];

/* Previous axis values to avoid redundant events */
static int ps3_axis_prev[4]; /* LX, LY, RX, RY */

/* ----------------------------------------------------------------
 * Button-to-keycode mapping
 *
 * Following the Xbox 360 port pattern:
 *   - D-pad -> classic arrow keys (menu navigation)
 *   - Cross -> K_JOY1 (gameplay: jump) + K_ENTER in menus
 *   - Circle -> K_JOY2 (gameplay: crouch) + K_ESCAPE in menus
 *   - Start -> K_ESCAPE (open/close menu)
 *   - Select -> K_JOY11 (scoreboard)
 *   - Face/shoulder/trigger -> K_JOY* (bindable gameplay)
 *
 * Cross and Circle use dual mapping: they always send K_JOY*
 * for gameplay binds, and additionally send K_ENTER/K_ESCAPE
 * when the UI/cgame catcher is active. This is handled in
 * PS3_Input_Frame with special-case logic.
 * ---------------------------------------------------------------- */

/* Primary keycode for each button (always sent) */
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

/* Button indices for dual-mapping (Cross/Circle send extra UI keys).
 * Cannot use BTN_CROSS/BTN_CIRCLE -- those are padData bitfield names
 * defined in PSL1GHT's <io/pad.h>. */
#define PS3_BTN_IDX_CROSS   0
#define PS3_BTN_IDX_CIRCLE  1

/* Trigger threshold (0-255 analog range, same as Xbox 360 port) */
#define TRIGGER_THRESHOLD 30

/*
 * Read all buttons from padData bitfields into an array.
 * Returns 1 (pressed) or 0 (released) for each.
 * Indices 16-17 are L2/R2 treated as digital via threshold.
 */
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

    /* Analog triggers as digital buttons: OR of digital bitfield and
     * analog pressure threshold. If pressure mode is not enabled by the
     * system, PRE_L2/PRE_R2 are 0 and the digital bitfield carries the
     * button state. If pressure mode IS enabled, the analog threshold
     * provides earlier/smoother detection. */
    out[16] = (out[6] || pd->PRE_L2 > TRIGGER_THRESHOLD) ? 1 : 0;
    out[17] = (out[7] || pd->PRE_R2 > TRIGGER_THRESHOLD) ? 1 : 0;
}

/* ----------------------------------------------------------------
 * Init / Shutdown
 * ---------------------------------------------------------------- */
void PS3_Input_Init(void)
{
    ioPadInit(7); /* max 7 controllers */

    /* Enable pressure-sensitive mode for analog trigger values (PRE_L2/PRE_R2).
     * Without this, PRE_* fields are always 0 and only the 1-bit BTN_* fields work. */
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

/* ----------------------------------------------------------------
 * Per-frame polling
 *
 * Called from main loop before Com_Frame.
 * Injects SE_KEY, SE_MOUSE, SE_JOYSTICK_AXIS events into Q3's
 * event queue via Com_QueueEvent.
 * ---------------------------------------------------------------- */
void PS3_Input_Frame(void)
{
    int btn_cur[NUM_PS3_BUTTONS];
    int i;

    ioPadGetInfo(&ps3_pad_info);

    /* Use first connected controller */
    if (ps3_pad_info.status[0] == 0) {
        ps3_pad_connected = qfalse;
        return;
    }
    ps3_pad_connected = qtrue;

    if (ioPadGetData(0, &ps3_pad_data) != 0)
        return;

    /* No new data since last poll */
    if (ps3_pad_data.len == 0)
        return;

    /* ----------------------------------------------------------------
     * Digital buttons -> SE_KEY events
     *
     * Edge detection: only send events on state change.
     * Button indices 6-7 (L2/R2 digital bitfield) are skipped
     * in favor of indices 16-17 (analog threshold) to avoid
     * duplicate events.
     *
     * Cross and Circle use dual mapping:
     *   - Always send their K_JOY* keycode (for gameplay binds)
     *   - Additionally send K_ENTER / K_ESCAPE when UI is active
     *     (so the Q3 menu system recognizes confirm/back)
     * ---------------------------------------------------------------- */
    read_buttons(&ps3_pad_data, btn_cur);

    int catchers = Key_GetCatcher();
    int in_menu = (catchers & (KEYCATCH_UI | KEYCATCH_CGAME)) ? 1 : 0;

    for (i = 0; i < NUM_PS3_BUTTONS; i++) {
        /* Skip the digital L2/R2 bitfields; use analog threshold instead */
        if (i == 6 || i == 7) continue;

        int cur  = btn_cur[i] ? 1 : 0;
        int prev = ps3_btn_prev[i];

        if (cur != prev) {
            /* Primary keycode (always sent) */
            Com_QueueEvent(0, SE_KEY, q3_key_map[i],
                           cur ? qtrue : qfalse, 0, NULL);

            /* Dual mapping for Cross/Circle in menu context */
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

    /* ----------------------------------------------------------------
     * Analog sticks
     *
     * Menu mode (KEYCATCH_UI or KEYCATCH_CGAME active):
     *   Left stick -> SE_MOUSE (cursor movement, like Xbox 360 port)
     *   Right stick -> ignored in menus
     *
     * Gameplay mode:
     *   Left stick -> SE_JOYSTICK_AXIS 0,1 (strafe/forward via j_* cvars)
     *   Right stick -> SE_JOYSTICK_AXIS 4,3 (yaw/pitch via j_* cvars)
     *   Axis numbering matches Xbox 360 port: 0=LX, 1=LY, 3=RY, 4=RX
     * ---------------------------------------------------------------- */
    {
        int lx_raw = (int)ps3_pad_data.ANA_L_H - STICK_CENTER;
        int ly_raw = (int)ps3_pad_data.ANA_L_V - STICK_CENTER;
        int rx_raw = (int)ps3_pad_data.ANA_R_H - STICK_CENTER;
        int ry_raw = (int)ps3_pad_data.ANA_R_V - STICK_CENTER;

        /* Deadzone */
        if (abs(lx_raw) < STICK_DEADZONE) lx_raw = 0;
        if (abs(ly_raw) < STICK_DEADZONE) ly_raw = 0;
        if (abs(rx_raw) < STICK_DEADZONE) rx_raw = 0;
        if (abs(ry_raw) < STICK_DEADZONE) ry_raw = 0;

        /* Reuse catchers from button section above */
        if (in_menu) {
            /* ----- Menu mode: left stick -> mouse cursor ----- */
            if (lx_raw != 0 || ly_raw != 0) {
                float fx = (float)lx_raw / (float)STICK_RANGE;
                float fy = (float)ly_raw / (float)STICK_RANGE;

                /* Clamp */
                if (fx >  1.0f) fx =  1.0f;
                if (fx < -1.0f) fx = -1.0f;
                if (fy >  1.0f) fy =  1.0f;
                if (fy < -1.0f) fy = -1.0f;

                /* Non-linear response: square the input for fine control
                 * at small deflections, fast movement at full tilt */
                float ax = fx * fx * MENU_CURSOR_SPEED;
                float ay = fy * fy * MENU_CURSOR_SPEED;
                if (fx < 0) ax = -ax;
                if (fy < 0) ay = -ay;

                /* Accumulate sub-pixel remainder */
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
            /* ----- Gameplay mode: both sticks -> joystick axes ----- */

            /* Scale PS3 stick range (0-98 after deadzone) to Q3 range (-32768..32767) */
            int jlx = lx_raw * 256;
            int jly = ly_raw * 256;  /* positive = down = forward */
            int jrx = rx_raw * 256;
            int jry = ry_raw * 256;

            /* Only send events on change (like Xbox 360 port) */
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

/* ----------------------------------------------------------------
 * ioQ3 input interface
 * ---------------------------------------------------------------- */
void IN_Init(void *windowData)
{
    (void)windowData;

    /* Set default controller binds for gameplay.
     * These can be overridden by the user via the console or q3config.cfg.
     * Follows the Xbox 360 port layout adapted for DualShock 3. */

    /* Face buttons */
    PS3_SetDefaultBind(K_JOY1,  "+moveup");      /* Cross    = jump */
    PS3_SetDefaultBind(K_JOY2,  "+movedown");    /* Circle   = crouch */
    PS3_SetDefaultBind(K_JOY3,  "weapprev");     /* Square   = prev weapon */
    PS3_SetDefaultBind(K_JOY4,  "weapnext");     /* Triangle = next weapon */

    /* Shoulder buttons */
    PS3_SetDefaultBind(K_JOY5,  "+moveleft");    /* L1 = strafe left */
    PS3_SetDefaultBind(K_JOY6,  "+moveright");   /* R1 = strafe right */

    /* Triggers */
    PS3_SetDefaultBind(K_JOY7,  "+zoom");        /* L2 = zoom */
    PS3_SetDefaultBind(K_JOY8,  "+attack");      /* R2 = fire */

    /* Stick clicks */
    PS3_SetDefaultBind(K_JOY9,  "+speed");       /* L3 = run/walk toggle */
    PS3_SetDefaultBind(K_JOY10, "+scores");      /* R3 = scoreboard */

    /* Select */
    PS3_SetDefaultBind(K_JOY11, "+scores");      /* Select = scoreboard */

    /* Joystick axis cvars: match Xbox 360 port axis layout.
     * These should already be set from the command line, but
     * ensure they're correct for twin-stick FPS. */
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
    /* Polling done in PS3_Input_Frame from main loop */
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
