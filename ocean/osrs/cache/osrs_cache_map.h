#ifndef OSRS_CACHE_MAP_H
#define OSRS_CACHE_MAP_H

#include <math.h>

#include "osrs_cache.h"

enum { MAP_PLANES = 4, MAP_REGION = 64 };

typedef struct {
    int id;
    int x, y, plane;
    int type, rotation;
} MapLoc;

typedef struct {
    int base_x, base_y, width, height;
    int* heights;
    uint8_t* settings;
    uint16_t* underlays;
    uint16_t* overlays;
    uint8_t* shapes;
    uint8_t* rotations;
    uint8_t* present;
    MapLoc* locs;
    int loc_count, loc_capacity;
} MapGrid;

static inline int map_tile(const MapGrid* g, int plane, int x, int y) {
    return (plane * g->height + y) * g->width + x;
}

static inline int map_corner(const MapGrid* g, int plane, int x, int y) {
    return (plane * (g->height + 1) + y) * (g->width + 1) + x;
}

static inline int map_contains(const MapGrid* g, int x, int y) {
    return x >= 0 && y >= 0 && x < g->width && y < g->height;
}

static int map_noise(int x, int y) {
    int n = x + y * 57;
    n ^= (int)((uint32_t)n << 13);
    uint32_t v = (uint32_t)n * ((uint32_t)n * (uint32_t)n * 15731u + 789221u) + 1376312589u;
    return (int)((v & 0x7FFFFFFF) >> 19) & 0xFF;
}

static int map_smooth_noise(int x, int y) {
    int corners = map_noise(x - 1, y - 1) + map_noise(x + 1, y - 1) + map_noise(x - 1, y + 1) + map_noise(x + 1, y + 1);
    int sides = map_noise(x - 1, y) + map_noise(x + 1, y) + map_noise(x, y - 1) + map_noise(x, y + 1);
    return corners / 16 + sides / 8 + map_noise(x, y) / 4;
}

static int map_cosine[2048];

__attribute__((constructor)) static void map_build_cosine(void) {
    for (int i = 0; i < 2048; i++) map_cosine[i] = (int)(65536.0 * cos(i * 0.0030679615));
}

static int map_interpolate(int a, int b, int x, int frequency) {
    int f = (65536 - map_cosine[x * 1024 / frequency]) >> 1;
    return ((65536 - f) * a >> 16) + (b * f >> 16);
}

static int map_interpolated_noise(int x, int y, int frequency) {
    int ix = x / frequency, fx = x & (frequency - 1);
    int iy = y / frequency, fy = y & (frequency - 1);
    int a = map_interpolate(map_smooth_noise(ix, iy), map_smooth_noise(ix + 1, iy), fx, frequency);
    int b = map_interpolate(map_smooth_noise(ix, iy + 1), map_smooth_noise(ix + 1, iy + 1), fx, frequency);
    return map_interpolate(a, b, fy, frequency);
}

static int map_procedural_height(int world_x, int world_y) {
    int x = world_x + 932731, y = world_y + 556238;
    int n = map_interpolated_noise(x + 45365, y + 91923, 4) - 128
        + ((map_interpolated_noise(x + 10294, y + 37821, 2) - 128) >> 1)
        + ((map_interpolated_noise(x, y, 1) - 128) >> 2);
    n = (int)(n * 0.3) + 35;
    return n < 10 ? 10 : n > 60 ? 60 : n;
}

static void map_decode_terrain(MapGrid* g, CacheBuf b, int ox, int oy) {
    for (int plane = 0; plane < MAP_PLANES; plane++)
        for (int lx = 0; lx < MAP_REGION; lx++)
            for (int ly = 0; ly < MAP_REGION; ly++) {
                int x = ox + lx, y = oy + ly;
                int t = map_tile(g, plane, x, y);
                int* h = &g->heights[map_corner(g, plane, x, y)];
                int below = plane ? g->heights[map_corner(g, plane - 1, x, y)] : 0;
                g->present[t] = 1;
                for (;;) {
                    int op = cache_u16(&b);
                    if (op == 0) {
                        *h = plane ? below - 240 : -map_procedural_height(g->base_x + x, g->base_y + y) * 8;
                        break;
                    }
                    if (op == 1) {
                        int v = cache_u8(&b);
                        v = v == 1 ? 0 : v;
                        *h = plane ? below - v * 8 : -v * 8;
                        break;
                    }
                    if (op <= 49) {
                        g->overlays[t] = (uint16_t)cache_i16(&b) & 0x7FFF;
                        g->shapes[t] = (uint8_t)((op - 2) / 4);
                        g->rotations[t] = (uint8_t)((op - 2) & 3);
                    } else if (op <= 81) {
                        g->settings[t] = (uint8_t)(op - 49);
                    } else {
                        g->underlays[t] = (uint16_t)((op - 81) & 0x7FFF);
                    }
                }
            }
}

static void map_decode_locs(MapGrid* g, CacheBuf b, int ox, int oy) {
    int id = -1;
    for (int delta = cache_extended_smart(&b); delta; delta = cache_extended_smart(&b)) {
        id += delta;
        int packed = 0;
        for (int step = cache_smart(&b); step; step = cache_smart(&b)) {
            packed += step - 1;
            int info = cache_u8(&b);
            if (g->loc_count == g->loc_capacity) {
                g->loc_capacity = g->loc_capacity ? g->loc_capacity * 2 : 1024;
                g->locs = realloc(g->locs, sizeof(MapLoc) * (size_t)g->loc_capacity);
            }
            g->locs[g->loc_count++] = (MapLoc){
                .id = id, .x = ox + (packed >> 6 & 63), .y = oy + (packed & 63), .plane = packed >> 12 & 3,
                .type = info >> 2, .rotation = info & 3,
            };
        }
    }
}

static MapGrid map_load(OsrsCache* cache, int region_x0, int region_y0, int region_x1, int region_y1) {
    MapGrid g = {
        .base_x = region_x0 * MAP_REGION, .base_y = region_y0 * MAP_REGION,
        .width = (region_x1 - region_x0 + 1) * MAP_REGION, .height = (region_y1 - region_y0 + 1) * MAP_REGION,
    };
    size_t tiles = (size_t)MAP_PLANES * g.width * g.height;
    g.heights = calloc((size_t)MAP_PLANES * (g.width + 1) * (g.height + 1), sizeof(int));
    g.settings = calloc(tiles, 1);
    g.underlays = calloc(tiles, sizeof(uint16_t));
    g.overlays = calloc(tiles, sizeof(uint16_t));
    g.shapes = calloc(tiles, 1);
    g.rotations = calloc(tiles, 1);
    g.present = calloc(tiles, 1);
    for (int rx = region_x0; rx <= region_x1; rx++)
        for (int ry = region_y0; ry <= region_y1; ry++) {
            CacheGroup square = cache_read_group(cache, CACHE_INDEX_MAPS, rx << 8 | ry);
            if (!square.file_count) continue;
            int ox = (rx - region_x0) * MAP_REGION, oy = (ry - region_y0) * MAP_REGION;
            const CacheFile* terrain = cache_group_file(&square, 0);
            const CacheFile* locs = cache_group_file(&square, 1);
            assert(terrain && locs);
            map_decode_terrain(&g, (CacheBuf){terrain->data, terrain->data + terrain->size}, ox, oy);
            map_decode_locs(&g, (CacheBuf){locs->data, locs->data + locs->size}, ox, oy);
            cache_group_free(&square);
        }
    for (int plane = 0; plane < MAP_PLANES; plane++)
        for (int y = 0; y <= g.height; y++)
            for (int x = 0; x <= g.width; x++)
                if (x == g.width || y == g.height)
                    g.heights[map_corner(&g, plane, x, y)] =
                        g.heights[map_corner(&g, plane, x < g.width ? x : x - 1, y < g.height ? y : y - 1)];
    return g;
}

static void map_free(MapGrid* g) {
    free(g->heights); free(g->settings); free(g->underlays); free(g->overlays);
    free(g->shapes); free(g->rotations); free(g->present); free(g->locs);
    *g = (MapGrid){0};
}

typedef struct {
    int size_x, size_y;
    int interact_type;
    int blocks_projectile;
    int model_count;
    int model_ids[16];
    int model_types[16];
    int recolor_count;
    uint16_t recolor_from[32], recolor_to[32];
    int retexture_count;
    uint16_t retexture_from[32], retexture_to[32];
    int scale_x, scale_height, scale_y;
    int offset_x, offset_height, offset_y;
    int ambient, contrast;
    int non_flat_shading, mirrored, contoured_ground;
    int decor_offset;
    int animation_id;
    int animated;
    int transform;
    int clipped;
} LocDef;

static LocDef loc_decode(const CacheFile* file) {
    LocDef d = {
        .size_x = 1, .size_y = 1, .interact_type = 2, .blocks_projectile = 1,
        .scale_x = 128, .scale_height = 128, .scale_y = 128,
        .contoured_ground = -1, .decor_offset = 16, .animation_id = -1, .transform = -1, .clipped = 1,
    };
    CacheBuf b = {file->data, file->data + file->size};
    for (int op = cache_u8(&b); op; op = cache_u8(&b)) {
        if (op == 1 || op == 5 || op == 6 || op == 7) {
            int n = cache_u8(&b);
            int typed = op == 1 || op == 6, wide = op >= 6;
            if (d.model_count) {
                cache_skip(&b, n * ((wide ? 4 : 2) + typed));
                continue;
            }
            assert(n <= 16);
            d.model_count = n;
            for (int i = 0; i < n; i++) {
                d.model_ids[i] = wide ? (int)cache_u32(&b) : cache_u16(&b);
                d.model_types[i] = typed ? cache_u8(&b) : -1;
            }
        } else if (op == 2) {
            cache_skip_string(&b);
        } else if (op == 14) {
            d.size_x = cache_u8(&b);
        } else if (op == 15) {
            d.size_y = cache_u8(&b);
        } else if (op == 17) {
            d.interact_type = 0;
            d.blocks_projectile = 0;
        } else if (op == 18) {
            d.blocks_projectile = 0;
        } else if (op == 19 || op == 69 || op == 75) {
            cache_u8(&b);
        } else if (op == 21) {
            d.contoured_ground = 0;
        } else if (op == 22) {
            d.non_flat_shading = 1;
        } else if (op == 64) {
            d.clipped = 0;
        } else if (op == 23 || op == 73 || op == 74 || op == 89 || op == 90 || op == 94) {
        } else if (op == 24) {
            int anim = cache_u16(&b);
            d.animation_id = anim == 0xFFFF ? -1 : anim;
        } else if (op == 27) {
            d.interact_type = 1;
        } else if (op == 28) {
            d.decor_offset = cache_u8(&b);
        } else if (op == 29) {
            d.ambient = cache_i8(&b);
        } else if (op == 39) {
            d.contrast = cache_i8(&b) * 25;
        } else if (op >= 30 && op < 35) {
            cache_skip_string(&b);
        } else if (op == 40 || op == 41) {
            int n = cache_u8(&b);
            assert(n <= 32);
            uint16_t* from = op == 40 ? d.recolor_from : d.retexture_from;
            uint16_t* to = op == 40 ? d.recolor_to : d.retexture_to;
            for (int i = 0; i < n; i++) {
                from[i] = cache_u16(&b);
                to[i] = cache_u16(&b);
            }
            *(op == 40 ? &d.recolor_count : &d.retexture_count) = n;
        } else if (op == 60 || op == 61 || op == 68 || op == 82) {
            cache_u16(&b);
        } else if (op == 62) {
            d.mirrored = 1;
        } else if (op == 65) {
            d.scale_x = cache_u16(&b);
        } else if (op == 66) {
            d.scale_height = cache_u16(&b);
        } else if (op == 67) {
            d.scale_y = cache_u16(&b);
        } else if (op == 70) {
            d.offset_x = cache_i16(&b);
        } else if (op == 71) {
            d.offset_height = cache_i16(&b);
        } else if (op == 72) {
            d.offset_y = cache_i16(&b);
        } else if (op == 77 || op == 92) {
            d.animated = 1;
            cache_skip(&b, 4);
            if (op == 92) cache_skip(&b, 2);
            int n = cache_u8(&b);
            int first = cache_u16(&b);
            d.transform = first == 0xFFFF ? -1 : first;
            cache_skip(&b, 2 * n);
        } else if (op == 78) {
            cache_skip(&b, 4);
        } else if (op == 79) {
            cache_skip(&b, 6);
            cache_skip(&b, 2 * cache_u8(&b));
        } else if (op == 81) {
            d.contoured_ground = cache_u8(&b) * 256;
        } else if (op == 91 || op == 95 || op == 96) {
            cache_u8(&b);
        } else if (op == 93) {
            cache_skip(&b, 6);
        } else if (op == 100) {
            cache_skip(&b, 2);
            cache_skip_string(&b);
        } else if (op == 101 || op == 102) {
            cache_skip(&b, op == 102 ? 15 : 13);
            cache_skip_string(&b);
        } else if (op == 249) {
            for (int n = cache_u8(&b); n; n--) {
                int is_string = cache_u8(&b);
                cache_u24(&b);
                if (is_string) cache_skip_string(&b);
                else cache_u32(&b);
            }
        } else {
            fprintf(stderr, "loc_decode: unknown opcode %d\n", op);
            abort();
        }
    }
    assert(b.p == b.end);
    d.animated |= d.animation_id != -1;
    return d;
}

typedef struct {
    int hue, saturation, lightness, hue_multiplier;
} Underlay;

typedef struct {
    int rgb, texture, hide_underlay, secondary_rgb;
    int hue, saturation, lightness;
} Overlay;

typedef struct {
    double h, s, l;
} Hsl;

static Hsl floor_hsl(int rgb) {
    double r = (rgb >> 16 & 255) / 256.0, g = (rgb >> 8 & 255) / 256.0, b = (rgb & 255) / 256.0;
    double lo = r, hi = r;
    lo = g < lo ? g : lo, lo = b < lo ? b : lo;
    hi = g > hi ? g : hi, hi = b > hi ? b : hi;
    Hsl out = {0, 0, (lo + hi) / 2.0};
    if (lo != hi) {
        out.s = out.l < 0.5 ? (hi - lo) / (lo + hi) : (hi - lo) / (2.0 - hi - lo);
        out.h = hi == r ? (g - b) / (hi - lo) : hi == g ? (b - r) / (hi - lo) + 2.0 : (r - g) / (hi - lo) + 4.0;
    }
    out.h /= 6.0;
    return out;
}

static int floor_clamp_byte(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static Underlay underlay_decode(const CacheFile* file) {
    int rgb = 0;
    CacheBuf b = {file->data, file->data + file->size};
    for (int op = cache_u8(&b); op; op = cache_u8(&b)) {
        assert(op == 1);
        rgb = (int)cache_u24(&b);
    }
    assert(b.p == b.end);
    Hsl c = floor_hsl(rgb);
    Underlay u = {.saturation = floor_clamp_byte((int)(256.0 * c.s)), .lightness = floor_clamp_byte((int)(256.0 * c.l))};
    u.hue_multiplier = (int)((c.l > 0.5 ? 1.0 - c.l : c.l) * c.s * 512.0);
    u.hue_multiplier = u.hue_multiplier < 1 ? 1 : u.hue_multiplier;
    u.hue = (int)(u.hue_multiplier * c.h);
    return u;
}

static Overlay overlay_decode(const CacheFile* file) {
    Overlay o = {.texture = -1, .hide_underlay = 1, .secondary_rgb = -1};
    CacheBuf b = {file->data, file->data + file->size};
    for (int op = cache_u8(&b); op; op = cache_u8(&b)) {
        if (op == 1) o.rgb = (int)cache_u24(&b);
        else if (op == 2) o.texture = cache_u8(&b);
        else if (op == 5) o.hide_underlay = 0;
        else if (op == 7) o.secondary_rgb = (int)cache_u24(&b);
        else if (op == 9) cache_u8(&b);
        else assert(op == 8);
    }
    assert(b.p == b.end);
    Hsl c = floor_hsl(o.rgb);
    o.hue = (int)(256.0 * c.h);
    o.saturation = floor_clamp_byte((int)(c.s * 256.0));
    o.lightness = floor_clamp_byte((int)(256.0 * c.l));
    return o;
}

typedef struct {
    int present, sprite, average_hsl, direction, speed;
} TextureDef;

static TextureDef texture_decode(const CacheFile* file) {
    CacheBuf b = {file->data, file->data + file->size};
    TextureDef t = {.present = 1, .sprite = cache_u16(&b), .average_hsl = cache_u16(&b)};
    cache_u8(&b);
    t.direction = cache_u8(&b);
    t.speed = cache_u8(&b);
    assert(b.p == b.end);
    return t;
}

#endif
