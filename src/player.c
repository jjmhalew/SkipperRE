/* Director 5-speler: films, frames, sprites, events en de builtins die speler-toestand nodig hebben.
 *
 * Eventvolgorde (D5): startMovie -> (frame 1 binnen) enterFrame ... exitFrame -> volgend frame.
 * Muis: primaire eventhandler (the mouseDownScript) -> spritescript -> castlidscript -> framescript
 * -> moviescript; `pass` geeft door, anders stopt het bij de eerste handler. */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

Player P;
Player *CP = &P;         /* huidige context: de stage of een MIAW */

void builtins_pure_register(void);
Datum vm_call_any(int name, Datum *a, int n);
void lingo_do(const char *s);
void globals_clear(void);
int chunk_count(const char *s, int kind);
void stage_text_dirty(Member *m);

#define ARG(i) ((i) < n ? a[i] : VOIDD)

/* ------------------------------------------------------------------ vensters (MIAW) */

static Window *win_find(const char *name, int create) {
    for (Window *w = P.windows; w; w = w->next)
        if (!_stricmp(w->name, name)) return w;
    if (!create) return NULL;
    Window *w = calloc(1, sizeof *w);
    snprintf(w->name, sizeof w->name, "%s", name);
    w->visible = 1;
    w->next = P.windows;
    P.windows = w;
    return w;
}

/* ------------------------------------------------------------------ films */
typedef struct MovieCache { char key[64]; Movie *mv; } MovieCache;
static MovieCache g_movies[64];
static int g_nmovies;

static void basename_noext(const char *p, char *out, int n) {
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '/' || *q == '\\' || *q == ':') b = q + 1;
    snprintf(out, n, "%s", b);
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
}

static Movie *movie_get(const char *name) {
    char key[64];
    basename_noext(name, key, sizeof key);
    for (int i = 0; i < g_nmovies; i++)
        if (!_stricmp(g_movies[i].key, key)) return g_movies[i].mv;
    static const char *exts[] = {".dxr", ".DXR", ".Dxr", ".dir", ".DIR", NULL};
    char path[260];
    Movie *mv = NULL;
    for (int i = 0; exts[i] && !mv; i++) {
        snprintf(path, sizeof path, "%s/%s%s", P.base_dir, key, exts[i]);
        mv = movie_load(path);
    }
    if (!mv) { vm_error("film niet gevonden: %s", name); return NULL; }
    if (g_nmovies < 64) {
        snprintf(g_movies[g_nmovies].key, 64, "%s", key);
        g_movies[g_nmovies++].mv = mv;
    }
    return mv;
}

Movie *vm_movie_for_handlers(void) { return CP->mv; }

/* ------------------------------------------------------------------ paletten */
static uint8_t g_mac_pal[256][3];

static void make_mac_palette(void) {
    static const uint8_t lv[6] = {255, 204, 153, 102, 51, 0};
    int i = 0;
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++, i++) {
                if (i >= 215) break;
                g_mac_pal[i][0] = lv[r]; g_mac_pal[i][1] = lv[g]; g_mac_pal[i][2] = lv[b];
            }
    static const uint8_t ramp[10] = {238, 221, 187, 170, 136, 119, 85, 68, 34, 17};
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 10; k++, i++) {
            uint8_t *p = g_mac_pal[i];
            p[0] = (c == 0 || c == 3) ? ramp[k] : 0;
            p[1] = (c == 1 || c == 3) ? ramp[k] : 0;
            p[2] = (c == 2 || c == 3) ? ramp[k] : 0;
        }
    g_mac_pal[255][0] = g_mac_pal[255][1] = g_mac_pal[255][2] = 0;
}

static int palette_lookup(Movie *mv, int lib, int num, uint8_t out[256][3]) {
    if (num <= 0) { memcpy(out, g_mac_pal, sizeof g_mac_pal); return 1; }
    CastLib *c;
    Member *m = movie_member(mv, lib ? lib : 1, num, &c);
    if (!m || !member_palette(c, m)) return 0;
    memcpy(out, m->pal, 768);
    return 1;
}

static void palette_set(const uint8_t pal[256][3], int fade_frames) {
    if (fade_frames > 0) {
        memcpy(P.pal_from, P.pal, 768);
        memcpy(P.pal_target, pal, 768);
        P.pal_fade_steps = P.pal_fade_left = fade_frames;
    } else {
        memcpy(P.pal, pal, 768);
        P.pal_fade_left = 0;
    }
    P.update_needed = 1;
}

void palette_step(void) {
    if (P.pal_fade_left <= 0) return;
    P.pal_fade_left--;
    double t = 1.0 - (double)P.pal_fade_left / P.pal_fade_steps;
    for (int i = 0; i < 256; i++)
        for (int k = 0; k < 3; k++)
            P.pal[i][k] = (uint8_t)(P.pal_from[i][k] + (P.pal_target[i][k] - P.pal_from[i][k]) * t);
    P.update_needed = 1;
}

/* ------------------------------------------------------------------ sprites */
static Channel *chan(int n) { return n >= 1 && n <= NCHAN ? &CP->ch[n] : NULL; }

static Member *chan_member(Channel *c, CastLib **cl) {
    if (!c || !c->member || !CP->mv) return NULL;
    return movie_member(CP->mv, c->lib, c->member, cl);
}

void sprite_rect(int ch, int *l, int *t, int *r, int *b) {
    Channel *c = chan(ch);
    *l = *t = *r = *b = 0;
    if (!c) return;
    CastLib *cl;
    Member *m = chan_member(c, &cl);
    int w = c->w, h = c->h, rx = 0, ry = 0;
    if (m && m->type == MT_BITMAP) {
        Bitmap *bm = member_bitmap(cl, m);
        if (bm) {
            if (!c->stretch) { w = bm->w; h = bm->h; }
            rx = bm->reg_x; ry = bm->reg_y;
            if (c->stretch && bm->w && bm->h) { rx = rx * w / bm->w; ry = ry * h / bm->h; }
        }
    } else if (m && (m->type == MT_TEXT || m->type == MT_BUTTON)) {
        Text *tx = member_text(cl, m);
        if (tx && !c->stretch) { w = tx->w; h = tx->h; }
    } else if (m && m->type == MT_FILMLOOP) {
        int fw = m->rect_r - m->rect_l, fh = m->rect_b - m->rect_t;
        if (!c->stretch) { w = fw; h = fh; }
        rx = w / 2; ry = h / 2;
    } else if (m && m->type == MT_SHAPE) {
        if (!c->stretch) { w = c->w; h = c->h; }
    }
    *l = c->loch - rx; *t = c->locv - ry;
    *r = *l + w; *b = *t + h;
}

int sprite_hit(int ch, int x, int y) {
    Channel *c = chan(ch);
    if (!c || !c->visible || !c->member) return 0;
    int l, t, r, b;
    sprite_rect(ch, &l, &t, &r, &b);
    if (x < l || x >= r || y < t || y >= b) return 0;
    /* matte/background transparent: pixel moet niet-transparant zijn */
    CastLib *cl;
    Member *m = chan_member(c, &cl);
    if (m && m->type == MT_BITMAP && (c->ink == 8 || c->ink == 36 || c->ink == 9)) {
        Bitmap *bm = member_bitmap(cl, m);
        if (bm && bm->w && bm->h && r > l && b > t) {
            int px = (x - l) * bm->w / (r - l), py = (y - t) * bm->h / (b - t);
            uint8_t v = bm->px[py * bm->w + px];
            if (v == (c->ink == 36 ? c->back : 0)) return 0;
        }
    }
    return 1;
}

static void chan_from_score(int n, const SprRec *r) {
    Channel *c = &CP->ch[n];
    c->type = r->type;
    c->lib = r->lib; c->member = r->member;
    c->ink = r->ink; c->fore = r->fore; c->back = r->back;
    c->loch = r->loch; c->locv = r->locv; c->w = r->w; c->h = r->h;
    c->stretch = r->stretch; c->trails = r->trails; c->blend = r->blend;
    c->slib = r->slib; c->script = r->smember;
}

static Frame *cur_frame(void) {
    Movie *mv = CP->mv;
    if (!mv || CP->frame < 1 || CP->frame > mv->score.nframes) return NULL;
    return &mv->score.f[CP->frame - 1];
}

/* ------------------------------------------------------------------ events */
static Script *member_script(int lib, int num) {
    CastLib *cl;
    Member *m = movie_member(CP->mv, lib ? lib : 1, num, &cl);
    return m ? m->script : NULL;
}

/* 1 = afgehandeld (niet doorgegeven) */
static int run_event(Script *sc, int ev, Datum *args, int n) {
    Handler *h = script_handler(sc, ev);
    if (!h) return 0;
    vm_pass = 0;
    Datum r = vm_call(sc, h, args, n);
    d_unref(r);
    if (vm_pass) { vm_pass = 0; return 0; }
    return 1;
}

static int movie_event(int ev) {
    Movie *mv = CP->mv;
    if (!mv) return 0;
    for (int l = 0; l < mv->nlibs; l++)
        for (int i = 0; i < mv->libs[l]->nscripts; i++) {
            Script *sc = mv->libs[l]->scripts[i];
            if (sc->type == 3 && script_handler(sc, ev)) return run_event(sc, ev, NULL, 0);
        }
    return 0;
}

static int frame_event(int ev) {
    Frame *fr = cur_frame();
    if (fr && fr->script) {
        Script *sc = member_script(fr->script_lib, fr->script);
        if (sc && run_event(sc, ev, NULL, 0)) return 1;
    }
    return movie_event(ev);
}

static int sprite_event(int ch, int ev) {
    Channel *c = chan(ch);
    if (c) {
        if (c->script) {
            Script *sc = member_script(c->slib, c->script);
            if (sc && run_event(sc, ev, NULL, 0)) return 1;
        }
        CastLib *cl;
        Member *m = chan_member(c, &cl);
        if (m && m->script && m->type != MT_SCRIPT && run_event(m->script, ev, NULL, 0)) return 1;
    }
    return frame_event(ev);
}

static int sprite_active(int ch) {
    Channel *c = chan(ch);
    if (!c || !c->member) return 0;
    if (c->script) return 1;
    CastLib *cl;
    Member *m = chan_member(c, &cl);
    return m && m->script != NULL;
}

static int sprite_under(int x, int y, int active_only) {
    for (int ch = NCHAN; ch >= 1; ch--)
        if (sprite_hit(ch, x, y) && (!active_only || sprite_active(ch))) return ch;
    return 0;
}

static int run_primary(Datum script) {
    /* geeft 1 terug als het event NIET verder mag */
    if (script.t != T_STR || !script.u.s->len) return 0;
    vm_dontpass = 0;
    lingo_do(script.u.s->s);
    int stop = vm_dontpass;
    vm_dontpass = 0;
    return stop;
}

/* ------------------------------------------------------------------ frames */
static void actor_step(void) {
    if (P.actor_list.t != T_LIST) return;
    List *l = P.actor_list.u.l;
    /* kopie: stepFrame mag de lijst aanpassen */
    int n = l->n;
    Datum *copy = malloc(sizeof(Datum) * (n + 1));
    for (int i = 0; i < n; i++) copy[i] = d_ref(l->v[i]);
    for (int i = 0; i < n; i++)
        if (copy[i].t == T_OBJ) {
            Script *sc;
            Handler *h = obj_handler(copy[i].u.o, sym("stepFrame"), &sc);
            if (h) d_unref(vm_call(sc, h, &copy[i], 1));
        }
    for (int i = 0; i < n; i++) d_unref(copy[i]);
    free(copy);
}

static int g_last_snd[3];

static void enter_frame(int first) {
    Movie *mv = CP->mv;
    Frame *fr = cur_frame();
    if (!fr) return;
    for (int i = 1; i <= NCHAN; i++) {
        if (!CP->ch[i].puppet) chan_from_score(i, &fr->spr[i - 1]);
        else {
            /* het sprite-script komt ook bij puppets uit de score (D5 kan het niet puppeten);
               Magnus frame 3 heeft het script per ongeluk op kanaal 10 i.p.v. 11 (de brievenbus) */
            CP->ch[i].slib = fr->spr[i - 1].slib;
            CP->ch[i].script = fr->spr[i - 1].smember;
        }
    }
    if (fr->tempo && fr->tempo <= 120) CP->tempo = fr->tempo;
    if (CP == &P) {
        /* paletkanaal */
        if (!P.pal_lib && fr->pal && (fr->pal != P.pal_num || first)) {
            uint8_t pal[256][3];
            if (palette_lookup(mv, fr->pal_lib, fr->pal, pal)) palette_set(pal, 0);
            P.pal_num = fr->pal;
        }
        /* geluidskanalen */
        int snd[3] = {0, fr->snd1, fr->snd2};
        int lib[3] = {0, fr->snd1_lib, fr->snd2_lib};
        for (int ch = 1; ch <= 2; ch++) {
            if (snd[ch] && snd[ch] != g_last_snd[ch]) {
                CastLib *cl;
                Member *m = movie_member(mv, lib[ch], snd[ch], &cl);
                if (m && m->type == MT_SOUND) sound_play_member(ch, cl, m);
            }
            g_last_snd[ch] = snd[ch];
        }
        if (fr->trans && !first) {
            CastLib *cl;
            Member *m = movie_member(mv, fr->trans_lib, fr->trans, &cl);
            if (m && m->speclen >= 4) {
                P.trans_pending = 1;
                P.trans_type = m->spec[3];
                P.trans_dur = m->spec[1] * 250 / 4;
            }
        }
    }
    actor_step();
    frame_event(sym("enterFrame"));
    CP->update_needed = 1;
}

static void movie_switch(Movie *mv, int frame) {
    if (CP->mv) movie_event(sym("stopMovie"));
    CP->mv = mv;
    for (int i = 1; i <= NCHAN; i++) {
        d_unref(CP->ch[i].cursor);
        memset(&CP->ch[i], 0, sizeof(Channel));
        CP->ch[i].visible = 1;   /* kanaalschakelaar: standaard aan, blijft staan over frames */
    }
    CP->frame = frame > 0 && frame <= mv->score.nframes ? frame : 1;
    CP->tempo = mv->tempo > 0 ? mv->tempo : 15;
    if (CP == &P) {
        P.pal_lib = 0;
        P.pal_num = 0;
        uint8_t pal[256][3];
        if (palette_lookup(mv, mv->def_pal_lib, mv->def_pal, pal)) palette_set(pal, 0);
        g_last_snd[1] = g_last_snd[2] = 0;
        P.stage_color = mv->stage_color;
    }
    Frame *fr = cur_frame();
    if (fr) for (int i = 1; i <= NCHAN; i++) chan_from_score(i, &fr->spr[i - 1]);
    movie_event(sym("startMovie"));
    enter_frame(1);
}

static int resolve_frame(Movie *mv, Datum d) {
    if (d.t == T_STR) { int f = movie_label(mv, d.u.s->s); return f ? f : -1; }
    if (d.t == T_SYM) return movie_label(mv, symname(d.u.i));
    return d_toint(d);
}

void player_start(const char *name) {
    Movie *mv = movie_get(name);
    if (!mv) { P.halted = 1; return; }
    CP = &P;
    movie_switch(mv, 1);
}

static void apply_go(void) {
    if (CP->pending_movie[0]) {
        Movie *mv = movie_get(CP->pending_movie);
        CP->pending_movie[0] = 0;
        if (mv) {
            int f = 1;
            if (CP->pending_label[0]) { f = movie_label(mv, CP->pending_label); if (!f) f = 1; }
            else if (CP->pending_frame > 0) f = CP->pending_frame;
            CP->pending_label[0] = 0;
            CP->going = 0;
            movie_switch(mv, f);
            return;
        }
    }
    int next = CP->going ? CP->next_frame : CP->frame + 1;
    CP->going = 0;
    if (!CP->mv) return;
    if (next < 1) next = 1;
    if (next > CP->mv->score.nframes) next = CP->mv->score.nframes;  /* aan het eind blijven staan */
    int changed = next != CP->frame;
    CP->frame = next;
    (void)changed;
    enter_frame(0);
}

static void ctx_tick(void) {
    frame_event(sym("exitFrame"));
    if (vm_abort) vm_abort = 0;
    apply_go();
    if (vm_abort) vm_abort = 0;
}

int player_tick(void) {
    if (P.halted || !P.mv) return 100;
    CP = &P;
    palette_step();
    if (!P.paused) ctx_tick();
    for (Window *w = P.windows; w; w = w->next)
        if (w->open && w->ctx && w->ctx->mv && !w->ctx->paused) {
            CP = w->ctx;
            ctx_tick();
            CP = &P;
        }
    int tempo = P.tempo > 0 ? P.tempo : 15;
    return 1000 / tempo;
}

void player_idle(void) {
    if (P.halted || !P.mv) return;
    CP = &P;
    frame_event(sym("idle"));
    vm_abort = 0;
}

/* ------------------------------------------------------------------ invoer */
static Window *top_window_at(int x, int y) {
    for (Window *w = P.windows; w; w = w->next)
        if (w->open && x >= w->l && x < w->r && y >= w->t && y < w->b) return w;
    return NULL;
}

static Window *modal_window(void) {
    for (Window *w = P.windows; w; w = w->next)
        if (w->open && w->modal) return w;
    return NULL;
}

/* moveableSprite: Director sleept de sprite zelf zolang de knop ingedrukt is */
static Player *g_drag_ctx;
static int g_drag_ch, g_drag_dx, g_drag_dy;
static Player *g_focus_ctx;   /* editable veld met toetsenbordfocus, zie player_key */
static int g_focus_ch;
static int field_editable(Player *ctx, int ch);

int player_drag_update(void) {
    if (!g_drag_ch) return 0;
    Channel *c = &g_drag_ctx->ch[g_drag_ch];
    int nh = P.mouse_x + g_drag_dx, nv = P.mouse_y + g_drag_dy;
    if (c->loch == nh && c->locv == nv) return 0;
    c->loch = nh; c->locv = nv;
    return 1;
}

void player_mouse(int x, int y, int down, int up, int right) {
    P.mouse_x = x; P.mouse_y = y;
    if (!down && !up) {
        player_drag_update();
        return;
    }
    if (up && !right && g_drag_ch) {
        player_drag_update();
        g_drag_ch = 0;
    }
    Window *w = top_window_at(x, y);
    Window *mw = modal_window();
    if (vm_trace)
        for (Window *k = P.windows; k; k = k->next)
            fprintf(stderr, "[muis %d,%d] venster %s open %d modal %d rect %d,%d-%d,%d %s\n", x, y, k->name, k->open,
                    k->modal, k->l, k->t, k->r, k->b, k == w ? "(geraakt)" : "");
    if (mw && w != mw) return;
    Player *save = CP;
    int lx = x, ly = y;
    if (w) { CP = w->ctx; lx = x - w->l; ly = y - w->t; }
    else CP = &P;
    int ev;
    if (down) {
        ev = sym(right ? "rightMouseDown" : "mouseDown");
        int ch = sprite_under(lx, ly, 1);
        P.click_on = ch;
        P.last_click = (int)now_ms();
        int top = sprite_under(lx, ly, 0);
        if (!right && top && field_editable(CP, top)) { g_focus_ctx = CP; g_focus_ch = top; }
        if (!right && top && CP->ch[top].moveable) {
            g_drag_ctx = CP;
            g_drag_ch = top;
            g_drag_dx = CP->ch[top].loch - x;   /* loc is lokaal, muis globaal */
            g_drag_dy = CP->ch[top].locv - y;
        }
        if (!right && run_primary(CP->mouse_down_script)) { CP = save; return; }
        if (ch) sprite_event(ch, ev); else frame_event(ev);
    } else {
        ev = sym(right ? "rightMouseUp" : "mouseUp");
        int ch = sprite_under(lx, ly, 1);
        if (!right && run_primary(CP->mouse_up_script)) { CP = save; return; }
        if (ch) sprite_event(ch, ev); else frame_event(ev);
    }
    vm_abort = 0;
    CP = save;
}

/* Editable velden (D5: textFlags bit 0 in de field-spec, byte 25). Toetsen die geen handler afvangt
 * (of die `pass` doet) komen in het veld met focus: het laatst aangeklikte, anders het eerste op het
 * podium. De cursor staat altijd aan het eind (de spellen hebben alleen korte naamvelden). */

static int field_editable(Player *ctx, int ch) {
    Channel *c = &ctx->ch[ch];
    if (!c->member || !c->visible) return 0;
    CastLib *cl;
    Member *m = movie_member(ctx->mv, c->lib ? c->lib : 1, c->member, &cl);
    return m && m->type == MT_TEXT && m->speclen > 25 && (m->spec[25] & 1);
}

int player_focus_field(void) {
    if (g_focus_ctx == CP && g_focus_ch && field_editable(CP, g_focus_ch)) return g_focus_ch;
    for (int ch = 1; ch <= NCHAN; ch++)
        if (field_editable(CP, ch)) return ch;
    return 0;
}

static void field_type(int fch, int code, int c) {
    Channel *chn = &CP->ch[fch];
    CastLib *cl;
    Member *m = movie_member(CP->mv, chn->lib ? chn->lib : 1, chn->member, &cl);
    Text *t = m ? member_text(cl, m) : NULL;
    if (!t) return;
    size_t n = t->text ? strlen(t->text) : 0;
    if (code == 51 || c == 8) {                       /* backspace */
        if (n) t->text[n - 1] = 0;
    } else if (code == 117) {                         /* delete: niets rechts van de cursor */
        return;
    } else if (c == 13 || (unsigned char)c >= 32) {
        if (n >= 255) return;
        char *nt = malloc(n + 2);
        if (n) memcpy(nt, t->text, n);
        nt[n] = (char)c; nt[n + 1] = 0;
        free(t->text);
        t->text = nt;
    } else return;
    t->dirty = 1;
    P.update_needed = 1;
}

void player_key(int code, int ch, int down) {
    P.key_code = code;
    P.key[0] = (char)ch;
    P.key[1] = 0;
    Window *mw = modal_window();
    Player *save = CP;
    CP = mw ? mw->ctx : &P;
    if (vm_trace) {
        char b1[64], b2[64];
        fprintf(stderr, "[toets %d '%c' %s] %s keyDownScript \"%s\" keyUpScript \"%s\"\n", code, ch > 31 ? ch : '?',
                down ? "neer" : "op", mw ? mw->name : "stage", d_tostr(CP->key_down_script, b1, sizeof b1),
                d_tostr(CP->key_up_script, b2, sizeof b2));
    }
    if (down) {
        if (!run_primary(CP->key_down_script)) {
            int fch = player_focus_field();
            int handled = fch ? sprite_event(fch, sym("keyDown")) : frame_event(sym("keyDown"));
            if (!handled && fch) field_type(fch, code, ch);
        }
    } else {
        if (!run_primary(CP->key_up_script)) {
            int fch = player_focus_field();   /* keyUp gaat ook eerst naar het veld (mmdlg3: sprite-script) */
            if (fch) sprite_event(fch, sym("keyUp")); else frame_event(sym("keyUp"));
        }
    }
    vm_abort = 0;
    CP = save;
}

/* ------------------------------------------------------------------ the-entities */
static Datum str_of(const char *s) { return d_str(s); }

Datum player_the(int name) {
    const char *n = symname(name);
    uint32_t ms = now_ms();
    if (!_stricmp(n, "ticks")) return d_int((int)((ms - P.start_ms) * 60ull / 1000));
    if (!_stricmp(n, "milliseconds")) return d_int((int)(ms - P.start_ms));
    if (!_stricmp(n, "frame")) return d_int(CP->frame);
    if (!_stricmp(n, "clickOn")) return d_int(P.click_on);
    if (!_stricmp(n, "keyCode")) return d_int(P.key_code);
    if (!_stricmp(n, "key")) return str_of(P.key);
    if (!_stricmp(n, "pathName") || !_stricmp(n, "moviePath")) {
        char buf[300];
        snprintf(buf, sizeof buf, "%s\\", P.base_dir);
        return str_of(buf);
    }
    if (!_stricmp(n, "movieName") || !_stricmp(n, "movie")) return str_of(CP->mv ? CP->mv->name : "");
    if (!_stricmp(n, "stillDown") || !_stricmp(n, "mouseDown")) {
        host_pump();
        return d_int(P.mouse_down && !P.release_pending);
    }
    if (!_stricmp(n, "mouseUp")) return d_int(!P.mouse_down);
    if (!_stricmp(n, "mouseH")) { host_pump(); return d_int(P.mouse_x); }
    if (!_stricmp(n, "mouseV")) return d_int(P.mouse_y);
    if (!_stricmp(n, "clickLoc")) return d_point(P.mouse_x, P.mouse_y);
    if (!_stricmp(n, "doubleClick")) return d_int(0);
    if (!_stricmp(n, "result")) return VOIDD;
    if (!_stricmp(n, "stageLeft") || !_stricmp(n, "stageTop")) return d_int(0);
    if (!_stricmp(n, "stageRight")) return d_int(640);
    if (!_stricmp(n, "stageBottom")) return d_int(480);
    if (!_stricmp(n, "colorDepth")) return d_int(8);
    if (!_stricmp(n, "lastFrame")) return d_int(CP->mv ? CP->mv->score.nframes : 0);
    if (!_stricmp(n, "mouseCast") || !_stricmp(n, "mouseMember")) {
        int ch = sprite_under(P.mouse_x, P.mouse_y, 0);
        return d_int(ch ? CP->ch[ch].member : -1);
    }
    if (!_stricmp(n, "rollOver")) return d_int(sprite_under(P.mouse_x, P.mouse_y, 0));
    if (!_stricmp(n, "platform")) return str_of("Windows,32");
    if (!_stricmp(n, "machineType")) return d_int(256);
    if (!_stricmp(n, "maxInteger")) return d_int(0x7fffffff);
    if (!_stricmp(n, "paramCount")) return d_int(0);
    vm_error("the %s: onbekend", n);
    return VOIDD;
}

/* get/set: type 0 movie, 1 aantal chunks, 4 sound, 6 sprite, 7 animatie, 8 animatie2, 9/10 member */
static Datum sprite_get(int ch, int id);
static void sprite_set(int ch, int id, Datum v);
static Datum member_get(Datum mem, int name);
static void member_set(Datum mem, int name, Datum v);
static Datum mkmember(Datum id, Datum lib);

static int S(const char *s) { return sym(s); }
static const char *MEMBER_PROPS[] = {NULL, "name", "text", "textStyle", "textFont", "textHeight", "textAlign",
                                     "textSize", "picture", "hilite", "number", "size", "loop", "duration",
                                     "controller", "directToStage", "sound", "foreColor", "backColor", "type"};

Datum player_get(int type, int id, Datum *tg, int nt) {
    switch (type) {
    case 0:
        if (id == 1) return d_ref(CP->mouse_down_script);
        if (id == 2) return d_ref(CP->mouse_up_script);
        if (id == 3) return d_ref(CP->key_down_script);
        if (id == 4) return d_ref(CP->key_up_script);
        if (id > 11 && nt) {
            /* the last char/word/item/line in x */
            Str *s = d_asstr(tg[0]);
            int kind = id - 11, cnt = chunk_count(s->s, kind);
            (void)cnt;
            if (--s->rc == 0) free(s);
            return VOIDD;
        }
        return VOIDD;
    case 1: {
        Str *s = d_asstr(tg[0]);
        int c = chunk_count(s->s, id);
        if (--s->rc == 0) free(s);
        return d_int(c);
    }
    case 4: return d_int(255);
    case 6: return sprite_get(d_toint(tg[0]), id);
    case 7:
        switch (id) {
        case 12: return d_int(0);
        case 13: return d_str(P.key);
        case 14: return d_int((int)((now_ms() - P.last_click) * 60ull / 1000));
        case 16: return d_int(P.key_code);
        case 25: return d_int(1);
        case 26: return d_int(P.sound_level);
        case 27: return d_int(P.stage_color);
        case 34: return d_int((int)((now_ms() - P.timer_base) * 60ull / 1000));
        case 8: return d_int(P.exit_lock);
        case 6: return d_int(8);
        case 36: return d_int(1);
        }
        return d_int(0);
    case 8:
        if (id == 2) {
            int lib = nt ? d_toint(tg[0]) : 1;
            if (!CP->mv || lib < 1 || lib > CP->mv->nlibs) return d_int(0);
            CastLib *c = CP->mv->libs[lib - 1];
            return d_int(c->n + c->first - 1);
        }
        if (id == 4) return d_int(CP->mv ? CP->mv->nlibs : 0);
        return d_int(0);
    case 9: case 10: case 11: {
        Datum mem = mkmember(tg[0], tg[1]);
        int nm = id >= 1 && id <= 19 ? S(MEMBER_PROPS[id]) : S("?");
        Datum r = member_get(mem, nm);
        return r;
    }
    }
    return VOIDD;
}

void player_set(int type, int id, Datum *tg, int nt, Datum v) {
    switch (type) {
    case 0:
        if (id >= 1 && id <= 4) {
            Datum *slot = id == 1 ? &CP->mouse_down_script : id == 2 ? &CP->mouse_up_script
                        : id == 3 ? &CP->key_down_script : &CP->key_up_script;
            d_unref(*slot);
            *slot = v;
            return;
        }
        break;
    case 4: break;
    case 6: sprite_set(d_toint(tg[0]), id, v); return;   /* sprite_set neemt v over */
    case 7:
        if (id == 26) { P.sound_level = d_toint(v); sound_set_level(P.sound_level); }
        else if (id == 8) P.exit_lock = d_toint(v);
        else if (id == 27) P.stage_color = d_toint(v);
        break;
    case 9: case 10: case 11: {
        Datum mem = mkmember(tg[0], tg[1]);
        int nm = id >= 1 && id <= 19 ? S(MEMBER_PROPS[id]) : S("?");
        member_set(mem, nm, v);
        return;
    }
    }
    d_unref(v);
}

/* ------------------------------------------------------------------ sprite-properties */
static int castnum_of(Channel *c) { return c->member ? ((c->lib > 1 ? c->lib - 1 : 0) << 16 | c->member) : 0; }

static void set_member(Channel *c, Datum v) {
    if (v.t == T_MEMBER) { c->lib = v.u.i >> 16; c->member = v.u.i & 0xffff; if (!c->lib) c->lib = 1; }
    else if (v.t == T_STR) {
        int r = movie_find_member(CP->mv, v.u.s->s, 0);
        c->lib = r >> 16; c->member = r & 0xffff;
    } else {
        int k = d_toint(v);
        c->lib = (k >> 16) + 1; c->member = k & 0xffff;
    }
}

static Datum sprite_get(int ch, int id) {
    Channel *c = chan(ch);
    if (!c) return VOIDD;
    int l, t, r, b;
    switch (id) {
    case 1: return d_int(c->type);
    case 2: return d_int(c->back);
    case 3: sprite_rect(ch, &l, &t, &r, &b); return d_int(b);
    case 4: return d_int(castnum_of(c));
    case 6: return d_ref(c->cursor);
    case 7: return d_int(c->fore);
    case 8: sprite_rect(ch, &l, &t, &r, &b); return d_int(b - t);
    case 10: return d_int(c->ink);
    case 11: sprite_rect(ch, &l, &t, &r, &b); return d_int(l);
    case 13: return d_int(c->loch);
    case 14: return d_int(c->locv);
    case 18: return d_int(c->puppet);
    case 19: sprite_rect(ch, &l, &t, &r, &b); return d_int(r);
    case 22: return d_int(c->stretch);
    case 23: sprite_rect(ch, &l, &t, &r, &b); return d_int(t);
    case 24: return d_int(c->trails);
    case 25: return d_int(c->visible);
    case 27: sprite_rect(ch, &l, &t, &r, &b); return d_int(r - l);
    case 28: return d_int(c->blend);
    case 29: return d_int(c->script);
    case 30: return d_int(c->moveable);
    case 33: return d_point(c->loch, c->locv);
    case 34: sprite_rect(ch, &l, &t, &r, &b); return d_rect(l, t, r, b);
    case 35: return d_int(c->member);
    case 36: return d_int(c->lib);
    case 37: return d_member(c->lib ? c->lib : 1, c->member);
    }
    return d_int(0);
}

static void sprite_set(int ch, int id, Datum v) {
    Channel *c = chan(ch);
    if (!c) { d_unref(v); return; }
    int l, t, r, b;
    switch (id) {
    case 2: c->back = d_toint(v); break;
    case 4: case 37: case 35: set_member(c, v); break;
    case 6: d_unref(c->cursor); c->cursor = d_ref(v); break;
    case 7: c->fore = d_toint(v); break;
    case 8: c->h = d_toint(v); c->stretch = 1; break;
    case 10: c->ink = d_toint(v); break;
    case 13: c->loch = d_toint(v); break;
    case 14: c->locv = d_toint(v); break;
    case 18: c->puppet = d_toint(v); break;
    case 22:
        c->stretch = d_toint(v);
        if (c->stretch) { sprite_rect(ch, &l, &t, &r, &b); c->w = r - l; c->h = b - t; }
        break;
    case 24: c->trails = d_toint(v); break;
    case 25: c->visible = d_truthy(v); break;
    case 27: c->w = d_toint(v); c->stretch = 1; break;
    case 28: c->blend = d_toint(v); break;
    case 29: c->script = d_toint(v); c->slib = 1; break;
    case 30: c->moveable = d_toint(v); break;
    case 33:
        if (v.t == T_POINT && v.u.l->n >= 2) { c->loch = d_toint(v.u.l->v[0]); c->locv = d_toint(v.u.l->v[1]); }
        break;
    case 34:
        if (v.t == T_RECT && v.u.l->n >= 4) {
            int nl = d_toint(v.u.l->v[0]), nt = d_toint(v.u.l->v[1]), nr = d_toint(v.u.l->v[2]), nb = d_toint(v.u.l->v[3]);
            sprite_rect(ch, &l, &t, &r, &b);
            c->loch += nl - l; c->locv += nt - t;
            c->w = nr - nl; c->h = nb - nt;
            c->stretch = 1;
        }
        break;
    }
    CP->update_needed = 1;
    d_unref(v);
}

/* ------------------------------------------------------------------ members */
static int lib_of(Datum lib) {
    if (lib.t == T_VOID) return 0;
    if (lib.t == T_CASTLIB) return lib.u.i;
    if (lib.t == T_STR && CP->mv) {
        for (int i = 0; i < CP->mv->nlibs; i++)
            if (!_stricmp(CP->mv->libs[i]->name, lib.u.s->s)) return i + 1;
        return -1;
    }
    return d_toint(lib);
}

static Datum mkmember(Datum id, Datum lib) {
    if (id.t == T_MEMBER) return id;
    int l = lib_of(lib);
    if (l < 0) return d_member(0, 0);
    if (id.t == T_STR) {
        int r = CP->mv ? movie_find_member(CP->mv, id.u.s->s, l) : 0;
        if (!r) vm_error("member niet gevonden: \"%s\"", id.u.s->s);
        return r ? d_member(r >> 16, r & 0xffff) : d_member(0, 0);
    }
    int k = d_toint(id);
    if (l == 0) { l = (k >> 16) + 1; k &= 0xffff; }
    return d_member(l, k);
}

static Member *member_of(Datum mem, CastLib **cl) {
    if (mem.t != T_MEMBER || !CP->mv) return NULL;
    return movie_member(CP->mv, mem.u.i >> 16, mem.u.i & 0xffff, cl);
}

static const char *type_names[] = {"empty", "bitmap", "filmLoop", "field", "palette", "picture", "sound",
                                   "button", "shape", "movie", "digitalVideo", "script", "richText"};

static Datum member_get(Datum mem, int name) {
    CastLib *cl;
    Member *m = member_of(mem, &cl);
    const char *n = symname(name);
    if (!_stricmp(n, "number")) return m ? d_int(((mem.u.i >> 16) - 1) << 16 | (mem.u.i & 0xffff)) : d_int(-1);
    if (!m) {
        if (!_stricmp(n, "name") || !_stricmp(n, "text")) return d_str("");
        return d_int(0);
    }
    if (!_stricmp(n, "name")) return d_str(m->name);
    if (!_stricmp(n, "text")) { Text *t = member_text(cl, m); return d_str(t && t->text ? t->text : ""); }
    if (!_stricmp(n, "loaded")) return d_int(1);
    if (!_stricmp(n, "type") || !_stricmp(n, "castType"))
        return d_sym(sym(m->type >= 0 && m->type <= 12 ? type_names[m->type] : "empty"));
    if (!_stricmp(n, "scriptText")) return d_str(m->script ? " " : "");
    if (!_stricmp(n, "rect") || !_stricmp(n, "width") || !_stricmp(n, "height") || !_stricmp(n, "regPoint")) {
        int w = 0, h = 0, rx = 0, ry = 0;
        if (m->type == MT_BITMAP) { Bitmap *b = member_bitmap(cl, m); if (b) w = b->w, h = b->h, rx = b->reg_x, ry = b->reg_y; }
        else if (m->type == MT_TEXT || m->type == MT_BUTTON) { Text *t = member_text(cl, m); if (t) w = t->w, h = t->h; }
        else { w = m->rect_r - m->rect_l; h = m->rect_b - m->rect_t; }
        if (!_stricmp(n, "rect")) return d_rect(0, 0, w, h);
        if (!_stricmp(n, "width")) return d_int(w);
        if (!_stricmp(n, "height")) return d_int(h);
        return d_point(rx, ry);
    }
    if (!_stricmp(n, "duration")) {
        Sound *s = m->type == MT_SOUND ? member_sound(cl, m) : NULL;
        return d_int(s && s->rate ? s->frames * 60 / s->rate : 0);
    }
    if (!_stricmp(n, "fileName")) return d_str("");
    if (!_stricmp(n, "foreColor")) { Text *t = member_text(cl, m); return d_int(t ? t->fore & 0xff : 255); }
    if (!_stricmp(n, "backColor")) return d_int(0);
    if (!_stricmp(n, "textSize")) { Text *t = member_text(cl, m); return d_int(t ? t->font_size : 12); }
    if (!_stricmp(n, "textFont")) { Text *t = member_text(cl, m); return d_str(t ? t->font : ""); }
    if (!_stricmp(n, "hilite")) return d_int(0);
    return d_int(0);
}

static void member_set(Datum mem, int name, Datum v) {
    CastLib *cl;
    Member *m = member_of(mem, &cl);
    const char *n = symname(name);
    if (!m) { d_unref(v); return; }
    if (!_stricmp(n, "text")) {
        Text *t = member_text(cl, m);
        if (t) {
            Str *s = d_asstr(v);
            free(t->text);
            t->text = strdup(s->s);
            if (--s->rc == 0) free(s);
            t->dirty = 1;
        }
    } else if (!_stricmp(n, "name")) {
        Str *s = d_asstr(v);
        free(m->name);
        m->name = strdup(s->s);
        if (--s->rc == 0) free(s);
    } else if (!_stricmp(n, "regPoint")) {
        Bitmap *b = m->type == MT_BITMAP ? member_bitmap(cl, m) : NULL;
        if (b && v.t == T_POINT && v.u.l->n >= 2) { b->reg_x = d_toint(v.u.l->v[0]); b->reg_y = d_toint(v.u.l->v[1]); }
    } else if (!_stricmp(n, "foreColor")) {
        Text *t = member_text(cl, m);
        if (t) { t->fore = d_toint(v); t->dirty = 1; }
    } else if (!_stricmp(n, "textSize")) {
        Text *t = member_text(cl, m);
        if (t) { t->font_size = d_toint(v); t->dirty = 1; }
    } else if (!_stricmp(n, "textStyle")) {
        Text *t = member_text(cl, m);
        Str *s = d_asstr(v);
        if (t) { t->style = strstr(s->s, "bold") ? 1 : 0; t->dirty = 1; }
        if (--s->rc == 0) free(s);
    } else if (!_stricmp(n, "textFont")) {
        Text *t = member_text(cl, m);
        Str *s = d_asstr(v);
        if (t) { snprintf(t->font, sizeof t->font, "%s", s->s); t->dirty = 1; }
        if (--s->rc == 0) free(s);
    } else if (!_stricmp(n, "textAlign")) {
        Text *t = member_text(cl, m);
        Str *s = d_asstr(v);
        if (t) { t->align = !_stricmp(s->s, "center") ? 1 : !_stricmp(s->s, "right") ? -1 : 0; t->dirty = 1; }
        if (--s->rc == 0) free(s);
    } else if (!_stricmp(n, "rect")) {
        Text *t = member_text(cl, m);
        if (t && v.t == T_RECT && v.u.l->n >= 4) {
            t->w = d_toint(v.u.l->v[2]) - d_toint(v.u.l->v[0]);
            t->h = d_toint(v.u.l->v[3]) - d_toint(v.u.l->v[1]);
            t->dirty = 1;
        }
    }
    CP->update_needed = 1;
    d_unref(v);
}

Datum player_field(Datum id, Datum lib) {
    Datum mem = mkmember(id, lib);
    return member_get(mem, sym("text"));
}

void player_set_field(Datum id, Datum lib, Datum v) {
    Datum mem = mkmember(id, lib);
    member_set(mem, sym("text"), d_ref(v));
}

/* ------------------------------------------------------------------ objectproperties */
static int rect_idx(const char *n) {
    if (!_stricmp(n, "left") || !_stricmp(n, "locH") || !_stricmp(n, "x")) return 0;
    if (!_stricmp(n, "top") || !_stricmp(n, "locV") || !_stricmp(n, "y")) return 1;
    if (!_stricmp(n, "right")) return 2;
    if (!_stricmp(n, "bottom")) return 3;
    return -1;
}

Datum player_objprop(Datum o, int name) {
    const char *n = symname(name);
    switch (o.t) {
    case T_OBJ: {
        int f;
        Datum v = obj_getprop(o.u.o, name, &f);
        if (!f) vm_error("property %s niet gevonden", n);
        return d_ref(v);
    }
    case T_PLIST: {
        for (int i = 0; i + 1 < o.u.l->n; i += 2)
            if (o.u.l->v[i].t == T_SYM && o.u.l->v[i].u.i == name) return d_ref(o.u.l->v[i + 1]);
        return VOIDD;
    }
    case T_RECT: case T_POINT: case T_LIST: {
        if (!_stricmp(n, "width") && o.u.l->n >= 4) return d_int(d_toint(o.u.l->v[2]) - d_toint(o.u.l->v[0]));
        if (!_stricmp(n, "height") && o.u.l->n >= 4) return d_int(d_toint(o.u.l->v[3]) - d_toint(o.u.l->v[1]));
        int i = rect_idx(n);
        return i >= 0 && i < o.u.l->n ? d_ref(o.u.l->v[i]) : VOIDD;
    }
    case T_MEMBER: return member_get(o, name);
    case T_WINDOW: {
        Window *w = o.u.w;
        if (!_stricmp(n, "rect")) {
            if (w->r <= w->l && w->file[0]) {
                Movie *mv = movie_get(w->file);
                if (mv) return d_rect(0, 0, mv->stage_w, mv->stage_h);
            }
            return d_rect(w->l, w->t, w->r, w->b);
        }
        if (!_stricmp(n, "visible")) return d_int(w->visible);
        if (!_stricmp(n, "name")) return d_str(w->name);
        if (!_stricmp(n, "fileName")) return d_str(w->file);
        return VOIDD;
    }
    default:
        if (o.t == T_STR || o.t == T_INT) return member_get(mkmember(o, VOIDD), name);
        return VOIDD;
    }
}

static void window_open(Window *w);

void player_set_objprop(Datum o, int name, Datum v) {
    const char *n = symname(name);
    switch (o.t) {
    case T_OBJ: obj_setprop(o.u.o, name, v); return;
    case T_PLIST: {
        for (int i = 0; i + 1 < o.u.l->n; i += 2)
            if (o.u.l->v[i].t == T_SYM && o.u.l->v[i].u.i == name) { d_unref(o.u.l->v[i + 1]); o.u.l->v[i + 1] = v; return; }
        list_push(o.u.l, d_sym(name));
        list_push(o.u.l, v);
        return;
    }
    case T_RECT: case T_POINT: case T_LIST: {
        int i = rect_idx(n);
        if (i >= 0 && i < o.u.l->n) { d_unref(o.u.l->v[i]); o.u.l->v[i] = v; return; }
        break;
    }
    case T_MEMBER: member_set(o, name, v); return;
    case T_WINDOW: {
        Window *w = o.u.w;
        if (!_stricmp(n, "rect") && v.t == T_RECT && v.u.l->n >= 4) {
            w->l = d_toint(v.u.l->v[0]); w->t = d_toint(v.u.l->v[1]);
            w->r = d_toint(v.u.l->v[2]); w->b = d_toint(v.u.l->v[3]);
        } else if (!_stricmp(n, "fileName")) {
            Str *s = d_asstr(v);
            snprintf(w->file, sizeof w->file, "%s", s->s);
            if (--s->rc == 0) free(s);
        } else if (!_stricmp(n, "visible")) w->visible = d_truthy(v);
        else if (!_stricmp(n, "modal")) w->modal = d_truthy(v);
        else if (!_stricmp(n, "windowType")) w->type = d_toint(v);
        break;
    }
    default:
        if (o.t == T_STR || o.t == T_INT) { member_set(mkmember(o, VOIDD), name, v); return; }
    }
    d_unref(v);
}

Datum player_movie_prop(int name) {
    const char *n = symname(name);
    if (!_stricmp(n, "actorList")) {
        if (P.actor_list.t != T_LIST) P.actor_list = d_list(T_LIST, 4);
        return d_ref(P.actor_list);
    }
    if (!_stricmp(n, "stage")) { Datum d = {T_WINDOW}; d.u.w = NULL; return d; }
    if (!_stricmp(n, "framePalette")) return d_int(P.pal_num);
    if (!_stricmp(n, "searchPath")) return d_list(T_LIST, 1);
    return player_the(name);
}

void player_set_movie_prop(int name, Datum v) {
    const char *n = symname(name);
    if (!_stricmp(n, "actorList")) { d_unref(P.actor_list); P.actor_list = v; return; }
    d_unref(v);
}

int player_sprite_intersects(int a, int b, int within) {
    int l1, t1, r1, b1, l2, t2, r2, b2;
    sprite_rect(a, &l1, &t1, &r1, &b1);
    sprite_rect(b, &l2, &t2, &r2, &b2);
    if (within) return l1 >= l2 && t1 >= t2 && r1 <= r2 && b1 <= b2;
    return l1 < r2 && l2 < r1 && t1 < b2 && t2 < b1;
}

/* tell window x / tell the stage */
static Player *g_tell_stack[8];
static int g_ntell;

void player_tell_begin(Datum win) {
    if (g_ntell < 8) g_tell_stack[g_ntell++] = CP;
    if (win.t == T_WINDOW && win.u.w && win.u.w->ctx) CP = win.u.w->ctx;
    else CP = &P;
}

void player_tell_end(void) {
    if (g_ntell > 0) CP = g_tell_stack[--g_ntell];
}

/* ------------------------------------------------------------------ builtins */
static Datum bi_go(Datum *a, int n) {
    if (n >= 2 && ARG(1).t != T_VOID) {
        Str *s = d_asstr(ARG(1));
        snprintf(CP->pending_movie, sizeof CP->pending_movie, "%s", s->s);
        if (--s->rc == 0) free(s);
        CP->pending_label[0] = 0;
        CP->pending_frame = 1;
        if (ARG(0).t == T_STR) snprintf(CP->pending_label, 64, "%s", ARG(0).u.s->s);
        else CP->pending_frame = d_toint(ARG(0));
        return VOIDD;
    }
    if (!CP->mv) return VOIDD;
    int f = resolve_frame(CP->mv, ARG(0));
    if (f < 0) { vm_error("label niet gevonden"); return VOIDD; }
    CP->next_frame = f;
    CP->going = 1;
    return VOIDD;
}

static Datum bi_updateStage(Datum *a, int n) { (void)a; (void)n; player_update_stage(); return VOIDD; }

static Datum bi_puppetSprite(Datum *a, int n) {
    Channel *c = chan(d_toint(ARG(0)));
    if (c) c->puppet = d_truthy(ARG(1));
    return VOIDD;
}

static Datum bi_member(Datum *a, int n) { return mkmember(ARG(0), ARG(1)); }

static Datum bi_script(Datum *a, int n) {
    Datum m = mkmember(ARG(0), ARG(1));
    CastLib *cl;
    Member *mm = member_of(m, &cl);
    if (!mm || !mm->script) { vm_error("script niet gevonden"); return VOIDD; }
    Datum d = {T_SCRIPT};
    d.u.sc = mm->script;
    return d;
}

static Datum bi_castLib(Datum *a, int n) {
    Datum d = {T_CASTLIB};
    d.u.i = lib_of(ARG(0));
    return d;
}

static Datum bi_sprite(Datum *a, int n) { return d_int(d_toint(ARG(0))); }

static Datum bi_birth(Datum *a, int n) {
    Datum s = ARG(0);
    if (s.t == T_SCRIPT) return obj_new(s.u.sc, a + 1, n - 1);
    if (s.t == T_XOBJ) return xobj_call(s.u.x, a, n);
    if (s.t == T_SYM) return VOIDD;   /* new(#field, castLib x): niet nodig */
    vm_error("birth/new op geen script");
    return VOIDD;
}

static Datum bi_puppetSound(Datum *a, int n) {
    int ch = 1;
    Datum x = ARG(0);
    if (n >= 2) { ch = d_toint(ARG(0)); x = ARG(1); }
    if (x.t == T_INT && d_toint(x) == 0) { sound_stop(ch); return VOIDD; }
    if (x.t == T_VOID) { sound_stop(ch); return VOIDD; }
    Datum m = mkmember(x, VOIDD);
    CastLib *cl;
    Member *mm = member_of(m, &cl);
    if (mm && mm->type == MT_SOUND) sound_play_member(ch, cl, mm);
    return VOIDD;
}

static Datum bi_soundBusy(Datum *a, int n) { return d_int(sound_busy(d_toint(ARG(0)))); }

static Datum bi_sound(Datum *a, int n) {
    Datum cmd = ARG(0);
    if (cmd.t != T_SYM) return VOIDD;
    const char *c = symname(cmd.u.i);
    if (!_stricmp(c, "stop") || !_stricmp(c, "fadeOut")) sound_stop(d_toint(ARG(1)));
    else if (!_stricmp(c, "playFile")) {
        char msg[300];
        vm_error("sound playFile niet ondersteund: %s", d_tostr(ARG(2), msg, sizeof msg));
    }
    return VOIDD;
}

static Datum bi_puppetPalette(Datum *a, int n) {
    Datum x = ARG(0);
    if (x.t == T_INT && d_toint(x) == 0) { P.pal_lib = 0; return VOIDD; }
    Datum m = mkmember(x, VOIDD);
    CastLib *cl;
    Member *mm = member_of(m, &cl);
    if (mm && member_palette(cl, mm)) {
        int speed = d_toint(ARG(1));
        /* snelheid 1..60: hoger = sneller; ruwweg (61-speed)/4+1 frames */
        int frames = n >= 2 && speed > 0 ? (61 - speed) / 4 + 2 : 0;
        palette_set(mm->pal, frames);
        P.pal_lib = m.u.i >> 16;
        P.pal_num = m.u.i & 0xffff;
    }
    return VOIDD;
}

static Datum bi_puppetTransition(Datum *a, int n) {
    P.trans_pending = 1;
    P.trans_type = d_toint(ARG(0));
    P.trans_dur = n >= 2 ? d_toint(ARG(1)) * 250 : 500;
    if (P.trans_dur <= 0) P.trans_dur = 500;
    P.trans_chunk = n >= 3 ? d_toint(ARG(2)) : 4;
    return VOIDD;
}

static Datum bi_startTimer(Datum *a, int n) { (void)a; (void)n; P.timer_base = now_ms(); return VOIDD; }

static Datum bi_spriteBox(Datum *a, int n) {
    int ch = d_toint(ARG(0));
    Channel *c = chan(ch);
    if (!c) return VOIDD;
    int l = d_toint(ARG(1)), t = d_toint(ARG(2)), r = d_toint(ARG(3)), b = d_toint(ARG(4));
    int ol, ot, orr, ob;
    c->stretch = 1;
    c->w = r - l; c->h = b - t;
    sprite_rect(ch, &ol, &ot, &orr, &ob);
    c->loch += l - ol; c->locv += t - ot;
    CP->update_needed = 1;
    return VOIDD;
}

static Datum bi_rollOver(Datum *a, int n) {
    if (n == 0) return d_int(sprite_under(P.mouse_x, P.mouse_y, 0));
    return d_int(sprite_hit(d_toint(ARG(0)), P.mouse_x, P.mouse_y));
}

static Datum bi_cursor(Datum *a, int n) { d_unref(*global_ref(sym("_cursor"))); *global_ref(sym("_cursor")) = d_ref(ARG(0)); return VOIDD; }

static Datum bi_constrainH(Datum *a, int n) {
    int l, t, r, b;
    sprite_rect(d_toint(ARG(0)), &l, &t, &r, &b);
    int v = d_toint(ARG(1));
    return d_int(v < l ? l : v > r ? r : v);
}
static Datum bi_constrainV(Datum *a, int n) {
    int l, t, r, b;
    sprite_rect(d_toint(ARG(0)), &l, &t, &r, &b);
    int v = d_toint(ARG(1));
    return d_int(v < t ? t : v > b ? b : v);
}

static Datum bi_dontPassEvent(Datum *a, int n) { (void)a; (void)n; vm_dontpass = 1; return VOIDD; }
static Datum bi_halt(Datum *a, int n) { (void)a; (void)n; P.halted = 1; vm_abort = 1; return VOIDD; }
static Datum bi_abort(Datum *a, int n) { (void)a; (void)n; vm_abort = 1; return VOIDD; }
static Datum bi_alert(Datum *a, int n) { char buf[1024]; host_alert(d_tostr(ARG(0), buf, sizeof buf)); return VOIDD; }
static Datum bi_put(Datum *a, int n) {
    fprintf(stderr, "-- ");
    for (int i = 0; i < n; i++) { d_print(stderr, a[i]); fputc(' ', stderr); }
    fputc('\n', stderr);
    return VOIDD;
}
static Datum bi_clearGlobals(Datum *a, int n) { (void)a; (void)n; globals_clear(); return VOIDD; }
static Datum bi_noop(Datum *a, int n) { (void)a; (void)n; return VOIDD; }

static Datum bi_openXLib(Datum *a, int n) {
    char buf[300], name[64];
    basename_noext(d_tostr(ARG(0), buf, sizeof buf), name, sizeof name);
    Datum f = xobj_factory(name);
    if (f.t == T_XOBJ) {
        /* de factory-naam als global (INI, FileIO, MovUtils, DLLGlue) */
        const char *g = !_stricmp(name, "DLLGLUE") ? "DLLGlue" : !_stricmp(name, "MOVUTILS") ? "MovUtils"
                      : !_stricmp(name, "FILEIO") ? "FileIO" : name;
        Datum *slot = global_ref(sym(g));
        d_unref(*slot);
        *slot = f;
    }
    return VOIDD;
}

static Datum bi_xtra(Datum *a, int n) {
    char buf[64];
    return xobj_factory(d_tostr(ARG(0), buf, sizeof buf));
}

static Datum bi_window(Datum *a, int n) {
    char buf[64];
    Window *w = win_find(d_tostr(ARG(0), buf, sizeof buf), 1);
    Datum d = {T_WINDOW};
    d.u.w = w;
    return d;
}

static void window_open(Window *w) {
    if (w->open) return;
    if (!w->ctx) w->ctx = calloc(1, sizeof(Player));
    Movie *mv = movie_get(w->file[0] ? w->file : w->name);
    if (!mv) return;
    if (w->r <= w->l) { w->l = (640 - mv->stage_w) / 2; w->t = (480 - mv->stage_h) / 2; w->r = w->l + mv->stage_w; w->b = w->t + mv->stage_h; }
    w->open = 1;
    Player *save = CP;
    CP = w->ctx;
    movie_switch(mv, 1);
    CP = save;
    P.update_needed = 1;
}

static Datum bi_open(Datum *a, int n) {
    if (ARG(0).t == T_WINDOW && ARG(0).u.w) window_open(ARG(0).u.w);
    return VOIDD;
}

static Datum bi_close(Datum *a, int n) {
    if (ARG(0).t == T_WINDOW && ARG(0).u.w) {
        Window *w = ARG(0).u.w;
        if (w->open && w->ctx) {
            Player *save = CP;
            CP = w->ctx;
            movie_event(sym("stopMovie"));
            CP = save;
        }
        w->open = 0;
        P.update_needed = 1;
    }
    return VOIDD;
}

static Datum bi_forget(Datum *a, int n) {
    if (ARG(0).t == T_WINDOW && ARG(0).u.w) {
        bi_close(a, n);
        Window *w = ARG(0).u.w;
        Window **pp = &P.windows;
        while (*pp && *pp != w) pp = &(*pp)->next;
        if (*pp) *pp = w->next;
        /* niet vrijgeven: er kunnen nog Datums naar wijzen */
    }
    return VOIDD;
}

static Datum bi_pause(Datum *a, int n) { (void)a; (void)n; CP->paused = 1; return VOIDD; }
static Datum bi_continue(Datum *a, int n) { (void)a; (void)n; P.paused = 0; CP->paused = 0; return VOIDD; }
static Datum bi_do(Datum *a, int n) {
    Str *s = d_asstr(ARG(0));
    lingo_do(s->s);
    if (--s->rc == 0) free(s);
    return VOIDD;
}

static Datum bi_param(Datum *a, int n) { (void)a; (void)n; return VOIDD; }

static Datum bi_quit(Datum *a, int n) { (void)a; (void)n; P.halted = 2; vm_abort = 1; return VOIDD; }

static Datum bi_delay(Datum *a, int n) { (void)a; (void)n; return VOIDD; }

void builtins_register(void) {
    builtins_pure_register();
    vm_register("go", bi_go);
    vm_register("updateStage", bi_updateStage);
    vm_register("puppetSprite", bi_puppetSprite);
    vm_register("member", bi_member);
    vm_register("cast", bi_member);
    vm_register("field", bi_member);
    vm_register("script", bi_script);
    vm_register("castLib", bi_castLib);
    vm_register("sprite", bi_sprite);
    vm_register("birth", bi_birth);
    vm_register("new", bi_birth);
    vm_register("puppetSound", bi_puppetSound);
    vm_register("soundBusy", bi_soundBusy);
    vm_register("sound", bi_sound);
    vm_register("puppetPalette", bi_puppetPalette);
    vm_register("puppetTransition", bi_puppetTransition);
    vm_register("startTimer", bi_startTimer);
    vm_register("spriteBox", bi_spriteBox);
    vm_register("rollOver", bi_rollOver);
    vm_register("cursor", bi_cursor);
    vm_register("constrainH", bi_constrainH);
    vm_register("constrainV", bi_constrainV);
    vm_register("dontPassEvent", bi_dontPassEvent);
    vm_register("halt", bi_halt);
    vm_register("quit", bi_quit);
    vm_register("abort", bi_abort);
    vm_register("alert", bi_alert);
    vm_register("put", bi_put);
    vm_register("clearGlobals", bi_clearGlobals);
    vm_register("openXLib", bi_openXLib);
    vm_register("closeXLib", bi_noop);
    vm_register("xtra", bi_xtra);
    vm_register("window", bi_window);
    vm_register("open", bi_open);
    vm_register("close", bi_close);
    vm_register("forget", bi_forget);
    vm_register("moveToFront", bi_noop);
    vm_register("preLoad", bi_noop);
    vm_register("preLoadCast", bi_noop);
    vm_register("preloadMember", bi_noop);
    vm_register("unLoadCast", bi_noop);
    vm_register("unloadMember", bi_noop);
    vm_register("pause", bi_pause);
    vm_register("continue", bi_continue);
    vm_register("do", bi_do);
    vm_register("erase", bi_noop);
    vm_register("print", bi_noop);
    vm_register("setDocumentName", bi_noop);
    vm_register("setMargins", bi_noop);
    vm_register("setLandscapeMode", bi_noop);
    vm_register("param", bi_param);
    vm_register("delay", bi_delay);
    vm_register("beep", bi_noop);
}

void player_init(const char *base_dir) {
    memset(&P, 0, sizeof P);
    CP = &P;
    snprintf(P.base_dir, sizeof P.base_dir, "%s", base_dir);
    P.start_ms = now_ms();
    P.timer_base = P.start_ms;
    P.sound_level = 7;
    P.tempo = 15;
    make_mac_palette();
    memcpy(P.pal, g_mac_pal, 768);
    builtins_register();
    xobj_register();
    /* LINGO.INI: de installer zet hier de cd-stationsletter; wij wijzen naar de datamap */
    *global_ref(sym("gCDDrive")) = d_str("CD");
}
