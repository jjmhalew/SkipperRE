/* Director-transities 1..52 als pure functie: één tussenbeeld op voortgang t (0..1) van `from` naar `to`.
 * chunk = Directors "chunk size" (stapgrootte in pixels bij wipes, blokgrootte bij dissolves/strips).
 * De nummers zijn die van puppetTransition en van transitie-castleden (spec[2]). */
#include "dir.h"
#include <math.h>
#include <string.h>

#define SW_ 640
#define SH_ 480

static uint32_t hash2(uint32_t x, uint32_t y) {
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
static double rnd01(uint32_t x, uint32_t y) { return (hash2(x, y) >> 8) / 16777216.0; }

/* afstand gequantiseerd op de chunk size (Director schuift in stappen van chunk pixels) */
static int qstep(double v, int chunk) {
    int i = (int)v;
    return chunk > 1 ? i / chunk * chunk : i;
}

/* richtingen voor cover/reveal/push: (dx, dy) = bewegingsrichting */
static int dir_of(int type, int *dx, int *dy) {
    static const signed char tab[][3] = {
        {11, -1, 0}, {12, 1, 0}, {13, 0, 1}, {14, 0, -1},                                   /* push */
        {15, 0, -1}, {16, 1, -1}, {17, 1, 0}, {18, 1, 1}, {19, 0, 1}, {20, -1, 1}, {21, -1, 0}, {22, -1, -1}, /* reveal */
        {29, 0, 1}, {30, -1, 1}, {31, 1, 1}, {32, -1, 0}, {33, 1, 0}, {34, 0, -1}, {35, -1, -1}, {36, 1, -1}, /* cover */
    };
    for (size_t i = 0; i < sizeof tab / sizeof *tab; i++)
        if (tab[i][0] == type) { *dx = tab[i][1]; *dy = tab[i][2]; return 1; }
    return 0;
}

/* scale s: from/to/out zijn (640 s) x (480 s); de maskers worden op podiumpixels bepaald, schuiven op schaalpixels.
 * Het effect speelt in de rechthoek (rx, ry, rw, rh) in podiumpixels ("changing area only": alleen het deel dat
 * verandert; anders het hele podium); daarbuiten staat al het nieuwe beeld. */
void trans_frame_r(uint32_t *out, const uint32_t *from, const uint32_t *to, int type, int chunk, double t, int s,
                   int rx, int ry, int rw, int rh) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    if (chunk < 1) chunk = 1;
    if (s < 1) s = 1;
    if (rx < 0) rw += rx, rx = 0;
    if (ry < 0) rh += ry, ry = 0;
    if (rx + rw > SW_) rw = SW_ - rx;
    if (ry + rh > SH_) rh = SH_ - ry;
    if (rw <= 0 || rh <= 0) { memcpy(out, to, (size_t)SW_ * SH_ * s * s * 4); return; }
    if (rw < SW_ || rh < SH_) memcpy(out, to, (size_t)SW_ * SH_ * s * s * 4);
    const int W = rw, H = rh, STR = SW_ * s, OX = rx * s, OY = ry * s;
#define IDX(x, y) ((size_t)(OY + (y)) * STR + OX + (x))
    int dx = 0, dy = 0, SWS = W * s, SHS = H * s;
    if (dir_of(type, &dx, &dy)) {
        int ox = qstep(t * W, chunk) * s, oy = qstep(t * H, chunk) * s;   /* afgelegde afstand */
        for (int y = 0; y < SHS; y++)
            for (int x = 0; x < SWS; x++) {
                uint32_t p;
                if (type <= 14) {            /* push: oud schuift weg, nieuw komt er direct achteraan */
                    int sx = x - dx * ox, sy = y - dy * oy;
                    if (sx >= 0 && sx < SWS && sy >= 0 && sy < SHS) p = from[IDX(sx, sy)];
                    else p = to[IDX(sx + dx * SWS, sy + dy * SHS)];
                } else if (type <= 22) {     /* reveal: oud schuift weg over het nieuwe */
                    int sx = x - dx * ox, sy = y - dy * oy;
                    p = sx >= 0 && sx < SWS && sy >= 0 && sy < SHS ? from[IDX(sx, sy)] : to[IDX(x, y)];
                } else {                     /* cover: nieuw schuift erover */
                    int sx = x + dx * (SWS - ox), sy = y + dy * (SHS - oy);
                    p = sx >= 0 && sx < SWS && sy >= 0 && sy < SHS ? to[IDX(sx, sy)] : from[IDX(x, y)];
                }
                out[IDX(x, y)] = p;
            }
        return;
    }
    int bw = chunk < 2 ? 8 : chunk;          /* blokgrootte voor boxy/strips/blinds */
    for (int Y = 0; Y < SHS; Y++)
        for (int X = 0; X < SWS; X++) {
            int show, x = X / s, y = Y / s;
            double cx = fabs(x + 0.5 - W / 2.0) / (W / 2.0), cy = fabs(y + 0.5 - H / 2.0) / (H / 2.0);
            switch (type) {
            case 1: show = x < qstep(t * W, chunk); break;              /* wipe right */
            case 2: show = W - 1 - x < qstep(t * W, chunk); break;      /* wipe left */
            case 3: show = y < qstep(t * H, chunk); break;              /* wipe down */
            case 4: show = H - 1 - y < qstep(t * H, chunk); break;      /* wipe up */
            case 5: show = cx < t; break;                               /* center out, horizontal */
            case 6: show = cx > 1 - t; break;                           /* edges in, horizontal */
            case 7: show = cy < t; break;                               /* center out, vertical */
            case 8: show = cy > 1 - t; break;                           /* edges in, vertical */
            case 9: case 47: show = cx < t && cy < t; break;            /* center out square, zoom open */
            case 10: case 48: show = cx > 1 - t || cy > 1 - t; break;   /* edges in square, zoom close */
            case 23: case 50: case 51: case 52:                         /* dissolve pixels / bits */
                show = rnd01(x / chunk, y / chunk) < t; break;
            case 24: show = rnd01(x / bw, y / (bw * 3 / 4 > 0 ? bw * 3 / 4 : 1)) < t; break;   /* boxy rects */
            case 25: show = rnd01(x / bw, y / bw) < t; break;           /* boxy squares */
            case 26: {                                                  /* dissolve patterns: geordende dither */
                static const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
                show = (bayer[y & 3][x & 3] + 0.5) / 16.0 < t;
                break;
            }
            case 27: show = rnd01(7, y / chunk) < t; break;             /* random rows */
            case 28: show = rnd01(x / chunk, 11) < t; break;            /* random columns */
            case 37: { int band = 48; show = y % band < t * band; break; }          /* venetian blinds */
            case 49: { int band = 64; show = x % band < t * band; break; }          /* vertical blinds */
            case 38: {                                                  /* checkerboard: eerst de ene helft */
                int cell = 40, par = ((x / cell) + (y / cell)) & 1;
                double f = (y % cell + 0.5) / cell, tt = par ? t * 2 - 1 : t * 2;
                show = f < tt;
                break;
            }
            case 39: case 40: case 45: case 46: {                      /* strips (kolommen), bouwen links/rechts */
                int nc = (W + bw - 1) / bw, c = x / bw;
                int k = (type == 39 || type == 45) ? nc - 1 - c : c;   /* volgorde van opbouwen */
                double p = t * 2 - (double)k / nc;
                double fill = p < 0 ? 0 : p > 1 ? 1 : p;
                int yy = (type == 39 || type == 40) ? H - 1 - y : y;   /* van onder of van boven */
                show = yy < fill * H;
                break;
            }
            case 41: case 42: case 43: case 44: {                      /* strips (rijen), bouwen omlaag/omhoog */
                int nr = (H + bw - 1) / bw, r = y / bw;
                int k = (type == 42 || type == 44) ? nr - 1 - r : r;
                double p = t * 2 - (double)k / nr;
                double fill = p < 0 ? 0 : p > 1 ? 1 : p;
                int xx = (type == 43 || type == 44) ? W - 1 - x : x;   /* van rechts of van links */
                show = xx < fill * W;
                break;
            }
            default: show = rnd01(x, y) < t; break;
            }
            out[IDX(X, Y)] = show ? to[IDX(X, Y)] : from[IDX(X, Y)];
        }
#undef IDX
}

void trans_frame_s(uint32_t *out, const uint32_t *from, const uint32_t *to, int type, int chunk, double t, int s) {
    trans_frame_r(out, from, to, type, chunk, t, s, 0, 0, SW_, SH_);
}

void trans_frame(uint32_t *out, const uint32_t *from, const uint32_t *to, int type, int chunk, double t) {
    trans_frame_s(out, from, to, type, chunk, t, 1);
}
