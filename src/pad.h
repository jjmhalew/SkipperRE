/* pad.h - controllers (from WoodyRE): Sony DualShock 4 and DualSense over USB and Bluetooth (raw HID), Xbox pads
 * and everything that speaks XInput (Windows: pad.c), or SDL2's game controllers (elsewhere: pad_sdl.c). All pads
 * together act as one; padinput.c turns them into a mouse and shortcut keys. */
#ifndef SKIPPER_PAD_H
#define SKIPPER_PAD_H
#include <stdint.h>

/* buttons by their Xbox names; on a Sony pad A = Cross, B = Circle, X = Square, Y = Triangle, LB/RB = L1/R1, LT/RT = L2/R2,
 * BACK = Share / Create, START = Options, LS/RS = L3/R3, GUIDE = PS, TOUCH = the touchpad click */
enum { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_LB, PAD_RB, PAD_LT, PAD_RT, PAD_BACK, PAD_START, PAD_LS, PAD_RS,
       PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_GUIDE, PAD_TOUCH, PAD_NBUTTONS };
enum { PADK_NONE, PADK_XBOX, PADK_DS4, PADK_DS5, PADK_SWITCH };   /* PADK_SWITCH: SDL builds only, by its labels (A = the right button) */

typedef struct {
    int kind;                    /* PADK_*: the pad that was used last; PADK_NONE while none is connected */
    uint32_t buttons;            /* 1 << PAD_*; LT / RT count as pressed from a quarter of their travel */
    float lx, ly, rx, ry;        /* the sticks, -1..1 with y down = + (as the original's joystick), no dead zone */
    float lt, rt;                /* the triggers, 0..1 */
} PadState;

/* once per frame: reads every pad (input and rumble are paused while the window is not in front); devchanges = a counter
 * of WM_DEVICECHANGE, a new value makes it look for pads that came or went */
void pad_poll(PadState *st, int focused, unsigned devchanges);
void pad_rumble(float strength, float seconds);   /* strength 0..1 for seconds; the strongest running one wins */
void pad_set_strength(float s);                   /* the Vibration option, 0..1 */
void pad_close(void);                             /* motors off, the DualSense light back to the system's (also at exit) */
const char *pad_kind_name(int kind);
#endif
