/* SkipperRE - Win32-host: venster, timing, invoer, cursors, transities, paden.
 *
 *   skipper.exe [datamap] [--movie start] [--bin SKIPPER_1.BIN] [--scale 2] [--trace]
 *               [--shot N out.bmp]   (headless: N frames draaien, stage opslaan, stoppen)
 *               [--click x y F]      (headless: klik op (x,y) vlak voor frame F)
 *               [--drag x1 y1 x2 y2 F] (headless: slepen van (x1,y1) naar (x2,y2) vlak voor frame F)
 *               [--key code char F]  (headless: toets met Mac-keyCode en teken (ASCII) vlak voor frame F)
 *               [--global naam int F] (headless: global op een getal zetten vlak voor frame F)
 */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <math.h>
#include <dbghelp.h>
#include <commdlg.h>

extern int sound_headless;
static HWND g_hwnd;
static int g_headless;
void palette_step(void);
void player_idle(void);

static int g_scale = 0;          /* beginvenster = 640x480 * g_scale; 0 = zo groot als past */
static RECT g_dst = {0, 0, 1280, 960};   /* waar het podium in het venster staat (beeldverhouding 4:3) */
static int g_fullscreen, g_start_fullscreen;
static WINDOWPLACEMENT g_wp = {sizeof(WINDOWPLACEMENT)};
static uint32_t *g_prev;       /* laatst getoonde stage (voor transities) */

uint32_t now_ms(void) { return GetTickCount(); }

void host_alert(const char *msg) {
    fprintf(stderr, "[alert] %s\n", msg);
    if (!g_headless) MessageBoxA(g_hwnd, msg, "Skipper & Skeeto", MB_OK | MB_ICONINFORMATION);
}

/* Paden van het spel (D:\..., C:\WINDOWS\..., "CD:\...") -> datamap of opslagmap */
char *path_resolve(const char *p, char *out, int n) {
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '\\' || *q == '/' || *q == ':') b = q + 1;
    char cand[600];
    snprintf(cand, sizeof cand, "%s\\%s", P.save_dir, b);
    if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) { snprintf(out, n, "%s", cand); return out; }
    snprintf(cand, sizeof cand, "%s\\%s", P.base_dir, b);
    if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) { snprintf(out, n, "%s", cand); return out; }
    snprintf(out, n, "%s\\%s", P.save_dir, b);   /* nieuw bestand: in de opslagmap */
    return out;
}

/* Afdrukken (PrintOMatic in het tekenspel): Windows-printdialoog, plaatje zo groot mogelijk binnen
 * marges van 1 inch, liggend als het spel dat vraagt. Headless: BMP in de opslagmap. */
void host_print(const uint32_t *px, int w, int h, int landscape, const char *name) {
    if (g_headless) {
        char path[600];
        snprintf(path, sizeof path, "%s\\print.bmp", P.save_dir);
        bmp_write(path, px, w, h);
        fprintf(stderr, "[print] %s %dx%d %s -> %s\n", name, w, h, landscape ? "liggend" : "staand", path);
        return;
    }
    PRINTDLGA pd = {0};
    pd.lStructSize = sizeof pd;
    pd.hwndOwner = g_hwnd;
    pd.Flags = PD_RETURNDEFAULT;
    if (PrintDlgA(&pd) && pd.hDevMode) {   /* standaardinstellingen ophalen om de oriëntatie te zetten */
        DEVMODEA *dm = GlobalLock(pd.hDevMode);
        if (dm) {
            dm->dmFields |= DM_ORIENTATION;
            dm->dmOrientation = landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
            GlobalUnlock(pd.hDevMode);
        }
    }
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    if (!PrintDlgA(&pd) || !pd.hDC) {
        if (pd.hDevMode) GlobalFree(pd.hDevMode);
        if (pd.hDevNames) GlobalFree(pd.hDevNames);
        return;
    }
    HDC dc = pd.hDC;
    DOCINFOA di = {0};
    di.cbSize = sizeof di;
    di.lpszDocName = name;
    if (StartDocA(dc, &di) > 0 && StartPage(dc) > 0) {
        int pw = GetDeviceCaps(dc, HORZRES), ph = GetDeviceCaps(dc, VERTRES);
        int mx = GetDeviceCaps(dc, LOGPIXELSX), my = GetDeviceCaps(dc, LOGPIXELSY);
        int aw = pw - 2 * mx, ah = ph - 2 * my;
        if (aw < pw / 2) { aw = pw; mx = 0; }
        if (ah < ph / 2) { ah = ph; my = 0; }
        double sc = (double)aw / w < (double)ah / h ? (double)aw / w : (double)ah / h;
        int dw = (int)(w * sc), dh = (int)(h * sc);
        BITMAPINFO bi = {0};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        SetStretchBltMode(dc, HALFTONE);
        StretchDIBits(dc, mx + (aw - dw) / 2, my + (ah - dh) / 2, dw, dh, 0, 0, w, h, px, &bi, DIB_RGB_COLORS, SRCCOPY);
        EndPage(dc);
        EndDoc(dc);
    }
    DeleteDC(dc);
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
}

/* MMSYS.LoadSaveGame(hwnd, isLoad): dialoog met 16 spelposities. De namen staan in
 * <gMMPath>\MAGNUS.INI [Saved games] GAMEn (SavedGamesExists leest die ook); het spel zelf schrijft
 * daarna MMSAVn.MMS. Geeft het gekozen nummer terug, 0 = annuleren. */
static int g_dlg_done, g_dlg_result;
static HWND g_dlg_list, g_dlg_edit;

static LRESULT CALLBACK dlg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == 1 || (LOWORD(wp) == 10 && HIWORD(wp) == LBN_DBLCLK)) { g_dlg_result = 1; g_dlg_done = 1; return 0; }
        if (LOWORD(wp) == 2) { g_dlg_result = 0; g_dlg_done = 1; return 0; }
        if (LOWORD(wp) == 10 && HIWORD(wp) == LBN_SELCHANGE && g_dlg_edit) {
            char buf[80];
            int i = (int)SendMessageA(g_dlg_list, LB_GETCURSEL, 0, 0);
            SendMessageA(g_dlg_list, LB_GETTEXT, i, (LPARAM)buf);
            char *p = strchr(buf, '\t');
            SetWindowTextA(g_dlg_edit, p && strcmp(p + 1, "(leeg)") ? p + 1 : "");
        }
        break;
    case WM_CLOSE: g_dlg_result = 0; g_dlg_done = 1; return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

int ld_save_game(int is_load) {
    if (g_headless) return 0;
    char ini[600];
    snprintf(ini, sizeof ini, "%s\\MAGNUS.INI", P.save_dir);
    static int reg;
    if (!reg) {
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = dlg_proc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "SkipperDlg";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        reg = 1;
    }
    RECT pr;
    GetWindowRect(g_hwnd, &pr);
    int w = 360, hgt = is_load ? 400 : 440;
    HWND d = CreateWindowA("SkipperDlg", is_load ? "Spel laden" : "Spel opslaan", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                           (pr.left + pr.right - w) / 2, (pr.top + pr.bottom - hgt) / 2, w, hgt, g_hwnd, NULL, NULL, NULL);
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    g_dlg_list = CreateWindowA("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_USETABSTOPS,
                               10, 10, w - 26, 300, d, (HMENU)10, NULL, NULL);
    SendMessageA(g_dlg_list, WM_SETFONT, (WPARAM)font, 0);
    for (int i = 1; i <= 16; i++) {
        char key[16], name[64], line[96];
        snprintf(key, sizeof key, "GAME%d", i);
        GetPrivateProfileStringA("Saved games", key, "", name, sizeof name, ini);
        snprintf(line, sizeof line, "%d.\t%s", i, name[0] ? name : "(leeg)");
        SendMessageA(g_dlg_list, LB_ADDSTRING, 0, (LPARAM)line);
    }
    SendMessageA(g_dlg_list, LB_SETCURSEL, 0, 0);
    int y = 316;
    g_dlg_edit = NULL;
    if (!is_load) {
        HWND lbl = CreateWindowA("STATIC", "Naam:", WS_CHILD | WS_VISIBLE, 10, y + 4, 50, 20, d, NULL, NULL, NULL);
        SendMessageA(lbl, WM_SETFONT, (WPARAM)font, 0);
        g_dlg_edit = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 60, y, w - 76, 24, d, NULL, NULL, NULL);
        SendMessageA(g_dlg_edit, WM_SETFONT, (WPARAM)font, 0);
        SendMessageA(g_dlg_edit, EM_LIMITTEXT, 40, 0);
        y += 34;
    }
    HWND ok = CreateWindowA("BUTTON", is_load ? "Laden" : "Opslaan", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, w - 196, y, 85, 28, d, (HMENU)1, NULL, NULL);
    HWND cancel = CreateWindowA("BUTTON", "Annuleren", WS_CHILD | WS_VISIBLE, w - 104, y, 85, 28, d, (HMENU)2, NULL, NULL);
    SendMessageA(ok, WM_SETFONT, (WPARAM)font, 0);
    SendMessageA(cancel, WM_SETFONT, (WPARAM)font, 0);
    ShowWindow(d, SW_SHOW);
    EnableWindow(g_hwnd, FALSE);
    g_dlg_done = 0;
    g_dlg_result = 0;
    int slot = 0;
    MSG m;
    while (!g_dlg_done && GetMessageA(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(d, &m)) { TranslateMessage(&m); DispatchMessageA(&m); }
        if (g_dlg_done && g_dlg_result) {
            slot = (int)SendMessageA(g_dlg_list, LB_GETCURSEL, 0, 0) + 1;
            char key[16], name[64] = "";
            snprintf(key, sizeof key, "GAME%d", slot);
            if (is_load) {
                GetPrivateProfileStringA("Saved games", key, "", name, sizeof name, ini);
                if (!name[0]) { g_dlg_done = 0; slot = 0; MessageBeep(MB_ICONWARNING); }   /* lege positie: blijf */
            } else {
                GetWindowTextA(g_dlg_edit, name, sizeof name);
                if (!name[0]) snprintf(name, sizeof name, "Spel %d", slot);
                WritePrivateProfileStringA("Saved games", key, name, ini);
            }
        }
    }
    EnableWindow(g_hwnd, TRUE);
    DestroyWindow(d);
    SetForegroundWindow(g_hwnd);
    return g_dlg_result ? slot : 0;
}

static void setup_save_dir(void) {
    char base[MAX_PATH];
    if (GetEnvironmentVariableA("APPDATA", base, sizeof base))
        snprintf(P.save_dir, sizeof P.save_dir, "%s\\SkipperRE", base);
    else snprintf(P.save_dir, sizeof P.save_dir, "save");
    CreateDirectoryA(P.save_dir, NULL);
    char f[600];
    snprintf(f, sizeof f, "%s\\IVANOFF.INI", P.save_dir);
    if (GetFileAttributesA(f) == INVALID_FILE_ATTRIBUTES) {
        char dir[300];
        snprintf(dir, sizeof dir, "%s\\", P.save_dir);
        WritePrivateProfileStringA("MAGNUS", "Path", dir, f);
    }
    snprintf(f, sizeof f, "%s\\MAGNUS.INI", P.save_dir);
    if (GetFileAttributesA(f) == INVALID_FILE_ATTRIBUTES) {
        FILE *fp = fopen(f, "wb");
        if (fp) {
            fprintf(fp, "[MAGNUS]\r\nPath=\"%s\\\"\r\n[Sound]\r\nStartLevel=4\r\n", P.save_dir);
            fclose(fp);
        }
    }
}

/* ------------------------------------------------------------------ tonen */
static void blit(const uint32_t *px) {
    if (g_headless || !g_hwnd) return;
    HDC dc = GetDC(g_hwnd);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 640;
    bi.bmiHeader.biHeight = -480;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    SetStretchBltMode(dc, COLORONCOLOR);
    RECT cr;
    GetClientRect(g_hwnd, &cr);
    /* zwarte randen rond het podium (venster met andere verhouding, volledig scherm) */
    if (g_dst.top > 0) PatBlt(dc, 0, 0, cr.right, g_dst.top, BLACKNESS);
    if (g_dst.bottom < cr.bottom) PatBlt(dc, 0, g_dst.bottom, cr.right, cr.bottom - g_dst.bottom, BLACKNESS);
    if (g_dst.left > 0) PatBlt(dc, 0, g_dst.top, g_dst.left, g_dst.bottom - g_dst.top, BLACKNESS);
    if (g_dst.right < cr.right) PatBlt(dc, g_dst.right, g_dst.top, cr.right - g_dst.right, g_dst.bottom - g_dst.top, BLACKNESS);
    StretchDIBits(dc, g_dst.left, g_dst.top, g_dst.right - g_dst.left, g_dst.bottom - g_dst.top, 0, 0, 640, 480, px, &bi,
                  DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_hwnd, dc);
}

/* podium-rechthoek bij een nieuwe venstergrootte: zo groot mogelijk in 4:3, gecentreerd; een geheel
 * veelvoud als dat bijna past (scherpere pixels) */
static void layout(void) {
    RECT cr;
    if (!g_hwnd || !GetClientRect(g_hwnd, &cr)) return;   /* WM_SIZE kan al tijdens CreateWindow komen */
    int cw = cr.right, ch = cr.bottom;
    if (cw <= 0 || ch <= 0) return;
    int w = cw, h = cw * 3 / 4;
    if (h > ch) { h = ch; w = ch * 4 / 3; }
    int k = w / 640;
    if (k >= 1 && 640 * k >= w * 95 / 100) { w = 640 * k; h = 480 * k; }
    g_dst.left = (cw - w) / 2; g_dst.top = (ch - h) / 2;
    g_dst.right = g_dst.left + w; g_dst.bottom = g_dst.top + h;
}

/* vensterpixel -> podiumcoördinaat */
static void to_stage(LPARAM lp, int *x, int *y) {
    int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
    int w = g_dst.right - g_dst.left, h = g_dst.bottom - g_dst.top;
    *x = w > 0 ? (mx - g_dst.left) * 640 / w : mx;
    *y = h > 0 ? (my - g_dst.top) * 480 / h : my;
}

/* Alt+Enter: randloos volledig scherm op de huidige monitor en weer terug */
static void toggle_fullscreen(void) {
    DWORD style = GetWindowLongA(g_hwnd, GWL_STYLE);
    if (!g_fullscreen) {
        MONITORINFO mi = {sizeof mi};
        GetWindowPlacement(g_hwnd, &g_wp);
        GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongA(g_hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = 1;
    } else {
        SetWindowLongA(g_hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(g_hwnd, &g_wp);
        SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = 0;
    }
    layout();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void pump(void);

/* Director-transities (codes 1..52, zie trans.c), live in het venster */
static void transition(const uint32_t *from, const uint32_t *to, int type, int dur, int chunk) {
    if (g_headless || dur <= 0) return;
    static uint32_t tmp[640 * 480];
    uint32_t t0 = now_ms();
    for (;;) {
        double t = (double)(now_ms() - t0) / dur;
        if (t >= 1) break;
        trans_frame(tmp, from, to, type, chunk, t);
        blit(tmp);
        pump();
        Sleep(10);
    }
}

void stage_present(void) {
    stage_compose();
    if (!g_prev) g_prev = calloc(640 * 480, 4);
    if (P.trans_pending) {
        P.trans_pending = 0;
        transition(g_prev, stage_px, P.trans_type, P.trans_dur > 2000 ? 2000 : P.trans_dur, P.trans_chunk);
    }
    blit(stage_px);
    memcpy(g_prev, stage_px, 640 * 480 * 4);
    P.update_needed = 0;
}

void player_update_stage(void) { stage_present(); }

/* ------------------------------------------------------------------ cursors */
static HCURSOR g_cur_arrow, g_cur_wait;
static HCURSOR g_cur_cache[64];
static int g_cur_key[64], g_ncur;

static HCURSOR bitmap_cursor(Datum lst) {
    if (lst.t != T_LIST || lst.u.l->n < 1) return g_cur_arrow;
    int num = d_toint(lst.u.l->v[0]), mask = lst.u.l->n > 1 ? d_toint(lst.u.l->v[1]) : 0;
    int key = num << 16 | (mask & 0xffff);
    for (int i = 0; i < g_ncur; i++) if (g_cur_key[i] == key) return g_cur_cache[i];
    CastLib *cl, *ml;
    Member *m = movie_member(P.mv, (num >> 16) + 1, num & 0xffff, &cl);
    Member *mm = mask ? movie_member(P.mv, (mask >> 16) + 1, mask & 0xffff, &ml) : NULL;
    Bitmap *b = m ? member_bitmap(cl, m) : NULL;
    Bitmap *mb = mm ? member_bitmap(ml, mm) : NULL;
    if (!b) return g_cur_arrow;
    int cw = GetSystemMetrics(SM_CXCURSOR), ch = GetSystemMetrics(SM_CYCURSOR);
    uint8_t *andp = malloc(cw * ch / 8), *xorp = malloc(cw * ch / 8);
    memset(andp, 0xff, cw * ch / 8);
    memset(xorp, 0, cw * ch / 8);
    for (int y = 0; y < b->h && y < ch; y++)
        for (int x = 0; x < b->w && x < cw; x++) {
            int black = b->px[y * b->w + x] != 0;
            int opaque = mb ? (x < mb->w && y < mb->h && mb->px[y * mb->w + x] != 0) : black;
            int bit = 0x80 >> (x & 7), o = y * cw / 8 + x / 8;
            if (opaque) andp[o] &= ~bit;
            if (opaque && !black) xorp[o] |= bit;   /* wit */
        }
    HCURSOR c = CreateCursor(GetModuleHandle(NULL), b->reg_x, b->reg_y, cw, ch, andp, xorp);
    free(andp);
    free(xorp);
    if (g_ncur < 64) { g_cur_key[g_ncur] = key; g_cur_cache[g_ncur++] = c; }
    return c;
}

static HCURSOR current_cursor(void) {
    /* sprite-cursor onder de muis, anders de globale cursor() */
    Datum c = VOIDD;
    for (int ch = NCHAN; ch >= 1; ch--)
        if (sprite_hit(ch, P.mouse_x, P.mouse_y)) {
            if (P.ch[ch].cursor.t != T_VOID && !(P.ch[ch].cursor.t == T_INT && P.ch[ch].cursor.u.i == 0)) c = P.ch[ch].cursor;
            break;
        }
    if (c.t == T_VOID) c = *global_ref(sym("_cursor"));
    if (c.t == T_LIST) return bitmap_cursor(c);
    if (c.t == T_INT && c.u.i == 4) return g_cur_wait;
    if (c.t == T_INT && c.u.i == 200) return NULL;
    return g_cur_arrow;
}

/* ------------------------------------------------------------------ toetsen: Windows VK -> Mac keyCode */
static int mac_keycode(int vk) {
    static const struct { int vk, mac; } map[] = {
        {VK_ESCAPE, 53}, {VK_RETURN, 36}, {VK_TAB, 48}, {VK_SPACE, 49}, {VK_BACK, 51}, {VK_DELETE, 117},
        {VK_LEFT, 123}, {VK_RIGHT, 124}, {VK_DOWN, 125}, {VK_UP, 126}, {VK_HOME, 115}, {VK_END, 119},
        {VK_PRIOR, 116}, {VK_NEXT, 121}, {VK_F1, 122}, {VK_F2, 120}, {VK_F3, 99}, {VK_F4, 118},
        {VK_F5, 96}, {VK_F6, 97}, {VK_F7, 98}, {VK_F8, 100}, {VK_F9, 101}, {VK_F10, 109}, {VK_F11, 103}, {VK_F12, 111},
        {VK_ADD, 69}, {VK_SUBTRACT, 78}, {VK_OEM_PLUS, 24}, {VK_OEM_MINUS, 27},
        {'A', 0}, {'S', 1}, {'D', 2}, {'F', 3}, {'H', 4}, {'G', 5}, {'Z', 6}, {'X', 7}, {'C', 8}, {'V', 9},
        {'B', 11}, {'Q', 12}, {'W', 13}, {'E', 14}, {'R', 15}, {'Y', 16}, {'T', 17}, {'1', 18}, {'2', 19},
        {'3', 20}, {'4', 21}, {'6', 22}, {'5', 23}, {'9', 25}, {'7', 26}, {'8', 28}, {'0', 29}, {'O', 31},
        {'U', 32}, {'I', 34}, {'P', 35}, {'L', 37}, {'J', 38}, {'K', 40}, {'N', 45}, {'M', 46},
        {VK_NUMPAD0, 82}, {VK_NUMPAD1, 83}, {VK_NUMPAD2, 84}, {VK_NUMPAD3, 85}, {VK_NUMPAD4, 86},
        {VK_NUMPAD5, 87}, {VK_NUMPAD6, 88}, {VK_NUMPAD7, 89}, {VK_NUMPAD8, 91}, {VK_NUMPAD9, 92},
    };
    for (size_t i = 0; i < sizeof map / sizeof *map; i++) if (map[i].vk == vk) return map[i].mac;
    return 0;
}

static int g_pending_char;

/* Invoer: de vensterprocedure werkt alleen de toestand bij (muispositie, knop) en zet events in een
 * wachtrij; main() dispatcht ze als er geen Lingo loopt. Zo kan een script dat
 * `repeat while the stillDown` doet via host_pump() de echte knop zien zonder dat events
 * midden in een handler binnenkomen. */
typedef struct InEv { int kind, x, y, a, b; } InEv;   /* kind: 1 muis neer, 2 muis op, 3 toets neer, 4 toets op */
static InEv g_q[256];
static int g_qh, g_qt;
static void qpush(int kind, int x, int y, int a, int b) {
    int n = (g_qt + 1) & 255;
    if (n == g_qh) return;
    g_q[g_qt] = (InEv){kind, x, y, a, b};
    g_qt = n;
}

static void drain_input(void) {
    while (g_qh != g_qt) {
        InEv e = g_q[g_qh];
        g_qh = (g_qh + 1) & 255;
        if (e.kind == 1) player_mouse(e.x, e.y, 1, 0, e.a);
        else if (e.kind == 2) player_mouse(e.x, e.y, 0, 1, e.a);
        else player_key(e.a, e.b, e.kind == 3);
    }
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    int x, y;
    to_stage(lp, &x, &y);
    switch (msg) {
    case WM_CLOSE: P.halted = 2; return 0;
    case WM_SIZE: layout(); InvalidateRect(h, NULL, FALSE); return 0;
    case WM_DPICHANGED: {   /* naar een monitor met een andere schaal: de voorgestelde grootte overnemen */
        RECT *r = (RECT *)lp;
        if (!g_fullscreen)
            SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        RECT r = {0, 0, 320, 240};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        mm->ptMinTrackSize.x = r.right - r.left;
        mm->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_SYSKEYDOWN:
        if (wp == VK_RETURN && (lp & (1 << 29))) { toggle_fullscreen(); return 0; }
        break;
    case WM_SYSCHAR:
        if (wp == VK_RETURN) return 0;   /* geen piep na Alt+Enter */
        break;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); if (stage_px) blit(stage_px); return 0; }
    case WM_MOUSEMOVE: P.mouse_x = x; P.mouse_y = y; return 0;
    case WM_LBUTTONDOWN: SetCapture(h); P.mouse_x = x; P.mouse_y = y; P.mouse_down = 1; qpush(1, x, y, 0, 0); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); P.mouse_x = x; P.mouse_y = y; P.mouse_down = 0; qpush(2, x, y, 0, 0); return 0;
    case WM_RBUTTONDOWN: qpush(1, x, y, 1, 0); return 0;
    case WM_RBUTTONUP: qpush(2, x, y, 1, 0); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(current_cursor()); return TRUE; }
        break;
    case WM_KEYDOWN: {
        MSG m;
        int ch = 0;
        if (PeekMessageA(&m, h, WM_CHAR, WM_CHAR, PM_REMOVE)) ch = (int)m.wParam;
        g_pending_char = ch;
        qpush(3, 0, 0, mac_keycode((int)wp), ch);
        return 0;
    }
    case WM_KEYUP: qpush(4, 0, 0, mac_keycode((int)wp), g_pending_char); return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void pump(void) {
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
}

void host_pump(void) {
    if (g_headless) return;
    pump();
    if (player_drag_update() || P.update_needed) stage_present();   /* scripts die in een lus wachten, zien toch hun updateStage */
}

static void make_window(void) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "SkipperRE";
    wc.hCursor = NULL;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassA(&wc);
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (g_scale <= 0) {   /* grootste gehele schaal waarbij het venster in het werkgebied past */
        RECT wa;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
        RECT fr = {0, 0, 640, 480};
        AdjustWindowRect(&fr, style, FALSE);
        int bw = (fr.right - fr.left) - 640, bh = (fr.bottom - fr.top) - 480;
        g_scale = 1;
        while (640 * (g_scale + 1) + bw <= wa.right - wa.left && 480 * (g_scale + 1) + bh <= wa.bottom - wa.top) g_scale++;
    }
    RECT r = {0, 0, 640 * g_scale, 480 * g_scale};
    AdjustWindowRect(&r, style, FALSE);
    g_hwnd = CreateWindowA("SkipperRE", "Skipper & Skeeto in Pretpark", style, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    layout();
    ShowWindow(g_hwnd, SW_SHOW);
    if (g_start_fullscreen) toggle_fullscreen();
    g_cur_arrow = LoadCursor(NULL, IDC_ARROW);
    g_cur_wait = LoadCursor(NULL, IDC_WAIT);
}

/* crash: adres + frame-pointer-keten, via dbghelp en de PDB naar functie:regel */
static void crash_sym(uintptr_t a) {
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
    si->SizeOfStruct = sizeof(SYMBOL_INFO);
    si->MaxNameLen = 255;
    DWORD64 disp = 0;
    DWORD d2 = 0;
    IMAGEHLP_LINE64 ln = {sizeof ln};
    HANDLE pr = GetCurrentProcess();
    if (SymFromAddr(pr, a, &disp, si)) {
        if (SymGetLineFromAddr64(pr, a, &d2, &ln)) fprintf(stderr, "[crash]   %s  %s:%lu\n", si->Name, ln.FileName, ln.LineNumber);
        else fprintf(stderr, "[crash]   %s+%llx\n", si->Name, (unsigned long long)disp);
    } else fprintf(stderr, "[crash]   %p\n", (void *)a);
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep) {
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    CONTEXT *c = ep->ContextRecord;
    fprintf(stderr, "[crash] code %08lx\n", ep->ExceptionRecord->ExceptionCode);
    crash_sym((uintptr_t)ep->ExceptionRecord->ExceptionAddress);
    uintptr_t *fp = (uintptr_t *)c->Rbp;
    for (int i = 0; i < 24 && fp && !IsBadReadPtr(fp, 16); i++) {
        crash_sym(fp[1]);
        fp = (uintptr_t *)fp[0];
    }
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

int main(int argc, char **argv) {
    SetUnhandledExceptionFilter(crash_filter);
    {   /* echte pixels op schermen met schaal > 100% (anders schaalt Windows het venster wazig op) */
        typedef BOOL(WINAPI * SetDpiCtx)(HANDLE);
        SetDpiCtx f = (SetDpiCtx)(void *)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
        if (f) f((HANDLE)(intptr_t)-4);   /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
    }
    const char *data = "extract", *movie = "start", *shot = NULL;
    int shot_frames = 0;
    int clicks[64][3], nclicks = 0, every = 0, dump = 0;
    int drags[16][5], ndrags = 0;
    int keys[64][3], nkeys = 0;
    struct { const char *name; int val, frame; } globs[16];
    int nglobs = 0;
    char bin[300] = "";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--movie") && i + 1 < argc) movie = argv[++i];
        else if (!strcmp(argv[i], "--bin") && i + 1 < argc) snprintf(bin, sizeof bin, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) g_scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) g_start_fullscreen = 1;
        else if (!strcmp(argv[i], "--trace")) vm_trace = 1;
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shot_frames = atoi(argv[++i]); shot = argv[++i]; g_headless = 1; }
        else if (!strcmp(argv[i], "--click") && i + 3 < argc && nclicks < 64) {
            clicks[nclicks][0] = atoi(argv[++i]); clicks[nclicks][1] = atoi(argv[++i]); clicks[nclicks++][2] = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--drag") && i + 5 < argc && ndrags < 16) {
            for (int k = 0; k < 5; k++) drags[ndrags][k] = atoi(argv[++i]);
            ndrags++;
        }
        else if (!strcmp(argv[i], "--key") && i + 3 < argc && nkeys < 64) {
            for (int k = 0; k < 3; k++) keys[nkeys][k] = atoi(argv[++i]);
            nkeys++;
        }
        else if (!strcmp(argv[i], "--transtest") && i + 1 < argc) {   /* test: alle transities op t = 0.35 */
            static uint32_t a[640 * 480], b[640 * 480], o[640 * 480];
            for (int y = 0; y < 480; y++)
                for (int x = 0; x < 640; x++) {   /* oud: blauw met raster, nieuw: oranje met diagonalen */
                    a[y * 640 + x] = ((x % 80 < 4) || (y % 80 < 4)) ? 0xffffffffu : 0xff2040c0u | (uint32_t)(y * 255 / 480) << 8;
                    b[y * 640 + x] = ((x + y) % 60 < 6) ? 0xff000000u : 0xfff09020u | (uint32_t)(x * 255 / 640);
                }
            for (int ty = 1; ty <= 52; ty++) {
                char path[300];
                trans_frame(o, a, b, ty, 8, 0.35);
                snprintf(path, sizeof path, "%s/t%02d.bmp", argv[i + 1], ty);
                bmp_write(path, o, 640, 480);
            }
            return 0;
        }
        else if (!strcmp(argv[i], "--avi") && i + 3 < argc) {   /* test: één videoframe naar BMP */
            Video *v = video_open(argv[i + 1]);
            if (!v) return 1;
            int fr = atoi(argv[i + 2]);
            bmp_write(argv[i + 3], video_frame(v, fr), video_width(v), video_height(v));
            printf("%dx%d, %d frames, %d ticks, audio %d samples\n", video_width(v), video_height(v), video_frames(v),
                   video_duration(v), video_audio(v) ? video_audio(v)->frames : 0);
            return 0;
        }
        else if (!strcmp(argv[i], "--global") && i + 3 < argc && nglobs < 16) {
            globs[nglobs].name = argv[++i];
            globs[nglobs].val = atoi(argv[++i]);
            globs[nglobs++].frame = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump")) dump = 1;
        else data = argv[i];
    }
    if (g_scale < 0) g_scale = 0;
    char full[MAX_PATH];
    GetFullPathNameA(data, sizeof full, full, NULL);
    player_init(full);
    setup_save_dir();
    if (!bin[0]) snprintf(bin, sizeof bin, "%s\\..\\SKIPPER_1.BIN", full);
    GetFullPathNameA(bin, sizeof P.bin_path, P.bin_path, NULL);
    sound_headless = g_headless;
    sound_init();
    if (!g_headless) make_window();
    player_start(movie);
    stage_present();
    uint32_t next = now_ms();
    int frames = 0;
    while (P.halted != 2) {
        if (!g_headless) { pump(); drain_input(); if (player_drag_update()) stage_present(); }
        uint32_t t = now_ms();
        if (g_headless || (int32_t)(t - next) >= 0) {
            for (int k = 0; k < nclicks; k++)
                if (frames == clicks[k][2]) {
                    P.mouse_x = clicks[k][0]; P.mouse_y = clicks[k][1];
                    P.mouse_down = 0;   /* headless: de knop geldt meteen als losgelaten */
                    player_mouse(clicks[k][0], clicks[k][1], 1, 0, 0);
                    player_mouse(clicks[k][0], clicks[k][1], 0, 1, 0);
                }
            for (int k = 0; k < nglobs; k++)
                if (frames == globs[k].frame) {   /* headless: global zetten (testen: gPoints enz.) */
                    Datum *g = global_ref(sym(globs[k].name));
                    d_unref(*g);
                    *g = d_int(globs[k].val);
                }
            for (int k = 0; k < nkeys; k++)
                if (frames == keys[k][2]) {   /* headless: toets (Mac-keyCode, teken) neer en los */
                    player_key(keys[k][0], keys[k][1], 1);
                    player_key(keys[k][0], keys[k][1], 0);
                }
            for (int k = 0; k < ndrags; k++)
                if (frames == drags[k][4]) {   /* headless: neer op (x1,y1), slepen naar (x2,y2), los */
                    P.mouse_x = drags[k][0]; P.mouse_y = drags[k][1];
                    P.mouse_down = 1;
                    player_mouse(drags[k][0], drags[k][1], 1, 0, 0);
                    player_mouse(drags[k][2], drags[k][3], 0, 0, 0);
                    P.mouse_down = 0;
                    player_mouse(drags[k][2], drags[k][3], 0, 1, 0);
                }
            DBG_CHECK();
            int ms = player_tick();
            if (g_headless) player_idle();   /* headless: één idle per frame */
            DBG_CHECK();
            stage_present();
            frames++;
            if (shot && every && frames % every == 0) {
                char fn[300];
                snprintf(fn, sizeof fn, "%s.%04d.bmp", shot, frames);
                stage_screenshot(fn);
                fprintf(stderr, "[shot] %d %s frame %d\n", frames, P.mv ? P.mv->name : "?", P.frame);
            }
            next = t + (ms > 0 ? ms : 1);
            if (shot && frames >= shot_frames) break;
            if (g_headless && ms > 0) Sleep(ms / 4);   /* headless: sneller dan echt, maar timers lopen door */
        } else {
            player_idle();
            if (P.update_needed) stage_present();
            Sleep(1);
        }
        if (P.halted == 1 && !shot) { /* halt: blijf het laatste beeld tonen tot het venster sluit */ }
    }
    if (dump) {
        void globals_dump(FILE *);
        globals_dump(stderr);
        /* podium en daarna de open vensters (dialogen), elk in hun eigen coördinaten */
        Window *w = NULL;
        for (Player *ctx = &P; ctx; ) {
            if (ctx != &P) fprintf(stderr, "-- venster %s (%s)\n", w->name, ctx->mv ? ctx->mv->name : "?");
            CP = ctx;
            for (int ch = 1; ch <= NCHAN; ch++) {
                Channel *c = &ctx->ch[ch];
                if (!c->member || !c->visible) continue;
                CastLib *cl;
                Member *m = movie_member(ctx->mv, c->lib, c->member, &cl);
                int l, t, r, b;
                sprite_rect(ch, &l, &t, &r, &b);
                fprintf(stderr, "[ch %2d] %d:%d %-14s type %d ink %d fg %d bg %d rect %d,%d-%d,%d stretch %d puppet %d script %d:%d\n", ch,
                        c->lib, c->member, m ? m->name : "?", m ? m->type : -1, c->ink, c->fore, c->back, l, t, r, b,
                        c->stretch, c->puppet, c->slib, c->script);
            }
            w = w ? w->next : P.windows;
            while (w && !(w->open && w->ctx && w->ctx->mv)) w = w->next;
            ctx = w ? w->ctx : NULL;
        }
        CP = &P;
    }
    if (shot) {
        stage_compose();
        stage_screenshot(shot);
        fprintf(stderr, "frame %d van %s -> %s\n", P.frame, P.mv ? P.mv->name : "?", shot);
    }
    return 0;
}
