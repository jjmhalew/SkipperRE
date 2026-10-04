/* text_gdi.c - tekstmembers tekenen met GDI (Windows), zoals de Director-projector dat deed: de lettersoort uit de
 * fontmap van de film, zonder anti-aliasing. Elders: text_ttf.c. */
#ifdef _WIN32
#include "dir.h"
#include <windows.h>

/* img (w x h, 0xAARRGGBB) wordt wit met de tekst in kleur fc; caret = invoegstreep aan het eind */
void text_raster(Text *t, uint32_t *img, int w, int h, uint32_t fc, int caret) {
    HDC dc = CreateCompatibleDC(NULL);
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
    RECT rc = {0, 0, w, h};
    UINT fmt = DT_WORDBREAK | DT_NOPREFIX | (t->align == 1 ? DT_CENTER : t->align == -1 ? DT_RIGHT : DT_LEFT);
    /* Director gebruikt \r als regeleinde */
    int n = (int)strlen(t->text ? t->text : "");
    char *txt = malloc(n * 2 + 1);
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (t->text[i] == '\r') { txt[k++] = '\r'; txt[k++] = '\n'; }
        else txt[k++] = t->text[i];
    }
    txt[k] = 0;
    DrawTextA(dc, txt, k, &rc, fmt);
    if (caret) {
        /* invoegpositie aan het eind van de laatste regel (links uitgelijnde naamvelden) */
        const char *last = strrchr(txt, '\n');
        last = last ? last + 1 : txt;
        int nl = 0;
        for (int i = 0; i < k; i++) if (txt[i] == '\n') nl++;
        SIZE sz = {0, 0}, lh = {0, 0};
        GetTextExtentPoint32A(dc, last, (int)strlen(last), &sz);
        GetTextExtentPoint32A(dc, "Ag", 2, &lh);
        RECT cr = {sz.cx + 1, nl * lh.cy, sz.cx + 2, nl * lh.cy + lh.cy};
        HBRUSH br = CreateSolidBrush(RGB(fc >> 16 & 255, fc >> 8 & 255, fc & 255));
        FillRect(dc, &cr, br);
        DeleteObject(br);
    }
    free(txt);
    GdiFlush();
    memcpy(img, bits, (size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) img[i] |= 0xff000000u;
    SelectObject(dc, of);
    DeleteObject(f);
    SelectObject(dc, ob);
    DeleteObject(hb);
    DeleteDC(dc);
}
#endif
