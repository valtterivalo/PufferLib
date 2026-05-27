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

static void setup_pvp_env(OsrsEnv* env, OpponentType type) {
    memset(env, 0, sizeof(*env));
    pvp_init(env);
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
    memset(player->inventory[GEAR_SLOT_WEAPON], ITEM_NONE,
        sizeof(player->inventory[GEAR_SLOT_WEAPON]));
    player->num_items_in_slot[GEAR_SLOT_WEAPON] = (uint8_t)count;
    for (int i = 0; i < count; i++) {
        player->inventory[GEAR_SLOT_WEAPON][i] = weapons[i];
    }
    slot_equip_item(player, GEAR_SLOT_WEAPON, weapons[0]);
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
    set_player_position(self, FIGHT_AREA_BASE_X, FIGHT_AREA_BASE_Y);
    set_player_position(target, FIGHT_AREA_BASE_X + distance, FIGHT_AREA_BASE_Y);

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

static int opponent_loadout(const OsrsEnv* env) {
    return env->pending_actions[NUM_ACTION_HEADS + HEAD_LOADOUT];
}

static int opponent_combat(const OsrsEnv* env) {
    return env->pending_actions[NUM_ACTION_HEADS + HEAD_COMBAT];
}

static int opponent_casts_spell(const OsrsEnv* env) {
    int combat = opponent_combat(env);
    return opponent_loadout(env) == LOADOUT_MAGE &&
        (combat == ATTACK_ICE || combat == ATTACK_BLOOD);
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
}

static void test_veng_fighter_excludes_mage(void) {
    printf("--- Veng fighter excludes mage ---\n");

    OsrsEnv env;
    setup_pvp_env(&env, OPP_VENG_FIGHTER);
    set_dead_zone_state(&env, OPP_VENG_FIGHTER, 10);
    generate_opponent_action(&env, &env.pvp_runtime.opponent);

    ASSERT_TRUE("veng fighter does not mage", opponent_loadout(&env) != LOADOUT_MAGE);
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
        opp_loadout_can_hit_now(self, target, LOADOUT_SPEC_RANGE, OPP_STYLE_RANGED), 0);

    const uint8_t dark_bow_weapons[] = {ITEM_DARK_BOW, ITEM_AHRIM_STAFF};
    set_weapon_inventory(self, dark_bow_weapons, 2);
    ASSERT_INT_EQ("dark bow spec can hit distance 10",
        opp_loadout_can_hit_now(self, target, LOADOUT_SPEC_RANGE, OPP_STYLE_RANGED), 1);
}

int main(void) {
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
