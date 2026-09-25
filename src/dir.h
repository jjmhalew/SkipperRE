/* SkipperRE - Director 5-runtime: gedeelde types.
 * Bestanden: dfile.c (RIFX, cast, bitmaps, scripts), lingo.c (waarden + VM), player.c (films, frames,
 * sprites, events, builtins), stage.c (compositie), xobj.c (INI/FileIO/MovUtils/DLLGlue), sound.c, main.c. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DBGHEAP
void *dbg_malloc(size_t n, const char *file, int line);
void *dbg_calloc(size_t a, size_t n, const char *file, int line);
void *dbg_realloc(void *p, size_t n, const char *file, int line);
void dbg_free(void *p, const char *file, int line);
char *dbg_strdup(const char *s, const char *file, int line);
void dbg_check(const char *file, int line);
#define malloc(n) dbg_malloc(n, __FILE__, __LINE__)
#define calloc(a, n) dbg_calloc(a, n, __FILE__, __LINE__)
#define realloc(p, n) dbg_realloc(p, n, __FILE__, __LINE__)
#define free(p) dbg_free(p, __FILE__, __LINE__)
#define strdup(s) dbg_strdup(s, __FILE__, __LINE__)
#define DBG_CHECK() dbg_check(__FILE__, __LINE__)
#else
#define DBG_CHECK() ((void)0)
#endif

#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

/* ------------------------------------------------------------------ waarden */
typedef enum {
    T_VOID = 0, T_INT, T_FLOAT, T_STR, T_SYM, T_LIST, T_PLIST, T_POINT, T_RECT,
    T_OBJ,      /* scriptinstantie (birth/new) */
    T_SCRIPT,   /* script "x" */
    T_MEMBER,   /* u.i = lib << 16 | nummer */
    T_XOBJ,     /* XObject (factory of instantie) */
    T_VARREF,   /* u.i = symbool */
    T_CASTLIB,  /* u.i = libnummer */
    T_WINDOW,   /* u.w */
    T_PICTURE,
} VType;

typedef struct Str { int rc; int len; char s[]; } Str;
struct List; struct Obj; struct Script; struct XObj; struct Window;

typedef struct Datum {
    uint8_t t;
    union {
        int32_t i;
        double f;
        Str *s;
        struct List *l;
        struct Obj *o;
        struct Script *sc;
        struct XObj *x;
        struct Window *w;
    } u;
} Datum;

typedef struct List { int rc; int n, cap; Datum *v; } List; /* PLIST: v[2k]=sleutel, v[2k+1]=waarde */

typedef struct Obj {
    int rc;
    struct Script *script;
    int n;         /* aantal props */
    int *names;    /* symbolen */
    Datum *vals;
} Obj;

typedef struct XObj {
    int rc;
    int kind;       /* XK_* */
    int is_factory;
    void *st;
    int ival;
    char *sval;
} XObj;
enum { XK_NONE, XK_INI, XK_FILEIO, XK_MOVUTILS, XK_DLLGLUE, XK_PRINT };

extern const Datum VOIDD;
Datum d_int(int32_t i);
Datum d_float(double f);
Datum d_str(const char *s);
Datum d_strn(const char *s, int n);
Datum d_sym(int sym);
Datum d_list(int type, int cap);
Datum d_point(int x, int y);
Datum d_rect(int l, int t, int r, int b);
Datum d_member(int lib, int num);
Datum d_ref(Datum d);           /* refcount +1, geeft d terug */
void d_unref(Datum d);
void list_push(List *l, Datum d); /* neemt eigenaarschap over */
int d_toint(Datum d);
double d_tofloat(Datum d);
const char *d_tostr(Datum d, char *buf, int n);  /* tekstvorm (zoals string()) */
Str *d_asstr(Datum d);          /* nieuwe Str (rc 1) met de tekstvorm */
int d_truthy(Datum d);
int d_equal(Datum a, Datum b);
int d_compare(Datum a, Datum b);  /* <0 / 0 / >0 */
void d_print(FILE *f, Datum d);

/* symbolen: hoofdletterongevoelig geïnterneerd */
int sym(const char *name);
int symn(const char *name, int n);
const char *symname(int s);

/* ------------------------------------------------------------------ bestanden */
typedef struct Chunk { uint32_t tag, size, off; } Chunk;
typedef struct KeyEnt { int32_t sec, owner; uint32_t tag; } KeyEnt;
typedef struct DFile {
    char path[260];
    uint8_t *data;
    size_t len;
    int le;
    uint32_t codec;
    Chunk *chunks;
    int nchunks;
    KeyEnt *keys;
    int nkeys;
} DFile;

DFile *dfile_open(const char *path);
const uint8_t *dfile_chunk(DFile *f, int id, uint32_t *size);
int dfile_child(DFile *f, int owner, uint32_t tag);
int dfile_first(DFile *f, uint32_t tag);
int dfile_owned(DFile *f, uint32_t tag, int owner);

enum { MT_NULL = 0, MT_BITMAP = 1, MT_FILMLOOP = 2, MT_TEXT = 3, MT_PALETTE = 4, MT_PICTURE = 5,
       MT_SOUND = 6, MT_BUTTON = 7, MT_SHAPE = 8, MT_MOVIE = 9, MT_VIDEO = 10, MT_SCRIPT = 11,
       MT_RICHTEXT = 12 };

typedef struct Bitmap {
    int w, h, pitch, bpp;
    int left, top, reg_x, reg_y;   /* reg relatief t.o.v. linksboven */
    int clut_lib, clut;
    uint8_t *px;    /* w*h indices (8 bpp), 1 bpp omgezet naar 0/255 */
} Bitmap;

typedef struct Sound {
    int rate, bits, channels, frames;
    int16_t *pcm;   /* mono/stereo interleaved, 16-bit */
    int loop;
} Sound;

typedef struct Text {
    char *text;         /* \r-regels */
    int font_size, style, align, fore, back;
    char font[64];
    int w, h, left, top;
    int dirty;
} Text;

struct Score;
typedef struct Video Video;
typedef struct Member {
    int type;
    char *name;
    const uint8_t *spec;
    uint32_t speclen;
    int cast_chunk;
    int script_type;    /* voor MT_SCRIPT: 1 score, 3 movie, 7 parent */
    struct Script *script;
    Bitmap *bmp;        /* lui gedecodeerd */
    Sound *snd;
    Text *txt;
    uint8_t pal[256][3];
    int has_pal;
    struct Score *loop; /* filmloop */
    int shape_type, shape_fore, shape_back, shape_filled, shape_line;
    int rect_l, rect_t, rect_r, rect_b;
    int purge;
    char *file;         /* gekoppeld bestand (digitalVideo), zonder map */
    struct Video *video;
    int video_failed;
} Member;

typedef struct CastLib {
    char name[64];
    DFile *f;
    int first, n;       /* nummers first .. first+n-1 */
    Member *m;          /* index nummer-first */
    struct Script **scripts;
    int nscripts;
    int lib;            /* libnummer in de film */
    int owner;          /* KEY-eigenaar van de cast- en Lctx-chunks (1024 + index) */
} CastLib;

CastLib *cast_load(DFile *f, int first, int lib, int owner);
Member *cast_member(CastLib *c, int num);
Bitmap *member_bitmap(CastLib *c, Member *m);
Sound *member_sound(CastLib *c, Member *m);
Text *member_text(CastLib *c, Member *m);
int member_palette(CastLib *c, Member *m);

/* ------------------------------------------------------------------ score */
typedef struct SprRec {
    uint8_t type, ink, fore, back;
    uint16_t lib, member, slib, smember;
    int16_t locv, loch, h, w;
    uint8_t trails, stretch, blend;
} SprRec;

typedef struct Frame {
    uint16_t script_lib, script;
    uint16_t snd1_lib, snd1, snd2_lib, snd2, trans_lib, trans;
    uint8_t tempo, pal_speed, pal_flags;
    int16_t pal_lib, pal;
    SprRec spr[48];
} Frame;

typedef struct Score {
    int nframes, nchan;
    Frame *f;
} Score;

typedef struct Label { char name[64]; int frame; } Label;

/* ------------------------------------------------------------------ Lingo */
typedef struct Handler {
    int name;
    int nargs, nlocals, nglobals;
    int *args, *locals, *globals;
    const uint8_t *code;
    int clen;
} Handler;

typedef struct Script {
    int type;           /* 1 score, 3 movie, 7 parent */
    int member;         /* nummer binnen zijn cast */
    CastLib *lib;
    int nh;
    Handler *h;
    int nprops;
    int *props;
    int nglobals;
    int *globals;
    int nlits;
    Datum *lits;
    int nnames;
    int *names;         /* Lnam-index -> symbool */
} Script;

void scripts_load(CastLib *c);
Handler *script_handler(Script *s, int name);

/* VM */
typedef Datum (*Builtin)(Datum *args, int n);
Datum vm_call(Script *s, Handler *h, Datum *args, int n);
Datum vm_call_name(int name, Datum *args, int n, int *found);  /* movie-handlers, dan builtins */
Handler *obj_handler(Obj *o, int name, Script **sc);
Datum obj_getprop(Obj *o, int name, int *found);
int obj_setprop(Obj *o, int name, Datum v);
Datum obj_new(Script *s, Datum *args, int n);   /* birth/new */
void vm_register(const char *name, Builtin fn);
Builtin vm_builtin(int name);
Datum *global_ref(int name);
Datum *global_find(int name);   /* NULL als de global (nog) niet bestaat */
void vm_error(const char *fmt, ...);
extern int vm_trace;
extern int vm_pass;         /* pass aangeroepen */
extern int vm_dontpass;
extern int vm_abort;        /* abort / halt */
extern Script *vm_cur_script;
extern Datum vm_cur_me;

/* ------------------------------------------------------------------ speler */
typedef struct Movie {
    char name[64];
    char path[260];
    DFile *f;
    CastLib *libs[8];
    int nlibs;
    Score score;
    Label *labels;
    int nlabels;
    int stage_w, stage_h, stage_color, tempo;
    int def_pal_lib, def_pal;
} Movie;

Movie *movie_load(const char *path);
void movie_free(Movie *m);
Member *movie_member(Movie *mv, int lib, int num, CastLib **out);
int movie_find_member(Movie *mv, const char *name, int lib);  /* -> lib<<16|num of 0 */
int movie_label(Movie *mv, const char *name);

typedef struct Channel {
    int puppet;
    int lib, member;
    int ink, fore, back;
    int loch, locv, w, h;
    int stretch, visible, trails, blend;
    int moveable;
    Datum cursor;
    int slib, script;   /* scorescript */
    int type;
    int killed;         /* `set the type of sprite to 0`: niet tekenen, niet klikbaar */
    int movie_rate;     /* digitale video: 0 = stil */
    uint32_t movie_t0;  /* ms waarop movie_time0 gold */
    int movie_time0;    /* ticks */
} Channel;

#define NCHAN 48
typedef struct Player {
    Movie *mv;
    int frame, next_frame;
    int going;               /* go aangeroepen tijdens dit frame */
    char pending_movie[260];
    int pending_frame;
    char pending_label[64];
    Channel ch[NCHAN + 1];
    Datum actor_list;
    int tempo;
    uint32_t start_ms;
    uint32_t timer_base;
    int mouse_x, mouse_y, mouse_down, click_on, last_click, last_roll;
    int key_code; char key[8];
    int sound_level;
    int exit_lock;
    Datum mouse_down_script, mouse_up_script, key_down_script, key_up_script;
    uint8_t pal[256][3];      /* huidig palet */
    uint8_t pal_target[256][3];
    int pal_fade_steps, pal_fade_left;
    uint8_t pal_from[256][3];
    int pal_lib, pal_num;
    int halted;
    int update_needed;
    struct Window *windows;
    int stage_color;
    char base_dir[260];
    char save_dir[260];
    char bin_path[260];
    int trans_pending, trans_type, trans_dur, trans_chunk;
    int cursor;
    int paused;               /* pause/continue: speelkop van de stage staat stil */
    int release_pending;      /* headless klik: knop geldt al als losgelaten */
} Player;
extern Player P;
extern Player *CP;

typedef struct Window {
    char name[64];
    char file[260];
    int l, t, r, b;
    int visible, open, modal, type;
    Player *ctx;
    struct Window *next;
} Window;

void player_init(const char *base_dir);
void player_start(const char *movie);
int player_tick(void);          /* 1 frame; geeft ms tot volgende frame */
void player_mouse(int x, int y, int down_event, int up_event, int right);
/* video.c: AVI met Cinepak */
Video *video_open(const char *path);
void video_free(Video *v);
int video_width(Video *v);
int video_height(Video *v);
int video_frames(Video *v);
int video_duration(Video *v);                 /* ticks */
int video_frame_at(Video *v, int ticks);
const uint32_t *video_frame(Video *v, int n);  /* 0xAARRGGBB, w*h */
Sound *video_audio(Video *v);
Video *member_video(Member *m);   /* laadt het gekoppelde AVI-bestand lui */
void bmp_write(const char *path, const uint32_t *px, int w, int h);
uint32_t *stage_bitmap_argb(Bitmap *bm);
void trans_frame(uint32_t *out, const uint32_t *from, const uint32_t *to, int type, int chunk, double t);
int xobj_print_cmd(const char *cmd, Datum *a, int n);   /* 1 als a[0] een PrintOMatic-document is */
void host_print(const uint32_t *px, int w, int h, int landscape, const char *name);
int player_drag_update(void);   /* moveableSprite volgt de muis; 1 als hij verschoof */
int player_focus_field(void);   /* kanaal van het editable veld met toetsenbordfocus in CP, of 0 */
void player_key(int code, int ch, int down);
void player_update_stage(void);
Datum player_the(int name);
void builtins_register(void);

/* stage */
extern uint32_t *stage_px;     /* 640x480 BGRA */
void stage_compose(void);
int sprite_hit(int ch, int x, int y);
void sprite_rect(int ch, int *l, int *t, int *r, int *b);
void stage_present(void);      /* main.c */
void stage_screenshot(const char *path);

/* sound */
void sound_init(void);
void sound_play_member(int ch, CastLib *c, Member *m);
void sound_play_sound(int ch, Sound *s);
Sound *sound_load_wav(const char *path);
void sound_stop(int ch);
int sound_busy(int ch);
void sound_set_level(int lvl);
void sound_video_play(Sound *s, double offset);
void sound_video_stop(void);
const uint32_t *chan_video_frame(Channel *c, int *w, int *h);   /* huidig videoframe van een kanaal */
void cd_play_track(int track);
void cd_stop(void);
int cd_playing(void);

/* xobjects */
void xobj_register(void);
Datum xobj_call(XObj *x, Datum *args, int n);
Datum xobj_factory(const char *name);

/* platform */
uint32_t now_ms(void);
void host_alert(const char *msg);
void host_pump(void);          /* Windows-berichten verwerken zonder events te dispatchen */
char *path_resolve(const char *p, char *out, int n);
