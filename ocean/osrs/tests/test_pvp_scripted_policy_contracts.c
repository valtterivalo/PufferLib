/**
 * @file test_pvp_scripted_policy_contracts.c
 * @brief regression tests for range-aware PvP scripted policy attack choices.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/test_pvp_scripted_policy_contracts \
 *       ocean/osrs/tests/test_pvp_scripted_policy_contracts.c -lm
 *   /tmp/test_pvp_scripted_policy_contracts
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ocean/osrs/osrs_env.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_INT_EQ(label, actual, expected) do { \
    tests_run++; \
    if ((actual) == (expected)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s got %d expected %d\n", (label), (actual), (expected)); \
    } \
} while (0)

#define ASSERT_TRUE(label, condition) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s\n", (label)); \
    } \
} while (0)

static CollisionMap* test_wilderness_collision_map(void) {
    static CollisionMap* cmap = NULL;
    if (cmap == NULL) {
        osrs_asset_require_group(OSRS_ASSET_GROUP_PVP);
        cmap = collision_map_load(OSRS_ASSET("wilderness.cmap"));
        if (cmap == NULL) {
            fprintf(stderr, "test setup: failed to load wilderness.cmap\n");
            abort();
        }
    }
    return cmap;
}

static void setup_pvp_env(OsrsEnv* env, OpponentType type) {
    memset(env, 0, sizeof(*env));
    pvp_init(env);
    env->collision_map = test_wilderness_collision_map();
    env->ocean_io.agent_obs = env->_obs_buf;
    env->ocean_io.agent_actions = env->_acts_buf;
    env->ocean_io.agent_rewards = env->_rews_buf;
    env->ocean_io.agent_terminals = env->_terms_buf;
    env->pvp_runtime.use_c_opponent = 1;
    env->pvp_runtime.opponent.type = type;
    pvp_seed(env, 1337);
    pvp_reset(env);
}

static void set_player_position(Player* player, int x, int y) {
    player->x = x;
    player->y = y;
    player->dest_x = x;
    player->dest_y = y;
    player->is_moving = 0;
}

static void set_weapon_inventory(Player* player, const uint8_t* weapons, int count) {
    osrs_player_inventory_clear(player);
    osrs_player_set_equipment_slot(player, GEAR_SLOT_WEAPON, weapons[0]);
    for (int i = 1; i < count; i++) {
        if (osrs_player_inventory_add(player, weapons[i]) < 0) abort();
    }
}

static void set_basic_hybrid_weapons(Player* player) {
    const uint8_t weapons[] = {
        ITEM_WHIP,
        ITEM_RUNE_CROSSBOW,
        ITEM_AHRIM_STAFF,
        ITEM_DRAGON_DAGGER,
    };
    set_weapon_inventory(player, weapons, 4);
}

static void set_style_bias_flat(OpponentState* opp) {
    opp->style_bias[OPP_STYLE_MAGE] = 1.0f;
    opp->style_bias[OPP_STYLE_RANGED] = 1.0f;
    opp->style_bias[OPP_STYLE_MELEE] = 1.0f;
}

static void set_style_bias_ranged_only(OpponentState* opp) {
    opp->style_bias[OPP_STYLE_MAGE] = 0.0f;
    opp->style_bias[OPP_STYLE_RANGED] = 1.0f;
    opp->style_bias[OPP_STYLE_MELEE] = 0.0f;
}

static void set_recent_target_attack_count(
    Player* self,
    AttackStyle style,
    int count
) {
    memset(self->recent_target_attack_styles, 0, sizeof(self->recent_target_attack_styles));
    for (int i = 0; i < count && i < HISTORY_SIZE; i++) {
        self->recent_target_attack_styles[i] = style;
    }
}

static void force_clean_policy_decision(OsrsEnv* env, OpponentType type) {
    env->pvp_runtime.opponent.type = type;
    env->pvp_runtime.opponent.active_sub_policy = OPP_NONE;
    opponent_reset(env, &env->pvp_runtime.opponent);

    OpponentState* opp = &env->pvp_runtime.opponent;
    opp->prayer_accuracy = 1.0f;
    opp->off_prayer_rate = 1.0f;
    opp->offensive_prayer_rate = 0.0f;
    opp->action_delay_chance = 0.0f;
    opp->mistake_rate = 0.0f;
    opp->offensive_prayer_miss = 0.0f;
    opp->read_chance = 0.0f;
    opp->combo_state = COMBO_IDLE;
    opp->ko_threshold = 0.0f;
    set_style_bias_flat(opp);
}

static void set_dead_zone_state(OsrsEnv* env, OpponentType type, int distance) {
    force_clean_policy_decision(env, type);

    Player* target = &env->players[0];
    Player* self = &env->players[1];
    set_basic_hybrid_weapons(self);
    set_player_position(self, 3042, 3520);
    set_player_position(target, 3042 + distance, 3520);

    target->prayer = PRAYER_PROTECT_MAGIC;
    target->frozen_ticks = 0;
    target->freeze_immunity_ticks = 0;
    self->prayer = PRAYER_NONE;
    self->is_lunar_spellbook = 0;
    self->attack_timer = 0;
    self->special_energy = 0;
    self->food_count = 0;
    self->karambwan_count = 0;
    self->brew_doses = 0;
    self->restore_doses = 0;
    self->current_hitpoints = self->base_hitpoints;
    self->current_magic = self->base_magic;
    self->current_ranged = self->base_ranged;
    self->current_attack = self->base_attack;
    self->current_strength = self->base_strength;
    self->current_defence = self->base_defence;
    memset(env->pending_actions, 0, sizeof(env->pending_actions));
    memset(env->actions, 0, NUM_AGENTS * NUM_ACTION_HEADS * sizeof(int));
}

static void set_adjacent_non_melee_spacing_state(OsrsEnv* env, OpponentType type) {
    force_clean_policy_decision(env, type);

    Player* target = &env->players[0];
    Player* self = &env->players[1];
    set_basic_hybrid_weapons(self);
    set_player_position(self, 3042, 3531);
    set_player_position(target, 3043, 3531);

    self->last_obs_target_x = target->x;
    self->last_obs_target_y = target->y;
    target->last_obs_target_x = self->x;
    target->last_obs_target_y = self->y;
    target->prayer = PRAYER_PROTECT_MELEE;
    target->frozen_ticks = 0;
    target->freeze_immunity_ticks = 0;
    self->prayer = PRAYER_NONE;
    self->is_lunar_spellbook = 0;
    self->attack_timer = 0;
    self->special_energy = 0;
    self->frozen_ticks = 0;
    self->food_count = 0;
    self->karambwan_count = 0;
    self->brew_doses = 0;
    self->restore_doses = 0;
    self->current_hitpoints = self->base_hitpoints;
    self->current_magic = self->base_magic;
    self->current_ranged = self->base_ranged;
    self->current_attack = self->base_attack;
    self->current_strength = self->base_strength;
    self->current_defence = self->base_defence;
    memset(env->pending_actions, 0, sizeof(env->pending_actions));
    memset(env->actions, 0, NUM_AGENTS * NUM_ACTION_HEADS * sizeof(int));
}

static void set_under_non_melee_spacing_state(OsrsEnv* env, OpponentType type) {
    set_adjacent_non_melee_spacing_state(env, type);

    Player* target = &env->players[0];
    Player* self = &env->players[1];
    set_player_position(self, target->x, target->y);
    self->last_obs_target_x = target->x;
    self->last_obs_target_y = target->y;
    target->last_obs_target_x = self->x;
    target->last_obs_target_y = self->y;
    set_style_bias_ranged_only(&env->pvp_runtime.opponent);
}

static int opponent_combat(const OsrsEnv* env) {
    return env->pending_actions[NUM_ACTION_HEADS + HEAD_ATTACK];
}

static int opponent_overhead(const OsrsEnv* env) {
    return env->pending_actions[NUM_ACTION_HEADS + HEAD_OVERHEAD];
}

static int opponent_move(const OsrsEnv* env) {
    return env->pending_actions[NUM_ACTION_HEADS + HEAD_MOVE];
}

static int opponent_move_dest_x(const OsrsEnv* env) {
    return env->players[1].x + ENCOUNTER_MOVE_TARGET_DX[opponent_move(env)];
}

static int opponent_move_dest_y(const OsrsEnv* env) {
    return env->players[1].y + ENCOUNTER_MOVE_TARGET_DY[opponent_move(env)];
}

static int opponent_special(const OsrsEnv* env) {
    return env->pending_actions[NUM_ACTION_HEADS + HEAD_SPECIAL];
}

static int opponent_potion(const OsrsEnv* env) {
    return env->pending_actions[NUM_ACTION_HEADS + HEAD_POTION];
}

static int opponent_casts_spell(const OsrsEnv* env) {
    int combat = opponent_combat(env);
    return combat == ATTACK_ICE || combat == ATTACK_BLOOD;
}

static void set_adaptive_mage_staff_camp_state(OsrsEnv* env, int recent_melee_count) {
    force_clean_policy_decision(env, OPP_ADAPTIVE_NH);

    Player* target = &env->players[0];
    Player* self = &env->players[1];
    const uint8_t target_weapons[] = {ITEM_AHRIM_STAFF};
    set_basic_hybrid_weapons(self);
    set_weapon_inventory(target, target_weapons, 1);
    set_player_position(self, 3042, 3520);
    set_player_position(target, 3043, 3520);

    target->visible_gear = GEAR_MAGE;
    target->current_gear = GEAR_MAGE;
    target->prayer = PRAYER_PROTECT_MAGIC;
    target->last_attack_style = ATTACK_STYLE_NONE;
    target->attack_style_this_tick = ATTACK_STYLE_NONE;
    target->just_attacked = 0;
    target->attack_timer = 0;
    self->prayer = PRAYER_NONE;
    self->attack_timer = 0;
    self->special_energy = 0;
    self->frozen_ticks = 0;
    self->food_count = 0;
    self->karambwan_count = 0;
    self->brew_doses = 0;
    self->restore_doses = 0;
    set_recent_target_attack_count(self, ATTACK_STYLE_MELEE, recent_melee_count);
    memset(env->pending_actions, 0, sizeof(env->pending_actions));
    memset(env->actions, 0, NUM_AGENTS * NUM_ACTION_HEADS * sizeof(int));
}

static void test_adaptive_nh_name_maps_correctly(void) {
    printf("--- Adaptive NH name maps correctly ---\n");

    ASSERT_TRUE(
        "adaptive NH name",
        strcmp(osrs_pvp_opponent_type_name(OPP_ADAPTIVE_NH), "Adaptive NH") == 0);
    ASSERT_TRUE(
        "strict kiter name",
        strcmp(osrs_pvp_opponent_type_name(OPP_STRICT_KITER), "Strict Kiter") == 0);
}

static void assert_hard_policy_moves_instead_of_adjacent_non_melee(
    OpponentType type,
    const char* label
) {
    OsrsEnv env;
    setup_pvp_env(&env, type);
    set_adjacent_non_melee_spacing_state(&env, type);

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    char combat_label[128];
    char move_label[128];
    snprintf(combat_label, sizeof(combat_label), "%s combat", label);
    snprintf(move_label, sizeof(move_label), "%s move", label);
    ASSERT_INT_EQ(combat_label, opponent_combat(&env), ATTACK_NONE);
    ASSERT_TRUE(move_label, opponent_move(&env) != MOVE_NONE);
}

static void test_hard_policies_do_not_farcast_while_adjacent(void) {
    printf("--- Hard policies move before adjacent non-melee attacks ---\n");

    assert_hard_policy_moves_instead_of_adjacent_non_melee(
        OPP_MASTER_NH, "master NH adjacent non-melee");
    assert_hard_policy_moves_instead_of_adjacent_non_melee(
        OPP_NIGHTMARE_NH, "nightmare NH adjacent non-melee");
    assert_hard_policy_moves_instead_of_adjacent_non_melee(
        OPP_RANGE_KITER, "range kiter adjacent non-melee");
    assert_hard_policy_moves_instead_of_adjacent_non_melee(
        OPP_ADAPTIVE_NH, "adaptive NH adjacent non-melee");
    assert_hard_policy_moves_instead_of_adjacent_non_melee(
        OPP_STRICT_KITER, "strict kiter adjacent non-melee");
}

static void test_smart_hard_policy_avoids_cardinal_step_out(void) {
    printf("--- Smart hard policy avoids cardinal frozen step-out ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_NIGHTMARE_NH);
    set_adjacent_non_melee_spacing_state(&env, OPP_NIGHTMARE_NH);
    env.players[0].frozen_ticks = 10;

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_INT_EQ("frozen adjacent combat", opponent_combat(&env), ATTACK_NONE);
    ASSERT_TRUE("frozen adjacent move", opponent_move(&env) != MOVE_NONE);
    ASSERT_TRUE("frozen adjacent not under",
        opponent_move_dest_x(&env) != env.players[0].x ||
        opponent_move_dest_y(&env) != env.players[0].y);
    ASSERT_TRUE("frozen adjacent avoids cardinal melee tile",
        abs_int(opponent_move_dest_x(&env) - env.players[0].x) +
        abs_int(opponent_move_dest_y(&env) - env.players[0].y) != 1);
}

static void test_smart_hard_policy_converts_under_ranged_attack_to_move_only(void) {
    printf("--- Smart hard policy moves before ranged target-click from under ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_NIGHTMARE_NH);
    set_under_non_melee_spacing_state(&env, OPP_NIGHTMARE_NH);
    env.players[0].frozen_ticks = 10;

    int* actions = &env.pending_actions[NUM_ACTION_HEADS];
    memset(actions, 0, NUM_ACTION_HEADS * sizeof(int));
    actions[HEAD_LOADOUT] = LOADOUT_RANGE;
    actions[HEAD_COMBAT] = ATTACK_ATK;

    opp_apply_hard_spacing_guard(&env, OPP_NIGHTMARE_NH, actions);

    ASSERT_INT_EQ("under ranged combat cleared", opponent_combat(&env), ATTACK_NONE);
    ASSERT_TRUE("under ranged move queued", opponent_move(&env) != MOVE_NONE);
    ASSERT_TRUE("under ranged destination leaves target tile",
        opponent_move_dest_x(&env) != env.players[0].x ||
        opponent_move_dest_y(&env) != env.players[0].y);
    ASSERT_TRUE("under ranged destination avoids cardinal melee tile",
        abs_int(opponent_move_dest_x(&env) - env.players[0].x) +
        abs_int(opponent_move_dest_y(&env) - env.players[0].y) != 1);
}

static void test_smart_hard_policy_allows_diagonal_projectile_attack(void) {
    printf("--- Smart hard policy allows diagonal projectile attack ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_NIGHTMARE_NH);
    set_adjacent_non_melee_spacing_state(&env, OPP_NIGHTMARE_NH);
    set_player_position(&env.players[1], env.players[0].x - 1, env.players[0].y - 1);
    env.players[0].frozen_ticks = 10;

    int* actions = &env.pending_actions[NUM_ACTION_HEADS];
    memset(actions, 0, NUM_ACTION_HEADS * sizeof(int));
    actions[HEAD_LOADOUT] = LOADOUT_RANGE;
    actions[HEAD_COMBAT] = ATTACK_ATK;

    opp_apply_hard_spacing_guard(&env, OPP_NIGHTMARE_NH, actions);

    ASSERT_INT_EQ("diagonal ranged combat preserved", opponent_combat(&env), ATTACK_ATK);
    ASSERT_INT_EQ("diagonal ranged move idle", opponent_move(&env), MOVE_NONE);
}

static void test_smart_hard_policy_full_step_under_ranged_attack_does_not_attack(void) {
    printf("--- Smart hard policy full step waits after ranged move-out ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_NIGHTMARE_NH);
    set_under_non_melee_spacing_state(&env, OPP_NIGHTMARE_NH);
    env.players[0].frozen_ticks = 10;
    memset(env.ocean_io.agent_actions, 0, NUM_ACTION_HEADS * sizeof(int));

    pvp_step(&env);

    int* executed = env.last_executed_actions + NUM_ACTION_HEADS;
    int dx = abs_int(env.players[1].x - env.players[0].x);
    int dy = abs_int(env.players[1].y - env.players[0].y);
    ASSERT_INT_EQ("full step ranged attack cleared",
        executed[HEAD_ATTACK], ATTACK_NONE);
    ASSERT_TRUE("full step move queued", executed[HEAD_MOVE] != MOVE_NONE);
    ASSERT_INT_EQ("full step opponent did not attack",
        env.players[1].just_attacked, 0);
    ASSERT_TRUE("full step leaves same tile", dx != 0 || dy != 0);
    ASSERT_TRUE("full step avoids cardinal melee tile", dx + dy != 1);
}

static void test_smart_hard_policy_treats_last_freeze_tick_as_unfrozen(void) {
    printf("--- Smart hard policy treats last freeze tick as unfrozen ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_NIGHTMARE_NH);
    set_adjacent_non_melee_spacing_state(&env, OPP_NIGHTMARE_NH);
    env.players[0].frozen_ticks = 1;

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_INT_EQ("last freeze tick combat", opponent_combat(&env), ATTACK_NONE);
    ASSERT_TRUE("last freeze tick move", opponent_move(&env) != MOVE_NONE);
    ASSERT_TRUE("last freeze tick farcasts",
        chebyshev_distance(
            opponent_move_dest_x(&env),
            opponent_move_dest_y(&env),
            env.players[0].x,
            env.players[0].y) > 1);
}

static void test_dumb_hard_policy_still_walks_under_frozen_adjacent_target(void) {
    printf("--- Dumb hard policy still walks under frozen adjacent target ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_ADVANCED_NH);
    set_adjacent_non_melee_spacing_state(&env, OPP_ADVANCED_NH);
    env.players[0].frozen_ticks = 10;

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_INT_EQ("dumb frozen adjacent combat", opponent_combat(&env), ATTACK_NONE);
    ASSERT_TRUE("dumb frozen adjacent move", opponent_move(&env) != MOVE_NONE);
    ASSERT_INT_EQ("dumb frozen adjacent x",
        opponent_move_dest_x(&env), env.players[0].x);
    ASSERT_INT_EQ("dumb frozen adjacent y",
        opponent_move_dest_y(&env), env.players[0].y);
}

static void test_hard_spacing_guard_does_not_move_when_self_frozen(void) {
    printf("--- Hard spacing guard does not move when self frozen ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_NIGHTMARE_NH);
    set_adjacent_non_melee_spacing_state(&env, OPP_NIGHTMARE_NH);
    env.players[0].frozen_ticks = 10;
    env.players[1].frozen_ticks = 10;

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_INT_EQ("self frozen move", opponent_move(&env), MOVE_NONE);
}

static void test_adaptive_nh_read_chance_exceeds_nightmare(void) {
    printf("--- Adaptive NH read chance exceeds Nightmare ---\n");

    OsrsEnv nightmare;
    setup_pvp_env(&nightmare, OPP_NIGHTMARE_NH);

    OsrsEnv adaptive;
    setup_pvp_env(&adaptive, OPP_ADAPTIVE_NH);

    ASSERT_TRUE(
        "nightmare read chance",
        nightmare.pvp_runtime.opponent.read_chance == 0.50f);
    ASSERT_TRUE(
        "adaptive read chance",
        adaptive.pvp_runtime.opponent.read_chance == 0.75f);
}

static void test_adaptive_nh_prays_melee_against_learned_mage_camp(void) {
    printf("--- Adaptive NH counters learned mage camp melee ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_ADAPTIVE_NH);
    set_adaptive_mage_staff_camp_state(&env, 2);

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_INT_EQ(
        "adaptive NH prays melee",
        opponent_overhead(&env),
        ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE);
}

static void test_adaptive_nh_prays_magic_without_learned_adjacent_melee(void) {
    printf("--- Adaptive NH keeps mage threat without learned adjacent melee ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_ADAPTIVE_NH);
    set_adaptive_mage_staff_camp_state(&env, 0);
    set_player_position(&env.players[0], 3045, 3520);

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_INT_EQ(
        "adaptive NH prays magic",
        opponent_overhead(&env),
        ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC);
}

static void test_adaptive_nh_learns_static_mage_camp(void) {
    printf("--- Adaptive NH learns static mage camp ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_ADAPTIVE_NH);
    set_adaptive_mage_staff_camp_state(&env, 0);
    env.pvp_runtime.opponent.adaptive_mage_camp_ticks = 3;

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_INT_EQ(
        "adaptive NH prays melee after repeated camp ticks",
        opponent_overhead(&env),
        ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE);
}

static void test_adaptive_nh_attacks_mage_prayer_camp_with_melee(void) {
    printf("--- Adaptive NH attacks mage-prayer camp with melee when frozen ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_ADAPTIVE_NH);
    set_adaptive_mage_staff_camp_state(&env, 1);
    env.players[1].frozen_ticks = 1;
    env.players[0].freeze_immunity_ticks = 5;

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_INT_EQ(
        "adaptive NH attacks camp with current melee weapon",
        opponent_combat(&env),
        ATTACK_ATK);
}

static void test_adaptive_nh_kites_unrooted_mage_prayer_camp(void) {
    printf("--- Adaptive NH kites unrooted mage-prayer camp ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_ADAPTIVE_NH);
    set_adaptive_mage_staff_camp_state(&env, 1);

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_INT_EQ("adaptive NH does not freeze while adjacent",
        opponent_combat(&env), ATTACK_NONE);
    ASSERT_TRUE("adaptive NH moves before freezing while adjacent",
        opponent_move(&env) != 0);
}

static void test_adaptive_nh_kites_freeze_immune_camp_without_spec(void) {
    printf("--- Adaptive NH kites freeze-immune camp without spec ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_ADAPTIVE_NH);
    set_adaptive_mage_staff_camp_state(&env, 1);
    env.players[0].freeze_immunity_ticks = 5;

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_TRUE("adaptive NH kites when freeze cannot land", opponent_move(&env) != 0);
    ASSERT_INT_EQ("adaptive NH does not take normal melee trade",
        opponent_combat(&env), ATTACK_NONE);
}

static void test_adaptive_nh_specs_mage_prayer_camp(void) {
    printf("--- Adaptive NH specs mage-prayer camp ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_ADAPTIVE_NH);
    set_adaptive_mage_staff_camp_state(&env, 1);
    env.players[1].special_energy = 100;

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_INT_EQ(
        "adaptive NH attacks camp with melee spec",
        opponent_combat(&env),
        ATTACK_ATK);
    ASSERT_INT_EQ(
        "adaptive NH arms spec",
        opponent_special(&env),
        SPECIAL_ARM);
}

static void test_adaptive_nh_reads_static_camp_action(void) {
    printf("--- Adaptive NH exact-reads static camp action ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_ADAPTIVE_NH);
    set_adaptive_mage_staff_camp_state(&env, 0);
    env.pvp_runtime.opponent.adaptive_mage_camp_ticks = 3;
    env.actions[HEAD_ATTACK] = ATTACK_ICE;

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_TRUE(
        "adaptive NH read succeeded",
        env.pvp_runtime.opponent.has_read_this_tick);
    ASSERT_INT_EQ(
        "adaptive NH exact read beats camp heuristic",
        opponent_overhead(&env),
        ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC);
}

static void test_adaptive_nh_brews_early_in_static_camp(void) {
    printf("--- Adaptive NH brews early in static camp ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_ADAPTIVE_NH);
    set_adaptive_mage_staff_camp_state(&env, 0);
    env.pvp_runtime.opponent.adaptive_mage_camp_ticks = 3;
    env.players[1].current_hitpoints = env.players[1].base_hitpoints * 4 / 5;
    env.players[1].brew_doses = 1;

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_INT_EQ(
        "adaptive NH brews before low hp in camp",
        opponent_potion(&env),
        POTION_BREW);
}

static void test_adaptive_nh_moves_out_of_adjacent_camp_when_not_ready(void) {
    printf("--- Adaptive NH moves out of adjacent camp when not ready ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_ADAPTIVE_NH);
    set_adaptive_mage_staff_camp_state(&env, 2);
    env.players[1].attack_timer = 3;

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_TRUE("adaptive NH moves while unable to attack", opponent_move(&env) != 0);
    ASSERT_INT_EQ("adaptive NH does not attack while moving", opponent_combat(&env), ATTACK_NONE);
}

static void test_resolver_distance_10_uses_mage_into_prayer(void) {
    printf("--- Resolver uses mage when only mage reaches ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_NIGHTMARE_NH);
    set_dead_zone_state(&env, OPP_NIGHTMARE_NH, 10);
    Player* self = &env.players[1];
    Player* target = &env.players[0];

    OppStyleChoice choice = opp_resolve_attack_style(
        &env, &env.pvp_runtime.opponent, self, target,
        OPP_STYLE_MASK_ALL, opp_get_off_prayer_mask(self, target));

    ASSERT_INT_EQ("distance 10 style", choice.style, OPP_STYLE_MAGE);
    ASSERT_INT_EQ("distance 10 can hit", choice.can_hit_now, 1);
}

static void test_resolver_distance_7_uses_crossbow_off_prayer(void) {
    printf("--- Resolver uses ranged when crossbow reaches off-prayer ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_NIGHTMARE_NH);
    set_dead_zone_state(&env, OPP_NIGHTMARE_NH, 7);
    Player* self = &env.players[1];
    Player* target = &env.players[0];

    OppStyleChoice choice = opp_resolve_attack_style(
        &env, &env.pvp_runtime.opponent, self, target,
        OPP_STYLE_MASK_ALL, opp_get_off_prayer_mask(self, target));

    ASSERT_INT_EQ("distance 7 style", choice.style, OPP_STYLE_RANGED);
    ASSERT_INT_EQ("distance 7 can hit", choice.can_hit_now, 1);
}

static void test_resolver_protect_ranged_uses_mage(void) {
    printf("--- Resolver respects protect ranged at distance 10 ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_NIGHTMARE_NH);
    set_dead_zone_state(&env, OPP_NIGHTMARE_NH, 10);
    env.players[0].prayer = PRAYER_PROTECT_RANGED;
    Player* self = &env.players[1];
    Player* target = &env.players[0];

    OppStyleChoice choice = opp_resolve_attack_style(
        &env, &env.pvp_runtime.opponent, self, target,
        OPP_STYLE_MASK_ALL, opp_get_off_prayer_mask(self, target));

    ASSERT_INT_EQ("protect ranged style", choice.style, OPP_STYLE_MAGE);
    ASSERT_INT_EQ("protect ranged can hit", choice.can_hit_now, 1);
}

static void test_resolver_no_hit_preserves_preference(void) {
    printf("--- Resolver preserves fallback when no style can hit ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_NIGHTMARE_NH);
    set_dead_zone_state(&env, OPP_NIGHTMARE_NH, 11);
    Player* self = &env.players[1];
    Player* target = &env.players[0];

    OppStyleChoice choice = opp_resolve_attack_style(
        &env, &env.pvp_runtime.opponent, self, target,
        OPP_STYLE_MASK_ALL, OPP_STYLE_MASK_RANGED);

    ASSERT_INT_EQ("no hit fallback style", choice.style, OPP_STYLE_RANGED);
    ASSERT_INT_EQ("no hit fallback cannot hit", choice.can_hit_now, 0);
}

static void assert_policy_casts_when_only_mage_reaches(OpponentType type, const char* label) {
    OsrsEnv env;
    setup_pvp_env(&env, type);
    set_dead_zone_state(&env, type, 10);

    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_TRUE(label, opponent_casts_spell(&env));
}

static void test_scripted_policies_do_not_emit_unreachable_ranged(void) {
    printf("--- Scripted policies mage when ranged is unreachable ---\n");

    assert_policy_casts_when_only_mage_reaches(OPP_IMPROVED, "improved casts");
    assert_policy_casts_when_only_mage_reaches(OPP_ONETICK, "onetick casts");
    assert_policy_casts_when_only_mage_reaches(
        OPP_UNPREDICTABLE_IMPROVED, "unpredictable improved casts");
    assert_policy_casts_when_only_mage_reaches(
        OPP_UNPREDICTABLE_ONETICK, "unpredictable onetick casts");
    assert_policy_casts_when_only_mage_reaches(OPP_EXPERT_NH, "expert NH casts");
    assert_policy_casts_when_only_mage_reaches(OPP_NIGHTMARE_NH, "nightmare NH casts");
    assert_policy_casts_when_only_mage_reaches(OPP_BLOOD_HEALER, "blood healer casts");
    assert_policy_casts_when_only_mage_reaches(OPP_GMAUL_COMBO, "gmaul combo casts");
    assert_policy_casts_when_only_mage_reaches(OPP_RANGE_KITER, "range kiter casts");
    assert_policy_casts_when_only_mage_reaches(OPP_ADAPTIVE_NH, "adaptive NH casts");
}

static void test_veng_fighter_excludes_mage(void) {
    printf("--- Veng fighter excludes mage ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_VENG_FIGHTER);
    set_dead_zone_state(&env, OPP_VENG_FIGHTER, 10);
    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_TRUE("veng fighter does not mage", !opponent_casts_spell(&env));
}

static void test_spec_range_gate_respects_weapon_range(void) {
    printf("--- Spec range gate uses resolved weapon range ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_RANGE_KITER);
    set_dead_zone_state(&env, OPP_RANGE_KITER, 10);
    Player* self = &env.players[1];
    Player* target = &env.players[0];

    const uint8_t acb_weapons[] = {ITEM_ARMADYL_CROSSBOW, ITEM_AHRIM_STAFF};
    set_weapon_inventory(self, acb_weapons, 2);
    ASSERT_INT_EQ("armadyl crossbow spec cannot hit distance 10",
        opp_loadout_can_hit_now(&env, self, target, LOADOUT_SPEC_RANGE, OPP_STYLE_RANGED), 0);

    const uint8_t dark_bow_weapons[] = {ITEM_DARK_BOW, ITEM_AHRIM_STAFF};
    set_weapon_inventory(self, dark_bow_weapons, 2);
    ASSERT_INT_EQ("dark bow spec can hit distance 10",
        opp_loadout_can_hit_now(&env, self, target, LOADOUT_SPEC_RANGE, OPP_STYLE_RANGED), 1);
}

int main(void) {
    test_adaptive_nh_name_maps_correctly();
    test_hard_policies_do_not_farcast_while_adjacent();
    test_smart_hard_policy_avoids_cardinal_step_out();
    test_smart_hard_policy_converts_under_ranged_attack_to_move_only();
    test_smart_hard_policy_allows_diagonal_projectile_attack();
    test_smart_hard_policy_full_step_under_ranged_attack_does_not_attack();
    test_smart_hard_policy_treats_last_freeze_tick_as_unfrozen();
    test_dumb_hard_policy_still_walks_under_frozen_adjacent_target();
    test_hard_spacing_guard_does_not_move_when_self_frozen();
    test_adaptive_nh_read_chance_exceeds_nightmare();
    test_adaptive_nh_prays_melee_against_learned_mage_camp();
    test_adaptive_nh_prays_magic_without_learned_adjacent_melee();
    test_adaptive_nh_learns_static_mage_camp();
    test_adaptive_nh_attacks_mage_prayer_camp_with_melee();
    test_adaptive_nh_kites_unrooted_mage_prayer_camp();
    test_adaptive_nh_kites_freeze_immune_camp_without_spec();
    test_adaptive_nh_specs_mage_prayer_camp();
    test_adaptive_nh_reads_static_camp_action();
    test_adaptive_nh_brews_early_in_static_camp();
    test_adaptive_nh_moves_out_of_adjacent_camp_when_not_ready();
    test_resolver_distance_10_uses_mage_into_prayer();
    test_resolver_distance_7_uses_crossbow_off_prayer();
    test_resolver_protect_ranged_uses_mage();
    test_resolver_no_hit_preserves_preference();
    test_scripted_policies_do_not_emit_unreachable_ranged();
    test_veng_fighter_excludes_mage();
    test_spec_range_gate_respects_weapon_range();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed == 0 ? 0 : 1;
}
