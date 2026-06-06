#line 2424 "encounter_inferno.h"
static AttackStyle inf_player_equipped_attack_style(const InfernoState* s) {
    uint8_t weapon = s->player.equipped[GEAR_SLOT_WEAPON];
    AttackStyle style = (AttackStyle)get_item_attack_style(weapon);
    if (style == ATTACK_STYLE_MAGIC ||
        style == ATTACK_STYLE_RANGED ||
        style == ATTACK_STYLE_MELEE) {
        return style;
    }
    return ATTACK_STYLE_RANGED;
}

static void inf_refresh_human_loadout_stats(InfernoState* s) {
    AttackStyle style = inf_player_equipped_attack_style(s);
    int spell_base_damage = (style == ATTACK_STYLE_MAGIC) ? 30 : 0;
    encounter_compute_player_equipped_stats(
        &s->player, style, s->player.fight_style, spell_base_damage,
        &s->human_loadout_stats);
}

static const EncounterLoadoutStats* inf_current_loadout_stats(
    InfernoState* s,
    const InfernoContext* ctx
) {
    if (ctx->human_command_mode) {
        inf_refresh_human_loadout_stats(s);
        return &s->human_loadout_stats;
    }
    return &s->loadout_stats[s->weapon_set];
}

static int inf_player_weapon_is(const InfernoState* s, uint8_t item) {
    return s->player.equipped[GEAR_SLOT_WEAPON] == item;
}

static void inf_damage_player_from_type(
    InfernoState* s,
    int source_type,
    int damage
);

typedef struct {
    EncounterLoadoutStats stats;
    int spell;
    int is_barrage;
    uint8_t weapon;
} InfPlayerAttack;

static int inf_is_barrage_spell(int spell) {
    return spell == ENCOUNTER_SPELL_BLOOD || spell == ENCOUNTER_SPELL_ICE;
}

static int inf_spell_magic_level_requirement(int spell) {
    if (spell == ENCOUNTER_SPELL_ICE) return ICE_BARRAGE_LEVEL;
    if (spell == ENCOUNTER_SPELL_BLOOD) return BLOOD_BARRAGE_LEVEL;
    fprintf(stderr, "BUG: invalid Inferno spell %d\n", spell);
    abort();
}

static int inf_spell_action_to_spell(int action_spell) {
    if (action_spell == 1) return ENCOUNTER_SPELL_BLOOD;
    if (action_spell == 2) return ENCOUNTER_SPELL_ICE;
    return ENCOUNTER_SPELL_NONE;
}

static int inf_spell_base_damage(int spell) {
    if (!inf_is_barrage_spell(spell)) {
        fprintf(stderr, "BUG: invalid Inferno spell %d\n", spell);
        abort();
    }
    return 30;
}

static int inf_player_autocast_spell(const Player* p) {
    return p->autocast_spell == ENCOUNTER_SPELL_ICE
        ? ENCOUNTER_SPELL_ICE
        : ENCOUNTER_SPELL_BLOOD;
}

static FightStyle inf_player_spell_fight_style(const Player* p) {
    return (p->autocast_defensive || p->fight_style == FIGHT_STYLE_DEFENSIVE_AUTOCAST)
        ? FIGHT_STYLE_DEFENSIVE_AUTOCAST
        : FIGHT_STYLE_AUTOCAST;
}

static int inf_autocast_is_active(const InfernoState* s) {
    return s->player.autocast_enabled &&
        item_supports_ancient_autocast(s->player.equipped[GEAR_SLOT_WEAPON]);
}

static void inf_compute_manual_spell_stats(
    InfernoState* s,
    int spell,
    EncounterLoadoutStats* out
) {
    encounter_compute_player_equipped_stats(
        &s->player,
        ATTACK_STYLE_MAGIC,
        inf_player_spell_fight_style(&s->player),
        inf_spell_base_damage(spell),
        out);
}

static int inf_resolve_player_attack_ctx(
    InfernoState* s,
    const InfernoContext* ctx,
    int manual_spell,
    InfPlayerAttack* out
) {
    memset(out, 0, sizeof(*out));
    out->spell = ENCOUNTER_SPELL_NONE;
    out->weapon = s->player.equipped[GEAR_SLOT_WEAPON];

    if (inf_is_barrage_spell(manual_spell)) {
        if (s->player.current_magic < inf_spell_magic_level_requirement(manual_spell))
            return 0;
        inf_compute_manual_spell_stats(s, manual_spell, &out->stats);
        out->spell = manual_spell;
        out->is_barrage = 1;
        return 1;
    }

    const EncounterLoadoutStats* ls = inf_current_loadout_stats(s, ctx);
    out->stats = *ls;

    if (ls->style != ATTACK_STYLE_MAGIC)
        return 1;

    if (!inf_autocast_is_active(s))
        return 0;

    int autocast_spell = inf_player_autocast_spell(&s->player);
    if (s->player.current_magic < inf_spell_magic_level_requirement(autocast_spell))
        return 0;

    out->spell = autocast_spell;
    out->is_barrage = 1;
    return 1;
}

static int inf_resolve_player_attack(
    InfernoState* s,
    int manual_spell,
    InfPlayerAttack* out
) {
    return inf_resolve_player_attack_ctx(
        s, inf_legacy_context(), manual_spell, out);
}

static int inf_player_has_any_barrage_spell_available(const InfernoState* s) {
    return s->player.current_magic >= BLOOD_BARRAGE_LEVEL;
}

static int inf_npc_type_can_be_tagged_off_shield(int type) {
    return type == INF_NPC_JAD ||
        type == INF_NPC_RANGER ||
        type == INF_NPC_MAGER;
}

static int inf_is_live_shield_slot(const InfernoState* s, int npc_idx) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;
    const InfNPC* npc = &s->npcs[npc_idx];
    return npc->active &&
        npc->death_ticks == 0 &&
        npc->hp > 0 &&
        npc->type == INF_NPC_ZUK_SHIELD;
}

static int inf_is_shield_taggable_slot(const InfernoState* s, int npc_idx) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;
    const InfNPC* npc = &s->npcs[npc_idx];
    return npc->active &&
        npc->death_ticks == 0 &&
        npc->hp > 0 &&
        inf_npc_type_can_be_tagged_off_shield(npc->type) &&
        inf_is_live_shield_slot(s, npc->aggro_target);
}

static int inf_player_behind_zuk_shield_now(const InfernoState* s) {
    int shield_idx = s->zuk.shield_idx;
    if (shield_idx < 0 || shield_idx >= INF_MAX_NPCS) return 0;

    const InfNPC* shield = &s->npcs[shield_idx];
    if (!shield->active || shield->death_ticks != 0 || shield->hp <= 0)
        return 0;

    int shield_size = INF_NPC_STATS[INF_NPC_ZUK_SHIELD].size;
    return s->player.x >= shield->x &&
        s->player.x < shield->x + shield_size &&
        s->player.y >= 41;
}

static int inf_is_untagged_live_zuk_healer_slot(
    const InfernoState* s,
    int npc_idx
) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;

    const InfNPC* npc = &s->npcs[npc_idx];
    if (!npc->active || npc->death_ticks != 0 || npc->hp <= 0 ||
            npc->type != INF_NPC_HEALER_ZUK)
        return 0;

    if (npc->aggro_target < 0 || npc->aggro_target >= INF_MAX_NPCS)
        return 0;

    const InfNPC* aggro = &s->npcs[npc->aggro_target];
    return aggro->active && aggro->type == INF_NPC_ZUK;
}

static int inf_player_can_attack_npc_from_current_tile(
    const InfernoState* s,
    int npc_idx
) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;

    const InfNPC* npc = &s->npcs[npc_idx];
    if (!npc->active || npc->death_ticks != 0 || npc->hp <= 0) return 0;

    const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];
    OsrsAttackReachQuery reach = {
        .source = osrs_footprint(s->player.x, s->player.y, 1),
        .target = osrs_footprint(npc->x, npc->y, inf_npc_footprint_size(npc)),
        .delivery = encounter_attack_delivery_from_style(ls->style),
        .range = ls->attack_range,
        .occlusion = inf_projectile_occlusion(s),
    };
    return osrs_attack_can_reach(&reach);
}

static int inf_player_can_phantom_barrage_npc(
    InfernoState* s,
    const InfernoContext* ctx,
    int npc_idx
) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;

    const InfNPC* npc = &s->npcs[npc_idx];
    if (!npc->active || npc->death_ticks <= 0 || npc->hp > 0 ||
            npc->type == INF_NPC_ZUK_SHIELD)
        return 0;

    if (!inf_player_has_any_barrage_spell_available(s)) return 0;

    OsrsAttackReachQuery reach = {
        .source = osrs_footprint(s->player.x, s->player.y, 1),
        .target = osrs_footprint(npc->x, npc->y, inf_npc_footprint_size(npc)),
        .delivery = OSRS_ATTACK_DELIVERY_PROJECTILE,
        .range = 10,
        .occlusion = inf_projectile_occlusion(s),
    };
    return osrs_attack_can_reach(&reach);
}

static int inf_npc_is_phantom_barrage_obs_candidate(
    InfernoState* s,
    const InfernoContext* ctx,
    int npc_idx
) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;
    return s->npcs[npc_idx].death_ticks <= 2 &&
        inf_player_can_phantom_barrage_npc(s, ctx, npc_idx);
}

static int inf_npc_is_phantom_barrage_cast_window(
    InfernoState* s,
    const InfernoContext* ctx,
    int npc_idx
) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;
    return s->player.attack_timer <= 1 &&
        s->npcs[npc_idx].death_ticks == 1 &&
        inf_player_can_phantom_barrage_npc(s, ctx, npc_idx);
}

static int inf_untagged_zuk_healer_target_is_safe_now(
    const InfernoState* s,
    const InfernoContext* ctx,
    int npc_idx
) {
    (void)ctx;
    if (!inf_is_untagged_live_zuk_healer_slot(s, npc_idx)) return 1;
    return inf_player_behind_zuk_shield_now(s) &&
        s->player.attack_timer == 0 &&
        inf_player_can_attack_npc_from_current_tile(s, npc_idx);
}

static int inf_is_safe_untagged_zuk_healer_target_now(
    const InfernoState* s,
    const InfernoContext* ctx,
    int npc_idx
) {
    return inf_is_untagged_live_zuk_healer_slot(s, npc_idx) &&
        inf_untagged_zuk_healer_target_is_safe_now(s, ctx, npc_idx);
}

static int inf_has_safe_untagged_zuk_healer_target_now(
    const InfernoState* s,
    const InfernoContext* ctx
) {
    for (int obs_slot = 0; obs_slot < INF_OBS_NPCS; obs_slot++) {
        int npc_idx = s->current_obs_slots[obs_slot];
        if (npc_idx >= 0 && npc_idx < INF_MAX_NPCS &&
                s->npcs[npc_idx].active &&
                s->npcs[npc_idx].death_ticks == 0 &&
                s->npcs[npc_idx].type != INF_NPC_ZUK_SHIELD &&
                inf_is_safe_untagged_zuk_healer_target_now(s, ctx, npc_idx)) {
            return 1;
        }
    }
    return 0;
}

static int inf_npc_target_hit_delay(
    const InfNPC* npc, const InfNPC* target, int style
) {
    int dist = encounter_projectile_distance(
        npc->x, npc->y, npc->size, target->x, target->y, target->size,
        ENCOUNTER_PROJECTILE_DISTANCE_CLOSEST_TILE);
    EncounterProjectileTiming timing = inf_npc_projectile_timing(npc->type, style, dist);
    if (npc->type == INF_NPC_JAD && timing.damage_delay_ticks > 0)
        return INF_JAD_PROJECTILE_DELAY + timing.damage_delay_ticks;
    return timing.damage_delay_ticks;
}

static void inf_apply_npc_target_damage(InfernoState* s, int target_idx, int damage) {
    if (target_idx < 0 || target_idx >= INF_MAX_NPCS) return;
    InfNPC* target = &s->npcs[target_idx];
    if (!target->active) return;

    int target_hp_before = target->hp;
    encounter_damage_npc(&target->hp, &target->hit_landed_this_tick,
                         &target->hit_damage, damage);
    if (target->type == INF_NPC_ZUK_SHIELD)
        s->shield_damage_this_tick += (float)(target_hp_before - target->hp);

    if (target->hp <= 0 && target->type == INF_NPC_ZUK_SHIELD) {
        inf_deactivate_npc(s, target_idx);
        s->zuk.shield_idx = -1;
        for (int i = 0; i < INF_MAX_NPCS; i++) {
            if (s->npcs[i].aggro_target == target_idx)
                s->npcs[i].aggro_target = -1;
        }
    }
}

static void inf_queue_npc_target_hit(
    InfernoState* s, int target_idx, int damage, int ticks_remaining
) {
    if (ticks_remaining <= 0) {
        inf_apply_npc_target_damage(s, target_idx, damage);
        return;
    }
    for (int i = 0; i < INF_MAX_NPC_TARGET_HITS; i++) {
        InfNpcTargetHit* hit = &s->npc_target_hits[i];
        if (hit->active) continue;
        hit->active = 1;
        hit->target_idx = target_idx;
        hit->damage = damage;
        hit->ticks_remaining = ticks_remaining;
        return;
    }
    fprintf(stderr, "FATAL: Inferno NPC target hit queue overflow\n");
    abort();
}

static void inf_resolve_npc_target_hits(InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPC_TARGET_HITS; i++) {
        InfNpcTargetHit* hit = &s->npc_target_hits[i];
        if (!hit->active) continue;
        hit->ticks_remaining--;
        if (hit->ticks_remaining > 0) continue;
        inf_apply_npc_target_damage(s, hit->target_idx, hit->damage);
        hit->active = 0;
    }
}

static int inf_apply_elysian_to_player_hit(InfernoState* s, int damage, int* reduced) {
    *reduced = 0;
    if (damage <= 0) return damage;
    osrs_ensure_player_equipment(&s->player);
    if (!osrs_effect_profile_has(
            &s->player.equipment_effect_profile, OSRS_ITEM_EFFECT_ELYSIAN))
        return damage;
    if (encounter_rand_int(&s->rng_state, 10) >= 7)
        return damage;
    *reduced = 1;
    return damage * 75 / 100;
}

static int inf_npc_footprint_overlaps_player_3x3(const InfernoState* s, const InfNPC* npc) {
    int size = inf_npc_effective_size(npc);
    int min_x = s->player.x - 1;
    int max_x = s->player.x + 1;
    int min_y = s->player.y - 1;
    int max_y = s->player.y + 1;
    return npc->x <= max_x &&
        npc->x + size - 1 >= min_x &&
        npc->y <= max_y &&
        npc->y + size - 1 >= min_y;
}

static void inf_apply_echo_boots_recoil(InfernoState* s, int final_damage) {
    osrs_ensure_player_equipment(&s->player);
    int recoil_damage = osrs_echo_boots_recoil_damage(
        &s->player.equipment_effect_profile,
        &s->player.item_effect_state,
        final_damage);
    if (recoil_damage <= 0) return;

    osrs_consume_echo_boots_charge(&s->player);
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks > 0 || npc->hp <= 0)
            continue;
        if (npc->type == INF_NPC_ZUK || npc->type == INF_NPC_ZUK_SHIELD)
            continue;
        if (!inf_npc_footprint_overlaps_player_3x3(s, npc))
            continue;

        int applied = encounter_damage_npc(
            &npc->hp, &npc->hit_landed_this_tick, &npc->hit_damage,
            recoil_damage);
        if (applied <= 0) continue;

        s->damage_dealt_this_tick += (float)applied;
        if (npc->type == INF_NPC_HEALER_ZUK) {
            s->damage_zuk_healers_this_tick += (float)applied;
        } else if (npc->type == INF_NPC_HEALER_JAD) {
            s->damage_jad_healers_this_tick += (float)applied;
        } else if (npc->type == INF_NPC_JAD) {
            s->damage_jad_this_tick += (float)applied;
        } else {
            s->damage_set_this_tick += (float)applied;
        }

        if (npc->hp <= 0) {
            if (npc->type == INF_NPC_JAD) s->kill_jad_this_tick++;
            else if (npc->type == INF_NPC_HEALER_ZUK) s->kill_zuk_healer_this_tick++;
            else if (npc->type != INF_NPC_HEALER_JAD) s->kill_set_this_tick++;
            inf_apply_npc_death(s, i);
        }
    }
}


static void inf_npc_attack_ctx(InfernoState* s, InfernoContext* ctx, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (!npc->active) return;
    const InfNPCStats* stats = &INF_NPC_STATS[npc->type];
    if (npc->attack_timer > 0) npc->attack_timer--;
    if (npc->stun_timer > 0) { npc->stun_timer--; return; }
    if (npc->dig_freeze_timer > 0) return;
    if (npc->dig_attack_delay > 0) return;

    int has_los_now = 0;
    if (stats->attack_range > 1) {
        has_los_now = inf_npc_has_los(s, idx);
    }

    if (npc->type == INF_NPC_BLOB &&
        npc->blob_scanned_prayer < 0 &&
        has_los_now &&
        !npc->had_los_last_tick) {
        npc->blob_scanned_prayer = (int)s->player.prayer;
        /* commit to opposite of scanned prayer; random if scanned prayer is neither */
        if (s->player.prayer == PRAYER_PROTECT_MAGIC) npc->attack_style = ATTACK_STYLE_RANGED;
        else if (s->player.prayer == PRAYER_PROTECT_RANGED) npc->attack_style = ATTACK_STYLE_MAGIC;
        else npc->attack_style = (encounter_rand_int(&s->rng_state, 2) == 0) ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_RANGED;
        npc->had_los_last_tick = has_los_now;
        npc->attacked_this_tick = 1;
        npc->attack_timer = stats->attack_speed;
        return;
    }

    npc->had_los_last_tick = has_los_now;

    if (npc->attack_timer > 0) return;

    /* shield doesn't attack */
    if (npc->type == INF_NPC_ZUK_SHIELD) return;

    /* nibbler attacks pillars, not player */
    if (npc->type == INF_NPC_NIBBLER) {
        for (int p = 0; p < INF_NUM_PILLARS; p++) {
            if (!s->pillars[p].active) continue;
            int ddx = npc->x - s->pillars[p].x;
            int ddy = npc->y - s->pillars[p].y;
            if (ddx >= -1 && ddx <= INF_PILLAR_SIZE && ddy >= -1 && ddy <= INF_PILLAR_SIZE) {
                /* nibblers deal 0-4 damage per hit (ref: InfernoTrainer JalNib.ts).
                   bypasses combat formula — custom weapon directly rolls rand(0..4). */
                int dmg = encounter_rand_int(&s->rng_state, 5);
                s->pillars[p].hp -= dmg;
                if (s->pillars[p].hp <= 0) {
                    s->pillars[p].active = 0;
                    s->pillar_lost_this_tick = p;
                    inf_rebuild_los(s);
                    inf_invalidate_los_cache(s);
                    /* pillar death AOE: deals damage to all mobs + player within 1 tile */
                    for (int n = 0; n < INF_MAX_NPCS; n++) {
                        if (!s->npcs[n].active) continue;
                        int ndx = s->npcs[n].x - s->pillars[p].x;
                        int ndy = s->npcs[n].y - s->pillars[p].y;
                        if (ndx >= -1 && ndx <= INF_PILLAR_SIZE && ndy >= -1 && ndy <= INF_PILLAR_SIZE) {
                            encounter_damage_npc(&s->npcs[n].hp, &s->npcs[n].hit_landed_this_tick, &s->npcs[n].hit_damage, 12);
                            inf_apply_npc_death(s, n);
                        }
                    }
                    /* also damages the player if standing next to the pillar */
                    {
                        int pdx = s->player.x - s->pillars[p].x;
                        int pdy = s->player.y - s->pillars[p].y;
                        if (pdx >= -1 && pdx <= INF_PILLAR_SIZE && pdy >= -1 && pdy <= INF_PILLAR_SIZE) {
                            /* pillar collapse: 49 damage (observed in-game, server-side formula unknown) */
                            inf_damage_player_from_type(s, -1, 49);
                        }
                    }
                }
                npc->attacked_this_tick = 1;
                npc->attack_timer = stats->attack_speed;
                return;
            }
        }
        return;
    }

    /* zuk healer: heal Zuk while it is untapped, switch to player-facing
       sparks after tag. */
    if (npc->type == INF_NPC_HEALER_ZUK) {
        if (npc->aggro_target >= 0) {
            /* heal Zuk with a 3-tick projectile */
            int ti = npc->aggro_target;
            if (ti >= 0 && ti < INF_MAX_NPCS && s->npcs[ti].active && s->npcs[ti].type == INF_NPC_ZUK) {
                npc->heal_target = ti;
                npc->heal_timer = 3;
                npc->attack_visual_target = ti;
            }
        } else {
            /* tagged healer: stop healing and fire the 3-spark ground pattern. */
            npc->heal_target = -1;
            npc->heal_timer = 0;
            npc->attack_visual_target = -1;
            inf_queue_zuk_healer_sparks(s, npc);
        }
        npc->attacked_this_tick = 1;
        npc->attack_style_this_tick = ATTACK_STYLE_MAGIC;
        npc->attack_timer = stats->attack_speed;
        return;
    }

    /* jad healer: heals its Jad while aggro_target points at the boss, then
       switches to crush melee after a player tag. */
    if (npc->type == INF_NPC_HEALER_JAD) {
        int jad_idx = npc->jad_owner_idx;
        if (npc->aggro_target >= 0) {
            if (jad_idx >= 0 && s->npcs[jad_idx].active &&
                s->npcs[jad_idx].type == INF_NPC_JAD) {
                /* heal Jad with the reference 3-tick delay. */
                npc->heal_target = jad_idx;
                npc->heal_timer = 3;
                npc->attack_visual_target = jad_idx;
            }
            npc->attacked_this_tick = 1;
            npc->attack_timer = stats->attack_speed;
            return;
        }
        /* if player has tagged this healer, it only attacks on cardinal melee
           contact. diagonal corners do not count. */
        OsrsAttackReachQuery healer_reach = {
            .source = osrs_footprint(npc->x, npc->y, 1),
            .target = osrs_footprint(s->player.x, s->player.y, 1),
            .delivery = OSRS_ATTACK_DELIVERY_MELEE,
            .range = 1,
            .occlusion = inf_projectile_occlusion(s),
        };
        if (osrs_attack_can_reach(&healer_reach)) {
            int max_hit = osrs_npc_melee_max_hit(stats->str_level, stats->melee_str_bonus);
            int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
            /* accuracy roll */
            int att_roll = osrs_npc_attack_roll(stats->att_level, stats->melee_att_bonus);
            const EncounterLoadoutStats* ls = inf_current_loadout_stats(s, ctx);
            int def_bonus = ls->def_crush;
            int def_roll = osrs_player_def_roll_vs_npc(s->player.current_defence, s->player.current_magic, def_bonus, ATTACK_STYLE_MELEE);
            if (encounter_rand_float(&s->rng_state) >= osrs_hit_chance(att_roll, def_roll)) dmg = 0;
            int prayer_matches = (s->player.prayer == PRAYER_PROTECT_MELEE);
              if (prayer_matches) { dmg = 0; s->prayer_correct_this_tick = 1; }
              else if (dmg > 0) { s->off_prayer_hits_this_tick++; }
            inf_apply_echo_boots_recoil(s, dmg);
            inf_damage_player_from_type(s, npc->type, dmg);
            npc->attack_style_this_tick = ATTACK_STYLE_MELEE;
            npc->attacked_this_tick = 1;
            npc->attack_visual_target = -1;
            npc->attack_timer = stats->attack_speed;
        }
        return;
    }

    if (npc->aggro_target >= 0 && npc->aggro_target < INF_MAX_NPCS) {
        InfNPC* target = &s->npcs[npc->aggro_target];
        if (target->active) {
            int actual_style = npc->attack_style;
            if (npc->type == INF_NPC_JAD) {
                actual_style = npc->jad_attack_style;
                if (actual_style == ATTACK_STYLE_NONE)
                    actual_style = inf_jad_roll_primary_style(&s->rng_state);
            }
            int max_hit = osrs_npc_max_hit(actual_style,
                stats->str_level, stats->range_level,
                stats->melee_str_bonus, stats->ranged_str_bonus,
                stats->magic_base_dmg, stats->magic_dmg_pct);
            if (stats->max_hit_cap > 0 && max_hit > stats->max_hit_cap)
                max_hit = stats->max_hit_cap;
            int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
            int hit_delay = inf_npc_target_hit_delay(npc, target, actual_style);
            inf_queue_npc_target_hit(s, npc->aggro_target, dmg, hit_delay);
            npc->attacked_this_tick = 1;
            npc->attack_style_this_tick = actual_style;
            npc->attack_visual_target = npc->aggro_target;
            npc->attack_timer = stats->attack_speed;
            if (npc->type == INF_NPC_JAD) {
                npc->jad_attack_style = ATTACK_STYLE_NONE;
                if (s->wave == 66)      npc->attack_timer = 8;
                else if (s->wave == 67) npc->attack_timer = 9;
                else                    npc->attack_timer = 8;
            }
            return;
        }
        npc->aggro_target = -1;
    }

    /* ranged/magic NPCs need LOS, except blobs once they have already stored
       a prayer scan. ref: InfernoTrainer JalAk.ts attackIfPossible(). */
    if (stats->attack_range > 1 &&
        !(npc->type == INF_NPC_BLOB && npc->blob_scanned_prayer >= 0) &&
        !has_los_now) return;

    /* compute distance to player */
    int dist = encounter_dist_to_npc(s->player.x, s->player.y,
                                      npc->x, npc->y, npc->size);
    if (dist == 0 || dist > stats->attack_range) return;

    /* blob prayer reading: 2-phase attack with attack_speed = 3.
       scan tick: read player prayer, set timer, return (shows scan animation).
       fire tick: determine style from scanned prayer, fall through to common attack.
       total cycle = 6 ticks (3 scan + 3 cooldown).
       ref: InfernoTrainer JalAk.ts attackIfPossible() */
    if (npc->type == INF_NPC_BLOB) {
        if (npc->blob_scanned_prayer < 0) {
            /* no pending scan → start scan phase: read prayer, commit to opposite style */
            npc->blob_scanned_prayer = (int)s->player.prayer;
            if (s->player.prayer == PRAYER_PROTECT_MAGIC) npc->attack_style = ATTACK_STYLE_RANGED;
            else if (s->player.prayer == PRAYER_PROTECT_RANGED) npc->attack_style = ATTACK_STYLE_MAGIC;
            else npc->attack_style = (encounter_rand_int(&s->rng_state, 2) == 0) ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_RANGED;
            npc->attacked_this_tick = 1;  /* triggers scan animation */
            npc->attack_timer = stats->attack_speed;  /* 3 */
            return;
        }
        /* has pending scan → determine style and fall through to fire */
        npc->blob_scanned_prayer = -1;
        /* fall through to common attack code */
    }

    /* determine actual attack style */
    int actual_style = npc->attack_style;

    /* jad: use the committed preview style if present. if the preview was not
       seeded, fall back to the 50/50 primary-style roll. */
    if (npc->type == INF_NPC_JAD) {
        actual_style = npc->jad_attack_style;
        if (actual_style == ATTACK_STYLE_NONE)
            actual_style = inf_jad_roll_primary_style(&s->rng_state);
    }

    /* zuk: typeless attack (not blockable by prayer).
       InfernoTrainer TzKalZuk.ts: Zuk always fires at the shield if the player is
       behind it (shield absorbs, 0 damage). otherwise fires at the player.
       hit delay = 4 ticks (ZukWeapon: setDelay=4). */
    if (npc->type == INF_NPC_ZUK) {
        int si = s->zuk.shield_idx;
        int player_behind_shield = 0;
        if (si >= 0 && s->npcs[si].active) {
            InfNPC* shield = &s->npcs[si];
            player_behind_shield = (s->player.x >= shield->x &&
                                    s->player.x < shield->x + shield->size &&
                                    s->player.y >= 41);
        }

        if (player_behind_shield) {
            /* shield absorbs — Zuk fires at shield, 0 damage */
            npc->attacked_this_tick = 1;
            npc->attack_style_this_tick = ATTACK_STYLE_MAGIC;
            npc->attack_visual_target = si;
        } else {
            /* typeless hit on player — not blockable by prayer, no accuracy roll.
               rolled 0..max_hit, queued at T and lands at T+4 unchanged. */
            int max_hit = osrs_npc_magic_max_hit(stats->magic_base_dmg, stats->magic_dmg_pct);
            int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
            if (s->player_pending_hit_count < ENCOUNTER_MAX_PENDING_HITS) {
                EncounterPendingHit* ph = &s->player_pending_hits[s->player_pending_hit_count++];
                ph->active = 1;
                ph->damage = dmg;
                ph->ticks_remaining = 4;
                ph->attack_style = ATTACK_STYLE_NONE;  /* typeless — not blockable */
                ph->check_prayer = 0;
                ph->prayer_check_delay = 0;
                ph->spell_type = 0;
                ph->source_npc_type = npc->type;
                ph->hit_success = 1;
                ph->elysian_reduced = 0;
            }
            s->last_hit_by_type = INF_NPC_ZUK;
            npc->attacked_this_tick = 1;
            npc->attack_style_this_tick = ATTACK_STYLE_MAGIC;
            /* attack_visual_target = -1 (player), already default */
        }
        npc->attack_timer = s->zuk.enraged ? 7 : stats->attack_speed;
        return;
    }

    {
        int style_mask = inf_attack_style_options_mask(
            s, npc, stats, actual_style, dist);
        actual_style = inf_choose_attack_style_for_tick(
            &s->rng_state, style_mask);
    }

    if (npc->type == INF_NPC_MAGER &&
        actual_style == ATTACK_STYLE_MAGIC &&
        inf_mager_resurrect_ctx(s, ctx, idx)) {
        npc->attacked_this_tick = 1;
        npc->attack_style_this_tick = ATTACK_STYLE_NONE;
        npc->attack_timer = stats->attack_speed;
        return;
    }

    /* max hit from stats + bonuses via OSRS combat formulas */
    int max_hit = osrs_npc_max_hit(actual_style,
        stats->str_level, stats->range_level,
        stats->melee_str_bonus, stats->ranged_str_bonus,
        stats->magic_base_dmg, stats->magic_dmg_pct);
    if (stats->max_hit_cap > 0 && max_hit > stats->max_hit_cap)
        max_hit = stats->max_hit_cap;
    int is_delayed_jad = (npc->type == INF_NPC_JAD &&
                          actual_style != ATTACK_STYLE_MELEE);
    int dmg = is_delayed_jad ? max_hit : encounter_rand_int(&s->rng_state, max_hit + 1);
    int accuracy_hit = 1;

    /* accuracy roll: NPC attack roll vs player defence roll */
    if (!is_delayed_jad) {
        int att_lvl, att_bonus;
        if (actual_style == ATTACK_STYLE_MELEE) {
            att_lvl = stats->att_level; att_bonus = stats->melee_att_bonus;
        } else if (actual_style == ATTACK_STYLE_RANGED) {
            att_lvl = stats->range_level; att_bonus = stats->range_att_bonus;
        } else {
            att_lvl = stats->magic_level; att_bonus = stats->magic_att_bonus;
        }
        int att_roll = osrs_npc_attack_roll(att_lvl, att_bonus);
        const EncounterLoadoutStats* ls = inf_current_loadout_stats(s, ctx);
        int def_bonus = encounter_player_def_bonus(
            ls->def_stab, ls->def_slash, ls->def_crush, ls->def_magic, ls->def_ranged,
            actual_style, stats->melee_style);
        int def_roll = osrs_player_def_roll_vs_npc(s->player.current_defence, s->player.current_magic, def_bonus, actual_style);
        if (encounter_rand_float(&s->rng_state) >= osrs_hit_chance(att_roll, def_roll)) {
            dmg = 0;  /* missed */
            accuracy_hit = 0;
        }
    }

    EncounterProjectileTiming hit_timing =
        inf_npc_projectile_timing(npc->type, actual_style, dist);
    int hit_delay = hit_timing.damage_delay_ticks;
    if (is_delayed_jad)
        hit_delay += INF_JAD_PROJECTILE_DELAY;

    /* track which styles fired this tick for multi-style analysis */
    if (actual_style == ATTACK_STYLE_MELEE) s->tick_styles_fired |= 1;
    else if (actual_style == ATTACK_STYLE_RANGED) s->tick_styles_fired |= 2;
    else if (actual_style == ATTACK_STYLE_MAGIC) s->tick_styles_fired |= 4;
    s->tick_attacks_fired++;
    s->attacks_by_type[npc->type]++;

    /* bat (JalMejRah): drain 3 run energy (300 internal) on every attack.
       ref: OSRS wiki Jal-MejRah, InfernoTrainer JalMejRah.ts */
    if (npc->type == INF_NPC_BAT) {
        s->player.run_energy -= 300;
        if (s->player.run_energy < 0) s->player.run_energy = 0;
    }

    if (hit_delay == 0) {
        /* melee: instant damage, check prayer now */
        int prayer_matches = encounter_prayer_correct_for_style(s->player.prayer, actual_style);
          if (prayer_matches) { dmg = 0; s->prayer_correct_this_tick++; s->prayer_correct_by_type[npc->type]++; }
          else if (dmg > 0) { s->off_prayer_hits_this_tick++; }
        int elysian_reduced = 0;
        dmg = inf_apply_elysian_to_player_hit(s, dmg, &elysian_reduced);
        if (elysian_reduced) s->player.elysian_proc_this_tick = 1;
        inf_apply_echo_boots_recoil(s, dmg);
        s->dmg_from_type[npc->type] += (float)dmg;
        if (dmg > 0) s->last_hit_by_type = npc->type;
        inf_damage_player_from_type(s, npc->type, dmg);
    } else {
            /* ranged/magic: queue pending hit on player */
        if (s->player_pending_hit_count < ENCOUNTER_MAX_PENDING_HITS) {
            int is_jad = (npc->type == INF_NPC_JAD);
            if (!is_jad) {
                int prayer_matches = encounter_prayer_correct_for_style(s->player.prayer, actual_style);
          if (prayer_matches) { dmg = 0; s->prayer_correct_this_tick++; s->prayer_correct_by_type[npc->type]++; }
          else if (dmg > 0) { s->off_prayer_hits_this_tick++; }
            }
            if (!is_delayed_jad) {
                s->dmg_from_type[npc->type] += (float)dmg;
                if (dmg > 0) s->last_hit_by_type = npc->type;
            }
            /* bat stat drain: 50% chance on successful hit when not praying protect
               from missiles, drain all combat stats by 1. ref: OSRS wiki Jal-MejRah */
            if (npc->type == INF_NPC_BAT && dmg > 0 &&
                encounter_rand_int(&s->rng_state, 2) == 0) {
                if (s->player.current_attack > 0) s->player.current_attack--;
                if (s->player.current_strength > 0) s->player.current_strength--;
                if (s->player.current_defence > 0) s->player.current_defence--;
                if (s->player.current_ranged > 0) s->player.current_ranged--;
                if (s->player.current_magic > 0) s->player.current_magic--;
            }
            EncounterPendingHit* ph = &s->player_pending_hits[s->player_pending_hit_count++];
            ph->active = 1;
            ph->damage = dmg;
            ph->ticks_remaining = hit_delay;
            ph->attack_style = actual_style;
            ph->check_prayer = is_jad ? 1 : 0;
            ph->prayer_check_delay = is_jad ? INF_JAD_PROJECTILE_DELAY + 1 : 0;
            ph->spell_type = 0;
            ph->source_npc_type = npc->type;
            ph->hit_success = accuracy_hit;
            ph->elysian_reduced = 0;
        }
    }

    npc->attacked_this_tick = 1;
    npc->attack_style_this_tick = actual_style;
    if (npc->type == INF_NPC_JAD)
        npc->jad_attack_style = ATTACK_STYLE_NONE;
    npc->attack_timer = stats->attack_speed;

    /* jad attack speed varies by wave */
    if (npc->type == INF_NPC_JAD) {
        if (s->wave == 66)      npc->attack_timer = 8;  /* wave 67 */
        else if (s->wave == 67) npc->attack_timer = 9;  /* wave 68 */
        else                    npc->attack_timer = 8;  /* zuk wave */
    }
}

static void inf_npc_attack(InfernoState* s, int idx) {
    inf_npc_attack_ctx(s, inf_legacy_context(), idx);
}


static int inf_find_mager_respawn_tile(
    InfernoState* s, const InfernoContext* ctx, int size, int* out_x, int* out_y
) {
    InfMoveCtx mc = { s, ctx, -1 };
    for (int x = 26; x < 33; x++) {
        for (int y = 24; y < 37; y++) {
            if (inf_npc_blocked(&mc, x, y, size)) continue;
            if (encounter_entity_footprints_overlap(x, y, size,
                                                    s->player.x, s->player.y, 1))
                continue;
            *out_x = x;
            *out_y = y;
            return 1;
        }
    }

    *out_x = 21;
    *out_y = 22;
    return !inf_npc_blocked(&mc, *out_x, *out_y, size);
}

/* mager resurrection is only evaluated on a real magic attack opportunity,
   not every NPC tick. */
static int inf_mager_resurrect_ctx(InfernoState* s, InfernoContext* ctx, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (npc->type != INF_NPC_MAGER || !npc->active) return 0;
    if (s->wave >= 68) return 0;  /* no resurrection during Zuk wave */
    if (npc->resurrect_cooldown > 0) return 0;
    if (s->dead_mob_count == 0) return 0;

    /* 10% chance when the mager converts a ready magic attack into a respawn. */
    if (encounter_rand_int(&s->rng_state, 10) != 0) return 0;

    /* pick a random dead mob */
    int di = encounter_rand_int(&s->rng_state, s->dead_mob_count);
    InfDeadMob* dm = &s->dead_mobs[di];

    int slot = inf_find_free_npc(s);
    if (slot < 0) return 0;

    int rx = 21;
    int ry = 22;
    if (!inf_find_mager_respawn_tile(s, ctx, INF_NPC_STATS[dm->type].size, &rx, &ry))
        return 0;

    inf_init_npc(s, slot, dm->type, rx, ry);
    s->npcs[slot].hp = dm->hp;      /* 50% of max HP */
    s->npcs[slot].max_hp = dm->max_hp;
    s->npcs[slot].resurrection_count = 1;
    s->npcs[slot].attack_timer = INF_NPC_STATS[dm->type].attack_speed;

    /* remove from dead store (swap with last) */
    s->dead_mobs[di] = s->dead_mobs[s->dead_mob_count - 1];
    s->dead_mob_count--;

    /* 8-tick cooldown */
    npc->resurrect_cooldown = 8;
    npc->resurrecting_this_tick = 1;
    npc->resurrection_visual_target = slot;
    npc->attack_visual_target = slot;
    return 1;
}

static int inf_mager_resurrect(InfernoState* s, int idx) {
    return inf_mager_resurrect_ctx(s, inf_legacy_context(), idx);
}


#define INF_JAD_HEALER_MAX_SPAWN_CANDIDATES 165

static int inf_try_sample_jad_healer_spawn(
    InfernoState* s, const InfernoContext* ctx, const InfNPC* jad,
    int min_dx, int max_dx, int min_dy, int max_dy,
    int* out_x, int* out_y
) {
    int offsets[INF_JAD_HEALER_MAX_SPAWN_CANDIDATES][2];
    int order[INF_JAD_HEALER_MAX_SPAWN_CANDIDATES];
    int count = 0;
    for (int dx = min_dx; dx <= max_dx; dx++) {
        for (int dy = min_dy; dy <= max_dy; dy++) {
            assert(count < INF_JAD_HEALER_MAX_SPAWN_CANDIDATES);
            offsets[count][0] = dx;
            offsets[count][1] = dy;
            order[count] = count;
            count++;
        }
    }
    encounter_shuffle(order, count, &s->rng_state);

    for (int i = 0; i < count; i++) {
        int hx = jad->x + offsets[order[i]][0];
        int hy = jad->y + offsets[order[i]][1];
        if (encounter_entity_footprints_overlap(hx, hy, 1, jad->x, jad->y, jad->size))
            continue;
        if (inf_npc_terrain_blocked_ctx(s, ctx, hx, hy, 1))
            continue;
        *out_x = hx;
        *out_y = hy;
        return 1;
    }

    return 0;
}

static void inf_sample_jad_healer_spawn(
    InfernoState* s,
    const InfernoContext* ctx,
    const InfNPC* jad,
    int* out_x,
    int* out_y
) {
    if (s->wave == 68 &&
        inf_try_sample_jad_healer_spawn(s, ctx, jad, 0, 5, 5, 8, out_x, out_y))
        return;

    if (inf_try_sample_jad_healer_spawn(s, ctx, jad, -5, 5, -4, 10, out_x, out_y))
        return;

    fprintf(stderr, "FATAL: no valid Jad healer spawn tile for Jad at (%d,%d) on wave %d\n",
            jad->x, jad->y, s->wave + 1);
    abort();
}

static void inf_jad_check_healers_ctx(InfernoState* s, const InfernoContext* ctx, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (npc->type != INF_NPC_JAD || !npc->active) return;
    if (npc->jad_healer_spawned) return;

    /* spawn healers when below 50% HP */
    if (npc->hp > npc->max_hp / 2) return;
    npc->jad_healer_spawned = 1;

    int num_healers;
    if (s->wave == 66)      num_healers = 5;  /* wave 67: 5 healers */
    else if (s->wave == 67) num_healers = 3;  /* wave 68: 3 per jad */
    else                    num_healers = 3;  /* zuk wave: 3 */

    for (int h = 0; h < num_healers; h++) {
        int slot = inf_find_free_npc(s);
        if (slot < 0) break;
        int hx = npc->x;
        int hy = npc->y;
        inf_sample_jad_healer_spawn(s, ctx, npc, &hx, &hy);
        inf_init_npc(s, slot, INF_NPC_HEALER_JAD, hx, hy);
        s->npcs[slot].jad_owner_idx = idx;
        s->npcs[slot].aggro_target = idx;
    }
}

static void inf_jad_check_healers(InfernoState* s, int idx) {
    inf_jad_check_healers_ctx(s, inf_legacy_context(), idx);
}


static void inf_zuk_tick(InfernoState* s) {
    if (!inf_is_final_wave(s)) return;

    /* find zuk NPC */
    int zuk_idx = inf_find_live_zuk_idx(s);
    if (zuk_idx < 0) return;
    InfNPC* zuk = &s->npcs[zuk_idx];

    /* shield oscillation */
    int si = s->zuk.shield_idx;
    if (si >= 0 && s->npcs[si].active) {
        InfNPC* shield = &s->npcs[si];
        if (s->zuk.shield_freeze > 0) {
            s->zuk.shield_freeze--;
        } else {
            int ox = shield->x;
            int oy = shield->y;
            if (inf_npc_sets_collision_flag(shield->type))
                inf_unstamp_npc_collision_footprint(s, ox, oy, shield->size);
            shield->x += s->zuk.shield_dir;
            /* boundary check: 5-tick freeze at edges */
            if (shield->x < 11) {
                shield->x = 11;
                s->zuk.shield_freeze = 5;
                s->zuk.shield_dir = 1;
            } else if (shield->x > 35) {
                shield->x = 35;
                s->zuk.shield_freeze = 5;
                s->zuk.shield_dir = -1;
            }
            if (inf_npc_sets_collision_flag(shield->type))
                inf_stamp_npc_collision_footprint(s, shield->x, shield->y, shield->size);
        }
    }

    /* set timer pause: freeze at HP < 600, resume at HP < 480 with +175 ticks */
    if (!s->zuk.has_paused && zuk->hp < 600) {
        s->zuk.timer_paused = 1;
        s->zuk.has_paused = 1;
    }

    /* jad spawn at HP < 480 (with shield alive, timer paused) */
    if (!s->zuk.jad_spawned && s->zuk.timer_paused && zuk->hp < 480 &&
        si >= 0 && s->npcs[si].active) {
        s->zuk.jad_spawned = 1;
        s->zuk.timer_paused = 0;
        s->zuk.set_timer += 175;
        int j_slot = inf_find_free_npc(s);
        if (j_slot >= 0) {
            inf_init_npc(s, j_slot, INF_NPC_JAD, 24, 32);
            s->npcs[j_slot].aggro_target = si;  /* target shield */
            s->npcs[j_slot].stun_timer = 7;     /* spawn delay */
        }
    }

    /* set timer: spawns JalZek + JalXil targeting the shield */
    if (!s->zuk.timer_paused) {
        if (s->zuk.set_timer > 0)
            s->zuk.set_timer--;
        if (s->zuk.set_timer == 0) {
            int m_slot = inf_find_free_npc(s);
            if (m_slot >= 0) {
                inf_init_npc(s, m_slot, INF_NPC_MAGER, 20, 36);
                if (si >= 0) s->npcs[m_slot].aggro_target = si;
                s->npcs[m_slot].stun_timer = 7;  /* spawn delay */
            }
            int r_slot = inf_find_free_npc(s);
            if (r_slot >= 0) {
                inf_init_npc(s, r_slot, INF_NPC_RANGER, 29, 36);
                if (si >= 0) s->npcs[r_slot].aggro_target = si;
                s->npcs[r_slot].stun_timer = 9;  /* spawn delay */
            }
            s->zuk.set_timer = s->zuk.set_interval;
        }
    }

    /* healer spawn at HP < 240: 4 JalMejJak targeting Zuk, sets enraged */
    if (!s->zuk.healer_spawned && zuk->hp < 240) {
        s->zuk.healer_spawned = 1;
        s->zuk.enraged = 1;
        static const int healer_pos[4][2] = {
            {16, 48}, {20, 48}, {30, 48}, {34, 48}
        };
        for (int h = 0; h < 4; h++) {
            int slot = inf_find_free_npc(s);
            if (slot >= 0) {
                inf_init_npc(s, slot, INF_NPC_HEALER_ZUK,
                             healer_pos[h][0], healer_pos[h][1]);
                s->npcs[slot].aggro_target = zuk_idx;  /* heal Zuk until tagged */
            }
        }
    }

    /* on zuk death: all other mobs die */
    if (zuk->hp <= 0) {
        for (int i = 0; i < INF_MAX_NPCS; i++) {
            inf_deactivate_npc(s, i);
        }
    }
}

static void inf_healer_apply_landed_heal(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    int target_idx = npc->heal_target;
    int heal_cap = (npc->type == INF_NPC_HEALER_ZUK) ? 25 : 20;

    npc->heal_target = -1;
    if (target_idx < 0 || target_idx >= INF_MAX_NPCS) return;
    if (!s->npcs[target_idx].active) return;

    int heal = encounter_rand_int(&s->rng_state, heal_cap);
    int before = s->npcs[target_idx].hp;
    s->npcs[target_idx].hp += heal;
    if (s->npcs[target_idx].hp > s->npcs[target_idx].max_hp)
        s->npcs[target_idx].hp = s->npcs[target_idx].max_hp;
    float effective_heal = (float)(s->npcs[target_idx].hp - before);
    s->hp_restored_this_tick += effective_heal;
    if (s->npcs[target_idx].type == INF_NPC_JAD)
        s->hp_restored_jad_this_tick += effective_heal;
    if (s->npcs[target_idx].type == INF_NPC_ZUK)
        s->hp_restored_zuk_this_tick += effective_heal;
}

static void inf_record_redemption_pressure(
    InfernoState* s,
    int source_type,
    int damage
) {
    if (s->player.current_prayer <= 0) return;

    int before_hp = s->player.current_hitpoints;
    int after_hp = before_hp - (damage > 0 ? damage : 0);
    if (after_hp < 0) after_hp = 0;

    int before_band = before_hp > 0 && before_hp <= 9;
    int after_band = after_hp > 0 && after_hp <= 9;
    int source_valid = source_type >= 0 && source_type < INF_NUM_NPC_TYPES;
    int after_240 = s->tick_at_le_240 >= 0;

    if (before_band || after_band) {
        int heal = s->player.base_prayer / 4;
        int missing = s->player.base_hitpoints - after_hp;
        float heal_potential = (float)(missing > heal ? heal : missing);
        if (heal_potential < 0.0f) heal_potential = 0.0f;

        s->redemption_proc_opportunities++;
        s->redemption_heal_potential += heal_potential;
        if (damage <= 0)
            s->redemption_zero_hit_proc_opportunities++;
        if (after_240) {
            s->redemption_proc_opportunities_after_240++;
            s->redemption_heal_potential_after_240 += heal_potential;
        }
        if (source_valid) {
            s->redemption_proc_opportunities_by_type[source_type]++;
            s->redemption_heal_potential_by_type[source_type] += heal_potential;
            if (damage <= 0)
                s->redemption_zero_hit_proc_opportunities_by_type[source_type]++;
        }
    }

    if (damage > 0 && before_hp > 0 && after_hp <= 0) {
        if (before_band) {
            s->redemption_deaths_from_band++;
            if (after_240)
                s->redemption_deaths_from_band_after_240++;
            if (source_valid)
                s->redemption_deaths_from_band_by_type[source_type]++;
        } else {
            s->redemption_deaths_from_above_band++;
        }
    }
}

static int inf_redemption_threshold(const InfernoState* s) {
    return (s->player.base_hitpoints - 1) / 10;
}

static void inf_apply_redemption_if_due(InfernoState* s, int before_hp, int damage) {
    Player* p = &s->player;
    if (p->prayer != PRAYER_REDEMPTION || p->current_prayer <= 0)
        return;
    if (p->current_hitpoints <= 0)
        return;

    int threshold = inf_redemption_threshold(s);
    int before_band = before_hp > 0 && before_hp <= threshold;
    int after_band = p->current_hitpoints > 0 && p->current_hitpoints <= threshold;
    if (!before_band && !after_band)
        return;

    int old_offensive = p->offensive_prayer;
    int hp_before_heal = p->current_hitpoints;
    int heal = p->base_prayer / 4;
    p->current_hitpoints += heal;
    if (p->current_hitpoints > p->base_hitpoints)
        p->current_hitpoints = p->base_hitpoints;
    s->redemption_proc_count++;
    if (damage <= 0)
        s->redemption_zero_hit_proc_count++;
    s->redemption_heal_done += (float)(p->current_hitpoints - hp_before_heal);
    p->current_prayer = 0;
    p->prayer = PRAYER_NONE;
    p->offensive_prayer = OFFENSIVE_PRAYER_NONE;
    p->prayer_drain_counter = 0;
    p->prayer_just_activated = 0;
    p->offensive_prayer_just_activated = 0;
    if (old_offensive != OFFENSIVE_PRAYER_NONE)
        encounter_recompute_loadout_max_hits(s->loadout_stats, INF_NUM_WEAPON_SETS, p);
}

static void inf_damage_player_from_type(
    InfernoState* s,
    int source_type,
    int damage
) {
    int before_hp = s->player.current_hitpoints;
    inf_record_redemption_pressure(s, source_type, damage);
    encounter_damage_player(&s->player, damage, &s->damage_received_this_tick);
    inf_apply_redemption_if_due(s, before_hp, damage);
}

static void inf_queue_pending_spark(
    InfernoState* s, int src_x, int src_y, int x, int y, int damage, int ticks_remaining
) {
    for (int i = 0; i < INF_MAX_PENDING_SPARKS; i++) {
        if (s->pending_sparks[i].active) continue;
        s->pending_sparks[i].active = 1;
        s->pending_sparks[i].src_x = src_x;
        s->pending_sparks[i].src_y = src_y;
        s->pending_sparks[i].x = x;
        s->pending_sparks[i].y = y;
        s->pending_sparks[i].damage = damage;
        s->pending_sparks[i].ticks_remaining = ticks_remaining;
        s->pending_sparks[i].visual_emitted = 0;
        return;
    }
    fprintf(stderr, "inferno: pending spark queue overflow at %d slots\n",
        INF_MAX_PENDING_SPARKS);
    abort();
}

static void inf_queue_zuk_healer_sparks(InfernoState* s, const InfNPC* npc) {
    int clamped_x = s->player.x;
    if (clamped_x < npc->x - 5) clamped_x = npc->x - 5;
    if (clamped_x > npc->x + 4) clamped_x = npc->x + 4;

    /* InfernoTrainer JalMejJak AoeWeapon: 2 random sparks target y=14+rand(0..3)
       in reference coords. our coord system is Y-flipped (ours_y = 57 - ref_y),
       so ref y=14..17 maps to ours y=40..43 (near player/zuk band). */
    inf_queue_pending_spark(s, npc->x, npc->y, clamped_x, s->player.y,
                            5 + encounter_rand_int(&s->rng_state, 6), 4);
    inf_queue_pending_spark(s,
                            npc->x, npc->y,
                            npc->x + encounter_rand_int(&s->rng_state, 11) - 5,
                            40 + encounter_rand_int(&s->rng_state, 4),
                            5 + encounter_rand_int(&s->rng_state, 6), 4);
    inf_queue_pending_spark(s,
                            npc->x, npc->y,
                            npc->x + encounter_rand_int(&s->rng_state, 11) - 5,
                            40 + encounter_rand_int(&s->rng_state, 4),
                            5 + encounter_rand_int(&s->rng_state, 6), 4);
}

static void inf_resolve_pending_sparks(InfernoState* s) {
    for (int i = 0; i < INF_MAX_PENDING_SPARKS; i++) {
        if (!s->pending_sparks[i].active) continue;
        s->pending_sparks[i].ticks_remaining--;
        if (s->pending_sparks[i].ticks_remaining > 0) continue;

        if (encounter_entity_footprints_overlap(
                s->pending_sparks[i].x - 1, s->pending_sparks[i].y - 1, 3,
                s->player.x, s->player.y, 1)) {
            inf_damage_player_from_type(
                s, INF_NPC_HEALER_ZUK, s->pending_sparks[i].damage);
            s->spark_damage_this_tick += (float)s->pending_sparks[i].damage;
            s->last_hit_by_type = INF_NPC_HEALER_ZUK;
        }
        s->pending_sparks[i].active = 0;
    }
}


static void inf_tick_npcs_ctx(InfernoState* s, InfernoContext* ctx) {
    /* NPC per-tick flags are cleared in inf_step BEFORE inf_tick_player,
       so player hit flags survive through both tick functions into render_post_tick. */

    inf_resolve_npc_target_hits(s);
    inf_zuk_tick(s);

    int blob_split_x[INF_MAX_NPCS];
    int blob_split_y[INF_MAX_NPCS];
    int blob_split_parent[INF_MAX_NPCS];
    int blob_split_count = 0;

    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (!s->npcs[i].active) continue;

        if ((s->npcs[i].type == INF_NPC_HEALER_JAD || s->npcs[i].type == INF_NPC_HEALER_ZUK) &&
            s->npcs[i].death_ticks == 0 && s->npcs[i].heal_timer > 0) {
            s->npcs[i].heal_timer--;
            if (s->npcs[i].heal_timer == 0)
                inf_healer_apply_landed_heal(s, i);
        }

        if (s->npcs[i].death_ticks > 0) {
            if (osrs_npc_death_linger_tick(&s->npcs[i].death_ticks)) {
                if (s->npcs[i].spawn_blob_splits_on_removal &&
                    blob_split_count < INF_MAX_NPCS) {
                    blob_split_x[blob_split_count] = s->npcs[i].x;
                    blob_split_y[blob_split_count] = s->npcs[i].y;
                    blob_split_parent[blob_split_count] = i;
                    blob_split_count++;
                }
                inf_deactivate_npc(s, i);
            }
            continue;
        }

        if (s->npcs[i].frozen_ticks > 0) s->npcs[i].frozen_ticks--;

        if (s->npcs[i].type == INF_NPC_MAGER && s->npcs[i].resurrect_cooldown > 0)
            s->npcs[i].resurrect_cooldown--;

        /* meleer dig check */
        if (s->npcs[i].type == INF_NPC_MELEER)
            inf_meleer_dig_check_ctx(s, ctx, i);

        inf_npc_move_ctx(s, ctx, i);
        inf_npc_attack_ctx(s, ctx, i);

        /* jad healer spawning */
        if (s->npcs[i].type == INF_NPC_JAD)
            inf_jad_check_healers_ctx(s, ctx, i);
    }

    for (int i = 0; i < blob_split_count; i++) {
        InfNPCType split_types[3] = {
            INF_NPC_BLOB_RANGE, INF_NPC_BLOB_MELEE, INF_NPC_BLOB_MAGE
        };
        int split_offsets[3][2] = {
            {1, 1}, {0, 0}, {2, 2}
        };
        for (int sp = 0; sp < 3; sp++) {
            int slot = -1;
            for (int n = 0; n < INF_MAX_NPCS; n++) {
                if (n == blob_split_parent[i] || s->npcs[n].active) continue;
                slot = n;
                break;
            }
            if (slot < 0) break;
            inf_init_npc(
                s, slot, split_types[sp],
                blob_split_x[i] + split_offsets[sp][0],
                blob_split_y[i] + split_offsets[sp][1]);
            s->npcs[slot].attack_timer = 4;
        }
    }
}

static void inf_tick_npcs(InfernoState* s) {
    inf_tick_npcs_ctx(s, inf_legacy_context());
}
