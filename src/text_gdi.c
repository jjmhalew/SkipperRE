/* text_gdi.c - tekstmembers tekenen met GDI (Windows), zoals de Director-projector dat deed: de lettersoort uit de
 * fontmap van de film, zonder anti-aliasing. Elders: text_ttf.c. */
#if defined(_WIN32) && !defined(TEXT_TTF)
#include "dir.h"
#include <windows.h>
#include "textlay.h"

static int measure_cb(void *ctx, const char *s, int n) {
    SIZE sz = {0, 0};
    if (n > 0) GetTextExtentPoint32A((HDC)ctx, s, n, &sz);
    return sz.cx;
}

/* img (w x h, 0xAARRGGBB) wordt wit met de tekst in kleur fc; caret = invoegstreep aan het eind.
 * Geeft de hoogte die de tekst bij breedte w nodig heeft; img NULL = alleen meten. */
int text_raster(Text *t, uint32_t *img, int w, int h, uint32_t fc, int caret) {
    HDC dc = CreateCompatibleDC(NULL);
    if (!img) h = 1;
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits;
    HBITMAP hb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ ob = SelectObject(dc, hb);
    memset(bits, 0xff, (size_t)w * h * 4);   /* wit = achtergrond */
    HFONT f = CreateFontA(-t->font_size, 0, 0, 0, (t->style & 1) ? FW_BOLD : FW_NORMAL, (t->style & 2) != 0,
                          (t->style & 4) != 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, t->font);
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, RGB(fc >> 16 & 255, fc >> 8 & 255, fc & 255));
    SetBkMode(dc, TRANSPARENT);
    UINT fmt = DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP | (t->align == 1 ? DT_CENTER : t->align == -1 ? DT_RIGHT : DT_LEFT);
    TEXTMETRICA tm;
    GetTextMetricsA(dc, &tm);
    int lh = tm.tmHeight;
    const char *s = t->text ? t->text : "";
    int nl = text_layout(s, w, measure_cb, dc, NULL, 0);
    TLine *ln = malloc(sizeof(TLine) * (nl ? nl : 1));
    text_layout(s, w, measure_cb, dc, ln, nl);
    int y = -text_scroll(nl, lh, h, img && caret);
    for (int i = 0; img && i < nl; i++, y += lh) {
        RECT rc = {0, y, w, y + lh};
        if (ln[i].n) DrawTextA(dc, ln[i].p, ln[i].n, &rc, fmt);
        if (caret && i == nl - 1) {   /* invoegpositie aan het eind van de laatste regel */
            int lw = measure_cb(dc, ln[i].p, ln[i].n);
            int x = (t->align == 1 ? (w - lw) / 2 : t->align == -1 ? w - lw : 0) + lw;
            RECT cr = {x + 1, y, x + 2, y + lh};
            HBRUSH br = CreateSolidBrush(RGB(fc >> 16 & 255, fc >> 8 & 255, fc & 255));
            FillRect(dc, &cr, br);
            DeleteObject(br);
        }
    }
    free(ln);
    GdiFlush();
    if (img) {
        memcpy(img, bits, (size_t)w * h * 4);
        for (int i = 0; i < w * h; i++) img[i] |= 0xff000000u;
    }
    SelectObject(dc, of);
    DeleteObject(f);
    SelectObject(dc, ob);
    DeleteObject(hb);
    DeleteDC(dc);
    return nl * lh;
}
#endif
