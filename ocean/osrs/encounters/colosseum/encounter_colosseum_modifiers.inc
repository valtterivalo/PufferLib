#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_modifiers.inc
 * @brief The between-wave handicap draft + all 14 modifier effects.
 *
 * The draft is offered during the inter-wave gap after clearing waves
 * 1..COLO_MODIFIER_LAST_WAVE: col_modifier_offer_draft picks 3 distinct eligible
 * modifiers (a new one or the next tier of an active tiered one), respecting the
 * "not offered after wave 11" flags. col_modifier_apply_selection activates the
 * chosen option (sets the active_mask bit, bumps the tier) and persists it for the
 * run. Effects split into pull-helpers the combat/boss code queries
 * (col_mod_* accessors) and push-hooks driven from the step loop
 * (col_modifiers_tick + the col_mod_on_* event hooks).
 *
 * Tier values come from COLO_MODIFIER_* tables in the model; every UNCONFIRMED
 * assumption is a named constant there. Damage routes through the shared
 * encounter_damage_player so HP/splat/tracker bookkeeping holds.
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

/** is `mod` eligible to offer after clearing `cleared_wave` (0-based)? Excludes
    maxed tiered modifiers and pre-boss-only modifiers once past wave 11. */
static int col_modifier_eligible(const ColosseumState* s, ColoModifier mod, int cleared_wave) {
    if (s->modifiers.tier[mod] >= COLO_MODIFIER_MAX_TIER[mod]) return 0;
    int next_wave = cleared_wave + 1;
    if (COLO_MODIFIER_PRE_BOSS_ONLY[mod] && next_wave >= COLO_WAVE_BOSS) return 0;
    return 1;
}

/**
 * Populate draft_options[3] with 3 distinct eligible modifiers and open the draft.
 *
 * Called once during the inter-wave gap after clearing waves 1..LAST_WAVE. Offering
 * an already-active tiered modifier advances its next tier; a new one starts at
 * tier 1. If fewer than 3 are eligible, trailing slots stay -1 (the mask gates them).
 */
static void col_modifier_offer_draft(ColosseumState* s, int cleared_wave) {
    int pool[COLO_NUM_REAL_MODIFIERS];
    int pool_n = 0;
    for (int m = 0; m < COLO_NUM_REAL_MODIFIERS; m++) {
        if (col_modifier_eligible(s, (ColoModifier)m, cleared_wave)) pool[pool_n++] = m;
    }

    for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) s->modifiers.draft_options[o] = -1;
    int offered = 0;
    while (offered < COLO_MODIFIER_DRAFT_OPTIONS && pool_n > 0) {
        int pick = encounter_rand_int(&s->rng_state, pool_n);
        s->modifiers.draft_options[offered++] = pool[pick];
        pool[pick] = pool[--pool_n];   /* swap-remove keeps options distinct */
    }

    if (offered == 0) return;   /* nothing left to draft: leave the draft closed */
    s->modifiers.draft_pending = 1;
    s->modifiers.draft_timer = s->config_modifier_draft_window;
}

/**
 * Activate the modifier in draft slot `option` (0..2). Sets the active_mask bit and
 * bumps that modifier's tier (capped at its max), then closes the draft. A
 * Frailty pick re-applies the player HP cut immediately so the bar reflects it.
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
    s->modifiers.draft_timer = 0;
    for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) s->modifiers.draft_options[o] = -1;

    if ((ColoModifier)mod == COLO_MOD_FRAILTY) col_mod_apply_frailty_hp(s);
}

/** count down an open draft; auto-close after the window so play resumes even if
    the policy never selects (window <= 0 keeps it open until a pick). */
static void col_modifier_tick_draft(ColosseumState* s) {
    if (!s->modifiers.draft_pending) return;
    if (s->config_modifier_draft_window <= 0) return;
    if (s->modifiers.draft_timer > 0) s->modifiers.draft_timer--;
    if (s->modifiers.draft_timer == 0) {
        s->modifiers.draft_pending = 0;
        for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) s->modifiers.draft_options[o] = -1;
    }
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

/** Relentless adjusts an NPC's defence-bypassed roll and flat max-hit bonus.
    Returns the max-hit bonus to add; *force_hit is set to 1 at tier III (ignore
    accuracy entirely). *def_roll is scaled down by the bypass fraction. */
static int col_mod_relentless_apply(const ColosseumState* s, int* def_roll, int* force_hit) {
    int tier = col_mod_tier(s, COLO_MOD_RELENTLESS);
    if (tier == 0) { *force_hit = 0; return 0; }
    int bypass = COLO_RELENTLESS_DEF_BYPASS_PCT[tier];
    *def_roll = (*def_roll * (100 - bypass)) / 100;
    *force_hit = (tier >= 3) ? 1 : 0;   /* T3 fully ignores accuracy checks */
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

/* ----- Mantimayhem T2 venom ------------------------------------------------- */

/** apply venom to the player on an unprotected manticore hit (Mantimayhem >= II).
    Resets the venom to full so repeated unprotected hits keep it stacked up. */
static void col_mod_manticore_apply_venom(ColosseumState* s, int hit_success) {
    if (col_mod_tier(s, COLO_MOD_MANTIMAYHEM) < 2) return;
    if (!hit_success) return;
    s->player_venom = COLO_VENOM_START;
    if (s->player_venom_timer == 0) s->player_venom_timer = COLO_VENOM_INTERVAL;
}

/** tick the player's venom: deal decaying damage every COLO_VENOM_INTERVAL ticks. */
static void col_mod_tick_venom(ColosseumState* s) {
    if (s->player_venom <= 0) return;
    if (s->player_venom_timer > 0) s->player_venom_timer--;
    if (s->player_venom_timer > 0) return;
    int dmg = s->player_venom;
    encounter_damage_player(&s->player, dmg, &s->tick_scratch.damage_received);
    s->last_hit_by_type = COLO_MANTICORE;
    col_mod_on_player_damaged(s, dmg);
    s->player_venom -= COLO_VENOM_DECAY;
    if (s->player_venom < 0) s->player_venom = 0;
    s->player_venom_timer = COLO_VENOM_INTERVAL;
}

/* ----- Totemic (50%-HP healing totem) --------------------------------------- */

/** spawn a healing totem next to an NPC that just crossed 50% HP, unless one is
    already bound to that slot. Hook from the NPC-damage path. */
static void col_mod_on_npc_hp_changed(ColosseumState* s, int npc_slot) {
    if (!col_mod_active(s, COLO_MOD_TOTEMIC)) return;
    const ColoNPC* npc = &s->npcs[npc_slot];
    if (!npc->active || npc->hp <= 0 || npc->max_hp <= 0) return;
    if (npc->hp * 2 > npc->max_hp) return;   /* not yet at <=50% */
    if (npc->type == COLO_SOL_HEREDIT) return;

    for (int t = 0; t < COLO_MAX_TOTEMS; t++)
        if (s->totems[t].active && s->totems[t].npc_slot == npc_slot) return;

    for (int t = 0; t < COLO_MAX_TOTEMS; t++) {
        if (s->totems[t].active) continue;
        s->totems[t].active = 1;
        s->totems[t].npc_slot = npc_slot;
        int size = col_npc_effective_size(npc);
        s->totems[t].x = npc->x + size;       /* one tile off the NPC's east edge */
        if (!col_in_arena(s->totems[t].x, npc->y)) s->totems[t].x = npc->x - 1;
        s->totems[t].y = npc->y;
        s->totems[t].heal_timer = COLO_TOTEM_HEAL_INTERVAL;
        return;
    }
}

/** tick all totems: heal their bound NPC each pulse; a totem is destroyed when the
    player stands adjacent to it or its NPC dies. */
static void col_mod_tick_totems(ColosseumState* s) {
    for (int t = 0; t < COLO_MAX_TOTEMS; t++) {
        ColoTotem* tot = &s->totems[t];
        if (!tot->active) continue;

        int slot = tot->npc_slot;
        ColoNPC* npc = (slot >= 0 && slot < COLO_MAX_NPCS) ? &s->npcs[slot] : NULL;
        if (!npc || !npc->active || npc->hp <= 0) { tot->active = 0; continue; }

        int pdist = encounter_rect_distance(s->player.x, s->player.y, 1, tot->x, tot->y, 1);
        if (pdist <= 1) { tot->active = 0; continue; }   /* player destroys the totem */

        if (tot->heal_timer > 0) tot->heal_timer--;
        if (tot->heal_timer == 0) {
            int heal = (npc->max_hp * COLO_TOTEM_HEAL_PCT) / 100;
            npc->hp += heal;
            if (npc->hp > npc->max_hp) npc->hp = npc->max_hp;
            tot->heal_timer = COLO_TOTEM_HEAL_INTERVAL;
        }
    }
}

/* ----- molten pools (Reentry / Volatility T3 on waves 1-11) ----------------- */

/** add a molten pool at (x,y) living `ticks` ticks (COLO_POOL_PERMANENT = forever).
    Overflow recycles the oldest slot so the list never asserts. */
static void col_mod_add_molten_pool(ColosseumState* s, int x, int y, int ticks) {
    if (!col_in_arena(x, y)) return;
    int slot;
    if (s->molten_count < COLO_SOL_HAZARD_TILES_MAX) slot = s->molten_count++;
    else slot = encounter_rand_int(&s->rng_state, COLO_SOL_HAZARD_TILES_MAX);
    s->molten_x[slot] = x;
    s->molten_y[slot] = y;
    s->molten_ticks[slot] = ticks;
}

/** tick molten pools: expire temporary ones, burn the player standing on any. */
static void col_mod_tick_molten_pools(ColosseumState* s) {
    int burned = 0;
    for (int i = 0; i < s->molten_count; i++) {
        if (s->molten_ticks[i] != COLO_POOL_PERMANENT) {
            s->molten_ticks[i]--;
            if (s->molten_ticks[i] <= 0) {
                s->molten_x[i] = s->molten_x[s->molten_count - 1];
                s->molten_y[i] = s->molten_y[s->molten_count - 1];
                s->molten_ticks[i] = s->molten_ticks[s->molten_count - 1];
                s->molten_count--;
                i--;
                continue;
            }
        }
        if (!burned && s->player.x == s->molten_x[i] && s->player.y == s->molten_y[i]) {
            encounter_damage_player(&s->player, COLO_MOLTEN_MAX_HIT, &s->tick_scratch.damage_received);
            s->last_hit_by_type = s->last_hit_by_type < 0 ? COLO_JAVELIN_COLOSSUS : s->last_hit_by_type;
            col_mod_on_player_damaged(s, COLO_MOLTEN_MAX_HIT);
            burned = 1;
        }
    }
}

/** Reentry hook: when a javelin skyfall lands, leave molten sand. T1 temporary,
    T2 permanent (until wave end), T3 also melts the tile one west of the target. */
static void col_mod_reentry_on_skyfall(ColosseumState* s, int tile_x, int tile_y) {
    int tier = col_mod_tier(s, COLO_MOD_REENTRY);
    if (tier == 0) return;
    int ticks = (tier >= 2) ? COLO_POOL_PERMANENT : COLO_REENTRY_POOL_TICKS;
    col_mod_add_molten_pool(s, tile_x, tile_y, ticks);
    if (tier >= 3) col_mod_add_molten_pool(s, tile_x - 1, tile_y, ticks);
}

/* ----- Volatility (death explosion) ----------------------------------------- */

/** Volatility hook on NPC death: an explosion `tier` tiles beyond the NPC footprint
    hits the player if caught in it; T3 also leaves a temporary molten pool at the
    death centre. Call before the slot is reused (uses the NPC's last position). */
static void col_mod_volatility_on_death(ColosseumState* s, int x, int y, int size) {
    int tier = col_mod_tier(s, COLO_MOD_VOLATILITY);
    if (tier == 0) return;
    int reach = (tier >= 2) ? 2 : 1;
    int span = size + 2 * reach;
    int ox = x - reach, oy = y - reach;
    if (encounter_entity_footprints_overlap(ox, oy, span, s->player.x, s->player.y, 1)) {
        encounter_damage_player(&s->player, COLO_VOLATILITY_MAX_HIT, &s->tick_scratch.damage_received);
        col_mod_on_player_damaged(s, COLO_VOLATILITY_MAX_HIT);
    }
    if (tier >= 3) col_mod_add_molten_pool(s, x + size / 2, y + size / 2, COLO_VOLATILITY_POOL_TICKS);
}

/* ----- Bees! (roaming poison swarms) ---------------------------------------- */

/** sync the live bee-swarm count to the Bees! tier (1/2/3), spawning new swarms at
    a random arena edge and despawning extras. Called at each wave spawn. */
static void col_mod_sync_bees(ColosseumState* s) {
    int want = col_mod_tier(s, COLO_MOD_BEES);
    for (int b = 0; b < COLO_MAX_BEE_SWARMS; b++) {
        ColoBeeSwarm* bee = &s->bees[b];
        if (b < want) {
            if (bee->active) continue;
            bee->active = 1;
            int pos = encounter_rand_int(&s->rng_state, COLO_NUM_SPAWN_POS);
            bee->x = COLO_SPAWN_POS[pos][0];
            bee->y = COLO_SPAWN_POS[pos][1];
            bee->move_timer = COLO_BEE_MOVE_INTERVAL;
        } else {
            bee->active = 0;
        }
    }
}

/** tick bee swarms: step one tile toward the player every COLO_BEE_MOVE_INTERVAL
    ticks, dealing up to COLO_BEE_MAX_POISON unblockable damage on contact. */
static void col_mod_tick_bees(ColosseumState* s) {
    for (int b = 0; b < COLO_MAX_BEE_SWARMS; b++) {
        ColoBeeSwarm* bee = &s->bees[b];
        if (!bee->active) continue;

        if (bee->move_timer > 0) bee->move_timer--;
        if (bee->move_timer == 0) {
            if (bee->x < s->player.x) bee->x++;
            else if (bee->x > s->player.x) bee->x--;
            if (bee->y < s->player.y) bee->y++;
            else if (bee->y > s->player.y) bee->y--;
            bee->move_timer = COLO_BEE_MOVE_INTERVAL;
        }

        if (bee->x == s->player.x && bee->y == s->player.y) {
            int dmg = encounter_rand_int(&s->rng_state, COLO_BEE_MAX_POISON + 1);
            encounter_damage_player(&s->player, dmg, &s->tick_scratch.damage_received);
            if (dmg > 0) col_mod_on_player_damaged(s, dmg);
        }
    }
}

/* ----- Solarflare (orb circling the pillars) -------------------------------- */

/** the orb path: the four boss pillars on the boss wave, else the four arena
    spawn corners (so the orb still sweeps the play space pre-boss). Writes the
    `corner`-th ring node (0..3, clockwise) into (*cx,*cy). */
static void col_mod_solarflare_corner(const ColosseumState* s, int corner, int* cx, int* cy) {
    corner &= 3;
    if (s->wave == COLO_WAVE_BOSS) {
        *cx = COLO_BOSS_PILLARS[corner][0];
        *cy = COLO_BOSS_PILLARS[corner][1];
    } else {
        *cx = COLO_SPAWN_POS[corner][0];   /* first four spawn positions are the corners */
        *cy = COLO_SPAWN_POS[corner][1];
    }
}

/** start/refresh the Solarflare orb at the first ring corner when active. */
static void col_mod_sync_solarflare(ColosseumState* s) {
    if (!col_mod_active(s, COLO_MOD_SOLARFLARE)) { s->solarflare.active = 0; return; }
    if (s->solarflare.active) return;
    s->solarflare.active = 1;
    s->solarflare.step = 0;
    s->solarflare.move_timer = 0;
    s->solarflare.pause_timer = 0;
    col_mod_solarflare_corner(s, 0, &s->solarflare.x, &s->solarflare.y);
}

/** orb damage by tier (UNCONFIRMED higher tiers). */
static int col_mod_solarflare_max_hit(int tier) {
    if (tier >= 3) return COLO_SOLARFLARE_MAX_HIT_T3;
    if (tier == 2) return COLO_SOLARFLARE_MAX_HIT_T2;
    return COLO_SOLARFLARE_MAX_HIT_T1;
}

/**
 * Tick the Solarflare orb: walk it around the pillar ring at the tier cadence
 * (T1 every 2 ticks, pausing 7 at corners; T2 continuous every 2; T3 every tick),
 * and on contact deal tier damage. T3 also disables the player's overhead prayer.
 */
static void col_mod_tick_solarflare(ColosseumState* s) {
    if (!s->solarflare.active) return;
    int tier = col_mod_tier(s, COLO_MOD_SOLARFLARE);
    if (tier == 0) { s->solarflare.active = 0; return; }

    ColoSolarflareOrb* orb = &s->solarflare;
    if (orb->pause_timer > 0) {
        orb->pause_timer--;
    } else {
        if (orb->move_timer > 0) orb->move_timer--;
        if (orb->move_timer == 0) {
            int c0 = (orb->step >> 1) & 3;             /* node index, 2 tiles between corners */
            int next_step = orb->step + 1;
            int c1 = (next_step >> 1) & 3;
            int ax, ay, bx, by;
            col_mod_solarflare_corner(s, c0, &ax, &ay);
            col_mod_solarflare_corner(s, c1, &bx, &by);
            int frac = next_step & 1;                  /* 0 = at corner c1, 1 = midpoint */
            orb->x = frac ? (ax + bx) / 2 : bx;
            orb->y = frac ? (ay + by) / 2 : by;
            orb->step = next_step & 7;
            orb->move_timer = (tier >= 3) ? 0 : 1;     /* T3 moves every tick */
            if (tier == 1 && (orb->step & 1) == 0) orb->pause_timer = COLO_SOLARFLARE_CORNER_PAUSE;
        }
    }

    if (orb->x == s->player.x && orb->y == s->player.y) {
        int dmg = encounter_rand_int(&s->rng_state, col_mod_solarflare_max_hit(tier) + 1);
        encounter_damage_player(&s->player, dmg, &s->tick_scratch.damage_received);
        if (dmg > 0) col_mod_on_player_damaged(s, dmg);
        if (tier >= 3) s->player.prayer = PRAYER_NONE;   /* T3 disables prayer on hit */
    }
}

/* ----- per-tick modifier driver --------------------------------------------- */

/**
 * Run all per-tick modifier hazards + draft countdown. Called from col_step after
 * combat resolves. Order: draft timer, then the hazards that damage the player
 * (bees, solarflare, molten pools, venom), then totems (heal NPCs). A lethal Doom
 * stack is surfaced via col_mod_doom_lethal and checked by the step death gate.
 */
static void col_modifiers_tick(ColosseumState* s) {
    col_modifier_tick_draft(s);
    col_mod_tick_bees(s);
    col_mod_tick_solarflare(s);
    col_mod_tick_molten_pools(s);
    col_mod_tick_venom(s);
    col_mod_tick_totems(s);
}

/** per-wave-spawn modifier sync: reset Doom stacks, re-apply Frailty, sync bee
    swarm count + the Solarflare orb. Called from col_spawn_wave after NPCs spawn. */
static void col_modifiers_on_wave_spawn(ColosseumState* s) {
    s->doom_stacks = 0;
    s->player_venom = 0;
    s->player_venom_timer = 0;
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
