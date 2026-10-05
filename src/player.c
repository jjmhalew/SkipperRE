/* Director 5-speler: films, frames, sprites, events en de builtins die speler-toestand nodig hebben.
 *
 * Eventvolgorde (D5): startMovie -> (frame 1 binnen) enterFrame ... exitFrame -> volgend frame.
 * Muis: primaire eventhandler (the mouseDownScript) -> spritescript -> castlidscript -> framescript
 * -> moviescript; `pass` geeft door, anders stopt het bij de eerste handler. */
#include "dir.h"
#include "winpal.h"
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
Datum chunk_last(Datum str, int kind);
extern char g_item_delim;   /* the itemDelimiter (lingo.c) */
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
    /* datamap, en daarna de opslagmap (daar komt start.dxr als de datamap een cd is, als P.start_name) */
    const char *dirs[2] = {P.base_dir, P.save_dir};
    for (int d = 0; d < 2 && !mv; d++)
        for (int i = 0; exts[i] && !mv && dirs[d][0]; i++) {
            const char *k = d == 1 && !_stricmp(key, "start") && P.start_name[0] ? P.start_name
                          : d == 1 && !_stricmp(key, "magnus") && P.main_name[0] ? P.main_name : key;
            snprintf(path, sizeof path, "%s/%s%s", dirs[d], k, exts[i]);
            if (vfs_exists(path) && (mv = movie_load(path)) && k != key) snprintf(mv->name, sizeof mv->name, "%s", key);
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

/* num < 0: ingebouwd palet zoals in de score (-1 Mac-systeem, -101 Windows-systeem, -102 Windows D5; de rest
 * (Rainbow, Grayscale, ...) gebruikt het spel niet en wordt het Mac-palet) */
static int palette_lookup(Movie *mv, int lib, int num, uint8_t out[256][3]) {
    if (num <= 0) {
        memcpy(out, num == -101 ? g_win_pal : num == -102 ? g_win_d5_pal : (const uint8_t (*)[3])g_mac_pal, 768);
        return 1;
    }
    CastLib *c;
    Member *m = movie_member(mv, lib ? lib : 1, num, &c);
    if (!m || !member_palette(c, m)) return 0;
    memcpy(out, m->pal, 768);
    return 1;
}

/* Duur van een paletovergang in ticks per snelheid 1..30 (D5, gemeten door ScummVM: palette-fade.h) */
static const short k_fade_ticks[30] = {
    494, 478, 460, 444, 426, 409, 392, 375, 358, 340, 323, 306, 289, 272, 254,
    238, 220, 203, 186, 168, 152, 134, 117, 100, 83, 66, 48, 32, 14, 1};

static int fade_ms(int speed) {
    int t = k_fade_ticks[(speed < 1 ? 1 : speed > 30 ? 30 : speed) - 1];
    return t <= 1 ? 0 : t * 1000 / 60;
}

static void palette_set(const uint8_t pal[256][3], int ms) {
    if (ms > 0) {
        memcpy(P.pal_from, P.pal, 768);
        memcpy(P.pal_target, pal, 768);
        P.pal_fade_left = 1;
        P.pal_fade_t0 = now_ms();
        P.pal_fade_ms = (uint32_t)ms;
    } else {
        memcpy(P.pal, pal, 768);
        P.pal_fade_left = 0;
    }
    P.update_needed = 1;
}

/* in echte tijd: Director wacht op de overgang (een frame duurt minstens tot hij klaar is, zie player_tick) */
void palette_step(void) {
    if (P.pal_fade_left <= 0) return;
    uint32_t el = now_ms() - P.pal_fade_t0;
    double t = el >= P.pal_fade_ms ? 1.0 : (double)el / P.pal_fade_ms;
    for (int i = 0; i < 256; i++)
        for (int k = 0; k < 3; k++)
            P.pal[i][k] = (uint8_t)(P.pal_from[i][k] + (P.pal_target[i][k] - P.pal_from[i][k]) * t);
    if (t >= 1.0) P.pal_fade_left = 0;
    P.update_needed = 1;
}

static int fade_remaining(void) {
    if (P.pal_fade_left <= 0) return 0;
    uint32_t el = now_ms() - P.pal_fade_t0;
    return el >= P.pal_fade_ms ? 0 : (int)(P.pal_fade_ms - el);
}

/* ------------------------------------------------------------------ sprites */
static Channel *chan(int n) { return n >= 1 && n <= NCHAN ? &CP->ch[n] : NULL; }

static Member *chan_member(Channel *c, CastLib **cl) {
    if (!c || !c->member || !CP->mv) return NULL;
    return movie_member(CP->mv, c->lib, c->member, cl);
}

/* ------------------------------------------------------------------ digitale video */
/* Gekoppelde AVI's: het spel verwijst naar R:\Magnus15\NL\Media\Video\x.avi; op de cd staan ze in Video\. */
Video *member_video(Member *m) {
    if (!m || m->type != MT_VIDEO) return NULL;
    if (!m->video && !m->video_failed && m->file && m->file[0]) {
        const char *sub[] = {"\\Video\\", "\\"};
        char path[260];
        for (int i = 0; i < 2 && !m->video; i++) {
            snprintf(path, sizeof path, "%s%s%s", P.base_dir, sub[i], m->file);
            m->video = video_open(path);
        }
        if (!m->video) {
            vm_error("video niet gevonden: %s", m->file);
            m->video_failed = 1;
        }
    }
    return m->video;
}

static Video *chan_video(Channel *c) {
    CastLib *cl;
    return member_video(chan_member(c, &cl));
}

static int movie_time(Channel *c, Video *v) {
    int t = c->movie_time0;
    if (c->movie_rate) t += (int)((now_ms() - c->movie_t0) * 60ull / 1000) * c->movie_rate;
    int d = v ? video_duration(v) : 0;
    if (t > d) t = d;
    return t < 0 ? 0 : t;
}

/* nieuwe tijd/snelheid vastleggen en het geluidsspoor meenemen (alleen normaal afspelen heeft geluid) */
static void movie_update(Channel *c, int time, int rate) {
    Video *v = chan_video(c);
    c->movie_time0 = time;
    c->movie_t0 = now_ms();
    c->movie_rate = rate;
    if (v && rate == 1) sound_video_play(video_audio(v), time / 60.0);
    else sound_video_stop();
}

const uint32_t *chan_video_frame(Channel *c, int *w, int *h) {
    Video *v = chan_video(c);
    if (!v) return NULL;
    *w = video_width(v); *h = video_height(v);
    return video_frame(v, video_frame_at(v, movie_time(c, v)));
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
        if (tx && !c->stretch) { w = tx->w + text_frame(tx); h = tx->h + text_frame(tx); }
    } else if (m && m->type == MT_VIDEO) {
        int fw = m->rect_r - m->rect_l, fh = m->rect_b - m->rect_t;
        if (!c->stretch) { w = fw; h = fh; }
        rx = w / 2; ry = h / 2;   /* registratiepunt van video = midden */
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
    if (!c || !c->visible || !c->member || c->killed) return 0;
    int l, t, r, b;
    sprite_rect(ch, &l, &t, &r, &b);
    if (x < l || x >= r || y < t || y >= b) return 0;
    /* Alleen matte (8) en mask (9) klikken per pixel; bij alle andere inks, ook background transparent,
     * telt de hele rechthoek (ABC-spel: de gaten in de letterknoppen zijn gewoon klikbaar). */
    CastLib *cl;
    Member *m = chan_member(c, &cl);
    if (m && m->type == MT_BITMAP && (c->ink == 8 || c->ink == 9)) {
        Bitmap *bm = member_bitmap(cl, m);
        if (bm && bm->w && bm->h && r > l && b > t) {
            int px = (x - l) * bm->w / (r - l), py = (y - t) * bm->h / (b - t);
            Bitmap *mk = c->ink == 9 ? sprite_mask(c) : NULL;
            if (mk ? !mask_at(bm, mk, px, py) : bm->px[py * bm->w + px] == 0) return 0;
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
    /* zoals Director (ScummVM Sprite::isActive): ook een moveable sprite en een knop; MMB10 sleept zijn letters met
       the clickOn, die zonder dit 0 bleef */
    if (c->script || c->moveable) return 1;
    CastLib *cl;
    Member *m = chan_member(c, &cl);
    return m && (m->script != NULL || m->type == MT_BUTTON);
}

/* Sprite die de muis opvangt: met script, moveable, editable of met een eigen cursor. Een kale sprite
 * (bijv. de voorgrond LocE2F over de emmer en de uitgang onderaan in E2) laat de muis door. */
static int field_editable(Player *ctx, int ch);
static int sprite_mousable(int ch) {
    Channel *c = chan(ch);
    if (!c) return 0;
    int cur = c->cursor.t != T_VOID && !(c->cursor.t == T_INT && c->cursor.u.i == 0);
    return sprite_active(ch) || c->moveable || cur || field_editable(CP, ch);
}

/* mode 0: bovenste sprite, 1: bovenste met script, 2: bovenste die de muis opvangt */
static int sprite_under(int x, int y, int mode) {
    for (int ch = NCHAN; ch >= 1; ch--)
        if (sprite_hit(ch, x, y) && (mode == 0 || (mode == 1 ? sprite_active(ch) : sprite_mousable(ch)))) return ch;
    return 0;
}

int sprite_mouse_target(int x, int y) { return sprite_under(x, y, 2); }

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
static int g_score_snd[3];   /* 1: wat op dit kanaal speelt kwam uit het geluidskanaal van de score */

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
        /* filmloop: elke sprite loopt zijn eigen loop, één beeld per frame van de score; opnieuw bij een ander lid */
        Channel *c = &CP->ch[i];
        int id = c->lib << 16 | c->member;
        if (id != c->loop_id) { c->loop_id = id; c->loop_frame = 0; }
        else c->loop_frame++;
    }
    if (fr->tempo && fr->tempo <= 120) CP->tempo = fr->tempo;
    if (CP == &P) {
        /* paletkanaal */
        /* zonder cel geldt het palet van de laatste cel ervoor in de score (ook na een sprong: Intro's klik naar
           "IntroEnd" tijdens een wit flitsframe); de overgang met de snelheid van de cel zelf */
        const Frame *pf = fr;
        for (int f = CP->frame - 1; !pf->pal && f >= 1; f--) pf = &mv->score.f[f - 1];
        if (!P.pal_lib && pf->pal && (pf->pal != P.pal_num || first)) {
            uint8_t pal[256][3];
            if (palette_lookup(mv, pf->pal_lib, pf->pal, pal)) palette_set(pal, pf == fr ? fade_ms(fr->pal_speed) : 0);
            P.pal_num = pf->pal;
        }
        /* geluidskanalen */
        int snd[3] = {0, fr->snd1, fr->snd2};
        int lib[3] = {0, fr->snd1_lib, fr->snd2_lib};
        for (int ch = 1; ch <= 2; ch++) {
            if (snd[ch] && snd[ch] != g_last_snd[ch]) {
                CastLib *cl;
                Member *m = movie_member(mv, lib[ch], snd[ch], &cl);
                if (m && m->type == MT_SOUND) { sound_play_member(ch, cl, m); g_score_snd[ch] = 1; }
            } else if (!snd[ch] && g_score_snd[ch]) {
                /* de cel is afgelopen: een geluid dat de score startte stopt (Magnus1: DrumLoop2 na frame 164) */
                sound_stop(ch);
                g_score_snd[ch] = 0;
            }
            g_last_snd[ch] = snd[ch];
        }
        if (fr->trans && !first) {
            CastLib *cl;
            Member *m = movie_member(mv, fr->trans_lib, fr->trans, &cl);
            if (m && m->speclen >= 6) {   /* transitie-lid: [?, chunk, type, gebied, duur ms (be16)] */
                P.trans_pending = 1;
                P.trans_chunk = m->spec[1];
                P.trans_type = m->spec[2];
                P.trans_dur = m->spec[4] << 8 | m->spec[5];
                P.trans_area = !(m->spec[3] & 1);   /* bit 0 = hele podium, anders alleen wat verandert (ScummVM) */
                if (P.trans_dur <= 0) P.trans_dur = 100;   /* 0 = zo snel mogelijk */
            }
        } else if (fr->trans_type && !first) {   /* D4: de transitie staat in het frame */
            P.trans_pending = 1;
            P.trans_chunk = fr->trans_chunk;
            P.trans_type = fr->trans_type;
            P.trans_dur = fr->trans_ms > 0 ? fr->trans_ms : 100;
            P.trans_area = fr->trans_area;
        }
    }
    /* de actorList (P.actor_list) hoort bij de film op het podium: niet stappen in een dialoogvenster,
       anders schrijft Skeeto's stepFrame in sprite 21 van mmdlg1 */
    if (CP == &P) actor_step();
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
        /* in VWCF begint de ingebouwde reeks bij 0: -100 = Windows-systeem (-101 in de score) */
        int dp = mv->def_pal <= 0 ? mv->def_pal - 1 : mv->def_pal;
        if (palette_lookup(mv, mv->def_pal_lib, dp, pal)) palette_set(pal, 0);
        g_last_snd[1] = g_last_snd[2] = 0;
        g_score_snd[1] = g_score_snd[2] = 0;   /* een geluid uit de vorige film speelt gewoon uit */
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
    int went = CP->going, next = went ? CP->next_frame : CP->frame + 1;
    CP->going = 0;
    if (!CP->mv) return;
    if (next < 1) next = 1;
    if (next > CP->mv->score.nframes) {
        /* de speelkop loopt van de score af: de projector stopt (Magnus1 eindigt zo op het lege frame StopGame);
         * een dialoogvenster of een go voorbij het eind blijft op het laatste frame staan */
        if (CP == &P && !went) { P.halted = 2; return; }
        next = CP->mv->score.nframes;
    }
    int changed = next != CP->frame;
    CP->frame = next;
    (void)changed;
    enter_frame(0);
}

static void ctx_tick(void) {
    /* Een go van een klik, toets of enterFrame verplaatst de speelkop al: dan geen exitFrame, anders zet het
     * gebruikelijke `on exitFrame go(the frame)` hem terug en sluit bijv. de beloningsdialoog (mmdlg5) nooit */
    if (!CP->going && !CP->pending_movie[0]) frame_event(sym("exitFrame"));
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
    int tempo = P.tempo > 0 ? P.tempo : 15, fr = fade_remaining();
    return 1000 / tempo > fr ? 1000 / tempo : fr;
}

void player_idle(void) {
    if (P.halted || !P.mv) return;
    CP = &P;
    palette_step();
    frame_event(sym("idle"));
    vm_abort = 0;
    /* ook de films in de dialoogvensters krijgen idle (mmtutor verbergt zo de volumemeter weer) */
    for (Window *w = P.windows; w; w = w->next)
        if (w->open && w->ctx && w->ctx->mv) {
            CP = w->ctx;
            frame_event(sym("idle"));
            vm_abort = 0;
        }
    CP = &P;
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
        /* the doubleClick: deze klik volgt binnen 500 ms (de standaard van Windows) en vlakbij op de vorige */
        int t = (int)now_ms();
        P.double_click = t - P.last_click < 500 && abs(lx - P.click_x) <= 4 && abs(ly - P.click_y) <= 4;
        P.last_click = t;
        P.click_x = lx; P.click_y = ly;   /* in de coördinaten van het venster, net als the mouseH */
        int top = sprite_under(lx, ly, 2);
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

/* staat er ergens (podium of een open venster) een editable veld? (Android: schermtoetsenbord) */
int player_text_wanted(void) {
    for (int ch = 1; ch <= NCHAN; ch++)
        if (field_editable(&P, ch)) return 1;
    for (Window *w = P.windows; w; w = w->next)
        if (w->open && w->visible && w->ctx && w->ctx->mv)
            for (int ch = 1; ch <= NCHAN; ch++)
                if (field_editable(w->ctx, ch)) return 1;
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

/* de muis in de coördinaten van de film in CP: in een dialoogvenster ten opzichte van dat venster */
static void mouse_in_cp(int *x, int *y) {
    *x = P.mouse_x; *y = P.mouse_y;
    for (Window *w = P.windows; w; w = w->next)
        if (w->ctx == CP) { *x -= w->l; *y -= w->t; }
}

/* sprite-cursor onder (x, y) in podiumcoördinaten: in het bovenste venster daar, anders op het podium; *mv = de
 * film waar de cursor-castleden bij horen */
Datum player_sprite_cursor(int x, int y, Movie **mv) {
    Window *w = top_window_at(x, y);
    Player *save = CP, *ctx = w && w->ctx ? w->ctx : &P;
    CP = ctx;
    int ch = sprite_under(w ? x - w->l : x, w ? y - w->t : y, 2);
    CP = save;
    *mv = ctx->mv;
    if (ch && ctx->ch[ch].cursor.t != T_VOID && !(ctx->ch[ch].cursor.t == T_INT && ctx->ch[ch].cursor.u.i == 0))
        return ctx->ch[ch].cursor;
    return VOIDD;
}

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
    /* de muis in de coördinaten van de film die het vraagt: in een dialoogvenster ten opzichte van dat venster
     * (de volumeschuif van mmdlg1 staat in een venster op x = 168) */
    if (!_stricmp(n, "mouseH")) host_pump();   /* de muis bijwerken in `repeat while the stillDown`-lussen */
    int mx, my;
    mouse_in_cp(&mx, &my);
    if (!_stricmp(n, "mouseH")) return d_int(mx);
    if (!_stricmp(n, "mouseV")) return d_int(my);
    if (!_stricmp(n, "clickLoc")) return d_point(P.click_x, P.click_y);
    if (!_stricmp(n, "doubleClick")) return d_int(P.double_click);
    if (!_stricmp(n, "result")) return d_ref(vm_result);
    if (!_stricmp(n, "stageLeft") || !_stricmp(n, "stageTop")) return d_int(0);
    if (!_stricmp(n, "stageRight")) return d_int(640);
    if (!_stricmp(n, "stageBottom")) return d_int(480);
    if (!_stricmp(n, "colorDepth")) return d_int(8);
    if (!_stricmp(n, "lastFrame")) return d_int(CP->mv ? CP->mv->score.nframes : 0);
    if (!_stricmp(n, "mouseCast") || !_stricmp(n, "mouseMember")) {
        int ch = sprite_under(mx, my, 0);
        return d_int(ch ? CP->ch[ch].member : -1);
    }
    if (!_stricmp(n, "rollOver")) return d_int(sprite_under(mx, my, 0));
    if (!_stricmp(n, "platform")) return str_of("Windows,32");
    if (!_stricmp(n, "machineType")) return d_int(256);
    if (!_stricmp(n, "maxInteger")) return d_int(0x7fffffff);
    if (!_stricmp(n, "itemDelimiter")) { char d[2] = {g_item_delim, 0}; return str_of(d); }
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
static int lib_of(Datum lib);

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
        if (id > 11 && nt) return chunk_last(tg[0], id - 11);   /* the last char/word/item/line in x */
        return VOIDD;
    case 1: {
        Str *s = d_asstr(tg[0]);
        int c = chunk_count(s->s, id);
        if (--s->rc == 0) free(s);
        return d_int(c);
    }
    case 4: return d_int(id == 1 && nt ? sound_volume(d_toint(tg[0])) : 255);   /* the volume of sound n */
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
        if (id == 2) {   /* the number of castMembers of castLib n / "naam" */
            int lib = nt ? lib_of(tg[0]) : 1;
            if (lib == 0) lib = 1;
            if (!CP->mv || lib < 1 || lib > CP->mv->nlibs) return d_int(0);
            CastLib *c = CP->mv->libs[lib - 1];
            return d_int(c->n + c->first - 1);
        }
        if (id == 4) return d_int(CP->mv ? CP->mv->nlibs : 0);
        return d_int(0);
    case 9: case 10: case 11: case 13: {   /* 13: video-eigenschappen (loop, duration, controller, ...) */
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
    case 4:
        if (id == 1 && nt) sound_set_volume(d_toint(tg[0]), d_toint(v));
        break;
    case 6: sprite_set(d_toint(tg[0]), id, v); return;   /* sprite_set neemt v over */
    case 7:
        if (id == 26) { P.sound_level = d_toint(v); sound_set_level(P.sound_level); }
        else if (id == 8) P.exit_lock = d_toint(v);
        else if (id == 27) P.stage_color = d_toint(v);
        break;
    case 9: case 10: case 11: case 13: {   /* 13: video-eigenschappen (loop, duration, controller, ...) */
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
    case 15: return d_int(c->movie_rate);
    case 16: return d_int(movie_time(c, chan_video(c)));
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
    case 1:   /* type 0 = leeg kanaal (ABC-spel: video uit), 16 = weer in gebruik */
        c->type = d_toint(v);
        c->killed = c->type == 0;
        break;
    case 2: c->back = d_toint(v); break;
    case 4: case 37: case 35: {
        int ol = c->lib, om = c->member;
        set_member(c, v);
        if (c->lib != ol || c->member != om) {   /* nieuwe member: video staat stil aan het begin */
            if (c->movie_rate) sound_video_stop();
            c->movie_rate = 0;
            c->movie_time0 = 0;
        }
        break;
    }
    case 15: movie_update(c, movie_time(c, chan_video(c)), d_toint(v)); break;
    case 16: movie_update(c, d_toint(v), c->movie_rate); break;
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
    if (!_stricmp(n, "text")) {
        if (m->type == MT_RICHTEXT) { member_richtext(cl, m); return d_str(m->rte_text ? m->rte_text : ""); }
        Text *t = member_text(cl, m);
        return d_str(t && t->text ? t->text : "");
    }
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
        if (m->type == MT_VIDEO) { Video *v = member_video(m); return d_int(v ? video_duration(v) : 0); }
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
    if (!_stricmp(n, "itemDelimiter")) {
        char d[8];
        d_tostr(v, d, sizeof d);
        g_item_delim = d[0] ? d[0] : ',';
    }
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
    if (s.t == T_XOBJ) {   /* new(xtra("PrintOMatic_Lite")): methode #new met de overige argumenten */
        Datum args[8];
        int k = n < 8 ? n : 8;
        args[0] = d_sym(sym("new"));
        for (int i = 1; i < k; i++) args[i] = a[i];
        return xobj_call(s.u.x, args, k);
    }
    if (s.t == T_SYM) return VOIDD;   /* new(#field, castLib x): niet nodig */
    vm_error("birth/new op geen script");
    return VOIDD;
}

static Datum bi_puppetSound(Datum *a, int n) {
    int ch = 1;
    Datum x = ARG(0);
    if (n >= 2) { ch = d_toint(ARG(0)); x = ARG(1); }
    if (ch >= 1 && ch <= 2) g_score_snd[ch] = 0;   /* het script heeft het kanaal nu */
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
    if (!_stricmp(c, "stop")) sound_stop(d_toint(ARG(1)));
    else if (!_stricmp(c, "fadeOut")) {   /* sound fadeOut ch, ticks; zonder ticks 15 frames van het huidige tempo */
        int tempo = P.tempo > 0 ? P.tempo : 15;
        int ticks = n >= 3 ? d_toint(ARG(2)) : 15 * 60 / tempo;
        sound_fade_out(d_toint(ARG(1)), ticks * 1000 / 60);
    }
    else if (!_stricmp(c, "playFile")) {
        /* spellingspel: eigen woorden, gMMPath & "WAV" & naam. Het laatst geladen bestand per kanaal
           blijft bewaard tot het volgende (de mixer speelt het nog af). */
        static Sound *loaded[9];
        int ch = d_toint(ARG(1));
        char path[300], alt[300];
        d_tostr(ARG(2), path, sizeof path);
        Sound *s = sound_load_wav(path);
        if (!s) s = sound_load_wav(path_resolve(path, alt, sizeof alt));
        if (!s) { vm_error("sound playFile: %s niet te laden", path); return VOIDD; }
        if (ch >= 1 && ch <= 8) {
            if (ch <= 2) g_score_snd[ch] = 0;
            sound_stop(ch);
            if (loaded[ch]) { free(loaded[ch]->pcm); free(loaded[ch]); }
            loaded[ch] = s;
            sound_play_sound(ch, s);
        } else { free(s->pcm); free(s); }
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
        /* snelheid 1..60 (60 = meteen, zo gebruikt het spel hem overal); de helft van de schaal van het paletkanaal */
        int speed = d_toint(ARG(1));
        palette_set(mm->pal, n >= 2 && speed > 0 ? fade_ms((speed + 1) / 2) : 0);
        P.pal_lib = m.u.i >> 16;
        P.pal_num = m.u.i & 0xffff;
    }
    return VOIDD;
}

static Datum bi_puppetTransition(Datum *a, int n) {
    P.trans_pending = 1;
    P.trans_type = d_toint(ARG(0));
    /* tijd in kwartseconden; 0 = zo snel mogelijk, net als bij de transities in de score */
    P.trans_dur = n >= 2 ? d_toint(ARG(1)) * 250 : 500;
    if (P.trans_dur <= 0) P.trans_dur = 100;
    P.trans_chunk = n >= 3 ? d_toint(ARG(2)) : 4;
    P.trans_area = n >= 4 && d_truthy(ARG(3));
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
    int mx, my;
    mouse_in_cp(&mx, &my);
    if (n == 0) return d_int(sprite_under(mx, my, 0));
    return d_int(sprite_hit(d_toint(ARG(0)), mx, my));
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
/* in een projector sluit halt het programma (start.dxr: geen cd, geen 256 kleuren) */
static Datum bi_halt(Datum *a, int n) { (void)a; (void)n; P.halted = 2; vm_abort = 1; return VOIDD; }
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
static Datum bi_print(Datum *a, int n) { xobj_print_cmd("print", a, n); return VOIDD; }
static Datum bi_setDocumentName(Datum *a, int n) { xobj_print_cmd("setDocumentName", a, n); return VOIDD; }
static Datum bi_setLandscapeMode(Datum *a, int n) { xobj_print_cmd("setLandscapeMode", a, n); return VOIDD; }

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
    vm_register("print", bi_print);
    vm_register("setDocumentName", bi_setDocumentName);
    vm_register("setMargins", bi_noop);
    vm_register("setLandscapeMode", bi_setLandscapeMode);
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
    /* LINGO.INI (startup): de openxlib-regels (D4: openxlib "fileio"); de cd-stationsletter van de installer niet */
    char li[PLAT_PATH], line[256];
    snprintf(li, sizeof li, "%s\\LINGO.INI", base_dir);
    FILE *lf = fopen(li, "rb");
    while (lf && fgets(line, sizeof line, lf)) {
        char *q = line;
        while (*q == ' ' || *q == '\t') q++;
        char *o = strchr(q, '"'), *e = o ? strchr(o + 1, '"') : NULL;
        if (!_strnicmp(q, "openxlib", 8) && o && e) {
            *e = 0;
            Datum arg = d_str(o + 1);
            bi_openXLib(&arg, 1);
            d_unref(arg);
        }
    }
    if (lf) fclose(lf);
    /* LINGO.INI: de installer zet hier de cd-stationsletter; wij wijzen naar de datamap */
    *global_ref(sym("gCDDrive")) = d_str("CD");
}
