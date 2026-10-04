/* Eén exe: tools/pack.c (make_standalone.bat) plakt de spelbestanden achter skipper.exe (Windows negeert alles na de
 * laatste sectie). Het spel ziet ze dan in de map van de exe staan: een pad onder die map dat niet
 * als echt bestand bestaat, wordt in het pakket opgezocht. Echte bestanden gaan altijd voor.
 *
 * Opbouw (little-endian), achteraan de exe:
 *   bestanden (elk los raw-deflate of ongecomprimeerd)
 *   map:   u32 aantal, per bestand: u16 naamlengte, naam (relatief, '\\'), u64 offset, u32 grootte
 *          ingepakt, u32 grootte uitgepakt, u8 methode (0 = zo, 1 = deflate)
 *   staart: "SKPACK01", u64 offset van de map */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { char *name; uint64_t off; uint32_t csize, usize; uint8_t method; } PEnt;

static PEnt *g_ent;
static int g_n;
static FILE *g_f;
static char g_root[600];

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }

int pack_open(const char *root) {
    if (g_n) return 1;
    char exe[600];
    plat_exe_path(exe, sizeof exe);
    FILE *f = fopen(exe, "rb");
    if (!f) return 0;
    uint8_t t[16];
    _fseeki64(f, 0, SEEK_END);
    int64_t end = _ftelli64(f);
    if (end < 16 || _fseeki64(f, end - 16, SEEK_SET) || fread(t, 1, 16, f) != 16 || memcmp(t, "SKPACK01", 8)) {
        fclose(f);
        return 0;
    }
    uint64_t dir = le64(t + 8);
    if (dir >= (uint64_t)end - 16) { fclose(f); return 0; }
    size_t dn = (size_t)(end - 16 - dir);
    uint8_t *d = malloc(dn);
    _fseeki64(f, (int64_t)dir, SEEK_SET);
    if (!d || fread(d, 1, dn, f) != dn || dn < 4) { free(d); fclose(f); return 0; }
    int n = (int)le32(d);
    g_ent = calloc(n, sizeof(PEnt));
    size_t o = 4;
    int i;
    for (i = 0; i < n && o + 2 <= dn; i++) {
        int nl = d[o] | d[o + 1] << 8;
        if (o + 2 + nl + 17 > dn) break;
        PEnt *e = &g_ent[i];
        e->name = malloc(nl + 1);
        memcpy(e->name, d + o + 2, nl);
        e->name[nl] = 0;
        o += 2 + nl;
        e->off = le64(d + o);
        e->csize = le32(d + o + 8);
        e->usize = le32(d + o + 12);
        e->method = d[o + 16];
        o += 17;
    }
    free(d);
    g_n = i;
    g_f = f;
    snprintf(g_root, sizeof g_root, "%s", root);
    for (char *q = g_root; *q; q++) if (*q == '/') *q = '\\';
    size_t rl = strlen(g_root);
    while (rl && g_root[rl - 1] == '\\') g_root[--rl] = 0;
    fprintf(stderr, "[pack] %d bestanden in de exe\n", g_n);
    return g_n > 0;
}

/* pad onder de pakketmap -> bestand in het pakket */
static PEnt *pack_find(const char *path) {
    if (!g_n) return NULL;
    char full[600];   /* '/' -> '\', dubbele scheidingstekens weg */
    int k = 0;
    for (const char *q = path; *q && k < (int)sizeof full - 1; q++) {
        char c = *q == '/' ? '\\' : *q;
        if (c == '\\' && k > 1 && full[k - 1] == '\\') continue;   /* k > 1: \\server blijft staan */
        full[k++] = c;
    }
    full[k] = 0;
    size_t rl = strlen(g_root);
    if (_strnicmp(full, g_root, rl) || full[rl] != '\\') return NULL;
    for (int i = 0; i < g_n; i++)
        if (!_stricmp(full + rl + 1, g_ent[i].name)) return &g_ent[i];
    return NULL;
}

int vfs_exists(const char *path) {
    return plat_exists(path) || pack_find(path) != NULL;
}

/* hele bestand in het geheugen (malloc); echt bestand of uit het pakket */
uint8_t *vfs_load(const char *path, long *n) {
    *n = 0;
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *b = malloc(sz > 0 ? sz : 1);
        if (b && fread(b, 1, sz, f) != (size_t)sz) { free(b); b = NULL; }
        fclose(f);
        if (b) *n = sz;
        return b;
    }
    PEnt *e = pack_find(path);
    if (!e) return NULL;
    uint8_t *c = malloc(e->csize ? e->csize : 1);
    if (!c || _fseeki64(g_f, (int64_t)e->off, SEEK_SET) || fread(c, 1, e->csize, g_f) != e->csize) { free(c); return NULL; }
    if (e->method == 0) { *n = (long)e->csize; return c; }
    uint8_t *u = NULL;
    size_t ul = 0;
    int r = disc_inflate(c, e->csize, e->usize, &u, &ul);
    free(c);
    if (r != 1 || ul != e->usize) { free(u); return NULL; }
    *n = (long)ul;
    return u;
}

/* Echt pad voor API's die zelf een bestand openen (FileIO, INI): een bestand uit het pakket wordt
 * naar %TEMP%\SkipperRE uitgepakt. Anders blijft het pad zoals het is. */
char *vfs_real(const char *path, char *out, int n) {
    snprintf(out, n, "%s", path);
    if (plat_exists(path)) return out;
    PEnt *e = pack_find(path);
    if (!e) return out;
    char tmp[PLAT_PATH];
    plat_temp_dir(tmp, sizeof tmp);
    snprintf(tmp + strlen(tmp), sizeof tmp - strlen(tmp), "\\SkipperRE");
    plat_mkdir(tmp);
    const char *b = e->name;
    for (const char *q = e->name; *q; q++) if (*q == '\\') b = q + 1;
    char dst[PLAT_PATH];
    snprintf(dst, sizeof dst, "%s\\%s", tmp, b);
    long len;
    uint8_t *d = vfs_load(path, &len);
    FILE *f = d ? fopen(dst, "wb") : NULL;
    if (f) {
        fwrite(d, 1, len, f);
        fclose(f);
        snprintf(out, n, "%s", dst);
    }
    free(d);
    return out;
}
