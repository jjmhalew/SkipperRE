/* android.c - wat alleen Android nodig heeft:
 *   - de eerste start: Androids bestandskiezer (SkipperActivity.java) voor een image van de cd (BIN of ISO, gelezen via
 *     de file descriptor die de kiezer geeft: /proc/self/fd/N) of een map met een kopie van de cd (naar de app-map
 *     gekopieerd); daarna staan de spelbestanden in Android/data/io.github.jjmhalew.skipperre/files/data
 *   - meldingen in Androids eigen dialoog (SDL's berichtvenster laat in liggend formaat zijn knoppen onder het scherm)
 *   - stderr naar skipper.log in de app-map
 *   - aanraken: de eerste vinger is de muis; knoppen in de zwarte balken naast het beeld (of achter een menuknop als
 *     het scherm 4:3 is) voor wat op de pc onder F-toetsen zit: laden, opslaan, uitleg, stoppen, ondertitels en muziek
 *     (met aan / uit) en het volume (met de meter van het spel). */
#ifdef __ANDROID__
#include "dir.h"
#undef fopen
#include <SDL.h>
#include <jni.h>
#include <unistd.h>

/* ------------------------------------------------------------------ Java */
static jclass act_class(JNIEnv **env) {
    *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!*env || !act) return NULL;
    jclass c = (**env)->GetObjectClass(*env, act);
    (**env)->DeleteLocalRef(*env, act);
    return c;
}

static void clear_exc(JNIEnv *env) { if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env); }

/* SkipperActivity.dialog: 1 = b1, 0 = b2, -1 = b3 */
int android_dialog(const char *text, const char *b1, const char *b2, const char *b3) {
    JNIEnv *env;
    jclass c = act_class(&env);
    int r = -1;
    if (!c) return -1;
    jmethodID m = (*env)->GetStaticMethodID(env, c, "dialog", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I");
    if (m) {
        jstring s[4] = {(*env)->NewStringUTF(env, text), b1 ? (*env)->NewStringUTF(env, b1) : NULL,
                        b2 ? (*env)->NewStringUTF(env, b2) : NULL, b3 ? (*env)->NewStringUTF(env, b3) : NULL};
        r = (*env)->CallStaticIntMethod(env, c, m, s[0], s[1], s[2], s[3]);
        for (int i = 0; i < 4; i++) if (s[i]) (*env)->DeleteLocalRef(env, s[i]);
    }
    clear_exc(env);
    (*env)->DeleteLocalRef(env, c);
    return r;
}

static void progress(const char *text) {
    JNIEnv *env;
    jclass c = act_class(&env);
    if (!c) return;
    jmethodID m = (*env)->GetStaticMethodID(env, c, "progress", "(Ljava/lang/String;)V");
    if (m) {
        jstring s = text ? (*env)->NewStringUTF(env, text) : NULL;
        (*env)->CallStaticVoidMethod(env, c, m, s);
        if (s) (*env)->DeleteLocalRef(env, s);
    }
    clear_exc(env);
    (*env)->DeleteLocalRef(env, c);
}

/* SkipperActivity.pickGameData(kind, dest): 1 = image -> file descriptor; 2 = map, gekopieerd naar dest -> 0 */
static int java_pick(int kind, const char *dest) {
    JNIEnv *env;
    jclass c = act_class(&env);
    int r = -2;
    if (!c) return -2;
    jmethodID m = (*env)->GetStaticMethodID(env, c, "pickGameData", "(ILjava/lang/String;)I");
    if (m) {
        jstring d = (*env)->NewStringUTF(env, dest ? dest : "");
        r = (*env)->CallStaticIntMethod(env, c, m, kind, d);
        (*env)->DeleteLocalRef(env, d);
    }
    clear_exc(env);
    (*env)->DeleteLocalRef(env, c);
    return r;
}

/* host_pick_data op Android: kiezen, en meteen uitpakken of kopiëren; out = de map met de spelbestanden */
int android_pick_data(char *out, int n) {
    char user[PLAT_PATH];
    plat_user_dir(user, sizeof user);
    for (;;) {
        int k = android_dialog("Welkom bij Skipper & Skeeto in Pretpark!\n\n"
                               "De spelbestanden komen van je eigen cd. Kies een image van de cd (SKIPPER_1.BIN of een .iso), "
                               "of een map met een kopie van alle bestanden op de cd. Ze worden eenmalig in de app gezet "
                               "(ongeveer 200 MB).",
                               "Cd-image", "Map van de cd", "Stoppen");
        if (k < 0) return 0;
        if (k == 1) {
            int fd = java_pick(1, NULL);
            if (fd == -1) continue;
            if (fd < 0) { android_dialog("Dat bestand kon niet worden geopend.", "OK", NULL, NULL); continue; }
            char img[64], bin[PLAT_PATH] = "";
            snprintf(img, sizeof img, "/proc/self/fd/%d", fd);
            progress("Spelbestanden uitpakken...");
            int ok = disc_use(img, user, out, n, bin, sizeof bin);
            progress(NULL);
            close(fd);
            if (ok) return 1;
            android_dialog("In dat bestand staan de spelbestanden van Skipper & Skeeto niet. Kies het image van de cd "
                           "(SKIPPER_1.BIN, niet het .cue-bestand) of een .iso.", "OK", NULL, NULL);
        } else {
            char part[PLAT_PATH], data[PLAT_PATH];
            snprintf(part, sizeof part, "%s/data.part", user);
            snprintf(data, sizeof data, "%s/data", user);
            plat_mkdir(part);
            progress("Spelbestanden kopiëren...");
            int r = java_pick(2, part);
            progress(NULL);
            if (r == -1) continue;
            snprintf(out, n, "%s", part);
            if (r == 0 && disc_use(part, user, out, n, NULL, 0)) {
                if (rename(part, data) == 0) snprintf(out, n, "%s", data);
                return 1;
            }
            android_dialog("In die map staan de spelbestanden van Skipper & Skeeto niet (Magnus.dxr ontbreekt).", "OK", NULL, NULL);
        }
    }
}

void android_init(void) {
    char log[PLAT_PATH];
    plat_user_dir(log, sizeof log);
    snprintf(log + strlen(log), sizeof log - strlen(log), "/skipper.log");
    freopen(log, "w", stderr);
    setvbuf(stderr, NULL, _IOLBF, 0);
}

/* ------------------------------------------------------------------ aanraken */
/* De knoppen in de kleuren van de onderbalk van het spel: cyaan met een donkerblauwe rand, ingedrukt lichtcyaan;
 * een schakelaar die aan staat is groen zoals de EXIT-knop. Ondertitels en muziek laten zien of ze aan of uit staan
 * (de globals van het spel zelf), het volume is één paneel met de rode meter van het spel en knoppen - en +. */
void host_sdl_to_stage(float wx, float wy, int *x, int *y);   /* host_sdl.c */
enum { K_KEY, K_TOGGLE, K_DOWN, K_UP };
typedef struct Btn { const char *label; int code, ch, kind; const char *global; SDL_Rect r; SDL_Texture *tex; int tw, th; } Btn;
static Btn g_btn[] = {
    {"Laden", 96, 0, K_KEY}, {"Opslaan", 97, 0, K_KEY}, {"Uitleg", 122, 0, K_KEY}, {"Stoppen", 53, 27, K_KEY},
    {"Tekst", 120, 0, K_TOGGLE, "gSubTextOn"}, {"Muziek", 99, 0, K_TOGGLE, "gBkgSoundOn"},
    {"Zachter", 125, 0, K_DOWN}, {"Harder", 126, 0, K_UP},
};
#define NBTN (int)(sizeof g_btn / sizeof *g_btn)
static int g_side;               /* 1: knoppen in de zijbalken, 0: achter de menuknop */
static int g_menu_open;
static SDL_Rect g_menu;          /* de menuknop (alleen als g_side == 0) */
static SDL_Rect g_vol;           /* het volumepaneel (alleen als g_side) */
static SDL_Texture *g_menu_tex, *g_on_tex, *g_off_tex, *g_vol_tex;
static int g_menu_tw, g_on_tw, g_off_tw, g_vol_tw, g_small_th;
static int g_pressed = -1;       /* knop onder de vinger */
static SDL_FingerID g_mouse_finger = -1, g_btn_finger = -1;
static int g_label_h;

typedef struct Rgb { Uint8 r, g, b; } Rgb;
static const Rgb C_FACE = {47, 178, 210}, C_LIGHT = {184, 231, 231}, C_EDGE = {0, 0, 128}, C_CREAM = {255, 251, 240},
                 C_ON = {107, 221, 111}, C_ON_EDGE = {0, 128, 0}, C_RED = {255, 0, 0};

/* label als witte tekst op doorzichtig (text_raster tekent zwart op wit); kleuren met SDL_SetTextureColorMod */
static SDL_Texture *label(SDL_Renderer *r, const char *s, int px, int *tw, int *th) {
    Text t;
    memset(&t, 0, sizeof t);
    t.text = (char *)s;
    t.font_size = px;
    t.style = 1;
    snprintf(t.font, sizeof t.font, "Arial");
    t.w = px * (int)strlen(s) + 8;
    t.h = px * 3 / 2;
    uint32_t *img = malloc(sizeof(uint32_t) * t.w * t.h);
    text_raster(&t, img, t.w, t.h, 0xff000000u, 0);
    int maxx = 0;
    for (int i = 0; i < t.w * t.h; i++) {
        int on = (img[i] & 0xffffff) == 0;
        img[i] = on ? 0xffffffffu : 0;
        if (on && i % t.w > maxx) maxx = i % t.w;
    }
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, t.w, t.h);
    if (tex) { SDL_UpdateTexture(tex, NULL, img, t.w * 4); SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND); }
    free(img);
    *tw = maxx + 2; *th = t.h;
    return tex;
}

static void relabel(SDL_Texture **tex, SDL_Renderer *r, const char *s, int px, int *tw, int *th) {
    if (*tex) SDL_DestroyTexture(*tex);
    *tex = label(r, s, px, tw, th);
}

/* 1 / 0 voor een global van het spel, -1 als die er (nog) niet is */
static int game_flag(const char *name) {
    Datum *g = global_find(sym(name));
    return g ? d_toint(*g) != 0 : -1;
}

static void layout(SDL_Renderer *r) {
    int ow, oh;
    SDL_GetRendererOutputSize(r, &ow, &oh);
    int sw = ow, sh = ow * 3 / 4;
    if (sh > oh) { sh = oh; sw = oh * 4 / 3; }
    int bar = (ow - sw) / 2;
    g_side = bar >= oh / 9;
    int lh;
    if (g_side) {   /* links laden, opslaan, uitleg, stoppen; rechts tekst, muziek en het volumepaneel (twee plaatsen) */
        int bw = bar * 85 / 100, bh = oh / 5;
        if (bh > bw) bh = bw;
        int gap = (oh - 4 * bh) / 5;
        for (int i = 0; i < 6; i++) {
            int col = i / 4, row = i % 4;
            g_btn[i].r = (SDL_Rect){col ? ow - bar + (bar - bw) / 2 : (bar - bw) / 2, gap + row * (bh + gap), bw, bh};
        }
        g_vol = (SDL_Rect){ow - bar + (bar - bw) / 2, gap + 2 * (bh + gap), bw, 2 * bh + gap};
        int pad = bw / 14, kw = (bw - 3 * pad) / 2, kh = g_vol.h * 36 / 100;
        g_btn[6].r = (SDL_Rect){g_vol.x + pad, g_vol.y + g_vol.h - pad - kh, kw, kh};
        g_btn[7].r = (SDL_Rect){g_vol.x + 2 * pad + kw, g_vol.y + g_vol.h - pad - kh, kw, kh};
        lh = bh / 4;
        if (lh > bw / 6) lh = bw / 6;
    } else {        /* een menuknop linksboven; open: een rij knoppen bovenin */
        int m = oh / 11, x0 = (ow - sw) / 2, y0 = (oh - sh) / 2;
        g_menu = (SDL_Rect){x0 + m / 4, y0 + m / 4, m, m};
        int bw = (sw - m * 3 / 2) / NBTN, bh = m * 5 / 4;
        for (int i = 0; i < NBTN; i++) g_btn[i].r = (SDL_Rect){x0 + m * 3 / 2 + i * bw, y0 + m / 4, bw - 4, bh};
        lh = bh / 4;
        if (lh > bw / 6) lh = bw / 6;
    }
    if (lh < 8) lh = 8;
    if (lh != g_label_h) {   /* labels opnieuw op deze grootte */
        g_label_h = lh;
        for (int i = 0; i < NBTN; i++) relabel(&g_btn[i].tex, r, g_btn[i].label, lh, &g_btn[i].tw, &g_btn[i].th);
        int th, sl = lh * 3 / 4 < 8 ? 8 : lh * 3 / 4;
        relabel(&g_menu_tex, r, "Menu", lh, &g_menu_tw, &th);
        relabel(&g_vol_tex, r, "Volume", lh, &g_vol_tw, &th);
        relabel(&g_on_tex, r, "aan", sl, &g_on_tw, &g_small_th);
        relabel(&g_off_tex, r, "uit", sl, &g_off_tw, &g_small_th);
    }
}

static void fill(SDL_Renderer *r, const SDL_Rect *b, Rgb c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_RenderFillRect(r, b);
}

/* vlak met een rand (dikte naar de grootte) en een lichte binnenrand links- en bovenaan */
static void face(SDL_Renderer *r, const SDL_Rect *b, Rgb c, Rgb edge, int bevel) {
    int e = b->w / 50 + 2;
    fill(r, b, edge);
    SDL_Rect in = {b->x + e, b->y + e, b->w - 2 * e, b->h - 2 * e};
    fill(r, &in, c);
    if (bevel) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 110);
        SDL_Rect t = {in.x, in.y, in.w, e / 2 + 1}, l = {in.x, in.y, e / 2 + 1, in.h};
        SDL_RenderFillRect(r, &t);
        SDL_RenderFillRect(r, &l);
    }
}

/* tekst gecentreerd op (cx, cy) */
static void text_at(SDL_Renderer *r, SDL_Texture *tex, int tw, int th, int cx, int cy, int maxw, Rgb c) {
    if (!tex) return;
    SDL_Rect s = {0, 0, tw, th}, d = {cx - tw / 2, cy - th / 2, tw, th};
    if (d.w > maxw) { s.w = d.w = maxw; d.x = cx - maxw / 2; }
    SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
    SDL_RenderCopy(r, tex, &s, &d);
}

static void draw_button(SDL_Renderer *r, Btn *b, int down) {
    const SDL_Rect *q = &b->r;
    int st = b->kind == K_TOGGLE ? game_flag(b->global) : -1;
    Rgb c = down ? C_LIGHT : st == 1 ? C_ON : C_FACE, ink = down ? C_EDGE : C_CREAM;
    face(r, q, c, st == 1 && !down ? C_ON_EDGE : C_EDGE, !down);
    int cx = q->x + q->w / 2, cy = q->y + q->h / 2;
    if (g_side && (b->kind == K_DOWN || b->kind == K_UP)) {   /* - en + als dikke strepen */
        int len = (q->w < q->h ? q->w : q->h) * 45 / 100, th = len / 5 + 1;
        SDL_Rect h = {cx - len / 2, cy - th / 2, len, th}, v = {cx - th / 2, cy - len / 2, th, len};
        fill(r, &h, ink);
        if (b->kind == K_UP) fill(r, &v, ink);
        return;
    }
    if (st < 0) { text_at(r, b->tex, b->tw, b->th, cx, cy, q->w - 6, ink); return; }
    /* schakelaar: naam en daaronder aan / uit */
    text_at(r, b->tex, b->tw, b->th, cx, q->y + q->h * 38 / 100, q->w - 6, ink);
    if (st) text_at(r, g_on_tex, g_on_tw, g_small_th, cx, q->y + q->h * 72 / 100, q->w - 6, ink);
    else text_at(r, g_off_tex, g_off_tw, g_small_th, cx, q->y + q->h * 72 / 100, q->w - 6, ink);
}

/* volumepaneel: lichtcyaan zoals het vak met de boomstam, "Volume" en de oplopende rode streepjes van het spel */
static void draw_volume(SDL_Renderer *r) {
    face(r, &g_vol, C_LIGHT, C_EDGE, 0);
    int pad = g_vol.w / 14, top = g_btn[6].r.y - pad;   /* ruimte boven de knoppen */
    int ty = g_vol.y + pad + g_label_h * 3 / 4;
    text_at(r, g_vol_tex, g_vol_tw, g_label_h * 3 / 2, g_vol.x + g_vol.w / 2, ty, g_vol.w - 6, C_EDGE);
    int my0 = ty + g_label_h, mh = top - my0, mw = g_vol.w - 2 * pad;
    if (mh < 6) return;
    int lvl = P.sound_level, n = 7, sw = mw / n, gap = sw / 5 + 1;
    for (int i = 0; i < n; i++) {
        int h = mh * (i + 2) / (n + 1);
        SDL_Rect s = {g_vol.x + pad + i * sw, my0 + mh - h, sw - gap, h};
        if (i < lvl) fill(r, &s, C_RED);
        else {
            SDL_SetRenderDrawColor(r, C_EDGE.r, C_EDGE.g, C_EDGE.b, 255);
            SDL_RenderDrawRect(r, &s);
        }
    }
}

void touch_draw(SDL_Renderer *r) {
    layout(r);   /* in renderer-pixels, ook in de balken */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    if (g_side) draw_volume(r);
    if (g_side || g_menu_open)
        for (int i = 0; i < NBTN; i++) draw_button(r, &g_btn[i], g_pressed == i);
    if (!g_side) {
        face(r, &g_menu, g_menu_open ? C_LIGHT : C_FACE, C_EDGE, !g_menu_open);
        text_at(r, g_menu_tex, g_menu_tw, g_label_h * 3 / 2, g_menu.x + g_menu.w / 2, g_menu.y + g_menu.h / 2, g_menu.w - 4,
                g_menu_open ? C_EDGE : C_CREAM);
    }
}

static int inside(const SDL_Rect *b, int x, int y) { return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h; }

/* vingers: knoppen, anders de eerste vinger als muis. 1 = afgehandeld */
int touch_event(SDL_Event *e, SDL_Window *win, SDL_Renderer *r) {
    if (e->type != SDL_FINGERDOWN && e->type != SDL_FINGERUP && e->type != SDL_FINGERMOTION) return 0;
    int ow, oh, ww, wh;
    SDL_GetRendererOutputSize(r, &ow, &oh);
    SDL_GetWindowSize(win, &ww, &wh);
    int px = (int)(e->tfinger.x * ow), py = (int)(e->tfinger.y * oh);
    int x, y;
    host_sdl_to_stage(e->tfinger.x * ww, e->tfinger.y * wh, &x, &y);
    SDL_FingerID f = e->tfinger.fingerId;
    if (e->type == SDL_FINGERDOWN) {
        if (!g_side && inside(&g_menu, px, py)) { g_menu_open = !g_menu_open; return 1; }
        if (g_side || g_menu_open)
            for (int i = 0; i < NBTN; i++)
                if (inside(&g_btn[i].r, px, py)) {
                    g_pressed = i;
                    g_btn_finger = f;
                    input_push(3, 0, 0, g_btn[i].code, g_btn[i].ch);
                    return 1;
                }
        if (g_mouse_finger != -1 || x < 0 || x >= 640 || y < 0 || y >= 480) return 1;
        g_menu_open = 0;
        g_mouse_finger = f;
        P.mouse_x = x; P.mouse_y = y; P.mouse_down = 1;
        input_push(1, x, y, 0, 0);
        return 1;
    }
    if (f == g_btn_finger) {
        if (e->type == SDL_FINGERUP) {
            input_push(4, 0, 0, g_btn[g_pressed].code, g_btn[g_pressed].ch);
            g_pressed = -1;
            g_btn_finger = -1;
            if (!g_side) g_menu_open = 0;
        }
        return 1;
    }
    if (f != g_mouse_finger) return 1;
    x = x < 0 ? 0 : x > 639 ? 639 : x;
    y = y < 0 ? 0 : y > 479 ? 479 : y;
    P.mouse_x = x; P.mouse_y = y;
    if (e->type == SDL_FINGERUP) {
        P.mouse_down = 0;
        input_push(2, x, y, 0, 0);
        g_mouse_finger = -1;
    }
    return 1;
}
#endif
