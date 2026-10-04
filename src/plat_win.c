/* plat_win.c - plat.h op Windows */
#ifdef _WIN32
#include "plat.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

int plat_exists(const char *path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

int plat_is_dir(const char *path) {
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void plat_mkdir(const char *path) { CreateDirectoryA(path, NULL); }

void plat_exe_path(char *out, int n) {
    if (!GetModuleFileNameA(NULL, out, n)) snprintf(out, n, "skipper.exe");
}

void plat_exe_dir(char *out, int n) {
    plat_exe_path(out, n);
    char *sl = strrchr(out, '\\');
    if (sl) *sl = 0;
}

void plat_full_path(const char *in, char *out, int n) {
    if (!GetFullPathNameA(in, n, out, NULL)) snprintf(out, n, "%s", in);
}

void plat_temp_dir(char *out, int n) {
    if (!GetTempPathA(n, out)) snprintf(out, n, ".");
    size_t l = strlen(out);
    if (l && out[l - 1] == '\\') out[l - 1] = 0;
}

void plat_user_dir(char *out, int n) {
    char base[MAX_PATH];
    if (GetEnvironmentVariableA("APPDATA", base, sizeof base)) snprintf(out, n, "%s\\SkipperRE", base);
    else snprintf(out, n, "save");
    CreateDirectoryA(out, NULL);
}

int plat_find_ext(const char *dir, const char *ext, char *out, int n) {
    char pat[PLAT_PATH];
    snprintf(pat, sizeof pat, "%s\\*%s", dir, ext);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    snprintf(out, n, "%s\\%s", dir, fd.cFileName);
    FindClose(h);
    return 1;
}

int plat_cd_dirs(char out[][PLAT_PATH], int max) {
    DWORD drives = GetLogicalDrives();
    int k = 0;
    for (int d = 0; d < 26 && k < max; d++) {
        if (!(drives & (1u << d))) continue;
        char root[8] = {(char)('A' + d), ':', '\\', 0};
        if (GetDriveTypeA(root) != DRIVE_CDROM) continue;
        snprintf(out[k++], PLAT_PATH, "%c:", 'A' + d);
    }
    return k;
}

void plat_sleep(int ms) { Sleep(ms); }
uint32_t now_ms(void) { return GetTickCount(); }

static CRITICAL_SECTION g_cs;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK cs_init(PINIT_ONCE o, PVOID p, PVOID *c) { (void)o; (void)p; (void)c; InitializeCriticalSection(&g_cs); return TRUE; }
void plat_lock(void) { InitOnceExecuteOnce(&g_once, cs_init, NULL, NULL); EnterCriticalSection(&g_cs); }
void plat_unlock(void) { LeaveCriticalSection(&g_cs); }
#endif
