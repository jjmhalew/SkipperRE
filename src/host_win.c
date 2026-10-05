/* host_win.c - het venster op Windows (Win32/GDI): tonen, muis en toetsen, cursors, Alt+Enter, afdrukken, de
 * opslagdialoog, de bestandskiezer voor de spelbestanden en de crash-melding. Elders: host_sdl.c. */
#ifdef _WIN32
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <dbghelp.h>
#include <commdlg.h>

static HWND g_hwnd;
static RECT g_dst = {0, 0, 1280, 960};   /* waar het podium in het venster staat (beeldverhouding 4:3) */
static int g_fullscreen;
static unsigned g_devchanges;   /* WM_DEVICECHANGE-teller: een pad kwam of ging */
static WINDOWPLACEMENT g_wp = {sizeof(WINDOWPLACEMENT)};

void host_message(const char *text, int warn) {
    fprintf(stderr, "%s\n", text);
    if (!g_headless) MessageBoxA(g_hwnd, text, "Skipper & Skeeto", MB_OK | (warn ? MB_ICONWARNING : MB_ICONINFORMATION));
}

/* taal van Windows ("da", "sv", ...): de weergavetaal, anders die van de landinstellingen */
void host_locale(char *out, int n) {
    LCID id = MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT);
    if (!GetLocaleInfoA(id, LOCALE_SISO639LANGNAME, out, n) && !GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, out, n))
        out[0] = 0;
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
        ini_get(ini, "Saved games", key, "", name, sizeof name);
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
                ini_get(ini, "Saved games", key, "", name, sizeof name);
                if (!name[0]) { g_dlg_done = 0; slot = 0; MessageBeep(MB_ICONWARNING); }   /* lege positie: blijf */
            } else {
                GetWindowTextA(g_dlg_edit, name, sizeof name);
                if (!name[0]) snprintf(name, sizeof name, "Spel %d", slot);
                ini_set(ini, "Saved games", key, name);
            }
        }
    }
    EnableWindow(g_hwnd, TRUE);
    DestroyWindow(d);
    SetForegroundWindow(g_hwnd);
    return g_dlg_result ? slot : 0;
}

/* Spelbestanden niet gevonden: vragen naar een cd-image of naar Magnus.dxr (op de cd of in een map) */
int host_pick_data(char *out, int n) {
    if (g_headless) return 0;
    if (MessageBoxA(NULL,
                    UI("De spelbestanden van Skipper & Skeeto zijn niet gevonden.\n\n"
                       "Stop de cd in het cd-station, of kies hierna een image van de cd (.cue, .bin, .iso of .img), "
                       "of Magnus.dxr op de cd of in een map met de bestanden van de cd.\n\n"
                       "Een image wordt eenmalig uitgepakt naar %APPDATA%\\SkipperRE\\data.",
                       "The game files of Skipper & Skeeto (Magnus & Myggen) were not found.\n\n"
                       "Put the CD in the drive, or choose an image of the CD next (.cue, .bin, .iso or .img), "
                       "or Magnus.dxr on the CD or in a folder with the files of the CD.\n\n"
                       "An image is unpacked once to %APPDATA%\\SkipperRE\\data."),
                    "Skipper & Skeeto", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
        return 0;
    char file[MAX_PATH] = "";
    OPENFILENAMEA of = {0};
    of.lStructSize = sizeof of;
    of.lpstrFilter = UI("Cd-image of Magnus.dxr (*.cue;*.bin;*.iso;*.img;*.ccd;Magnus.dxr)\0*.cue;*.bin;*.iso;*.img;*.ccd;Magnus.dxr\0"
                        "Alle bestanden\0*.*\0",
                        "CD image or Magnus.dxr (*.cue;*.bin;*.iso;*.img;*.ccd;Magnus.dxr)\0*.cue;*.bin;*.iso;*.img;*.ccd;Magnus.dxr\0"
                        "All files\0*.*\0");
    of.lpstrFile = file;
    of.nMaxFile = sizeof file;
    of.lpstrTitle = UI("Skipper & Skeeto: cd-image of Magnus.dxr kiezen", "Skipper & Skeeto: choose a CD image or Magnus.dxr");
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&of)) return 0;
    char *sl = strrchr(file, '\\');
    if (sl && !_stricmp(sl + 1, "Magnus.dxr")) *sl = 0;   /* de map */
    snprintf(out, n, "%s", file);
    return 1;
}

/* ------------------------------------------------------------------ tonen */
static const uint32_t *g_shown;   /* laatst getoonde beeld (WM_PAINT) */
static int g_shown_w, g_shown_h;

void host_blit(const uint32_t *px, int w, int h) {
    if (g_headless || !g_hwnd) return;
    g_shown = px; g_shown_w = w; g_shown_h = h;
    HDC dc = GetDC(g_hwnd);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    if (g_dst.right - g_dst.left < w) { SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, NULL); }   /* HD-beeld verkleinen */
    else SetStretchBltMode(dc, COLORONCOLOR);
    RECT cr;
    GetClientRect(g_hwnd, &cr);
    /* zwarte randen rond het podium (venster met andere verhouding, volledig scherm) */
    if (g_dst.top > 0) PatBlt(dc, 0, 0, cr.right, g_dst.top, BLACKNESS);
    if (g_dst.bottom < cr.bottom) PatBlt(dc, 0, g_dst.bottom, cr.right, cr.bottom - g_dst.bottom, BLACKNESS);
    if (g_dst.left > 0) PatBlt(dc, 0, g_dst.top, g_dst.left, g_dst.bottom - g_dst.top, BLACKNESS);
    if (g_dst.right < cr.right) PatBlt(dc, g_dst.right, g_dst.top, cr.right - g_dst.right, g_dst.bottom - g_dst.top, BLACKNESS);
    StretchDIBits(dc, g_dst.left, g_dst.top, g_dst.right - g_dst.left, g_dst.bottom - g_dst.top, 0, 0, w, h, px, &bi,
                  DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_hwnd, dc);
}

static void layout(void) {
    RECT cr;
    if (!g_hwnd || !GetClientRect(g_hwnd, &cr)) return;   /* WM_SIZE kan al tijdens CreateWindow komen */
    if (cr.right <= 0 || cr.bottom <= 0) return;
    int l, t, w, h;
    stage_fit(cr.right, cr.bottom, &l, &t, &w, &h);
    g_dst.left = l; g_dst.top = t; g_dst.right = l + w; g_dst.bottom = t + h;
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

/* ------------------------------------------------------------------ cursors */
static HCURSOR g_cur_arrow, g_cur_wait;
static HCURSOR g_cur_cache[64];
static int g_cur_key[64], g_cur_scale[64], g_ncur;
static Movie *g_cur_mv[64];   /* cursorleden van het podium of van een dialoogvenster */

/* cursors groeien mee met het podium (een 16x16-cursor hoort bij 640x480) */
static int cursor_scale(void) {
    int s = (g_dst.right - g_dst.left + 320) / 640;
    return s < 1 ? 1 : s > 8 ? 8 : s;
}

static HCURSOR bitmap_cursor(Datum lst) {
    int num = d_toint(lst.u.l->v[0]), mask = lst.u.l->n > 1 ? d_toint(lst.u.l->v[1]) : 0;
    int key = num << 16 | (mask & 0xffff), s = cursor_scale();
    for (int i = 0; i < g_ncur; i++) if (g_cur_key[i] == key && g_cur_scale[i] == s && g_cur_mv[i] == cursor_movie()) return g_cur_cache[i];
    int cw, ch, hx, hy;
    uint32_t *img = cursor_image(lst, s, &cw, &ch, &hx, &hy);
    if (!img) return g_cur_arrow;
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = cw;
    bi.bmiHeader.biHeight = -ch;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HDC dc = GetDC(NULL);
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!color || !bits) { free(img); return g_cur_arrow; }
    memcpy(bits, img, (size_t)cw * ch * 4);
    free(img);
    HBITMAP bmask = CreateBitmap(cw, ch, 1, 1, NULL);
    ICONINFO ii = {FALSE, (DWORD)hx, (DWORD)hy, bmask, color};
    HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
    DeleteObject(bmask);
    DeleteObject(color);
    if (!c) return g_cur_arrow;
    if (g_ncur == 64) {   /* vol (bijv. na vaak van grootte wisselen): opnieuw beginnen */
        for (int i = 0; i < g_ncur; i++) DestroyCursor(g_cur_cache[i]);
        g_ncur = 0;
    }
    g_cur_key[g_ncur] = key; g_cur_scale[g_ncur] = s; g_cur_mv[g_ncur] = cursor_movie(); g_cur_cache[g_ncur++] = c;
    return c;
}

static HCURSOR current_cursor(void) {
    Datum c = cursor_wanted();
    if (c.t == T_LIST && c.u.l->n >= 1) return bitmap_cursor(c);
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

/* De vensterprocedure werkt alleen de toestand bij (muispositie, knop) en zet events in de wachtrij van main.c */
static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    int x, y;
    to_stage(lp, &x, &y);
    switch (msg) {
    case WM_CLOSE: P.halted = 2; return 0;
    case WM_DEVICECHANGE: g_devchanges++; break;
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
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); if (g_shown) host_blit(g_shown, g_shown_w, g_shown_h); return 0; }
    case WM_MOUSEMOVE: P.mouse_x = x; P.mouse_y = y; return 0;
    case WM_LBUTTONDOWN: SetCapture(h); P.mouse_x = x; P.mouse_y = y; P.mouse_down = 1; input_push(1, x, y, 0, 0); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); P.mouse_x = x; P.mouse_y = y; P.mouse_down = 0; input_push(2, x, y, 0, 0); return 0;
    case WM_RBUTTONDOWN: input_push(1, x, y, 1, 0); return 0;
    case WM_RBUTTONUP: input_push(2, x, y, 1, 0); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(current_cursor()); return TRUE; }
        break;
    case WM_KEYDOWN: {
        MSG m;
        int ch = 0;
        if (PeekMessageA(&m, h, WM_CHAR, WM_CHAR, PM_REMOVE)) ch = (int)m.wParam;
        g_pending_char = ch;
        input_push(3, 0, 0, mac_keycode((int)wp), ch);
        return 0;
    }
    case WM_KEYUP: input_push(4, 0, 0, mac_keycode((int)wp), g_pending_char); return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}


void host_events(void) {
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    if (g_hwnd && pad_input(GetForegroundWindow() == g_hwnd, g_devchanges)) {
        /* de pad verplaatste de aanwijzer: de echte muis erheen (midden van de podiumpixel) */
        int w = g_dst.right - g_dst.left, h = g_dst.bottom - g_dst.top;
        POINT pt = {g_dst.left + (P.mouse_x * 2 + 1) * w / 1280, g_dst.top + (P.mouse_y * 2 + 1) * h / 960};
        ClientToScreen(g_hwnd, &pt);
        SetCursorPos(pt.x, pt.y);
    }
}

int host_open(int scale, int fullscreen) {
    {   /* echte pixels op schermen met schaal > 100% (anders schaalt Windows het venster wazig op) */
        typedef BOOL(WINAPI * SetDpiCtx)(HANDLE);
        SetDpiCtx f = (SetDpiCtx)(void *)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
        if (f) f((HANDLE)(intptr_t)-4);   /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
    }
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "SkipperRE";
    wc.hCursor = NULL;
    char ico[300];   /* het icoon van de cd (Magnus.ico in de datamap) */
    snprintf(ico, sizeof ico, "%s\\Magnus.ico", P.base_dir);
    wc.hIcon = (HICON)LoadImageA(NULL, ico, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (!wc.hIcon) wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(1));   /* in de exe (build.sh) */
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassA(&wc);
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (scale <= 0) {   /* grootste gehele schaal waarbij het venster in het werkgebied past */
        RECT wa;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
        RECT fr = {0, 0, 640, 480};
        AdjustWindowRect(&fr, style, FALSE);
        int bw = (fr.right - fr.left) - 640, bh = (fr.bottom - fr.top) - 480;
        scale = 1;
        while (640 * (scale + 1) + bw <= wa.right - wa.left && 480 * (scale + 1) + bh <= wa.bottom - wa.top) scale++;
    }
    RECT r = {0, 0, 640 * scale, 480 * scale};
    AdjustWindowRect(&r, style, FALSE);
    g_hwnd = CreateWindowA("SkipperRE", g_title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) return 0;
    layout();
    ShowWindow(g_hwnd, SW_SHOW);
    if (fullscreen) toggle_fullscreen();
    g_cur_arrow = LoadCursor(NULL, IDC_ARROW);
    g_cur_wait = LoadCursor(NULL, IDC_WAIT);
    return 1;
}

/* ------------------------------------------------------------------ crash: adres + frame-pointer-keten via de PDB */
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

void host_crash_init(void) { SetUnhandledExceptionFilter(crash_filter); }
#endif
