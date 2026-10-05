/* textlay.h - regels van een tekstmember, gedeeld door text_gdi.c en text_ttf.c. Zoals Director (TextEdit):
 * alinea's op '\r', afbreken op woordgrenzen binnen de breedte; een woord dat alleen al te breed is wordt op
 * een letter afgebroken (een lange naam in het opslagveld van mmdlg3). Spaties op de breekplek vallen weg. */
#ifndef TEXTLAY_H
#define TEXTLAY_H

typedef struct TLine { const char *p; int n; } TLine;
typedef int (*TMeasure)(void *ctx, const char *s, int n);

/* vult hoogstens max regels; geeft het aantal regels (ook als dat meer is dan max) */
static int text_layout(const char *s, int w, TMeasure measure, void *ctx, TLine *out, int max) {
    int nl = 0;
    for (;;) {   /* per alinea */
        const char *e = s;
        while (*e && *e != '\r' && *e != '\n') e++;
        const char *p = s;
        do {   /* per regel: zoveel woorden als passen */
            const char *q = p, *fit = NULL;
            while (q < e) {
                const char *we = q;
                while (we < e && *we == ' ') we++;
                while (we < e && *we != ' ') we++;
                if (measure(ctx, p, (int)(we - p)) > w) {
                    if (!fit) {   /* het eerste woord past al niet: afbreken op een letter (minstens één) */
                        int n = 1;
                        while (p + n < we && measure(ctx, p, n + 1) <= w) n++;
                        fit = p + n;
                    }
                    break;
                }
                fit = q = we;
            }
            if (!fit) fit = e;
            int n = (int)(fit - p);
            while (n > 0 && p[n - 1] == ' ') n--;
            if (nl < max) { out[nl].p = p; out[nl].n = n; }
            nl++;
            p = fit;
            while (p < e && *p == ' ') p++;
        } while (p < e);
        if (!*e) break;
        s = e + 1;
        if (*e == '\r' && *s == '\n') s++;
    }
    return nl;
}

/* bewerkbaar veld dat te klein is: zoveel regels omhoog dat de laatste (met de invoegstreep) zichtbaar is */
static int text_scroll(int nlines, int lh, int h, int caret) {
    if (!caret || lh <= 0 || nlines * lh <= h) return 0;
    int vis = h / lh > 0 ? h / lh : 1;
    return (nlines - vis) * lh;
}
#endif
