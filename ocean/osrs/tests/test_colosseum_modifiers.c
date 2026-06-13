/**
 * @file test_colosseum_modifiers.c
 * @brief Regression tests for the Fortis Colosseum modifier system.
 *
 * Two layers:
 *   1. a fuzz loop that drives col_step + col_write_obs + col_write_mask every
 *      tick across full runs, so the obs/mask running-index asserts fire on real
 *      state (the standalone --profile harness skips the obs writer);
 *   2. deterministic scenario checks that each modifier mechanic actually fires:
 *      the P5 mandatory draft (fixed wave-1 offer, frozen-until-pick, 12 picks
 *      per run, pool windows, upgrade bias), Frailty max-HP cut, Relentless
 *      defence-LEVEL bypass + damage uplift, Quartet extra warbander, the
 *      1-HP totem/bee hazard entities with their respawn cycles, Reentry sand
 *      tiles/lifetimes, and A25 venom escalation;
 *   3. P1 arena geometry: the los `Lr` wall-mask port (hardcoded row extents),
 *      pillar blocking, spawn-anchor placement + the B3 player-proximity
 *      exclusion, gate-gap reinforcements with the b5 yellow-line side rule,
 *      static-mask line of sight + the ranged attack gate, and the wave-12
 *      quartet-reachability + Sol-death-wins predicate;
 *   4. P2 warband rework: the shared wave-anchored 6-tick cycle offsets (A5+B2),
 *      the player-moving attack skip, the cardinal melee-distance gate, formation
 *      convergence (diamond N/E/W/S), 2-tiles/tick routefinding around pillars vs
 *      the safespottable greedy shaman, and Red Flag minotaur routefinding (A30);
 *   5. P3 NPC mechanic fixes: minotaur single-target heal-to-full with the
 *      <75%/7-tile/centre-LoS gates + melee priority (A13+D9), the manticore
 *      10-tick barrage period (A19), travel-time-0 orbs with fire-tick flicks
 *      (D12), the wave-9 pair pattern-copy (B10), and the gateless javelin
 *      skyfall (D7);
 *   6. P4 Sol Heredit overhaul: the A2 adjacency gate + kiting delay + per-
 *      attack delays, A1 pool selection invariants (forced spears, 2-normal
 *      special cooldown, variant alternation), the A20+B4 parry schedule with
 *      the early-prayer punish + per-hit deactivation, the A12+B7 grapple
 *      (5-slot domain, perfect-parry guaranteed max consumed by the player
 *      attack), A3 shield safe rings + spear lines, A9/A10 accumulating
 *      crystals with 25-35t cooldowns + 60-75 spheres, and A11 beams becoming
 *      permanent 5-9/tick molten pools;
 *   7. researched loadout profiles (L1-L16): profile sampling + gear/supply
 *      tables, brew/restore/combat/ranging/surge consumables with the L12
 *      max-hit recompute invariant, sanfew venom/poison cure + serp-helm immunity,
 *      claws/elder-maul/SGS spec weapons, scythe splats, tbow/crystal/blood-
 *      fury item effects, and the offensive prayer head.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/test_colosseum_modifiers \
 *       ocean/osrs/tests/test_colosseum_modifiers.c -lm
 *   /tmp/test_colosseum_modifiers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(label, cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s\n", (label)); } \
} while (0)

/* drive one step with a fixed action vector, then exercise the obs + mask writers
   (their internal running-index asserts validate the layout each tick). */
static void step_and_observe(ColosseumState* s, ColosseumContext* ctx, const int* actions) {
    static float obs[COLO_NUM_OBS];
    static float mask[COLO_ACTION_MASK_SIZE];
    col_step_ctx((EncounterState*)s, (EncounterContext*)ctx, actions);
    col_write_obs_ctx((EncounterState*)s, (EncounterContext*)ctx, obs);
    col_write_mask_ctx((EncounterState*)s, (EncounterContext*)ctx, mask);
}

/* a draft is open iff draft_pending is set with at least one real option. */
static int draft_is_open(const ColosseumState* s) {
    if (!s->modifiers.draft_pending) return 0;
    for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++)
        if (s->modifiers.draft_options[o] >= 0) return 1;
    return 0;
}

/* complete the mandatory pre-wave draft (A16+B6): pick `option`, then run the
   armed spawn tick so the wave roster exists. Leaves the 6-tick ready delay
   running, mirroring the old post-reset state. Tests that just need to get
   past the wave-1 draft pick option 1 = Blasphemy, the most inert of the
   fixed wave-1 offer. */
static void complete_open_draft(ColosseumState* s, ColosseumContext* ctx, int option) {
    int pick[COLO_NUM_ACTION_HEADS] = {0};
    pick[COLO_HEAD_MODIFIER_SELECT] = option + 1;
    step_and_observe(s, ctx, pick);
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    while (s->wave_spawn_delay > 0) step_and_observe(s, ctx, idle);
}

/* force-kill every live non-hazard NPC so the next step registers a clear. */
static void force_clear_wave(ColosseumState* s) {
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (!s->npcs[i].active) continue;
        if (col_type_is_hazard_entity(s->npcs[i].type)) continue;
        s->npcs[i].hp = 0;
        s->npcs[i].active = 0;
    }
}

/* land the player's queued attack outside the step loop: pending hits carry a
   1-3 tick projectile delay (the scythe's range-2 path takes 3). */
static void land_pending_player_hits(ColosseumState* s) {
    for (int t = 0; t < 4; t++) col_resolve_player_projectiles_on_npcs(s);
}

/* clear every NPC, its collision stamps, and the hazard bookkeeping so checks
   run on an empty arena (mirrors what col_spawn_wave does before placing a
   roster). */
static void geo_clear_npcs(ColosseumState* s) {
    memset(s->npcs, 0, sizeof(s->npcs));
    memset(s->npc_collision_flags, 0, sizeof(s->npc_collision_flags));
    memset(s->totems, 0, sizeof(s->totems));
    memset(s->bees, 0, sizeof(s->bees));
    col_rebuild_player_collision_flags(s);
}

static void init_forecast_test_state(
    ColosseumState* s,
    ColosseumContext* ctx,
    uint32_t seed,
    int player_x,
    int player_y
) {
    col_init_context_typed(ctx);
    memset(s, 0, sizeof(*s));
    col_reset_ctx((EncounterState*)s, (EncounterContext*)ctx, seed);
    geo_clear_npcs(s);
    s->modifiers.draft_pending = 0;
    s->player.x = player_x;
    s->player.y = player_y;
    col_rebuild_player_collision_flags(s);
}

static int forecast_move_action_for_delta(int dx, int dy) {
    for (int action = 0; action < ENCOUNTER_MOVE_ACTIONS; action++)
        if (ENCOUNTER_MOVE_TARGET_DX[action] == dx &&
            ENCOUNTER_MOVE_TARGET_DY[action] == dy) return action;
    assert(0 && "missing movement action delta");
    return 0;
}

static int forecast_action_has_event(const ColoStepOutForecastAction* action) {
    for (int tick = 0; tick < COLO_STEP_OUT_FORECAST_HORIZON; tick++)
        if (col_step_out_forecast_tick_has_event(&action->ticks[tick])) return 1;
    return 0;
}

/* ---- 1. fuzz: random actions over many full runs, obs/mask asserted each tick.
   Validates the obs/mask running-index asserts + crash-freedom across the boss and
   all waves (the standalone --profile harness never calls the obs writer). */
static void test_fuzz_obs_mask(void) {
    printf("test_fuzz_obs_mask\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 0;
    ctx.config.loadout_profile_mode = COLO_LOADOUT_PROFILE_MODE_MIXED;
    ctx.config.beginner_loadout_fraction = 0.5f;

    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 12345);

    /* sweep every start wave so the boss + every modifier-eligible wave is hit. */
    int episodes = 0;
    int actions[COLO_NUM_ACTION_HEADS];
    unsigned int rng = 99;
    for (int start = 1; start <= COLO_NUM_WAVES; start++) {
        ctx.config.start_wave = start - 1;
        col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, (uint32_t)(start * 7 + 1));
        for (long t = 0; t < 120000 && episodes < 600; t++) {
            for (int h = 0; h < COLO_NUM_ACTION_HEADS; h++) {
                rng = rng * 1103515245u + 12345u;
                actions[h] = (int)((rng >> 16) % (unsigned)COLO_ACTION_DIMS[h]);
            }
            step_and_observe(&s, &ctx, actions);
            if (s.episode_over) {
                episodes++;
                col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, (uint32_t)(rng | 1));
            }
        }
    }

    CHECK("fuzz ran full episodes with obs/mask asserts holding", episodes > 0);
    printf("  episodes=%d (obs+mask running-index asserts held every tick)\n", episodes);
}

/* ---- 1a-bis. timeout is unconditional: an all-"none" action stream parks the
   episode at the mandatory wave-1 draft forever, and the draft-frozen path used
   to skip the MAX_TICKS check entirely (training-iter-1 freeze bug). */
static void test_zero_actions_hit_timeout(void) {
    printf("test_zero_actions_hit_timeout\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 0;

    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 12345);
    CHECK("reset opens the mandatory wave-1 draft", s.modifiers.draft_pending == 1);

    int actions[COLO_NUM_ACTION_HEADS] = {0};
    long t = 0;
    for (; t < COLO_MAX_TICKS + 10 && !s.episode_over; t++)
        step_and_observe(&s, &ctx, actions);

    CHECK("all-none actions terminate at the tick cap", s.episode_over == 1);
    CHECK("timeout fires exactly at MAX_TICKS", s.tick == COLO_MAX_TICKS);
    CHECK("timeout counts as a loss", s.winner == COLO_OUTCOME_PLAYER_DIED);
    CHECK("the draft was still pending when time ran out", s.modifiers.draft_pending == 1);
}

/* ---- 1a-ter. per-NPC-type prayer attribution: off-prayer hits attribute their
   damage to the source type, prayed hits count correct, ignore-prayer hits
   (javelin skyfall style) stay out of the prayer log entirely. */
static void test_offpray_attribution_log(void) {
    printf("test_offpray_attribution_log\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 0;

    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 777);
    int hp0 = s.player.current_hitpoints;

    s.player.prayer = PRAYER_PROTECT_MELEE;
    col_queue_player_pending_hit(&s, 0, COLO_SERPENT_SHAMAN, 11, 1, ATTACK_STYLE_MAGIC, 1, 1);
    col_resolve_player_pending_hits(&s);
    CHECK("off-prayer hit counted as faced", s.log.pray_faced_by_type[COLO_SERPENT_SHAMAN] == 1.0f);
    CHECK("off-prayer hit not counted correct", s.log.pray_correct_by_type[COLO_SERPENT_SHAMAN] == 0.0f);
    CHECK("off-prayer damage attributed to the shaman", s.log.offpray_damage_by_type[COLO_SERPENT_SHAMAN] == 11.0f);
    CHECK("off-prayer damage landed", s.player.current_hitpoints == hp0 - 11);

    s.player.prayer = PRAYER_PROTECT_MAGIC;
    col_queue_player_pending_hit(&s, 0, COLO_SERPENT_SHAMAN, 11, 1, ATTACK_STYLE_MAGIC, 1, 1);
    col_resolve_player_pending_hits(&s);
    CHECK("prayed hit counted as faced", s.log.pray_faced_by_type[COLO_SERPENT_SHAMAN] == 2.0f);
    CHECK("prayed hit counted correct", s.log.pray_correct_by_type[COLO_SERPENT_SHAMAN] == 1.0f);
    CHECK("prayed hit dealt no damage", s.player.current_hitpoints == hp0 - 11);

    col_queue_player_pending_hit(&s, 0, COLO_JAVELIN_COLOSSUS, 7, 1, ATTACK_STYLE_RANGED, 0, 1);
    col_resolve_player_pending_hits(&s);
    CHECK("ignore-prayer hit stays out of the prayer log",
        s.log.pray_faced_by_type[COLO_JAVELIN_COLOSSUS] == 0.0f);
    CHECK("ignore-prayer damage still landed", s.player.current_hitpoints == hp0 - 18);
}

/* ---- 1b. step-loop draft (A16+B6+D26): the wave-1 fixed offer opens at reset
   BEFORE any NPC spawns, the player is frozen until the mandatory pick (no
   skip, no auto-close), the pick gates the spawn + 6-tick ready delay, and the
   next clear opens the wave-2 draft the same way. */
static void test_step_loop_draft(void) {
    printf("test_step_loop_draft\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 0;
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 42);

    int idle[COLO_NUM_ACTION_HEADS] = {0};
    int walk_east[COLO_NUM_ACTION_HEADS] = {0};
    walk_east[COLO_HEAD_MOVE] = 7;

    /* A16: the run opens with the draft BEFORE wave 1, fixed offer. */
    CHECK("the wave-1 draft is open at reset, before any spawn", draft_is_open(&s));
    CHECK("wave-1 offer is the fixed {Relentless, Blasphemy, Frailty}",
        s.modifiers.draft_options[0] == COLO_MOD_RELENTLESS &&
        s.modifiers.draft_options[1] == COLO_MOD_BLASPHEMY &&
        s.modifiers.draft_options[2] == COLO_MOD_FRAILTY);
    int spawned = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++) if (s.npcs[i].active) spawned = 1;
    CHECK("no NPC spawns before the pick", !spawned);

    /* B6: frozen until the pick — walk actions are ignored and masked off, and
       the world stays paused (no skip, no timer ever closes the draft). */
    int px = s.player.x, py = s.player.y;
    for (int t = 0; t < 12; t++) step_and_observe(&s, &ctx, walk_east);
    CHECK("movement is ignored while the draft is open",
        s.player.x == px && s.player.y == py);
    CHECK("the draft never auto-closes", draft_is_open(&s));
    float mask[COLO_ACTION_MASK_SIZE];
    col_write_mask_ctx((EncounterState*)&s, (EncounterContext*)&ctx, mask);
    int any_walk_valid = 0;
    for (int d = 1; d < ENCOUNTER_MOVE_ACTIONS; d++)
        if (mask[d] > 0.0f) any_walk_valid = 1;
    CHECK("the mask offers only idle movement while frozen", !any_walk_valid);

    /* the pick spawns wave 1 (next tick) and re-arms the 6-tick ready delay. */
    int pick[COLO_NUM_ACTION_HEADS] = {0};
    pick[COLO_HEAD_MODIFIER_SELECT] = 1;   /* option 0 = Relentless */
    step_and_observe(&s, &ctx, pick);
    CHECK("the pick activated the chosen modifier", col_mod_active(&s, COLO_MOD_RELENTLESS));
    CHECK("draft closed after the pick", !s.modifiers.draft_pending);
    step_and_observe(&s, &ctx, idle);
    spawned = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++) if (s.npcs[i].active) spawned = 1;
    CHECK("the pick gated the wave-1 spawn", spawned);
    CHECK("the spawn re-armed the 6-tick ready delay (D26)",
        s.wave_ready_delay == COLO_START_READY_TICKS);

    /* movement works again after the pick. */
    px = s.player.x;
    step_and_observe(&s, &ctx, walk_east);
    CHECK("movement is effective after the pick", s.player.x == px + 1);

    /* the wave-1 clear opens the (random-pool) wave-2 draft; the player is
       frozen again until that pick, then advances. */
    force_clear_wave(&s);
    step_and_observe(&s, &ctx, idle);
    CHECK("clearing wave 1 opened the wave-2 draft", draft_is_open(&s));
    px = s.player.x;
    step_and_observe(&s, &ctx, walk_east);
    CHECK("frozen again during the wave-2 draft", s.player.x == px);
    int chosen = s.modifiers.draft_options[0];
    complete_open_draft(&s, &ctx, 0);
    CHECK("advanced to wave 2 after the pick", s.wave == 1);
    CHECK("the wave-2 pick persisted", chosen >= 0 && col_mod_active(&s, (ColoModifier)chosen));
}

/* ---- 1c. A16: 12 mandatory drafts per full run — one before every wave
   including wave 1 and into wave 12 — counted through the real step loop with
   force-cleared waves. */
static void test_twelve_drafts_per_run(void) {
    printf("test_twelve_drafts_per_run\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 0;
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 4242);

    int idle[COLO_NUM_ACTION_HEADS] = {0};
    int picks = 0;
    int draft_waves_ok = 1;
    for (long t = 0; t < 4000 && !s.episode_over; t++) {
        if (draft_is_open(&s)) {
            /* every draft gates the NEXT wave: picks 1..12 gate waves 1..12. */
            if (s.wave_spawn_target != picks) draft_waves_ok = 0;
            complete_open_draft(&s, &ctx, 0);
            picks++;
            continue;
        }
        force_clear_wave(&s);
        /* stay alive through accumulated modifier hazards (Doom/bees/venom). */
        s.player.current_hitpoints = s.player.base_hitpoints;
        s.doom_stacks = 0;
        step_and_observe(&s, &ctx, idle);
    }
    CHECK("the run ended in victory", s.episode_over && s.winner == COLO_OUTCOME_PLAYER_WON);
    CHECK("exactly 12 drafts were offered and picked", picks == 12);
    CHECK("the log counted all 12 mandatory picks", s.log.modifiers_picked == 12);
    CHECK("draft k gated wave k for every k", draft_waves_ok);
}

/* ---- 2a. draft offer, selection, persistence + the A16/D31 pool windows. */
static void test_draft_offer_and_select(void) {
    printf("test_draft_offer_and_select\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 1;   /* skip the reset draft: drive open_draft directly */
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 7);

    /* a fresh run starts with zero modifiers. */
    CHECK("fresh run has no active modifiers", s.modifiers.active_mask == 0);
    CHECK("start_wave>1 runs skip the prior drafts (no draft open)",
        s.modifiers.draft_pending == 0);

    /* the wave-1 draft is the fixed offer; later drafts are random + distinct. */
    col_modifier_open_draft(&s, 0);
    CHECK("the wave-1 draft is always {Relentless, Blasphemy, Frailty}",
        s.modifiers.draft_options[0] == COLO_MOD_RELENTLESS &&
        s.modifiers.draft_options[1] == COLO_MOD_BLASPHEMY &&
        s.modifiers.draft_options[2] == COLO_MOD_FRAILTY);
    col_modifier_open_draft(&s, 4);
    CHECK("draft opened with options", draft_is_open(&s));
    int distinct = 1;
    int a = s.modifiers.draft_options[0], b = s.modifiers.draft_options[1], c = s.modifiers.draft_options[2];
    if (a == b || a == c || (b == c && b >= 0)) distinct = 0;
    CHECK("draft options are distinct", distinct);

    int chosen = s.modifiers.draft_options[0];
    col_modifier_apply_selection(&s, 0);
    CHECK("selection set the active bit", col_mod_active(&s, (ColoModifier)chosen));
    CHECK("selection set tier >= 1", col_mod_tier(&s, (ColoModifier)chosen) >= 1);
    CHECK("draft closed after selection", s.modifiers.draft_pending == 0);
    CHECK("selection logged", s.log.modifiers_picked == 1);

    /* persistence: the active modifier survives a wave spawn. */
    s.wave = 1;
    col_spawn_wave(&s);
    CHECK("modifier persists across waves", col_mod_active(&s, (ColoModifier)chosen));

    /* A16 pool windows over many rolls: RF/DD absent from every draft into
       wave 8+ (0-based >= 7) yet present in the open window; the wave-12 draft
       excludes all four pre-boss-only modifiers (Mantimayhem per UPD-0424,
       Reentry per D31, RF/DD per the wiki). */
    int rfdd_late = 0, rfdd_window = 0, boss_excluded_seen = 0;
    for (int rep = 0; rep < 400; rep++) {
        int late_wave = 7 + rep % 4;   /* drafts into waves 8..11 (0-based 7..10) */
        col_modifier_open_draft(&s, late_wave);
        for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) {
            int m = s.modifiers.draft_options[o];
            if (m == COLO_MOD_RED_FLAG || m == COLO_MOD_DYNAMIC_DUO) rfdd_late = 1;
        }
        col_modifier_open_draft(&s, 2 + rep % 5);   /* drafts into waves 3..7 */
        for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) {
            int m = s.modifiers.draft_options[o];
            if (m == COLO_MOD_RED_FLAG || m == COLO_MOD_DYNAMIC_DUO) rfdd_window = 1;
        }
        col_modifier_open_draft(&s, COLO_WAVE_BOSS);
        for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) {
            int m = s.modifiers.draft_options[o];
            if (m >= 0 && COLO_MODIFIER_PRE_BOSS_ONLY[m]) boss_excluded_seen = 1;
        }
    }
    s.modifiers.draft_pending = 0;
    CHECK("Red Flag / Dynamic Duo never offered into wave 8+", rfdd_late == 0);
    CHECK("Red Flag / Dynamic Duo do appear in drafts before wave 7", rfdd_window == 1);
    CHECK("the wave-12 draft excludes RF/DD/Mantimayhem/Reentry", boss_excluded_seen == 0);
}

/* ---- 2a2. A16 MODELED upgrade bias: an owned tier-1 modifier reappears (as
   its tier-2 offer) with measurably elevated frequency vs an unowned peer over
   many drafts (weight 2 vs 1). */
static void test_draft_upgrade_bias(void) {
    printf("test_draft_upgrade_bias\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 1;
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 11);

    s.modifiers.active_mask |= (1u << COLO_MOD_RELENTLESS);
    s.modifiers.tier[COLO_MOD_RELENTLESS] = 1;   /* owned T1: upgradable */

    const int N = 4000;
    int owned_offers = 0, unowned_offers = 0;
    for (int rep = 0; rep < N; rep++) {
        col_modifier_open_draft(&s, 4);
        for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) {
            int m = s.modifiers.draft_options[o];
            if (m == COLO_MOD_RELENTLESS) owned_offers++;
            if (m == COLO_MOD_DOOM) unowned_offers++;   /* unowned tiered baseline */
        }
        s.modifiers.draft_pending = 0;
    }
    CHECK("both modifiers appear across the sample", owned_offers > 0 && unowned_offers > 0);
    CHECK("the owned T1 modifier is offered with clearly elevated frequency (~2x weight)",
        owned_offers * 2 > unowned_offers * 3);   /* owned/unowned > 1.5 */
    printf("  owned=%d unowned=%d over %d drafts\n", owned_offers, unowned_offers, N);

    /* a maxed modifier leaves the pool entirely. */
    s.modifiers.tier[COLO_MOD_RELENTLESS] = 3;
    int maxed_seen = 0;
    for (int rep = 0; rep < 200; rep++) {
        col_modifier_open_draft(&s, 4);
        for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++)
            if (s.modifiers.draft_options[o] == COLO_MOD_RELENTLESS) maxed_seen = 1;
        s.modifiers.draft_pending = 0;
    }
    CHECK("a maxed modifier is never offered again", maxed_seen == 0);
}

/* ---- 2b. Frailty lowers the player's max HP by tier. */
static void test_frailty_hp(void) {
    printf("test_frailty_hp\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 1);
    CHECK("base HP is 99 with no Frailty", s.player.base_hitpoints == 99);

    s.modifiers.active_mask |= (1u << COLO_MOD_FRAILTY);
    s.modifiers.tier[COLO_MOD_FRAILTY] = 3;   /* -40%: 99 - floor(99*40/100) = 60 */
    col_mod_apply_frailty_hp(&s);
    CHECK("Frailty III cuts max HP to 60", s.player.base_hitpoints == 60);
    CHECK("current HP clamped to new max", s.player.current_hitpoints <= 60);

    s.modifiers.tier[COLO_MOD_FRAILTY] = 1;   /* -10%: 99 - 9 = 90 */
    col_mod_apply_frailty_hp(&s);
    CHECK("Frailty I cuts max HP to 90", s.player.base_hitpoints == 90);
}

/* ---- 2c. Relentless raises incoming NPC damage (max-hit bonus + force hit). */
static void test_relentless_damage(void) {
    printf("test_relentless_damage\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 3);

    /* a berserker stat block, baseline vs Relentless III over many rolls. */
    const ColoNpcStats* berserker = &COLO_NPC_STATS[COLO_FREMENNIK_BERSERKER];
    long base_total = 0, relentless_total = 0;
    int base_hits = 0, relentless_hits = 0;
    const int N = 20000;

    s.modifiers.active_mask = 0;
    s.modifiers.tier[COLO_MOD_RELENTLESS] = 0;
    for (int i = 0; i < N; i++) {
        int hit = 0;
        int dmg = col_npc_roll_vs_player(&s, berserker, ATTACK_STYLE_MELEE, berserker->max_hit, &hit);
        base_total += dmg;
        base_hits += hit;
    }

    s.modifiers.active_mask |= (1u << COLO_MOD_RELENTLESS);
    s.modifiers.tier[COLO_MOD_RELENTLESS] = 3;   /* +6 max hit, ignore accuracy */
    for (int i = 0; i < N; i++) {
        int hit = 0;
        int dmg = col_npc_roll_vs_player(&s, berserker, ATTACK_STYLE_MELEE, berserker->max_hit, &hit);
        relentless_total += dmg;
        relentless_hits += hit;
    }

    CHECK("Relentless III forces every hit to land", relentless_hits == N);
    CHECK("Relentless III lands more often than baseline", relentless_hits > base_hits);
    CHECK("Relentless raises mean incoming damage", relentless_total > base_total);
    printf("  base_hits=%d/%d relentless_hits=%d/%d base_dmg=%ld relentless_dmg=%ld\n",
        base_hits, N, relentless_hits, N, base_total, relentless_total);
}

/* ---- 2d. Quartet adds an extra warbander to a wave's roster. */
static void test_quartet_extra_spawn(void) {
    printf("test_quartet_extra_spawn\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);

    ColosseumState base;
    memset(&base, 0, sizeof(base));
    col_reset_ctx((EncounterState*)&base, (EncounterContext*)&ctx, 5);
    base.wave = 0;
    col_spawn_wave(&base);
    int base_count = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++) if (base.npcs[i].active) base_count++;

    ColosseumState q;
    memset(&q, 0, sizeof(q));
    col_reset_ctx((EncounterState*)&q, (EncounterContext*)&ctx, 5);
    q.modifiers.active_mask |= (1u << COLO_MOD_QUARTET);
    q.modifiers.tier[COLO_MOD_QUARTET] = 1;
    q.wave = 0;
    col_spawn_wave(&q);
    int q_count = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++) if (q.npcs[i].active) q_count++;

    CHECK("Quartet spawns one extra NPC at wave start", q_count == base_count + 1);

    /* Quartet also makes a warbander appear on wave 12 (Sol-only normally). */
    ColosseumState q12;
    memset(&q12, 0, sizeof(q12));
    col_reset_ctx((EncounterState*)&q12, (EncounterContext*)&ctx, 5);
    q12.modifiers.active_mask |= (1u << COLO_MOD_QUARTET);
    q12.modifiers.tier[COLO_MOD_QUARTET] = 1;
    q12.wave = COLO_WAVE_BOSS;
    col_spawn_wave(&q12);
    int sol = 0, warband = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (!q12.npcs[i].active) continue;
        if (q12.npcs[i].type == COLO_SOL_HEREDIT) sol++;
        else warband++;
    }
    CHECK("wave 12 has Sol", sol == 1);
    CHECK("Quartet adds a warbander on wave 12", warband == 1);
}

/* ---- 2e. B9+E8: bee swarms are attackable 1-HP 2x2 NPCs — they spawn per
   tier, converge every 12 ticks, deal up-to-10 unblockable contact damage,
   apply standard poison, die to a single hit of any style, respawn 50 ticks
   later, and never block the wave clear. */
static void test_bees_hazard(void) {
    printf("test_bees_hazard\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 11);
    s.modifiers.active_mask |= (1u << COLO_MOD_BEES);
    s.modifiers.tier[COLO_MOD_BEES] = 2;   /* two swarms */
    s.wave = 0;
    col_spawn_wave(&s);

    int bee_npcs = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++)
        if (s.npcs[i].active && s.npcs[i].type == COLO_BEE_SWARM) bee_npcs++;
    CHECK("Bees II fields two 1-HP bee NPCs", bee_npcs == 2 &&
        s.bees[0].phase == COLO_HAZARD_ALIVE && s.bees[1].phase == COLO_HAZARD_ALIVE);
    CHECK("a bee NPC has exactly 1 HP", s.npcs[s.bees[0].npc_slot].hp == 1);
    CHECK("a bee swarm uses its cache 2x2 footprint", s.npcs[s.bees[0].npc_slot].size == 2);

    /* movement: a fresh swarm steps one tile toward the player every 12 ticks. */
    ColoNPC* bee_npc = &s.npcs[s.bees[0].npc_slot];
    int bx = bee_npc->x, by = bee_npc->y;
    for (int t = 0; t < COLO_BEE_MOVE_INTERVAL - 1; t++) col_mod_tick_bees(&s);
    CHECK("the swarm holds for 11 ticks", bee_npc->x == bx && bee_npc->y == by);
    col_mod_tick_bees(&s);
    int stepped = abs(bee_npc->x - bx) <= 1 && abs(bee_npc->y - by) <= 1 &&
        (bee_npc->x != bx || bee_npc->y != by);
    CHECK("the 12th tick steps one tile (diagonal allowed) toward the player", stepped);

    /* park the 2x2 swarm over each player-footprint tile. */
    int all_tiles_poison = 1;
    for (int dx = 0; dx < 2; dx++) {
        for (int dy = 0; dy < 2; dy++) {
            bee_npc->x = 10;
            bee_npc->y = 10;
            s.player.x = 10 + dx;
            s.player.y = 10 + dy;
            s.player_poison = 0;
            s.player_poison_timer = 0;
            s.bees[0].move_timer = COLO_BEE_MOVE_INTERVAL;
            col_mod_tick_bees(&s);
            if (s.player_poison != COLO_POISON_BEE_CONTACT_SEVERITY ||
                    s.player_poison_timer != COLO_POISON_INTERVAL)
                all_tiles_poison = 0;
        }
    }
    CHECK("bee contact applies poison from all four footprint tiles", all_tiles_poison);

    bee_npc->x = s.player.x;
    bee_npc->y = s.player.y;
    s.player.prayer = PRAYER_PROTECT_MELEE;
    int damaged = 0;
    for (int t = 0; t < 64 && !damaged; t++) {
        bee_npc->x = s.player.x;
        bee_npc->y = s.player.y;
        s.player.current_hitpoints = 99;
        col_mod_tick_bees(&s);
        if (s.player.current_hitpoints < 99) damaged = 1;
    }
    CHECK("a swarm beneath the player deals unblockable damage", damaged);

    s.wave = COLO_WAVE_BOSS;
    col_sol_begin_boss_arena(&s);
    bee_npc->x = COLO_BOSS_ARENA_MIN_X;
    bee_npc->y = COLO_BOSS_ARENA_MIN_Y + 2;
    s.player.x = COLO_BOSS_ARENA_MIN_X - 3;
    s.player.y = bee_npc->y;
    s.bees[0].move_timer = 1;
    col_mod_tick_bees(&s);
    CHECK("bee movement ignores the wave-12 boss-box clamp",
        bee_npc->x == COLO_BOSS_ARENA_MIN_X - 1);
    s.wave = 0;
    s.sol = (SolHereditState){0};
    s.player.x = bee_npc->x;
    s.player.y = bee_npc->y;

    /* B9: one player attack of any style kills the swarm; it respawns 50t later. */
    int slot = s.bees[0].npc_slot;
    col_player_attack_target(&s, slot);
    land_pending_player_hits(&s);
    CHECK("a single hit kills the swarm", !s.npcs[slot].active);
    CHECK("the killed swarm enters its 50-tick respawn",
        s.bees[0].phase == COLO_HAZARD_RESPAWNING &&
        s.bees[0].respawn_timer == COLO_BEE_RESPAWN_TICKS);
    for (int t = 0; t < COLO_BEE_RESPAWN_TICKS - 1; t++) col_mod_tick_bees(&s);
    CHECK("still respawning one tick early", s.bees[0].phase == COLO_HAZARD_RESPAWNING);
    col_mod_tick_bees(&s);
    CHECK("the swarm respawns exactly 50 ticks after death",
        s.bees[0].phase == COLO_HAZARD_ALIVE &&
        s.npcs[s.bees[0].npc_slot].active &&
        s.npcs[s.bees[0].npc_slot].type == COLO_BEE_SWARM);

    /* A21+B9: live hazard entities never block the wave clear. */
    ColosseumState sc;
    memset(&sc, 0, sizeof(sc));
    ctx.config.start_wave = 0;
    col_reset_ctx((EncounterState*)&sc, (EncounterContext*)&ctx, 13);
    sc.modifiers.active_mask |= (1u << COLO_MOD_BEES);
    sc.modifiers.tier[COLO_MOD_BEES] = 1;
    complete_open_draft(&sc, &ctx, 1);
    int live_bee = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++)
        if (sc.npcs[i].active && sc.npcs[i].type == COLO_BEE_SWARM) live_bee = 1;
    CHECK("a bee NPC is live going into the clear check", live_bee);
    force_clear_wave(&sc);
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    step_and_observe(&sc, &ctx, idle);
    CHECK("the wave clears (next draft opens) with the bee swarm still alive",
        draft_is_open(&sc) && sc.wave_spawn_target == 1);
}

/* ---- 2e2. A21+D22: totem lifecycle — spawns at the owner's <=50% crossing as
   an attackable 1-HP NPC, pulses 30% of max HP every 7 ticks while the owner
   sits at or below 50%, dies to one hit, respawns 200 ticks later, and
   despawns with its owner's death (respawn cancelled). */
static void test_totem_lifecycle(void) {
    printf("test_totem_lifecycle\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 1;   /* skip the reset draft */
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 211);
    geo_clear_npcs(&s);
    s.modifiers.active_mask |= (1u << COLO_MOD_TOTEMIC);
    s.modifiers.tier[COLO_MOD_TOTEMIC] = 1;
    s.player.x = 25; s.player.y = 18;
    col_rebuild_player_collision_flags(&s);

    /* shaman (125 max HP) damaged to 60 (48%): the hp-changed hook spawns a totem. */
    col_init_npc(&s, 0, COLO_SERPENT_SHAMAN, 12, 16);
    s.npcs[0].hp = 70;
    col_mod_on_npc_hp_changed(&s, 0);
    CHECK("no totem above 50% HP", s.totems[0].phase == COLO_HAZARD_NONE);
    s.npcs[0].hp = 60;
    col_mod_on_npc_hp_changed(&s, 0);
    CHECK("crossing <=50% spawns a totem", s.totems[0].phase == COLO_HAZARD_ALIVE);
    int tslot = s.totems[0].npc_slot;
    CHECK("the totem is a live 1-HP NPC beside its owner",
        tslot >= 0 && s.npcs[tslot].active &&
        s.npcs[tslot].type == COLO_HEALING_TOTEM && s.npcs[tslot].hp == 1);
    col_mod_on_npc_hp_changed(&s, 0);
    CHECK("no duplicate totem for the same owner", s.totems[0].phase == COLO_HAZARD_ALIVE);

    /* D22 pulse: 7 ticks -> +30% of 125 = +37 (60 -> 97); the next pulse is
       gated off because the owner now sits above 50%. */
    for (int t = 0; t < COLO_TOTEM_HEAL_INTERVAL - 1; t++) col_mod_tick_totems(&s);
    CHECK("no heal before the 7th tick", s.npcs[0].hp == 60);
    col_mod_tick_totems(&s);
    CHECK("the 7th tick heals 30% of the owner's max HP", s.npcs[0].hp == 60 + 37);
    for (int t = 0; t < COLO_TOTEM_HEAL_INTERVAL; t++) col_mod_tick_totems(&s);
    CHECK("the pulse is gated while the owner is above 50%", s.npcs[0].hp == 97);
    s.npcs[0].hp = 50;
    for (int t = 0; t < COLO_TOTEM_HEAL_INTERVAL; t++) col_mod_tick_totems(&s);
    CHECK("the pulse resumes once the owner re-crosses 50%", s.npcs[0].hp == 87);

    /* A21: one player attack of any style kills the totem; 200-tick respawn. */
    col_player_attack_target(&s, tslot);
    land_pending_player_hits(&s);
    CHECK("a single attack destroys the totem", !s.npcs[tslot].active);
    CHECK("destruction arms the 200-tick respawn",
        s.totems[0].phase == COLO_HAZARD_RESPAWNING &&
        s.totems[0].respawn_timer == COLO_TOTEM_RESPAWN_TICKS);
    for (int t = 0; t < COLO_TOTEM_RESPAWN_TICKS - 1; t++) col_mod_tick_totems(&s);
    CHECK("still down one tick early", s.totems[0].phase == COLO_HAZARD_RESPAWNING);
    col_mod_tick_totems(&s);
    CHECK("the totem respawns exactly 200 ticks after destruction",
        s.totems[0].phase == COLO_HAZARD_ALIVE &&
        s.npcs[s.totems[0].npc_slot].active &&
        s.npcs[s.totems[0].npc_slot].type == COLO_HEALING_TOTEM);

    /* the owner's death despawns the live totem outright. */
    int tslot2 = s.totems[0].npc_slot;
    col_apply_npc_death(&s, 0);
    CHECK("the owner's death despawns its totem",
        !s.npcs[tslot2].active && s.totems[0].phase == COLO_HAZARD_NONE);

    /* an owner dying while the totem is respawning cancels the respawn. */
    col_init_npc(&s, 0, COLO_SERPENT_SHAMAN, 12, 16);
    s.npcs[0].hp = 60;
    col_mod_on_npc_hp_changed(&s, 0);
    int tslot3 = s.totems[0].npc_slot;
    col_player_attack_target(&s, tslot3);
    land_pending_player_hits(&s);
    CHECK("second totem down and respawning", s.totems[0].phase == COLO_HAZARD_RESPAWNING);
    col_apply_npc_death(&s, 0);
    CHECK("the owner's death cancels a pending totem respawn",
        s.totems[0].phase == COLO_HAZARD_NONE);

    /* a live totem never blocks the wave clear: its owner is dead by clear time
       (totems despawn with the owner), and a lab-orphaned one self-clears. */
}

/* ---- 2e3. B5: wave 12 with Totemic — totems start spawning when Sol reaches
   50% and heal HIM a flat 75 every 7 ticks until destroyed (no <=50% pulse
   gate for Sol). */
static void test_totemic_sol_wave12(void) {
    printf("test_totemic_sol_wave12\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 11;
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 223);
    s.modifiers.active_mask |= (1u << COLO_MOD_TOTEMIC);
    s.modifiers.tier[COLO_MOD_TOTEMIC] = 1;
    int sol = col_sol_find_idx(&s);
    CHECK("Sol is live", sol >= 0);

    s.npcs[sol].hp = COLO_SOL_HP_MAX * 60 / 100;
    col_mod_on_npc_hp_changed(&s, sol);
    CHECK("no totem while Sol is above 50%", s.totems[sol].phase == COLO_HAZARD_NONE);
    s.npcs[sol].hp = COLO_SOL_HP_MAX / 2;
    col_mod_on_npc_hp_changed(&s, sol);
    CHECK("Sol at 50% spawns a totem", s.totems[sol].phase == COLO_HAZARD_ALIVE);
    int tslot = s.totems[sol].npc_slot;
    CHECK("Sol's totem is an attackable 1-HP NPC inside the boss arena",
        s.npcs[tslot].type == COLO_HEALING_TOTEM && s.npcs[tslot].hp == 1 &&
        col_in_boss_arena(&s, s.npcs[tslot].x, s.npcs[tslot].y));

    int hp0 = s.npcs[sol].hp;
    for (int t = 0; t < COLO_TOTEM_HEAL_INTERVAL - 1; t++) col_mod_tick_totems(&s);
    CHECK("no Sol heal before the 7th tick", s.npcs[sol].hp == hp0);
    col_mod_tick_totems(&s);
    CHECK("the pulse heals Sol exactly 75", s.npcs[sol].hp == hp0 + COLO_TOTEM_SOL_HEAL);
    for (int t = 0; t < COLO_TOTEM_HEAL_INTERVAL; t++) col_mod_tick_totems(&s);
    CHECK("Sol keeps healing 75/7t even above 50% (until destroyed)",
        s.npcs[sol].hp == hp0 + 2 * COLO_TOTEM_SOL_HEAL);

    col_player_attack_target(&s, tslot);
    land_pending_player_hits(&s);
    int hp1 = s.npcs[sol].hp;
    for (int t = 0; t < 3 * COLO_TOTEM_HEAL_INTERVAL; t++) col_mod_tick_totems(&s);
    CHECK("a destroyed totem stops the Sol heal (until the 200t respawn)",
        !s.npcs[tslot].active && s.npcs[sol].hp == hp1 &&
        s.totems[sol].phase == COLO_HAZARD_RESPAWNING);
}

/* ---- 2e4. A22: Reentry sand tiles + lifetimes — T1 marks the targeted tile
   until wave end; T2 is permanent and adds the SOUTH-WEST tile; T3 adds the
   WEST tile; all pools burn the shared 5-9 roll. */
static void test_reentry_sand_tiles(void) {
    printf("test_reentry_sand_tiles\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 1;
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 227);
    geo_clear_npcs(&s);

    s.modifiers.active_mask |= (1u << COLO_MOD_REENTRY);
    s.modifiers.tier[COLO_MOD_REENTRY] = 1;
    col_mod_reentry_on_skyfall(&s, 20, 12);
    CHECK("T1 leaves one pool on the targeted tile",
        s.molten_count == 1 && s.molten_x[0] == 20 && s.molten_y[0] == 12);
    CHECK("the T1 pool lasts until wave end (not tick-counted)",
        s.molten_kind[0] == COLO_POOL_UNTIL_WAVE_END);

    /* A22: pools burn the shared 5-9 molten-sand roll every tick stood on. */
    s.player.x = 20; s.player.y = 12;
    int burn_ok = 1;
    for (int t = 0; t < 24; t++) {
        s.player.current_hitpoints = 99;
        col_mod_tick_molten_pools(&s);
        int dmg = 99 - s.player.current_hitpoints;
        if (dmg < COLO_MOLTEN_SAND_MIN_HIT ||
            dmg > COLO_MOLTEN_SAND_MIN_HIT + COLO_MOLTEN_SAND_RAND - 1) burn_ok = 0;
    }
    CHECK("standing on Reentry sand burns the shared 5-9 roll", burn_ok);

    /* the T1 pool persists through arbitrary mid-wave time but clears at the
       next wave spawn. */
    for (int t = 0; t < 500; t++) col_mod_tick_molten_pools(&s);
    CHECK("the T1 pool persists all wave (old 8-tick lifetime deleted)",
        s.molten_count == 1);
    col_modifiers_on_wave_spawn(&s);
    CHECK("wave end clears the temporary pool", s.molten_count == 0);

    /* T2: until-wave-end + the SW tile. */
    s.modifiers.tier[COLO_MOD_REENTRY] = 2;
    col_mod_reentry_on_skyfall(&s, 20, 12);
    int has_target = 0, has_sw = 0, has_w = 0, all_until_wave_end = 1;
    for (int i = 0; i < s.molten_count; i++) {
        if (s.molten_x[i] == 20 && s.molten_y[i] == 12) has_target = 1;
        if (s.molten_x[i] == 19 && s.molten_y[i] == 11) has_sw = 1;
        if (s.molten_x[i] == 19 && s.molten_y[i] == 12) has_w = 1;
        if (s.molten_kind[i] != COLO_POOL_UNTIL_WAVE_END) all_until_wave_end = 0;
    }
    CHECK("T2 covers the targeted tile + the tile SOUTH-WEST of it",
        s.molten_count == 2 && has_target && has_sw && !has_w);
    CHECK("T2 pools last until wave end", all_until_wave_end);
    col_modifiers_on_wave_spawn(&s);
    CHECK("Reentry T2 pools clear at wave end", s.molten_count == 0);

    /* T3: adds the WEST tile. */
    s.molten_count = 0;
    s.modifiers.tier[COLO_MOD_REENTRY] = 3;
    col_mod_reentry_on_skyfall(&s, 20, 12);
    has_target = has_sw = has_w = 0;
    for (int i = 0; i < s.molten_count; i++) {
        if (s.molten_x[i] == 20 && s.molten_y[i] == 12) has_target = 1;
        if (s.molten_x[i] == 19 && s.molten_y[i] == 11) has_sw = 1;
        if (s.molten_x[i] == 19 && s.molten_y[i] == 12) has_w = 1;
    }
    CHECK("T3 additionally covers the WEST tile", s.molten_count == 3 &&
        has_target && has_sw && has_w);
    col_modifiers_on_wave_spawn(&s);
    CHECK("Reentry T3 pools clear at wave end", s.molten_count == 0);

    /* D20: Volatility T3 leaves an until-wave-end pool at the death centre. */
    s.molten_count = 0;
    s.modifiers.active_mask |= (1u << COLO_MOD_VOLATILITY);
    s.modifiers.tier[COLO_MOD_VOLATILITY] = 3;
    s.player.x = 5; s.player.y = 18;
    col_mod_volatility_on_death(&s, 20, 16, 1);
    CHECK("Volatility T3 leaves an until-wave-end pool at the centre",
        s.molten_count == 1 && s.molten_kind[0] == COLO_POOL_UNTIL_WAVE_END);
    col_modifiers_on_wave_spawn(&s);
    CHECK("the Volatility pool clears at wave end", s.molten_count == 0);
}

/* ---- 2e5. A25: venom escalation — first proc 6, +2 per damage tick toward
   the 20 cap on a 30-tick cadence; reapplication bumps the NEXT instance +2
   instead of resetting to 6. */
static void test_venom_escalation(void) {
    printf("test_venom_escalation\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 1;
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 229);
    s.modifiers.active_mask |= (1u << COLO_MOD_MANTIMAYHEM);
    s.modifiers.tier[COLO_MOD_MANTIMAYHEM] = 2;

    col_mod_manticore_apply_venom(&s, 1);
    CHECK("the first proc arms 6 damage on the 30-tick clock",
        s.player_venom == COLO_VENOM_START && s.player_venom_timer == COLO_VENOM_INTERVAL);

    /* damage sequence 6, 8, 10 .. 20 every 30 ticks, then held at the cap. */
    static const int EXPECT[9] = { 6, 8, 10, 12, 14, 16, 18, 20, 20 };
    int seq_ok = 1, cadence_ok = 1;
    for (int k = 0; k < 9; k++) {
        for (int t = 0; t < COLO_VENOM_INTERVAL - 1; t++) {
            s.player.current_hitpoints = 99;
            col_mod_tick_venom(&s);
            if (s.player.current_hitpoints != 99) cadence_ok = 0;
        }
        s.player.current_hitpoints = 99;
        col_mod_tick_venom(&s);
        if (99 - s.player.current_hitpoints != EXPECT[k]) seq_ok = 0;
    }
    CHECK("venom deals 6,8,10..20 then holds the cap", seq_ok);
    CHECK("venom damage lands exactly every 30 ticks", cadence_ok);

    /* reapplication while envenomed: +2 to the NEXT instance, timer untouched. */
    s.player_venom = COLO_VENOM_START;
    s.player_venom_timer = 17;
    col_mod_manticore_apply_venom(&s, 1);
    CHECK("reapplication bumps the next damage +2 without resetting the clock",
        s.player_venom == COLO_VENOM_START + COLO_VENOM_STEP &&
        s.player_venom_timer == 17);
    s.player_venom = COLO_VENOM_CAP;
    col_mod_manticore_apply_venom(&s, 1);
    CHECK("reapplication never exceeds the 20 cap", s.player_venom == COLO_VENOM_CAP);

    s.player_venom = COLO_VENOM_START;
    s.player_venom_timer = 17;
    s.modifiers.draft_pending = 1;
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    step_and_observe(&s, &ctx, idle);
    CHECK("venom timer freezes during the draft gap", s.player_venom_timer == 17);
    s.modifiers.draft_pending = 0;

    s.player_venom = COLO_VENOM_START;
    s.player_venom_timer = 2;
    col_modifiers_on_wave_spawn(&s);
    CHECK("venom survives the wave boundary",
        s.player_venom == COLO_VENOM_START && s.player_venom_timer == 2);
    s.player.current_hitpoints = 99;
    col_mod_tick_venom(&s);
    CHECK("venom does not tick early after the wave boundary", s.player.current_hitpoints == 99);
    col_mod_tick_venom(&s);
    CHECK("venom still ticks on the next wave",
        s.player.current_hitpoints == 99 - COLO_VENOM_START &&
        s.player_venom == COLO_VENOM_START + COLO_VENOM_STEP);
}

/* ---- 2e6. E8: bee poison is standard OSRS poison starting at 1 damage. */
static void test_bee_poison_status(void) {
    printf("test_bee_poison_status\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 230);

    col_mod_apply_bee_poison(&s);
    CHECK("bee poison starts at severity 5",
        s.player_poison == COLO_POISON_BEE_CONTACT_SEVERITY &&
        s.player_poison_timer == COLO_POISON_INTERVAL);

    int cadence_ok = 1;
    int hits_ok = 1;
    for (int hit = 0; hit < COLO_POISON_BEE_CONTACT_SEVERITY; hit++) {
        int severity_before = s.player_poison;
        for (int t = 0; t < COLO_POISON_INTERVAL - 1; t++) {
            s.player.current_hitpoints = 99;
            col_mod_tick_poison(&s);
            if (s.player.current_hitpoints != 99) cadence_ok = 0;
        }
        s.player.current_hitpoints = 99;
        col_mod_tick_poison(&s);
        if (99 - s.player.current_hitpoints != 1 ||
                s.player_poison != severity_before - 1)
            hits_ok = 0;
    }
    CHECK("bee poison deals exactly five 1-damage hits", hits_ok);
    CHECK("bee poison hits exactly 30 ticks apart", cadence_ok);
    CHECK("bee poison expires at severity 0",
        s.player_poison == 0 && s.player_poison_timer == 0);
}

/* ---- 2e5b. A24: Mantimayhem T3 shuffles the one-each {magic, ranged, melee}
   orb set — the melee orb's POSITION randomizes (all 3 slots seen), but a
   barrage never rolls duplicate styles. */
static void test_mantimayhem_t3_shuffle(void) {
    printf("test_mantimayhem_t3_shuffle\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 1;
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 239);
    geo_clear_npcs(&s);
    s.modifiers.active_mask |= (1u << COLO_MOD_MANTIMAYHEM);
    s.modifiers.tier[COLO_MOD_MANTIMAYHEM] = 3;
    s.player.x = 17; s.player.y = 16;
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_MANTICORE, 16, 12);
    ColoManticoreState* mc = colo_npc_manticore(&s.npcs[0]);

    int one_each_ok = 1;
    int melee_slot_seen[3] = { 0, 0, 0 };
    for (int rep = 0; rep < 300; rep++) {
        mc->cycle_step = -1;
        mc->pattern_copied = 0;
        mc->orb_style[0] = ATTACK_STYLE_NONE;   /* force a fresh arm/roll each rep */
        mc->orb_style[1] = ATTACK_STYLE_NONE;
        mc->orb_style[2] = ATTACK_STYLE_NONE;
        s.npcs[0].attack_timer = 0;
        s.player.current_hitpoints = 99;
        col_npc_attack_ctx(&s, &ctx, 0);   /* arms (rolls the set) then fires orb 0 */
        int counts[3] = { 0, 0, 0 };
        for (int o = 0; o < 3; o++) {
            if (mc->orb_style[o] == ATTACK_STYLE_RANGED) counts[0]++;
            if (mc->orb_style[o] == ATTACK_STYLE_MAGIC) counts[1]++;
            if (mc->orb_style[o] == ATTACK_STYLE_MELEE) {
                counts[2]++;
                melee_slot_seen[o] = 1;
            }
        }
        if (counts[0] != 1 || counts[1] != 1 || counts[2] != 1) one_each_ok = 0;
    }
    CHECK("T3 barrages always carry exactly one of each style (no iid rolls)",
        one_each_ok);
    CHECK("the melee orb appears in every position across the sample",
        melee_slot_seen[0] && melee_slot_seen[1] && melee_slot_seen[2]);
}

/* ---- 2e6. A23: Relentless bypasses the player's Defence LEVEL only — the
   geared defence roll shrinks by exactly the level share, with the gear bonus
   term fully intact. */
static void test_relentless_def_level_bypass(void) {
    printf("test_relentless_def_level_bypass\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 1;
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 233);

    const EncounterLoadoutStats* ls = &s.loadout_stats[s.weapon_set];
    int def_bonus = encounter_player_def_bonus(
        ls->def_stab, ls->def_slash, ls->def_crush, ls->def_magic, ls->def_ranged,
        ATTACK_STYLE_MELEE, MELEE_STYLE_STAB);
    CHECK("rig sanity: the geared player has a positive melee defence bonus", def_bonus > 0);

    /* (eff level + 8) * (bonus + 64) with the LEVEL scaled by the bypass. */
    int t0 = col_player_def_roll(&s, ATTACK_STYLE_MELEE, MELEE_STYLE_STAB);
    CHECK("tier 0 uses the full 99 defence level", t0 == (99 + 8) * (def_bonus + 64));

    s.modifiers.active_mask |= (1u << COLO_MOD_RELENTLESS);
    s.modifiers.tier[COLO_MOD_RELENTLESS] = 1;
    int t1 = col_player_def_roll(&s, ATTACK_STYLE_MELEE, MELEE_STYLE_STAB);
    CHECK("tier I keeps exactly 67% of the level (99 -> 66), bonus intact",
        t1 == (66 + 8) * (def_bonus + 64));

    s.modifiers.tier[COLO_MOD_RELENTLESS] = 2;
    int t2 = col_player_def_roll(&s, ATTACK_STYLE_MELEE, MELEE_STYLE_STAB);
    CHECK("tier II keeps exactly 34% of the level (99 -> 33), bonus intact",
        t2 == (33 + 8) * (def_bonus + 64));

    s.modifiers.tier[COLO_MOD_RELENTLESS] = 3;
    int t3 = col_player_def_roll(&s, ATTACK_STYLE_MELEE, MELEE_STYLE_STAB);
    CHECK("tier III zeroes the level share; the gear term alone remains",
        t3 == 8 * (def_bonus + 64));
}

/* ---- 2f. Mantimayhem stress: T1 doubles manticore orbs; with two manticores on
   a late wave the player pending-hit queue must not overflow (a hard abort). T2
   adds venom on unprotected hits. */
static void test_mantimayhem_stress(void) {
    printf("test_mantimayhem_stress\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 8;   /* wave 9 (index 8) has two manticores */
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 17);
    s.modifiers.active_mask |= (1u << COLO_MOD_MANTIMAYHEM);
    s.modifiers.tier[COLO_MOD_MANTIMAYHEM] = 2;   /* T2: double orbs + venom */
    s.wave = 8;
    col_spawn_wave(&s);

    int manticores = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++)
        if (s.npcs[i].active && s.npcs[i].type == COLO_MANTICORE) manticores++;
    CHECK("wave 9 spawns two manticores", manticores == 2);

    /* idle player, never praying, in range: drives sustained barrages. Topped up
       to immortal so hits keep queueing at maximal pressure. The queue push aborts
       on overflow, so simply surviving the loop is the assertion. */
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    int venom_seen = 0;
    for (int t = 0; t < 2000 && !s.episode_over; t++) {
        s.player.current_hitpoints = 9999;   /* keep alive to maximise queue depth */
        step_and_observe(&s, &ctx, idle);
        if (s.player_venom > 0) venom_seen = 1;
    }
    CHECK("Mantimayhem T2 survived sustained barrages without queue overflow", 1);
    CHECK("Mantimayhem T2 inflicted venom on the player", venom_seen);
}

/* collect the gaps (in ticks) between successive orb moves over `ticks`. */
static int sf_collect_move_gaps(ColosseumState* s, int ticks, int* gaps, int max_gaps) {
    int n = 0;
    int last_move_t = -1;
    int ox = s->solarflare.x, oy = s->solarflare.y;
    for (int t = 1; t <= ticks; t++) {
        col_mod_tick_solarflare(s);
        if (s->solarflare.x != ox || s->solarflare.y != oy) {
            if (last_move_t >= 0 && n < max_gaps) gaps[n++] = t - last_move_t;
            last_move_t = t;
            ox = s->solarflare.x;
            oy = s->solarflare.y;
        }
    }
    return n;
}

/* ---- 2g. A27: Solarflare cadence per tier — T1 moves every 2 ticks pausing 7
   at corners (move gaps alternate 2/9), T2 every 2 ticks continuous (all gaps
   2), T3 every tick pausing 2 at corners (gaps alternate 1/3) — plus contact
   damage and the T3 prayer disable. */
static void test_solarflare_orb(void) {
    printf("test_solarflare_orb\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 23);
    s.modifiers.active_mask |= (1u << COLO_MOD_SOLARFLARE);
    s.modifiers.tier[COLO_MOD_SOLARFLARE] = 2;   /* T2 continuous */
    s.wave = 0;
    col_spawn_wave(&s);
    CHECK("Solarflare orb is active", s.solarflare.active);
    /* park the player away from the ring so cadence runs damage-free. */
    s.player.x = 16; s.player.y = 16;

    int gaps[32];
    int n = sf_collect_move_gaps(&s, 40, gaps, 32);
    int t2_ok = n >= 8;
    for (int g = 0; g < n; g++) if (gaps[g] != 2) t2_ok = 0;
    CHECK("T2 moves every 2 ticks with no corner pause", t2_ok);

    /* T1: 2-tick steps, 7-tick corner pause -> gaps alternate 2 and 9. */
    s.modifiers.tier[COLO_MOD_SOLARFLARE] = 1;
    s.solarflare.active = 0;
    col_mod_sync_solarflare(&s);
    n = sf_collect_move_gaps(&s, 60, gaps, 32);
    int t1_ok = n >= 6;
    for (int g = 0; g < n; g++)
        if (gaps[g] != 2 && gaps[g] != 2 + COLO_SOLARFLARE_CORNER_PAUSE) t1_ok = 0;
    int t1_paused = 0;
    for (int g = 0; g < n; g++)
        if (gaps[g] == 2 + COLO_SOLARFLARE_CORNER_PAUSE) t1_paused = 1;
    CHECK("T1 moves every 2 ticks and pauses 7 at each corner", t1_ok && t1_paused);

    /* A27 T3: every-tick steps, 2-tick corner pause -> gaps alternate 1 and 3. */
    s.modifiers.tier[COLO_MOD_SOLARFLARE] = 3;
    s.solarflare.active = 0;
    col_mod_sync_solarflare(&s);
    n = sf_collect_move_gaps(&s, 40, gaps, 32);
    int t3_ok = n >= 8, t3_paused = 0, t3_fast = 0;
    for (int g = 0; g < n; g++) {
        if (gaps[g] != 1 && gaps[g] != 1 + COLO_SOLARFLARE_CORNER_PAUSE_T3) t3_ok = 0;
        if (gaps[g] == 1 + COLO_SOLARFLARE_CORNER_PAUSE_T3) t3_paused = 1;
        if (gaps[g] == 1) t3_fast = 1;
    }
    CHECK("T3 moves every tick AND stops 2 ticks at each corner (A27)",
        t3_ok && t3_paused && t3_fast);

    /* parking the player on the orb tile takes contact damage (T3 also drops prayer). */
    s.player.prayer = PRAYER_PROTECT_MAGIC;
    s.player.current_hitpoints = 99;
    int damaged = 0;
    for (int t = 0; t < 80 && !damaged; t++) {
        s.player.x = s.solarflare.x;
        s.player.y = s.solarflare.y;
        int hp = s.player.current_hitpoints;
        col_mod_tick_solarflare(&s);
        if (s.player.current_hitpoints < hp) damaged = 1;
    }
    CHECK("Solarflare orb deals contact damage", damaged);
    CHECK("Solarflare III disables prayer on hit", s.player.prayer == PRAYER_NONE);
}

/* ---- 2h. Volatility: a dying NPC explodes onto an adjacent player. */
static void test_volatility_explosion(void) {
    printf("test_volatility_explosion\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 29);
    s.modifiers.active_mask |= (1u << COLO_MOD_VOLATILITY);
    s.modifiers.tier[COLO_MOD_VOLATILITY] = 1;   /* 1 tile beyond footprint */

    /* place a 1x1 NPC one tile from the player, then kill it. */
    s.player.x = 17; s.player.y = 17;
    s.player.current_hitpoints = 99;
    int idx = 0;
    col_init_npc(&s, idx, COLO_FREMENNIK_BERSERKER, 18, 17);
    int hp_before = s.player.current_hitpoints;
    col_apply_npc_death(&s, idx);
    CHECK("Volatility explosion hits an adjacent player", s.player.current_hitpoints < hp_before);
}

/* ---- 3. P1 arena geometry ------------------------------------------------- */

/* 3a. los `Lr` port spot-checks: hardcoded row/column extents, the gate doors +
   flanks, the west entrance, the fully walled east edge, and the one
   asymmetric north/south row pair (catches a row-flip bug outright). */
static void test_static_arena_mask(void) {
    printf("test_static_arena_mask\n");
    col_build_npc_stats();

    int gate_rows_ok = 1;
    for (int x = 0; x <= 33; x++) {
        int walkable = (x == 13 || x == 14 || x == 19 || x == 20);
        if (col_static_blocked(x, 0) != !walkable) gate_rows_ok = 0;
        if (col_static_blocked(x, 33) != !walkable) gate_rows_ok = 0;
    }
    CHECK("south+north inner rows walkable exactly at the gate flanks {13,14,19,20}",
        gate_rows_ok);

    int west_ok = 1;
    for (int y = 0; y <= 33; y++) {
        int walkable = (y == 13 || y == 14 || y == 19 || y == 20);
        if (col_static_blocked(0, y) != !walkable) west_ok = 0;
    }
    CHECK("west col 0 open exactly at the entrance rows {13,14,19,20}", west_ok);

    int east_ok = 1;
    for (int y = 0; y <= 33; y++)
        if (!col_static_blocked(33, y)) east_ok = 0;
    CHECK("east col 33 fully walled", east_ok);

    /* the only asymmetric row pair: sim y=3 (los 30) blocks x<5, sim y=30
       (los 3) blocks x<6. A row flip inverts these. */
    CHECK("row 3 west extent [0,5)", col_static_blocked(4, 3) && !col_static_blocked(5, 3));
    CHECK("row 30 west extent [0,6)", col_static_blocked(5, 30) && !col_static_blocked(6, 30));
    CHECK("row 29 east extent [29,34)", !col_static_blocked(28, 29) && col_static_blocked(29, 29));

    int pillars_ok = 1, rim_ok = 1;
    for (int p = 0; p < COLO_NUM_PILLARS; p++) {
        int px = COLO_PILLARS[p][0], py = COLO_PILLARS[p][1];
        for (int dx = 0; dx < 3; dx++)
            for (int dy = 0; dy < 3; dy++)
                if (!col_static_blocked(px + dx, py + dy)) pillars_ok = 0;
        if (col_static_blocked(px - 1, py + 1)) rim_ok = 0;   /* west flank open */
        if (col_static_blocked(px + 3, py + 1)) rim_ok = 0;   /* east flank open */
    }
    CHECK("all 36 pillar tiles blocked on every wave", pillars_ok);
    CHECK("tiles flanking each pillar stay walkable", rim_ok);

    int zones_ok = 1;
    for (int a = 0; a < COLO_NUM_SPAWN_ANCHORS; a++)
        for (int dx = 0; dx < COLO_SPAWN_ZONE_SIZE; dx++)
            for (int dy = 0; dy < COLO_SPAWN_ZONE_SIZE; dy++)
                if (col_static_blocked(COLO_SPAWN_ANCHORS[a][0] + dx,
                                       COLO_SPAWN_ANCHORS[a][1] + dy)) zones_ok = 0;
    CHECK("every 3x3 spawn-anchor zone fully walkable on the static mask", zones_ok);

    CHECK("wave start (7,18) walkable",
        !col_static_blocked(COLO_PLAYER_START_X, COLO_PLAYER_START_Y));
    CHECK("boss start (16,10) walkable",
        !col_static_blocked(COLO_BOSS_PLAYER_START_X, COLO_BOSS_PLAYER_START_Y));
    int sol_ok = 1;
    for (int dx = 0; dx < 5; dx++)
        for (int dy = 0; dy < 5; dy++)
            if (col_static_blocked(COLO_SOL_SPAWN_X + dx, COLO_SOL_SPAWN_Y + dy)) sol_ok = 0;
    CHECK("Sol's 5x5 footprint at (16,19) unblocked", sol_ok);
}

/* 3b. static-mask LoS + the ranged attack gate: pillars and gate doors block
   centre-to-centre rays; a shaman without LoS holds fire and chases instead.
   (A5 made the warband archer melee-only, so the serpent shaman now carries the
   range-10 LoS-gate coverage this test originally pinned on the archer.) */
static void test_static_los_and_attack_gate(void) {
    printf("test_static_los_and_attack_gate\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 31);
    geo_clear_npcs(&s);

    CHECK("SW pillar blocks a ray along row 9", !col_tiles_have_los(&s, 7, 9, 12, 9));
    CHECK("pillar block is symmetric", !col_tiles_have_los(&s, 12, 9, 7, 9));
    CHECK("ray one row north of the pillar is clear", col_tiles_have_los(&s, 7, 12, 12, 12));
    CHECK("north gate doors block along the inner row", !col_tiles_have_los(&s, 14, 33, 19, 33));
    CHECK("row 32 inside the north gate is clear", col_tiles_have_los(&s, 14, 32, 19, 32));

    /* shaman at (13,9) vs player at (5,9): pillar (8..10, 8..10) between them. */
    s.player.x = 5; s.player.y = 9;
    col_init_npc(&s, 0, COLO_SERPENT_SHAMAN, 13, 9);
    s.npcs[0].attack_timer = 0;
    CHECK("shaman behind the pillar has no LoS", !col_npc_has_los_to_player(&s, &s.npcs[0]));
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("no-LoS shaman holds fire", s.npcs[0].attacked_this_tick == 0);
    col_npc_move_ctx(&s, &ctx, 0);
    CHECK("no-LoS shaman steps toward the player instead", s.npcs[0].moved_this_tick == 1);

    /* same range with a clear row: the attack fires. */
    geo_clear_npcs(&s);
    s.player.x = 5; s.player.y = 12;
    col_init_npc(&s, 0, COLO_SERPENT_SHAMAN, 13, 12);
    s.npcs[0].attack_timer = 0;
    CHECK("clear-row shaman has LoS", col_npc_has_los_to_player(&s, &s.npcs[0]));
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("clear-row shaman attacks", s.npcs[0].attacked_this_tick == 1);
}

/* 3c. spawn anchors: NPC SW lands ON an anchor, the B3 exclusion suppresses
   anchors near the player (exactly 3 of 12 on the b5 spawn-fix tile), anchors
   draw without replacement, and no footprint ever touches a blocked tile. */
static void test_spawn_anchor_exclusion(void) {
    printf("test_spawn_anchor_exclusion\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 37);

    /* the b5 spawn-fix tile, world (1813,3108): suppresses exactly anchors
       (3,14), (9,16), (3,19) -> 9 candidates ("4 out of 9" guide arithmetic). */
    geo_clear_npcs(&s);
    s.player.x = 5; s.player.y = 18;
    int cand[COLO_NUM_SPAWN_ANCHORS];
    int n = col_spawn_anchor_candidates(&s, cand);
    CHECK("b5 spawn-fix tile leaves exactly 9 candidate anchors", n == 9);
    int suppressed_ok = 1;
    for (int i = 0; i < n; i++)
        if (cand[i] == 0 || cand[i] == 1 || cand[i] == 2) suppressed_ok = 0;
    CHECK("the 3 suppressed anchors are (3,14),(9,16),(3,19)", suppressed_ok);

    int on_anchor_ok = 1, excluded_ok = 1, distinct_ok = 1, unblocked_ok = 1;
    int warband_ok = 1;
    for (int rep = 0; rep < 30; rep++) {
        s.player.x = 5; s.player.y = 18;
        s.wave = 4;   /* wave 5: BZ AR SE SH JV MC -> 3 primaries */
        col_spawn_wave(&s);
        int used[COLO_NUM_SPAWN_ANCHORS] = {0};
        int archer_x = -1, archer_y = -1;
        for (int i = 0; i < COLO_MAX_NPCS; i++) {
            if (!s.npcs[i].active || s.npcs[i].type != COLO_FREMENNIK_ARCHER) continue;
            archer_x = s.npcs[i].x; archer_y = s.npcs[i].y;
        }
        for (int i = 0; i < COLO_MAX_NPCS; i++) {
            ColoNPC* npc = &s.npcs[i];
            if (!npc->active) continue;
            int size = col_npc_effective_size(npc);
            for (int dx = 0; dx < size; dx++)
                for (int dy = 0; dy < size; dy++)
                    if (col_static_blocked(npc->x + dx, npc->y + dy)) unblocked_ok = 0;
            if (col_type_is_warbander(npc->type)) {
                /* trio: archer in the centre box, the others adjacent to it. */
                if (npc->type == COLO_FREMENNIK_ARCHER) {
                    if (npc->x < COLO_WARBAND_BOX_MIN_X || npc->x > COLO_WARBAND_BOX_MAX_X ||
                        npc->y < COLO_WARBAND_BOX_MIN_Y || npc->y > COLO_WARBAND_BOX_MAX_Y)
                        warband_ok = 0;
                } else {
                    int ddx = abs(npc->x - archer_x), ddy = abs(npc->y - archer_y);
                    if ((ddx > ddy ? ddx : ddy) > 1) warband_ok = 0;
                }
                continue;
            }
            int anchor = -1;
            for (int a = 0; a < COLO_NUM_SPAWN_ANCHORS; a++)
                if (COLO_SPAWN_ANCHORS[a][0] == npc->x &&
                    COLO_SPAWN_ANCHORS[a][1] == npc->y) anchor = a;
            if (anchor < 0) { on_anchor_ok = 0; continue; }
            if (used[anchor]) distinct_ok = 0;
            used[anchor] = 1;
            if (col_spawn_excluded_near_player(&s, npc->x, npc->y, COLO_SPAWN_ZONE_SIZE))
                excluded_ok = 0;
        }
    }
    CHECK("every primary spawns with its SW tile ON one of the 12 anchors", on_anchor_ok);
    CHECK("no primary ever lands within Chebyshev 4 of the player", excluded_ok);
    CHECK("anchors draw without replacement (no double-booking)", distinct_ok);
    CHECK("no spawned footprint touches a blocked tile", unblocked_ok);
    CHECK("warband trio spawns centre-box, berserker+seer adjacent to the ranger", warband_ok);
}

/* 3d. reinforcements: side-by-side inside the gate gap (x 15-18) on the
   innermost walkable row, side chosen by the b5 yellow line (y>=16 north /
   y<=15 south), exercised at the exact boundary where nearest-distance differs. */
static void test_reinforcement_gates(void) {
    printf("test_reinforcement_gates\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 10;   /* wave 11: reinforce set = minotaur + shaman */
    ColosseumState s;

    for (int north = 0; north <= 1; north++) {
        memset(&s, 0, sizeof(s));
        col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 41);
        geo_clear_npcs(&s);
        s.player.x = 16;
        s.player.y = north ? 16 : 15;   /* both sit nearer the SOUTH gate rows */
        col_spawn_reinforcements(&s);

        int count = 0, in_gap_ok = 1, row_ok = 1;
        for (int i = 0; i < COLO_MAX_NPCS; i++) {
            ColoNPC* npc = &s.npcs[i];
            if (!npc->active) continue;
            count++;
            int size = col_npc_effective_size(npc);
            if (npc->x < COLO_GATE_MIN_X || npc->x + size - 1 > COLO_GATE_MAX_X)
                in_gap_ok = 0;
            int inner_row = north ? npc->y + size - 1 : npc->y;
            if (inner_row != (north ? COLO_GATE_NORTH_SPAWN_ROW : COLO_GATE_SOUTH_SPAWN_ROW))
                row_ok = 0;
            for (int dx = 0; dx < size; dx++)
                for (int dy = 0; dy < size; dy++)
                    if (col_static_blocked(npc->x + dx, npc->y + dy)) row_ok = 0;
        }
        CHECK("reinforcement set spawned (minotaur + shaman)", count == 2);
        CHECK("reinforcements land inside the gate gap x 15-18", in_gap_ok);
        CHECK(north ? "player y=16 -> north gate row (yellow line, not nearest)"
                    : "player y=15 -> south gate row", row_ok);
    }
}

/* 3d2. A29 roster cap: the largest initial spawn set (wave 8's 7 scripted NPCs +
   Quartet + Dynamic Duo pair = 9) places everyone — the spawn asserts replace
   the old silent drop, so reaching 9 actives IS the regression check. */
static void test_roster_cap_nine(void) {
    printf("test_roster_cap_nine\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 47);
    s.modifiers.active_mask |= (1u << COLO_MOD_QUARTET) | (1u << COLO_MOD_DYNAMIC_DUO);
    s.modifiers.tier[COLO_MOD_QUARTET] = 1;
    s.modifiers.tier[COLO_MOD_DYNAMIC_DUO] = 1;
    s.wave = 7;   /* wave 8: BZ AR SE JV JV MC SW */
    col_spawn_wave(&s);
    int count = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++) if (s.npcs[i].active) count++;
    CHECK("wave 8 + Quartet + Dynamic Duo spawns all 9 NPCs", count == 9);
}

/* 3e. wave 12: clamp + Sol placement, the Quartet warbander spawns reachable
   inside the interior, and Sol's death wins with the warbander still alive. */
static void test_wave12_quartet_and_win(void) {
    printf("test_wave12_quartet_and_win\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 11;
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 43);

    CHECK("boss-wave player start (16,10)",
        s.player.x == COLO_BOSS_PLAYER_START_X && s.player.y == COLO_BOSS_PLAYER_START_Y);
    CHECK("boss arena clamp is (9,9)-(24,24)",
        s.sol.boss_arena_min_x == 9 && s.sol.boss_arena_min_y == 9 &&
        s.sol.boss_arena_max_x == 24 && s.sol.boss_arena_max_y == 24);
    int sol_idx = col_sol_find_idx(&s);
    CHECK("Sol spawned at SW (16,19)", sol_idx >= 0 &&
        s.npcs[sol_idx].x == COLO_SOL_SPAWN_X && s.npcs[sol_idx].y == COLO_SOL_SPAWN_Y);

    int placement_ok = 1, reachable_ok = 1, win_ok = 1, survivor_ok = 1;
    for (int rep = 0; rep < 10; rep++) {
        memset(&s, 0, sizeof(s));
        col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 43 + (uint32_t)rep);
        s.modifiers.active_mask |= (1u << COLO_MOD_QUARTET);
        s.modifiers.tier[COLO_MOD_QUARTET] = 1;
        s.wave = COLO_WAVE_BOSS;
        col_spawn_wave(&s);

        int wb = -1;
        for (int i = 0; i < COLO_MAX_NPCS; i++)
            if (s.npcs[i].active && col_type_is_warbander(s.npcs[i].type)) wb = i;
        if (wb < 0) { placement_ok = 0; continue; }
        int wx = s.npcs[wb].x, wy = s.npcs[wb].y;
        int cheb_dx = abs(wx - s.player.x), cheb_dy = abs(wy - s.player.y);
        if (wx < 9 || wx > 24 || wy < 9 || wy > 24) placement_ok = 0;
        if (col_static_blocked(wx, wy)) placement_ok = 0;
        if ((cheb_dx > cheb_dy ? cheb_dx : cheb_dy) <= COLO_SPAWN_EXCLUSION_CHEB)
            placement_ok = 0;

        /* BFS from the player over clamped, unblocked, NPC-free tiles: the
           warbander must be attackable (some cardinal neighbor reachable). */
        int seen[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT] = {{0}};
        int qx[34 * 34], qy[34 * 34], head = 0, tail = 0;
        qx[tail] = s.player.x; qy[tail] = s.player.y; tail++;
        seen[s.player.x][s.player.y] = 1;
        int reached = 0;
        while (head < tail && !reached) {
            int cx = qx[head], cy = qy[head]; head++;
            static const int D[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
            for (int d = 0; d < 4; d++) {
                int nx = cx + D[d][0], ny = cy + D[d][1];
                if (nx == wx && ny == wy) { reached = 1; break; }
                if (nx < 9 || nx > 24 || ny < 9 || ny > 24) continue;
                if (seen[nx][ny] || col_static_blocked(nx, ny)) continue;
                int gx, gy;
                if (!col_grid_index(nx, ny, &gx, &gy)) continue;
                if (s.npc_collision_flags[gx][gy]) continue;
                seen[nx][ny] = 1;
                qx[tail] = nx; qy[tail] = ny; tail++;
            }
        }
        if (!reached) reachable_ok = 0;

        /* A6: killing Sol ends the run in victory with the warbander alive. */
        int sol = col_sol_find_idx(&s);
        if (sol < 0) { win_ok = 0; continue; }
        col_apply_npc_death(&s, sol);
        int idle[COLO_NUM_ACTION_HEADS] = {0};
        step_and_observe(&s, &ctx, idle);
        if (!(s.episode_over && s.winner == COLO_OUTCOME_PLAYER_WON)) win_ok = 0;
        if (!s.npcs[wb].active) survivor_ok = 0;
    }
    CHECK("Quartet warbander spawns on a walkable interior tile outside the exclusion",
        placement_ok);
    CHECK("Quartet warbander is pathable from the player", reachable_ok);
    CHECK("Sol's death wins the wave-12 run (A6)", win_ok);
    CHECK("the surviving warbander does not block the win", survivor_ok);
}

/* ---- 4. P2 warband rework + Red Flag routefinding -------------------------- */

static int wb_find_npc(const ColosseumState* s, ColoNpcType type) {
    for (int i = 0; i < COLO_MAX_NPCS; i++)
        if (s->npcs[i].active && s->npcs[i].type == type) return i;
    return -1;
}

/* deactivate every non-warbander so warband behavior runs in isolation. */
static void wb_isolate_warband(ColosseumState* s) {
    for (int i = 0; i < COLO_MAX_NPCS; i++)
        if (s->npcs[i].active && !col_type_is_warbander(s->npcs[i].type))
            col_deactivate_npc(s, i);
}

/* teleport an NPC with clean collision restamping (mirrors the lab move). */
static void wb_move_npc(ColosseumState* s, int slot, int x, int y) {
    int size = col_npc_effective_size(&s->npcs[slot]);
    col_stamp_npc_collision_footprint(s, s->npcs[slot].x, s->npcs[slot].y, size, 0);
    s->npcs[slot].x = x;
    s->npcs[slot].y = y;
    col_stamp_npc_collision_footprint(s, x, y, size, 1);
}

/* count of active warbanders flagged attacked_this_tick after a step. */
static int wb_attacks_this_tick(const ColosseumState* s, ColoNpcType type) {
    int n = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++)
        if (s->npcs[i].active && s->npcs[i].type == type &&
            s->npcs[i].attacked_this_tick) n++;
    return n;
}

/* 4a. B2(a): one shared cycle anchored to the wave's first actionable tick N —
   berserker lands only on ticks = N+1 (mod 6), seer N+2, archer N+3, and the
   first berserker window is exactly N+1 (not a full attack-speed timer later). */
static void test_warband_cycle_offsets(void) {
    printf("test_warband_cycle_offsets\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 0;
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 51);
    complete_open_draft(&s, &ctx, 1);   /* mandatory wave-1 draft (Blasphemy, inert here) */
    wb_isolate_warband(&s);

    /* park the trio on its formation tiles so every window is eligible. */
    wb_move_npc(&s, wb_find_npc(&s, COLO_FREMENNIK_BERSERKER), s.player.x, s.player.y + 1);
    wb_move_npc(&s, wb_find_npc(&s, COLO_FREMENNIK_SEER), s.player.x + 1, s.player.y);
    wb_move_npc(&s, wb_find_npc(&s, COLO_FREMENNIK_ARCHER), s.player.x - 1, s.player.y);

    int idle[COLO_NUM_ACTION_HEADS] = {0};
    int anchor = -1;
    int first_tick[3] = { -1, -1, -1 };   /* berserker, seer, archer */
    int count[3] = { 0, 0, 0 };
    int offsets_ok = 1;
    static const ColoNpcType SPECIES[3] = {
        COLO_FREMENNIK_BERSERKER, COLO_FREMENNIK_SEER, COLO_FREMENNIK_ARCHER };

    for (int t = 0; t < 46 && !s.episode_over; t++) {
        s.player.current_hitpoints = 9999;   /* survive the full standing cycle */
        step_and_observe(&s, &ctx, idle);
        if (anchor < 0) anchor = s.warband_cycle_anchor;
        for (int sp = 0; sp < 3; sp++) {
            if (!wb_attacks_this_tick(&s, SPECIES[sp])) continue;
            count[sp]++;
            if (first_tick[sp] < 0) first_tick[sp] = s.tick;
            if (anchor < 0 || (s.tick - anchor) % COLO_WARBAND_CYCLE_TICKS != sp + 1)
                offsets_ok = 0;
        }
    }
    CHECK("cycle anchored at the wave's first actionable tick", anchor >= 0);
    CHECK("standing player eats repeated full cycles (berserker)", count[0] >= 4);
    CHECK("standing player eats repeated full cycles (seer)", count[1] >= 4);
    CHECK("standing player eats repeated full cycles (archer)", count[2] >= 4);
    CHECK("berserker only lands on ticks = N+1 mod 6; seer +2; archer +3", offsets_ok);
    CHECK("first berserker window is exactly N+1 (wave-anchored, no spawn timer)",
        first_tick[0] == anchor + 1);
    CHECK("seer first window N+2", first_tick[1] == anchor + 2);
    CHECK("archer first window N+3", first_tick[2] == anchor + 3);
}

/* 4b. B2(b): the player moving on a window tick makes that member skip its whole
   cycle — a scripted stutter-step (walk south every tick) takes zero warband
   damage even with the trio glued to the player. */
static void test_warband_move_skip(void) {
    printf("test_warband_move_skip\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 0;
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 53);
    complete_open_draft(&s, &ctx, 1);   /* mandatory wave-1 draft (Blasphemy, inert here) */
    wb_isolate_warband(&s);
    wb_move_npc(&s, wb_find_npc(&s, COLO_FREMENNIK_BERSERKER), s.player.x, s.player.y + 1);
    wb_move_npc(&s, wb_find_npc(&s, COLO_FREMENNIK_SEER), s.player.x + 1, s.player.y);
    wb_move_npc(&s, wb_find_npc(&s, COLO_FREMENNIK_ARCHER), s.player.x - 1, s.player.y);

    int idle[COLO_NUM_ACTION_HEADS] = {0};
    int walk_south[COLO_NUM_ACTION_HEADS] = {0};
    walk_south[COLO_HEAD_MOVE] = 4;   /* walk (0,-1) */

    /* idle through the ready gap; the anchor arms on the first live tick (a
       windowless phase-0 tick, safe to stand on). */
    while (s.warband_cycle_anchor < 0 && !s.episode_over)
        step_and_observe(&s, &ctx, idle);

    int attacks = 0;
    int moved_every_tick = 1;
    for (int t = 0; t < 14 && !s.episode_over; t++) {
        s.player.current_hitpoints = 9999;
        step_and_observe(&s, &ctx, walk_south);
        if (!s.tick_scratch.player_moved) moved_every_tick = 0;
        for (int sp = 0; sp < COLO_MAX_NPCS; sp++)
            if (s.npcs[sp].active && col_type_is_warbander(s.npcs[sp].type) &&
                s.npcs[sp].attacked_this_tick) attacks++;
    }
    CHECK("the scripted stutter-step actually moved every tick", moved_every_tick);
    CHECK("warband fired zero attacks across the stutter-step run", attacks == 0);
    CHECK("zero warband damage across the stutter-step run",
        s.log.total_damage_received == 0.0f);
}

/* 4c. A5+D33: the melee-distance gate — an archer at range never attacks on its
   window even with clear LoS; cardinal contact attacks; diagonal contact does
   not (1x1 OSRS melee is cardinal-only). */
static void test_warband_melee_distance_gate(void) {
    printf("test_warband_melee_distance_gate\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 57);
    geo_clear_npcs(&s);
    s.player.x = 7; s.player.y = 18;
    s.tick_scratch.player_moved = 0;
    s.tick = 100;

    /* distance 5 along a clear row (LoS verified): the pre-A5 sim attacked here. */
    col_init_npc(&s, 0, COLO_FREMENNIK_ARCHER, 12, 18);
    CHECK("rig sanity: the ranged archer has clear LoS",
        col_npc_has_los_to_player(&s, &s.npcs[0]));
    s.warband_cycle_anchor = s.tick - 3;   /* phase 3 = archer window NOW */
    col_warband_attack_phase(&s);
    CHECK("archer at distance never attacks, even with LoS on its window",
        s.npcs[0].attacked_this_tick == 0);

    /* cardinal contact on the window attacks. */
    wb_move_npc(&s, 0, 8, 18);
    col_warband_attack_phase(&s);
    CHECK("cardinally adjacent archer attacks on its window",
        s.npcs[0].attacked_this_tick == 1);

    /* diagonal contact is not melee distance for a 1x1 warbander (D33). */
    s.npcs[0].attacked_this_tick = 0;
    wb_move_npc(&s, 0, 8, 19);
    col_warband_attack_phase(&s);
    CHECK("diagonally adjacent archer does not attack (cardinal-only)",
        s.npcs[0].attacked_this_tick == 0);
}

/* 4d. B2(c): formation convergence — the trio ends N/E/W of a stationary player,
   and a Quartet run completes the diamond with the extra member SOUTH. */
static void test_warband_formation_convergence(void) {
    printf("test_warband_formation_convergence\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 0;
    int idle[COLO_NUM_ACTION_HEADS] = {0};

    for (int quartet = 0; quartet <= 1; quartet++) {
        ColosseumState s;
        memset(&s, 0, sizeof(s));
        col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 59 + (uint32_t)quartet);
        complete_open_draft(&s, &ctx, 1);   /* mandatory wave-1 draft (Blasphemy) */
        if (quartet) {
            s.modifiers.active_mask |= (1u << COLO_MOD_QUARTET);
            s.modifiers.tier[COLO_MOD_QUARTET] = 1;
            s.wave = 0;
            col_spawn_wave(&s);
        }
        wb_isolate_warband(&s);

        for (int t = 0; t < 30 && !s.episode_over; t++) {
            s.player.current_hitpoints = 9999;
            step_and_observe(&s, &ctx, idle);
        }

        int formed_ok = 1, members = 0;
        for (int i = 0; i < COLO_MAX_NPCS; i++) {
            ColoNPC* npc = &s.npcs[i];
            if (!npc->active || !col_type_is_warbander(npc->type)) continue;
            members++;
            int dir = colo_npc_warband(npc)->formation_dir;
            int ex = s.player.x + COLO_WARBAND_FORM_OFFSET[dir][0];
            int ey = s.player.y + COLO_WARBAND_FORM_OFFSET[dir][1];
            if (npc->x != ex || npc->y != ey) formed_ok = 0;
        }
        if (quartet) {
            CHECK("Quartet diamond: 4 members each on their N/E/W/S slot",
                formed_ok && members == 4);
        } else {
            CHECK("trio converges to exactly N/E/W of a stationary player",
                formed_ok && members == 3);
        }
    }
}

/* 4e. B2(e)+D3: a warbander covers 2 tiles in a single movement tick. */
static void test_warband_two_tile_speed(void) {
    printf("test_warband_two_tile_speed\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 61);
    geo_clear_npcs(&s);
    s.player.x = 7; s.player.y = 18;

    col_init_npc(&s, 0, COLO_FREMENNIK_ARCHER, 20, 18);
    col_npc_move_ctx(&s, &ctx, 0);
    CHECK("warbander closes 2 tiles in one tick on open ground",
        s.npcs[0].x == 18 && s.npcs[0].y == 18 && s.npcs[0].moved_this_tick == 1);
    col_npc_move_ctx(&s, &ctx, 0);
    CHECK("second tick closes 2 more", s.npcs[0].x == 16 && s.npcs[0].y == 18);
}

/* 4f. B2(e): warband routefinding rounds a pillar to reach the player while the
   greedy serpent shaman wedges against it — the pillar-safespot asymmetry. */
static void test_warband_pillar_routefind_vs_shaman_safespot(void) {
    printf("test_warband_pillar_routefind_vs_shaman_safespot\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    int idle[COLO_NUM_ACTION_HEADS] = {0};

    /* player flush behind the SW pillar (8..10, 8..10) on row 9; the chaser
       starts due east so the greedy x-pull has no y component to slide on. */
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 67);
    complete_open_draft(&s, &ctx, 1);   /* mandatory wave-1 draft (Blasphemy) */
    geo_clear_npcs(&s);
    s.player.x = 7; s.player.y = 9;
    col_rebuild_player_collision_flags(&s);

    col_init_npc(&s, 0, COLO_FREMENNIK_ARCHER, 13, 9);
    int adjacent_by = -1;
    int archer_attacks = 0;
    for (int t = 0; t < 40 && !s.episode_over; t++) {
        s.player.current_hitpoints = 9999;
        step_and_observe(&s, &ctx, idle);
        int dx = abs(s.npcs[0].x - s.player.x), dy = abs(s.npcs[0].y - s.player.y);
        if (dx + dy == 1 && adjacent_by < 0) adjacent_by = t;
        archer_attacks += wb_attacks_this_tick(&s, COLO_FREMENNIK_ARCHER);
    }
    CHECK("archer routefinds around the pillar into melee contact",
        adjacent_by >= 0 && adjacent_by <= 14);
    CHECK("the routefinding archer then lands cycle attacks", archer_attacks > 0);

    /* same spot, greedy shaman: wedges on the pillar face with no LoS, never
       attacks — the shaman is MEANT to be pillar-safespottable. */
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 67);
    complete_open_draft(&s, &ctx, 1);
    geo_clear_npcs(&s);
    s.player.x = 7; s.player.y = 9;
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_SERPENT_SHAMAN, 13, 9);
    int shaman_attacks = 0;
    for (int t = 0; t < 40 && !s.episode_over; t++) {
        s.player.current_hitpoints = 9999;
        step_and_observe(&s, &ctx, idle);
        shaman_attacks += s.npcs[0].attacked_this_tick;
    }
    CHECK("greedy shaman wedges against the pillar (safespot holds)",
        s.npcs[0].x == 11 && s.npcs[0].y == 9);
    CHECK("safespotted shaman never attacks", shaman_attacks == 0);
}

/* 4g. A30: the Red Flag minotaur routefinds a pillar-safespotted player; the
   plain minotaur stays wedged (safespottable by design). */
static void test_red_flag_minotaur_routefind(void) {
    printf("test_red_flag_minotaur_routefind\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);

    for (int red_flag = 0; red_flag <= 1; red_flag++) {
        ColosseumState s;
        memset(&s, 0, sizeof(s));
        col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 71);
        geo_clear_npcs(&s);
        if (red_flag) {
            s.modifiers.active_mask |= (1u << COLO_MOD_RED_FLAG);
            s.modifiers.tier[COLO_MOD_RED_FLAG] = 1;
        }
        s.player.x = 7; s.player.y = 9;
        col_rebuild_player_collision_flags(&s);

        /* 3x3 footprint (11..13, 9..11), pure-west pull into the pillar face. */
        col_init_npc(&s, 0, COLO_MINOTAUR, 11, 9);
        int min_dist = 99;
        for (int t = 0; t < 40; t++) {
            col_npc_move_ctx(&s, &ctx, 0);
            int d = encounter_dist_to_npc(
                s.player.x, s.player.y, s.npcs[0].x, s.npcs[0].y, 3);
            if (d < min_dist) min_dist = d;
        }
        if (red_flag) {
            CHECK("Red Flag minotaur routefinds into melee contact", min_dist == 1);
        } else {
            CHECK("plain minotaur stays wedged on the pillar (safespot holds)",
                s.npcs[0].x == 11 && s.npcs[0].y == 9 && min_dist == 4);
        }
    }
}

/* ---- 5. P3 NPC mechanic fixes ---------------------------------------------- */

/* 5a. A13+D9: on its 5-tick action timer the minotaur heals exactly ONE eligible
   ally (non-minotaur, below 75% max HP, centre <=7 tiles, centre-to-centre LoS)
   TO FULL — lowest HP-fraction first — never through a pillar, and a player in
   melee distance preempts the heal with the 1-tick-delayed crush. */
static void test_minotaur_heal_semantics(void) {
    printf("test_minotaur_heal_semantics\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 73);

    /* one action, four wounded candidates: only the lowest-fraction eligible
       shaman heals to full; the above-75% shaman and the minotaur ally never do. */
    geo_clear_npcs(&s);
    s.player.x = 30; s.player.y = 18;   /* far outside melee distance */
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_MINOTAUR, 12, 12);        /* healer, centre (13,13) */
    col_init_npc(&s, 1, COLO_SERPENT_SHAMAN, 16, 13);  /* 62/125 ~ 50%, dist 3 */
    col_init_npc(&s, 2, COLO_SERPENT_SHAMAN, 17, 17);  /* 30/125 = 24%, dist 4 */
    col_init_npc(&s, 3, COLO_SERPENT_SHAMAN, 13, 17);  /* 100/125 = 80%, dist 4 */
    col_init_npc(&s, 4, COLO_MINOTAUR, 18, 12);        /* wounded minotaur, dist 6 */
    s.npcs[1].hp = 62;
    s.npcs[2].hp = 30;
    s.npcs[3].hp = 100;
    s.npcs[4].hp = 100;
    s.npcs[0].attack_timer = 0;
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("the lowest-HP-fraction eligible ally heals to FULL", s.npcs[2].hp == 125);
    CHECK("exactly one ally healed per action", s.npcs[1].hp == 62);
    CHECK("an ally at/above 75% max HP is not eligible", s.npcs[3].hp == 100);
    CHECK("another minotaur is never healed", s.npcs[4].hp == 100);
    CHECK("healing is not an attack", s.npcs[0].attacked_this_tick == 0);
    CHECK("the heal action re-arms the 5-tick timer (D9)",
        s.npcs[0].attack_timer == COLO_NPC_STATS[COLO_MINOTAUR].attack_speed);

    /* range edge: centre-to-centre 7 heals, 8 does not. */
    geo_clear_npcs(&s);
    col_init_npc(&s, 0, COLO_MINOTAUR, 12, 12);        /* centre (13,13) */
    col_init_npc(&s, 1, COLO_SERPENT_SHAMAN, 20, 13);  /* centre dist exactly 7 */
    s.npcs[1].hp = 30;
    s.npcs[0].attack_timer = 0;
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("centre distance 7 is in heal reach", s.npcs[1].hp == 125);
    geo_clear_npcs(&s);
    col_init_npc(&s, 0, COLO_MINOTAUR, 12, 12);
    col_init_npc(&s, 1, COLO_SERPENT_SHAMAN, 21, 13);  /* centre dist 8 */
    s.npcs[1].hp = 30;
    s.npcs[0].attack_timer = 0;
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("centre distance 8 is out of heal reach", s.npcs[1].hp == 30);

    /* pillar LoS: the lower-fraction ally behind the SW pillar is skipped; the
       clear-LoS ally heals instead. */
    geo_clear_npcs(&s);
    s.player.x = 5; s.player.y = 14;
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_MINOTAUR, 4, 8);          /* centre (5,9), W of pillar */
    col_init_npc(&s, 1, COLO_SERPENT_SHAMAN, 12, 9);   /* 10/125, ray crosses pillar */
    col_init_npc(&s, 2, COLO_SERPENT_SHAMAN, 5, 16);   /* 60/125, clear column ray */
    s.npcs[1].hp = 10;
    s.npcs[2].hp = 60;
    s.npcs[0].attack_timer = 0;
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("pillars block the heal (blocked ally skipped)", s.npcs[1].hp == 10);
    CHECK("the clear-LoS ally heals instead", s.npcs[2].hp == 125);

    /* melee priority: a (diagonally) adjacent player preempts the heal, and the
       crush is queued with the confirmed 1-tick delay (tick-eatable). */
    geo_clear_npcs(&s);
    s.player.x = 11; s.player.y = 11;   /* diagonal corner contact, rect dist 1 */
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_MINOTAUR, 12, 12);
    col_init_npc(&s, 1, COLO_SERPENT_SHAMAN, 16, 13);
    s.npcs[1].hp = 30;
    s.npcs[0].attack_timer = 0;
    s.player.current_hitpoints = 99;
    int queue_before = s.player_pending_hits.count;
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("player in melee distance: the minotaur attacks instead of healing",
        s.npcs[0].attacked_this_tick == 1 && s.npcs[1].hp == 30);
    CHECK("the crush is 1-tick-delayed (queued, no damage this tick)",
        s.player_pending_hits.count == queue_before + 1 &&
        s.player.current_hitpoints == 99);
}

/* 5b. A19: barrage-to-barrage period is exactly 10 ticks — 3 orbs on consecutive
   ticks + a 7-tick recharge — measured over 3+ consecutive barrages (the old
   fresh-10 reset after orb 3 gave 12). */
static void test_manticore_barrage_period(void) {
    printf("test_manticore_barrage_period\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 79);
    geo_clear_npcs(&s);
    s.player.x = 17; s.player.y = 16;
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_MANTICORE, 16, 12);   /* dist 2, clear LoS */
    s.npcs[0].attack_timer = 2;

    ColoManticoreState* mc = colo_npc_manticore(&s.npcs[0]);
    int starts[8];
    int nstarts = 0;
    for (int t = 0; t < 36; t++) {
        s.player.current_hitpoints = 99;   /* orbs land inline now; stay alive */
        int prev = mc->cycle_step;
        col_npc_attack_ctx(&s, &ctx, 0);
        if (prev < 0 && mc->cycle_step >= 0 && nstarts < 8) starts[nstarts++] = t;
    }
    CHECK("4 barrage starts inside 36 ticks", nstarts == 4);
    int period_ok = nstarts >= 4;
    for (int b = 1; b < nstarts; b++)
        if (starts[b] - starts[b - 1] != 10) period_ok = 0;
    CHECK("barrage-to-barrage period is exactly 10 ticks across 3 gaps", period_ok);
}

/* 5b-bis. The barrage pattern is decided + telegraphed during the charge-up, not
   at fire time: while the manticore is still charging (cycle_step < 0, timer > 0)
   the full 3-orb sequence is locked and stable, so a player (and the obs) can see
   the orbs coming. Pre-praying the telegraphed orb 0 blocks it on its fire tick —
   the capability this change exists to enable. */
static void test_manticore_telegraph_during_windup(void) {
    printf("test_manticore_telegraph_during_windup\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 131);
    geo_clear_npcs(&s);
    s.player.x = 17; s.player.y = 16;
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_MANTICORE, 16, 12);   /* dist 2, clear LoS */
    s.npcs[0].attack_timer = 6;                     /* a 6-tick charge ahead of orb 0 */
    ColoManticoreState* mc = colo_npc_manticore(&s.npcs[0]);

    /* one idle tick arms the manticore: the full pattern locks while it is still
       charging (cycle_step < 0, attack_timer > 0) — visible before any orb lands. */
    s.player.current_hitpoints = 99;
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("manticore arms during the charge-up (still idle)", mc->cycle_step < 0);
    CHECK("the full barrage pattern locks before any orb fires",
        mc->orb_style[0] != ATTACK_STYLE_NONE &&
        mc->orb_style[1] != ATTACK_STYLE_NONE &&
        mc->orb_style[2] != ATTACK_STYLE_NONE);
    CHECK("orb 2 is melee (the range+magic pair leads, melee last)",
        mc->orb_style[2] == ATTACK_STYLE_MELEE);

    AttackStyle locked0 = mc->orb_style[0];
    for (int t = 0; t < 4 && mc->cycle_step < 0; t++) {
        s.player.current_hitpoints = 99;
        col_npc_attack_ctx(&s, &ctx, 0);
    }
    CHECK("the charge pattern stays stable until orb 0 fires (no per-tick re-roll)",
        mc->orb_style[0] == locked0);

    /* pre-praying the telegraphed orb 0 blocks it on the first fire tick. */
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 131);
    geo_clear_npcs(&s);
    s.player.x = 17; s.player.y = 16;
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_MANTICORE, 16, 12);
    s.npcs[0].attack_timer = 3;
    mc = colo_npc_manticore(&s.npcs[0]);
    int orb0_blocked = 0;
    for (int t = 0; t < 12; t++) {
        if (mc->cycle_step < 0 && mc->orb_style[0] != ATTACK_STYLE_NONE) {
            AttackStyle s0 = mc->orb_style[0];
            s.player.prayer = s0 == ATTACK_STYLE_MAGIC ? PRAYER_PROTECT_MAGIC :
                              s0 == ATTACK_STYLE_RANGED ? PRAYER_PROTECT_RANGED :
                              PRAYER_PROTECT_MELEE;
        }
        s.player.current_hitpoints = 99;
        int step_before = mc->cycle_step;
        col_npc_attack_ctx(&s, &ctx, 0);
        if (step_before < 0 && mc->cycle_step == 1) {
            orb0_blocked = (s.player.current_hitpoints == 99);
            break;
        }
    }
    CHECK("a pre-prayed telegraphed orb 0 is blocked on its fire tick", orb0_blocked);
}

/* 5c. D12: orbs land ON their launch tick — nothing enters the pending queue,
   damage applies on the fire tick, and the prayer set that tick (pretick runs
   before NPC actions) blocks the orb. Step-loop layer: flicking the telegraphed
   next-orb style each tick blocks orbs 1 and 2 of every barrage. */
static void test_manticore_orb_same_tick_flick(void) {
    printf("test_manticore_orb_same_tick_flick\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 83);
    geo_clear_npcs(&s);
    s.player.x = 17; s.player.y = 16;
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_MANTICORE, 16, 12);
    ColoManticoreState* mc = colo_npc_manticore(&s.npcs[0]);

    /* prayed orb: zero damage, zero queue entries, prayer_correct counted. */
    int blocked_ok = 1, queued = 0, prayer_counted = 0;
    for (int rep = 0; rep < 16; rep++) {
        mc->cycle_step = 1;                       /* mid-barrage: next orb = 1 */
        mc->orb_style[1] = ATTACK_STYLE_MAGIC;
        s.player.prayer = PRAYER_PROTECT_MAGIC;   /* flicked on the fire tick */
        s.player.current_hitpoints = 99;
        int pc_before = s.tick_scratch.prayer_correct;
        col_npc_attack_ctx(&s, &ctx, 0);
        if (s.player.current_hitpoints != 99) blocked_ok = 0;
        if (s.tick_scratch.prayer_correct > pc_before) prayer_counted = 1;
        queued += s.player_pending_hits.count;
    }
    CHECK("praying the orb's style on its fire tick blocks it", blocked_ok);
    CHECK("orbs never enter the pending queue (travel time 0)", queued == 0);
    CHECK("a blocked orb still counts prayer_correct", prayer_counted);

    /* unprayed orb: damage lands on the very same call (Relentless III forces
       the accuracy roll so only the 1/32 zero-roll can blank a given orb). */
    s.modifiers.active_mask |= (1u << COLO_MOD_RELENTLESS);
    s.modifiers.tier[COLO_MOD_RELENTLESS] = 3;
    int same_tick_damage = 0;
    for (int rep = 0; rep < 32 && !same_tick_damage; rep++) {
        mc->cycle_step = 1;
        mc->orb_style[1] = ATTACK_STYLE_MAGIC;
        s.player.prayer = PRAYER_PROTECT_RANGED;   /* wrong prayer */
        s.player.current_hitpoints = 99;
        col_npc_attack_ctx(&s, &ctx, 0);
        if (s.player.current_hitpoints < 99) same_tick_damage = 1;
    }
    CHECK("an unprayed orb damages the player on the fire tick", same_tick_damage);
    CHECK("nothing was queued for a later landing", s.player_pending_hits.count == 0);

    /* step loop: pray the telegraphed next-orb style each tick. cycle_step from
       the prior tick names the orb that fires next, so orbs 1 and 2 are always
       blockable; only orb 0's 50/50 lead can connect. */
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 83);
    complete_open_draft(&s, &ctx, 1);   /* mandatory wave-1 draft (Blasphemy) */
    geo_clear_npcs(&s);
    s.player.x = 17; s.player.y = 16;
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_MANTICORE, 16, 12);
    s.npcs[0].attack_timer = 4;
    mc = colo_npc_manticore(&s.npcs[0]);

    int actions[COLO_NUM_ACTION_HEADS] = {0};
    int protected_ticks = 0, flicked_damage = 0;
    for (int t = 0; t < 40 && !s.episode_over; t++) {
        int expect_orb = mc->cycle_step >= 0 && mc->cycle_step < 3;
        if (expect_orb) {
            AttackStyle next = mc->orb_style[mc->cycle_step];
            actions[COLO_HEAD_PRAYER] =
                next == ATTACK_STYLE_MAGIC ? ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC :
                next == ATTACK_STYLE_RANGED ? ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED :
                ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE;
        } else {
            actions[COLO_HEAD_PRAYER] = ENCOUNTER_OVERHEAD_NO_CHANGE;
        }
        int hp_before = s.player.current_hitpoints;
        step_and_observe(&s, &ctx, actions);
        if (expect_orb) {
            protected_ticks++;
            if (s.player.current_hitpoints < hp_before) flicked_damage = 1;
        }
        s.player.current_hitpoints = 99;
        s.player.current_prayer = 99;
    }
    CHECK("multiple telegraphed orb ticks observed through the step loop",
        protected_ticks >= 6);
    CHECK("flicking the telegraphed style each fire tick blocks every such orb",
        !flicked_damage);
}

/* 5d. B10: from wave 9 a freshly drawn orb pattern is copied by the other
   manticore when within 15 tiles (centre-to-centre) with LoS; out-of-LoS,
   out-of-range, and pre-wave-9 peers do not copy. The copy is consumed at the
   peer's own barrage start without re-propagating, and the 5-tick stagger is
   unchanged. */
static void test_manticore_pattern_copy(void) {
    printf("test_manticore_pattern_copy\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 89);

    /* wave 9, in range + LoS: the idle peer copies A's fresh draw. */
    geo_clear_npcs(&s);
    s.wave = 8;   /* wave 9, 0-based */
    s.player.x = 13; s.player.y = 12;
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_MANTICORE, 12, 16);   /* A: centre (13,17) */
    col_init_npc(&s, 1, COLO_MANTICORE, 18, 16);   /* B: centre (19,17), dist 6 */
    ColoManticoreState* amc = colo_npc_manticore(&s.npcs[0]);
    ColoManticoreState* bmc = colo_npc_manticore(&s.npcs[1]);
    s.npcs[0].attack_timer = 0;
    s.npcs[1].attack_timer = 30;
    s.player.current_hitpoints = 99;
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("A committed and fired orb 0", amc->cycle_step == 1);
    CHECK("the in-LoS peer copied the fresh pattern",
        bmc->pattern_copied == 1 &&
        bmc->orb_style[0] == amc->orb_style[0] &&
        bmc->orb_style[1] == amc->orb_style[1] &&
        bmc->orb_style[2] == amc->orb_style[2]);

    /* stagger unchanged: a ready B mid-A-barrage delays 5 ticks, copy intact. */
    s.npcs[1].attack_timer = 0;
    col_npc_attack_ctx(&s, &ctx, 1);
    CHECK("a ready peer still staggers 5 ticks during A's barrage",
        s.npcs[1].attack_timer == COLO_MANTICORE_STAGGER_TICKS &&
        bmc->cycle_step == -1 && bmc->pattern_copied == 1);

    /* B consumes the copy at its barrage start; a consumed copy does not
       re-propagate back onto the now-idle A (no echo-lock). */
    AttackStyle a0 = amc->orb_style[0], a1 = amc->orb_style[1], a2 = amc->orb_style[2];
    s.player.current_hitpoints = 99;
    col_npc_attack_ctx(&s, &ctx, 0);   /* A orb 1 */
    s.player.current_hitpoints = 99;
    col_npc_attack_ctx(&s, &ctx, 0);   /* A orb 2 -> idle */
    s.npcs[1].attack_timer = 0;
    s.player.current_hitpoints = 99;
    col_npc_attack_ctx(&s, &ctx, 1);   /* B starts on the copied pattern */
    CHECK("B fired the copied pattern (consumed, not re-rolled)",
        bmc->cycle_step == 1 && bmc->pattern_copied == 0 &&
        bmc->orb_style[0] == a0 && bmc->orb_style[1] == a1 && bmc->orb_style[2] == a2);
    CHECK("a consumed copy does not re-propagate to the idle peer",
        amc->pattern_copied == 0);

    /* wave gate: identical rig on wave 8 (index 7) never copies. */
    geo_clear_npcs(&s);
    s.wave = 7;
    col_init_npc(&s, 0, COLO_MANTICORE, 12, 16);
    col_init_npc(&s, 1, COLO_MANTICORE, 18, 16);
    bmc = colo_npc_manticore(&s.npcs[1]);
    s.npcs[0].attack_timer = 0;
    s.npcs[1].attack_timer = 30;
    s.player.current_hitpoints = 99;
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("before wave 9 the pattern is never copied", bmc->pattern_copied == 0);

    /* pillar LoS: a peer behind the SW pillar does not copy. */
    geo_clear_npcs(&s);
    s.wave = 8;
    s.player.x = 5; s.player.y = 14;
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_MANTICORE, 4, 8);     /* A: centre (5,9), W of pillar */
    col_init_npc(&s, 1, COLO_MANTICORE, 11, 8);    /* B: centre (12,9), E of pillar */
    bmc = colo_npc_manticore(&s.npcs[1]);
    s.npcs[0].attack_timer = 0;
    s.npcs[1].attack_timer = 30;
    s.player.current_hitpoints = 99;
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("a peer with the pillar in the centre ray does not copy",
        bmc->pattern_copied == 0);

    /* range: clear LoS but >15 tiles apart does not copy. */
    geo_clear_npcs(&s);
    s.wave = 8;
    s.player.x = 5; s.player.y = 12;
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_MANTICORE, 4, 15);    /* A: centre (5,16) */
    col_init_npc(&s, 1, COLO_MANTICORE, 26, 15);   /* B: centre (27,16), dist 22 */
    bmc = colo_npc_manticore(&s.npcs[1]);
    s.npcs[0].attack_timer = 0;
    s.npcs[1].attack_timer = 30;
    s.player.current_hitpoints = 99;
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("a peer beyond 15 tiles does not copy", bmc->pattern_copied == 0);
}

/* 5e. D7: the javelin skyfall has no accuracy/defence gate — marks carry the raw
   uniform 0..48 roll (the removed roll zeroed ~half vs this maxed player), land
   on the marked tile through Protect-from-Missiles, and miss entirely off-tile.
   Cadence: every 5th attack, D6 3-tick delay. */
static void test_javelin_skyfall_no_defence_gate(void) {
    printf("test_javelin_skyfall_no_defence_gate\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 97);
    geo_clear_npcs(&s);
    s.player.x = 17; s.player.y = 16;
    col_rebuild_player_collision_flags(&s);
    col_init_npc(&s, 0, COLO_JAVELIN_COLOSSUS, 16, 12);
    ColoJavelinState* jv = colo_npc_javelin(&s.npcs[0]);
    const ColoNpcStats* stats = &COLO_NPC_STATS[COLO_JAVELIN_COLOSSUS];

    /* cadence: throws 1-4 queue normal (prayable) projectiles, the 5th marks. */
    int queue_before = s.player_pending_hits.count;
    for (int a = 0; a < 4; a++) col_npc_attack_javelin(&s, 0, stats);
    CHECK("attacks 1-4 are normal queued throws",
        s.player_pending_hits.count == queue_before + 4 && jv->skyfall_pending == 0);
    col_npc_attack_javelin(&s, 0, stats);
    CHECK("the 5th attack marks the player's tile with the D6 3-tick delay",
        jv->skyfall_pending == 1 && jv->skyfall_timer == COLO_JAVELIN_SKYFALL_DELAY &&
        jv->skyfall_tile_x == s.player.x && jv->skyfall_tile_y == s.player.y);

    /* no defence gate: raw uniform damage rolls vs a maxed-defence player. */
    int nonzero = 0, in_range_ok = 1, high_roll = 0;
    for (int rep = 0; rep < 300; rep++) {
        jv->attack_count = 4;
        jv->skyfall_pending = 0;
        col_npc_attack_javelin(&s, 0, stats);
        if (jv->skyfall_damage > 0) nonzero++;
        if (jv->skyfall_damage < 0 || jv->skyfall_damage > stats->max_hit) in_range_ok = 0;
        if (jv->skyfall_damage >= 45) high_roll = 1;
    }
    CHECK("skyfall damage ignores defence (>=80% of 300 marks nonzero)", nonzero >= 240);
    CHECK("rolls span the raw 0..48 band up to the top", in_range_ok && high_roll);

    /* PfM ignored on-tile; stepping off dodges fully. */
    s.player.prayer = PRAYER_PROTECT_RANGED;
    s.player.current_hitpoints = 99;
    jv->skyfall_pending = 1;
    jv->skyfall_timer = 1;
    jv->skyfall_damage = 37;
    jv->skyfall_tile_x = s.player.x;
    jv->skyfall_tile_y = s.player.y;
    col_npc_resolve_javelin_skyfall(&s, 0);
    CHECK("on the marked tile the skyfall lands through Protect-from-Missiles",
        s.player.current_hitpoints == 99 - 37 && jv->skyfall_pending == 0);

    s.player.current_hitpoints = 99;
    jv->skyfall_pending = 1;
    jv->skyfall_timer = 1;
    jv->skyfall_damage = 37;
    jv->skyfall_tile_x = s.player.x + 1;
    jv->skyfall_tile_y = s.player.y;
    col_npc_resolve_javelin_skyfall(&s, 0);
    CHECK("off the marked tile the skyfall misses entirely",
        s.player.current_hitpoints == 99 && jv->skyfall_pending == 0);
}

/* ---- 6. P4 Sol Heredit overhaul --------------------------------------------- */

/* start a clean wave-12 fight via the step loop, run out the ready delay, and
   return Sol's NPC slot. */
static int sol_setup(ColosseumState* s, ColosseumContext* ctx, uint32_t seed) {
    col_init_context_typed(ctx);
    ctx->config.start_wave = 11;
    memset(s, 0, sizeof(*s));
    col_reset_ctx((EncounterState*)s, (EncounterContext*)ctx, seed);
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    while (s->wave_ready_delay > 0) step_and_observe(s, ctx, idle);
    return col_sol_find_idx(s);
}

/** start a clean wave-12 fight with the speedrun profile pinned. */
static int sol_setup_speedrun(ColosseumState* s, ColosseumContext* ctx, uint32_t seed) {
    col_init_context_typed(ctx);
    ctx->config.start_wave = 11;
    ctx->config.loadout_profile_mode = COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY;
    memset(s, 0, sizeof(*s));
    col_reset_ctx((EncounterState*)s, (EncounterContext*)ctx, seed);
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    while (s->wave_ready_delay > 0) step_and_observe(s, ctx, idle);
    return col_sol_find_idx(s);
}

/* teleport the player with clean interaction + collision state. */
static void sol_move_player(ColosseumState* s, int x, int y) {
    s->player.x = x;
    s->player.y = y;
    s->player_dest_x = -1;
    s->player_dest_y = -1;
    osrs_interaction_clear(&s->interaction);
    col_rebuild_player_collision_flags(s);
}

static int sol_count_active_beams(const ColosseumState* s) {
    int n = 0;
    for (int b = 0; b < COLO_SOL_BEAM_MAX; b++)
        if (s->sol.beams[b].active) n++;
    return n;
}

/** Clear Sol's molten-sand layer when a test has already asserted transition
    hazards and needs to isolate an unrelated boss subsystem. */
static void sol_clear_beams_and_sand(ColosseumState* s) {
    memset(s->sol.beams, 0, sizeof(s->sol.beams));
    s->sol.hazard_tile_count = 0;
}

/** Validate E6 molten-sand placement after phase beams have converted. */
static int sol_phase_sand_invariants_hold(const ColosseumState* s, int expected_count) {
    if (s->sol.hazard_tile_count != expected_count) return 0;
    int player_tile_seen = 0;
    for (int i = 0; i < s->sol.hazard_tile_count; i++) {
        int x = s->sol.hazard_tile_x[i];
        int y = s->sol.hazard_tile_y[i];
        if (!col_in_boss_arena(s, x, y)) return 0;
        if (col_static_blocked(x, y)) return 0;
        if (x == s->player.x && y == s->player.y) player_tile_seen = 1;
        for (int j = 0; j < i; j++)
            if (x == s->sol.hazard_tile_x[j] && y == s->sol.hazard_tile_y[j])
                return 0;
    }
    return player_tile_seen;
}

/* park Sol at (x, y) and silence his engine + movement: geometry rigs need a
   pinned hazard anchor (Sol takes one live chase step during sol_setup). */
static void sol_pin(ColosseumState* s, int idx, int x, int y) {
    wb_move_npc(s, idx, x, y);
    s->sol.attack_delay = 30000;
    s->sol.immobile_ticks = 30000;
}

/* 6a. A2: Sol never initiates without start-of-tick adjacency, the fight opens
   with a forced spear, a stationary player eats the exact 7-tick spear delay
   (A1+D28), and kiting on the cooldown measurably delays the next attack. */
static void test_sol_adjacency_gate_and_kiting(void) {
    printf("test_sol_adjacency_gate_and_kiting\n");
    ColosseumContext ctx;
    ColosseumState s;
    int idx = sol_setup(&s, &ctx, 101);
    CHECK("Sol spawned on wave 12", idx >= 0);

    int idle[COLO_NUM_ACTION_HEADS] = {0};
    int moved = 0;
    int first_attack_tick = -1;
    int dist_at_first_attack = -1;
    for (int t = 0; t < 30 && first_attack_tick < 0; t++) {
        s.player.current_hitpoints = 99;
        int pre_dist = encounter_dist_to_npc(
            s.player.x, s.player.y, s.npcs[idx].x, s.npcs[idx].y, 5);
        step_and_observe(&s, &ctx, idle);
        if (s.npcs[idx].moved_this_tick) moved++;
        if (s.npcs[idx].attacked_this_tick) {
            first_attack_tick = s.tick;
            dist_at_first_attack = pre_dist;
        }
    }
    CHECK("Sol chases the player across the arena", moved >= 5);
    CHECK("Sol initiates only when adjacent at the start of a tick",
        first_attack_tick > 0 && dist_at_first_attack == 1);
    CHECK("the fight opener is a forced Spear (variant 1)",
        s.sol.last_attack_kind == COLO_SOL_ATTACK_SPEAR &&
        s.sol.aoe_attack == COLO_SOL_AOE_SPEAR1);

    /* stationary player: the next attack initiates exactly 7 ticks later. */
    int second_attack_tick = -1;
    for (int t = 0; t < 12 && second_attack_tick < 0; t++) {
        s.player.current_hitpoints = 99;
        step_and_observe(&s, &ctx, idle);
        if (s.npcs[idx].attacked_this_tick) second_attack_tick = s.tick;
    }
    CHECK("a stationary player eats the next attack exactly 7 ticks later",
        second_attack_tick == first_attack_tick + COLO_SOL_SPEAR_DELAY);

    /* kiting: walking away (east along the open row) during the cooldown
       pushes the third attack well past another per-attack delay. */
    int walk_east[COLO_NUM_ACTION_HEADS] = {0};
    walk_east[COLO_HEAD_MOVE] = 7;
    int third_attack_tick = -1;
    for (int t = 0; t < 40 && third_attack_tick < 0; t++) {
        s.player.current_hitpoints = 99;
        step_and_observe(&s, &ctx, t < 8 ? walk_east : idle);
        if (s.npcs[idx].attacked_this_tick) third_attack_tick = s.tick;
    }
    CHECK("kiting on the cooldown delays the next attack beyond its delay",
        third_attack_tick > second_attack_tick + COLO_SOL_SPEAR_DELAY);
}

/* 6b. A1: selection invariants — forced draws are spears, specials need 2
   normals since the last special (held across 100 draws), no specials above
   90%, and variants alternate 1/2/1 with type switches resetting to 1. A
   step-loop coda checks a real transition freezes Sol and forces a spear with
   its phase hazards (beams + crystal). */
static void test_sol_attack_selection_invariants(void) {
    printf("test_sol_attack_selection_invariants\n");
    ColosseumContext ctx;
    ColosseumState s;
    int idx = sol_setup(&s, &ctx, 103);
    CHECK("the fight opens with the forced-spear flag armed", s.sol.force_spear == 1);
    CHECK("a forced draw is a Spear", col_sol_select_attack(&s) == COLO_SOL_ATTACK_SPEAR);

    s.sol.phase = 3;   /* triple-long + grapple both in the pool */
    s.sol.special_cooldown = 0;
    int normals_since_special = 99;
    int specials = 0, normals = 0, violations = 0;
    for (int n = 0; n < 100; n++) {
        int kind = col_sol_select_attack(&s);
        if (kind == COLO_SOL_ATTACK_TRIPLE || kind == COLO_SOL_ATTACK_GRAPPLE) {
            if (normals_since_special < COLO_SOL_SPECIAL_COOLDOWN) violations++;
            specials++;
            normals_since_special = 0;
        } else {
            normals++;
            normals_since_special++;
        }
    }
    CHECK("specials appear in the 100-draw mix", specials > 0);
    CHECK("the double-weighted normals dominate", normals > specials);
    CHECK("every special has >= 2 normals since the previous special", violations == 0);

    s.sol.phase = 0;
    s.sol.special_cooldown = 0;
    int early_specials = 0;
    for (int n = 0; n < 50; n++) {
        int kind = col_sol_select_attack(&s);
        if (kind != COLO_SOL_ATTACK_SPEAR && kind != COLO_SOL_ATTACK_SHIELD)
            early_specials++;
    }
    CHECK("above 90% HP only spear/shield are drawn", early_specials == 0);

    s.sol.last_attack_kind = COLO_SOL_ATTACK_NONE;
    s.sol.last_variant = 0;
    int v1 = col_sol_pick_variant(&s.sol, COLO_SOL_ATTACK_SPEAR);
    int v2 = col_sol_pick_variant(&s.sol, COLO_SOL_ATTACK_SPEAR);
    int v3 = col_sol_pick_variant(&s.sol, COLO_SOL_ATTACK_SPEAR);
    int v4 = col_sol_pick_variant(&s.sol, COLO_SOL_ATTACK_SHIELD);
    int v5 = col_sol_pick_variant(&s.sol, COLO_SOL_ATTACK_SPEAR);
    CHECK("consecutive same-type casts alternate 1 -> 2 -> 1", v1 == 1 && v2 == 2 && v3 == 1);
    CHECK("a type switch resets the variant to 1", v4 == 1 && v5 == 1);

    /* step-loop transition: dropping below 90% freezes Sol, drops 6 beams,
       spawns the phase crystal, and forces the next attack to be a Spear. */
    idx = sol_setup(&s, &ctx, 107);
    sol_move_player(&s, s.npcs[idx].x + 2, s.npcs[idx].y - 1);  /* flush south-centre */
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    int opener_tick = -1;
    for (int t = 0; t < 10 && opener_tick < 0; t++) {
        s.player.current_hitpoints = 99;
        step_and_observe(&s, &ctx, idle);
        if (s.npcs[idx].attacked_this_tick) opener_tick = s.tick;
    }
    CHECK("opener landed against the adjacent player", opener_tick > 0);

    s.npcs[idx].hp = (COLO_SOL_HP_MAX * 89) / 100;
    s.player.current_hitpoints = 99;
    step_and_observe(&s, &ctx, idle);
    CHECK("the 90% crossing enters phase 1", s.sol.phase == 1);
    CHECK("the transition spawns the phase crystal", s.sol.crystal_count == 1);
    CHECK("the transition drops 6 beams around the player",
        sol_count_active_beams(&s) == 6);
    CHECK("Sol is frozen through the transition", s.sol.immobile_ticks > 0);
    CHECK("the post-transition attack is forced to Spear", s.sol.force_spear == 1);
    int next_kind = -1;
    for (int t = 0; t < 20 && next_kind < 0; t++) {
        s.player.current_hitpoints = 99;
        step_and_observe(&s, &ctx, idle);
        if (s.npcs[idx].attacked_this_tick) next_kind = s.sol.last_attack_kind;
    }
    CHECK("the first attack after the transition is a Spear",
        next_kind == COLO_SOL_ATTACK_SPEAR);
}

/* 6c. A20: the parry hit schedule + damages per combo and phase — 15/25/35 at
   +3/+6/+9 in the 50-90% band, 15/30/45 at +3/+6/+10 below 50% — with no
   off-schedule damage. */
static void test_sol_parry_schedule_and_damage(void) {
    printf("test_sol_parry_schedule_and_damage\n");
    ColosseumContext ctx;
    ColosseumState s;
    int idle[COLO_NUM_ACTION_HEADS] = {0};

    for (int low = 0; low <= 1; low++) {
        int idx = sol_setup(&s, &ctx, 109 + (uint32_t)low);
        s.npcs[idx].hp = low ? (COLO_SOL_HP_MAX * 40) / 100 : (COLO_SOL_HP_MAX * 80) / 100;
        s.sol.phase = low ? 3 : 1;
        s.sol.attack_delay = 1000;   /* silence the engine: isolate the combo */
        sol_move_player(&s, 12, 12);
        col_sol_start_triple_parry(&s, idx);
        s.player.current_hitpoints = 99;

        int dmg_at[13] = {0};
        int hp_prev = 99;
        for (int t = 1; t <= 12; t++) {
            step_and_observe(&s, &ctx, idle);
            dmg_at[t] = hp_prev - s.player.current_hitpoints;
            hp_prev = s.player.current_hitpoints;
        }
        int h3 = low ? 10 : 9;
        int d2 = low ? 30 : 25;
        int d3 = low ? 45 : 35;
        CHECK(low ? "low band: 15/30/45 land at +3/+6/+10"
                  : "high band: 15/25/35 land at +3/+6/+9",
            dmg_at[3] == 15 && dmg_at[6] == d2 && dmg_at[h3] == d3);
        int clean = 1;
        for (int t = 1; t <= 12; t++)
            if (t != 3 && t != 6 && t != h3 && dmg_at[t] != 0) clean = 0;
        CHECK("no parry damage lands off-schedule", clean);
        CHECK("the combo retires after the third hit", s.sol.parry_hits_left == 0);
    }
}

/* 6d. B4+D18: flicking Protect from Melee exactly at each land tick blocks all
   three hits, every hit force-deactivates the overheads, and camping the
   prayer early makes every hit unblockable. */
static void test_sol_parry_prayer_punish(void) {
    printf("test_sol_parry_prayer_punish\n");
    ColosseumContext ctx;
    ColosseumState s;
    int idx = sol_setup(&s, &ctx, 113);
    s.npcs[idx].hp = (COLO_SOL_HP_MAX * 80) / 100;
    s.sol.phase = 1;
    s.sol.attack_delay = 1000;
    sol_move_player(&s, 12, 12);

    /* flick at land: prayer OFF through every lookback window, ON exactly on
       the land tick. */
    col_sol_start_triple_parry(&s, idx);
    s.player.current_hitpoints = 99;
    int actions[COLO_NUM_ACTION_HEADS] = {0};
    int deactivated_after_each = 1;
    int prayer_correct_before = s.log.total_prayer_correct;
    for (int t = 1; t <= 9; t++) {
        actions[COLO_HEAD_PRAYER] = (t == 3 || t == 6 || t == 9)
            ? ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE : ENCOUNTER_OVERHEAD_NO_CHANGE;
        step_and_observe(&s, &ctx, actions);
        if ((t == 3 || t == 6 || t == 9) && s.player.prayer != PRAYER_NONE)
            deactivated_after_each = 0;
    }
    CHECK("flicking Protect from Melee exactly at land blocks all three hits",
        s.player.current_hitpoints == 99);
    CHECK("every parry hit force-deactivates the overhead prayers",
        deactivated_after_each);
    CHECK("blocked parry hits count prayer_correct",
        s.log.total_prayer_correct >= prayer_correct_before + 3);

    /* early prayer: camping the overhead through the combo punishes every hit
       as unblockable — 15+25+35 lands through the active prayer. */
    col_sol_start_triple_parry(&s, idx);
    s.player.current_hitpoints = 99;
    for (int t = 1; t <= 9; t++) {
        actions[COLO_HEAD_PRAYER] = ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE;
        step_and_observe(&s, &ctx, actions);
    }
    CHECK("camping the prayer early makes every hit unblockable (75 total)",
        s.player.current_hitpoints == 99 - (15 + 25 + 35));
}

/* 6e. A12+A28+B7: the grapple calls one of exactly 5 slots; no response lands
   20-44 unblockable; an early correct click parries without the bonus; a
   last-tick click is a PERFECT parry whose guaranteed max the very next player
   attack consumes at exactly max_hit, expiring after 5 unconsumed ticks. */
static void test_sol_grapple_perfect_parry(void) {
    printf("test_sol_grapple_perfect_parry\n");
    ColosseumContext ctx;
    ColosseumState s;
    int idx = sol_setup(&s, &ctx, 127);
    s.sol.attack_delay = 1000;
    sol_move_player(&s, 18, 18);
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    int actions[COLO_NUM_ACTION_HEADS] = {0};

    /* fail: no response across the 4-tick window. */
    col_sol_start_grapple(&s);
    CHECK("the called slot is inside the 5-slot A12 domain",
        s.sol.grapple_body_slot >= 0 && s.sol.grapple_body_slot < COLO_NUM_GRAPPLE_SLOTS);
    s.player.current_hitpoints = 99;
    for (int t = 0; t < COLO_SOL_GRAPPLE_WINDOW; t++) step_and_observe(&s, &ctx, idle);
    int fail_dmg = 99 - s.player.current_hitpoints;
    CHECK("an unanswered grapple lands 20-44", fail_dmg >= 20 && fail_dmg <= 44);

    /* ordinary parry: a correct click before the last tick, no bonus. */
    col_sol_start_grapple(&s);
    s.player.current_hitpoints = 99;
    actions[COLO_HEAD_GRAPPLE_PARRY] = s.sol.grapple_body_slot + 1;
    step_and_observe(&s, &ctx, actions);
    CHECK("an early correct click parries without the perfect bonus",
        !s.sol.grapple_active && s.player.current_hitpoints == 99 &&
        s.sol.next_attack_guaranteed_max == 0);
    actions[COLO_HEAD_GRAPPLE_PARRY] = 0;

    /* perfect parry on the last window tick (grapple_timer 1 at the click). */
    col_sol_start_grapple(&s);
    s.player.current_hitpoints = 99;
    int slot = s.sol.grapple_body_slot;
    while (s.sol.grapple_timer > 2) step_and_observe(&s, &ctx, idle);
    actions[COLO_HEAD_GRAPPLE_PARRY] = slot + 1;
    step_and_observe(&s, &ctx, actions);
    actions[COLO_HEAD_GRAPPLE_PARRY] = 0;
    CHECK("a last-tick click is a perfect parry: no damage, max armed",
        s.player.current_hitpoints == 99 && s.sol.next_attack_guaranteed_max == 1);

    /* B7: the next player attack consumes the guaranteed max on its FIRST splat
       (the scythe's extra splats into 5x5 Sol roll normally on top). */
    int max_hit = s.loadout_stats[COLO_GEAR_MELEE].max_hit;
    CHECK("rig sanity: the melee loadout has a positive max hit", max_hit > 0);
    col_player_attack_target(&s, idx);
    CHECK("the guaranteed max is consumed at no less than the loadout max hit",
        s.player_attack_dmg >= max_hit && s.sol.next_attack_guaranteed_max == 0 &&
        s.sol.guaranteed_max_ticks == 0);

    /* expiry: an armed window not consumed within 5 ticks lapses. */
    col_sol_start_grapple(&s);
    s.player.current_hitpoints = 99;
    while (s.sol.grapple_timer > 2) step_and_observe(&s, &ctx, idle);
    actions[COLO_HEAD_GRAPPLE_PARRY] = s.sol.grapple_body_slot + 1;
    step_and_observe(&s, &ctx, actions);
    actions[COLO_HEAD_GRAPPLE_PARRY] = 0;
    CHECK("second perfect parry armed", s.sol.next_attack_guaranteed_max == 1);
    for (int t = 0; t < COLO_SOL_PERFECT_MAX_TICKS; t++) step_and_observe(&s, &ctx, idle);
    CHECK("an unconsumed guaranteed max expires after 5 ticks",
        s.sol.next_attack_guaranteed_max == 0);
}

/** E2: a perfect-parry guaranteed max also applies to special attacks, so the
    canonical parry-into-claws line forces the claws first-success best split
    and consumes the armed flag. */
static void test_sol_perfect_parry_forces_spec_attack(void) {
    printf("test_sol_perfect_parry_forces_spec_attack\n");
    ColosseumContext ctx;
    ColosseumState s;
    int idx = sol_setup_speedrun(&s, &ctx, 128);
    CHECK("speedrun Sol setup succeeded", idx >= 0);
    CHECK("speedrun spec A is dragon claws",
        COLO_SPEC_WEAPONS[s.active_loadout_profile][0] == ITEM_DRAGON_CLAWS);

    int max_hit = s.spec_stats[0].max_hit;
    int claws_total = 2 * max_hit - 1;
    int expected_total = claws_total / 2 + claws_total / 4 +
        claws_total / 8 + claws_total / 8 + 1;
    CHECK("rig sanity: claws max hit is positive", max_hit > 0);

    s.sol.attack_delay = 1000;
    s.player.special_energy = 100;
    s.spec_armed_kind = 1;
    s.sol.next_attack_guaranteed_max = 1;
    s.sol.guaranteed_max_ticks = COLO_SOL_PERFECT_MAX_TICKS;
    col_player_attack_target(&s, idx);

    CHECK("perfect-parry claws uses the forced first-success best total",
        s.player_attack_dmg == expected_total &&
        s.npcs[idx].pending_hits.count == 4);
    CHECK("perfect-parry spec consumes and clears the max flag",
        s.sol.next_attack_guaranteed_max == 0 &&
        s.sol.guaranteed_max_ticks == 0);
    CHECK("perfect-parry spec still spends claws energy",
        s.player.special_energy == 50 && s.spec_armed_kind == 0);
}

/* 6f. A3: shield safe-ring geometry for both variants — the Chebyshev ring is
   safe, the inner block and the rest of the arena both burn — checked on the
   predicate and through the step loop at the bite tick. */
static void test_sol_shield_safe_rings(void) {
    printf("test_sol_shield_safe_rings\n");
    ColosseumContext ctx;
    ColosseumState s;
    int idx = sol_setup(&s, &ctx, 131);
    sol_pin(&s, idx, COLO_SOL_SPAWN_X, COLO_SOL_SPAWN_Y);
    ColoNPC* boss = &s.npcs[idx];
    int cx = boss->x + 2, cy = boss->y + 2;   /* (18,21) */
    s.sol.aoe_x = boss->x;
    s.sol.aoe_y = boss->y;

    s.sol.aoe_attack = COLO_SOL_AOE_SHIELD1;
    CHECK("shield1: the inner 7x7 burns",
        col_sol_aoe_tile_is_hazard(&s.sol, cx, cy - 3));
    CHECK("shield1: the Chebyshev-4 ring face is safe",
        !col_sol_aoe_tile_is_hazard(&s.sol, cx, cy - 4));
    CHECK("shield1: the ring corner is safe",
        !col_sol_aoe_tile_is_hazard(&s.sol, cx - 4, cy - 4));
    CHECK("shield1: one past the ring burns",
        col_sol_aoe_tile_is_hazard(&s.sol, cx, cy - 5));
    CHECK("shield1: the far arena burns",
        col_sol_aoe_tile_is_hazard(&s.sol, cx - 8, cy - 8));

    s.sol.aoe_attack = COLO_SOL_AOE_SHIELD2;
    CHECK("shield2: Chebyshev 4 is inside the 9x9 block",
        col_sol_aoe_tile_is_hazard(&s.sol, cx, cy - 4));
    CHECK("shield2: the Chebyshev-5 ring is safe",
        !col_sol_aoe_tile_is_hazard(&s.sol, cx, cy - 5) &&
        !col_sol_aoe_tile_is_hazard(&s.sol, cx - 5, cy - 5));
    CHECK("shield2: one past the ring burns",
        col_sol_aoe_tile_is_hazard(&s.sol, cx, cy - 6));

    /* step-loop bite checks: ring / inside / outside for both variants. */
    s.sol.aoe_attack = COLO_SOL_AOE_NONE;
    s.sol.attack_delay = 1000;
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    for (int variant = 1; variant <= 2; variant++) {
        int ring = variant == 1 ? COLO_SOL_SHIELD1_RING : COLO_SOL_SHIELD2_RING;
        for (int spot = 0; spot < 3; spot++) {
            int off = spot == 0 ? ring : (spot == 1 ? ring - 1 : ring + 1);
            sol_move_player(&s, cx, cy - off);
            col_sol_cast_aoe(&s, idx, COLO_SOL_ATTACK_SHIELD, variant);
            s.player.current_hitpoints = 99;
            step_and_observe(&s, &ctx, idle);
            int dmg = 99 - s.player.current_hitpoints;
            if (spot == 0)
                CHECK(variant == 1 ? "shield1 bite: the ring tile is safe"
                                   : "shield2 bite: the ring tile is safe", dmg == 0);
            else
                CHECK(spot == 1 ? "shield bite: inside the block lands 20-44"
                                : "shield bite: outside the ring lands 20-44",
                    dmg >= 20 && dmg <= 44);
            s.sol.aoe_attack = COLO_SOL_AOE_NONE;   /* clear residue between casts */
        }
    }
}

/* 6g. A3+A15+D14: spear hazard frames point at the player — front coverage,
   lines at the documented columns, length 4 — and the tiles bite exactly 1
   tick after appearing, so stepping to a documented dodge tile during the cast
   tick avoids everything. */
static void test_sol_spear_lines(void) {
    printf("test_sol_spear_lines\n");
    ColosseumContext ctx;
    ColosseumState s;
    int idx = sol_setup(&s, &ctx, 137);
    sol_pin(&s, idx, COLO_SOL_SPAWN_X, COLO_SOL_SPAWN_Y);

    /* player due south of the (16..20, 19..23) footprint: direction (0,-1). */
    sol_move_player(&s, 18, 18);
    col_sol_cast_aoe(&s, idx, COLO_SOL_ATTACK_SPEAR, 1);
    CHECK("spear lines aim at the player (south)",
        s.sol.aoe_dir_x == 0 && s.sol.aoe_dir_y == -1);
    int front_ok = 1;
    for (int x = 16; x <= 20; x++)
        if (!col_sol_aoe_tile_is_hazard(&s.sol, x, 18)) front_ok = 0;
    CHECK("spear1 front row covers all 5 columns", front_ok);
    CHECK("spear1 runs TWO lines at the off-centre columns",
        col_sol_aoe_tile_is_hazard(&s.sol, 17, 17) &&
        col_sol_aoe_tile_is_hazard(&s.sol, 19, 17));
    CHECK("spear1 centre + corner columns are safe 1 back (the dodge)",
        !col_sol_aoe_tile_is_hazard(&s.sol, 16, 17) &&
        !col_sol_aoe_tile_is_hazard(&s.sol, 18, 17) &&
        !col_sol_aoe_tile_is_hazard(&s.sol, 20, 17));
    CHECK("spear lines reach exactly 4 tiles from the boss edge",
        col_sol_aoe_tile_is_hazard(&s.sol, 17, 15) &&
        !col_sol_aoe_tile_is_hazard(&s.sol, 17, 14));

    col_sol_cast_aoe(&s, idx, COLO_SOL_ATTACK_SPEAR, 2);
    CHECK("spear2 runs THREE lines at the corner + centre columns",
        col_sol_aoe_tile_is_hazard(&s.sol, 16, 18) &&
        col_sol_aoe_tile_is_hazard(&s.sol, 18, 18) &&
        col_sol_aoe_tile_is_hazard(&s.sol, 20, 18));
    CHECK("spear2 off-centre flush tiles are safe (the diagonal dodge)",
        !col_sol_aoe_tile_is_hazard(&s.sol, 17, 18) &&
        !col_sol_aoe_tile_is_hazard(&s.sol, 19, 18));

    /* the direction tracks the player: cast again with the player east. */
    sol_move_player(&s, 21, 21);
    col_sol_cast_aoe(&s, idx, COLO_SOL_ATTACK_SPEAR, 1);
    CHECK("spear lines aim at the player (east)",
        s.sol.aoe_dir_x == 1 && s.sol.aoe_dir_y == 0);
    CHECK("east cast: the front column burns and lines run east",
        col_sol_aoe_tile_is_hazard(&s.sol, 21, 21) &&
        col_sol_aoe_tile_is_hazard(&s.sol, 22, 20) &&
        col_sol_aoe_tile_is_hazard(&s.sol, 22, 22) &&
        !col_sol_aoe_tile_is_hazard(&s.sol, 22, 21));

    /* A15: standing still eats the per-tile 20-44 one tick after the cast;
       the documented 1-back centre dodge taken during the cast tick is clean. */
    s.sol.aoe_attack = COLO_SOL_AOE_NONE;
    s.sol.attack_delay = 1000;
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    sol_move_player(&s, 18, 18);
    col_sol_cast_aoe(&s, idx, COLO_SOL_ATTACK_SPEAR, 1);
    s.player.current_hitpoints = 99;
    step_and_observe(&s, &ctx, idle);
    int dmg = 99 - s.player.current_hitpoints;
    CHECK("standing on a spear tile at the bite tick lands 20-44",
        dmg >= 20 && dmg <= 44);
    s.sol.aoe_attack = COLO_SOL_AOE_NONE;
    sol_move_player(&s, 18, 18);
    col_sol_cast_aoe(&s, idx, COLO_SOL_ATTACK_SPEAR, 1);
    sol_move_player(&s, 18, 17);   /* the centre-column 1-back dodge */
    s.player.current_hitpoints = 99;
    step_and_observe(&s, &ctx, idle);
    CHECK("the 1-tick dodge to the centre column avoids spear1 fully",
        s.player.current_hitpoints == 99);
}

/* 6h. A9+A10+D15+D16: one crystal accumulates per transition (none at enrage),
   cooldowns roll 25-35 (12 at enrage), the crystal walks the edge ring every 4
   ticks, and firing telegraphs 4 ticks then launches a 60-75 sphere at the
   player's tile that lands 4 ticks later. */
static void test_sol_crystal_lifecycle(void) {
    printf("test_sol_crystal_lifecycle\n");
    ColosseumContext ctx;
    ColosseumState s;
    int idx = sol_setup(&s, &ctx, 139);
    (void)idx;
    s.sol.attack_delay = 1000;
    sol_move_player(&s, 16, 14);

    int accumulates = 1;
    for (int p = 1; p <= 4; p++) {
        col_sol_enter_phase(&s, p);
        if (s.sol.crystal_count != p) accumulates = 0;
    }
    CHECK("one crystal spawns at each transition (4 by 25%)", accumulates);
    col_sol_enter_phase(&s, 5);
    CHECK("the enrage transition adds no fifth crystal", s.sol.crystal_count == 4);
    s.sol.phase = 4;   /* hold pre-enrage for the cooldown checks below */
    int cooldowns_ok = 1;
    for (int c = 0; c < 4; c++)
        if (s.sol.crystals[c].fire_cooldown < 25 || s.sol.crystals[c].fire_cooldown > 35)
            cooldowns_ok = 0;
    CHECK("fresh crystal cooldowns roll uniform 25-35", cooldowns_ok);
    sol_clear_beams_and_sand(&s);

    /* ring motion: a crystal advances one ring step every 4 ticks. */
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    for (int c = 0; c < 4; c++) s.sol.crystals[c].fire_cooldown = 1000;
    int step_before = s.sol.crystals[0].perim_step;
    for (int t = 0; t < 4; t++) {
        s.player.current_hitpoints = 99;
        step_and_observe(&s, &ctx, idle);
    }
    CHECK("the crystal advances exactly one ring step per 4 ticks",
        s.sol.crystals[0].perim_step == step_before + 1);

    /* firing: cooldown -> 4-tick telegraph -> sphere on the player's tile. */
    s.sol.crystals[0].perim_step = 7;   /* (16,9): clear column to the player */
    s.sol.crystals[0].move_timer = COLO_SOL_CRYSTAL_MOVE_TICKS;
    s.sol.crystals[0].fire_cooldown = 1;
    for (int t = 0; t < 1 + COLO_SOL_CRYSTAL_TELEGRAPH; t++) {
        s.player.current_hitpoints = 99;
        step_and_observe(&s, &ctx, idle);
    }
    int sphere = -1;
    for (int i = 0; i < COLO_SOL_SPHERE_QUEUE_MAX; i++)
        if (s.sol.spheres[i].active) sphere = i;
    CHECK("the telegraph launches a sphere marking the player's tile",
        sphere >= 0 &&
        s.sol.spheres[sphere].tile_x == s.player.x &&
        s.sol.spheres[sphere].tile_y == s.player.y);
    CHECK("sphere damage rolls 60-75",
        sphere >= 0 && s.sol.spheres[sphere].damage >= 60 &&
        s.sol.spheres[sphere].damage <= 75);
    CHECK("the post-fire cooldown rerolls 25-35 before enrage",
        s.sol.crystals[0].fire_cooldown >= 25 && s.sol.crystals[0].fire_cooldown <= 35);

    int expected = sphere >= 0 ? s.sol.spheres[sphere].damage : 0;
    s.player.current_hitpoints = 99;
    for (int t = 0; t < COLO_SOL_SPHERE_DELAY; t++) step_and_observe(&s, &ctx, idle);
    CHECK("the sphere lands on the marked tile 4 ticks after launch",
        99 - s.player.current_hitpoints == expected);

    /* enrage: the post-fire cooldown drops to 12. */
    s.sol.phase = 5;
    s.sol.crystals[0].fire_cooldown = 1;
    for (int t = 0; t < 1 + COLO_SOL_CRYSTAL_TELEGRAPH; t++) {
        s.player.current_hitpoints = 99;
        step_and_observe(&s, &ctx, idle);
    }
    CHECK("at enrage the post-fire cooldown is 12",
        s.sol.crystals[0].fire_cooldown == COLO_SOL_CRYSTAL_COOLDOWN_ENRAGE);
}

/** E6: phase-transition molten sand always includes the player's tile and
    fills all 6 in-arena tiles when the valid 9x9 candidate pool has room. */
static void test_sol_phase_transition_sand_guarantees(void) {
    printf("test_sol_phase_transition_sand_guarantees\n");
    ColosseumContext ctx;
    ColosseumState s;
    int seeded_ok = 1;

    for (uint32_t seed = 150; seed < 230; seed++) {
        int idx = sol_setup(&s, &ctx, seed);
        (void)idx;
        s.sol.attack_delay = 1000;
        sol_move_player(&s, 17, 14);
        col_sol_enter_phase(&s, 1);
        for (int t = 0; t < COLO_SOL_BEAM_TO_POOL_TICKS; t++)
            col_sol_tick_molten(&s);
        if (!sol_phase_sand_invariants_hold(&s, COLO_SOL_BEAM_COUNT)) {
            seeded_ok = 0;
            break;
        }
    }
    CHECK("seeded phase transitions place exactly 6 unique in-arena sand tiles with one under player",
        seeded_ok);

    int idx = sol_setup(&s, &ctx, 231);
    (void)idx;
    s.sol.attack_delay = 1000;
    int corner_x = COLO_BOSS_ARENA_MIN_X + 2;
    int corner_y = COLO_BOSS_ARENA_MIN_Y;
    sol_move_player(&s, corner_x, corner_y);
    CHECK("rig sanity: corner-edge player tile is walkable",
        col_in_boss_arena(&s, s.player.x, s.player.y) &&
        !col_static_blocked(s.player.x, s.player.y));
    col_sol_enter_phase(&s, 1);
    for (int t = 0; t < COLO_SOL_BEAM_TO_POOL_TICKS; t++)
        col_sol_tick_molten(&s);
    CHECK("corner-edge phase transition still places 6 in-arena sand tiles including player",
        sol_phase_sand_invariants_hold(&s, COLO_SOL_BEAM_COUNT));
}

/* 6i. A11+D25: 6 beams drop in the 9x9 around the player and become PERMANENT
   molten pools after 2 ticks, burning 5-9 per tick stood on. */
static void test_sol_beams_become_pools(void) {
    printf("test_sol_beams_become_pools\n");
    ColosseumContext ctx;
    ColosseumState s;
    int idx = sol_setup(&s, &ctx, 149);
    (void)idx;
    s.sol.attack_delay = 1000;
    sol_move_player(&s, 17, 14);

    col_sol_drop_beams(&s);
    int beams = sol_count_active_beams(&s);
    int in_box = 1;
    for (int b = 0; b < COLO_SOL_BEAM_MAX; b++) {
        if (!s.sol.beams[b].active) continue;
        int dx = abs(s.sol.beams[b].x - s.player.x);
        int dy = abs(s.sol.beams[b].y - s.player.y);
        if (dx > COLO_SOL_BEAM_SPREAD || dy > COLO_SOL_BEAM_SPREAD) in_box = 0;
        if (col_static_blocked(s.sol.beams[b].x, s.sol.beams[b].y)) in_box = 0;
    }
    CHECK("6 beams drop inside the 9x9 around the player", beams == 6 && in_box);

    int idle[COLO_NUM_ACTION_HEADS] = {0};
    for (int t = 0; t < COLO_SOL_BEAM_TO_POOL_TICKS; t++) {
        s.player.current_hitpoints = 99;
        step_and_observe(&s, &ctx, idle);
    }
    CHECK("every beam becomes a molten pool after 2 ticks",
        s.sol.hazard_tile_count == 6 && sol_count_active_beams(&s) == 0);

    sol_move_player(&s, s.sol.hazard_tile_x[0], s.sol.hazard_tile_y[0]);
    int burns_ok = 1;
    for (int t = 0; t < 30; t++) {
        s.player.current_hitpoints = 99;
        step_and_observe(&s, &ctx, idle);
        int dmg = 99 - s.player.current_hitpoints;
        if (dmg < COLO_MOLTEN_SAND_MIN_HIT ||
            dmg > COLO_MOLTEN_SAND_MIN_HIT + COLO_MOLTEN_SAND_RAND - 1) burns_ok = 0;
    }
    CHECK("standing on a pool burns 5-9 every tick", burns_ok);
    CHECK("pools persist for the rest of the fight", s.sol.hazard_tile_count == 6);
}


/* ---- 7. researched loadout profiles (L1-L16): gear, supplies, consumables,
   spec weapons, item effects, offensive prayers. */

static void loadout_reset(ColosseumState* s, ColosseumContext* ctx, int mode,
                          float frac, uint32_t seed) {
    col_init_context_typed(ctx);
    ctx->config.loadout_profile_mode = mode;
    ctx->config.beginner_loadout_fraction = frac;
    memset(s, 0, sizeof(*s));
    col_reset_ctx((EncounterState*)s, (EncounterContext*)ctx, seed);
}

static int col_loadout_stats_equal(
    const EncounterLoadoutStats* a,
    const EncounterLoadoutStats* b
) {
    return a->attack_bonus == b->attack_bonus &&
           a->strength_bonus == b->strength_bonus &&
           a->eff_level == b->eff_level &&
           a->max_hit == b->max_hit &&
           a->attack_speed == b->attack_speed &&
           a->attack_range == b->attack_range &&
           a->style == b->style &&
           a->fight_style == b->fight_style &&
           a->def_stab == b->def_stab &&
           a->def_slash == b->def_slash &&
           a->def_crush == b->def_crush &&
           a->def_magic == b->def_magic &&
           a->def_ranged == b->def_ranged &&
           a->att_prayer_mult == b->att_prayer_mult &&
           a->str_prayer_mult == b->str_prayer_mult &&
           a->spell_base_damage == b->spell_base_damage;
}

static void test_loadout_profiles_and_supplies(void) {
    printf("test_loadout_profiles_and_supplies\n");
    ColosseumContext ctx;
    ColosseumState s;

    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_BEGINNER_ONLY, 0.0f, 11);
    CHECK("beginner mode pins the beginner profile",
        s.active_loadout_profile == COLO_LOADOUT_PROFILE_BEGINNER);
    CHECK("beginner melee weapon is the fang",
        s.player.equipped[GEAR_SLOT_WEAPON] == ITEM_OSMUMTENS_FANG);
    CHECK("beginner supplies match the budget example",
        s.player.brew_doses == 24 && s.player.restore_doses == 32 &&
        s.player.combat_potion_doses == 8 && s.player.ranged_potion_doses == 8 &&
        s.surge_doses == 0);
    CHECK("beginner restore kind is super restore",
        s.full_supplies.restore_kind == COLO_RESTORE_SUPER_RESTORE);
    CHECK("beginner bowfa set carries full crystal points (1+2+3)",
        s.set_effects[COLO_GEAR_RANGED].crystal_armour_points == 6);
    CHECK("beginner melee neck carries blood fury",
        osrs_effect_profile_has(&s.set_effects[COLO_GEAR_MELEE], OSRS_ITEM_EFFECT_BLOOD_FURY));
    CHECK("beginner melee head carries venom immunity",
        osrs_effect_profile_has(&s.set_effects[COLO_GEAR_MELEE], OSRS_ITEM_EFFECT_VENOM_IMMUNE));
    int beginner_melee_max = s.loadout_stats[COLO_GEAR_MELEE].max_hit;
    CHECK("beginner spec weapons are SGS then claws",
        COLO_SPEC_WEAPONS[s.active_loadout_profile][0] == ITEM_SGS &&
        COLO_SPEC_WEAPONS[s.active_loadout_profile][1] == ITEM_DRAGON_CLAWS);

    uint8_t beginner_sgs_without_defender[NUM_GEAR_SLOTS];
    memcpy(beginner_sgs_without_defender, COLO_BEGINNER_MELEE_LOADOUT, NUM_GEAR_SLOTS);
    beginner_sgs_without_defender[GEAR_SLOT_WEAPON] = ITEM_SGS;
    beginner_sgs_without_defender[GEAR_SLOT_SHIELD] = ITEM_NONE;
    EncounterLoadoutStats beginner_sgs_expected;
    encounter_compute_loadout_stats(beginner_sgs_without_defender, ATTACK_STYLE_MELEE,
        OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_AGGRESSIVE, 0, &beginner_sgs_expected);
    CHECK("beginner SGS spec stats exclude dragon defender bonuses",
        col_loadout_stats_equal(&s.spec_stats[0], &beginner_sgs_expected));

    uint8_t beginner_claws_without_defender[NUM_GEAR_SLOTS];
    memcpy(beginner_claws_without_defender, COLO_BEGINNER_MELEE_LOADOUT, NUM_GEAR_SLOTS);
    beginner_claws_without_defender[GEAR_SLOT_WEAPON] = ITEM_DRAGON_CLAWS;
    beginner_claws_without_defender[GEAR_SLOT_SHIELD] = ITEM_NONE;
    EncounterLoadoutStats beginner_claws_without;
    encounter_compute_loadout_stats(beginner_claws_without_defender, ATTACK_STYLE_MELEE,
        OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_AGGRESSIVE, 0, &beginner_claws_without);
    CHECK("beginner claws spec stats keep dragon defender strength",
        s.spec_stats[1].strength_bonus ==
            beginner_claws_without.strength_bonus +
            ITEM_DATABASE[ITEM_DRAGON_DEFENDER].melee_strength);
    CHECK("beginner claws spec stats keep dragon defender accuracy",
        s.spec_stats[1].attack_bonus > beginner_claws_without.attack_bonus);

    uint8_t beginner_bowfa_with_defender[NUM_GEAR_SLOTS];
    memcpy(beginner_bowfa_with_defender, COLO_BEGINNER_RANGED_LOADOUT, NUM_GEAR_SLOTS);
    beginner_bowfa_with_defender[GEAR_SLOT_SHIELD] = ITEM_DRAGON_DEFENDER;
    EncounterLoadoutStats beginner_bowfa_illegal_shield;
    encounter_compute_loadout_stats(beginner_bowfa_with_defender, ATTACK_STYLE_RANGED,
        OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_RAPID, 0, &beginner_bowfa_illegal_shield);
    CHECK("beginner bowfa ranged stats are unchanged by an occupied shield slot",
        col_loadout_stats_equal(
            &s.loadout_stats[COLO_GEAR_RANGED], &beginner_bowfa_illegal_shield));

    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY, 1.0f, 12);
    CHECK("speedrun mode pins the speedrun profile",
        s.active_loadout_profile == COLO_LOADOUT_PROFILE_SPEEDRUN);
    CHECK("speedrun melee weapon is the scythe",
        s.player.equipped[GEAR_SLOT_WEAPON] == ITEM_SCYTHE_OF_VITUR);
    CHECK("speedrun supplies match the high-efficiency kit",
        s.player.brew_doses == 4 && s.player.restore_doses == 28 &&
        s.player.combat_potion_doses == 4 && s.player.ranged_potion_doses == 4 &&
        s.surge_doses == 4);
    CHECK("speedrun restore kind is sanfew",
        s.full_supplies.restore_kind == COLO_RESTORE_SANFEW);
    CHECK("speedrun ranged set has the tbow effect",
        osrs_effect_profile_has(&s.set_effects[COLO_GEAR_RANGED], OSRS_ITEM_EFFECT_TWISTED_BOW));
    /* per-splat the fang can rival the scythe (wiki Budget footnote j: the fang
       wins on 1x1s); the scythe's edge is the 7/4 splat total into 3x3+ NPCs. */
    CHECK("speedrun scythe total (7/4 splats) out-hits the beginner fang",
        s.loadout_stats[COLO_GEAR_MELEE].max_hit * 7 / 4 > beginner_melee_max);
    CHECK("both loadouts melee-style on the melee set",
        s.loadout_stats[COLO_GEAR_MELEE].style == ATTACK_STYLE_MELEE &&
        s.loadout_stats[COLO_GEAR_RANGED].style == ATTACK_STYLE_RANGED);
    CHECK("spec stats computed for both spec weapons",
        s.spec_stats[0].max_hit > 0 && s.spec_stats[1].max_hit > 0);

    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_MIXED, 1.0f, 13);
    CHECK("mixed fraction 1.0 always samples beginner",
        s.active_loadout_profile == COLO_LOADOUT_PROFILE_BEGINNER);
    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_MIXED, 0.0f, 14);
    CHECK("mixed fraction 0.0 always samples speedrun",
        s.active_loadout_profile == COLO_LOADOUT_PROFILE_SPEEDRUN);
}

static void test_loadout_consumables(void) {
    printf("test_loadout_consumables\n");
    ColosseumContext ctx;
    ColosseumState s;
    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_BEGINNER_ONLY, 0.0f, 21);
    complete_open_draft(&s, &ctx, 1);
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    int base_max_hit = s.loadout_stats[COLO_GEAR_MELEE].max_hit;

    /* brew: heals 16, drains stats, melee max hit drops (L12) */
    s.player.current_hitpoints = 50;
    int brew[COLO_NUM_ACTION_HEADS] = {0};
    brew[COLO_HEAD_EAT] = 1;
    step_and_observe(&s, &ctx, brew);
    CHECK("brew heals 16", s.player.current_hitpoints == 66);
    CHECK("brew consumes a dose and starts the timer",
        s.player.brew_doses == 23 && s.player.potion_timer == 3);
    CHECK("brew drains attack below base", s.player.current_attack < 99);
    CHECK("brew drain lowers the melee max hit",
        s.loadout_stats[COLO_GEAR_MELEE].max_hit < base_max_hit);

    /* restore: stats (and the max hit) come back once the timer clears */
    for (int t = 0; t < 3; t++) step_and_observe(&s, &ctx, idle);
    int restore[COLO_NUM_ACTION_HEADS] = {0};
    restore[COLO_HEAD_POTION] = COLO_POTION_RESTORE;
    step_and_observe(&s, &ctx, restore);
    CHECK("super restore returns attack to base", s.player.current_attack == 99);
    CHECK("restore recovers the melee max hit",
        s.loadout_stats[COLO_GEAR_MELEE].max_hit == base_max_hit);
    CHECK("restore consumed a dose", s.player.restore_doses == 31);

    /* super combat: boosts att/str/def to 118 and raises the max hit */
    for (int t = 0; t < 3; t++) step_and_observe(&s, &ctx, idle);
    int combat[COLO_NUM_ACTION_HEADS] = {0};
    combat[COLO_HEAD_POTION] = COLO_POTION_COMBAT;
    step_and_observe(&s, &ctx, combat);
    CHECK("super combat boosts attack to 118", s.player.current_attack == 118);
    CHECK("super combat boosts strength to 118", s.player.current_strength == 118);
    CHECK("super combat raises the melee max hit",
        s.loadout_stats[COLO_GEAR_MELEE].max_hit > base_max_hit);
    CHECK("combat pot consumed a dose", s.player.combat_potion_doses == 7);

    /* boosted: the combat-pot mask entry closes (inferno [base, base+5] window) */
    float mask[COLO_ACTION_MASK_SIZE];
    col_write_mask_ctx((EncounterState*)&s, (EncounterContext*)&ctx, mask);
    int pot_off = col_action_head_mask_offset(COLO_HEAD_POTION);
    CHECK("combat pot masked while boosted past the window",
        mask[pot_off + COLO_POTION_COMBAT] == 0.0f);
    CHECK("surge masked for the beginner (no doses)",
        mask[pot_off + COLO_POTION_SURGE] == 0.0f);

    /* ranging potion boosts ranged to 112 */
    for (int t = 0; t < 3; t++) step_and_observe(&s, &ctx, idle);
    int rng_pot[COLO_NUM_ACTION_HEADS] = {0};
    rng_pot[COLO_HEAD_POTION] = COLO_POTION_RANGING;
    step_and_observe(&s, &ctx, rng_pot);
    CHECK("ranging potion boosts ranged to 112", s.player.current_ranged == 112);

    /* prayer restore amount: super restore = +32 */
    for (int t = 0; t < 3; t++) step_and_observe(&s, &ctx, idle);
    s.player.current_prayer = 40;
    step_and_observe(&s, &ctx, restore);
    CHECK("super restore gives +32 prayer", s.player.current_prayer == 72);
}

/** E13: divine boost potions self-damage, pin boosted stats for 500 live ticks,
    and share the 100-tick natural stat drift contract. */
static void test_loadout_divine_potions_and_stat_drift(void) {
    printf("test_loadout_divine_potions_and_stat_drift\n");
    ColosseumContext ctx;
    ColosseumState s;
    int idle[COLO_NUM_ACTION_HEADS] = {0};
    float mask[COLO_ACTION_MASK_SIZE];
    int pot_off = col_action_head_mask_offset(COLO_HEAD_POTION);

    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY, 0.0f, 81);
    CHECK("speedrun combat and ranging potions are divine",
        col_combat_potion_is_divine(&s) && col_ranged_potion_is_divine(&s));
    s.player.current_hitpoints = 10;
    col_write_mask_ctx((EncounterState*)&s, (EncounterContext*)&ctx, mask);
    CHECK("divine boost potions are masked at 10 HP",
        mask[pot_off + COLO_POTION_COMBAT] == 0.0f &&
        mask[pot_off + COLO_POTION_RANGING] == 0.0f);
    s.player.current_hitpoints = 11;
    col_write_mask_ctx((EncounterState*)&s, (EncounterContext*)&ctx, mask);
    CHECK("divine boost potions unmask above 10 HP",
        mask[pot_off + COLO_POTION_COMBAT] == 1.0f &&
        mask[pot_off + COLO_POTION_RANGING] == 1.0f);

    s.player.current_hitpoints = 50;
    int combat[COLO_NUM_ACTION_HEADS] = {0};
    combat[COLO_HEAD_POTION] = COLO_POTION_COMBAT;
    step_and_observe(&s, &ctx, combat);
    CHECK("divine combat chunks 10 HP and starts a 500-tick hold",
        s.player.current_hitpoints == 40 &&
        s.divine_combat_timer == ENCOUNTER_DIVINE_POTION_TICKS &&
        s.player.combat_potion_doses == 3);
    CHECK("divine combat boosts to the held floor",
        s.player.current_attack == 118 && s.player.current_strength == 118 &&
        s.player.current_defence == 118);
    for (int t = 0; t < ENCOUNTER_DIVINE_POTION_TICKS - 1; t++)
        col_tick_live_stat_drift_and_divines(&s);
    CHECK("divine combat stats survive through tick 499",
        s.player.current_attack == 118 && s.player.current_strength == 118 &&
        s.player.current_defence == 118 && s.divine_combat_timer == 1);
    col_tick_live_stat_drift_and_divines(&s);
    CHECK("divine combat expiry drops its stats to base instantly",
        s.player.current_attack == 99 && s.player.current_strength == 99 &&
        s.player.current_defence == 99 && s.divine_combat_timer == 0);

    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY, 0.0f, 82);
    s.player.current_hitpoints = 50;
    int ranged[COLO_NUM_ACTION_HEADS] = {0};
    ranged[COLO_HEAD_POTION] = COLO_POTION_RANGING;
    step_and_observe(&s, &ctx, ranged);
    CHECK("divine ranging chunks 10 HP and starts a 500-tick hold",
        s.player.current_hitpoints == 40 &&
        s.divine_ranged_timer == ENCOUNTER_DIVINE_POTION_TICKS &&
        s.player.ranged_potion_doses == 3);
    for (int t = 0; t < ENCOUNTER_DIVINE_POTION_TICKS; t++)
        col_tick_live_stat_drift_and_divines(&s);
    CHECK("divine ranging expiry drops Ranged to base instantly",
        s.player.current_ranged == 99 && s.divine_ranged_timer == 0);

    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_BEGINNER_ONLY, 0.0f, 83);
    CHECK("beginner boost potions are regular",
        !col_combat_potion_is_divine(&s) && !col_ranged_potion_is_divine(&s));
    step_and_observe(&s, &ctx, combat);
    CHECK("regular beginner combat boost does not arm a divine timer",
        s.player.current_hitpoints == 99 && s.divine_combat_timer == 0 &&
        s.player.current_attack == 118);
    for (int t = 0; t < ENCOUNTER_STAT_DRIFT_TICKS; t++)
        col_tick_live_stat_drift_and_divines(&s);
    CHECK("regular beginner combat boost decays one level after 100 live ticks",
        s.player.current_attack == 117 && s.player.current_strength == 117 &&
        s.player.current_defence == 117);

    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY, 0.0f, 84);
    s.stat_drift_timer = ENCOUNTER_STAT_DRIFT_TICKS - 1;
    s.divine_combat_timer = 1;
    s.player.current_attack = 118;
    s.player.current_strength = 118;
    s.player.current_defence = 118;
    step_and_observe(&s, &ctx, idle);
    CHECK("stat drift and divine timers freeze during the draft gap",
        s.stat_drift_timer == ENCOUNTER_STAT_DRIFT_TICKS - 1 &&
        s.divine_combat_timer == 1 && s.player.current_attack == 118);

    s.stat_drift_timer = 37;
    s.divine_combat_timer = 123;
    s.divine_ranged_timer = 234;
    ColoSnapshot snap;
    col_snapshot_ctx((EncounterState*)&s, (EncounterContext*)&ctx, &snap);
    CHECK("snapshot version is v6 for stat drift fields",
        snap.version == COLO_SNAPSHOT_VERSION && COLO_SNAPSHOT_VERSION == 6u);
    ColosseumState restored;
    memset(&restored, 0, sizeof(restored));
    col_restore_ctx((EncounterState*)&restored, (EncounterContext*)&ctx, &snap, sizeof(snap));
    CHECK("snapshot v6 round-trips stat drift and divine timers",
        restored.stat_drift_timer == 37 &&
        restored.divine_combat_timer == 123 &&
        restored.divine_ranged_timer == 234);
}

static void test_loadout_sanfew_and_serp_helm(void) {
    printf("test_loadout_sanfew_and_serp_helm\n");
    ColosseumContext ctx;
    ColosseumState s;

    /* sanfew (speedrun restore) cures venom on drink (L5) */
    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY, 0.0f, 31);
    complete_open_draft(&s, &ctx, 1);
    s.player_venom = 8;
    s.player_venom_timer = 12;
    s.player_poison = COLO_POISON_BEE_CONTACT_SEVERITY;
    s.player_poison_timer = 9;
    s.player.current_prayer = 40;
    int restore[COLO_NUM_ACTION_HEADS] = {0};
    restore[COLO_HEAD_POTION] = COLO_POTION_RESTORE;
    step_and_observe(&s, &ctx, restore);
    CHECK("sanfew clears venom", s.player_venom == 0 && s.player_venom_timer == 0);
    CHECK("sanfew clears poison", s.player_poison == 0 && s.player_poison_timer == 0);
    CHECK("sanfew gives +33 prayer", s.player.current_prayer == 73);
    CHECK("sanfew consumed a restore dose", s.player.restore_doses == 27);

    /* serp helm: blocks venom application in the melee set only; an existing
       stack survives switching back (no cure). */
    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_BEGINNER_ONLY, 0.0f, 32);
    s.modifiers.active_mask |= (1u << COLO_MOD_MANTIMAYHEM);
    s.modifiers.tier[COLO_MOD_MANTIMAYHEM] = 2;
    CHECK("rig sanity: beginner starts in the melee set",
        s.weapon_set == COLO_GEAR_MELEE);
    col_mod_manticore_apply_venom(&s, 1);
    CHECK("serp helm blocks venom in the melee set", s.player_venom == 0);
    s.weapon_set = COLO_GEAR_RANGED;   /* crystal helm: no immunity */
    col_mod_manticore_apply_venom(&s, 1);
    CHECK("venom applies in the ranged set", s.player_venom == COLO_VENOM_START);
    s.weapon_set = COLO_GEAR_MELEE;
    int venom_before = s.player_venom;
    col_mod_manticore_apply_venom(&s, 1);
    CHECK("serp helm does not cure or escalate an existing stack",
        s.player_venom == venom_before);

    /* serp helm immunizes poison too, and venom supersedes poison: the two
       share one status slot in OSRS (wiki Serpentine_helm + Venom). */
    s.player_poison = 0;
    s.player_poison_timer = 0;
    col_mod_apply_bee_poison(&s);
    CHECK("serp helm blocks bee poison in the melee set", s.player_poison == 0);
    s.weapon_set = COLO_GEAR_RANGED;
    col_mod_apply_bee_poison(&s);
    CHECK("a venomed player cannot also be poisoned", s.player_poison == 0);
    s.player_venom = 0;
    s.player_venom_timer = 0;
    col_mod_apply_bee_poison(&s);
    CHECK("bee poison applies in the ranged set once venom is gone",
        s.player_poison == COLO_POISON_BEE_CONTACT_SEVERITY);
    col_mod_manticore_apply_venom(&s, 1);
    CHECK("venom application replaces an active poison",
        s.player_venom == COLO_VENOM_START && s.player_poison == 0 &&
        s.player_poison_timer == 0);
}

static void test_loadout_surge_potion(void) {
    printf("test_loadout_surge_potion\n");
    ColosseumContext ctx;
    ColosseumState s;
    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY, 0.0f, 41);
    int idle[COLO_NUM_ACTION_HEADS] = {0};

    /* drink during the open draft (consumables stay live in the gap), with the
       cooldown frozen until gameplay starts (L13). */
    s.player.special_energy = 40;
    int surge[COLO_NUM_ACTION_HEADS] = {0};
    surge[COLO_HEAD_POTION] = COLO_POTION_SURGE;
    step_and_observe(&s, &ctx, surge);
    CHECK("surge restores 25 energy", s.player.special_energy == 65);
    CHECK("surge consumes a dose and arms the cooldown",
        s.surge_doses == 3 && s.surge_cooldown == COLO_SURGE_COOLDOWN_TICKS);
    step_and_observe(&s, &ctx, idle);
    CHECK("surge cooldown frozen during the draft gap",
        s.surge_cooldown == COLO_SURGE_COOLDOWN_TICKS);
    complete_open_draft(&s, &ctx, 1);
    while (s.wave_ready_delay > 0) step_and_observe(&s, &ctx, idle);
    int cd_before = s.surge_cooldown;
    step_and_observe(&s, &ctx, idle);
    CHECK("surge cooldown ticks during live gameplay", s.surge_cooldown == cd_before - 1);

    /* full energy gates the drink */
    s.player.special_energy = 100;
    s.surge_cooldown = 0;
    s.player.potion_timer = 0;
    float mask[COLO_ACTION_MASK_SIZE];
    col_write_mask_ctx((EncounterState*)&s, (EncounterContext*)&ctx, mask);
    int pot_off = col_action_head_mask_offset(COLO_HEAD_POTION);
    CHECK("surge masked at full special energy",
        mask[pot_off + COLO_POTION_SURGE] == 0.0f);
}

static void test_loadout_spec_weapons(void) {
    printf("test_loadout_spec_weapons\n");
    ColosseumContext ctx;
    ColosseumState s;

    /* speedrun spec A = dragon claws: arming + firing drains 50 and queues 4 splats */
    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY, 0.0f, 51);
    geo_clear_npcs(&s);
    s.modifiers.draft_pending = 0;
    s.wave_ready_delay = 0;
    s.player.x = 16; s.player.y = 16;
    col_init_npc(&s, 0, COLO_FREMENNIK_BERSERKER, 16, 17);
    s.player.special_energy = 100;
    s.spec_armed_kind = 1;
    osrs_interaction_set(&s.interaction, 0);
    s.player.attack_timer = 0;
    col_player_attack_target(&s, 0);
    CHECK("claws spec drains 50 energy", s.player.special_energy == 50);
    CHECK("claws spec disarms after firing", s.spec_armed_kind == 0);
    CHECK("claws spec queues the 4-splat cascade",
        s.npcs[0].pending_hits.count == 4);
    CHECK("spec sets the claws attack speed", s.player.attack_timer ==
        get_item(ITEM_DRAGON_CLAWS)->attack_speed);

    /* scythe splat counts: 3 into a 5x5 (Sol-sized), 1 into a 1x1 warbander */
    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY, 0.0f, 54);
    geo_clear_npcs(&s);
    s.modifiers.draft_pending = 0;
    s.wave_ready_delay = 0;
    s.player.x = 10; s.player.y = 16;
    col_init_npc(&s, 0, COLO_SOL_HEREDIT, 11, 14);
    s.player.attack_timer = 0;
    col_player_attack_target(&s, 0);
    CHECK("scythe queues 3 splats into the 5x5 boss",
        s.npcs[0].pending_hits.count == 3);
    geo_clear_npcs(&s);
    col_init_npc(&s, 0, COLO_FREMENNIK_BERSERKER, 10, 17);
    s.player.attack_timer = 0;
    col_player_attack_target(&s, 0);
    CHECK("scythe queues 1 splat into a 1x1 warbander",
        s.npcs[0].pending_hits.count == 1);

    /* speedrun spec B = elder maul: a landed spec drains 35% of current def */
    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY, 0.0f, 52);
    geo_clear_npcs(&s);
    s.modifiers.draft_pending = 0;
    s.wave_ready_delay = 0;
    s.player.x = 16; s.player.y = 16;
    col_init_npc(&s, 0, COLO_FREMENNIK_BERSERKER, 16, 17);
    const ColoNpcStats* zerk = &COLO_NPC_STATS[COLO_FREMENNIK_BERSERKER];
    int tries = 0;
    while (s.npcs[0].def_drained == 0 && tries < 200) {
        s.player.special_energy = 100;
        s.spec_armed_kind = 2;
        s.player.attack_timer = 0;
        s.npcs[0].hp = zerk->hp;
        col_player_attack_target(&s, 0);
        tries++;
    }
    CHECK("elder maul spec eventually lands", s.npcs[0].def_drained > 0);
    CHECK("elder maul drains 35% of current defence",
        s.npcs[0].def_drained == zerk->def_level * 35 / 100);

    /* beginner spec A = SGS: a landed spec heals >= 10 and restores >= 5 prayer */
    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_BEGINNER_ONLY, 0.0f, 53);
    geo_clear_npcs(&s);
    s.modifiers.draft_pending = 0;
    s.wave_ready_delay = 0;
    s.player.x = 16; s.player.y = 16;
    col_init_npc(&s, 0, COLO_FREMENNIK_SEER, 16, 17);
    const ColoNpcStats* seer = &COLO_NPC_STATS[COLO_FREMENNIK_SEER];
    int healed = 0;
    tries = 0;
    while (!healed && tries < 200) {
        s.player.current_hitpoints = 20;
        s.player.current_prayer = 10;
        s.player.special_energy = 100;
        s.spec_armed_kind = 1;
        s.player.attack_timer = 0;
        s.npcs[0].hp = seer->hp;
        col_player_attack_target(&s, 0);
        if (s.player.current_hitpoints > 20) healed = 1;
        tries++;
    }
    CHECK("SGS spec eventually lands", healed);
    CHECK("SGS heal honors the wiki minimum (>= 10)",
        s.player.current_hitpoints >= 30);
    CHECK("SGS prayer restore honors the wiki minimum (>= 5)",
        s.player.current_prayer >= 15);

    /* arming gates on energy; an armed spec pulls the reach to melee */
    s.player.special_energy = 10;
    s.spec_armed_kind = 0;
    int arm[COLO_NUM_ACTION_HEADS] = {0};
    arm[COLO_HEAD_SPEC] = 1;
    col_tick_player_ctx(&s, &ctx, arm, 0);
    CHECK("arming is refused without the energy", s.spec_armed_kind == 0);
    s.player.special_energy = 100;
    col_tick_player_ctx(&s, &ctx, arm, 0);
    CHECK("arming succeeds with the energy", s.spec_armed_kind == 1);
    s.weapon_set = COLO_GEAR_RANGED;
    CHECK("an armed melee spec pulls the reach to 1",
        col_player_attack_range(&s) == 1);
    s.weapon_set = COLO_GEAR_MELEE;
    col_tick_player_ctx(&s, &ctx, arm, 0);
    CHECK("re-pressing the armed spec disarms it", s.spec_armed_kind == 0);
}

static void test_loadout_item_effects(void) {
    printf("test_loadout_item_effects\n");
    ColosseumContext ctx;
    ColosseumState s;

    /* L11 tbow scaling: max hit vs Sol (magic 300) beats max hit vs the jaguar
       (magic 100) with the same base stats. */
    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY, 0.0f, 61);
    const EncounterLoadoutStats* rls = &s.loadout_stats[COLO_GEAR_RANGED];
    int base_att = osrs_player_att_roll(rls->eff_level, rls->attack_bonus);
    const ColoNpcStats* sol = &COLO_NPC_STATS[COLO_SOL_HEREDIT];
    const ColoNpcStats* jag = &COLO_NPC_STATS[COLO_JAGUAR_WARRIOR];
    OsrsPreparedAttackEffects vs_sol = osrs_prepare_attack_effects(
        &s.set_effects[COLO_GEAR_RANGED], &s.player.item_effect_state,
        ITEM_TWISTED_BOW, ATTACK_STYLE_RANGED, OSRS_MAGIC_ATTACK_NONE,
        osrs_target_ref_none(), 1, base_att, rls->max_hit,
        osrs_target_effect_context_magic(sol->magic_level, sol->magic_att_bonus),
        s.player.current_hitpoints, s.player.base_hitpoints);
    OsrsPreparedAttackEffects vs_jag = osrs_prepare_attack_effects(
        &s.set_effects[COLO_GEAR_RANGED], &s.player.item_effect_state,
        ITEM_TWISTED_BOW, ATTACK_STYLE_RANGED, OSRS_MAGIC_ATTACK_NONE,
        osrs_target_ref_none(), 1, base_att, rls->max_hit,
        osrs_target_effect_context_magic(jag->magic_level, jag->magic_att_bonus),
        s.player.current_hitpoints, s.player.base_hitpoints);
    CHECK("tbow hits harder into Sol's 300 magic than the jaguar's 100",
        vs_sol.max_hit > vs_jag.max_hit && vs_sol.attack_roll > vs_jag.attack_roll);

    /* L11 crystal armour: bowfa + full crystal = x26/20 accuracy, x46/40 damage */
    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_BEGINNER_ONLY, 0.0f, 62);
    const EncounterLoadoutStats* bls = &s.loadout_stats[COLO_GEAR_RANGED];
    int bowfa_att = osrs_player_att_roll(bls->eff_level, bls->attack_bonus);
    OsrsPreparedAttackEffects bowfa = osrs_prepare_attack_effects(
        &s.set_effects[COLO_GEAR_RANGED], &s.player.item_effect_state,
        ITEM_BOW_OF_FAERDHINEN, ATTACK_STYLE_RANGED, OSRS_MAGIC_ATTACK_NONE,
        osrs_target_ref_none(), 1, bowfa_att, bls->max_hit,
        osrs_target_effect_context_magic(100, 0),
        s.player.current_hitpoints, s.player.base_hitpoints);
    CHECK("crystal armour scales the bowfa damage by 46/40",
        bowfa.max_hit == bls->max_hit * 46 / 40);
    CHECK("crystal armour scales the bowfa accuracy by 26/20",
        bowfa.attack_roll == bowfa_att * 26 / 20);

    /* blood fury: ~20% of melee damage events heal 30% of the damage */
    int procs = 0;
    uint32_t rng = 777;
    for (int i = 0; i < 400; i++) {
        OsrsPostAttackEffects post = osrs_finalize_attack_effects(
            &s.set_effects[COLO_GEAR_MELEE], &s.player.item_effect_state,
            ITEM_OSMUMTENS_FANG, ATTACK_STYLE_MELEE, OSRS_MAGIC_ATTACK_NONE,
            osrs_target_ref_none(), 1, 0, 1, 30, &rng);
        if (post.heal_amount > 0) {
            procs++;
            CHECK("blood fury heals 30% of the damage", post.heal_amount == 9);
            if (post.heal_amount != 9) break;
        }
    }
    CHECK("blood fury procs at a plausible 20% rate", procs > 40 && procs < 130);

    /* no blood fury on the ranged set */
    OsrsPostAttackEffects ranged_post = osrs_finalize_attack_effects(
        &s.set_effects[COLO_GEAR_RANGED], &s.player.item_effect_state,
        ITEM_BOW_OF_FAERDHINEN, ATTACK_STYLE_RANGED, OSRS_MAGIC_ATTACK_NONE,
        osrs_target_ref_none(), 1, 0, 1, 30, &rng);
    CHECK("no blood fury heal on the ranged set", ranged_post.heal_amount == 0);
}

static void test_loadout_offensive_prayers(void) {
    printf("test_loadout_offensive_prayers\n");
    ColosseumContext ctx;
    ColosseumState s;
    loadout_reset(&s, &ctx, COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY, 0.0f, 71);
    complete_open_draft(&s, &ctx, 1);
    int base_max_hit = s.loadout_stats[COLO_GEAR_MELEE].max_hit;

    int piety[COLO_NUM_ACTION_HEADS] = {0};
    piety[COLO_HEAD_OFFENSIVE] = ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY;
    step_and_observe(&s, &ctx, piety);
    CHECK("piety activates", s.player.offensive_prayer == OFFENSIVE_PRAYER_PIETY);
    CHECK("piety raises the melee max hit (L12)",
        s.loadout_stats[COLO_GEAR_MELEE].max_hit > base_max_hit);
    CHECK("piety raises the spec max hits too",
        s.spec_stats[0].max_hit > 0 && s.spec_stats[1].max_hit > 0);

    int off[COLO_NUM_ACTION_HEADS] = {0};
    off[COLO_HEAD_OFFENSIVE] = ENCOUNTER_OFFENSIVE_OFF;
    step_and_observe(&s, &ctx, off);
    CHECK("offensive off restores the base max hit",
        s.player.offensive_prayer == OFFENSIVE_PRAYER_NONE &&
        s.loadout_stats[COLO_GEAR_MELEE].max_hit == base_max_hit);

    float mask[COLO_ACTION_MASK_SIZE];
    col_write_mask_ctx((EncounterState*)&s, (EncounterContext*)&ctx, mask);
    int off_offset = col_action_head_mask_offset(COLO_HEAD_OFFENSIVE);
    CHECK("augury stays masked (no magic set, L1)",
        mask[off_offset + ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY] == 0.0f);
    CHECK("piety and rigour are offered",
        mask[off_offset + ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY] == 1.0f &&
        mask[off_offset + ENCOUNTER_OFFENSIVE_SET_REFRESH_RIGOUR] == 1.0f);
}

static void test_step_out_forecast_manticore_armed_pattern(void) {
    printf("test_step_out_forecast_manticore_armed_pattern\n");
    ColosseumContext ctx;
    ColosseumState s;
    init_forecast_test_state(&s, &ctx, 401, 17, 16);
    col_init_npc(&s, 0, COLO_MANTICORE, 16, 12);
    s.npcs[0].attack_timer = 1;
    ColoManticoreState* mc = colo_npc_manticore(&s.npcs[0]);
    mc->cycle_step = -1;
    mc->orb_style[0] = ATTACK_STYLE_MAGIC;
    mc->orb_style[1] = ATTACK_STYLE_RANGED;
    mc->orb_style[2] = ATTACK_STYLE_MELEE;

    ColoStepOutForecast forecast;
    col_build_step_out_forecast_ctx(&s, &forecast);
    const ColoStepOutForecastAction* idle = &forecast.actions[0];
    CHECK("armed manticore idle forecast is valid", idle->valid == 1);
    CHECK("armed manticore orb 0 records magic on tick 1",
        idle->ticks[0].magic_count == 1 && idle->ticks[0].max_hit == COLO_MANTICORE_MAX_HIT_MAGIC);
    CHECK("armed manticore orb 1 records ranged on tick 2",
        idle->ticks[1].ranged_count == 1 && idle->ticks[1].max_hit == COLO_MANTICORE_MAX_HIT_RANGED);
    CHECK("armed manticore orb 2 records melee on tick 3",
        idle->ticks[2].melee_count == 1 && idle->melee_fallback_exposure == 1);
}

static void test_step_out_forecast_warband_window_and_break(void) {
    printf("test_step_out_forecast_warband_window_and_break\n");
    ColosseumContext ctx;
    ColosseumState s;
    init_forecast_test_state(&s, &ctx, 402, 7, 18);
    s.tick = 100;
    s.warband_cycle_anchor = 100;
    col_init_npc(&s, 0, COLO_FREMENNIK_BERSERKER, 8, 18);

    ColoStepOutForecast forecast;
    col_build_step_out_forecast_ctx(&s, &forecast);
    int run_west = forecast_move_action_for_delta(-2, 0);
    CHECK("adjacent berserker records melee on its next window",
        forecast.actions[0].ticks[0].melee_count == 1);
    CHECK("running west breaks the berserker forecast adjacency",
        !forecast_action_has_event(&forecast.actions[run_west]));
}

static void test_step_out_forecast_ranged_los_candidate_tiles(void) {
    printf("test_step_out_forecast_ranged_los_candidate_tiles\n");
    ColosseumContext ctx;
    ColosseumState s;
    init_forecast_test_state(&s, &ctx, 403, 7, 9);
    col_init_npc(&s, 0, COLO_SERPENT_SHAMAN, 12, 12);
    s.npcs[0].attack_timer = 1;

    ColoStepOutForecast forecast;
    col_build_step_out_forecast_ctx(&s, &forecast);
    int run_north = forecast_move_action_for_delta(0, 2);
    CHECK("pillar-blocked idle tile records no shaman forecast",
        !forecast_action_has_event(&forecast.actions[0]));
    CHECK("clear run-north tile records the shaman magic forecast",
        forecast.actions[run_north].ticks[0].magic_count == 1);
}

static void test_step_out_forecast_valid_flags(void) {
    printf("test_step_out_forecast_valid_flags\n");
    ColosseumContext ctx;
    ColosseumState s;
    init_forecast_test_state(&s, &ctx, 404, 7, 9);

    ColoStepOutForecast forecast;
    col_build_step_out_forecast_ctx(&s, &forecast);
    int walk_east = forecast_move_action_for_delta(1, 0);
    int walk_west = forecast_move_action_for_delta(-1, 0);
    CHECK("pillar move has invalid step-out forecast flag",
        forecast.actions[walk_east].valid == 0);
    CHECK("clear move has valid step-out forecast flag",
        forecast.actions[walk_west].valid == 1);
}

static void test_step_out_forecast_same_tick_mixed_styles(void) {
    printf("test_step_out_forecast_same_tick_mixed_styles\n");
    ColosseumContext ctx;
    ColosseumState s;
    init_forecast_test_state(&s, &ctx, 405, 17, 16);
    col_init_npc(&s, 0, COLO_SERPENT_SHAMAN, 13, 16);
    col_init_npc(&s, 1, COLO_JAVELIN_COLOSSUS, 20, 15);
    s.npcs[0].attack_timer = 1;
    s.npcs[1].attack_timer = 1;

    ColoStepOutForecast forecast;
    col_build_step_out_forecast_ctx(&s, &forecast);
    const ColoStepOutForecastAction* idle = &forecast.actions[0];
    CHECK("same tick magic and ranged forecast conflict is flagged",
        idle->same_tick_mixed_style_conflict == 1);
    CHECK("same tick magic and ranged counts are both recorded",
        idle->ticks[0].magic_count == 1 && idle->ticks[0].ranged_count == 1);
}

int main(void) {
    test_fuzz_obs_mask();
    test_zero_actions_hit_timeout();
    test_offpray_attribution_log();
    test_step_loop_draft();
    test_twelve_drafts_per_run();
    test_solarflare_orb();
    test_volatility_explosion();
    test_draft_offer_and_select();
    test_draft_upgrade_bias();
    test_mantimayhem_stress();
    test_frailty_hp();
    test_relentless_damage();
    test_relentless_def_level_bypass();
    test_quartet_extra_spawn();
    test_bees_hazard();
    test_totem_lifecycle();
    test_totemic_sol_wave12();
    test_reentry_sand_tiles();
    test_venom_escalation();
    test_bee_poison_status();
    test_mantimayhem_t3_shuffle();
    test_static_arena_mask();
    test_static_los_and_attack_gate();
    test_spawn_anchor_exclusion();
    test_reinforcement_gates();
    test_roster_cap_nine();
    test_wave12_quartet_and_win();
    test_warband_cycle_offsets();
    test_warband_move_skip();
    test_warband_melee_distance_gate();
    test_warband_formation_convergence();
    test_warband_two_tile_speed();
    test_warband_pillar_routefind_vs_shaman_safespot();
    test_red_flag_minotaur_routefind();
    test_minotaur_heal_semantics();
    test_manticore_barrage_period();
    test_manticore_telegraph_during_windup();
    test_manticore_orb_same_tick_flick();
    test_manticore_pattern_copy();
    test_javelin_skyfall_no_defence_gate();
    test_sol_adjacency_gate_and_kiting();
    test_sol_attack_selection_invariants();
    test_sol_parry_schedule_and_damage();
    test_sol_parry_prayer_punish();
    test_sol_grapple_perfect_parry();
    test_sol_perfect_parry_forces_spec_attack();
    test_sol_shield_safe_rings();
    test_sol_spear_lines();
    test_sol_crystal_lifecycle();
    test_sol_phase_transition_sand_guarantees();
    test_sol_beams_become_pools();
    test_loadout_profiles_and_supplies();
    test_loadout_consumables();
    test_loadout_divine_potions_and_stat_drift();
    test_loadout_sanfew_and_serp_helm();
    test_loadout_surge_potion();
    test_loadout_spec_weapons();
    test_loadout_item_effects();
    test_loadout_offensive_prayers();
    test_step_out_forecast_manticore_armed_pattern();
    test_step_out_forecast_warband_window_and_break();
    test_step_out_forecast_ranged_los_candidate_tiles();
    test_step_out_forecast_valid_flags();
    test_step_out_forecast_same_tick_mixed_styles();

    printf("\n%d/%d passed", tests_passed, tests_run);
    if (tests_failed) printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
