/**
 * @file test_colosseum_modifiers.c
 * @brief Regression tests for the Fortis Colosseum modifier system.
 *
 * Two layers:
 *   1. a fuzz loop that drives col_step + col_write_obs + col_write_mask every
 *      tick across full runs, so the obs/mask running-index asserts fire on real
 *      state (the standalone --profile harness skips the obs writer);
 *   2. deterministic scenario checks that each modifier mechanic actually fires:
 *      draft offer + selection + persistence, Frailty max-HP cut, Relentless
 *      damage uplift, Quartet extra warbander, and a per-tick hazard (Bees);
 *   3. P1 arena geometry: the los `Lr` wall-mask port (hardcoded row extents),
 *      pillar blocking, spawn-anchor placement + the B3 player-proximity
 *      exclusion, gate-gap reinforcements with the b5 yellow-line side rule,
 *      static-mask line of sight + the ranged attack gate, and the wave-12
 *      quartet-reachability + Sol-death-wins predicate.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/test_colosseum_modifiers \
 *       ocean/osrs/tests/test_colosseum_modifiers.c -lm
 *   /tmp/test_colosseum_modifiers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---- 1. fuzz: random actions over many full runs, obs/mask asserted each tick.
   Validates the obs/mask running-index asserts + crash-freedom across the boss and
   all waves (the standalone --profile harness never calls the obs writer). */
static void test_fuzz_obs_mask(void) {
    printf("test_fuzz_obs_mask\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 0;

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

/* ---- 1b. step-loop draft: clear wave 1 through the real step loop, confirm a
   draft opens during the inter-wave gap, then a MODIFIER_SELECT action activates a
   modifier that persists into the next wave. */
static void test_step_loop_draft(void) {
    printf("test_step_loop_draft\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 0;
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 42);

    int idle[COLO_NUM_ACTION_HEADS] = {0};

    /* kill every live NPC so the next step registers a wave clear. */
    for (int i = 0; i < COLO_MAX_NPCS; i++)
        if (s.npcs[i].active) s.npcs[i].hp = 0, s.npcs[i].active = 0;
    step_and_observe(&s, &ctx, idle);
    CHECK("wave-1 clear opened a draft", draft_is_open(&s));

    /* pick option 0 via the MODIFIER_SELECT head (index 1 = option 0). */
    int chosen = s.modifiers.draft_options[0];
    int pick[COLO_NUM_ACTION_HEADS] = {0};
    pick[COLO_HEAD_MODIFIER_SELECT] = 1;
    step_and_observe(&s, &ctx, pick);
    CHECK("MODIFIER_SELECT activated the chosen modifier",
        chosen >= 0 && col_mod_active(&s, (ColoModifier)chosen));
    CHECK("draft closed after the action selection", !s.modifiers.draft_pending);

    /* run out the inter-wave gap into wave 2 and confirm persistence. */
    for (int t = 0; t < 20 && s.wave == 0; t++) step_and_observe(&s, &ctx, idle);
    CHECK("advanced to wave 2", s.wave == 1);
    CHECK("modifier persisted into wave 2",
        chosen >= 0 && col_mod_active(&s, (ColoModifier)chosen));
}

/* ---- 2a. draft offer, selection, persistence, last-wave gate. */
static void test_draft_offer_and_select(void) {
    printf("test_draft_offer_and_select\n");
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ColosseumState s;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 7);

    /* a fresh run starts with zero modifiers. */
    CHECK("fresh run has no active modifiers", s.modifiers.active_mask == 0);
    CHECK("fresh run draft closed", s.modifiers.draft_pending == 0);

    /* offer a draft as if wave 1 (index 0) was cleared. */
    col_modifier_offer_draft(&s, 0);
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

    /* pre-boss-only modifiers are not offered going into wave 12. */
    col_modifier_offer_draft(&s, COLO_MODIFIER_LAST_WAVE);
    int offered_pre_boss_only = 0;
    for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) {
        int m = s.modifiers.draft_options[o];
        if (m >= 0 && COLO_MODIFIER_PRE_BOSS_ONLY[m]) offered_pre_boss_only = 1;
    }
    CHECK("no pre-boss-only modifier offered before wave 12", offered_pre_boss_only == 0);
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

/* ---- 2e. Bees spawn and converge, dealing contact damage. */
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

    int active_bees = 0;
    for (int b = 0; b < COLO_MAX_BEE_SWARMS; b++) if (s.bees[b].active) active_bees++;
    CHECK("Bees II spawns two swarms", active_bees == 2);

    /* park a swarm on the player and tick: contact must deal unblockable damage. */
    s.bees[0].x = s.player.x;
    s.bees[0].y = s.player.y;
    s.bees[0].move_timer = COLO_BEE_MOVE_INTERVAL;
    int hp_before = s.player.current_hitpoints;
    int damaged = 0;
    for (int t = 0; t < 64 && !damaged; t++) {
        s.bees[0].x = s.player.x;
        s.bees[0].y = s.player.y;
        col_mod_tick_bees(&s);
        if (s.player.current_hitpoints < hp_before) damaged = 1;
        hp_before = s.player.current_hitpoints;
        if (s.player.current_hitpoints <= 0) break;
    }
    CHECK("a bee swarm on the player deals contact damage", damaged);
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

/* ---- 2g. Solarflare orb moves around the ring and damages on contact. */
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

    /* the orb visits distinct tiles over time (it is not stuck). */
    int ox = s.solarflare.x, oy = s.solarflare.y;
    int moved = 0;
    for (int t = 0; t < 40 && !moved; t++) {
        col_mod_tick_solarflare(&s);
        if (s.solarflare.x != ox || s.solarflare.y != oy) moved = 1;
    }
    CHECK("Solarflare orb moves around the ring", moved);

    /* parking the player on the orb tile takes contact damage (T3 also drops prayer). */
    s.modifiers.tier[COLO_MOD_SOLARFLARE] = 3;
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

/* clear every NPC and its collision stamps so geometry checks run on an empty
   arena (mirrors what col_spawn_wave does before placing a roster). */
static void geo_clear_npcs(ColosseumState* s) {
    memset(s->npcs, 0, sizeof(s->npcs));
    memset(s->npc_collision_flags, 0, sizeof(s->npc_collision_flags));
    col_rebuild_player_collision_flags(s);
}

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
   centre-to-centre rays; an archer without LoS holds fire and chases instead. */
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

    /* archer at (13,9) vs player at (5,9): pillar (8..10, 8..10) between them. */
    s.player.x = 5; s.player.y = 9;
    col_init_npc(&s, 0, COLO_FREMENNIK_ARCHER, 13, 9);
    s.npcs[0].attack_timer = 0;
    CHECK("archer behind the pillar has no LoS", !col_npc_has_los_to_player(&s, &s.npcs[0]));
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("no-LoS archer holds fire", s.npcs[0].attacked_this_tick == 0);
    col_npc_move_ctx(&s, &ctx, 0);
    CHECK("no-LoS archer steps toward the player instead", s.npcs[0].moved_this_tick == 1);

    /* same range with a clear row: the attack fires. */
    geo_clear_npcs(&s);
    s.player.x = 5; s.player.y = 12;
    col_init_npc(&s, 0, COLO_FREMENNIK_ARCHER, 13, 12);
    s.npcs[0].attack_timer = 0;
    CHECK("clear-row archer has LoS", col_npc_has_los_to_player(&s, &s.npcs[0]));
    col_npc_attack_ctx(&s, &ctx, 0);
    CHECK("clear-row archer attacks", s.npcs[0].attacked_this_tick == 1);
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

int main(void) {
    test_fuzz_obs_mask();
    test_step_loop_draft();
    test_solarflare_orb();
    test_volatility_explosion();
    test_draft_offer_and_select();
    test_mantimayhem_stress();
    test_frailty_hp();
    test_relentless_damage();
    test_quartet_extra_spawn();
    test_bees_hazard();
    test_static_arena_mask();
    test_static_los_and_attack_gate();
    test_spawn_anchor_exclusion();
    test_reinforcement_gates();
    test_roster_cap_nine();
    test_wave12_quartet_and_win();

    printf("\n%d/%d passed", tests_passed, tests_run);
    if (tests_failed) printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
