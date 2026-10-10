#include "../../osrs_pvp/osrs_pvp.h"
#include "osrs_test_check.h"
#include "osrs_test_nh_pvp.h"
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static Env env;
static float observations[2][OBS_SIZE], actions[2][NUM_ATNS], rewards[2], terminals[2];
static unsigned char masks[2][NH_PVP_ACTION_MASK_SIZE];

static void reset(int self_play, double opponent_type) {
    memset(&env, 0, sizeof(env));
    memset(actions, 0, sizeof(actions));
    DictItem items[] = {
        {.key = "opponent_type", .value = opponent_type},
        {.key = "gear_tier", .value = 3},
        {.key = "is_lms", .value = 1},
        {.key = "use_c_opponent", .value = !self_play},
        {.key = "self_play", .value = self_play},
        {.key = "auto_reset", .value = 0},
        OSRS_TEST_NH_PVP_REWARD_ITEMS
    };
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    env.rng = 123;
    puf_init(&env, &kwargs);
    for (int i = 0; i < env.num_agents; i++) env.agents[i] = (Agent){
        .observations = observations[i], .actions = actions[i], .rewards = &rewards[i],
        .terminals = &terminals[i], .action_mask = masks[i], .policy = i,
    };
    puf_reset(&env);
}

int main(void) {
    for (int boundary = 0; boundary < 9; boundary++) {
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            if (boundary < 2) {
                ENCOUNTER_NH_PVP.init_state(NH_PVP_ENV_STATE(&env), NH_PVP_ENV_CONTEXT(&env));
                ENCOUNTER_NH_PVP.put_int(NH_PVP_ENV_STATE(&env), NH_PVP_ENV_CONTEXT(&env),
                    boundary == 0 ? "opponent_type" : "opponent_p0_type", boundary == 0 ? 8 : 32);
            } else if (boundary < 6) {
                const double rejected_types[] = {8, 13.5, OPP_NONE, OPP_SELFPLAY};
                reset(0, rejected_types[boundary - 2]);
            } else if (boundary < 8) {
                puf_set_bot_policy(&env, boundary == 6 ? OPP_NONE : OPP_SELFPLAY);
            } else {
                ENCOUNTER_NH_PVP.init_state(NH_PVP_ENV_STATE(&env), NH_PVP_ENV_CONTEXT(&env));
                ENCOUNTER_NH_PVP.put_int(NH_PVP_ENV_STATE(&env), NH_PVP_ENV_CONTEXT(&env),
                    "opponent_p0_type", OPP_SELFPLAY);
                ENCOUNTER_NH_PVP.put_int(NH_PVP_ENV_STATE(&env), NH_PVP_ENV_CONTEXT(&env),
                    "use_c_opponent_p0", 1);
                ENCOUNTER_NH_PVP.finalize_context(NH_PVP_ENV_STATE(&env), NH_PVP_ENV_CONTEXT(&env));
            }
            _exit(0);
        }
        int status;
        CHECK("rejected opponent stops at the consumer boundary",
            waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    }
    reset(1, OPP_SELFPLAY);
    CHECK("both players have policy slots", env.num_agents == 2 && env.agents[1].policy == 1);
    for (int i = 0; i < 2; i++) {
        Player* player = &env.state.env.players[i];
        player->x = FIGHT_AREA_BASE_X + 10 + 5 * i;
        player->y = FIGHT_AREA_BASE_Y + 10;
    }
    actions[1][OSRS_HEAD_PRIMARY] = 1;
    puf_step(&env);
    CHECK("second policy actions reach engine", env.state.env.last_executed_actions[OSRS_BASE_NUM_ACTION_HEADS] == 1);
    for (int i = 0; i < 2; i++) {
        unsigned char expected[NH_PVP_ACTION_MASK_SIZE];
        pvp_write_action_mask_bytes(expected, &env.state.env, i, env.context.route_topology);
        CHECK("each policy receives its own mask", memcmp(expected, masks[i], sizeof(expected)) == 0);
    }
    float before[OBS_SIZE], after[OBS_SIZE];
    pvp_write_observations(before, &env.state.env, 0);
    Player* opponent = &env.state.env.players[1];
    opponent->current_hitpoints = 42;
    opponent->current_prayer = 7;
    opponent->special_energy = 0;
    opponent->attack_timer = 6;
    opponent->food_timer = 2;
    opponent->potion_timer = 2;
    opponent->frozen_ticks = 31;
    opponent->freeze_immunity_ticks = 4;
    opponent->veng_active = 1;
    opponent->veng_cooldown = 49;
    opponent->offensive_prayer = OFFENSIVE_PRAYER_PIETY;
    opponent->num_pending_hits = 1;
    opponent->pending_hits[0].damage = 70;
    env.state.env.players[0].num_pending_hits = 1;
    env.state.env.players[0].pending_hits[0].damage = 50;
    pvp_write_observations(after, &env.state.env, 0);
    CHECK("hidden opponent state and rolled damage do not change observations",
        memcmp(before, after, sizeof(before)) == 0);
    CHECK("health bar stays stale until a hit", after[NH_PVP_TARGET_START] == 1);
    PendingHit hit = {.kind = OSRS_HIT_DIRECT};
    pvp_observe_hit(&env.state.env, 0, 1, &hit, 0, 42);
    pvp_write_observations(after, &env.state.env, 0);
    CHECK("visible hit refreshes quantized health bar", after[NH_PVP_TARGET_START] == 13.0f / 30.0f);
    puf_close(&env);

    reset(1, OPP_SELFPLAY);
    env.state.env.players[1].morr_dot_remaining = 5;
    env.state.env.players[1].morr_dot_tick_counter = 1;
    puf_step(&env);
    CHECK("bleed refreshes visible health bar", env.state.env.players[1].current_hitpoints == 94 &&
        observations[0][NH_PVP_TARGET_START] == 28.0f / 30.0f);
    CHECK("Morrigan bleed contributes actual HP loss to reward scales",
        env.state.env.players[0].damage_dealt_scale == 5.0f / 99.0f &&
        env.state.env.players[1].damage_received_scale == 5.0f / 99.0f);
    puf_close(&env);

    reset(1, OPP_SELFPLAY);
    env.state.env.tick = MAX_EPISODE_TICKS - 1;
    puf_step(&env);
    CHECK("timeout is a draw for both policies", terminals[0] == 1 && terminals[1] == 1 &&
        rewards[0] == 0 && rewards[1] == 0 && env.log.draw_rate == 1);
    CHECK("timeout gives no selection credit", env.log.wins == 0);
    puf_close(&env);

    for (int winner = -1; winner < 2; winner++) {
        reset(1, OPP_SELFPLAY);
        for (int i = 0; i < 2; i++)
            if (i != winner) env.state.env.players[i].current_hitpoints = 0;
        puf_step(&env);
        for (int i = 0; i < 2; i++) {
            float expected = winner < 0 ? 0 : (winner == i ? 1 : -1);
            CHECK("both actors receive their terminal outcome", rewards[i] == expected && terminals[i] == 1);
            CHECK("terminal resets each actor observation", observations[i][0] == 1);
        }
        CHECK("terminal resets the encounter", env.state.env.tick == 0);
        CHECK("selection score remains actual actor zero wins", env.log.wins == (winner == 0));
        CHECK("mutual death remains a draw", env.log.draw_rate == (winner < 0));
        puf_close(&env);
    }

    reset(1, OPP_SELFPLAY);
    OsrsEnv* pvp = &env.state.env;
    pvp->shaping = (RewardShapingConfig){
        .terminal_reward_coef = 1.0f,
        .damage_dealt_coef = 0.01f,
        .damage_received_coef = -0.02f,
        .correct_prayer_bonus = 0.03f,
        .prayer_penalty_enabled = 1,
        .prayer_switch_no_attack_penalty = -0.04f,
        .click_penalty_enabled = 1,
        .click_penalty_threshold = 1,
        .click_penalty_coef = -0.05f,
        .ko_bonus = 0.06f,
        .wasted_resources_penalty = -0.07f,
        .ko_supplies_bonus_coef = 0.08f,
    };
    for (int i = 0; i < 2; i++) {
        Player* player = &pvp->players[i];
        player->base_hitpoints = 100;
        player->damage_dealt_scale = 0.1f;
        player->damage_received_scale = 0.025f;
        player->just_attacked = 1;
        player->player_prayed_correct = 1;
        pvp->pvp_runtime.overhead_changed_this_tick[i] = 1;
        player->clicks_this_tick = 3;
        pvp->last_executed_actions[i * OSRS_BASE_NUM_ACTION_HEADS + OSRS_HEAD_OVERHEAD] = OVERHEAD_MAGE;
    }
    for (int mode = 0; mode < 2; mode++) {
        pvp->shaping.enabled = mode;
        pvp->shaping.shaping_scale = mode ? 0 : 1;
        for (int winner = -2; winner < 2; winner++) {
            pvp->episode_over = winner != -2;
            pvp->winner = winner;
            for (int i = 0; i < 2; i++) {
                float expected = winner < 0 ? 0 : (winner == i ? 1 : -1);
                CHECK("disabled or zero scale leaves only outcomes", calculate_reward(pvp, i) == expected);
            }
        }
        pvp->episode_over = 0;
        for (int i = 0; i < 2; i++) {
            pvp->players[i].just_attacked = 0;
            CHECK("idle prayer switches have no unshaped penalty", calculate_reward(pvp, i) == 0);
            pvp->players[1 - i].just_attacked = 1;
            CHECK("idle correct prayer has no unshaped bonus", calculate_reward(pvp, i) == 0);
            pvp->players[i].just_attacked = 1;
        }
    }
    pvp->shaping.enabled = 1;
    for (int scale = 1; scale <= 2; scale++) {
        pvp->shaping.shaping_scale = 0.5f * scale;
        for (int i = 0; i < 2; i++) {
            pvp->players[i].just_attacked = 1;
            pvp->players[1 - i].just_attacked = 1;
            ASSERT_FLOAT_NEAR("damage prayer and click shaping scale once",
                calculate_reward(pvp, i), -0.02f * pvp->shaping.shaping_scale, 0.000001f);
            pvp->players[1 - i].just_attacked = 0;
            ASSERT_FLOAT_NEAR("prayer switch penalties scale once",
                calculate_reward(pvp, i), -0.09f * pvp->shaping.shaping_scale, 0.000001f);
            pvp->players[1 - i].just_attacked = 1;
        }
        pvp->episode_over = 1;
        for (int winner = 0; winner < 2; winner++) {
            pvp->winner = winner;
            for (int i = 0; i < 2; i++) {
                float expected = winner == i
                    ? 1 + 0.12f * pvp->shaping.shaping_scale
                    : -1 - 0.09f * pvp->shaping.shaping_scale;
                ASSERT_FLOAT_NEAR("terminal shaping scales around symmetric outcomes",
                    calculate_reward(pvp, i), expected, 0.000001f);
            }
        }
        pvp->episode_over = 0;
    }
    puf_close(&env);

    reset(0, OPP_GENERALIST_NH);
    CHECK("bot evaluation uses one policy", env.num_agents == 1);
    for (unsigned i = 0; i < sizeof(NH_SCRIPTED_OPPONENTS) / sizeof(*NH_SCRIPTED_OPPONENTS); i++) {
        puf_set_bot_policy(&env, NH_SCRIPTED_OPPONENTS[i]);
        puf_reset(&env);
        CHECK("selected opponent survives reset",
            env.state.env.pvp_runtime.opponent.type == NH_SCRIPTED_OPPONENTS[i]);
    }
    env.log.wins = 0;
    env.log.damage_dealt = 99;
    Dict output = {0};
    puf_log(&env.log, &output);
    CHECK("damage without a win earns no selection score", dict_get(&output, "perf") == 0);
    dict_clear(&output);
    puf_close(&env);
    return osrs_test_summary();
}
