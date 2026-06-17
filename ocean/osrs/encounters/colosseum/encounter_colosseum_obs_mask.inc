#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_obs_mask.inc
 * @brief Obs section sizes + obs writer, mirroring inferno obs_mask.
 *
 * Obs layout (flat float vector, every feature egocentric + normalized):
 *   player(28) + inventory cells(28 * 16) + equipped self(11 * 12)
 *   + npc slots(24 * 30) + modifiers(49) + wave(4) + boss(51)
 *   + pending-hit forecast(16 * 7) + step-out forecast(25 * 8)
 *   + threat-LoS(3) + thrall/Death-Charge(6) = 1753.
 * P5 made the healing totem + bee swarm NPC-slot citizens (per-NPC type one-hot
 * 10 -> 12, A21+B9); the researched loadout pass added supply fractions + surge
 * + spec-armed one-hots to the player block; the loadout profile is implicit in
 * the gear-derived stats + supply normalization (inferno precedent).
 * The eval-fix pass raised COLO_OBS_NPCS 12 -> 24 (== COLO_MAX_NPCS, so a busy
 * wave never drops a live NPC from obs or the TARGET head) and grew the player
 * block 38 -> 45 (+5 current-vs-base offensive level deltas + 2 divine boost
 * timer fractions), so the policy can see its own boost-hold state.
 * col_write_obs_ctx hard-asserts the running index against the section constants
 * after each block and aborts on mismatch, exactly like inferno: this is the
 * contract that keeps the obs honest as the layout grows.
 *
 * The action mask, reward/terminal getters, and render-entity fill live in
 * encounter_colosseum_mask_render.inc (included right after this file) to keep
 * each .inc under ~500 lines.
 */

/* ----- obs section sizes ---------------------------------------------------- */

#define COLO_PLAYER_OBS_SIZE      28
#define COLO_INVENTORY_OBS_SIZE \
    (COLO_INVENTORY_DISPLAY_SLOTS * OSRS_INVENTORY_CELL_OBS_FEATURES)
#define COLO_EQUIPPED_SELF_OBS_SIZE \
    (NUM_GEAR_SLOTS * OSRS_EQUIPPED_SELF_OBS_FEATURES)
#define COLO_FEATURES_PER_NPC     30   /* 12-type one-hot + 8 scalars + 10 tells */
#define COLO_NPC_OBS_SIZE         (COLO_OBS_NPCS * COLO_FEATURES_PER_NPC)  /* 720 */
/** modifier obs = per-modifier active flag + tier (2 * 16) + draft pending + 3
    draft option ids (36), then a derived-hazard tail (13): Doom stacks, player
    venom, nearest bee swarm, the Solarflare orb tile, and the nearest non-boss
    molten pool. The tail gives the policy the modifier-driven state it must react
    to but cannot read from the static active/tier flags. */
#define COLO_MODIFIER_FLAGS_OBS_SIZE  (2 * COLO_NUM_MODIFIERS + 1 + COLO_MODIFIER_DRAFT_OPTIONS)  /* 36 */
#define COLO_MODIFIER_HAZARD_OBS_SIZE 13
#define COLO_MODIFIER_OBS_SIZE    (COLO_MODIFIER_FLAGS_OBS_SIZE + COLO_MODIFIER_HAZARD_OBS_SIZE)  /* 49 */
#define COLO_WAVE_OBS_SIZE        4
#define COLO_BOSS_OBS_SIZE        51
/** obs surfaces the soonest-landing pending hits only (the 32-deep queue rarely
    fills; 16 covers a jaguar triple + manticore barrage + a parry combo at once). */
#define COLO_OBS_PENDING_HITS     16
#define COLO_FEATURES_PER_HIT     7
#define COLO_PENDING_HIT_OBS_SIZE (COLO_FEATURES_PER_HIT * COLO_OBS_PENDING_HITS)  /* 112 */
/** current threat composition: count of live NPCs that can hit the player THIS
    tick (in attack range + line of sight), split into the 3 attack styles
    [magic, ranged, melee]. The direct de-confliction signal — the agent must
    position so at most one style is "live" (a same-tick multi-style stack is
    exactly what a single overhead prayer cannot block). */
#define COLO_THREAT_LOS_OBS_SIZE  3
/** Arceuus thrall + Death-Charge state (combat-fidelity pass), appended after
    THREAT_LOS: thrall_active, thrall_lifetime_norm, thrall_recast_norm, dc_armed,
    dc_window_norm, dc_cd_norm. The +15% spec from a Death-Charge proc surfaces
    downstream via the existing spec_energy obs. */
#define COLO_THRALL_DC_OBS_SIZE   6

#define COLO_NUM_OBS (COLO_PLAYER_OBS_SIZE + COLO_INVENTORY_OBS_SIZE + \
    COLO_EQUIPPED_SELF_OBS_SIZE + COLO_NPC_OBS_SIZE + COLO_MODIFIER_OBS_SIZE + \
    COLO_WAVE_OBS_SIZE + COLO_BOSS_OBS_SIZE + COLO_PENDING_HIT_OBS_SIZE + \
    COLO_STEP_OUT_FORECAST_OBS_SIZE + COLO_THREAT_LOS_OBS_SIZE + COLO_THRALL_DC_OBS_SIZE)

/* running-index checkpoints, asserted after each obs block. */
#define COLO_OBS_AFTER_PLAYER   COLO_PLAYER_OBS_SIZE
#define COLO_OBS_AFTER_INVENTORY (COLO_OBS_AFTER_PLAYER + COLO_INVENTORY_OBS_SIZE)
#define COLO_OBS_AFTER_EQUIPPED_SELF \
    (COLO_OBS_AFTER_INVENTORY + COLO_EQUIPPED_SELF_OBS_SIZE)
#define COLO_OBS_AFTER_NPCS     (COLO_OBS_AFTER_EQUIPPED_SELF + COLO_NPC_OBS_SIZE)
#define COLO_OBS_AFTER_MODS     (COLO_OBS_AFTER_NPCS + COLO_MODIFIER_OBS_SIZE)
#define COLO_OBS_AFTER_WAVE     (COLO_OBS_AFTER_MODS + COLO_WAVE_OBS_SIZE)
#define COLO_OBS_AFTER_BOSS     (COLO_OBS_AFTER_WAVE + COLO_BOSS_OBS_SIZE)
#define COLO_OBS_AFTER_PENDING_HITS (COLO_OBS_AFTER_BOSS + COLO_PENDING_HIT_OBS_SIZE)
#define COLO_OBS_AFTER_STEP_OUT_FORECAST \
    (COLO_OBS_AFTER_PENDING_HITS + COLO_STEP_OUT_FORECAST_OBS_SIZE)
#define COLO_OBS_AFTER_THREAT_LOS \
    (COLO_OBS_AFTER_STEP_OUT_FORECAST + COLO_THREAT_LOS_OBS_SIZE)
#define COLO_OBS_AFTER_THRALL_DC \
    (COLO_OBS_AFTER_THREAT_LOS + COLO_THRALL_DC_OBS_SIZE)


/* ----- normalization helpers ------------------------------------------------ */

static inline float col_clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/** egocentric x offset, normalized to [-1, 1] by arena width. */
static inline float col_obs_rel_x(int nx, int px) {
    return (float)(nx - px) / (float)COLO_ARENA_WIDTH;
}

static inline float col_obs_rel_y(int ny, int py) {
    return (float)(ny - py) / (float)COLO_ARENA_HEIGHT;
}

typedef struct {
    int found;
    int index;
    int x;
    int y;
    int distance;
} ColoNearestHazardTile;

static inline ColoNearestHazardTile col_nearest_hazard_tile_none(void) {
    return (ColoNearestHazardTile){
        .found = 0,
        .index = -1,
        .x = 0,
        .y = 0,
        .distance = 1 << 30,
    };
}

static inline void col_nearest_hazard_tile_consider(
    ColoNearestHazardTile* nearest,
    int index,
    int x,
    int y,
    int player_x,
    int player_y
) {
    int dx = abs(x - player_x);
    int dy = abs(y - player_y);
    int distance = dx > dy ? dx : dy;
    if (distance < nearest->distance) {
        nearest->found = 1;
        nearest->index = index;
        nearest->x = x;
        nearest->y = y;
        nearest->distance = distance;
    }
}

static inline void col_write_hazard_xy_value_if(
    const ColosseumState* s,
    int present,
    int x,
    int y,
    float value,
    float* obs,
    int* idx
) {
    int i = *idx;
    if (present) {
        obs[i++] = col_obs_rel_x(x, s->player.x);
        obs[i++] = col_obs_rel_y(y, s->player.y);
        obs[i++] = value;
    } else {
        obs[i++] = 0.0f;
        obs[i++] = 0.0f;
        obs[i++] = 0.0f;
    }
    *idx = i;
}

static inline void col_write_nearest_hazard_xy_value(
    const ColosseumState* s,
    const ColoNearestHazardTile* nearest,
    float value,
    float* obs,
    int* idx
) {
    col_write_hazard_xy_value_if(
        s, nearest->found, nearest->x, nearest->y, value, obs, idx);
}

static inline void col_write_nearest_hazard_xy_values(
    const ColosseumState* s,
    const ColoNearestHazardTile* nearest,
    float value0,
    float value1,
    float* obs,
    int* idx
) {
    int i = *idx;
    if (nearest->found) {
        obs[i++] = col_obs_rel_x(nearest->x, s->player.x);
        obs[i++] = col_obs_rel_y(nearest->y, s->player.y);
        obs[i++] = value0;
        obs[i++] = value1;
    } else {
        obs[i++] = 0.0f;
        obs[i++] = 0.0f;
        obs[i++] = 0.0f;
        obs[i++] = 0.0f;
    }
    *idx = i;
}


/** assign each active NPC to a fixed obs slot in array order. */
static void col_refresh_current_obs_slots_ctx(ColosseumState* s, const ColosseumContext* ctx) {
    (void)ctx;
    for (int i = 0; i < COLO_OBS_NPCS; i++) s->current_obs_slots[i] = -1;
    int slot = 0;
    for (int i = 0; i < COLO_MAX_NPCS && slot < COLO_OBS_NPCS; i++) {
        if (s->npcs[i].active) s->current_obs_slots[slot++] = i;
    }
}

static int col_obs_slot_is_targetable(const ColosseumState* s, int obs_slot) {
    int npc_idx = s->current_obs_slots[obs_slot];
    if (npc_idx < 0 || npc_idx >= COLO_MAX_NPCS) return 0;
    const ColoNPC* npc = &s->npcs[npc_idx];
    return col_npc_is_live_target(npc);
}


/* ----- per-NPC obs ---------------------------------------------------------- */

/** one-hot an attack style into a 3-wide [ranged, magic, melee] slice. */
static void col_tell_style_onehot(AttackStyle style, float* out3) {
    out3[0] = (style == ATTACK_STYLE_RANGED) ? 1.0f : 0.0f;
    out3[1] = (style == ATTACK_STYLE_MAGIC) ? 1.0f : 0.0f;
    out3[2] = (style == ATTACK_STYLE_MELEE) ? 1.0f : 0.0f;
}

/** write the 10-wide per-type tell block for an NPC slot (always 10 floats).

    manticore (the prayer puzzle — full upcoming barrage, like the orbs a player
    watches charge): [ticks-until-next-orb frac, then the REMAINING orbs as three
    [ranged,magic,melee] one-hots, slot 0 = the next orb to land]. During the
    charge-up the locked pattern shows all three orbs before any land; mid-barrage
    the fired orbs drop off so slot 0 is always the orb to pray next. All zero
    when idle (no pattern locked).
    javelin    -> [skyfall incoming, skyfall ticks frac, 0...]
    minotaur   -> [in heal-cast range (player not in its melee), 0...]
    jaguar     -> [multi-hit marker, 0...]
    everything else -> zeros. */
static void col_write_npc_tells(const ColosseumState* s, const ColoNPC* npc, float* obs, int* idx) {
    int i = *idx;
    float tells[10] = {0};
    switch (npc->type) {
        case COLO_MANTICORE: {
            const ColoManticoreState* mc = &npc->type_state.manticore;
            int firing = mc->cycle_step >= 0;
            int armed = firing || mc->orb_style[0] != ATTACK_STYLE_NONE;
            if (armed) {
                int start = firing ? mc->cycle_step : 0;
                int speed = COLO_NPC_STATS[npc->type].attack_speed;
                /* ticks until the next orb lands: the charge countdown while
                   arming, ~0 once firing (orbs are one tick apart). */
                tells[0] = firing ? 0.0f
                    : col_clamp01((float)npc->attack_timer / (float)(speed > 0 ? speed : 1));
                for (int slot = 0; slot < 3; slot++) {
                    int orb = start + slot;
                    if (orb < 3) col_tell_style_onehot(mc->orb_style[orb], &tells[1 + slot * 3]);
                }
            }
            break;
        }
        case COLO_JAVELIN_COLOSSUS: {
            const ColoJavelinState* jv = &npc->type_state.javelin;
            tells[0] = jv->skyfall_pending ? 1.0f : 0.0f;
            tells[1] = jv->skyfall_pending
                ? col_clamp01((float)jv->skyfall_timer / (float)COLO_JAVELIN_SKYFALL_DELAY)
                : 0.0f;
            break;
        }
        case COLO_MINOTAUR: {
            int size = col_npc_effective_size(npc);
            int dist = encounter_dist_to_npc(s->player.x, s->player.y, npc->x, npc->y, size);
            tells[0] = (dist > 1) ? 1.0f : 0.0f;  /* heals allies when player not in its melee */
            break;
        }
        case COLO_JAGUAR_WARRIOR:
            tells[0] = 1.0f;  /* multi-hit (3 independent rolls per attack) marker */
            break;
        default:
            break;
    }
    for (int t = 0; t < 10; t++) obs[i++] = tells[t];
    *idx = i;
}

/** write one NPC slot (COLO_FEATURES_PER_NPC floats). Caller has cleared obs to 0. */
static void col_write_npc_slot(const ColosseumState* s, const ColoNPC* npc, float* obs, int* idx) {
    int i = *idx;
    int px = s->player.x, py = s->player.y;
    const ColoNpcStats* ns = &COLO_NPC_STATS[npc->type];
    int size = col_npc_effective_size(npc);

    for (int t = 0; t < COLO_NUM_NPC_TYPES; t++)
        obs[i++] = (npc->type == t) ? 1.0f : 0.0f;
    obs[i++] = col_obs_rel_x(npc->x, px);
    obs[i++] = col_obs_rel_y(npc->y, py);
    obs[i++] = npc->max_hp > 0 ? col_clamp01((float)npc->hp / (float)npc->max_hp) : 0.0f;
    obs[i++] = (npc->attack_style == ATTACK_STYLE_MELEE) ? 1.0f : 0.0f;
    obs[i++] = (npc->attack_style == ATTACK_STYLE_RANGED) ? 1.0f : 0.0f;
    obs[i++] = (npc->attack_style == ATTACK_STYLE_MAGIC) ? 1.0f : 0.0f;
    obs[i++] = col_clamp01((float)npc->attack_timer / 8.0f);
    int dist = encounter_dist_to_npc(px, py, npc->x, npc->y, size);
    int range = ns->attack_range > 0 ? ns->attack_range : 1;
    obs[i++] = (dist >= 1 && dist <= range) ? 1.0f : 0.0f;
    *idx = i;
    col_write_npc_tells(s, npc, obs, idx);
}


/* ----- boss obs ------------------------------------------------------------- */

/** nearest in-flight crystal sphere to the player: writes dx, dy, ticks frac,
    present flag (4 floats). dx/dy are egocentric tile-to-player offsets. */
static void col_write_nearest_sphere(const ColosseumState* s, float* obs, int* idx) {
    ColoNearestHazardTile nearest = col_nearest_hazard_tile_none();
    for (int k = 0; k < COLO_SOL_SPHERE_QUEUE_MAX; k++) {
        const ColoSolSphere* sp = &s->sol.spheres[k];
        if (!sp->active) continue;
        col_nearest_hazard_tile_consider(
            &nearest, k, sp->tile_x, sp->tile_y, s->player.x, s->player.y);
    }
    float ticks_frac = nearest.found
        ? col_clamp01((float)s->sol.spheres[nearest.index].ticks_remaining /
              (float)COLO_SOL_SPHERE_DELAY)
        : 0.0f;
    col_write_nearest_hazard_xy_values(s, &nearest, ticks_frac, 1.0f, obs, idx);
}

/** nearest molten-sand pool to the player: dx, dy, present flag (3 floats). */
static void col_write_nearest_molten(const ColosseumState* s, float* obs, int* idx) {
    ColoNearestHazardTile nearest = col_nearest_hazard_tile_none();
    for (int k = 0; k < s->sol.hazard_tile_count; k++) {
        col_nearest_hazard_tile_consider(
            &nearest, k, s->sol.hazard_tile_x[k], s->sol.hazard_tile_y[k],
            s->player.x, s->player.y);
    }
    col_write_nearest_hazard_xy_value(s, &nearest, 1.0f, obs, idx);
}

/** is the player currently standing on a live telegraphed AoE hazard tile? */
static int col_player_in_aoe_danger(const ColosseumState* s) {
    const SolHereditState* sol = &s->sol;
    if (sol->aoe_attack == COLO_SOL_AOE_NONE) return 0;
    return col_sol_aoe_tile_is_hazard(sol, s->player.x, s->player.y);
}

/** nearest active crystal to the player: dx, dy, telegraphing flag, telegraph
    ticks frac (4 floats; zeros when no crystal is up). */
static void col_write_nearest_crystal(const ColosseumState* s, float* obs, int* idx) {
    ColoNearestHazardTile nearest = col_nearest_hazard_tile_none();
    for (int k = 0; k < s->sol.crystal_count; k++) {
        if (!s->sol.crystals[k].active) continue;
        int cx, cy;
        col_sol_crystal_pos(s, s->sol.crystals[k].perim_step, &cx, &cy);
        col_nearest_hazard_tile_consider(
            &nearest, k, cx, cy, s->player.x, s->player.y);
    }
    const ColoSolCrystal* c = nearest.found ? &s->sol.crystals[nearest.index] : NULL;
    col_write_nearest_hazard_xy_values(
        s,
        &nearest,
        c && c->telegraph_ticks > 0 ? 1.0f : 0.0f,
        c ? col_clamp01((float)c->telegraph_ticks / (float)COLO_SOL_CRYSTAL_TELEGRAPH) : 0.0f,
        obs,
        idx);
}

/** nearest active light beam to the player: dx, dy, ticks-to-pool frac,
    present flag (4 floats). */
static void col_write_nearest_beam(const ColosseumState* s, float* obs, int* idx) {
    ColoNearestHazardTile nearest = col_nearest_hazard_tile_none();
    for (int k = 0; k < COLO_SOL_BEAM_MAX; k++) {
        if (!s->sol.beams[k].active) continue;
        col_nearest_hazard_tile_consider(
            &nearest, k, s->sol.beams[k].x, s->sol.beams[k].y,
            s->player.x, s->player.y);
    }
    float ticks_frac = nearest.found
        ? col_clamp01((float)s->sol.beams[nearest.index].ticks_to_pool /
              (float)COLO_SOL_BEAM_TO_POOL_TICKS)
        : 0.0f;
    col_write_nearest_hazard_xy_values(s, &nearest, ticks_frac, 1.0f, obs, idx);
}

/** write the Sol Heredit boss block (COLO_BOSS_OBS_SIZE floats). Rich enough to
    learn the dodge / kite / triple-parry / grapple responses. All zeros
    pre-wave-12. */
static void col_write_boss_obs(const ColosseumState* s, float* obs, int* idx) {
    int i = *idx;
    const SolHereditState* sol = &s->sol;
    int px = s->player.x, py = s->player.y;

    obs[i++] = (float)sol->phase / (float)(COLO_SOL_NUM_PHASES - 1);
    for (int p = 0; p < COLO_SOL_NUM_PHASES; p++)
        obs[i++] = (sol->phase == p) ? 1.0f : 0.0f;

    /* live AoE one-hot (none/spear1/spear2/shield1/shield2) + age + danger +
       boss-relative player offset + line direction: the dodge cue set. */
    obs[i++] = (sol->aoe_attack == COLO_SOL_AOE_NONE) ? 1.0f : 0.0f;
    obs[i++] = (sol->aoe_attack == COLO_SOL_AOE_SPEAR1) ? 1.0f : 0.0f;
    obs[i++] = (sol->aoe_attack == COLO_SOL_AOE_SPEAR2) ? 1.0f : 0.0f;
    obs[i++] = (sol->aoe_attack == COLO_SOL_AOE_SHIELD1) ? 1.0f : 0.0f;
    obs[i++] = (sol->aoe_attack == COLO_SOL_AOE_SHIELD2) ? 1.0f : 0.0f;
    obs[i++] = col_clamp01((float)sol->aoe_age / (float)COLO_SOL_AOE_LIFETIME);
    obs[i++] = col_player_in_aoe_danger(s) ? 1.0f : 0.0f;
    if (sol->boss_idx >= 0) {
        const ColoNPC* boss = &s->npcs[sol->boss_idx];
        int bsz = boss->size > 0 ? boss->size : 5;
        obs[i++] = col_obs_rel_x(px, boss->x + bsz / 2);
        obs[i++] = col_obs_rel_y(py, boss->y + bsz / 2);
    } else {
        obs[i++] = 0.0f; obs[i++] = 0.0f;
    }
    obs[i++] = (float)sol->aoe_dir_x;
    obs[i++] = (float)sol->aoe_dir_y;

    /* attack engine tells (A1+A2): the kiting clock. */
    obs[i++] = col_clamp01((float)sol->attack_delay / 12.0f);
    obs[i++] = col_clamp01((float)sol->immobile_ticks / 10.0f);
    obs[i++] = sol->special_cooldown <= 0 ? 1.0f : 0.0f;
    obs[i++] = sol->force_spear ? 1.0f : 0.0f;

    /* triple parry: active, ticks-to-next-hit, next-hit damage, hits left,
       long-combo flag (15/30/45 from phase 3). */
    int next_land = 0, next_dmg = 0;
    for (int h = 0; h < COLO_SOL_PARRY_HITS; h++) {
        if (sol->parry_land_in[h] <= 0) continue;
        if (next_land == 0 || sol->parry_land_in[h] < next_land) {
            next_land = sol->parry_land_in[h];
            next_dmg = sol->parry_damage[h];
        }
    }
    obs[i++] = sol->parry_hits_left > 0 ? 1.0f : 0.0f;
    obs[i++] = col_clamp01((float)next_land / 10.0f);
    obs[i++] = col_clamp01((float)next_dmg / 45.0f);
    obs[i++] = col_clamp01((float)sol->parry_hits_left / (float)COLO_SOL_PARRY_HITS);
    obs[i++] = sol->phase >= 3 ? 1.0f : 0.0f;

    /* grapple: active, called-slot one-hot (the 5-slot A12 domain), timer,
       guaranteed-max-armed flag. */
    obs[i++] = sol->grapple_active ? 1.0f : 0.0f;
    for (int g = 0; g < COLO_NUM_GRAPPLE_SLOTS; g++)
        obs[i++] = (sol->grapple_active && sol->grapple_body_slot == g) ? 1.0f : 0.0f;
    obs[i++] = col_clamp01((float)sol->grapple_timer / (float)COLO_SOL_GRAPPLE_WINDOW);
    obs[i++] = sol->next_attack_guaranteed_max ? 1.0f : 0.0f;

    /* crystals: count + nearest (dx, dy, telegraphing, telegraph frac), then
       the nearest in-flight sphere (dx, dy, ticks, present). */
    obs[i++] = col_clamp01((float)sol->crystal_count / (float)COLO_SOL_MAX_CRYSTALS);
    *idx = i;
    col_write_nearest_crystal(s, obs, idx);
    col_write_nearest_sphere(s, obs, idx);

    /* nearest molten-sand pool (dx, dy, present) */
    col_write_nearest_molten(s, obs, idx);

    /* nearest phase-transition beam (dx, dy, ticks-to-pool, present) */
    col_write_nearest_beam(s, obs, idx);
}


/* ----- pending-hit forecast ------------------------------------------------- */

/** a pending hit ignores Protect prayer iff it is not flagged for a prayer check
    yet carries a non-zero typeless-style hit (javelin skyfall / Sol AoE land via
    encounter_damage_player, so they never enter the queue; queued ignore-prayer
    hits are the ones pushed with check_prayer=0 and a real style). */
static inline int col_pending_hit_ignores_prayer(const EncounterPendingHit* ph) {
    return (!ph->check_prayer && ph->attack_style != ATTACK_STYLE_NONE) ? 1 : 0;
}

/** order the player's pending hits by soonest landing into out[] (indices into
    the queue), returning how many were written (<= COLO_OBS_PENDING_HITS). */
static int col_collect_obs_pending_hits(const ColosseumState* s, int* out) {
    int n = 0;
    for (int h = 0; h < s->player_pending_hits.count && h < ENCOUNTER_MAX_PENDING_HITS; h++) {
        if (!s->player_pending_hits.hits[h].active) continue;
        out[n++] = h;
    }
    /* insertion sort by ticks_remaining (n is tiny). */
    for (int a = 1; a < n; a++) {
        int key = out[a];
        int kt = s->player_pending_hits.hits[key].ticks_remaining;
        int b = a - 1;
        while (b >= 0 && s->player_pending_hits.hits[out[b]].ticks_remaining > kt) {
            out[b + 1] = out[b];
            b--;
        }
        out[b + 1] = key;
    }
    return n < COLO_OBS_PENDING_HITS ? n : COLO_OBS_PENDING_HITS;
}


/** nearest non-boss molten pool (Reentry / Volatility) to the player: writes
    dx, dy, present (3 floats). */
static void col_write_nearest_modifier_molten(const ColosseumState* s, float* obs, int* idx) {
    ColoNearestHazardTile nearest = col_nearest_hazard_tile_none();
    for (int k = 0; k < s->molten_count; k++) {
        col_nearest_hazard_tile_consider(
            &nearest, k, s->molten_x[k], s->molten_y[k], s->player.x, s->player.y);
    }
    col_write_nearest_hazard_xy_value(s, &nearest, 1.0f, obs, idx);
}

/** write the modifier-hazard obs tail (COLO_MODIFIER_HAZARD_OBS_SIZE floats):
    Doom (active, stacks frac), player venom (active, frac), nearest bee swarm
    (dx, dy, present), Solarflare orb (dx, dy, active), nearest molten pool. */
static void col_write_modifier_hazards(const ColosseumState* s, float* obs, int* idx) {
    int i = *idx;
    int doom_tier = col_mod_tier(s, COLO_MOD_DOOM);
    int doom_cap = doom_tier > 0 ? COLO_DOOM_CAP[doom_tier] : 0;
    obs[i++] = doom_tier > 0 ? 1.0f : 0.0f;
    obs[i++] = doom_cap > 0 ? col_clamp01((float)s->doom_stacks / (float)doom_cap) : 0.0f;

    obs[i++] = s->player_venom > 0 ? 1.0f : 0.0f;
    obs[i++] = col_clamp01((float)s->player_venom / (float)COLO_VENOM_CAP);

    ColoNearestHazardTile nearest_bee = col_nearest_hazard_tile_none();
    for (int b = 0; b < COLO_MAX_BEE_SWARMS; b++) {
        if (s->bees[b].phase != COLO_HAZARD_ALIVE) continue;
        const ColoNPC* npc = &s->npcs[s->bees[b].npc_slot];
        if (!npc->active || npc->type != COLO_BEE_SWARM) continue;
        col_nearest_hazard_tile_consider(
            &nearest_bee, b, npc->x, npc->y, s->player.x, s->player.y);
    }
    *idx = i;
    col_write_nearest_hazard_xy_value(s, &nearest_bee, 1.0f, obs, idx);
    i = *idx;

    col_write_hazard_xy_value_if(
        s, s->solarflare.active, s->solarflare.x, s->solarflare.y, 1.0f, obs, &i);
    *idx = i;

    col_write_nearest_modifier_molten(s, obs, idx);
}


/* ----- top-level obs writer ------------------------------------------------- */

/** COLO_THREAT_LOS_OBS_SIZE floats: count of live NPCs that can currently hit the
    player (within attack range, and with line of sight for the ranged/magic ones),
    split by the style they threaten with [magic, ranged, melee], each normalized by
    a small cap. This is the direct de-confliction signal — when two of the three are
    nonzero, a single overhead prayer cannot cover the stack and the agent must
    reposition (LoS-block or offtick) to drive all-but-one style to zero. The
    manticore is counted as its next orb's style. */
static void col_write_threat_los_counts(const ColosseumState* s, float* obs, int* idx) {
    int i = *idx;
    int magic = 0, ranged = 0, melee = 0;
    for (int n = 0; n < COLO_MAX_NPCS; n++) {
        const ColoNPC* npc = &s->npcs[n];
        if (!col_npc_is_live_enemy(npc)) continue;
        const ColoNpcStats* ns = &COLO_NPC_STATS[npc->type];
        if (ns->attack_range < 1) continue;   /* non-attackers (e.g. healing totem) */
        int dist = col_npc_dist_to_player(s, npc);
        if (dist < 1 || dist > ns->attack_range) continue;
        if (ns->attack_range > 1 && !col_npc_has_los_to_player(s, npc)) continue;
        AttackStyle style = ns->default_style;
        if (npc->type == COLO_MANTICORE) {
            const ColoManticoreState* mc = &npc->type_state.manticore;
            int orb = mc->cycle_step >= 0 ? mc->cycle_step : 0;
            if (orb < 3 && mc->orb_style[orb] != ATTACK_STYLE_NONE) style = mc->orb_style[orb];
        }
        if (style == ATTACK_STYLE_MAGIC) magic++;
        else if (style == ATTACK_STYLE_RANGED) ranged++;
        else if (style == ATTACK_STYLE_MELEE) melee++;
    }
    obs[i++] = col_clamp01((float)magic / 4.0f);
    obs[i++] = col_clamp01((float)ranged / 4.0f);
    obs[i++] = col_clamp01((float)melee / 4.0f);
    *idx = i;
}

/** COLO_THRALL_DC_OBS_SIZE floats: the Arceuus thrall + Death-Charge state the
    policy needs to time summons/recasts and the kill-window. */
static void col_write_thrall_dc(const ColosseumState* s, float* obs, int* idx) {
    int i = *idx;
    int lifetime_full = COLO_THRALL_LIFETIME_TICKS[s->active_loadout_profile];
    obs[i++] = s->thrall_active ? 1.0f : 0.0f;
    obs[i++] = (s->thrall_active && lifetime_full > 0)
        ? col_clamp01((float)s->thrall_lifetime_left / (float)lifetime_full) : 0.0f;
    obs[i++] = col_clamp01((float)s->thrall_recast_cd / (float)COLO_THRALL_RECAST_CD);
    obs[i++] = s->death_charge_window_left > 0 ? 1.0f : 0.0f;
    obs[i++] = col_clamp01((float)s->death_charge_window_left / (float)COLO_DEATH_CHARGE_WINDOW);
    obs[i++] = col_clamp01((float)s->death_charge_cd / (float)COLO_DEATH_CHARGE_CD);
    *idx = i;
}

static void col_equipment_bonus_projection(
    const uint8_t equipment[NUM_GEAR_SLOTS],
    float out[6]
) {
    int slash = 0;
    int melee_str = 0;
    int ranged_att = 0;
    int ranged_str = 0;
    int magic = 0;
    int defence = 0;
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        uint8_t item_idx = equipment[slot];
        if (item_idx == ITEM_NONE) continue;
        if (item_idx >= NUM_ITEMS) {
            fprintf(stderr, "colosseum obs: invalid equipped item %u\n", item_idx);
            abort();
        }
        const Item* item = &ITEM_DATABASE[item_idx];
        slash += item->attack_slash;
        melee_str += item->melee_strength;
        ranged_att += item->attack_ranged;
        ranged_str += item->ranged_strength;
        magic += item->attack_magic + item->magic_damage;
        defence += item->defence_stab + item->defence_slash + item->defence_crush +
            item->defence_magic + item->defence_ranged;
    }
    out[0] = (float)slash / STAT_NORM_ATTACK;
    out[1] = (float)melee_str / STAT_NORM_STRENGTH;
    out[2] = (float)ranged_att / STAT_NORM_ATTACK;
    out[3] = (float)ranged_str / STAT_NORM_STRENGTH;
    out[4] = (float)magic / (STAT_NORM_ATTACK + STAT_NORM_MAGIC_DMG);
    out[5] = (float)defence / (5.0f * STAT_NORM_DEFENCE);
}

static void col_hypothetical_equipment_after_cell(
    const ColosseumState* s,
    int cell_idx,
    uint8_t out[NUM_GEAR_SLOTS]
) {
    memcpy(out, s->player.equipped, NUM_GEAR_SLOTS);
    const ColoInvCell* cell = &s->inventory_cells[cell_idx];
    if (cell->item_idx == ITEM_NONE) return;
    int gear_slot = osrs_item_gear_slot(cell->item_idx);
    if (gear_slot < 0) return;
    if (gear_slot == GEAR_SLOT_WEAPON && item_is_two_handed(cell->item_idx))
        out[GEAR_SLOT_SHIELD] = ITEM_NONE;
    if (gear_slot == GEAR_SLOT_SHIELD && item_is_two_handed(out[GEAR_SLOT_WEAPON]))
        out[GEAR_SLOT_WEAPON] = ITEM_NONE;
    out[gear_slot] = cell->item_idx;
}

static void col_inventory_cell_post_use_deltas(
    const ColosseumState* s,
    int cell_idx,
    float out[6]
) {
    for (int i = 0; i < 6; i++) out[i] = 0.0f;
    if (s->inventory_cells[cell_idx].item_idx == ITEM_NONE) return;

    float before[6];
    float after[6];
    uint8_t hypothetical[NUM_GEAR_SLOTS];
    col_equipment_bonus_projection(s->player.equipped, before);
    col_hypothetical_equipment_after_cell(s, cell_idx, hypothetical);
    col_equipment_bonus_projection(hypothetical, after);
    for (int i = 0; i < 6; i++) out[i] = after[i] - before[i];
}

static void col_write_inventory_and_equipped_obs(
    const ColosseumState* s,
    float* obs,
    int* idx
) {
    int i = *idx;
    for (int cell_idx = 0; cell_idx < COLO_INVENTORY_DISPLAY_SLOTS; cell_idx++) {
        float deltas[6];
        const ColoInvCell* cell = &s->inventory_cells[cell_idx];
        col_inventory_cell_post_use_deltas(s, cell_idx, deltas);
        osrs_write_inventory_cell_affordance_features(
            &obs[i],
            cell->item_idx,
            cell->osrs_id,
            cell->dose,
            col_inventory_cell_actionable(s, cell_idx),
            col_inventory_cell_holds_equipped_item(s, cell_idx),
            deltas);
        i += OSRS_INVENTORY_CELL_OBS_FEATURES;
    }
    assert(i == COLO_OBS_AFTER_INVENTORY);

    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        osrs_write_equipped_self_features(&obs[i], s->player.equipped[slot]);
        i += OSRS_EQUIPPED_SELF_OBS_FEATURES;
    }
    assert(i == COLO_OBS_AFTER_EQUIPPED_SELF);
    *idx = i;
}

static void col_write_obs_ctx(EncounterState* state, EncounterContext* context, float* obs) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
#ifdef COLO_PROFILE_ENABLED
    int col_prof_enabled = COLO_PROFILE_ENABLED();
    double col_prof_t0 = col_prof_enabled ? COLO_PROFILE_NOW_MS() : 0.0;
#endif
    col_refresh_current_obs_slots_ctx(s, ctx);
#ifdef COLO_PROFILE_ENABLED
    COLO_PROFILE_MARK(COLO_PROF_OBS_REFRESH_SLOTS);
#endif
    memset(obs, 0, COLO_NUM_OBS * sizeof(float));
    int i = 0;
    int px = s->player.x, py = s->player.y;
    const EncounterLoadoutStats* ls = col_live_loadout_stats(s);

    /* player block (28) */
    obs[i++] = col_clamp01((float)s->player.current_hitpoints / 99.0f);
    obs[i++] = (float)(px - COLO_ARENA_MIN_X) / (float)COLO_ARENA_WIDTH;   /* dist to west wall */
    obs[i++] = (float)(COLO_ARENA_MAX_X - px) / (float)COLO_ARENA_WIDTH;   /* dist to east wall */
    obs[i++] = (float)(py - COLO_ARENA_MIN_Y) / (float)COLO_ARENA_HEIGHT;  /* dist to south wall */
    obs[i++] = (float)(COLO_ARENA_MAX_Y - py) / (float)COLO_ARENA_HEIGHT;  /* dist to north wall */
    obs[i++] = (s->player.prayer == PRAYER_PROTECT_MELEE) ? 1.0f : 0.0f;
    obs[i++] = (s->player.prayer == PRAYER_PROTECT_RANGED) ? 1.0f : 0.0f;
    obs[i++] = (s->player.prayer == PRAYER_PROTECT_MAGIC) ? 1.0f : 0.0f;
    obs[i++] = (s->player.offensive_prayer == OFFENSIVE_PRAYER_PIETY) ? 1.0f : 0.0f;
    obs[i++] = (s->player.offensive_prayer == OFFENSIVE_PRAYER_RIGOUR) ? 1.0f : 0.0f;
    obs[i++] = (s->player.offensive_prayer == OFFENSIVE_PRAYER_AUGURY) ? 1.0f : 0.0f;
    obs[i++] = col_clamp01((float)s->player.current_prayer / 99.0f);
    obs[i++] = col_clamp01((float)s->player.run_energy / 10000.0f);
    obs[i++] = col_clamp01((float)s->player.special_energy / 100.0f);
    obs[i++] = s->player.spec_armed ? 1.0f : 0.0f;
    obs[i++] = col_clamp01((float)s->player.attack_timer / 8.0f);
    obs[i++] = col_clamp01((float)s->player.food_timer / 3.0f);
    obs[i++] = col_clamp01((float)s->player.potion_timer / 3.0f);
    obs[i++] = osrs_interaction_active(&s->interaction) ? 1.0f : 0.0f;
    obs[i++] = col_clamp01((float)ls->attack_range / 15.0f);
    obs[i++] = col_clamp01((float)ls->max_hit / 80.0f);
    obs[i++] = col_clamp01((float)ls->attack_speed / 6.0f);
    obs[i++] = col_clamp01((float)s->player.current_defence / 99.0f);
    obs[i++] = col_clamp01((float)ls->def_stab / 300.0f);
    obs[i++] = col_clamp01((float)ls->def_slash / 300.0f);
    obs[i++] = col_clamp01((float)ls->def_crush / 300.0f);
    obs[i++] = col_clamp01((float)ls->def_magic / 300.0f);
    obs[i++] = col_clamp01((float)ls->def_ranged / 300.0f);
    assert(i == COLO_OBS_AFTER_PLAYER);
#ifdef COLO_PROFILE_ENABLED
    COLO_PROFILE_MARK(COLO_PROF_OBS_PREFIX);
#endif

    col_write_inventory_and_equipped_obs(s, obs, &i);

    /* per-NPC slots (COLO_OBS_NPCS * COLO_FEATURES_PER_NPC) */
    for (int n = 0; n < COLO_OBS_NPCS; n++) {
        int npc_idx = s->current_obs_slots[n];
        if (npc_idx < 0 || npc_idx >= COLO_MAX_NPCS || !s->npcs[npc_idx].active) {
            i += COLO_FEATURES_PER_NPC;
            continue;
        }
        col_write_npc_slot(s, &s->npcs[npc_idx], obs, &i);
    }
    assert(i == COLO_OBS_AFTER_NPCS);
#ifdef COLO_PROFILE_ENABLED
    COLO_PROFILE_MARK(COLO_PROF_OBS_NPC_SLOTS);
#endif

    /* modifiers: per-modifier active flag + tier, then draft state */
    for (int m = 0; m < COLO_NUM_MODIFIERS; m++) {
        obs[i++] = (s->modifiers.active_mask & (1u << m)) ? 1.0f : 0.0f;
        obs[i++] = col_clamp01((float)s->modifiers.tier[m] / 3.0f);
    }
    obs[i++] = s->modifiers.draft_pending ? 1.0f : 0.0f;
    for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) {
        int opt = s->modifiers.draft_options[o];
        obs[i++] = (s->modifiers.draft_pending && opt >= 0)
            ? (float)(opt + 1) / (float)COLO_NUM_MODIFIERS : 0.0f;
    }
    col_write_modifier_hazards(s, obs, &i);
    assert(i == COLO_OBS_AFTER_MODS);
#ifdef COLO_PROFILE_ENABLED
    COLO_PROFILE_MARK(COLO_PROF_OBS_MODIFIERS);
#endif

    /* wave block (4). npcs_remaining tracks wave progress, so hazard entities
       (totems/bees — visible in the per-NPC slots) are excluded. */
    int npcs_remaining = 0;
    for (int n = 0; n < COLO_MAX_NPCS; n++)
        if (col_npc_is_live_enemy(&s->npcs[n])) npcs_remaining++;
    obs[i++] = col_clamp01((float)(s->wave + 1) / (float)COLO_NUM_WAVES);
    obs[i++] = col_clamp01((float)npcs_remaining / (float)COLO_MAX_NPCS);
    obs[i++] = (s->reinforcement_timer > 0)
        ? col_clamp01((float)s->reinforcement_timer / (float)COLO_REINFORCEMENT_TICKS) : 0.0f;
    obs[i++] = (s->wave_spawn_delay > 0 || s->wave_ready_delay > 0) ? 1.0f : 0.0f;
    assert(i == COLO_OBS_AFTER_WAVE);

    /* boss block (51) */
    col_write_boss_obs(s, obs, &i);
    assert(i == COLO_OBS_AFTER_BOSS);
#ifdef COLO_PROFILE_ENABLED
    COLO_PROFILE_MARK(COLO_PROF_OBS_BOSS);
#endif

    /* pending-hit forecast on the player (soonest-landing first) */
    int order[ENCOUNTER_MAX_PENDING_HITS];
    int nhits = col_collect_obs_pending_hits(s, order);
    for (int h = 0; h < COLO_OBS_PENDING_HITS; h++) {
        if (h < nhits) {
            const EncounterPendingHit* ph = &s->player_pending_hits.hits[order[h]];
            obs[i++] = 1.0f;
            obs[i++] = (ph->attack_style == ATTACK_STYLE_RANGED) ? 1.0f : 0.0f;
            obs[i++] = (ph->attack_style == ATTACK_STYLE_MAGIC) ? 1.0f : 0.0f;
            obs[i++] = (ph->attack_style == ATTACK_STYLE_MELEE) ? 1.0f : 0.0f;
            obs[i++] = col_clamp01((float)ph->ticks_remaining / 10.0f);
            obs[i++] = col_clamp01((float)ph->damage / 75.0f);
            obs[i++] = col_pending_hit_ignores_prayer(ph) ? 1.0f : 0.0f;
        } else {
            i += COLO_FEATURES_PER_HIT;
        }
    }
    assert(i == COLO_OBS_AFTER_PENDING_HITS);
#ifdef COLO_PROFILE_ENABLED
    COLO_PROFILE_MARK(COLO_PROF_OBS_PENDING_HITS);
#endif

    if (ctx->config.step_out_forecast_obs_enabled) {
        int horizon = ctx->config.forecast_horizon;
        if (horizon < 1) horizon = 1;
        if (horizon > COLO_STEP_OUT_FORECAST_HORIZON)
            horizon = COLO_STEP_OUT_FORECAST_HORIZON;
        ColoStepOutForecast forecast;
        col_build_step_out_forecast_horizon_mode(
            s, &forecast, horizon, ctx->config.forecast_run_tile_mode);
        for (int action_idx = 0; action_idx < ENCOUNTER_MOVE_ACTIONS; action_idx++) {
            const ColoStepOutForecastAction* action = &forecast.actions[action_idx];
            int first_attack_tick = 0;
            int first_style_mask = 0;
            int max_hit = 0;
            int ranged_magic_same_tick = 0;
            for (int tick_idx = 0; tick_idx < horizon; tick_idx++) {
                const ColoStepOutForecastTick* tick = &action->ticks[tick_idx];
                int style_mask = col_step_out_forecast_tick_style_mask(tick);
                if (first_attack_tick == 0 &&
                        col_step_out_forecast_tick_has_event(tick)) {
                    first_attack_tick = tick_idx + 1;
                    first_style_mask = style_mask;
                }
                if (tick->max_hit > max_hit) max_hit = tick->max_hit;
                if (tick->ranged_count > 0 && tick->magic_count > 0)
                    ranged_magic_same_tick = 1;
            }
            obs[i++] = action->valid ? 1.0f : 0.0f;
            obs[i++] = (float)first_attack_tick / (float)horizon;
            obs[i++] = (float)first_style_mask / 7.0f;
            obs[i++] = (float)max_hit / 150.0f;
            obs[i++] = action->same_tick_mixed_style_conflict ? 1.0f : 0.0f;
            obs[i++] = ranged_magic_same_tick ? 1.0f : 0.0f;
            obs[i++] = action->ranged_magic_offtick_opportunity ? 1.0f : 0.0f;
            obs[i++] = action->melee_fallback_exposure ? 1.0f : 0.0f;
        }
    } else {
        i += COLO_STEP_OUT_FORECAST_OBS_SIZE;
    }
    assert(i == COLO_OBS_AFTER_STEP_OUT_FORECAST);
#ifdef COLO_PROFILE_ENABLED
    COLO_PROFILE_MARK(COLO_PROF_OBS_FORECAST);
#endif

    col_write_threat_los_counts(s, obs, &i);
    assert(i == COLO_OBS_AFTER_THREAT_LOS);

    col_write_thrall_dc(s, obs, &i);
    assert(i == COLO_OBS_AFTER_THRALL_DC);

    if (i != COLO_NUM_OBS) {
        fprintf(stderr, "colosseum obs index mismatch: wrote %d, expected %d\n", i, COLO_NUM_OBS);
        abort();
    }
}
