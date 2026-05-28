#line 4619 "encounter_inferno.h"
static int inf_healer_is_actively_healing(const InfernoState* s, const InfNPC* npc) {
    if (!npc->active || npc->death_ticks > 0) return 0;
    if (npc->type != INF_NPC_HEALER_JAD && npc->type != INF_NPC_HEALER_ZUK) return 0;
    if (npc->aggro_target < 0 || npc->aggro_target >= INF_MAX_NPCS) return 0;
    return s->npcs[npc->aggro_target].active;
}

static int inf_npc_type_is_actively_healed(
    const InfernoState* s,
    InfNPCType target_type
) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* healer = &s->npcs[i];
        if (!inf_healer_is_actively_healing(s, healer)) continue;
        const InfNPC* target = &s->npcs[healer->aggro_target];
        if (target->type == target_type) return 1;
    }
    return 0;
}

static int inf_count_untagged_zuk_healers(const InfernoState* s) {
    int count = 0;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* healer = &s->npcs[i];
        if (!inf_healer_is_actively_healing(s, healer)) continue;
        if (healer->type != INF_NPC_HEALER_ZUK) continue;

        const InfNPC* target = &s->npcs[healer->aggro_target];
        if (target->type == INF_NPC_ZUK) count++;
    }
    return count;
}

static int inf_count_post_healer_set_pressure(const InfernoState* s) {
    int count = 0;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks != 0 || npc->hp <= 0)
            continue;
        if (inf_npc_type_is_set_pressure(npc->type))
            count++;
    }
    return count;
}

static inline float inf_dmg_below_threshold(float old_hp, float new_hp, float t) {
    if (new_hp >= old_hp) return 0.0f;
    float a = old_hp < t ? old_hp : t;
    float b = new_hp < t ? new_hp : t;
    float d = a - b;
    return d > 0.0f ? d : 0.0f;
}

typedef enum {
    INF_ZUK_LOWWATERMARK_TRACK_ONLY,
    INF_ZUK_LOWWATERMARK_REWARD_ALLOWED,
    INF_ZUK_LOWWATERMARK_REWARD_BLOCKED_EXCEPT_THRESHOLD,
} InfZukLowWatermarkMode;

static float inf_zuk_low_watermark_reward(
    InfernoState* s,
    const InfernoContext* ctx,
    InfZukLowWatermarkMode mode
) {
    int zuk_idx = inf_find_live_zuk_idx(s);
    if (zuk_idx < 0) return 0.0f;

    float zuk_hp = (float)s->npcs[zuk_idx].hp;
    if (zuk_hp >= s->min_zuk_hp_seen) return 0.0f;

    float old_min = s->min_zuk_hp_seen;
    float dmg = old_min - zuk_hp;
    int crosses_zuk_healer_threshold = old_min > 240.0f && zuk_hp <= 240.0f;
    int should_pay_zuk_damage =
        mode == INF_ZUK_LOWWATERMARK_REWARD_ALLOWED ||
        (mode == INF_ZUK_LOWWATERMARK_REWARD_BLOCKED_EXCEPT_THRESHOLD &&
            crosses_zuk_healer_threshold);
    float reward = should_pay_zuk_damage ? ctx->config.damage_reward_coeff * dmg : 0.0f;
    if (s->jad_killed_this_episode) {
        reward *= ctx->config.post_jad_zuk_multiplier;
    } else if (s->zuk.jad_spawned) {
        reward *= ctx->config.jad_alive_zuk_multiplier;
    }
    s->min_zuk_hp_seen = zuk_hp;

    if (s->tick_at_le_300 < 0 && zuk_hp <= 300.0f) s->tick_at_le_300 = s->tick;
    if (s->tick_at_le_240 < 0 && zuk_hp <= 240.0f) s->tick_at_le_240 = s->tick;
    if (s->tick_at_le_150 < 0 && zuk_hp <= 150.0f) s->tick_at_le_150 = s->tick;
    s->damage_after_300 += inf_dmg_below_threshold(old_min, zuk_hp, 300.0f);
    s->damage_after_240 += inf_dmg_below_threshold(old_min, zuk_hp, 240.0f);
    s->damage_after_150 += inf_dmg_below_threshold(old_min, zuk_hp, 150.0f);

    return reward;
}

static float inf_joseph_heal_damped_damage(float damage, float max_hp, float total_healed) {
    if (damage <= 0.0f) return 0.0f;
    float denom = max_hp + 4.0f * total_healed;
    if (denom <= 0.0f) return damage;
    return damage * max_hp / denom;
}

static float inf_zuk_healer_attack_shape_reward(
    const InfernoState* s,
    const InfernoContext* ctx
) {
    return
        ctx->config.zuk_untagged_healer_nonmagic_attack_bonus_coeff *
            (float)s->zuk_untagged_healer_nonmagic_attacks_this_tick -
        ctx->config.zuk_healer_mage_attack_penalty_coeff *
            (float)s->zuk_healer_mage_attack_fires_this_tick;
}

static float inf_offensive_prayer_damage_reward(
    const InfernoState* s,
    const InfernoContext* ctx
) {
    return ctx->config.offensive_prayer_reward_coeff *
        ctx->config.damage_reward_coeff *
        s->offensive_prayer_correct_damage_roll_this_tick;
}

static float inf_compute_joseph_reward(
    const InfernoState* s,
    const InfernoContext* ctx,
    int healer_is_actively_healing
) {
    float reward = 0.0f;
    if (healer_is_actively_healing) {
        reward = ctx->config.tag_reward_coeff * (float)s->healer_tags_this_tick;
    } else {
        float rewarded_other_damage = fmaxf(
            0.0f,
            s->damage_dealt_this_tick -
                s->damage_jad_this_tick -
                s->damage_zuk_this_tick -
                s->damage_jad_healers_this_tick);
        reward = ctx->config.damage_reward_coeff * (
            rewarded_other_damage +
            inf_joseph_heal_damped_damage(
                s->damage_jad_this_tick,
                (float)INF_NPC_STATS[INF_NPC_JAD].hp,
                s->total_hp_restored_jad) +
            inf_joseph_heal_damped_damage(
                s->damage_zuk_this_tick,
                (float)INF_NPC_STATS[INF_NPC_ZUK].hp,
                s->total_hp_restored_zuk));
        reward += inf_offensive_prayer_damage_reward(s, ctx);
    }
    reward -= ctx->config.shield_penalty_coeff * s->shield_damage_this_tick;
    reward += inf_zuk_healer_attack_shape_reward(s, ctx);
    return reward;
}

static float inf_compute_reward_ctx(InfernoState* s, const InfernoContext* ctx) {
    s->total_damage_dealt += s->damage_dealt_this_tick;
    s->total_zuk_healer_damage += s->damage_zuk_healers_this_tick;
    s->total_damage_received += s->damage_received_this_tick;
    s->total_hp_restored += s->hp_restored_this_tick;
    s->total_hp_restored_jad += s->hp_restored_jad_this_tick;
    s->total_hp_restored_zuk += s->hp_restored_zuk_this_tick;

    if (s->kill_jad_this_tick > 0) s->jad_killed_this_episode = 1;

    if (s->episode_over) {
        if (s->winner == 0) return 1.0f;
        if (ctx->config.terminal_penalty_enabled) return -1.0f;
        return -ctx->config.death_penalty_coeff;
    }

    int jad_is_actively_healed =
        inf_npc_type_is_actively_healed(s, INF_NPC_JAD);
    int zuk_is_actively_healed =
        inf_npc_type_is_actively_healed(s, INF_NPC_ZUK);
    int healer_is_actively_healing =
        jad_is_actively_healed || zuk_is_actively_healed;
    int tags_first_gate = inf_zuk_healer_tags_first_reward_gate_active(s, ctx);

    if (ctx->config.joseph_reward_mode == INF_JOSEPH_REWARD_MODE_ON) {
        if (inf_is_final_wave(s))
            inf_zuk_low_watermark_reward(s, ctx, INF_ZUK_LOWWATERMARK_TRACK_ONLY);
        return inf_compute_joseph_reward(s, ctx, healer_is_actively_healing);
    }

    float reward = 0.0f;
    if (inf_is_final_wave(s)) {
        int use_zuk_healer_phase_hp_delta =
            !tags_first_gate && s->zuk.healer_spawned &&
            ctx->config.zuk_healer_phase_hp_delta_coeff > 0.0f;
        InfZukLowWatermarkMode watermark_mode =
            INF_ZUK_LOWWATERMARK_REWARD_ALLOWED;
        if (tags_first_gate || use_zuk_healer_phase_hp_delta) {
            watermark_mode = INF_ZUK_LOWWATERMARK_TRACK_ONLY;
        } else if (zuk_is_actively_healed) {
            watermark_mode = INF_ZUK_LOWWATERMARK_REWARD_BLOCKED_EXCEPT_THRESHOLD;
        }
        reward = inf_zuk_low_watermark_reward(s, ctx, watermark_mode);
        if (use_zuk_healer_phase_hp_delta) {
            reward += ctx->config.zuk_healer_phase_hp_delta_coeff *
                (s->damage_zuk_this_tick - s->hp_restored_zuk_this_tick);
        }
        if (!tags_first_gate)
            reward += ctx->config.damage_reward_coeff * s->damage_zuk_healers_this_tick;
        if (!tags_first_gate && healer_is_actively_healing) {
            float generic_heal_cost = s->hp_restored_this_tick;
            if (use_zuk_healer_phase_hp_delta)
                generic_heal_cost -= s->hp_restored_zuk_this_tick;
            if (generic_heal_cost < 0.0f) generic_heal_cost = 0.0f;
            reward -= ctx->config.damage_reward_coeff * generic_heal_cost;
        }
        if (!tags_first_gate && s->zuk.healer_spawned &&
                ctx->config.zuk_untagged_healer_tick_penalty_coeff > 0.0f) {
            reward -= ctx->config.zuk_untagged_healer_tick_penalty_coeff *
                (float)inf_count_untagged_zuk_healers(s);
        }
    } else {
        float rewardable_damage = s->damage_dealt_this_tick -
            s->damage_jad_healers_this_tick -
            s->damage_resurrected_this_tick;
        if (jad_is_actively_healed)
            rewardable_damage -= s->damage_jad_this_tick;
        reward = ctx->config.damage_reward_coeff *
            fmaxf(0.0f, rewardable_damage - s->hp_restored_this_tick);
    }
    if (!tags_first_gate) reward += inf_offensive_prayer_damage_reward(s, ctx);
    reward += ctx->config.tag_reward_coeff * (float)s->healer_tags_this_tick;
    reward += inf_zuk_healer_attack_shape_reward(s, ctx);
    if (!tags_first_gate) {
        reward += ctx->config.shield_tag_reward_coeff * (float)s->shield_tags_this_tick;
        reward += ctx->config.zuk_untagged_healer_target_bonus_coeff *
            (float)s->zuk_untagged_healer_targets_this_tick;
        reward += ctx->config.zuk_safe_untagged_healer_target_bonus_coeff *
            (float)s->zuk_safe_untagged_healer_targets_this_tick;
    }

    /* Late-game reward shape. */
    int zuk_idx = inf_find_live_zuk_idx(s);
    int zuk_hp_now = (zuk_idx >= 0) ? s->npcs[zuk_idx].hp : 1200;
    int late_add_reward_active = s->zuk.jad_spawned || zuk_hp_now <= 600;
    if (late_add_reward_active && !tags_first_gate) {
        float rewardable_jad_damage =
            jad_is_actively_healed ? 0.0f : s->damage_jad_this_tick;
        reward += ctx->config.jad_damage_reward_coeff * rewardable_jad_damage;
        reward += ctx->config.zuk_healer_damage_reward_coeff * s->damage_zuk_healers_this_tick;
        reward += ctx->config.set_damage_reward_coeff * s->damage_set_this_tick;
        if (s->tick_at_all_zuk_healers_dead >= 0) {
            reward += ctx->config.post_healer_zuk_damage_coeff * s->damage_zuk_this_tick;
            float post_healer_set_damage_reward =
                ctx->config.post_healer_set_damage_reward_coeff * s->damage_set_this_tick;
            float post_healer_set_kill_bonus =
                (float)s->kill_set_this_tick * ctx->config.post_healer_set_kill_bonus;
            reward += post_healer_set_damage_reward;
            s->post_healer_set_damage_reward_total +=
                post_healer_set_damage_reward;
            s->post_healer_set_kill_bonus_total += post_healer_set_kill_bonus;
            s->pending_set_kill_bonus += post_healer_set_kill_bonus;
            if (ctx->config.post_healer_set_alive_tick_penalty_coeff > 0.0f) {
                int set_pressure = inf_count_post_healer_set_pressure(s);
                s->post_healer_set_pressure_total += (float)set_pressure;
                float penalty = ctx->config.post_healer_set_alive_tick_penalty_coeff *
                    (float)set_pressure;
                if (ctx->config.post_healer_set_alive_penalty_cap > 0.0f) {
                    float remaining = ctx->config.post_healer_set_alive_penalty_cap -
                        s->post_healer_set_alive_penalty_total;
                    if (remaining < 0.0f) remaining = 0.0f;
                    if (penalty > remaining) penalty = remaining;
                }
                s->post_healer_set_alive_penalty_total += penalty;
                reward -= penalty;
            }
        }
        s->pending_jad_kill_bonus +=
            (float)s->kill_jad_this_tick * ctx->config.jad_kill_bonus;
        s->pending_zuk_healer_kill_bonus +=
            (float)s->kill_zuk_healer_this_tick * ctx->config.zuk_healer_kill_bonus;
        s->pending_set_kill_bonus +=
            (float)s->kill_set_this_tick * ctx->config.set_kill_bonus;
    }
    /* emit pending kill bonuses gradually so a single-tick kill doesn't
       saturate the [-1, 1] PPO reward clamp. */
    if (!tags_first_gate && s->pending_jad_kill_bonus > 0.0f) {
        float emit = fminf(0.07f, s->pending_jad_kill_bonus);
        reward += emit;
        s->pending_jad_kill_bonus -= emit;
    }
    if (!tags_first_gate && s->pending_zuk_healer_kill_bonus > 0.0f) {
        float emit = fminf(0.05f, s->pending_zuk_healer_kill_bonus);
        reward += emit;
        s->pending_zuk_healer_kill_bonus -= emit;
    }
    if (!tags_first_gate && s->pending_set_kill_bonus > 0.0f) {
        float emit = fminf(0.03f, s->pending_set_kill_bonus);
        reward += emit;
        s->pending_set_kill_bonus -= emit;
    }

    if (!tags_first_gate) {
        float shield_penalty = ctx->config.shield_penalty_coeff * s->shield_damage_this_tick;
        if (ctx->config.shield_penalty_episode_cap > 0.0f) {
            float remaining = ctx->config.shield_penalty_episode_cap -
                s->shield_penalty_episode_total;
            if (remaining < 0.0f) remaining = 0.0f;
            if (shield_penalty > remaining) shield_penalty = remaining;
        }
        s->shield_penalty_episode_total += shield_penalty;
        reward -= shield_penalty;
    }

    if (!tags_first_gate && ctx->config.phase_900_bonus > 0.0f && !s->phase_900_fired
            && s->min_zuk_hp_seen <= 900.0f && s->min_zuk_hp_seen > 0.0f) {
        reward += ctx->config.phase_900_bonus;
        s->phase_900_fired = 1;
    }
    if (!tags_first_gate && ctx->config.phase_600_bonus > 0.0f && !s->phase_600_fired
            && s->min_zuk_hp_seen <= 600.0f && s->min_zuk_hp_seen > 0.0f) {
        reward += ctx->config.phase_600_bonus;
        s->phase_600_fired = 1;
    }
    if (!tags_first_gate && ctx->config.phase_300_bonus > 0.0f && !s->phase_300_fired
            && s->min_zuk_hp_seen <= 300.0f && s->min_zuk_hp_seen > 0.0f) {
        reward += ctx->config.phase_300_bonus;
        s->phase_300_fired = 1;
    }
    return reward;
}

static float inf_compute_reward(InfernoState* s) {
    return inf_compute_reward_ctx(s, inf_legacy_context());
}

static float inf_terminal_loss_reward(
    const InfernoState* s,
    const InfernoContext* ctx
) {
    (void)s;
    return ctx->config.terminal_penalty_enabled ? -1.0f : 0.0f;
}

static void inf_update_healer_transition_stats(InfernoState* s) {
    s->total_shield_tags += s->shield_tags_this_tick;

    if (!s->zuk.healer_spawned) return;

    if (s->tick_at_zuk_healer_spawn < 0)
        s->tick_at_zuk_healer_spawn = s->tick;

    int zuk_idx = inf_find_live_zuk_idx(s);
    if (zuk_idx >= 0) {
        float hp = (float)s->npcs[zuk_idx].hp;
        if (s->tick_at_le_240 < 0 && hp <= 240.0f)
            s->tick_at_le_240 = s->tick;
        if (hp > s->zuk_hp_max_after_healer_spawn)
            s->zuk_hp_max_after_healer_spawn = hp;
    }

    s->total_zuk_healer_tags += s->zuk_healer_tags_this_tick;
    if (s->total_zuk_healer_tags > 4) s->total_zuk_healer_tags = 4;
    s->total_zuk_healer_kills += s->kill_zuk_healer_this_tick;
    if (s->total_zuk_healer_kills > 4) s->total_zuk_healer_kills = 4;

    if (s->total_zuk_healer_tags > 0 && s->tick_at_first_zuk_healer_tag < 0)
        s->tick_at_first_zuk_healer_tag = s->tick;
    if (s->total_zuk_healer_tags >= 4 && s->tick_at_all_zuk_healers_tagged < 0)
        s->tick_at_all_zuk_healers_tagged = s->tick;

    int live_zuk_healers = 0;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (!s->npcs[i].active || s->npcs[i].hp <= 0) continue;
        if (s->npcs[i].type == INF_NPC_HEALER_ZUK)
            live_zuk_healers++;
    }
    if (live_zuk_healers == 0 && s->tick_at_all_zuk_healers_dead < 0) {
        s->tick_at_all_zuk_healers_dead = s->tick;
        s->zuk_hp_at_all_zuk_healers_dead =
            zuk_idx >= 0 ? (float)s->npcs[zuk_idx].hp : 0.0f;
    }

    if (s->tick_at_all_zuk_healers_dead >= 0 && s->damage_zuk_this_tick > 0.0f) {
        s->damage_after_all_zuk_healers_dead += s->damage_zuk_this_tick;
        if (s->tick_at_first_zuk_hit_after_all_healers_dead < 0)
            s->tick_at_first_zuk_hit_after_all_healers_dead = s->tick;
    }

    if (s->tick_at_le_240 >= 0) {
        s->hp_restored_after_240 += s->hp_restored_this_tick;
        s->spark_damage_after_240 += s->spark_damage_this_tick;
    }
}

static int inf_npc_is_live_player_target(const InfernoState* s, int npc_idx) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;
    const InfNPC* npc = &s->npcs[npc_idx];
    return npc->active && npc->death_ticks == 0 && npc->hp > 0 &&
        npc->type != INF_NPC_ZUK_SHIELD;
}

static int inf_has_live_player_target(const InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (inf_npc_is_live_player_target(s, i)) return 1;
    }
    return 0;
}

static int inf_has_attackable_player_target(const InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (inf_npc_is_live_player_target(s, i) &&
                inf_player_can_attack_npc_from_current_tile(s, i))
            return 1;
    }
    return 0;
}

static int inf_has_live_npc_type(const InfernoState* s, InfNPCType type) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (npc->active && npc->death_ticks == 0 && npc->hp > 0 &&
                npc->type == type)
            return 1;
    }
    return 0;
}

static InfIdleDiagnosticPhase inf_idle_diagnostic_phase(const InfernoState* s) {
    if (!inf_is_final_wave(s))
        return inf_has_live_npc_type(s, INF_NPC_JAD)
            ? INF_IDLE_PHASE_JAD : INF_IDLE_PHASE_SET;
    if (inf_has_live_npc_type(s, INF_NPC_HEALER_ZUK))
        return INF_IDLE_PHASE_ZUK_HEALERS;
    if (inf_has_live_npc_type(s, INF_NPC_JAD))
        return INF_IDLE_PHASE_ZUK_JAD;
    if (s->tick_at_all_zuk_healers_dead >= 0)
        return INF_IDLE_PHASE_ZUK_POST_HEALERS;
    return INF_IDLE_PHASE_ZUK_PRE_JAD;
}

static int inf_player_has_immediate_threat(const InfernoState* s) {
    if (s->tick_attacks_fired > 0) return 1;
    for (int h = 0; h < s->player_pending_hits.count; h++) {
        const EncounterPendingHit* ph = &s->player_pending_hits.hits[h];
        if (ph->check_prayer && inf_pending_hit_obs_timer(ph) <= 1)
            return 1;
    }
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks > 0 || npc->hp <= 0)
            continue;
        if (npc->type == INF_NPC_ZUK ||
                npc->type == INF_NPC_ZUK_SHIELD ||
                npc->type == INF_NPC_NIBBLER ||
                npc->type == INF_NPC_HEALER_ZUK)
            continue;
        if (npc->frozen_ticks > 0 || npc->stun_timer > 0)
            continue;

        InfTargetArea target = inf_npc_current_target_area(s, npc);
        if (!target.is_player) continue;

        const InfNPCStats* st = &INF_NPC_STATS[npc->type];
        int dist = encounter_dist_to_npc(
            s->player.x, s->player.y, npc->x, npc->y, npc->size);
        if (dist == 0 || dist > st->attack_range)
            continue;

        int planned_style = npc->attack_style;
        if (npc->type == INF_NPC_BLOB && npc->blob_scanned_prayer >= 0) {
            OverheadPrayer scanned = (OverheadPrayer)npc->blob_scanned_prayer;
            planned_style = scanned == PRAYER_PROTECT_MAGIC
                ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MAGIC;
        } else if (npc->type == INF_NPC_JAD) {
            planned_style = npc->jad_attack_style;
            if (planned_style == ATTACK_STYLE_NONE) continue;
        }

        if (inf_attack_style_telegraph_mask(s, npc, st, planned_style, dist) &&
                (npc->attack_timer <= 1))
            return 1;
    }
    return 0;
}

static int inf_progress_happened_this_tick(const InfernoState* s) {
    return s->damage_dealt_this_tick > 0.0f ||
        s->healer_tags_this_tick > 0 ||
        s->shield_tags_this_tick > 0 ||
        s->kill_jad_this_tick > 0 ||
        s->kill_zuk_healer_this_tick > 0 ||
        s->kill_set_this_tick > 0 ||
        s->wave_completed_this_tick > 0;
}

static void inf_record_idle_diagnostics(
    InfernoState* s,
    int can_player_attack,
    int attack_ready_no_attack,
    int target_available_no_attack,
    int safe_attack_opportunity_missed
) {
    if (!can_player_attack || !inf_has_live_player_target(s))
        return;

    InfIdleDiagnosticPhase phase = inf_idle_diagnostic_phase(s);
    assert((int)phase >= 0 && (int)phase < OSRS_INFERNO_IDLE_PHASE_COUNT);

    if (attack_ready_no_attack) {
        s->total_attack_ready_no_attack_ticks++;
        s->attack_ready_no_attack_ticks_by_phase[phase]++;
    }
    if (target_available_no_attack) {
        s->total_target_available_no_attack_ticks++;
        s->target_available_no_attack_ticks_by_phase[phase]++;
    }
    if (safe_attack_opportunity_missed) {
        s->total_safe_attack_opportunity_missed_ticks++;
        s->safe_attack_opportunity_missed_ticks_by_phase[phase]++;
    }
    if (!inf_progress_happened_this_tick(s)) {
        s->total_progressless_ticks++;
        s->progressless_ticks_by_phase[phase]++;
    }
}

static void inf_step_ctx(EncounterState* state, EncounterContext* context, const int* actions) {
    InfernoState* s = (InfernoState*)state;
    InfernoContext* ctx = context ? (InfernoContext*)context : inf_legacy_context();
    if (s->episode_over) return;

    /* clear per-tick state */
    s->reward = 0.0f;
    s->damage_dealt_this_tick = 0.0f;
    s->damage_resurrected_this_tick = 0.0f;
    s->damage_zuk_this_tick = 0.0f;
    s->damage_zuk_healers_this_tick = 0.0f;
    s->damage_jad_healers_this_tick = 0.0f;
    s->damage_jad_this_tick = 0.0f;
    s->damage_set_this_tick = 0.0f;
    s->kill_jad_this_tick = 0;
    s->kill_zuk_healer_this_tick = 0;
    s->kill_set_this_tick = 0;
    s->shield_damage_this_tick = 0.0f;
    s->healer_tags_this_tick = 0;
    s->zuk_healer_tags_this_tick = 0;
    s->zuk_untagged_healer_targets_this_tick = 0;
    s->zuk_safe_untagged_healer_targets_this_tick = 0;
    s->zuk_untagged_healer_nonmagic_attacks_this_tick = 0;
    s->zuk_healer_mage_attack_fires_this_tick = 0;
    s->shield_tags_this_tick = 0;
    s->spark_damage_this_tick = 0.0f;
    s->damage_received_this_tick = 0.0f;
    s->hp_restored_this_tick = 0.0f;
    s->hp_restored_jad_this_tick = 0.0f;
    s->hp_restored_zuk_this_tick = 0.0f;
    s->offensive_prayer_correct_damage_roll_this_tick = 0.0f;
    s->offensive_prayer_correct_this_tick = 0;
    s->prayer_correct_this_tick = 0;
    s->off_prayer_hits_this_tick = 0;
    s->tick_styles_fired = 0;
    s->tick_attacks_fired = 0;
    s->wave_completed_this_tick = 0;
    s->pillar_lost_this_tick = -1;
    s->player_moved_last_tick = s->player_moved_this_tick;
    s->player_moved_this_tick = 0;
    s->player_attacked_this_tick = 0;
    s->player_attack_timing = (EncounterProjectileTiming){0};
    s->brewed_this_tick = 0;
    s->blood_heal_this_tick = 0;
    s->behind_shield_this_tick = 0;
    encounter_clear_tick_flags(&s->player);
    /* clear NPC per-tick flags BEFORE player actions, so hit flags set by
       inf_tick_player survive through inf_tick_npcs into render_post_tick */
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        s->npcs[i].attacked_this_tick = 0;
        s->npcs[i].attack_style_this_tick = ATTACK_STYLE_NONE;
        s->npcs[i].attack_visual_target = -1;
        s->npcs[i].resurrecting_this_tick = 0;
        s->npcs[i].resurrection_visual_target = -1;
        s->npcs[i].moved_this_tick = 0;
        s->npcs[i].hit_landed_this_tick = 0;
        s->npcs[i].hit_damage = 0;
        s->npcs[i].hit_was_successful_this_tick = 0;
        s->npcs[i].hit_spell_type = 0;
    }
    s->tick++;
    inf_player_pretick(s, ctx, actions);

    int spawn_wave_now = 0;
    if (s->wave_spawn_delay > 0) {
        s->wave_spawn_delay--;
        if (s->wave_spawn_delay == 0)
            spawn_wave_now = 1;
    }
    int in_wave_gap = (s->wave_spawn_delay > 0 || spawn_wave_now);
    if (s->wave_ready_delay > 0)
        s->wave_ready_delay--;
    int in_ready_gap = s->wave_ready_delay > 0;

    inf_resolve_player_projectiles_on_npcs(s);
    inf_resolve_player_pending_hits_ctx(s, ctx);
    inf_resolve_pending_sparks(s);

    if (!in_wave_gap && !in_ready_gap) {
        inf_rebuild_player_collision_flags(s);
        inf_invalidate_los_cache(s);
        inf_tick_npcs_ctx(s, ctx);
    }
    inf_update_healer_transition_stats(s);

    /* Stop before player actions so lethal hits cannot be brewed back from 0 HP. */
    if (s->player.current_hitpoints <= 0) {
        (void)inf_compute_reward_ctx(s, ctx);
        if (s->last_hit_by_type >= 0 && s->last_hit_by_type < INF_NUM_NPC_TYPES)
            s->killed_by_type[s->last_hit_by_type]++;
        s->episode_over = 1;
        s->winner = 1;
        s->reward = inf_terminal_loss_reward(s, ctx);
        s->episode_return += s->reward;
        return;
    }

    /* player actions */
    int can_player_attack = !in_wave_gap && !in_ready_gap;
    int attack_ready_no_attack = 0;
    int target_available_no_attack = 0;
    int safe_attack_opportunity_missed = 0;
    int player_x_before_tick_player = s->player.x;
    int player_y_before_tick_player = s->player.y;
    inf_tick_player_ctx(s, ctx, actions, can_player_attack);
    s->player_moved_this_tick =
        (s->player.x != player_x_before_tick_player ||
         s->player.y != player_y_before_tick_player) ? 1 : 0;
    if (s->player_moved_this_tick)
        inf_invalidate_los_cache(s);
    inf_resolve_jad_prayer_checks_after_player(s, ctx);

    /* idle penalty counter: consecutive ticks where player could attack but didn't */
    {
        int has_alive_npc = can_player_attack && inf_has_live_player_target(s);
        if (has_alive_npc && s->player.attack_timer == 0 && !s->player_attacked_this_tick)
            s->ticks_without_action++;
        else
            s->ticks_without_action = 0;
        attack_ready_no_attack = s->ticks_without_action > 0;
        if (attack_ready_no_attack) {
            int target_available = inf_has_live_player_target(s);
            int attackable_target = inf_has_attackable_player_target(s);
            target_available_no_attack = target_available;
            safe_attack_opportunity_missed =
                attackable_target && !inf_player_has_immediate_threat(s);
        }
    }

    /* accumulate diagnostic counters.
       prayer_correct_this_tick is a count (multiple NPCs can attack same tick).
       total_npc_attacks counts attacks directed at the player (not nibbler→pillar). */
    s->total_prayer_correct += s->prayer_correct_this_tick;
    int ranger_attacked_this_tick = 0;
    int mager_attacked_this_tick = 0;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (s->npcs[i].attacked_this_tick &&
                s->npcs[i].aggro_target < 0 &&
                s->npcs[i].type != INF_NPC_NIBBLER &&
                !(s->npcs[i].type == INF_NPC_BLOB &&
                    s->npcs[i].blob_scanned_prayer >= 0)) {
            s->total_npc_attacks++;
            if (s->npcs[i].type == INF_NPC_RANGER)
                ranger_attacked_this_tick = 1;
            if (s->npcs[i].type == INF_NPC_MAGER)
                mager_attacked_this_tick = 1;
        }
    }
    if (ranger_attacked_this_tick && mager_attacked_this_tick) {
        s->total_ranger_mager_same_tick_attacks++;
        if (s->player_moved_last_tick)
            s->total_step_out_ranger_mager_same_tick_attacks++;
    }
    /* multi-style analysis: count off-prayer hits that were unavoidable because
       a different style was correctly prayed on the same tick. popcount of
       tick_styles_fired tells us how many distinct styles fired. if 2+, the
       off-prayer hits from non-prayed styles are "unavoidable" (can only pray one). */
    if (s->tick_attacks_fired > 0) {
        int styles = s->tick_styles_fired;
        int n_styles = ((styles >> 0) & 1) + ((styles >> 1) & 1) + ((styles >> 2) & 1);
        if (n_styles >= 2 && s->prayer_correct_this_tick > 0) {
            /* we prayed correctly against at least one style, but other styles
               also fired — those off-prayer hits were unavoidable */
            int off_prayer = s->tick_attacks_fired - s->prayer_correct_this_tick;
            s->total_unavoidable_off += off_prayer;
        }
    }
    if (s->ticks_without_action > 0) s->total_idle_ticks++;
    s->total_brews_used += s->brewed_this_tick;
    s->total_blood_healed += s->blood_heal_this_tick;

    if (can_player_attack && s->wave == 68) {
        s->total_zuk_ticks++;
        int is_behind_shield = inf_player_behind_zuk_shield_now(s);
        if (is_behind_shield) {
            s->behind_shield_ticks++;
            s->behind_shield_this_tick = 1;
        }
        if (!is_behind_shield && s->tick_at_le_240 >= 0)
            s->offshield_ticks_after_240++;
        if (!is_behind_shield && s->tick_at_all_zuk_healers_dead >= 0)
            s->offshield_ticks_after_all_zuk_healers_dead++;
    }

    /* bank the tick's irreversible HP progress before computing reward. all
       damage landings and healer applies have resolved by this point. */
    s->reward = inf_compute_reward_ctx(s, ctx);
    int all_dead = 1;

    /* check player death */
    if (s->player.current_hitpoints <= 0) {
        if (s->last_hit_by_type >= 0 && s->last_hit_by_type < INF_NUM_NPC_TYPES)
            s->killed_by_type[s->last_hit_by_type]++;
        s->episode_over = 1;
        s->winner = 1;
        s->reward = inf_terminal_loss_reward(s, ctx);
        goto finish_step;
    }

    if (spawn_wave_now) {
        s->wave = s->wave_spawn_target;
        inf_spawn_wave(s);
        inf_invalidate_los_cache(s);
        goto finish_step;
    }
    if (s->wave_spawn_delay > 0) goto finish_step;

    /* check wave completion */
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (s->npcs[i].active) { all_dead = 0; break; }
    }
    if (all_dead) {
        s->wave_completed_this_tick = 1;
        s->total_waves_cleared++;
        int next_public_wave = s->wave + 2;
        float wave_clear_reward = 1.0f +
            inf_supply_milestone_surplus_reward(s, ctx, next_public_wave);
        s->reward = wave_clear_reward;
        if (s->wave + 1 >= INF_NUM_WAVES) {
            s->episode_over = 1;
            s->winner = 0;
        } else {
            s->wave_spawn_target = s->wave + 1;
            s->wave_spawn_delay = 9;
        }
    }

    if (s->tick >= INF_MAX_TICKS) {
        s->episode_over = 1;
        s->winner = 1;
        s->reward = inf_terminal_loss_reward(s, ctx);
    }

finish_step:
    inf_record_idle_diagnostics(
        s,
        can_player_attack,
        attack_ready_no_attack,
        target_available_no_attack,
        safe_attack_opportunity_missed);
    s->episode_return += s->reward;
}

static void inf_step(EncounterState* state, const int* actions) {
    inf_step_ctx(state, (EncounterContext*)inf_legacy_context(), actions);
}
