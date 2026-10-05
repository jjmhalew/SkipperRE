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
#ifdef _WIN32
#include <windows.h>
#endif

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

/* VkKeyScan: teken -> virtuele toets (laag byte) + Shift (0x100), US-indeling. De ABC-film (MMB09) slaat letters over
 * die niet op het toetsenbord staan; buiten Windows tellen de letters van Windows-1252 (æ, ø, å, ä, ö, ...) als
 * typbaar, want SDL levert ze als tekst van elke indeling of van het schermtoetsenbord. */
static int vk_key_scan(int c) {
#ifdef _WIN32
    return VkKeyScanA((char)c);
#else
    c &= 255;
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 'A' && c <= 'Z') return c | 0x100;
    if (c >= 0xe0 && c != 0xf7) return c;                  /* kleine letters met accent */
    if (c >= 0xc0 && c <= 0xde && c != 0xd7) return c | 0x100;
    if (c >= '0' && c <= '9') return c;
    static const char *sh = ")!@#$%^&*(";
    const char *p = c ? strchr(sh, c) : NULL;
    if (p) return ('0' + (int)(p - sh)) | 0x100;
    switch (c) {
    case ' ': return 0x20; case 13: return 0x0d; case 8: return 0x08; case 9: return 0x09; case 27: return 0x1b;
    case '-': return 0xbd; case '_': return 0x1bd; case '=': return 0xbb; case '+': return 0x1bb;
    case ',': return 0xbc; case '<': return 0x1bc; case '.': return 0xbe; case '>': return 0x1be;
    case '/': return 0xbf; case '?': return 0x1bf; case ';': return 0xba; case ':': return 0x1ba;
    case '\'': return 0xde; case '"': return 0x1de;
    }
    return -1;
#endif
}

static const char *sarg(Datum d, char *buf, int n) { return d_tostr(d, buf, n); }



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
            path_resolve(sarg(ARG(5), b4, sizeof b4), b3, sizeof b3);
            vfs_real(b3, path, sizeof path);
            char out[512];
            ini_get(path, sarg(ARG(1), b1, sizeof b1), sarg(ARG(2), b2, sizeof b2), sarg(ARG(3), b3, sizeof b3), out, sizeof out);
            /* het origineel gaf een buffer met NUL terug; de scripts zoeken die op. Wij geven de
             * string zonder NUL; offset(NUL) = 0 -> scripts knippen dan 'char 1 to -1'. Voeg daarom
             * een expliciet eindteken toe dat offset wel vindt. */
            int len = (int)strlen(out), size = d_toint(ARG(4));   /* Size: de buffer van het origineel, met NUL */
            if (size > 0 && len > size - 1) len = size - 1;
            out[len] = 0;
            Datum r = d_strn(out, len + 1);
            return r;
        }
        if (!_stricmp(m, "mGetPrivateProfileInt")) {
            path_resolve(sarg(ARG(4), b4, sizeof b4), b3, sizeof b3);
            vfs_real(b3, path, sizeof path);
            return d_int(ini_get_int(path, sarg(ARG(1), b1, sizeof b1), sarg(ARG(2), b2, sizeof b2), d_toint(ARG(3))));
        }
        if (!_stricmp(m, "mWritePrivateProfileString")) {
            path_resolve(sarg(ARG(4), b4, sizeof b4), path, sizeof path);
            return d_int(ini_set(path, sarg(ARG(1), b1, sizeof b1), sarg(ARG(2), b2, sizeof b2), sarg(ARG(3), b3, sizeof b3)));
        }
        break;
    case XK_FILEIO:
        if (!_stricmp(m, "mnew")) {
            const char *mode = sarg(ARG(1), b1, sizeof b1);
            path_resolve(sarg(ARG(2), b2, sizeof b2), path, sizeof path);
            int w = !_stricmp(mode, "write"), ap = !_stricmp(mode, "append");
            if (!w && !ap) vfs_real(strcpy(b3, path), path, sizeof path);   /* lezen mag uit het pakket */
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
            if (!_stricmp(g->fn, "CDPlayTrack")) {   /* zonder cd-image (alleen de datatrack): mislukt, het script stopt dan */
                if (!cd_tracks()) return d_int(0);
                cd_play_track(d_toint(ARG(1)));
                return d_int(1);
            }
            if (!_stricmp(g->fn, "CheckCD")) return d_int(1);   /* D4: staat de cd in het station? De spelbestanden zijn er */
            if (!_stricmp(g->fn, "PrintMetaFile")) {
                /* D4 (MMPRINT.DLL): de kleurplaat PIC\PICn als WMF. Wij drukken dezelfde plaat af als bitmap uit de cast
                 * ("Pic" & n, wat ook op het scherm staat), net als bij de latere cd */
                const char *p = sarg(ARG(1), b1, sizeof b1), *q = p + strlen(p);
                while (q > p && isdigit((unsigned char)q[-1])) q--;
                char nm[32];
                snprintf(nm, sizeof nm, "Pic%s", q);
                int id = P.mv ? movie_find_member(P.mv, nm, 0) : 0;
                CastLib *cl;
                Member *mm = id ? movie_member(P.mv, id >> 16, id & 0xffff, &cl) : NULL;
                Bitmap *bm = mm && mm->type == MT_BITMAP ? member_bitmap(cl, mm) : NULL;
                if (!bm) { vm_error("PrintMetaFile: geen %s", nm); return d_int(0); }
                uint32_t *px = stage_bitmap_argb(bm);
                host_print(px, bm->w, bm->h, bm->w > bm->h, "Magnus og Myggen");
                free(px);
                return d_int(1);
            }
            if (!_stricmp(g->fn, "CDPlaying")) return d_int(cd_playing());
            if (!_stricmp(g->fn, "CDStop")) { cd_stop(); return d_int(1); }
            if (!_stricmp(g->fn, "LoadSaveGame")) {
                if (g_headless) {   /* testen: SKIPPER_SLOT=n kiest positie n (opslaan zet de naam "Test") */
                    const char *e = getenv("SKIPPER_SLOT");
                    int slot = e ? atoi(e) : 0;
                    if (slot > 0 && !d_toint(ARG(2))) {
                        char ini[PLAT_PATH], key[16];
                        snprintf(ini, sizeof ini, "%s\\MAGNUS.INI", P.save_dir);
                        snprintf(key, sizeof key, "GAME%d", slot);
                        ini_set(ini, "Saved games", key, "Test");
                    }
                    return d_int(slot);
                }
                return d_int(ld_save_game(d_toint(ARG(2))));
            }
            if (!_stricmp(g->fn, "VkKeyScan")) return d_int(vk_key_scan(d_toint(ARG(1))) & 0xffff);   /* "W": geen toets = 65535 (MMB09 test daarop) */
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

/* PrintOMatic_Lite (tekenspel MMB11): één member afdrukken. append/print/setLandscapeMode/
 * setDocumentName worden als gewone functies met het document als eerste argument aangeroepen. */
typedef struct PrintJob { int member; } PrintJob;   /* lib << 16 | nummer */

int xobj_print_cmd(const char *cmd, Datum *a, int n) {
    if (n < 1 || a[0].t != T_XOBJ || a[0].u.x->kind != XK_PRINT) return 0;
    XObj *x = a[0].u.x;
    if (!_stricmp(cmd, "setDocumentName")) {
        char buf[200];
        free(x->sval);
        x->sval = _strdup(n > 1 ? d_tostr(a[1], buf, sizeof buf) : "");
    } else if (!_stricmp(cmd, "setLandscapeMode")) x->ival = n > 1 && d_truthy(a[1]);
    else if (!_stricmp(cmd, "append")) {
        if (!x->st) x->st = calloc(1, sizeof(PrintJob));
        if (n > 1 && a[1].t == T_MEMBER) ((PrintJob *)x->st)->member = a[1].u.i;
    } else if (!_stricmp(cmd, "print")) {
        PrintJob *j = x->st;
        CastLib *cl;
        Member *m = j && P.mv ? movie_member(P.mv, j->member >> 16, j->member & 0xffff, &cl) : NULL;
        Bitmap *bm = m && m->type == MT_BITMAP ? member_bitmap(cl, m) : NULL;
        if (!bm) { vm_error("print: geen bitmap om af te drukken"); return 1; }
        uint32_t *px = stage_bitmap_argb(bm);
        host_print(px, bm->w, bm->h, x->ival, x->sval ? x->sval : "Skipper & Skeeto");
        free(px);
    }
    return 1;
}

void xobj_register(void) {}
