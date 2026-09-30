#ifndef OSRS_CACHE_ITEM_H
#define OSRS_CACHE_ITEM_H

#include "osrs_cache_anim.h"

typedef struct {
    int inv_model;
    int male_model[3];
    int male_offset;
    int wearpos1, wearpos2, wearpos3;
    int recolor_count;
    uint16_t recolor_from[32], recolor_to[32];
    int retexture_count;
    uint16_t retexture_from[32], retexture_to[32];
    int ambient, contrast;
} ItemDef;

static ItemDef item_decode(const CacheFile* file) {
    ItemDef d = {.inv_model = -1, .male_model = {-1, -1, -1}, .wearpos1 = -1, .wearpos2 = -1, .wearpos3 = -1};
    CacheBuf b = {file->data, file->data + file->size};
    for (int op = cache_u8(&b); op; op = cache_u8(&b)) {
        if (op == 1) d.inv_model = cache_u16(&b);
        else if (op == 2 || op == 3 || op == 9 || (op >= 30 && op < 40)) cache_skip_string(&b);
        else if (op == 4 || op == 5 || op == 6 || op == 7 || op == 8) cache_u16(&b);
        else if (op == 11 || op == 15 || op == 16 || op == 65 || op == 160 || op == 251) {}
        else if (op == 12) cache_u32(&b);
        else if (op == 13) d.wearpos1 = cache_i8(&b);
        else if (op == 14) d.wearpos2 = cache_i8(&b);
        else if (op == 23) d.male_model[0] = cache_u16(&b), d.male_offset = cache_u8(&b);
        else if (op == 24) d.male_model[1] = cache_u16(&b);
        else if (op == 25) cache_u16(&b), cache_u8(&b);
        else if (op == 26) cache_u16(&b);
        else if (op == 27) d.wearpos3 = cache_i8(&b);
        else if (op == 40) cache_pairs(&b, &d.recolor_count, d.recolor_from, d.recolor_to);
        else if (op == 41) cache_pairs(&b, &d.retexture_count, d.retexture_from, d.retexture_to);
        else if (op == 42) cache_i8(&b);
        else if (op == 43) {
            cache_u8(&b);
            for (int sub = cache_u8(&b); sub; sub = cache_u8(&b)) cache_skip_string(&b);
        } else if (op == 44) d.inv_model = (int)cache_u32(&b);
        else if (op == 45) d.male_model[0] = (int)cache_u32(&b), d.male_offset = cache_u8(&b);
        else if (op == 46) d.male_model[1] = (int)cache_u32(&b);
        else if (op == 47) d.male_model[2] = (int)cache_u32(&b);
        else if (op == 48) cache_u32(&b), cache_u8(&b);
        else if (op == 49 || op == 50 || op == 51 || op == 52 || op == 53 || op == 54) cache_u32(&b);
        else if (op == 75) cache_i16(&b);
        else if (op == 78) d.male_model[2] = cache_u16(&b);
        else if (op == 79 || op == 90 || op == 91 || op == 92 || op == 93 || op == 94 || op == 95 || op == 97 ||
                 op == 98 || op == 99 || op == 139 || op == 140 || op == 148 || op == 149) cache_u16(&b);
        else if (op >= 100 && op < 110) cache_u16(&b), cache_u16(&b);
        else if (op == 110 || op == 111 || op == 112) cache_u16(&b);
        else if (op == 113) d.ambient = cache_i8(&b);
        else if (op == 114) d.contrast = cache_i8(&b);
        else if (op == 115) cache_u8(&b);
        else if (op == 161) cache_skip(&b, 2 * cache_u16(&b));
        else if (op == 200) {
            cache_skip(&b, 2);
            cache_skip_string(&b);
        } else if (op == 201) {
            cache_skip(&b, 5);
            cache_u32(&b);
            cache_u32(&b);
            cache_skip_string(&b);
        } else if (op == 202) {
            cache_skip(&b, 7);
            cache_u32(&b);
            cache_u32(&b);
            cache_skip_string(&b);
        } else if (op == 249) {
            for (int n = cache_u8(&b); n; n--) {
                int is_string = cache_u8(&b);
                cache_u24(&b);
                if (is_string) cache_skip_string(&b);
                else cache_u32(&b);
            }
        } else {
            fprintf(stderr, "item_decode: unknown opcode %d\n", op);
            abort();
        }
    }
    assert(b.p == b.end);
    return d;
}

typedef struct {
    int body_part;
    int model_count;
    int model_ids[32];
    int recolor_count;
    uint16_t recolor_from[32], recolor_to[32];
    int retexture_count;
    uint16_t retexture_from[32], retexture_to[32];
} IdentityKitDef;

static IdentityKitDef kit_decode(const CacheFile* file) {
    IdentityKitDef d = {.body_part = -1};
    CacheBuf b = {file->data, file->data + file->size};
    for (int op = cache_u8(&b); op; op = cache_u8(&b)) {
        if (op == 1) d.body_part = cache_u8(&b);
        else if (op == 2 || op == 5) {
            d.model_count = cache_u8(&b);
            assert(d.model_count <= 32);
            for (int i = 0; i < d.model_count; i++) d.model_ids[i] = op == 2 ? cache_u16(&b) : (int)cache_u32(&b);
        } else if (op == 3) {}
        else if (op == 40) cache_pairs(&b, &d.recolor_count, d.recolor_from, d.recolor_to);
        else if (op == 41) cache_pairs(&b, &d.retexture_count, d.retexture_from, d.retexture_to);
        else if (op >= 60 && op < 70) cache_u16(&b);
        else if (op >= 70 && op < 80) cache_u32(&b);
        else {
            fprintf(stderr, "kit_decode: unknown opcode %d\n", op);
            abort();
        }
    }
    assert(b.p == b.end);
    return d;
}

#endif
