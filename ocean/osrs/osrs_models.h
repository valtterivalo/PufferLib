/**
 * @fileoverview Loads OSRS 3D models from .models v2 binary and converts to raylib meshes.
 *
 * Binary format produced by scripts/export_models.py (MDL2):
 *   header: uint32 magic ("MDL2"), uint32 count, uint32 offsets[count]
 *   per model:
 *     uint32 model_id
 *     uint16 expanded_vert_count    (face_count * 3)
 *     uint16 face_count
 *     uint16 base_vert_count        (original indexed vertex count)
 *     float  expanded_verts[expanded_vert_count * 3]
 *     uint8  colors[expanded_vert_count * 4]
 *     int16  base_verts[base_vert_count * 3]   (original OSRS coords, y NOT negated)
 *     uint8  vertex_skins[base_vert_count]     (label group per vertex for animation)
 *     uint16 face_indices[face_count * 3]      (a,b,c per face into base verts)
 *
 * Expanded vertices + colors are used directly by raylib Mesh for rendering.
 * Base vertices, skins, and face indices are used by the animation system to
 * transform the original geometry and re-expand for GPU upload.
 */

#ifndef OSRS_MODELS_H
#define OSRS_MODELS_H

#include "raylib.h"
#include "osrs_binary_io.h"
#include "data/item_models.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MDL2_MAGIC 0x4D444C32  /* "MDL2" */

typedef struct {
    uint32_t model_id;
    Mesh mesh;
    Model model;

    /* animation data (from base indexed geometry) */
    int16_t*  base_vertices;    /* [base_vert_count * 3] original OSRS coords */
    uint8_t*  vertex_skins;     /* [base_vert_count] label group per vertex */
    uint16_t* face_indices;     /* [face_count * 3] triangle index buffer */
    uint8_t*  face_priorities;  /* [face_count] render priority per face (0-11) */
    uint16_t  base_vert_count;
    uint8_t   min_priority;     /* minimum face priority in this model */

} OsrsModel;

typedef struct {
    OsrsModel* models;
    int count;
} ModelCache;


static ModelCache* model_cache_load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "model_cache_load: cannot open %s\n", path);
        return NULL;
    }

    /* read header */
    uint32_t magic, count;
    osrs_read_exact(f, &magic, 4, 1, path, "magic");
    osrs_read_exact(f, &count, 4, 1, path, "model count");

    if (magic != MDL2_MAGIC) {
        fprintf(stderr, "model_cache_load: bad magic 0x%08X (expected MDL2 0x%08X)\n",
                magic, MDL2_MAGIC);
        abort();
    }

    /* read offset table */
    uint32_t* offsets = (uint32_t*)osrs_malloc_or_abort(
        count * sizeof(uint32_t), "model offsets");
    osrs_read_exact(f, offsets, 4, count, path, "model offsets");

    ModelCache* cache = (ModelCache*)osrs_calloc_or_abort(
        1, sizeof(ModelCache), "model cache");
    cache->models = (OsrsModel*)osrs_calloc_or_abort(
        count, sizeof(OsrsModel), "model entries");
    cache->count = (int)count;

    for (uint32_t i = 0; i < count; i++) {
        osrs_seek_or_abort(f, (long)offsets[i], path);

        uint32_t model_id;
        uint16_t vert_count, face_count, base_vert_count;
        osrs_read_exact(f, &model_id, 4, 1, path, "model id");
        osrs_read_exact(f, &vert_count, 2, 1, path, "expanded vertex count");
        osrs_read_exact(f, &face_count, 2, 1, path, "face count");
        osrs_read_exact(f, &base_vert_count, 2, 1, path, "base vertex count");

        cache->models[i].model_id = model_id;
        cache->models[i].base_vert_count = base_vert_count;

        /* allocate raylib mesh for expanded rendering geometry */
        Mesh mesh = { 0 };
        mesh.vertexCount = vert_count;
        mesh.triangleCount = face_count;

        mesh.vertices = (float*)RL_MALLOC(vert_count * 3 * sizeof(float));
        mesh.colors = (unsigned char*)RL_MALLOC(vert_count * 4);
        if (!mesh.vertices || !mesh.colors) {
            fprintf(stderr, "model_cache_load: raylib mesh allocation failed for model %u\n",
                model_id);
            abort();
        }

        osrs_read_exact(f, mesh.vertices, sizeof(float), vert_count * 3, path, "expanded vertices");
        osrs_read_exact(f, mesh.colors, 1, vert_count * 4, path, "vertex colors");

        /* read animation data */
        cache->models[i].base_vertices = (int16_t*)osrs_malloc_or_abort(
            base_vert_count * 3 * sizeof(int16_t), "model base vertices");
        osrs_read_exact(f, cache->models[i].base_vertices, sizeof(int16_t),
            base_vert_count * 3, path, "base vertices");

        cache->models[i].vertex_skins = (uint8_t*)osrs_malloc_or_abort(
            base_vert_count, "model vertex skins");
        osrs_read_exact(f, cache->models[i].vertex_skins, 1,
            base_vert_count, path, "vertex skins");

        cache->models[i].face_indices = (uint16_t*)osrs_malloc_or_abort(
            face_count * 3 * sizeof(uint16_t), "model face indices");
        osrs_read_exact(f, cache->models[i].face_indices, sizeof(uint16_t),
            face_count * 3, path, "face indices");

        cache->models[i].face_priorities = (uint8_t*)osrs_malloc_or_abort(
            face_count, "model face priorities");
        osrs_read_exact(f, cache->models[i].face_priorities, 1,
            face_count, path, "face priorities");

        /* compute min priority for this model */
        uint8_t min_pri = 255;
        for (uint16_t fp = 0; fp < face_count; fp++) {
            if (cache->models[i].face_priorities[fp] < min_pri)
                min_pri = cache->models[i].face_priorities[fp];
        }
        cache->models[i].min_priority = min_pri;

        /* upload to GPU */
        UploadMesh(&mesh, false);
        cache->models[i].mesh = mesh;
        cache->models[i].model = LoadModelFromMesh(mesh);
    }

    free(offsets);
    fclose(f);

    fprintf(stderr, "model_cache_load: loaded %d models from %s\n", cache->count, path);
    return cache;
}


static OsrsModel* model_cache_get(ModelCache* cache, uint32_t model_id) {
    if (!cache) return NULL;
    for (int i = 0; i < cache->count; i++) {
        if (cache->models[i].model_id == model_id) {
            return &cache->models[i];
        }
    }
    return NULL;
}

/**
 * Find the inv_model ID for a given OSRS item ID using the generated mapping.
 * Returns 0xFFFFFFFF if not found.
 */
static uint32_t item_to_inv_model(uint16_t item_id) {
    for (int i = 0; i < ITEM_MODEL_COUNT; i++) {
        if (ITEM_MODEL_MAP[i].item_id == item_id) {
            return ITEM_MODEL_MAP[i].inv_model;
        }
    }
    return 0xFFFFFFFF;
}

/**
 * Find the wield_model ID for a given OSRS item ID using the generated mapping.
 * Returns 0xFFFFFFFF if not found.
 */
static uint32_t item_to_wield_model(uint16_t item_id) {
    for (int i = 0; i < ITEM_MODEL_COUNT; i++) {
        if (ITEM_MODEL_MAP[i].item_id == item_id) {
            return ITEM_MODEL_MAP[i].wield_model;
        }
    }
    return 0xFFFFFFFF;
}

/**
 * Check if a body item provides its own arm model (has sleeves).
 * When true, the default arm body parts should be hidden.
 */
static int item_has_sleeves(uint16_t item_id) {
    for (int i = 0; i < ITEM_MODEL_COUNT; i++) {
        if (ITEM_MODEL_MAP[i].item_id == item_id) {
            return ITEM_MODEL_MAP[i].has_sleeves;
        }
    }
    return 0;
}


static void model_cache_free(ModelCache* cache) {
    if (!cache) return;
    for (int i = 0; i < cache->count; i++) {
        UnloadModel(cache->models[i].model);
        /* UnloadModel already frees the mesh */
        free(cache->models[i].base_vertices);
        free(cache->models[i].vertex_skins);
        free(cache->models[i].face_indices);
        free(cache->models[i].face_priorities);
    }
    free(cache->models);
    free(cache);
}

#endif /* OSRS_MODELS_H */
