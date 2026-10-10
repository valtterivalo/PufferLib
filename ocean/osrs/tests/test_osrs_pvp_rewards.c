#include "../../osrs_pvp/osrs_pvp.h"
#include "osrs_test_check.h"
#include "osrs_test_nh_pvp.h"

int main(void) {
    {
        Env env = {0};
        DictItem items[] = {
            {.key = "opponent_type", .value = OPP_SELFPLAY},
            {.key = "gear_tier", .value = 3},
            {.key = "is_lms", .value = 1},
            {.key = "use_c_opponent", .value = 0},
            {.key = "self_play", .value = 1},
            {.key = "auto_reset", .value = 0},
            OSRS_TEST_NH_PVP_REWARD_ITEMS
        };
        Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
        dict_set(&kwargs, "terminal_reward_coef", 0.25);
        dict_set(&kwargs, "damage_dealt_coef", 0.000123);
        dict_set(&kwargs, "click_penalty_coef", -0.000321);
        dict_set(&kwargs, "shaping_enabled", 1);
        dict_set(&kwargs, "shaping_scale", 0.75);
        dict_set(&kwargs, "click_penalty_threshold", 7);
        puf_init(&env, &kwargs);
        RewardShapingConfig* shaping = &env.state.env.shaping;
        CHECK("native config preserves terminal, damage, and click coefficients",
            shaping->terminal_reward_coef == 0.25f &&
            shaping->damage_dealt_coef == 0.000123f &&
            shaping->click_penalty_coef == -0.000321f);
        CHECK("native config preserves shaping state and thresholds",
            shaping->enabled == 1 && shaping->shaping_scale == 0.75f &&
            shaping->click_penalty_threshold == 7);
    }

    {
        OsrsEnv env = {0};
        pvp_init(&env);
        CHECK("reward registry initializes the terminal coefficient to legacy scale",
            env.shaping.terminal_reward_coef == 1.0f);
        env.episode_over = 1;
        env.winner = 0;
        CHECK("terminal coefficient scales only the terminal outcome",
            calculate_reward(&env, 0) == 1.0f && calculate_reward(&env, 1) == -1.0f);
        env.shaping.terminal_reward_coef = 0.25f;
        CHECK("terminal coefficient exposes the sweep value",
            calculate_reward(&env, 0) == 0.25f && calculate_reward(&env, 1) == -0.25f);
    }

    {
        OsrsEnv env = {0};
        Player* attacker = &env.players[0];
        Player* defender = &env.players[1];
        attacker->base_hitpoints = 99;
        attacker->current_hitpoints = 99;
        defender->base_hitpoints = 99;
        defender->current_hitpoints = 1;
        defender->prayer = PRAYER_NONE;
        PendingHit hit = {.kind = OSRS_HIT_RECOIL, .damage = 10, .hit_success = 1};
        apply_damage(&env, 0, 1, &hit);

        CHECK("overkill rewards only HP actually removed",
            defender->current_hitpoints == 0 &&
            defender->damage_received_scale == 1.0f / 99.0f &&
            attacker->damage_dealt_scale == 1.0f / 99.0f);
        CHECK("overkill leaves cumulative damage and hit events unchanged",
            defender->total_damage_received == 10.0f / 99.0f &&
            attacker->total_damage_dealt == 10.0f / 99.0f &&
            defender->hit_damage == 10 && defender->render_hit_damage[0] == 10);

        env.shaping = (RewardShapingConfig){
            .enabled = 1,
            .shaping_scale = 1.0f,
            .damage_dealt_coef = 0.00075f,
            .damage_received_coef = -0.00075f,
            .terminal_reward_coef = 1.0f,
        };
        ASSERT_FLOAT_NEAR("damage shaping uses actual HP loss",
            calculate_reward(&env, 0), 0.00075f, 0.000001f);
        ASSERT_FLOAT_NEAR("received damage shaping uses actual HP loss",
            calculate_reward(&env, 1), -0.00075f, 0.000001f);
    }

    {
        OsrsEnv env = {0};
        Player* attacker = &env.players[0];
        Player* defender = &env.players[1];
        attacker->base_hitpoints = 99;
        attacker->current_hitpoints = 99;
        defender->base_hitpoints = 99;
        defender->current_hitpoints = 1;
        defender->prayer = PRAYER_NONE;
        defender->slot_gear_dirty = 1;
        PendingHit hit = {
            .kind = OSRS_HIT_DIRECT,
            .damage = 10,
            .attack_type = ATTACK_STYLE_MELEE,
            .defender_prayer_at_attack = PRAYER_NONE,
            .hit_success = 1,
        };
        apply_damage(&env, 0, 1, &hit);
        CHECK("ordinary hit shaping caps at remaining HP",
            defender->current_hitpoints == 0 &&
            defender->damage_received_scale == 1.0f / 99.0f &&
            attacker->damage_dealt_scale == 1.0f / 99.0f &&
            defender->total_damage_received == 10.0f / 99.0f &&
            attacker->total_damage_dealt == 10.0f / 99.0f);
    }

    {
        OsrsEnv env = {0};
        Player* attacker = &env.players[0];
        Player* target = &env.players[1];
        attacker->base_hitpoints = target->base_hitpoints = 99;
        attacker->current_hitpoints = target->current_hitpoints = 99;
        attacker->equipped[GEAR_SLOT_WEAPON] = ITEM_VOIDWAKER;
        attacker->slot_cached_bonuses.magic_attack = -20;
        attacker->slot_gear_dirty = 0;
        attacker->just_attacked = 1;
        attacker->attack_style_this_tick = ATTACK_STYLE_MAGIC;
        attacker->used_special_this_tick = 1;
        attacker->attack_was_on_prayer = 1;
        target->prayer = PRAYER_PROTECT_MAGIC;
        env.shaping = (RewardShapingConfig){
            .enabled = 1,
            .shaping_scale = 1.0f,
            .magic_no_staff_penalty = -0.05f,
            .gear_mismatch_penalty = -0.05f,
            .spec_off_prayer_bonus = 0.02f,
            .terminal_reward_coef = 1.0f,
        };
        CHECK("Voidwaker magic-style special does not incur mage-gear penalties",
            calculate_reward(&env, 0) == 0.0f);

        attacker->attack_was_on_prayer = 0;
        CHECK("special prayer reward uses launch prayer result",
            calculate_reward(&env, 0) == 0.02f);

        attacker->equipped[GEAR_SLOT_WEAPON] = ITEM_RUNE_CROSSBOW;
        attacker->used_special_this_tick = 0;
        attacker->magic_type_this_tick = OSRS_MAGIC_ATTACK_ANCIENT_ICE;
        attacker->attack_was_on_prayer = 1;
        CHECK("manual spell without a magic weapon retains both gear penalties",
            calculate_reward(&env, 0) == -0.10f);
    }

    {
        OsrsEnv env = {0};
        Player* player = &env.players[0];
        player->base_hitpoints = 99;
        player->current_hitpoints = 90;
        player->prev_hp_percent = 70.0f / 99.0f;
        player->ate_food_this_tick = 1;
        player->ate_karambwan_this_tick = 1;
        player->last_karambwan_waste = 9;
        env.shaping = (RewardShapingConfig){
            .enabled = 1,
            .shaping_scale = 1.0f,
            .wasted_eat_penalty = -0.001f,
            .premature_eat_threshold = 1.0f,
            .terminal_reward_coef = 1.0f,
        };
        ASSERT_FLOAT_NEAR("food plus karambwan uses measured waste",
            calculate_reward(&env, 0), -0.009f, 0.000001f);

        player->ate_brew_this_tick = 1;
        player->last_karambwan_waste = 6;
        env.shaping.wasted_eat_penalty = 0.0f;
        env.shaping.wasted_triple_eat_penalty = -0.0005f;
        player->prev_hp_percent = 80.0f / 99.0f;
        ASSERT_FLOAT_NEAR("triple-eat penalty uses measured karambwan waste",
            calculate_reward(&env, 0), -0.003f, 0.000001f);
    }

    {
        OsrsEnv env = {0};
        Player* player = &env.players[0];
        int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
        actions[OSRS_HEAD_OVERHEAD] = ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC;
        player->current_prayer = 99;
        player->prayer = PRAYER_PROTECT_MAGIC;
        pvp_apply_control_actions(&env, 0, actions, 0);
        CHECK("reselecting the same overhead is not a prayer switch",
            !env.pvp_runtime.overhead_changed_this_tick[0]);

        player->current_prayer = 0;
        player->prayer = PRAYER_NONE;
        pvp_apply_control_actions(&env, 0, actions, 0);
        CHECK("unaffordable overhead request is not a prayer switch",
            !env.pvp_runtime.overhead_changed_this_tick[0] && player->prayer == PRAYER_NONE);

        player->current_prayer = 99;
        env.last_executed_actions[OSRS_HEAD_OVERHEAD] = OVERHEAD_MAGE;
        env.shaping = (RewardShapingConfig){
            .enabled = 1,
            .shaping_scale = 1.0f,
            .prayer_penalty_enabled = 1,
            .prayer_switch_no_attack_penalty = -0.01f,
            .terminal_reward_coef = 1.0f,
        };
        CHECK("same-prayer overhead request receives no switch penalty",
            calculate_reward(&env, 0) == 0.0f);

        player->prayer = PRAYER_NONE;
        pvp_apply_control_actions(&env, 0, actions, 0);
        CHECK("actual overhead transition is captured", env.pvp_runtime.overhead_changed_this_tick[0]);
        CHECK("actual overhead transition receives switch shaping",
            calculate_reward(&env, 0) == -0.01f);
    }

    return osrs_test_summary();
}
