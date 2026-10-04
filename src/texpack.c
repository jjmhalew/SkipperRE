/* texpack.c - texture packs: elke bitmap van het spel kan vervangen worden door een PNG van willekeurige grootte.
 *
 * Herkenning gaat op de inhoud: een 64-bit FNV-1a-hash over breedte, hoogte, kleurdiepte en de pixelindexen van de
 * bitmap. `--dumptex` schrijft elke bitmap die het spel laat zien (met het palet van dat moment) naar
 *     <opslagmap>/mods/dump/<film>/<member>_<b>x<h>_<hash>.png
 * Een PNG in mods/textures/ (submappen mogen; naast de opslagmap of naast het programma) waarvan de naam eindigt op
 * _<hash>.png vervangt die bitmap; wat ervoor staat is vrij. Is de PNG groter (bijvoorbeeld 2x of 4x), dan tekent
 * stage.c het beeld op het scherm in die hogere resolutie (render-schaal: de grootste verhouding in het pakket, of
 * --hd N); het spel zelf, de tests en screenshots blijven 640x480.
 * Doorzichtigheid komt altijd van de originele bitmap (inks, matte), zodat een pakket alleen kleuren hoeft te
 * leveren; een pixel met alfa < 128 in de PNG is daarnaast ook doorzichtig. */
#include "dir.h"
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

typedef struct TexEnt { uint64_t hash; char *path; int w, h, ow, oh; } TexEnt;
static TexEnt *g_ent;
static int g_n, g_cap;
static int g_scale = 1, g_dump;
static char g_dumpdir[PLAT_PATH];

static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/* naam ...[_<b>x<h>]_<16 hex>.png -> hash (en de originele maat als die erin staat) */
static int parse_name(const char *name, uint64_t *hash, int *ow, int *oh) {
    size_t n = strlen(name);
    if (n < 21 || _stricmp(name + n - 4, ".png") || name[n - 21] != '_') return 0;
    uint64_t h = 0;
    for (size_t i = n - 20; i < n - 4; i++) {
        int v = hexval((unsigned char)name[i]);
        if (v < 0) return 0;
        h = h << 4 | (uint64_t)v;
    }
    *hash = h;
    *ow = *oh = 0;
    /* _<b>x<h> ervoor */
    size_t e = n - 21, s = e;
    while (s > 0 && name[s - 1] != '_') s--;
    if (s > 0) sscanf(name + s, "%dx%d", ow, oh);
    return 1;
}

static void add_file(const char *path, const char *name) {
    uint64_t h;
    int ow, oh, w, hh, comp;
    if (!parse_name(name, &h, &ow, &oh)) return;
    if (!stbi_info(path, &w, &hh, &comp)) { fprintf(stderr, "[texpack] geen PNG: %s\n", path); return; }
    if (g_n == g_cap) { g_cap = g_cap ? g_cap * 2 : 256; g_ent = realloc(g_ent, sizeof *g_ent * g_cap); }
    g_ent[g_n++] = (TexEnt){h, _strdup(path), w, hh, ow, oh};
    if (ow > 0 && oh > 0) {   /* render-schaal: de grootste (afgeronde) vergroting in het pakket, hoogstens 4 */
        int k = (w + ow / 2) / ow;
        if (k > g_scale) g_scale = k > 4 ? 4 : k;
    }
}

static void scan(const char *dir, int depth) {
    if (depth > 8) return;
#ifdef _WIN32
    char pat[PLAT_PATH];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        char p[PLAT_PATH];
        snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) scan(p, depth + 1);
        else add_file(p, fd.cFileName);
    } while (FindNextFileA(f, &fd));
    FindClose(f);
#else
    char d0[PLAT_PATH];
    snprintf(d0, sizeof d0, "%s", dir);
    for (char *q = d0; *q; q++) if (*q == '\\') *q = '/';
    DIR *d = opendir(d0);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[PLAT_PATH];
        snprintf(p, sizeof p, "%s/%s", d0, e->d_name);
        struct stat st;
        if (stat(p, &st)) continue;
        if (S_ISDIR(st.st_mode)) scan(p, depth + 1);
        else add_file(p, e->d_name);
    }
    closedir(d);
#endif
}

static int cmp_ent(const void *a, const void *b) {
    uint64_t x = ((const TexEnt *)a)->hash, y = ((const TexEnt *)b)->hash;
    return x < y ? -1 : x > y;
}

void texpack_init(int dump, int force_scale) {
    char dirs[2][PLAT_PATH];
    snprintf(dirs[0], PLAT_PATH, "%s\\mods\\textures", P.save_dir);
    plat_exe_dir(dirs[1], PLAT_PATH);
    snprintf(dirs[1] + strlen(dirs[1]), PLAT_PATH - strlen(dirs[1]), "\\mods\\textures");
    scan(dirs[0], 0);
    if (strcmp(dirs[0], dirs[1])) scan(dirs[1], 0);
    qsort(g_ent, g_n, sizeof *g_ent, cmp_ent);
    if (force_scale > 0) g_scale = force_scale > 4 ? 4 : force_scale;
    if (g_n) fprintf(stderr, "[texpack] %d vervangende plaatjes, beeld op %dx\n", g_n, g_scale);
    g_dump = dump;
    if (dump) {
        snprintf(g_dumpdir, sizeof g_dumpdir, "%s\\mods", P.save_dir);
        plat_mkdir(g_dumpdir);
        snprintf(g_dumpdir, sizeof g_dumpdir, "%s\\mods\\dump", P.save_dir);
        plat_mkdir(g_dumpdir);
        fprintf(stderr, "[texpack] bitmaps naar %s\n", g_dumpdir);
    }
}

int texpack_scale(void) { return g_scale; }

static uint64_t bm_hash(Bitmap *bm) {
    uint64_t h = 1469598103934665603ull;
    int hdr[3] = {bm->w, bm->h, bm->bpp};
    const uint8_t *p = (const uint8_t *)hdr;
    for (size_t i = 0; i < sizeof hdr; i++) { h ^= p[i]; h *= 1099511628211ull; }
    for (size_t i = 0, n = (size_t)bm->w * bm->h; i < n; i++) { h ^= bm->px[i]; h *= 1099511628211ull; }
    return h;
}

static void dump(Bitmap *bm, const uint32_t *lut, const char *movie) {
    char dir[PLAT_PATH], path[PLAT_PATH], nm[64] = "";
    snprintf(dir, sizeof dir, "%s\\%s", g_dumpdir, movie && movie[0] ? movie : "film");
    plat_mkdir(dir);
    int k = 0;   /* membernaam, veilig als bestandsnaam */
    for (const char *s = bm->name ? bm->name : ""; *s && k < 40; s++)
        nm[k++] = (*s == ' ' || *s == '.' || *s == '-') ? '_' : (strchr("\\/:*?\"<>|", *s) ? '_' : *s);
    nm[k] = 0;
    snprintf(path, sizeof path, "%s\\%s_%dx%d_%016llx.png", dir, k ? nm : "bitmap", bm->w, bm->h, (unsigned long long)bm->hash);
    if (plat_exists(path)) return;
    uint8_t *rgba = malloc((size_t)bm->w * bm->h * 4);
    for (int i = 0; i < bm->w * bm->h; i++) {
        uint32_t c = bm->bpp == 1 ? (bm->px[i] ? 0xff000000u : 0xffffffffu) : lut[bm->px[i]];
        rgba[4 * i] = c >> 16; rgba[4 * i + 1] = c >> 8; rgba[4 * i + 2] = c; rgba[4 * i + 3] = 255;
    }
#ifndef _WIN32
    for (char *q = path; *q; q++) if (*q == '\\') *q = '/';
#endif
    stbi_write_png(path, bm->w, bm->h, 4, rgba, bm->w * 4);
    free(rgba);
}

/* de vervanging van bm (ARGB, *w x *h) of NULL; lut = het palet van nu (voor --dumptex) */
const uint32_t *texpack_get(Bitmap *bm, const uint32_t *lut, const char *movie, int *w, int *h) {
    if (bm->hd_state == 0) {
        bm->hd_state = 1;
        if (!g_n && !g_dump) return NULL;
        if (!bm->w || !bm->h) return NULL;
        bm->hash = bm_hash(bm);
        if (g_dump) dump(bm, lut, movie);
        int lo = 0, hi = g_n - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (g_ent[mid].hash == bm->hash) {
                int iw, ih, comp;
                uint8_t *d = stbi_load(g_ent[mid].path, &iw, &ih, &comp, 4);
                if (!d) { fprintf(stderr, "[texpack] kan %s niet lezen\n", g_ent[mid].path); break; }
                bm->hd = malloc(sizeof(uint32_t) * iw * ih);
                for (int i = 0; i < iw * ih; i++)
                    bm->hd[i] = (uint32_t)d[4 * i + 3] << 24 | (uint32_t)d[4 * i] << 16 | (uint32_t)d[4 * i + 1] << 8 | d[4 * i + 2];
                stbi_image_free(d);
                bm->hd_w = iw; bm->hd_h = ih;
                break;
            }
            if (g_ent[mid].hash < bm->hash) lo = mid + 1; else hi = mid - 1;
        }
    }
    if (!bm->hd) return NULL;
    *w = bm->hd_w; *h = bm->hd_h;
    return bm->hd;
}
