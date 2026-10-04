/* Stagecompositie: sprites (met inks) naar een 32-bit buffer via het huidige 8-bit palet.
 * Director tekent kanaal 1 onderaan, 48 bovenaan; MIAW-vensters komen daar bovenop.
 *
 * Alles wordt in podiumcoördinaten (640x480) gerekend en op schaal g_s getekend: 1 voor stage_px (het spel, tests,
 * screenshots), 2..4 voor stage_hd_px als een texture pack grotere plaatjes heeft (texpack.c). Een vervangen bitmap
 * levert de kleuren; welke pixels doorzichtig zijn bepaalt altijd de originele bitmap. */
#include "dir.h"
#include <stdlib.h>
#include <string.h>

#define SW 640
#define SH 480
uint32_t *stage_px, *stage_hd_px;
extern Player *CP;

void score_parse_ext(Score *sc, const uint8_t *b, uint32_t sz);

static uint32_t g_lut[256];
static int g_s = 1, g_stride = SW;   /* schaal en regellengte van de buffer waarin nu getekend wordt */
static int g_fading;                 /* paletovergang bezig: vervangen plaatjes meekleuren */
static uint16_t g_fade[256][3];      /* per index: huidige kleur / kleur zonder overgang (8.8) */

static void build_lut(void) {
    for (int i = 0; i < 256; i++)
        g_lut[i] = 0xff000000u | (uint32_t)P.pal[i][0] << 16 | (uint32_t)P.pal[i][1] << 8 | P.pal[i][2];
    /* tijdens een paletovergang: naar zwart/wit (vlak doel) telt het palet van vóór de overgang als 'echt', anders het doel */
    g_fading = P.pal_fade_left > 0;
    if (!g_fading) return;
    int flat = 1;
    for (int i = 1; i < 256 && flat; i++)
        for (int k = 0; k < 3; k++) if (P.pal_target[i][k] != P.pal_target[0][k]) flat = 0;
    for (int i = 0; i < 256; i++)
        for (int k = 0; k < 3; k++) {
            int ref = flat ? P.pal_from[i][k] : P.pal_target[i][k], cur = P.pal[i][k];
            g_fade[i][k] = (uint16_t)(ref ? (cur * 256 / ref > 1024 ? 1024 : cur * 256 / ref) : 256);
        }
}

static inline uint32_t faded(uint32_t c, int v) {
    int r = (int)(c >> 16 & 255) * g_fade[v][0] >> 8, g = (int)(c >> 8 & 255) * g_fade[v][1] >> 8, b = (int)(c & 255) * g_fade[v][2] >> 8;
    return 0xff000000u | (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(g > 255 ? 255 : g) << 8 | (uint32_t)(b > 255 ? 255 : b);
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

/* rij Y (in schaalpixels) van de buffer, met venster-offset (ox, oy) in podiumpixels */
static inline uint32_t *row_of(uint32_t *dst, int ox, int oy, int y) { return dst + (size_t)(oy * g_s + y) * g_stride + ox * g_s; }

static void draw_bitmap(uint32_t *dst, int ox, int oy, int cw, int ch_, Bitmap *bm, Channel *c,
                        int l, int t, int r, int b) {
    int w = r - l, h = b - t, s = g_s;
    if (w <= 0 || h <= 0 || !bm->w || !bm->h) return;
    const uint8_t *mm = c->ink == 8 ? matte_of(bm) : NULL;
    int ink = c->ink;
    int blend = c->blend ? c->blend : 100;
    if (ink == 32 && c->blend == 0) blend = 100;
    int hw = 0, hh = 0;
    const uint32_t *hd = bm->bpp == 1 ? NULL : texpack_get(bm, g_lut, CP->mv ? CP->mv->name : "", &hw, &hh);
    int L = l * s, T = t * s, WS = w * s, HS = h * s;
    int X0 = (l < 0 ? 0 : l) * s, X1 = (r < cw ? r : cw) * s, Y1 = (b < ch_ ? b : ch_) * s;
    for (int Y = (t < 0 ? 0 : t) * s; Y < Y1; Y++) {
        int sy = (Y - T) * bm->h / HS;
        const uint32_t *hrow = hd ? hd + (size_t)((Y - T) * hh / HS) * hw : NULL;
        uint32_t *row = row_of(dst, ox, oy, Y);
        for (int X = X0; X < X1; X++) {
            int sx = (X - L) * bm->w / WS;
            int i = sy * bm->w + sx;
            uint8_t v = bm->px[i];
            if (bm->bpp == 1) {
                /* 1-bit: voorgrond = foreColor, achtergrond = backColor */
                if (ink == 36 || ink == 1 || ink == 8) { if (!v) continue; }
                v = v ? (uint8_t)c->fore : (uint8_t)c->back;
                row[X] = g_lut[v];
                continue;
            }
            if (mm && !mm[i]) continue;
            if (ink == 36 && v == c->back) continue;
            if (ink == 1 && v == 0) continue;
            if (ink == 9 && v == 0) continue;
            uint32_t sc = g_lut[v];
            if (hrow) {   /* texture pack: kleur uit de vervanging */
                uint32_t p = hrow[(X - L) * hw / WS];
                if (p >> 24 < 128) continue;
                sc = g_fading ? faded(p, v) : p | 0xff000000u;
            }
            if (ink == 0 || ink == 8 || ink == 36 || ink == 1 || ink == 9) row[X] = sc;
            else row[X] = mix(row[X], sc, ink, blend);
        }
    }
}

/* ------------------------------------------------------------------ tekst (text_gdi.c / text_ttf.c) */
typedef struct TextCache { Text *t; uint32_t *img; int w, h, caret; } TextCache;
static TextCache g_text[512];
static int g_ntext;

static uint32_t color_of(int c) {
    if (c & 0x1000000) return 0xff000000u | (c & 0xffffff);
    return g_lut[c & 255];
}

static TextCache *text_render(Text *t, int caret) {
    TextCache *tc = NULL;
    for (int i = 0; i < g_ntext; i++) if (g_text[i].t == t) tc = &g_text[i];
    if (!tc) {
        if (g_ntext >= 512) return NULL;
        tc = &g_text[g_ntext++];
        tc->t = t;
        t->dirty = 1;
    }
    int w = t->w > 0 ? t->w : 1;
    if (t->dirty && t->box_type == 0) {   /* "adjust to fit": de hoogte volgt de tekst (ondertitels op 18 pt) */
        int need = text_raster(t, NULL, w, 0, 0, 0);
        if (need > 0) t->h = need;
    }
    int h = t->h > 0 ? t->h : 1, fr = text_frame(t), W = w + fr, H = h + fr;
    if (!t->dirty && tc->img && tc->w == W && tc->h == H && tc->caret == caret) return tc;
    tc->caret = caret;
    free(tc->img);
    tc->w = W; tc->h = H;
    tc->img = malloc(sizeof(uint32_t) * W * H);
    uint32_t *in = fr ? malloc(sizeof(uint32_t) * w * h) : tc->img;
    text_raster(t, in, w, h, color_of(t->fore), caret);
    /* alfa in de cache: 0 = niets, 1 = achtergrond (valt weg bij doorzichtige inkt), 255 = tekst / rand / schaduw */
    uint32_t bg = 0x01000000u | (t->bg & 0xffffff);
    for (int i = 0; i < w * h; i++) in[i] = (in[i] & 0xffffff) == 0xffffff ? bg : in[i] | 0xff000000u;
    if (fr) {   /* veldkader: rand in zwart, marge in de achtergrondkleur, slagschaduw rechtsonder */
        int b = t->border, m = b + t->gutter, sh = t->shadow;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint32_t p = 0;
                if (x < W - sh && y < H - sh) {
                    if (x < b || y < b || x >= W - sh - b || y >= H - sh - b) p = 0xff000000u;
                    else if (x >= m && y >= m && x < m + w && y < m + h) p = in[(y - m) * w + x - m];
                    else p = bg;
                } else if (x >= sh && y >= sh) p = 0xff000000u;
                tc->img[y * W + x] = p;
            }
        free(in);
    }
    t->dirty = 0;
    return tc;
}

static void draw_text(uint32_t *dst, int ox, int oy, int cw, int chh, Text *t, Channel *c, int l, int tp, int caret) {
    TextCache *tc = text_render(t, caret);
    if (!tc) return;
    int transparent = c->ink == 36 || c->ink == 8 || c->ink == 1, s = g_s;
    int Y0 = (tp < 0 ? 0 : tp) * s, Y1 = (tp + tc->h < chh ? tp + tc->h : chh) * s;
    int X0 = (l < 0 ? 0 : l) * s, X1 = (l + tc->w < cw ? l + tc->w : cw) * s;
    for (int Y = Y0; Y < Y1; Y++) {
        const uint32_t *src = tc->img + (size_t)(Y / s - tp) * tc->w;
        uint32_t *row = row_of(dst, ox, oy, Y);
        for (int X = X0; X < X1; X++) {
            uint32_t p = src[X / s - l];
            if (!(p >> 24) || (transparent && p >> 24 == 1)) continue;
            row[X] = p | 0xff000000u;
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
        k->lib = (r->lib && r->lib != 0xffff) ? r->lib : cl->lib;   /* -1 = de cast van de loop zelf */
        k->member = r->member; k->ink = r->ink;
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
    int fch = ctx->mv ? player_focus_field() : 0;
    int blink = (now_ms() / 500) & 1, s = g_s;
    for (int ch = 1; ch <= NCHAN; ch++) {
        Channel *c = &ctx->ch[ch];
        if (!c->visible || !c->member || !ctx->mv || c->killed) continue;
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
            if (tx) draw_text(dst, ox, oy, cw, chh, tx, c, l, t, ch == fch && !blink);
            break;
        }
        case MT_SHAPE: {
            /* background transparent: pixels in de achtergrondkleur verdwijnen. De hotspots van het
             * spel (HSC5Clock, ExitDown, ...) zijn vormen met fore = back = 0 en dus onzichtbaar. */
            if ((c->ink == 36 || c->ink == 1) && (c->fore & 255) == (c->back & 255)) break;
            uint32_t col = g_lut[c->fore & 255];
            int filled = m->shape_filled;
            for (int Y = (t < 0 ? 0 : t) * s; Y < (b < chh ? b : chh) * s; Y++) {
                uint32_t *row = row_of(dst, ox, oy, Y);
                for (int X = (l < 0 ? 0 : l) * s; X < (r < cw ? r : cw) * s; X++) {
                    if (!filled && Y >= (t + 1) * s && Y < (b - 1) * s && X >= (l + 1) * s && X < (r - 1) * s) continue;
                    row[X] = c->ink == 32 ? mix(row[X], col, 32, c->blend ? c->blend : 100) : col;
                }
            }
            break;
        }
        case MT_FILMLOOP:
            draw_filmloop(dst, ox, oy, cw, chh, cl, m, c, l, t);
            break;
        case MT_VIDEO: {   /* digitale video: huidig frame, geschaald naar de sprite-rect (copy-ink) */
            int vw, vh;
            const uint32_t *px = chan_video_frame(c, &vw, &vh);
            if (!px || r <= l || b <= t) break;
            int WS = (r - l) * s, HS = (b - t) * s;
            for (int Y = (t < 0 ? 0 : t) * s; Y < (b < chh ? b : chh) * s; Y++) {
                const uint32_t *src = px + (size_t)((Y - t * s) * vh / HS) * vw;
                uint32_t *row = row_of(dst, ox, oy, Y);
                for (int X = (l < 0 ? 0 : l) * s; X < (r < cw ? r : cw) * s; X++) row[X] = src[(X - l * s) * vw / WS];
            }
            break;
        }
        }
    }
    CP = save;
}

static void compose(uint32_t *dst, int s) {
    g_s = s;
    g_stride = SW * s;
    uint32_t bg = g_lut[P.stage_color & 255];
    for (size_t i = 0, n = (size_t)SW * SH * s * s; i < n; i++) dst[i] = bg;
    draw_channels(dst, 0, 0, SW, SH, &P);
    for (Window *w = P.windows; w; w = w->next) {
        if (!w->open || !w->visible || !w->ctx) continue;
        int ww = w->r - w->l, wh = w->b - w->t;
        if (w->l < 0 || w->t < 0 || w->l + ww > SW || w->t + wh > SH) continue;
        uint32_t wbg = g_lut[w->ctx->mv ? w->ctx->mv->stage_color & 255 : 0];
        for (int y = 0; y < wh * s; y++) {
            uint32_t *row = row_of(dst, w->l, w->t, y);
            for (int x = 0; x < ww * s; x++) row[x] = wbg;
        }
        draw_channels(dst, w->l, w->t, ww, wh, w->ctx);
    }
    g_s = 1;
    g_stride = SW;
}

void stage_compose(void) {
    if (!stage_px) stage_px = calloc(SW * SH, 4);
    build_lut();
    g_loop_tick++;
    compose(stage_px, 1);
}

/* het beeld voor het scherm op de schaal van het texture pack (na stage_compose); NULL als dat 1 is */
uint32_t *stage_compose_hd(void) {
    int s = texpack_scale();
    if (s <= 1) return NULL;
    if (!stage_hd_px) stage_hd_px = calloc((size_t)SW * SH * s * s, 4);
    compose(stage_hd_px, s);
    return stage_hd_px;
}

void bmp_write(const char *path, const uint32_t *px, int w, int hgt) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint8_t hdr[54] = {'B', 'M'};
    uint32_t size = 54 + (uint32_t)(w * hgt * 4);
    memcpy(hdr + 2, &size, 4);
    uint32_t v = 54; memcpy(hdr + 10, &v, 4);
    v = 40; memcpy(hdr + 14, &v, 4);
    int32_t ww = w, h = -hgt; memcpy(hdr + 18, &ww, 4); memcpy(hdr + 22, &h, 4);
    uint16_t planes = 1, bpp = 32; memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2);
    fwrite(hdr, 1, 54, f);
    fwrite(px, 4, (size_t)w * hgt, f);
    fclose(f);
}

/* bitmap-member naar 32-bit met het huidige palet (1-bit: zwart op wit), voor afdrukken */
uint32_t *stage_bitmap_argb(Bitmap *bm) {
    uint32_t *out = malloc(sizeof(uint32_t) * bm->w * bm->h);
    for (int i = 0; i < bm->w * bm->h; i++)
        out[i] = bm->bpp == 1 ? (bm->px[i] ? 0xff000000u : 0xffffffffu) : g_lut[bm->px[i]];
    return out;
}

void stage_screenshot(const char *path) { bmp_write(path, stage_px, SW, SH); }
