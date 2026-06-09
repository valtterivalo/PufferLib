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
 *      damage uplift, Quartet extra warbander, and a per-tick hazard (Bees).
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

    printf("\n%d/%d passed", tests_passed, tests_run);
    if (tests_failed) printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
