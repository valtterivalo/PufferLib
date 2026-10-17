#include "../../osrs_riskfight/osrs_riskfight.h"
#include <assert.h>
#include <stdio.h>

static Env* env;
static float observations[2][RF_OBS_SIZE];
static float actions[2][RF_HEADS];
static float rewards[2], terminals[2];
static unsigned char masks[2][RF_MASK_SIZE];

static void reset(float damage_coeff) {
    riskfight_reset((EncounterState*)&env->state, (EncounterContext*)&env->context, 12345);
    riskfight_put_float((EncounterState*)&env->state, (EncounterContext*)&env->context,
        "damage_reward_coeff", damage_coeff);
    env->context.chance_reward_coeff = 0;
    env->session_elapsed = env->session_fights = env->wait_left = 0;
    env->session_stake = 0;
    memset(&env->log, 0, sizeof(env->log));
    memset(actions, 0, sizeof(actions));
    for (int i = 0; i < 2; i++) env->state.env.players[i].veng_active = 0;
}

static void queued_hit(int source, int damage, OsrsHitKind kind) {
    OsrsEnv* simulation = &env->state.env;
    queue_hit(simulation, source, 1 - source,
        &simulation->players[source], &simulation->players[1 - source],
        damage, ATTACK_STYLE_MELEE, 0, 0, damage > 0, 0, 0, 0, 0, 0);
    Player* attacker = &simulation->players[source];
    attacker->pending_hits[attacker->num_pending_hits - 1].kind = kind;
}

static void test_default_reward_and_tick_reset(void) {
    reset(0);
    queued_hit(0, 20, OSRS_HIT_DIRECT);
    puf_step(env);
    assert(rewards[0] == 0 && rewards[1] == 0);
    assert(env->state.env.players[1].current_hitpoints == 101);
    assert(!terminals[0] && !terminals[1]);
    assert(env->state.episode_returns[0] == 0);
    reset(0.00390625f);
    queued_hit(0, 20, OSRS_HIT_DIRECT);
    puf_step(env);
    assert(rewards[0] == 20 * 0.00390625f * RF_REWARD_SCALE && rewards[1] == 0);
    assert(env->state.env.players[1].current_hitpoints == 101);
    puf_step(env);
    assert(rewards[0] == 0 && rewards[1] == 0);
    assert(env->state.episode_returns[0] == 20 * 0.00390625f);
}

static void test_orb_and_passive_excluded(void) {
    reset(0.00390625f);
    actions[0][RF_ORB] = 1;
    queued_hit(0, 7, OSRS_HIT_RECOIL);
    queued_hit(1, 15, OSRS_HIT_VENGEANCE);
    puf_step(env);
    assert(env->state.env.players[0].current_hitpoints == 96);
    assert(env->state.env.players[1].current_hitpoints == 114);
    assert(rewards[0] == 0 && rewards[1] == 0);
}

static void test_terminal_shaping_keeps_raw_scores(void) {
    reset(0.00390625f);
    queued_hit(0, 20, OSRS_HIT_DIRECT);
    puf_step(env);
    assert(rewards[0] == 20 * 0.00390625f * RF_REWARD_SCALE);
    env->state.env.players[1].current_hitpoints = 10;
    queued_hit(0, 45, OSRS_HIT_DIRECT);
    puf_step(env);
    /* The kill ends the fight, not the session: the next fight starts, the session goes on. */
    assert(!terminals[0] && !terminals[1]);
    assert(rewards[0] == (1 + 10 * 0.00390625f) * RF_REWARD_SCALE);
    assert(rewards[1] == -1 * RF_REWARD_SCALE);
    assert(env->log.n == 0 && env->log.kills == 1 && env->log.deaths == 0);
    assert(env->session_stake == 1 && env->log.policy_0_score == 1 && env->log.draw_rate == 0);
    assert(env->log.damage_reward == 30 * 0.00390625f);
    assert(env->log.episode_return == 1 + 30 * 0.00390625f);
    puf_step(env);
    assert(rewards[0] == 0 && rewards[1] == 0);
    assert(env->log.n == 0);
}

/* A teleport carries no reward of its own, for either side. */
static void test_teleport_is_free(void) {
    for (int actor = 0; actor < 2; actor++) {
        reset(0);
        actions[actor][RF_PRIMARY] = RF_TELEPORT;
        puf_step(env);
        assert(!terminals[0] && !terminals[1]);
        assert(rewards[0] == 0 && rewards[1] == 0);
        assert(env->session_stake == 0 && env->log.policy_0_score == 0.5f && env->log.draw_rate == 1);
        assert(env->log.escapes == 1 && env->log.kills == 0 && env->log.deaths == 0);
        assert(env->log.self_teleports == (actor == 0) && env->log.opponent_teleports == (actor == 1));
    }
}

static void test_raw_winner_with_shaped_loser(void) {
    reset(1);
    env->state.env.pid_holder = 0;
    env->state.env.players[0].current_hitpoints = 5;
    queued_hit(0, 20, OSRS_HIT_DIRECT);
    queued_hit(1, 45, OSRS_HIT_DIRECT);
    HumanCommandQueue empty = {0};
    riskfight_step_queues(&env->state, &env->context, &empty, &empty);
    assert(env->state.outcome[0] == RISKFIGHT_DEATH);
    assert(env->state.outcome[1] == RISKFIGHT_KILL);
    assert(env->state.rewards[0] == 19 * RF_REWARD_SCALE);
    assert(env->state.env.winner == 1);
    assert(riskfight_outcome_reward(env->state.outcome[0], env->state.death_price) == -1);
}

static void test_native_reward_kwargs(void) {
    Env* configured = (Env*)calloc(1, sizeof(*configured));
    DictItem items[] = {
        {.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1},
        {.key = "session_ticks", .value = 100000}, {.key = "curveball_probability", .value = 0},
        {.key = "damage_reward_coeff", .value = 0.00390625},
        {.key = "chance_reward_coeff", .value = 0.125},
    };
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    puf_init(configured, &kwargs);
    assert(configured->context.damage_reward_coeff == 0.00390625f);
    assert(configured->context.chance_reward_coeff == 0.125f);
    puf_close(configured);
    free(configured);
}

static void observe_count(void* context, const OsrsPvpHitEvent* event) {
    int* count = (int*)context;
    assert(event->hitpoints_lost == 20);
    (*count)++;
}

static void test_external_observer_composes(void) {
    for (int shaping = 0; shaping < 2; shaping++) {
        reset(shaping ? 0.00390625f : 0);
        int count = 0;
        env->state.env.pvp_runtime.hit_observer = observe_count;
        env->state.env.pvp_runtime.hit_observer_context = &count;
        queued_hit(0, 20, OSRS_HIT_DIRECT);
        puf_step(env);
        assert(count == 1);
        assert(env->state.env.pvp_runtime.hit_observer == observe_count);
        assert(env->state.env.pvp_runtime.hit_observer_context == &count);
    }
}

static void test_chance_mass_bounded(void) {
    reset(0);
    RiskfightChanceObserver observer = {.state = &env->state};
    OsrsPvpAttackChanceEvent event = {.source = 0, .target = 1, .ko_probability = 0.5f};
    float total_reward = 0;
    for (int i = 0; i < 100; i++) {
        float previous = env->state.direct_ko_chance_mass[0];
        riskfight_observe_attack_chance(&observer, &event);
        float current = env->state.direct_ko_chance_mass[0];
        total_reward += 0.25f * (current - previous);
        assert(current >= previous && current <= 1);
        assert(total_reward <= 0.25f);
        if (i == 0) assert(current == 0.5f);
        if (i == 1) assert(current == 0.75f);
    }
    assert(total_reward == 0.25f);
    assert(env->state.direct_ko_chance_mass[1] == 0);
}

static void observe_chance_count(void* context, const OsrsPvpAttackChanceEvent* event) {
    assert(event->source == 0 && event->target == 1);
    assert(event->target_hitpoints == 1 && event->ko_probability > 0);
    (*(int*)context)++;
}

static void test_executed_chance_reward(void) {
    uint32_t final_rng = 0;
    for (int shaped = 0; shaped < 2; shaped++) {
        reset(0);
        env->context.chance_reward_coeff = shaped ? 0.25f : 0;
        env->state.env.players[1].current_hitpoints = 1;
        int count = 0;
        env->state.env.pvp_runtime.attack_chance_observer = observe_chance_count;
        env->state.env.pvp_runtime.attack_chance_observer_context = &count;
        HumanInput commands;
        human_input_init(&commands);
        int attack[RF_HEADS] = {0};
        attack[RF_PRIMARY] = RF_ATTACK;
        riskfight_policy_commands(&env->state, 0, attack, &commands);
        HumanCommandQueue empty = {0};
        riskfight_step_queues(&env->state, &env->context, &commands.commands, &empty);
        assert(count == 1 && env->state.direct_ko_chance_mass[0] > 0);
        assert(env->state.chance_rewards[0] == env->context.chance_reward_coeff * env->state.direct_ko_chance_mass[0]);
        assert(env->state.rewards[0] == (riskfight_outcome_reward(env->state.outcome[0], env->state.death_price) + env->state.chance_rewards[0]) * RF_REWARD_SCALE);
        assert(env->state.chance_rewards[1] == 0);
        assert(env->state.env.pvp_runtime.attack_chance_observer == observe_chance_count);
        assert(env->state.env.pvp_runtime.attack_chance_observer_context == &count);
        if (shaped) assert(final_rng == env->state.env.rng_state);
        final_rng = env->state.env.rng_state;
        free(commands.commands.items);
    }
    reset(0);
    env->context.chance_reward_coeff = 0.25f;
    actions[0][RF_ORB] = 1;
    queued_hit(0, 20, OSRS_HIT_DIRECT);
    puf_step(env);
    assert(env->state.direct_ko_chance_mass[0] == 0 && env->state.chance_rewards[0] == 0);
}

static void test_native_chance_logs(void) {
    reset(0);
    env->context.chance_reward_coeff = 0.25f;
    env->state.env.players[1].current_hitpoints = 1;
    actions[0][RF_PRIMARY] = RF_ATTACK;
    puf_step(env);
    if (!env->log.kills) {
        actions[0][RF_PRIMARY] = RF_STOP;
        queued_hit(0, 1, OSRS_HIT_DIRECT);
        puf_step(env);
    }
    assert(env->log.kills == 1 && env->session_stake == 1);
    assert(env->log.direct_ko_chance_mass > 0 && env->log.direct_ko_chance_mass <= 1);
    assert(env->log.chance_reward == 0.25f * env->log.direct_ko_chance_mass);
    assert(env->log.episode_return == 1 + env->log.chance_reward);
    while (env->wait_left > 0) puf_step(env); /* the next fight starts clean */
    assert(env->state.direct_ko_chance_mass[0] == 0 && env->state.chance_rewards[0] == 0);
}

static void test_voidwaker_chance_is_guaranteed_below_minimum(void) {
    reset(0);
    env->context.chance_reward_coeff = 0.25f;
    env->state.env.players[1].current_hitpoints = 1;
    int voidwaker[RF_HEADS] = {0};
    voidwaker[RF_WEAPON] = 24; /* the kit's voidwaker slot, as the net's entry of the canonical bag */
    riskfight_bag_to_canonical(&env->state.env.players[0], voidwaker);
    actions[0][RF_WEAPON] = (float)voidwaker[RF_WEAPON];
    actions[0][RF_SPECIAL] = 1;
    actions[0][RF_PRIMARY] = RF_ATTACK;
    puf_step(env);
    assert(env->log.direct_ko_chance_mass + env->state.direct_ko_chance_mass[0] == 1);
    assert(env->log.chance_reward + env->state.chance_rewards[0] == 0.25f);
    assert(rewards[0] == (env->log.kills ? 1.25f : 0.25f) * RF_REWARD_SCALE);
    assert(env->state.direct_ko_chance_mass[1] == 0);
}

int main(void) {
    env = (Env*)calloc(1, sizeof(*env));
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1},
        {.key = "session_ticks", .value = 100000}, {.key = "curveball_probability", .value = 0}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    puf_init(env, &kwargs);
    assert(env->context.damage_reward_coeff == 0);
    for (int i = 0; i < 2; i++) env->agents[i] = (Agent){
        .observations = observations[i], .actions = actions[i], .rewards = &rewards[i],
        .terminals = &terminals[i], .action_mask = masks[i], .policy = i};
    test_default_reward_and_tick_reset();
    test_orb_and_passive_excluded();
    test_terminal_shaping_keeps_raw_scores();
    test_teleport_is_free();
    test_external_observer_composes();
    test_raw_winner_with_shaped_loser();
    test_native_reward_kwargs();
    test_chance_mass_bounded();
    test_executed_chance_reward();
    test_native_chance_logs();
    test_voidwaker_chance_is_guaranteed_below_minimum();
    puf_close(env);
    free(env);
    puts("Riskfight reward contracts passed");
}
