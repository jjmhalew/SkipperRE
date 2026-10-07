/* android.c - wat alleen Android nodig heeft:
 *   - de eerste start: Androids bestandskiezer (SkipperActivity.java) voor een image van de cd (BIN of ISO, gelezen via
 *     de file descriptor die de kiezer geeft: /proc/self/fd/N) of een map met een kopie van de cd (naar de app-map
 *     gekopieerd); daarna staan de spelbestanden in Android/data/io.github.jjmhalew.skipperre/files/data
 *   - meldingen in Androids eigen dialoog (SDL's berichtvenster laat in liggend formaat zijn knoppen onder het scherm)
 *   - stderr naar skipper.log in de app-map
 * Aanraken staat in touch.c (ook voor de Switch). */
#ifdef __ANDROID__
#include "dir.h"
#undef fopen
#include <SDL.h>
#include <jni.h>
#include <unistd.h>

/* ------------------------------------------------------------------ Java */
static jclass act_class(JNIEnv **env) {
    *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!*env || !act) return NULL;
    jclass c = (**env)->GetObjectClass(*env, act);
    (**env)->DeleteLocalRef(*env, act);
    return c;
}

static void clear_exc(JNIEnv *env) { if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env); }

/* SkipperActivity.dialog: 1 = b1, 0 = b2, -1 = b3 */
int android_dialog(const char *text, const char *b1, const char *b2, const char *b3) {
    JNIEnv *env;
    jclass c = act_class(&env);
    int r = -1;
    if (!c) return -1;
    jmethodID m = (*env)->GetStaticMethodID(env, c, "dialog", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I");
    if (m) {
        jstring s[4] = {(*env)->NewStringUTF(env, text), b1 ? (*env)->NewStringUTF(env, b1) : NULL,
                        b2 ? (*env)->NewStringUTF(env, b2) : NULL, b3 ? (*env)->NewStringUTF(env, b3) : NULL};
        r = (*env)->CallStaticIntMethod(env, c, m, s[0], s[1], s[2], s[3]);
        for (int i = 0; i < 4; i++) if (s[i]) (*env)->DeleteLocalRef(env, s[i]);
    }
    clear_exc(env);
    (*env)->DeleteLocalRef(env, c);
    return r;
}

static void progress(const char *text) {
    JNIEnv *env;
    jclass c = act_class(&env);
    if (!c) return;
    jmethodID m = (*env)->GetStaticMethodID(env, c, "progress", "(Ljava/lang/String;)V");
    if (m) {
        jstring s = text ? (*env)->NewStringUTF(env, text) : NULL;
        (*env)->CallStaticVoidMethod(env, c, m, s);
        if (s) (*env)->DeleteLocalRef(env, s);
    }
    clear_exc(env);
    (*env)->DeleteLocalRef(env, c);
}

/* SkipperActivity.pickGameData(kind, dest): 1 = image -> file descriptor; 2 = map, gekopieerd naar dest -> 0 */
static int java_pick(int kind, const char *dest) {
    JNIEnv *env;
    jclass c = act_class(&env);
    int r = -2;
    if (!c) return -2;
    jmethodID m = (*env)->GetStaticMethodID(env, c, "pickGameData", "(ILjava/lang/String;)I");
    if (m) {
        jstring d = (*env)->NewStringUTF(env, dest ? dest : "");
        r = (*env)->CallStaticIntMethod(env, c, m, kind, d);
        (*env)->DeleteLocalRef(env, d);
    }
    clear_exc(env);
    (*env)->DeleteLocalRef(env, c);
    return r;
}

/* host_pick_data op Android: kiezen, en meteen uitpakken of kopiëren; out = de map met de spelbestanden */
int android_pick_data(char *out, int n) {
    char user[PLAT_PATH];
    plat_user_dir(user, sizeof user);
    for (;;) {
        int k = android_dialog(UI("Welkom bij Skipper & Skeeto in Pretpark!\n\n"
                                  "De spelbestanden komen van je eigen cd. Kies een image van de cd (SKIPPER_1.BIN, een .iso "
                                  "of een .img), of een map met een kopie van alle bestanden op de cd. Ze worden eenmalig in "
                                  "de app gezet (ongeveer 200 MB).",
                                  "Welcome to Skipper & Skeeto (Magnus & Myggen)!\n\n"
                                  "The game files come from your own CD. Choose an image of the CD (a .bin, .iso or .img), "
                                  "or a folder with a copy of all files on the CD. They are put into the app once (about "
                                  "200 MB)."),
                               UI("Cd-image", "CD image"), UI("Map van de cd", "CD folder"), UI("Stoppen", "Quit"));
        if (k < 0) return 0;
        if (k == 1) {
            int fd = java_pick(1, NULL);
            if (fd == -1) continue;
            if (fd < 0) { android_dialog(UI("Dat bestand kon niet worden geopend.", "That file could not be opened."), "OK", NULL, NULL); continue; }
            char img[64], bin[PLAT_PATH] = "";
            snprintf(img, sizeof img, "/proc/self/fd/%d", fd);
            progress(UI("Spelbestanden uitpakken...", "Unpacking the game files..."));
            int ok = disc_use(img, user, out, n, bin, sizeof bin);
            progress(NULL);
            close(fd);
            if (ok) return 1;
            android_dialog(UI("In dat bestand staan de spelbestanden van Skipper & Skeeto niet. Kies het image van de cd "
                              "(SKIPPER_1.BIN, niet het .cue-bestand), een .iso of een .img.",
                              "The game files of Skipper & Skeeto are not in that file. Choose the image of the CD "
                              "(the .bin, not the .cue file; the .img, not the .ccd), or an .iso."), "OK", NULL, NULL);
        } else {
            char part[PLAT_PATH], data[PLAT_PATH];
            snprintf(part, sizeof part, "%s/data.part", user);
            snprintf(data, sizeof data, "%s/data", user);
            plat_mkdir(part);
            progress(UI("Spelbestanden kopiëren...", "Copying the game files..."));
            int r = java_pick(2, part);
            progress(NULL);
            if (r == -1) continue;
            snprintf(out, n, "%s", part);
            if (r == 0 && disc_use(part, user, out, n, NULL, 0)) {
                if (rename(part, data) == 0) snprintf(out, n, "%s", data);
                return 1;
            }
            android_dialog(UI("In die map staan de spelbestanden van Skipper & Skeeto niet (Magnus.dxr ontbreekt).",
                              "The game files of Skipper & Skeeto are not in that folder (Magnus.dxr is missing)."),
                           "OK", NULL, NULL);
        }
    }
}

void android_init(void) {
    char log[PLAT_PATH];
    plat_user_dir(log, sizeof log);
    snprintf(log + strlen(log), sizeof log - strlen(log), "/skipper.log");
    freopen(log, "w", stderr);
    setvbuf(stderr, NULL, _IOLBF, 0);
}
#endif
