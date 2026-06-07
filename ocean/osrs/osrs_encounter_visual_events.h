/**
 * @file osrs_encounter_visual_events.h
 * @brief Shared render entity and projectile event helpers for OSRS encounters.
 */

#ifndef OSRS_ENCOUNTER_VISUAL_EVENTS_H
#define OSRS_ENCOUNTER_VISUAL_EVENTS_H

#include "osrs_combat_visuals.h"
#include "osrs_encounter.h"

typedef struct {
    int npc_def_id;
    int npc_slot;
    uint32_t npc_instance_id;
    int npc_visible;
    int npc_size;
    int npc_anim_id;
    int x;
    int y;
    int dest_x;
    int dest_y;
    int current_hitpoints;
    int base_hitpoints;
    AttackStyle attack_style_this_tick;
    int hit_landed_this_tick;
    int hit_damage;
    int hit_was_successful;
    int hit_spell_type;
    int attack_target_entity_idx;
} OsrsNpcRenderEntitySpec;

typedef struct {
    int src_x;
    int src_y;
    int dst_x;
    int dst_y;
    int style;
    int damage;
    int duration_ticks;
    int start_h;
    int end_h;
    int curve;
    float arc_height;
    int src_size;
    int dst_size;
    uint32_t model_id;
    int anim_id;
    int launch_gfx_id;
    int impact_gfx_id;
    int start_delay;
} OsrsProjectileEventSpec;

typedef struct {
    int src_x;
    int src_y;
    int dst_x;
    int dst_y;
    int src_size;
    int dst_size;
    int target_npc_slot;
    AttackStyle attack_style;
    int damage;
    int duration_ticks;
    int start_delay;
    int fallback_start_h;
    int fallback_end_h;
    int curve;
    int splash_gfx_id;
} OsrsCombatProjectileEmitSpec;

static inline int osrs_render_target_ref_resolve_entity_idx(
    const RenderEntity* entities,
    int count,
    OsrsRenderTargetRef target
) {
    if (!entities || count < 0) abort();
    switch (target.kind) {
        case OSRS_RENDER_TARGET_NONE:
            return -1;
        case OSRS_RENDER_TARGET_ENTITY_INDEX:
            if (target.slot < 0) abort();
            return target.slot < count ? target.slot : -1;
        case OSRS_RENDER_TARGET_PLAYER_SLOT:
            if (target.slot < 0) abort();
            for (int i = 0; i < count; i++) {
                if (entities[i].entity_type == ENTITY_PLAYER &&
                        entities[i].player_slot == target.slot) {
                    return i;
                }
            }
            return -1;
        case OSRS_RENDER_TARGET_NPC_SLOT:
            if (target.slot < 0) abort();
            for (int i = 0; i < count; i++) {
                if (entities[i].entity_type == ENTITY_NPC &&
                        entities[i].npc_slot == target.slot) {
                    return i;
                }
            }
            return -1;
    }
    abort();
}

static inline void osrs_render_entity_set_attack_target_ref(
    RenderEntity* entities,
    int count,
    int source_entity_idx,
    OsrsRenderTargetRef target
) {
    if (!entities || source_entity_idx < 0 || source_entity_idx >= count) abort();
    entities[source_entity_idx].attack_target_entity_idx =
        osrs_render_target_ref_resolve_entity_idx(entities, count, target);
}

static inline void osrs_render_entity_set_preferred_attack_target_ref(
    RenderEntity* entities,
    int count,
    int source_entity_idx,
    OsrsRenderTargetRef primary,
    OsrsRenderTargetRef fallback
) {
    if (!entities || source_entity_idx < 0 || source_entity_idx >= count) abort();
    int target = osrs_render_target_ref_resolve_entity_idx(entities, count, primary);
    if (target < 0)
        target = osrs_render_target_ref_resolve_entity_idx(entities, count, fallback);
    entities[source_entity_idx].attack_target_entity_idx = target;
}

static inline int osrs_npc_death_linger_start(
    int current_hitpoints,
    int active,
    int* death_ticks,
    int linger_ticks
) {
    if (!death_ticks || linger_ticks <= 0) {
        fprintf(stderr, "invalid npc death linger start input\n");
        abort();
    }
    if (current_hitpoints > 0 || !active || *death_ticks > 0)
        return 0;
    *death_ticks = linger_ticks;
    return 1;
}

static inline int osrs_npc_death_linger_tick(int* death_ticks) {
    if (!death_ticks) {
        fprintf(stderr, "invalid npc death linger tick input\n");
        abort();
    }
    if (*death_ticks <= 0)
        return 0;
    *death_ticks -= 1;
    return *death_ticks == 0;
}

static inline void osrs_render_entity_from_npc_spec(
    const OsrsNpcRenderEntitySpec* spec,
    RenderEntity* out
) {
    if (!spec || !out || spec->npc_slot < 0 || spec->npc_size <= 0) {
        fprintf(stderr, "invalid npc render entity spec\n");
        abort();
    }
    memset(out, 0, sizeof(RenderEntity));
    memset(out->equipped, ITEM_NONE, NUM_GEAR_SLOTS);
    out->entity_type = ENTITY_NPC;
    out->npc_def_id = spec->npc_def_id;
    out->player_slot = -1;
    out->npc_slot = spec->npc_slot;
    out->npc_instance_id = spec->npc_instance_id;
    out->npc_visible = spec->npc_visible;
    out->npc_size = spec->npc_size;
    out->npc_anim_id = spec->npc_anim_id;
    out->x = spec->x;
    out->y = spec->y;
    out->dest_x = spec->dest_x;
    out->dest_y = spec->dest_y;
    out->current_hitpoints = spec->current_hitpoints;
    out->base_hitpoints = spec->base_hitpoints;
    out->attack_style_this_tick = spec->attack_style_this_tick;
    out->hit_landed_this_tick = spec->hit_landed_this_tick;
    out->hit_damage = spec->hit_damage;
    out->hit_was_successful = spec->hit_was_successful;
    out->hit_spell_type = spec->hit_spell_type;
    out->attack_target_entity_idx = spec->attack_target_entity_idx;
}

static inline void osrs_npc_primary_anim_event_set(
    Player* npc,
    int* event_tick,
    int* until_tick,
    int tick,
    int anim_id,
    int duration_ticks
) {
    if (!npc || !event_tick || !until_tick || anim_id < 0 || duration_ticks <= 0) {
        fprintf(stderr, "invalid npc primary animation event\n");
        abort();
    }
    npc->npc_anim_id = anim_id;
    *event_tick = tick;
    *until_tick = tick + duration_ticks;
}

static inline int osrs_npc_primary_anim_event_should_emit(
    const Player* npc,
    int event_tick,
    int tick
) {
    return npc && npc->npc_anim_id >= 0 && event_tick == tick;
}

static inline void osrs_npc_primary_anim_event_expire(
    Player* npc,
    int until_tick,
    int tick
) {
    if (npc && npc->npc_anim_id >= 0 && tick >= until_tick)
        npc->npc_anim_id = -1;
}

static inline void osrs_render_entity_from_npc_player(
    const Player* npc,
    RenderEntity* out,
    int npc_slot,
    uint32_t npc_instance_id
) {
    if (!npc || !out || npc_slot < 0) {
        fprintf(stderr, "invalid npc render entity input\n");
        abort();
    }
    render_entity_from_player(npc, out);
    out->npc_slot = npc_slot;
    out->npc_instance_id = npc_instance_id;
}

static inline void osrs_render_entity_from_player_entity(
    const Player* player,
    RenderEntity* out
) {
    if (!player || !out) {
        fprintf(stderr, "invalid player render entity input\n");
        abort();
    }
    render_entity_from_player(player, out);
    if (out->entity_type != ENTITY_PLAYER) {
        fprintf(stderr, "render entity is not a player\n");
        abort();
    }
}

static inline void osrs_render_entity_from_player_slot(
    const Player* player,
    RenderEntity* out,
    int player_slot
) {
    if (player_slot < 0) abort();
    osrs_render_entity_from_player_entity(player, out);
    out->player_slot = player_slot;
}

static inline void osrs_render_entity_suppress_pose_anims(
    RenderEntity* entity,
    int idle_anim_id,
    int walk_anim_id
) {
    if (!entity) {
        fprintf(stderr, "missing render entity\n");
        abort();
    }
    if (entity->npc_anim_id == idle_anim_id ||
            entity->npc_anim_id == walk_anim_id) {
        entity->npc_anim_id = -1;
    }
}

static inline int osrs_emit_projectile_with_spec(
    EncounterOverlay* overlay,
    const OsrsProjectileEventSpec* spec,
    int tracks_target
) {
    if (!overlay || !spec) {
        fprintf(stderr, "invalid projectile event input\n");
        abort();
    }
    int idx = encounter_emit_projectile(
        overlay,
        spec->src_x,
        spec->src_y,
        spec->dst_x,
        spec->dst_y,
        spec->style,
        spec->damage,
        spec->duration_ticks,
        spec->start_h,
        spec->end_h,
        spec->curve,
        spec->arc_height,
        tracks_target,
        spec->src_size,
        spec->dst_size,
        spec->model_id,
        spec->impact_gfx_id);
    if (spec->anim_id >= 0)
        encounter_set_projectile_animation(overlay, idx, spec->anim_id);
    if (spec->launch_gfx_id > 0)
        encounter_set_projectile_launch_gfx(overlay, idx, spec->launch_gfx_id);
    overlay->projectiles[idx].start_delay = spec->start_delay;
    return idx;
}

static inline float osrs_combat_projectile_profile_arc_height(
    const OsrsCombatProjectileProfile* profile,
    AttackStyle attack_style
) {
    if (attack_style == ATTACK_STYLE_MAGIC)
        return 0.0f;
    if (profile && profile->travel_spotanim_id == GFX_DRAGON_DART)
        return 0.5f;
    return 1.0f;
}

static inline int osrs_emit_combat_projectile_profile_player_to_npc(
    EncounterOverlay* overlay,
    const OsrsCombatProjectileProfile* profile,
    const OsrsCombatProjectileEmitSpec* spec
) {
    if (!overlay || !profile || !spec || spec->target_npc_slot < 0) {
        fprintf(stderr, "invalid combat projectile profile emit input\n");
        abort();
    }
    if (profile->projectile_model_id <= 0) {
        fprintf(stderr, "missing combat projectile model for style %d\n",
            spec->attack_style);
        abort();
    }

    int impact_gfx = osrs_combat_projectile_value_or(
        profile->impact_spotanim_id, 0);
    if (spec->attack_style == ATTACK_STYLE_MAGIC &&
            spec->damage <= 0 && spec->splash_gfx_id > 0) {
        impact_gfx = spec->splash_gfx_id;
    }

    int curve = spec->curve > 0
        ? spec->curve
        : osrs_combat_projectile_value_or(profile->projectile_angle, 16);
    OsrsProjectileEventSpec projectile_spec = {
        .src_x = spec->src_x,
        .src_y = spec->src_y,
        .dst_x = spec->dst_x,
        .dst_y = spec->dst_y,
        .style = encounter_attack_style_to_proj_style(spec->attack_style),
        .damage = spec->damage,
        .duration_ticks = spec->duration_ticks,
        .start_h = osrs_combat_projectile_value_or(
            profile->projectile_start_height, spec->fallback_start_h),
        .end_h = osrs_combat_projectile_value_or(
            profile->projectile_end_height, spec->fallback_end_h),
        .curve = curve,
        .arc_height = osrs_combat_projectile_profile_arc_height(
            profile, spec->attack_style),
        .src_size = spec->src_size,
        .dst_size = spec->dst_size,
        .model_id = (uint32_t)profile->projectile_model_id,
        .anim_id = profile->projectile_anim_id,
        .launch_gfx_id = osrs_combat_projectile_value_or(
            profile->launch_spotanim_id, 0),
        .impact_gfx_id = impact_gfx,
        .start_delay = spec->start_delay,
    };
    int idx = osrs_emit_projectile_with_spec(overlay, &projectile_spec, 1);
    encounter_set_projectile_source_player(overlay, idx);
    encounter_set_projectile_target_npc_slot(overlay, idx, spec->target_npc_slot);
    return idx;
}

static inline int osrs_emit_projectile_player_to_npc(
    EncounterOverlay* overlay,
    const OsrsProjectileEventSpec* spec,
    int target_npc_slot
) {
    int idx = osrs_emit_projectile_with_spec(overlay, spec, 1);
    encounter_set_projectile_source_player(overlay, idx);
    encounter_set_projectile_target_npc_slot(overlay, idx, target_npc_slot);
    return idx;
}

static inline int osrs_emit_projectile_npc_to_player(
    EncounterOverlay* overlay,
    const OsrsProjectileEventSpec* spec,
    int source_npc_slot
) {
    int idx = osrs_emit_projectile_with_spec(overlay, spec, 1);
    encounter_set_projectile_source_npc_slot(overlay, idx, source_npc_slot);
    return idx;
}

static inline int osrs_emit_projectile_npc_to_npc(
    EncounterOverlay* overlay,
    const OsrsProjectileEventSpec* spec,
    int source_npc_slot,
    int target_npc_slot
) {
    int idx = osrs_emit_projectile_with_spec(overlay, spec, 1);
    encounter_set_projectile_source_npc_slot(overlay, idx, source_npc_slot);
    encounter_set_projectile_target_npc_slot(overlay, idx, target_npc_slot);
    return idx;
}

#endif
