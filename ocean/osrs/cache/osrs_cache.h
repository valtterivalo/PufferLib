#ifndef OSRS_CACHE_H
#define OSRS_CACHE_H

#include <assert.h>
#include <bzlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

enum {
    CACHE_INDEX_ANIMS = 0,
    CACHE_INDEX_BASES = 1,
    CACHE_INDEX_CONFIGS = 2,
    CACHE_INDEX_MAPS = 5,
    CACHE_INDEX_MODELS = 7,
    CACHE_INDEX_SPRITES = 8,
    CACHE_INDEX_TEXTURES = 9,
    CACHE_INDEX_FONTS = 13,
    CACHE_INDEX_REFERENCE = 255,
};

enum {
    CACHE_CONFIG_UNDERLAY = 1,
    CACHE_CONFIG_IDK = 3,
    CACHE_CONFIG_OVERLAY = 4,
    CACHE_CONFIG_LOC = 6,
    CACHE_CONFIG_NPC = 9,
    CACHE_CONFIG_OBJ = 10,
    CACHE_CONFIG_SEQ = 12,
    CACHE_CONFIG_SPOTANIM = 13,
};

typedef struct {
    const uint8_t* p;
    const uint8_t* end;
} CacheBuf;

static inline int cache_buf_left(const CacheBuf* b) { return (int)(b->end - b->p); }

static inline uint8_t cache_u8(CacheBuf* b) {
    assert(b->p < b->end);
    return *b->p++;
}

static inline int8_t cache_i8(CacheBuf* b) { return (int8_t)cache_u8(b); }

static inline uint16_t cache_u16(CacheBuf* b) {
    assert(b->end - b->p >= 2);
    uint16_t v = (uint16_t)(b->p[0] << 8 | b->p[1]);
    b->p += 2;
    return v;
}

static inline int16_t cache_i16(CacheBuf* b) { return (int16_t)cache_u16(b); }

static inline uint32_t cache_u24(CacheBuf* b) {
    assert(b->end - b->p >= 3);
    uint32_t v = (uint32_t)b->p[0] << 16 | (uint32_t)b->p[1] << 8 | b->p[2];
    b->p += 3;
    return v;
}

static inline uint32_t cache_u32(CacheBuf* b) {
    assert(b->end - b->p >= 4);
    uint32_t v = (uint32_t)b->p[0] << 24 | (uint32_t)b->p[1] << 16 | (uint32_t)b->p[2] << 8 | b->p[3];
    b->p += 4;
    return v;
}

static inline int cache_smart(CacheBuf* b) {
    assert(b->p < b->end);
    return b->p[0] < 128 ? cache_u8(b) : cache_u16(b) - 32768;
}

static inline int cache_signed_smart(CacheBuf* b) {
    assert(b->p < b->end);
    return b->p[0] < 128 ? cache_u8(b) - 64 : cache_u16(b) - 49152;
}

static inline int cache_extended_smart(CacheBuf* b) {
    int total = 0;
    int value = cache_smart(b);
    for (; value == 32767; value = cache_smart(b)) total += 32767;
    return total + value;
}

static inline int cache_big_smart(CacheBuf* b) {
    assert(b->p < b->end);
    return b->p[0] < 128 ? cache_u16(b) : (int)(cache_u32(b) & 0x7FFFFFFF);
}

static inline void cache_skip(CacheBuf* b, int n) {
    assert(b->end - b->p >= n);
    b->p += n;
}

static inline void cache_skip_string(CacheBuf* b) {
    while (cache_u8(b) != 0) {}
}

typedef struct {
    uint8_t* data;
    int size;
} CacheFile;

typedef struct {
    int id;
    int name_hash;
    int file_count;
    int* file_ids;
} CacheGroupRef;

typedef struct {
    int group_count;
    int has_names;
    CacheGroupRef* groups;
    int* slot_by_id;
    int id_limit;
} CacheIndex;

typedef struct {
    int file_count;
    int* file_ids;
    CacheFile* files;
} CacheGroup;

typedef struct {
    const uint8_t* dat;
    size_t dat_size;
    const uint8_t* idx[256];
    size_t idx_size[256];
    CacheIndex* indexes[256];
} OsrsCache;

static const uint8_t* cache_map_file(const char* path, size_t* size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    assert(fstat(fd, &st) == 0);
    *size = (size_t)st.st_size;
    const uint8_t* data = *size ? mmap(NULL, *size, PROT_READ, MAP_PRIVATE, fd, 0) : NULL;
    assert(!*size || data != MAP_FAILED);
    close(fd);
    return data;
}

static OsrsCache* osrs_cache_open(const char* dir) {
    OsrsCache* cache = calloc(1, sizeof(OsrsCache));
    char path[4096];
    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", dir);
    cache->dat = cache_map_file(path, &cache->dat_size);
    if (!cache->dat) {
        fprintf(stderr, "osrs_cache_open: no main_file_cache.dat2 in %s\n", dir);
        exit(1);
    }
    for (int i = 0; i < 256; i++) {
        snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", dir, i);
        cache->idx[i] = cache_map_file(path, &cache->idx_size[i]);
    }
    assert(cache->idx[CACHE_INDEX_REFERENCE]);
    return cache;
}

static CacheFile cache_read_raw(const OsrsCache* cache, int index, int group) {
    CacheFile out = {0};
    if (!cache->idx[index] || (size_t)group * 6 + 6 > cache->idx_size[index]) return out;
    const uint8_t* entry = cache->idx[index] + (size_t)group * 6;
    int size = entry[0] << 16 | entry[1] << 8 | entry[2];
    int sector = entry[3] << 16 | entry[4] << 8 | entry[5];
    if (size <= 0 || sector <= 0) return out;
    int header = group > 0xFFFF ? 10 : 8;
    out.data = malloc((size_t)size);
    out.size = size;
    for (int read = 0, chunk = 0; read < size; chunk++) {
        int n = size - read < 520 - header ? size - read : 520 - header;
        assert((size_t)sector * 520 + (size_t)(header + n) <= cache->dat_size);
        const uint8_t* s = cache->dat + (size_t)sector * 520;
        int owner = header == 10 ? (int)((uint32_t)s[0] << 24 | s[1] << 16 | s[2] << 8 | s[3]) : s[0] << 8 | s[1];
        const uint8_t* h = s + header - 6;
        int part = h[0] << 8 | h[1];
        int next = h[2] << 16 | h[3] << 8 | h[4];
        assert(owner == group && part == chunk && h[5] == index);
        memcpy(out.data + read, s + header, (size_t)n);
        read += n;
        sector = next;
    }
    return out;
}

static CacheFile cache_decompress(CacheFile raw) {
    CacheBuf b = {raw.data, raw.data + raw.size};
    int compression = cache_u8(&b);
    int length = (int)cache_u32(&b);
    CacheFile out = {0};
    if (compression == 0) {
        out.size = length;
        out.data = malloc((size_t)length);
        memcpy(out.data, b.p, (size_t)length);
        return out;
    }
    out.size = (int)cache_u32(&b);
    out.data = malloc((size_t)out.size);
    if (compression == 1) {
        uint8_t* framed = malloc((size_t)length + 4);
        memcpy(framed, "BZh1", 4);
        memcpy(framed + 4, b.p, (size_t)length);
        unsigned int produced = (unsigned int)out.size;
        int rc = BZ2_bzBuffToBuffDecompress((char*)out.data, &produced, (char*)framed, (unsigned int)length + 4, 0, 0);
        assert(rc == BZ_OK && produced == (unsigned int)out.size);
        free(framed);
        return out;
    }
    assert(compression == 2);
    z_stream z = {0};
    z.next_in = (Bytef*)b.p;
    z.avail_in = (uInt)length;
    z.next_out = out.data;
    z.avail_out = (uInt)out.size;
    assert(inflateInit2(&z, 16 + MAX_WBITS) == Z_OK);
    int rc = inflate(&z, Z_FINISH);
    assert(rc == Z_STREAM_END && z.total_out == (uLong)out.size);
    inflateEnd(&z);
    return out;
}

static CacheFile cache_read_container(const OsrsCache* cache, int index, int group) {
    CacheFile raw = cache_read_raw(cache, index, group);
    if (!raw.data) return raw;
    CacheFile out = cache_decompress(raw);
    free(raw.data);
    return out;
}

static CacheIndex* cache_index(OsrsCache* cache, int index) {
    if (cache->indexes[index]) return cache->indexes[index];
    CacheFile table = cache_read_container(cache, CACHE_INDEX_REFERENCE, index);
    assert(table.data);
    CacheBuf b = {table.data, table.data + table.size};
    int protocol = cache_u8(&b);
    assert(protocol >= 5 && protocol <= 7);
    if (protocol >= 6) cache_u32(&b);
    int flags = cache_u8(&b);
    CacheIndex* ix = calloc(1, sizeof(CacheIndex));
    ix->has_names = flags & 1;
    ix->group_count = protocol >= 7 ? cache_big_smart(&b) : cache_u16(&b);
    ix->groups = calloc((size_t)ix->group_count, sizeof(CacheGroupRef));
    int id = 0;
    for (int i = 0; i < ix->group_count; i++) {
        id += protocol >= 7 ? cache_big_smart(&b) : cache_u16(&b);
        ix->groups[i].id = id;
    }
    ix->id_limit = id + 1;
    ix->slot_by_id = malloc(sizeof(int) * (size_t)ix->id_limit);
    for (int i = 0; i < ix->id_limit; i++) ix->slot_by_id[i] = -1;
    for (int i = 0; i < ix->group_count; i++) ix->slot_by_id[ix->groups[i].id] = i;
    if (ix->has_names)
        for (int i = 0; i < ix->group_count; i++) ix->groups[i].name_hash = (int)cache_u32(&b);
    cache_skip(&b, 4 * ix->group_count);
    if (flags & 2) cache_skip(&b, 64 * ix->group_count);
    if (flags & 4) cache_skip(&b, 8 * ix->group_count);
    if (flags & 8) cache_skip(&b, 4 * ix->group_count);
    cache_skip(&b, 4 * ix->group_count);
    for (int i = 0; i < ix->group_count; i++)
        ix->groups[i].file_count = protocol >= 7 ? cache_big_smart(&b) : cache_u16(&b);
    for (int i = 0; i < ix->group_count; i++) {
        CacheGroupRef* g = &ix->groups[i];
        g->file_ids = malloc(sizeof(int) * (size_t)g->file_count);
        int file = 0;
        for (int f = 0; f < g->file_count; f++) {
            file += protocol >= 7 ? cache_big_smart(&b) : cache_u16(&b);
            g->file_ids[f] = file;
        }
    }
    free(table.data);
    cache->indexes[index] = ix;
    return ix;
}

static const CacheGroupRef* cache_group_ref(OsrsCache* cache, int index, int group) {
    CacheIndex* ix = cache_index(cache, index);
    if (group < 0 || group >= ix->id_limit || ix->slot_by_id[group] < 0) return NULL;
    return &ix->groups[ix->slot_by_id[group]];
}

static int cache_name_hash(const char* name) {
    uint32_t h = 0;
    for (const char* c = name; *c; c++) h = h * 31 + (uint32_t)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
    return (int)h;
}

static int cache_find_group(OsrsCache* cache, int index, const char* name) {
    CacheIndex* ix = cache_index(cache, index);
    assert(ix->has_names);
    int hash = cache_name_hash(name);
    for (int i = 0; i < ix->group_count; i++)
        if (ix->groups[i].name_hash == hash) return ix->groups[i].id;
    return -1;
}

static CacheGroup cache_read_group(OsrsCache* cache, int index, int group) {
    CacheGroup out = {0};
    const CacheGroupRef* ref = cache_group_ref(cache, index, group);
    if (!ref) return out;
    CacheFile data = cache_read_container(cache, index, group);
    assert(data.data);
    out.file_count = ref->file_count;
    out.file_ids = ref->file_ids;
    out.files = calloc((size_t)ref->file_count, sizeof(CacheFile));
    if (ref->file_count == 1) {
        out.files[0] = data;
        return out;
    }
    int chunks = data.data[data.size - 1];
    const uint8_t* table = data.data + data.size - 1 - 4 * chunks * ref->file_count;
    assert(chunks > 0 && table >= data.data);
    CacheBuf sizes = {table, data.data + data.size - 1};
    int* chunk_sizes = malloc(sizeof(int) * (size_t)(chunks * ref->file_count));
    for (int c = 0; c < chunks; c++) {
        int running = 0;
        for (int f = 0; f < ref->file_count; f++) {
            running += (int)cache_u32(&sizes);
            chunk_sizes[c * ref->file_count + f] = running;
            out.files[f].size += running;
        }
    }
    for (int f = 0; f < ref->file_count; f++) {
        out.files[f].data = malloc((size_t)out.files[f].size + 1);
        out.files[f].size = 0;
    }
    const uint8_t* p = data.data;
    for (int c = 0; c < chunks; c++)
        for (int f = 0; f < ref->file_count; f++) {
            int n = chunk_sizes[c * ref->file_count + f];
            memcpy(out.files[f].data + out.files[f].size, p, (size_t)n);
            out.files[f].size += n;
            p += n;
        }
    assert(p == table);
    free(chunk_sizes);
    free(data.data);
    return out;
}

static const CacheFile* cache_group_file(const CacheGroup* group, int file_id) {
    for (int i = 0; i < group->file_count; i++)
        if (group->file_ids[i] == file_id) return &group->files[i];
    return NULL;
}

static void cache_group_free(CacheGroup* group) {
    for (int i = 0; i < group->file_count; i++) free(group->files[i].data);
    free(group->files);
    *group = (CacheGroup){0};
}

static CacheFile cache_read_file(OsrsCache* cache, int index, int group, int file_id) {
    CacheGroup g = cache_read_group(cache, index, group);
    CacheFile out = {0};
    for (int i = 0; i < g.file_count; i++) {
        if (g.file_ids[i] == file_id) {
            out = g.files[i];
            g.files[i].data = NULL;
        }
    }
    cache_group_free(&g);
    return out;
}

#endif
