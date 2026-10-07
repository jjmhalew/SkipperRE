/* switch.c - wat alleen de Nintendo Switch (homebrew: libnx, devkitA64, build_switch.sh) nodig heeft:
 *   - alles staat in sdmc:/switch/skipperre naast skipperre.nro: de spelbestanden in data/, de opgeslagen spellen,
 *     skipper.log (stderr)
 *   - de eerste start: een image van de cd in die map (SKIPPER_1.CUE + .BIN, een .iso, of CloneCD .ccd + .img) wordt
 *     eenmalig uitgepakt naar data/, met de voortgang op libnx' tekstconsole. Daarna stopt het programma: het venster
 *     van SDL komt niet op een scherm dat de console heeft gebruikt (WoodyRE zag dat in Eden); de volgende start speelt.
 *     De .BIN blijft nodig voor de muziek op de cd (de audiotracks).
 *   - meldingen in de foutdialoog van het systeem (SDL heeft op de Switch geen berichtvenster)
 *   - de slotkeuze van de Deense cd (MMSYS.LoadSaveGame) met het cijfertoetsenbord van het systeem
 * Alleen losse headers van libnx: switch.h zelf zou met de namen van dir.h kunnen botsen. */
#ifdef __SWITCH__
#include <switch/types.h>
#include <switch/result.h>
#include <switch/applets/error.h>
#include <switch/applets/swkbd.h>
#include <switch/runtime/devices/console.h>
#include <switch/runtime/pad.h>
#include <switch/services/applet.h>
#include "dir.h"
#undef fopen
#include <stdarg.h>

void switch_init(void) {
    char log[PLAT_PATH];
    plat_user_dir(log, sizeof log);
    snprintf(log + strlen(log), sizeof log - strlen(log), "/skipper.log");
    freopen(log, "w", stderr);
    setvbuf(stderr, NULL, _IOLBF, 0);
}

void switch_message(const char *text) {
    ErrorApplicationConfig c;
    if (R_SUCCEEDED(errorApplicationCreate(&c, text, NULL))) errorApplicationShow(&c);
}

/* ------------------------------------------------------------------ eerste start: image uitpakken */
static int g_con;   /* de tekstconsole staat aan */

static void con_printf(const char *fmt, ...) {
    if (!g_con) { consoleInit(NULL); g_con = 1; }
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    fflush(stdout);
    consoleUpdate(NULL);
}

static void on_progress(const char *name, uint64_t bytes) {
    con_printf("\r%5u MB  %-40.40s", (unsigned)(bytes >> 20), name);
}

static int has_game(const char *dir) {
    char p[PLAT_PATH];
    snprintf(p, sizeof p, "%s/Magnus.dxr", dir);
    return plat_exists(p) || disc_d4(dir);
}

/* het image in de map: CUE eerst (die wijst de BIN aan), dan ISO, CloneCD, en een BIN of IMG zonder iets ernaast */
static int find_image(const char *home, char *img, int n) {
    static const char *const exts[] = {".cue", ".iso", ".ccd", ".bin", ".img"};
    for (int i = 0; i < 5; i++)
        if (plat_find_ext(home, exts[i], img, n)) return 1;
    return 0;
}

/* 1 = er is net uitgepakt (of dat mislukte) en het programma moet stoppen; 0 = verder zoals altijd */
int switch_unpack(void) {
    char home[PLAT_PATH], data[PLAT_PATH], img[PLAT_PATH];
    plat_user_dir(home, sizeof home);
    snprintf(data, sizeof data, "%s/data", home);
    if (has_game(data) || !find_image(home, img, sizeof img)) return 0;
    con_printf("%s\n\n%s\n\n", UI("Skipper & Skeeto in Pretpark", "Skipper & Skeeto (Magnus & Myggen)"),
               UI("De spelbestanden worden eenmalig uit het cd-image gehaald. Dat duurt een paar minuten...",
                  "Unpacking the game files from the CD image. This happens once and takes a few minutes..."));
    con_printf("%s\n\n", img);
    disc_progress = on_progress;
    int ok = disc_extract(img, data, NULL, 0) && has_game(data);
    disc_progress = NULL;
    fprintf(stderr, "[switch] %s uitpakken: %s\n", img, ok ? "gelukt" : "mislukt");
    if (ok)
        con_printf("\n\n%s\n\n%s\n", UI("Klaar. Laat het cd-image (.cue en .bin) staan: de muziek van de cd komt daaruit.",
                                        "Done. Keep the CD image (.cue and .bin) where it is: the music of the CD is read from it."),
                   UI("Druk op + om te stoppen en start SkipperRE opnieuw.", "Press + to exit, then start SkipperRE again."));
    else
        con_printf("\n\n%s\n\n%s\n", UI("De spelbestanden van Skipper & Skeeto staan niet in dit image, of de SD-kaart is vol.",
                                        "The game files of Skipper & Skeeto are not in this image, or the SD card is full."),
                   UI("Druk op + om te stoppen.", "Press + to exit."));
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    fflush(stdout);
    consoleExit(NULL);   /* daarna niets meer naar stdout: dat schreef in het vrijgegeven beeld van de console */
    g_con = 0;
    return 1;
}

/* geen spelbestanden en geen image: uitleg waar ze heen moeten */
int switch_pick_data(char *out, int n) {
    (void)out; (void)n;
    char home[PLAT_PATH], m[2 * PLAT_PATH + 600];
    plat_user_dir(home, sizeof home);
    snprintf(m, sizeof m,
             UI("SkipperRE heeft de bestanden van de cd-rom Skipper & Skeeto in Pretpark nodig.\n\n"
                "Zet een image van de cd (SKIPPER_1.CUE en SKIPPER_1.BIN, een .iso, of .ccd en .img) in %s/ "
                "(het wordt bij de volgende start eenmalig uitgepakt), of kopieer alle bestanden van de cd naar\n%s/data/\n"
                "en start SkipperRE opnieuw.",
                "SkipperRE needs the files of the CD-ROM Skipper & Skeeto (Magnus & Myggen).\n\n"
                "Put an image of the CD (SKIPPER_1.CUE and SKIPPER_1.BIN, an .iso, or .ccd and .img) into %s/ "
                "(the next start unpacks it once), or copy all files of the CD into\n%s/data/\nand start SkipperRE again."),
             home, home);
    fprintf(stderr, "%s\n", m);
    switch_message(m);
    return 0;
}

/* de BIN met de audiotracks: SKIPPER_1.BIN, of de BIN waar de .cue in de map naar wijst */
void switch_find_bin(char *bin, int n) {
    if (plat_exists(bin)) return;
    char home[PLAT_PATH], f[PLAT_PATH];
    plat_user_dir(home, sizeof home);
    if (plat_find_ext(home, ".bin", f, sizeof f)) snprintf(bin, n, "%s", f);
}

/* MMSYS.LoadSaveGame (alleen de Deense cd van 1996): het cijfertoetsenbord met de namen erboven; 0 = annuleren */
int switch_slot(int is_load, const char names[][64], int slots) {
    char list[512] = "";
    size_t o = 0;
    for (int i = 0; i < slots && o < sizeof list - 80; i++)
        if (names[i][0] || !is_load) o += (size_t)snprintf(list + o, sizeof list - o, "%d: %.24s  ", i + 1, names[i][0] ? names[i] : "-");
    SwkbdConfig k;
    if (R_FAILED(swkbdCreate(&k, 0))) return 0;
    swkbdConfigMakePresetDefault(&k);
    swkbdConfigSetType(&k, SwkbdType_NumPad);
    swkbdConfigSetHeaderText(&k, is_load ? UI("Welk spel? (1-8)", "Which game? (1-8)") : UI("Op welke plaats? (1-8)", "In which slot? (1-8)"));
    swkbdConfigSetSubText(&k, list);
    swkbdConfigSetStringLenMax(&k, 1);
    swkbdConfigSetStringLenMin(&k, 1);
    char out[8] = "";
    Result rc = swkbdShow(&k, out, sizeof out);
    swkbdClose(&k);
    int slot = R_SUCCEEDED(rc) ? atoi(out) : 0;
    if (slot < 1 || slot > slots || (is_load && !names[slot - 1][0])) return 0;
    return slot;
}
#endif
