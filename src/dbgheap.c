/* Debug-heap (alleen met -DDBGHEAP): kop + staart-canary per blok, vrijgegeven blokken blijven in
 * quarantaine (gevuld met 0xDD) zodat overschrijven en dubbel vrijgeven met bestand:regel gemeld worden.
 * dbg_check() loopt alle blokken na; main.c roept hem elk frame aan. */
#ifdef DBGHEAP
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct Blk {
    uint32_t magic;
    uint32_t line;
    const char *file;
    size_t size;
    struct Blk *next;
    uint64_t pad;
} Blk;
#define LIVE 0xA11C0DE5u
#define DEAD 0xDEADF4EEu
#define TAIL 0x5A5A5A5A5A5A5A5Aull
static Blk *g_all;

static void fail(Blk *b, const char *what, const char *file, int line) {
    fprintf(stderr, "[heap] %s: blok %zu bytes uit %s:%u (gezien in %s:%d)\n", what, b->size, b->file, b->line, file, line);
    fflush(stderr);
    abort();
}

static void check(Blk *b, const char *file, int line) {
    uint64_t t;
    memcpy(&t, (char *)(b + 1) + b->size, 8);
    if (b->magic == LIVE && t != TAIL) fail(b, "staart overschreven", file, line);
    if (b->magic == DEAD) {
        const uint8_t *p = (const uint8_t *)(b + 1);
        for (size_t i = 0; i < b->size; i++)
            if (p[i] != 0xDD) fail(b, "geschreven na free", file, line);
    }
}

void *dbg_malloc(size_t n, const char *file, int line) {
    Blk *b = malloc(sizeof(Blk) + n + 8);
    b->magic = LIVE; b->file = file; b->line = line; b->size = n;
    uint64_t t = TAIL;
    memcpy((char *)(b + 1) + n, &t, 8);
    memset(b + 1, 0xCD, n);
    b->next = g_all;
    g_all = b;
    return b + 1;
}

void *dbg_calloc(size_t a, size_t n, const char *file, int line) {
    void *p = dbg_malloc(a * n, file, line);
    memset(p, 0, a * n);
    return p;
}

void dbg_free(void *p, const char *file, int line) {
    if (!p) return;
    Blk *b = (Blk *)p - 1;
    if (b->magic == DEAD) fail(b, "dubbel free", file, line);
    if (b->magic != LIVE) { fprintf(stderr, "[heap] free van onbekend blok %p in %s:%d\n", p, file, line); abort(); }
    check(b, file, line);
    b->magic = DEAD;
    memset(p, 0xDD, b->size);
}

void *dbg_realloc(void *p, size_t n, const char *file, int line) {
    if (!p) return dbg_malloc(n, file, line);
    Blk *b = (Blk *)p - 1;
    if (b->magic != LIVE) fail(b, "realloc van dood blok", file, line);
    void *q = dbg_malloc(n, file, line);
    memcpy(q, p, b->size < n ? b->size : n);
    dbg_free(p, file, line);
    return q;
}

char *dbg_strdup(const char *s, const char *file, int line) {
    size_t n = strlen(s) + 1;
    char *p = dbg_malloc(n, file, line);
    memcpy(p, s, n);
    return p;
}

void dbg_check(const char *file, int line) {
    for (Blk *b = g_all; b; b = b->next) check(b, file, line);
}
#else
void dbg_check_dummy(void) {}
#endif
