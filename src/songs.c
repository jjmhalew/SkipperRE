/* songs.c - het menu "Liedjes om mee te zingen" van de Nederlandse cd (--songs). Het origineel is Liedjes.exe, een
 * Transposia-starter: Liedjes.bmp met klikvlakken uit Liedjes.ini ([startup] hotspotN = l, t, r, b; actionN = music /
 * run / quit; objectN = 0.. voor de audiotracks; soundN = geluidje als de muis erop komt). De cd is een Enhanced CD:
 * de liedjes zijn audiotrack 1-15, object n speelt dus track n + 1. "run autorun.exe" (de pijl terug) start hier het spel.
 * Geeft 1 als daarna het spel moet starten, 0 om te stoppen. */
#include "dir.h"

typedef struct { int l, t, r, b, action, object; char sound[64]; } Spot;
enum { A_MUSIC, A_RUN, A_QUIT };

int input_next(int *kind, int *x, int *y, int *a, int *b);

/* 24-bit BMP (onderste regel eerst) naar 0xffRRGGBB */
static uint32_t *bmp24(const char *path, int *w, int *h) {
    long n;
    uint8_t *b = vfs_load(path, &n);
    if (!b || n < 54 || b[0] != 'B' || b[1] != 'M' || (b[28] | b[29] << 8) != 24) { free(b); return NULL; }
    uint32_t off = b[10] | b[11] << 8 | b[12] << 16 | (uint32_t)b[13] << 24;
    int W = b[18] | b[19] << 8 | b[20] << 16 | b[21] << 24, H = b[22] | b[23] << 8 | b[24] << 16 | b[25] << 24;
    int up = H < 0;
    if (up) H = -H;
    int pitch = (W * 3 + 3) & ~3;
    if (W <= 0 || H <= 0 || off + (long)pitch * H > n) { free(b); return NULL; }
    uint32_t *px = malloc(sizeof(uint32_t) * W * H);
    for (int y = 0; y < H; y++) {
        const uint8_t *row = b + off + (size_t)pitch * (up ? y : H - 1 - y);
        for (int x = 0; x < W; x++) px[y * W + x] = 0xff000000u | row[3 * x + 2] << 16 | row[3 * x + 1] << 8 | row[3 * x];
    }
    free(b);
    *w = W; *h = H;
    return px;
}

int songs_run(int (*clicks)[3], int nclicks, int frames, const char *shot) {
    char ini[PLAT_PATH], path[PLAT_PATH], v[128], key[32];
    snprintf(path, sizeof path, "%s\\Liedjes.ini", P.base_dir);
    vfs_real(path, ini, sizeof ini);
    ini_get(ini, "startup", "bitmap", "Liedjes.bmp", v, sizeof v);
    snprintf(path, sizeof path, "%s\\%s", P.base_dir, v);
    int bw, bh;
    uint32_t *bmp = bmp24(path, &bw, &bh);
    if (!bmp) {
        host_message(UI("Het liedjesmenu staat niet op deze cd (Liedjes.bmp ontbreekt).",
                        "This CD has no songs menu (Liedjes.bmp is missing)."), 1);
        return 0;
    }
    Spot spots[32];
    int ns = 0;
    for (int i = 1; ns < 32; i++) {
        snprintf(key, sizeof key, "hotspot%d", i);
        Spot *s = &spots[ns];
        if (!ini_get(ini, "startup", key, "", v, sizeof v) || sscanf(v, "%d , %d , %d , %d", &s->l, &s->t, &s->r, &s->b) != 4) break;
        snprintf(key, sizeof key, "action%d", i);
        ini_get(ini, "startup", key, "", v, sizeof v);
        s->action = !_stricmp(v, "music") ? A_MUSIC : !_stricmp(v, "run") ? A_RUN : A_QUIT;
        snprintf(key, sizeof key, "object%d", i);
        s->object = ini_get_int(ini, "startup", key, 0);
        snprintf(key, sizeof key, "sound%d", i);
        ini_get(ini, "startup", key, "", s->sound, sizeof s->sound);
        ns++;
    }
    if (!g_headless && !cd_tracks())
        host_message(UI("De liedjes zijn audiotracks op de cd; ze spelen uit een cd-image (.CUE + .BIN): start met --bin <SKIPPER_1.CUE>.",
                        "The songs are audio tracks on the CD; they play from a CD image (.CUE + .BIN): start with --bin <SKIPPER_1.CUE>."), 0);
    /* in het midden van het podium (600x450 in 640x480), zwart eromheen */
    if (!stage_px) stage_px = calloc(640 * 480, 4);   /* (stage.c maakt hem pas bij het eerste beeld) */
    int ox = (640 - bw) / 2, oy = (480 - bh) / 2;
    for (int i = 0; i < 640 * 480; i++) stage_px[i] = 0xff000000u;
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++)
            if (x + ox >= 0 && x + ox < 640 && y + oy >= 0 && y + oy < 480) stage_px[(y + oy) * 640 + x + ox] = bmp[y * bw + x];
    free(bmp);
    Sound *over[32] = {0};
    int hot = -1, result = -1;
    for (int frame = 0; result < 0; frame++) {
        int kind, x, y, a, b, click = 0, cx = 0, cy = 0;
        if (g_headless) {
            if (frame >= frames) break;
            for (int k = 0; k < nclicks; k++)
                if (clicks[k][2] == frame) { click = 1; cx = clicks[k][0]; cy = clicks[k][1]; }
        } else {
            host_events();
            if (P.halted == 2) result = 0;   /* venster dicht */
            while (input_next(&kind, &x, &y, &a, &b)) {
                if (kind == 2 && !a) { click = 1; cx = x; cy = y; }
                if (kind == 3 && a == 53) result = 0;   /* Esc */
            }
            host_blit(stage_px, 640, 480);
            plat_sleep(15);
        }
        /* muis boven een klikvlak: het geluidje van dat vlak, één keer */
        int mx = click ? cx : P.mouse_x, my = click ? cy : P.mouse_y, at = -1;
        for (int i = 0; i < ns; i++)
            if (mx - ox >= spots[i].l && mx - ox < spots[i].r && my - oy >= spots[i].t && my - oy < spots[i].b) at = i;
        if (at != hot && at >= 0 && spots[at].sound[0]) {
            if (!over[at]) {
                snprintf(path, sizeof path, "%s\\%s", P.base_dir, spots[at].sound);
                over[at] = sound_load_wav(path);
            }
            if (over[at]) sound_play_sound(1, over[at]);
        }
        hot = at;
        if (click && at >= 0) {
            Spot *s = &spots[at];
            if (s->action == A_MUSIC) {
                fprintf(stderr, "[liedjes] track %d\n", s->object + 1);
                cd_play_track(s->object + 1);
            } else result = s->action == A_RUN;
        }
    }
    if (shot) stage_screenshot(shot);
    cd_stop();
    sound_stop(1);
    for (int i = 0; i < 32; i++) if (over[i]) { free(over[i]->pcm); free(over[i]); }
    return result > 0;
}
