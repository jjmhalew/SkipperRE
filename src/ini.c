/* ini.c - INI-bestanden zoals Windows' GetPrivateProfileString / GetPrivateProfileInt / WritePrivateProfileString:
 * sectie en sleutel zonder op hoofdletters te letten, waarden zonder spaties eromheen en zonder omsluitende
 * aanhalingstekens, ';' begint een commentaarregel. Schrijven bewaart de rest van het bestand (CRLF). */
#include "plat.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *load(const char *file, long *n) {
    *n = 0;
    FILE *f = fopen(file, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(sz + 1);
    if (!b) { fclose(f); return NULL; }
    sz = (long)fread(b, 1, sz, f);
    fclose(f);
    b[sz] = 0;
    *n = sz;
    return b;
}

static void trim(const char **s, const char **e) {
    while (*s < *e && isspace((unsigned char)**s)) (*s)++;
    while (*e > *s && isspace((unsigned char)(*e)[-1])) (*e)--;
}

/* regel [s, e): sectienaam -> 1 en naam in sect; sleutel=waarde -> 2 */
static int parse(const char *s, const char *e, const char **ks, const char **ke, const char **vs, const char **ve) {
    trim(&s, &e);
    if (s == e || *s == ';') return 0;
    if (*s == '[') {
        const char *c = memchr(s, ']', e - s);
        *ks = s + 1; *ke = c ? c : e;
        trim(ks, ke);
        return 1;
    }
    const char *eq = memchr(s, '=', e - s);
    if (!eq) return 0;
    *ks = s; *ke = eq; *vs = eq + 1; *ve = e;
    trim(ks, ke);
    trim(vs, ve);
    return 2;
}

static int same(const char *s, const char *e, const char *name) {
    size_t l = strlen(name);
    return (size_t)(e - s) == l && !_strnicmp(s, name, l);
}

/* zoekt sleutel in sectie; geeft regelgrenzen terug (begin van de regel, na het regeleinde) */
typedef struct Hit { long sect_end, last, line_s, line_e; const char *vs, *ve; int in_sect; } Hit;

static void find(const char *b, long n, const char *sect, const char *key, Hit *h) {
    memset(h, 0, sizeof *h);
    h->sect_end = h->line_s = -1;
    int in = 0;
    for (long i = 0; i < n;) {
        long j = i;
        while (j < n && b[j] != '\n') j++;
        long next = j < n ? j + 1 : j;
        const char *ks, *ke, *vs = NULL, *ve = NULL;
        int t = parse(b + i, b + j, &ks, &ke, &vs, &ve);
        if (t == 1) {
            if (in) return;
            in = same(ks, ke, sect);
            if (in) { h->in_sect = 1; h->sect_end = h->last = next; }
        } else if (in) {
            h->sect_end = next;
            const char *s = b + i, *e = b + j;
            trim(&s, &e);
            if (s < e) h->last = next;   /* nieuwe sleutels komen na de laatste niet-lege regel */
            if (t == 2 && key && same(ks, ke, key)) { h->line_s = i; h->line_e = next; h->vs = vs; h->ve = ve; return; }
        }
        i = next;
    }
}

int ini_get(const char *file, const char *sect, const char *key, const char *def, char *out, int n) {
    long len;
    char *b = load(file, &len);
    Hit h;
    const char *s = def ? def : "", *e = s + strlen(s);
    if (b) {
        find(b, len, sect, key, &h);
        if (h.line_s >= 0) { s = h.vs; e = h.ve; }
    }
    if (e - s >= 2 && (*s == '"' || *s == '\'') && e[-1] == *s) { s++; e--; }
    int k = (int)(e - s) < n - 1 ? (int)(e - s) : n - 1;
    if (k < 0) k = 0;
    memmove(out, s, k);
    out[k] = 0;
    free(b);
    return k;
}

int ini_get_int(const char *file, const char *sect, const char *key, int def) {
    char v[64];
    char d[2] = "";
    if (!ini_get(file, sect, key, d, v, sizeof v)) return def;
    int r = atoi(v);
    return r;
}

int ini_set(const char *file, const char *sect, const char *key, const char *val) {
    long len;
    char *b = load(file, &len);
    if (!b) { b = calloc(1, 1); len = 0; }
    Hit h;
    find(b, len, sect, key, &h);
    size_t cap = (size_t)len + strlen(sect) + (key ? strlen(key) : 0) + (val ? strlen(val) : 0) + 16;
    char *o = malloc(cap);
    size_t k = 0;
    char line[1200] = "";
    if (key && val) snprintf(line, sizeof line, "%s=%s\r\n", key, val);
    if (!key) {   /* hele sectie weg */
        if (h.in_sect) {
            long s = 0;
            for (long i = 0; i < len;) {   /* begin van de sectiekop zoeken */
                long j = i;
                while (j < len && b[j] != '\n') j++;
                const char *ks, *ke, *vs, *ve;
                if (parse(b + i, b + j, &ks, &ke, &vs, &ve) == 1 && same(ks, ke, sect)) { s = i; break; }
                i = j < len ? j + 1 : j;
            }
            memcpy(o, b, s); k = s;
            memcpy(o + k, b + h.sect_end, len - h.sect_end); k += len - h.sect_end;
        } else { memcpy(o, b, len); k = len; }
    } else if (h.line_s >= 0) {   /* bestaande regel vervangen of weghalen */
        memcpy(o, b, h.line_s); k = h.line_s;
        size_t ll = strlen(line);
        memcpy(o + k, line, ll); k += ll;
        memcpy(o + k, b + h.line_e, len - h.line_e); k += len - h.line_e;
    } else if (!val) { memcpy(o, b, len); k = len; }
    else if (h.in_sect) {   /* nieuwe sleutel aan het eind van de sectie */
        long at = h.last;
        memcpy(o, b, at); k = at;
        if (at > 0 && b[at - 1] != '\n') { o[k++] = '\r'; o[k++] = '\n'; }
        size_t ll = strlen(line);
        memcpy(o + k, line, ll); k += ll;
        memcpy(o + k, b + at, len - at); k += len - at;
    } else {   /* nieuwe sectie aan het eind */
        memcpy(o, b, len); k = len;
        if (len > 0 && b[len - 1] != '\n') { o[k++] = '\r'; o[k++] = '\n'; }
        k += (size_t)snprintf(o + k, cap - k, "[%s]\r\n%s", sect, line);
    }
    free(b);
    FILE *f = fopen(file, "wb");
    if (!f) { free(o); return 0; }
    fwrite(o, 1, k, f);
    fclose(f);
    free(o);
    return 1;
}
