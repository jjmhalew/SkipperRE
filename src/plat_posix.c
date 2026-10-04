/* plat_posix.c - plat.h op Linux en Android */
#ifndef _WIN32
#include "plat.h"
#undef fopen
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <SDL.h>
#endif

static void slashes(const char *in, char *out, size_t n) {
    size_t i = 0;
    for (; in[i] && i + 1 < n; i++) out[i] = in[i] == '\\' ? '/' : in[i];
    out[i] = 0;
}

/* pad waarvan elk deel zonder op hoofdletters te letten wordt opgezocht; 0 = een deel bestaat niet */
static int resolve(const char *in, char *out, size_t cap) {
    char buf[1024];
    slashes(in, buf, sizeof buf);
    size_t o = 0;
    out[0] = 0;
    char *p = buf;
    if (*p == '/') { out[o++] = '/'; out[o] = 0; while (*p == '/') p++; }
    while (*p) {
        char *e = strchr(p, '/');
        if (e) *e = 0;
        char test[1100];
        struct stat st;
        snprintf(test, sizeof test, "%s%s", out, p);
        if (!strcmp(p, ".") || !strcmp(p, "..") || stat(test, &st) == 0) o += (size_t)snprintf(out + o, cap - o, "%s", p);
        else {
            DIR *d = opendir(o ? out : ".");
            struct dirent *de;
            int found = 0;
            if (d) {
                while ((de = readdir(d)))
                    if (!strcasecmp(de->d_name, p)) { o += (size_t)snprintf(out + o, cap - o, "%s", de->d_name); found = 1; break; }
                closedir(d);
            }
            if (!found) return 0;
        }
        if (o >= cap - 1) return 0;
        if (!e) break;
        out[o++] = '/';
        out[o] = 0;
        p = e + 1;
        while (*p == '/') p++;
    }
    return 1;
}

/* schrijven: de map wordt ook opgezocht, de bestandsnaam blijft zoals gevraagd */
static void resolve_parent(const char *in, char *out, size_t cap) {
    char buf[1024];
    slashes(in, buf, sizeof buf);
    char *sl = strrchr(buf, '/');
    if (!sl) { snprintf(out, cap, "%s", buf); return; }
    *sl = 0;
    char dir[1024];
    if (!buf[0]) { snprintf(out, cap, "/%s", sl + 1); return; }
    if (resolve(buf, dir, sizeof dir)) snprintf(out, cap, "%s/%s", dir, sl + 1);
    else snprintf(out, cap, "%s/%s", buf, sl + 1);
}

FILE *plat_fopen(const char *path, const char *mode) {
    char p[1024], real[1024];
    slashes(path, p, sizeof p);
    if (mode[0] != 'r') {
        FILE *f = fopen(p, mode);
        if (f) return f;
        resolve_parent(path, real, sizeof real);
        return fopen(real, mode);
    }
    FILE *f = fopen(p, mode);
    if (f || errno != ENOENT) return f;
    if (!resolve(path, real, sizeof real)) return NULL;
    return fopen(real, mode);
}

int plat_exists(const char *path) {
    char p[1024];
    struct stat st;
    slashes(path, p, sizeof p);
    if (stat(p, &st) == 0) return 1;
    return resolve(path, p, sizeof p);
}

int plat_is_dir(const char *path) {
    char p[1024];
    struct stat st;
    if (!resolve(path, p, sizeof p)) return 0;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

void plat_mkdir(const char *path) {
    char p[1024];
    slashes(path, p, sizeof p);
    for (char *q = p + 1; *q; q++)   /* ook de tussenliggende mappen (~/.local/share) */
        if (*q == '/') { *q = 0; mkdir(p, 0755); *q = '/'; }
    mkdir(p, 0755);
}

void plat_exe_path(char *out, int n) {
    ssize_t k = readlink("/proc/self/exe", out, (size_t)n - 1);
    if (k <= 0) { snprintf(out, n, "skipper"); return; }
    out[k] = 0;
}

void plat_exe_dir(char *out, int n) {
    plat_exe_path(out, n);
    char *sl = strrchr(out, '/');
    if (sl) *sl = 0;
}

void plat_full_path(const char *in, char *out, int n) {
    char p[1024];
    slashes(in, p, sizeof p);
    if (p[0] == '/') { snprintf(out, n, "%s", p); return; }
    char cwd[700];
    if (!getcwd(cwd, sizeof cwd)) snprintf(cwd, sizeof cwd, ".");
    snprintf(out, n, "%s/%s", cwd, p);
}

void plat_user_dir(char *out, int n) {
#ifdef __ANDROID__
    const char *ext = SDL_AndroidGetExternalStoragePath();
    snprintf(out, n, "%s", ext ? ext : SDL_AndroidGetInternalStoragePath());
#else
    const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    if (x && x[0]) snprintf(out, n, "%s/SkipperRE", x);
    else if (h && h[0]) snprintf(out, n, "%s/.local/share/SkipperRE", h);
    else snprintf(out, n, "save");
#endif
    plat_mkdir(out);
}

void plat_temp_dir(char *out, int n) {
#ifdef __ANDROID__
    plat_user_dir(out, n);
    snprintf(out + strlen(out), n - strlen(out), "/tmp");
    plat_mkdir(out);
#else
    const char *t = getenv("TMPDIR");
    snprintf(out, n, "%s", t && t[0] ? t : "/tmp");
#endif
}

int plat_find_ext(const char *dir, const char *ext, char *out, int n) {
    char p[1024];
    if (!resolve(dir, p, sizeof p)) return 0;
    DIR *d = opendir(p);
    if (!d) return 0;
    struct dirent *de;
    size_t el = strlen(ext);
    int ok = 0;
    while ((de = readdir(d))) {
        size_t l = strlen(de->d_name);
        if (l > el && !strcasecmp(de->d_name + l - el, ext)) { snprintf(out, n, "%s/%s", p, de->d_name); ok = 1; break; }
    }
    closedir(d);
    return ok;
}

/* gemounte schijven: /media/<gebruiker>/<label>, /run/media/<gebruiker>/<label>, /media/<label>, /mnt/<x> */
int plat_cd_dirs(char out[][PLAT_PATH], int max) {
    int k = 0;
    const char *roots[] = {"/media", "/run/media", "/mnt"};
    for (int r = 0; r < 3; r++) {
        DIR *d = opendir(roots[r]);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d)) && k < max) {
            if (de->d_name[0] == '.') continue;
            char p[PLAT_PATH];
            snprintf(p, sizeof p, "%s/%s", roots[r], de->d_name);
            snprintf(out[k++], PLAT_PATH, "%s", p);
            DIR *d2 = opendir(p);
            if (!d2) continue;
            struct dirent *e2;
            while ((e2 = readdir(d2)) && k < max)
                if (e2->d_name[0] != '.') snprintf(out[k++], PLAT_PATH, "%s/%s", p, e2->d_name);
            closedir(d2);
        }
        closedir(d);
    }
    return k;
}

void plat_sleep(int ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

uint32_t plat_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
void plat_lock(void) { pthread_mutex_lock(&g_mx); }
void plat_unlock(void) { pthread_mutex_unlock(&g_mx); }
#endif
