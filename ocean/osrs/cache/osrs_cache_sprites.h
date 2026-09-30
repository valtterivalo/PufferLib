#ifndef OSRS_CACHE_SPRITES_H
#define OSRS_CACHE_SPRITES_H

#include "osrs_cache.h"
#include "osrs_cache_model.h"

enum { SPRITE_FLAG_VERTICAL = 1, SPRITE_FLAG_ALPHA = 2 };

typedef enum { SPRITE_ALPHA_CHANNEL, SPRITE_ALPHA_INDEX } SpriteAlpha;

typedef struct {
    int width, height;
    uint32_t* pixels;
} SpriteFrame;

typedef struct {
    int count;
    SpriteFrame* frames;
} SpriteGroup;

static SpriteGroup sprite_group_decode(const uint8_t* data, int size, double brightness, SpriteAlpha alpha_mode) {
    assert(size >= 9);
    CacheBuf tail = {data + size - 2, data + size};
    int count = cache_u16(&tail);
    assert(count > 0);
    int meta_pos = size - 7 - count * 8;
    assert(meta_pos >= 0);
    CacheBuf meta = {data + meta_pos, data + size};
    int max_width = cache_u16(&meta), max_height = cache_u16(&meta);
    int palette_size = cache_u8(&meta) + 1;
    int* x_off = malloc(sizeof(int) * (size_t)count);
    int* y_off = malloc(sizeof(int) * (size_t)count);
    int* sub_w = malloc(sizeof(int) * (size_t)count);
    int* sub_h = malloc(sizeof(int) * (size_t)count);
    for (int i = 0; i < count; i++) x_off[i] = cache_u16(&meta);
    for (int i = 0; i < count; i++) y_off[i] = cache_u16(&meta);
    for (int i = 0; i < count; i++) sub_w[i] = cache_u16(&meta);
    for (int i = 0; i < count; i++) sub_h[i] = cache_u16(&meta);

    int palette_start = meta_pos - (palette_size - 1) * 3;
    assert(palette_start >= 0);
    CacheBuf pal = {data + palette_start, data + size};
    uint32_t* palette = calloc((size_t)palette_size, sizeof(uint32_t));
    for (int i = 1; i < palette_size; i++) {
        uint32_t rgb = cache_u24(&pal);
        palette[i] = (uint32_t)palette_brighten((int)(rgb ? rgb : 1), brightness);
    }

    CacheBuf px = {data, data + size};
    SpriteGroup g = {.count = count, .frames = calloc((size_t)count, sizeof(SpriteFrame))};
    for (int i = 0; i < count; i++) {
        int w = sub_w[i], h = sub_h[i];
        int canvas_w = max_width > w + x_off[i] ? max_width : w + x_off[i];
        int canvas_h = max_height > h + y_off[i] ? max_height : h + y_off[i];
        if (canvas_w < 1) canvas_w = 1;
        if (canvas_h < 1) canvas_h = 1;
        SpriteFrame* frame = &g.frames[i];
        frame->width = canvas_w, frame->height = canvas_h;
        frame->pixels = calloc((size_t)canvas_w * (size_t)canvas_h, sizeof(uint32_t));
        if (w <= 0 || h <= 0) continue;
        int dim = w * h;
        uint8_t* index = malloc((size_t)dim);
        uint8_t* alpha = malloc((size_t)dim);
        int flags = cache_u8(&px);
        if (flags & SPRITE_FLAG_VERTICAL) {
            for (int x = 0; x < w; x++)
                for (int y = 0; y < h; y++) index[y * w + x] = cache_u8(&px);
        } else {
            for (int j = 0; j < dim; j++) index[j] = cache_u8(&px);
        }
        if (flags & SPRITE_FLAG_ALPHA) {
            if (flags & SPRITE_FLAG_VERTICAL) {
                for (int x = 0; x < w; x++)
                    for (int y = 0; y < h; y++) alpha[y * w + x] = cache_u8(&px);
            } else {
                for (int j = 0; j < dim; j++) alpha[j] = cache_u8(&px);
            }
        }
        if (!(flags & SPRITE_FLAG_ALPHA) || alpha_mode == SPRITE_ALPHA_INDEX) memset(alpha, 0, (size_t)dim);
        for (int j = 0; j < dim; j++)
            if (index[j]) alpha[j] = 0xFF;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int src = y * w + x;
                int dst_x = x + x_off[i], dst_y = y + y_off[i];
                uint32_t rgb = palette[index[src]];
                uint32_t a = (uint32_t)alpha[src];
                frame->pixels[dst_y * canvas_w + dst_x] =
                    a << 24 | (rgb & 0xFFu) << 16 | (rgb & 0xFF00u) | (rgb >> 16 & 0xFFu);
            }
        free(index);
        free(alpha);
    }
    free(x_off);
    free(y_off);
    free(sub_w);
    free(sub_h);
    free(palette);
    return g;
}

static void sprite_group_free(SpriteGroup* g) {
    for (int i = 0; i < g->count; i++) free(g->frames[i].pixels);
    free(g->frames);
    *g = (SpriteGroup){0};
}

#endif
