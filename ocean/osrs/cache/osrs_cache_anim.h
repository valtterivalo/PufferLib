#ifndef OSRS_CACHE_ANIM_H
#define OSRS_CACHE_ANIM_H

#include "osrs_cache.h"

enum { CACHE_INDEX_MAYA = 22 };

typedef struct {
    int model_count;
    int model_ids[32];
    int recolor_count;
    uint16_t recolor_from[32], recolor_to[32];
    int retexture_count;
    uint16_t retexture_from[32], retexture_to[32];
    int size;
    int idle, walk, run, turn_180, turn_cw, turn_ccw;
    int width_scale, height_scale;
    int ambient, contrast;
} NpcDef;

static void cache_pairs(CacheBuf* b, int* count, uint16_t* from, uint16_t* to) {
    *count = cache_u8(b);
    assert(*count <= 32);
    for (int i = 0; i < *count; i++) from[i] = cache_u16(b), to[i] = cache_u16(b);
}

static int cache_anim_id(CacheBuf* b) {
    int v = cache_u16(b);
    return v == 0xFFFF ? -1 : v;
}

static NpcDef npc_decode(const CacheFile* file) {
    NpcDef d = {
        .size = 1, .idle = -1, .walk = -1, .run = -1, .turn_180 = -1, .turn_cw = -1, .turn_ccw = -1,
        .width_scale = 128, .height_scale = 128,
    };
    CacheBuf b = {file->data, file->data + file->size};
    for (int op = cache_u8(&b); op; op = cache_u8(&b)) {
        if (op == 1 || op == 61) {
            d.model_count = cache_u8(&b);
            assert(d.model_count <= 32);
            for (int i = 0; i < d.model_count; i++) d.model_ids[i] = op == 1 ? cache_u16(&b) : (int)cache_u32(&b);
        } else if (op == 2 || (op >= 30 && op < 35)) {
            cache_skip_string(&b);
        } else if (op == 60 || op == 62) {
            cache_skip(&b, cache_u8(&b) * (op == 62 ? 4 : 2));
        } else if (op == 12) {
            d.size = cache_u8(&b);
        } else if (op == 13) {
            d.idle = cache_anim_id(&b);
        } else if (op == 14) {
            d.walk = cache_anim_id(&b);
        } else if (op == 15) {
            d.turn_ccw = cache_anim_id(&b);
        } else if (op == 16) {
            d.turn_cw = cache_anim_id(&b);
        } else if (op == 17) {
            d.walk = cache_anim_id(&b), d.turn_180 = cache_anim_id(&b);
            cache_skip(&b, 4);
        } else if (op == 114) {
            d.run = cache_anim_id(&b);
        } else if (op == 115) {
            d.run = cache_anim_id(&b);
            cache_skip(&b, 6);
        } else if (op == 18 || op == 42 || (op >= 74 && op <= 79) || op == 95 || op == 103 || op == 116 || op == 124 ||
                   op == 126 || op == 146) {
            cache_u16(&b);
        } else if (op == 117) {
            cache_skip(&b, 8);
        } else if (op == 40) {
            cache_pairs(&b, &d.recolor_count, d.recolor_from, d.recolor_to);
        } else if (op == 41) {
            cache_pairs(&b, &d.retexture_count, d.retexture_from, d.retexture_to);
        } else if (op == 97) {
            d.width_scale = cache_u16(&b);
        } else if (op == 98) {
            d.height_scale = cache_u16(&b);
        } else if (op == 100) {
            d.ambient = cache_i8(&b);
        } else if (op == 101) {
            d.contrast = cache_i8(&b);
        } else if (op == 102) {
            int bits = cache_u8(&b);
            for (int i = 0; bits >> i; i++) {
                if (!(bits >> i & 1)) continue;
                cache_skip(&b, b.p[0] >= 128 ? 4 : 2);
                cache_smart(&b);
            }
        } else if (op == 106 || op == 118) {
            cache_skip(&b, op == 118 ? 6 : 4);
            cache_skip(&b, 2 * (cache_u8(&b) + 1));
        } else if (op == 249) {
            for (int n = cache_u8(&b); n; n--) {
                int is_string = cache_u8(&b);
                cache_u24(&b);
                if (is_string) cache_skip_string(&b);
                else cache_u32(&b);
            }
        } else if (op == 251) {
            cache_skip(&b, 2);
            cache_skip_string(&b);
        } else if (op == 252 || op == 253) {
            cache_skip(&b, op == 253 ? 15 : 13);
            cache_skip_string(&b);
        } else if (op != 93 && op != 99 && op != 107 && op != 109 && op != 111 && op != 122 && op != 123 && op != 129 &&
                   op != 130 && op != 145 && op != 147) {
            fprintf(stderr, "npc_decode: unknown opcode %d\n", op);
            abort();
        }
    }
    assert(b.p == b.end);
    return d;
}

typedef struct {
    int model_id, sequence;
    int width_scale, height_scale, rotation, ambient, contrast;
    int recolor_count;
    uint16_t recolor_from[32], recolor_to[32];
    int retexture_count;
    uint16_t retexture_from[32], retexture_to[32];
} SpotAnimDef;

static SpotAnimDef spotanim_decode(const CacheFile* file) {
    SpotAnimDef d = {.model_id = -1, .sequence = -1, .width_scale = 128, .height_scale = 128};
    CacheBuf b = {file->data, file->data + file->size};
    for (int op = cache_u8(&b); op; op = cache_u8(&b)) {
        if (op == 1) d.model_id = cache_u16(&b);
        else if (op == 2) d.sequence = cache_anim_id(&b);
        else if (op == 3) d.model_id = (int)cache_u32(&b);
        else if (op == 4) d.width_scale = cache_u16(&b);
        else if (op == 5) d.height_scale = cache_u16(&b);
        else if (op == 6) d.rotation = cache_u16(&b);
        else if (op == 7) d.ambient = cache_u8(&b);
        else if (op == 8) d.contrast = cache_u8(&b);
        else if (op == 40) cache_pairs(&b, &d.recolor_count, d.recolor_from, d.recolor_to);
        else if (op == 41) cache_pairs(&b, &d.retexture_count, d.retexture_from, d.retexture_to);
        else {
            fprintf(stderr, "spotanim_decode: unknown opcode %d\n", op);
            abort();
        }
    }
    assert(b.p == b.end);
    return d;
}

typedef struct {
    int frame_count;
    int* delays;
    int* frames;
    int interleave_count;
    uint8_t interleave[256];
    int walk_flag;
    int maya;
    int maya_start, maya_end;
} SeqDef;

static SeqDef seq_decode(const CacheFile* file) {
    SeqDef d = {.walk_flag = -1, .maya = -1};
    int has_mask = 0;
    CacheBuf b = {file->data, file->data + file->size};
    for (int op = cache_u8(&b); op; op = cache_u8(&b)) {
        if (op == 1) {
            d.frame_count = cache_u16(&b);
            d.delays = malloc(sizeof(int) * (size_t)d.frame_count);
            d.frames = malloc(sizeof(int) * (size_t)d.frame_count);
            for (int i = 0; i < d.frame_count; i++) d.delays[i] = cache_u16(&b);
            for (int i = 0; i < d.frame_count; i++) d.frames[i] = cache_u16(&b);
            for (int i = 0; i < d.frame_count; i++) d.frames[i] |= cache_u16(&b) << 16;
        } else if (op == 2 || op == 6 || op == 7) {
            cache_u16(&b);
        } else if (op == 3) {
            d.interleave_count = cache_u8(&b);
            for (int i = 0; i < d.interleave_count; i++) d.interleave[i] = cache_u8(&b);
        } else if (op == 5 || op == 8 || op == 9 || op == 11 || op == 16) {
            cache_u8(&b);
        } else if (op == 10) {
            d.walk_flag = cache_u8(&b);
        } else if (op == 12) {
            cache_skip(&b, cache_u8(&b) * 4);
        } else if (op == 13) {
            d.maya = (int)cache_u32(&b);
        } else if (op == 14) {
            cache_skip(&b, cache_u16(&b) * 8);
        } else if (op == 15) {
            d.maya_start = cache_u16(&b);
            d.maya_end = cache_u16(&b);
        } else if (op == 17) {
            has_mask = 1;
            cache_skip(&b, cache_u8(&b));
        } else if (op == 18) {
            cache_skip_string(&b);
        } else if (op != 4 && op != 19) {
            fprintf(stderr, "seq_decode: unknown opcode %d\n", op);
            abort();
        }
    }
    assert(b.p == b.end);
    if (d.walk_flag == -1) d.walk_flag = d.interleave_count || has_mask ? 2 : 0;
    return d;
}

typedef struct {
    int count;
    uint8_t types[256];
    uint8_t label_counts[256];
    uint8_t* labels[256];
} Skeleton;

static Skeleton skeleton_decode(const CacheFile* file) {
    Skeleton s = {0};
    CacheBuf b = {file->data, file->data + file->size};
    s.count = cache_u8(&b);
    for (int i = 0; i < s.count; i++) s.types[i] = cache_u8(&b);
    for (int i = 0; i < s.count; i++) s.label_counts[i] = cache_u8(&b);
    for (int i = 0; i < s.count; i++) {
        s.labels[i] = malloc((size_t)s.label_counts[i] + 1);
        for (int k = 0; k < s.label_counts[i]; k++) s.labels[i][k] = cache_u8(&b);
    }
    return s;
}

typedef struct {
    int skeleton;
    int count;
    uint8_t slot[256];
    int16_t dx[256], dy[256], dz[256];
} Frame;

static Frame frame_decode(const CacheFile* file, const Skeleton* s) {
    Frame f = {0};
    CacheBuf head = {file->data, file->data + file->size};
    f.skeleton = cache_u16(&head);
    int slots = cache_u8(&head);
    CacheBuf data = {head.p + slots, file->data + file->size};
    int last = -1;
    for (int i = 0; i < slots; i++) {
        int attr = cache_u8(&head);
        if (!attr) continue;
        if (s->types[i] != 0)
            for (int j = i - 1; j > last; j--)
                if (s->types[j] == 0) {
                    assert(f.count < 256);
                    f.slot[f.count] = (uint8_t)j, f.dx[f.count] = f.dy[f.count] = f.dz[f.count] = 0;
                    f.count++;
                    break;
                }
        assert(f.count < 256);
        int fallback = s->types[i] == 3 ? 128 : 0;
        f.slot[f.count] = (uint8_t)i;
        f.dx[f.count] = (int16_t)(attr & 1 ? cache_signed_smart(&data) : fallback);
        f.dy[f.count] = (int16_t)(attr & 2 ? cache_signed_smart(&data) : fallback);
        f.dz[f.count] = (int16_t)(attr & 4 ? cache_signed_smart(&data) : fallback);
        f.count++;
        last = i;
    }
    assert(data.p == data.end);
    return f;
}

#endif
