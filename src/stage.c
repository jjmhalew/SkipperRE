/* Stagecompositie: sprites (met inks) naar een 32-bit buffer via het huidige 8-bit palet.
 * Director tekent kanaal 1 onderaan, 48 bovenaan; MIAW-vensters komen daar bovenop. */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define SW 640
#define SH 480
uint32_t *stage_px;
extern Player *CP;

void score_parse_ext(Score *sc, const uint8_t *b, uint32_t sz);

static uint32_t g_lut[256];

static void build_lut(void) {
    for (int i = 0; i < 256; i++)
        g_lut[i] = 0xff000000u | (uint32_t)P.pal[i][0] << 16 | (uint32_t)P.pal[i][1] << 8 | P.pal[i][2];
}

/* matte: wit (index 0) dat met de rand verbonden is wordt transparant; per bitmap gecachet */
typedef struct MatteCache { Bitmap *bm; uint8_t *mask; } MatteCache;
static MatteCache g_matte[4096];
static int g_nmatte;

static const uint8_t *matte_of(Bitmap *bm) {
    for (int i = 0; i < g_nmatte; i++) if (g_matte[i].bm == bm) return g_matte[i].mask;
    int w = bm->w, h = bm->h;
    uint8_t *mask = malloc(w * h);
    memset(mask, 1, w * h);
    int *stack = malloc(sizeof(int) * (w * h + 1));
    int sp = 0;
#define PUSH(x, y) do { int _i = (y) * w + (x); if (mask[_i] && bm->px[_i] == 0) { mask[_i] = 0; stack[sp++] = _i; } } while (0)
    for (int x = 0; x < w; x++) { PUSH(x, 0); PUSH(x, h - 1); }
    for (int y = 0; y < h; y++) { PUSH(0, y); PUSH(w - 1, y); }
    while (sp) {
        int i = stack[--sp], x = i % w, y = i / w;
        if (x > 0) PUSH(x - 1, y);
        if (x < w - 1) PUSH(x + 1, y);
        if (y > 0) PUSH(x, y - 1);
        if (y < h - 1) PUSH(x, y + 1);
    }
#undef PUSH
    free(stack);
    if (g_nmatte < 4096) { g_matte[g_nmatte].bm = bm; g_matte[g_nmatte++].mask = mask; }
    return mask;
}

static inline uint32_t mix(uint32_t d, uint32_t s, int ink, int blend) {
    int dr = d >> 16 & 255, dg = d >> 8 & 255, db = d & 255;
    int sr = s >> 16 & 255, sg = s >> 8 & 255, sb = s & 255;
    int r, g, b;
    switch (ink) {
    case 32: /* blend */
        r = (sr * blend + dr * (100 - blend)) / 100; g = (sg * blend + dg * (100 - blend)) / 100;
        b = (sb * blend + db * (100 - blend)) / 100; break;
    case 33: r = dr + sr > 255 ? 255 : dr + sr; g = dg + sg > 255 ? 255 : dg + sg; b = db + sb > 255 ? 255 : db + sb; break;
    case 34: r = (dr + sr) & 255; g = (dg + sg) & 255; b = (db + sb) & 255; break;
    case 35: r = dr - sr < 0 ? 0 : dr - sr; g = dg - sg < 0 ? 0 : dg - sg; b = db - sb < 0 ? 0 : db - sb; break;
    case 38: r = (dr - sr) & 255; g = (dg - sg) & 255; b = (db - sb) & 255; break;
    case 37: r = sr > dr ? sr : dr; g = sg > dg ? sg : dg; b = sb > db ? sb : db; break;
    case 39: r = sr < dr ? sr : dr; g = sg < dg ? sg : dg; b = sb < db ? sb : db; break;
    case 2: case 3: /* reverse/ghost: XOR-achtig */ r = dr ^ sr; g = dg ^ sg; b = db ^ sb; break;
    default: return s;
    }
    return 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

static void draw_bitmap(uint32_t *dst, int ox, int oy, int cw, int ch_, Bitmap *bm, Channel *c,
                        int l, int t, int r, int b) {
    int w = r - l, h = b - t;
    if (w <= 0 || h <= 0 || !bm->w || !bm->h) return;
    const uint8_t *mm = c->ink == 8 ? matte_of(bm) : NULL;
    int ink = c->ink;
    int blend = c->blend ? c->blend : 100;
    if (ink == 32 && c->blend == 0) blend = 100;
    for (int y = t < 0 ? 0 : t; y < b && y < ch_; y++) {
        int sy = (y - t) * bm->h / h;
        uint32_t *row = dst + (oy + y) * SW + ox;
        for (int x = l < 0 ? 0 : l; x < r && x < cw; x++) {
            int sx = (x - l) * bm->w / w;
            int i = sy * bm->w + sx;
            uint8_t v = bm->px[i];
            if (bm->bpp == 1) {
                /* 1-bit: voorgrond = foreColor, achtergrond = backColor */
                if (ink == 36 || ink == 1 || ink == 8) { if (!v) continue; }
                v = v ? (uint8_t)c->fore : (uint8_t)c->back;
                row[x] = g_lut[v];
                continue;
            }
            if (mm && !mm[i]) continue;
            if (ink == 36 && v == c->back) continue;
            if (ink == 1 && v == 0) continue;
            if (ink == 9 && v == 0) continue;
            uint32_t s = g_lut[v];
            if (ink == 0 || ink == 8 || ink == 36 || ink == 1 || ink == 9) row[x] = s;
            else row[x] = mix(row[x], s, ink, blend);
        }
    }
}

/* ------------------------------------------------------------------ tekst (GDI) */
typedef struct TextCache { Text *t; uint32_t *img; int w, h; } TextCache;
static TextCache g_text[512];
static int g_ntext;

static uint32_t color_of(int c) {
    if (c & 0x1000000) return 0xff000000u | (c & 0xffffff);
    return g_lut[c & 255];
}

static TextCache *text_render(Text *t) {
    TextCache *tc = NULL;
    for (int i = 0; i < g_ntext; i++) if (g_text[i].t == t) tc = &g_text[i];
    if (!tc) {
        if (g_ntext >= 512) return NULL;
        tc = &g_text[g_ntext++];
        tc->t = t;
        t->dirty = 1;
    }
    int w = t->w > 0 ? t->w : 1, h = t->h > 0 ? t->h : 1;
    if (!t->dirty && tc->img && tc->w == w && tc->h == h) return tc;
    free(tc->img);
    tc->w = w; tc->h = h;
    tc->img = malloc(sizeof(uint32_t) * w * h);
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
    uint32_t fc = color_of(t->fore);
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
    free(txt);
    GdiFlush();
    memcpy(tc->img, bits, (size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) tc->img[i] |= 0xff000000u;
    SelectObject(dc, of);
    DeleteObject(f);
    SelectObject(dc, ob);
    DeleteObject(hb);
    DeleteDC(dc);
    t->dirty = 0;
    return tc;
}

static void draw_text(uint32_t *dst, int ox, int oy, int cw, int chh, Text *t, Channel *c, int l, int tp) {
    TextCache *tc = text_render(t);
    if (!tc) return;
    int transparent = c->ink == 36 || c->ink == 8 || c->ink == 1;
    for (int y = 0; y < tc->h; y++) {
        int dy = tp + y;
        if (dy < 0 || dy >= chh) continue;
        for (int x = 0; x < tc->w; x++) {
            int dx = l + x;
            if (dx < 0 || dx >= cw) continue;
            uint32_t s = tc->img[y * tc->w + x];
            if (transparent && (s & 0xffffff) == 0xffffff) continue;
            dst[(oy + dy) * SW + ox + dx] = s;
        }
    }
}

/* ------------------------------------------------------------------ filmloops */
static uint32_t g_loop_tick;

static void draw_channels(uint32_t *dst, int ox, int oy, int cw, int chh, Player *ctx);

static void draw_filmloop(uint32_t *dst, int ox, int oy, int cw, int chh, CastLib *cl, Member *m, Channel *c, int l, int t) {
    if (!m->loop) {
        m->loop = calloc(1, sizeof(Score));
        uint32_t sz;
        const uint8_t *b = dfile_chunk(cl->f, dfile_child(cl->f, m->cast_chunk, FOURCC('S', 'C', 'V', 'W')), &sz);
        score_parse_ext(m->loop, b, sz);
    }
    Score *sc = m->loop;
    if (!sc->nframes) return;
    Frame *fr = &sc->f[g_loop_tick % sc->nframes];
    /* de sprites van de loop staan in loop-coördinaten; de loop-rect begint op (rect_l, rect_t) */
    Player tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.mv = CP->mv;
    for (int i = 0; i < 48; i++) {
        SprRec *r = &fr->spr[i];
        Channel *k = &tmp.ch[i + 1];
        if (!r->member) continue;
        k->lib = r->lib ? r->lib : cl->lib; k->member = r->member; k->ink = r->ink;
        k->fore = r->fore; k->back = r->back;
        k->loch = r->loch - m->rect_l + l; k->locv = r->locv - m->rect_t + t;
        k->w = r->w; k->h = r->h; k->stretch = r->stretch; k->visible = 1; k->blend = r->blend;
    }
    (void)c;
    draw_channels(dst, ox, oy, cw, chh, &tmp);
}

/* ------------------------------------------------------------------ compositie */
static void draw_channels(uint32_t *dst, int ox, int oy, int cw, int chh, Player *ctx) {
    Player *save = CP;
    CP = ctx;
    for (int ch = 1; ch <= NCHAN; ch++) {
        Channel *c = &ctx->ch[ch];
        if (!c->visible || !c->member || !ctx->mv) continue;
        CastLib *cl;
        Member *m = movie_member(ctx->mv, c->lib, c->member, &cl);
        if (!m) continue;
        int l, t, r, b;
        sprite_rect(ch, &l, &t, &r, &b);
        if (r <= 0 || b <= 0 || l >= cw || t >= chh) continue;
        switch (m->type) {
        case MT_BITMAP: {
            Bitmap *bm = member_bitmap(cl, m);
            if (bm) draw_bitmap(dst, ox, oy, cw, chh, bm, c, l, t, r, b);
            break;
        }
        case MT_TEXT: case MT_BUTTON: {
            Text *tx = member_text(cl, m);
            if (tx) draw_text(dst, ox, oy, cw, chh, tx, c, l, t);
            break;
        }
        case MT_SHAPE: {
            uint32_t col = g_lut[c->fore & 255];
            int filled = m->shape_filled;
            for (int y = t < 0 ? 0 : t; y < b && y < chh; y++)
                for (int x = l < 0 ? 0 : l; x < r && x < cw; x++) {
                    if (!filled && y != t && y != b - 1 && x != l && x != r - 1) continue;
                    uint32_t *p = &dst[(oy + y) * SW + ox + x];
                    *p = c->ink == 32 ? mix(*p, col, 32, c->blend ? c->blend : 100) : col;
                }
            break;
        }
        case MT_FILMLOOP:
            draw_filmloop(dst, ox, oy, cw, chh, cl, m, c, l, t);
            break;
        }
    }
    CP = save;
}

void stage_compose(void) {
    if (!stage_px) stage_px = calloc(SW * SH, 4);
    build_lut();
    g_loop_tick++;
    uint32_t bg = g_lut[P.stage_color & 255];
    for (int i = 0; i < SW * SH; i++) stage_px[i] = bg;
    draw_channels(stage_px, 0, 0, SW, SH, &P);
    for (Window *w = P.windows; w; w = w->next) {
        if (!w->open || !w->visible || !w->ctx) continue;
        int ww = w->r - w->l, wh = w->b - w->t;
        if (w->l < 0 || w->t < 0 || w->l + ww > SW || w->t + wh > SH) continue;
        uint32_t wbg = g_lut[w->ctx->mv ? w->ctx->mv->stage_color & 255 : 0];
        for (int y = 0; y < wh; y++)
            for (int x = 0; x < ww; x++) stage_px[(w->t + y) * SW + w->l + x] = wbg;
        draw_channels(stage_px, w->l, w->t, ww, wh, w->ctx);
    }
}

void stage_screenshot(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint8_t hdr[54] = {'B', 'M'};
    uint32_t size = 54 + SW * SH * 4;
    memcpy(hdr + 2, &size, 4);
    uint32_t v = 54; memcpy(hdr + 10, &v, 4);
    v = 40; memcpy(hdr + 14, &v, 4);
    int32_t w = SW, h = -SH; memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
    uint16_t planes = 1, bpp = 32; memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2);
    fwrite(hdr, 1, 54, f);
    fwrite(stage_px, 4, SW * SH, f);
    fclose(f);
}
