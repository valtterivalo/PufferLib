/**
 * @fileoverview Visual effect system for spell impacts and projectiles.
 *
 * Manages animated spotanim effects (ice barrage splash, blood barrage) and
 * traveling projectiles (crossbow bolts, ice barrage orb). Each effect has a
 * model, animation, position, and lifetime. Projectiles follow parabolic arcs
 * matching OSRS SceneProjectile.java trajectory math.
 *
 * Effects are spawned from game state in render_post_tick and drawn as 3D
 * models in the render pipeline. Animation advances at 50 Hz client ticks.
 */

#ifndef OSRS_PVP_EFFECTS_H
#define OSRS_PVP_EFFECTS_H

#include "osrs_models.h"
#include "osrs_anim.h"
#include "osrs_spotanims.h"
#include "osrs_gfx_ids.h"
#include <math.h>

#define MAX_ACTIVE_EFFECTS 16

static const OsrsSpotAnimDef* spotanim_lookup(
    const OsrsSpotAnimSet* spotanims,
    int gfx_id
) {
    return osrs_spotanim_find(spotanims, gfx_id);
}

static OsrsModel* effect_find_model_in_cache(ModelCache* cache, uint32_t model_id) {
    if (!cache) return NULL;
    return model_cache_get(cache, model_id);
}

static OsrsModel* effect_find_model(
    const OsrsSpotAnimDef* meta,
    ModelCache* model_cache,
    ModelCache* secondary_model_cache,
    ModelCache* projectile_model_cache
) {
    if (!meta || meta->model_id < 0) return NULL;
    uint32_t model_ids[3] = {
        OSRS_SPOTANIM_MODEL_BASE + meta->id,
        OSRS_SPOTANIM_RECOLOR_MODEL_BASE | meta->id,
        (uint32_t)meta->model_id,
    };
    ModelCache* caches[3] = {
        projectile_model_cache,
        model_cache,
        secondary_model_cache,
    };
    for (int m = 0; m < 3; m++) {
        for (int c = 0; c < 3; c++) {
            OsrsModel* om = effect_find_model_in_cache(caches[c], model_ids[m]);
            if (om) return om;
        }
    }
    return NULL;
}


typedef enum {
    EFFECT_NONE = 0,
    EFFECT_SPOTANIM,     /* plays at a fixed position (impact effects) */
    EFFECT_PROJECTILE,   /* travels from source to target with parabolic arc */
} EffectType;

typedef struct {
    EffectType type;
    int gfx_id;
    const OsrsSpotAnimDef* meta;

    /* world position in sub-tile coords (128 units per tile) */
    double src_x, src_y;      /* start (projectiles) */
    double dst_x, dst_y;      /* end (projectiles) or position (spotanims) */
    double cur_x, cur_y;      /* current interpolated position */
    double height;             /* current height in sub-tile units */

    /* projectile trajectory (from SceneProjectile.java) */
    double x_increment;
    double y_increment;
    double diagonal_increment;
    double height_increment;
    double height_accel;       /* aDouble1578: parabolic curvature */
    int start_height;          /* sub-tile units */
    int end_height;
    int initial_slope;         /* trajectory arc angle */

    /* timing in client ticks (50 Hz) */
    int start_tick;
    int stop_tick;
    int started;               /* has calculateIncrements been called? */

    /* animation state */
    int anim_frame;
    int anim_tick_counter;
    AnimModelState* anim_state;  /* per-effect vertex transform state (heap) */

    /* orientation */
    int turn_value;            /* 0-2047 OSRS angle units */
    int tilt_angle;
} ActiveEffect;


/** Free an effect's animation state and mark it inactive. */
static void effect_free(ActiveEffect* e) {
    if (e->anim_state) {
        anim_model_state_free(e->anim_state);
        e->anim_state = NULL;
    }
    e->type = EFFECT_NONE;
}

/** Find a free effect slot, evicting the oldest if full. */
static int effect_find_slot(ActiveEffect effects[MAX_ACTIVE_EFFECTS]) {
    for (int i = 0; i < MAX_ACTIVE_EFFECTS; i++) {
        if (effects[i].type == EFFECT_NONE) return i;
    }
    /* evict oldest */
    int oldest = 0;
    for (int i = 1; i < MAX_ACTIVE_EFFECTS; i++) {
        if (effects[i].start_tick < effects[oldest].start_tick) oldest = i;
    }
    effect_free(&effects[oldest]);
    return oldest;
}

/** Create AnimModelState for an effect's model (if it has animation data). */
static void effect_init_anim_state(
    ActiveEffect* e,
    ModelCache* model_cache,
    ModelCache* secondary_model_cache,
    ModelCache* projectile_model_cache
) {
    if (!e->meta || e->meta->animation_id < 0) return;

    OsrsModel* om = effect_find_model(
        e->meta, model_cache, secondary_model_cache, projectile_model_cache);
    if (!om || !om->vertex_skins || om->base_vert_count == 0) return;

    e->anim_state = anim_model_state_create_with_face_alpha(
        om->vertex_skins, om->base_vert_count,
        om->face_alpha_labels, om->base_face_alphas, om->mesh.triangleCount);
}


/**
 * Spawn a spotanim effect at a world position (impact splash, etc).
 * Duration is determined by the animation length, or a fixed 30 client ticks
 * for static models.
 */
static int effect_spawn_spotanim_subtile(
    ActiveEffect effects[MAX_ACTIVE_EFFECTS],
    int gfx_id,
    float subtile_x, float subtile_y,
    int current_client_tick,
    const OsrsSpotAnimSet* spotanims,
    AnimCache* anim_cache,
    ModelCache* model_cache,
    ModelCache* secondary_model_cache,
    ModelCache* projectile_model_cache
) {
    const OsrsSpotAnimDef* meta = spotanim_lookup(spotanims, gfx_id);
    if (!meta) return -1;

    int slot = effect_find_slot(effects);
    ActiveEffect* e = &effects[slot];
    memset(e, 0, sizeof(ActiveEffect));
    e->type = EFFECT_SPOTANIM;
    e->gfx_id = gfx_id;
    e->meta = meta;

    e->cur_x = subtile_x;
    e->cur_y = subtile_y;
    e->height = 0;

    e->start_tick = current_client_tick;

    /* duration from animation, or 30 client ticks default */
    int duration = 30;
    if (meta->animation_id >= 0 && anim_cache) {
        AnimSequence* seq = anim_get_sequence(anim_cache, meta->animation_id);
        if (seq) {
            duration = 0;
            for (int f = 0; f < seq->frame_count; f++) {
                duration += seq->frames[f].delay;
            }
        }
    }
    e->stop_tick = current_client_tick + duration;

    effect_init_anim_state(e, model_cache, secondary_model_cache, projectile_model_cache);
    return slot;
}

/* convenience wrapper: integer tile coords → sub-tile center */
static int effect_spawn_spotanim(
    ActiveEffect effects[MAX_ACTIVE_EFFECTS],
    int gfx_id, int world_x, int world_y,
    int current_client_tick, const OsrsSpotAnimSet* spotanims,
    AnimCache* anim_cache, ModelCache* model_cache,
    ModelCache* secondary_model_cache, ModelCache* projectile_model_cache
) {
    return effect_spawn_spotanim_subtile(effects, gfx_id,
        world_x * 128.0f + 64.0f, world_y * 128.0f + 64.0f,
        current_client_tick, spotanims, anim_cache, model_cache,
        secondary_model_cache, projectile_model_cache);
}

/**
 * Spawn a traveling projectile from source to target position.
 *
 * Trajectory math from SceneProjectile.java calculateIncrements/progressCycles:
 * - parabolic height arc controlled by initialSlope
 * - position advances linearly per client tick
 * - height has quadratic acceleration term
 */
static int effect_spawn_projectile(
    ActiveEffect effects[MAX_ACTIVE_EFFECTS],
    int gfx_id,
    int src_world_x, int src_world_y,
    int dst_world_x, int dst_world_y,
    int delay_client_ticks,
    int duration_client_ticks,
    int start_height_subtile,
    int end_height_subtile,
    int slope,
    int current_client_tick,
    const OsrsSpotAnimSet* spotanims,
    ModelCache* model_cache,
    ModelCache* secondary_model_cache,
    ModelCache* projectile_model_cache
) {
    const OsrsSpotAnimDef* meta = spotanim_lookup(spotanims, gfx_id);
    if (!meta) return -1;

    int slot = effect_find_slot(effects);
    ActiveEffect* e = &effects[slot];
    memset(e, 0, sizeof(ActiveEffect));
    e->type = EFFECT_PROJECTILE;
    e->gfx_id = gfx_id;
    e->meta = meta;

    e->src_x = src_world_x * 128.0 + 64.0;
    e->src_y = src_world_y * 128.0 + 64.0;
    e->dst_x = dst_world_x * 128.0 + 64.0;
    e->dst_y = dst_world_y * 128.0 + 64.0;
    e->cur_x = e->src_x;
    e->cur_y = e->src_y;
    e->start_height = start_height_subtile;
    e->end_height = end_height_subtile;
    e->height = start_height_subtile;
    e->initial_slope = slope;
    e->started = 0;

    e->start_tick = current_client_tick + delay_client_ticks;
    e->stop_tick = current_client_tick + delay_client_ticks + duration_client_ticks;

    effect_init_anim_state(e, model_cache, secondary_model_cache, projectile_model_cache);
    return slot;
}

/**
 * Advance all active effects by one client tick (20ms).
 * Call this from the 50 Hz client-tick loop.
 */
static void effect_client_tick(
    ActiveEffect effects[MAX_ACTIVE_EFFECTS],
    int current_client_tick,
    AnimCache* anim_cache
) {
    for (int i = 0; i < MAX_ACTIVE_EFFECTS; i++) {
        ActiveEffect* e = &effects[i];
        if (e->type == EFFECT_NONE) continue;

        /* expired? */
        if (current_client_tick >= e->stop_tick) {
            effect_free(e);
            continue;
        }

        /* not started yet (delayed projectile) */
        if (current_client_tick < e->start_tick) continue;

        if (e->type == EFFECT_PROJECTILE) {
            if (!e->started) {
                /* calculateIncrements (SceneProjectile.java:37-58) */
                e->cur_x = e->src_x;
                e->cur_y = e->src_y;
                e->height = e->start_height;

                double cycles_left = (double)(e->stop_tick + 1 - current_client_tick);
                e->x_increment = (e->dst_x - e->cur_x) / cycles_left;
                e->y_increment = (e->dst_y - e->cur_y) / cycles_left;
                e->diagonal_increment = sqrt(
                    e->x_increment * e->x_increment +
                    e->y_increment * e->y_increment
                );

                e->height_increment = -e->diagonal_increment *
                    tan((double)e->initial_slope * 0.02454369);
                e->height_accel = 2.0 * (
                    (double)e->end_height - e->height -
                    e->height_increment * cycles_left
                ) / (cycles_left * cycles_left);

                e->started = 1;
            }

            /* progressCycles (SceneProjectile.java:100-118) */
            e->cur_x += e->x_increment;
            e->cur_y += e->y_increment;
            e->height += e->height_increment + 0.5 * e->height_accel;
            e->height_increment += e->height_accel;

            /* update orientation */
            e->turn_value = (int)(atan2(e->x_increment, e->y_increment) *
                325.949) + 1024;
            e->turn_value &= 0x7FF;
            e->tilt_angle = (int)(atan2(e->height_increment,
                e->diagonal_increment) * 325.949);
            e->tilt_angle &= 0x7FF;
        }

        /* advance animation */
        if (e->meta && e->meta->animation_id >= 0 && anim_cache) {
            AnimSequence* seq = anim_get_sequence(anim_cache, e->meta->animation_id);
            if (seq && seq->frame_count > 0) {
                e->anim_tick_counter++;
                while (e->anim_tick_counter >= seq->frames[e->anim_frame].delay) {
                    e->anim_tick_counter -= seq->frames[e->anim_frame].delay;
                    e->anim_frame++;
                    if (e->anim_frame >= seq->frame_count) {
                        e->anim_frame = 0;
                    }
                }
            }
        }
    }
}

/**
 * Clear all active effects (on episode reset).
 */
static void effect_clear_all(ActiveEffect effects[MAX_ACTIVE_EFFECTS]) {
    for (int i = 0; i < MAX_ACTIVE_EFFECTS; i++) {
        effect_free(&effects[i]);
    }
}

#endif /* OSRS_PVP_EFFECTS_H */
