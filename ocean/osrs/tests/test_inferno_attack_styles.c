/**
 * @file test_inferno_attack_styles.c
 * @brief regression tests for inferno NPC attack-style selection and melee
 * fallback geometry.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/test_inferno_attack_styles \
 *       ocean/osrs/tests/test_inferno_attack_styles.c -lm
 *   /tmp/test_inferno_attack_styles
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ocean/osrs/encounters/encounter_inferno.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_INT_EQ(label, actual, expected) do { \
    tests_run++; \
    if ((actual) == (expected)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s — got %d, expected %d\n", (label), (actual), (expected)); \
    } \
} while (0)

#define ASSERT_FLOAT_NEAR(label, actual, expected, tol) do { \
    tests_run++; \
    float diff = (float)((actual) - (expected)); \
    if (diff < 0.0f) diff = -diff; \
    if (diff <= (tol)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s — got %.6f, expected %.6f (tol %.6f)\n", \
            (label), (float)(actual), (float)(expected), (float)(tol)); \
    } \
} while (0)

static InfernoState make_test_state(int player_x, int player_y) {
    InfernoState state;
    memset(&state, 0, sizeof(state));
    memset(state.npc_los_cache, -1, sizeof(state.npc_los_cache));
    state.player.x = player_x;
    state.player.y = player_y;
    state.player_last_interaction_target_slot = -1;
    state.player_last_interaction_age = 1;
    return state;
}

static InfNPC make_test_npc(InfNPCType type, int x, int y, int size) {
    InfNPC npc;
    memset(&npc, 0, sizeof(npc));
    npc.type = type;
    npc.x = x;
    npc.y = y;
    npc.size = size;
    npc.aggro_target = -1;
    npc.attack_visual_target = -1;
    npc.jad_owner_idx = -1;
    npc.blob_scanned_prayer = -1;
    npc.jad_attack_style = ATTACK_STYLE_NONE;
    return npc;
}

static InfNPCStats make_test_stats(int default_style) {
    InfNPCStats stats;
    memset(&stats, 0, sizeof(stats));
    stats.default_style = default_style;
    stats.can_melee = 1;
    return stats;
}

static HumanInput make_human_input(void) {
    HumanInput input;
    memset(&input, 0, sizeof(input));
    input.pending_move_x = -1;
    input.pending_move_y = -1;
    input.pending_prayer = -1;
    input.pending_offensive_prayer = -1;
    input.pending_target_idx = -1;
    return input;
}

static int force_mager_resurrect(InfernoState* s, int idx) {
    for (uint32_t seed = 1; seed < 100000; seed++) {
        InfernoState probe = *s;
        probe.rng_state = seed;
        if (inf_mager_resurrect(&probe, idx)) {
            s->rng_state = seed;
            return inf_mager_resurrect(s, idx);
        }
    }
    return 0;
}

static int distance_to_player(const InfernoState* state, const InfNPC* npc) {
    return encounter_dist_to_npc(
        state->player.x, state->player.y, npc->x, npc->y, npc->size);
}

static void test_tagged_jad_healer_melee_geometry(void) {
    printf("--- tagged jad healer melee geometry ---\n");

    InfernoState diagonal_state = make_test_state(5, 5);
    InfernoState cardinal_state = make_test_state(5, 5);

    diagonal_state.player.current_defence = 99;
    diagonal_state.player.current_magic = 99;
    diagonal_state.player.prayer = PRAYER_NONE;
    diagonal_state.weapon_set = INF_GEAR_MAGE;

    cardinal_state.player.current_defence = 99;
    cardinal_state.player.current_magic = 99;
    cardinal_state.player.prayer = PRAYER_NONE;
    cardinal_state.weapon_set = INF_GEAR_MAGE;

    diagonal_state.npcs[0] = make_test_npc(INF_NPC_HEALER_JAD, 6, 6, 1);
    diagonal_state.npcs[0].active = 1;
    diagonal_state.npcs[0].aggro_target = -1;

    cardinal_state.npcs[0] = make_test_npc(INF_NPC_HEALER_JAD, 6, 5, 1);
    cardinal_state.npcs[0].active = 1;
    cardinal_state.npcs[0].aggro_target = -1;

    inf_npc_attack(&diagonal_state, 0);
    inf_npc_attack(&cardinal_state, 0);

    ASSERT_INT_EQ("diagonal healer does not attack", diagonal_state.npcs[0].attacked_this_tick, 0);
    ASSERT_INT_EQ("diagonal healer keeps attack style none",
                  diagonal_state.npcs[0].attack_style_this_tick, ATTACK_STYLE_NONE);
    ASSERT_INT_EQ("cardinal healer attacks", cardinal_state.npcs[0].attacked_this_tick, 1);
    ASSERT_INT_EQ("cardinal healer uses melee",
                  cardinal_state.npcs[0].attack_style_this_tick, ATTACK_STYLE_MELEE);
}

static void test_overlap_shuffle_hold_after_recent_target_click(void) {
    printf("--- overlap shuffle held after recent target click ---\n");

    InfernoState state = make_test_state(20, 20);
    state.player_last_interaction_target_slot = 0;
    state.player_last_interaction_age = 0;

    state.npcs[0] = make_test_npc(
        INF_NPC_RANGER, 20, 20, INF_NPC_STATS[INF_NPC_RANGER].size);
    state.npcs[0].active = 1;

    inf_npc_move(&state, 0);

    ASSERT_INT_EQ("held overlap keeps x", state.npcs[0].x, 20);
    ASSERT_INT_EQ("held overlap keeps y", state.npcs[0].y, 20);
    ASSERT_INT_EQ("held overlap does not mark moved", state.npcs[0].moved_this_tick, 0);
}

static void test_overlap_shuffle_respects_npc_occupancy(void) {
    printf("--- overlap shuffle respects npc occupancy ---\n");

    InfernoState state = make_test_state(20, 20);
    state.rng_state = 12345;

    state.npcs[0] = make_test_npc(INF_NPC_HEALER_JAD, 20, 20, 1);
    state.npcs[0].active = 1;
    state.npcs[1] = make_test_npc(INF_NPC_HEALER_JAD, 21, 20, 1);
    state.npcs[1].active = 1;
    state.npcs[2] = make_test_npc(INF_NPC_HEALER_JAD, 19, 20, 1);
    state.npcs[2].active = 1;
    state.npcs[3] = make_test_npc(INF_NPC_HEALER_JAD, 20, 21, 1);
    state.npcs[3].active = 1;

    inf_rebuild_occupancy(&state);
    inf_npc_move(&state, 0);

    ASSERT_INT_EQ("overlap shuffle picks the only free tile x", state.npcs[0].x, 20);
    ASSERT_INT_EQ("overlap shuffle picks the only free tile y", state.npcs[0].y, 19);
}

static void test_meleer_dig_landing_order(void) {
    printf("--- meleer dig landing order ---\n");

    InfernoState state = make_test_state(20, 20);
    state.npcs[0] = make_test_npc(
        INF_NPC_MELEER, 5, 5, INF_NPC_STATS[INF_NPC_MELEER].size);
    state.npcs[0].active = 1;
    state.npcs[0].dig_freeze_timer = 1;

    state.pillars[0].active = 1;
    state.pillars[0].x = 17;
    state.pillars[0].y = 17;

    inf_meleer_dig_check(&state, 0);

    ASSERT_INT_EQ("blocked first landing candidate falls through to player tile x", state.npcs[0].x, 20);
    ASSERT_INT_EQ("blocked first landing candidate falls through to player tile y", state.npcs[0].y, 20);
    ASSERT_INT_EQ("dig freeze consumed", state.npcs[0].dig_freeze_timer, 0);
    ASSERT_INT_EQ("post-dig stun applied", state.npcs[0].stun_timer, 2);
    ASSERT_INT_EQ("post-dig attack delay applied", state.npcs[0].dig_attack_delay, 6);
}

static void test_melee_fallback_geometry(void) {
    printf("--- inferno melee fallback geometry ---\n");

    InfernoState diagonal_state = make_test_state(5, 5);
    InfernoState cardinal_state = make_test_state(5, 5);
    InfernoState distant_state = make_test_state(5, 5);

    InfNPC ranger_diagonal = make_test_npc(INF_NPC_RANGER, 6, 6, 1);
    InfNPC mager_diagonal = make_test_npc(INF_NPC_MAGER, 6, 6, 1);
    InfNPC blob_diagonal = make_test_npc(INF_NPC_BLOB, 6, 6, 1);
    InfNPC blob_cardinal = make_test_npc(INF_NPC_BLOB, 6, 5, 1);
    InfNPC jad_diagonal = make_test_npc(INF_NPC_JAD, 6, 6, 1);
    InfNPC jad_cardinal = make_test_npc(INF_NPC_JAD, 6, 5, 1);
    InfNPC blob_distant = make_test_npc(INF_NPC_BLOB, 7, 5, 1);

    InfNPCStats ranged_stats = make_test_stats(ATTACK_STYLE_RANGED);
    InfNPCStats magic_stats = make_test_stats(ATTACK_STYLE_MAGIC);

    ASSERT_INT_EQ(
        "ranger diagonal melee fallback",
        inf_melee_fallback_possible(
            &diagonal_state, &ranger_diagonal, &ranged_stats,
            ATTACK_STYLE_RANGED, distance_to_player(&diagonal_state, &ranger_diagonal)),
        1);
    ASSERT_INT_EQ(
        "mager diagonal melee fallback",
        inf_melee_fallback_possible(
            &diagonal_state, &mager_diagonal, &magic_stats,
            ATTACK_STYLE_MAGIC, distance_to_player(&diagonal_state, &mager_diagonal)),
        1);
    ASSERT_INT_EQ(
        "blob diagonal melee fallback blocked",
        inf_melee_fallback_possible(
            &diagonal_state, &blob_diagonal, &magic_stats,
            ATTACK_STYLE_MAGIC, distance_to_player(&diagonal_state, &blob_diagonal)),
        0);
    ASSERT_INT_EQ(
        "blob cardinal melee fallback",
        inf_melee_fallback_possible(
            &cardinal_state, &blob_cardinal, &magic_stats,
            ATTACK_STYLE_MAGIC, distance_to_player(&cardinal_state, &blob_cardinal)),
        1);
    ASSERT_INT_EQ(
        "jad diagonal melee fallback blocked",
        inf_melee_fallback_possible(
            &diagonal_state, &jad_diagonal, &ranged_stats,
            ATTACK_STYLE_RANGED, distance_to_player(&diagonal_state, &jad_diagonal)),
        0);
    ASSERT_INT_EQ(
        "jad cardinal melee fallback",
        inf_melee_fallback_possible(
            &cardinal_state, &jad_cardinal, &ranged_stats,
            ATTACK_STYLE_RANGED, distance_to_player(&cardinal_state, &jad_cardinal)),
        1);
    ASSERT_INT_EQ(
        "fallback blocked outside melee distance",
        inf_melee_fallback_possible(
            &distant_state, &blob_distant, &magic_stats,
            ATTACK_STYLE_MAGIC, distance_to_player(&distant_state, &blob_distant)),
        0);
    ASSERT_INT_EQ(
        "fallback blocked when planned style already melee",
        inf_melee_fallback_possible(
            &cardinal_state, &blob_cardinal, &magic_stats,
            ATTACK_STYLE_MELEE, distance_to_player(&cardinal_state, &blob_cardinal)),
        0);
}

static void test_style_mask_preview(void) {
    printf("--- inferno style mask preview ---\n");

    InfernoState diagonal_state = make_test_state(5, 5);
    InfernoState cardinal_state = make_test_state(5, 5);
    InfNPC ranger_diagonal = make_test_npc(INF_NPC_RANGER, 6, 6, 1);
    InfNPC blob_cardinal = make_test_npc(INF_NPC_BLOB, 6, 5, 1);
    InfNPC blob_diagonal = make_test_npc(INF_NPC_BLOB, 6, 6, 1);
    InfNPCStats ranged_stats = make_test_stats(ATTACK_STYLE_RANGED);
    InfNPCStats magic_stats = make_test_stats(ATTACK_STYLE_MAGIC);

    int ranger_mask = inf_attack_style_options_mask(
        &diagonal_state, &ranger_diagonal, &ranged_stats,
        ATTACK_STYLE_RANGED, distance_to_player(&diagonal_state, &ranger_diagonal));
    int blob_cardinal_mask = inf_attack_style_options_mask(
        &cardinal_state, &blob_cardinal, &magic_stats,
        ATTACK_STYLE_MAGIC, distance_to_player(&cardinal_state, &blob_cardinal));
    int blob_diagonal_mask = inf_attack_style_options_mask(
        &diagonal_state, &blob_diagonal, &magic_stats,
        ATTACK_STYLE_MAGIC, distance_to_player(&diagonal_state, &blob_diagonal));

    ASSERT_INT_EQ(
        "ranger diagonal preview mask",
        ranger_mask,
        INF_STYLE_MASK_MELEE | INF_STYLE_MASK_RANGED);
    ASSERT_INT_EQ(
        "ranger diagonal preview style is uncertain",
        inf_attack_style_from_mask(ranger_mask),
        ATTACK_STYLE_NONE);
    ASSERT_INT_EQ(
        "ranger diagonal obs preview keeps ranged primary",
        inf_attack_style_obs_preview(ranger_mask),
        ATTACK_STYLE_RANGED);
    ASSERT_INT_EQ(
        "blob cardinal preview mask",
        blob_cardinal_mask,
        INF_STYLE_MASK_MELEE | INF_STYLE_MASK_MAGIC);
    ASSERT_INT_EQ(
        "blob cardinal obs preview keeps magic primary",
        inf_attack_style_obs_preview(blob_cardinal_mask),
        ATTACK_STYLE_MAGIC);
    ASSERT_INT_EQ(
        "blob diagonal preview keeps magic only",
        blob_diagonal_mask,
        INF_STYLE_MASK_MAGIC);
    ASSERT_INT_EQ(
        "blob diagonal preview style is magic",
        inf_attack_style_from_mask(blob_diagonal_mask),
        ATTACK_STYLE_MAGIC);
}

static void test_style_choice_sampling(void) {
    printf("--- inferno style choice sampling ---\n");

    uint32_t rng_state = 12345;
    int saw_melee = 0;
    int saw_ranged = 0;

    for (int i = 0; i < 128; i++) {
        int style = inf_choose_attack_style_for_tick(
            &rng_state, INF_STYLE_MASK_MELEE | INF_STYLE_MASK_RANGED);
        if (style == ATTACK_STYLE_MELEE) saw_melee = 1;
        if (style == ATTACK_STYLE_RANGED) saw_ranged = 1;
    }

    ASSERT_INT_EQ("50-50 branch can emit melee", saw_melee, 1);
    ASSERT_INT_EQ("50-50 branch can emit primary style", saw_ranged, 1);
    ASSERT_INT_EQ(
        "single-style mask stays deterministic",
        inf_choose_attack_style_for_tick(&rng_state, INF_STYLE_MASK_MAGIC),
        ATTACK_STYLE_MAGIC);
}

static void test_dead_mob_store_eligibility(void) {
    printf("--- inferno dead mob resurrection eligibility ---\n");

    ASSERT_INT_EQ("bat resurrectable", inf_dead_mob_is_resurrectable(INF_NPC_BAT), 1);
    ASSERT_INT_EQ("blob parent resurrectable", inf_dead_mob_is_resurrectable(INF_NPC_BLOB), 1);
    ASSERT_INT_EQ("meleer resurrectable", inf_dead_mob_is_resurrectable(INF_NPC_MELEER), 1);
    ASSERT_INT_EQ("ranger resurrectable", inf_dead_mob_is_resurrectable(INF_NPC_RANGER), 1);
    ASSERT_INT_EQ("mager resurrectable", inf_dead_mob_is_resurrectable(INF_NPC_MAGER), 1);

    ASSERT_INT_EQ("nibbler not resurrectable", inf_dead_mob_is_resurrectable(INF_NPC_NIBBLER), 0);
    ASSERT_INT_EQ("blob melee split not resurrectable", inf_dead_mob_is_resurrectable(INF_NPC_BLOB_MELEE), 0);
    ASSERT_INT_EQ("blob range split not resurrectable", inf_dead_mob_is_resurrectable(INF_NPC_BLOB_RANGE), 0);
    ASSERT_INT_EQ("blob mage split not resurrectable", inf_dead_mob_is_resurrectable(INF_NPC_BLOB_MAGE), 0);
    ASSERT_INT_EQ("jad not resurrectable", inf_dead_mob_is_resurrectable(INF_NPC_JAD), 0);
}

static void test_resurrected_mob_does_not_reenter_dead_store(void) {
    printf("--- resurrected mob does not reenter dead store ---\n");

    InfernoState state = make_test_state(25, 16);
    state.wave = 35;

    state.npcs[0] = make_test_npc(INF_NPC_MAGER, 20, 20, INF_NPC_STATS[INF_NPC_MAGER].size);
    state.npcs[0].active = 1;
    state.npcs[0].hp = state.npcs[0].max_hp = INF_NPC_STATS[INF_NPC_MAGER].hp;

    state.dead_mobs[0].type = INF_NPC_RANGER;
    state.dead_mobs[0].x = 18;
    state.dead_mobs[0].y = 18;
    state.dead_mobs[0].hp = INF_NPC_STATS[INF_NPC_RANGER].hp / 2;
    state.dead_mobs[0].max_hp = INF_NPC_STATS[INF_NPC_RANGER].hp;
    state.dead_mob_count = 1;

    ASSERT_INT_EQ("resurrection succeeds", force_mager_resurrect(&state, 0), 1);
    ASSERT_INT_EQ("dead store consumed", state.dead_mob_count, 0);

    int resurrected_slot = -1;
    for (int i = 1; i < INF_MAX_NPCS; i++) {
        if (state.npcs[i].active && state.npcs[i].type == INF_NPC_RANGER) {
            resurrected_slot = i;
            break;
        }
    }
    ASSERT_INT_EQ("resurrected ranger spawned", resurrected_slot >= 0, 1);
    ASSERT_INT_EQ("respawned ranger marked resurrected",
        state.npcs[resurrected_slot].resurrection_count, 1);

    inf_store_dead_mob(&state, &state.npcs[resurrected_slot]);
    ASSERT_INT_EQ("resurrected ranger not re-added", state.dead_mob_count, 0);
}

static void test_double_mager_wave_resurrection_limit(void) {
    printf("--- double mager wave respects once-only resurrection ---\n");

    InfernoState state = make_test_state(25, 16);
    state.wave = 65;

    state.npcs[0] = make_test_npc(INF_NPC_MAGER, 18, 18, INF_NPC_STATS[INF_NPC_MAGER].size);
    state.npcs[0].active = 1;
    state.npcs[0].hp = state.npcs[0].max_hp = INF_NPC_STATS[INF_NPC_MAGER].hp;

    state.dead_mobs[0].type = INF_NPC_MAGER;
    state.dead_mobs[0].x = 22;
    state.dead_mobs[0].y = 22;
    state.dead_mobs[0].hp = INF_NPC_STATS[INF_NPC_MAGER].hp / 2;
    state.dead_mobs[0].max_hp = INF_NPC_STATS[INF_NPC_MAGER].hp;
    state.dead_mob_count = 1;

    ASSERT_INT_EQ("first mage resurrection succeeds", force_mager_resurrect(&state, 0), 1);

    int resurrected_slot = -1;
    for (int i = 1; i < INF_MAX_NPCS; i++) {
        if (state.npcs[i].active && state.npcs[i].type == INF_NPC_MAGER) {
            resurrected_slot = i;
            break;
        }
    }
    ASSERT_INT_EQ("second mager spawned", resurrected_slot >= 0, 1);
    ASSERT_INT_EQ("respawned mager marked resurrected",
        state.npcs[resurrected_slot].resurrection_count, 1);

    inf_store_dead_mob(&state, &state.npcs[0]);
    ASSERT_INT_EQ("original mage can still enter dead store once", state.dead_mob_count, 1);

    ASSERT_INT_EQ("resurrected mage can resurrect the original once",
        force_mager_resurrect(&state, resurrected_slot), 1);

    int original_respawn_slot = -1;
    for (int i = 1; i < INF_MAX_NPCS; i++) {
        if (i == resurrected_slot) continue;
        if (state.npcs[i].active && state.npcs[i].type == INF_NPC_MAGER &&
            state.npcs[i].resurrection_count == 1) {
            original_respawn_slot = i;
            break;
        }
    }
    ASSERT_INT_EQ("original mage respawned once", original_respawn_slot >= 0, 1);

    inf_store_dead_mob(&state, &state.npcs[resurrected_slot]);
    ASSERT_INT_EQ("already-resurrected mage stays out of store", state.dead_mob_count, 0);
    inf_store_dead_mob(&state, &state.npcs[original_respawn_slot]);
    ASSERT_INT_EQ("re-resurrected original mage stays out of store", state.dead_mob_count, 0);
}

static void test_pending_hit_obs_timer_prefers_prayer_window(void) {
    printf("--- pending hit obs timer prefers prayer window ---\n");

    EncounterPendingHit jad_hit = {0};
    jad_hit.check_prayer = 1;
    jad_hit.prayer_check_delay = 3;
    jad_hit.ticks_remaining = 4;

    EncounterPendingHit normal_hit = {0};
    normal_hit.check_prayer = 0;
    normal_hit.prayer_check_delay = 0;
    normal_hit.ticks_remaining = 2;

    ASSERT_INT_EQ("jad timer uses prayer window", inf_pending_hit_obs_timer(&jad_hit), 3);
    ASSERT_INT_EQ("normal timer uses travel time", inf_pending_hit_obs_timer(&normal_hit), 2);
}

static void test_jad_preview_and_obs_timing(void) {
    printf("--- jad preview and obs timing ---\n");

    InfernoState state = make_test_state(10, 10);
    state.player.current_defence = 99;
    state.player.current_magic = 99;
    state.player.prayer = PRAYER_NONE;
    state.weapon_set = INF_GEAR_MAGE;
    state.wave = 66;

    state.npcs[0] = make_test_npc(
        INF_NPC_JAD, 20, 10, INF_NPC_STATS[INF_NPC_JAD].size);
    state.npcs[0].active = 1;
    state.npcs[0].attack_timer = 2;

    inf_npc_attack(&state, 0);

    ASSERT_INT_EQ("jad preview decrements to one", state.npcs[0].attack_timer, 1);
    ASSERT_INT_EQ(
        "jad preview style committed",
        state.npcs[0].jad_attack_style == ATTACK_STYLE_RANGED ||
            state.npcs[0].jad_attack_style == ATTACK_STYLE_MAGIC,
        1);

    float obs[INF_NUM_OBS];
    inf_write_obs((EncounterState*)&state, obs);
    ASSERT_FLOAT_NEAR("prayer-critical timer exposes next-tick jad telegraph", obs[37], 0.1f, 1e-6f);
    ASSERT_INT_EQ(
        "prayer-critical style is one-hot for jad preview",
        (int)(obs[38] + obs[39] + obs[40]),
        1);

    state.npcs[0].attack_timer = 1;
    state.npcs[0].jad_attack_style = ATTACK_STYLE_MAGIC;
    state.player_pending_hit_count = 0;
    state.npcs[0].attacked_this_tick = 0;

    inf_npc_attack(&state, 0);

    ASSERT_INT_EQ("jad attack queued one pending hit", state.player_pending_hit_count, 1);
    ASSERT_INT_EQ("jad preview resets after firing", state.npcs[0].jad_attack_style, ATTACK_STYLE_NONE);
    ASSERT_INT_EQ("jad pending hit keeps prayer delay", state.player_pending_hits[0].prayer_check_delay, 3);
    ASSERT_INT_EQ("jad pending hit keeps land delay", state.player_pending_hits[0].ticks_remaining, 4);

    memset(obs, 0, sizeof(obs));
    inf_write_obs((EncounterState*)&state, obs);
    int pending_start = INF_NUM_OBS - INF_FEATURES_PER_HIT * ENCOUNTER_MAX_PENDING_HITS;
    ASSERT_FLOAT_NEAR("pending hit obs timer uses prayer window not impact delay", obs[pending_start + 3], 0.3f, 1e-6f);
}

static void test_jad_melee_stays_instant_and_untelegraphed(void) {
    printf("--- jad melee stays instant and untelegraphed ---\n");

    InfernoState preview_state = make_test_state(5, 5);
    preview_state.player.current_defence = 99;
    preview_state.player.current_magic = 99;
    preview_state.player.prayer = PRAYER_NONE;
    preview_state.weapon_set = INF_GEAR_MAGE;
    preview_state.wave = 66;

    preview_state.npcs[0] = make_test_npc(
        INF_NPC_JAD, 6, 5, INF_NPC_STATS[INF_NPC_JAD].size);
    preview_state.npcs[0].active = 1;
    preview_state.npcs[0].attack_timer = 1;
    preview_state.npcs[0].jad_attack_style = ATTACK_STYLE_RANGED;

    float obs[INF_NUM_OBS];
    inf_write_obs((EncounterState*)&preview_state, obs);

    ASSERT_FLOAT_NEAR(
        "jad prayer-critical preview does not advertise melee fallback",
        obs[41], 1.0f / 3.0f, 1e-6f);
    ASSERT_INT_EQ(
        "jad prayer-critical preview keeps ranged one-hot",
        (int)(obs[38] + obs[39] + obs[40]),
        1);
    ASSERT_FLOAT_NEAR(
        "jad preview keeps ranged as the visible style",
        obs[39], 1.0f, 1e-6f);

    int saw_melee = 0;
    for (uint32_t seed = 0; seed < 256; seed++) {
        InfernoState attack_state = make_test_state(5, 5);
        attack_state.rng_state = seed;
        attack_state.player.current_defence = 99;
        attack_state.player.current_magic = 99;
        attack_state.player.prayer = PRAYER_NONE;
        attack_state.weapon_set = INF_GEAR_MAGE;
        attack_state.wave = 66;

        attack_state.npcs[0] = make_test_npc(
            INF_NPC_JAD, 6, 5, INF_NPC_STATS[INF_NPC_JAD].size);
        attack_state.npcs[0].active = 1;
        attack_state.npcs[0].attack_timer = 0;
        attack_state.npcs[0].jad_attack_style = ATTACK_STYLE_RANGED;

        inf_npc_attack(&attack_state, 0);

        if (attack_state.npcs[0].attack_style_this_tick == ATTACK_STYLE_MELEE) {
            saw_melee = 1;
            ASSERT_INT_EQ(
                "jad melee fallback does not queue a pending hit",
                attack_state.player_pending_hit_count, 0);
            break;
        }
    }

    ASSERT_INT_EQ("jad can still choose melee instantly at fire time", saw_melee, 1);
}

static int inferno_obs_slot_type(int slot_idx) {
    if (slot_idx >= 0 && slot_idx < 2) return INF_NPC_MAGER;
    if (slot_idx >= 2 && slot_idx < 4) return INF_NPC_RANGER;
    if (slot_idx >= 4 && slot_idx < 6) return INF_NPC_MELEER;
    if (slot_idx >= 6 && slot_idx < 8) return INF_NPC_BLOB;
    if (slot_idx >= 8 && slot_idx < 10) return INF_NPC_BAT;
    if (slot_idx >= 10 && slot_idx < 12) return INF_NPC_BLOB_MAGE;
    if (slot_idx >= 12 && slot_idx < 14) return INF_NPC_BLOB_RANGE;
    if (slot_idx >= 14 && slot_idx < 16) return INF_NPC_BLOB_MELEE;
    if (slot_idx >= 16 && slot_idx < 22) return INF_NPC_NIBBLER;
    if (slot_idx >= 22 && slot_idx < 25) return INF_NPC_JAD;
    if (slot_idx == 25) return INF_NPC_ZUK;
    if (slot_idx == 26) return INF_NPC_ZUK_SHIELD;
    if (slot_idx >= 27 && slot_idx < 33) return INF_NPC_HEALER_JAD;
    if (slot_idx >= 33 && slot_idx < 37) return INF_NPC_HEALER_ZUK;
    return -1;
}

static int inferno_obs_slot_feature_count(int slot_idx) {
    int type = inferno_obs_slot_type(slot_idx);
    int has_style = (type == INF_NPC_BLOB || type == INF_NPC_JAD);
    int has_scan = (type == INF_NPC_BLOB);
    int has_los = (type != INF_NPC_NIBBLER && type != INF_NPC_MELEER &&
        type != INF_NPC_HEALER_JAD && type != INF_NPC_ZUK_SHIELD);
    int has_aggro = (type != INF_NPC_NIBBLER && type != INF_NPC_ZUK_SHIELD);
    int has_timer = (type != INF_NPC_NIBBLER && type != INF_NPC_HEALER_JAD &&
        type != INF_NPC_ZUK_SHIELD);
    int has_targeted = 1;

    return 4 + has_timer + 3 * has_style + has_los + 3 * has_scan +
        has_aggro + has_targeted;
}

static int inferno_obs_slot_start(int slot_idx) {
    int start = INF_PLAYER_OBS_SIZE + 12;
    for (int i = 0; i < slot_idx; i++) {
        start += inferno_obs_slot_feature_count(i);
    }
    return start;
}

static int inferno_target_mask_slot_offset(int slot_idx) {
    return ENCOUNTER_MOVE_ACTIONS + ENCOUNTER_OVERHEAD_DIM_PVE + 1 + slot_idx;
}

static void test_zuk_obs_tracks_shield_and_mager_aggro(void) {
    printf("--- zuk obs tracks shield hp/death and mager aggro ---\n");

    InfernoState state = make_test_state(INF_ZUK_PLAYER_START_X, INF_ZUK_PLAYER_START_Y);
    state.wave = 68;
    state.player.current_defence = 99;
    state.player.current_magic = 99;
    state.player.current_ranged = 99;
    state.player.current_hitpoints = 99;
    state.player.base_hitpoints = 99;
    state.player.base_prayer = 99;
    state.player.current_prayer = 99;
    state.player.prayer = PRAYER_NONE;
    state.weapon_set = INF_GEAR_TBOW;

    state.npcs[0] = make_test_npc(
        INF_NPC_MAGER, 20, 36, INF_NPC_STATS[INF_NPC_MAGER].size);
    state.npcs[0].active = 1;
    state.npcs[0].hp = state.npcs[0].max_hp = INF_NPC_STATS[INF_NPC_MAGER].hp;
    state.npcs[0].attack_timer = 4;

    state.npcs[1] = make_test_npc(
        INF_NPC_ZUK, 22, 14, INF_NPC_STATS[INF_NPC_ZUK].size);
    state.npcs[1].active = 1;
    state.npcs[1].hp = state.npcs[1].max_hp = INF_NPC_STATS[INF_NPC_ZUK].hp;

    state.npcs[2] = make_test_npc(
        INF_NPC_ZUK_SHIELD, 23, 44, INF_NPC_STATS[INF_NPC_ZUK_SHIELD].size);
    state.npcs[2].active = 1;
    state.npcs[2].hp = 300;
    state.npcs[2].max_hp = INF_NPC_STATS[INF_NPC_ZUK_SHIELD].hp;

    state.zuk.shield_idx = 2;
    state.zuk.shield_dir = -1;
    state.zuk.shield_freeze = 3;
    state.npcs[0].aggro_target = 2;

    float obs[INF_NUM_OBS];
    float mask[INF_ACTION_MASK_SIZE];
    inf_write_obs((EncounterState*)&state, obs);
    inf_write_mask((EncounterState*)&state, mask);

    int mager_slot = 0;
    int shield_slot = 26;
    int mager_start = inferno_obs_slot_start(mager_slot);
    int shield_start = inferno_obs_slot_start(shield_slot);

    ASSERT_INT_EQ("first mager occupies mager slot 0", state.current_obs_slots[mager_slot], 0);
    ASSERT_INT_EQ("shield occupies dedicated shield slot", state.current_obs_slots[shield_slot], 2);
    ASSERT_FLOAT_NEAR("shield hp ratio visible in shield slot", obs[shield_start], 0.5f, 1e-6f);
    ASSERT_FLOAT_NEAR("mager aggro bit is 0 while on shield", obs[mager_start + 6], 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("shield direction visible while alive", obs[43], 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("shield freeze visible while alive", obs[44], 0.6f, 1e-6f);
    ASSERT_FLOAT_NEAR("mager target mask is valid", mask[inferno_target_mask_slot_offset(mager_slot)], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("shield target mask stays invalid", mask[inferno_target_mask_slot_offset(shield_slot)], 0.0f, 1e-6f);

    state.npcs[2].active = 0;
    state.zuk.shield_idx = -1;
    state.npcs[0].aggro_target = -1;

    memset(obs, 0, sizeof(obs));
    memset(mask, 0, sizeof(mask));
    inf_write_obs((EncounterState*)&state, obs);
    inf_write_mask((EncounterState*)&state, mask);

    ASSERT_INT_EQ("dead shield drops out of shield slot", state.current_obs_slots[shield_slot], -1);
    ASSERT_FLOAT_NEAR("dead shield slot hp zeros out", obs[shield_start], 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("dead shield zeroes stale direction", obs[43], 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("dead shield zeroes stale freeze", obs[44], 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("mager aggro bit flips to player", obs[mager_start + 6], 1.0f, 1e-6f);
}

static void test_human_target_and_potion_translation(void) {
    printf("--- inferno human target and potion translation ---\n");

    InfernoState state = make_test_state(20, 20);
    state.player.base_hitpoints = 99;
    state.player.current_hitpoints = 80;
    state.player.base_prayer = 99;
    state.player.current_prayer = 60;
    state.player.current_attack = 99;
    state.player.current_strength = 99;
    state.player.current_defence = 99;
    state.player.current_ranged = 99;
    state.player.current_magic = 99;

    state.npcs[0] = make_test_npc(
        INF_NPC_MAGER, 24, 24, INF_NPC_STATS[INF_NPC_MAGER].size);
    state.npcs[0].active = 1;
    state.npcs[0].hp = state.npcs[0].max_hp = INF_NPC_STATS[INF_NPC_MAGER].hp;

    state.npcs[1] = make_test_npc(
        INF_NPC_MAGER, 26, 24, INF_NPC_STATS[INF_NPC_MAGER].size);
    state.npcs[1].active = 1;
    state.npcs[1].hp = state.npcs[1].max_hp = INF_NPC_STATS[INF_NPC_MAGER].hp;

    state.npcs[2] = make_test_npc(
        INF_NPC_MAGER, 28, 24, INF_NPC_STATS[INF_NPC_MAGER].size);
    state.npcs[2].active = 1;
    state.npcs[2].hp = state.npcs[2].max_hp = INF_NPC_STATS[INF_NPC_MAGER].hp;

    state.npcs[3] = make_test_npc(
        INF_NPC_ZUK_SHIELD, 23, 44, INF_NPC_STATS[INF_NPC_ZUK_SHIELD].size);
    state.npcs[3].active = 1;
    state.npcs[3].hp = state.npcs[3].max_hp = INF_NPC_STATS[INF_NPC_ZUK_SHIELD].hp;

    {
        float obs[INF_NUM_OBS];
        inf_write_obs((EncounterState*)&state, obs);
    }

    ASSERT_INT_EQ("first visible mager is targetable",
        inf_is_human_targetable_npc_slot((EncounterState*)&state, 0), 1);
    ASSERT_INT_EQ("second visible mager is targetable",
        inf_is_human_targetable_npc_slot((EncounterState*)&state, 1), 1);
    ASSERT_INT_EQ("third capped-out mager is not targetable",
        inf_is_human_targetable_npc_slot((EncounterState*)&state, 2), 0);
    ASSERT_INT_EQ("shield is never targetable",
        inf_is_human_targetable_npc_slot((EncounterState*)&state, 3), 0);

    {
        HumanInput hi;
        int actions[INF_NUM_ACTION_HEADS];

        hi = make_human_input();
        hi.pending_target_idx = 0;
        inf_translate_human_input(&hi, actions, (EncounterState*)&state);
        ASSERT_INT_EQ("visible mager click maps into target head",
            actions[INF_HEAD_TARGET], 1);

        hi = make_human_input();
        hi.pending_target_idx = 2;
        inf_translate_human_input(&hi, actions, (EncounterState*)&state);
        ASSERT_INT_EQ("capped-out mager click is rejected",
            actions[INF_HEAD_TARGET], 0);

        hi = make_human_input();
        hi.pending_target_idx = 3;
        inf_translate_human_input(&hi, actions, (EncounterState*)&state);
        ASSERT_INT_EQ("shield click is rejected",
            actions[INF_HEAD_TARGET], 0);

        hi = make_human_input();
        hi.pending_potion = POTION_BREW;
        inf_translate_human_input(&hi, actions, (EncounterState*)&state);
        ASSERT_INT_EQ("brew still maps to eat head", actions[INF_HEAD_EAT], 1);
        ASSERT_INT_EQ("brew does not touch potion head", actions[INF_HEAD_POTION], 0);

        hi = make_human_input();
        hi.pending_potion = POTION_RESTORE;
        inf_translate_human_input(&hi, actions, (EncounterState*)&state);
        ASSERT_INT_EQ("restore maps to potion 1", actions[INF_HEAD_POTION], 1);

        hi = make_human_input();
        hi.pending_potion = POTION_BASTION;
        inf_translate_human_input(&hi, actions, (EncounterState*)&state);
        ASSERT_INT_EQ("bastion maps to potion 2", actions[INF_HEAD_POTION], 2);

        hi = make_human_input();
        hi.pending_potion = POTION_STAMINA;
        inf_translate_human_input(&hi, actions, (EncounterState*)&state);
        ASSERT_INT_EQ("stamina maps to potion 3", actions[INF_HEAD_POTION], 3);

        hi = make_human_input();
        hi.pending_potion = POTION_PRAYER_POT;
        inf_translate_human_input(&hi, actions, (EncounterState*)&state);
        ASSERT_INT_EQ("prayer pot no longer aliases to restore",
            actions[INF_HEAD_POTION], 0);
    }
}

int main(void) {
    inf_build_npc_stats();

    test_melee_fallback_geometry();
    test_style_mask_preview();
    test_style_choice_sampling();
    test_tagged_jad_healer_melee_geometry();
    test_overlap_shuffle_hold_after_recent_target_click();
    test_overlap_shuffle_respects_npc_occupancy();
    test_meleer_dig_landing_order();
    test_dead_mob_store_eligibility();
    test_resurrected_mob_does_not_reenter_dead_store();
    test_double_mager_wave_resurrection_limit();
    test_pending_hit_obs_timer_prefers_prayer_window();
    test_jad_preview_and_obs_timing();
    test_jad_melee_stays_instant_and_untelegraphed();
    test_zuk_obs_tracks_shield_and_mager_aggro();
    test_human_target_and_potion_translation();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(" (%d failed)\n", tests_failed);
        return 1;
    }
    printf("\n");
    return 0;
}
