/* host_sdl.c - het venster buiten Windows (Linux, Android) met SDL2: tonen via een SDL-renderer (logische grootte
 * 640x480, zwarte randen, scherpe pixels), muis / aanraken / toetsen, de cursors van het spel, meldingen, de
 * bestandskiezer voor de spelbestanden en afdrukken als PNG. Op Windows: host_win.c. */
#ifndef _WIN32
#include "dir.h"
#undef fopen
#include <SDL.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#ifdef __GLIBC__
#include <execinfo.h>
#endif
#include <time.h>
#include "stb/stb_image_write.h"

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;

#ifdef __ANDROID__
int android_dialog(const char *text, const char *b1, const char *b2, const char *b3);   /* android.c */
int android_pick_data(char *out, int n);
void touch_draw(SDL_Renderer *r);
int touch_event(SDL_Event *e, SDL_Window *win, SDL_Renderer *r);
#endif

/* taal van het systeem ("da", "sv", ...): SDL leest LANG / LC_ALL, op Android de taal van het toestel */
void host_locale(char *out, int n) {
    out[0] = 0;
    SDL_Locale *l = SDL_GetPreferredLocales();
    if (l && l[0].language) snprintf(out, n, "%s", l[0].language);
    SDL_free(l);
}

void host_message(const char *text, int warn) {
    fprintf(stderr, "%s\n", text);
#ifdef __ANDROID__
    (void)warn;
    if (!g_headless) android_dialog(text, "OK", NULL, NULL);
    return;
#endif
    if (!g_headless)
        SDL_ShowSimpleMessageBox(warn ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION, "Skipper & Skeeto", text, g_win);
}

/* MMSYS.LoadSaveGame (GetGameNumber in Magnus.dxr) wordt door het spel nergens aangeroepen: opslaan en laden gaan via
 * zijn eigen scherm Mmdlg3. Op Windows staat er toch een dialoog achter; hier niets. */
int ld_save_game(int is_load) { (void)is_load; return 0; }

/* Afdrukken (tekenspel): de tekening als PNG in de opslagmap */
void host_print(const uint32_t *px, int w, int h, int landscape, const char *name) {
    (void)landscape;
    char path[PLAT_PATH];
    snprintf(path, sizeof path, "%s/tekening-%u.png", P.save_dir, (unsigned)time(NULL));
    for (char *q = path; *q; q++) if (*q == '\\') *q = '/';
    uint8_t *rgb = malloc((size_t)w * h * 3);
    for (int i = 0; i < w * h; i++) { rgb[3 * i] = px[i] >> 16; rgb[3 * i + 1] = px[i] >> 8; rgb[3 * i + 2] = px[i]; }
    int ok = stbi_write_png(path, w, h, 3, rgb, w * 3);
    free(rgb);
    char msg[PLAT_PATH + 100];
    snprintf(msg, sizeof msg, ok ? UI("%s is bewaard als\n%s", "%s was saved as\n%s")
                                 : UI("%s kon niet worden bewaard als\n%s", "%s could not be saved as\n%s"), name, path);
    host_message(msg, !ok);
}

/* ------------------------------------------------------------------ bestandskiezer */

static int ask(const char *text, const char *yes, const char *no) {
    const SDL_MessageBoxButtonData b[2] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yes}, {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, no}};
    SDL_MessageBoxData d = {SDL_MESSAGEBOX_INFORMATION, g_win, "Skipper & Skeeto", text, 2, b, NULL};
    int r = 0;
    if (SDL_ShowMessageBox(&d, &r)) return 0;
    return r;
}

int host_pick_data(char *out, int n) {
    if (g_headless) return 0;
#ifdef __ANDROID__
    return android_pick_data(out, n);
#else
    if (!ask(UI("De spelbestanden van Skipper & Skeeto zijn niet gevonden.\n\n"
                "Kies hierna een image van de cd (.cue, .bin, .iso of .img), of Magnus.dxr op de gemounte cd of in een "
                "map met de bestanden van de cd. Een image wordt eenmalig uitgepakt naar ~/.local/share/SkipperRE/data.",
                "The game files of Skipper & Skeeto (Magnus & Myggen) were not found.\n\n"
                "Choose an image of the CD next (.cue, .bin, .iso or .img), or Magnus.dxr on the mounted CD or in a "
                "folder with the files of the CD. An image is unpacked once to ~/.local/share/SkipperRE/data."),
             UI("Kiezen", "Choose"), UI("Stoppen", "Quit")))
        return 0;
#define EXTS "*.cue *.CUE *.bin *.BIN *.iso *.ISO *.img *.IMG *.ccd *.CCD Magnus.dxr MAGNUS.DXR"
    const char *cmds[] = {
        ui_nl() ? "zenity --file-selection --title='Skipper & Skeeto: cd-image of Magnus.dxr' "
                  "--file-filter='Cd-image of Magnus.dxr | " EXTS "' --file-filter='Alle bestanden | *' 2>/dev/null"
                : "zenity --file-selection --title='Skipper & Skeeto: CD image or Magnus.dxr' "
                  "--file-filter='CD image or Magnus.dxr | " EXTS "' --file-filter='All files | *' 2>/dev/null",
        "kdialog --title 'Skipper & Skeeto' --getopenfilename . '" EXTS "' 2>/dev/null",
    };
#undef EXTS
    for (int i = 0; i < 2; i++) {
        char probe[64];
        snprintf(probe, sizeof probe, "command -v %s >/dev/null 2>&1", i ? "kdialog" : "zenity");
        if (system(probe) != 0) continue;
        FILE *p = popen(cmds[i], "r");
        if (!p) continue;
        char line[PLAT_PATH] = "";
        int got = fgets(line, sizeof line, p) != NULL;
        pclose(p);
        if (!got) return 0;
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) return 0;
        char *sl = strrchr(line, '/');
        if (sl && !strcasecmp(sl + 1, "Magnus.dxr")) *sl = 0;   /* de map */
        snprintf(out, n, "%s", line);
        return 1;
    }
    host_message(UI("Er is geen bestandskiezer (zenity of kdialog) gevonden. Start skipper met de map of het image:\n"
                    "  skipper <map met Magnus.dxr>\n  skipper --bin <SKIPPER_1.CUE, .BIN, .ISO of .IMG>",
                    "No file chooser (zenity or kdialog) was found. Start skipper with the folder or the image:\n"
                    "  skipper <folder with Magnus.dxr>\n  skipper --bin <CD image: .CUE, .BIN, .ISO or .IMG>"), 1);
    return 0;
#endif
}

/* ------------------------------------------------------------------ tonen */
/* Het beeld staat in g_dst (renderer-pixels): zo groot mogelijk in 4:3 (stage_fit). Geen SDL_RenderSetLogicalSize:
 * die schaalt ook aanraakposities naar het beeld en klemt ze erin, en dan zijn knoppen in de zwarte balken niet te raken. */
static SDL_Rect g_dst = {0, 0, 640, 480};

static void place(void) {
    int ow, oh, l, t, w, h;
    if (SDL_GetRendererOutputSize(g_ren, &ow, &oh) || ow <= 0 || oh <= 0) return;
    stage_fit(ow, oh, &l, &t, &w, &h);
    g_dst = (SDL_Rect){l, t, w, h};
}

/* vensterpunt (muis; vinger x venstergrootte) -> podiumcoördinaat */
void host_sdl_to_stage(float wx, float wy, int *x, int *y) {
    int ww, wh, ow, oh;
    SDL_GetWindowSize(g_win, &ww, &wh);
    SDL_GetRendererOutputSize(g_ren, &ow, &oh);
    float px = ww > 0 ? wx * ow / ww : wx, py = wh > 0 ? wy * oh / wh : wy;
    *x = (int)floorf((px - g_dst.x) * 640.0f / g_dst.w);
    *y = (int)floorf((py - g_dst.y) * 480.0f / g_dst.h);
}

#ifdef __ANDROID__
/* Android heeft geen muisaanwijzer: met een controller tekenen we de cursor van het spel zelf (of een pijl) */
static void draw_soft_cursor(void) {
    static SDL_Texture *tex;
    static int key = -1, tw, th, thx, thy;
    Datum c = cursor_wanted();
    int k = c.t == T_LIST && c.u.l->n >= 1 ? d_toint(c.u.l->v[0]) : c.t == T_INT && c.u.i == 200 ? -2 : 0;
    if (k == -2) return;
    if (k != key) {
        if (tex) SDL_DestroyTexture(tex);
        tex = NULL;
        key = k;
        uint32_t *img = k ? cursor_image(c, 1, &tw, &th, &thx, &thy) : NULL;
        if (!img) {   /* pijl van 11x17 */
            static const char *arrow[17] = {"X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X", "X.......X",
                                            "X........X", "X.....XXXXX", "X..X..X", "X.X X..X", "XX  X..X", "X    X..X", "     X..X", "      XX"};
            tw = 11; th = 17; thx = thy = 0;
            img = calloc(tw * th, 4);
            for (int y = 0; y < th; y++)
                for (int x = 0; arrow[y][x]; x++)
                    img[y * tw + x] = arrow[y][x] == 'X' ? 0xff000000u : arrow[y][x] == '.' ? 0xffffffffu : 0;
        }
        tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw, th);
        if (tex) { SDL_UpdateTexture(tex, NULL, img, tw * 4); SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND); }
        free(img);
    }
    if (!tex) return;
    SDL_Rect r = {g_dst.x + (P.mouse_x - thx) * g_dst.w / 640, g_dst.y + (P.mouse_y - thy) * g_dst.h / 480,
                  tw * g_dst.w / 640, th * g_dst.h / 480};
    SDL_RenderCopy(g_ren, tex, NULL, &r);
}
#endif

static const uint32_t *g_shown;   /* laatst getoonde beeld (venster blootgelegd) */
static int g_shown_w, g_shown_h, g_tex_w = 640, g_tex_h = 480;

void host_blit(const uint32_t *px, int w, int h) {
    if (g_headless || !g_ren) return;
    g_shown = px; g_shown_w = w; g_shown_h = h;
    if (w != g_tex_w || h != g_tex_h) {   /* texture pack in HD: grotere texture, lineair verkleinen */
        SDL_DestroyTexture(g_tex);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, w > 640 ? "1" : "0");
        g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        g_tex_w = w; g_tex_h = h;
    }
    SDL_UpdateTexture(g_tex, NULL, px, w * 4);
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    place();
    SDL_RenderCopy(g_ren, g_tex, NULL, &g_dst);
#ifdef __ANDROID__
    if (pad_recent()) draw_soft_cursor();
    touch_draw(g_ren);
#endif
    SDL_RenderPresent(g_ren);
}

static void toggle_fullscreen(void) {
    int fs = SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(g_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

/* ------------------------------------------------------------------ cursors */
static SDL_Cursor *g_cur_arrow, *g_cur_wait, *g_cur_now;
static SDL_Cursor *g_cur_cache[64];
static int g_cur_key[64], g_cur_scale[64], g_ncur;
static Movie *g_cur_mv[64];   /* cursorleden van het podium of van een dialoogvenster */

static int cursor_scale(void) {   /* in vensterpunten, zoals de systeemcursor */
    int ww, wh, ow, oh;
    SDL_GetWindowSize(g_win, &ww, &wh);
    SDL_GetRendererOutputSize(g_ren, &ow, &oh);
    int s = ow > 0 ? (g_dst.w * ww / ow + 320) / 640 : 1;
    return s < 1 ? 1 : s > 8 ? 8 : s;
}

static SDL_Cursor *bitmap_cursor(Datum lst) {
    int num = d_toint(lst.u.l->v[0]), mask = lst.u.l->n > 1 ? d_toint(lst.u.l->v[1]) : 0;
    int key = num << 16 | (mask & 0xffff), s = cursor_scale();
    for (int i = 0; i < g_ncur; i++) if (g_cur_key[i] == key && g_cur_scale[i] == s && g_cur_mv[i] == cursor_movie()) return g_cur_cache[i];
    int cw, ch, hx, hy;
    uint32_t *img = cursor_image(lst, s, &cw, &ch, &hx, &hy);
    if (!img) return g_cur_arrow;
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormatFrom(img, cw, ch, 32, cw * 4, SDL_PIXELFORMAT_ARGB8888);
    SDL_Cursor *c = sf ? SDL_CreateColorCursor(sf, hx, hy) : NULL;
    if (sf) SDL_FreeSurface(sf);
    free(img);
    if (!c) return g_cur_arrow;
    if (g_ncur == 64) {
        SDL_SetCursor(g_cur_arrow);
        for (int i = 0; i < g_ncur; i++) SDL_FreeCursor(g_cur_cache[i]);
        g_ncur = 0;
    }
    g_cur_key[g_ncur] = key; g_cur_scale[g_ncur] = s; g_cur_mv[g_ncur] = cursor_movie(); g_cur_cache[g_ncur++] = c;
    return c;
}

static void update_cursor(void) {
    Datum c = cursor_wanted();
    SDL_Cursor *want = g_cur_arrow;
    int show = 1;
    if (c.t == T_LIST && c.u.l->n >= 1) want = bitmap_cursor(c);
    else if (c.t == T_INT && c.u.i == 4) want = g_cur_wait;
    else if (c.t == T_INT && c.u.i == 200) show = 0;
    if (want && want != g_cur_now) { SDL_SetCursor(want); g_cur_now = want; }
    SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
}

/* ------------------------------------------------------------------ toetsen: SDL -> Mac keyCode */
static int mac_keycode(SDL_Keycode k) {
    static const struct { int sdl, mac; } map[] = {
        {SDLK_ESCAPE, 53}, {SDLK_AC_BACK, 53}, {SDLK_RETURN, 36}, {SDLK_KP_ENTER, 36}, {SDLK_TAB, 48}, {SDLK_SPACE, 49},
        {SDLK_BACKSPACE, 51}, {SDLK_DELETE, 117}, {SDLK_LEFT, 123}, {SDLK_RIGHT, 124}, {SDLK_DOWN, 125}, {SDLK_UP, 126},
        {SDLK_HOME, 115}, {SDLK_END, 119}, {SDLK_PAGEUP, 116}, {SDLK_PAGEDOWN, 121}, {SDLK_F1, 122}, {SDLK_F2, 120},
        {SDLK_F3, 99}, {SDLK_F4, 118}, {SDLK_F5, 96}, {SDLK_F6, 97}, {SDLK_F7, 98}, {SDLK_F8, 100}, {SDLK_F9, 101},
        {SDLK_F10, 109}, {SDLK_F11, 103}, {SDLK_F12, 111}, {SDLK_KP_PLUS, 69}, {SDLK_KP_MINUS, 78}, {SDLK_EQUALS, 24},
        {SDLK_MINUS, 27}, {SDLK_PLUS, 24},
        {'a', 0}, {'s', 1}, {'d', 2}, {'f', 3}, {'h', 4}, {'g', 5}, {'z', 6}, {'x', 7}, {'c', 8}, {'v', 9},
        {'b', 11}, {'q', 12}, {'w', 13}, {'e', 14}, {'r', 15}, {'y', 16}, {'t', 17}, {'1', 18}, {'2', 19},
        {'3', 20}, {'4', 21}, {'6', 22}, {'5', 23}, {'9', 25}, {'7', 26}, {'8', 28}, {'0', 29}, {'o', 31},
        {'u', 32}, {'i', 34}, {'p', 35}, {'l', 37}, {'j', 38}, {'k', 40}, {'n', 45}, {'m', 46},
        {SDLK_KP_0, 82}, {SDLK_KP_1, 83}, {SDLK_KP_2, 84}, {SDLK_KP_3, 85}, {SDLK_KP_4, 86},
        {SDLK_KP_5, 87}, {SDLK_KP_6, 88}, {SDLK_KP_7, 89}, {SDLK_KP_8, 91}, {SDLK_KP_9, 92},
    };
    if (k >= 'A' && k <= 'Z') k += 32;
    for (size_t i = 0; i < sizeof map / sizeof *map; i++) if (map[i].sdl == (int)k) return map[i].mac;
    return 0;
}

/* tekens uit SDL_TEXTINPUT (UTF-8) -> Windows-1252, zoals het spel ze kent */
static int utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    int c = *p++;
    if (c >= 0xc0 && (*p & 0xc0) == 0x80) {
        int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
        c &= 0x3f >> n;
        for (int i = 0; i < n && (*p & 0xc0) == 0x80; i++) c = c << 6 | (*p++ & 0x3f);
    }
    *s = (const char *)p;
    return c;
}

static int to_cp1252(int u) {
    static const unsigned short hi[32] = {0x20ac, 0, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160,
                                          0x2039, 0x152, 0, 0x17d, 0, 0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013,
                                          0x2014, 0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0, 0x17e, 0x178};
    if (u < 0x80 || (u >= 0xa0 && u < 0x100)) return u;
    for (int i = 0; i < 32; i++) if (hi[i] == u) return 0x80 + i;
    return 0;
}

/* ------------------------------------------------------------------ events */
static int g_text_on = -1;
static unsigned g_devchanges;   /* een controller kwam of ging */

void host_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
#ifdef __ANDROID__
        if (touch_event(&e, g_win, g_ren)) continue;   /* vingers: muis en knoppen (android.c) */
#endif
        switch (e.type) {
        case SDL_QUIT: P.halted = 2; break;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED: g_devchanges++; break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_EXPOSED)
                if (g_shown) host_blit(g_shown, g_shown_w, g_shown_h);
            break;
        case SDL_MOUSEMOTION:
            host_sdl_to_stage((float)e.motion.x, (float)e.motion.y, &P.mouse_x, &P.mouse_y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            int down = e.type == SDL_MOUSEBUTTONDOWN, right = e.button.button == SDL_BUTTON_RIGHT;
            if (e.button.button != SDL_BUTTON_LEFT && !right) break;
            host_sdl_to_stage((float)e.button.x, (float)e.button.y, &P.mouse_x, &P.mouse_y);
            if (!right) P.mouse_down = down;
            input_push(down ? 1 : 2, P.mouse_x, P.mouse_y, right, 0);
            break;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            SDL_Keycode k = e.key.keysym.sym;
            int down = e.type == SDL_KEYDOWN;
            if (down && ((k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) || k == SDLK_F11)) { toggle_fullscreen(); break; }
            /* Shift, Ctrl, CapsLock enz. alleen geven in Director geen keyDown/keyUp */
            if ((k >= SDLK_LCTRL && k <= SDLK_RGUI) || k == SDLK_CAPSLOCK || k == SDLK_NUMLOCKCLEAR || k == SDLK_SCROLLLOCK ||
                k == SDLK_MODE || k == SDLK_APPLICATION) break;
            /* gewone tekens komen als SDL_TEXTINPUT; hier alleen toetsen zonder teken (of met Ctrl) */
            int printable = k >= 32 && k < 127 && !(e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI));
            if (printable && g_text_on) break;
            int ch = k == SDLK_RETURN || k == SDLK_KP_ENTER ? 13 : k == SDLK_BACKSPACE ? 8 : k == SDLK_TAB ? 9 :
                     k == SDLK_ESCAPE || k == SDLK_AC_BACK ? 27 : printable ? (int)k : 0;
            input_push(down ? 3 : 4, 0, 0, mac_keycode(k), ch);
            break;
        }
        case SDL_TEXTINPUT:
            for (const char *s = e.text.text; *s;) {
                int c = to_cp1252(utf8_next(&s));
                if (!c) continue;
                int code = mac_keycode(c < 128 ? c : 0);
                input_push(3, 0, 0, code, c);
                input_push(4, 0, 0, code, c);
            }
            break;
        }
    }
    if (g_win && pad_input((SDL_GetWindowFlags(g_win) & SDL_WINDOW_INPUT_FOCUS) != 0, g_devchanges)) {
#ifndef __ANDROID__   /* de pad verplaatste de aanwijzer: de echte muis erheen (Android tekent hem zelf, zie host_blit) */
        int ww, wh, ow, oh;
        SDL_GetWindowSize(g_win, &ww, &wh);
        SDL_GetRendererOutputSize(g_ren, &ow, &oh);
        float px = g_dst.x + (P.mouse_x + 0.5f) * g_dst.w / 640, py = g_dst.y + (P.mouse_y + 0.5f) * g_dst.h / 480;
        SDL_WarpMouseInWindow(g_win, ow > 0 ? (int)(px * ww / ow) : (int)px, oh > 0 ? (int)(py * wh / oh) : (int)py);
#endif
    }
    if (g_win) update_cursor();
#ifdef __ANDROID__
    int want = player_text_wanted();   /* schermtoetsenbord zolang er een naamveld is */
    if (want != g_text_on) { if (want) SDL_StartTextInput(); else SDL_StopTextInput(); g_text_on = want; }
#endif
}

/* ------------------------------------------------------------------ venster */
int host_open(int scale, int fullscreen) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");   /* Android: vingers zelf afhandelen */
    if (SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 0; }
    if (scale <= 0) {   /* grootste gehele schaal die op het scherm past */
        SDL_Rect r;
        scale = 1;
        if (!SDL_GetDisplayUsableBounds(0, &r))
            while (640 * (scale + 1) <= r.w - 16 && 480 * (scale + 1) <= r.h - 48) scale++;
    }
    Uint32 fl = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef __ANDROID__
    fl |= SDL_WINDOW_FULLSCREEN;
#endif
    g_win = SDL_CreateWindow(g_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             640 * scale, 480 * scale, fl);
    if (!g_win) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 0; }
    SDL_SetWindowMinimumSize(g_win, 320, 240);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 0; }
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 480);
    if (fullscreen) toggle_fullscreen();
    g_cur_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    g_cur_wait = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
#ifndef __ANDROID__
    SDL_StartTextInput();
    g_text_on = 1;
#else
    SDL_StopTextInput();
    g_text_on = 0;
#endif
    return 1;
}

/* ------------------------------------------------------------------ crash */
static void on_crash(int sig) {
    fprintf(stderr, "[crash] signaal %d\n", sig);
#ifdef __GLIBC__
    void *bt[32];
    int n = backtrace(bt, 32);
    backtrace_symbols_fd(bt, n, 2);
#endif
    _exit(1);
}

void host_crash_init(void) {
    signal(SIGSEGV, on_crash);
    signal(SIGBUS, on_crash);
    signal(SIGFPE, on_crash);
    signal(SIGABRT, on_crash);
}
#endif
