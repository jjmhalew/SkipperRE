/* Spelbestanden vinden zonder handwerk:
 *   - datamap met Magnus.dxr (argument, extract\ naast de exe of in de repo, %APPDATA%\SkipperRE\data)
 *   - een cd-station met de cd erin
 *   - een cd-image (BIN/CUE, ISO of CloneCD IMG/CCD): de datatrack wordt eenmalig uitgepakt naar
 *     %APPDATA%\SkipperRE\data
 * en de opstartfilm start.dxr, die alleen in de projector START32.EXE zit: los op de Scandinavische cd, op de
 * Nederlandse als deflate-stroom in de Wise-installer SETUP.EXE.
 *
 * Eigen inflate (RFC 1951, canonieke Huffman-decodering), eigen ISO9660-lezer; geen bibliotheken. */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ inflate */
typedef struct {
    const uint8_t *src, *end;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t len, cap, max;   /* max = stop na zoveel bytes (proberen) */
    int err;
} Inf;

typedef struct { short count[16], symbol[288]; } Huff;

static int bits(Inf *s, int n) {
    while (s->bitcnt < n) {
        if (s->src >= s->end) { s->err = 1; return 0; }
        s->bitbuf |= (uint32_t)*s->src++ << s->bitcnt;
        s->bitcnt += 8;
    }
    int v = (int)(s->bitbuf & ((1u << n) - 1));
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return v;
}

static int put(Inf *s, uint8_t b) {
    if (s->len >= s->max) return 0;
    if (s->len == s->cap) {
        s->cap = s->cap ? s->cap * 2 : (s->max < 65536 ? s->max : 65536);   /* proberen: klein beginnen */
        s->out = realloc(s->out, s->cap);
    }
    s->out[s->len++] = b;
    return 1;
}

/* canonieke Huffman-tabel uit codelengtes; <0 = ongeldig (overvol) */
static int build(Huff *h, const short *len, int n) {
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int l = 1; l < 16; l++) {
        left <<= 1;
        left -= h->count[l];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h->count[l];
    for (int i = 0; i < n; i++)
        if (len[i]) h->symbol[offs[len[i]]++] = (short)i;
    return left;
}

static int decode(Inf *s, const Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= bits(s, 1);
        if (s->err) return -1;
        int count = h->count[l];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/* 1 = blok klaar, 0 = uitvoerlimiet bereikt, -1 = fout */
static int codes(Inf *s, const Huff *lc, const Huff *dc) {
    for (;;) {
        int sym = decode(s, lc);
        if (sym < 0) return -1;
        if (sym < 256) { if (!put(s, (uint8_t)sym)) return 0; }
        else if (sym == 256) return 1;
        else {
            sym -= 257;
            if (sym >= 29) return -1;
            int len = lbase[sym] + bits(s, lext[sym]);
            int ds = decode(s, dc);
            if (ds < 0 || ds >= 30) return -1;
            size_t dist = (size_t)(dbase[ds] + bits(s, dext[ds]));
            if (s->err || dist > s->len) return -1;
            while (len--) if (!put(s, s->out[s->len - dist])) return 0;
        }
    }
}

static int block_stored(Inf *s) {
    s->bitbuf = 0; s->bitcnt = 0;   /* naar bytegrens */
    if (s->src + 4 > s->end) return -1;
    unsigned len = s->src[0] | s->src[1] << 8, nlen = s->src[2] | s->src[3] << 8;
    s->src += 4;
    if (len != (~nlen & 0xffff) || s->src + len > s->end) return -1;
    while (len--) if (!put(s, *s->src++)) return 0;
    return 1;
}

static int block_fixed(Inf *s) {
    static Huff lc, dc;
    static int done;
    if (!done) {
        short l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        build(&lc, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        build(&dc, l, 30);
        done = 1;
    }
    return codes(s, &lc, &dc);
}

static int block_dynamic(Inf *s) {
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short len[320];
    Huff lc, dc;
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->err || nlen > 286 || ndist > 30) return -1;
    int i = 0;
    for (; i < ncode; i++) len[order[i]] = (short)bits(s, 3);
    for (; i < 19; i++) len[order[i]] = 0;
    if (s->err || build(&lc, len, 19) != 0) return -1;
    for (i = 0; i < nlen + ndist;) {
        int sym = decode(s, &lc);
        if (sym < 0) return -1;
        if (sym < 16) len[i++] = (short)sym;
        else {
            int rep, v = 0;
            if (sym == 16) { if (!i) return -1; v = len[i - 1]; rep = 3 + bits(s, 2); }
            else if (sym == 17) rep = 3 + bits(s, 3);
            else rep = 11 + bits(s, 7);
            if (s->err || i + rep > nlen + ndist) return -1;
            while (rep--) len[i++] = (short)v;
        }
    }
    if (len[256] == 0) return -1;
    int e = build(&lc, len, nlen);
    if (e < 0 || (e > 0 && nlen - lc.count[0] != 1)) return -1;
    e = build(&dc, len + nlen, ndist);
    if (e < 0 || (e > 0 && ndist - dc.count[0] != 1)) return -1;
    return codes(s, &lc, &dc);
}

/* raw deflate vanaf src; max = maximaal aantal uitvoerbytes. Geeft 1 (compleet), 0 (limiet), -1 (fout). */
static int inflate_raw(const uint8_t *src, size_t n, size_t max, uint8_t **out, size_t *outlen) {
    Inf s = {src, src + n, 0, 0, NULL, 0, 0, max, 0};
    int last, r = 1;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.err) { r = -1; break; }
        r = type == 0 ? block_stored(&s) : type == 1 ? block_fixed(&s) : type == 2 ? block_dynamic(&s) : -1;
        if (r <= 0) break;
    } while (!last);
    *out = s.out;
    *outlen = s.len;
    return s.err ? -1 : r;
}

int disc_inflate(const uint8_t *src, size_t n, size_t usize, uint8_t **out, size_t *outlen) {
    return inflate_raw(src, n, usize, out, outlen);
}

/* ------------------------------------------------------------------ bestanden */
static uint8_t *read_file(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = sz > 0 ? malloc(sz) : NULL;
    if (b && fread(b, 1, sz, f) != (size_t)sz) { free(b); b = NULL; }
    fclose(f);
    *n = b ? (size_t)sz : 0;
    return b;
}

static int file_exists(const char *p) { return plat_exists(p); }

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* ------------------------------------------------------------------ start.dxr uit SETUP.EXE */
/* De projector bevat een APPL-RIFX (achter de '59JP'-kop) met File-chunks; de eerste MV93-film is
 * 'start'. Zijn mmap-offsets zijn absoluut in de exe en worden teruggerekend (zie tools/projector.py). */
static int projector_start(const uint8_t *exe, size_t n, const char *dst) {
    /* kop "59JP" (Director 5) of "PJ93" (Director 4) met de offset van de APPL-RIFX erachter */
    uint32_t appl = 0;
    for (size_t i = 0; i + 8 <= n && !appl; i++)
        if (!memcmp(exe + i, "59JP", 4) || !memcmp(exe + i, "PJ93", 4)) {
            uint32_t a = rd32(exe + i + 4);
            if (a + 32 <= n && !memcmp(exe + a, "XFIR", 4) && !memcmp(exe + a + 8, "LPPA", 4)) appl = a;
        }
    if (!appl) return 0;
    uint32_t mm = rd32(exe + appl + 24);
    if (mm + 24 > n) return 0;
    int hl = rd16(exe + mm + 8), el = rd16(exe + mm + 10);
    int used = (int)rd32(exe + mm + 16);
    for (int i = 0; i < used; i++) {
        size_t e = mm + 8 + hl + (size_t)i * el;
        if (e + 12 > n) break;
        uint32_t off = rd32(exe + e + 8);
        if (memcmp(exe + e, "eliF", 4) || off + 12 > n || memcmp(exe + off, "XFIR", 4) || memcmp(exe + off + 8, "39VM", 4))
            continue;
        uint32_t size = rd32(exe + off + 4) + 8;
        if (off + size > n) return 0;
        uint8_t *o = malloc(size);
        memcpy(o, exe + off, size);
        uint32_t imm = rd32(o + 24);
        wr32(o + 24, imm - off);
        uint32_t m = imm - off;
        if (m + 24 > size || memcmp(o + m, "pamm", 4)) { free(o); return 0; }
        int h2 = rd16(o + m + 8), e2 = rd16(o + m + 10);
        int u2 = (int)rd32(o + m + 16);
        for (int k = 0; k < u2; k++) {
            size_t q = m + 8 + h2 + (size_t)k * e2;
            if (q + 12 > size) break;
            uint32_t v = rd32(o + q + 8);
            if (v >= off) wr32(o + q + 8, v - off);
        }
        FILE *f = fopen(dst, "wb");
        int ok = f && fwrite(o, 1, size, f) == size;
        if (f) fclose(f);
        free(o);
        return ok;
    }
    return 0;
}

/* start.dxr uit de projector in de datamap halen: START32.EXE (Scandinavische cd) of, ingepakt, SETUP.EXE (Nederlandse
 * cd; zijn START32.EXE is een lader zonder film) */
int disc_make_start(const char *dir, const char *dst) {
    char p[PLAT_PATH];
    size_t n;
    snprintf(p, sizeof p, "%s\\START32.EXE", dir);
    uint8_t *b = read_file(p, &n);
    int ok = b && projector_start(b, n, dst);
    free(b);
    if (ok) return 1;
    snprintf(p, sizeof p, "%s\\SETUP.EXE", dir);
    b = read_file(p, &n);
    if (!b) return 0;
    /* Wise: losse deflate-stromen; zoek de stroom die uitpakt tot een exe met een Director-projector */
    for (size_t o = 0; o + 16 < n && !ok; o++) {
        uint8_t *head;
        size_t hl;
        if (inflate_raw(b + o, n - o, 2, &head, &hl) < 0 || hl < 2 || head[0] != 'M' || head[1] != 'Z') {
            free(head);
            continue;
        }
        free(head);
        uint8_t *exe;
        size_t en;
        if (inflate_raw(b + o, n - o, (size_t)64 << 20, &exe, &en) == 1 && en > 100000) ok = projector_start(exe, en, dst);
        free(exe);
    }
    free(b);
    return ok;
}

/* ------------------------------------------------------------------ ISO9660 uit BIN/CUE of ISO */
typedef struct { FILE *f; long start; int raw, hdr; } Img;   /* raw: 2352-byte sectoren, hdr = 16/24 */

static int img_sector(Img *im, uint32_t lba, uint8_t *buf) {
    long long pos = im->raw ? (long long)lba * 2352 + im->hdr : (long long)lba * 2048;
    if (_fseeki64(im->f, pos, SEEK_SET)) return 0;
    return fread(buf, 1, 2048, im->f) == 2048;
}

static int skip_dir(const char *name) {
    /* niet gebruikt door het spel: catalogus met demo's en de Video for Windows-installatie */
    return !_stricmp(name, "Catalog") || !_stricmp(name, "VFW");
}

void (*disc_progress)(const char *name, uint64_t bytes);   /* na elk uitgepakt bestand (Switch: voortgang op de console) */
static uint64_t g_unpacked;

static int iso_walk(Img *im, uint32_t lba, uint32_t size, const char *dst, int depth) {
    if (depth > 8) return 1;
    plat_mkdir(dst);
    uint32_t nsec = (size + 2047) / 2048;
    uint8_t *dir = malloc((size_t)nsec * 2048);
    for (uint32_t i = 0; i < nsec; i++)
        if (!img_sector(im, lba + i, dir + i * 2048)) { free(dir); return 0; }
    int ok = 1;
    for (uint32_t p = 0; p < nsec * 2048 && ok;) {
        int rl = dir[p];
        if (!rl) { p = (p / 2048 + 1) * 2048; continue; }
        const uint8_t *r = dir + p;
        p += rl;
        int nl = r[32];
        if (nl == 1 && (r[33] == 0 || r[33] == 1)) continue;   /* . en .. */
        char name[256];
        int k = 0;
        for (int i = 0; i < nl && r[33 + i] != ';' && k < 255; i++) name[k++] = (char)r[33 + i];
        name[k] = 0;
        if (k && name[k - 1] == '.') name[k - 1] = 0;
        uint32_t elba = rd32(r + 2), esz = rd32(r + 10);
        char path[600];
        snprintf(path, sizeof path, "%s\\%s", dst, name);
        if (r[25] & 2) {
            if (!skip_dir(name)) ok = iso_walk(im, elba, esz, path, depth + 1);
        } else {
            FILE *o = fopen(path, "wb");
            if (!o) { ok = 0; break; }
            uint8_t sec[2048];
            for (uint32_t left = esz, i = 0; left > 0; i++) {
                if (!img_sector(im, elba + i, sec)) { ok = 0; break; }
                uint32_t n = left < 2048 ? left : 2048;
                if (fwrite(sec, 1, n, o) != n) { ok = 0; break; }   /* schijf / SD-kaart vol */
                left -= n;
            }
            if (fclose(o)) ok = 0;
            g_unpacked += esz;
            if (disc_progress) disc_progress(name, g_unpacked);
        }
    }
    free(dir);
    return ok;
}

/* datatrack uit de CUE: laatste TRACK MODE1/2352 of MODE2/2352 en zijn INDEX 01 */
static int cue_data_track(const char *cue, char *bin, int nbin, uint32_t *lba, int *hdr) {
    FILE *f = fopen(cue, "rb");
    if (!f) return 0;
    char line[512], file[300] = "";
    int mode = 0, found = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        int mm, ss, ff, idx;
        char q[300];
        if (sscanf(p, "FILE \"%299[^\"]\"", q) == 1) snprintf(file, sizeof file, "%s", q);
        else if (!strncmp(p, "TRACK", 5)) mode = strstr(p, "MODE1/2352") ? 16 : strstr(p, "MODE2/2352") ? 24 : 0;
        else if (sscanf(p, "INDEX %d %d:%d:%d", &idx, &mm, &ss, &ff) == 4 && idx == 1 && mode) {
            *lba = (uint32_t)((mm * 60 + ss) * 75 + ff);
            *hdr = mode;
            found = 1;
        }
    }
    fclose(f);
    if (!found || !file[0]) return 0;
    /* BIN-pad relatief t.o.v. de CUE */
    const char *slash = strrchr(cue, '\\');
    const char *s2 = strrchr(cue, '/');
    if (s2 > slash) slash = s2;
    if (slash) snprintf(bin, nbin, "%.*s\\%s", (int)(slash - cue), cue, file);
    else snprintf(bin, nbin, "%s", file);
    return 1;
}

/* Een image openen. "/proc/self/fd/N" is een bestand dat de bestandskiezer al geopend heeft (Android): dat lezen we via
 * een kopie van de descriptor. Opnieuw openen via het /proc-pad mag op Android niet, de app heeft geen rechten op het
 * echte pad (bijv. Download/). */
static FILE *img_open(const char *path) {
#ifndef _WIN32
    int fd;
    char rest;
    if (sscanf(path, "/proc/self/fd/%d%c", &fd, &rest) == 1) {
        int d = dup(fd);
        FILE *f = d >= 0 ? fdopen(d, "rb") : NULL;
        if (!f && d >= 0) close(d);
        if (f && fseeko(f, 0, SEEK_SET)) {   /* geen gewoon bestand (pijp van een cloud-app): niet bruikbaar */
            fprintf(stderr, "[disc] %s: niet doorzoekbaar (%s)\n", path, strerror(errno));
            fclose(f);
            return NULL;
        }
        if (!f) fprintf(stderr, "[disc] %s: %s\n", path, strerror(errno));
        return f;
    }
#endif
    FILE *f = fopen(path, "rb");
    if (!f) fprintf(stderr, "[disc] %s: %s\n", path, strerror(errno));
    return f;
}

/* image (CUE, BIN met CUE ernaast, of ISO) uitpakken naar dst; 1 = gelukt */
int disc_extract(const char *image, const char *dst, char *bin_out, int nbin) {
    char cue[600], bin[600];
    snprintf(cue, sizeof cue, "%s", image);
    char *dot = strrchr(cue, '.');
    Img im = {0};
    uint32_t start = 0;
    if (dot && (!_stricmp(dot, ".bin") || !_stricmp(dot, ".cue"))) {
        strcpy(dot, ".cue");
        if (!file_exists(cue)) strcpy(dot, ".CUE");
        if (cue_data_track(cue, bin, sizeof bin, &start, &im.hdr)) im.raw = 1;
        else if (!_stricmp(strrchr(image, '.'), ".bin")) snprintf(bin, sizeof bin, "%s", image);   /* BIN zonder CUE */
        else return 0;
    } else if (dot && !_stricmp(dot, ".ccd")) {   /* CloneCD: de sectoren staan in de .img ernaast */
        strcpy(dot, ".img");
        if (!file_exists(cue)) strcpy(dot, ".IMG");
        snprintf(bin, sizeof bin, "%s", cue);
    } else snprintf(bin, sizeof bin, "%s", image);   /* .iso, .img (of een geopend bestand zonder naam) */
    im.f = img_open(bin);
    if (!im.f) return 0;
    uint8_t pvd[2048];
    if (!im.raw && !(img_sector(&im, 16, pvd) && pvd[0] == 1 && !memcmp(pvd + 1, "CD001", 5))) {
        /* zonder CUE en geen ISO: ruwe sectoren van 2352 bytes. Op deze cd (Enhanced CD) staan de muzieknummers
         * vooraan en de datatrack achteraan: de eerste sector met het synchronisatiepatroon waar 16 sectoren verder
         * een ISO9660-volumebeschrijving staat */
        static const uint8_t sync[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
        uint8_t h[16];
        for (uint32_t lba = 0; !im.raw && !_fseeki64(im.f, (long long)lba * 2352, SEEK_SET) && fread(h, 1, 16, im.f) == 16; lba++) {
            if (memcmp(h, sync, 12) || (h[15] != 1 && h[15] != 2)) continue;
            Img t = im;
            t.raw = 1;
            t.hdr = h[15] == 2 ? 24 : 16;
            for (uint32_t k = 0; k < 600 && !im.raw; k++)   /* voorloop (pregap) van de track overslaan */
                if (img_sector(&t, lba + k + 16, pvd) && pvd[0] == 1 && !memcmp(pvd + 1, "CD001", 5)) { im = t; start = lba + k; }
            if (!im.raw) lba += 600;
        }
        if (im.raw) fprintf(stderr, "[disc] datatrack op sector %u (zonder CUE)\n", start);
    }
    int ok = 0;
    if (img_sector(&im, start + 16, pvd) && pvd[0] == 1 && !memcmp(pvd + 1, "CD001", 5)) {
        fprintf(stderr, "[disc] datatrack uitpakken uit %s naar %s ...\n", bin, dst);
        ok = iso_walk(&im, rd32(pvd + 156 + 2), rd32(pvd + 156 + 10), dst, 0);
    }
    fclose(im.f);
    if (ok && bin_out && im.raw && strncmp(bin, "/proc/", 6)) snprintf(bin_out, nbin, "%s", bin);   /* fd: gaat zo dicht */
    return ok;
}

/* ------------------------------------------------------------------ zoeken */
int disc_d4(const char *dir) {   /* Deense cd van 1996 (Director 4): hoofdfilm in MAGNUS.EXE, MAGNUS0/1.DXR */
    char p[600], q[600];
    snprintf(p, sizeof p, "%s\\MAGNUS0.DXR", dir);
    snprintf(q, sizeof q, "%s\\MAGNUS.EXE", dir);
    return vfs_exists(p) && vfs_exists(q);
}

/* de hoofdfilm van de D4-cd uit de projector MAGNUS.EXE halen */
int disc_make_main_d4(const char *dir, const char *dst) {
    char p[PLAT_PATH];
    size_t n;
    snprintf(p, sizeof p, "%s\\MAGNUS.EXE", dir);
    uint8_t *b = read_file(p, &n);
    int ok = b && projector_start(b, n, dst);
    free(b);
    return ok;
}

static int has_game(const char *dir) {
    char p[600];
    snprintf(p, sizeof p, "%s\\Magnus.dxr", dir);
    return file_exists(p) || disc_d4(dir);
}

int disc_nordic(const char *dir) {
    char p[600], q[600];
    snprintf(p, sizeof p, "%s\\MAGNUSDK.CXT", dir);
    snprintf(q, sizeof q, "%s\\MagnusNL.cxt", dir);
    return vfs_exists(p) && !vfs_exists(q);   /* ook in een exe met ingepakte bestanden */
}

/* Zoekt de spelbestanden. data = wat de gebruiker opgaf (of "extract"), image = --bin/--image (of "").
 * Schrijft de gevonden map in out; bin_out krijgt het BIN-pad als dat bekend wordt (CD-audio). */
int disc_find_data(const char *data, const char *image, const char *appdir, char *out, int n, char *bin_out, int nbin) {
    char exedir[PLAT_PATH], cand[PLAT_PATH];
    plat_exe_dir(exedir, sizeof exedir);
    plat_full_path(data, cand, sizeof cand);
    if (has_game(cand)) { snprintf(out, n, "%s", cand); return 1; }
    if (pack_open(exedir)) { snprintf(out, n, "%s", exedir); return 1; }   /* alles in de exe */
    const char *rel[] = {"%s\\extract", "%s\\..\\extract", "%s\\data"};
    for (int i = 0; i < 3; i++) {
        char p[PLAT_PATH];
        snprintf(p, sizeof p, rel[i], exedir);
        plat_full_path(p, cand, sizeof cand);
        if (has_game(cand)) { snprintf(out, n, "%s", cand); return 1; }
    }
    char appdata[PLAT_PATH];
    snprintf(appdata, sizeof appdata, "%s\\data", appdir);
    if (has_game(appdata)) { snprintf(out, n, "%s", appdata); return 1; }   /* eerder uitgepakt */
    if (image && image[0] && disc_extract(image, appdata, bin_out, nbin) && has_game(appdata)) {
        snprintf(out, n, "%s", appdata);
        return 1;
    }
    /* cd-stations (Linux: gemounte schijven) */
    char cds[64][PLAT_PATH];
    int ncd = plat_cd_dirs(cds, 64);
    for (int i = 0; i < ncd; i++)
        if (has_game(cds[i])) { snprintf(out, n, "%s", cds[i]); return 1; }
    /* een .cue of .iso naast de exe, in de werkmap of een map hoger */
    char img[PLAT_PATH];
    const char *dirs[] = {exedir, ".", ".."}, *exts[] = {".cue", ".iso", ".ccd"};
    for (int i = 0; i < 3; i++)
        for (int e = 0; e < 3; e++) {
            char full[PLAT_PATH];
            plat_full_path(dirs[i], full, sizeof full);
            if (plat_find_ext(full, exts[e], img, sizeof img) && disc_extract(img, appdata, bin_out, nbin) && has_game(appdata)) {
                snprintf(out, n, "%s", appdata);
                return 1;
            }
        }
    return 0;
}

/* een gekozen image of map (bestandskiezer) -> datamap; 1 = gelukt */
int disc_use(const char *pick, const char *appdir, char *out, int n, char *bin_out, int nbin) {
    if (plat_is_dir(pick)) {
        if (has_game(pick)) { snprintf(out, n, "%s", pick); return 1; }
        char img[PLAT_PATH];   /* een map met een image erin */
        if (!plat_find_ext(pick, ".cue", img, sizeof img) && !plat_find_ext(pick, ".iso", img, sizeof img) &&
            !plat_find_ext(pick, ".ccd", img, sizeof img) && !plat_find_ext(pick, ".bin", img, sizeof img) &&
            !plat_find_ext(pick, ".img", img, sizeof img))
            return 0;
        return disc_use(img, appdir, out, n, bin_out, nbin);
    }
    char appdata[PLAT_PATH];
    snprintf(appdata, sizeof appdata, "%s\\data", appdir);
    if (disc_extract(pick, appdata, bin_out, nbin) && has_game(appdata)) { snprintf(out, n, "%s", appdata); return 1; }
    return 0;
}
