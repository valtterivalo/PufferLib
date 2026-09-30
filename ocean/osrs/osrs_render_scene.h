#pragma once

#include <stdio.h>
#include <string.h>

#include "encounters/encounter_nh_pvp.h"
#include "encounters/encounter_zulrah.h"
#include "encounters/encounter_inferno.h"
#include "encounters/encounter_colosseum.h"
#include "osrs_render.h"

static int encounter_name_is_pvp(const char* encounter_name) {
    return encounter_name &&
        (strcmp(encounter_name, "pvp") == 0 ||
         strcmp(encounter_name, "nh_pvp") == 0 || strcmp(encounter_name, "riskfight") == 0);
}

static void visual_require_gui_item_sprite(int raw_osrs_id, void* context) {
    gui_require_sprite_by_osrs_id((GuiState*)context, raw_osrs_id);
}

static const char* visual_scene_name(const EncounterDef* encounter_def) {
    return encounter_def ? encounter_def->scene : PVP_SCENE;
}

static const char* visual_scene_asset(const char* scene, const char* extension) {
    char name[256];
    snprintf(name, sizeof(name), "%s.%s", scene, extension);
    return OSRS_ASSET(name);
}

static void visual_load_scene_meshes(RenderClient* rc, const EncounterDef* encounter_def) {
    const char* scene = visual_scene_name(encounter_def);
    int origin_x = encounter_def ? encounter_def->scene_origin_x : 0;
    int origin_y = encounter_def ? encounter_def->scene_origin_y : 0;
    rc->terrain = terrain_load(visual_scene_asset(scene, "terrain"));
    rc->objects = objects_load(visual_scene_asset(scene, "objects"));
    if (strcmp(scene, "inferno") == 0) rc->objects_zuk = objects_load(OSRS_ASSET("inferno_zuk.objects"));
    terrain_offset(rc->terrain, origin_x, origin_y);
    objects_offset(rc->objects, origin_x, origin_y);
    objects_offset(rc->objects_zuk, origin_x, origin_y);
    rc->collision_map = collision_map_scene(scene);
    rc->collision_world_offset_x = origin_x;
    rc->collision_world_offset_y = origin_y;
    if (encounter_def && encounter_def->npc_pack) {
        rc->npc_model_cache = model_cache_load(visual_scene_asset(encounter_def->npc_pack, "models"));
        rc->npc_anim_cache = anim_cache_load(visual_scene_asset(encounter_def->npc_pack, "anims"));
    }
}

static RenderClient* visual_init_render_scene(
    OsrsEnv* env,
    const char* encounter_name,
    const EncounterArenaTopology* route_topology
) {
    RenderClient* render_client = render_make_client(env);
    env->client = render_client;
    render_client->route_topology = route_topology;
    pvp_actor_route_caches_clear(render_client->player_route_cache);
#ifdef __EMSCRIPTEN__
    if (!encounter_name || encounter_name_is_pvp(encounter_name)) {
        render_client->ticks_per_second = 15.0f;
    }
#endif

    if (!encounter_name || encounter_name_is_pvp(encounter_name)) {
        osrs_asset_require_group(OSRS_ASSET_GROUP_PVP);
    } else if (strcmp(encounter_name, "zulrah") == 0) {
        osrs_asset_require_group(OSRS_ASSET_GROUP_ZULRAH);
        osrs_asset_require_group(OSRS_ASSET_GROUP_COMBAT_VISUALS);
    } else if (strcmp(encounter_name, "inferno") == 0) {
        osrs_asset_require_group(OSRS_ASSET_GROUP_INFERNO);
        osrs_asset_require_group(OSRS_ASSET_GROUP_COMBAT_VISUALS);
    } else if (strcmp(encounter_name, "colosseum") == 0) {
        osrs_asset_require_group(OSRS_ASSET_GROUP_COLOSSEUM);
        osrs_asset_require_group(OSRS_ASSET_GROUP_COMBAT_VISUALS);
        col_for_each_display_inventory_sprite_raw_osrs_id(
            visual_require_gui_item_sprite,
            &render_client->gui);
    }


    double t0 = osrs_now_ms();
    render_client->model_cache = model_cache_load(OSRS_ASSET("equipment.models"));
    if (render_client->model_cache) render_client->show_models = 1;
    render_client->anim_cache = anim_cache_load(OSRS_ASSET("equipment.anims"));
    osrs_time_log("equipment models+anims", &t0);
    render_load_projectile_assets(render_client);
    osrs_time_log("projectile assets", &t0);
    osrs_time_log("overlay models", &t0);

    visual_load_scene_meshes(render_client, (const EncounterDef*)env->encounter_def);
    osrs_time_log("scene meshes", &t0);

    render_populate_entities(render_client, env);
    render_client->cam_target_x = (float)render_client->arena_base_x +
        (float)render_client->arena_width / 2.0f;
    render_client->cam_target_z = -((float)render_client->arena_base_y +
        (float)render_client->arena_height / 2.0f);

    for (int i = 0; i < render_client->entity_count; i++) {
        int size = render_client->entities[i].npc_size > 1
            ? render_client->entities[i].npc_size
            : 1;
        render_client->sub_x[i] = render_client->entities[i].x * 128 + size * 64;
        render_client->sub_y[i] = render_client->entities[i].y * 128 + size * 64;
        render_client->dest_x[i] = render_client->sub_x[i];
        render_client->dest_y[i] = render_client->sub_y[i];
    }

    return render_client;
}
