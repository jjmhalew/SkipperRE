/* Director 5-bestanden: RIFX/XFIR-container, cast, bitmaps, geluid, tekst, scripts, score.
 * Zie docs/ANALYSE.md voor de formaten; de Python-tools in tools/ zijn de referentie. */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[1] << 8 | p[0]); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }

static uint32_t rd32(DFile *f, const uint8_t *p) { return f->le ? le32(p) : be32(p); }
static uint16_t rd16(DFile *f, const uint8_t *p) { return f->le ? le16(p) : be16(p); }
static uint32_t rdtag(DFile *f, const uint8_t *p) { return f->le ? le32(p) : be32(p); }

DFile *dfile_open(const char *path) {
    long len;
    uint8_t *data = vfs_load(path, &len);
    if (!data) return NULL;
    if (len < 64) { free(data); return NULL; }
    DFile *f = calloc(1, sizeof *f);
    snprintf(f->path, sizeof f->path, "%s", path);
    f->data = data;
    f->len = len;
    const uint8_t *d = f->data;
    if (!memcmp(d, "XFIR", 4)) f->le = 1;
    else if (!memcmp(d, "RIFX", 4)) f->le = 0;
    else { free(f->data); free(f); return NULL; }
    f->codec = rdtag(f, d + 8);
    uint32_t mmap_off = rd32(f, d + 12 + 8 + 4);
    const uint8_t *m = d + mmap_off + 8;
    int hl = rd16(f, m), el = rd16(f, m + 2);
    int used = (int)rd32(f, m + 8);
    f->nchunks = used;
    f->chunks = calloc(used, sizeof(Chunk));
    for (int i = 0; i < used; i++) {
        const uint8_t *e = m + hl + i * el;
        f->chunks[i].tag = rdtag(f, e);
        f->chunks[i].size = rd32(f, e + 4);
        f->chunks[i].off = rd32(f, e + 8);
    }
    int k = dfile_first(f, FOURCC('K', 'E', 'Y', '*'));
    if (k >= 0) {
        const uint8_t *b = f->data + f->chunks[k].off + 8;
        int n = (int)rd32(f, b + 8);
        f->nkeys = n;
        f->keys = calloc(n, sizeof(KeyEnt));
        for (int i = 0; i < n; i++) {
            f->keys[i].sec = (int32_t)rd32(f, b + 12 + i * 12);
            f->keys[i].owner = (int32_t)rd32(f, b + 16 + i * 12);
            f->keys[i].tag = rdtag(f, b + 20 + i * 12);
        }
    }
    return f;
}

const uint8_t *dfile_chunk(DFile *f, int id, uint32_t *size) {
    if (id < 0 || id >= f->nchunks) { if (size) *size = 0; return NULL; }
    if (size) *size = f->chunks[id].size;
    return f->data + f->chunks[id].off + 8;
}

int dfile_child(DFile *f, int owner, uint32_t tag) {
    for (int i = 0; i < f->nkeys; i++)
        if (f->keys[i].owner == owner && f->keys[i].tag == tag) return f->keys[i].sec;
    return -1;
}

int dfile_first(DFile *f, uint32_t tag) {
    for (int i = 0; i < f->nchunks; i++)
        if (f->chunks[i].tag == tag) return i;
    return -1;
}

int dfile_owned(DFile *f, uint32_t tag, int owner) {
    int c = dfile_child(f, owner, tag);
    return c >= 0 ? c : dfile_first(f, tag);
}

/* ------------------------------------------------------------------ cast */
static char *pstr_dup(const uint8_t *p, int maxlen) {
    int n = maxlen > 0 ? p[0] : 0;
    if (n > maxlen - 1) n = maxlen - 1;
    char *s = malloc(n + 1);
    memcpy(s, p + 1, n);
    s[n] = 0;
    return s;
}

CastLib *cast_load(DFile *f, int first, int lib, int owner) {
    CastLib *c = calloc(1, sizeof *c);
    c->f = f;
    c->first = first;
    c->lib = lib;
    c->owner = owner;
    int cs = dfile_child(f, owner, FOURCC('C', 'A', 'S', '*'));
    if (cs < 0 && owner == 1024) cs = dfile_first(f, FOURCC('C', 'A', 'S', '*'));
    if (cs < 0) return c;
    uint32_t sz;
    const uint8_t *b = dfile_chunk(f, cs, &sz);
    c->n = sz / 4;
    c->m = calloc(c->n ? c->n : 1, sizeof(Member));
    for (int i = 0; i < c->n; i++) {
        int32_t id = (int32_t)be32(b + i * 4);
        if (id <= 0) continue;
        uint32_t csz;
        const uint8_t *cb = dfile_chunk(f, id, &csz);
        Member *m = &c->m[i];
        m->type = (int)be32(cb);
        uint32_t info_len = be32(cb + 4), spec_len = be32(cb + 8);
        const uint8_t *info = cb + 12;
        m->spec = cb + 12 + info_len;
        m->speclen = spec_len;
        m->cast_chunk = id;
        if (info_len >= 16) m->info_flags = be32(info + 12);
        /* info: offset naar data, daar u16 aantal + offsets; item 1 = naam */
        if (info_len >= 20) {
            uint32_t doff = be32(info);
            int cnt = be16(info + doff);
            if (cnt >= 2) {
                uint32_t o1 = be32(info + doff + 2 + 4), o2 = be32(info + doff + 2 + 8);
                const uint8_t *base = info + doff + 2 + 4 * (cnt + 1);
                if (o2 > o1) m->name = pstr_dup(base + o1, (int)(o2 - o1));
            }
            if (cnt >= 4) {   /* gekoppelde bestanden (video): item 2 = map, item 3 = bestandsnaam */
                uint32_t o3 = be32(info + doff + 2 + 12), o4 = be32(info + doff + 2 + 16);
                const uint8_t *base = info + doff + 2 + 4 * (cnt + 1);
                if (o4 > o3) m->file = pstr_dup(base + o3, (int)(o4 - o3));
            }
        }
        if (!m->name) m->name = strdup("");
        if (m->type == MT_SCRIPT && spec_len >= 2) m->script_type = be16(m->spec);
        if (m->type == MT_SHAPE && spec_len >= 17) {
            const uint8_t *s = m->spec;
            m->shape_type = be16(s);
            m->rect_t = (int16_t)be16(s + 2); m->rect_l = (int16_t)be16(s + 4);
            m->rect_b = (int16_t)be16(s + 6); m->rect_r = (int16_t)be16(s + 8);
            m->shape_fore = s[12]; m->shape_back = s[13]; m->shape_filled = s[14]; m->shape_line = s[15];
        }
        if ((m->type == MT_FILMLOOP || m->type == MT_VIDEO) && spec_len >= 8) {   /* initialRect */
            const uint8_t *s = m->spec;
            m->rect_t = (int16_t)be16(s); m->rect_l = (int16_t)be16(s + 2);
            m->rect_b = (int16_t)be16(s + 4); m->rect_r = (int16_t)be16(s + 6);
        }
    }
    scripts_load(c);
    return c;
}

Member *cast_member(CastLib *c, int num) {
    if (!c) return NULL;
    int i = num - c->first;
    if (i < 0 || i >= c->n || !c->m[i].type) return NULL;
    return &c->m[i];
}

static int member_owner(CastLib *c, Member *m) { (void)c; return m->cast_chunk; }

Bitmap *member_bitmap(CastLib *c, Member *m) {
    if (m->bmp) return m->bmp;
    if (m->type != MT_BITMAP || m->speclen < 22) return NULL;
    int bd = dfile_child(c->f, member_owner(c, m), FOURCC('B', 'I', 'T', 'D'));
    const uint8_t *s = m->spec;
    Bitmap *b = calloc(1, sizeof *b);
    b->name = m->name;
    uint16_t flags = be16(s);
    b->pitch = flags & 0x0fff;
    int top = (int16_t)be16(s + 2), left = (int16_t)be16(s + 4);
    int bottom = (int16_t)be16(s + 6), right = (int16_t)be16(s + 8);
    int regy = (int16_t)be16(s + 18), regx = (int16_t)be16(s + 20);
    b->w = right - left; b->h = bottom - top;
    b->left = left; b->top = top;
    b->reg_x = regx - left; b->reg_y = regy - top;
    b->bpp = 1;
    if ((flags & 0x8000) && m->speclen >= 28) {
        b->bpp = s[23];
        b->clut_lib = (int16_t)be16(s + 24);
        b->clut = (int16_t)be16(s + 26);
    }
    if (b->w <= 0 || b->h <= 0) { b->w = b->h = 0; b->px = calloc(1, 1); m->bmp = b; return b; }
    int need = b->pitch * b->h;
    uint8_t *raw = calloc(need + 1, 1);
    uint32_t sz = 0;
    const uint8_t *src = bd >= 0 ? dfile_chunk(c->f, bd, &sz) : NULL;
    if (src) {
        if ((int)sz == need) memcpy(raw, src, need);
        else {
            int o = 0;
            uint32_t i = 0;
            while (i < sz && o < need) {
                int n = src[i++];
                if (n < 0x80) {
                    for (int k = 0; k <= n && i < sz && o < need; k++) raw[o++] = src[i++];
                } else {
                    int cnt = 257 - n;
                    uint8_t v = i < sz ? src[i++] : 0;
                    for (int k = 0; k < cnt && o < need; k++) raw[o++] = v;
                }
            }
        }
    }
    b->px = malloc(b->w * b->h);
    for (int y = 0; y < b->h; y++) {
        const uint8_t *row = raw + y * b->pitch;
        uint8_t *dst = b->px + y * b->w;
        if (b->bpp == 8) memcpy(dst, row, b->w);
        else if (b->bpp == 1)
            for (int x = 0; x < b->w; x++) dst[x] = (row[x >> 3] >> (7 - (x & 7)) & 1) ? 255 : 0;
        else memset(dst, 0, b->w);  /* andere dieptes komen in dit spel niet voor */
    }
    free(raw);
    m->bmp = b;
    return b;
}

int member_palette(CastLib *c, Member *m) {
    if (m->has_pal) return 1;
    if (m->type != MT_PALETTE) return 0;
    int cl = dfile_child(c->f, member_owner(c, m), FOURCC('C', 'L', 'U', 'T'));
    uint32_t sz;
    const uint8_t *b = dfile_chunk(c->f, cl, &sz);
    if (!b) return 0;
    for (int i = 0; i < 256 && (uint32_t)(i * 6 + 5) < sz; i++) {
        m->pal[i][0] = b[i * 6]; m->pal[i][1] = b[i * 6 + 2]; m->pal[i][2] = b[i * 6 + 4];
    }
    m->has_pal = 1;
    return 1;
}

/* Mac 'snd '-resource (formaat 1 of 2) met standaard- of extended header */
Sound *member_sound(CastLib *c, Member *m) {
    if (m->snd) return m->snd;
    int sc = dfile_child(c->f, member_owner(c, m), FOURCC('s', 'n', 'd', ' '));
    uint32_t sz;
    const uint8_t *b = dfile_chunk(c->f, sc, &sz);
    if (!b || sz < 20) return NULL;
    uint32_t p = 0;
    int format = be16(b);
    if (format == 1) {
        int ndf = be16(b + 2);
        p = 4 + ndf * 6;
    } else p = 4;
    int ncmd = be16(b + p);
    p += 2;
    uint32_t hdr = 0;
    for (int i = 0; i < ncmd; i++) {
        uint16_t cmd = be16(b + p);
        if ((cmd & 0x7fff) == 0x51 || (cmd & 0x7fff) == 0x50) hdr = be32(b + p + 4);
        p += 8;
    }
    if (!hdr || hdr + 22 > sz) return NULL;
    const uint8_t *h = b + hdr;
    Sound *s = calloc(1, sizeof *s);
    uint32_t len = be32(h + 4);
    s->rate = (int)(be32(h + 8) >> 16);
    int enc = h[20];
    const uint8_t *data;
    s->channels = 1;
    s->bits = 8;
    if (enc == 0) {
        s->frames = (int)len;
        data = h + 22;
    } else if (enc == 0xff) {
        s->channels = (int)len;
        s->frames = (int)be32(h + 22);
        s->bits = be16(h + 48);
        data = h + 64;
    } else { free(s); return NULL; }
    if (s->channels < 1 || s->channels > 2) s->channels = 1;
    int n = s->frames * s->channels;
    size_t avail = sz - (size_t)(data - b);
    int bps = s->bits / 8;
    if ((size_t)n * bps > avail) n = (int)(avail / bps), s->frames = n / s->channels;
    s->pcm = malloc(sizeof(int16_t) * (n ? n : 1));
    if (s->bits == 8) {
        for (int i = 0; i < n; i++) s->pcm[i] = (int16_t)((data[i] - 128) * 256);
    } else {
        /* big-endian (Mac) of little-endian: kies de vloeiendste interpretatie */
        double dbe = 0, dle = 0;
        int16_t pbe = 0, ple = 0;
        for (int i = 0; i < n && i < 4000; i++) {
            int16_t vb = (int16_t)(data[2 * i] << 8 | data[2 * i + 1]);
            int16_t vl = (int16_t)(data[2 * i + 1] << 8 | data[2 * i]);
            dbe += abs(vb - pbe); dle += abs(vl - ple);
            pbe = vb; ple = vl;
        }
        int bigend = dbe <= dle;
        for (int i = 0; i < n; i++)
            s->pcm[i] = bigend ? (int16_t)(data[2 * i] << 8 | data[2 * i + 1])
                               : (int16_t)(data[2 * i + 1] << 8 | data[2 * i]);
    }
    s->loop = !(m->info_flags & 16);   /* "Loop" in het castvenster: achtergrondmuziek, DrumLoop */
    m->snd = s;
    return s;
}

static void font_name(DFile *f, int id, char *out, int n);

Text *member_text(CastLib *c, Member *m) {
    if (m->txt) return m->txt;
    if (m->type != MT_TEXT && m->type != MT_BUTTON) return NULL;
    Text *t = calloc(1, sizeof *t);
    const uint8_t *s = m->spec;
    t->align = 0; t->fore = 255; t->back = 0; t->font_size = 12;
    strcpy(t->font, "Arial");
    if (m->speclen >= 22) {
        t->align = (int16_t)be16(s + 4);
        t->top = (int16_t)be16(s + 14); t->left = (int16_t)be16(s + 16);
        t->h = (int16_t)be16(s + 18) - t->top; t->w = (int16_t)be16(s + 20) - t->left;
        t->back = 0;
    }
    int st = dfile_child(c->f, member_owner(c, m), FOURCC('S', 'T', 'X', 'T'));
    uint32_t sz;
    const uint8_t *b = dfile_chunk(c->f, st, &sz);
    if (b && sz >= 12) {
        uint32_t off = be32(b), tl = be32(b + 4);
        if (off + tl > sz) tl = sz - off;
        t->text = malloc(tl + 1);
        memcpy(t->text, b + off, tl);
        t->text[tl] = 0;
        const uint8_t *fmt = b + off + tl;
        if (off + tl + 2 + 20 <= sz && be16(fmt) > 0) {
            const uint8_t *r = fmt + 2;
            int fid = be16(r + 8);
            t->style = r[10];
            t->font_size = be16(r + 12);
            int rr = r[14], gg = r[16], bb = r[18];
            t->fore = rr << 16 | gg << 8 | bb;  /* RGB; zie stage.c */
            t->fore |= 0x1000000;
            font_name(c->f, fid, t->font, sizeof t->font);
        }
    } else t->text = strdup("");
    t->dirty = 1;
    m->txt = t;
    return t;
}

/* Fmap: fontmap. Formaat (D5): u32 mapLength, u32 namesLength, u32 bodyStart?, ... we zoeken
 * simpelweg de naam die bij id hoort in de namentabel (tweede helft); valt terug op Arial. */
static void font_name(DFile *f, int id, char *out, int n) {
    int fm = dfile_first(f, FOURCC('F', 'm', 'a', 'p'));
    snprintf(out, n, "Arial");
    if (fm < 0) return;
    uint32_t sz;
    const uint8_t *b = dfile_chunk(f, fm, &sz);
    if (sz < 32) return;
    uint32_t map_len = be32(b), names_len = be32(b + 4);
    uint32_t body = 8 + map_len;
    uint32_t count = be32(b + 8 + 8);
    const uint8_t *e = b + 8 + 16;
    for (uint32_t i = 0; i < count && (size_t)(e - b) + 8 <= 8 + map_len; i++, e += 8) {
        uint32_t noff = be32(e);
        int fid = be16(e + 4);
        if (fid == id && body + noff + 4 < sz && noff < names_len) {
            uint32_t ln = be32(b + body + noff);
            if (ln > (uint32_t)n - 1) ln = n - 1;
            memcpy(out, b + body + noff + 4, ln);
            out[ln] = 0;
            return;
        }
    }
}

/* ------------------------------------------------------------------ scripts */
static Datum ext80(const uint8_t *r) {
    int exp = be16(r) & 0x7fff, sign = be16(r) & 0x8000;
    uint64_t mant = (uint64_t)be32(r + 2) << 32 | be32(r + 6);
    double v = (exp == 0 && mant == 0) ? 0.0 : (double)mant * __builtin_pow(2.0, exp - 16383 - 63);
    return d_float(sign ? -v : v);
}

void scripts_load(CastLib *c) {
    DFile *f = c->f;
    for (int ci = 0; ci < f->nchunks; ci++) {
        uint32_t tag = f->chunks[ci].tag;
        if (tag != FOURCC('L', 'c', 't', 'x') && tag != FOURCC('L', 'c', 't', 'X')) continue;
        /* elke cast heeft zijn eigen Lctx (eigenaar = cast-id) */
        if (dfile_child(f, c->owner, tag) != ci) continue;
        uint32_t sz;
        const uint8_t *b = dfile_chunk(f, ci, &sz);
        if (sz < 36) continue;
        int cnt = (int)be32(b + 8);
        int eoff = be16(b + 16);
        int lnam = (int32_t)be32(b + 32);
        uint32_t nsz;
        const uint8_t *nb = dfile_chunk(f, lnam, &nsz);
        if (!nb) continue;
        int noff = be16(nb + 16), ncnt = be16(nb + 18);
        int *names = malloc(sizeof(int) * (ncnt ? ncnt : 1));
        const uint8_t *p = nb + noff;
        for (int i = 0; i < ncnt; i++) { names[i] = symn((const char *)p + 1, p[0]); p += 1 + p[0]; }
        for (int k = 0; k < cnt; k++) {
            int sec = (int32_t)be32(b + eoff + k * 12 + 4);
            if (sec < 0 || sec >= f->nchunks || f->chunks[sec].tag != FOURCC('L', 's', 'c', 'r')) continue;
            uint32_t lsz;
            const uint8_t *s = dfile_chunk(f, sec, &lsz);
            Script *sc = calloc(1, sizeof *sc);
            sc->lib = c;
            sc->names = names;
            sc->nnames = ncnt;
            uint32_t castid = be32(s + 44);
            sc->member = castid & 0xffff;
            const uint8_t *q = s + 50;
            /* hvc hvo hvs pc po gc go hc ho lc lo ldc ldo */
            int pc = be16(q + 10); uint32_t po = be32(q + 12);
            int gc = be16(q + 16); uint32_t go = be32(q + 18);
            int hc = be16(q + 22); uint32_t ho = be32(q + 24);
            int lc = be16(q + 28); uint32_t lo = be32(q + 30);
            uint32_t ldo = be32(q + 38);
#define NAME(i) ((int16_t)(i) >= 0 && (int16_t)(i) < ncnt ? names[(int16_t)(i)] : sym("?"))
            sc->nprops = pc;
            sc->props = malloc(sizeof(int) * (pc + 1));
            for (int i = 0; i < pc; i++) sc->props[i] = NAME(be16(s + po + 2 * i));
            sc->nglobals = gc;
            sc->globals = malloc(sizeof(int) * (gc + 1));
            for (int i = 0; i < gc; i++) sc->globals[i] = NAME(be16(s + go + 2 * i));
            sc->nlits = lc;
            sc->lits = calloc(lc + 1, sizeof(Datum));
            for (int i = 0; i < lc; i++) {
                uint32_t typ = be32(s + lo + i * 8), off = be32(s + lo + i * 8 + 4);
                if (typ == 4) sc->lits[i] = d_int((int32_t)off);
                else if (typ == 1 || typ == 2) {
                    uint32_t n = be32(s + ldo + off);
                    const char *str = (const char *)s + ldo + off + 4;
                    while (n && str[n - 1] == 0) n--;
                    sc->lits[i] = d_strn(str, (int)n);
                } else if (typ == 9) {
                    uint32_t n = be32(s + ldo + off);
                    if (n == 10) sc->lits[i] = ext80(s + ldo + off + 4);
                    else {
                        uint64_t v = (uint64_t)be32(s + ldo + off + 4) << 32 | be32(s + ldo + off + 8);
                        double dv; memcpy(&dv, &v, 8);
                        sc->lits[i] = d_float(dv);
                    }
                }
            }
            sc->nh = hc;
            sc->h = calloc(hc + 1, sizeof(Handler));
            for (int i = 0; i < hc; i++) {
                const uint8_t *r = s + ho + i * 42;
                Handler *h = &sc->h[i];
                h->name = NAME(be16(r));
                h->clen = (int)be32(r + 4);
                h->code = s + be32(r + 8);
                h->nargs = be16(r + 12);
                uint32_t ao = be32(r + 14);
                h->nlocals = be16(r + 18);
                uint32_t lo2 = be32(r + 20);
                h->nglobals = be16(r + 24);
                uint32_t go2 = be32(r + 26);
                h->args = malloc(sizeof(int) * (h->nargs + 1));
                for (int a = 0; a < h->nargs; a++) h->args[a] = NAME(be16(s + ao + 2 * a));
                h->locals = malloc(sizeof(int) * (h->nlocals + 1));
                for (int a = 0; a < h->nlocals; a++) h->locals[a] = NAME(be16(s + lo2 + 2 * a));
                h->globals = malloc(sizeof(int) * (h->nglobals + 1));
                for (int a = 0; a < h->nglobals; a++) h->globals[a] = NAME(be16(s + go2 + 2 * a));
            }
#undef NAME
            Member *m = cast_member(c, sc->member);
            if (m) {
                m->script = sc;
                sc->type = m->type == MT_SCRIPT ? m->script_type : 0; /* 0 = castlidscript */
            }
            c->scripts = realloc(c->scripts, sizeof(Script *) * (c->nscripts + 1));
            c->scripts[c->nscripts++] = sc;
        }
    }
}

Handler *script_handler(Script *s, int name) {
    if (!s) return NULL;
    for (int i = 0; i < s->nh; i++)
        if (s->h[i].name == name) return &s->h[i];
    return NULL;
}

/* ------------------------------------------------------------------ score */
static void score_parse(Score *sc, const uint8_t *b, uint32_t sz) {
    memset(sc, 0, sizeof *sc);
    if (!b || sz < 20) return;
    uint32_t size = be32(b), f1 = be32(b + 4);
    int sprsize = be16(b + 14), nchan = be16(b + 16);
    if (size > sz) size = sz;
    sc->nchan = nchan;
    int buflen = nchan * sprsize;
    uint8_t *buf = calloc(buflen + 64, 1);
    int cap = 16;
    sc->f = calloc(cap, sizeof(Frame));
    uint32_t p = f1;
    while (p + 2 <= size) {
        int flen = be16(b + p);
        if (flen < 2) break;
        uint32_t q = p + 2, end = p + flen;
        while (q + 4 <= end) {
            int ln = be16(b + q), off = be16(b + q + 2);
            if (off + ln <= buflen) memcpy(buf + off, b + q + 4, ln);
            q += 4 + ln;
        }
        if (sc->nframes == cap) { cap *= 2; sc->f = realloc(sc->f, cap * sizeof(Frame)); }
        Frame *fr = &sc->f[sc->nframes++];
        memset(fr, 0, sizeof *fr);
        fr->script_lib = be16(buf); fr->script = be16(buf + 2);
        fr->snd1_lib = be16(buf + 4); fr->snd1 = be16(buf + 6);
        fr->snd2_lib = be16(buf + 8); fr->snd2 = be16(buf + 10);
        fr->trans_lib = be16(buf + 12); fr->trans = be16(buf + 14);
        fr->tempo = buf[21];
        fr->pal_lib = (int16_t)be16(buf + 24); fr->pal = (int16_t)be16(buf + 26);
        fr->pal_speed = buf[28]; fr->pal_flags = buf[29];
        for (int ch = 0; ch < 48 && ch < nchan - 2; ch++) {
            const uint8_t *s = buf + 48 + ch * sprsize;
            SprRec *r = &fr->spr[ch];
            r->type = s[0];
            r->ink = s[1] & 0x3f; r->trails = (s[1] & 0x40) != 0; r->stretch = (s[1] & 0x80) != 0;
            r->lib = be16(s + 2); r->member = be16(s + 4);
            r->slib = be16(s + 6); r->smember = be16(s + 8);
            r->fore = s[10]; r->back = s[11];
            r->locv = (int16_t)be16(s + 12); r->loch = (int16_t)be16(s + 14);
            r->h = (int16_t)be16(s + 16); r->w = (int16_t)be16(s + 18);
            r->blend = s[21];
        }
        p = end;
    }
    free(buf);
}

void score_parse_ext(Score *sc, const uint8_t *b, uint32_t sz) { score_parse(sc, b, sz); }

/* ------------------------------------------------------------------ film */
static int file_exists(const char *p) { return vfs_exists(p); }

static int find_file(const char *dir, const char *name, const char **exts, char *out, int n) {
    for (int i = 0; exts[i]; i++) {
        snprintf(out, n, "%s/%s%s", dir, name, exts[i]);
        if (file_exists(out)) return 1;
    }
    return 0;
}

Movie *movie_load(const char *path) {
    DFile *f = dfile_open(path);
    if (!f) return NULL;
    Movie *mv = calloc(1, sizeof *mv);
    mv->f = f;
    snprintf(mv->path, sizeof mv->path, "%s", path);
    const char *bn = strrchr(path, '/');
    const char *bn2 = strrchr(path, '\\');
    if (bn2 > bn) bn = bn2;
    bn = bn ? bn + 1 : path;
    snprintf(mv->name, sizeof mv->name, "%s", bn);
    char *dot = strrchr(mv->name, '.');
    if (dot) *dot = 0;
    char dir[260];
    snprintf(dir, sizeof dir, "%.*s", (int)(bn - path), path);
    if (!dir[0]) strcpy(dir, ".");
    else dir[strlen(dir) - 1] = 0;

    const int MOVIE = 1024;
    uint32_t sz;
    const uint8_t *cf = dfile_chunk(f, dfile_owned(f, FOURCC('V', 'W', 'C', 'F'), MOVIE), &sz);
    int min_member = 1;
    mv->stage_w = 640; mv->stage_h = 480; mv->tempo = 15;
    if (cf) {
        int top = (int16_t)be16(cf + 4), left = (int16_t)be16(cf + 6);
        int bottom = (int16_t)be16(cf + 8), right = (int16_t)be16(cf + 10);
        mv->stage_w = right - left; mv->stage_h = bottom - top;
        min_member = be16(cf + 12);
        mv->stage_color = be16(cf + 26);
        mv->tempo = (int16_t)be16(cf + 54);
        mv->def_pal_lib = (int16_t)be16(cf + 76);
        mv->def_pal = (int16_t)be16(cf + 78);
    }
    const uint8_t *sb = dfile_chunk(f, dfile_child(f, MOVIE, FOURCC('V', 'W', 'S', 'C')), &sz);
    score_parse(&mv->score, sb, sz);
    const uint8_t *lb = dfile_chunk(f, dfile_child(f, MOVIE, FOURCC('V', 'W', 'L', 'B')), &sz);
    if (lb && sz >= 2) {
        int n = be16(lb);
        mv->labels = calloc(n + 1, sizeof(Label));
        int base = 2 + (n + 1) * 4;
        for (int i = 0; i < n; i++) {
            int fr = be16(lb + 2 + i * 4), o1 = be16(lb + 4 + i * 4), o2 = be16(lb + 8 + i * 4);
            mv->labels[i].frame = fr;
            snprintf(mv->labels[i].name, 64, "%.*s", o2 - o1, (const char *)lb + base + o1);
        }
        mv->nlabels = n;
    }
    /* cast-libs: MCsL = per lib (naam, pad, preload, (min, max, id)); leeg pad = interne cast */
    const uint8_t *mc = dfile_chunk(f, dfile_child(f, MOVIE, FOURCC('M', 'C', 's', 'L')), &sz);
    int cnt = 0, per = 4, n = 0;
    const uint8_t *offs = NULL, *data = NULL;
    if (mc && sz > 12) {
        uint32_t doff = be32(mc);
        cnt = be16(mc + 6); per = be16(mc + 8);
        n = be16(mc + doff);
        offs = mc + doff + 2;
        data = offs + 4 * n + 4;
    }
    if (cnt == 0) {
        mv->libs[0] = cast_load(f, min_member, 1, MOVIE);
        snprintf(mv->libs[0]->name, 64, "Internal");
        mv->nlibs = 1;
    }
    for (int i = 0; i < cnt && mv->nlibs < 8; i++) {
        int k = 1 + i * per;
        char name[64] = "", path[260] = "";
        int first = 1, id = MOVIE + i;
        if (k < n) {
            uint32_t o = be32(offs + 4 * k), o2 = k + 1 < n ? be32(offs + 4 * (k + 1)) : o;
            if (o2 > o) snprintf(name, sizeof name, "%.*s", data[o], (const char *)data + o + 1);
        }
        if (k + 1 < n) {
            uint32_t o = be32(offs + 4 * (k + 1)), o2 = k + 2 < n ? be32(offs + 4 * (k + 2)) : o;
            if (o2 > o) snprintf(path, sizeof path, "%.*s", data[o], (const char *)data + o + 1);
        }
        if (k + 3 < n) {
            uint32_t o = be32(offs + 4 * (k + 3));
            first = be16(data + o);
            id = (int)be32(data + o + 4);
        }
        if (first < 1) first = 1;
        CastLib *cl = NULL;
        if (!path[0]) cl = cast_load(f, i == 0 ? min_member : first, i + 1, id);
        else {
            static const char *exts[] = {".cxt", ".CXT", ".cst", ".CST", ".Cxt", NULL};
            char fp[260];
            if (find_file(dir, name, exts, fp, sizeof fp)) {
                DFile *ef = dfile_open(fp);
                if (ef) cl = cast_load(ef, first, i + 1, MOVIE);
            }
        }
        if (!cl) { cl = calloc(1, sizeof *cl); cl->lib = i + 1; }
        snprintf(cl->name, 64, "%s", name[0] ? name : "Internal");
        mv->libs[mv->nlibs++] = cl;
    }
    return mv;
}

void movie_free(Movie *mv) {
    /* films worden gecachet; niet vrijgeven (castleden kunnen nog in sprites of lijsten staan) */
    (void)mv;
}

Member *movie_member(Movie *mv, int lib, int num, CastLib **out) {
    if (lib <= 0) lib = 1;
    if (lib > mv->nlibs) return NULL;
    CastLib *c = mv->libs[lib - 1];
    if (out) *out = c;
    return cast_member(c, num);
}

static int stricmp_(const char *a, const char *b) {
    while (*a && *b) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d) return d;
        a++, b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int movie_find_member(Movie *mv, const char *name, int lib) {
    for (int l = 0; l < mv->nlibs; l++) {
        if (lib && l + 1 != lib) continue;
        CastLib *c = mv->libs[l];
        for (int i = 0; i < c->n; i++)
            if (c->m[i].type && c->m[i].name && !stricmp_(c->m[i].name, name))
                return (l + 1) << 16 | (i + c->first);
    }
    return 0;
}

int movie_label(Movie *mv, const char *name) {
    for (int i = 0; i < mv->nlabels; i++)
        if (!stricmp_(mv->labels[i].name, name)) return mv->labels[i].frame;
    return 0;
}
