/* SkipperRE - Win32-host: venster, timing, invoer, cursors, transities, paden.
 *
 *   skipper.exe [datamap] [--movie start] [--bin SKIPPER_1.BIN] [--scale 2] [--trace]
 *               [--shot N out.bmp]   (headless: N frames draaien, stage opslaan, stoppen)
 *               [--click x y F]      (headless: klik op (x,y) vlak voor frame F)
 */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <math.h>
#include <dbghelp.h>

extern int sound_headless;
void palette_step(void);
void player_idle(void);

static HWND g_hwnd;
static int g_scale = 2;
static int g_headless;
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

/* MMSYS.LoadSaveGame: het origineel toont een dialoog met spelposities. Voorlopig: positie 1. */
int ld_save_game(int is_load) {
    (void)is_load;
    return 1;
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
    StretchDIBits(dc, 0, 0, 640 * g_scale, 480 * g_scale, 0, 0, 640, 480, px, &bi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_hwnd, dc);
}

static void pump(void);

/* Director-transities (codes 1..52): een benadering met wipes/center-out/dissolve */
static void transition(const uint32_t *from, const uint32_t *to, int type, int dur) {
    if (g_headless || dur <= 0) return;
    static uint32_t tmp[640 * 480];
    uint32_t t0 = now_ms();
    for (;;) {
        double t = (double)(now_ms() - t0) / dur;
        if (t >= 1) break;
        for (int y = 0; y < 480; y++)
            for (int x = 0; x < 640; x++) {
                int i = y * 640 + x, show;
                double cx = fabs((double)x - 320) / 320.0, cy = fabs((double)y - 240) / 240.0;
                switch (type) {
                case 1: show = x < t * 640; break;                    /* wipe right */
                case 2: show = x > (1 - t) * 640; break;              /* wipe left */
                case 3: show = y < t * 480; break;                    /* wipe down */
                case 4: show = y > (1 - t) * 480; break;              /* wipe up */
                case 5: show = cx < t; break;                         /* center out horizontal */
                case 6: show = cx > 1 - t; break;                     /* edges in horizontal */
                case 7: show = cy < t; break;                         /* center out vertical */
                case 8: show = cy > 1 - t; break;                     /* edges in vertical */
                case 9: show = cx < t && cy < t; break;               /* center out square */
                case 10: show = cx > 1 - t || cy > 1 - t; break;      /* edges in square */
                default: show = ((x * 7 + y * 13) * 2654435761u >> 24) / 255.0 < t; break;
                }
                tmp[i] = show ? to[i] : from[i];
            }
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
        transition(g_prev, stage_px, P.trans_type, P.trans_dur > 2000 ? 2000 : P.trans_dur);
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

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    int x = (short)LOWORD(lp) / g_scale, y = (short)HIWORD(lp) / g_scale;
    switch (msg) {
    case WM_CLOSE: P.halted = 2; return 0;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); if (stage_px) blit(stage_px); return 0; }
    case WM_MOUSEMOVE: player_mouse(x, y, 0, 0, 0); return 0;
    case WM_LBUTTONDOWN: SetCapture(h); player_mouse(x, y, 1, 0, 0); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); player_mouse(x, y, 0, 1, 0); return 0;
    case WM_RBUTTONDOWN: player_mouse(x, y, 1, 0, 1); return 0;
    case WM_RBUTTONUP: player_mouse(x, y, 0, 1, 1); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(current_cursor()); return TRUE; }
        break;
    case WM_KEYDOWN: {
        MSG m;
        int ch = 0;
        if (PeekMessageA(&m, h, WM_CHAR, WM_CHAR, PM_REMOVE)) ch = (int)m.wParam;
        g_pending_char = ch;
        player_key(mac_keycode((int)wp), ch, 1);
        return 0;
    }
    case WM_KEYUP: player_key(mac_keycode((int)wp), g_pending_char, 0); return 0;
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

static void make_window(void) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "SkipperRE";
    wc.hCursor = NULL;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassA(&wc);
    RECT r = {0, 0, 640 * g_scale, 480 * g_scale};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    g_hwnd = CreateWindowA("SkipperRE", "Skipper & Skeeto in Pretpark", style, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(g_hwnd, SW_SHOW);
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
    const char *data = "extract", *movie = "start", *shot = NULL;
    int shot_frames = 0;
    int clicks[64][3], nclicks = 0, every = 0;
    char bin[300] = "";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--movie") && i + 1 < argc) movie = argv[++i];
        else if (!strcmp(argv[i], "--bin") && i + 1 < argc) snprintf(bin, sizeof bin, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) g_scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace")) vm_trace = 1;
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shot_frames = atoi(argv[++i]); shot = argv[++i]; g_headless = 1; }
        else if (!strcmp(argv[i], "--click") && i + 3 < argc && nclicks < 64) {
            clicks[nclicks][0] = atoi(argv[++i]); clicks[nclicks][1] = atoi(argv[++i]); clicks[nclicks++][2] = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atoi(argv[++i]);
        else data = argv[i];
    }
    if (g_scale < 1) g_scale = 1;
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
        if (!g_headless) pump();
        uint32_t t = now_ms();
        if (g_headless || (int32_t)(t - next) >= 0) {
            for (int k = 0; k < nclicks; k++)
                if (frames == clicks[k][2]) {
                    player_mouse(clicks[k][0], clicks[k][1], 0, 0, 0);
                    player_mouse(clicks[k][0], clicks[k][1], 1, 0, 0);
                    player_mouse(clicks[k][0], clicks[k][1], 0, 1, 0);
                }
            DBG_CHECK();
            int ms = player_tick();
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
    if (shot) {
        stage_compose();
        stage_screenshot(shot);
        fprintf(stderr, "frame %d van %s -> %s\n", P.frame, P.mv ? P.mv->name : "?", shot);
    }
    return 0;
}
