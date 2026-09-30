#ifndef OSRS_CACHE_MAYA_H
#define OSRS_CACHE_MAYA_H

#include "osrs_cache.h"
#include "osrs_cache_anim.h"
#include "osrs_cache_model.h"

enum { MAYA_GROUP_BONE_TRANSFORMS = 1, MAYA_GROUP_ALPHA = 4 };

enum { ANIM_FRAME_LEGACY = 0, ANIM_FRAME_MAYA_BAKED = 1 };

static const int MAYA_COMPONENT_INDEX[17] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 0};

static inline float cache_f32(CacheBuf* b) {
    assert(b->end - b->p >= 4);
    uint32_t bits = (uint32_t)b->p[0] << 24 | (uint32_t)b->p[1] << 16 | (uint32_t)b->p[2] << 8 | b->p[3];
    b->p += 4;
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

typedef struct {
    int16_t frame;
    float value, in_x, in_y, out_x, out_y;
} MayaCurveKey;

typedef struct {
    int key_count;
    MayaCurveKey* keys;
} MayaCurve;

static MayaCurve* maya_curve_decode(CacheBuf* b) {
    MayaCurve* c = calloc(1, sizeof(MayaCurve));
    c->key_count = cache_u16(b);
    cache_u8(b);
    cache_u8(b);
    cache_u8(b);
    cache_u8(b);
    c->keys = malloc(sizeof(MayaCurveKey) * (size_t)c->key_count);
    for (int i = 0; i < c->key_count; i++) {
        MayaCurveKey* k = &c->keys[i];
        k->frame = cache_i16(b);
        k->value = cache_f32(b);
        k->in_x = cache_f32(b);
        k->in_y = cache_f32(b);
        k->out_x = cache_f32(b);
        k->out_y = cache_f32(b);
    }
    return c;
}

static void maya_curve_free(MayaCurve* c) {
    if (!c) return;
    free(c->keys);
    free(c);
}

static double maya_cubic_bezier(double p0, double p1, double p2, double p3, double t) {
    double u = 1.0 - t;
    return u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
}

static double maya_curve_evaluate(const MayaCurve* c, int frame) {
    if (c->key_count == 0) return 0.0;
    const MayaCurveKey* first = &c->keys[0];
    const MayaCurveKey* last = &c->keys[c->key_count - 1];
    if (frame <= first->frame) return first->value;
    if (frame >= last->frame) return last->value;
    const MayaCurveKey* left = first;
    const MayaCurveKey* right = last;
    for (int i = 0; i < c->key_count - 1; i++) {
        if (c->keys[i].frame <= frame && frame <= c->keys[i + 1].frame) {
            left = &c->keys[i];
            right = &c->keys[i + 1];
            break;
        }
    }
    if (left->out_x == 0.0f && left->out_y == 0.0f) return left->value;
    if (left->out_x >= 3.3e38f && left->out_y >= 3.3e38f) return frame != left->frame ? right->value : left->value;
    if (right->frame == left->frame) return left->value;

    double p0x = (double)left->frame, p0y = left->value;
    double p1x = p0x + left->out_x / 3.0, p1y = p0y + left->out_y / 3.0;
    double p3x = (double)right->frame, p3y = right->value;
    double p2x = p3x - right->in_x / 3.0, p2y = p3y - right->in_y / 3.0;
    double lo = 0.0, hi = 1.0, target = (double)frame;
    for (int i = 0; i < 24; i++) {
        double mid = (lo + hi) * 0.5;
        double x = maya_cubic_bezier(p0x, p1x, p2x, p3x, mid);
        if (x < target) lo = mid;
        else hi = mid;
    }
    double t = (lo + hi) * 0.5;
    return maya_cubic_bezier(p0y, p1y, p2y, p3y, t);
}

static void mat_identity(double m[16]) {
    memset(m, 0, sizeof(double) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0;
}

static void mat_mul3(double out[16], const double a[16], const double b[16]) {
    double r[16];
    r[0] = b[12] * a[3] + b[8] * a[2] + a[0] * b[0] + b[4] * a[1];
    r[1] = a[2] * b[9] + a[0] * b[1] + b[5] * a[1] + a[3] * b[13];
    r[2] = a[3] * b[14] + b[6] * a[1] + b[2] * a[0] + a[2] * b[10];
    r[3] = a[3] * b[15] + a[2] * b[11] + b[3] * a[0] + b[7] * a[1];
    r[4] = a[6] * b[8] + a[5] * b[4] + b[0] * a[4] + a[7] * b[12];
    r[5] = b[13] * a[7] + a[5] * b[5] + b[1] * a[4] + a[6] * b[9];
    r[6] = b[14] * a[7] + a[6] * b[10] + b[2] * a[4] + a[5] * b[6];
    r[7] = b[15] * a[7] + a[4] * b[3] + a[5] * b[7] + a[6] * b[11];
    r[8] = b[12] * a[11] + b[8] * a[10] + a[8] * b[0] + a[9] * b[4];
    r[9] = b[13] * a[11] + b[1] * a[8] + b[5] * a[9] + a[10] * b[9];
    r[10] = b[14] * a[11] + a[8] * b[2] + b[6] * a[9] + a[10] * b[10];
    r[11] = a[10] * b[11] + a[9] * b[7] + a[8] * b[3] + a[11] * b[15];
    r[12] = b[12] * a[15] + b[4] * a[13] + b[0] * a[12] + a[14] * b[8];
    r[13] = b[13] * a[15] + b[1] * a[12] + b[5] * a[13] + b[9] * a[14];
    r[14] = a[15] * b[14] + b[10] * a[14] + b[6] * a[13] + a[12] * b[2];
    r[15] = a[14] * b[11] + a[13] * b[7] + b[3] * a[12] + a[15] * b[15];
    memcpy(out, r, sizeof(double) * 16);
}

static void mat_invert4x4(double out[16], const double m[16]) {
    double a[4][8];
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) a[row][col] = m[row + 4 * col];
        for (int col = 0; col < 4; col++) a[row][4 + col] = row == col ? 1.0 : 0.0;
    }
    for (int col = 0; col < 4; col++) {
        int pivot = col;
        for (int r = col + 1; r < 4; r++)
            if (fabs(a[r][col]) > fabs(a[pivot][col])) pivot = r;
        assert(fabs(a[pivot][col]) >= 1e-8);
        if (pivot != col)
            for (int c = 0; c < 8; c++) {
                double t = a[col][c];
                a[col][c] = a[pivot][c], a[pivot][c] = t;
            }
        double factor = a[col][col];
        for (int c = 0; c < 8; c++) a[col][c] /= factor;
        for (int r = 0; r < 4; r++) {
            if (r == col) continue;
            double scale = a[r][col];
            for (int c = 0; c < 8; c++) a[r][c] -= scale * a[col][c];
        }
    }
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++) out[row + 4 * col] = a[row][4 + col];
}

static void mat_default_rotation(const double m[16], double out[3]) {
    double c6 = m[6] < -1.0 ? -1.0 : m[6] > 1.0 ? 1.0 : m[6];
    out[0] = -asin(c6);
    double cos_x = cos(out[0]);
    out[1] = out[2] = 0.0;
    if (fabs(cos_x) > 0.005) {
        out[1] = atan2(m[2], m[10]);
        out[2] = atan2(m[4], m[5]);
    } else if (m[6] < 0.0) {
        out[1] = atan2(m[1], m[0]);
    } else {
        out[1] = -atan2(m[1], m[0]);
    }
}

static void mat_default_scale(const double m[16], double out[3]) {
    out[0] = sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    out[1] = sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    out[2] = sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
}

static void mat_rotation_x(double angle, double m[16]) {
    double c = cos(angle), s = sin(angle);
    mat_identity(m);
    m[5] = c, m[6] = s, m[9] = -s, m[10] = c;
}

static void mat_rotation_y(double angle, double m[16]) {
    double c = cos(angle), s = sin(angle);
    mat_identity(m);
    m[0] = c, m[2] = -s, m[8] = s, m[10] = c;
}

static void mat_rotation_z(double angle, double m[16]) {
    double c = cos(angle), s = sin(angle);
    mat_identity(m);
    m[0] = c, m[1] = s, m[4] = -s, m[5] = c;
}

static void mat_scale_xyz(double x, double y, double z, double m[16]) {
    mat_identity(m);
    m[0] = x, m[5] = y, m[10] = z;
}

static void maya_compose_local(const double rotation[3], const double translation[3], const double scale[3],
                                double out[16]) {
    double rz[16], rx[16], ry[16], sc[16], tmp[16];
    mat_rotation_z(rotation[2], rz);
    mat_rotation_x(rotation[0], rx);
    mat_rotation_y(rotation[1], ry);
    mat_scale_xyz(scale[0], scale[1], scale[2], sc);
    mat_identity(out);
    mat_mul3(tmp, out, rz);
    mat_mul3(out, tmp, rx);
    mat_mul3(tmp, out, ry);
    mat_mul3(out, tmp, sc);
    out[12] = translation[0], out[13] = translation[1], out[14] = translation[2];
}

static void maya_transform_vertex(const double m[16], int x, int y, int z, int* ox, int* oy, int* oz) {
    double fx = (double)x, fy = (double)(-y), fz = (double)(-z);
    *ox = (int)(m[0] * fx + m[4] * fy + m[8] * fz + m[12]);
    *oy = -(int)(m[1] * fx + m[5] * fy + m[9] * fz + m[13]);
    *oz = -(int)(m[2] * fx + m[6] * fy + m[10] * fz + m[14]);
}

static inline int16_t maya_clamp_i16(int v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

typedef struct {
    int parent_index;
    int pose_count;
    double (*base_matrices)[16];
    double (*default_rotation)[3];
    double (*default_translation)[3];
    double (*default_scale)[3];
} MayaBone;

typedef struct {
    int bone_count;
    int bind_frame_count;
    MayaBone* bones;
} MayaSkeleton;

static MayaSkeleton maya_skeleton_decode(const CacheFile* file) {
    CacheBuf b = {file->data, file->data + file->size};
    int legacy_count = cache_u8(&b);
    cache_skip(&b, legacy_count);
    uint8_t label_counts[256];
    for (int i = 0; i < legacy_count; i++) label_counts[i] = cache_u8(&b);
    for (int i = 0; i < legacy_count; i++) cache_skip(&b, label_counts[i]);
    assert(cache_buf_left(&b) > 0);
    MayaSkeleton sk = {0};
    sk.bone_count = cache_u16(&b);
    assert(sk.bone_count > 0);
    sk.bind_frame_count = cache_u8(&b);
    assert(sk.bind_frame_count > 0);
    sk.bones = calloc((size_t)sk.bone_count, sizeof(MayaBone));
    for (int i = 0; i < sk.bone_count; i++) {
        MayaBone* bone = &sk.bones[i];
        bone->parent_index = cache_i16(&b);
        bone->pose_count = sk.bind_frame_count;
        bone->base_matrices = malloc(sizeof(*bone->base_matrices) * (size_t)sk.bind_frame_count);
        bone->default_rotation = malloc(sizeof(*bone->default_rotation) * (size_t)sk.bind_frame_count);
        bone->default_translation = malloc(sizeof(*bone->default_translation) * (size_t)sk.bind_frame_count);
        bone->default_scale = malloc(sizeof(*bone->default_scale) * (size_t)sk.bind_frame_count);
        for (int p = 0; p < sk.bind_frame_count; p++) {
            double* m = bone->base_matrices[p];
            for (int k = 0; k < 16; k++) m[k] = (double)cache_f32(&b);
            cache_f32(&b), cache_f32(&b), cache_f32(&b);
            double inv[16];
            mat_invert4x4(inv, m);
            mat_default_rotation(inv, bone->default_rotation[p]);
            bone->default_translation[p][0] = m[12];
            bone->default_translation[p][1] = m[13];
            bone->default_translation[p][2] = m[14];
            mat_default_scale(m, bone->default_scale[p]);
        }
    }
    return sk;
}

static void maya_skeleton_free(MayaSkeleton* sk) {
    for (int i = 0; i < sk->bone_count; i++) {
        free(sk->bones[i].base_matrices);
        free(sk->bones[i].default_rotation);
        free(sk->bones[i].default_translation);
        free(sk->bones[i].default_scale);
    }
    free(sk->bones);
    *sk = (MayaSkeleton){0};
}

typedef struct {
    MayaSkeleton skeleton;
    int bind_frame;
    MayaCurve** bone_curves;
} MayaAnimation;

static MayaAnimation maya_animation_decode(OsrsCache* cache, int maya_id) {
    int group = (maya_id >> 16) & 0xFFFF, file_id = maya_id & 0xFFFF;
    CacheFile blob = cache_read_file(cache, CACHE_INDEX_MAYA, group, file_id);
    assert(blob.data);
    CacheBuf b = {blob.data, blob.data + blob.size};
    cache_u8(&b);
    int skeleton_id = cache_u16(&b);
    CacheFile sk_file = cache_read_file(cache, CACHE_INDEX_BASES, skeleton_id, 0);
    assert(sk_file.data);
    MayaAnimation anim = {0};
    anim.skeleton = maya_skeleton_decode(&sk_file);
    free(sk_file.data);
    cache_u16(&b), cache_u16(&b);
    anim.bind_frame = cache_u8(&b);
    assert(anim.bind_frame >= 0 && anim.bind_frame < anim.skeleton.bind_frame_count);
    int curve_count = cache_u16(&b);
    anim.bone_curves = calloc((size_t)anim.skeleton.bone_count * 9, sizeof(MayaCurve*));
    for (int i = 0; i < curve_count; i++) {
        int group_id = cache_u8(&b);
        int bone_index = cache_signed_smart(&b);
        int component_ordinal = cache_u8(&b);
        MayaCurve* curve = maya_curve_decode(&b);
        if (group_id == MAYA_GROUP_ALPHA) {
            maya_curve_free(curve);
            continue;
        }
        assert(group_id == MAYA_GROUP_BONE_TRANSFORMS);
        assert(component_ordinal >= 1 && component_ordinal <= 16);
        int component = MAYA_COMPONENT_INDEX[component_ordinal];
        assert(component >= 0 && component < 9);
        assert(bone_index >= 0 && bone_index < anim.skeleton.bone_count);
        anim.bone_curves[bone_index * 9 + component] = curve;
    }
    assert(b.p == b.end);
    free(blob.data);
    return anim;
}

static void maya_animation_free(MayaAnimation* anim) {
    for (int i = 0; i < anim->skeleton.bone_count * 9; i++) maya_curve_free(anim->bone_curves[i]);
    free(anim->bone_curves);
    maya_skeleton_free(&anim->skeleton);
    *anim = (MayaAnimation){0};
}

static void maya_bone_local(const MayaAnimation* anim, int bone_index, int frame, double out[16]) {
    const MayaBone* bone = &anim->skeleton.bones[bone_index];
    int bf = anim->bind_frame;
    double rotation[3] = {bone->default_rotation[bf][0], bone->default_rotation[bf][1], bone->default_rotation[bf][2]};
    double translation[3] = {bone->default_translation[bf][0], bone->default_translation[bf][1],
                              bone->default_translation[bf][2]};
    double scale[3] = {bone->default_scale[bf][0], bone->default_scale[bf][1], bone->default_scale[bf][2]};
    MayaCurve** curves = &anim->bone_curves[bone_index * 9];
    for (int c = 0; c < 3; c++)
        if (curves[c]) rotation[c] = maya_curve_evaluate(curves[c], frame);
    for (int c = 0; c < 3; c++)
        if (curves[3 + c]) translation[c] = maya_curve_evaluate(curves[3 + c], frame);
    for (int c = 0; c < 3; c++)
        if (curves[6 + c]) scale[c] = maya_curve_evaluate(curves[6 + c], frame);
    maya_compose_local(rotation, translation, scale, out);
}

static void maya_resolve_world(const double (*local)[16], const int* parent, int bone_index, double (*world)[16],
                                uint8_t* done) {
    if (done[bone_index]) return;
    if (parent[bone_index] >= 0) {
        maya_resolve_world(local, parent, parent[bone_index], world, done);
        mat_mul3(world[bone_index], local[bone_index], world[parent[bone_index]]);
    } else {
        memcpy(world[bone_index], local[bone_index], sizeof(double) * 16);
    }
    done[bone_index] = 1;
}

static void maya_resolve_bind(const MayaBone* bones, int bind_frame, int bone_index, double (*world)[16],
                               uint8_t* done) {
    if (done[bone_index]) return;
    const MayaBone* bone = &bones[bone_index];
    if (bone->parent_index >= 0) {
        maya_resolve_bind(bones, bind_frame, bone->parent_index, world, done);
        mat_mul3(world[bone_index], bone->base_matrices[bind_frame], world[bone->parent_index]);
    } else {
        memcpy(world[bone_index], bone->base_matrices[bind_frame], sizeof(double) * 16);
    }
    done[bone_index] = 1;
}

static void maya_world_matrices(const MayaAnimation* anim, int frame, double (*skin_out)[16]) {
    int bc = anim->skeleton.bone_count;
    int* parent = malloc(sizeof(int) * (size_t)bc);
    for (int i = 0; i < bc; i++) parent[i] = anim->skeleton.bones[i].parent_index;
    double(*local)[16] = malloc(sizeof(*local) * (size_t)bc);
    for (int i = 0; i < bc; i++) maya_bone_local(anim, i, frame, local[i]);
    double(*world)[16] = malloc(sizeof(*world) * (size_t)bc);
    uint8_t* done_w = calloc((size_t)bc, 1);
    for (int i = 0; i < bc; i++) maya_resolve_world(local, parent, i, world, done_w);
    double(*bindw)[16] = malloc(sizeof(*bindw) * (size_t)bc);
    uint8_t* done_b = calloc((size_t)bc, 1);
    for (int i = 0; i < bc; i++) maya_resolve_bind(anim->skeleton.bones, anim->bind_frame, i, bindw, done_b);
    for (int i = 0; i < bc; i++) {
        double inv[16];
        mat_invert4x4(inv, bindw[i]);
        mat_mul3(skin_out[i], inv, world[i]);
    }
    free(parent);
    free(local);
    free(world);
    free(done_w);
    free(bindw);
    free(done_b);
}

static void maya_bake_frame(const MayaAnimation* anim, const Model* model, int frame, int16_t* out) {
    int bc = anim->skeleton.bone_count;
    double(*skin)[16] = malloc(sizeof(*skin) * (size_t)bc);
    maya_world_matrices(anim, frame, skin);
    for (int v = 0; v < model->vertex_count; v++) {
        int start = model->maya_bones ? model->maya_start[v] : 0;
        int end = model->maya_bones ? model->maya_start[v + 1] : 0;
        if (start == end) {
            out[v * 3] = maya_clamp_i16(model->x[v]);
            out[v * 3 + 1] = maya_clamp_i16(model->y[v]);
            out[v * 3 + 2] = maya_clamp_i16(model->z[v]);
            continue;
        }
        double acc[16] = {0};
        for (int k = start; k < end; k++) {
            int bone = model->maya_bones[k];
            int weight = model->maya_weights[k];
            assert(bone < bc);
            double w = (double)weight / 255.0;
            for (int e = 0; e < 16; e++) acc[e] += skin[bone][e] * w;
        }
        int ox, oy, oz;
        maya_transform_vertex(acc, model->x[v], model->y[v], model->z[v], &ox, &oy, &oz);
        out[v * 3] = maya_clamp_i16(ox);
        out[v * 3 + 1] = maya_clamp_i16(oy);
        out[v * 3 + 2] = maya_clamp_i16(oz);
    }
    free(skin);
}

static void maya_apply_npc_scale(int16_t* frame, int vertex_count, int width_scale, int height_scale) {
    if (width_scale == 128 && height_scale == 128) return;
    for (int v = 0; v < vertex_count; v++) {
        frame[v * 3] = maya_clamp_i16(frame[v * 3] * width_scale / 128);
        frame[v * 3 + 1] = maya_clamp_i16(frame[v * 3 + 1] * height_scale / 128);
        frame[v * 3 + 2] = maya_clamp_i16(frame[v * 3 + 2] * width_scale / 128);
    }
}

#endif
