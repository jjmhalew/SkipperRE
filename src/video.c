/* Digitale video: AVI-bestanden (RIFF) met Cinepak-beeld en PCM-geluid, zoals de 26 filmpjes van het
 * ABC-spel (MMB09, Video\*.avi: 'cvid' 320x240 24-bit, 15 fps, 8-bit mono 11/22 kHz).
 *
 * Cinepak (Radius/SuperMac): elk frame bestaat uit horizontale strips; per strip twee codeboeken van
 * 256 vectoren van 2x2 pixels (v4 = detail, v1 = één vector opgeblazen tot 4x4). Een vector heeft 4
 * helderheden Y en (in kleur) een gedeelde U/V. Codeboeken blijven per strip-nummer bewaard tussen
 * frames; tussenframes (chunk 0x31) slaan blokken over die niet veranderen. Frames worden daarom op
 * volgorde gedecodeerd; terugspoelen begint opnieuw bij frame 0. */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_STRIPS 32

typedef struct { uint8_t px[4][3]; } CvVec;   /* 4 pixels (lb, rb, lo, ro) in RGB */
typedef struct { CvVec v4[256], v1[256]; } CvStrip;

struct Video {
    uint8_t *file;
    size_t size;
    int w, h, nframes;
    double us_per_frame;
    uint32_t *off, *len;        /* per videoframe: positie en lengte in het bestand (len 0 = herhaling) */
    Sound audio;                /* hele geluidsspoor als 16-bit PCM */
    uint8_t *rgb;               /* huidig frame, 24-bit */
    uint32_t *argb;             /* idem, 32-bit voor de stage */
    int cur;                    /* nummer van het frame in rgb, -1 = nog niets */
    CvStrip strips[MAX_STRIPS];
};

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t be32v(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t be24(const uint8_t *p) { return (uint32_t)p[0] << 16 | p[1] << 8 | p[2]; }
static uint16_t be16v(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint8_t clip8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* ------------------------------------------------------------------ Cinepak */
static void cv_codebook(CvVec *cb, int id, const uint8_t *d, const uint8_t *end) {
    int n = (id & 0x04) ? 4 : 6;   /* 4 = grijs (alleen Y), 6 = Y0..Y3 + U + V */
    uint32_t flag = 0, mask = 0;
    for (int i = 0; i < 256; i++) {
        if ((id & 0x01) && !(mask >>= 1)) {   /* gedeeltelijke update: bitmasker per 32 vectoren */
            if (d + 4 > end) break;
            flag = be32v(d); d += 4;
            mask = 0x80000000u;
        }
        if (!(id & 0x01) || (flag & mask)) {
            if (d + n > end) break;
            int y[4] = {d[0], d[1], d[2], d[3]};
            int u = 0, v = 0;
            if (n == 6) { u = (int8_t)d[4]; v = (int8_t)d[5]; }
            d += n;
            for (int k = 0; k < 4; k++) {
                cb[i].px[k][0] = clip8(y[k] + v * 2);
                cb[i].px[k][1] = clip8(y[k] - u / 2 - v);
                cb[i].px[k][2] = clip8(y[k] + u * 2);
            }
        }
    }
}

static void put2(uint8_t *row, const uint8_t *a, const uint8_t *b) {
    memcpy(row, a, 3);
    memcpy(row + 3, b, 3);
}

static int cv_vectors(Video *v, CvStrip *s, int id, int x1, int y1, int x2, int y2, const uint8_t *d,
                      const uint8_t *end) {
    uint32_t flag = 0, mask = 0;
    int pitch = v->w * 3;
    for (int y = y1; y < y2; y += 4)
        for (int x = x1; x < x2; x += 4) {
            if ((id & 0x01) && !(mask >>= 1)) {   /* tussenframe: 1 = blok verandert */
                if (d + 4 > end) return -1;
                flag = be32v(d); d += 4;
                mask = 0x80000000u;
            }
            if ((id & 0x01) && !(flag & mask)) continue;
            int use_v1;
            if (id & 0x02) use_v1 = 1;             /* 0x32: alleen v1 */
            else {
                if (!(mask >>= 1)) {
                    if (d + 4 > end) return -1;
                    flag = be32v(d); d += 4;
                    mask = 0x80000000u;
                }
                use_v1 = !(flag & mask);
            }
            if (x + 4 > v->w || y + 4 > v->h) {   /* rand: niet voorkomend bij 320x240 */
                d += use_v1 ? 1 : 4;
                continue;
            }
            uint8_t *r0 = v->rgb + y * pitch + x * 3, *r1 = r0 + pitch, *r2 = r1 + pitch, *r3 = r2 + pitch;
            if (use_v1) {
                if (d >= end) return -1;
                CvVec *c = &s->v1[*d++];
                put2(r0, c->px[0], c->px[0]); put2(r0 + 6, c->px[1], c->px[1]);
                put2(r1, c->px[0], c->px[0]); put2(r1 + 6, c->px[1], c->px[1]);
                put2(r2, c->px[2], c->px[2]); put2(r2 + 6, c->px[3], c->px[3]);
                put2(r3, c->px[2], c->px[2]); put2(r3 + 6, c->px[3], c->px[3]);
            } else {
                if (d + 4 > end) return -1;
                CvVec *c0 = &s->v4[d[0]], *c1 = &s->v4[d[1]], *c2 = &s->v4[d[2]], *c3 = &s->v4[d[3]];
                d += 4;
                put2(r0, c0->px[0], c0->px[1]); put2(r0 + 6, c1->px[0], c1->px[1]);
                put2(r1, c0->px[2], c0->px[3]); put2(r1 + 6, c1->px[2], c1->px[3]);
                put2(r2, c2->px[0], c2->px[1]); put2(r2 + 6, c3->px[0], c3->px[1]);
                put2(r3, c2->px[2], c2->px[3]); put2(r3 + 6, c3->px[2], c3->px[3]);
            }
        }
    return 0;
}

static void cv_frame(Video *v, const uint8_t *d, uint32_t size) {
    if (size < 10) return;
    const uint8_t *end = d + size;
    int flags = d[0];
    int nstrips = be16v(d + 8);
    if (nstrips > MAX_STRIPS) nstrips = MAX_STRIPS;
    d += 10;
    int y0 = 0;
    for (int i = 0; i < nstrips; i++) {
        if (d + 12 > end) return;
        int y1 = be16v(d + 4), x1 = be16v(d + 6), y2 = be16v(d + 8), x2 = be16v(d + 10);
        if (!y1) { y1 = y0; y2 = y0 + be16v(d + 8); }   /* 0 = relatief t.o.v. de vorige strip */
        int ssize = (int)be24(d + 1) - 12;
        d += 12;
        if (ssize < 0) return;
        const uint8_t *send = d + ssize > end ? end : d + ssize;
        CvStrip *s = &v->strips[i];
        if (i > 0 && !(flags & 0x01)) *s = v->strips[i - 1];   /* codeboeken van de vorige strip erven */
        if (x2 > v->w) x2 = v->w;
        if (y2 > v->h) y2 = v->h;
        const uint8_t *p = d;
        while (p + 4 <= send) {
            int id = p[0], csize = (int)be24(p + 1) - 4;
            p += 4;
            if (csize < 0) break;
            const uint8_t *cend = p + csize > send ? send : p + csize;
            if (id >= 0x20 && id <= 0x27) cv_codebook((id & 0x02) ? s->v1 : s->v4, id, p, cend);
            else if (id >= 0x30 && id <= 0x32) { cv_vectors(v, s, id, x1, y1, x2, y2, p, cend); break; }
            p = cend;
        }
        d = send;
        y0 = y2;
    }
}

/* ------------------------------------------------------------------ AVI */
typedef struct { uint32_t *off, *len; int n, cap; uint8_t *pcm8; size_t npcm, cappcm; int abits, achan; } Scan;

static void push_frame(Scan *sc, uint32_t off, uint32_t len) {
    if (sc->n == sc->cap) {
        sc->cap = sc->cap ? sc->cap * 2 : 256;
        sc->off = realloc(sc->off, sizeof(uint32_t) * sc->cap);
        sc->len = realloc(sc->len, sizeof(uint32_t) * sc->cap);
    }
    sc->off[sc->n] = off;
    sc->len[sc->n++] = len;
}

static void scan_movi(Video *v, Scan *sc, size_t o, size_t end) {
    while (o + 8 <= end) {
        const uint8_t *p = v->file + o;
        uint32_t sz = le32(p + 4);
        if (o + 8 + sz > v->size) break;
        if (!memcmp(p, "LIST", 4)) scan_movi(v, sc, o + 12, o + 8 + sz);   /* 'rec ' */
        else if (p[2] == 'd' && (p[3] == 'c' || p[3] == 'b')) push_frame(sc, (uint32_t)(o + 8), sz);
        else if (p[2] == 'w' && p[3] == 'b') {
            if (sc->npcm + sz > sc->cappcm) {
                sc->cappcm = (sc->npcm + sz) * 2;
                sc->pcm8 = realloc(sc->pcm8, sc->cappcm);
            }
            memcpy(sc->pcm8 + sc->npcm, p + 8, sz);
            sc->npcm += sz;
        }
        o += 8 + sz + (sz & 1);
    }
}

Video *video_open(const char *path) {
    long n;
    uint8_t *file = vfs_load(path, &n);
    if (!file) return NULL;
    if (n < 64) { free(file); return NULL; }
    Video *v = calloc(1, sizeof(Video));
    v->file = file;
    v->size = n;
    if (memcmp(v->file, "RIFF", 4) || memcmp(v->file + 8, "AVI ", 4)) { video_free(v); return NULL; }
    Scan sc = {0};
    int arate = 0, is_cvid = 0;
    const char *stype = NULL;
    /* hdrl doorlopen (één niveau diep genoeg voor deze bestanden) */
    size_t o = 12;
    while (o + 8 <= v->size) {
        const uint8_t *p = v->file + o;
        uint32_t sz = le32(p + 4);
        if (!memcmp(p, "LIST", 4) && !memcmp(p + 8, "movi", 4)) scan_movi(v, &sc, o + 12, o + 8 + sz);
        else if (!memcmp(p, "LIST", 4) && (!memcmp(p + 8, "hdrl", 4) || !memcmp(p + 8, "strl", 4))) {
            o += 12;
            continue;   /* de subchunks liggen er direct achter */
        } else if (!memcmp(p, "avih", 4)) {
            v->us_per_frame = le32(p + 8);
            v->w = (int)le32(p + 8 + 32);
            v->h = (int)le32(p + 8 + 36);
        } else if (!memcmp(p, "strh", 4)) {
            stype = !memcmp(p + 8, "vids", 4) ? "v" : !memcmp(p + 8, "auds", 4) ? "a" : NULL;
            if (stype && *stype == 'v') {
                is_cvid = !memcmp(p + 12, "cvid", 4) || !memcmp(p + 12, "CVID", 4);
                uint32_t scale = le32(p + 8 + 20), rate = le32(p + 8 + 24);
                if (scale && rate) v->us_per_frame = 1e6 * scale / rate;
            }
        } else if (!memcmp(p, "strf", 4) && stype) {
            if (*stype == 'v') {
                v->w = (int)le32(p + 8 + 4);
                v->h = (int)le32(p + 8 + 8);
                if (!memcmp(p + 8 + 16, "cvid", 4)) is_cvid = 1;
            } else {
                sc.achan = le16(p + 8 + 2);
                arate = (int)le32(p + 8 + 4);
                sc.abits = le16(p + 8 + 14);
            }
        }
        o += 8 + sz + (sz & 1);
    }
    if (!is_cvid || v->w <= 0 || v->h <= 0 || v->w > 1024 || v->h > 1024 || !sc.n) {
        vm_error("video %s: geen Cinepak-AVI", path);
        free(sc.off); free(sc.len); free(sc.pcm8);
        video_free(v);
        return NULL;
    }
    if (v->us_per_frame <= 0) v->us_per_frame = 1e6 / 15;
    v->nframes = sc.n;
    v->off = sc.off;
    v->len = sc.len;
    v->rgb = calloc((size_t)v->w * v->h, 3);
    v->argb = calloc((size_t)v->w * v->h, 4);
    v->cur = -1;
    if (sc.npcm && arate > 0 && (sc.abits == 8 || sc.abits == 16)) {
        int ch = sc.achan == 2 ? 2 : 1;
        size_t samples = sc.abits == 8 ? sc.npcm : sc.npcm / 2;
        v->audio.pcm = malloc(samples * sizeof(int16_t));
        for (size_t i = 0; i < samples; i++)
            v->audio.pcm[i] = sc.abits == 8 ? (int16_t)((sc.pcm8[i] - 128) * 256)
                                            : (int16_t)le16(sc.pcm8 + 2 * i);
        v->audio.rate = arate;
        v->audio.bits = 16;
        v->audio.channels = ch;
        v->audio.frames = (int)(samples / ch);
    }
    free(sc.pcm8);
    return v;
}

void video_free(Video *v) {
    if (!v) return;
    free(v->file); free(v->off); free(v->len); free(v->rgb); free(v->argb); free(v->audio.pcm);
    free(v);
}

int video_width(Video *v) { return v->w; }
int video_height(Video *v) { return v->h; }
int video_frames(Video *v) { return v->nframes; }
Sound *video_audio(Video *v) { return v->audio.frames ? &v->audio : NULL; }

/* duur en posities in ticks (1/60 s), zoals Director */
int video_duration(Video *v) { return (int)(v->nframes * v->us_per_frame * 60 / 1e6 + 0.5); }
int video_frame_at(Video *v, int ticks) {
    int f = (int)(ticks * 1e6 / 60 / v->us_per_frame);
    return f < 0 ? 0 : f >= v->nframes ? v->nframes - 1 : f;
}

/* frame nummer n als 32-bit (0xAARRGGBB); decodeert vanaf het huidige of vanaf 0 */
const uint32_t *video_frame(Video *v, int n) {
    if (n < 0) n = 0;
    if (n >= v->nframes) n = v->nframes - 1;
    if (n != v->cur) {
        if (n < v->cur || v->cur < 0) {
            memset(v->rgb, 0, (size_t)v->w * v->h * 3);
            memset(v->strips, 0, sizeof v->strips);
            v->cur = -1;
        }
        for (int i = v->cur + 1; i <= n; i++)
            if (v->len[i]) cv_frame(v, v->file + v->off[i], v->len[i]);
        v->cur = n;
        int np = v->w * v->h;
        for (int i = 0; i < np; i++)
            v->argb[i] = 0xff000000u | (uint32_t)v->rgb[3 * i] << 16 | (uint32_t)v->rgb[3 * i + 1] << 8 | v->rgb[3 * i + 2];
    }
    return v->argb;
}
