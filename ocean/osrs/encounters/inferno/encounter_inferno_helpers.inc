#line 862 "encounter_inferno.h"
/* prayer check and RNG: use shared encounter_prayer_correct_for_style(),
   encounter_rand_int(), encounter_rand_float() from osrs_combat.h */

static void inf_clear_npc_pending_hits(InfNPC* npc) {
    memset(npc->pending_hits, 0, sizeof(npc->pending_hits));
    npc->pending_hit_count = 0;
}

static EncounterPendingHit* inf_queue_npc_pending_hit(
    InfernoState* s,
    int npc_idx,
    int ticks_remaining,
    int damage,
    AttackStyle attack_style,
    int spell_type,
    int hit_success
) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) {
        fprintf(stderr, "inferno: invalid pending-hit npc slot %d\n", npc_idx);
        abort();
    }

    InfNPC* npc = &s->npcs[npc_idx];
    if (!npc->active) {
        fprintf(stderr,
            "inferno: pending-hit queued for inactive npc tick=%d slot=%d "
            "type=%d delay=%d style=%d spell=%d damage=%d\n",
            s->tick, npc_idx, npc->type, ticks_remaining, attack_style,
            spell_type, damage);
        abort();
    }

    if (npc->pending_hit_count >= ENCOUNTER_MAX_PENDING_HITS) {
        fprintf(stderr,
            "inferno: npc pending-hit queue overflow tick=%d slot=%d type=%d "
            "count=%d delay=%d style=%d spell=%d damage=%d\n",
            s->tick, npc_idx, npc->type, npc->pending_hit_count,
            ticks_remaining, attack_style, spell_type, damage);
        abort();
    }

    EncounterPendingHit* ph = &npc->pending_hits[npc->pending_hit_count++];
    *ph = (EncounterPendingHit){
        .active = 1,
        .damage = damage,
        .ticks_remaining = ticks_remaining,
        .attack_style = attack_style,
        .check_prayer = 0,
        .spell_type = spell_type,
        .hit_success = hit_success,
        .elysian_reduced = 0,
    };
    return ph;
}

static void inf_remove_npc_pending_hit(InfNPC* npc, int idx) {
    if (idx < 0 || idx >= npc->pending_hit_count) {
        fprintf(stderr, "inferno: invalid pending-hit remove idx %d count %d\n",
            idx, npc->pending_hit_count);
        abort();
    }
    for (int i = idx + 1; i < npc->pending_hit_count; i++)
        npc->pending_hits[i - 1] = npc->pending_hits[i];
    npc->pending_hit_count--;
    memset(&npc->pending_hits[npc->pending_hit_count], 0,
        sizeof(npc->pending_hits[npc->pending_hit_count]));
}

static const EncounterPendingHit* inf_earliest_npc_pending_hit(
    const InfNPC* npc
) {
    const EncounterPendingHit* best = NULL;
    for (int i = 0; i < npc->pending_hit_count; i++) {
        const EncounterPendingHit* ph = &npc->pending_hits[i];
        if (!ph->active) continue;
        if (!best || ph->ticks_remaining < best->ticks_remaining)
            best = ph;
    }
    return best;
}

static int inf_npc_pending_hit_damage_sum(const InfNPC* npc) {
    int total = 0;
    for (int i = 0; i < npc->pending_hit_count; i++) {
        if (npc->pending_hits[i].active)
            total += npc->pending_hits[i].damage;
    }
    return total;
}

static void inf_shuffle_spawns(InfernoState* s) {
    for (int i = 0; i < INF_NUM_SPAWN_POS; i++)
        s->spawn_order[i] = i;
    encounter_shuffle(s->spawn_order, INF_NUM_SPAWN_POS, &s->rng_state);
}


static void inf_rebuild_los(InfernoState* s) {
    s->los_blocker_count = 0;
    for (int i = 0; i < INF_NUM_PILLARS; i++) {
        if (s->pillars[i].active) {
            LOSBlocker* b = &s->los_blockers[s->los_blocker_count++];
            b->x = s->pillars[i].x;
            b->y = s->pillars[i].y;
            b->size = INF_PILLAR_SIZE;
            b->los_mask = LOS_FULL_MASK;
        }
    }
}

typedef struct {
    int x;
    int y;
    int size;
    int is_player;
} InfTargetArea;

static InfTargetArea inf_npc_current_target_area(const InfernoState* s, const InfNPC* npc) {
    if (npc->type == INF_NPC_NIBBLER) {
        int pillar_idx = s->nibbler_target_pillar;
        if (pillar_idx >= 0 && pillar_idx < INF_NUM_PILLARS &&
            s->pillars[pillar_idx].active) {
            return (InfTargetArea){
                .x = s->pillars[pillar_idx].x,
                .y = s->pillars[pillar_idx].y,
                .size = INF_PILLAR_SIZE,
                .is_player = 0,
            };
        }
        for (int i = 0; i < INF_NUM_PILLARS; i++) {
            if (s->pillars[i].active) {
                return (InfTargetArea){
                    .x = s->pillars[i].x,
                    .y = s->pillars[i].y,
                    .size = INF_PILLAR_SIZE,
                    .is_player = 0,
                };
            }
        }
    }

    if (npc->aggro_target >= 0 && npc->aggro_target < INF_MAX_NPCS &&
        s->npcs[npc->aggro_target].active) {
        const InfNPC* target = &s->npcs[npc->aggro_target];
        return (InfTargetArea){
            .x = target->x,
            .y = target->y,
            .size = target->size,
            .is_player = 0,
        };
    }

    return (InfTargetArea){
        .x = s->player.x,
        .y = s->player.y,
        .size = 1,
        .is_player = 1,
    };
}

/* check if NPC at index i has LOS to its current target */
static int inf_npc_has_los_direct(InfernoState* s, int i) {
    InfNPC* npc = &s->npcs[i];
    const InfNPCStats* stats = &INF_NPC_STATS[npc->type];
    InfTargetArea target = inf_npc_current_target_area(s, npc);
    return entity_has_line_of_sight(s->los_blockers, s->los_blocker_count,
                                    npc->x, npc->y, npc->size,
                                    target.x, target.y, target.size,
                                    stats->attack_range);
}

/* cached LOS check — lazy: computes on first access per tick, caches for reuse.
   cache entries: -1 = not yet computed, 0 = no LOS, 1 = has LOS. */
static int inf_npc_has_los(InfernoState* s, int i) {
    if (s->npc_los_cache[i] >= 0)
        return s->npc_los_cache[i];
    int result = inf_npc_has_los_direct(s, i);
    s->npc_los_cache[i] = (int8_t)result;
    return result;
}

/* invalidate entire LOS cache (call at start of tick and on pillar collapse) */
static inline void inf_invalidate_los_cache(InfernoState* s) {
    memset(s->npc_los_cache, -1, sizeof(s->npc_los_cache));
}

static inline void inf_invalidate_npc_los_cache(InfernoState* s, int i) {
    s->npc_los_cache[i] = -1;
}

static inline int inf_is_final_wave(const InfernoState* s) {
    return s->wave == INF_NUM_WAVES - 1;
}

static inline int inf_zuk_healer_tags_first_reward_gate_active(
    const InfernoState* s,
    const InfernoContext* ctx
) {
    return ctx->config.zuk_healer_reward_mode == INF_ZUK_HEALER_REWARD_MODE_TAGS_FIRST &&
        s->zuk.healer_spawned && s->total_zuk_healer_tags < 4;
}

static int inf_find_live_zuk_idx(const InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (s->npcs[i].active && s->npcs[i].type == INF_NPC_ZUK) return i;
    }
    return -1;
}

static int inf_weapon_set_is_valid(const InfernoState* s) {
    return s->weapon_set >= 0 && s->weapon_set < INF_NUM_WEAPON_SETS;
}

typedef enum {
    INF_HEALER_DIAG_OFF = 0,
    INF_HEALER_DIAG_PRE_HEALER = 1,
    INF_HEALER_DIAG_IMMEDIATE_HEALER = 2,
    INF_HEALER_DIAG_PARTIAL_HEALER = 3,
    INF_HEALER_DIAG_POST_HEALER = 4,
    INF_HEALER_DIAG_POST_150 = 5,
    INF_HEALER_DIAG_H0_PRE_HEALER_TIGHT = 6,
    INF_HEALER_DIAG_H4_HEALER_OVERLAP = 7,
    INF_HEALER_DIAG_POST_HEALER_SET_ALIVE = 8,
    INF_HEALER_DIAG_COUNT = 9,
} InfHealerDiagnosticPhase;

typedef struct {
    int live_count;
    int tagged_count;
    int healing_count;
} InfZukHealerCounts;

static InfZukHealerCounts inf_zuk_healer_counts(const InfernoState* s) {
    InfZukHealerCounts counts;
    memset(&counts, 0, sizeof(counts));

    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->hp <= 0 || npc->type != INF_NPC_HEALER_ZUK)
            continue;
        counts.live_count++;
        if (npc->aggro_target >= 0) counts.healing_count++;
        else counts.tagged_count++;
    }

    return counts;
}

static int inf_is_live_zuk_healer_slot(const InfernoState* s, int npc_slot) {
    if (npc_slot < 0 || npc_slot >= INF_MAX_NPCS) return 0;
    const InfNPC* npc = &s->npcs[npc_slot];
    return npc->active &&
        npc->hp > 0 &&
        npc->death_ticks == 0 &&
        npc->type == INF_NPC_HEALER_ZUK;
}

static int inf_has_live_set_or_jad(const InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->hp <= 0) continue;
        if (npc->type == INF_NPC_JAD ||
                npc->type == INF_NPC_MAGER ||
                npc->type == INF_NPC_MELEER ||
                npc->type == INF_NPC_RANGER) {
            return 1;
        }
    }
    return 0;
}

static int inf_npc_type_is_set_pressure(int type) {
    return type == INF_NPC_MAGER ||
        type == INF_NPC_MELEER ||
        type == INF_NPC_RANGER;
}

static int inf_has_live_set_pressure(const InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks != 0 || npc->hp <= 0)
            continue;
        if (inf_npc_type_is_set_pressure(npc->type)) {
            return 1;
        }
    }
    return 0;
}

static int inf_healer_diagnostic_phase_matches(
    const InfernoState* s,
    int diagnostic_phase
) {
    if (diagnostic_phase == INF_HEALER_DIAG_OFF) return 1;
    if (s->episode_over) return 0;
    if (!inf_weapon_set_is_valid(s)) return 0;

    int zuk_idx = inf_find_live_zuk_idx(s);
    if (zuk_idx < 0 || !inf_is_final_wave(s)) return 0;

    int zuk_hp = s->npcs[zuk_idx].hp;
    InfZukHealerCounts healers = inf_zuk_healer_counts(s);

    switch ((InfHealerDiagnosticPhase)diagnostic_phase) {
        case INF_HEALER_DIAG_PRE_HEALER:
            return !s->zuk.healer_spawned && zuk_hp > 240 && zuk_hp <= 300;
        case INF_HEALER_DIAG_IMMEDIATE_HEALER:
            return s->zuk.healer_spawned &&
                healers.live_count == 4 &&
                healers.healing_count == 4;
        case INF_HEALER_DIAG_PARTIAL_HEALER:
            return s->zuk.healer_spawned &&
                healers.live_count > 0 &&
                (healers.live_count < 4 || healers.tagged_count > 0);
        case INF_HEALER_DIAG_POST_HEALER:
            return s->zuk.healer_spawned && healers.live_count == 0;
        case INF_HEALER_DIAG_POST_150:
            return s->zuk.healer_spawned &&
                (zuk_hp <= 150 ||
                 (s->min_zuk_hp_seen > 0.0f && s->min_zuk_hp_seen <= 150.0f));
        case INF_HEALER_DIAG_H0_PRE_HEALER_TIGHT:
            return !s->zuk.healer_spawned && zuk_hp > 240 && zuk_hp <= 260;
        case INF_HEALER_DIAG_H4_HEALER_OVERLAP:
            return s->zuk.healer_spawned &&
                healers.live_count > 0 &&
                inf_has_live_set_or_jad(s);
        case INF_HEALER_DIAG_POST_HEALER_SET_ALIVE:
            return s->zuk.healer_spawned &&
                healers.live_count == 0 &&
                inf_has_live_set_pressure(s);
        case INF_HEALER_DIAG_OFF:
            return 1;
        case INF_HEALER_DIAG_COUNT:
            return 0;
    }

    return 0;
}

static int inf_healer_diagnostic_attack_timer_matches(
    const InfernoState* s,
    int max_player_attack_timer
) {
    return max_player_attack_timer < 0 ||
        s->player.attack_timer <= max_player_attack_timer;
}

static void inf_reset_transition_diagnostics_for_restored_start(InfernoState* s) {
    int zuk_idx = inf_find_live_zuk_idx(s);
    int has_live_zuk = zuk_idx >= 0;
    float zuk_hp = has_live_zuk ? (float)s->npcs[zuk_idx].hp : 1200.0f;

    s->tick_at_le_300 = has_live_zuk && zuk_hp <= 300.0f ? s->tick : -1;
    s->tick_at_le_240 = has_live_zuk && zuk_hp <= 240.0f ? s->tick : -1;
    s->tick_at_le_150 = has_live_zuk && zuk_hp <= 150.0f ? s->tick : -1;

    s->damage_after_300 = 0.0f;
    s->damage_after_240 = 0.0f;
    s->damage_after_150 = 0.0f;
    s->damage_after_all_zuk_healers_dead = 0.0f;
    s->zuk_hp_at_all_zuk_healers_dead = 0.0f;
    s->hp_restored_after_240 = 0.0f;
    s->spark_damage_after_240 = 0.0f;
    s->zuk_hp_max_after_healer_spawn =
        s->zuk.healer_spawned && has_live_zuk ? zuk_hp : 0.0f;
    s->redemption_proc_opportunities = 0;
    s->redemption_zero_hit_proc_opportunities = 0;
    s->redemption_proc_opportunities_after_240 = 0;
    s->redemption_deaths_from_band = 0;
    s->redemption_deaths_from_band_after_240 = 0;
    s->redemption_deaths_from_above_band = 0;
    s->redemption_heal_potential = 0.0f;
    s->redemption_heal_potential_after_240 = 0.0f;
    memset(s->redemption_proc_opportunities_by_type, 0,
        sizeof(s->redemption_proc_opportunities_by_type));
    memset(s->redemption_zero_hit_proc_opportunities_by_type, 0,
        sizeof(s->redemption_zero_hit_proc_opportunities_by_type));
    memset(s->redemption_deaths_from_band_by_type, 0,
        sizeof(s->redemption_deaths_from_band_by_type));
    memset(s->redemption_heal_potential_by_type, 0,
        sizeof(s->redemption_heal_potential_by_type));
    s->redemption_action_count = 0;
    s->redemption_active_ticks = 0;
    s->redemption_proc_count = 0;
    s->redemption_zero_hit_proc_count = 0;
    s->redemption_heal_done = 0.0f;

    InfZukHealerCounts healers = inf_zuk_healer_counts(s);
    s->total_zuk_healer_tags = healers.tagged_count;
    if (s->total_zuk_healer_tags > 4) s->total_zuk_healer_tags = 4;
    s->total_shield_tags = 0;
    s->total_zuk_healer_kills = 0;
    s->total_zuk_healer_target_ticks = 0;
    s->total_zuk_healer_attack_fires = 0;
    s->total_zuk_healer_cannot_attack_ticks = 0;
    s->total_zuk_healer_cooldown_ticks = 0;
    s->total_zuk_healer_out_of_range_ticks = 0;
    s->total_zuk_healer_attackable_ticks = 0;

    s->tick_at_zuk_healer_spawn = s->zuk.healer_spawned ? s->tick : -1;
    s->tick_at_first_zuk_healer_tag =
        s->zuk.healer_spawned && healers.tagged_count > 0 ? s->tick : -1;
    s->tick_at_all_zuk_healers_tagged =
        s->zuk.healer_spawned &&
        healers.live_count > 0 &&
        healers.healing_count == 0 ? s->tick : -1;
    s->tick_at_all_zuk_healers_dead =
        s->zuk.healer_spawned && healers.live_count == 0 ? s->tick : -1;
    if (s->tick_at_all_zuk_healers_dead >= 0)
        s->zuk_hp_at_all_zuk_healers_dead = has_live_zuk ? zuk_hp : 0.0f;
    s->tick_at_first_zuk_hit_after_all_healers_dead = -1;
    s->tick_at_first_zuk_healer_target = -1;
    s->tick_at_first_zuk_healer_attack = -1;
    s->offshield_ticks_after_240 = 0;
    s->offshield_ticks_after_all_zuk_healers_dead = 0;
}

enum {
    INF_STYLE_MASK_MELEE = 1 << 0,
    INF_STYLE_MASK_RANGED = 1 << 1,
    INF_STYLE_MASK_MAGIC = 1 << 2,
};

static inline int inf_attack_style_mask_bit(int style) {
    if (style == ATTACK_STYLE_MELEE) return INF_STYLE_MASK_MELEE;
    if (style == ATTACK_STYLE_RANGED) return INF_STYLE_MASK_RANGED;
    if (style == ATTACK_STYLE_MAGIC) return INF_STYLE_MASK_MAGIC;
    return 0;
}

static inline int inf_cardinal_contact_with_npc(int px, int py, int nx, int ny, int npc_size) {
    int cx = px < nx ? nx : (px > nx + npc_size - 1 ? nx + npc_size - 1 : px);
    int cy = py < ny ? ny : (py > ny + npc_size - 1 ? ny + npc_size - 1 : py);
    int dx = px - cx; if (dx < 0) dx = -dx;
    int dy = py - cy; if (dy < 0) dy = -dy;
    return dx + dy == 1;
}

static inline int inf_melee_fallback_possible(
    const InfernoState* s, const InfNPC* npc, const InfNPCStats* stats,
    int planned_style, int dist
) {
    if (!stats->can_melee || planned_style == ATTACK_STYLE_MELEE || dist != 1)
        return 0;

    switch (npc->type) {
        case INF_NPC_RANGER:
        case INF_NPC_MAGER:
            return 1;
        case INF_NPC_BLOB:
        case INF_NPC_JAD:
            return inf_cardinal_contact_with_npc(
                s->player.x, s->player.y, npc->x, npc->y, npc->size);
        default:
            return 0;
    }
}

static inline int inf_attack_style_options_mask(
    const InfernoState* s, const InfNPC* npc, const InfNPCStats* stats,
    int planned_style, int dist
) {
    int mask = inf_attack_style_mask_bit(planned_style);
    if (inf_melee_fallback_possible(s, npc, stats, planned_style, dist))
        mask |= INF_STYLE_MASK_MELEE;
    return mask;
}

static inline int inf_attack_style_from_mask(int style_mask) {
    if (style_mask == INF_STYLE_MASK_MELEE) return ATTACK_STYLE_MELEE;
    if (style_mask == INF_STYLE_MASK_RANGED) return ATTACK_STYLE_RANGED;
    if (style_mask == INF_STYLE_MASK_MAGIC) return ATTACK_STYLE_MAGIC;
    return ATTACK_STYLE_NONE;
}

static inline int inf_attack_style_obs_preview(int style_mask) {
    int primary_mask = style_mask & (INF_STYLE_MASK_RANGED | INF_STYLE_MASK_MAGIC);
    int primary_style = inf_attack_style_from_mask(primary_mask);
    if (primary_style != ATTACK_STYLE_NONE)
        return primary_style;
    return inf_attack_style_from_mask(style_mask);
}

static inline int inf_attack_style_telegraph_mask(
    const InfernoState* s, const InfNPC* npc, const InfNPCStats* stats,
    int planned_style, int dist
) {
    /* Jad only telegraphs its ranged/magic branch. Melee is an immediate
       fallback choice at fire time, not a prayer-switch cue. */
    if (npc->type == INF_NPC_JAD)
        return inf_attack_style_mask_bit(planned_style);
    return inf_attack_style_options_mask(s, npc, stats, planned_style, dist);
}

static inline int inf_pending_hit_obs_timer(const EncounterPendingHit* ph) {
    if (ph->check_prayer && ph->prayer_check_delay > 0)
        return ph->prayer_check_delay;
    return ph->ticks_remaining;
}

#define INF_JAD_PROJECTILE_DELAY 3
#define INF_ANIM_JALTOK_JAD_MAGIC_ATTACK INF_GEN_ANIM_JALTOK_JAD_ATTACK_MAGIC
#define INF_ANIM_JALTOK_JAD_RANGED_ATTACK INF_GEN_ANIM_JALTOK_JAD_ATTACK_RANGED
#define INF_ANIM_JALTOK_JAD_MELEE_ATTACK INF_GEN_ANIM_JALTOK_JAD_ATTACK_MELEE

static inline EncounterProjectileDelayKind inf_projectile_delay_kind_for_style(int style) {
    if (style == ATTACK_STYLE_MAGIC) return ENCOUNTER_PROJECTILE_DELAY_MAGIC;
    if (style == ATTACK_STYLE_RANGED) return ENCOUNTER_PROJECTILE_DELAY_RANGED;
    return ENCOUNTER_PROJECTILE_DELAY_MELEE;
}

static inline EncounterProjectileDelayOptions inf_npc_projectile_options(
    InfNPCType type, int style
) {
    EncounterProjectileDelayOptions options = {0};
    if (type == INF_NPC_MAGER && style == ATTACK_STYLE_MAGIC) {
        options.visual_delay_ticks = 2;
        options.visual_hit_early_ticks = -1;
    } else if (type == INF_NPC_RANGER && style == ATTACK_STYLE_RANGED) {
        options.reduce_delay = -2;
        options.visual_delay_ticks = 3;
    } else if (type == INF_NPC_JAD && style != ATTACK_STYLE_MELEE) {
        options.reduce_delay = INF_JAD_PROJECTILE_DELAY;
        options.visual_hit_early_ticks = -1;
    } else if (type == INF_NPC_ZUK) {
        options.set_delay = 4;
        options.visual_delay_ticks = 2;
    }
    return options;
}

static inline EncounterProjectileTiming inf_npc_projectile_timing(
    InfNPCType type, int style, int distance
) {
    return encounter_projectile_timing(
        distance, 0, inf_projectile_delay_kind_for_style(style),
        inf_npc_projectile_options(type, style));
}

static inline EncounterProjectileTiming inf_player_projectile_timing(
    int style, uint8_t weapon, int is_special, int distance
) {
    EncounterProjectileDelayKind kind = inf_projectile_delay_kind_for_style(style);
    EncounterProjectileDelayOptions options = {0};
    if (style == ATTACK_STYLE_RANGED) {
        if (weapon == ITEM_TOXIC_BLOWPIPE) {
            kind = ENCOUNTER_PROJECTILE_DELAY_THROWN;
            options.visual_delay_ticks = 1;
            if (is_special) {
                options.reduce_delay = -1;
                options.visual_hit_early_ticks = 1;
            }
        } else if (weapon == ITEM_TWISTED_BOW) {
            options.visual_delay_ticks = 1;
        }
    }
    return encounter_projectile_timing(distance, 1, kind, options);
}

static inline int inf_jad_roll_primary_style(uint32_t* rng_state) {
    return (encounter_rand_int(rng_state, 2) == 0)
        ? ATTACK_STYLE_RANGED
        : ATTACK_STYLE_MAGIC;
}

static inline int inf_npc_attack_anim_id(const InfNPC* npc, const NpcModelMapping* nm) {
    if (npc->resurrecting_this_tick)
        return INF_GEN_ANIM_MAGER_RESURRECT;
    if (npc->type == INF_NPC_RANGER &&
        npc->attack_style_this_tick == ATTACK_STYLE_MELEE)
        return INF_GEN_ANIM_RANGER_ATTACK_MELEE;
    if (npc->type == INF_NPC_MAGER &&
        npc->attack_style_this_tick == ATTACK_STYLE_MELEE)
        return INF_GEN_ANIM_MAGER_ATTACK_MELEE;

    if (npc->type == INF_NPC_JAD) {
        switch (npc->attack_style_this_tick) {
            case ATTACK_STYLE_MAGIC:
                return INF_ANIM_JALTOK_JAD_MAGIC_ATTACK;
            case ATTACK_STYLE_RANGED:
                return INF_ANIM_JALTOK_JAD_RANGED_ATTACK;
            case ATTACK_STYLE_MELEE:
                return INF_ANIM_JALTOK_JAD_MELEE_ATTACK;
            default:
                return -1;
        }
    }

    if (!nm || nm->attack_anim == 65535)
        return -1;
    return (int)nm->attack_anim;
}

static inline int inf_npc_death_anim_id(const InfNPC* npc, const NpcModelMapping* nm) {
    switch (npc->type) {
        case INF_NPC_NIBBLER:
            return INF_GEN_ANIM_NIBBLER_DEATH;
        case INF_NPC_BAT:
            return INF_GEN_ANIM_BAT_DEATH;
        case INF_NPC_BLOB:
        case INF_NPC_BLOB_MELEE:
        case INF_NPC_BLOB_RANGE:
        case INF_NPC_BLOB_MAGE:
            return INF_GEN_ANIM_BLOB_DEATH;
        case INF_NPC_MELEER:
            return INF_GEN_ANIM_MELEER_DEATH;
        case INF_NPC_RANGER:
            return INF_GEN_ANIM_RANGER_DEATH;
        case INF_NPC_MAGER:
            return INF_GEN_ANIM_MAGER_DEATH;
        case INF_NPC_JAD:
            return INF_GEN_ANIM_JALTOK_JAD_DEATH;
        case INF_NPC_ZUK:
            return INF_GEN_ANIM_TZKAL_ZUK_DEATH;
        case INF_NPC_ZUK_SHIELD:
            return INF_GEN_ANIM_ZUK_SHIELD_DEATH;
        default:
            return nm ? (int)nm->idle_anim : -1;
    }
}

static inline int inf_choose_attack_style_for_tick(
    uint32_t* rng_state, int style_mask
) {
    int primary_style = inf_attack_style_from_mask(style_mask & ~INF_STYLE_MASK_MELEE);
    if ((style_mask & INF_STYLE_MASK_MELEE) && primary_style != ATTACK_STYLE_NONE)
        return (encounter_rand_int(rng_state, 2) == 0) ? ATTACK_STYLE_MELEE : primary_style;
    return inf_attack_style_from_mask(style_mask);
}


static inline int inf_dead_mob_is_resurrectable(InfNPCType type) {
    switch (type) {
        case INF_NPC_BAT:
        case INF_NPC_BLOB:
        case INF_NPC_MELEER:
        case INF_NPC_RANGER:
        case INF_NPC_MAGER:
            return 1;
        default:
            return 0;
    }
}

static void inf_store_dead_mob(InfernoState* s, InfNPC* npc) {
    if (s->dead_mob_count >= INF_MAX_DEAD_MOBS) return;
    /* only store the exact types that register with InfernoMobDeathStore in
       the reference: bat, blob parent, meleer, ranger, and mager. */
    if (!inf_dead_mob_is_resurrectable(npc->type)) return;
    if (npc->resurrection_count != 0) return;

    InfDeadMob* dm = &s->dead_mobs[s->dead_mob_count++];
    dm->type = npc->type;
    dm->x = npc->x;
    dm->y = npc->y;
    dm->hp = npc->max_hp / 2;  /* resurrect at 50% HP */
    dm->max_hp = npc->max_hp;
}


static float inf_compute_reward_ctx(InfernoState* s, const InfernoContext* ctx);
static float inf_compute_reward(InfernoState* s);
static void inf_spawn_wave(InfernoState* s);
static void inf_tick_npcs_ctx(InfernoState* s, InfernoContext* ctx);
static void inf_tick_npcs(InfernoState* s);
static void inf_tick_player_ctx(
    InfernoState* s,
    InfernoContext* ctx,
    const int* actions,
    int can_attack);
static void inf_tick_player(InfernoState* s, const int* actions, int can_attack);
static void inf_apply_npc_death(InfernoState* s, int npc_idx);
static int inf_mager_resurrect_ctx(InfernoState* s, InfernoContext* ctx, int idx);
static void inf_queue_zuk_healer_sparks(InfernoState* s, const InfNPC* npc);
static void inf_resolve_pending_sparks(InfernoState* s);
static void inf_rebuild_player_collision_flags(InfernoState* s);
static void inf_refresh_current_obs_slots_ctx(InfernoState* s, const InfernoContext* ctx);
static void inf_refresh_current_obs_slots(InfernoState* s);

static InfConfig inf_default_config(void) {
    return (InfConfig){
        .start_wave = 0,
        .loadout_profile_mode = INF_LOADOUT_PROFILE_MODE_MAX_ONLY,
        .budget_loadout_fraction = 0.0f,
        .damage_reward_coeff = 0.0f,
        .shield_penalty_coeff = 0.0f,
        .tag_reward_coeff = 0.0f,
        .late_start_supply_profile_scale = 1.0f,
        .supply_milestone_brew_reward_coeff = 0.0f,
        .supply_milestone_restore_reward_coeff = 0.0f,
        .death_penalty_coeff = 0.0f,
        .terminal_penalty_enabled = 0,
        .step_out_forecast_obs_enabled = 1,
        .phase_900_bonus = 0.0f,
        .phase_600_bonus = 0.0f,
        .phase_300_bonus = 0.0f,
        .shield_penalty_episode_cap = 0.0f,
        .jad_damage_reward_coeff = 0.0f,
        .zuk_healer_damage_reward_coeff = 0.0f,
        .set_damage_reward_coeff = 0.0f,
        .jad_kill_bonus = 0.0f,
        .zuk_healer_kill_bonus = 0.0f,
        .set_kill_bonus = 0.0f,
        .post_healer_zuk_damage_coeff = 0.0f,
        .post_healer_set_damage_reward_coeff = 0.0f,
        .post_healer_set_kill_bonus = 0.0f,
        .post_healer_set_alive_tick_penalty_coeff = 0.0f,
        .post_healer_set_alive_penalty_cap = 0.0f,
        .zuk_healer_phase_hp_delta_coeff = 0.0f,
        .zuk_untagged_healer_tick_penalty_coeff = 0.0f,
        .zuk_untagged_healer_target_bonus_coeff = 0.0f,
        .zuk_safe_untagged_healer_target_bonus_coeff = 0.0f,
        .zuk_untagged_healer_nonmagic_attack_bonus_coeff = 0.0f,
        .zuk_healer_mage_attack_penalty_coeff = 0.0f,
        .zuk_safe_untagged_healer_target_mask = 0,
        .zuk_force_safe_untagged_healer_target_mask = 0,
        .zuk_healer_reward_mode = INF_ZUK_HEALER_REWARD_MODE_BASELINE,
        .post_jad_zuk_multiplier = 1.0f,
        .jad_alive_zuk_multiplier = 1.0f,
        .oracle_mode = 0,
        .shield_tag_reward_coeff = 0.0f,
        .joseph_reward_mode = INF_JOSEPH_REWARD_MODE_OFF,
    };
}

static void inf_init_context_typed(InfernoContext* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->config = inf_default_config();
}

static void inf_init_context(EncounterContext* context) {
    inf_init_context_typed((InfernoContext*)context);
}

static void inf_destroy_context(EncounterContext* context) {
    (void)context;
}

static InfernoContext* inf_legacy_context(void) {
    static InfernoContext ctx;
    static Log log;
    static int initialized = 0;
    if (!initialized) {
        inf_init_context_typed(&ctx);
        ctx.log = &log;
        initialized = 1;
    }
    return &ctx;
}

static void inf_init_state_typed(InfernoState* s, InfernoContext* ctx) {
    inf_build_npc_stats();
    memset(s, 0, sizeof(*s));
    s->rng_state = 12345;
    s->start_wave = ctx ? ctx->config.start_wave : 0;
    s->active_loadout_profile = INF_LOADOUT_PROFILE_MAX;
    memset(s->npc_los_cache, -1, sizeof(s->npc_los_cache));
}

static void inf_init_state(EncounterState* state, EncounterContext* context) {
    inf_init_state_typed((InfernoState*)state, (InfernoContext*)context);
}

static EncounterState* inf_create(void) {
    InfernoState* s = (InfernoState*)calloc(1, sizeof(InfernoState));
    inf_init_state_typed(s, inf_legacy_context());
    return (EncounterState*)s;
}

static void inf_destroy(EncounterState* state) {
    free(state);
}
