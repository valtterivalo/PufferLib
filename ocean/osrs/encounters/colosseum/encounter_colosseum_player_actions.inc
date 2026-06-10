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

/* consumable amounts at base 99 (shared formulas; L4/L5) */
#define COLO_BREW_HEAL          (99 * 15 / 100 + 2)
#define COLO_RESTORE_PRAYER     (8 + 99 / 4)
#define COLO_SANFEW_PRAYER      (4 + 99 * 3 / 10)
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


/** apply prayer actions + drain, before movement and attack. an offensive
    prayer change recomputes every loadout's prayer multipliers (L12). */
static void col_player_pretick(ColosseumState* s, ColosseumContext* ctx, const int* actions) {
    (void)ctx;
    if (encounter_apply_overhead_action(&s->player.prayer, actions[COLO_HEAD_PRAYER]))
        s->player.prayer_just_activated = 1;
    OffensivePrayer prev_offensive = s->player.offensive_prayer;
    encounter_apply_offensive_action(&s->player.offensive_prayer, actions[COLO_HEAD_OFFENSIVE]);
    if (s->player.offensive_prayer != prev_offensive) {
        encounter_recompute_loadout_max_hits(s->loadout_stats, COLO_NUM_WEAPON_SETS, &s->player);
        encounter_recompute_loadout_max_hits(s->spec_stats, COLO_NUM_SPEC_WEAPONS, &s->player);
    }
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

/** L5: sanfew serum stat recovery — 4 + floor(30% base) per combat stat,
    capped at base. weaker per-stat than super restore (8 + 25%) but cures venom.
    ref: OSRS wiki Sanfew serum. */
static void col_sanfew_restore_stats(Player* p) {
    int restore = 4 + p->base_attack * 30 / 100;
    p->current_attack += restore;
    if (p->current_attack > p->base_attack) p->current_attack = p->base_attack;
    restore = 4 + p->base_strength * 30 / 100;
    p->current_strength += restore;
    if (p->current_strength > p->base_strength) p->current_strength = p->base_strength;
    restore = 4 + p->base_defence * 30 / 100;
    p->current_defence += restore;
    if (p->current_defence > p->base_defence) p->current_defence = p->base_defence;
    restore = 4 + p->base_ranged * 30 / 100;
    p->current_ranged += restore;
    if (p->current_ranged > p->base_ranged) p->current_ranged = p->base_ranged;
    restore = 4 + p->base_magic * 30 / 100;
    p->current_magic += restore;
    if (p->current_magic > p->base_magic) p->current_magic = p->base_magic;
}

/** super combat boost: att/str/def each +5 + floor(15% base), cap base + boost.
    L4: divine variants share this (re-boost behavior not modeled).
    ref: OSRS wiki Super combat potion. */
static void col_super_combat_boost(Player* p) {
    int boost = 5 + p->base_attack * 15 / 100;
    p->current_attack += boost;
    if (p->current_attack > p->base_attack + boost) p->current_attack = p->base_attack + boost;
    boost = 5 + p->base_strength * 15 / 100;
    p->current_strength += boost;
    if (p->current_strength > p->base_strength + boost) p->current_strength = p->base_strength + boost;
    boost = 5 + p->base_defence * 15 / 100;
    p->current_defence += boost;
    if (p->current_defence > p->base_defence + boost) p->current_defence = p->base_defence + boost;
}

/** ranging potion boost: ranged +4 + floor(10% base), cap base + boost (L4).
    ref: OSRS wiki Ranging potion. */
static void col_ranging_boost(Player* p) {
    int boost = 4 + p->base_ranged / 10;
    p->current_ranged += boost;
    if (p->current_ranged > p->base_ranged + boost) p->current_ranged = p->base_ranged + boost;
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

static MeleeStyle col_weapon_melee_style(const uint8_t* loadout) {
    return col_item_melee_style(loadout[GEAR_SLOT_WEAPON]);
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
    the SGS heal/prayer with the wiki minimums (10/5) and the elder maul 35%%
    defence drain, and runs the post-attack item effects (blood fury heal). */
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
    if (item == ITEM_SGS && total > 0) {
        int heal = r.heal > 10 ? r.heal : 10;
        int pray = r.prayer_restore > 5 ? r.prayer_restore : 5;
        s->player.current_hitpoints += heal;
        if (s->player.current_hitpoints > s->player.base_hitpoints)
            s->player.current_hitpoints = s->player.base_hitpoints;
        s->player.current_prayer += pray;
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
    int base_att_roll = osrs_player_att_roll(ls->eff_level, ls->attack_bonus);
    OsrsPreparedAttackEffects prepared = osrs_prepare_attack_effects(
        effects, &s->player.item_effect_state, weapon_item, ls->style,
        OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1,
        base_att_roll, ls->max_hit,
        osrs_target_effect_context_magic(ns->magic_level, ns->magic_att_bonus),
        s->player.current_hitpoints, s->player.base_hitpoints);

    /* scythe of vitur: 3 splats vs 3x3+ targets, 2 vs 2x2, 1 vs 1x1; splat k
       caps at max/2^k, each rolled independently. This is WHY the scythe is
       the speedrun weapon into 5x5 Sol while the fang wins on 1x1 warbanders
       (wiki Budget footnote j). ref: OSRS wiki Scythe of vitur. */
    int num_splats = 1;
    if (weapon_item == ITEM_SCYTHE_OF_VITUR && !col_type_is_hazard_entity(npc->type)) {
        int npc_size = col_npc_effective_size(npc);
        num_splats = npc_size >= 3 ? 3 : npc_size == 2 ? 2 : 1;
    }

    int def_bonus;
    if (ls->style == ATTACK_STYLE_MAGIC) {
        def_bonus = ns->magic_def_bonus;
    } else if (ls->style == ATTACK_STYLE_RANGED) {
        def_bonus = ns->ranged_def_bonus;
    } else {
        def_bonus = col_melee_def_bonus(ns, col_item_melee_style(weapon_item));
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
            splat_damage = encounter_npc_roll_attack(prepared.attack_roll,
                target_def_roll, splat_max, &s->rng_state);
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
        s->player.current_hitpoints += COLO_BREW_HEAL;
        if (s->player.current_hitpoints > s->player.base_hitpoints + COLO_BREW_HEAL)
            s->player.current_hitpoints = s->player.base_hitpoints + COLO_BREW_HEAL;
        s->player.brew_doses--;
        s->player.potion_timer = 3;
        s->player.ate_food_this_tick = 1;
        encounter_brew_drain_stats(&s->player);
        encounter_recompute_loadout_max_hits(s->loadout_stats, COLO_NUM_WEAPON_SETS, &s->player);
        encounter_recompute_loadout_max_hits(s->spec_stats, COLO_NUM_SPEC_WEAPONS, &s->player);
    }

    int pot_act = actions[COLO_HEAD_POTION];
    if (pot_act == COLO_POTION_RESTORE &&
            s->player.restore_doses > 0 && s->player.potion_timer == 0) {
        if (s->full_supplies.restore_kind == COLO_RESTORE_SANFEW) {
            s->player.current_prayer += COLO_SANFEW_PRAYER;
            col_sanfew_restore_stats(&s->player);
            /* L5: the sanfew drink clears Mantimayhem venom */
            s->player_venom = 0;
            s->player_venom_timer = 0;
        } else {
            s->player.current_prayer += COLO_RESTORE_PRAYER;
            encounter_restore_stats(&s->player);
        }
        if (s->player.current_prayer > s->player.base_prayer)
            s->player.current_prayer = s->player.base_prayer;
        s->player.restore_doses--;
        s->player.potion_timer = 3;
        encounter_recompute_loadout_max_hits(s->loadout_stats, COLO_NUM_WEAPON_SETS, &s->player);
        encounter_recompute_loadout_max_hits(s->spec_stats, COLO_NUM_SPEC_WEAPONS, &s->player);
    } else if (pot_act == COLO_POTION_COMBAT &&
            s->player.combat_potion_doses > 0 && s->player.potion_timer == 0) {
        col_super_combat_boost(&s->player);
        s->player.combat_potion_doses--;
        s->player.potion_timer = 3;
        encounter_recompute_loadout_max_hits(s->loadout_stats, COLO_NUM_WEAPON_SETS, &s->player);
        encounter_recompute_loadout_max_hits(s->spec_stats, COLO_NUM_SPEC_WEAPONS, &s->player);
    } else if (pot_act == COLO_POTION_RANGING &&
            s->player.ranged_potion_doses > 0 && s->player.potion_timer == 0) {
        col_ranging_boost(&s->player);
        s->player.ranged_potion_doses--;
        s->player.potion_timer = 3;
        encounter_recompute_loadout_max_hits(s->loadout_stats, COLO_NUM_WEAPON_SETS, &s->player);
        encounter_recompute_loadout_max_hits(s->spec_stats, COLO_NUM_SPEC_WEAPONS, &s->player);
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
    if (has_new_target)
        osrs_interaction_set(&s->interaction, new_target_slot);

    int has_explicit_move = (actions[COLO_HEAD_MOVE] > 0 || s->player_dest_x >= 0);
    if (!has_new_target && has_explicit_move)
        osrs_interaction_check_interrupt(&s->interaction, OSRS_IACT_MOVE);
    if (osrs_interaction_active(&s->interaction)) {
        int slot = s->interaction.target_slot;
        if (slot < 0 || slot >= COLO_MAX_NPCS || !s->npcs[slot].active)
            osrs_interaction_clear(&s->interaction);
    }

    OsrsPlayerMoveKind move_kind = OSRS_PLAYER_MOVE_NONE;
    int move_act = actions[COLO_HEAD_MOVE];
    if (has_explicit_move && !osrs_interaction_active(&s->interaction)) {
        if (s->player_dest_x >= 0) move_kind = OSRS_PLAYER_MOVE_DESTINATION;
        else if (move_act > 0 && move_act < ENCOUNTER_MOVE_ACTIONS)
            move_kind = OSRS_PLAYER_MOVE_ACTION;
    }
    ColoWalkCtx wc = { s };
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
            .los_blockers = NULL,
            .los_blocker_count = 0,
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
            if (encounter_player_can_attack(s->player.x, s->player.y,
                    npc->x, npc->y, col_npc_effective_size(npc),
                    col_player_attack_range(s), NULL, 0)) {
                col_player_attack_target(s, slot);
            }
        }
    }
}

static void col_tick_player(ColosseumState* s, const int* actions, int can_attack) {
    col_tick_player_ctx(s, col_legacy_context(), actions, can_attack);
}
