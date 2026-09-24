/* Lingo: waarden, symbolen, globals en de bytecode-VM (Director 5).
 * Opcodes en hun stackgedrag: zie tools/lingodec.py (dezelfde semantiek, leesbaar). */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>

const Datum VOIDD = {0};
int vm_trace = 0;
int vm_pass = 0, vm_dontpass = 0, vm_abort = 0;
Script *vm_cur_script;
Datum vm_cur_me;

/* ------------------------------------------------------------------ symbolen */
static char **g_syms;
static int g_nsyms, g_capsyms;
static int *g_hash;
static int g_hcap;

static unsigned hash_ci(const char *s, int n) {
    unsigned h = 2166136261u;
    for (int i = 0; i < n; i++) h = (h ^ (unsigned)tolower((unsigned char)s[i])) * 16777619u;
    return h;
}

static int ci_eqn(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return b[n] == 0;
}

static void sym_rehash(void) {
    int cap = g_hcap ? g_hcap * 2 : 4096;
    int *h = malloc(sizeof(int) * cap);
    for (int i = 0; i < cap; i++) h[i] = -1;
    for (int i = 0; i < g_nsyms; i++) {
        unsigned k = hash_ci(g_syms[i], (int)strlen(g_syms[i])) & (cap - 1);
        while (h[k] >= 0) k = (k + 1) & (cap - 1);
        h[k] = i;
    }
    free(g_hash);
    g_hash = h;
    g_hcap = cap;
}

int symn(const char *name, int n) {
    if (g_nsyms * 2 >= g_hcap) sym_rehash();
    unsigned k = hash_ci(name, n) & (g_hcap - 1);
    while (g_hash[k] >= 0) {
        if (ci_eqn(name, g_syms[g_hash[k]], n)) return g_hash[k];
        k = (k + 1) & (g_hcap - 1);
    }
    if (g_nsyms == g_capsyms) {
        g_capsyms = g_capsyms ? g_capsyms * 2 : 1024;
        g_syms = realloc(g_syms, sizeof(char *) * g_capsyms);
    }
    char *s = malloc(n + 1);
    memcpy(s, name, n);
    s[n] = 0;
    g_syms[g_nsyms] = s;
    g_hash[k] = g_nsyms;
    return g_nsyms++;
}

int sym(const char *name) { return symn(name, (int)strlen(name)); }
const char *symname(int s) { return s >= 0 && s < g_nsyms ? g_syms[s] : "?"; }

/* ------------------------------------------------------------------ waarden */
Datum d_int(int32_t i) { Datum d = {T_INT}; d.u.i = i; return d; }
Datum d_float(double f) { Datum d = {T_FLOAT}; d.u.f = f; return d; }
Datum d_sym(int s) { Datum d = {T_SYM}; d.u.i = s; return d; }
Datum d_member(int lib, int num) { Datum d = {T_MEMBER}; d.u.i = lib << 16 | (num & 0xffff); return d; }

Datum d_strn(const char *s, int n) {
    Str *st = malloc(sizeof(Str) + n + 1);
    st->rc = 1;
    st->len = n;
    memcpy(st->s, s, n);
    st->s[n] = 0;
    Datum d = {T_STR};
    d.u.s = st;
    return d;
}
Datum d_str(const char *s) { return d_strn(s, (int)strlen(s)); }

Datum d_list(int type, int cap) {
    List *l = calloc(1, sizeof *l);
    l->rc = 1;
    l->cap = cap > 4 ? cap : 4;
    l->v = calloc(l->cap, sizeof(Datum));
    Datum d = {(uint8_t)type};
    d.u.l = l;
    return d;
}

void list_push(List *l, Datum d) {
    if (l->n == l->cap) { l->cap *= 2; l->v = realloc(l->v, l->cap * sizeof(Datum)); }
    l->v[l->n++] = d;
}

Datum d_point(int x, int y) {
    Datum d = d_list(T_POINT, 2);
    list_push(d.u.l, d_int(x)); list_push(d.u.l, d_int(y));
    return d;
}
Datum d_rect(int l, int t, int r, int b) {
    Datum d = d_list(T_RECT, 4);
    list_push(d.u.l, d_int(l)); list_push(d.u.l, d_int(t));
    list_push(d.u.l, d_int(r)); list_push(d.u.l, d_int(b));
    return d;
}

static int is_rc(int t) {
    return t == T_STR || t == T_LIST || t == T_PLIST || t == T_POINT || t == T_RECT || t == T_OBJ || t == T_XOBJ;
}

Datum d_ref(Datum d) {
    switch (d.t) {
    case T_STR: d.u.s->rc++; break;
    case T_LIST: case T_PLIST: case T_POINT: case T_RECT: d.u.l->rc++; break;
    case T_OBJ: d.u.o->rc++; break;
    case T_XOBJ: d.u.x->rc++; break;
    }
    return d;
}

void d_unref(Datum d) {
    if (!is_rc(d.t)) return;
    switch (d.t) {
    case T_STR: if (--d.u.s->rc == 0) free(d.u.s); break;
    case T_LIST: case T_PLIST: case T_POINT: case T_RECT:
        if (--d.u.l->rc == 0) {
            for (int i = 0; i < d.u.l->n; i++) d_unref(d.u.l->v[i]);
            free(d.u.l->v);
            free(d.u.l);
        }
        break;
    case T_OBJ:
        /* objecten verwijzen vaak naar elkaar (ancestor, lijsten): niet vrijgeven */
        if (d.u.o->rc > 0) d.u.o->rc--;
        break;
    case T_XOBJ:
        if (d.u.x->rc > 0) d.u.x->rc--;
        break;
    }
}

static int fmt_float(double f, char *buf, int n) {
    /* Director toont floats met floatPrecision 4 */
    return snprintf(buf, n, "%.4f", f);
}

static void append(char **buf, int *len, int *cap, const char *s) {
    int n = (int)strlen(s);
    if (*len + n + 1 > *cap) { *cap = (*len + n + 1) * 2; *buf = realloc(*buf, *cap); }
    memcpy(*buf + *len, s, n + 1);
    *len += n;
}

static void d_text(Datum d, char **buf, int *len, int *cap, int quote) {
    char tmp[64];
    switch (d.t) {
    case T_VOID: append(buf, len, cap, quote ? "<Void>" : ""); break;
    case T_INT: snprintf(tmp, sizeof tmp, "%d", d.u.i); append(buf, len, cap, tmp); break;
    case T_FLOAT: fmt_float(d.u.f, tmp, sizeof tmp); append(buf, len, cap, tmp); break;
    case T_STR:
        if (quote) append(buf, len, cap, "\"");
        append(buf, len, cap, d.u.s->s);
        if (quote) append(buf, len, cap, "\"");
        break;
    case T_SYM:
        if (quote) append(buf, len, cap, "#");
        append(buf, len, cap, symname(d.u.i));
        break;
    case T_LIST: case T_PLIST: case T_POINT: case T_RECT: {
        List *l = d.u.l;
        if (d.t == T_POINT) append(buf, len, cap, "point(");
        else if (d.t == T_RECT) append(buf, len, cap, "rect(");
        else append(buf, len, cap, "[");
        if (d.t == T_PLIST) {
            if (l->n == 0) append(buf, len, cap, ":");
            for (int i = 0; i + 1 < l->n; i += 2) {
                if (i) append(buf, len, cap, ", ");
                d_text(l->v[i], buf, len, cap, 1);
                append(buf, len, cap, ": ");
                d_text(l->v[i + 1], buf, len, cap, 1);
            }
        } else
            for (int i = 0; i < l->n; i++) {
                if (i) append(buf, len, cap, ", ");
                d_text(l->v[i], buf, len, cap, 1);
            }
        append(buf, len, cap, d.t == T_POINT || d.t == T_RECT ? ")" : "]");
        break;
    }
    case T_OBJ:
        snprintf(tmp, sizeof tmp, "<offspring %d>", d.u.o->script ? d.u.o->script->member : 0);
        append(buf, len, cap, tmp);
        break;
    case T_SCRIPT:
        snprintf(tmp, sizeof tmp, "(script %d)", d.u.sc ? d.u.sc->member : 0);
        append(buf, len, cap, tmp);
        break;
    case T_MEMBER:
        snprintf(tmp, sizeof tmp, "(member %d of castLib %d)", d.u.i & 0xffff, d.u.i >> 16);
        append(buf, len, cap, tmp);
        break;
    case T_XOBJ: append(buf, len, cap, "<Object:XObject>"); break;
    case T_WINDOW: append(buf, len, cap, "(window)"); break;
    case T_CASTLIB: snprintf(tmp, sizeof tmp, "(castLib %d)", d.u.i); append(buf, len, cap, tmp); break;
    default: append(buf, len, cap, "?"); break;
    }
}

Str *d_asstr(Datum d) {
    if (d.t == T_STR) { d.u.s->rc++; return d.u.s; }
    int len = 0, cap = 32;
    char *buf = malloc(cap);
    buf[0] = 0;
    d_text(d, &buf, &len, &cap, 0);
    Datum r = d_strn(buf, len);
    free(buf);
    return r.u.s;
}

const char *d_tostr(Datum d, char *out, int n) {
    Str *s = d_asstr(d);
    snprintf(out, n, "%s", s->s);
    if (--s->rc == 0) free(s);
    return out;
}

void d_print(FILE *f, Datum d) {
    int len = 0, cap = 64;
    char *buf = malloc(cap);
    buf[0] = 0;
    d_text(d, &buf, &len, &cap, 1);
    fputs(buf, f);
    free(buf);
}

static int parse_num(const char *s, double *out, int *isint) {
    while (isspace((unsigned char)*s)) s++;
    if (!*s) return 0;
    char *e;
    double v = strtod(s, &e);
    if (e == s) return 0;
    while (isspace((unsigned char)*e)) e++;
    if (*e) return 0;
    *out = v;
    *isint = !strchr(s, '.') && !strchr(s, 'e') && !strchr(s, 'E');
    return 1;
}

int d_toint(Datum d) {
    switch (d.t) {
    case T_INT: case T_SYM: return d.u.i;
    case T_FLOAT: return (int)lround(d.u.f);
    case T_STR: { double v; int ii; return parse_num(d.u.s->s, &v, &ii) ? (int)v : 0; }
    case T_MEMBER: return d.u.i & 0xffff;
    default: return 0;
    }
}

double d_tofloat(Datum d) {
    switch (d.t) {
    case T_INT: return d.u.i;
    case T_FLOAT: return d.u.f;
    case T_STR: { double v; int ii; return parse_num(d.u.s->s, &v, &ii) ? v : 0; }
    default: return 0;
    }
}

int d_truthy(Datum d) {
    switch (d.t) {
    case T_VOID: return 0;
    case T_INT: return d.u.i != 0;
    case T_FLOAT: return d.u.f != 0;
    case T_STR: { double v; int ii; return parse_num(d.u.s->s, &v, &ii) ? v != 0 : 0; }
    default: return 1;
    }
}

static int ci_cmp(const char *a, const char *b) {
    while (*a && *b) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d) return d;
        a++, b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int is_num(Datum d) { return d.t == T_INT || d.t == T_FLOAT; }

int d_compare(Datum a, Datum b) {
    if (is_num(a) && is_num(b)) {
        double x = d_tofloat(a), y = d_tofloat(b);
        return x < y ? -1 : x > y;
    }
    if ((a.t == T_STR && is_num(b)) || (is_num(a) && b.t == T_STR)) {
        double v; int ii;
        Datum s = a.t == T_STR ? a : b;
        if (parse_num(s.u.s->s, &v, &ii)) {
            double x = a.t == T_STR ? v : d_tofloat(a), y = b.t == T_STR ? v : d_tofloat(b);
            return x < y ? -1 : x > y;
        }
    }
    if (a.t == T_VOID && is_num(b)) return d_compare(d_int(0), b);
    if (is_num(a) && b.t == T_VOID) return d_compare(a, d_int(0));
    char ba[1024], bb[1024];
    return ci_cmp(d_tostr(a, ba, sizeof ba), d_tostr(b, bb, sizeof bb));
}

int d_equal(Datum a, Datum b) {
    if (a.t == T_VOID || b.t == T_VOID) {
        if (a.t == b.t) return 1;
        Datum o = a.t == T_VOID ? b : a;
        return is_num(o) && d_tofloat(o) == 0;
    }
    if (a.t == T_SYM && b.t == T_SYM) return a.u.i == b.u.i;
    if (a.t == T_OBJ || b.t == T_OBJ) return a.t == b.t && a.u.o == b.u.o;
    if (a.t == T_XOBJ || b.t == T_XOBJ) return a.t == b.t && a.u.x == b.u.x;
    if (a.t == T_MEMBER && b.t == T_MEMBER) return a.u.i == b.u.i;
    if ((a.t == T_LIST || a.t == T_POINT || a.t == T_RECT || a.t == T_PLIST) && a.t == b.t) {
        if (a.u.l->n != b.u.l->n) return 0;
        for (int i = 0; i < a.u.l->n; i++)
            if (!d_equal(a.u.l->v[i], b.u.l->v[i])) return 0;
        return 1;
    }
    if (a.t == T_SYM || b.t == T_SYM) {
        if (a.t == T_STR || b.t == T_STR) {
            Datum s = a.t == T_STR ? a : b, y = a.t == T_SYM ? a : b;
            return !ci_cmp(s.u.s->s, symname(y.u.i));
        }
        return 0;
    }
    return d_compare(a, b) == 0;
}

/* ------------------------------------------------------------------ globals */
typedef struct GEnt { int name; Datum v; } GEnt;
static GEnt *g_glob;
static int g_nglob, g_capglob;

Datum *global_ref(int name) {
    for (int i = 0; i < g_nglob; i++)
        if (g_glob[i].name == name) return &g_glob[i].v;
    if (g_nglob == g_capglob) {
        g_capglob = g_capglob ? g_capglob * 2 : 256;
        g_glob = realloc(g_glob, sizeof(GEnt) * g_capglob);
    }
    g_glob[g_nglob].name = name;
    g_glob[g_nglob].v = VOIDD;
    return &g_glob[g_nglob++].v;
}

void globals_clear(void) {
    for (int i = 0; i < g_nglob; i++) {
        /* XObject-factories (INI, FileIO, ...) blijven staan */
        if (g_glob[i].v.t == T_XOBJ && g_glob[i].v.u.x->is_factory) continue;
        d_unref(g_glob[i].v);
        g_glob[i].v = VOIDD;
    }
}

/* ------------------------------------------------------------------ builtins */
typedef struct BEnt { int name; Builtin fn; } BEnt;
static BEnt *g_bi;
static int g_nbi, g_capbi;

void vm_register(const char *name, Builtin fn) {
    if (g_nbi == g_capbi) { g_capbi = g_capbi ? g_capbi * 2 : 256; g_bi = realloc(g_bi, sizeof(BEnt) * g_capbi); }
    g_bi[g_nbi].name = sym(name);
    g_bi[g_nbi++].fn = fn;
}

Builtin vm_builtin(int name) {
    for (int i = 0; i < g_nbi; i++)
        if (g_bi[i].name == name) return g_bi[i].fn;
    return NULL;
}

void vm_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[lingo] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/* ------------------------------------------------------------------ objecten */
static int S_ancestor = -1;

Datum obj_getprop(Obj *o, int name, int *found) {
    if (S_ancestor < 0) S_ancestor = sym("ancestor");
    for (int depth = 0; o && depth < 32; depth++) {
        for (int i = 0; i < o->n; i++)
            if (o->names[i] == name) { if (found) *found = 1; return o->vals[i]; }
        Datum anc = VOIDD;
        for (int i = 0; i < o->n; i++)
            if (o->names[i] == S_ancestor) { anc = o->vals[i]; break; }
        o = anc.t == T_OBJ ? anc.u.o : NULL;
    }
    if (found) *found = 0;
    return VOIDD;
}

int obj_setprop(Obj *o, int name, Datum v) {
    if (S_ancestor < 0) S_ancestor = sym("ancestor");
    Obj *start = o;
    for (int depth = 0; o && depth < 32; depth++) {
        for (int i = 0; i < o->n; i++)
            if (o->names[i] == name) { d_unref(o->vals[i]); o->vals[i] = v; return 1; }
        Datum anc = VOIDD;
        for (int i = 0; i < o->n; i++)
            if (o->names[i] == S_ancestor) { anc = o->vals[i]; break; }
        o = anc.t == T_OBJ ? anc.u.o : NULL;
    }
    /* onbekend: toevoegen aan het object zelf (Director geeft een fout, maar dit is robuuster) */
    o = start;
    o->names = realloc(o->names, sizeof(int) * (o->n + 1));
    o->vals = realloc(o->vals, sizeof(Datum) * (o->n + 1));
    o->names[o->n] = name;
    o->vals[o->n++] = v;
    return 0;
}

Handler *obj_handler(Obj *o, int name, Script **sc) {
    if (S_ancestor < 0) S_ancestor = sym("ancestor");
    for (int depth = 0; o && depth < 32; depth++) {
        Handler *h = script_handler(o->script, name);
        if (h) { if (sc) *sc = o->script; return h; }
        Datum anc = VOIDD;
        for (int i = 0; i < o->n; i++)
            if (o->names[i] == S_ancestor) { anc = o->vals[i]; break; }
        o = anc.t == T_OBJ ? anc.u.o : NULL;
    }
    return NULL;
}

Datum obj_new(Script *s, Datum *args, int n) {
    if (S_ancestor < 0) S_ancestor = sym("ancestor");
    Obj *o = calloc(1, sizeof *o);
    o->rc = 1;
    o->script = s;
    int has_anc = 0;
    for (int i = 0; i < s->nprops; i++) if (s->props[i] == S_ancestor) has_anc = 1;
    o->n = s->nprops + !has_anc;
    o->names = malloc(sizeof(int) * (o->n + 1));
    o->vals = calloc(o->n + 1, sizeof(Datum));
    for (int i = 0; i < s->nprops; i++) o->names[i] = s->props[i];
    if (!has_anc) o->names[s->nprops] = S_ancestor;
    Datum me = {T_OBJ};
    me.u.o = o;
    /* birth/new-handler met me als eerste argument */
    Handler *h = script_handler(s, sym("birth"));
    if (!h) h = script_handler(s, sym("new"));
    if (h) {
        Datum *a = malloc(sizeof(Datum) * (n + 1));
        a[0] = me;
        for (int i = 0; i < n; i++) a[i + 1] = args[i];
        Datum r = vm_call(s, h, a, n + 1);
        free(a);
        if (r.t == T_OBJ) return r;
        d_unref(r);
    }
    return me;
}

/* ------------------------------------------------------------------ chunks */
static int is_delim(char c, int kind, char item_delim) {
    if (kind == 2) return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    if (kind == 3) return c == item_delim;
    if (kind == 4) return c == '\r' || c == '\n';
    return 0;
}

/* zoek bereik [*b, *e) van chunk first..last (1-based) van soort kind in s[0..n) */
static void chunk_range(const char *s, int n, int kind, int first, int last, int *b, int *e) {
    if (last < first) last = first;
    if (kind == 1) {
        *b = first - 1 < n ? first - 1 : n;
        *e = last < n ? last : n;
        if (*b < 0) *b = 0;
        return;
    }
    char idl = ',';
    int idx = 1, i = 0, start = -1, end = n;
    if (kind == 2) {
        /* woorden: reeksen niet-spaties */
        int w = 0;
        *b = *e = n;
        while (i < n) {
            while (i < n && is_delim(s[i], 2, idl)) i++;
            if (i >= n) break;
            int ws = i;
            while (i < n && !is_delim(s[i], 2, idl)) i++;
            w++;
            if (w == first) *b = ws;
            if (w == last) { *e = i; return; }
        }
        if (*b < n) *e = n;
        return;
    }
    start = (first == 1) ? 0 : -1;
    for (i = 0; i < n; i++) {
        if (is_delim(s[i], kind, idl)) {
            if (idx == last) { end = i; break; }
            idx++;
            if (idx == first) start = i + 1;
        }
    }
    if (start < 0) { *b = *e = n; return; }
    *b = start;
    *e = end;
}

static Datum get_chunk(Datum str, int v[8]) {
    /* v: firstChar lastChar firstWord lastWord firstItem lastItem firstLine lastLine */
    Str *s = d_asstr(str);
    int b = 0, e = s->len;
    int kinds[4] = {4, 3, 2, 1};
    int idx[4][2] = {{v[6], v[7]}, {v[4], v[5]}, {v[2], v[3]}, {v[0], v[1]}};
    for (int k = 0; k < 4; k++) {
        if (!idx[k][0]) continue;
        int bb, ee;
        chunk_range(s->s + b, e - b, kinds[k], idx[k][0], idx[k][1] ? idx[k][1] : idx[k][0], &bb, &ee);
        e = b + ee;
        b = b + bb;
    }
    Datum r = d_strn(s->s + b, e > b ? e - b : 0);
    if (--s->rc == 0) free(s);
    return r;
}

int chunk_count(const char *s, int kind) {
    int n = (int)strlen(s);
    if (kind == 1) return n;
    if (kind == 2) {
        int c = 0, i = 0;
        while (i < n) {
            while (i < n && is_delim(s[i], 2, ',')) i++;
            if (i >= n) break;
            c++;
            while (i < n && !is_delim(s[i], 2, ',')) i++;
        }
        return c;
    }
    if (n == 0) return 0;
    int c = 1;
    for (int i = 0; i < n; i++)
        if (is_delim(s[i], kind, ',')) c++;
    if (kind == 4 && (s[n - 1] == '\r' || s[n - 1] == '\n')) c--;
    return c;
}

/* put x into/after/before chunk van een string -> nieuwe string */
static Datum put_chunk(Datum target, int v[8], Datum val, int how) {
    Str *s = d_asstr(target);
    int b = 0, e = s->len;
    int kinds[4] = {4, 3, 2, 1};
    int idx[4][2] = {{v[6], v[7]}, {v[4], v[5]}, {v[2], v[3]}, {v[0], v[1]}};
    for (int k = 0; k < 4; k++) {
        if (!idx[k][0]) continue;
        int bb, ee;
        chunk_range(s->s + b, e - b, kinds[k], idx[k][0], idx[k][1] ? idx[k][1] : idx[k][0], &bb, &ee);
        e = b + ee;
        b = b + bb;
    }
    Str *x = d_asstr(val);
    int nb = how == 3 ? b : how == 2 ? e : b, ne = how == 1 ? e : nb;
    int len = nb + x->len + (s->len - ne);
    char *buf = malloc(len + 1);
    memcpy(buf, s->s, nb);
    memcpy(buf + nb, x->s, x->len);
    memcpy(buf + nb + x->len, s->s + ne, s->len - ne);
    Datum r = d_strn(buf, len);
    free(buf);
    if (--s->rc == 0) free(s);
    if (--x->rc == 0) free(x);
    return r;
}

/* ------------------------------------------------------------------ aritmetiek */
static Datum arith(int op, Datum a, Datum b);

static Datum arith_list(int op, Datum a, Datum b) {
    Datum l = (a.t == T_LIST || a.t == T_POINT || a.t == T_RECT) ? a : b;
    Datum r = d_list(l.t, l.u.l->n);
    for (int i = 0; i < l.u.l->n; i++) {
        Datum x = (a.t == l.t && a.u.l == l.u.l) ? a.u.l->v[i]
                  : (a.t == T_LIST || a.t == T_POINT || a.t == T_RECT) ? (i < a.u.l->n ? a.u.l->v[i] : VOIDD) : a;
        Datum y = (b.t == T_LIST || b.t == T_POINT || b.t == T_RECT) ? (i < b.u.l->n ? b.u.l->v[i] : VOIDD) : b;
        list_push(r.u.l, arith(op, x, y));
    }
    return r;
}

static Datum arith(int op, Datum a, Datum b) {
    if (a.t == T_LIST || a.t == T_POINT || a.t == T_RECT || b.t == T_LIST || b.t == T_POINT || b.t == T_RECT)
        return arith_list(op, a, b);
    int fl = a.t == T_FLOAT || b.t == T_FLOAT;
    if (a.t == T_STR) { double v; int ii; if (parse_num(a.u.s->s, &v, &ii) && !ii) fl = 1; }
    if (b.t == T_STR) { double v; int ii; if (parse_num(b.u.s->s, &v, &ii) && !ii) fl = 1; }
    if (fl) {
        double x = d_tofloat(a), y = d_tofloat(b);
        switch (op) {
        case 0: return d_float(x + y);
        case 1: return d_float(x - y);
        case 2: return d_float(x * y);
        case 3: return d_float(y != 0 ? x / y : 0);
        case 4: return d_float(y != 0 ? fmod(x, y) : 0);
        }
    }
    int32_t x = d_toint(a), y = d_toint(b);
    switch (op) {
    case 0: return d_int(x + y);
    case 1: return d_int(x - y);
    case 2: return d_int(x * y);
    case 3: return d_int(y ? x / y : 0);
    case 4: return d_int(y ? x % y : 0);
    }
    return VOIDD;
}

static Datum concat(Datum a, Datum b, int pad) {
    Str *x = d_asstr(a), *y = d_asstr(b);
    int n = x->len + y->len + pad;
    char *buf = malloc(n + 1);
    memcpy(buf, x->s, x->len);
    if (pad) buf[x->len] = ' ';
    memcpy(buf + x->len + pad, y->s, y->len);
    Datum r = d_strn(buf, n);
    free(buf);
    if (--x->rc == 0) free(x);
    if (--y->rc == 0) free(y);
    return r;
}

static int contains_ci(const char *h, const char *n, int starts) {
    int ln = (int)strlen(n), lh = (int)strlen(h);
    for (int i = 0; i + ln <= lh; i++) {
        int ok = 1;
        for (int k = 0; k < ln && ok; k++)
            if (tolower((unsigned char)h[i + k]) != tolower((unsigned char)n[k])) ok = 0;
        if (ok) return 1;
        if (starts) return 0;
    }
    return ln == 0;
}

/* ------------------------------------------------------------------ interpreter */
#define STACK_MAX 4096
static Datum g_stack[STACK_MAX];
static int g_sp;
static int g_depth;

static void push(Datum d) {
    if (g_sp >= STACK_MAX) { vm_error("stack overflow"); vm_abort = 1; d_unref(d); return; }
    g_stack[g_sp++] = d;
}
static Datum pop(void) { return g_sp > 0 ? g_stack[--g_sp] : VOIDD; }

typedef struct ArgList { int n; Datum *v; int noret; } ArgList;

/* arglijst op de stack: T_LIST-achtige markering; we gebruiken een aparte type-waarde 200 */
#define T_ARGS 200
static Datum make_args(int n, int noret) {
    Datum d = d_list(T_LIST, n);
    d.t = T_ARGS;
    d.u.l->n = n;
    for (int i = n - 1; i >= 0; i--) d.u.l->v[i] = pop();
    d.u.l->cap = noret ? -d.u.l->cap : d.u.l->cap;  /* teken = noret-vlag */
    return d;
}
static int args_noret(Datum a) { return a.u.l->cap < 0; }
static void args_free(Datum a) {
    if (a.t != T_ARGS) { d_unref(a); return; }
    for (int i = 0; i < a.u.l->n; i++) d_unref(a.u.l->v[i]);
    free(a.u.l->v);
    free(a.u.l);
}

typedef struct VFrame {
    Script *s;
    Handler *h;
    Datum *args;
    int nargs;
    Datum *locals;
    Datum ret;
    int done;
} VFrame;

static VFrame *g_cur;
Datum player_movie_prop(int name);
void player_set_movie_prop(int name, Datum v);
Datum player_get(int type, int id, Datum *target, int ntarget);
void player_set(int type, int id, Datum *target, int ntarget, Datum v);
Datum player_objprop(Datum obj, int name);
void player_set_objprop(Datum obj, int name, Datum v);
Datum player_field(Datum id, Datum lib);
void player_set_field(Datum id, Datum lib, Datum v);
int player_sprite_intersects(int a, int b, int within);
void player_tell_begin(Datum win);
void player_tell_end(void);

static Datum call_builtin_or_handler(int name, Datum *a, int n);

/* variabele lezen/schrijven volgens vartype (1/2 global, 3 prop, 4 arg, 5 local, 6 field) */
static Datum *var_slot(int vt, Datum id, Datum lib, Datum *field_tmp) {
    VFrame *f = g_cur;
    switch (vt) {
    case 1: case 2: {
        int nm = id.t == T_SYM || id.t == T_VARREF ? id.u.i : (id.t == T_STR ? sym(id.u.s->s) : d_toint(id));
        if (id.t == T_INT && f && f->s && id.u.i < f->s->nnames) nm = f->s->names[id.u.i];
        return global_ref(nm);
    }
    case 3: {
        int nm = id.t == T_SYM || id.t == T_VARREF ? id.u.i : (id.t == T_STR ? sym(id.u.s->s) : -1);
        if (id.t == T_INT && f && f->s && id.u.i < f->s->nnames) nm = f->s->names[id.u.i];
        if (f && f->nargs > 0 && f->args[0].t == T_OBJ) {
            Obj *o = f->args[0].u.o;
            for (int depth = 0; o && depth < 32; depth++) {
                for (int i = 0; i < o->n; i++)
                    if (o->names[i] == nm) return &o->vals[i];
                Datum anc = VOIDD;
                for (int i = 0; i < o->n; i++)
                    if (o->names[i] == sym("ancestor")) { anc = o->vals[i]; break; }
                o = anc.t == T_OBJ ? anc.u.o : NULL;
            }
        }
        return global_ref(nm);
    }
    case 4: { int i = d_toint(id) / 8; return f && i < f->nargs ? &f->args[i] : NULL; }
    case 5: { int i = d_toint(id) / 8; return f && i < f->h->nlocals ? &f->locals[i] : NULL; }
    case 6:
        *field_tmp = player_field(id, lib);
        return field_tmp;
    }
    return NULL;
}

static void read8(int v[8]) { for (int i = 7; i >= 0; i--) { Datum d = pop(); v[i] = d_toint(d); d_unref(d); } }

Datum vm_call(Script *s, Handler *h, Datum *args, int n) {
    if (!h) return VOIDD;
    if (g_depth > 200) { vm_error("recursie te diep in %s", symname(h->name)); vm_abort = 1; return VOIDD; }
    VFrame fr = {0};
    fr.s = s;
    fr.h = h;
    fr.nargs = n > h->nargs ? n : h->nargs;
    fr.args = calloc(fr.nargs + 1, sizeof(Datum));
    for (int i = 0; i < n; i++) fr.args[i] = d_ref(args[i]);
    fr.locals = calloc(h->nlocals + 1, sizeof(Datum));
    VFrame *prev = g_cur;
    Script *prev_script = vm_cur_script;
    g_cur = &fr;
    vm_cur_script = s;
    g_depth++;
    int base_sp = g_sp;
    if (vm_trace) {
        fprintf(stderr, "%*s> %s(", g_depth * 2, "", symname(h->name));
        for (int i = 0; i < n; i++) { if (i) fprintf(stderr, ", "); d_print(stderr, args[i]); }
        fprintf(stderr, ")\n");
    }
    const uint8_t *code = h->code;
    int pc = 0, len = h->clen;
    while (pc < len && !fr.done && !vm_abort) {
        int pos = pc;
        int op = code[pc++];
        int32_t arg = 0;
        if (op >= 0x40) {
            if (op >= 0xc0) { arg = (int32_t)((uint32_t)code[pc] << 24 | code[pc + 1] << 16 | code[pc + 2] << 8 | code[pc + 3]); pc += 4; }
            else if (op >= 0x80) { arg = code[pc] << 8 | code[pc + 1]; pc += 2; }
            else arg = code[pc++];
            op = 0x40 + op % 0x40;
        }
#define NAME(a) ((a) >= 0 && (a) < s->nnames ? s->names[a] : sym("?"))
        Datum a, b, r;
        switch (op) {
        case 0x01: case 0x02: fr.done = 1; break;                 /* ret */
        case 0x03: push(d_int(0)); break;
        case 0x04: b = pop(); a = pop(); push(arith(2, a, b)); d_unref(a); d_unref(b); break;
        case 0x05: b = pop(); a = pop(); push(arith(0, a, b)); d_unref(a); d_unref(b); break;
        case 0x06: b = pop(); a = pop(); push(arith(1, a, b)); d_unref(a); d_unref(b); break;
        case 0x07: b = pop(); a = pop(); push(arith(3, a, b)); d_unref(a); d_unref(b); break;
        case 0x08: b = pop(); a = pop(); push(arith(4, a, b)); d_unref(a); d_unref(b); break;
        case 0x09: a = pop(); push(arith(1, d_int(0), a)); d_unref(a); break;
        case 0x0a: b = pop(); a = pop(); push(concat(a, b, 0)); d_unref(a); d_unref(b); break;
        case 0x0b: b = pop(); a = pop(); push(concat(a, b, 1)); d_unref(a); d_unref(b); break;
        case 0x0c: b = pop(); a = pop(); push(d_int(d_compare(a, b) < 0)); d_unref(a); d_unref(b); break;
        case 0x0d: b = pop(); a = pop(); push(d_int(d_compare(a, b) <= 0)); d_unref(a); d_unref(b); break;
        case 0x0e: b = pop(); a = pop(); push(d_int(!d_equal(a, b))); d_unref(a); d_unref(b); break;
        case 0x0f: b = pop(); a = pop(); push(d_int(d_equal(a, b))); d_unref(a); d_unref(b); break;
        case 0x10: b = pop(); a = pop(); push(d_int(d_compare(a, b) > 0)); d_unref(a); d_unref(b); break;
        case 0x11: b = pop(); a = pop(); push(d_int(d_compare(a, b) >= 0)); d_unref(a); d_unref(b); break;
        case 0x12: b = pop(); a = pop(); push(d_int(d_truthy(a) && d_truthy(b))); d_unref(a); d_unref(b); break;
        case 0x13: b = pop(); a = pop(); push(d_int(d_truthy(a) || d_truthy(b))); d_unref(a); d_unref(b); break;
        case 0x14: a = pop(); push(d_int(!d_truthy(a))); d_unref(a); break;
        case 0x15: case 0x16: {
            b = pop(); a = pop();
            char ba[2048], bb[512];
            push(d_int(contains_ci(d_tostr(a, ba, sizeof ba), d_tostr(b, bb, sizeof bb), op == 0x16)));
            d_unref(a); d_unref(b);
            break;
        }
        case 0x17: { a = pop(); int v[8]; read8(v); push(get_chunk(a, v)); d_unref(a); break; }
        case 0x18: { a = pop(); int v[8]; read8(v); d_unref(a); break; }          /* hilite */
        case 0x19: case 0x1a: { b = pop(); a = pop();
            push(d_int(player_sprite_intersects(d_toint(a), d_toint(b), op == 0x1a))); break; }
        case 0x1b: { b = pop(); a = pop(); push(player_field(a, b)); d_unref(a); d_unref(b); break; }
        case 0x1c: a = pop(); player_tell_begin(a); d_unref(a); break;
        case 0x1d: player_tell_end(); break;
        case 0x1e: {
            a = pop();
            r = d_list(T_LIST, a.u.l->n);
            for (int i = 0; i < a.u.l->n; i++) list_push(r.u.l, d_ref(a.u.l->v[i]));
            args_free(a);
            push(r);
            break;
        }
        case 0x1f: {
            a = pop();
            r = d_list(T_PLIST, a.u.l->n);
            for (int i = 0; i < a.u.l->n; i++) list_push(r.u.l, d_ref(a.u.l->v[i]));
            args_free(a);
            push(r);
            break;
        }
        case 0x21: if (g_sp >= 2) { a = g_stack[g_sp - 1]; g_stack[g_sp - 1] = g_stack[g_sp - 2]; g_stack[g_sp - 2] = a; } break;
        case 0x41: case 0x6e: case 0x6f:
            if (op == 0x41 && arg >= 0x80 && pos + 2 == pc) arg = (int8_t)arg;
            if (op == 0x41 && pos + 3 == pc) arg = (int16_t)arg;
            if (op == 0x6e) arg = (int16_t)arg;
            push(d_int(arg));
            break;
        case 0x42: push(make_args(arg, 1)); break;
        case 0x43: push(make_args(arg, 0)); break;
        case 0x44: { int i = arg / 8; push(i < s->nlits ? d_ref(s->lits[i]) : VOIDD); break; }
        case 0x45: push(d_sym(NAME(arg))); break;
        case 0x46: { Datum d = {T_VARREF}; d.u.i = NAME(arg); push(d); break; }
        case 0x48: case 0x49: push(d_ref(*global_ref(NAME(arg)))); break;
        case 0x4a: { Datum id = d_sym(NAME(arg)); Datum t; Datum *p = var_slot(3, id, VOIDD, &t); push(p ? d_ref(*p) : VOIDD); break; }
        case 0x4b: { int i = arg / 8; push(i < fr.nargs ? d_ref(fr.args[i]) : VOIDD); break; }
        case 0x4c: { int i = arg / 8; push(i < h->nlocals ? d_ref(fr.locals[i]) : VOIDD); break; }
        case 0x4e: case 0x4f: { Datum *p = global_ref(NAME(arg)); d_unref(*p); *p = pop(); break; }
        case 0x50: { Datum id = d_sym(NAME(arg)); Datum t; Datum v = pop();
            if (fr.nargs > 0 && fr.args[0].t == T_OBJ) obj_setprop(fr.args[0].u.o, id.u.i, v);
            else { Datum *p = var_slot(3, id, VOIDD, &t); if (p) { d_unref(*p); *p = v; } else d_unref(v); }
            break; }
        case 0x51: { int i = arg / 8; Datum v = pop(); if (i < fr.nargs) { d_unref(fr.args[i]); fr.args[i] = v; } else d_unref(v); break; }
        case 0x52: { int i = arg / 8; Datum v = pop(); if (i < h->nlocals) { d_unref(fr.locals[i]); fr.locals[i] = v; } else d_unref(v); break; }
        case 0x53: pc = pos + arg; break;
        case 0x54: pc = pos - arg; break;
        case 0x55: a = pop(); if (!d_truthy(a)) pc = pos + arg; d_unref(a); break;
        case 0x56: {
            a = pop();
            Datum res;
            /* birth/new(script "X") blijft de constructor, ook als dit script zelf 'birth' heeft */
            Handler *other = NULL;
            Script *osc = NULL;
            if (arg < s->nh && a.u.l->n > 0) {
                /* ook bij een lokale aanroep wint een script/object als eerste argument dat de
                 * handler zelf heeft: mouseUp(script "X") in een script met eigen mouseUp */
                Datum a0 = a.u.l->v[0];
                if (a0.t == T_SCRIPT && a0.u.sc != s) { osc = a0.u.sc; other = script_handler(osc, s->h[arg].name); }
                else if (a0.t == T_OBJ) other = obj_handler(a0.u.o, s->h[arg].name, &osc);
                if (other && osc == s && other == &s->h[arg]) other = NULL;
            }
            if (arg < s->nh && a.u.l->n > 0 && a.u.l->v[0].t == T_SCRIPT
                && (s->h[arg].name == sym("birth") || s->h[arg].name == sym("new")))
                res = obj_new(a.u.l->v[0].u.sc, a.u.l->v + 1, a.u.l->n - 1);
            else if (other) res = vm_call(osc, other, a.u.l->v, a.u.l->n);
            else res = arg < s->nh ? vm_call(s, &s->h[arg], a.u.l->v, a.u.l->n) : VOIDD;
            int nr = args_noret(a);
            args_free(a);
            if (nr) d_unref(res); else push(res);
            break;
        }
        case 0x57: case 0x63: {
            a = pop();
            int nm = NAME(arg);
            Datum res;
            if (nm == sym("return")) {
                d_unref(fr.ret);
                fr.ret = a.u.l->n ? d_ref(a.u.l->v[0]) : VOIDD;
                res = VOIDD;
            } else if (nm == sym("pass")) {
                vm_pass = 1;   /* de eventdispatcher kijkt hiernaar; alleen deze handler stopt */
                fr.done = 1;
                res = VOIDD;
            } else res = call_builtin_or_handler(nm, a.u.l->v, a.u.l->n);
            int nr = a.t == T_ARGS ? args_noret(a) : 0;
            args_free(a);
            if (nr) d_unref(res); else push(res);
            break;
        }
        case 0x58: {  /* objcallv4: variabele (vartype arg) aanroepen */
            Datum lib = arg == 6 ? pop() : VOIDD;
            Datum id = pop();
            a = pop();
            Datum t;
            Datum *slot = var_slot(arg, id, lib, &t);
            Datum obj = slot ? *slot : VOIDD;
            Datum res = VOIDD;
            if (obj.t == T_XOBJ) res = xobj_call(obj.u.x, a.u.l->v, a.u.l->n);
            else if (obj.t == T_OBJ && a.u.l->n > 0 && a.u.l->v[0].t == T_SYM) {
                Script *hs;
                Handler *hh = obj_handler(obj.u.o, a.u.l->v[0].u.i, &hs);
                if (hh) {
                    Datum sv = a.u.l->v[0];
                    a.u.l->v[0] = obj;
                    res = vm_call(hs, hh, a.u.l->v, a.u.l->n);
                    a.u.l->v[0] = sv;
                }
            } else if (id.t == T_VARREF) {
                /* D4-syntax f(var, ...) waarbij de compiler niet wist of f een XObject was: het eerste
                 * argument is dan de NAAM van een variabele (local, arg of property); niet gedeclareerd
                 * = VOID, net als in het origineel */
                if (a.u.l->n > 0 && a.u.l->v[0].t == T_SYM) {
                    int vn = a.u.l->v[0].u.i;
                    Datum val = VOIDD;
                    for (int i = 0; i < h->nlocals; i++) if (h->locals[i] == vn) val = fr.locals[i];
                    for (int i = 0; i < h->nargs && i < fr.nargs; i++) if (h->args[i] == vn) val = fr.args[i];
                    if (val.t == T_VOID && fr.nargs > 0 && fr.args[0].t == T_OBJ) {
                        int f;
                        val = obj_getprop(fr.args[0].u.o, vn, &f);
                    }
                    d_unref(a.u.l->v[0]);
                    a.u.l->v[0] = d_ref(val);
                }
                res = call_builtin_or_handler(id.u.i, a.u.l->v, a.u.l->n);
            }
            int nr = args_noret(a);
            args_free(a);
            d_unref(id); d_unref(lib);
            if (nr) d_unref(res); else push(res);
            break;
        }
        case 0x59: {  /* put */
            int vt = arg & 15, how = arg >> 4;
            Datum lib = vt == 6 ? pop() : VOIDD;
            Datum id = pop();
            Datum v = pop();
            Datum t = VOIDD;
            Datum *slot = var_slot(vt, id, lib, &t);
            if (slot) {
                Datum nv = how == 1 ? v : how == 2 ? concat(*slot, v, 0) : concat(v, *slot, 0);
                if (how != 1) d_unref(v);
                if (vt == 6) { player_set_field(id, lib, nv); d_unref(t); d_unref(nv); }
                else { d_unref(*slot); *slot = nv; }
            } else d_unref(v);
            d_unref(id); d_unref(lib);
            break;
        }
        case 0x5a: {  /* putchunk */
            int vt = arg & 15, how = arg >> 4;
            Datum lib = vt == 6 ? pop() : VOIDD;
            Datum id = pop();
            int v[8];
            read8(v);
            Datum val = pop();
            Datum t = VOIDD;
            Datum *slot = var_slot(vt, id, lib, &t);
            if (slot) {
                Datum nv = put_chunk(*slot, v, val, how);
                if (vt == 6) { player_set_field(id, lib, nv); d_unref(t); d_unref(nv); }
                else { d_unref(*slot); *slot = nv; }
            }
            d_unref(val); d_unref(id); d_unref(lib);
            break;
        }
        case 0x5b: {  /* deletechunk */
            int vt = arg & 15;
            Datum lib = vt == 6 ? pop() : VOIDD;
            Datum id = pop();
            int v[8];
            read8(v);
            Datum t = VOIDD;
            Datum *slot = var_slot(vt, id, lib, &t);
            if (slot) {
                Datum empty = d_str("");
                Datum nv = put_chunk(*slot, v, empty, 1);
                d_unref(empty);
                if (vt == 6) { player_set_field(id, lib, nv); d_unref(t); d_unref(nv); }
                else { d_unref(*slot); *slot = nv; }
            }
            d_unref(id); d_unref(lib);
            break;
        }
        case 0x5c: {  /* get */
            Datum pid = pop();
            int id = d_toint(pid);
            Datum tg[2] = {VOIDD, VOIDD};
            int nt = 0;
            if (arg == 0 && id > 11) nt = 1;
            else if (arg == 1 || arg == 4 || arg == 6) nt = 1;
            else if (arg == 8 && id == 2) nt = 1;
            else if (arg == 9 || arg == 10) nt = 2;
            for (int i = nt - 1; i >= 0; i--) tg[i] = pop();
            push(player_get(arg, id, tg, nt));
            for (int i = 0; i < nt; i++) d_unref(tg[i]);
            break;
        }
        case 0x5d: {  /* set */
            Datum pid = pop();
            int id = d_toint(pid);
            Datum v = pop();
            Datum tg[2] = {VOIDD, VOIDD};
            int nt = 0;
            if (arg == 4 || arg == 6) nt = 1;
            else if (arg == 9 || arg == 10) nt = 2;
            for (int i = nt - 1; i >= 0; i--) tg[i] = pop();
            player_set(arg, id, tg, nt, v);
            for (int i = 0; i < nt; i++) d_unref(tg[i]);
            break;
        }
        case 0x5f: push(player_movie_prop(NAME(arg))); break;
        case 0x60: player_set_movie_prop(NAME(arg), pop()); break;
        case 0x61: { a = pop(); push(player_objprop(a, NAME(arg))); d_unref(a); break; }
        case 0x62: { Datum v = pop(); a = pop(); player_set_objprop(a, NAME(arg), v); d_unref(a); break; }
        case 0x64: push(arg < g_sp ? d_ref(g_stack[g_sp - 1 - arg]) : VOIDD); break;
        case 0x65: for (int i = 0; i < arg; i++) d_unref(pop()); break;
        case 0x66: { a = pop(); args_free(a); push(player_the(NAME(arg))); break; }
        case 0x67: {  /* objcall: methode op eerste argument */
            a = pop();
            Datum res = call_builtin_or_handler(NAME(arg), a.u.l->v, a.u.l->n);
            int nr = args_noret(a);
            args_free(a);
            if (nr) d_unref(res); else push(res);
            break;
        }
        case 0x71: { float fv; memcpy(&fv, &arg, 4); push(d_float(fv)); break; }
        default:
            vm_error("onbekende opcode %02x in %s @%d", op, symname(h->name), pos);
            fr.done = 1;
            break;
        }
#undef NAME
    }
    while (g_sp > base_sp) d_unref(pop());
    for (int i = 0; i < fr.nargs; i++) d_unref(fr.args[i]);
    for (int i = 0; i < h->nlocals; i++) d_unref(fr.locals[i]);
    free(fr.args);
    free(fr.locals);
    g_cur = prev;
    vm_cur_script = prev_script;
    g_depth--;
    if (vm_trace) { fprintf(stderr, "%*s< %s = ", g_depth * 2 + 2, "", symname(h->name)); d_print(stderr, fr.ret); fprintf(stderr, "\n"); }
    return fr.ret;
}

/* ------------------------------------------------------------------ aanroepen op naam */
extern Movie *vm_movie_for_handlers(void);

static Handler *movie_handler(int name, Script **out) {
    Movie *mv = vm_movie_for_handlers();
    if (!mv) return NULL;
    for (int l = 0; l < mv->nlibs; l++) {
        CastLib *c = mv->libs[l];
        for (int i = 0; i < c->nscripts; i++) {
            Script *sc = c->scripts[i];
            if (sc->type != 3) continue;
            Handler *h = script_handler(sc, name);
            if (h) { *out = sc; return h; }
        }
    }
    return NULL;
}

Datum vm_call_name(int name, Datum *a, int n, int *found) {
    Script *sc;
    Handler *h = movie_handler(name, &sc);
    if (found) *found = h != NULL;
    return h ? vm_call(sc, h, a, n) : VOIDD;
}

static int g_warned[4096];

static Datum call_builtin_or_handler(int name, Datum *a, int n) {
    /* 0. birth/new(script "X", ...) is altijd de constructor, ook al definiëren de (D4-stijl)
     *    klassen zelf een globale movie-handler 'birth' */
    static int S_birth = -1, S_new = -1;
    if (S_birth < 0) { S_birth = sym("birth"); S_new = sym("new"); }
    if (n > 0 && a[0].t == T_SCRIPT && (name == S_birth || name == S_new))
        return obj_new(a[0].u.sc, a + 1, n - 1);
    /* 1. handler van een object of script als eerste argument (D4-stijl: stepFrame(obj),
     *    Event(script "LocScriptC5", ...)) */
    if (n > 0 && a[0].t == T_OBJ) {
        Script *hs;
        Handler *h = obj_handler(a[0].u.o, name, &hs);
        if (h) return vm_call(hs, h, a, n);
    }
    if (n > 0 && a[0].t == T_SCRIPT && a[0].u.sc) {
        Handler *h = script_handler(a[0].u.sc, name);
        if (h) return vm_call(a[0].u.sc, h, a, n);
    }
    /* 2. movie-scripts */
    Script *sc;
    Handler *h = movie_handler(name, &sc);
    if (h) return vm_call(sc, h, a, n);
    /* 3. builtins */
    Builtin fn = vm_builtin(name);
    if (fn) return fn(a, n);
    /* 4. XObject-methode via een factory-global met die naam (bijv. FileIO(...)) */
    Datum *g = global_ref(name);
    if (g->t == T_XOBJ) return xobj_call(g->u.x, a, n);
    if (name < 4096 && !g_warned[name]) {
        g_warned[name] = 1;
        vm_error("handler niet gevonden: %s", symname(name));
    }
    return VOIDD;
}

Datum vm_call_any(int name, Datum *a, int n) { return call_builtin_or_handler(name, a, n); }

/* ------------------------------------------------------------------ mini-parser voor value()/scriptstrings */
static const char *skipws(const char *p) { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++; return p; }

static Datum parse_value(const char **pp) {
    const char *p = skipws(*pp);
    Datum r = VOIDD;
    if (*p == '"') {
        const char *e = strchr(p + 1, '"');
        if (!e) e = p + strlen(p);
        r = d_strn(p + 1, (int)(e - p - 1));
        p = *e ? e + 1 : e;
    } else if (*p == '#') {
        const char *e = p + 1;
        while (isalnum((unsigned char)*e) || *e == '_') e++;
        r = d_sym(symn(p + 1, (int)(e - p - 1)));
        p = e;
    } else if (*p == '[') {
        p = skipws(p + 1);
        if (*p == ':') { r = d_list(T_PLIST, 4); p = skipws(p + 1); }
        else r = d_list(T_LIST, 4);
        while (*p && *p != ']') {
            Datum k = parse_value(&p);
            p = skipws(p);
            if (*p == ':') {
                r.t = T_PLIST;
                p++;
                Datum v = parse_value(&p);
                list_push(r.u.l, k);
                list_push(r.u.l, v);
            } else list_push(r.u.l, k);
            p = skipws(p);
            if (*p == ',') p++;
            p = skipws(p);
        }
        if (*p == ']') p++;
    } else if (*p == '-' || isdigit((unsigned char)*p) || *p == '.') {
        char *e;
        double v = strtod(p, &e);
        int isf = 0;
        for (const char *q = p; q < e; q++) if (*q == '.' || *q == 'e' || *q == 'E') isf = 1;
        r = isf ? d_float(v) : d_int((int32_t)v);
        p = e;
    } else if (isalpha((unsigned char)*p)) {
        const char *e = p;
        while (isalnum((unsigned char)*e) || *e == '_') e++;
        int nm = symn(p, (int)(e - p));
        p = e;
        if (nm == sym("void")) r = VOIDD;
        else if (nm == sym("true")) r = d_int(1);
        else if (nm == sym("false")) r = d_int(0);
        else if (nm == sym("empty") || nm == sym("EMPTY")) r = d_str("");
        else r = d_ref(*global_ref(nm));
    }
    *pp = p;
    return r;
}

Datum lingo_value(const char *s) { const char *p = s; return parse_value(&p); }

/* scriptstring uitvoeren: "", "Handler", "dontPassEvent", "go to \"Label\"" */
void lingo_do(const char *s) {
    const char *p = skipws(s);
    if (!*p) return;
    const char *e = p;
    while (isalnum((unsigned char)*e) || *e == '_') e++;
    int nm = symn(p, (int)(e - p));
    p = skipws(e);
    if (nm == sym("go")) {
        if (!strncmp(p, "to ", 3)) p = skipws(p + 3);
        if (!strncmp(p, "frame ", 6)) p = skipws(p + 6);
        Datum arg = parse_value(&p);
        call_builtin_or_handler(nm, &arg, 1);
        d_unref(arg);
        return;
    }
    Datum args[8];
    int n = 0;
    while (*p && n < 8) {
        args[n++] = parse_value(&p);
        p = skipws(p);
        if (*p == ',') p = skipws(p + 1);
        else break;
    }
    Datum r = call_builtin_or_handler(nm, args, n);
    d_unref(r);
    for (int i = 0; i < n; i++) d_unref(args[i]);
}

void globals_dump(FILE *f) {
    for (int i = 0; i < g_nglob; i++) {
        fprintf(f, "%s = ", symname(g_glob[i].name));
        d_print(f, g_glob[i].v);
        fputc('\n', f);
    }
}
