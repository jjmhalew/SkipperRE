/* text_ttf.c - tekstmembers tekenen buiten Windows, met stb_truetype en de ingebouwde Liberation-lettertypen
 * (fonts.h: Sans voor Arial en de rest, Mono voor Courier). Doet na wat GDI's DrawText deed: regels op '\r',
 * afbreken op woordgrenzen binnen de breedte, links / midden / rechts, vet / cursief / onderstreept, geen
 * anti-aliasing. De tekst staat in Windows-1252. */
#if !defined(_WIN32) || defined(TEXT_TTF)
#include "dir.h"
#include <math.h>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"
#include "fonts.h"

static stbtt_fontinfo g_font[6];
static int g_ready;

static void init(void) {
    const unsigned char *d[6] = {font_sans_regular, font_sans_bold, font_sans_italic, font_sans_bolditalic,
                                 font_mono_regular, font_mono_bold};
    for (int i = 0; i < 6; i++) stbtt_InitFont(&g_font[i], d[i], 0);
    g_ready = 1;
}

static stbtt_fontinfo *pick(const char *name, int style) {
    if (!g_ready) init();
    if (strstr(name, "Courier") || strstr(name, "Mono")) return &g_font[(style & 1) ? 5 : 4];
    return &g_font[((style & 1) ? 1 : 0) + ((style & 2) ? 2 : 0)];
}

static int cp1252(unsigned char c) {
    static const unsigned short hi[32] = {0x20ac, 0, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160,
                                          0x2039, 0x152, 0, 0x17d, 0, 0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013,
                                          0x2014, 0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0, 0x17e, 0x178};
    if (c >= 0x80 && c < 0xa0) return hi[c - 0x80] ? hi[c - 0x80] : '?';
    return c;
}

/* breedte van s[0..n) in pixels */
static float measure(stbtt_fontinfo *f, float sc, const char *s, int n) {
    float x = 0;
    for (int i = 0; i < n; i++) {
        int c = cp1252((unsigned char)s[i]), adv, lsb;
        stbtt_GetCodepointHMetrics(f, c, &adv, &lsb);
        x += adv * sc;
        if (i + 1 < n) x += sc * stbtt_GetCodepointKernAdvance(f, c, cp1252((unsigned char)s[i + 1]));
    }
    return x;
}

static void draw_run(stbtt_fontinfo *f, float sc, const char *s, int n, float x, int base, uint32_t *img, int w, int h,
                     uint32_t fc) {
    static unsigned char glyph[256 * 256];
    for (int i = 0; i < n; i++) {
        int c = cp1252((unsigned char)s[i]), adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(f, c, &adv, &lsb);
        stbtt_GetCodepointBitmapBox(f, c, sc, sc, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0 && gw <= 256 && gh <= 256) {
            stbtt_MakeCodepointBitmap(f, glyph, gw, gh, gw, sc, sc, c);
            int ox = (int)floorf(x + 0.5f) + x0, oy = base + y0;
            for (int y = 0; y < gh; y++) {
                int dy = oy + y;
                if (dy < 0 || dy >= h) continue;
                for (int xx = 0; xx < gw; xx++) {
                    int dx = ox + xx;
                    if (dx >= 0 && dx < w && glyph[y * gw + xx] >= 128) img[dy * w + dx] = fc;
                }
            }
        }
        x += adv * sc;
        if (i + 1 < n) x += sc * stbtt_GetCodepointKernAdvance(f, c, cp1252((unsigned char)s[i + 1]));
    }
}

void text_raster(Text *t, uint32_t *img, int w, int h, uint32_t fc, int caret) {
    for (int i = 0; i < w * h; i++) img[i] = 0xffffffffu;   /* wit = achtergrond */
    stbtt_fontinfo *f = pick(t->font, t->style);
    int size = t->font_size > 0 ? t->font_size : 12;
    float sc = stbtt_ScaleForMappingEmToPixels(f, (float)size);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(f, &asc, &desc, &gap);
    int ascent = (int)ceilf(asc * sc), lh = (int)ceilf((asc - desc) * sc);
    const char *s = t->text ? t->text : "";
    int y = 0;
    float last_w = 0;
    int last_y = 0;
    fc |= 0xff000000u;
    for (;;) {   /* per alinea (tot \r of \n) */
        const char *e = s;
        while (*e && *e != '\r' && *e != '\n') e++;
        const char *p = s;
        do {   /* per regel: zoveel woorden als passen (een te lang woord staat alleen) */
            const char *q = p, *fit = NULL;
            while (q < e) {
                const char *we = q;
                while (we < e && *we == ' ') we++;
                while (we < e && *we != ' ') we++;
                if (fit && measure(f, sc, p, (int)(we - p)) > w) break;
                fit = q = we;
            }
            if (!fit) fit = e;
            int n = (int)(fit - p);
            while (n > 0 && p[n - 1] == ' ') n--;
            float lw = measure(f, sc, p, n);
            float x = t->align == 1 ? (w - lw) / 2 : t->align == -1 ? w - lw : 0;
            draw_run(f, sc, p, n, x, y + ascent, img, w, h, fc);
            if (t->style & 4) {   /* onderstreept */
                int uy = y + ascent + 1;
                for (int xx = (int)x; xx < (int)(x + lw) && uy < h; xx++) if (xx >= 0 && xx < w) img[uy * w + xx] = fc;
            }
            last_w = x + lw;
            last_y = y;
            y += lh;
            p = fit;
            while (p < e && *p == ' ') p++;
        } while (p < e);
        if (!*e) break;
        s = e + 1;
        if (*e == '\r' && *s == '\n') s++;
    }
    if (caret) {   /* invoegpositie aan het eind van de laatste regel */
        int cx = (int)last_w + 1;
        for (int yy = last_y; yy < last_y + lh && yy < h; yy++) if (cx >= 0 && cx < w && yy >= 0) img[yy * w + cx] = fc;
    }
}
#endif
