/* Geluid: waveOut-mixer (44,1 kHz stereo) met 8 Director-kanalen en CD-audio rechtstreeks uit de BIN.
 * CDPlayTrack(n) speelt track n van SKIPPER_1.BIN; de trackposities komen uit de .CUE. */
#include "dir.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <SDL.h>
#endif

#define RATE 44100
#define NBUF 4
#define BUFFRAMES 1024
#define NCH 8
#define VVOICE (NCH + 1)   /* extra stem voor het geluid van digitale video */

typedef struct Voice { Sound *s; double pos, step; int playing; uint32_t start, dur; } Voice;
static Voice g_v[NCH + 2];
#ifdef _WIN32
static HWAVEOUT g_wo;
static WAVEHDR g_hdr[NBUF];
static int16_t g_buf[NBUF][BUFFRAMES * 2];
#endif
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
    plat_lock();
    for (int c = 1; c <= VVOICE; c++) {
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
    plat_unlock();
    for (int i = 0; i < frames * 2; i++) {
        int32_t v = acc[i] * gain / 256;
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

#ifdef _WIN32
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
#else
static void audio_cb(void *u, Uint8 *stream, int len) {   /* SDL: in stukken van hoogstens BUFFRAMES */
    (void)u;
    int16_t *o = (int16_t *)stream;
    int frames = len / 4;
    while (frames > 0) {
        int k = frames < BUFFRAMES ? frames : BUFFRAMES;
        mix(o, k);
        o += k * 2;
        frames -= k;
    }
}
#endif

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

void sound_init(void) {
    /* CD: SKIPPER_1.BIN/.CUE */
    char cue[300];
    snprintf(cue, sizeof cue, "%s", P.bin_path);
    char *dot = strrchr(cue, '.');
    if (dot) strcpy(dot, ".CUE");
    cue_load(cue);
    g_bin = fopen(P.bin_path, "rb");
    if (sound_headless) return;
#ifdef _WIN32
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
#else
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) { fprintf(stderr, "[geluid] %s\n", SDL_GetError()); return; }
    SDL_AudioSpec want = {0}, have;
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = BUFFRAMES;
    want.callback = audio_cb;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) { fprintf(stderr, "[geluid] %s\n", SDL_GetError()); return; }
    g_ok = 1;
    SDL_PauseAudioDevice(dev, 0);
#endif
}

void sound_play_member(int ch, CastLib *c, Member *m) {
    sound_play_sound(ch, member_sound(c, m));
}

void sound_play_sound(int ch, Sound *s) {
    if (ch < 1 || ch > NCH) return;
    if (!s || !s->rate) return;
    plat_lock();
    g_v[ch].s = s;
    g_v[ch].pos = 0;
    g_v[ch].step = (double)s->rate / RATE;
    g_v[ch].playing = 1;
    g_v[ch].start = now_ms();
    g_v[ch].dur = (uint32_t)((double)s->frames * 1000 / s->rate);
    plat_unlock();
}

void sound_stop(int ch) {
    if (ch < 1 || ch > NCH) return;
    plat_lock();
    g_v[ch].playing = 0;
    plat_unlock();
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
    plat_lock();
    g_gain = lvl * 256 / 7;
    plat_unlock();
}

void cd_play_track(int track) {
    if (!g_bin || track < 1 || track > g_ntracks) { vm_error("CD-track %d niet beschikbaar", track); return; }
    plat_lock();
    g_cd_track = track;
    g_cd_pos = g_track_start[track] * 2352L;
    g_cd_end = g_track_end[track] * 2352L;
    plat_unlock();
}

void cd_stop(void) {
    plat_lock();
    g_cd_track = 0;
    plat_unlock();
}

int cd_playing(void) { return g_cd_track != 0; }

/* WAV-bestand (PCM 8/16-bit, mono/stereo) voor `sound playFile`; NULL als het niet lukt */
Sound *sound_load_wav(const char *path) {
    long n;
    uint8_t *b = vfs_load(path, &n);
    if (!b || n <= 44 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { free(b); return NULL; }
    int fmt = 0, ch = 0, rate = 0, bits = 0;
    const uint8_t *data = NULL;
    uint32_t dlen = 0;
    for (long o = 12; o + 8 <= n;) {
        uint32_t sz = b[o + 4] | b[o + 5] << 8 | b[o + 6] << 16 | (uint32_t)b[o + 7] << 24;
        if (o + 8 + (long)sz > n) sz = (uint32_t)(n - o - 8);
        if (!memcmp(b + o, "fmt ", 4) && sz >= 16) {
            fmt = b[o + 8] | b[o + 9] << 8;
            ch = b[o + 10] | b[o + 11] << 8;
            rate = b[o + 12] | b[o + 13] << 8 | b[o + 14] << 16 | b[o + 15] << 24;
            bits = b[o + 22] | b[o + 23] << 8;
        } else if (!memcmp(b + o, "data", 4)) { data = b + o + 8; dlen = sz; }
        o += 8 + sz + (sz & 1);
    }
    if (fmt != 1 || (ch != 1 && ch != 2) || (bits != 8 && bits != 16) || !data || rate <= 0) { free(b); return NULL; }
    Sound *s = calloc(1, sizeof(Sound));
    size_t samples = bits == 8 ? dlen : dlen / 2;
    s->pcm = malloc(samples * sizeof(int16_t) + 2);
    for (size_t i = 0; i < samples; i++)
        s->pcm[i] = bits == 8 ? (int16_t)((data[i] - 128) * 256) : (int16_t)(data[2 * i] | data[2 * i + 1] << 8);
    s->rate = rate; s->bits = 16; s->channels = ch; s->frames = (int)(samples / ch);
    free(b);
    return s;
}

/* geluidsspoor van een video vanaf offset (seconden) */
void sound_video_play(Sound *s, double offset) {
    if (!s || !s->rate) return;
    plat_lock();
    g_v[VVOICE].s = s;
    g_v[VVOICE].pos = offset * s->rate;
    g_v[VVOICE].step = (double)s->rate / RATE;
    g_v[VVOICE].playing = g_v[VVOICE].pos < s->frames;
    plat_unlock();
}

void sound_video_stop(void) {
    plat_lock();
    g_v[VVOICE].playing = 0;
    g_v[VVOICE].s = NULL;
    plat_unlock();
}
