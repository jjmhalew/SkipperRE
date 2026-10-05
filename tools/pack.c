/* pack.c - maakt de standalone-exe voor eigen gebruik (make_standalone.bat): plakt de spelbestanden van jouw cd achter
 * skipper.exe, in de opbouw die src/pack.c leest. Het resultaat bevat de spelbestanden: alleen voor jezelf, nooit delen.
 *
 *   pack [skipper.exe] [map met Magnus.dxr] [uit.exe]
 *
 * Standaard: out\skipper.exe + extract\, data\ of %APPDATA%\SkipperRE\data -> SkipperRE-standalone.exe.
 * Alleen wat het spel gebruikt gaat mee: films (.dxr), casts (.cxt), het icoon en .ini's uit de hoofdmap plus Video\;
 * niet de Catalog-map, de installers en de 16-bit DLL's. start.dxr komt uit de datamap of uit de opslagmap
 * %APPDATA%\SkipperRE (die haalt skipper.exe bij de eerste start uit START32.EXE of SETUP.EXE; van de Scandinavische
 * cd heet hij daar start_nordic.dxr).
 * Bouwen (doet build.bat standalone): zig cc -std=c99 -O2 -o out\pack.exe tools\pack.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../src/stb/stb_image_write.h"   /* alleen voor stbi_zlib_compress */

typedef struct { char name[MAX_PATH], path[2 * MAX_PATH]; uint64_t off; uint32_t csize, usize; uint8_t method; } Ent;

static Ent *g_e;
static int g_n, g_cap;

static int exists(const char *p) { DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

static int has_game(const char *dir) { char p[2 * MAX_PATH]; snprintf(p, sizeof p, "%s\\Magnus.dxr", dir); return exists(p); }

static int wanted(const char *f) {
    const char *d = strrchr(f, '.');
    return d && (!_stricmp(d, ".dxr") || !_stricmp(d, ".cxt") || !_stricmp(d, ".ico") || !_stricmp(d, ".ini"));
}

static void add(const char *name, const char *path) {
    if (g_n == g_cap) { g_cap = g_cap ? g_cap * 2 : 64; g_e = realloc(g_e, g_cap * sizeof *g_e); }
    Ent *e = &g_e[g_n++];
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", name);
    snprintf(e->path, sizeof e->path, "%s", path);
}

static int by_name(const void *a, const void *b) { return strcmp(((const Ent *)a)->name, ((const Ent *)b)->name); }

/* de bestanden van dir (gefilterd of alle) onder het voorvoegsel pre, op naam gesorteerd */
static void list(const char *dir, const char *pre, int filter) {
    char pat[2 * MAX_PATH], name[MAX_PATH], path[2 * MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    int first = g_n;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (filter && !wanted(fd.cFileName)) continue;
        snprintf(name, sizeof name, "%s%s", pre, fd.cFileName);
        snprintf(path, sizeof path, "%s\\%s", dir, fd.cFileName);
        add(name, path);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    qsort(g_e + first, g_n - first, sizeof *g_e, by_name);
}

static unsigned char *slurp(const char *path, long *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    _fseeki64(f, 0, SEEK_END);
    *n = (long)_ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n ? *n : 1);
    if (b && fread(b, 1, *n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static void put(FILE *o, const void *p, size_t n) { fwrite(p, 1, n, o); }
static void put16(FILE *o, uint32_t v) { unsigned char b[2] = {v, v >> 8}; put(o, b, 2); }
static void put32(FILE *o, uint32_t v) { unsigned char b[4] = {v, v >> 8, v >> 16, v >> 24}; put(o, b, 4); }
static void put64(FILE *o, uint64_t v) { put32(o, (uint32_t)v); put32(o, (uint32_t)(v >> 32)); }

int main(int argc, char **argv) {
    const char *exe = argc > 1 ? argv[1] : "out\\skipper.exe", *out = argc > 3 ? argv[3] : "SkipperRE-standalone.exe";
    char app[MAX_PATH], appdata[2 * MAX_PATH], data[2 * MAX_PATH] = "";
    snprintf(app, sizeof app, "%s\\SkipperRE", getenv("APPDATA") ? getenv("APPDATA") : ".");
    snprintf(appdata, sizeof appdata, "%s\\data", app);
    if (argc > 2) snprintf(data, sizeof data, "%s", argv[2]);
    else {
        const char *cand[] = {"extract", "data", appdata};
        for (int i = 0; i < 3 && !data[0]; i++)
            if (has_game(cand[i])) snprintf(data, sizeof data, "%s", cand[i]);
    }
    if (!data[0] || !has_game(data)) {
        fprintf(stderr, "pack: geen spelbestanden (Magnus.dxr) in %s\n", data[0] ? data : "extract\\, data\\ of %APPDATA%\\SkipperRE\\data");
        return 1;
    }

    long en;
    unsigned char *eb = slurp(exe, &en);
    if (!eb) { fprintf(stderr, "pack: %s niet gevonden\n", exe); return 1; }
    if (en >= 16 && !memcmp(eb + en - 16, "SKPACK01", 8)) {
        fprintf(stderr, "pack: %s bevat al spelbestanden; geef de gewone skipper.exe op\n", exe);
        return 1;
    }

    list(data, "", 1);
    int top = g_n, start = 0;
    for (int i = 0; i < top; i++) if (!_stricmp(g_e[i].name, "start.dxr")) start = 1;
    char vid[2 * MAX_PATH];
    snprintf(vid, sizeof vid, "%s\\Video", data);
    list(vid, "Video\\", 0);
    if (!start) {   /* de opstartfilm staat in de opslagmap, van de Scandinavische cd als start_nordic.dxr */
        const char *dirs[] = {app, NULL};
        char save[2 * MAX_PATH], p[2 * MAX_PATH], dk[2 * MAX_PATH], nl[2 * MAX_PATH];
        snprintf(save, sizeof save, "%s\\save", app);
        dirs[1] = save;
        snprintf(dk, sizeof dk, "%s\\MAGNUSDK.CXT", data);
        snprintf(nl, sizeof nl, "%s\\MagnusNL.cxt", data);
        const char *name = exists(dk) && !exists(nl) ? "start_nordic.dxr" : "start.dxr";
        for (int i = 0; i < 2 && !start; i++) {
            snprintf(p, sizeof p, "%s\\%s", dirs[i], name);
            if (exists(p)) { add("start.dxr", p); start = 1; }
        }
        if (!start) {
            fprintf(stderr, "pack: start.dxr niet gevonden: start skipper.exe eerst een keer (die haalt hem uit START32.EXE of SETUP.EXE)\n");
            return 1;
        }
    }

    FILE *o = fopen(out, "wb");
    if (!o) { fprintf(stderr, "pack: kan %s niet schrijven\n", out); return 1; }
    put(o, eb, en);
    free(eb);
    uint64_t tot = 0, totc = 0;
    for (int i = 0; i < g_n; i++) {
        Ent *e = &g_e[i];
        long n;
        unsigned char *raw = slurp(e->path, &n);
        if (!raw) { fprintf(stderr, "pack: kan %s niet lezen\n", e->path); fclose(o); remove(out); return 1; }
        int zl = 0;
        unsigned char *z = stbi_zlib_compress(raw, (int)n, &zl, 8);   /* zlib: 2 bytes kop + deflate + 4 bytes adler32 */
        e->off = (uint64_t)_ftelli64(o);
        e->usize = (uint32_t)n;
        if (z && zl - 6 < n * 0.98) { e->method = 1; e->csize = zl - 6; put(o, z + 2, zl - 6); }
        else { e->method = 0; e->csize = (uint32_t)n; put(o, raw, n); }
        tot += e->usize;
        totc += e->csize;
        free(z);
        free(raw);
        if (i % 10 == 9) printf("pack: %d / %d bestanden\n", i + 1, g_n);
    }
    uint64_t dir = (uint64_t)_ftelli64(o);
    put32(o, g_n);
    for (int i = 0; i < g_n; i++) {
        Ent *e = &g_e[i];
        put16(o, (uint32_t)strlen(e->name));
        put(o, e->name, strlen(e->name));
        put64(o, e->off);
        put32(o, e->csize);
        put32(o, e->usize);
        put(o, &e->method, 1);
    }
    put(o, "SKPACK01", 8);
    put64(o, dir);
    int bad = ferror(o);
    if (fclose(o) || bad) { fprintf(stderr, "pack: schrijven naar %s mislukt\n", out); remove(out); return 1; }
    printf("%d bestanden uit %s, %llu MB -> %llu MB; %s: %llu MB\n", g_n, data, (unsigned long long)(tot >> 20),
           (unsigned long long)(totc >> 20), out, (unsigned long long)((dir + 16) >> 20));
    return 0;
}
