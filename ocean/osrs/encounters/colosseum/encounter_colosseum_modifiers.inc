#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_modifiers.inc
 * @brief The mandatory pre-wave handicap draft + all 14 modifier effects.
 *
 * A draft gates every wave start from wave 2 through wave 12 (11 picks per full
 * run; wave 1 carries no modifier). Each offer draws weighted-without-
 * replacement from the eligible pool with owned tier-1/2 modifiers
 * upgrade-biased (COLO_DRAFT_UPGRADE_WEIGHT). Selection is mandatory:
 * no skip exists, the player is frozen and the world paused until the pick,
 * which arms the gated wave spawn. col_modifier_apply_selection activates the
 * chosen option (sets the active_mask bit, bumps the tier) and persists it for
 * the run. Effects split into pull-helpers the combat/boss code queries
 * (col_mod_* accessors) and push-hooks driven from the step loop
 * (col_modifiers_tick + the col_mod_on_* event hooks).
 *
 * A21+B9: totems and bee swarms are 1-HP hazard-entity NPCs the bookkeeping
 * here drives (spawn, movement, heal pulses, respawn timers); the player kills
 * them through the ordinary target/attack path.
 *
 * Tier values come from COLO_MODIFIER_* tables in the model; every modeled
 * assumption is a named constant there. Damage routes through the shared
 * player damage helpers so HP/splat/tracker bookkeeping holds.
 */

/* forward declarations for hooks referenced before their definition. */
static void col_mod_apply_frailty_hp(ColosseumState* s);

/* ----- modifier state accessors -------------------------------------------- */

static inline int col_mod_active(const ColosseumState* s, ColoModifier mod) {
    return (s->modifiers.active_mask & (1u << (unsigned)mod)) ? 1 : 0;
}

/** active tier of a modifier (1..3), or 0 when inactive. */
static inline int col_mod_tier(const ColosseumState* s, ColoModifier mod) {
    return col_mod_active(s, mod) ? s->modifiers.tier[mod] : 0;
}

/* ----- draft offer / selection --------------------------------------------- */

/** is `mod` eligible for the draft gating `next_wave` (0-based)? Excludes
    maxed modifiers, the four pre-boss-only modifiers from the wave-12 draft
    (Mantimayhem per UPD-0424; Reentry modeled on the same logic, D31), and
    Red Flag / Dynamic Duo from every draft into wave 8+ (A16). */
static int col_modifier_eligible(const ColosseumState* s, ColoModifier mod, int next_wave) {
    if (s->modifiers.tier[mod] >= COLO_MODIFIER_MAX_TIER[mod]) return 0;
    if (COLO_MODIFIER_PRE_BOSS_ONLY[mod] && next_wave >= COLO_WAVE_BOSS) return 0;
    if ((mod == COLO_MOD_RED_FLAG || mod == COLO_MOD_DYNAMIC_DUO) &&
        next_wave > COLO_RFDD_LAST_DRAFT_WAVE) return 0;
    return 1;
}

/** A16 MODELED: draw weight for an eligible modifier — owned upgradable
    (tier 1/2 of a tiered modifier) gets COLO_DRAFT_UPGRADE_WEIGHT, else 1. */
static int col_modifier_draft_weight(const ColosseumState* s, ColoModifier mod) {
    int tier = s->modifiers.tier[mod];
    if (col_mod_active(s, mod) && tier >= 1 && tier < COLO_MODIFIER_MAX_TIER[mod])
        return COLO_DRAFT_UPGRADE_WEIGHT;
    return 1;
}

/**
 * Open the mandatory draft gating `next_wave` (0-based): populate
 * draft_options[3] and freeze play until a pick.
 *
 * Draws 3 distinct options weighted-without-replacement from the eligible pool
 * (upgrade bias per col_modifier_draft_weight); offering an active tiered
 * modifier means its next tier. If fewer than 3 are eligible, trailing slots
 * stay -1 (the mask gates them); an empty pool leaves the draft closed and the
 * caller spawns the wave ungated (unreachable in real runs — 11 picks cannot
 * exhaust the 14-modifier pool).
 */
static void col_modifier_open_draft(ColosseumState* s, int next_wave) {
    for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) s->modifiers.draft_options[o] = -1;

    int pool[COLO_NUM_REAL_MODIFIERS];
    int weight[COLO_NUM_REAL_MODIFIERS];
    int pool_n = 0;
    int total_weight = 0;
    for (int m = 0; m < COLO_NUM_REAL_MODIFIERS; m++) {
        if (!col_modifier_eligible(s, (ColoModifier)m, next_wave)) continue;
        pool[pool_n] = m;
        weight[pool_n] = col_modifier_draft_weight(s, (ColoModifier)m);
        total_weight += weight[pool_n];
        pool_n++;
    }

    int offered = 0;
    while (offered < COLO_MODIFIER_DRAFT_OPTIONS && pool_n > 0) {
        int roll = encounter_rand_int(&s->rng_state, total_weight);
        int pick = 0;
        while (roll >= weight[pick]) roll -= weight[pick++];
        s->modifiers.draft_options[offered++] = pool[pick];
        total_weight -= weight[pick];
        pool[pick] = pool[pool_n - 1];      /* swap-remove keeps options distinct */
        weight[pick] = weight[pool_n - 1];
        pool_n--;
    }

    if (offered == 0) return;   /* nothing left to draft: leave the draft closed */
    s->modifiers.draft_pending = 1;
    s->modifiers.draft_gates_spawn = 1;
}

/**
 * Activate the modifier in draft slot `option` (0..2). Sets the active_mask bit
 * and bumps that modifier's tier (capped at its max), closes the draft, and —
 * for the wave-gating drafts — arms the queued wave spawn (next tick; the
 * spawn re-arms the 6-tick ready delay, D26). A Frailty pick re-applies the
 * player HP cut immediately so the bar reflects it.
 */
static void col_modifier_apply_selection(ColosseumState* s, int option) {
    if (!s->modifiers.draft_pending) return;
    if (option < 0 || option >= COLO_MODIFIER_DRAFT_OPTIONS) return;
    int mod = s->modifiers.draft_options[option];
    if (mod < 0 || mod >= COLO_NUM_REAL_MODIFIERS) return;

    s->modifiers.active_mask |= (1u << (unsigned)mod);
    if (s->modifiers.tier[mod] < COLO_MODIFIER_MAX_TIER[mod]) s->modifiers.tier[mod]++;
    s->log.modifiers_picked++;

    s->modifiers.draft_pending = 0;
    s->modifiers.draft_free_movement = 0;
    for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) s->modifiers.draft_options[o] = -1;
    if (s->modifiers.draft_gates_spawn) {
        s->modifiers.draft_gates_spawn = 0;
        /* the pick is t0: spawn resolves on t5 against the pre-move t5 tile. */
        s->wave_spawn_delay = col_wave_entry_delay_ticks(s->wave_spawn_target);
    }

    if ((ColoModifier)mod == COLO_MOD_FRAILTY) col_mod_apply_frailty_hp(s);
}

/* ----- Frailty (player max HP) ---------------------------------------------- */

/** apply Frailty's max-HP cut to the player. base is the maxed 99; the cut scales
    the base hitpoints down, and current HP is clamped to the new max. Called on
    reset, on each wave spawn, and the moment Frailty is drafted. */
static void col_mod_apply_frailty_hp(ColosseumState* s) {
    int tier = col_mod_tier(s, COLO_MOD_FRAILTY);
    int max_hp = 99 - (99 * COLO_FRAILTY_HP_CUT_PCT[tier]) / 100;
    if (max_hp < 1) max_hp = 1;
    s->player.base_hitpoints = max_hp;
    if (s->player.current_hitpoints > max_hp) s->player.current_hitpoints = max_hp;
}

/* ----- Relentless (enemy accuracy / max hit) -------------------------------- */

/** A23: Relentless bypasses 33/66/100% of the player's Defence LEVEL — gear
    defence bonuses stay fully intact. Returns the level the defence roll
    should use. */
static int col_mod_relentless_def_level(const ColosseumState* s, int def_level) {
    int tier = col_mod_tier(s, COLO_MOD_RELENTLESS);
    if (tier == 0) return def_level;
    return (def_level * (100 - COLO_RELENTLESS_DEF_BYPASS_PCT[tier])) / 100;
}

/** Relentless flat max-hit bonus; *force_hit is set at tier III (attacks
    ignore accuracy entirely). */
static int col_mod_relentless_max_hit_bonus(const ColosseumState* s, int* force_hit) {
    int tier = col_mod_tier(s, COLO_MOD_RELENTLESS);
    *force_hit = (tier >= 3) ? 1 : 0;
    return COLO_RELENTLESS_MAX_HIT_BONUS[tier];
}

/* ----- Myopia (player attack range) ----------------------------------------- */

/** Myopia shrinks the player's effective attack reach, floored at 1 tile. */
static int col_mod_myopia_range(const ColosseumState* s, int base_range) {
    int cut = COLO_MYOPIA_RANGE_CUT[col_mod_tier(s, COLO_MOD_MYOPIA)];
    int r = base_range - cut;
    return r < 1 ? 1 : r;
}

/* ----- Blasphemy + Doom (on player damage) ---------------------------------- */

/** hook for every damage instance the player takes from a modifier-aware source.
    Blasphemy drains prayer by a fraction of the damage; Doom adds a stack. Call
    AFTER the damage is applied, with the actual amount dealt (0 is a no-op). */
static void col_mod_on_player_damaged(ColosseumState* s, int damage) {
    if (damage <= 0) return;

    int blas = col_mod_tier(s, COLO_MOD_BLASPHEMY);
    if (blas > 0 && s->player.current_prayer > 0) {
        int drain = (damage * COLO_BLASPHEMY_DRAIN_PCT[blas]) / 100;
        s->player.current_prayer -= drain;
        if (s->player.current_prayer < 0) s->player.current_prayer = 0;
    }

    if (col_mod_active(s, COLO_MOD_DOOM)) s->doom_stacks++;
}

/** Doom kills the player once stacks reach the tier cap. Returns 1 if lethal. */
static int col_mod_doom_lethal(const ColosseumState* s) {
    int tier = col_mod_tier(s, COLO_MOD_DOOM);
    return tier > 0 && s->doom_stacks >= COLO_DOOM_CAP[tier];
}

/* ----- Mantimayhem T2 venom (A25) -------------------------------------------- */

/** envenom the player on an unprotected manticore hit (Mantimayhem >= II).
    A25 standard-OSRS semantics: a fresh application starts the 6-damage /
    30-tick cycle; reapplication while envenomed bumps the NEXT instance +2
    toward the cap instead of resetting. */
static void col_mod_manticore_apply_venom(ColosseumState* s, int hit_success) {
    /* serpentine helm blocks NEW venom while worn (active set only); it does
       not cure an existing stack (wiki). beginner camps it on the melee set. */
    if (osrs_effect_profile_has(col_live_effects(s), OSRS_ITEM_EFFECT_VENOM_IMMUNE))
        return;
    if (col_mod_tier(s, COLO_MOD_MANTIMAYHEM) < 2) return;
    if (!hit_success) return;
    s->player_poison = 0;
    s->player_poison_timer = 0;
    if (s->player_venom == 0) {
        s->player_venom = COLO_VENOM_START;
        s->player_venom_timer = COLO_VENOM_INTERVAL;
    } else {
        s->player_venom += COLO_VENOM_STEP;
        if (s->player_venom > COLO_VENOM_CAP) s->player_venom = COLO_VENOM_CAP;
    }
}

/** tick the player's venom: every COLO_VENOM_INTERVAL ticks deal the current
    venom damage, then escalate it +2 toward the cap (6, 8, 10 .. 20). Venom
    never self-cures and persists across waves. E7: the timer freezes during
    wave gaps because the simulator abstracts gap time. */
static void col_mod_tick_venom(ColosseumState* s) {
    if (s->player_venom <= 0) return;
    if (s->player_venom_timer > 0) s->player_venom_timer--;
    if (s->player_venom_timer > 0) return;
    int dmg = s->player_venom;
    col_damage_player_from(s, dmg, COLO_MANTICORE, COLO_DMG_UNPRAYABLE);
    s->last_hit_by_type = COLO_MANTICORE;
    col_mod_on_player_damaged(s, dmg);
    s->player_venom += COLO_VENOM_STEP;
    if (s->player_venom > COLO_VENOM_CAP) s->player_venom = COLO_VENOM_CAP;
    s->player_venom_timer = COLO_VENOM_INTERVAL;
}

/** apply E8 bee-contact poison at the OSRS severity that starts at 1 damage.
    The serpentine helm immunizes poison as well as venom (one item bit covers
    both, wiki Serpentine_helm), and an active venom supersedes poison — they
    share a single status slot in OSRS, so venom application also clears
    poison and a venomed player cannot be poisoned. */
static void col_mod_apply_bee_poison(ColosseumState* s) {
    if (osrs_effect_profile_has(col_live_effects(s), OSRS_ITEM_EFFECT_VENOM_IMMUNE))
        return;
    if (s->player_venom > 0) return;
    if (s->player_poison <= 0) s->player_poison_timer = COLO_POISON_INTERVAL;
    if (s->player_poison < COLO_POISON_BEE_CONTACT_SEVERITY)
        s->player_poison = COLO_POISON_BEE_CONTACT_SEVERITY;
}

/** tick standard poison during live gameplay only. E8 keeps poison out of the
    observation vector for the current contract. */
static void col_mod_tick_poison(ColosseumState* s) {
    if (s->player_poison <= 0) return;
    if (s->player_poison_timer > 0) s->player_poison_timer--;
    if (s->player_poison_timer > 0) return;
    int dmg = (s->player_poison + 4) / 5;
    col_damage_player_from(s, dmg, COLO_BEE_SWARM, COLO_DMG_UNPRAYABLE);
    if (dmg > 0) {
        s->last_hit_by_type = COLO_BEE_SWARM;
        col_mod_on_player_damaged(s, dmg);
    }
    s->player_poison--;
    if (s->player_poison <= 0) {
        s->player_poison = 0;
        s->player_poison_timer = 0;
    } else {
        s->player_poison_timer = COLO_POISON_INTERVAL;
    }
}

/* ----- hazard-entity NPC plumbing (totems + bees) ---------------------------- */

/** spawn a 1-HP hazard NPC flush with the owner footprint's east edge, walking
    the adjacent ring for a static-unblocked tile inside the boss clamp when
    active. Hazards carry no collision, so NPC overlap is acceptable. */
static int col_spawn_hazard_npc_beside(
    ColosseumState* s, ColoNpcType type, const ColoNPC* owner
) {
    int size = col_npc_effective_size(owner);
    for (int ring = 0; ring < 2; ring++) {
        int lo_x = owner->x - 1 - ring, hi_x = owner->x + size + ring;
        int lo_y = owner->y - 1 - ring, hi_y = owner->y + size + ring;
        /* east column first (the visual convention), then the rest of the shell. */
        for (int pass = 0; pass < 2; pass++) {
            for (int y = lo_y; y <= hi_y; y++) {
                for (int x = lo_x; x <= hi_x; x++) {
                    int on_shell = (x == lo_x || x == hi_x || y == lo_y || y == hi_y);
                    if (!on_shell) continue;
                    int is_east = (x == hi_x);
                    if ((pass == 0) != is_east) continue;
                    if (col_static_blocked(x, y)) continue;
                    if (col_sol_clamp_active(s) && !col_in_boss_arena(s, x, y)) continue;
                    return col_spawn_npc_at(s, type, x, y);
                }
            }
        }
    }
    /* no static-free tile in the 2-ring shell (owner cornered against walls/
       pillars or outside the boss clamp): a legitimate crowded state, not a
       defect. Return -1 so the caller defers and retries rather than aborting. */
    return -1;
}

/** owner-keyed totem bookkeeping; asserts the slot domain. */
static ColoTotem* col_totem_for_owner(ColosseumState* s, int owner_slot) {
    assert(owner_slot >= 0 && owner_slot < COLO_MAX_TOTEMS);
    return &s->totems[owner_slot];
}

/** A21: hazard-entity death bookkeeping, called from col_apply_npc_death for a
    dying totem/bee NPC: arm the matching respawn timer (totem 200t keyed to its
    owner, bee 50t). */
static void col_hazard_on_npc_death(ColosseumState* s, int npc_slot) {
    const ColoNPC* npc = &s->npcs[npc_slot];
    if (npc->type == COLO_HEALING_TOTEM) {
        for (int o = 0; o < COLO_MAX_TOTEMS; o++) {
            ColoTotem* tot = &s->totems[o];
            if (tot->phase != COLO_HAZARD_ALIVE || tot->npc_slot != npc_slot) continue;
            tot->phase = COLO_HAZARD_RESPAWNING;
            tot->npc_slot = -1;
            tot->respawn_timer = COLO_TOTEM_RESPAWN_TICKS;
            return;
        }
        return;   /* lab-spawned ownerless totem: dies for good */
    }
    if (npc->type == COLO_BEE_SWARM) {
        for (int b = 0; b < COLO_MAX_BEE_SWARMS; b++) {
            ColoBeeSwarm* bee = &s->bees[b];
            if (bee->phase != COLO_HAZARD_ALIVE || bee->npc_slot != npc_slot) continue;
            bee->phase = COLO_HAZARD_RESPAWNING;
            bee->npc_slot = -1;
            bee->respawn_timer = COLO_BEE_RESPAWN_TICKS;
            return;
        }
        return;   /* lab-spawned unmanaged bee: dies for good */
    }
}

/** A21: a real NPC died — its totem despawns with it (live totem removed, a
    pending respawn cancelled). */
static void col_mod_on_owner_death(ColosseumState* s, int owner_slot) {
    ColoTotem* tot = col_totem_for_owner(s, owner_slot);
    if (tot->phase == COLO_HAZARD_ALIVE && tot->npc_slot >= 0 &&
        s->npcs[tot->npc_slot].active &&
        s->npcs[tot->npc_slot].type == COLO_HEALING_TOTEM)
        col_deactivate_npc(s, tot->npc_slot);
    *tot = (ColoTotem){ .phase = COLO_HAZARD_NONE };
}

/* ----- Totemic (50%-HP healing totem, A21+B5+D22) ----------------------------- */

/** spawn a totem NPC for `owner_slot` and bind the bookkeeping. */
static void col_totem_spawn_for_owner(ColosseumState* s, int owner_slot) {
    ColoTotem* tot = col_totem_for_owner(s, owner_slot);
    int slot = col_spawn_hazard_npc_beside(s, COLO_HEALING_TOTEM, &s->npcs[owner_slot]);
    if (slot < 0) {
        /* no free tile beside the owner this tick; defer one tick via the
           respawn path (the owner may move or a neighbour may die). */
        tot->phase = COLO_HAZARD_RESPAWNING;
        tot->npc_slot = -1;
        tot->respawn_timer = 1;
        return;
    }
    tot->phase = COLO_HAZARD_ALIVE;
    tot->npc_slot = slot;
    tot->heal_timer = COLO_TOTEM_HEAL_INTERVAL;
}

/** hook from the NPC-damage path: an NPC (Sol included, B5) at or below 50% HP
    with no totem fielded or pending gets one. */
static void col_mod_on_npc_hp_changed(ColosseumState* s, int npc_slot) {
    if (!col_mod_active(s, COLO_MOD_TOTEMIC)) return;
    const ColoNPC* npc = &s->npcs[npc_slot];
    if (!npc->active || npc->hp <= 0 || npc->max_hp <= 0) return;
    if (col_type_is_hazard_entity(npc->type)) return;
    if (npc->hp * 2 > npc->max_hp) return;   /* not yet at <=50% */
    if (col_totem_for_owner(s, npc_slot)->phase != COLO_HAZARD_NONE) return;
    col_totem_spawn_for_owner(s, npc_slot);
}

/**
 * Tick all totems. ALIVE: pulse every 7 ticks — heal the owner 30% of max HP
 * while the owner sits at or below 50% (the confirmed trigger, D22); Sol
 * instead takes the flat 75 every pulse until the totem is destroyed (B5).
 * RESPAWNING: count down 200 ticks, then respawn beside the (still live)
 * owner. An owner that died through any path despawns its totem; a totem NPC
 * removed externally (lab) clears its bookkeeping without a respawn.
 */
static void col_mod_tick_totems(ColosseumState* s) {
    for (int o = 0; o < COLO_MAX_TOTEMS; o++) {
        ColoTotem* tot = &s->totems[o];
        if (tot->phase == COLO_HAZARD_NONE) continue;

        ColoNPC* owner = &s->npcs[o];
        if (!owner->active || owner->hp <= 0 ||
            col_type_is_hazard_entity(owner->type)) {
            col_mod_on_owner_death(s, o);
            continue;
        }

        if (tot->phase == COLO_HAZARD_RESPAWNING) {
            if (tot->respawn_timer > 0) tot->respawn_timer--;
            if (tot->respawn_timer == 0) col_totem_spawn_for_owner(s, o);
            continue;
        }

        ColoNPC* totem_npc = (tot->npc_slot >= 0) ? &s->npcs[tot->npc_slot] : NULL;
        if (!totem_npc || !totem_npc->active || totem_npc->type != COLO_HEALING_TOTEM) {
            *tot = (ColoTotem){ .phase = COLO_HAZARD_NONE };   /* removed outside the death path: no respawn */
            continue;
        }

        if (tot->heal_timer > 0) tot->heal_timer--;
        if (tot->heal_timer > 0) continue;
        tot->heal_timer = COLO_TOTEM_HEAL_INTERVAL;
        if (owner->type == COLO_SOL_HEREDIT) {
            owner->hp += COLO_TOTEM_SOL_HEAL;
        } else {
            if (owner->hp * 2 > owner->max_hp) continue;   /* pulse gated at <=50% */
            owner->hp += (owner->max_hp * COLO_TOTEM_HEAL_PCT) / 100;
        }
        if (owner->hp > owner->max_hp) owner->hp = owner->max_hp;
    }
}

/* ----- molten pools (Reentry / Volatility T3 on waves 1-11) ----------------- */

#define COLO_REENTRY_MOLTEN_MAX_HIT 15
#define COLO_REENTRY_MOLTEN_BURN_INTERVAL 2

typedef struct {
    int x;
    int y;
} ColoMoltenPoolTile;

static int col_mod_molten_pool_covered(const ColosseumState* s, int x, int y) {
    for (int i = 0; i < s->molten_count; i++)
        if (s->molten_x[i] == x && s->molten_y[i] == y) return 1;
    return 0;
}

static int col_mod_molten_pool_available(const ColosseumState* s, int x, int y) {
    return !col_static_blocked(x, y) && !col_mod_molten_pool_covered(s, x, y);
}

static ColoMoltenPoolTile col_mod_molten_pool_random_fallback(ColosseumState* s) {
    ColoMoltenPoolTile candidates[COLO_ARENA_TILE_COUNT];
    int count = 0;
    for (int x = COLO_ARENA_MIN_X; x <= COLO_ARENA_MAX_X; x++) {
        for (int y = COLO_ARENA_MIN_Y; y <= COLO_ARENA_MAX_Y; y++) {
            if (!col_mod_molten_pool_available(s, x, y)) continue;
            candidates[count++] = (ColoMoltenPoolTile){ .x = x, .y = y };
        }
    }
    assert(count > 0);
    return candidates[encounter_rand_int(&s->rng_state, count)];
}

static ColoMoltenPoolTile col_mod_molten_pool_tile_or_fallback(
    ColosseumState* s,
    int x,
    int y
) {
    if (col_mod_molten_pool_available(s, x, y))
        return (ColoMoltenPoolTile){ .x = x, .y = y };
    return col_mod_molten_pool_random_fallback(s);
}

/** add a molten pool at (x,y) of the given ColoMoltenPoolKind. Overflow
    recycles a random slot so the list never asserts. */
static void col_mod_add_molten_pool(
    ColosseumState* s,
    int x,
    int y,
    ColoMoltenPoolKind kind,
    ColoMoltenPoolLifetime lifetime
) {
    if (col_static_blocked(x, y)) return;
    for (int i = 0; i < s->molten_count; i++) {
        if (s->molten_x[i] != x || s->molten_y[i] != y) continue;
        s->molten_kind[i] = kind;
        if (lifetime == COLO_POOL_PERMANENT) s->molten_lifetime[i] = lifetime;
        s->molten_burn_timer[i] = 0;
        return;
    }
    int slot;
    if (s->molten_count < COLO_SOL_HAZARD_TILES_MAX) slot = s->molten_count++;
    else slot = encounter_rand_int(&s->rng_state, COLO_SOL_HAZARD_TILES_MAX);
    s->molten_x[slot] = x;
    s->molten_y[slot] = y;
    s->molten_kind[slot] = kind;
    s->molten_lifetime[slot] = lifetime;
    s->molten_burn_timer[slot] = 0;
}

/** Clear only temporary pools at wave end; permanent Reentry pools survive. */
static void col_mod_clear_wave_end_pools(ColosseumState* s) {
    int out = 0;
    for (int i = 0; i < s->molten_count; i++) {
        if (s->molten_lifetime[i] != COLO_POOL_PERMANENT) continue;
        if (out != i) {
            s->molten_x[out] = s->molten_x[i];
            s->molten_y[out] = s->molten_y[i];
            s->molten_kind[out] = s->molten_kind[i];
            s->molten_lifetime[out] = s->molten_lifetime[i];
            s->molten_burn_timer[out] = s->molten_burn_timer[i];
        }
        out++;
    }
    s->molten_count = out;
}

static int col_mod_molten_pool_damage(ColosseumState* s, ColoMoltenPoolKind kind) {
    if (kind == COLO_POOL_REENTRY)
        return 1 + encounter_rand_int(&s->rng_state, COLO_REENTRY_MOLTEN_MAX_HIT);
    return COLO_MOLTEN_SAND_MIN_HIT
        + encounter_rand_int(&s->rng_state, COLO_MOLTEN_SAND_RAND);
}

/** burn the player standing on any pool. Reentry burns every other tick for
    1-15 typeless; Volatility uses the weaker per-tick molten sand band. */
static void col_mod_tick_molten_pools(ColosseumState* s) {
    int burned = 0;
    for (int i = 0; i < s->molten_count; i++) {
        int reentry = s->molten_kind[i] == COLO_POOL_REENTRY;
        int player_on_pool = s->player.x == s->molten_x[i] && s->player.y == s->molten_y[i];
        if (!burned && player_on_pool && (!reentry || s->molten_burn_timer[i] == 0)) {
            int dmg = col_mod_molten_pool_damage(s, (ColoMoltenPoolKind)s->molten_kind[i]);
            if (reentry) s->molten_burn_timer[i] = COLO_REENTRY_MOLTEN_BURN_INTERVAL - 1;
            col_damage_player_from(s, dmg, COLO_JAVELIN_COLOSSUS, COLO_DMG_UNPRAYABLE);
            s->last_hit_by_type = s->last_hit_by_type < 0 ? COLO_JAVELIN_COLOSSUS : s->last_hit_by_type;
            col_mod_on_player_damaged(s, dmg);
            burned = 1;
            continue;
        }
        if (reentry && s->molten_burn_timer[i] > 0)
            s->molten_burn_timer[i]--;
    }
}

/** Reentry hook: when a javelin skyfall lands, leave stronger molten. T1 drops
    a temporary targeted pool. T2 adds southwest and makes both permanent. T3
    adds west and makes all permanent. Blocked or covered side tiles fall back to
    a random in-arena tile. */
static void col_mod_reentry_on_skyfall(ColosseumState* s, int tile_x, int tile_y) {
    int tier = col_mod_tier(s, COLO_MOD_REENTRY);
    if (tier == 0) return;
    ColoMoltenPoolLifetime lifetime = tier >= 2 ? COLO_POOL_PERMANENT : COLO_POOL_TEMPORARY;
    col_mod_add_molten_pool(s, tile_x, tile_y, COLO_POOL_REENTRY, lifetime);
    if (tier >= 2) {
        ColoMoltenPoolTile sw = col_mod_molten_pool_tile_or_fallback(
            s, tile_x - 1, tile_y - 1);
        col_mod_add_molten_pool(s, sw.x, sw.y, COLO_POOL_REENTRY, COLO_POOL_PERMANENT);
    }
    if (tier >= 3) {
        ColoMoltenPoolTile west = col_mod_molten_pool_tile_or_fallback(
            s, tile_x - 1, tile_y);
        col_mod_add_molten_pool(s, west.x, west.y, COLO_POOL_REENTRY, COLO_POOL_PERMANENT);
    }
}

/* ----- Volatility (death explosion) ----------------------------------------- */

/** Volatility blast reach by active tier. */
static int col_volatility_reach_for_tier(int tier) {
    if (tier <= 0) return 0;
    return tier >= 2 ? 2 : 1;
}

/** true when the player is inside the Volatility blast footprint. */
static int col_volatility_player_in_blast(
    const ColosseumState* s,
    int x,
    int y,
    int size,
    int tier
) {
    int reach = col_volatility_reach_for_tier(tier);
    if (reach <= 0) return 0;
    int span = size + 2 * reach;
    return encounter_entity_footprints_overlap(
        x - reach, y - reach, span, s->player.x, s->player.y, 1);
}

/** Volatility hook on NPC death. */
static void col_mod_volatility_on_death(ColosseumState* s, int x, int y, int size) {
    int tier = col_mod_tier(s, COLO_MOD_VOLATILITY);
    if (tier == 0) return;
    if (col_volatility_player_in_blast(s, x, y, size, tier)) {
        int vol_dmg = col_scale_incoming_damage(s, COLO_VOLATILITY_MAX_HIT);
        encounter_damage_player(&s->player, vol_dmg, &s->tick_scratch.damage_received);
        /* environmental typeless damage (no NPC attribution): classify for the
           land-time forensics channel split by hand since it bypasses the sink. */
        s->tick_scratch.landed_unprayable_damage += (float)vol_dmg;
        col_mod_on_player_damaged(s, vol_dmg);
    }
    if (tier >= 3)
        col_mod_add_molten_pool(
            s, x + size / 2, y + size / 2, COLO_POOL_VOLATILITY, COLO_POOL_TEMPORARY);
}

/* ----- Bees! (roaming 1-HP poison swarms, B9) -------------------------------- */

/** clamp a bee swarm's SW tile to the full arena, not the wave-12 boss box. */
static void col_clamp_bee_swarm_to_arena(ColoNPC* npc) {
    int size = col_npc_effective_size(npc);
    int max_x = COLO_ARENA_MAX_X - size + 1;
    int max_y = COLO_ARENA_MAX_Y - size + 1;
    if (npc->x < COLO_ARENA_MIN_X) npc->x = COLO_ARENA_MIN_X;
    if (npc->y < COLO_ARENA_MIN_Y) npc->y = COLO_ARENA_MIN_Y;
    if (npc->x > max_x) npc->x = max_x;
    if (npc->y > max_y) npc->y = max_y;
}

/** E8: spawn a 2x2 bee NPC on a random spawn-anchor tile. */
static void col_bee_spawn(ColosseumState* s, int b) {
    int pos = encounter_rand_int(&s->rng_state, COLO_NUM_SPAWN_ANCHORS);
    int slot = col_spawn_npc_at(s, COLO_BEE_SWARM,
        COLO_SPAWN_ANCHORS[pos][0], COLO_SPAWN_ANCHORS[pos][1]);
    ColoNPC* npc = &s->npcs[slot];
    col_clamp_bee_swarm_to_arena(npc);
    npc->target_x = npc->x;
    npc->target_y = npc->y;
    s->bees[b] = (ColoBeeSwarm){
        .phase = COLO_HAZARD_ALIVE,
        .npc_slot = slot,
        .move_timer = COLO_BEE_MOVE_INTERVAL,
    };
}

/** sync the fielded swarm count to the Bees! tier (1/2/3): spawn missing
    swarms, despawn extras; live swarms and pending respawns carry across
    waves. Called at each wave spawn. */
static void col_mod_sync_bees(ColosseumState* s) {
    int want = col_mod_tier(s, COLO_MOD_BEES);
    for (int b = 0; b < COLO_MAX_BEE_SWARMS; b++) {
        ColoBeeSwarm* bee = &s->bees[b];
        int npc_live = bee->phase == COLO_HAZARD_ALIVE && bee->npc_slot >= 0 &&
            s->npcs[bee->npc_slot].active &&
            s->npcs[bee->npc_slot].type == COLO_BEE_SWARM;
        if (b < want) {
            if (npc_live || bee->phase == COLO_HAZARD_RESPAWNING) continue;
            col_bee_spawn(s, b);
        } else {
            if (npc_live) col_deactivate_npc(s, bee->npc_slot);
            *bee = (ColoBeeSwarm){ .phase = COLO_HAZARD_NONE };
        }
    }
}

/** true when the player's tile overlaps any tile in the swarm's 2x2 footprint. */
static int col_bee_swarm_contacts_player(const ColosseumState* s, const ColoNPC* npc) {
    int size = col_npc_effective_size(npc);
    return encounter_entity_footprints_overlap(npc->x, npc->y, size, s->player.x, s->player.y, 1);
}

/** compute the bee swarm's next anchor tile without mutating state. */
static void col_bee_swarm_next_tile_toward_player(
    const ColosseumState* s,
    const ColoNPC* npc,
    int* out_x,
    int* out_y
) {
    if (!s || !npc || !out_x || !out_y) abort();
    int size = col_npc_effective_size(npc);
    int x = npc->x;
    int y = npc->y;
    int hi_x = x + size - 1;
    int hi_y = y + size - 1;
    if (s->player.x < x) x--;
    else if (s->player.x > hi_x) x++;
    if (s->player.y < y) y--;
    else if (s->player.y > hi_y) y++;
    int max_x = COLO_ARENA_MAX_X - size + 1;
    int max_y = COLO_ARENA_MAX_Y - size + 1;
    if (x < COLO_ARENA_MIN_X) x = COLO_ARENA_MIN_X;
    if (y < COLO_ARENA_MIN_Y) y = COLO_ARENA_MIN_Y;
    if (x > max_x) x = max_x;
    if (y > max_y) y = max_y;
    *out_x = x;
    *out_y = y;
}

/** unblockable bee-contact damage roll (15-20). The serpentine helm immunizes
    poison and venom, which on the dedicated Bee_Swarm wiki page extends to the
    swarm's contact damage (full immunity). The colosseum carries no standalone
    antipoison potion, so the wiki "halved with antipoison active" reduction has
    no live signal here beyond that full-immunity helm bit. */
static int col_bee_swarm_contact_damage(ColosseumState* s) {
    if (osrs_effect_profile_has(col_live_effects(s), OSRS_ITEM_EFFECT_VENOM_IMMUNE))
        return 0;
    int span = COLO_BEE_MAX_DAMAGE - COLO_BEE_MIN_DAMAGE + 1;
    return COLO_BEE_MIN_DAMAGE + encounter_rand_int(&s->rng_state, span);
}

/** move the 2x2 bee footprint one tile toward the player's tile when the player
    is outside the current x or y span. */
static void col_bee_swarm_step_toward_player(ColosseumState* s, ColoNPC* npc) {
    int next_x, next_y;
    col_bee_swarm_next_tile_toward_player(s, npc, &next_x, &next_y);
    npc->x = next_x;
    npc->y = next_y;
    npc->target_x = s->player.x;
    npc->target_y = s->player.y;
}

/** tick bee swarms: a live E8 2x2 swarm steps one tile toward the player every
    12 ticks, ignoring walls and the wave-12 boss-box clamp, and contact with
    any footprint tile deals 15-20 unblockable damage plus standard poison.
    A killed swarm respawns 50 ticks later on a fresh anchor. */
static void col_mod_tick_bees(ColosseumState* s) {
    for (int b = 0; b < COLO_MAX_BEE_SWARMS; b++) {
        ColoBeeSwarm* bee = &s->bees[b];
        if (bee->phase == COLO_HAZARD_NONE) continue;

        if (bee->phase == COLO_HAZARD_RESPAWNING) {
            if (bee->respawn_timer > 0) bee->respawn_timer--;
            if (bee->respawn_timer == 0) col_bee_spawn(s, b);
            continue;
        }

        ColoNPC* npc = (bee->npc_slot >= 0) ? &s->npcs[bee->npc_slot] : NULL;
        if (!npc || !npc->active || npc->type != COLO_BEE_SWARM) {
            *bee = (ColoBeeSwarm){ .phase = COLO_HAZARD_NONE };   /* removed outside the death path: no respawn */
            continue;
        }

        if (bee->move_timer > 0) bee->move_timer--;
        if (bee->move_timer == 0) {
            col_bee_swarm_step_toward_player(s, npc);
            bee->move_timer = COLO_BEE_MOVE_INTERVAL;
        }

        if (col_bee_swarm_contacts_player(s, npc)) {
            col_mod_apply_bee_poison(s);
            int dmg = col_bee_swarm_contact_damage(s);
            col_damage_player_from(s, dmg, COLO_BEE_SWARM, COLO_DMG_UNPRAYABLE);
            if (dmg > 0) {
                s->last_hit_by_type = COLO_BEE_SWARM;
                col_mod_on_player_damaged(s, dmg);
            }
        }
    }
}

/* ----- Solarflare (per-pillar orbs) ---------------------------------------- */

/** write the Solarflare tile for one pillar and one shared ring step. */
static void col_solarflare_tile(int pillar_index, int step, int* x, int* y) {
    if (pillar_index < 0 || pillar_index >= COLO_NUM_PILLARS ||
            step < 0 || step >= COLO_SOLARFLARE_RING_STEPS || !x || !y) {
        fprintf(stderr, "invalid solarflare tile request\n");
        abort();
    }

    int px = COLO_PILLARS[pillar_index][0];
    int py = COLO_PILLARS[pillar_index][1];
    if (step < 4) {
        *x = px - 1 + step;
        *y = py - 1;
    } else if (step < 8) {
        *x = px + COLO_PILLAR_SIZE;
        *y = py - 1 + (step - 4);
    } else if (step < 12) {
        *x = px + COLO_PILLAR_SIZE - (step - 8);
        *y = py + COLO_PILLAR_SIZE;
    } else {
        *x = px - 1;
        *y = py + COLO_PILLAR_SIZE - (step - 12);
    }
}

/** return whether a Solarflare ring step is a pillar-ring corner. */
static int col_solarflare_step_is_corner(int step) {
    if (step < 0 || step >= COLO_SOLARFLARE_RING_STEPS) {
        fprintf(stderr, "invalid solarflare step\n");
        abort();
    }
    return (step & 3) == 0;
}

/** start or refresh all Solarflare orbs at ring index 0 when active. */
static void col_mod_sync_solarflare(ColosseumState* s) {
    if (!col_mod_active(s, COLO_MOD_SOLARFLARE)) {
        s->solarflare = (ColoSolarflareOrb){
            .active = 0,
            .step = 0,
            .move_timer = 0,
            .pause_timer = 0,
        };
        return;
    }
    s->solarflare = (ColoSolarflareOrb){
        .active = 1,
        .step = 0,
        .move_timer = 0,
        .pause_timer = 0,
    };
}

/** orb damage by tier (D21 modeled values). */
static int col_mod_solarflare_max_hit(int tier) {
    if (tier >= 3) return COLO_SOLARFLARE_MAX_HIT_T3;
    if (tier == 2) return COLO_SOLARFLARE_MAX_HIT_T2;
    return COLO_SOLARFLARE_MAX_HIT_T1;
}

/** apply contact damage for every Solarflare orb on the player tile. */
static void col_mod_apply_solarflare_contact(ColosseumState* s, int tier) {
    for (int p = 0; p < COLO_NUM_PILLARS; p++) {
        int x, y;
        col_solarflare_tile(p, s->solarflare.step, &x, &y);
        if (x != s->player.x || y != s->player.y) continue;
        int dmg = encounter_rand_int(&s->rng_state, col_mod_solarflare_max_hit(tier) + 1);
        dmg = col_scale_incoming_damage(s, dmg);
        encounter_damage_player(&s->player, dmg, &s->tick_scratch.damage_received);
        /* environmental typeless damage (no NPC attribution): classify for the
           land-time forensics channel split by hand since it bypasses the sink. */
        s->tick_scratch.landed_unprayable_damage += (float)dmg;
        if (dmg > 0) col_mod_on_player_damaged(s, dmg);
        if (tier >= 3) s->player.prayer = PRAYER_NONE;
    }
}

/** Advance the shared Solarflare cadence by exactly one tick for the given tier.
    Pure in the orb state: the sole source of truth for ring-step progression,
    shared by the live tick and the step-out forecast projection so their
    cadence can never drift. Tier 2 runs continuous (no corner pause). */
static void col_solarflare_advance_orb(ColoSolarflareOrb* orb, int tier) {
    if (orb->pause_timer > 0) {
        orb->pause_timer--;
        return;
    }
    if (orb->move_timer > 0) orb->move_timer--;
    if (orb->move_timer != 0) return;
    int next_step = orb->step + 1;
    if (next_step == COLO_SOLARFLARE_RING_STEPS) next_step = 0;
    orb->step = next_step;
    orb->move_timer = (tier >= 3)
        ? COLO_SOLARFLARE_MOVE_INTERVAL_T3 : COLO_SOLARFLARE_MOVE_INTERVAL;
    if (col_solarflare_step_is_corner(orb->step)) {
        if (tier == 1) orb->pause_timer = COLO_SOLARFLARE_CORNER_PAUSE;
        if (tier >= 3) orb->pause_timer = COLO_SOLARFLARE_CORNER_PAUSE_T3;
    }
}

/** tick the shared Solarflare cadence and apply per-pillar orb contact damage. */
static void col_mod_tick_solarflare(ColosseumState* s) {
    if (!s->solarflare.active) return;
    int tier = col_mod_tier(s, COLO_MOD_SOLARFLARE);
    if (tier == 0) {
        s->solarflare = (ColoSolarflareOrb){
            .active = 0,
            .step = 0,
            .move_timer = 0,
            .pause_timer = 0,
        };
        return;
    }

    col_solarflare_advance_orb(&s->solarflare, tier);
    col_mod_apply_solarflare_contact(s, tier);
}

/* ----- per-tick modifier driver --------------------------------------------- */

/**
 * Run all per-tick modifier hazards. Called from col_step after combat
 * resolves, paused during wave gaps and open drafts. Order: the hazards that
 * damage the player (bees, solarflare, molten pools, venom), then totems
 * (heal NPCs). A lethal Doom stack is surfaced via col_mod_doom_lethal and
 * checked by the step death gate.
 */
static void col_modifiers_tick(ColosseumState* s) {
#ifdef COLO_PROFILE_ENABLED
    int col_prof_enabled = COLO_PROFILE_ENABLED();
    double col_prof_t0 = col_prof_enabled ? COLO_PROFILE_NOW_MS() : 0.0;
#endif
    col_mod_tick_bees(s);
    col_mod_tick_solarflare(s);
    col_mod_tick_molten_pools(s);
    col_mod_tick_venom(s);
    col_mod_tick_poison(s);
    col_mod_tick_totems(s);
#ifdef COLO_PROFILE_ENABLED
    COLO_PROFILE_MARK(COLO_PROF_STEP_MODIFIERS_HAZARDS);
#endif
}

/** per-wave-spawn modifier sync: reset Doom stacks, drop E10 wave-end pools,
    re-apply Frailty, sync the bee swarms and the Solarflare orb. Called from
    col_spawn_wave after NPCs spawn. */
static void col_modifiers_on_wave_spawn(ColosseumState* s) {
    s->doom_stacks = 0;
    col_mod_clear_wave_end_pools(s);
    col_mod_apply_frailty_hp(s);
    col_mod_sync_bees(s);
    col_mod_sync_solarflare(s);
}

/** sum of active modifier tiers, for the run-difficulty log proxy. */
static int col_modifier_active_tier_sum(const ColosseumState* s) {
    int sum = 0;
    for (int m = 0; m < COLO_NUM_REAL_MODIFIERS; m++)
        if (col_mod_active(s, (ColoModifier)m)) sum += s->modifiers.tier[m];
    return sum;
}
