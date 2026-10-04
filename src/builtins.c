/* Lingo-builtins zonder speler-toestand: lijsten, proplijsten, strings, rekenen, types. */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

Datum lingo_value(const char *s);
int chunk_count(const char *s, int kind);

#define ARG(i) ((i) < n ? a[i] : VOIDD)
static int is_list(Datum d) { return d.t == T_LIST || d.t == T_POINT || d.t == T_RECT; }

static Datum bi_count(Datum *a, int n) {
    Datum l = ARG(0);
    if (is_list(l)) return d_int(l.u.l->n);
    if (l.t == T_PLIST) return d_int(l.u.l->n / 2);
    return d_int(0);
}

static int plist_find(List *l, Datum key) {
    for (int i = 0; i + 1 < l->n; i += 2)
        if (d_equal(l->v[i], key) && (l->v[i].t == key.t || key.t != T_SYM || l->v[i].t != T_STR)) return i;
    return -1;
}

static Datum bi_getAt(Datum *a, int n) {
    Datum l = ARG(0);
    int i = d_toint(ARG(1)) - 1;
    if (is_list(l)) return i >= 0 && i < l.u.l->n ? d_ref(l.u.l->v[i]) : VOIDD;
    if (l.t == T_PLIST) return i >= 0 && 2 * i + 1 < l.u.l->n ? d_ref(l.u.l->v[2 * i + 1]) : VOIDD;
    return VOIDD;
}

static Datum bi_setAt(Datum *a, int n) {
    Datum l = ARG(0);
    int i = d_toint(ARG(1)) - 1;
    if (i < 0) return VOIDD;
    if (is_list(l)) {
        while (l.u.l->n <= i) list_push(l.u.l, d_int(0));
        d_unref(l.u.l->v[i]);
        l.u.l->v[i] = d_ref(ARG(2));
    } else if (l.t == T_PLIST && 2 * i + 1 < l.u.l->n) {
        d_unref(l.u.l->v[2 * i + 1]);
        l.u.l->v[2 * i + 1] = d_ref(ARG(2));
    }
    return VOIDD;
}

static Datum bi_getLast(Datum *a, int n) {
    Datum l = ARG(0);
    if (is_list(l) && l.u.l->n) return d_ref(l.u.l->v[l.u.l->n - 1]);
    if (l.t == T_PLIST && l.u.l->n) return d_ref(l.u.l->v[l.u.l->n - 1]);
    return VOIDD;
}

static Datum bi_add(Datum *a, int n) {
    Datum l = ARG(0);
    if (is_list(l)) list_push(l.u.l, d_ref(ARG(1)));
    return VOIDD;
}

static Datum bi_addAt(Datum *a, int n) {
    Datum l = ARG(0);
    if (!is_list(l)) return VOIDD;
    int i = d_toint(ARG(1)) - 1;
    if (i < 0) i = 0;
    while (l.u.l->n < i) list_push(l.u.l, d_int(0));
    list_push(l.u.l, VOIDD);
    memmove(l.u.l->v + i + 1, l.u.l->v + i, sizeof(Datum) * (l.u.l->n - 1 - i));
    l.u.l->v[i] = d_ref(ARG(2));
    return VOIDD;
}

static Datum bi_deleteAt(Datum *a, int n) {
    Datum l = ARG(0);
    int i = d_toint(ARG(1)) - 1;
    if (is_list(l) && i >= 0 && i < l.u.l->n) {
        d_unref(l.u.l->v[i]);
        memmove(l.u.l->v + i, l.u.l->v + i + 1, sizeof(Datum) * (l.u.l->n - i - 1));
        l.u.l->n--;
    } else if (l.t == T_PLIST && i >= 0 && 2 * i + 1 < l.u.l->n) {
        d_unref(l.u.l->v[2 * i]); d_unref(l.u.l->v[2 * i + 1]);
        memmove(l.u.l->v + 2 * i, l.u.l->v + 2 * i + 2, sizeof(Datum) * (l.u.l->n - 2 * i - 2));
        l.u.l->n -= 2;
    }
    return VOIDD;
}

static Datum bi_getPos(Datum *a, int n) {
    Datum l = ARG(0);
    if (is_list(l)) {
        for (int i = 0; i < l.u.l->n; i++)
            if (d_equal(l.u.l->v[i], ARG(1))) return d_int(i + 1);
    } else if (l.t == T_PLIST) {
        for (int i = 1; i < l.u.l->n; i += 2)
            if (d_equal(l.u.l->v[i], ARG(1))) return d_int(i / 2 + 1);
    }
    return d_int(0);
}

static Datum bi_getOne(Datum *a, int n) {
    Datum l = ARG(0);
    if (is_list(l)) return bi_getPos(a, n);
    if (l.t == T_PLIST)
        for (int i = 1; i < l.u.l->n; i += 2)
            if (d_equal(l.u.l->v[i], ARG(1))) return d_ref(l.u.l->v[i - 1]);
    return d_int(0);
}

static Datum bi_deleteOne(Datum *a, int n) {
    Datum l = ARG(0);
    if (is_list(l)) {
        for (int i = 0; i < l.u.l->n; i++)
            if (d_equal(l.u.l->v[i], ARG(1))) {
                Datum x[2] = {l, d_int(i + 1)};
                return bi_deleteAt(x, 2);
            }
    } else if (l.t == T_PLIST) {
        for (int i = 1; i < l.u.l->n; i += 2)
            if (d_equal(l.u.l->v[i], ARG(1))) {
                Datum x[2] = {l, d_int(i / 2 + 1)};
                return bi_deleteAt(x, 2);
            }
    }
    return VOIDD;
}

static Datum bi_append(Datum *a, int n) {
    if (xobj_print_cmd("append", a, n)) return VOIDD;   /* PrintOMatic: append(doc, member) */
    return bi_add(a, n);
}

static Datum bi_getaProp(Datum *a, int n) {
    Datum l = ARG(0);
    if (l.t == T_PLIST) {
        int i = plist_find(l.u.l, ARG(1));
        return i >= 0 ? d_ref(l.u.l->v[i + 1]) : VOIDD;
    }
    if (is_list(l)) return bi_getAt(a, n);
    if (l.t == T_OBJ && ARG(1).t == T_SYM) { int f; return d_ref(obj_getprop(l.u.o, ARG(1).u.i, &f)); }
    return VOIDD;
}

static Datum bi_getProp(Datum *a, int n) { return bi_getaProp(a, n); }

static Datum bi_setaProp(Datum *a, int n) {
    Datum l = ARG(0);
    if (l.t == T_PLIST) {
        int i = plist_find(l.u.l, ARG(1));
        if (i >= 0) { d_unref(l.u.l->v[i + 1]); l.u.l->v[i + 1] = d_ref(ARG(2)); }
        else { list_push(l.u.l, d_ref(ARG(1))); list_push(l.u.l, d_ref(ARG(2))); }
    } else if (is_list(l)) return bi_setAt(a, n);
    else if (l.t == T_OBJ && ARG(1).t == T_SYM) obj_setprop(l.u.o, ARG(1).u.i, d_ref(ARG(2)));
    return VOIDD;
}

static Datum bi_addProp(Datum *a, int n) {
    Datum l = ARG(0);
    if (l.t == T_PLIST) { list_push(l.u.l, d_ref(ARG(1))); list_push(l.u.l, d_ref(ARG(2))); }
    return VOIDD;
}

static Datum bi_deleteProp(Datum *a, int n) {
    Datum l = ARG(0);
    if (l.t == T_PLIST) {
        int i = plist_find(l.u.l, ARG(1));
        if (i >= 0) { Datum x[2] = {l, d_int(i / 2 + 1)}; bi_deleteAt(x, 2); }
    } else if (is_list(l)) return bi_deleteAt(a, n);
    return VOIDD;
}

static Datum bi_getPropAt(Datum *a, int n) {
    Datum l = ARG(0);
    int i = d_toint(ARG(1)) - 1;
    if (l.t == T_PLIST && i >= 0 && 2 * i < l.u.l->n) return d_ref(l.u.l->v[2 * i]);
    return VOIDD;
}

static Datum dup(Datum d) {
    if (d.t == T_LIST || d.t == T_PLIST || d.t == T_POINT || d.t == T_RECT) {
        Datum r = d_list(d.t, d.u.l->n);
        for (int i = 0; i < d.u.l->n; i++) list_push(r.u.l, dup(d.u.l->v[i]));
        return r;
    }
    return d_ref(d);
}
static Datum bi_duplicate(Datum *a, int n) { return dup(ARG(0)); }

static Datum bi_point(Datum *a, int n) {
    Datum r = d_list(T_POINT, 2);
    list_push(r.u.l, d_ref(ARG(0)));
    list_push(r.u.l, d_ref(ARG(1)));
    return r;
}

static Datum bi_rect(Datum *a, int n) {
    if (n == 2 && ARG(0).t == T_POINT && ARG(1).t == T_POINT) {
        List *p = ARG(0).u.l, *q = ARG(1).u.l;
        return d_rect(d_toint(p->v[0]), d_toint(p->v[1]), d_toint(q->v[0]), d_toint(q->v[1]));
    }
    Datum r = d_list(T_RECT, 4);
    for (int i = 0; i < 4; i++) list_push(r.u.l, d_ref(ARG(i)));
    return r;
}

static int ri(Datum r, int i) { return is_list(r) && i < r.u.l->n ? d_toint(r.u.l->v[i]) : 0; }

static Datum bi_intersect(Datum *a, int n) {
    Datum x = ARG(0), y = ARG(1);
    int l = ri(x, 0) > ri(y, 0) ? ri(x, 0) : ri(y, 0);
    int t = ri(x, 1) > ri(y, 1) ? ri(x, 1) : ri(y, 1);
    int r = ri(x, 2) < ri(y, 2) ? ri(x, 2) : ri(y, 2);
    int b = ri(x, 3) < ri(y, 3) ? ri(x, 3) : ri(y, 3);
    if (r <= l || b <= t) return d_rect(0, 0, 0, 0);
    return d_rect(l, t, r, b);
}

static Datum bi_union(Datum *a, int n) {
    Datum x = ARG(0), y = ARG(1);
    return d_rect(ri(x, 0) < ri(y, 0) ? ri(x, 0) : ri(y, 0), ri(x, 1) < ri(y, 1) ? ri(x, 1) : ri(y, 1),
                  ri(x, 2) > ri(y, 2) ? ri(x, 2) : ri(y, 2), ri(x, 3) > ri(y, 3) ? ri(x, 3) : ri(y, 3));
}

static Datum bi_inside(Datum *a, int n) {
    Datum p = ARG(0), r = ARG(1);
    int x = ri(p, 0), y = ri(p, 1);
    return d_int(x >= ri(r, 0) && x < ri(r, 2) && y >= ri(r, 1) && y < ri(r, 3));
}

static Datum bi_offsetRect(Datum *a, int n) {
    Datum r = ARG(0);
    int dx = d_toint(ARG(1)), dy = d_toint(ARG(2));
    return d_rect(ri(r, 0) + dx, ri(r, 1) + dy, ri(r, 2) + dx, ri(r, 3) + dy);
}

static Datum bi_string(Datum *a, int n) { Str *s = d_asstr(ARG(0)); Datum d = {T_STR}; d.u.s = s; return d; }
static Datum bi_length(Datum *a, int n) { Str *s = d_asstr(ARG(0)); int l = s->len; if (--s->rc == 0) free(s); return d_int(l); }
static Datum bi_voidp(Datum *a, int n) { return d_int(ARG(0).t == T_VOID); }
static Datum bi_objectp(Datum *a, int n) {
    int t = ARG(0).t;
    return d_int(t == T_OBJ || t == T_XOBJ || t == T_LIST || t == T_PLIST || t == T_POINT || t == T_RECT
                 || t == T_SCRIPT || t == T_WINDOW || t == T_MEMBER);
}
static Datum bi_symbolp(Datum *a, int n) { return d_int(ARG(0).t == T_SYM); }
static Datum bi_stringp(Datum *a, int n) { return d_int(ARG(0).t == T_STR); }
static Datum bi_integerp(Datum *a, int n) { return d_int(ARG(0).t == T_INT); }
static Datum bi_floatp(Datum *a, int n) { return d_int(ARG(0).t == T_FLOAT); }
static Datum bi_listp(Datum *a, int n) { return d_int(is_list(ARG(0)) || ARG(0).t == T_PLIST); }
static Datum bi_abs(Datum *a, int n) {
    Datum x = ARG(0);
    return x.t == T_FLOAT ? d_float(fabs(x.u.f)) : d_int(abs(d_toint(x)));
}
static Datum bi_integer(Datum *a, int n) {
    Datum x = ARG(0);
    if (x.t == T_STR) {
        Datum v = lingo_value(x.u.s->s);
        if (v.t == T_INT) return v;
        if (v.t == T_FLOAT) return d_int((int)lround(v.u.f));
        d_unref(v);
        return VOIDD;
    }
    return d_int(d_toint(x));
}
static Datum bi_float(Datum *a, int n) { return d_float(d_tofloat(ARG(0))); }
/* random(n): eigen generator (xorshift32), zodat elk platform dezelfde reeks geeft. Headless (tests) altijd vanaf
 * hetzelfde zaad, anders vanaf de klok (zoals Director: elke keer anders). */
static uint32_t g_rng;
static Datum bi_random(Datum *a, int n) {
    int m = d_toint(ARG(0));
    if (!g_rng) g_rng = g_headless ? 0x2545f491u : (plat_ms() * 2654435761u) | 1;
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return d_int(m > 0 ? (int)(g_rng % (uint32_t)m) + 1 : 1);
}
static Datum bi_charToNum(Datum *a, int n) {
    Str *s = d_asstr(ARG(0));
    int c = s->len ? (unsigned char)s->s[0] : 0;
    if (--s->rc == 0) free(s);
    return d_int(c);
}
static Datum bi_numToChar(Datum *a, int n) { char c = (char)d_toint(ARG(0)); return d_strn(&c, 1); }
static Datum bi_offset(Datum *a, int n) {
    if (ARG(0).t == T_RECT || ARG(0).t == T_POINT) return bi_offsetRect(a, n);   /* D4: offset(rect, dx, dy) */
    Str *x = d_asstr(ARG(0)), *s = d_asstr(ARG(1));
    int r = 0;
    for (int i = 0; i + x->len <= s->len && !r; i++) {
        int ok = 1;
        for (int k = 0; k < x->len && ok; k++)
            if (tolower((unsigned char)s->s[i + k]) != tolower((unsigned char)x->s[k])) ok = 0;
        if (ok) r = i + 1;
    }
    if (x->len == 0 && s->len == 0) r = 0;
    /* offset(numToChar(0), s): een NUL zit nooit in onze strings; dan 0 -> de aanroeper knipt alles */
    if (--x->rc == 0) free(x);
    if (--s->rc == 0) free(s);
    return d_int(r);
}
static Datum bi_value(Datum *a, int n) {
    if (ARG(0).t != T_STR) return d_ref(ARG(0));
    return lingo_value(ARG(0).u.s->s);
}
static Datum bi_ilk(Datum *a, int n) {
    static const char *names[] = {"void", "integer", "float", "string", "symbol", "list", "propList", "point",
                                  "rect", "instance", "script", "member", "instance", "void", "castLib", "window", "picture"};
    Datum x = ARG(0);
    if (n >= 2) {
        /* ilk(x, #type) */
        Datum r = bi_ilk(a, 1);
        int eq = r.u.i == ARG(1).u.i;
        if (!eq && ARG(1).u.i == sym("list")) eq = is_list(x) || x.t == T_PLIST;
        return d_int(eq);
    }
    return d_sym(sym(x.t < sizeof names / sizeof *names ? names[x.t] : "void"));
}
static Datum bi_nothing(Datum *a, int n) { (void)a; (void)n; return VOIDD; }
static Datum bi_max(Datum *a, int n) {
    if (n == 1 && is_list(ARG(0))) return bi_max(ARG(0).u.l->v, ARG(0).u.l->n);
    Datum m = VOIDD;
    for (int i = 0; i < n; i++) if (i == 0 || d_compare(a[i], m) > 0) m = a[i];
    return d_ref(m);
}
static Datum bi_min(Datum *a, int n) {
    if (n == 1 && is_list(ARG(0))) return bi_min(ARG(0).u.l->v, ARG(0).u.l->n);
    Datum m = VOIDD;
    for (int i = 0; i < n; i++) if (i == 0 || d_compare(a[i], m) < 0) m = a[i];
    return d_ref(m);
}
static Datum bi_list(Datum *a, int n) {
    Datum r = d_list(T_LIST, n);
    for (int i = 0; i < n; i++) list_push(r.u.l, d_ref(a[i]));
    return r;
}
static Datum bi_sort(Datum *a, int n) {
    Datum l = ARG(0);
    if (!is_list(l) && l.t != T_PLIST) return VOIDD;
    int step = l.t == T_PLIST ? 2 : 1;
    for (int i = step; i < l.u.l->n; i += step)
        for (int j = i; j >= step && d_compare(l.u.l->v[j - step], l.u.l->v[j]) > 0; j -= step)
            for (int k = 0; k < step; k++) {
                Datum t = l.u.l->v[j - step + k];
                l.u.l->v[j - step + k] = l.u.l->v[j + k];
                l.u.l->v[j + k] = t;
            }
    return VOIDD;
}
static Datum bi_symbol(Datum *a, int n) {
    if (ARG(0).t == T_SYM) return ARG(0);
    Str *s = d_asstr(ARG(0));
    int k = sym(s->s);
    if (--s->rc == 0) free(s);
    return d_sym(k);
}
static Datum bi_sqrt(Datum *a, int n) { return d_float(sqrt(d_tofloat(ARG(0)))); }
static Datum bi_chars(Datum *a, int n) {
    Str *s = d_asstr(ARG(0));
    int b = d_toint(ARG(1)) - 1, e = d_toint(ARG(2));
    if (b < 0) b = 0;
    if (e > s->len) e = s->len;
    Datum r = d_strn(s->s + b, e > b ? e - b : 0);
    if (--s->rc == 0) free(s);
    return r;
}

void builtins_pure_register(void) {
    vm_register("count", bi_count);
    vm_register("getAt", bi_getAt);
    vm_register("setAt", bi_setAt);
    vm_register("getLast", bi_getLast);
    vm_register("add", bi_add);
    vm_register("addAt", bi_addAt);
    vm_register("append", bi_append);
    vm_register("deleteAt", bi_deleteAt);
    vm_register("deleteOne", bi_deleteOne);
    vm_register("getPos", bi_getPos);
    vm_register("getOne", bi_getOne);
    vm_register("getaProp", bi_getaProp);
    vm_register("getProp", bi_getProp);
    vm_register("setaProp", bi_setaProp);
    vm_register("setProp", bi_setaProp);
    vm_register("addProp", bi_addProp);
    vm_register("deleteProp", bi_deleteProp);
    vm_register("getPropAt", bi_getPropAt);
    vm_register("duplicate", bi_duplicate);
    vm_register("point", bi_point);
    vm_register("rect", bi_rect);
    vm_register("intersect", bi_intersect);
    vm_register("union", bi_union);
    vm_register("inside", bi_inside);
    vm_register("offsetRect", bi_offsetRect);
    vm_register("string", bi_string);
    vm_register("length", bi_length);
    vm_register("voidp", bi_voidp);
    vm_register("objectp", bi_objectp);
    vm_register("symbolp", bi_symbolp);
    vm_register("stringp", bi_stringp);
    vm_register("integerp", bi_integerp);
    vm_register("floatp", bi_floatp);
    vm_register("listp", bi_listp);
    vm_register("abs", bi_abs);
    vm_register("integer", bi_integer);
    vm_register("float", bi_float);
    vm_register("random", bi_random);
    vm_register("charToNum", bi_charToNum);
    vm_register("numToChar", bi_numToChar);
    vm_register("offset", bi_offset);
    vm_register("value", bi_value);
    vm_register("ilk", bi_ilk);
    vm_register("nothing", bi_nothing);
    vm_register("max", bi_max);
    vm_register("min", bi_min);
    vm_register("list", bi_list);
    vm_register("sort", bi_sort);
    vm_register("symbol", bi_symbol);
    vm_register("sqrt", bi_sqrt);
    vm_register("chars", bi_chars);
}
