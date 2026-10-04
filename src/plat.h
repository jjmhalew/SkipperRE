/* plat.h - het weinige dat de engine van het besturingssysteem nodig heeft (bestanden, mappen, tijd, een mutex).
 * Windows: plat_win.c. Linux / Android: plat_posix.c. De engine schrijft paden met '\' (zoals het spel zelf);
 * buiten Windows maakt plat_fopen daar '/' van en zoekt elk deel van het pad op zonder op hoofdletters te letten
 * (de bestanden op de cd heten MAGNUS.DXR, het spel vraagt om Magnus.dxr). */
#pragma once
#include <stdio.h>
#include <stdint.h>

#ifndef _WIN32
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _strdup strdup
#define _fseeki64 fseeko
#define _ftelli64 ftello
FILE *plat_fopen(const char *path, const char *mode);
#define fopen plat_fopen
#endif

#define PLAT_PATH 600

int plat_exists(const char *path);                   /* bestand of map */
int plat_is_dir(const char *path);
void plat_mkdir(const char *path);
void plat_exe_dir(char *out, int n);                 /* map van het programma */
void plat_exe_path(char *out, int n);
void plat_full_path(const char *in, char *out, int n);
void plat_temp_dir(char *out, int n);
void plat_user_dir(char *out, int n);                /* %APPDATA%\SkipperRE, ~/.local/share/SkipperRE, Android: app-map */
int plat_find_ext(const char *dir, const char *ext, char *out, int n);   /* eerste bestand met deze extensie (".cue") */
int plat_cd_dirs(char out[][PLAT_PATH], int max);    /* cd-stations en gemounte schijven */
void plat_sleep(int ms);
void plat_lock(void);                                /* één globale mutex (mixer <-> engine) */
void plat_unlock(void);
uint32_t plat_ms(void);                              /* milliseconden, monotoon */
uint32_t now_ms(void);                               /* main.c: plat_ms, of de virtuele klok van een headless run */

/* ini.c: Windows-INI-bestanden zoals GetPrivateProfileString/WritePrivateProfileString (ook op Windows zelf) */
int ini_get(const char *file, const char *sect, const char *key, const char *def, char *out, int n);
int ini_get_int(const char *file, const char *sect, const char *key, int def);
int ini_set(const char *file, const char *sect, const char *key, const char *val);
