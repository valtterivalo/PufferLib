#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_player_actions.inc
 * @brief Action head layout + player tick, mirroring inferno player_actions.
 *
 * Action heads (10): MOVE, PRAYER, TARGET, GEAR, EAT, POTION, SPEC,
 * MODIFIER_SELECT, GRAPPLE_PARRY, OFFENSIVE. The player tick reuses the shared
 * osrs_encounter_player_step orchestrator for target acquisition, movement, and
 * chase-into-range.
 */

#define COLO_HEAD_MOVE            0  /* 25: idle + 8 walk + 16 run */
#define COLO_HEAD_PRAYER          1  /* overhead protect set */
#define COLO_HEAD_TARGET          2  /* COLO_OBS_NPCS+1: none or obs slot */
#define COLO_HEAD_GEAR            3  /* no_switch, melee, ranged */
#define COLO_HEAD_EAT             4  /* none, brew (L14: no solid food in any kit) */
#define COLO_HEAD_POTION          5  /* none, restore, super combat, ranging, surge (L15) */
#define COLO_HEAD_SPEC            6  /* none + arm/disarm spec weapon A/B (L2) */
#define COLO_HEAD_MODIFIER_SELECT 7  /* none + pick draft option 0/1/2 */
#define COLO_HEAD_GRAPPLE_PARRY   8  /* none + the 5 grapple callout slots (A12) */
#define COLO_HEAD_OFFENSIVE       9  /* no_change, off, piety, rigour, augury */

#define COLO_GEAR_DIM 3
#define COLO_EAT_DIM 2
#define COLO_POTION_DIM 5
#define COLO_SPEC_DIM (1 + COLO_NUM_SPEC_WEAPONS)  /* 3 */

/* COLO_HEAD_POTION action indices */
#define COLO_POTION_RESTORE 1
#define COLO_POTION_COMBAT  2
#define COLO_POTION_RANGING 3
#define COLO_POTION_SURGE   4

#define COLO_MODIFIER_SELECT_DIM (1 + COLO_MODIFIER_DRAFT_OPTIONS)  /* 4 */
/** grapple-parry head: index 0 = no click, index k = click grapple slot (k-1).
    A12: the domain is the 5-slot ColoGrappleSlot callout set, matching
    SolHereditState.grapple_body_slot. */
#define COLO_GRAPPLE_PARRY_DIM (1 + COLO_NUM_GRAPPLE_SLOTS)  /* 6 */

#define COLO_ACTION_DIMS_INIT { \
    ENCOUNTER_MOVE_ACTIONS, \
    ENCOUNTER_OVERHEAD_DIM_PVE, \
    COLO_OBS_NPCS + 1, \
    COLO_GEAR_DIM, \
    COLO_EAT_DIM, \
    COLO_POTION_DIM, \
    COLO_SPEC_DIM, \
    COLO_MODIFIER_SELECT_DIM, \
    COLO_GRAPPLE_PARRY_DIM, \
    ENCOUNTER_OFFENSIVE_DIM \
}
static const int COLO_ACTION_DIMS[COLO_NUM_ACTION_HEADS] = COLO_ACTION_DIMS_INIT;
#define COLO_ACTION_MASK_SIZE (ENCOUNTER_MOVE_ACTIONS + ENCOUNTER_OVERHEAD_DIM_PVE + \
    COLO_OBS_NPCS + 1 + COLO_GEAR_DIM + COLO_EAT_DIM + COLO_POTION_DIM + COLO_SPEC_DIM + \
    COLO_MODIFIER_SELECT_DIM + COLO_GRAPPLE_PARRY_DIM + ENCOUNTER_OFFENSIVE_DIM)

static int col_action_head_mask_offset(int head) {
    int offset = 0;
    for (int h = 0; h < head; h++)
        offset += COLO_ACTION_DIMS[h];
    return offset;
}


/** L12: recompute every loadout's effective levels + max hits (weapon sets and
    spec weapons) after any stat-moving drink or offensive prayer change. */
static void col_recompute_all_loadout_stats(ColosseumState* s) {
    encounter_recompute_loadout_max_hits(s->loadout_stats, COLO_NUM_WEAPON_SETS, &s->player);
    encounter_recompute_loadout_max_hits(s->spec_stats, COLO_NUM_SPEC_WEAPONS, &s->player);
}

/** true when the profile's combat potion slot is the divine speedrun dose. */
static int col_combat_potion_is_divine(const ColosseumState* s) {
    return s->full_supplies.combat_boost_kind == COLO_BOOST_POTION_DIVINE;
}

/** true when the profile's ranging potion slot is the divine speedrun dose. */
static int col_ranged_potion_is_divine(const ColosseumState* s) {
    return s->full_supplies.ranged_boost_kind == COLO_BOOST_POTION_DIVINE;
}

/** E13: derive active divine stat floors from Colosseum's live timers. */
static EncounterStatDriftPins col_divine_stat_drift_pins(const ColosseumState* s) {
    EncounterStatDriftPins pins = encounter_stat_drift_no_pins();
    if (s->divine_combat_timer > 0) {
        pins = encounter_merge_stat_drift_pins(
            pins, encounter_divine_super_combat_pins(&s->player));
    }
    if (s->divine_ranged_timer > 0) {
        pins = encounter_merge_stat_drift_pins(
            pins, encounter_divine_ranging_pins(&s->player));
    }
    return pins;
}

/** E13: active divine boosts prevent their potion stats from draining below
    the boosted floor. Returns 1 when loadout max hits need recomputing. */
static int col_enforce_divine_stat_floors(ColosseumState* s) {
    return encounter_enforce_stat_drift_pins(&s->player, col_divine_stat_drift_pins(s));
}

/** Apply colosseum's divine floor tail and recompute every loadout after stat changes. */
static void col_finish_stat_moving_consumable(ColosseumState* s) {
    col_enforce_divine_stat_floors(s);
    col_recompute_all_loadout_stats(s);
}

/** E13: expire a divine combat timer and drop its potion stats to base. */
static int col_tick_divine_combat_timer(ColosseumState* s) {
    if (s->divine_combat_timer <= 0) return 0;
    s->divine_combat_timer--;
    if (s->divine_combat_timer > 0) return 0;
    s->player.current_attack = s->player.base_attack;
    s->player.current_strength = s->player.base_strength;
    s->player.current_defence = s->player.base_defence;
    return 1;
}

/** E13: expire a divine ranging timer and drop Ranged to base. */
static int col_tick_divine_ranged_timer(ColosseumState* s) {
    if (s->divine_ranged_timer <= 0) return 0;
    s->divine_ranged_timer--;
    if (s->divine_ranged_timer > 0) return 0;
    s->player.current_ranged = s->player.base_ranged;
    return 1;
}

/** E13: live-gameplay stat drift and divine timers freeze during draft and
    ready gaps, matching the existing surge and venom gap precedent. */
static void col_tick_live_stat_drift_and_divines(ColosseumState* s) {
    int changed = 0;
    changed |= col_tick_divine_combat_timer(s);
    changed |= col_tick_divine_ranged_timer(s);
    changed |= encounter_tick_stat_drift(
        &s->player, &s->stat_drift_timer, col_divine_stat_drift_pins(s));
    if (changed) col_recompute_all_loadout_stats(s);
}

/** apply the 10 HP divine potion self-damage and arm the duration timer. */
static void col_apply_divine_combat_potion_effect(ColosseumState* s) {
    encounter_damage_player(&s->player, 10, &s->tick_scratch.damage_received);
    s->divine_combat_timer = ENCOUNTER_DIVINE_POTION_TICKS;
}

/** apply the 10 HP divine potion self-damage and arm the duration timer. */
static void col_apply_divine_ranged_potion_effect(ColosseumState* s) {
    encounter_damage_player(&s->player, 10, &s->tick_scratch.damage_received);
    s->divine_ranged_timer = ENCOUNTER_DIVINE_POTION_TICKS;
}

/** apply prayer actions + drain, before movement and attack. an offensive
    prayer change recomputes every loadout's prayer multipliers (L12). */
static void col_player_pretick(ColosseumState* s, ColosseumContext* ctx, const int* actions) {
    (void)ctx;
    if (encounter_apply_overhead_action(&s->player.prayer, actions[COLO_HEAD_PRAYER]))
        s->player.prayer_just_activated = 1;
    OffensivePrayer prev_offensive = s->player.offensive_prayer;
    encounter_apply_offensive_action(&s->player.offensive_prayer, actions[COLO_HEAD_OFFENSIVE]);
    if (s->player.offensive_prayer != prev_offensive)
        col_recompute_all_loadout_stats(s);
    encounter_drain_all_prayers(&s->player, encounter_player_prayer_bonus(&s->player));
}


/** apply the active weapon set's loadout to the player. */
static void col_apply_weapon_set(ColosseumState* s, ColoWeaponSet set) {
    if (set < 0 || set >= COLO_NUM_WEAPON_SETS) return;
    if (s->weapon_set == set) return;
    GearSet gear = (set == COLO_GEAR_RANGED) ? GEAR_RANGED : GEAR_MELEE;
    encounter_apply_loadout(&s->player,
        col_loadouts_for_profile(s->active_loadout_profile)[set], gear);
    s->weapon_set = set;
}


typedef struct {
    ColosseumState* s;
} ColoWalkCtx;

static int col_tile_walkable(void* ctx, int x, int y) {
    ColoWalkCtx* wc = (ColoWalkCtx*)ctx;
    return col_player_walkable(wc->s, x, y);
}

static int col_pathfind_blocked(void* ctx, int abs_x, int abs_y) {
    ColoWalkCtx* wc = (ColoWalkCtx*)ctx;
    return col_player_walkable(wc->s, abs_x, abs_y) ? 0 : 1;
}

/** the player's effective attack reach, after Myopia's range reduction. an
    armed spec weapon (all melee) pulls the reach to its own range (L2). */
static int col_player_attack_range(const ColosseumState* s) {
    const EncounterLoadoutStats* ls = (s->spec_armed_kind > 0)
        ? &s->spec_stats[s->spec_armed_kind - 1]
        : &s->loadout_stats[s->weapon_set];
    int base = ls->attack_range > 0 ? ls->attack_range : 1;
    return col_mod_myopia_range(s, base);
}

/** attack-target lookup callback: returns the NPC at an obs slot. The reach the
    chase uses is the PLAYER's effective range (Myopia-reduced), not the NPC's. */
static int col_lookup_player_attack_target(void* ctx, int target_slot, OsrsAttackTarget* out) {
    ColosseumState* s = (ColosseumState*)ctx;
    if (target_slot < 0 || target_slot >= COLO_MAX_NPCS) return 0;
    ColoNPC* npc = &s->npcs[target_slot];
    if (!npc->active) return 0;
    out->slot = target_slot;
    out->x = npc->x;
    out->y = npc->y;
    out->size = col_npc_effective_size(npc);
    out->attack_range = col_player_attack_range(s);
    return 1;
}

/** map an obs slot (action target index) to an NPC array index, or -1. */
static int col_obs_slot_to_npc(const ColosseumState* s, int obs_slot) {
    if (obs_slot < 0 || obs_slot >= COLO_OBS_NPCS) return -1;
    return s->current_obs_slots[obs_slot];
}


/** A14: a weapon's melee style = the argmax of its stab/slash/crush attack
    bonuses (the scythe/fang are slash, elder maul is crush, so Sol's slash-5
    weakness and the right defence lookups are live). */
static MeleeStyle col_item_melee_style(uint8_t weapon_item) {
    const Item* weapon = get_item(weapon_item);
    if (!weapon) return MELEE_STYLE_STAB;
    MeleeStyle style = MELEE_STYLE_STAB;
    int best = weapon->attack_stab;
    if (weapon->attack_slash > best) { best = weapon->attack_slash; style = MELEE_STYLE_SLASH; }
    if (weapon->attack_crush > best) { style = MELEE_STYLE_CRUSH; }
    return style;
}

/** effective NPC defence level after player spec drains (elder maul). */
static int col_npc_effective_def_level(const ColoNPC* npc, const ColoNpcStats* ns) {
    int def = ns->def_level - npc->def_drained;
    return def < 0 ? 0 : def;
}

/** melee defence bonus on `ns` against a given attacking melee style. */
static int col_melee_def_bonus(const ColoNpcStats* ns, MeleeStyle ms) {
    return ms == MELEE_STYLE_SLASH ? ns->slash_def
         : ms == MELEE_STYLE_CRUSH ? ns->crush_def
         : ns->stab_def;
}

/**
 * Resolve a player attack on the targeted NPC. Rolls accuracy + damage via the
 * shared combat math and queues a pending hit. Honors the Fremennik RPS weakness
 * (style match = forced max), the B7 perfect-parry guaranteed max (armed by a
 * last-tick grapple parry, consumed by the very next attack), and the A14
 * melee-style defence lookup (the weapon's style picks stab/slash/crush def).
 * A21+B9: 1-HP hazard entities (totem/bee) die to ANY attack of any style —
 * the hit is guaranteed for their full HP, no rolls, and it does not consume
 * an armed perfect-parry max; the attack still spends the weapon cycle.
 */
/** L2/L3: fire the armed special attack with the spec weapon's loadout stats
    (melee gear base, weapon slot swapped). Queues one pending hit per splat
    (claws cascade = 4), spends energy via the shared resolver cost, applies
    the resolver's SGS heal/prayer restore and the elder maul 35%% defence
    drain, consumes Sol perfect-parry guaranteed max for specs too (E2), and
    runs the post-attack item effects (blood fury heal). */
static void col_player_spec_attack(ColosseumState* s, int npc_slot) {
    ColoNPC* npc = &s->npcs[npc_slot];
    const ColoNpcStats* ns = &COLO_NPC_STATS[npc->type];
    int kind = s->spec_armed_kind;
    uint8_t item = COLO_SPEC_WEAPONS[s->active_loadout_profile][kind - 1];
    const EncounterLoadoutStats* ls = &s->spec_stats[kind - 1];

    s->spec_armed_kind = 0;
    if (!encounter_use_spec(&s->player, osrs_spec_cost(item))) return;

    int eff_def = col_npc_effective_def_level(npc, ns);
    int def_bonus = col_melee_def_bonus(ns, col_item_melee_style(item));
    int def_roll = (eff_def + 9) * (def_bonus + 64);
    int att_roll = osrs_player_att_roll(ls->eff_level, ls->attack_bonus);

    SpecResult r = osrs_resolve_spec(item, att_roll, ls->max_hit, def_roll,
                                     eff_def, &s->rng_state);
    if (s->wave == COLO_WAVE_BOSS && s->sol.started && s->sol.next_attack_guaranteed_max) {
        osrs_spec_result_force_max(&r, item, ls->max_hit, eff_def);
        s->sol.next_attack_guaranteed_max = 0;
        s->sol.guaranteed_max_ticks = 0;
    }

    int total = 0;
    int num_hits = r.num_hits > 0 ? r.num_hits : 1;
    for (int i = 0; i < num_hits; i++) {
        col_queue_npc_pending_hit(s, npc_slot, r.damage[i], 1,
                                  ATTACK_STYLE_MELEE, ENCOUNTER_SPELL_NONE);
        total += r.damage[i];
    }

    if (r.def_drain > 0) {
        npc->def_drained += r.def_drain;
        if (npc->def_drained > ns->def_level) npc->def_drained = ns->def_level;
    }
    if (r.heal > 0) {
        s->player.current_hitpoints += r.heal;
        if (s->player.current_hitpoints > s->player.base_hitpoints)
            s->player.current_hitpoints = s->player.base_hitpoints;
    }
    if (r.prayer_restore > 0) {
        s->player.current_prayer += r.prayer_restore;
        if (s->player.current_prayer > s->player.base_prayer)
            s->player.current_prayer = s->player.base_prayer;
    }

    OsrsPostAttackEffects post = osrs_finalize_attack_effects(
        &s->spec_effects[kind - 1], &s->player.item_effect_state, item,
        ATTACK_STYLE_MELEE, OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1,
        0, total > 0, total, &s->rng_state);
    if (post.heal_amount > 0) {
        s->player.current_hitpoints += post.heal_amount;
        if (s->player.current_hitpoints > s->player.base_hitpoints)
            s->player.current_hitpoints = s->player.base_hitpoints;
    }

    s->player.attack_timer = ls->attack_speed > 0 ? ls->attack_speed : 4;
    s->player_attack_npc_idx = npc_slot;
    s->player_attack_dmg = total;
    s->player_attack_style_id = ATTACK_STYLE_MELEE;
    s->player_attack_timing = (EncounterProjectileTiming){
        .damage_delay_ticks = 1,
        .visual_start_delay_ticks = 0,
        .visual_duration_ticks = 1,
    };
    s->player.attack_style_this_tick = ATTACK_STYLE_MELEE;
    s->player.used_special_this_tick = 1;
    s->tick_scratch.player_attacked = 1;
    s->tick_scratch.attacks_fired++;
}

static void col_player_attack_target(ColosseumState* s, int npc_slot) {
    ColoNPC* npc = &s->npcs[npc_slot];
    const ColoNpcStats* ns = &COLO_NPC_STATS[npc->type];
    const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];

    if (!col_type_is_hazard_entity(npc->type) && s->spec_armed_kind > 0) {
        col_player_spec_attack(s, npc_slot);
        return;
    }

    int rps_max_hit = ns->player_style_that_max_hits != ATTACK_STYLE_NONE &&
                      ns->player_style_that_max_hits == ls->style;

    /* L11: per-set item effects (tbow target-magic scaling, bowfa + crystal
       armour) scale the base roll and max hit before any branch uses them. */
    const OsrsEquipmentEffectProfile* effects = &s->set_effects[s->weapon_set];
    uint8_t weapon_item =
        col_loadouts_for_profile(s->active_loadout_profile)[s->weapon_set][GEAR_SLOT_WEAPON];
    int projectile_dist = encounter_projectile_distance(
        s->player.x, s->player.y, 1,
        npc->x, npc->y, col_npc_effective_size(npc),
        ENCOUNTER_PROJECTILE_DISTANCE_CLOSEST_TILE);
    EncounterProjectileTiming hit_timing =
        col_player_projectile_timing(ls->style, weapon_item, projectile_dist);
    int base_att_roll = osrs_player_att_roll(ls->eff_level, ls->attack_bonus);
    MeleeStyle weapon_melee_style = col_item_melee_style(weapon_item);
    OsrsPreparedAttackEffects prepared = osrs_prepare_attack_effects_for_melee_style(
        effects, &s->player.item_effect_state, weapon_item, ls->style,
        weapon_melee_style, OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1,
        base_att_roll, ls->max_hit,
        osrs_target_effect_context_magic(ns->magic_level, ns->magic_att_bonus),
        s->player.current_hitpoints, s->player.base_hitpoints);

    /* scythe of vitur: 3 splats vs 3x3+ targets, 2 vs 2x2, 1 vs 1x1; splat k
       caps at max/2^k, each rolled independently. This is WHY the scythe is
       the speedrun weapon into 5x5 Sol while the fang wins on 1x1 warbanders
       (wiki Budget footnote j). ref: OSRS wiki Scythe of vitur. */
    int num_splats = 1;
    if (weapon_item == ITEM_SCYTHE_OF_VITUR && !col_type_is_hazard_entity(npc->type))
        num_splats = osrs_scythe_splats_for_target_size(col_npc_effective_size(npc));

    int def_bonus;
    if (ls->style == ATTACK_STYLE_MAGIC) {
        def_bonus = ns->magic_def_bonus;
    } else if (ls->style == ATTACK_STYLE_RANGED) {
        def_bonus = ns->ranged_def_bonus;
    } else {
        def_bonus = col_melee_def_bonus(ns, weapon_melee_style);
    }
    int target_def_roll =
        (col_npc_effective_def_level(npc, ns) + 9) * (def_bonus + 64);

    int ticks = ls->attack_range > 1 ? 3 : 1;
    int damage = 0;
    for (int splat = 0; splat < num_splats; splat++) {
        int splat_max = prepared.max_hit >> splat;
        int splat_damage;
        if (col_type_is_hazard_entity(npc->type)) {
            splat_damage = npc->hp;
        } else if (s->sol.next_attack_guaranteed_max) {
            splat_damage = splat_max;
            s->sol.next_attack_guaranteed_max = 0;
            s->sol.guaranteed_max_ticks = 0;
        } else if (rps_max_hit) {
            splat_damage = splat_max;
        } else {
            splat_damage = osrs_roll_prepared_attack_damage(
                &prepared, target_def_roll, splat_max, &s->rng_state);
        }
        col_queue_npc_pending_hit(s, npc_slot, splat_damage, ticks, ls->style,
                                  ENCOUNTER_SPELL_NONE);
        damage += splat_damage;
    }

    /* blood fury heal (beginner melee neck) via the shared post-attack effects */
    OsrsPostAttackEffects post = osrs_finalize_attack_effects(
        effects, &s->player.item_effect_state, weapon_item, ls->style,
        OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1, 0,
        damage > 0, damage, &s->rng_state);
    if (post.heal_amount > 0) {
        s->player.current_hitpoints += post.heal_amount;
        if (s->player.current_hitpoints > s->player.base_hitpoints)
            s->player.current_hitpoints = s->player.base_hitpoints;
    }

    s->player.attack_timer = ls->attack_speed > 0 ? ls->attack_speed : 4;
    s->player_attack_npc_idx = npc_slot;
    s->player_attack_dmg = damage;
    s->player_attack_style_id = ls->style;
    s->player_attack_timing = hit_timing;
    s->player.attack_style_this_tick = ls->style;
    s->tick_scratch.player_attacked = 1;
    s->tick_scratch.attacks_fired++;
}


/** the main per-tick player routine. OSRS order: target -> move -> attack. */
static void col_tick_player_ctx(
    ColosseumState* s,
    ColosseumContext* ctx,
    const int* actions,
    int can_attack
) {
    /* gear switch */
    int gear_act = actions[COLO_HEAD_GEAR];
    if (gear_act == 1) col_apply_weapon_set(s, COLO_GEAR_MELEE);
    else if (gear_act == 2) col_apply_weapon_set(s, COLO_GEAR_RANGED);

    /* spec energy regen runs every tick, gaps included (OSRS regen never pauses) */
    encounter_tick_spec_regen(&s->player);

    /* consumables — shared 3-tick potion timer; supplies are the researched
       profile inventory (L4/L5/L14/L15). every stat-moving drink recomputes the
       loadout max hits (L12). */
    if (s->player.potion_timer > 0) s->player.potion_timer--;

    int eat_act = actions[COLO_HEAD_EAT];
    if (eat_act == 1 && s->player.brew_doses > 0 && s->player.potion_timer == 0
        && s->player.current_hitpoints < s->player.base_hitpoints) {
        encounter_apply_brew_heal_and_timer(
            &s->player, osrs_brew_heal_amount(s->player.base_hitpoints));
        encounter_apply_consumable_stat_effect(
            &s->player, ENCOUNTER_CONSUMABLE_STAT_EFFECT_BREW_DRAIN);
        col_finish_stat_moving_consumable(s);
    }

    int pot_act = actions[COLO_HEAD_POTION];
    if (pot_act == COLO_POTION_RESTORE &&
            s->player.restore_doses > 0 && s->player.potion_timer == 0) {
        if (s->full_supplies.restore_kind == COLO_RESTORE_SANFEW) {
            encounter_add_prayer_restore(
                &s->player, osrs_sanfew_restore_amount(s->player.base_prayer));
            encounter_apply_consumable_stat_effect(
                &s->player, ENCOUNTER_CONSUMABLE_STAT_EFFECT_SANFEW);
            s->player_venom = 0;
            s->player_venom_timer = 0;
            s->player_poison = 0;
            s->player_poison_timer = 0;
        } else {
            encounter_add_prayer_restore(
                &s->player, osrs_super_restore_amount(s->player.base_prayer));
            encounter_apply_consumable_stat_effect(
                &s->player, ENCOUNTER_CONSUMABLE_STAT_EFFECT_RESTORE);
        }
        encounter_cap_prayer_restore(&s->player);
        encounter_finish_potion_dose(&s->player, &s->player.restore_doses);
        col_finish_stat_moving_consumable(s);
    } else if (pot_act == COLO_POTION_COMBAT &&
            s->player.combat_potion_doses > 0 && s->player.potion_timer == 0 &&
            (!col_combat_potion_is_divine(s) || s->player.current_hitpoints > 10)) {
        encounter_super_combat_boost(&s->player);
        if (col_combat_potion_is_divine(s))
            col_apply_divine_combat_potion_effect(s);
        col_enforce_divine_stat_floors(s);
        encounter_finish_potion_dose(&s->player, &s->player.combat_potion_doses);
        col_recompute_all_loadout_stats(s);
    } else if (pot_act == COLO_POTION_RANGING &&
            s->player.ranged_potion_doses > 0 && s->player.potion_timer == 0 &&
            (!col_ranged_potion_is_divine(s) || s->player.current_hitpoints > 10)) {
        encounter_ranging_boost(&s->player);
        if (col_ranged_potion_is_divine(s))
            col_apply_divine_ranged_potion_effect(s);
        col_enforce_divine_stat_floors(s);
        encounter_finish_potion_dose(&s->player, &s->player.ranged_potion_doses);
        col_recompute_all_loadout_stats(s);
    } else if (pot_act == COLO_POTION_SURGE &&
            s->surge_doses > 0 && s->player.potion_timer == 0 &&
            s->surge_cooldown == 0 && s->player.special_energy < 100) {
        s->player.special_energy += COLO_SURGE_RESTORE_AMOUNT;
        if (s->player.special_energy > 100) s->player.special_energy = 100;
        s->surge_doses--;
        s->surge_cooldown = COLO_SURGE_COOLDOWN_TICKS;
        s->player.potion_timer = 3;
    }

    /* spec arm/disarm (L2): action k toggles spec weapon k-1; arming requires
       the energy for that weapon's cost. firing clears the armed state. */
    int spec_act = actions[COLO_HEAD_SPEC];
    if (spec_act > 0 && spec_act <= COLO_NUM_SPEC_WEAPONS) {
        if (s->spec_armed_kind == spec_act) {
            s->spec_armed_kind = 0;
        } else {
            uint8_t spec_item = COLO_SPEC_WEAPONS[s->active_loadout_profile][spec_act - 1];
            if (s->player.special_energy >= osrs_spec_cost(spec_item))
                s->spec_armed_kind = spec_act;
        }
    }

    /* modifier draft: index 0 = STILL DECIDING (no skip exists, B6); index k
       activates draft option (k-1). Only acts while a draft is pending (the
       mask gates the rest). */
    int modifier_act = actions[COLO_HEAD_MODIFIER_SELECT];
    if (modifier_act > 0 && s->modifiers.draft_pending)
        col_modifier_apply_selection(s, modifier_act - 1);

    /* B6: the player is FROZEN while a draft is open — movement, targeting,
       and attacks are ignored until the pick (which closes draft_pending the
       same tick, so post-pick actions take effect from the next tick). */
    if (s->modifiers.draft_pending) return;

    /* grapple parry: during an active Sol grapple, action index k clicks
       grapple slot (k-1) of the 5-slot A12 domain. A correct slot parries;
       clicking on the last tick is a perfect parry. */
    int grapple_act = actions[COLO_HEAD_GRAPPLE_PARRY];
    if (grapple_act > 0 && grapple_act <= COLO_NUM_GRAPPLE_SLOTS)
        col_sol_grapple_parry_attempt(s, grapple_act - 1);

    /* target acquisition: translate the obs-slot target action into an NPC slot */
    int target_act = actions[COLO_HEAD_TARGET];
    int has_new_target = 0;
    int new_target_slot = -1;
    if (target_act > 0) {
        int npc_slot = col_obs_slot_to_npc(s, target_act - 1);
        if (npc_slot >= 0 && s->npcs[npc_slot].active) {
            has_new_target = 1;
            new_target_slot = npc_slot;
        }
    }

    int has_explicit_move = (actions[COLO_HEAD_MOVE] > 0 || s->player_dest_x >= 0);
    OsrsPlayerMoveKind move_kind = OSRS_PLAYER_MOVE_NONE;
    int move_act = actions[COLO_HEAD_MOVE];
    if (has_explicit_move) {
        if (s->player_dest_x >= 0) move_kind = OSRS_PLAYER_MOVE_DESTINATION;
        else if (move_act > 0 && move_act < ENCOUNTER_MOVE_ACTIONS)
            move_kind = OSRS_PLAYER_MOVE_ACTION;
    }
    ColoWalkCtx wc = { s };
    OsrsLosQuery los_query = col_player_los_query(s);
    (void)osrs_encounter_player_step(&(OsrsPlayerStepInput){
        .player = &s->player,
        .interaction = &s->interaction,
        .target_lookup = col_lookup_player_attack_target,
        .target_ctx = s,
        .has_new_target = has_new_target,
        .new_target_slot = new_target_slot,
        .move_kind = move_kind,
        .move_action = move_act,
        .dest_x = &s->player_dest_x,
        .dest_y = &s->player_dest_y,
        .arena = {
            .collision_map = ctx->collision_map,
            .world_offset_x = ctx->world_offset_x,
            .world_offset_y = ctx->world_offset_y,
            .is_walkable = col_tile_walkable,
            .walkable_ctx = &wc,
            .extra_blocked = col_pathfind_blocked,
            .blocked_ctx = &wc,
            .los_query = &los_query,
            .arena_base_x = COLO_ARENA_MIN_X,
            .arena_base_y = COLO_ARENA_MIN_Y,
            .arena_w = COLO_ARENA_WIDTH,
            .arena_h = COLO_ARENA_HEIGHT,
        },
    });
    col_rebuild_player_collision_flags(s);

    /* attack */
    if (can_attack && s->player.attack_timer > 0) s->player.attack_timer--;
    if (can_attack && osrs_interaction_active(&s->interaction) && s->player.attack_timer == 0) {
        int slot = s->interaction.target_slot;
        ColoNPC* npc = &s->npcs[slot];
        if (npc->active) {
            int player_range = col_player_attack_range(s);
            if (encounter_player_can_attack(s->player.x, s->player.y,
                    npc->x, npc->y, col_npc_effective_size(npc),
                    player_range, &los_query)) {
                col_player_attack_target(s, slot);
            }
        }
    }
}

static void col_tick_player(ColosseumState* s, const int* actions, int can_attack) {
    col_tick_player_ctx(s, col_legacy_context(), actions, can_attack);
}
