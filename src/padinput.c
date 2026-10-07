/* padinput.c - een controller als muis (Skipper & Skeeto kende zelf alleen muis en toetsenbord). De hosts roepen
 * pad_input elke keer dat ze OS-berichten verwerken:
 *   sticks          aanwijzer bewegen (verder duwen = sneller), d-pad: langzaam en precies
 *   A / Kruis       klikken; vasthouden = slepen (spullen naar de zak)      touchpad-klik ook
 *   X / Vierkant    spel laden (F5)              Y / Driehoek   spel opslaan (F6)
 *   B / Rondje      Esc: dialoog dicht, in het spel de vraag om te stoppen    Start / Options ook
 *   Back / Share    uitleg (F1)                  LB / L1  ondertitels (F2)    RB / R1  achtergrondmuziek (F3)
 *   LT / L2         zachter (pijl omlaag)        RT / R2  harder (pijl omhoog) */
#include "dir.h"
#include "pad.h"
#include <math.h>

static float g_fx = -1, g_fy;
static uint32_t g_last, g_prev, g_used_ms;
static int g_click;

int pad_recent(void) { return g_used_ms && plat_ms() - g_used_ms < 4000; }
uint32_t pad_used_ms(void) { return g_used_ms; }

static float curve(float v) {   /* dode zone 0,18, daarna kwadratisch */
    float m = fabsf(v);
    if (m < 0.18f) return 0;
    m = (m - 0.18f) / 0.82f;
    return (v < 0 ? -1 : 1) * m * m;
}

/* 1 = de aanwijzer is door de pad verplaatst (de host zet dan de echte muis daarheen) */
int pad_input(int focused, unsigned devchanges) {
    uint32_t now = plat_ms();
    if (g_last && now - g_last < 8) return 0;   /* hoogstens ~120 keer per seconde */
    PadState st;
    pad_poll(&st, focused, devchanges);
    float dt = g_last ? (now - g_last) / 1000.0f : 0;
    if (dt > 0.1f) dt = 0.1f;
    g_last = now;
    uint32_t b = st.buttons;
    if (!focused) b = 0;
    int moved = 0;
    if (st.buttons) g_used_ms = now;
    /* aanwijzer */
    float vx = curve(st.lx) + curve(st.rx), vy = curve(st.ly) + curve(st.ry);
    if (b & 1u << PAD_LEFT) vx -= 0.18f;
    if (b & 1u << PAD_RIGHT) vx += 0.18f;
    if (b & 1u << PAD_UP) vy -= 0.18f;
    if (b & 1u << PAD_DOWN) vy += 0.18f;
    if (focused && (vx != 0 || vy != 0)) {
        if (g_fx < 0 || fabsf(g_fx - P.mouse_x) > 1.5f || fabsf(g_fy - P.mouse_y) > 1.5f) { g_fx = (float)P.mouse_x; g_fy = (float)P.mouse_y; }
        g_fx += vx * 560.0f * dt;
        g_fy += vy * 560.0f * dt;
        g_fx = g_fx < 0 ? 0 : g_fx > 639 ? 639 : g_fx;
        g_fy = g_fy < 0 ? 0 : g_fy > 479 ? 479 : g_fy;
        if ((int)g_fx != P.mouse_x || (int)g_fy != P.mouse_y) {
            P.mouse_x = (int)g_fx;
            P.mouse_y = (int)g_fy;
            moved = 1;
        }
        g_used_ms = now;
    }
    /* klikken */
    int click = (b & (1u << PAD_A | 1u << PAD_TOUCH)) != 0;
    if (click != g_click) {
        g_click = click;
        P.mouse_down = click;
        input_push(click ? 1 : 2, P.mouse_x, P.mouse_y, 0, 0);
    }
    /* sneltoetsen (Mac-keyCodes, zoals main.c ze van het toetsenbord krijgt) */
    static const struct { int button, code, ch; } keys[] = {
        {PAD_X, 96, 0}, {PAD_Y, 97, 0}, {PAD_B, 53, 27}, {PAD_START, 53, 27}, {PAD_BACK, 122, 0},
        {PAD_LB, 120, 0}, {PAD_RB, 99, 0}, {PAD_LT, 125, 0}, {PAD_RT, 126, 0}};
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
        uint32_t m = 1u << keys[i].button;
        if ((b & m) && !(g_prev & m)) input_push(3, 0, 0, keys[i].code, keys[i].ch);
        if (!(b & m) && (g_prev & m)) input_push(4, 0, 0, keys[i].code, keys[i].ch);
    }
    g_prev = b;
    return moved;
}
