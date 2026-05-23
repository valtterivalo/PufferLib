/**
 * @file osrs_scene_assets.h
 * @brief Shared scene asset loader for encounter eval bindings.
 *
 * Each encounter binding's c_render needs to bootstrap the same set of
 * render assets: equipment models/anims, projectile + overlay visuals,
 * arena terrain, placed objects, optional collision map, and optional
 * NPC model/anim caches. This header packages the common path so a new
 * encounter only needs to declare its EncounterSceneConfig and call
 * encounter_load_scene_assets(rc, &cfg).
 *
 * Encounter-specific wiring (e.g. zulrah's put_ptr collision_map onto
 * the encounter state, inferno's custom camera bootstrap) stays in the
 * binding because it depends on the encounter SDK's typed state.
 */

#ifndef OSRS_SCENE_ASSETS_H
#define OSRS_SCENE_ASSETS_H

#include "osrs_assets.h"
#include "osrs_collision.h"
#include "osrs_objects.h"
#include "osrs_render.h"
#include "osrs_terrain.h"

typedef struct {
    /* asset groups to require before any load. terminate with -1. */
    OsrsAssetGroupKind required_groups[4];

    /* logical paths (relative to OSRS_ASSET_ROOT). NULL = skip the load. */
    const char* terrain_path;
    const char* objects_path;
    const char* objects_secondary_path;  /* e.g. inferno_zuk.objects */
    const char* cmap_path;
    const char* npc_models_path;
    const char* npc_anims_path;

    /* world origin to subtract from terrain/objects so encounter-local (0,0)
       maps to raylib (0,0). collision_world_offset_{x,y} is set to match.
       leave at 0 to render with raw world coordinates (PvP arena pattern). */
    int world_origin_x;
    int world_origin_y;
} EncounterSceneConfig;

/** Load all scene assets defined in cfg into rc.
    Caller is responsible for: making the RenderClient first, setting
    rc->ticks_per_second, post-load camera/entity bootstrap, and any
    encounter-specific wiring (e.g. put_ptr cmap onto encounter state).
    The loaded CollisionMap pointer (if any) is returned so the caller
    can also stash it on the OsrsEnv->collision_map field. */
static inline CollisionMap* encounter_load_scene_assets(
    RenderClient* rc, const EncounterSceneConfig* cfg
) {
    if (!rc || !cfg) {
        fprintf(stderr, "encounter_load_scene_assets: null rc or cfg\n");
        abort();
    }

    for (int i = 0; i < (int)(sizeof(cfg->required_groups) / sizeof(cfg->required_groups[0])); i++) {
        OsrsAssetGroupKind group = cfg->required_groups[i];
        if ((int)group < 0) break;
        osrs_asset_require_group(group);
    }

    rc->model_cache = model_cache_load(OSRS_ASSET("equipment.models"));
    if (rc->model_cache) rc->show_models = 1;
    rc->anim_cache = anim_cache_load(OSRS_ASSET("equipment.anims"));
    render_load_projectile_assets(rc);
    render_init_overlay_models(rc);

    if (cfg->terrain_path) {
        rc->terrain = terrain_load(cfg->terrain_path);
        if (rc->terrain && (cfg->world_origin_x || cfg->world_origin_y))
            terrain_offset(rc->terrain, cfg->world_origin_x, cfg->world_origin_y);
    }

    if (cfg->objects_path) {
        rc->objects = objects_load(cfg->objects_path);
        if (rc->objects && (cfg->world_origin_x || cfg->world_origin_y))
            objects_offset(rc->objects, cfg->world_origin_x, cfg->world_origin_y);
    }

    if (cfg->objects_secondary_path) {
        rc->objects_zuk = objects_load(cfg->objects_secondary_path);
        if (rc->objects_zuk && (cfg->world_origin_x || cfg->world_origin_y))
            objects_offset(rc->objects_zuk, cfg->world_origin_x, cfg->world_origin_y);
    }

    if (cfg->npc_models_path)
        rc->npc_model_cache = model_cache_load(cfg->npc_models_path);
    if (cfg->npc_anims_path)
        rc->npc_anim_cache = anim_cache_load(cfg->npc_anims_path);

    CollisionMap* cmap = NULL;
    if (cfg->cmap_path) {
        cmap = collision_map_load(cfg->cmap_path);
        if (cmap) {
            rc->collision_map = cmap;
            rc->collision_world_offset_x = cfg->world_origin_x;
            rc->collision_world_offset_y = cfg->world_origin_y;
        }
    }

    return cmap;
}

#endif /* OSRS_SCENE_ASSETS_H */
