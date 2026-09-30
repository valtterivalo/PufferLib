#ifndef OSRS_CACHE_MODEL_H
#define OSRS_CACHE_MODEL_H

#include <math.h>

#include "osrs_cache.h"

enum { MODEL_HIDDEN_COLOR = 12345678 };

typedef struct {
    int vertex_count, face_count, tex_count;
    int *x, *y, *z;
    int *a, *b, *c;
    int* colors;
    int8_t* render_types;
    int8_t* priorities;
    int8_t* alphas;
    uint8_t* face_skins;
    int16_t* textures;
    int16_t* tex_coords;
    uint8_t* tex_types;
    int *tex_p, *tex_m, *tex_n;
    uint8_t* vertex_skins;
    int* maya_start;
    uint8_t* maya_bones;
    uint8_t* maya_weights;
    int has_face_skins, has_textures;
} Model;

static void model_alloc(Model* m, int vertices, int faces, int tex) {
    m->vertex_count = vertices, m->face_count = faces, m->tex_count = tex;
    m->x = calloc((size_t)vertices + 1, sizeof(int));
    m->y = calloc((size_t)vertices + 1, sizeof(int));
    m->z = calloc((size_t)vertices + 1, sizeof(int));
    m->vertex_skins = calloc((size_t)vertices + 1, 1);
    m->maya_start = calloc((size_t)vertices + 1, sizeof(int));
    m->a = calloc((size_t)faces + 1, sizeof(int));
    m->b = calloc((size_t)faces + 1, sizeof(int));
    m->c = calloc((size_t)faces + 1, sizeof(int));
    m->colors = calloc((size_t)faces + 1, sizeof(int));
    m->render_types = calloc((size_t)faces + 1, 1);
    m->priorities = calloc((size_t)faces + 1, 1);
    m->alphas = calloc((size_t)faces + 1, 1);
    m->face_skins = calloc((size_t)faces + 1, 1);
    m->textures = malloc(sizeof(int16_t) * ((size_t)faces + 1));
    m->tex_coords = malloc(sizeof(int16_t) * ((size_t)faces + 1));
    for (int f = 0; f < faces; f++) m->textures[f] = m->tex_coords[f] = -1;
    m->tex_types = calloc((size_t)tex + 1, 1);
    m->tex_p = calloc((size_t)tex + 1, sizeof(int));
    m->tex_m = calloc((size_t)tex + 1, sizeof(int));
    m->tex_n = calloc((size_t)tex + 1, sizeof(int));
}

static void model_free(Model* m) {
    free(m->x); free(m->y); free(m->z); free(m->vertex_skins); free(m->maya_start);
    free(m->maya_bones); free(m->maya_weights);
    free(m->a); free(m->b); free(m->c); free(m->colors); free(m->render_types); free(m->priorities);
    free(m->alphas); free(m->face_skins); free(m->textures); free(m->tex_coords);
    free(m->tex_types); free(m->tex_p); free(m->tex_m); free(m->tex_n);
    *m = (Model){0};
}

typedef struct {
    int has_render_types, priority, has_alphas, has_face_skins, has_textures, has_vertex_skins, has_maya;
    int len_vx, len_vy, len_vz, len_indices, len_tex_coords, len_skins;
} ModelHeader;

static CacheBuf model_at(const CacheFile* f, int offset) {
    assert(offset >= 0 && offset <= f->size);
    return (CacheBuf){f->data + offset, f->data + f->size};
}

static void model_read_indices(Model* m, CacheBuf* types, CacheBuf* deltas) {
    int a = 0, b = 0, c = 0, last = 0;
    for (int f = 0; f < m->face_count; f++) {
        int type = cache_u8(types);
        if (type == 1) {
            a = cache_signed_smart(deltas) + last;
            b = cache_signed_smart(deltas) + a;
            c = cache_signed_smart(deltas) + b;
        } else if (type == 2) {
            b = c;
            c = cache_signed_smart(deltas) + last;
        } else if (type == 3) {
            a = c;
            c = cache_signed_smart(deltas) + last;
        } else if (type == 4) {
            int t = a;
            a = b, b = t;
            c = cache_signed_smart(deltas) + last;
        }
        last = c;
        m->a[f] = a, m->b[f] = b, m->c[f] = c;
    }
}

static void model_read_vertices(Model* m, const ModelHeader* h, CacheBuf* flags, CacheBuf* dx, CacheBuf* dy,
                                CacheBuf* dz, CacheBuf* skins) {
    int x = 0, y = 0, z = 0;
    for (int v = 0; v < m->vertex_count; v++) {
        int flag = cache_u8(flags);
        x += flag & 1 ? cache_signed_smart(dx) : 0;
        y += flag & 2 ? cache_signed_smart(dy) : 0;
        z += flag & 4 ? cache_signed_smart(dz) : 0;
        m->x[v] = x, m->y[v] = y, m->z[v] = z;
        if (h->has_vertex_skins) m->vertex_skins[v] = cache_u8(skins);
    }
    if (!h->has_maya) return;
    const uint8_t* start = skins->p;
    int total = 0;
    for (int v = 0; v < m->vertex_count; v++) {
        int n = cache_u8(skins);
        cache_skip(skins, 2 * n);
        total += n;
    }
    m->maya_bones = malloc((size_t)total + 1);
    m->maya_weights = malloc((size_t)total + 1);
    skins->p = start;
    for (int v = 0, k = 0; v < m->vertex_count; v++) {
        m->maya_start[v] = k;
        for (int n = cache_u8(skins); n; n--, k++) {
            m->maya_bones[k] = cache_u8(skins);
            m->maya_weights[k] = cache_u8(skins);
        }
    }
    m->maya_start[m->vertex_count] = total;
}

static void model_decode_type3(Model* m, const CacheFile* f) {
    CacheBuf b = model_at(f, f->size - 26);
    int vertices = cache_u16(&b), faces = cache_u16(&b), tex = cache_u8(&b);
    ModelHeader h = {0};
    h.has_render_types = cache_u8(&b);
    h.priority = cache_u8(&b);
    h.has_alphas = cache_u8(&b);
    h.has_face_skins = cache_u8(&b);
    h.has_textures = cache_u8(&b);
    h.has_vertex_skins = cache_u8(&b);
    h.has_maya = cache_u8(&b);
    h.len_vx = cache_u16(&b), h.len_vy = cache_u16(&b), h.len_vz = cache_u16(&b);
    h.len_indices = cache_u16(&b), h.len_tex_coords = cache_u16(&b), h.len_skins = cache_u16(&b);
    model_alloc(m, vertices, faces, tex);
    for (int t = 0; t < tex; t++) m->tex_types[t] = f->data[t];
    int o = tex;
    int flags = o; o += vertices;
    int render_types = o; o += h.has_render_types ? faces : 0;
    int compress = o; o += faces;
    int priorities = o; o += h.priority == 255 ? faces : 0;
    int face_skins = o; o += h.has_face_skins ? faces : 0;
    int skins = o; o += h.len_skins;
    int alphas = o; o += h.has_alphas ? faces : 0;
    int indices = o; o += h.len_indices;
    int textures = o; o += h.has_textures ? faces * 2 : 0;
    int tex_coords = o; o += h.len_tex_coords;
    int colors = o; o += faces * 2;
    int vx = o; o += h.len_vx;
    int vy = o; o += h.len_vy;
    int vz = o; o += h.len_vz;
    int triangles = o;
    CacheBuf sf = model_at(f, flags), sx = model_at(f, vx), sy = model_at(f, vy), sz = model_at(f, vz), ss = model_at(f, skins);
    model_read_vertices(m, &h, &sf, &sx, &sy, &sz, &ss);
    CacheBuf sc = model_at(f, colors), srt = model_at(f, render_types), sp = model_at(f, priorities);
    CacheBuf sa = model_at(f, alphas), sfs = model_at(f, face_skins), st = model_at(f, textures), stc = model_at(f, tex_coords);
    for (int i = 0; i < faces; i++) {
        m->colors[i] = cache_u16(&sc);
        if (h.has_render_types) m->render_types[i] = cache_i8(&srt);
        m->priorities[i] = h.priority == 255 ? cache_i8(&sp) : (int8_t)h.priority;
        if (h.has_alphas) m->alphas[i] = cache_i8(&sa);
        if (h.has_face_skins) m->face_skins[i] = cache_u8(&sfs);
        if (h.has_textures) m->textures[i] = (int16_t)(cache_u16(&st) - 1);
        if (h.has_textures && tex > 0 && m->textures[i] != -1) m->tex_coords[i] = (int16_t)(int8_t)(cache_u8(&stc) - 1);
    }
    CacheBuf si = model_at(f, indices), sct = model_at(f, compress);
    model_read_indices(m, &sct, &si);
    CacheBuf str = model_at(f, triangles);
    for (int t = 0; t < tex; t++)
        if (m->tex_types[t] == 0) m->tex_p[t] = cache_u16(&str), m->tex_m[t] = cache_u16(&str), m->tex_n[t] = cache_u16(&str);
    m->has_face_skins = h.has_face_skins;
    m->has_textures = h.has_textures;
}

static void model_decode_type2(Model* m, const CacheFile* f) {
    CacheBuf b = model_at(f, f->size - 23);
    int vertices = cache_u16(&b), faces = cache_u16(&b), tex = cache_u8(&b);
    ModelHeader h = {0};
    int has_info = cache_u8(&b);
    h.priority = cache_u8(&b);
    h.has_alphas = cache_u8(&b);
    h.has_face_skins = cache_u8(&b);
    h.has_vertex_skins = cache_u8(&b);
    h.has_maya = cache_u8(&b);
    h.len_vx = cache_u16(&b), h.len_vy = cache_u16(&b), h.len_vz = cache_u16(&b), h.len_indices = cache_u16(&b);
    h.len_skins = cache_u16(&b);
    model_alloc(m, vertices, faces, tex);
    int o = vertices;
    int compress = o; o += faces;
    int priorities = o; o += h.priority == 255 ? faces : 0;
    int face_skins = o; o += h.has_face_skins ? faces : 0;
    int info = o; o += has_info ? faces : 0;
    int skins = o; o += h.len_skins;
    int alphas = o; o += h.has_alphas ? faces : 0;
    int indices = o; o += h.len_indices;
    int colors = o; o += faces * 2;
    int triangles = o; o += tex * 6;
    int vx = o; o += h.len_vx;
    int vy = o; o += h.len_vy;
    int vz = o;
    CacheBuf sf = model_at(f, 0), sx = model_at(f, vx), sy = model_at(f, vy), sz = model_at(f, vz), ss = model_at(f, skins);
    model_read_vertices(m, &h, &sf, &sx, &sy, &sz, &ss);
    CacheBuf sc = model_at(f, colors), sn = model_at(f, info), sp = model_at(f, priorities);
    CacheBuf sa = model_at(f, alphas), sfs = model_at(f, face_skins);
    for (int i = 0; i < faces; i++) {
        m->colors[i] = cache_u16(&sc);
        if (has_info) {
            int bits = cache_u8(&sn);
            m->render_types[i] = bits & 1;
            if (bits & 2) {
                m->tex_coords[i] = (int16_t)(bits >> 2);
                m->textures[i] = (int16_t)m->colors[i];
                m->colors[i] = 127;
                m->has_textures |= m->textures[i] != -1;
            }
        }
        m->priorities[i] = h.priority == 255 ? cache_i8(&sp) : (int8_t)h.priority;
        if (h.has_alphas) m->alphas[i] = cache_i8(&sa);
        if (h.has_face_skins) m->face_skins[i] = cache_u8(&sfs);
    }
    CacheBuf si = model_at(f, indices), sct = model_at(f, compress);
    model_read_indices(m, &sct, &si);
    CacheBuf str = model_at(f, triangles);
    for (int t = 0; t < tex; t++) m->tex_p[t] = cache_u16(&str), m->tex_m[t] = cache_u16(&str), m->tex_n[t] = cache_u16(&str);
    for (int i = 0; i < faces; i++) {
        int t = m->tex_coords[i];
        if (t >= 0 && m->a[i] == m->tex_p[t] && m->b[i] == m->tex_m[t] && m->c[i] == m->tex_n[t]) m->tex_coords[i] = -1;
    }
    m->has_face_skins = h.has_face_skins;
}

static Model model_decode(const CacheFile* f) {
    Model m = {0};
    const uint8_t* end = f->data + f->size;
    assert(end[-2] == 0xFF && (end[-1] == 0xFD || end[-1] == 0xFE));
    if (end[-1] == 0xFD) model_decode_type3(&m, f);
    else model_decode_type2(&m, f);
    return m;
}

static Model model_copy(const Model* s) {
    Model m = {0};
    model_alloc(&m, s->vertex_count, s->face_count, s->tex_count);
    size_t v = (size_t)s->vertex_count, f = (size_t)s->face_count, t = (size_t)s->tex_count;
    memcpy(m.x, s->x, v * sizeof(int)); memcpy(m.y, s->y, v * sizeof(int)); memcpy(m.z, s->z, v * sizeof(int));
    memcpy(m.vertex_skins, s->vertex_skins, v); memcpy(m.maya_start, s->maya_start, (v + 1) * sizeof(int));
    memcpy(m.a, s->a, f * sizeof(int)); memcpy(m.b, s->b, f * sizeof(int)); memcpy(m.c, s->c, f * sizeof(int));
    memcpy(m.colors, s->colors, f * sizeof(int)); memcpy(m.render_types, s->render_types, f);
    memcpy(m.priorities, s->priorities, f); memcpy(m.alphas, s->alphas, f); memcpy(m.face_skins, s->face_skins, f);
    memcpy(m.textures, s->textures, f * sizeof(int16_t)); memcpy(m.tex_coords, s->tex_coords, f * sizeof(int16_t));
    memcpy(m.tex_types, s->tex_types, t); memcpy(m.tex_p, s->tex_p, t * sizeof(int));
    memcpy(m.tex_m, s->tex_m, t * sizeof(int)); memcpy(m.tex_n, s->tex_n, t * sizeof(int));
    if (s->maya_bones) {
        size_t n = (size_t)s->maya_start[s->vertex_count];
        m.maya_bones = malloc(n + 1); m.maya_weights = malloc(n + 1);
        memcpy(m.maya_bones, s->maya_bones, n); memcpy(m.maya_weights, s->maya_weights, n);
    }
    m.has_face_skins = s->has_face_skins, m.has_textures = s->has_textures;
    return m;
}

static int model_merge_vertex(Model* m, const Model* part, int v, const Model** source, int* source_vertex) {
    for (int i = 0; i < m->vertex_count; i++)
        if (m->x[i] == part->x[v] && m->y[i] == part->y[v] && m->z[i] == part->z[v]) return i;
    int i = m->vertex_count++;
    m->x[i] = part->x[v], m->y[i] = part->y[v], m->z[i] = part->z[v];
    m->vertex_skins[i] = part->vertex_skins[v];
    source[i] = part, source_vertex[i] = v;
    return i;
}

static Model model_merge(const Model* parts, int count) {
    int vertices = 0, faces = 0, tex = 0;
    for (int p = 0; p < count; p++) vertices += parts[p].vertex_count, faces += parts[p].face_count, tex += parts[p].tex_count;
    Model m = {0};
    model_alloc(&m, vertices, faces, tex);
    m.vertex_count = m.face_count = m.tex_count = 0;
    const Model** source = calloc((size_t)vertices + 1, sizeof(Model*));
    int* source_vertex = calloc((size_t)vertices + 1, sizeof(int));
    int maya = 0;
    for (int p = 0; p < count; p++) {
        const Model* s = &parts[p];
        for (int i = 0; i < s->face_count; i++) {
            int f = m.face_count++;
            m.render_types[f] = s->render_types[i];
            m.priorities[f] = s->priorities[i];
            m.alphas[f] = s->alphas[i];
            m.face_skins[f] = s->face_skins[i];
            m.textures[f] = s->textures[i];
            m.tex_coords[f] = (int16_t)(s->tex_coords[i] != -1 ? (int8_t)(m.tex_count + s->tex_coords[i]) : -1);
            m.colors[f] = s->colors[i];
            m.a[f] = model_merge_vertex(&m, s, s->a[i], source, source_vertex);
            m.b[f] = model_merge_vertex(&m, s, s->b[i], source, source_vertex);
            m.c[f] = model_merge_vertex(&m, s, s->c[i], source, source_vertex);
        }
        for (int t = 0; t < s->tex_count; t++) {
            int d = m.tex_count++;
            m.tex_types[d] = s->tex_types[t];
            if (s->tex_types[t] == 0) {
                m.tex_p[d] = model_merge_vertex(&m, s, s->tex_p[t], source, source_vertex);
                m.tex_m[d] = model_merge_vertex(&m, s, s->tex_m[t], source, source_vertex);
                m.tex_n[d] = model_merge_vertex(&m, s, s->tex_n[t], source, source_vertex);
            }
        }
        m.has_face_skins |= s->has_face_skins;
        m.has_textures |= s->has_textures;
        maya |= s->maya_bones != NULL;
    }
    if (maya) {
        int total = 0;
        for (int v = 0; v < m.vertex_count; v++)
            if (source[v]->maya_bones) total += source[v]->maya_start[source_vertex[v] + 1] - source[v]->maya_start[source_vertex[v]];
        m.maya_bones = malloc((size_t)total + 1);
        m.maya_weights = malloc((size_t)total + 1);
        for (int v = 0, k = 0; v < m.vertex_count; v++) {
            m.maya_start[v] = k;
            if (!source[v]->maya_bones) continue;
            for (int j = source[v]->maya_start[source_vertex[v]]; j < source[v]->maya_start[source_vertex[v] + 1]; j++, k++) {
                m.maya_bones[k] = source[v]->maya_bones[j];
                m.maya_weights[k] = source[v]->maya_weights[j];
            }
        }
        m.maya_start[m.vertex_count] = total;
    }
    free(source);
    free(source_vertex);
    return m;
}

static int model_sine[2048], model_cosine[2048];

__attribute__((constructor)) static void model_build_trig(void) {
    for (int i = 0; i < 2048; i++) {
        model_sine[i] = (int)(65536.0 * sin(i * 0.0030679615));
        model_cosine[i] = (int)(65536.0 * cos(i * 0.0030679615));
    }
}

static void model_mirror(Model* m) {
    for (int v = 0; v < m->vertex_count; v++) m->z[v] = -m->z[v];
    for (int f = 0; f < m->face_count; f++) {
        int t = m->a[f];
        m->a[f] = m->c[f], m->c[f] = t;
    }
}

static void model_rotate_quarter(Model* m, int quarter) {
    for (int v = 0; v < m->vertex_count; v++) {
        int x = m->x[v], z = m->z[v];
        if (quarter == 1) m->x[v] = z, m->z[v] = -x;
        else if (quarter == 2) m->x[v] = -x, m->z[v] = -z;
        else if (quarter == 3) m->x[v] = -z, m->z[v] = x;
    }
}

static void model_rotate_y(Model* m, int angle) {
    int s = model_sine[angle], c = model_cosine[angle];
    for (int v = 0; v < m->vertex_count; v++) {
        int x = (s * m->z[v] + c * m->x[v]) >> 16;
        m->z[v] = (c * m->z[v] - s * m->x[v]) >> 16;
        m->x[v] = x;
    }
}

static void model_translate(Model* m, int x, int y, int z) {
    for (int v = 0; v < m->vertex_count; v++) m->x[v] += x, m->y[v] += y, m->z[v] += z;
}

static void model_resize(Model* m, int x, int y, int z) {
    for (int v = 0; v < m->vertex_count; v++) {
        m->x[v] = m->x[v] * x / 128;
        m->y[v] = y * m->y[v] / 128;
        m->z[v] = z * m->z[v] / 128;
    }
}

static void model_recolor(Model* m, int from, int to) {
    for (int f = 0; f < m->face_count; f++)
        if (m->colors[f] == from) m->colors[f] = to;
}

static void model_retexture(Model* m, int from, int to) {
    for (int f = 0; f < m->face_count; f++)
        if (m->textures[f] == from) m->textures[f] = (int16_t)to;
}

static int model_light_hsl(int hsl, int light) {
    light = (hsl & 127) * light >> 7;
    return (hsl & 0xFF80) + (light < 2 ? 2 : light > 126 ? 126 : light);
}

static int model_light_level(int light) { return light < 2 ? 2 : light > 126 ? 126 : light; }

typedef struct {
    int* c1;
    int* c2;
    int* c3;
} ModelShade;

static ModelShade model_light(const Model* m, int ambient, int contrast, int lx, int ly, int lz) {
    int* nx = calloc((size_t)m->vertex_count + 1, sizeof(int));
    int* ny = calloc((size_t)m->vertex_count + 1, sizeof(int));
    int* nz = calloc((size_t)m->vertex_count + 1, sizeof(int));
    int* nm = calloc((size_t)m->vertex_count + 1, sizeof(int));
    int* fx = calloc((size_t)m->face_count + 1, sizeof(int));
    int* fy = calloc((size_t)m->face_count + 1, sizeof(int));
    int* fz = calloc((size_t)m->face_count + 1, sizeof(int));
    for (int f = 0; f < m->face_count; f++) {
        int a = m->a[f], b = m->b[f], c = m->c[f];
        int x1 = m->x[b] - m->x[a], y1 = m->y[b] - m->y[a], z1 = m->z[b] - m->z[a];
        int x2 = m->x[c] - m->x[a], y2 = m->y[c] - m->y[a], z2 = m->z[c] - m->z[a];
        int x = y1 * z2 - y2 * z1, y = z1 * x2 - z2 * x1, z = x1 * y2 - x2 * y1;
        while (x > 8192 || y > 8192 || z > 8192 || x < -8192 || y < -8192 || z < -8192) x >>= 1, y >>= 1, z >>= 1;
        int len = (int)sqrt((double)(x * x + y * y + z * z));
        len = len <= 0 ? 1 : len;
        x = x * 256 / len, y = y * 256 / len, z = z * 256 / len;
        if (m->render_types[f] == 0) {
            int v3[3] = {a, b, c};
            for (int k = 0; k < 3; k++) nx[v3[k]] += x, ny[v3[k]] += y, nz[v3[k]] += z, nm[v3[k]]++;
        } else if (m->render_types[f] == 1) {
            fx[f] = x, fy[f] = y, fz[f] = z;
        }
    }
    int magnitude = (int)sqrt((double)(lz * lz + lx * lx + ly * ly));
    int scale = magnitude * contrast >> 8;
    ModelShade s = {
        calloc((size_t)m->face_count + 1, sizeof(int)), calloc((size_t)m->face_count + 1, sizeof(int)),
        calloc((size_t)m->face_count + 1, sizeof(int)),
    };
    for (int f = 0; f < m->face_count; f++) {
        int type = m->render_types[f];
        int alpha = m->alphas[f];
        if (alpha == -2) type = 3;
        if (alpha == -1) type = 2;
        int textured = m->textures[f] != -1;
        int hsl = m->colors[f] & 0xFFFF;
        if (type == 1) {
            int light = (ly * fy[f] + lz * fz[f] + lx * fx[f]) / (scale / 2 + scale) + ambient;
            s.c1[f] = textured ? model_light_level(light) : model_light_hsl(hsl, light);
            s.c3[f] = -1;
        } else if (type == 3 && !textured) {
            s.c1[f] = 128;
            s.c3[f] = -1;
        } else if (type != 0) {
            s.c3[f] = -2;
        } else {
            int v3[3] = {m->a[f], m->b[f], m->c[f]};
            int* out[3] = {&s.c1[f], &s.c2[f], &s.c3[f]};
            for (int k = 0; k < 3; k++) {
                int v = v3[k];
                int light = (ly * ny[v] + lz * nz[v] + lx * nx[v]) / (scale * nm[v]) + ambient;
                *out[k] = textured ? model_light_level(light) : model_light_hsl(hsl, light);
            }
        }
    }
    free(nx); free(ny); free(nz); free(nm); free(fx); free(fy); free(fz);
    return s;
}

static void model_shade_free(ModelShade* s) {
    free(s->c1); free(s->c2); free(s->c3);
    *s = (ModelShade){0};
}

static int model_height(const Model* m) {
    int height = 0;
    for (int v = 0; v < m->vertex_count; v++)
        if (-m->y[v] > height) height = -m->y[v];
    return height;
}

typedef int (*ModelGroundFn)(const void* ctx, int x, int z);

static void model_contour(Model* m, ModelGroundFn ground, const void* ctx, int cx, int cy, int cz, int contour) {
    int height = model_height(m);
    for (int v = 0; v < m->vertex_count; v++) {
        int x = cx + m->x[v], z = cz + m->z[v];
        int fx = x & 127, fz = z & 127, tx = x >> 7, tz = z >> 7;
        int south = (ground(ctx, tx, tz) * (128 - fx) + ground(ctx, tx + 1, tz) * fx) >> 7;
        int north = (ground(ctx, tx, tz + 1) * (128 - fx) + fx * ground(ctx, tx + 1, tz + 1)) >> 7;
        int h = (south * (128 - fz) + north * fz) >> 7;
        if (contour == 0) {
            m->y[v] = h + m->y[v] - cy;
        } else {
            int t = (-m->y[v] << 16) / height;
            if (t < contour) m->y[v] = (contour - t) * (h - cy) / contour + m->y[v];
        }
    }
}

static int model_palette[65536];

static int palette_channel(double p, double q, double t) {
    double v = 6.0 * t < 1.0 ? p + (q - p) * 6.0 * t : 2.0 * t < 1.0 ? q
        : 3.0 * t < 2.0 ? p + (q - p) * (0.6666666666666666 - t) * 6.0 : p;
    return (int)(v * 256.0);
}

static int palette_brighten(int rgb, double brightness) {
    int r = (int)(pow((rgb >> 16) / 256.0, brightness) * 256.0);
    int g = (int)(pow((rgb >> 8 & 255) / 256.0, brightness) * 256.0);
    int b = (int)(pow((rgb & 255) / 256.0, brightness) * 256.0);
    return (r << 16) + (g << 8) + b;
}

enum { PALETTE_BRIGHTNESS_PERMILLE = 800 };

static void model_palette_build(int* out, double brightness) {
    for (int i = 0, k = 0; i < 512; i++) {
        double hue = (i >> 3) / 64.0 + 0.0078125, sat = (i & 7) / 8.0 + 0.0625;
        for (int l = 0; l < 128; l++, k++) {
            double light = l / 128.0;
            double q = light < 0.5 ? light * (1.0 + sat) : light + sat - light * sat;
            double p = 2.0 * light - q;
            double hr = hue + 0.3333333333333333, hb = hue - 0.3333333333333333;
            hr -= hr > 1.0 ? 1.0 : 0.0;
            hb += hb < 0.0 ? 1.0 : 0.0;
            int rgb = (palette_channel(p, q, hr) << 16) + (palette_channel(p, q, hue) << 8) + palette_channel(p, q, hb);
            rgb = palette_brighten(rgb, brightness);
            out[k] = rgb ? rgb : 1;
        }
    }
}

__attribute__((constructor)) static void model_build_palette(void) {
    model_palette_build(model_palette, PALETTE_BRIGHTNESS_PERMILLE / 1000.0);
}

#endif
