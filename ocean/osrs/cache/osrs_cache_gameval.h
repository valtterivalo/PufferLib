#ifndef OSRS_CACHE_GAMEVAL_H
#define OSRS_CACHE_GAMEVAL_H

#include "osrs_cache.h"

enum {
    CACHE_INDEX_GAMEVAL = 24,
    GAMEVAL_ITEMS = 0,
    GAMEVAL_NPCS = 1,
    GAMEVAL_OBJECTS = 6,
    GAMEVAL_ANIMATIONS = 7,
    GAMEVAL_SPOTANIMS = 8,
};

typedef struct {
    char** names;
    int limit;
} GamevalTable;

static char* gameval_read_name(CacheBuf* b) {
    int n = cache_buf_left(b);
    assert(n > 0);
    char* name = malloc((size_t)n + 1);
    for (int i = 0; i < n; i++) name[i] = (char)cache_u8(b);
    name[n] = 0;
    assert(b->p == b->end);
    return name;
}

static GamevalTable gameval_load(OsrsCache* cache, int archive) {
    assert(archive == GAMEVAL_ITEMS || archive == GAMEVAL_NPCS || archive == GAMEVAL_OBJECTS ||
        archive == GAMEVAL_ANIMATIONS || archive == GAMEVAL_SPOTANIMS);
    CacheGroup g = cache_read_group(cache, CACHE_INDEX_GAMEVAL, archive);
    int limit = 0;
    for (int i = 0; i < g.file_count; i++)
        if (g.file_ids[i] >= limit) limit = g.file_ids[i] + 1;
    GamevalTable t = {calloc((size_t)limit, sizeof(char*)), limit};
    for (int i = 0; i < g.file_count; i++) {
        CacheBuf b = {g.files[i].data, g.files[i].data + g.files[i].size};
        t.names[g.file_ids[i]] = gameval_read_name(&b);
    }
    cache_group_free(&g);
    return t;
}

static const char* gameval_name(const GamevalTable* t, int id) {
    assert(id >= 0 && id < t->limit && t->names[id]);
    return t->names[id];
}

static void gameval_ident(const char* name, char* out, size_t out_size) {
    size_t n = strlen(name);
    assert(n > 0 && n + 2 <= out_size);
    size_t o = 0;
    if (name[0] >= '0' && name[0] <= '9') out[o++] = '_';
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        out[o++] = (char)(ok ? (c >= 'a' && c <= 'z' ? c - 32 : c) : '_');
    }
    out[o] = 0;
}

#endif
