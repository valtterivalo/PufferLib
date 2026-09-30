#include "ocean/osrs/cache/osrs_cache_anim.h"
#include "ocean/osrs/cache/osrs_cache_item.h"
#include "ocean/osrs/cache/osrs_cache_gameval.h"
#include "ocean/osrs/cache/osrs_cache_interfaces.h"
#include "ocean/osrs/cache/osrs_cache_map.h"
#include "ocean/osrs/cache/osrs_cache_maya.h"
#include "ocean/osrs/cache/osrs_cache_model.h"
#include "ocean/osrs/cache/osrs_cache_sprites.h"
#include "ocean/osrs/osrs_asset_formats.h"
#include "ocean/osrs/osrs_collision.h"
#include "ocean/osrs/osrs_combat_visuals.h"
#include "ocean/osrs/osrs_combat_visuals_generated.h"
#include "ocean/osrs/osrs_items.h"

typedef struct {
    int x0, y0, x1, y1;
} RegionRect;

typedef struct {
    OsrsCache* cache;
    CacheGroup loc_files;
    LocDef* locs;
    uint8_t* loc_decoded;
    Underlay* underlays;
    int underlay_count;
    Overlay* overlays;
    int overlay_count;
    TextureDef* textures;
    int texture_count;
    Model* models;
    uint8_t* model_decoded;
    int model_limit;
} Defs;

static void defs_load_floors(Defs* d) {
    CacheGroup u = cache_read_group(d->cache, CACHE_INDEX_CONFIGS, CACHE_CONFIG_UNDERLAY);
    CacheGroup o = cache_read_group(d->cache, CACHE_INDEX_CONFIGS, CACHE_CONFIG_OVERLAY);
    CacheGroup t = cache_read_group(d->cache, CACHE_INDEX_TEXTURES, 0);
    d->underlay_count = u.file_ids[u.file_count - 1] + 1;
    d->overlay_count = o.file_ids[o.file_count - 1] + 1;
    d->texture_count = t.file_ids[t.file_count - 1] + 1;
    d->underlays = calloc((size_t)d->underlay_count, sizeof(Underlay));
    d->overlays = calloc((size_t)d->overlay_count, sizeof(Overlay));
    d->textures = calloc((size_t)d->texture_count, sizeof(TextureDef));
    for (int i = 0; i < u.file_count; i++) d->underlays[u.file_ids[i]] = underlay_decode(&u.files[i]);
    for (int i = 0; i < o.file_count; i++) d->overlays[o.file_ids[i]] = overlay_decode(&o.files[i]);
    for (int i = 0; i < t.file_count; i++) d->textures[t.file_ids[i]] = texture_decode(&t.files[i]);
    cache_group_free(&u);
    cache_group_free(&o);
    cache_group_free(&t);
    d->model_limit = cache_index(d->cache, CACHE_INDEX_MODELS)->id_limit;
    d->models = calloc((size_t)d->model_limit, sizeof(Model));
    d->model_decoded = calloc((size_t)d->model_limit, 1);
}

static const Model* defs_model(Defs* d, int id) {
    assert(id < d->model_limit);
    if (!d->model_decoded[id]) {
        CacheFile f = cache_read_file(d->cache, CACHE_INDEX_MODELS, id, 0);
        assert(f.data);
        d->models[id] = model_decode(&f);
        d->model_decoded[id] = 1;
        free(f.data);
    }
    return &d->models[id];
}

static const LocDef* defs_loc(Defs* d, int id) {
    if (!d->locs) {
        d->loc_files = cache_read_group(d->cache, CACHE_INDEX_CONFIGS, CACHE_CONFIG_LOC);
        int limit = d->loc_files.file_ids[d->loc_files.file_count - 1] + 1;
        d->locs = calloc((size_t)limit, sizeof(LocDef));
        d->loc_decoded = calloc((size_t)limit, 1);
    }
    if (!d->loc_decoded[id]) {
        const CacheFile* file = cache_group_file(&d->loc_files, id);
        assert(file);
        d->locs[id] = loc_decode(file);
        d->loc_decoded[id] = 1;
    }
    return &d->locs[id];
}

static void flag(int* flags, const MapGrid* g, int plane, int x, int y, int bits) {
    if (map_contains(g, x, y)) flags[map_tile(g, plane, x, y)] |= bits;
}

static void flag_wall(int* flags, const MapGrid* g, int plane, int x, int y, int type, int rotation, int impenetrable) {
    static const int dx[4] = {-1, 0, 1, 0}, dy[4] = {0, 1, 0, -1};
    static const int side[4] = {COLLISION_WALL_WEST, COLLISION_WALL_NORTH, COLLISION_WALL_EAST, COLLISION_WALL_SOUTH};
    static const int corner[4] = {
        COLLISION_WALL_NORTH_WEST, COLLISION_WALL_NORTH_EAST, COLLISION_WALL_SOUTH_EAST, COLLISION_WALL_SOUTH_WEST,
    };
    static const int cx[4] = {-1, 1, 1, -1}, cy[4] = {1, 1, -1, -1};
    int r = rotation, opposite = (rotation + 2) & 3;
    for (int pass = 0; pass <= impenetrable; pass++) {
        int shift = pass * 9;
        if (type == 0 || type == 2) {
            flag(flags, g, plane, x, y, side[r] << shift);
            flag(flags, g, plane, x + dx[r], y + dy[r], side[opposite] << shift);
        }
        if (type == 2) {
            int n = (r + 1) & 3;
            flag(flags, g, plane, x, y, side[n] << shift);
            flag(flags, g, plane, x + dx[n], y + dy[n], side[(n + 2) & 3] << shift);
        }
        if (type == 1 || type == 3) {
            flag(flags, g, plane, x, y, corner[r] << shift);
            flag(flags, g, plane, x + cx[r], y + cy[r], corner[opposite] << shift);
        }
    }
}

static int* build_collision(Defs* defs, const MapGrid* g) {
    int* flags = calloc((size_t)MAP_PLANES * g->width * g->height, sizeof(int));
    for (int plane = 0; plane < MAP_PLANES; plane++)
        for (int y = 0; y < g->height; y++)
            for (int x = 0; x < g->width; x++) {
                if (!(g->settings[map_tile(g, plane, x, y)] & 1)) continue;
                int target = g->settings[map_tile(g, 1, x, y)] & 2 ? plane - 1 : plane;
                if (target >= 0) flags[map_tile(g, target, x, y)] |= COLLISION_BLOCKED;
            }
    for (int i = 0; i < g->loc_count; i++) {
        const MapLoc* loc = &g->locs[i];
        int plane = g->settings[map_tile(g, 1, loc->x, loc->y)] & 2 ? loc->plane - 1 : loc->plane;
        const LocDef* def = defs_loc(defs, loc->id);
        if (plane < 0 || !def->interact_type) continue;
        int swap = loc->rotation == 1 || loc->rotation == 3;
        int sx = swap ? def->size_y : def->size_x, sy = swap ? def->size_x : def->size_y;
        if (loc->type == 22) {
            if (def->interact_type == 1) flag(flags, g, plane, loc->x, loc->y, COLLISION_BLOCKED);
        } else if (loc->type == 9 || loc->type >= 10) {
            int bits = COLLISION_BLOCKED | (def->blocks_projectile ? COLLISION_IMPENETRABLE_BLOCKED : 0);
            for (int ox = 0; ox < sx; ox++)
                for (int oy = 0; oy < sy; oy++) flag(flags, g, plane, loc->x + ox, loc->y + oy, bits);
        } else if (loc->type <= 3) {
            flag_wall(flags, g, plane, loc->x, loc->y, loc->type, loc->rotation, def->blocks_projectile);
        }
    }
    return flags;
}

static void write_collision(const char* path, const MapGrid* g, const int* flags, RegionRect r) {
    FILE* f = fopen(path, "wb");
    assert(f);
    uint32_t header[3] = {COLLISION_MAP_MAGIC, COLLISION_MAP_VERSION, 0};
    for (int rx = r.x0; rx <= r.x1; rx++)
        for (int ry = r.y0; ry <= r.y1; ry++) header[2]++;
    fwrite(header, sizeof(header), 1, f);
    static CollisionRegion region;
    for (int rx = r.x0; rx <= r.x1; rx++)
        for (int ry = r.y0; ry <= r.y1; ry++) {
            int32_t key = rx << 8 | ry;
            int ox = rx * MAP_REGION - g->base_x, oy = ry * MAP_REGION - g->base_y;
            for (int plane = 0; plane < MAP_PLANES; plane++)
                for (int x = 0; x < MAP_REGION; x++)
                    for (int y = 0; y < MAP_REGION; y++)
                        region.flags[plane][x][y] = flags[map_tile(g, plane, ox + x, oy + y)];
            fwrite(&key, sizeof(key), 1, f);
            fwrite(&region, sizeof(region), 1, f);
        }
    fclose(f);
}

typedef struct {
    float* xyz;
    uint8_t* rgba;
    float* uv;
    int count, capacity;
} Soup;

static void soup_push(Soup* s, float x, float y, float z, int rgb, int alpha, float u, float v) {
    if (s->count == s->capacity) {
        s->capacity = s->capacity ? s->capacity * 2 : 1 << 16;
        s->xyz = realloc(s->xyz, sizeof(float) * 3 * (size_t)s->capacity);
        s->rgba = realloc(s->rgba, 4 * (size_t)s->capacity);
        s->uv = realloc(s->uv, sizeof(float) * 2 * (size_t)s->capacity);
    }
    int i = s->count++;
    s->xyz[i * 3] = x, s->xyz[i * 3 + 1] = y, s->xyz[i * 3 + 2] = z;
    s->rgba[i * 4] = (uint8_t)(rgb >> 16), s->rgba[i * 4 + 1] = (uint8_t)(rgb >> 8);
    s->rgba[i * 4 + 2] = (uint8_t)rgb, s->rgba[i * 4 + 3] = (uint8_t)alpha;
    s->uv[i * 2] = u, s->uv[i * 2 + 1] = v;
}

static int hsl_encode(int hue, int saturation, int lightness) {
    if (lightness > 179) saturation /= 2;
    if (lightness > 192) saturation /= 2;
    if (lightness > 217) saturation /= 2;
    if (lightness > 243) saturation /= 2;
    return saturation / 32 * 128 + hue / 4 * 1024 + lightness / 2;
}

static int tile_light_level(int light) { return light < 2 ? 2 : light > 126 ? 126 : light; }

static int tile_light_underlay(int hsl, int light) {
    if (hsl == -1) return MODEL_HIDDEN_COLOR;
    return (hsl & 0xFF80) + tile_light_level((hsl & 127) * light / 128);
}

static int tile_light_overlay(int hsl, int light) {
    if (hsl == -2) return MODEL_HIDDEN_COLOR;
    if (hsl == -1) return tile_light_level(light);
    return (hsl & 0xFF80) + tile_light_level((hsl & 127) * light / 128);
}

typedef struct {
    Defs* defs;
    const MapGrid* g;
    RegionRect r;
    int* light;
    uint8_t* shadow;
} Scene;

static int scene_corner(const MapGrid* g, int plane, int x, int y) { return map_corner(g, plane, x, y); }

static int scene_in_rect(const Scene* s, int x, int y, int margin) {
    int wx = s->g->base_x + x, wy = s->g->base_y + y;
    return wx >= s->r.x0 * MAP_REGION - margin && wy >= s->r.y0 * MAP_REGION - margin &&
        wx < (s->r.x1 + 1) * MAP_REGION + margin && wy < (s->r.y1 + 1) * MAP_REGION + margin;
}

static void scene_shadow(Scene* s, int plane, int x, int y, int value) {
    uint8_t* v = &s->shadow[scene_corner(s->g, plane, x, y)];
    if (value > *v) *v = (uint8_t)value;
}

static void scene_light(Scene* s) {
    const MapGrid* g = s->g;
    int distribution = (int)sqrt(5100.0) * 768 >> 8;
    for (int plane = 0; plane < MAP_PLANES; plane++)
        for (int y = 1; y < g->height; y++)
            for (int x = 1; x < g->width; x++) {
                const int* h = g->heights;
                int dx = h[scene_corner(g, plane, x + 1, y)] - h[scene_corner(g, plane, x - 1, y)];
                int dy = h[scene_corner(g, plane, x, y + 1)] - h[scene_corner(g, plane, x, y - 1)];
                int len = (int)sqrt((double)(dx * dx + dy * dy + 65536));
                int nx = dx * 256 / len, ny = 65536 / len, nz = dy * 256 / len;
                int light = (nz * -50 + nx * -50 + ny * -10) / distribution + 96;
                const uint8_t* sh = s->shadow;
                int shade = (sh[scene_corner(g, plane, x, y + 1)] >> 3) + (sh[scene_corner(g, plane, x - 1, y)] >> 2)
                    + (sh[scene_corner(g, plane, x, y - 1)] >> 2) + (sh[scene_corner(g, plane, x + 1, y)] >> 3)
                    + (sh[scene_corner(g, plane, x, y)] >> 1);
                s->light[scene_corner(g, plane, x, y)] = light - shade;
            }
}

static const int TILE_SHAPE_VERTICES[13][6] = {
    {1, 3, 5, 7}, {1, 3, 5, 7}, {1, 3, 5, 7}, {1, 3, 5, 7, 6}, {1, 3, 5, 7, 6}, {1, 3, 5, 7, 6},
    {1, 3, 5, 7, 6}, {1, 3, 5, 7, 2, 6}, {1, 3, 5, 7, 2, 8}, {1, 3, 5, 7, 2, 8}, {1, 3, 5, 7, 11, 12},
    {1, 3, 5, 7, 11, 12}, {1, 3, 5, 7, 13, 14},
};
static const int TILE_SHAPE_VERTEX_COUNT[13] = {4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6};
static const int TILE_SHAPE_FACES[13][24] = {
    {0, 1, 2, 3, 0, 0, 1, 3}, {1, 1, 2, 3, 1, 0, 1, 3}, {0, 1, 2, 3, 1, 0, 1, 3},
    {0, 0, 1, 2, 0, 0, 2, 4, 1, 0, 4, 3}, {0, 0, 1, 4, 0, 0, 4, 3, 1, 1, 2, 4},
    {0, 0, 4, 3, 1, 0, 1, 2, 1, 0, 2, 4}, {0, 1, 2, 4, 1, 0, 1, 4, 1, 0, 4, 3},
    {0, 4, 1, 2, 0, 4, 2, 5, 1, 0, 4, 5, 1, 0, 5, 3}, {0, 4, 1, 2, 0, 4, 2, 3, 0, 4, 3, 5, 1, 0, 4, 5},
    {0, 0, 4, 5, 1, 4, 1, 2, 1, 4, 2, 3, 1, 4, 3, 5},
    {0, 0, 1, 5, 0, 1, 4, 5, 0, 1, 2, 4, 1, 0, 5, 3, 1, 5, 4, 3, 1, 4, 2, 3},
    {1, 0, 1, 5, 1, 1, 4, 5, 1, 1, 2, 4, 0, 0, 5, 3, 0, 5, 4, 3, 0, 4, 2, 3},
    {1, 0, 5, 4, 1, 0, 1, 5, 0, 0, 4, 3, 0, 4, 5, 3, 0, 5, 2, 3, 0, 1, 2, 5},
};
static const int TILE_SHAPE_FACE_COUNT[13] = {2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 6, 6, 6};

typedef struct {
    int x, z, h, under, over;
} TileVertex;

static TileVertex tile_shape_vertex(int code, int rotation, const int h[4], const int u[4], const int o[4]) {
    if ((code & 1) == 0 && code <= 8) code = ((code - rotation - rotation - 1) & 7) + 1;
    if (code > 8 && code <= 12) code = ((code - 9 - rotation) & 3) + 9;
    if (code > 12 && code <= 16) code = ((code - 13 - rotation) & 3) + 13;
    static const int px[17] = {0, 0, 64, 128, 128, 128, 64, 0, 0, 64, 96, 64, 32, 32, 96, 96, 32};
    static const int pz[17] = {0, 0, 0, 0, 64, 128, 128, 128, 64, 32, 64, 96, 64, 32, 32, 96, 96};
    static const int ca[17] = {0, 0, 0, 1, 2, 2, 2, 3, 3, 0, 1, 2, 3, 0, 1, 2, 3};
    static const int cb[17] = {0, 0, 1, 1, 1, 2, 3, 3, 0, 1, 2, 3, 0, 0, 1, 2, 3};
    int a = ca[code], b = cb[code];
    return (TileVertex){px[code], pz[code], (h[a] + h[b]) >> 1, (u[a] + u[b]) >> 1, (o[a] + o[b]) >> 1};
}

static void scene_push_tile_vertex(Soup* out, const Scene* s, int x, int y, int dx, int dz, int height, int hsl) {
    float wx = (float)(s->g->base_x + x) + dx / 128.0f;
    float wy = (float)(s->g->base_y + y) + dz / 128.0f;
    soup_push(out, wx, -height / 128.0f, -wy, model_palette[hsl & 0xFFFF], 255, 0, 0);
}

static void scene_paint_tile(Soup* out, const Scene* s, int x, int y, const int h[4], const int c[4]) {
    static const int dx[4] = {0, 128, 128, 0}, dz[4] = {0, 0, 128, 128};
    static const int tris[2][3] = {{2, 3, 1}, {0, 1, 3}};
    for (int t = 0; t < 2; t++) {
        if (c[tris[t][0]] == MODEL_HIDDEN_COLOR) continue;
        for (int k = 0; k < 3; k++) {
            int v = tris[t][k];
            scene_push_tile_vertex(out, s, x, y, dx[v], dz[v], h[v], c[v]);
        }
    }
}

static void scene_shape_tile(Soup* out, const Scene* s, int x, int y, int shape, int rotation, const int h[4], const int u[4],
                             const int o[4]) {
    TileVertex v[6];
    for (int i = 0; i < TILE_SHAPE_VERTEX_COUNT[shape]; i++) v[i] = tile_shape_vertex(TILE_SHAPE_VERTICES[shape][i], rotation, h, u, o);
    for (int f = 0; f < TILE_SHAPE_FACE_COUNT[shape]; f++) {
        const int* face = &TILE_SHAPE_FACES[shape][f * 4];
        int idx[3];
        for (int k = 0; k < 3; k++) idx[k] = face[k + 1] < 4 ? (face[k + 1] - rotation) & 3 : face[k + 1];
        int overlay = face[0];
        if ((overlay ? v[idx[0]].over : v[idx[0]].under) == MODEL_HIDDEN_COLOR) continue;
        long cross = (long)(v[idx[1]].x - v[idx[0]].x) * (v[idx[2]].z - v[idx[0]].z)
            - (long)(v[idx[1]].z - v[idx[0]].z) * (v[idx[2]].x - v[idx[0]].x);
        if (cross < 0) {
            int t = idx[1];
            idx[1] = idx[2], idx[2] = t;
        }
        for (int k = 0; k < 3; k++) {
            TileVertex* p = &v[idx[k]];
            scene_push_tile_vertex(out, s, x, y, p->x, p->z, p->h, overlay ? p->over : p->under);
        }
    }
}

static void scene_terrain_plane(Soup* out, Scene* s, int plane, int linked_only) {
    const MapGrid* g = s->g;
    Defs* d = s->defs;
    size_t n = (size_t)(g->width + 1) * (g->height + 1);
    long* sums[5];
    for (int k = 0; k < 5; k++) sums[k] = calloc(n, sizeof(long));
    for (int y = 0; y < g->height; y++)
        for (int x = 0; x < g->width; x++) {
            int u = g->underlays[map_tile(g, plane, x, y)];
            long add[5] = {0};
            if (u > 0) {
                const Underlay* def = &d->underlays[u - 1];
                add[0] = def->hue, add[1] = def->saturation, add[2] = def->lightness, add[3] = def->hue_multiplier, add[4] = 1;
            }
            size_t i = (size_t)(y + 1) * (g->width + 1) + (x + 1);
            for (int k = 0; k < 5; k++)
                sums[k][i] = add[k] + sums[k][i - 1] + sums[k][i - (g->width + 1)] - sums[k][i - (g->width + 1) - 1];
        }
    for (int y = 1; y < g->height - 1; y++)
        for (int x = 1; x < g->width - 1; x++) {
            if (!scene_in_rect(s, x, y, 0)) continue;
            if (linked_only && !(g->settings[map_tile(g, 1, x, y)] & 2)) continue;
            int t = map_tile(g, plane, x, y);
            int under = g->underlays[t], over = g->overlays[t];
            if (!under && !over) continue;
            int x0 = x - 4 < 0 ? 0 : x - 4, x1 = x + 5 >= g->width ? g->width - 1 : x + 5;
            int y0 = y - 4 < 0 ? 0 : y - 4, y1 = y + 5 >= g->height ? g->height - 1 : y + 5;
            long w[5];
            for (int k = 0; k < 5; k++) {
                long* S = sums[k];
                int W = g->width + 1;
                w[k] = S[(size_t)(y1 + 1) * W + x1 + 1] - S[(size_t)y0 * W + x1 + 1] - S[(size_t)(y1 + 1) * W + x0] + S[(size_t)y0 * W + x0];
            }
            int h[4] = {
                g->heights[map_corner(g, plane, x, y)], g->heights[map_corner(g, plane, x + 1, y)],
                g->heights[map_corner(g, plane, x + 1, y + 1)], g->heights[map_corner(g, plane, x, y + 1)],
            };
            int l[4] = {
                s->light[map_corner(g, plane, x, y)], s->light[map_corner(g, plane, x + 1, y)],
                s->light[map_corner(g, plane, x + 1, y + 1)], s->light[map_corner(g, plane, x, y + 1)],
            };
            int underlay_hsl = under > 0 ? hsl_encode((int)(w[0] * 256 / w[3]), (int)(w[1] / w[4]), (int)(w[2] / w[4])) : -1;
            int uc[4];
            for (int k = 0; k < 4; k++) uc[k] = tile_light_underlay(underlay_hsl, l[k]);
            if (!over) {
                scene_paint_tile(out, s, x, y, h, uc);
                continue;
            }
            const Overlay* o = &d->overlays[over - 1];
            int overlay_hsl = o->texture >= 0 ? d->textures[o->texture].average_hsl
                : o->rgb == 0xFF00FF ? -2 : hsl_encode(o->hue, o->saturation, o->lightness);
            int oc[4];
            for (int k = 0; k < 4; k++) oc[k] = tile_light_overlay(overlay_hsl, l[k]);
            int shape = g->shapes[t] + 1;
            if (shape == 1) scene_paint_tile(out, s, x, y, h, oc);
            else scene_shape_tile(out, s, x, y, shape - 1, g->rotations[t], h, uc, oc);
        }
    for (int k = 0; k < 5; k++) free(sums[k]);
}

static int loc_model(Defs* d, const LocDef* def, int type, int rotation, Model* out) {
    int ids[16], count = 0;
    int typed = def->model_count && def->model_types[0] != -1;
    if (typed) {
        for (int i = 0; i < def->model_count && !count; i++)
            if (def->model_types[i] == type) ids[count++] = def->model_ids[i];
    } else if (type == 10) {
        for (int i = 0; i < def->model_count; i++) ids[count++] = def->model_ids[i];
    }
    if (!count) return 0;
    int mirror = def->mirrored ^ (rotation > 3);
    Model parts[16];
    for (int i = 0; i < count; i++) {
        parts[i] = model_copy(defs_model(d, ids[i]));
        if (mirror) model_mirror(&parts[i]);
    }
    if (count == 1) {
        *out = parts[0];
    } else {
        *out = model_merge(parts, count);
        for (int i = 0; i < count; i++) model_free(&parts[i]);
    }
    if (type == 4 && rotation > 3) {
        model_rotate_y(out, 256);
        model_translate(out, 45, 0, -45);
    }
    model_rotate_quarter(out, rotation & 3);
    for (int i = 0; i < def->recolor_count; i++) model_recolor(out, def->recolor_from[i], def->recolor_to[i]);
    for (int i = 0; i < def->retexture_count; i++) model_retexture(out, def->retexture_from[i], def->retexture_to[i]);
    if (def->scale_x != 128 || def->scale_height != 128 || def->scale_y != 128)
        model_resize(out, def->scale_x, def->scale_height, def->scale_y);
    if (def->offset_x || def->offset_height || def->offset_y) model_translate(out, def->offset_x, def->offset_height, def->offset_y);
    return 1;
}

typedef struct {
    const MapGrid* g;
    int plane;
} Ground;

static int ground_height(const void* ctx, int x, int z) {
    const Ground* gr = ctx;
    assert(x >= 0 && z >= 0 && x <= gr->g->width && z <= gr->g->height);
    return gr->g->heights[map_corner(gr->g, gr->plane, x, z)];
}

static int model_xz_radius(const Model* m) {
    int r = 0;
    for (int v = 0; v < m->vertex_count; v++)
        if (m->x[v] * m->x[v] + m->z[v] * m->z[v] > r) r = m->x[v] * m->x[v] + m->z[v] * m->z[v];
    return (int)(sqrt((double)r) + 0.99);
}

enum { ATLAS_COLUMNS = 16, ATLAS_CELL = 128 };

typedef struct {
    int width, height, white_cell;
} AtlasLayout;

static AtlasLayout atlas_layout(const Defs* d) {
    int cells = d->texture_count + 1;
    return (AtlasLayout){ATLAS_COLUMNS * ATLAS_CELL, (cells + ATLAS_COLUMNS - 1) / ATLAS_COLUMNS * ATLAS_CELL, d->texture_count};
}

static void atlas_uv(const AtlasLayout* a, int cell, float u, float v, float* out_u, float* out_v) {
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    float cx = (float)(cell % ATLAS_COLUMNS * ATLAS_CELL), cy = (float)(cell / ATLAS_COLUMNS * ATLAS_CELL);
    *out_u = (cx + 0.5f + u * (ATLAS_CELL - 1)) / a->width;
    *out_v = (cy + 0.5f + v * (ATLAS_CELL - 1)) / a->height;
}

static void face_uvs(const Model* m, int f, float u[3], float v[3]) {
    int t = m->tex_coords[f];
    int p = t == -1 ? m->a[f] : m->tex_p[t & 255], q = t == -1 ? m->b[f] : m->tex_m[t & 255], r = t == -1 ? m->c[f] : m->tex_n[t & 255];
    double ox = m->x[p], oy = m->y[p], oz = m->z[p];
    double ax = m->x[q] - ox, ay = m->y[q] - oy, az = m->z[q] - oz;
    double bx = m->x[r] - ox, by = m->y[r] - oy, bz = m->z[r] - oz;
    double nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
    double ux = by * nz - bz * ny, uy = bz * nx - bx * nz, uz = bx * ny - by * nx;
    double vx = ay * nz - az * ny, vy = az * nx - ax * nz, vz = ax * ny - ay * nx;
    double du = ux * ax + uy * ay + uz * az, dv = vx * bx + vy * by + vz * bz;
    int corners[3] = {m->a[f], m->b[f], m->c[f]};
    for (int k = 0; k < 3; k++) {
        double px = m->x[corners[k]] - ox, py = m->y[corners[k]] - oy, pz = m->z[corners[k]] - oz;
        u[k] = du ? (float)((ux * px + uy * py + uz * pz) / du) : (float)(k == 1);
        v[k] = dv ? (float)((vx * px + vy * py + vz * pz) / dv) : (float)(k == 2);
    }
}

static void emit_model(Soup* out, const Defs* d, const Model* m, const ModelShade* shade, float px, float py, float pz) {
    AtlasLayout atlas = atlas_layout(d);
    int min_priority = 127;
    for (int f = 0; f < m->face_count; f++) min_priority = m->priorities[f] < min_priority ? m->priorities[f] : min_priority;
    for (int f = 0; f < m->face_count; f++) {
        if (shade->c3[f] == -2) continue;
        int textured = m->textures[f] >= 0 && m->textures[f] < d->texture_count;
        int corner[3] = {m->a[f], m->b[f], m->c[f]};
        int light[3] = {shade->c1[f], shade->c3[f] == -1 ? shade->c1[f] : shade->c2[f], shade->c3[f] == -1 ? shade->c1[f] : shade->c3[f]};
        float fu[3] = {0}, fv[3] = {0};
        if (textured) face_uvs(m, f, fu, fv);
        float bias[3] = {0};
        int delta = m->priorities[f] - min_priority;
        if (delta > 0) {
            float e1[3] = {(float)(m->x[corner[1]] - m->x[corner[0]]), (float)(m->y[corner[1]] - m->y[corner[0]]), (float)(m->z[corner[1]] - m->z[corner[0]])};
            float e2[3] = {(float)(m->x[corner[2]] - m->x[corner[0]]), (float)(m->y[corner[2]] - m->y[corner[0]]), (float)(m->z[corner[2]] - m->z[corner[0]])};
            float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
            float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 0.001f)
                for (int k = 0; k < 3; k++) bias[k] = n[k] * delta * 0.15f / len;
        }
        int alpha = 255 - (uint8_t)m->alphas[f];
        for (int k = 0; k < 3; k++) {
            int v = corner[k];
            float u = 0, w = 0;
            int rgb;
            if (textured) {
                int grey = tile_light_level(light[k]) * 2;
                rgb = grey << 16 | grey << 8 | grey;
                atlas_uv(&atlas, m->textures[f], fu[k], fv[k], &u, &w);
            } else {
                rgb = model_palette[light[k] & 0xFFFF];
                atlas_uv(&atlas, atlas.white_cell, 0.5f, 0.5f, &u, &w);
            }
            float x = px + m->x[v] - bias[0], y = py + m->y[v] - bias[1], z = pz + m->z[v] - bias[2];
            soup_push(out, x / 128.0f, -y / 128.0f, -z / 128.0f, rgb, alpha, u, w);
        }
    }
}

typedef struct {
    Model model;
    int type, rotation;
    int x, y, height;
} PlacedModel;

static void loc_footprint(const LocDef* def, int rotation, int* sx, int* sy) {
    int swap = rotation == 1 || rotation == 3;
    *sx = swap ? def->size_y : def->size_x;
    *sy = swap ? def->size_x : def->size_y;
}

static int place_loc(Scene* s, const MapLoc* loc, const LocDef* def, int type, int rotation, PlacedModel* out) {
    const MapGrid* g = s->g;
    const LocDef* base = defs_loc(s->defs, loc->id);
    int sx, sy;
    loc_footprint(base, loc->rotation, &sx, &sy);
    int x0 = loc->x + (sx >> 1), x1 = loc->x + ((sx + 1) >> 1);
    int y0 = loc->y + (sy >> 1), y1 = loc->y + ((sy + 1) >> 1);
    if (loc->x + sx > g->width) x0 = loc->x, x1 = loc->x + 1;
    if (loc->y + sy > g->height) y0 = loc->y, y1 = loc->y + 1;
    const int* h = g->heights;
    int p = loc->plane;
    int height = (h[map_corner(g, p, x1, y0)] + h[map_corner(g, p, x0, y0)] + h[map_corner(g, p, x0, y1)] + h[map_corner(g, p, x1, y1)]) >> 2;
    if (!loc_model(s->defs, def, type, rotation, &out->model)) return 0;
    out->type = type, out->rotation = rotation, out->height = height;
    out->x = (loc->x << 7) + (sx << 6);
    out->y = (loc->y << 7) + (sy << 6);
    return 1;
}

static const LocDef* loc_visual(Defs* d, const LocDef* def) {
    if (def->transform != -1) return defs_loc(d, def->transform);
    return def->model_count ? def : NULL;
}

static void scene_shadows(Scene* s) {
    const MapGrid* g = s->g;
    static const int wall[4][2][2] = {{{0, 0}, {0, 1}}, {{0, 1}, {1, 1}}, {{1, 0}, {1, 1}}, {{0, 0}, {1, 0}}};
    static const int corner[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (int i = 0; i < g->loc_count; i++) {
        const MapLoc* loc = &g->locs[i];
        if (!scene_in_rect(s, loc->x, loc->y, 2)) continue;
        const LocDef* def = defs_loc(s->defs, loc->id);
        if (!def->clipped) continue;
        int r = loc->rotation;
        if (loc->type == 0)
            for (int k = 0; k < 2; k++) scene_shadow(s, loc->plane, loc->x + wall[r][k][0], loc->y + wall[r][k][1], 50);
        if (loc->type == 1 || loc->type == 3) scene_shadow(s, loc->plane, loc->x + corner[r][0], loc->y + corner[r][1], 50);
        if (loc->type != 10 && loc->type != 11) continue;
        int value = 15;
        if (!def->animated) {
            PlacedModel pm;
            if (!place_loc(s, loc, def, 10, r, &pm)) continue;
            value = model_xz_radius(&pm.model) / 4;
            value = value > 30 ? 30 : value;
            model_free(&pm.model);
        }
        int sx, sy;
        loc_footprint(def, r, &sx, &sy);
        for (int cx = 0; cx <= sx; cx++)
            for (int cy = 0; cy <= sy; cy++) scene_shadow(s, loc->plane, loc->x + cx, loc->y + cy, value);
    }
}

static int id_in_set(const int* ids, int count, int id) {
    for (int i = 0; i < count; i++)
        if (ids[i] == id) return 1;
    return 0;
}

static void scene_objects(Scene* s, Soup* out, const int* exclude_ids, int exclude_count) {
    const MapGrid* g = s->g;
    static const int deco_x[4] = {1, 0, -1, 0}, deco_y[4] = {0, -1, 0, 1};
    static const int diag_x[4] = {1, -1, -1, 1}, diag_y[4] = {-1, -1, 1, 1};
    int* wall_offset = malloc(sizeof(int) * (size_t)MAP_PLANES * g->width * g->height);
    for (size_t i = 0; i < (size_t)MAP_PLANES * g->width * g->height; i++) wall_offset[i] = -1;
    for (int i = 0; i < g->loc_count; i++) {
        const MapLoc* loc = &g->locs[i];
        if (loc->type <= 3 && scene_in_rect(s, loc->x, loc->y, 2))
            wall_offset[map_tile(g, loc->plane, loc->x, loc->y)] = defs_loc(s->defs, loc->id)->decor_offset;
    }
    for (int i = 0; i < g->loc_count; i++) {
        const MapLoc* loc = &g->locs[i];
        if (!scene_in_rect(s, loc->x, loc->y, 0)) continue;
        if (id_in_set(exclude_ids, exclude_count, loc->id)) continue;
        int linked = g->settings[map_tile(g, 1, loc->x, loc->y)] & 2;
        if (loc->plane != 0 && !(loc->plane == 1 && linked)) continue;
        const LocDef* base = defs_loc(s->defs, loc->id);
        const LocDef* def = loc_visual(s->defs, base);
        if (!def) continue;
        int type = loc->type, r = loc->rotation;
        int requests[2][2] = {{type, r}, {-1, 0}};
        int ox = 0, oy = 0, yaw = 0;
        int boundary = wall_offset[map_tile(g, loc->plane, loc->x, loc->y)];
        if (type == 11) requests[0][0] = 10, yaw = 256;
        if (type == 2) requests[0][1] = r + 4, requests[1][0] = 2, requests[1][1] = (r + 1) & 3;
        if (type >= 4 && type <= 8) requests[0][0] = 4;
        if (type == 5) {
            int off = boundary >= 0 ? boundary : 16;
            ox = deco_x[r] * off, oy = deco_y[r] * off;
        }
        if (type == 6 || type == 8) {
            int off = boundary >= 0 ? boundary / 2 : 8;
            requests[0][1] = r + 4;
            ox = diag_x[r] * off, oy = diag_y[r] * off;
            if (type == 8) requests[1][0] = 4, requests[1][1] = ((r + 2) & 3) + 4;
        }
        if (type == 7) requests[0][1] = ((r + 2) & 3) + 4;
        int tile_center = type <= 9 || type >= 12;
        for (int q = 0; q < 2 && requests[q][0] >= 0; q++) {
            PlacedModel pm;
            if (!place_loc(s, loc, def, requests[q][0], requests[q][1], &pm)) continue;
            ModelShade shade = model_light(&pm.model, def->ambient + 64, def->contrast + 768, -50, -10, -50);
            if (def->contoured_ground >= 0) {
                Ground ground = {g, loc->plane};
                model_contour(&pm.model, ground_height, &ground, pm.x, pm.height, pm.y, def->contoured_ground);
            }
            if (yaw) model_rotate_y(&pm.model, yaw);
            float px = (float)(tile_center ? (loc->x << 7) + 64 : pm.x) + ox + g->base_x * 128.0f;
            float pz = (float)(tile_center ? (loc->y << 7) + 64 : pm.y) + oy + g->base_y * 128.0f;
            emit_model(out, s->defs, &pm.model, &shade, px, (float)pm.height, pz);
            model_shade_free(&shade);
            model_free(&pm.model);
        }
    }
    free(wall_offset);
}

static void texture_pixels(OsrsCache* cache, const TextureDef* t, uint32_t* out) {
    CacheFile f = cache_read_file(cache, CACHE_INDEX_SPRITES, t->sprite, 0);
    assert(f.data);
    SpriteGroup g = sprite_group_decode(f.data, f.size, PALETTE_BRIGHTNESS_PERMILLE / 1000.0, SPRITE_ALPHA_INDEX);
    SpriteFrame* frame = &g.frames[0];
    int width = frame->width;
    assert(width == frame->height && (width == 64 || width == ATLAS_CELL));
    for (int y = 0; y < ATLAS_CELL; y++)
        for (int x = 0; x < ATLAS_CELL; x++) {
            int sx = width == ATLAS_CELL ? x : x >> 1, sy = width == ATLAS_CELL ? y : y >> 1;
            out[y * ATLAS_CELL + x] = frame->pixels[sy * width + sx];
        }
    sprite_group_free(&g);
    free(f.data);
}

static void write_atlas(const char* path, Defs* d) {
    AtlasLayout a = atlas_layout(d);
    uint32_t* pixels = calloc((size_t)a.width * a.height, sizeof(uint32_t));
    uint32_t* cell = malloc(sizeof(uint32_t) * ATLAS_CELL * ATLAS_CELL);
    for (int t = 0; t <= d->texture_count; t++) {
        if (t == a.white_cell) {
            for (int i = 0; i < ATLAS_CELL * ATLAS_CELL; i++) cell[i] = 0xFFFFFFFFu;
        } else {
            if (!d->textures[t].present) continue;
            texture_pixels(d->cache, &d->textures[t], cell);
        }
        int cx = t % ATLAS_COLUMNS * ATLAS_CELL, cy = t / ATLAS_COLUMNS * ATLAS_CELL;
        for (int y = 0; y < ATLAS_CELL; y++) memcpy(&pixels[(size_t)(cy + y) * a.width + cx], &cell[y * ATLAS_CELL], ATLAS_CELL * 4);
    }
    FILE* f = fopen(path, "wb");
    assert(f);
    uint32_t header[3] = {ATLS_MAGIC, (uint32_t)a.width, (uint32_t)a.height};
    fwrite(header, sizeof(header), 1, f);
    fwrite(pixels, 4, (size_t)a.width * a.height, f);
    fclose(f);
    free(cell);
    free(pixels);
}

static void write_tanim(const char* path, const Defs* d) {
    FILE* f = fopen(path, "wb");
    assert(f);
    uint32_t header[3] = {TANM_MAGIC, TANM_VERSION, 0};
    fwrite(header, sizeof(header), 1, f);
    uint32_t count = 0;
    for (int t = 0; t < d->texture_count; t++) {
        if (!d->textures[t].present || !d->textures[t].speed) continue;
        uint32_t texture_id = (uint32_t)t;
        uint16_t x = (uint16_t)(t % ATLAS_COLUMNS * ATLAS_CELL), y = (uint16_t)(t / ATLAS_COLUMNS * ATLAS_CELL);
        uint16_t w = ATLAS_CELL, h = ATLAS_CELL, pad = 0;
        uint8_t direction = (uint8_t)d->textures[t].direction, speed = (uint8_t)d->textures[t].speed;
        fwrite(&texture_id, 4, 1, f);
        fwrite(&x, 2, 1, f);
        fwrite(&y, 2, 1, f);
        fwrite(&w, 2, 1, f);
        fwrite(&h, 2, 1, f);
        fwrite(&direction, 1, 1, f);
        fwrite(&speed, 1, 1, f);
        fwrite(&pad, 2, 1, f);
        count++;
    }
    fseek(f, 0, SEEK_SET);
    header[2] = count;
    fwrite(header, sizeof(header), 1, f);
    fclose(f);
}

static void export_textures(OsrsCache* cache, const char* out_dir) {
    Defs defs = {.cache = cache};
    defs_load_floors(&defs);
    char path[4096];
    snprintf(path, sizeof(path), "%s/textures.atlas", out_dir);
    write_atlas(path, &defs);
    snprintf(path, sizeof(path), "%s/textures.tanim", out_dir);
    write_tanim(path, &defs);
}

static void write_terrain(const char* path, const Scene* s, const Soup* soup) {
    const MapGrid* g = s->g;
    RegionRect r = s->r;
    int min_x = r.x0 * MAP_REGION, min_y = r.y0 * MAP_REGION;
    int w = (r.x1 - r.x0 + 1) * MAP_REGION, h = (r.y1 - r.y0 + 1) * MAP_REGION;
    FILE* f = fopen(path, "wb");
    assert(f);
    uint32_t header[3] = {TERR_MAGIC, (uint32_t)soup->count, (uint32_t)((r.x1 - r.x0 + 1) * (r.y1 - r.y0 + 1))};
    int32_t origin[2] = {min_x, min_y};
    fwrite(header, sizeof(header), 1, f);
    fwrite(origin, sizeof(origin), 1, f);
    fwrite(soup->xyz, sizeof(float) * 3, (size_t)soup->count, f);
    fwrite(soup->rgba, 4, (size_t)soup->count, f);
    int32_t hm[4] = {min_x, min_y, w, h};
    fwrite(hm, sizeof(hm), 1, f);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int gx = min_x - g->base_x + x, gy = min_y - g->base_y + y;
            int plane = 0;
            for (int k = 0; k < 4; k++) {
                int tx = gx - (k & 1), ty = gy - (k >> 1);
                if (g->settings[map_tile(g, 1, tx, ty)] & 2) plane = 1;
            }
            float height = -g->heights[map_corner(g, plane, gx, gy)] / 128.0f;
            fwrite(&height, sizeof(height), 1, f);
        }
    fclose(f);
}

static void write_objects(const char* path, const Scene* s, const Soup* soup, int placements) {
    FILE* f = fopen(path, "wb");
    assert(f);
    uint32_t magic = OBJ2_MAGIC, count = (uint32_t)placements;
    int32_t origin[2] = {s->r.x0 * MAP_REGION, s->r.y0 * MAP_REGION};
    uint32_t vertices = (uint32_t)soup->count;
    fwrite(&magic, 4, 1, f);
    fwrite(&count, 4, 1, f);
    fwrite(origin, sizeof(origin), 1, f);
    fwrite(&vertices, 4, 1, f);
    fwrite(soup->xyz, sizeof(float) * 3, (size_t)soup->count, f);
    fwrite(soup->rgba, 4, (size_t)soup->count, f);
    fwrite(soup->uv, sizeof(float) * 2, (size_t)soup->count, f);
    fclose(f);
}

typedef struct {
    const char* name;
    int ids[64];
    int count;
} AltObjects;

static void export_scene(OsrsCache* cache, const char* out_dir, const char* name, RegionRect r, const AltObjects* alt) {
    Defs defs = {.cache = cache};
    defs_load_floors(&defs);
    MapGrid g = map_load(cache, r.x0 - 1, r.y0 - 1, r.x1 + 1, r.y1 + 1);
    size_t corners = (size_t)MAP_PLANES * (g.width + 1) * (g.height + 1);
    Scene s = {.defs = &defs, .g = &g, .r = r, .light = calloc(corners, sizeof(int)), .shadow = calloc(corners, 1)};
    char path[4096];
    int* flags = build_collision(&defs, &g);
    snprintf(path, sizeof(path), "%s/%s.cmap", out_dir, name);
    write_collision(path, &g, flags, r);
    scene_shadows(&s);
    scene_light(&s);
    Soup terrain = {0}, objects = {0};
    scene_terrain_plane(&terrain, &s, 0, 0);
    scene_terrain_plane(&terrain, &s, 1, 1);
    snprintf(path, sizeof(path), "%s/%s.terrain", out_dir, name);
    write_terrain(path, &s, &terrain);
    scene_objects(&s, &objects, NULL, 0);
    snprintf(path, sizeof(path), "%s/%s.objects", out_dir, name);
    write_objects(path, &s, &objects, g.loc_count);
    printf("%s: %d locs, %d terrain vertices, %d object vertices\n", name, g.loc_count, terrain.count, objects.count);
    if (alt) {
        Soup alt_objects = {0};
        scene_objects(&s, &alt_objects, alt->ids, alt->count);
        snprintf(path, sizeof(path), "%s/%s.objects", out_dir, alt->name);
        write_objects(path, &s, &alt_objects, g.loc_count);
        printf("%s: %d locs excluded, %d object vertices\n", alt->name, alt->count, alt_objects.count);
        free(alt_objects.xyz); free(alt_objects.rgba); free(alt_objects.uv);
    }
    free(flags);
    free(terrain.xyz); free(terrain.rgba); free(terrain.uv);
    free(objects.xyz); free(objects.rgba); free(objects.uv);
    map_free(&g);
}


typedef struct {
    int id;
    Model model;
    ModelShade shade;
} ExportModel;

typedef struct {
    ExportModel* models;
    int model_count;
    int seqs[1024];
    int seq_count;
} NpcPack;

static void pack_seq(NpcPack* p, int seq) {
    if (seq < 0) return;
    for (int i = 0; i < p->seq_count; i++)
        if (p->seqs[i] == seq) return;
    assert(p->seq_count < 1024);
    p->seqs[p->seq_count++] = seq;
}

typedef struct {
    int seq;
    const Model* model;
    int width_scale, height_scale;
} SeqBake;

typedef struct {
    SeqBake binds[1024];
    int count;
} SeqBakeTable;

static void bind_seq(SeqBakeTable* t, int seq, const Model* model, int width_scale, int height_scale) {
    if (seq < 0) return;
    for (int i = 0; i < t->count; i++)
        if (t->binds[i].seq == seq) {
            t->binds[i] = (SeqBake){seq, model, width_scale, height_scale};
            return;
        }
    assert(t->count < 1024);
    t->binds[t->count++] = (SeqBake){seq, model, width_scale, height_scale};
}

static const SeqBake* find_bind(const SeqBakeTable* t, int seq) {
    for (int i = 0; i < t->count; i++)
        if (t->binds[i].seq == seq) return &t->binds[i];
    return NULL;
}

static Model build_merged(Defs* d, const int* ids, int count) {
    Model parts[32];
    for (int i = 0; i < count; i++) parts[i] = model_copy(defs_model(d, ids[i]));
    if (count == 1) return parts[0];
    Model m = model_merge(parts, count);
    for (int i = 0; i < count; i++) model_free(&parts[i]);
    return m;
}

static void pack_shaded(NpcPack* p, int id, Model m, ModelShade shade) {
    p->models = realloc(p->models, sizeof(ExportModel) * (size_t)(p->model_count + 1));
    p->models[p->model_count++] = (ExportModel){.id = id, .model = m, .shade = shade};
}

static void pack_model(NpcPack* p, int id, Model m, int ambient, int contrast, int width_scale, int height_scale) {
    ModelShade shade = model_light(&m, ambient, contrast, -30, -50, -30);
    if (width_scale != 128 || height_scale != 128) model_resize(&m, width_scale, height_scale, width_scale);
    pack_shaded(p, id, m, shade);
}

static const Model* skin_copy(const Model* m) {
    Model* s = malloc(sizeof(Model));
    *s = model_copy(m);
    return s;
}

static int pack_spotanim(Defs* d, const CacheGroup* gfx_files, NpcPack* p, SeqBakeTable* bakes, int gfx) {
    const CacheFile* file = cache_group_file(gfx_files, gfx);
    assert(file);
    SpotAnimDef g = spotanim_decode(file);
    assert(g.model_id >= 0);
    Model m = build_merged(d, &g.model_id, 1);
    for (int k = 0; k < g.recolor_count; k++) model_recolor(&m, g.recolor_from[k], g.recolor_to[k]);
    for (int k = 0; k < g.retexture_count; k++) model_retexture(&m, g.retexture_from[k], g.retexture_to[k]);
    bind_seq(bakes, g.sequence, skin_copy(&m), 128, 128);
    pack_model(p, (int)(OSRS_SPOTANIM_MODEL_BASE | (uint32_t)gfx), m, g.ambient + 64, g.contrast + 850, 128, 128);
    pack_seq(p, g.sequence);
    return g.sequence;
}

static void pack_loc(Defs* d, NpcPack* p, int loc) {
    assert(loc < 0x10000);
    const LocDef* def = defs_loc(d, loc);
    Model m;
    int built = loc_model(d, def, 10, 0, &m);
    assert(built);
    ModelShade shade = model_light(&m, def->ambient + 64, def->contrast + 768, -50, -10, -50);
    pack_shaded(p, (int)(OSRS_LOC_MODEL_BASE | (uint32_t)loc), m, shade);
}

static void write_models(const char* path, const Defs* d, const NpcPack* p) {
    AtlasLayout atlas = atlas_layout(d);
    FILE* f = fopen(path, "wb");
    assert(f);
    uint32_t header[2] = {MDL4_MAGIC, (uint32_t)p->model_count};
    fwrite(header, sizeof(header), 1, f);
    long table = ftell(f);
    uint32_t* offsets = calloc((size_t)p->model_count, sizeof(uint32_t));
    fwrite(offsets, 4, (size_t)p->model_count, f);
    for (int i = 0; i < p->model_count; i++) {
        const Model* m = &p->models[i].model;
        const ModelShade* shade = &p->models[i].shade;
        assert(m->vertex_count <= 0xFFFF && m->face_count * 3 <= 0xFFFF);
        offsets[i] = (uint32_t)ftell(f);
        uint32_t id = (uint32_t)p->models[i].id;
        uint16_t counts[3] = {(uint16_t)(m->face_count * 3), (uint16_t)m->face_count, (uint16_t)m->vertex_count};
        fwrite(&id, 4, 1, f);
        fwrite(counts, sizeof(counts), 1, f);
        Soup soup = {0};
        int min_priority = 127;
        for (int k = 0; k < m->face_count; k++) min_priority = m->priorities[k] < min_priority ? m->priorities[k] : min_priority;
        for (int k = 0; k < m->face_count; k++) {
            int corner[3] = {m->a[k], m->b[k], m->c[k]};
            int hidden = shade->c3[k] == -2;
            int textured = m->textures[k] >= 0 && m->textures[k] < d->texture_count;
            int light[3] = {shade->c1[k], shade->c3[k] == -1 ? shade->c1[k] : shade->c2[k], shade->c3[k] == -1 ? shade->c1[k] : shade->c3[k]};
            float fu[3] = {0}, fv[3] = {0};
            if (textured) face_uvs(m, k, fu, fv);
            float bias[3] = {0};
            int delta = m->priorities[k] - min_priority;
            if (delta > 0) {
                float e1[3] = {(float)(m->x[corner[1]] - m->x[corner[0]]), (float)(m->y[corner[0]] - m->y[corner[1]]), (float)(m->z[corner[1]] - m->z[corner[0]])};
                float e2[3] = {(float)(m->x[corner[2]] - m->x[corner[0]]), (float)(m->y[corner[0]] - m->y[corner[2]]), (float)(m->z[corner[2]] - m->z[corner[0]])};
                float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (len > 0.001f)
                    for (int c = 0; c < 3; c++) bias[c] = n[c] * delta * 0.15f / len;
            }
            int alpha = hidden ? 0 : 255 - (uint8_t)m->alphas[k];
            for (int c = 0; c < 3; c++) {
                int v = corner[c];
                float u = 0, w = 0;
                int rgb = 0;
                if (!hidden && textured) {
                    int grey = tile_light_level(light[c]) * 2;
                    rgb = grey << 16 | grey << 8 | grey;
                    atlas_uv(&atlas, m->textures[k], fu[c], fv[c], &u, &w);
                } else {
                    if (!hidden) rgb = model_palette[light[c] & 0xFFFF];
                    atlas_uv(&atlas, atlas.white_cell, 0.5f, 0.5f, &u, &w);
                }
                soup_push(&soup, m->x[v] - bias[0], -m->y[v] - bias[1], m->z[v] - bias[2], rgb, alpha, u, w);
            }
        }
        fwrite(soup.xyz, sizeof(float) * 3, (size_t)soup.count, f);
        fwrite(soup.rgba, 4, (size_t)soup.count, f);
        fwrite(soup.uv, sizeof(float) * 2, (size_t)soup.count, f);
        free(soup.xyz); free(soup.rgba); free(soup.uv);
        for (int v = 0; v < m->vertex_count; v++) {
            int16_t xyz[3] = {(int16_t)m->x[v], (int16_t)m->y[v], (int16_t)m->z[v]};
            assert(xyz[0] == m->x[v] && xyz[1] == m->y[v] && xyz[2] == m->z[v]);
            fwrite(xyz, sizeof(xyz), 1, f);
        }
        fwrite(m->vertex_skins, 1, (size_t)m->vertex_count, f);
        for (int k = 0; k < m->face_count; k++) {
            uint16_t abc[3] = {(uint16_t)m->a[k], (uint16_t)m->b[k], (uint16_t)m->c[k]};
            fwrite(abc, sizeof(abc), 1, f);
        }
        fwrite(m->priorities, 1, (size_t)m->face_count, f);
        fwrite(m->alphas, 1, (size_t)m->face_count, f);
        for (int k = 0; k < m->face_count; k++) {
            uint8_t label = m->has_face_skins ? m->face_skins[k] : 255;
            fwrite(&label, 1, 1, f);
        }
    }
    fseek(f, table, SEEK_SET);
    fwrite(offsets, 4, (size_t)p->model_count, f);
    fclose(f);
    free(offsets);
}

static int compare_int(const void* a, const void* b) { return *(const int*)a - *(const int*)b; }

static void write_anims(const char* path, OsrsCache* cache, NpcPack* p, const SeqBakeTable* bakes) {
    CacheGroup seq_files = cache_read_group(cache, CACHE_INDEX_CONFIGS, CACHE_CONFIG_SEQ);
    CacheIndex* bases = cache_index(cache, CACHE_INDEX_BASES);
    Skeleton* skeletons = calloc((size_t)bases->id_limit, sizeof(Skeleton));
    uint8_t* used = calloc((size_t)bases->id_limit, 1);
    qsort(p->seqs, (size_t)p->seq_count, sizeof(int), compare_int);
    SeqDef* seqs = calloc((size_t)p->seq_count, sizeof(SeqDef));
    Frame** frames = calloc((size_t)p->seq_count, sizeof(Frame*));
    int16_t** maya_frames = calloc((size_t)p->seq_count, sizeof(int16_t*));
    int* maya_vcount = calloc((size_t)p->seq_count, sizeof(int));
    uint32_t total = 0;
    int any_maya = 0;
    for (int i = 0; i < p->seq_count; i++) {
        const CacheFile* file = cache_group_file(&seq_files, p->seqs[i]);
        assert(file);
        seqs[i] = seq_decode(file);
        if (seqs[i].maya >= 0) {
            any_maya = 1;
            const SeqBake* bind = find_bind(bakes, p->seqs[i]);
            assert(bind);
            assert(seqs[i].maya_end > seqs[i].maya_start);
            seqs[i].frame_count = seqs[i].maya_end - seqs[i].maya_start;
            seqs[i].interleave_count = 0;
            int vc = bind->model->vertex_count;
            maya_vcount[i] = vc;
            maya_frames[i] = malloc(sizeof(int16_t) * (size_t)vc * 3 * (size_t)seqs[i].frame_count);
            MayaAnimation anim = maya_animation_decode(cache, seqs[i].maya);
            for (int k = 0; k < seqs[i].frame_count; k++) {
                int16_t* out = maya_frames[i] + (size_t)k * (size_t)vc * 3;
                maya_bake_frame(&anim, bind->model, seqs[i].maya_start + k, out);
                maya_apply_npc_scale(out, vc, bind->width_scale, bind->height_scale);
            }
            maya_animation_free(&anim);
            total += (uint32_t)seqs[i].frame_count;
            continue;
        }
        frames[i] = calloc((size_t)seqs[i].frame_count + 1, sizeof(Frame));
        for (int k = 0; k < seqs[i].frame_count; k++) {
            CacheFile raw = cache_read_file(cache, CACHE_INDEX_ANIMS, seqs[i].frames[k] >> 16, seqs[i].frames[k] & 0xFFFF);
            assert(raw.data);
            int skeleton = raw.data[0] << 8 | raw.data[1];
            if (!used[skeleton]) {
                CacheFile sk = cache_read_file(cache, CACHE_INDEX_BASES, skeleton, 0);
                skeletons[skeleton] = skeleton_decode(&sk);
                free(sk.data);
                used[skeleton] = 1;
            }
            frames[i][k] = frame_decode(&raw, &skeletons[skeleton]);
            free(raw.data);
        }
        total += (uint32_t)seqs[i].frame_count;
    }
    int base_count = 0;
    for (int b = 0; b < bases->id_limit; b++) base_count += used[b];
    FILE* f = fopen(path, "wb");
    assert(f);
    uint32_t magic = ANIM2_MAGIC;
    uint16_t version[2] = {(uint16_t)(any_maya ? 3 : 2), 24};
    uint32_t header[4] = {(uint32_t)base_count, (uint32_t)p->seq_count, total, any_maya ? 7u : 1u};
    fwrite(&magic, 4, 1, f);
    fwrite(version, sizeof(version), 1, f);
    fwrite(header, sizeof(header), 1, f);
    for (int b = 0; b < bases->id_limit; b++) {
        if (!used[b]) continue;
        const Skeleton* sk = &skeletons[b];
        uint16_t id = (uint16_t)b;
        uint8_t count = (uint8_t)sk->count;
        fwrite(&id, 2, 1, f);
        fwrite(&count, 1, 1, f);
        fwrite(sk->types, 1, (size_t)sk->count, f);
        for (int s = 0; s < sk->count; s++) {
            fwrite(&sk->label_counts[s], 1, 1, f);
            fwrite(sk->labels[s], 1, sk->label_counts[s], f);
        }
    }
    for (int i = 0; i < p->seq_count; i++) {
        const SeqDef* q = &seqs[i];
        uint16_t head[2] = {(uint16_t)p->seqs[i], (uint16_t)q->frame_count};
        uint8_t interleave = (uint8_t)q->interleave_count;
        int8_t walk = (int8_t)q->walk_flag;
        fwrite(head, sizeof(head), 1, f);
        fwrite(&interleave, 1, 1, f);
        fwrite(q->interleave, 1, (size_t)q->interleave_count, f);
        fwrite(&walk, 1, 1, f);
        if (q->maya >= 0) {
            uint16_t vc16 = (uint16_t)maya_vcount[i];
            for (int k = 0; k < q->frame_count; k++) {
                uint16_t delay = 1;
                uint8_t kind = ANIM_FRAME_MAYA_BAKED;
                fwrite(&delay, 2, 1, f);
                fwrite(&kind, 1, 1, f);
                fwrite(&vc16, 2, 1, f);
                fwrite(maya_frames[i] + (size_t)k * (size_t)vc16 * 3, sizeof(int16_t), (size_t)vc16 * 3, f);
            }
            continue;
        }
        for (int k = 0; k < q->frame_count; k++) {
            const Frame* fr = &frames[i][k];
            uint16_t delay = (uint16_t)q->delays[k], base = (uint16_t)fr->skeleton;
            uint8_t count = (uint8_t)fr->count;
            fwrite(&delay, 2, 1, f);
            if (any_maya) {
                uint8_t kind = ANIM_FRAME_LEGACY;
                fwrite(&kind, 1, 1, f);
            }
            fwrite(&base, 2, 1, f);
            fwrite(&count, 1, 1, f);
            for (int t = 0; t < fr->count; t++) {
                int16_t d3[3] = {fr->dx[t], fr->dy[t], fr->dz[t]};
                fwrite(&fr->slot[t], 1, 1, f);
                fwrite(d3, sizeof(d3), 1, f);
            }
        }
    }
    fclose(f);
    for (int i = 0; i < p->seq_count; i++) free(maya_frames[i]);
    free(maya_frames);
    free(maya_vcount);
}

typedef struct {
    int npc, attack;
    int owned[8];
    int owned_count;
} NpcSpec;

typedef struct {
    int seq, model;
} SeqModelSpec;

typedef struct {
    NpcSpec npcs[256];
    SeqModelSpec seqmodels[256];
    int gfx[256], seqs[256], locs[64];
    int npc_count, seqmodel_count, gfx_count, seq_count, loc_count;
} PackSpec;

static void bind_npc_seqs(SeqBakeTable* bakes, const NpcSpec* spec, const NpcDef* n, const Model* skin) {
    int seqs[] = {n->idle, n->walk, n->run, n->turn_180, n->turn_cw, n->turn_ccw, spec->attack};
    for (size_t k = 0; k < sizeof(seqs) / sizeof(*seqs); k++) bind_seq(bakes, seqs[k], skin, n->width_scale, n->height_scale);
    for (int k = 0; k < spec->owned_count; k++) bind_seq(bakes, spec->owned[k], skin, n->width_scale, n->height_scale);
}

static void export_npcs(OsrsCache* cache, const char* out_dir, const char* name, const PackSpec* spec) {
    Defs defs = {.cache = cache};
    defs_load_floors(&defs);
    CacheGroup npc_files = cache_read_group(cache, CACHE_INDEX_CONFIGS, CACHE_CONFIG_NPC);
    CacheGroup gfx_files = cache_read_group(cache, CACHE_INDEX_CONFIGS, CACHE_CONFIG_SPOTANIM);
    GamevalTable npc_names = gameval_load(cache, GAMEVAL_NPCS);
    GamevalTable seq_names = gameval_load(cache, GAMEVAL_ANIMATIONS);
    GamevalTable gfx_names = gameval_load(cache, GAMEVAL_SPOTANIMS);
    GamevalTable loc_names = gameval_load(cache, GAMEVAL_OBJECTS);
    NpcPack pack = {0};
    SeqBakeTable bakes = {0};
    char path[4096];
    snprintf(path, sizeof(path), "%s/npc_models_%s.h", out_dir, name);
    FILE* h = fopen(path, "w");
    assert(h);
    char upper[256];
    size_t len = strlen(name);
    assert(len < sizeof(upper));
    for (size_t i = 0; i <= len; i++) upper[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]);
    fprintf(h, "#ifndef NPC_MODELS_%s_H\n#define NPC_MODELS_%s_H\n\n#include \"../osrs_asset_formats.h\"\n\n", upper, upper);
    fprintf(h, "static const NpcModelMapping NPC_MODEL_MAP_%s_GEN[] = {\n", upper);
    for (int i = 0; i < spec->npc_count; i++) {
        const CacheFile* file = cache_group_file(&npc_files, spec->npcs[i].npc);
        assert(file);
        NpcDef n = npc_decode(file);
        Model m = build_merged(&defs, n.model_ids, n.model_count);
        for (int k = 0; k < n.recolor_count; k++) model_recolor(&m, n.recolor_from[k], n.recolor_to[k]);
        for (int k = 0; k < n.retexture_count; k++) model_retexture(&m, n.retexture_from[k], n.retexture_to[k]);
        const Model* skin = skin_copy(&m);
        pack_model(&pack, OSRS_NPC_MODEL_BASE | spec->npcs[i].npc, m, n.ambient + 64, n.contrast * 5 + 850, n.width_scale, n.height_scale);
        int anims[] = {n.idle, n.walk, n.run, n.turn_180, n.turn_cw, n.turn_ccw, spec->npcs[i].attack};
        for (size_t k = 0; k < sizeof(anims) / sizeof(*anims); k++) pack_seq(&pack, anims[k]);
        for (int k = 0; k < spec->npcs[i].owned_count; k++) pack_seq(&pack, spec->npcs[i].owned[k]);
        bind_npc_seqs(&bakes, &spec->npcs[i], &n, skin);
        fprintf(h, "    {%d, 0x%X, %d, %d, %d, %d},  /* %s */\n", spec->npcs[i].npc, OSRS_NPC_MODEL_BASE | spec->npcs[i].npc,
            n.idle < 0 ? 65535 : n.idle, spec->npcs[i].attack < 0 ? 65535 : spec->npcs[i].attack,
            n.walk < 0 ? 65535 : n.walk, n.run < 0 ? 65535 : n.run, gameval_name(&npc_names, spec->npcs[i].npc));
    }
    fprintf(h, "};\n\n");
    fprintf(h, "/* spotanim model + animation ids, named from the cache's gameval archive */\n");
    for (int i = 0; i < spec->gfx_count; i++) {
        int seq = pack_spotanim(&defs, &gfx_files, &pack, &bakes, spec->gfx[i]);
        char ident[128];
        gameval_ident(gameval_name(&gfx_names, spec->gfx[i]), ident, sizeof(ident));
        fprintf(h, "#define SPOTANIM_%s_%s 0x%X\n#define SPOTANIM_%s_%s_ANIM %d\n", upper, ident,
            OSRS_SPOTANIM_MODEL_BASE | spec->gfx[i], upper, ident, seq);
    }
    for (int i = 0; i < spec->loc_count; i++) {
        pack_loc(&defs, &pack, spec->locs[i]);
        char ident[128];
        gameval_ident(gameval_name(&loc_names, spec->locs[i]), ident, sizeof(ident));
        fprintf(h, "#define LOC_%s_%s 0x%X\n", upper, ident, OSRS_LOC_MODEL_BASE | spec->locs[i]);
    }
    for (int i = 0; i < spec->seq_count; i++) pack_seq(&pack, spec->seqs[i]);
    for (int i = 0; i < spec->seqmodel_count; i++) {
        pack_seq(&pack, spec->seqmodels[i].seq);
        bind_seq(&bakes, spec->seqmodels[i].seq, skin_copy(defs_model(&defs, spec->seqmodels[i].model)), 128, 128);
    }
    fprintf(h, "\n/* sequence ids, named from the cache's gameval archive */\n");
    for (int i = 0; i < pack.seq_count; i++) {
        char ident[128];
        gameval_ident(gameval_name(&seq_names, pack.seqs[i]), ident, sizeof(ident));
        fprintf(h, "#define SEQ_%s_%s %d\n", upper, ident, pack.seqs[i]);
    }
    fprintf(h, "\n#endif\n");
    fclose(h);
    snprintf(path, sizeof(path), "%s/%s.models", out_dir, name);
    write_models(path, &defs, &pack);
    snprintf(path, sizeof(path), "%s/%s.anims", out_dir, name);
    write_anims(path, cache, &pack, &bakes);
    printf("%s: %d models, %d sequences\n", name, pack.model_count, pack.seq_count);
}

typedef struct {
    int ids[1024];
    int count;
} IdSet;

static void id_add(IdSet* s, int id) {
    if (id <= 0) return;
    for (int i = 0; i < s->count; i++)
        if (s->ids[i] == id) return;
    assert(s->count < 1024);
    s->ids[s->count++] = id;
}

typedef struct {
    IdSet gfx;
    SeqModelSpec raw[256];
    int raw_count;
} ProjectileSet;

static void projectile_add_raw(ProjectileSet* p, int model, int seq) {
    if (model <= 0 || (uint32_t)model >= OSRS_NPC_MODEL_BASE) return;
    for (int i = 0; i < p->raw_count; i++)
        if (p->raw[i].model == model) return;
    assert(p->raw_count < 256);
    p->raw[p->raw_count++] = (SeqModelSpec){seq, model};
}

static void projectile_add_profile(ProjectileSet* p, const OsrsCombatProjectileProfile* pr) {
    id_add(&p->gfx, pr->launch_spotanim_id);
    id_add(&p->gfx, pr->travel_spotanim_id);
    id_add(&p->gfx, pr->impact_spotanim_id);
    projectile_add_raw(p, pr->projectile_model_id, pr->projectile_anim_id);
}

static void projectile_add_row(ProjectileSet* p, const OsrsCombatVisualRow* row) {
    projectile_add_profile(p, &row->projectile);
    id_add(&p->gfx, row->aux_travel_spotanim_id);
    id_add(&p->gfx, row->aux_impact_spotanim_id);
    id_add(&p->gfx, row->double_launch_spotanim_id);
    projectile_add_raw(p, row->aux_projectile_model_id, row->aux_projectile_anim_id);
}

static ProjectileSet projectile_set(void) {
    ProjectileSet p = {0};
    for (size_t i = 0; i < OSRS_COMBAT_VISUAL_ROW_COUNT; i++) projectile_add_row(&p, &OSRS_COMBAT_VISUAL_ROWS[i]);
    for (size_t i = 0; i < OSRS_COMBAT_VISUAL_COLOSSEUM_ROW_COUNT; i++)
        projectile_add_row(&p, &OSRS_COMBAT_VISUAL_COLOSSEUM_ROWS[i]);
    projectile_add_row(&p, &OSRS_COMBAT_VISUAL_VOIDWAKER_SPECIAL_ROW);
    projectile_add_profile(&p, &OSRS_POWERED_STAFF_PROJECTILE_PROFILE);
    projectile_add_profile(&p, &OSRS_TUMEKENS_SHADOW_PROJECTILE_PROFILE);
    static const int gfx[] = {
        GFX_VENGEANCE, GFX_TELEPORT_BREAK, GFX_SPLASH, GFX_RUNE_ARROW_LAUNCH, GFX_TRIDENT_CAST, GFX_TRIDENT_PROJ,
        GFX_TRIDENT_IMPACT, GFX_TUMEKENS_SHADOW_CAST, GFX_TUMEKENS_SHADOW_PROJ, GFX_TUMEKENS_SHADOW_IMPACT,
        GFX_BLOWPIPE_SPEC, GFX_DRAGON_ARROW_LAUNCH, GFX_DRAGON_DART,
        GFX_COLOSSEUM_JAVELIN_SKYFALL_LAUNCH_TRAVEL, GFX_COLOSSEUM_JAVELIN_SKYFALL_DROP_TRAVEL,
        GFX_COLOSSEUM_JAVELIN_SKYFALL_IMPACT, GFX_COLOSSEUM_SOL_SPHERE_TRAVEL, GFX_COLOSSEUM_SOL_SPHERE_IMPACT,
        GFX_COLOSSEUM_VOLATILITY_HUMAN_EXPLOSION, GFX_COLOSSEUM_VOLATILITY_MANTICORE_EXPLOSION,
        GFX_COLOSSEUM_VOLATILITY_COLOSSUS_EXPLOSION, GFX_COLOSSEUM_VOLATILITY_MINOTAUR_EXPLOSION,
        GFX_COLOSSEUM_VOLATILITY_SOL_EXPLOSION,
    };
    for (size_t i = 0; i < sizeof(gfx) / sizeof(*gfx); i++) id_add(&p.gfx, gfx[i]);
    for (int i = 0; i < GFX_COLOSSEUM_SOL_DUST_COUNT; i++) id_add(&p.gfx, GFX_COLOSSEUM_SOL_DUST_BASE + i);
    projectile_add_raw(&p, MODEL_COLOSSEUM_SOL_CRYSTAL_CHARGE, ANIM_COLOSSEUM_SOL_CRYSTAL_CHARGE);
    projectile_add_raw(&p, MODEL_COLOSSEUM_SOL_SAND_PILLAR, ANIM_COLOSSEUM_SOL_SAND_PILLAR);
    return p;
}

static int compare_gfx_row(const void* a, const void* b) {
    uint32_t x = ((const OsrsSpotAnimDef*)a)->id, y = ((const OsrsSpotAnimDef*)b)->id;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void write_spotanims_bin(const char* out_dir, CacheGroup* spotanim_files) {
    OsrsSpotAnimDef* rows = calloc((size_t)spotanim_files->file_count, sizeof(OsrsSpotAnimDef));
    int count = 0;
    for (int i = 0; i < spotanim_files->file_count; i++) {
        SpotAnimDef d = spotanim_decode(&spotanim_files->files[i]);
        if (d.model_id < 0) continue;
        rows[count].id = (uint32_t)spotanim_files->file_ids[i];
        rows[count].model_id = d.model_id;
        rows[count].animation_id = d.sequence;
        rows[count].resize_xy = (uint32_t)d.width_scale;
        rows[count].resize_z = (uint32_t)d.height_scale;
        rows[count].rotation = (uint32_t)d.rotation;
        rows[count].brightness = d.ambient;
        rows[count].shadow = d.contrast;
        count++;
    }
    qsort(rows, (size_t)count, sizeof(OsrsSpotAnimDef), compare_gfx_row);
    char path[4096];
    snprintf(path, sizeof(path), "%s/spotanims.bin", out_dir);
    FILE* f = fopen(path, "wb");
    assert(f);
    uint32_t header[3] = {OSRS_SPOTANIM_MAGIC, OSRS_SPOTANIM_VERSION, (uint32_t)count};
    fwrite(header, sizeof(header), 1, f);
    fwrite(rows, sizeof(OsrsSpotAnimDef), (size_t)count, f);
    fclose(f);
    free(rows);
}

static void export_projectiles(OsrsCache* cache, const char* out_dir) {
    Defs defs = {.cache = cache};
    defs_load_floors(&defs);
    CacheGroup gfx_files = cache_read_group(cache, CACHE_INDEX_CONFIGS, CACHE_CONFIG_SPOTANIM);
    NpcPack pack = {0};
    SeqBakeTable bakes = {0};
    ProjectileSet set = projectile_set();
    for (int i = 0; i < set.gfx.count; i++) pack_spotanim(&defs, &gfx_files, &pack, &bakes, set.gfx.ids[i]);
    for (int i = 0; i < set.raw_count; i++) {
        Model m = build_merged(&defs, &set.raw[i].model, 1);
        bind_seq(&bakes, set.raw[i].seq, skin_copy(&m), 128, 128);
        pack_model(&pack, set.raw[i].model, m, 64, 850, 128, 128);
        pack_seq(&pack, set.raw[i].seq);
    }
    char path[4096];
    snprintf(path, sizeof(path), "%s/projectiles.models", out_dir);
    write_models(path, &defs, &pack);
    snprintf(path, sizeof(path), "%s/projectiles.anims", out_dir);
    write_anims(path, cache, &pack, &bakes);
    write_spotanims_bin(out_dir, &gfx_files);
    printf("projectiles: %d spotanims, %d raw models, %d sequences\n", set.gfx.count, set.raw_count, pack.seq_count);
}

enum {
    ITEM_MODEL_BASE = 0xE0000,
    BODY_MODEL_BASE = 0xF0000,
    EQUIP_RENDER_FLAG_TWO_HANDED = 1u,
    EQUIP_RENDER_FLAG_WEARPOS_AUTHORITY = 2u,
    EQUIP_MISSING = 0xFFFFFFFFu,
};

enum { BP_HEAD = 0, BP_JAW, BP_TORSO, BP_ARMS, BP_HANDS, BP_LEGS, BP_FEET, BODY_PART_COUNT };
static const char* const BODY_PART_NAMES[BODY_PART_COUNT] = {"HEAD", "JAW", "TORSO", "ARMS", "HANDS", "LEGS", "FEET"};

static const int DEFAULT_MALE_KITS[BODY_PART_COUNT] = {0, 10, 18, 26, 34, 36, 42};

enum {
    WEARPOS_HAT = 0, WEARPOS_BACK = 1, WEARPOS_FRONT = 2, WEARPOS_RIGHT_HAND = 3, WEARPOS_TORSO = 4,
    WEARPOS_LEFT_HAND = 5, WEARPOS_ARMS = 6, WEARPOS_LEGS = 7, WEARPOS_HEAD = 8, WEARPOS_HANDS = 9,
    WEARPOS_FEET = 10, WEARPOS_JAW = 11,
};

static const struct { int item_id, ready, walk, run; } STANCE_OVERRIDES[] = {
    {11802, 7053, 7052, 7043}, {26233, 7053, 7052, 7043}, {11806, 7053, 7052, 7043},
    {4153, 1662, 1663, 1664}, {24225, 1662, 1663, 1664}, {19481, 7220, 7223, 7221},
    {27690, 244, 247, 248}, {4718, 2065, 2064, -1},
};

static void stance_override(int item_id, int* ready, int* walk, int* run) {
    *ready = -1, *walk = -1, *run = -1;
    for (size_t i = 0; i < sizeof(STANCE_OVERRIDES) / sizeof(STANCE_OVERRIDES[0]); i++)
        if (STANCE_OVERRIDES[i].item_id == item_id) {
            *ready = STANCE_OVERRIDES[i].ready, *walk = STANCE_OVERRIDES[i].walk, *run = STANCE_OVERRIDES[i].run;
            return;
        }
}

static uint32_t equip_hide_mask(const ItemDef* item, int equip_slot) {
    uint32_t mask = 0;
    int wearpos[3] = {item->wearpos1, item->wearpos2, item->wearpos3};
    for (int i = 0; i < 3; i++) {
        if (wearpos[i] == WEARPOS_TORSO) mask |= 1u << BP_TORSO;
        else if (wearpos[i] == WEARPOS_ARMS) mask |= 1u << BP_ARMS;
        else if (wearpos[i] == WEARPOS_LEGS) mask |= 1u << BP_LEGS;
        else if (wearpos[i] == WEARPOS_HEAD) mask |= 1u << BP_HEAD;
        else if (wearpos[i] == WEARPOS_HANDS) mask |= 1u << BP_HANDS;
        else if (wearpos[i] == WEARPOS_FEET) mask |= 1u << BP_FEET;
        else if (wearpos[i] == WEARPOS_JAW) mask |= 1u << BP_JAW;
    }
    int has_wearpos = item->wearpos1 >= 0 || item->wearpos2 >= 0 || item->wearpos3 >= 0;
    if (mask == 0 && !has_wearpos) {
        if (equip_slot == SLOT_BODY) mask |= 1u << BP_TORSO;
        else if (equip_slot == SLOT_LEGS) mask |= 1u << BP_LEGS;
        else if (equip_slot == SLOT_HANDS) mask |= 1u << BP_HANDS;
        else if (equip_slot == SLOT_FEET) mask |= 1u << BP_FEET;
    }
    return mask;
}

static uint32_t equip_render_flags(const ItemDef* item, int equip_slot, int two_handed) {
    uint32_t flags = 0;
    if (item->wearpos1 >= 0 || item->wearpos2 >= 0 || item->wearpos3 >= 0) flags |= EQUIP_RENDER_FLAG_WEARPOS_AUTHORITY;
    if (equip_slot == SLOT_WEAPON &&
        (two_handed || item->wearpos2 == WEARPOS_LEFT_HAND || item->wearpos3 == WEARPOS_LEFT_HAND))
        flags |= EQUIP_RENDER_FLAG_TWO_HANDED;
    return flags;
}

static uint32_t u32_or_missing(int v) { return v < 0 ? EQUIP_MISSING : (uint32_t)v; }

typedef struct {
    uint16_t item_id;
    uint32_t inv_model, wield_model, hide_mask, equip_slot;
    uint32_t wearpos1, wearpos2, wearpos3, render_flags, ready, walk, run;
} EquipRow;

static void pack_equipment_seqs(NpcPack* p) {
    static const int fixed[] = {
        ANIM_SEQ_IDLE, ANIM_SEQ_WALK, ANIM_SEQ_RUN, ANIM_SEQ_EAT, ANIM_SEQ_DEATH, ANIM_SEQ_CAST_STANDARD,
        ANIM_SEQ_CAST_BARRAGE, ANIM_SEQ_CAST_VENG, ANIM_SEQ_TAB_BREAK, ANIM_SEQ_BLOCK_SHIELD, ANIM_SEQ_BLOCK_MELEE,
        OSRS_PLAYER_UNARMED_ATTACK_ANIM, OSRS_PLAYER_POWERED_STAFF_ATTACK_ANIM,
    };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(*fixed); i++) pack_seq(p, fixed[i]);
    pack_seq(p, OSRS_COMBAT_VISUAL_VOIDWAKER_SPECIAL_ROW.attack_anim_id);
    for (size_t i = 0; i < OSRS_COMBAT_VISUAL_ROW_COUNT; i++)
        if (OSRS_COMBAT_VISUAL_ROWS[i].kind != OSRS_COMBAT_VISUAL_KIND_NPC) pack_seq(p, OSRS_COMBAT_VISUAL_ROWS[i].attack_anim_id);
    for (size_t i = 0; i < sizeof(OSRS_COMBAT_SPECIAL_FALLBACKS) / sizeof(*OSRS_COMBAT_SPECIAL_FALLBACKS); i++)
        pack_seq(p, OSRS_COMBAT_SPECIAL_FALLBACKS[i].special_attack_anim_id);
}


static void write_item_model_header(const char* out_dir, const EquipRow* rows, int row_count) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/item_models.h", out_dir);
    FILE* h = fopen(path, "w");
    assert(h);
    fprintf(h, "/* generated by ocean/osrs/tools/osrs_export.c equipment, do not edit */\n");
    fprintf(h, "#ifndef ITEM_MODELS_H\n#define ITEM_MODELS_H\n\n#include <stdint.h>\n\n");
    fprintf(h, "#define ITEM_RENDER_MODEL_MISSING 0xFFFFFFFFu\n");
    fprintf(h, "#define ITEM_RENDER_FLAG_TWO_HANDED %uu\n", EQUIP_RENDER_FLAG_TWO_HANDED);
    fprintf(h, "#define ITEM_RENDER_FLAG_WEARPOS_AUTHORITY %uu\n\n", EQUIP_RENDER_FLAG_WEARPOS_AUTHORITY);
    fprintf(h, "typedef struct {\n"
               "    uint16_t item_id;\n"
               "    uint32_t inv_model;\n"
               "    uint32_t wield_model;\n"
               "    uint32_t hide_body_mask;\n"
               "    uint32_t equip_slot;\n"
               "    uint32_t wearpos1;\n"
               "    uint32_t wearpos2;\n"
               "    uint32_t wearpos3;\n"
               "    uint32_t render_flags;\n"
               "    uint32_t ready_anim_id;\n"
               "    uint32_t walk_anim_id;\n"
               "    uint32_t run_anim_id;\n"
               "} ItemModelMapping;\n\n");
    fprintf(h, "#define ITEM_MODEL_COUNT %d\n\n", row_count);
    fprintf(h, "static const ItemModelMapping ITEM_MODEL_MAP[] = {\n");
    for (int i = 0; i < row_count; i++) {
        const EquipRow* r = &rows[i];
        fprintf(h, "    { %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u },\n", r->item_id, r->inv_model,
            r->wield_model, r->hide_mask, r->equip_slot, r->wearpos1, r->wearpos2, r->wearpos3, r->render_flags,
            r->ready, r->walk, r->run);
    }
    fprintf(h, "};\n\n#endif\n");
    fclose(h);
}

static void write_player_model_header(const char* out_dir) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/player_models.h", out_dir);
    FILE* h = fopen(path, "w");
    assert(h);
    fprintf(h, "/* generated by ocean/osrs/tools/osrs_export.c equipment, do not edit */\n");
    fprintf(h, "#ifndef PLAYER_MODELS_H\n#define PLAYER_MODELS_H\n\n#include <stdint.h>\n\n");
    for (int bp = 0; bp < BODY_PART_COUNT; bp++) fprintf(h, "#define BODY_PART_%s %d\n", BODY_PART_NAMES[bp], bp);
    fprintf(h, "#define BODY_PART_COUNT %d\n\n", BODY_PART_COUNT);
    fprintf(h, "static const uint32_t DEFAULT_BODY_MODELS[BODY_PART_COUNT] = {\n");
    for (int bp = 0; bp < BODY_PART_COUNT; bp++)
        fprintf(h, "    0x%X,  /* %s */\n", BODY_MODEL_BASE + bp, BODY_PART_NAMES[bp]);
    fprintf(h, "};\n\n#endif /* PLAYER_MODELS_H */\n");
    fclose(h);
}

static void export_equipment(OsrsCache* cache, const char* out_dir) {
    Defs defs = {.cache = cache};
    defs_load_floors(&defs);
    NpcPack pack = {0};

    CacheGroup idk_files = cache_read_group(cache, CACHE_INDEX_CONFIGS, CACHE_CONFIG_IDK);
    for (int bp = 0; bp < BODY_PART_COUNT; bp++) {
        const CacheFile* kf = cache_group_file(&idk_files, DEFAULT_MALE_KITS[bp]);
        assert(kf);
        IdentityKitDef kit = kit_decode(kf);
        assert(kit.model_count > 0);
        Model merged = build_merged(&defs, kit.model_ids, kit.model_count);
        for (int i = 0; i < kit.recolor_count; i++) model_recolor(&merged, kit.recolor_from[i], kit.recolor_to[i]);
        for (int i = 0; i < kit.retexture_count; i++) model_retexture(&merged, kit.retexture_from[i], kit.retexture_to[i]);
        pack_model(&pack, BODY_MODEL_BASE + bp, merged, 64, 850, 128, 128);
    }

    CacheGroup obj_files = cache_read_group(cache, CACHE_INDEX_CONFIGS, CACHE_CONFIG_OBJ);
    EquipRow* rows = calloc((size_t)NUM_ITEMS, sizeof(EquipRow));
    for (int i = 0; i < NUM_ITEMS; i++) {
        uint16_t item_id = ITEM_DATABASE[i].item_id;
        const CacheFile* f = cache_group_file(&obj_files, item_id);
        assert(f);
        ItemDef item = item_decode(f);

        int wield_ids[3], wield_count = 0;
        for (int k = 0; k < 3; k++)
            if (item.male_model[k] >= 0) wield_ids[wield_count++] = item.male_model[k];
        uint32_t wield_model = EQUIP_MISSING;
        if (wield_count) {
            Model merged = build_merged(&defs, wield_ids, wield_count);
            for (int r = 0; r < item.recolor_count; r++) model_recolor(&merged, item.recolor_from[r], item.recolor_to[r]);
            for (int r = 0; r < item.retexture_count; r++)
                model_retexture(&merged, item.retexture_from[r], item.retexture_to[r]);
            if (item.male_offset) model_translate(&merged, 0, item.male_offset, 0);
            wield_model = (uint32_t)(ITEM_MODEL_BASE + i);
            pack_model(&pack, (int)wield_model, merged, 64, 850, 128, 128);
        }

        int equip_slot = ITEM_DATABASE[i].slot;
        int ready, walk, run;
        stance_override(item_id, &ready, &walk, &run);
        rows[i] = (EquipRow){
            .item_id = item_id,
            .inv_model = u32_or_missing(item.inv_model),
            .wield_model = wield_model,
            .hide_mask = equip_hide_mask(&item, equip_slot),
            .equip_slot = (uint32_t)equip_slot,
            .wearpos1 = u32_or_missing(item.wearpos1),
            .wearpos2 = u32_or_missing(item.wearpos2),
            .wearpos3 = u32_or_missing(item.wearpos3),
            .render_flags = equip_render_flags(&item, equip_slot, ITEM_DATABASE[i].two_handed),
            .ready = u32_or_missing(ready),
            .walk = u32_or_missing(walk),
            .run = u32_or_missing(run),
        };
        pack_seq(&pack, ready);
        pack_seq(&pack, walk);
        pack_seq(&pack, run);
    }
    pack_equipment_seqs(&pack);

    char path[4096];
    snprintf(path, sizeof(path), "%s/equipment.models", out_dir);
    write_models(path, &defs, &pack);
    snprintf(path, sizeof(path), "%s/equipment.anims", out_dir);
    write_anims(path, cache, &pack, &(SeqBakeTable){0});
    write_item_model_header(out_dir, rows, NUM_ITEMS);
    write_player_model_header(out_dir);
    printf("equipment: %d body parts, %d items, %d models, %d sequences\n", BODY_PART_COUNT, NUM_ITEMS,
        pack.model_count, pack.seq_count);
    free(rows);
}

static void ensure_dir(const char* path) {
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char* p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        mkdir(buf, 0755);
        *p = '/';
    }
    mkdir(buf, 0755);
}

static uint32_t png_crc_table[256];

__attribute__((constructor)) static void png_build_crc_table(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        png_crc_table[n] = c;
    }
}

static uint32_t png_crc(const uint8_t* buf, size_t len) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = png_crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void png_write_u32(FILE* f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    fwrite(b, 1, 4, f);
}

static void png_write_chunk(FILE* f, const char* type, const uint8_t* data, uint32_t len) {
    png_write_u32(f, len);
    uint8_t* buf = malloc(4 + (size_t)len);
    memcpy(buf, type, 4);
    if (len) memcpy(buf + 4, data, len);
    fwrite(buf, 1, 4 + (size_t)len, f);
    png_write_u32(f, png_crc(buf, 4 + (size_t)len));
    free(buf);
}

static void png_write_rgba(const char* path, const uint32_t* pixels, int width, int height) {
    size_t stride = 1 + (size_t)width * 4;
    uint8_t* raw = malloc((size_t)height * stride);
    for (int y = 0; y < height; y++) {
        uint8_t* row = raw + (size_t)y * stride;
        row[0] = 0;
        memcpy(row + 1, pixels + (size_t)y * width, (size_t)width * 4);
    }
    uLongf bound = compressBound((uLong)((size_t)height * stride));
    uint8_t* comp = malloc(bound);
    assert(compress2(comp, &bound, raw, (uLong)((size_t)height * stride), 6) == Z_OK);
    FILE* f = fopen(path, "wb");
    assert(f);
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13] = {
        (uint8_t)(width >> 24), (uint8_t)(width >> 16), (uint8_t)(width >> 8), (uint8_t)width,
        (uint8_t)(height >> 24), (uint8_t)(height >> 16), (uint8_t)(height >> 8), (uint8_t)height,
        8, 6, 0, 0, 0,
    };
    png_write_chunk(f, "IHDR", ihdr, sizeof(ihdr));
    png_write_chunk(f, "IDAT", comp, (uint32_t)bound);
    png_write_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(comp);
}

typedef struct {
    const char* name;
    int sprite_id;
    int frame;
    int masked;
} GuiSpriteEntry;

static const GuiSpriteEntry GUI_SPRITE_TABLE[] = {
#include "ocean/osrs/tools/osrs_gui_sprite_table_generated.inc"
};

static const int COLOSSEUM_MODIFIER_SPRITE_IDS[] = {
    5544, 5559, 5574, 5538, 5553, 5568, 5543, 5558, 5573, 5545,
    5541, 5556, 5571, 5539, 5554, 5569, 5547, 5562, 5577, 5536,
    5551, 5566, 5540, 5535, 5550, 5565, 5537, 5552, 5567, 5546,
    5542, 5534, 5549, 5564,
};

static SpriteFrame sprite_frame_read(OsrsCache* cache, int sprite_id, int frame) {
    CacheFile f = cache_read_container(cache, CACHE_INDEX_SPRITES, sprite_id);
    assert(f.data);
    SpriteGroup g = sprite_group_decode(f.data, f.size, 1.0, SPRITE_ALPHA_CHANNEL);
    assert(frame < g.count);
    SpriteFrame out = g.frames[frame];
    g.frames[frame] = (SpriteFrame){0};
    sprite_group_free(&g);
    free(f.data);
    return out;
}

static void sprite_frame_whiten(SpriteFrame* frame) {
    for (int i = 0; i < frame->width * frame->height; i++) {
        uint32_t a = frame->pixels[i] >> 24 & 0xFF;
        frame->pixels[i] = a << 24 | (a ? 0x00FFFFFFu : 0);
    }
}

static void export_gui_sprites(OsrsCache* cache, const char* out_dir) {
    char dir[4096];
    snprintf(dir, sizeof(dir), "%s/sprites/gui", out_dir);
    ensure_dir(dir);
    int n = (int)(sizeof(GUI_SPRITE_TABLE) / sizeof(GUI_SPRITE_TABLE[0]));
    for (int i = 0; i < n; i++) {
        const GuiSpriteEntry* e = &GUI_SPRITE_TABLE[i];
        SpriteFrame frame = sprite_frame_read(cache, e->sprite_id, e->frame);
        if (e->masked) sprite_frame_whiten(&frame);
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s.png", dir, e->name);
        png_write_rgba(path, frame.pixels, frame.width, frame.height);
        free(frame.pixels);
    }
    printf("sprites/gui: %d files\n", n);

    char mod_dir[4096];
    snprintf(mod_dir, sizeof(mod_dir), "%s/sprites/colosseum/modifiers", out_dir);
    ensure_dir(mod_dir);
    int m = (int)(sizeof(COLOSSEUM_MODIFIER_SPRITE_IDS) / sizeof(COLOSSEUM_MODIFIER_SPRITE_IDS[0]));
    for (int i = 0; i < m; i++) {
        int id = COLOSSEUM_MODIFIER_SPRITE_IDS[i];
        SpriteFrame frame = sprite_frame_read(cache, id, 0);
        char path[4096];
        snprintf(path, sizeof(path), "%s/%d.png", mod_dir, id);
        png_write_rgba(path, frame.pixels, frame.width, frame.height);
        free(frame.pixels);
    }
    printf("sprites/colosseum/modifiers: %d files\n", m);
}

typedef struct {
    int id;
    const char* name;
} CoreInterfaceGroup;

static const CoreInterfaceGroup CORE_INTERFACE_GROUPS[] = {
    {161, "toplevel_osrs_stretch"}, {162, "chatbox"}, {160, "orbs"}, {303, "hpbar_hud"},
    {90, "pvp_icons"}, {651, "buff_bar"}, {708, "stat_boosts_hud"}, {163, "pm_chat"},
    {122, "xp_drops"}, {149, "inventory"}, {320, "stats"}, {119, "questjournal"},
    {629, "side_journal"}, {387, "wornitems"}, {541, "prayerbook"}, {218, "magic_spellbook"},
    {429, "friends"}, {109, "account"}, {182, "logout"}, {116, "settings_side"},
    {216, "emote"}, {239, "music"}, {707, "side_channels"}, {593, "combat_interface"},
    {84, "equipment"}, {85, "equipment_side"}, {238, "ge_pricechecker_side"}, {4, "deathkeep"},
};

static void bin_u8(FILE* f, int v) { uint8_t b = (uint8_t)v; fwrite(&b, 1, 1, f); }

static void bin_u32(FILE* f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}

static void bin_i32(FILE* f, int32_t v) { bin_u32(f, (uint32_t)v); }

static void bin_string(FILE* f, const char* s) {
    size_t len = s ? strlen(s) : 0;
    if (len > 0xFFFF) len = 0xFFFF;
    uint8_t lb[2] = {(uint8_t)len, (uint8_t)(len >> 8)};
    fwrite(lb, 1, 2, f);
    if (len) fwrite(s, 1, len, f);
}

static void bin_listener_value(FILE* f, const IfListenerValue* v) {
    if (v->is_string) {
        bin_u8(f, 1);
        bin_string(f, v->str_value);
    } else {
        bin_u8(f, 0);
        bin_i32(f, v->int_value);
    }
}

static void bin_component(FILE* f, const IfComponent* c) {
    bin_u32(f, c->id);
    bin_i32(f, c->parent_id);
    bin_u32(f, c->group_id);
    bin_u32(f, c->file_id);
    bin_u8(f, c->is_if3 ? 1 : 0);
    bin_u8(f, c->type);
    bin_u8(f, c->hidden ? 1 : 0);
    bin_u8(f, c->sprite_tiling ? 1 : 0);
    bin_u8(f, c->filled ? 1 : 0);
    bin_u8(f, c->line_direction ? 1 : 0);
    bin_u8(f, c->text_shadowed ? 1 : 0);
    bin_u8(f, c->flipped_vertically ? 1 : 0);
    bin_u8(f, c->flipped_horizontally ? 1 : 0);
    bin_u8(f, c->no_click_through ? 1 : 0);
    bin_u8(f, c->opacity);
    bin_u8(f, c->border_type);
    bin_u8(f, c->line_width);
    bin_u8(f, c->line_height);
    bin_i32(f, c->content_type);
    bin_i32(f, c->x);
    bin_i32(f, c->y);
    bin_i32(f, c->width);
    bin_i32(f, c->height);
    bin_i32(f, c->width_mode);
    bin_i32(f, c->height_mode);
    bin_i32(f, c->x_position_mode);
    bin_i32(f, c->y_position_mode);
    bin_i32(f, c->scroll_width);
    bin_i32(f, c->scroll_height);
    bin_i32(f, c->sprite_id);
    bin_i32(f, c->texture_id);
    bin_i32(f, c->shadow_color);
    bin_i32(f, c->model_id);
    bin_i32(f, c->model_type);
    bin_i32(f, c->font_id);
    bin_i32(f, c->text_color);
    bin_u32(f, c->click_mask);
    bin_i32(f, c->x_text_alignment);
    bin_i32(f, c->y_text_alignment);
    bin_string(f, c->name);
    bin_string(f, c->text);
    bin_string(f, c->target_verb);
    int action_count = c->action_count < 255 ? c->action_count : 255;
    bin_u8(f, action_count);
    for (int i = 0; i < action_count; i++) bin_string(f, c->actions[i]);

    int listener_count = 0;
    for (int i = 0; i < IF_LISTENER_COUNT; i++) listener_count += c->listeners[i].present;
    bin_u8(f, listener_count);
    for (int i = 0; i < IF_LISTENER_COUNT; i++) {
        if (!c->listeners[i].present) continue;
        bin_u8(f, i);
        int n = c->listeners[i].count < 255 ? c->listeners[i].count : 255;
        bin_u8(f, n);
        for (int j = 0; j < n; j++) bin_listener_value(f, &c->listeners[i].values[j]);
    }

    int trigger_count = 0;
    for (int i = 0; i < IF_TRIGGER_COUNT; i++) trigger_count += c->triggers[i].present;
    bin_u8(f, trigger_count);
    for (int i = 0; i < IF_TRIGGER_COUNT; i++) {
        if (!c->triggers[i].present) continue;
        bin_u8(f, i);
        int n = c->triggers[i].count < 255 ? c->triggers[i].count : 255;
        bin_u8(f, n);
        for (int j = 0; j < n; j++) bin_i32(f, c->triggers[i].values[j]);
    }
}

static void export_interfaces(OsrsCache* cache, const char* out_dir) {
    char dir[4096];
    snprintf(dir, sizeof(dir), "%s/ui", out_dir);
    ensure_dir(dir);
    char path[4096];
    snprintf(path, sizeof(path), "%s/interfaces.bin", dir);
    FILE* f = fopen(path, "wb");
    assert(f);
    fwrite("RCUIBIN2", 1, 8, f);
    bin_u32(f, 2);
    int group_count = (int)(sizeof(CORE_INTERFACE_GROUPS) / sizeof(CORE_INTERFACE_GROUPS[0]));
    bin_u32(f, (uint32_t)group_count);

    int total_components = 0, total_if3 = 0;
    for (int gi = 0; gi < group_count; gi++) {
        int group_id = CORE_INTERFACE_GROUPS[gi].id;
        CacheGroup g = cache_read_group(cache, CACHE_INDEX_INTERFACES, group_id);
        bin_u32(f, (uint32_t)group_id);
        bin_string(f, CORE_INTERFACE_GROUPS[gi].name);
        bin_u32(f, (uint32_t)g.file_count);
        int* order = malloc(sizeof(int) * (size_t)g.file_count);
        for (int i = 0; i < g.file_count; i++) order[i] = i;
        for (int i = 0; i < g.file_count; i++)
            for (int j = i + 1; j < g.file_count; j++)
                if (g.file_ids[order[j]] < g.file_ids[order[i]]) {
                    int t = order[i];
                    order[i] = order[j];
                    order[j] = t;
                }
        for (int k = 0; k < g.file_count; k++) {
            int i = order[k];
            uint32_t component_id = ((uint32_t)group_id << 16) | (uint32_t)g.file_ids[i];
            IfComponent c = if_component_decode(component_id, &g.files[i]);
            bin_component(f, &c);
            total_components++;
            total_if3 += c.is_if3;
            if_component_free(&c);
        }
        free(order);
        cache_group_free(&g);
    }
    fclose(f);
    printf("ui/interfaces.bin: %d groups, %d components (%d IF3, %d IF1)\n",
        group_count, total_components, total_if3, total_components - total_if3);
}

static void export_collision(OsrsCache* cache, const char* out_dir, const char* name, RegionRect r) {
    Defs defs = {.cache = cache};
    MapGrid g = map_load(cache, r.x0 - 1, r.y0 - 1, r.x1 + 1, r.y1 + 1);
    int* flags = build_collision(&defs, &g);
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s.cmap", out_dir, name);
    write_collision(path, &g, flags, r);
    free(flags);
    map_free(&g);
}

static void usage(void) {
    fprintf(stderr,
        "usage: osrs_export <cache_dir> <out_dir> scene <name> <rx0,ry0> [rx1,ry1] [altobjects=NAME:ID,ID,...]\n"
        "       osrs_export <cache_dir> <out_dir> collision <name> <rx0,ry0> [rx1,ry1]\n"
        "       osrs_export <cache_dir> <out_dir> npcs <name> "
        "[npc=ID[:ATTACK_SEQ[:OWNED_SEQ]...]] [gfx=ID] [loc=ID] [seq=ID] [seqmodel=SEQ_ID:MODEL_ID]...\n"
        "       osrs_export <cache_dir> <out_dir> projectiles\n"
        "       osrs_export <cache_dir> <out_dir> equipment\n"
        "       osrs_export <cache_dir> <out_dir> textures\n"
        "       osrs_export <cache_dir> <out_dir> sprites\n"
        "       osrs_export <cache_dir> <out_dir> interfaces\n");
    exit(2);
}

int main(int argc, char** argv) {
    if (argc < 4) usage();
    OsrsCache* cache = osrs_cache_open(argv[1]);
    const char* out_dir = argv[2];
    int is_scene = strcmp(argv[3], "scene") == 0, is_collision = strcmp(argv[3], "collision") == 0;
    if ((is_scene || is_collision) && argc >= 6 && argc <= 8) {
        RegionRect r;
        if (sscanf(argv[5], "%d,%d", &r.x0, &r.y0) != 2) usage();
        r.x1 = r.x0, r.y1 = r.y0;
        int next = 6;
        if (argc >= 7 && sscanf(argv[6], "%d,%d", &r.x1, &r.y1) == 2) next = 7;
        else r.x1 = r.x0, r.y1 = r.y0;
        AltObjects alt = {0};
        int has_alt = 0;
        if (argc == next + 1) {
            if (!is_scene) usage();
            char alt_name[256];
            const char* spec = argv[next];
            if (sscanf(spec, "altobjects=%255[^:]:", alt_name) != 1) usage();
            alt.name = strdup(alt_name);
            const char* ids = strchr(spec, ':');
            if (!ids) usage();
            ids++;
            while (*ids) {
                int id, n = 0;
                if (sscanf(ids, "%d%n", &id, &n) != 1) usage();
                alt.ids[alt.count++] = id;
                ids += n;
                if (*ids == ',') ids++;
            }
            has_alt = 1;
        } else if (argc != next) {
            usage();
        }
        if (is_scene) export_scene(cache, out_dir, argv[4], r, has_alt ? &alt : NULL);
        else export_collision(cache, out_dir, argv[4], r);
        return 0;
    }
    if (strcmp(argv[3], "npcs") == 0 && argc >= 5) {
        static PackSpec spec;
        for (int i = 5; i < argc; i++) {
            int id, extra = -1;
            if (strncmp(argv[i], "npc=", 4) == 0) {
                assert(spec.npc_count < 256);
                NpcSpec* n = &spec.npcs[spec.npc_count++];
                *n = (NpcSpec){.attack = -1};
                char* tok = strtok(argv[i] + 4, ":");
                assert(tok);
                n->npc = atoi(tok);
                if ((tok = strtok(NULL, ":"))) n->attack = atoi(tok);
                while ((tok = strtok(NULL, ":"))) {
                    assert(n->owned_count < 8);
                    n->owned[n->owned_count++] = atoi(tok);
                }
            } else if (sscanf(argv[i], "gfx=%d", &id) == 1) spec.gfx[spec.gfx_count++] = id;
            else if (sscanf(argv[i], "loc=%d", &id) == 1) spec.locs[spec.loc_count++] = id;
            else if (sscanf(argv[i], "seqmodel=%d:%d", &id, &extra) == 2) spec.seqmodels[spec.seqmodel_count++] = (SeqModelSpec){id, extra};
            else if (sscanf(argv[i], "seq=%d", &id) == 1) spec.seqs[spec.seq_count++] = id;
            else usage();
        }
        export_npcs(cache, out_dir, argv[4], &spec);
        return 0;
    }
    if (strcmp(argv[3], "projectiles") == 0 && argc == 4) {
        export_projectiles(cache, out_dir);
        return 0;
    }
    if (strcmp(argv[3], "textures") == 0 && argc == 4) {
        export_textures(cache, out_dir);
        return 0;
    }
    if (strcmp(argv[3], "equipment") == 0) {
        export_equipment(cache, out_dir);
        return 0;
    }
    if (strcmp(argv[3], "sprites") == 0 && argc == 4) {
        export_gui_sprites(cache, out_dir);
        return 0;
    }
    if (strcmp(argv[3], "interfaces") == 0 && argc == 4) {
        export_interfaces(cache, out_dir);
        return 0;
    }
    usage();
}
