/* Native vervangers voor de XObjects/Xtras van het spel:
 *   INI      mnew, mGetPrivateProfileString/Int, mWritePrivateProfileString, mdispose
 *   FileIO   mnew(mode, pad), mReadLine, mReadFile, mWriteString, mdispose
 *   MovUtils mnew, mGetWindowsPath, mTrimWhiteChars, mdispose
 *   DLLGlue  mnew(dll, functie, ret, args) -> object; obj(#mCall, ...), obj(#mWindowHandle)
 *            functies uit MMSYS.DLL: CDPlayTrack, CDPlaying, CDStop, LoadSaveGame
 *   PrintOMatic_Lite: alles no-op.
 * Paden: alles buiten de spelmap wordt naar de opslagmap (P.save_dir) omgeleid, zie path_resolve. */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>

#define ARG(i) ((i) < n ? a[i] : VOIDD)

typedef struct FileSt { FILE *fp; int write; } FileSt;
typedef struct GlueSt { char fn[64]; } GlueSt;

static Datum mk(int kind, int factory, void *st) {
    XObj *x = calloc(1, sizeof *x);
    x->rc = 1;
    x->kind = kind;
    x->is_factory = factory;
    x->st = st;
    Datum d = {T_XOBJ};
    d.u.x = x;
    return d;
}

Datum xobj_factory(const char *name) {
    if (!_stricmp(name, "INI")) return mk(XK_INI, 1, NULL);
    if (!_stricmp(name, "FILEIO")) return mk(XK_FILEIO, 1, NULL);
    if (!_stricmp(name, "MOVUTILS")) return mk(XK_MOVUTILS, 1, NULL);
    if (!_stricmp(name, "DLLGLUE")) return mk(XK_DLLGLUE, 1, NULL);
    if (!_stricmp(name, "PrintOMatic_Lite") || !_stricmp(name, "POMLITE")) return mk(XK_PRINT, 1, NULL);
    vm_error("onbekende XObject/Xtra: %s", name);
    return VOIDD;
}

static const char *sarg(Datum d, char *buf, int n) { return d_tostr(d, buf, n); }

int ld_save_game(int is_load);   /* main.c: slotkeuze */

Datum xobj_call(XObj *x, Datum *a, int n) {
    if (n < 1 || ARG(0).t != T_SYM) return VOIDD;
    const char *m = symname(ARG(0).u.i);
    char b1[512], b2[512], b3[512], b4[512], path[512];
    if (!_stricmp(m, "mdispose")) {
        if (x->kind == XK_FILEIO && x->st) { FileSt *f = x->st; if (f->fp) fclose(f->fp); free(f); x->st = NULL; }
        return VOIDD;
    }
    switch (x->kind) {
    case XK_INI:
        if (!_stricmp(m, "mnew")) return mk(XK_INI, 0, NULL);
        if (!_stricmp(m, "mGetPrivateProfileString")) {
            path_resolve(sarg(ARG(5), b4, sizeof b4), path, sizeof path);
            char out[512];
            GetPrivateProfileStringA(sarg(ARG(1), b1, sizeof b1), sarg(ARG(2), b2, sizeof b2),
                                     sarg(ARG(3), b3, sizeof b3), out, sizeof out, path);
            /* het origineel gaf een buffer met NUL terug; de scripts zoeken die op. Wij geven de
             * string zonder NUL; offset(NUL) = 0 -> scripts knippen dan 'char 1 to -1'. Voeg daarom
             * een expliciet eindteken toe dat offset wel vindt. */
            int len = (int)strlen(out);
            out[len] = 0;
            Datum r = d_strn(out, len + 1);
            return r;
        }
        if (!_stricmp(m, "mGetPrivateProfileInt")) {
            path_resolve(sarg(ARG(4), b4, sizeof b4), path, sizeof path);
            return d_int((int)GetPrivateProfileIntA(sarg(ARG(1), b1, sizeof b1), sarg(ARG(2), b2, sizeof b2),
                                                    d_toint(ARG(3)), path));
        }
        if (!_stricmp(m, "mWritePrivateProfileString")) {
            path_resolve(sarg(ARG(4), b4, sizeof b4), path, sizeof path);
            return d_int(WritePrivateProfileStringA(sarg(ARG(1), b1, sizeof b1), sarg(ARG(2), b2, sizeof b2),
                                                    sarg(ARG(3), b3, sizeof b3), path) ? 1 : 0);
        }
        break;
    case XK_FILEIO:
        if (!_stricmp(m, "mnew")) {
            const char *mode = sarg(ARG(1), b1, sizeof b1);
            path_resolve(sarg(ARG(2), b2, sizeof b2), path, sizeof path);
            int w = !_stricmp(mode, "write"), ap = !_stricmp(mode, "append");
            FILE *fp = fopen(path, w ? "wb" : ap ? "ab" : "rb");
            if (!fp) return d_int(-43);   /* fnfErr, zoals het origineel */
            FileSt *f = calloc(1, sizeof *f);
            f->fp = fp;
            f->write = w || ap;
            return mk(XK_FILEIO, 0, f);
        }
        if (x->st) {
            FileSt *f = x->st;
            if (!_stricmp(m, "mReadLine")) {
                int len = 0, cap = 256, c;
                char *buf = malloc(cap);
                while ((c = fgetc(f->fp)) != EOF) {
                    if (len + 2 >= cap) buf = realloc(buf, cap *= 2);
                    buf[len++] = (char)c;
                    if (c == '\r') break;
                }
                Datum r = d_strn(buf, len);
                free(buf);
                return r;
            }
            if (!_stricmp(m, "mReadFile")) {
                long pos = ftell(f->fp);
                fseek(f->fp, 0, SEEK_END);
                long end = ftell(f->fp);
                fseek(f->fp, pos, SEEK_SET);
                char *buf = malloc(end - pos + 1);
                size_t got = fread(buf, 1, end - pos, f->fp);
                Datum r = d_strn(buf, (int)got);
                free(buf);
                return r;
            }
            if (!_stricmp(m, "mWriteString")) {
                Str *s = d_asstr(ARG(1));
                fwrite(s->s, 1, s->len, f->fp);
                if (--s->rc == 0) free(s);
                return VOIDD;
            }
            if (!_stricmp(m, "mWriteChar")) { fputc(d_toint(ARG(1)), f->fp); return VOIDD; }
            if (!_stricmp(m, "mReadChar")) { int c = fgetc(f->fp); return d_int(c == EOF ? 0 : c); }
        }
        break;
    case XK_MOVUTILS:
        if (!_stricmp(m, "mnew")) return mk(XK_MOVUTILS, 0, NULL);
        if (!_stricmp(m, "mGetWindowsPath")) return d_str(P.save_dir);
        if (!_stricmp(m, "mTrimWhiteChars")) {
            Str *s = d_asstr(ARG(1));
            int b = 0, e = s->len;
            while (b < e && isspace((unsigned char)s->s[b])) b++;
            while (e > b && isspace((unsigned char)s->s[e - 1])) e--;
            Datum r = d_strn(s->s + b, e - b);
            if (--s->rc == 0) free(s);
            return r;
        }
        break;
    case XK_DLLGLUE:
        if (!_stricmp(m, "mnew")) {
            GlueSt *g = calloc(1, sizeof *g);
            snprintf(g->fn, sizeof g->fn, "%s", sarg(ARG(2), b1, sizeof b1));
            return mk(XK_DLLGLUE, 0, g);
        }
        if (!_stricmp(m, "mWindowHandle")) return d_int(1);
        if (!_stricmp(m, "mCall") && x->st) {
            GlueSt *g = x->st;
            if (!_stricmp(g->fn, "CDPlayTrack")) { cd_play_track(d_toint(ARG(1))); return d_int(1); }
            if (!_stricmp(g->fn, "CDPlaying")) return d_int(cd_playing());
            if (!_stricmp(g->fn, "CDStop")) { cd_stop(); return d_int(1); }
            if (!_stricmp(g->fn, "LoadSaveGame")) return d_int(ld_save_game(d_toint(ARG(2))));
            if (!_stricmp(g->fn, "VkKeyScan")) return d_int(VkKeyScanA((char)d_toint(ARG(1))));
            if (!_stricmp(g->fn, "InvalidateRect") || !_stricmp(g->fn, "UpdateWindow")) { P.update_needed = 1; return d_int(0); }
            vm_error("MMSYS.%s niet geïmplementeerd", g->fn);
            return d_int(0);
        }
        break;
    case XK_PRINT:
        if (!_stricmp(m, "mnew") || !_stricmp(m, "new")) return mk(XK_PRINT, 0, NULL);
        return VOIDD;
    }
    vm_error("XObject-methode %s niet ondersteund (soort %d)", m, x->kind);
    return VOIDD;
}

void xobj_register(void) {}
