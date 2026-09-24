/* Geluid: waveOut-mixer (44,1 kHz stereo) met 8 Director-kanalen en CD-audio rechtstreeks uit de BIN.
 * CDPlayTrack(n) speelt track n van SKIPPER_1.BIN; de trackposities komen uit de .CUE. */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>

#define RATE 44100
#define NBUF 4
#define BUFFRAMES 1024
#define NCH 8

typedef struct Voice { Sound *s; double pos, step; int playing; uint32_t start, dur; } Voice;
static Voice g_v[NCH + 1];
static CRITICAL_SECTION g_cs;
static HWAVEOUT g_wo;
static WAVEHDR g_hdr[NBUF];
static int16_t g_buf[NBUF][BUFFRAMES * 2];
static int g_gain = 256;
static int g_ok;
int sound_headless;

/* CD */
static FILE *g_bin;
static long g_track_start[100], g_track_end[100];   /* in sectoren */
static int g_ntracks;
static int g_cd_track;
static long g_cd_pos, g_cd_end;                       /* in bytes */

static void mix(int16_t *out, int frames) {
    static int32_t acc[BUFFRAMES * 2];
    memset(acc, 0, sizeof(int32_t) * frames * 2);
    EnterCriticalSection(&g_cs);
    for (int c = 1; c <= NCH; c++) {
        Voice *v = &g_v[c];
        if (!v->playing || !v->s) continue;
        Sound *s = v->s;
        for (int i = 0; i < frames; i++) {
            int k = (int)v->pos;
            if (k >= s->frames) { v->playing = 0; break; }
            int l, r;
            if (s->channels == 2) { l = s->pcm[2 * k]; r = s->pcm[2 * k + 1]; }
            else l = r = s->pcm[k];
            acc[2 * i] += l;
            acc[2 * i + 1] += r;
            v->pos += v->step;
        }
    }
    if (g_bin && g_cd_track && g_cd_pos < g_cd_end) {
        static int16_t cd[BUFFRAMES * 2];
        long want = frames * 4;
        if (want > g_cd_end - g_cd_pos) want = g_cd_end - g_cd_pos;
        fseek(g_bin, g_cd_pos, SEEK_SET);
        size_t got = fread(cd, 1, want, g_bin);
        g_cd_pos += (long)got;
        for (size_t i = 0; i < got / 2; i++) acc[i] += cd[i];
        if (g_cd_pos >= g_cd_end) g_cd_track = 0;
    }
    int gain = g_gain;
    LeaveCriticalSection(&g_cs);
    for (int i = 0; i < frames * 2; i++) {
        int32_t v = acc[i] * gain / 256;
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

static DWORD WINAPI audio_thread(LPVOID p) {
    (void)p;
    for (;;) {
        for (int i = 0; i < NBUF; i++) {
            if (g_hdr[i].dwFlags & WHDR_DONE) {
                mix(g_buf[i], BUFFRAMES);
                g_hdr[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(g_wo, &g_hdr[i], sizeof(WAVEHDR));
            }
        }
        Sleep(5);
    }
    return 0;
}

static void cue_load(const char *cue) {
    FILE *f = fopen(cue, "rb");
    if (!f) return;
    char line[512];
    int track = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        int t, mm, ss, ff, idx;
        if (sscanf(p, "TRACK %d", &t) == 1) track = t;
        else if (sscanf(p, "INDEX %d %d:%d:%d", &idx, &mm, &ss, &ff) == 4 && idx == 1 && track < 100) {
            g_track_start[track] = (mm * 60 + ss) * 75 + ff;
            if (track > g_ntracks) g_ntracks = track;
        } else if (sscanf(p, "INDEX %d %d:%d:%d", &idx, &mm, &ss, &ff) == 4 && idx == 0 && track > 1 && track < 100) {
            g_track_end[track - 1] = (mm * 60 + ss) * 75 + ff;
        } else if (sscanf(p, "REM LEAD-OUT %d:%d:%d", &mm, &ss, &ff) == 3 && track < 100) {
            g_track_end[track] = (mm * 60 + ss) * 75 + ff;
        }
    }
    fclose(f);
    for (int t = 1; t < g_ntracks; t++)
        if (!g_track_end[t]) g_track_end[t] = g_track_start[t + 1];
}

static int g_cs_init;
void sound_init_cs(void) { if (!g_cs_init) { InitializeCriticalSection(&g_cs); g_cs_init = 1; } }

void sound_init(void) {
    sound_init_cs();
    /* CD: SKIPPER_1.BIN/.CUE */
    char cue[300];
    snprintf(cue, sizeof cue, "%s", P.bin_path);
    char *dot = strrchr(cue, '.');
    if (dot) strcpy(dot, ".CUE");
    cue_load(cue);
    g_bin = fopen(P.bin_path, "rb");
    if (sound_headless) return;
    WAVEFORMATEX wf = {0};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = RATE;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = RATE * 4;
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return;
    for (int i = 0; i < NBUF; i++) {
        g_hdr[i].lpData = (LPSTR)g_buf[i];
        g_hdr[i].dwBufferLength = BUFFRAMES * 4;
        waveOutPrepareHeader(g_wo, &g_hdr[i], sizeof(WAVEHDR));
        g_hdr[i].dwFlags |= WHDR_DONE;
    }
    g_ok = 1;
    CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
}

void sound_play_member(int ch, CastLib *c, Member *m) {
    if (ch < 1 || ch > NCH) return;
    Sound *s = member_sound(c, m);
    if (!s || !s->rate) return;
    EnterCriticalSection(&g_cs);
    g_v[ch].s = s;
    g_v[ch].pos = 0;
    g_v[ch].step = (double)s->rate / RATE;
    g_v[ch].playing = 1;
    g_v[ch].start = now_ms();
    g_v[ch].dur = (uint32_t)((double)s->frames * 1000 / s->rate);
    LeaveCriticalSection(&g_cs);
}

void sound_stop(int ch) {
    if (ch < 1 || ch > NCH) return;
    EnterCriticalSection(&g_cs);
    g_v[ch].playing = 0;
    LeaveCriticalSection(&g_cs);
}

int sound_busy(int ch) {
    if (ch < 1 || ch > NCH) return 0;
    /* zonder audio-apparaat (headless): bezig zolang de echte duur van het geluid */
    if (!g_ok) return g_v[ch].playing && now_ms() - g_v[ch].start < g_v[ch].dur;
    return g_v[ch].playing;
}

void sound_set_level(int lvl) {
    if (lvl < 0) lvl = 0;
    if (lvl > 7) lvl = 7;
    EnterCriticalSection(&g_cs);
    g_gain = lvl * 256 / 7;
    LeaveCriticalSection(&g_cs);
}

void cd_play_track(int track) {
    if (!g_bin || track < 1 || track > g_ntracks) { vm_error("CD-track %d niet beschikbaar", track); return; }
    EnterCriticalSection(&g_cs);
    g_cd_track = track;
    g_cd_pos = g_track_start[track] * 2352L;
    g_cd_end = g_track_end[track] * 2352L;
    LeaveCriticalSection(&g_cs);
}

void cd_stop(void) {
    EnterCriticalSection(&g_cs);
    g_cd_track = 0;
    LeaveCriticalSection(&g_cs);
}

int cd_playing(void) { return g_cd_track != 0; }
