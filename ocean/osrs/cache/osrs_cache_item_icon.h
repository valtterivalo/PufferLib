#ifndef OSRS_CACHE_ITEM_ICON_H
#define OSRS_CACHE_ITEM_ICON_H

#include "osrs_cache_item.h"
#include "osrs_cache_map.h"
#include "osrs_cache_sprites.h"

enum {
    ICON_W = 36,
    ICON_H = 32,
    ICON_CENTER = 16,
    ICON_ZOOM = 512,
    ICON_TEXTURE_SIZE = 128,
    ICON_SHADOW = 0x303030,
    ICON_MAX_DIAMETER = 6000,
    ICON_PRIORITIES = 12,
};

#define ICON_BRIGHTNESS 0.6

static int icon_palette[65536];

__attribute__((constructor)) static void icon_build_palette(void) { model_palette_build(icon_palette, ICON_BRIGHTNESS); }

typedef enum { ICON_PLAIN, ICON_NOTED } IconNote;

typedef struct {
    OsrsCache* cache;
    CacheGroup objs;
    TextureDef* textures;
    int texture_count;
    int32_t** texture_pixels;
} IconSource;

typedef struct {
    int32_t pixels[ICON_W * ICON_H];
    int clipped;
    int alpha;
} IconRaster;

static IconSource icon_source_open(OsrsCache* cache) {
    IconSource s = {.cache = cache, .objs = cache_read_group(cache, CACHE_INDEX_CONFIGS, CACHE_CONFIG_OBJ)};
    CacheGroup t = cache_read_group(cache, CACHE_INDEX_TEXTURES, 0);
    s.texture_count = t.file_ids[t.file_count - 1] + 1;
    s.textures = calloc((size_t)s.texture_count, sizeof(TextureDef));
    s.texture_pixels = calloc((size_t)s.texture_count, sizeof(int32_t*));
    for (int i = 0; i < t.file_count; i++) s.textures[t.file_ids[i]] = texture_decode(&t.files[i]);
    cache_group_free(&t);
    return s;
}

static const int32_t* icon_texture(IconSource* s, int id) {
    assert(id < s->texture_count);
    if (!s->textures[id].present) return NULL;
    if (s->texture_pixels[id]) return s->texture_pixels[id];
    CacheFile f = cache_read_file(s->cache, CACHE_INDEX_SPRITES, s->textures[id].sprite, 0);
    assert(f.data);
    SpriteGroup g = sprite_group_decode(f.data, f.size, ICON_BRIGHTNESS, SPRITE_ALPHA_INDEX);
    const SpriteFrame* frame = &g.frames[0];
    int shift = frame->width == ICON_TEXTURE_SIZE ? 0 : 1;
    assert(frame->width == frame->height && frame->width << shift == ICON_TEXTURE_SIZE);
    int32_t* out = malloc(sizeof(int32_t) * ICON_TEXTURE_SIZE * ICON_TEXTURE_SIZE);
    for (int y = 0; y < ICON_TEXTURE_SIZE; y++)
        for (int x = 0; x < ICON_TEXTURE_SIZE; x++) {
            uint32_t p = frame->pixels[(y >> shift) * frame->width + (x >> shift)];
            out[y * ICON_TEXTURE_SIZE + x] = (int32_t)((p & 0xFFu) << 16 | (p & 0xFF00u) | (p >> 16 & 0xFFu));
        }
    sprite_group_free(&g);
    free(f.data);
    return s->texture_pixels[id] = out;
}

static ItemDef icon_item(const IconSource* s, int id) {
    ItemDef d = item_decode(cache_group_file(&s->objs, id));
    int model_from = d.note_template != -1 ? d.note_template : d.bought_template != -1 ? d.bought_template
        : d.placeholder_template;
    if (model_from == -1) return d;
    ItemDef m = item_decode(cache_group_file(&s->objs, model_from));
    ItemDef c = d.bought_template != -1 && d.note_template == -1 ? item_decode(cache_group_file(&s->objs, d.bought)) : m;
    d.inv_model = m.inv_model, d.zoom2d = m.zoom2d, d.xan2d = m.xan2d, d.yan2d = m.yan2d, d.zan2d = m.zan2d;
    d.xoff2d = m.xoff2d, d.yoff2d = m.yoff2d;
    d.recolor_count = c.recolor_count, d.retexture_count = c.retexture_count;
    memcpy(d.recolor_from, c.recolor_from, sizeof(d.recolor_from));
    memcpy(d.recolor_to, c.recolor_to, sizeof(d.recolor_to));
    memcpy(d.retexture_from, c.retexture_from, sizeof(d.retexture_from));
    memcpy(d.retexture_to, c.retexture_to, sizeof(d.retexture_to));
    return d;
}

typedef void (*IconSpan)(IconRaster* r, const void* ctx, int y, int x0, int x1);

static void icon_scan(IconRaster* r, const int y[3], const int x[3], IconSpan span, const void* ctx) {
    int s01 = y[0] != y[1] ? ((x[1] - x[0]) << 14) / (y[1] - y[0]) : 0;
    int s12 = y[2] != y[1] ? ((x[2] - x[1]) << 14) / (y[2] - y[1]) : 0;
    int s02 = y[0] != y[2] ? ((x[0] - x[2]) << 14) / (y[0] - y[2]) : 0;
    int t, n, l, eq_next, eq_last;
    if (y[0] <= y[1] && y[0] <= y[2]) t = 0, n = 1, l = 2, eq_next = 1, eq_last = 1;
    else if (y[1] <= y[2]) t = 1, n = 2, l = 0, eq_next = 1, eq_last = 0;
    else t = 2, n = 0, l = 1, eq_next = 0, eq_last = 0;
    if (y[t] >= ICON_H) return;
    int s_tn = t + n == 1 ? s01 : t + n == 3 ? s12 : s02;
    int s_tl = t + l == 1 ? s01 : t + l == 3 ? s12 : s02;
    int s_nl = n + l == 1 ? s01 : n + l == 3 ? s12 : s02;
    int top = y[t] < 0 ? 0 : y[t];
    int next = y[n] < ICON_H ? y[n] : ICON_H, last = y[l] < ICON_H ? y[l] : ICON_H;
    int next_first = next < last;
    int mid = next_first ? next : last;
    mid = mid < 0 ? 0 : mid;
    int end = next_first ? last : next;
    int tl_left = next_first
        ? (eq_next ? (top != mid && s_tl < s_tn) || (top == mid && s_tl > s_nl) : s_tl < s_tn)
        : (eq_last ? (top != mid && s_tl < s_tn) || (top == mid && s_nl > s_tn) : s_tl < s_tn);
    int xt = x[t] << 14;
    for (int row = top; row < mid; row++) {
        int tn = xt + s_tn * (row - y[t]), tl = xt + s_tl * (row - y[t]);
        span(r, ctx, row, (tl_left ? tl : tn) >> 14, (tl_left ? tn : tl) >> 14);
    }
    for (int row = mid; row < end; row++) {
        int a = xt + (next_first ? s_tl : s_tn) * (row - y[t]);
        int b = next_first ? (x[n] << 14) + s_nl * (row - y[n]) : (x[l] << 14) + s_nl * (row - y[l]);
        int a_left = next_first == tl_left;
        span(r, ctx, row, (a_left ? a : b) >> 14, (a_left ? b : a) >> 14);
    }
}

static int icon_clip_span(const IconRaster* r, int* x0, int* x1) {
    if (r->clipped) {
        if (*x1 > ICON_W) *x1 = ICON_W;
        if (*x0 < 0) *x0 = 0;
    }
    return *x0 < *x1;
}

static int32_t icon_blend(int32_t dst, int32_t premul, int alpha) {
    return ((dst & 0xFF00FF) * alpha >> 8 & 0xFF00FF) + premul + (alpha * (dst & 0xFF00) >> 8 & 0xFF00);
}

static int32_t icon_premultiply(int32_t color, int alpha) {
    int inv = 256 - alpha;
    return (inv * (color & 0xFF00) >> 8 & 0xFF00) + (inv * (color & 0xFF00FF) >> 8 & 0xFF00FF);
}

static void icon_span_flat(IconRaster* r, const void* ctx, int y, int x0, int x1) {
    if (!icon_clip_span(r, &x0, &x1)) return;
    int32_t* p = r->pixels + y * ICON_W;
    int32_t color = *(const int32_t*)ctx;
    if (r->alpha == 0) {
        for (int x = x0; x < x1; x++) p[x] = color;
    } else if (r->alpha == 254) {
        assert(y * ICON_W + x1 < ICON_W * ICON_H);
        for (int x = x0; x < x1; x++) p[x] = p[x + 1];
    } else {
        int32_t pre = icon_premultiply(color, r->alpha);
        for (int x = x0; x < x1; x++) p[x] = icon_blend(p[x], pre, r->alpha);
    }
}

typedef struct {
    int base, dx, dy, top;
} IconGouraud;

static void icon_span_gouraud(IconRaster* r, const void* ctx, int y, int x0, int x1) {
    const IconGouraud* g = ctx;
    if (!icon_clip_span(r, &x0, &x1)) return;
    int32_t* p = r->pixels + y * ICON_W;
    int shade = g->base + g->dy * (y - g->top) + x0 * g->dx;
    for (int x = x0; x < x1; x++, shade += g->dx) {
        assert(shade >> 8 >= 0 && shade >> 8 < 65536);
        int32_t color = icon_palette[shade >> 8];
        p[x] = r->alpha == 0 ? color : icon_blend(p[x], icon_premultiply(color, r->alpha), r->alpha);
    }
}

static void icon_gouraud(IconRaster* r, const int y[3], const int x[3], int c0, int c1, int c2) {
    int area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    if (area == 0) return;
    int dx = (((c1 - c0) * (y[2] - y[0]) - (c2 - c0) * (y[1] - y[0])) << 8) / area;
    int dy = (((c2 - c0) * (x[1] - x[0]) - (c1 - c0) * (x[2] - x[0])) << 8) / area;
    int c[3] = {c0, c1, c2};
    int t = y[0] <= y[1] && y[0] <= y[2] ? 0 : y[1] <= y[2] ? 1 : 2;
    IconGouraud g = {dx + ((c[t] << 8) - x[t] * dx), dx, dy, y[t]};
    icon_scan(r, y, x, icon_span_gouraud, &g);
}

typedef struct {
    const int32_t* texels;
    TextureAlpha alpha;
    int shade, shade_dx, shade_dy, top;
    int u, v, w, u_dy, v_dy, w_dy, u_dx, v_dx, w_dx;
} IconTexturing;

static void icon_perspective(int u, int v, int w, int* tu, int* tv) {
    int den = w >> 14;
    if (den == 0) {
        *tu = *tv = 0;
        return;
    }
    int q = u / den;
    *tu = q < 0 ? 0 : q > 16256 ? 16256 : q;
    *tv = v / den;
}

static void icon_texel(const IconTexturing* t, int32_t* dst, int uv, int shade) {
    int32_t texel = t->texels[(uv & 16256) + (int)((uint32_t)uv >> 25)];
    if (texel == 0 && t->alpha == TEXTURE_ALPHA_KEYED) return;
    *dst = ((shade * (texel & 0xFF00) & 0xFF0000) + ((texel & 0xFF00FF) * shade & (int32_t)0xFF00FF00)) >> 8;
}

static void icon_span_texture(IconRaster* r, const void* ctx, int y, int x0, int x1) {
    const IconTexturing* t = ctx;
    if (!icon_clip_span(r, &x0, &x1)) return;
    int32_t* p = r->pixels + y * ICON_W + x0;
    int rows = y - ICON_CENTER;
    int u = t->u + t->u_dy * rows, v = t->v + t->v_dy * rows, w = t->w + t->w_dy * rows;
    int shade = t->shade + t->shade_dy * (y - t->top) + x0 * t->shade_dx;
    int dx = x0 - ICON_CENTER;
    u += dx * (t->u_dx >> 3), v += (t->v_dx >> 3) * dx, w += dx * (t->w_dx >> 3);
    int u0, v0, u1, v1;
    icon_perspective(u, v, w, &u0, &v0);
    u += t->u_dx, v += t->v_dx, w += t->w_dx;
    icon_perspective(u, v, w, &u1, &v1);
    int uv = (u0 << 18) + v0, step = ((v1 - v0) >> 3) + (((u1 - u0) >> 3) << 18);
    int n = x1 - x0;
    for (int block = n >> 3; block > 0; block--) {
        int s = shade >> 8;
        for (int k = 0; k < 8; k++, uv += step) icon_texel(t, p++, uv, s);
        u0 = u1, v0 = v1;
        u += t->u_dx, v += t->v_dx, w += t->w_dx;
        icon_perspective(u, v, w, &u1, &v1);
        uv = (u0 << 18) + v0, step = ((v1 - v0) >> 3) + (((u1 - u0) >> 3) << 18);
        shade += t->shade_dx << 3;
    }
    int s = shade >> 8;
    for (int k = n & 7; k > 0; k--, uv += step) icon_texel(t, p++, uv, s);
}

static void icon_textured(IconRaster* r, IconSource* src, const int y[3], const int x[3], int c0, int c1, int c2,
                          const int lx[3], const int ly[3], const int lz[3], int texture) {
    const int32_t* texels = icon_texture(src, texture);
    if (!texels) {
        int avg = src->textures[texture].average_hsl;
        icon_gouraud(r, y, x, model_light_hsl(avg, c0), model_light_hsl(avg, c1), model_light_hsl(avg, c2));
        return;
    }
    int area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    if (area == 0) return;
    int dx = (((c1 - c0) * (y[2] - y[0]) - (c2 - c0) * (y[1] - y[0])) << 9) / area;
    int dy = (((c2 - c0) * (x[1] - x[0]) - (c1 - c0) * (x[2] - x[0])) << 9) / area;
    int x0 = lx[0], x1 = lx[0] - lx[1], x2 = lx[2] - lx[0];
    int y0 = ly[0], y1 = ly[0] - ly[1], y2 = ly[2] - ly[0];
    int z0 = lz[0], z1 = lz[0] - lz[1], z2 = lz[2] - lz[0];
    int c[3] = {c0, c1, c2};
    int t = y[0] <= y[1] && y[0] <= y[2] ? 0 : y[1] <= y[2] ? 1 : 2;
    IconTexturing tx = {
        .texels = texels,
        .alpha = src->textures[texture].alpha,
        .shade = dx + ((c[t] << 9) - x[t] * dx),
        .shade_dx = dx,
        .shade_dy = dy,
        .top = y[t],
        .u = (x2 * y0 - x0 * y2) << 14,
        .u_dx = (int)(((int64_t)(z0 * y2 - z2 * y0) << 3 << 14) / ICON_ZOOM),
        .u_dy = (int)(((int64_t)(z2 * x0 - x2 * z0) << 14) / ICON_ZOOM),
        .v = (x1 * y0 - y1 * x0) << 14,
        .v_dx = (int)(((int64_t)(y1 * z0 - z1 * y0) << 3 << 14) / ICON_ZOOM),
        .v_dy = (int)(((int64_t)(z1 * x0 - x1 * z0) << 14) / ICON_ZOOM),
        .w = (y1 * x2 - x1 * y2) << 14,
        .w_dx = (int)(((int64_t)(z1 * y2 - y1 * z2) << 3 << 14) / ICON_ZOOM),
        .w_dy = (int)(((int64_t)(z2 * x1 - x2 * z1) << 14) / ICON_ZOOM),
    };
    icon_scan(r, y, x, icon_span_texture, &tx);
}

typedef struct {
    int radius, diameter, height;
} IconBounds;

static IconBounds icon_bounds(const Model* m) {
    int height = 0, bottom = 0, mag = 0;
    for (int v = 0; v < m->vertex_count; v++) {
        if (-m->y[v] > height) height = -m->y[v];
        if (m->y[v] > bottom) bottom = m->y[v];
        int d = m->x[v] * m->x[v] + m->z[v] * m->z[v];
        if (d > mag) mag = d;
    }
    mag = (int)(sqrt((double)mag) + 0.99);
    int radius = (int)(sqrt((double)(mag * mag + height * height)) + 0.99);
    return (IconBounds){radius, radius + (int)(sqrt((double)(mag * mag + bottom * bottom)) + 0.99), height};
}

typedef struct {
    int *sx, *sy, *sz, *lx, *ly, *lz;
} IconProjection;

static void icon_raster_face(IconRaster* r, IconSource* src, const Model* m, const ModelShade* s, const IconProjection* p,
                             const uint8_t* clipped, int f) {
    int a = m->a[f], b = m->b[f], c = m->c[f];
    r->clipped = clipped[f];
    r->alpha = m->alphas[f] & 255;
    int y[3] = {p->sy[a], p->sy[b], p->sy[c]}, x[3] = {p->sx[a], p->sx[b], p->sx[c]};
    if (m->textures[f] != -1) {
        int tc = m->tex_coords[f];
        int ta = a, tb = b, tcv = c;
        if (tc != -1 && m->tex_types[tc] == 0) ta = m->tex_p[tc], tb = m->tex_m[tc], tcv = m->tex_n[tc];
        int lx[3] = {p->lx[ta], p->lx[tb], p->lx[tcv]}, ly[3] = {p->ly[ta], p->ly[tb], p->ly[tcv]};
        int lz[3] = {p->lz[ta], p->lz[tb], p->lz[tcv]};
        int flat = s->c3[f] == -1;
        icon_textured(r, src, y, x, s->c1[f], flat ? s->c1[f] : s->c2[f], flat ? s->c1[f] : s->c3[f], lx, ly, lz,
            m->textures[f]);
    } else if (s->c3[f] == -1) {
        int32_t color = icon_palette[s->c1[f]];
        icon_scan(r, y, x, icon_span_flat, &color);
    } else {
        icon_gouraud(r, y, x, s->c1[f], s->c2[f], s->c3[f]);
    }
}

static void icon_draw_model(IconRaster* r, IconSource* src, const Model* m, const ModelShade* s, IconBounds bounds,
                            int yan, int zan, int xan, int x_off, int y_off, int z_off) {
    int nv = m->vertex_count, nf = m->face_count;
    IconProjection p = {
        malloc(sizeof(int) * (size_t)(nv + 1)), malloc(sizeof(int) * (size_t)(nv + 1)), malloc(sizeof(int) * (size_t)(nv + 1)),
        malloc(sizeof(int) * (size_t)(nv + 1)), malloc(sizeof(int) * (size_t)(nv + 1)), malloc(sizeof(int) * (size_t)(nv + 1)),
    };
    int sin_x = model_sine[xan], cos_x = model_cosine[xan];
    int z_bias = (sin_x * y_off + cos_x * z_off) >> 16;
    for (int i = 0; i < nv; i++) {
        int x = m->x[i], y = m->y[i], z = m->z[i];
        if (zan != 0) {
            int t = (y * model_sine[zan] + x * model_cosine[zan]) >> 16;
            y = (y * model_cosine[zan] - x * model_sine[zan]) >> 16;
            x = t;
        }
        if (yan != 0) {
            int t = (z * model_sine[yan] + x * model_cosine[yan]) >> 16;
            z = (z * model_cosine[yan] - x * model_sine[yan]) >> 16;
            x = t;
        }
        x += x_off, y += y_off, z += z_off;
        int t = (y * cos_x - z * sin_x) >> 16;
        z = (y * sin_x + z * cos_x) >> 16;
        assert(z != 0);
        p.sz[i] = z - z_bias;
        p.sx[i] = x * ICON_ZOOM / z + ICON_CENTER;
        p.sy[i] = t * ICON_ZOOM / z + ICON_CENTER;
        p.lx[i] = x, p.ly[i] = t, p.lz[i] = z;
    }
    if (bounds.diameter < ICON_MAX_DIAMETER) {
        uint8_t* clipped = calloc((size_t)nf + 1, 1);
        int* depth = malloc(sizeof(int) * (size_t)(nf + 1));
        int* bucket_start = calloc((size_t)bounds.diameter + 1, sizeof(int));
        for (int f = 0; f < nf; f++) {
            depth[f] = -1;
            if (s->c3[f] == -2) continue;
            int a = m->a[f], b = m->b[f], c = m->c[f];
            int xa = p.sx[a], xb = p.sx[b], xc = p.sx[c];
            if ((xa - xb) * (p.sy[c] - p.sy[b]) - (xc - xb) * (p.sy[a] - p.sy[b]) <= 0) continue;
            clipped[f] = !(xa >= 0 && xb >= 0 && xc >= 0 && xa <= ICON_W && xb <= ICON_W && xc <= ICON_W);
            int d = (p.sz[a] + p.sz[b] + p.sz[c]) / 3 + bounds.radius;
            assert(d >= 0 && d < ICON_MAX_DIAMETER);
            if (d >= bounds.diameter) continue;
            depth[f] = d;
            bucket_start[d + 1]++;
        }
        for (int d = 0; d < bounds.diameter; d++) bucket_start[d + 1] += bucket_start[d];
        int* by_depth = malloc(sizeof(int) * (size_t)(nf + 1));
        int* fill = malloc(sizeof(int) * (size_t)(bounds.diameter + 1));
        memcpy(fill, bucket_start, sizeof(int) * (size_t)(bounds.diameter + 1));
        for (int f = 0; f < nf; f++)
            if (depth[f] >= 0) by_depth[fill[depth[f]]++] = f;
        int total = bucket_start[bounds.diameter];
        int* order = malloc(sizeof(int) * (size_t)(total + 1));
        int* order_depth = malloc(sizeof(int) * (size_t)(total + 1));
        int k = 0;
        for (int d = bounds.diameter - 1; d >= 0; d--)
            for (int i = bucket_start[d]; i < bucket_start[d + 1]; i++) order[k] = by_depth[i], order_depth[k++] = d;
        int count[ICON_PRIORITIES] = {0}, depth_sum[ICON_PRIORITIES] = {0};
        int* lists[ICON_PRIORITIES];
        int* list_depth[ICON_PRIORITIES];
        for (int q = 0; q < ICON_PRIORITIES; q++) {
            lists[q] = malloc(sizeof(int) * (size_t)(total + 1));
            list_depth[q] = malloc(sizeof(int) * (size_t)(total + 1));
        }
        for (int i = 0; i < total; i++) {
            int q = m->priorities[order[i]];
            assert(q >= 0 && q < ICON_PRIORITIES);
            list_depth[q][count[q]] = order_depth[i];
            lists[q][count[q]++] = order[i];
            if (q < 10) depth_sum[q] += order_depth[i];
        }
        int avg12 = count[1] > 0 || count[2] > 0 ? (depth_sum[1] + depth_sum[2]) / (count[1] + count[2]) : 0;
        int avg34 = count[3] > 0 || count[4] > 0 ? (depth_sum[3] + depth_sum[4]) / (count[3] + count[4]) : 0;
        int avg68 = count[6] > 0 || count[8] > 0 ? (depth_sum[8] + depth_sum[6]) / (count[8] + count[6]) : 0;
        int late = count[10] ? 10 : 11, pos = 0;
        int late_depth = pos < count[late] ? list_depth[late][pos] : -1000;
        for (int q = 0; q < 10; q++) {
            int limit = q == 0 ? avg12 : q == 3 ? avg34 : q == 5 ? avg68 : 0;
            while ((q == 0 || q == 3 || q == 5) && late_depth > limit) {
                icon_raster_face(r, src, m, s, &p, clipped, lists[late][pos++]);
                if (pos == count[late] && late == 10) late = 11, pos = 0;
                late_depth = pos < count[late] ? list_depth[late][pos] : -1000;
            }
            for (int i = 0; i < count[q]; i++) icon_raster_face(r, src, m, s, &p, clipped, lists[q][i]);
        }
        while (late_depth != -1000) {
            icon_raster_face(r, src, m, s, &p, clipped, lists[late][pos++]);
            if (pos == count[late] && late == 10) late = 11, pos = 0;
            late_depth = pos < count[late] ? list_depth[late][pos] : -1000;
        }
        for (int q = 0; q < ICON_PRIORITIES; q++) free(lists[q]), free(list_depth[q]);
        free(order);
        free(order_depth);
        free(fill);
        free(by_depth);
        free(bucket_start);
        free(depth);
        free(clipped);
    }
    free(p.sx); free(p.sy); free(p.sz); free(p.lx); free(p.ly); free(p.lz);
}

static void icon_border(int32_t* px, int32_t color) {
    int32_t out[ICON_W * ICON_H];
    for (int y = 0, i = 0; y < ICON_H; y++)
        for (int x = 0; x < ICON_W; x++, i++) {
            int edge = px[i] == 0 && ((x > 0 && px[i - 1] != 0) || (y > 0 && px[i - ICON_W] != 0) ||
                                      (x < ICON_W - 1 && px[i + 1] != 0) || (y < ICON_H - 1 && px[i + ICON_W] != 0));
            out[i] = edge ? color : px[i];
        }
    memcpy(px, out, sizeof(out));
}

static void icon_overlay(int32_t* dst, const int32_t* src) {
    for (int i = 0; i < ICON_W * ICON_H; i++)
        if (src[i] != 0) dst[i] = src[i];
}

typedef enum { ICON_EMPTY, ICON_DRAWN } IconResult;

static IconResult icon_render(IconSource* src, int id, int quantity, int border, int32_t shadow, IconNote note,
                              int32_t* out) {
    ItemDef item = icon_item(src, id);
    if (quantity > 1) {
        int stack = -1;
        for (int i = 0; i < 10; i++)
            if (quantity >= item.count_co[i] && item.count_co[i] != 0) stack = item.count_obj[i];
        if (stack != -1) item = icon_item(src, stack);
    }
    if (item.inv_model < 0) return ICON_EMPTY;
    CacheFile f = cache_read_file(src->cache, CACHE_INDEX_MODELS, item.inv_model, 0);
    if (!f.data) return ICON_EMPTY;
    int32_t aux[ICON_W * ICON_H] = {0};
    IconResult aux_result = ICON_DRAWN;
    if (item.note_template != -1) aux_result = icon_render(src, item.note, 10, 1, 0, ICON_NOTED, aux);
    else if (item.bought_template != -1) aux_result = icon_render(src, item.bought, quantity, border, 0, ICON_PLAIN, aux);
    else if (item.placeholder_template != -1) aux_result = icon_render(src, item.placeholder, quantity, 0, 0, ICON_PLAIN, aux);
    if (aux_result == ICON_EMPTY) {
        free(f.data);
        return ICON_EMPTY;
    }
    Model m = model_decode(&f);
    free(f.data);
    if (item.resize_x != 128 || item.resize_y != 128 || item.resize_z != 128)
        model_resize(&m, item.resize_x, item.resize_y, item.resize_z);
    for (int i = 0; i < item.recolor_count; i++) model_recolor(&m, item.recolor_from[i], item.recolor_to[i]);
    for (int i = 0; i < item.retexture_count; i++) model_retexture(&m, item.retexture_from[i], item.retexture_to[i]);
    ModelShade shade = model_light(&m, item.ambient + 64, item.contrast + 768, -50, -10, -50);

    IconRaster r = {0};
    if (item.placeholder_template != -1) icon_overlay(r.pixels, aux);
    int zoom = note == ICON_NOTED ? (int)(item.zoom2d * 1.5) : border == 2 ? (int)(item.zoom2d * 1.04) : item.zoom2d;
    int lift = zoom * model_sine[item.xan2d] >> 16, depth = zoom * model_cosine[item.xan2d] >> 16;
    IconBounds bounds = icon_bounds(&m);
    icon_draw_model(&r, src, &m, &shade, bounds, item.yan2d, item.zan2d, item.xan2d, item.xoff2d,
        bounds.height / 2 + lift + item.yoff2d, depth + item.yoff2d);
    if (item.bought_template != -1) icon_overlay(r.pixels, aux);
    if (border >= 1) icon_border(r.pixels, 1);
    if (border >= 2) icon_border(r.pixels, 0xFFFFFF);
    for (int y = ICON_H - 1; shadow != 0 && y > 0; y--)
        for (int x = ICON_W - 1; x > 0; x--)
            if (r.pixels[y * ICON_W + x] == 0 && r.pixels[(y - 1) * ICON_W + x - 1] != 0) r.pixels[y * ICON_W + x] = shadow;
    if (item.note_template != -1) icon_overlay(r.pixels, aux);
    memcpy(out, r.pixels, sizeof(r.pixels));
    model_shade_free(&shade);
    model_free(&m);
    return ICON_DRAWN;
}

#endif
