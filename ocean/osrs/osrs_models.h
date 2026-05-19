/**
 * @fileoverview Loads OSRS 3D models from .models v2 binary and converts to raylib meshes.
 *
 * Binary format produced by scripts/export_models.py (MDL2/MDL3/MDL4):
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
 *     uint8  face_priorities[face_count]
 *     uint8  face_alphas[face_count]           (MDL4 only, OSRS alpha)
 *     uint8  face_alpha_labels[face_count]     (MDL4 only, 255 = none)
 *
 * Expanded vertices + colors are used directly by raylib Mesh for rendering.
 * Base vertices, skins, and face indices are used by the animation system to
 * transform the original geometry and re-expand for GPU upload.
 */

#ifndef OSRS_MODELS_H
#define OSRS_MODELS_H

#include "raylib.h"
#include "osrs_assets.h"
#include "osrs_binary_io.h"
#include "osrs_types.h"
#include "osrs_items.h"
#include "data/item_models.h"
#include "data/player_models.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MDL2_MAGIC 0x4D444C32  /* "MDL2" */
#define MDL3_MAGIC 0x4D444C33  /* "MDL3" */
#define MDL4_MAGIC 0x4D444C34  /* "MDL4" */
#define ATLS_MAGIC 0x41544C53  /* "ATLS" */

typedef struct {
    uint32_t model_id;
    Mesh mesh;
    Model model;

    /* animation data (from base indexed geometry) */
    int16_t*  base_vertices;    /* [base_vert_count * 3] original OSRS coords */
    uint8_t*  vertex_skins;     /* [base_vert_count] label group per vertex */
    uint16_t* face_indices;     /* [face_count * 3] triangle index buffer */
    uint8_t*  face_priorities;  /* [face_count] render priority per face (0-11) */
    uint8_t*  base_face_alphas; /* [face_count] OSRS alpha: 0 opaque, 255 transparent */
    uint8_t*  face_alpha_labels;/* [face_count] label group per face for type-5 anims */
    uint16_t  base_vert_count;
    uint8_t   min_priority;     /* minimum face priority in this model */

} OsrsModel;

typedef struct {
    OsrsModel* models;
    int count;
    Texture2D atlas_texture;
    int has_atlas;
} ModelCache;


static Texture2D model_cache_load_atlas(const char* model_path) {
    char atlas_path[1024];
    strncpy(atlas_path, model_path, sizeof(atlas_path) - 1);
    atlas_path[sizeof(atlas_path) - 1] = '\0';
    char* dot = strrchr(atlas_path, '.');
    if (dot) {
        strcpy(dot, ".atlas");
    } else {
        strncat(atlas_path, ".atlas", sizeof(atlas_path) - strlen(atlas_path) - 1);
    }

    FILE* f = osrs_asset_fopen(atlas_path, "rb");
    if (!f) return (Texture2D){0};

    uint32_t magic, width, height;
    osrs_read_exact(f, &magic, 4, 1, atlas_path, "atlas magic");
    osrs_read_exact(f, &width, 4, 1, atlas_path, "atlas width");
    osrs_read_exact(f, &height, 4, 1, atlas_path, "atlas height");
    if (magic != ATLS_MAGIC || width == 0 || height == 0) {
        fprintf(stderr, "model_cache_load: bad atlas %s\n", atlas_path);
        abort();
    }

    size_t pixel_count = (size_t)width * (size_t)height * 4;
    unsigned char* pixels = (unsigned char*)osrs_malloc_or_abort(
        pixel_count, "model atlas pixels");
    osrs_read_exact(f, pixels, 1, pixel_count, atlas_path, "atlas pixels");
    fclose(f);

    Image image = {
        .data = pixels,
        .width = (int)width,
        .height = (int)height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    Texture2D texture = LoadTextureFromImage(image);
    free(pixels);
    if (texture.id > 0) SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    fprintf(stderr, "model_cache_load: loaded atlas %ux%u from %s\n", width, height, atlas_path);
    return texture;
}


static ModelCache* model_cache_load(const char* path) {
    FILE* f = osrs_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "model_cache_load: cannot open %s\n", path);
        return NULL;
    }

    /* read header */
    uint32_t magic, count;
    osrs_read_exact(f, &magic, 4, 1, path, "magic");
    osrs_read_exact(f, &count, 4, 1, path, "model count");

    if (magic != MDL2_MAGIC && magic != MDL3_MAGIC && magic != MDL4_MAGIC) {
        fprintf(stderr, "model_cache_load: bad magic 0x%08X (expected MDL2/MDL3/MDL4)\n",
                magic);
        abort();
    }
    int has_texcoords = (magic == MDL3_MAGIC || magic == MDL4_MAGIC);
    int has_face_alpha_labels = (magic == MDL4_MAGIC);

    /* read offset table */
    uint32_t* offsets = (uint32_t*)osrs_malloc_or_abort(
        count * sizeof(uint32_t), "model offsets");
    osrs_read_exact(f, offsets, 4, count, path, "model offsets");

    ModelCache* cache = (ModelCache*)osrs_calloc_or_abort(
        1, sizeof(ModelCache), "model cache");
    cache->models = (OsrsModel*)osrs_calloc_or_abort(
        count, sizeof(OsrsModel), "model entries");
    cache->count = (int)count;
    if (has_texcoords) {
        cache->atlas_texture = model_cache_load_atlas(path);
        cache->has_atlas = cache->atlas_texture.id > 0;
        if (!cache->has_atlas) {
            fprintf(stderr, "model_cache_load: MDL3 model set requires a sibling .atlas file: %s\n", path);
            abort();
        }
    }

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
        if (has_texcoords) {
            mesh.texcoords = (float*)RL_MALLOC(vert_count * 2 * sizeof(float));
        }
        if (!mesh.vertices || !mesh.colors || (has_texcoords && !mesh.texcoords)) {
            fprintf(stderr, "model_cache_load: raylib mesh allocation failed for model %u\n",
                model_id);
            abort();
        }

        osrs_read_exact(f, mesh.vertices, sizeof(float), vert_count * 3, path, "expanded vertices");
        osrs_read_exact(f, mesh.colors, 1, vert_count * 4, path, "vertex colors");
        if (has_texcoords) {
            osrs_read_exact(f, mesh.texcoords, sizeof(float), vert_count * 2, path, "texcoords");
        }

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

        if (has_face_alpha_labels) {
            cache->models[i].base_face_alphas = (uint8_t*)osrs_malloc_or_abort(
                face_count, "model base face alphas");
            osrs_read_exact(f, cache->models[i].base_face_alphas, 1,
                face_count, path, "base face alphas");
            cache->models[i].face_alpha_labels = (uint8_t*)osrs_malloc_or_abort(
                face_count, "model face alpha labels");
            osrs_read_exact(f, cache->models[i].face_alpha_labels, 1,
                face_count, path, "face alpha labels");
        }

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
        if (cache->has_atlas) {
            cache->models[i].model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
                cache->atlas_texture;
        }
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

typedef struct {
    uint32_t hide_body_mask;
    uint32_t body_model_ids[BODY_PART_COUNT];
    uint32_t item_model_ids[NUM_GEAR_SLOTS];
    uint8_t body_visible[BODY_PART_COUNT];
    uint8_t item_visible[NUM_GEAR_SLOTS];
} OsrsPlayerAppearance;

#define OSRS_VISIBLE_EQUIP_SLOT_COUNT 9

static const int OSRS_VISIBLE_EQUIP_SLOTS[OSRS_VISIBLE_EQUIP_SLOT_COUNT] = {
    GEAR_SLOT_HEAD,
    GEAR_SLOT_CAPE,
    GEAR_SLOT_NECK,
    GEAR_SLOT_WEAPON,
    GEAR_SLOT_SHIELD,
    GEAR_SLOT_BODY,
    GEAR_SLOT_LEGS,
    GEAR_SLOT_HANDS,
    GEAR_SLOT_FEET,
};

static const ItemModelMapping* item_model_mapping_for_item(uint16_t item_id) {
    for (int i = 0; i < ITEM_MODEL_COUNT; i++) {
        if (ITEM_MODEL_MAP[i].item_id == item_id) {
            return &ITEM_MODEL_MAP[i];
        }
    }
    return NULL;
}

static uint32_t item_to_inv_model(uint16_t item_id) {
    const ItemModelMapping* mapping = item_model_mapping_for_item(item_id);
    if (!mapping) return ITEM_RENDER_MODEL_MISSING;
    return mapping->inv_model;
}

static uint32_t item_to_wield_model(uint16_t item_id) {
    const ItemModelMapping* mapping = item_model_mapping_for_item(item_id);
    if (!mapping) return ITEM_RENDER_MODEL_MISSING;
    return mapping->wield_model;
}

static uint32_t item_hide_body_mask(uint16_t item_id) {
    const ItemModelMapping* mapping = item_model_mapping_for_item(item_id);
    if (!mapping) return 0;
    return mapping->hide_body_mask;
}

static inline uint32_t item_render_equip_slot(uint16_t item_id) {
    const ItemModelMapping* mapping = item_model_mapping_for_item(item_id);
    if (!mapping) return ITEM_RENDER_MODEL_MISSING;
    return mapping->equip_slot;
}

static uint32_t item_render_flags(uint16_t item_id) {
    const ItemModelMapping* mapping = item_model_mapping_for_item(item_id);
    if (!mapping) return 0;
    return mapping->render_flags;
}

static uint32_t item_render_ready_anim(uint16_t item_id) {
    const ItemModelMapping* mapping = item_model_mapping_for_item(item_id);
    if (!mapping) return ITEM_RENDER_MODEL_MISSING;
    return mapping->ready_anim_id;
}

static uint32_t item_render_walk_anim(uint16_t item_id) {
    const ItemModelMapping* mapping = item_model_mapping_for_item(item_id);
    if (!mapping) return ITEM_RENDER_MODEL_MISSING;
    return mapping->walk_anim_id;
}

static uint32_t item_render_run_anim(uint16_t item_id) {
    const ItemModelMapping* mapping = item_model_mapping_for_item(item_id);
    if (!mapping) return ITEM_RENDER_MODEL_MISSING;
    return mapping->run_anim_id;
}

static int item_render_is_two_handed(uint16_t item_id) {
    return (item_render_flags(item_id) & ITEM_RENDER_FLAG_TWO_HANDED) != 0;
}

static OsrsPlayerAppearance osrs_resolve_player_appearance(
    const uint8_t equipped[NUM_GEAR_SLOTS]
) {
    OsrsPlayerAppearance out;
    memset(&out, 0, sizeof(out));

    for (int bp = 0; bp < BODY_PART_COUNT; bp++) {
        out.body_model_ids[bp] = ITEM_RENDER_MODEL_MISSING;
    }
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        out.item_model_ids[slot] = ITEM_RENDER_MODEL_MISSING;
    }

    uint8_t weapon_index = equipped[GEAR_SLOT_WEAPON];
    int suppress_shield = 0;
    if (weapon_index < NUM_ITEMS) {
        uint16_t weapon_item_id = ITEM_DATABASE[weapon_index].item_id;
        suppress_shield = item_is_two_handed(weapon_index) ||
            item_render_is_two_handed(weapon_item_id);
    }

    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        if (slot == GEAR_SLOT_SHIELD && suppress_shield) continue;
        uint8_t db_idx = equipped[slot];
        if (db_idx >= NUM_ITEMS) continue;
        out.hide_body_mask |= item_hide_body_mask(ITEM_DATABASE[db_idx].item_id);
    }

    for (int bp = 0; bp < BODY_PART_COUNT; bp++) {
        uint32_t model_id = DEFAULT_BODY_MODELS[bp];
        out.body_model_ids[bp] = model_id;
        out.body_visible[bp] = ((out.hide_body_mask & (1u << bp)) == 0) &&
            model_id != ITEM_RENDER_MODEL_MISSING;
    }

    for (int i = 0; i < OSRS_VISIBLE_EQUIP_SLOT_COUNT; i++) {
        int slot = OSRS_VISIBLE_EQUIP_SLOTS[i];
        if (slot == GEAR_SLOT_SHIELD && suppress_shield) continue;
        uint8_t db_idx = equipped[slot];
        if (db_idx >= NUM_ITEMS) continue;
        uint16_t item_id = ITEM_DATABASE[db_idx].item_id;
        uint32_t model_id = item_to_wield_model(item_id);
        out.item_model_ids[slot] = model_id;
        out.item_visible[slot] = model_id != ITEM_RENDER_MODEL_MISSING;
    }

    return out;
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
        free(cache->models[i].base_face_alphas);
        free(cache->models[i].face_alpha_labels);
    }
    if (cache->atlas_texture.id > 0) UnloadTexture(cache->atlas_texture);
    free(cache->models);
    free(cache);
}

#endif /* OSRS_MODELS_H */
