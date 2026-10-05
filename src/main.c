/* SkipperRE - opstarten, hoofdlus, invoerwachtrij, transities, paden; overal hetzelfde. Het venster zelf zit in
 * host_win.c (Windows) of host_sdl.c (Linux, Android).
 *
 *   skipper.exe [datamap] [--movie start] [--bin SKIPPER_1.BIN] [--scale 2] [--fullscreen] [--trace]
 *               [--intro | --nointro]  (intro altijd / logo en intro nooit; standaard: geen intro als er een opgeslagen spel is)
 *               [--dumptex] [--hd N]   (texture packs: bitmaps wegschrijven / beeldschaal kiezen, zie texpack.c)
 *               [--shot N out.bmp]   (headless: N frames draaien, stage opslaan, stoppen)
 *               [--click x y F]      (headless: klik op (x,y) vlak voor frame F)
 *               [--drag x1 y1 x2 y2 F] (headless: slepen van (x1,y1) naar (x2,y2) vlak voor frame F)
 *               [--key code char F]  (headless: toets met Mac-keyCode en teken (ASCII) vlak voor frame F)
 *               [--global naam int F] (headless: global op een getal zetten vlak voor frame F)
 */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef __ANDROID__
#include <SDL_main.h>
#endif

extern int sound_headless;
int g_headless;
void palette_step(void);
void player_idle(void);

static uint32_t *g_prev;       /* laatst getoonde stage (voor transities) */

/* Headless (tests) loopt de tijd virtueel: elk frame schuift de klok de frameduur op, en elke keer dat iets de tijd
 * opvraagt 1 ms (zodat een script dat op de klok wacht altijd verder komt). Zo geeft een testrun op elke machine en
 * elk platform hetzelfde, onafhankelijk van hoe snel hij draait. */
static uint32_t g_vclock = 1000000;
uint32_t now_ms(void) { return g_headless ? g_vclock++ : plat_ms(); }

void host_alert(const char *msg) {
    fprintf(stderr, "[alert] %s\n", msg);
    if (!g_headless) host_message(msg, 0);
}

/* Paden van het spel (D:\..., C:\WINDOWS\..., "CD:\...") -> datamap of opslagmap */
char *path_resolve(const char *p, char *out, int n) {
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '\\' || *q == '/' || *q == ':') b = q + 1;
    char cand[600];
    snprintf(cand, sizeof cand, "%s\\%s", P.save_dir, b);
    if (plat_exists(cand)) { snprintf(out, n, "%s", cand); return out; }
    snprintf(cand, sizeof cand, "%s\\%s", P.base_dir, b);
    if (vfs_exists(cand)) { snprintf(out, n, "%s", cand); return out; }
    snprintf(out, n, "%s\\%s", P.save_dir, b);   /* nieuw bestand: in de opslagmap */
    return out;
}

static void setup_save_dir(void) {
    plat_user_dir(P.save_dir, sizeof P.save_dir);
    char f[600];
    snprintf(f, sizeof f, "%s\\IVANOFF.INI", P.save_dir);
    if (!plat_exists(f)) {
        char dir[300];
        snprintf(dir, sizeof dir, "%s\\", P.save_dir);
        ini_set(f, "MAGNUS", "Path", dir);
    }
    snprintf(f, sizeof f, "%s\\MAGNUS.INI", P.save_dir);
    if (!plat_exists(f)) {
        FILE *fp = fopen(f, "wb");
        if (fp) {
            fprintf(fp, "[MAGNUS]\r\nPath=%s\\\r\n[Sound]\r\nStartLevel=4\r\n", P.save_dir);
            fclose(fp);
        }
    } else {   /* zonder aanhalingstekens: de D4-cd leest deze regel zelf met FileIO (eerdere versies schreven "...") */
        char dir[300];
        snprintf(dir, sizeof dir, "%s\\", P.save_dir);
        ini_set(f, "MAGNUS", "Path", dir);
    }
}

/* De Scandinavische cd heeft spraak en tekst in vier talen; start.dxr leest de keuze uit MAGNUS.INI [Language] Speak /
 * Text (DK, N, S, SF; het installatieprogramma schreef die, de instellingendialoog van het spel past ze aan). Bij de
 * eerste start komt de taal van het systeem erin (anders Deens, zoals start.dxr zelf), --lang zet hem altijd. */
int ui_nl(void) {
    static int v = -1;
    if (v < 0) {
        char l[64] = "";
        host_locale(l, sizeof l);
        v = !_strnicmp(l, "nl", 2);
    }
    return v;
}

static const char *lang_code(const char *s) {
    static const struct { const char *in, *code; } map[] = {
        {"DK", "DK"}, {"da", "DK"}, {"N", "N"}, {"no", "N"}, {"nb", "N"}, {"nn", "N"},
        {"S", "S"}, {"sv", "S"}, {"SF", "SF"}, {"fi", "SF"},
    };
    for (int i = 0; i < (int)(sizeof map / sizeof *map); i++) {
        size_t n = strlen(map[i].in);
        if (!_strnicmp(s, map[i].in, n) && (!s[n] || s[n] == '_' || s[n] == '-' || s[n] == '.')) return map[i].code;
    }
    return NULL;
}

static void setup_language(const char *want) {
    char f[600], cur[16];
    snprintf(f, sizeof f, "%s\\MAGNUS.INI", P.save_dir);
    const char *code = want ? lang_code(want) : NULL;
    if (want && !code) fprintf(stderr, "[taal] onbekend: %s (DK, N, S of SF)\n", want);
    if (!code) {
        if (ini_get(f, "Language", "Speak", "", cur, sizeof cur)) return;   /* al gekozen */
        char loc[64] = "";
        host_locale(loc, sizeof loc);
        code = lang_code(loc);
        fprintf(stderr, "[taal] systeemtaal %s -> %s\n", loc[0] ? loc : "?", code ? code : "DK");
        if (!code) code = "DK";
    }
    ini_set(f, "Language", "Speak", code);
    ini_set(f, "Language", "Text", code);
}

/* venstertitel: de naam van het spel in zijn teksttaal */
const char *g_title = "Skipper & Skeeto in Pretpark";

static void set_title(void) {
    char f[600], t[16];
    snprintf(f, sizeof f, "%s\\MAGNUS.INI", P.save_dir);
    ini_get(f, "Language", "Text", "DK", t, sizeof t);
    const char *c = lang_code(t);
    g_title = !c || !strcmp(c, "DK") ? "Magnus og Myggen" : !strcmp(c, "N") ? "Magnus & Myggen"
            : !strcmp(c, "S") ? "Magnus och Myggan" : "Manu ja Matti";
}

/* ------------------------------------------------------------------ venster-hulp */
/* podium-rechthoek in een venster van cw x ch: zo groot mogelijk in 4:3, gecentreerd; een geheel veelvoud als dat
 * bijna past (scherpere pixels) */
void stage_fit(int cw, int ch, int *l, int *t, int *w, int *h) {
    int ww = cw, hh = cw * 3 / 4;
    if (hh > ch) { hh = ch; ww = ch * 4 / 3; }
    int k = ww / 640;
    if (k >= 1 && 640 * k >= ww * 95 / 100) { ww = 640 * k; hh = 480 * k; }
    *l = (cw - ww) / 2; *t = (ch - hh) / 2;
    *w = ww; *h = hh;
}

/* sprite-cursor onder de muis, anders de globale cursor() */
/* de film van de laatst gevraagde cursor: een sprite in een dialoogvenster wijst naar castleden van dat venster */
static Movie *g_cursor_mv;
Movie *cursor_movie(void) { return g_cursor_mv ? g_cursor_mv : P.mv; }

Datum cursor_wanted(void) {
    Datum c = player_sprite_cursor(P.mouse_x, P.mouse_y, &g_cursor_mv);
    if (c.t != T_VOID) return c;
    g_cursor_mv = P.mv;
    return *global_ref(sym("_cursor"));
}

/* bitmapcursor [member, masker] als 32-bit beeld met alfa, scale keer zo groot: zwart/wit uit de bitmap,
 * doorzichtig buiten het masker. NULL = geen bitmap (pijl). */
uint32_t *cursor_image(Datum lst, int s, int *w, int *h, int *hx, int *hy) {
    if (lst.t != T_LIST || lst.u.l->n < 1) return NULL;
    int num = d_toint(lst.u.l->v[0]), mask = lst.u.l->n > 1 ? d_toint(lst.u.l->v[1]) : 0;
    CastLib *cl, *ml;
    Movie *mv = cursor_movie();
    Member *m = mv ? movie_member(mv, (num >> 16) + 1, num & 0xffff, &cl) : NULL;
    Member *mm = mask && mv ? movie_member(mv, (mask >> 16) + 1, mask & 0xffff, &ml) : NULL;
    Bitmap *b = m ? member_bitmap(cl, m) : NULL;
    Bitmap *mb = mm ? member_bitmap(ml, mm) : NULL;
    if (!b || !b->w || !b->h) return NULL;
    if (s < 1) s = 1;
    int cw = b->w * s, ch = b->h * s;
    if (cw > 256) cw = 256;
    if (ch > 256) ch = 256;
    uint32_t *px = malloc(sizeof(uint32_t) * cw * ch);
    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++) {
            int sx = x / s, sy = y / s;
            int black = b->px[sy * b->w + sx] != 0;
            int opaque = mb ? (sx < mb->w && sy < mb->h && mb->px[sy * mb->w + sx] != 0) : black;
            px[y * cw + x] = !opaque ? 0 : black ? 0xff000000u : 0xffffffffu;
        }
    *w = cw; *h = ch; *hx = b->reg_x * s; *hy = b->reg_y * s;
    return px;
}

/* ------------------------------------------------------------------ invoer */
/* De host werkt alleen de toestand bij (muispositie, knop) en zet events in deze wachtrij; de hoofdlus dispatcht ze
 * als er geen Lingo loopt. Zo kan een script dat `repeat while the stillDown` doet via host_pump() de echte knop zien
 * zonder dat events midden in een handler binnenkomen. */
typedef struct InEv { int kind, x, y, a, b; } InEv;
static InEv g_q[256];
static int g_qh, g_qt;

void input_push(int kind, int x, int y, int a, int b) {
    int n = (g_qt + 1) & 255;
    if (n == g_qh) return;
    g_q[g_qt] = (InEv){kind, x, y, a, b};
    g_qt = n;
}

static int logo_tap(void);

/* voor een eigen lus buiten het spel (songs.c): het volgende event uit de wachtrij */
int input_next(int *kind, int *x, int *y, int *a, int *b) {
    if (g_qh == g_qt) return 0;
    InEv e = g_q[g_qh];
    g_qh = (g_qh + 1) & 255;
    *kind = e.kind; *x = e.x; *y = e.y; *a = e.a; *b = e.b;
    return 1;
}

/* 1 = een klik of toets vroeg om een ander frame of een andere film (go): de hoofdlus wacht dan niet de rest van
 * dit frame af (bij tempo 8 tot 125 ms), zodat overslaan meteen reageert */
static int drain_input(void) {
    int any = 0;
    while (g_qh != g_qt) {
        InEv e = g_q[g_qh];
        g_qh = (g_qh + 1) & 255;
        if (((e.kind == 1 && !e.a) || e.kind == 3) && logo_tap()) { any = 1; continue; }
        if (e.kind == 1) player_mouse(e.x, e.y, 1, 0, e.a);
        else if (e.kind == 2) player_mouse(e.x, e.y, 0, 1, e.a);
        else player_key(e.a, e.b, e.kind == 3);
        any = 1;
    }
    return any && (P.going || P.pending_movie[0]);
}

/* ------------------------------------------------------------------ tonen */
/* Director-transities (codes 1..52, zie trans.c), live in het venster */
static void transition(const uint32_t *from, const uint32_t *to, int type, int dur, int chunk, int s, int area) {
    if (g_headless || dur <= 0) return;
    static uint32_t *tmp;
    static int tmp_s;
    if (tmp_s != s) { free(tmp); tmp = malloc((size_t)640 * 480 * s * s * 4); tmp_s = s; }
    /* changing area only: de rechthoek (in podiumpixels) waarin oud en nieuw verschillen */
    int rx = 0, ry = 0, rw = 640, rh = 480;
    if (area) {
        int l = 640, t = 480, r = 0, b = 0, W = 640 * s;
        for (int y = 0; y < 480 * s; y++)
            for (int x = 0; x < W; x++)
                if (from[(size_t)y * W + x] != to[(size_t)y * W + x]) {
                    if (x / s < l) l = x / s;
                    if (x / s >= r) r = x / s + 1;
                    if (y / s < t) t = y / s;
                    if (y / s >= b) b = y / s + 1;
                }
        if (r <= l) return;   /* niets veranderd */
        rx = l; ry = t; rw = r - l; rh = b - t;
    }
    uint32_t t0 = now_ms();
    for (;;) {
        double t = (double)(now_ms() - t0) / dur;
        if (t >= 1) break;
        trans_frame_r(tmp, from, to, type, chunk, t, s, rx, ry, rw, rh);
        host_blit(tmp, 640 * s, 480 * s);
        host_events();
        plat_sleep(10);
    }
}

/* het beeld tonen: stage_px (640x480), of met een HD texture pack het beeld op schaal s */
void stage_present(void) {
    stage_compose();
    uint32_t *hd = g_headless ? NULL : stage_compose_hd();
    int s = hd ? texpack_scale() : 1;
    const uint32_t *shown = hd ? hd : stage_px;
    size_t n = (size_t)640 * 480 * s * s;
    static int prev_s;
    if (prev_s != s) { free(g_prev); g_prev = NULL; prev_s = s; }
    if (!g_prev) g_prev = calloc(n, 4);
    if (P.trans_pending) {
        P.trans_pending = 0;
        transition(g_prev, shown, P.trans_type, P.trans_dur > 2000 ? 2000 : P.trans_dur, P.trans_chunk, s, P.trans_area);
    }
    host_blit(shown, 640 * s, 480 * s);
    memcpy(g_prev, shown, n * 4);
    P.update_needed = 0;
}

void player_update_stage(void) { stage_present(); }

/* headless slepen: zolang een script in `repeat while the stillDown` wacht, beweegt host_pump de muis in
 * stappen naar het doel en laat daarna los (zoals een echte gebruiker) */
static int g_hd_active, g_hd_x0, g_hd_y0, g_hd_x1, g_hd_y1, g_hd_step;
#define HD_STEPS 12
/* headless klikken in een wachtlus (`repeat while not the mouseDown`, D4's WantToStop): de frames lopen dan niet door,
 * dus na een poos wachten geeft host_pump de volgende geplande klik alvast (die telt daarna niet nog eens) */
static int (*g_hd_clicks)[3], g_hd_nclicks, g_hd_frame, g_hd_wait, g_hd_press;

void host_pump(void) {
    if (g_headless) {
        if (g_hd_active) {
            if (g_hd_step < HD_STEPS) {
                g_hd_step++;
                P.mouse_x = g_hd_x0 + (g_hd_x1 - g_hd_x0) * g_hd_step / HD_STEPS;
                P.mouse_y = g_hd_y0 + (g_hd_y1 - g_hd_y0) * g_hd_step / HD_STEPS;
                player_drag_update();
            } else {
                P.mouse_down = 0;
                g_hd_active = 0;
            }
        } else if (g_hd_press) {
            P.mouse_down = 0;   /* de klik uit de wachtlus: weer los */
            g_hd_press = 0;
        } else if (++g_hd_wait > 1000) {
            int k = -1;
            for (int i = 0; i < g_hd_nclicks; i++)
                if (g_hd_clicks[i][2] > g_hd_frame && (k < 0 || g_hd_clicks[i][2] < g_hd_clicks[k][2])) k = i;
            if (k >= 0) {
                fprintf(stderr, "[headless] klik %d,%d (frame %d) in een wachtlus\n", g_hd_clicks[k][0], g_hd_clicks[k][1], g_hd_clicks[k][2]);
                P.mouse_x = g_hd_clicks[k][0]; P.mouse_y = g_hd_clicks[k][1];
                P.mouse_down = 1;
                g_hd_clicks[k][2] = -1;
                g_hd_press = 1;
            }
            g_hd_wait = 0;
        }
        return;
    }
    host_events();
    if (player_drag_update() || P.update_needed) stage_present();   /* scripts die in een lus wachten, zien toch hun updateStage */
}

/* ------------------------------------------------------------------ opening overslaan */
/* Eerst het Ivanoff-logo (start.dxr), dan de intro (Intro.dxr: tekenfilm en titel).
 * - Een klik of toets tijdens het logo: start.dxr springt naar "SkipIntro" en wacht daar tot het deuntje klaar is (na een
 *   vroege klik, nog op tempo 1, seconden lang). De port stopt het deuntje en gaat meteen naar de intro, zoals frame 35.
 * - Is er al een opgeslagen spel, dan speelt het logo wel en slaat de start de intro over: meteen naar "IntroEnd" (zet de
 *   muisactie terug, stopt de muziek, gaat naar Magnus), zonder de frames ertussen te tonen en zonder zijn overgang van 2 s.
 * --intro speelt de intro toch altijd, --nointro slaat logo en intro altijd over. */
void lingo_do(const char *s);
static int g_skip = -1;   /* -1 de intro alleen met een opgeslagen spel, 0 nooit, 1 logo en intro altijd */

static int saves_exist(void) {   /* zoals SavedGamesExists in Magnus.dxr: een naam in MAGNUS.INI [Saved games] */
    char ini[PLAT_PATH], key[16], v[64];
    snprintf(ini, sizeof ini, "%s\\MAGNUS.INI", P.save_dir);
    for (int i = 1; i <= 16; i++) {
        snprintf(key, sizeof key, "GAME%d", i);
        if (ini_get(ini, "Saved games", key, "", v, sizeof v) > 0) return 1;
    }
    return 0;
}

/* logo overslaan, zodra start.dxr het toestaat (frame 1 zet mouseDownScript op 'go to "SkipIntro"', frame 35 leegt hem
 * en gaat naar de intro). 1 = gedaan, de klik zelf hoeft dan niet meer naar het spel. */
static int logo_tap(void) {
    Datum d = P.mouse_down_script;
    if (!P.mv || _stricmp(P.mv->name, "start") || d.t != T_STR || !strstr(d.u.s->s, "SkipIntro")) return 0;
    sound_stop(1);
    d_unref(P.mouse_down_script);
    P.mouse_down_script = d_str("");
    snprintf(P.pending_movie, sizeof P.pending_movie, "INTRO");
    P.pending_label[0] = 0;
    P.pending_frame = 1;
    return 1;
}

/* na elke tick; 1 = dit frame niet tonen, meteen de volgende tick */
static int skip_intro(void) {
    static int state;   /* 0 nog niet bekeken, 1 overslaan, 2 klaar */
    if (state == 2 || !P.mv) return 0;
    if (g_skip == 1 && logo_tap()) return 1;
    if (_stricmp(P.mv->name, "INTRO")) {
        if (_stricmp(P.mv->name, "start")) state = 2;
        return 0;
    }
    int end = movie_label(P.mv, "IntroEnd");
    if (!state) {
        state = end && (g_skip == 1 || (g_skip < 0 && saves_exist())) ? 1 : 2;
        if (state == 2) return 0;
        fprintf(stderr, "[start] intro overgeslagen\n");
        lingo_do("go to \"IntroEnd\"");
        return 1;
    }
    state = 2;
    if (P.frame != end) return 0;
    P.trans_pending = 0;   /* niet 2 s oplossen naar een leeg podium */
    sound_stop(2);         /* de muziek die frame 1 net startte; IntroEnd stopt hem ook, een tick later */
    return 1;
}

/* ------------------------------------------------------------------ start */
#ifdef __ANDROID__
void android_init(void);
#endif

int main(int argc, char **argv) {
#ifdef __ANDROID__
    android_init();   /* stderr naar skipper.log in de app-map */
#endif
    host_crash_init();
    const char *data = "extract", *movie = "start", *shot = NULL, *lang = NULL;
    int shot_frames = 0, scale = 0, fullscreen = 0, dumptex = 0, hd = 0, songs = 0;
    enum { MAXEV = 512 };   /* headless invoer van de opdrachtregel */
    static int clicks[MAXEV][3], drags[MAXEV][5], keys[MAXEV][3];
    int nclicks = 0, ndrags = 0, nkeys = 0, every = 0, dump = 0;
    struct { const char *name; int val, frame; } globs[16];
    int nglobs = 0;
    char bin[300] = "";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--movie") && i + 1 < argc) movie = argv[++i];
        else if (!strcmp(argv[i], "--bin") && i + 1 < argc) snprintf(bin, sizeof bin, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--lang") && i + 1 < argc) lang = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
        else if (!strcmp(argv[i], "--songs")) songs = 1;
        else if (!strcmp(argv[i], "--dumptex")) dumptex = 1;
        else if (!strcmp(argv[i], "--intro")) g_skip = 0;
        else if (!strcmp(argv[i], "--nointro")) g_skip = 1;
        else if (!strcmp(argv[i], "--hd") && i + 1 < argc) hd = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace")) vm_trace = 1;
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shot_frames = atoi(argv[++i]); shot = argv[++i]; g_headless = 1; }
        /* boven de grens de getallen toch overslaan: anders werd het laatste getal de datamap (fuzz.py met honderden klikken) */
        else if (!strcmp(argv[i], "--click") && i + 3 < argc) {
            if (nclicks < MAXEV) {
                clicks[nclicks][0] = atoi(argv[i + 1]); clicks[nclicks][1] = atoi(argv[i + 2]); clicks[nclicks++][2] = atoi(argv[i + 3]);
            } else fprintf(stderr, "[args] te veel --click\n");
            i += 3;
        }
        else if (!strcmp(argv[i], "--drag") && i + 5 < argc) {
            if (ndrags < MAXEV) {
                for (int k = 0; k < 5; k++) drags[ndrags][k] = atoi(argv[i + 1 + k]);
                ndrags++;
            } else fprintf(stderr, "[args] te veel --drag\n");
            i += 5;
        }
        else if (!strcmp(argv[i], "--key") && i + 3 < argc) {
            if (nkeys < MAXEV) {
                for (int k = 0; k < 3; k++) keys[nkeys][k] = atoi(argv[i + 1 + k]);
                nkeys++;
            } else fprintf(stderr, "[args] te veel --key\n");
            i += 3;
        }
        else if (!strcmp(argv[i], "--transtest") && i + 1 < argc) {   /* test: alle transities op t = 0.35 */
            static uint32_t a[640 * 480], b[640 * 480], o[640 * 480];
            for (int y = 0; y < 480; y++)
                for (int x = 0; x < 640; x++) {   /* oud: blauw met raster, nieuw: oranje met diagonalen */
                    a[y * 640 + x] = ((x % 80 < 4) || (y % 80 < 4)) ? 0xffffffffu : 0xff2040c0u | (uint32_t)(y * 255 / 480) << 8;
                    b[y * 640 + x] = ((x + y) % 60 < 6) ? 0xff000000u : 0xfff09020u | (uint32_t)(x * 255 / 640);
                }
            for (int ty = 1; ty <= 52; ty++) {
                char path[300];
                trans_frame(o, a, b, ty, 8, 0.35);
                snprintf(path, sizeof path, "%s/t%02d.bmp", argv[i + 1], ty);
                bmp_write(path, o, 640, 480);
            }
            return 0;
        }
        else if (!strcmp(argv[i], "--avi") && i + 3 < argc) {   /* test: één videoframe naar BMP */
            char ed[PLAT_PATH];   /* ook uit een exe met ingepakte bestanden: <exemap>\Video\x.avi */
            plat_exe_dir(ed, sizeof ed);
            pack_open(ed);
            Video *v = video_open(argv[i + 1]);
            if (!v) return 1;
            int fr = atoi(argv[i + 2]);
            bmp_write(argv[i + 3], video_frame(v, fr), video_width(v), video_height(v));
            printf("%dx%d, %d frames, %d ticks, audio %d samples\n", video_width(v), video_height(v), video_frames(v),
                   video_duration(v), video_audio(v) ? video_audio(v)->frames : 0);
            return 0;
        }
        else if (!strcmp(argv[i], "--global") && i + 3 < argc && nglobs < 16) {
            globs[nglobs].name = argv[++i];
            globs[nglobs].val = atoi(argv[++i]);
            globs[nglobs++].frame = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump")) dump = 1;
        else if (argv[i][0] != '-') data = argv[i];
    }
    if (scale < 0) scale = 0;
    /* spelbestanden: opgegeven map, extract\, <opslagmap>\data, cd-station of een cd-image; anders vragen */
    char full[PLAT_PATH], appdir[PLAT_PATH], binfound[600] = "";
    plat_user_dir(appdir, sizeof appdir);
    if (!disc_find_data(data, bin, appdir, full, sizeof full, binfound, sizeof binfound)) {
        char pick[PLAT_PATH];
        int ok = 0;
        while (!ok && host_pick_data(pick, sizeof pick)) {
            ok = disc_use(pick, appdir, full, sizeof full, binfound, sizeof binfound);
            if (!ok) host_message(UI("Daarin zijn de spelbestanden van Skipper & Skeeto niet gevonden.",
                                     "The game files of Skipper & Skeeto (Magnus & Myggen) are not in there."), 1);
        }
        if (!ok) {
            const char *msg = UI("De spelbestanden van Skipper & Skeeto zijn niet gevonden.\n\n"
                                 "Stop de cd in het cd-station, of start met:\n"
                                 "  skipper <map met Magnus.dxr>\n"
                                 "  skipper --bin <pad naar SKIPPER_1.BIN, .CUE, .ISO of .IMG>",
                                 "The game files of Skipper & Skeeto (Magnus & Myggen) were not found.\n\n"
                                 "Put the CD in the drive, or start with:\n"
                                 "  skipper <folder with Magnus.dxr>\n"
                                 "  skipper --bin <path to the CD image: .CUE, .BIN, .ISO or .IMG>");
            if (g_headless) fprintf(stderr, "%s\n", msg);
            else host_message(msg, 1);
            return 1;
        }
    }
    player_init(full);
    setup_save_dir();
    int nordic = disc_nordic(full), d4 = disc_d4(full);
    if (nordic) { setup_language(lang); set_title(); }
    else if (d4) g_title = "Magnus og Myggen";   /* de Deense cd van 1996: alleen Deens */
    else if (lang) fprintf(stderr, "[taal] --lang %s: deze cd is alleen Nederlands\n", lang);
    texpack_init(dumptex, hd);
    /* de opstartfilm zit alleen in een projector (START32.EXE, of die in SETUP.EXE): eenmalig naar de opslagmap halen,
     * per cd onder een eigen naam (de Nederlandse en de Scandinavische start.dxr verschillen in de taalkeuze) */
    snprintf(P.start_name, sizeof P.start_name, nordic ? "start_nordic" : "start");
    char sd[PLAT_PATH], sd2[PLAT_PATH];
    snprintf(sd, sizeof sd, "%s\\start.dxr", full);
    snprintf(sd2, sizeof sd2, "%s\\%s.dxr", P.save_dir, P.start_name);
    if (d4) {   /* Deense cd van 1996: geen start.dxr; de projector MAGNUS.EXE speelt de hoofdfilm "magnus" */
        snprintf(P.main_name, sizeof P.main_name, "magnus_d4");
        snprintf(sd2, sizeof sd2, "%s\\%s.dxr", P.save_dir, P.main_name);
        if (!vfs_exists(sd2)) {
            fprintf(stderr, "[disc] hoofdfilm uit MAGNUS.EXE halen ...\n");
            if (!disc_make_main_d4(full, sd2)) fprintf(stderr, "[disc] geen film in MAGNUS.EXE\n");
        }
        if (!strcmp(movie, "start")) movie = "magnus";
    } else if (!vfs_exists(sd) && !vfs_exists(sd2)) {
        fprintf(stderr, "[disc] start.dxr uit START32.EXE of SETUP.EXE halen ...\n");
        if (!disc_make_start(full, sd2)) fprintf(stderr, "[disc] start.dxr niet gevonden in %s\n", full);
    }
    if (binfound[0]) snprintf(bin, sizeof bin, "%s", binfound);
    if (!bin[0]) snprintf(bin, sizeof bin, "%s\\..\\SKIPPER_1.BIN", full);
    plat_full_path(bin, P.bin_path, sizeof P.bin_path);
    sound_headless = g_headless;
    sound_init();
    if (!g_headless && !host_open(scale, fullscreen)) { fprintf(stderr, "geen venster\n"); return 1; }
    if (songs) {   /* eerst het liedjesmenu (Liedjes.exe); de pijl terug start het spel */
        int songs_run(int (*clicks)[3], int nclicks, int frames, const char *shot);
        if (!songs_run(clicks, nclicks, shot_frames, shot) || g_headless) return 0;
    }
    player_start(movie);
    stage_present();
    uint32_t next = now_ms();
    int frames = 0;
    g_hd_clicks = clicks; g_hd_nclicks = nclicks;
    while (P.halted != 2) {
        g_hd_frame = frames; g_hd_wait = 0;
        if (!g_headless) {
            host_events();
            if (drain_input()) next = now_ms();
            if (player_drag_update()) stage_present();
        }
        uint32_t t = now_ms();
        if (g_headless || (int32_t)(t - next) >= 0) {
            for (int k = 0; k < nclicks; k++)
                if (frames == clicks[k][2] && !logo_tap()) {
                    P.mouse_x = clicks[k][0]; P.mouse_y = clicks[k][1];
                    P.mouse_down = 0;   /* headless: de knop geldt meteen als losgelaten */
                    player_mouse(clicks[k][0], clicks[k][1], 1, 0, 0);
                    player_mouse(clicks[k][0], clicks[k][1], 0, 1, 0);
                }
            for (int k = 0; k < nglobs; k++)
                if (frames == globs[k].frame) {   /* headless: global zetten (testen: gPoints enz.) */
                    Datum *g = global_ref(sym(globs[k].name));
                    d_unref(*g);
                    *g = d_int(globs[k].val);
                }
            for (int k = 0; k < nkeys; k++)
                if (frames == keys[k][2]) {   /* headless: toets (Mac-keyCode, teken) neer en los */
                    player_key(keys[k][0], keys[k][1], 1);
                    player_key(keys[k][0], keys[k][1], 0);
                }
            for (int k = 0; k < ndrags; k++)
                if (frames == drags[k][4]) {   /* headless: neer op (x1,y1), slepen naar (x2,y2), los */
                    P.mouse_x = drags[k][0]; P.mouse_y = drags[k][1];
                    P.mouse_down = 1;
                    g_hd_active = 1; g_hd_step = 0;
                    g_hd_x0 = drags[k][0]; g_hd_y0 = drags[k][1]; g_hd_x1 = drags[k][2]; g_hd_y1 = drags[k][3];
                    player_mouse(drags[k][0], drags[k][1], 1, 0, 0);   /* kan zelf slepen via host_pump */
                    g_hd_active = 0;
                    player_mouse(drags[k][2], drags[k][3], 0, 0, 0);
                    P.mouse_down = 0;
                    player_mouse(drags[k][2], drags[k][3], 0, 1, 0);
                }
            DBG_CHECK();
            int ms = player_tick();
            int skipped = skip_intro();   /* overgeslagen: dit frame niet tonen en niet wachten */
            if (g_headless) player_idle();   /* headless: één idle per frame */
            DBG_CHECK();
            if (!skipped) stage_present();
            frames++;
            if (shot && every && frames % every == 0) {
                char fn[300];
                snprintf(fn, sizeof fn, "%s.%04d.bmp", shot, frames);
                stage_screenshot(fn);
                fprintf(stderr, "[shot] %d %s frame %d\n", frames, P.mv ? P.mv->name : "?", P.frame);
            }
            if (skipped) ms = 0;
            next = t + (ms > 0 ? ms : 1);
            if (shot && frames >= shot_frames) break;
            if (g_headless && ms > 0) g_vclock += (uint32_t)ms;   /* headless: virtuele tijd, zo snel als het kan */
        } else {
            player_idle();
            if (P.update_needed) stage_present();
            plat_sleep(1);
        }
    }
    fprintf(stderr, "[einde] film %s frame %d na %d frames\n", P.mv ? P.mv->name : "?", P.frame, frames);
    if (dump) {
        void globals_dump(FILE *);
        globals_dump(stderr);
        /* podium en daarna de open vensters (dialogen), elk in hun eigen coördinaten */
        Window *w = NULL;
        for (Player *ctx = &P; ctx; ) {
            if (ctx != &P) fprintf(stderr, "-- venster %s (%s)\n", w->name, ctx->mv ? ctx->mv->name : "?");
            CP = ctx;
            for (int ch = 1; ch <= NCHAN; ch++) {
                Channel *c = &ctx->ch[ch];
                if (!c->member || !c->visible) continue;
                CastLib *cl;
                Member *m = movie_member(ctx->mv, c->lib, c->member, &cl);
                int l, t, r, b;
                sprite_rect(ch, &l, &t, &r, &b);
                fprintf(stderr, "[ch %2d] %d:%d %-14s type %d ink %d fg %d bg %d rect %d,%d-%d,%d stretch %d puppet %d script %d:%d\n", ch,
                        c->lib, c->member, m ? m->name : "?", m ? m->type : -1, c->ink, c->fore, c->back, l, t, r, b,
                        c->stretch, c->puppet, c->slib, c->script);
            }
            w = w ? w->next : P.windows;
            while (w && !(w->open && w->ctx && w->ctx->mv)) w = w->next;
            ctx = w ? w->ctx : NULL;
        }
        CP = &P;
    }
    if (shot) {
        stage_compose();
        stage_screenshot(shot);
        uint32_t *hdpx = stage_compose_hd();   /* texture pack in HD: ook dat beeld (<shot>.hd.bmp) */
        if (hdpx) {
            char fn[PLAT_PATH];
            snprintf(fn, sizeof fn, "%s.hd.bmp", shot);
            bmp_write(fn, hdpx, 640 * texpack_scale(), 480 * texpack_scale());
        }
        fprintf(stderr, "frame %d van %s -> %s\n", P.frame, P.mv ? P.mv->name : "?", shot);
    }
    return 0;
}
