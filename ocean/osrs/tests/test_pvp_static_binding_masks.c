#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ocean/osrs_pvp/binding.c"

cudaError_t cudaHostAlloc(void** ptr, size_t size, unsigned int flags) {
    (void)flags;
    *ptr = calloc(1, size);
    return *ptr ? 0 : 1;
}

cudaError_t cudaMalloc(void** ptr, size_t size) {
    *ptr = calloc(1, size);
    return *ptr ? 0 : 1;
}

cudaError_t cudaMemcpy(void* dst, const void* src, size_t size, cudaMemcpyKind kind) {
    (void)kind;
    memcpy(dst, src, size);
    return 0;
}

cudaError_t cudaMemcpyAsync(
    void* dst,
    const void* src,
    size_t size,
    cudaMemcpyKind kind,
    cudaStream_t stream
) {
    (void)stream;
    return cudaMemcpy(dst, src, size, kind);
}

cudaError_t cudaMemset(void* ptr, int value, size_t size) {
    memset(ptr, value, size);
    return 0;
}

cudaError_t cudaFree(void* ptr) {
    free(ptr);
    return 0;
}

cudaError_t cudaFreeHost(void* ptr) {
    free(ptr);
    return 0;
}

cudaError_t cudaSetDevice(int device) {
    (void)device;
    return 0;
}

cudaError_t cudaDeviceSynchronize(void) {
    return 0;
}

cudaError_t cudaStreamSynchronize(cudaStream_t stream) {
    (void)stream;
    return 0;
}

cudaError_t cudaStreamCreateWithFlags(cudaStream_t* stream, unsigned int flags) {
    (void)flags;
    *stream = NULL;
    return 0;
}

cudaError_t cudaStreamQuery(cudaStream_t stream) {
    (void)stream;
    return 0;
}

const char* cudaGetErrorString(cudaError_t error) {
    (void)error;
    return "cuda stub";
}

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static int test_pvp_projectile_can_reach(
    const CollisionMap* cmap,
    int source_x,
    int source_y,
    int target_x,
    int target_y,
    int range
) {
    OsrsAttackReachQuery reach = {
        .source = osrs_footprint(source_x, source_y, 1),
        .target = osrs_footprint(target_x, target_y, 1),
        .delivery = OSRS_ATTACK_DELIVERY_PROJECTILE,
        .range = range,
        .occlusion = osrs_projectile_occlusion_collision_map(cmap, 0),
    };
    return osrs_attack_can_reach(&reach);
}

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

#define ASSERT_FLOAT_NEAR(label, actual, expected, tolerance) do { \
    tests_run++; \
    float actual_value = (float)(actual); \
    float expected_value = (float)(expected); \
    if (fabsf(actual_value - expected_value) <= (tolerance)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s got %.6f expected %.6f\n", \
            (label), actual_value, expected_value); \
    } \
} while (0)

static int test_gear_bonuses_equal(GearBonuses a, GearBonuses b) {
    return a.stab_attack == b.stab_attack &&
        a.slash_attack == b.slash_attack &&
        a.crush_attack == b.crush_attack &&
        a.magic_attack == b.magic_attack &&
        a.ranged_attack == b.ranged_attack &&
        a.stab_defence == b.stab_defence &&
        a.slash_defence == b.slash_defence &&
        a.crush_defence == b.crush_defence &&
        a.magic_defence == b.magic_defence &&
        a.ranged_defence == b.ranged_defence &&
        a.melee_strength == b.melee_strength &&
        a.ranged_strength == b.ranged_strength &&
        a.magic_strength == b.magic_strength &&
        a.attack_speed == b.attack_speed &&
        a.attack_range == b.attack_range;
}

static void assert_projection_int_eq(
    const char* label,
    int actual,
    int expected,
    int state,
    uint8_t item_idx
) {
    tests_run++;
    if (actual == expected) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s state %d item %u got %d expected %d\n",
            label, state, (unsigned)item_idx, actual, expected);
    }
}

static void assert_projection_gear_eq(
    const char* label,
    GearBonuses actual,
    GearBonuses expected,
    int state,
    uint8_t item_idx
) {
    tests_run++;
    if (test_gear_bonuses_equal(actual, expected)) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s state %d item %u\n", label, state, (unsigned)item_idx);
        printf("    attack got %d/%d/%d/%d/%d expected %d/%d/%d/%d/%d\n",
            actual.stab_attack, actual.slash_attack, actual.crush_attack,
            actual.magic_attack, actual.ranged_attack,
            expected.stab_attack, expected.slash_attack, expected.crush_attack,
            expected.magic_attack, expected.ranged_attack);
        printf("    defence got %d/%d/%d/%d/%d expected %d/%d/%d/%d/%d\n",
            actual.stab_defence, actual.slash_defence, actual.crush_defence,
            actual.magic_defence, actual.ranged_defence,
            expected.stab_defence, expected.slash_defence, expected.crush_defence,
            expected.magic_defence, expected.ranged_defence);
        printf("    strength speed range got %d/%d/%d/%d/%d expected %d/%d/%d/%d/%d\n",
            actual.melee_strength, actual.ranged_strength, actual.magic_strength,
            actual.attack_speed, actual.attack_range,
            expected.melee_strength, expected.ranged_strength, expected.magic_strength,
            expected.attack_speed, expected.attack_range);
    }
}

static Dict* pvp_kwargs(void) {
    Dict* kwargs = create_dict(12);
    dict_set(kwargs, "seed", 73);
    dict_set(kwargs, "use_rollout_opponent", 1);
    dict_set(kwargs, "opponent_type", OPP_IMPROVED);
    dict_set(kwargs, "gear_tier", 3);
    dict_set(kwargs, "shaping_enabled", 1);
    dict_set(kwargs, "shaping_scale", 1.0);
    return kwargs;
}

static Dict* pvp_vec_kwargs(int total_agents, int num_buffers) {
    Dict* kwargs = create_dict(4);
    dict_set(kwargs, "total_agents", total_agents);
    dict_set(kwargs, "num_buffers", num_buffers);
    return kwargs;
}

static StaticVec* pvp_test_vec(int total_agents, int num_buffers) {
    Dict* vec_kwargs = pvp_vec_kwargs(total_agents, num_buffers);
    Dict* env_kwargs = pvp_kwargs();
    StaticVec* vec = create_static_vec(total_agents, num_buffers, 0, vec_kwargs, env_kwargs);
    static_vec_reset(vec);
    free(vec_kwargs->items);
    free(vec_kwargs);
    free(env_kwargs->items);
    free(env_kwargs);
    return vec;
}

static int action_head_offset(int head) {
    int offset = 0;
    for (int i = 0; i < head; i++) {
        offset += ACTION_HEAD_DIMS[i];
    }
    return offset;
}

typedef struct {
    int p0_x;
    int p0_y;
    int p1_x;
    int p1_y;
    int pid_holder;
} PvpResetSignature;

static void pvp_setup_seeded_reset_env(OsrsEnv* env, CollisionMap* cmap, uint32_t seed) {
    memset(env, 0, sizeof(*env));
    pvp_init(env);
    env->collision_map = cmap;
    env->ocean_io.agent_obs = env->_obs_buf;
    env->ocean_io.agent_actions = env->_acts_buf;
    env->ocean_io.agent_rewards = env->_rews_buf;
    env->ocean_io.agent_terminals = env->_terms_buf;
    env->pvp_runtime.gear_tier_weights[3] = 1.0f;
    pvp_seed(env, seed);
}

static PvpResetSignature pvp_reset_signature(OsrsEnv* env) {
    return (PvpResetSignature){
        .p0_x = env->players[0].x,
        .p0_y = env->players[0].y,
        .p1_x = env->players[1].x,
        .p1_y = env->players[1].y,
        .pid_holder = env->pid_holder,
    };
}

static int pvp_reset_signature_equal(PvpResetSignature a, PvpResetSignature b) {
    return a.p0_x == b.p0_x &&
        a.p0_y == b.p0_y &&
        a.p1_x == b.p1_x &&
        a.p1_y == b.p1_y &&
        a.pid_holder == b.pid_holder;
}

static void test_terminal_reward_survives_auto_reset(void) {
    printf("--- PvP terminal reward survives auto-reset ---\n");

    StaticVec* vec = pvp_test_vec(2, 1);
    Env* env = &vec->envs[0];
    env->pvp.shaping.enabled = 0;
    env->pvp.players[1].current_hitpoints = 0;

    c_step(env);

    ASSERT_FLOAT_NEAR("p0 terminal win reward", vec->rewards[0], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("p1 terminal loss reward", vec->rewards[1], 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("p0 terminal flag", vec->terminals[0], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("p1 terminal flag", vec->terminals[1], 1.0f, 1e-6f);

    static_vec_close(vec);
}

static void test_terminal_loss_and_draw_rewards_zero(void) {
    printf("--- PvP terminal loss and draw rewards are zero ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    pvp_reset(&env);
    env.shaping.enabled = 0;
    env.episode_over = 1;

    env.winner = 1;
    ASSERT_FLOAT_NEAR("p0 loss reward", calculate_reward(&env, 0), 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("p1 win reward", calculate_reward(&env, 1), 1.0f, 1e-6f);

    env.winner = -1;
    ASSERT_FLOAT_NEAR("p0 draw reward", calculate_reward(&env, 0), 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("p1 draw reward", calculate_reward(&env, 1), 0.0f, 1e-6f);

    collision_map_free(cmap);
}

static void test_timeout_and_simultaneous_death_are_draws(void) {
    printf("--- PvP timeout and simultaneous death are draws ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    env.auto_reset = 0;
    pvp_reset(&env);
    env.tick = MAX_EPISODE_TICKS - 1;
    pvp_step(&env);

    ASSERT_INT_EQ("timeout winner draw", env.winner, -1);
    ASSERT_FLOAT_NEAR("timeout p0 reward", env.step_rewards[0], 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("timeout p1 reward", env.step_rewards[1], 0.0f, 1e-6f);

    pvp_reset(&env);
    env.auto_reset = 0;
    env.players[0].current_hitpoints = 0;
    env.players[1].current_hitpoints = 0;
    pvp_step(&env);

    ASSERT_INT_EQ("sim death winner draw", env.winner, -1);
    ASSERT_FLOAT_NEAR("sim death p0 reward", env.step_rewards[0], 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("sim death p1 reward", env.step_rewards[1], 0.0f, 1e-6f);

    collision_map_free(cmap);
}

static void test_p1_reset_obs_refreshes_after_auto_reset(void) {
    printf("--- PvP p1 reset obs refreshes after auto-reset ---\n");

    StaticVec* vec = pvp_test_vec(2, 1);
    Env* env = &vec->envs[0];
    env->pvp.shaping.enabled = 0;
    env->pvp.players[1].current_hitpoints = 0;

    c_step(env);

    ASSERT_FLOAT_NEAR("p1 reset self hp obs", vec->observations.data[OBS_SIZE + 10], 1.0f, 1e-6f);

    static_vec_close(vec);
}

static void test_target_hp_observation_is_current(void) {
    printf("--- PvP target HP obs is current ---\n");

    StaticVec* vec = pvp_test_vec(2, 1);
    Env* env = &vec->envs[0];

    ASSERT_FLOAT_NEAR("target hp starts full", vec->observations.data[11], 1.0f, 1e-6f);

    env->pvp.players[1].current_hitpoints = env->pvp.players[1].base_hitpoints / 2;
    generate_slot_observations(&env->pvp, 0);
    ocean_write_obs(&env->pvp);

    float expected = (float)env->pvp.players[1].current_hitpoints /
        (float)env->pvp.players[1].base_hitpoints;
    ASSERT_FLOAT_NEAR("target hp reflects damage", vec->observations.data[11], expected, 1e-6f);

    env->pvp.players[1].current_hitpoints = env->pvp.players[1].base_hitpoints;
    generate_slot_observations(&env->pvp, 0);
    ocean_write_obs(&env->pvp);
    ASSERT_FLOAT_NEAR("target hp reflects healing", vec->observations.data[11], 1.0f, 1e-6f);

    static_vec_close(vec);
}

static void test_shaping_disabled_emits_only_terminal_reward(void) {
    printf("--- PvP shaping disabled emits only terminal reward ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    pvp_reset(&env);
    env.shaping.enabled = 0;
    env.players[0].damage_dealt_scale = 1.0f;
    env.players[1].just_attacked = 1;
    env.players[0].player_prayed_correct = 1;

    ASSERT_FLOAT_NEAR("nonterminal sparse reward", calculate_reward(&env, 0), 0.0f, 1e-6f);

    env.episode_over = 1;
    env.winner = 0;
    ASSERT_FLOAT_NEAR("terminal sparse win reward", calculate_reward(&env, 0), 1.0f, 1e-6f);

    collision_map_free(cmap);
}

static void test_expected_damage_reward_works_without_legacy_shaping(void) {
    printf("--- PvP expected damage reward works without legacy shaping ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    pvp_reset(&env);
    env.shaping.enabled = 0;
    env.shaping.expected_damage_reward_coef = 0.01f;
    env.players[0].expected_damage_dealt_tick = 25.0f;

    ASSERT_FLOAT_NEAR("expected damage dense reward",
        calculate_reward(&env, 0), 0.25f, 1e-6f);

    collision_map_free(cmap);
}

static void test_incoming_damage_avoidance_reward_works_without_legacy_shaping(void) {
    printf("--- PvP incoming damage avoidance reward works without legacy shaping ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    pvp_reset(&env);
    env.shaping.enabled = 0;
    env.shaping.incoming_damage_avoidance_reward_coef = 0.02f;
    env.players[0].expected_damage_prevented_tick = 10.0f;

    ASSERT_FLOAT_NEAR("incoming damage avoidance dense reward",
        calculate_reward(&env, 0), 0.2f, 1e-6f);

    collision_map_free(cmap);
}

static void test_ko_supply_reward_works_without_legacy_shaping(void) {
    printf("--- PvP KO supply reward works without legacy shaping ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    pvp_reset(&env);
    env.shaping.enabled = 0;
    env.shaping.ko_supply_reward_coef = 0.5f;
    env.episode_over = 1;
    env.winner = 0;

    Player* target = &env.players[1];
    target->food_count = 1;
    target->karambwan_count = 1;
    target->brew_doses = 1;
    float expected = 1.0f
        + 0.5f * pvp_remaining_supply_hp_fraction(target);

    ASSERT_FLOAT_NEAR("KO supply terminal reward",
        calculate_reward(&env, 0), expected, 1e-6f);

    collision_map_free(cmap);
}

static void test_ko_chance_reward_works_without_legacy_shaping(void) {
    printf("--- PvP KO chance reward works without legacy shaping ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    pvp_reset(&env);
    env.shaping.enabled = 0;
    env.shaping.ko_chance_reward_coef = 0.5f;

    register_ko_chance(&env, 0, 0.25f);

    ASSERT_FLOAT_NEAR("KO chance dense reward",
        calculate_reward(&env, 0), 0.125f, 1e-6f);
    ASSERT_FLOAT_NEAR("KO chance count",
        env.players[0].ko_chance_count, 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("KO chance survival product",
        env.players[0].ko_chance_survival_prob, 0.75f, 1e-6f);

    collision_map_free(cmap);
}

static void test_reward_coefficients_parse_from_binding_kwargs(void) {
    printf("--- PvP reward coefficients parse from binding kwargs ---\n");

    Dict* kwargs = pvp_kwargs();
    dict_set(kwargs, "expected_damage_reward_coef", 0.0125);
    dict_set(kwargs, "incoming_damage_avoidance_reward_coef", 0.025);
    dict_set(kwargs, "ko_supply_reward_coef", 0.75);
    dict_set(kwargs, "ko_chance_reward_coef", 1.25);
    Dict* vec_kwargs = pvp_vec_kwargs(2, 1);

    StaticVec* vec = create_static_vec(2, 1, 0, vec_kwargs, kwargs);
    Env* env = &vec->envs[0];

    ASSERT_FLOAT_NEAR("expected damage reward coef",
        env->pvp.shaping.expected_damage_reward_coef, 0.0125f, 1e-6f);
    ASSERT_FLOAT_NEAR("incoming damage avoidance reward coef",
        env->pvp.shaping.incoming_damage_avoidance_reward_coef, 0.025f, 1e-6f);
    ASSERT_FLOAT_NEAR("KO supply reward coef",
        env->pvp.shaping.ko_supply_reward_coef, 0.75f, 1e-6f);
    ASSERT_FLOAT_NEAR("KO chance reward coef",
        env->pvp.shaping.ko_chance_reward_coef, 1.25f, 1e-6f);

    static_vec_close(vec);
    free(vec_kwargs->items);
    free(vec_kwargs);
    free(kwargs->items);
    free(kwargs);
}

static void test_shaping_uses_encounter_overhead_and_spec_prayer_style(void) {
    printf("--- PvP shaping overhead and spec prayer style ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    pvp_reset(&env);
    env.shaping.enabled = 1;
    env.shaping.shaping_scale = 1.0f;
    env.shaping.prayer_penalty_enabled = 1;
    env.shaping.prayer_switch_no_attack_penalty = -0.25f;
    env.players[1].just_attacked = 0;
    env.last_executed_actions[HEAD_OVERHEAD] = ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC;

    ASSERT_FLOAT_NEAR("encounter overhead penalty", calculate_reward(&env, 0), -0.25f, 1e-6f);

    env.last_executed_actions[HEAD_OVERHEAD] = ENCOUNTER_OVERHEAD_OFF;
    ASSERT_FLOAT_NEAR("overhead off no penalty", calculate_reward(&env, 0), 0.0f, 1e-6f);

    pvp_reset(&env);
    env.shaping.enabled = 1;
    env.shaping.shaping_scale = 1.0f;
    env.shaping.spec_off_prayer_bonus = 0.75f;
    env.players[0].just_attacked = 1;
    env.players[0].used_special_this_tick = 1;
    env.players[0].target_prayed_correct = 1;
    ASSERT_FLOAT_NEAR("spec on-prayer no bonus", calculate_reward(&env, 0), 0.0f, 1e-6f);

    env.players[0].target_prayed_correct = 0;
    ASSERT_FLOAT_NEAR("spec off-prayer bonus", calculate_reward(&env, 0), 0.75f, 1e-6f);

    collision_map_free(cmap);
}

static void test_scripted_legacy_movement_maps_to_head_move(void) {
    printf("--- PvP scripted legacy movement maps to HEAD_MOVE ---\n");

    StaticVec* vec = pvp_test_vec(2, 1);
    Env* env = &vec->envs[0];
    Player* p = &env->pvp.players[1];
    Player* target = &env->pvp.players[0];
    pvp_set_player_spawn(p, 3041, 3530);
    pvp_set_player_spawn(target, 3043, 3530);
    p->last_obs_target_x = target->x;
    p->last_obs_target_y = target->y;

    int actions[NUM_ACTION_HEADS] = {0};
    actions[HEAD_LOADOUT] = LOADOUT_KEEP;
    actions[HEAD_COMBAT] = MOVE_UNDER;
    pvp_translate_legacy_loadout_action_to_slotclicks(&env->pvp, 1, actions);

    ASSERT_INT_EQ("legacy movement clears attack head", actions[HEAD_ATTACK], ATTACK_NONE);
    ASSERT_TRUE("legacy movement sets head move", actions[HEAD_MOVE] > 0);
    ASSERT_INT_EQ("legacy movement head exact x",
        p->x + ENCOUNTER_MOVE_TARGET_DX[actions[HEAD_MOVE]], target->x);
    ASSERT_INT_EQ("legacy movement head exact y",
        p->y + ENCOUNTER_MOVE_TARGET_DY[actions[HEAD_MOVE]], target->y);

    actions[HEAD_MOVE] = pvp_head_move_toward_tile(p, p->x - 1, p->y);
    actions[HEAD_COMBAT] = MOVE_UNDER;
    pvp_set_player_spawn(p, 3041, 3530);
    pvp_set_player_spawn(target, 3046, 3531);
    target->frozen_ticks = 5;
    p->last_obs_target_x = target->x;
    p->last_obs_target_y = target->y;
    pvp_translate_legacy_loadout_action_to_slotclicks(&env->pvp, 1, actions);

    ASSERT_INT_EQ("nonexact movement clears attack head", actions[HEAD_ATTACK], ATTACK_NONE);
    ASSERT_INT_EQ("nonexact movement clears stale head move", actions[HEAD_MOVE], MOVE_NONE);
    ASSERT_INT_EQ("nonexact movement walk dest x",
        env->pvp.pvp_runtime.walk_dest_x[1], target->x);
    ASSERT_INT_EQ("nonexact movement walk dest y",
        env->pvp.pvp_runtime.walk_dest_y[1], target->y);

    static_vec_close(vec);
}

static void test_duplicate_equip_clicks_apply_once(void) {
    printf("--- PvP duplicate equip clicks apply once ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    pvp_reset(&env);
    Player* p = &env.players[0];
    osrs_player_inventory_clear(p);
    osrs_player_set_equipment_slot(p, GEAR_SLOT_WEAPON, ITEM_AHRIM_STAFF);
    p->inventory[0] = ITEM_RUNE_CROSSBOW;

    int actions[NUM_ACTION_HEADS] = {0};
    actions[HEAD_EQUIP_0] = 1;
    actions[HEAD_EQUIP_1] = 1;
    int clicks = execute_equip_clicks(&env, 0, actions);

    ASSERT_INT_EQ("duplicate equip click success count", clicks, 1);
    ASSERT_INT_EQ("crossbow equipped once", p->equipped[GEAR_SLOT_WEAPON], ITEM_RUNE_CROSSBOW);
    ASSERT_INT_EQ("old weapon swapped into slot", p->inventory[0], ITEM_AHRIM_STAFF);

    collision_map_free(cmap);
}

static void test_food_brew_karambwan_can_resolve_same_tick(void) {
    printf("--- PvP food brew karambwan same tick ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    pvp_reset(&env);
    Player* p = &env.players[0];

    p->base_hitpoints = 99;
    p->current_hitpoints = 30;
    p->base_attack = 99;
    p->base_strength = 99;
    p->base_defence = 99;
    p->base_ranged = 99;
    p->base_magic = 99;
    p->current_attack = 99;
    p->current_strength = 99;
    p->current_defence = 99;
    p->current_ranged = 99;
    p->current_magic = 99;
    p->food_count = 1;
    p->brew_doses = 1;
    p->karambwan_count = 1;
    p->food_timer = 0;
    p->potion_timer = 0;
    p->karambwan_timer = 0;

    int actions[NUM_ACTION_HEADS] = {0};
    actions[HEAD_FOOD] = FOOD_EAT;
    actions[HEAD_POTION] = POTION_BREW;
    actions[HEAD_KARAMBWAN] = KARAM_EAT;
    execute_switches(&env, 0, actions);

    ASSERT_INT_EQ("same tick triple eat hp", p->current_hitpoints, 84);
    ASSERT_INT_EQ("same tick food consumed", p->food_count, 0);
    ASSERT_INT_EQ("same tick brew consumed", p->brew_doses, 0);
    ASSERT_INT_EQ("same tick karambwan consumed", p->karambwan_count, 0);
    ASSERT_INT_EQ("same tick food flag", p->ate_food_this_tick, 1);
    ASSERT_INT_EQ("same tick brew flag", p->ate_brew_this_tick, 1);
    ASSERT_INT_EQ("same tick karambwan flag", p->ate_karambwan_this_tick, 1);
    ASSERT_INT_EQ("same tick brew heal", p->last_brew_heal, 16);
    ASSERT_INT_EQ("same tick karambwan locks potion", p->potion_timer, 3);

    collision_map_free(cmap);
}

static void test_static_vec_train_mask_round_trip(void) {
    printf("--- Static vec train mask round trip ---\n");

    StaticVec* vec = pvp_test_vec(2, 1);
    unsigned char train_mask[2] = {1, 0};
    static_vec_set_train_mask(vec, train_mask);

    ASSERT_INT_EQ("host train mask p0", vec->train_mask[0], 1);
    ASSERT_INT_EQ("host train mask p1", vec->train_mask[1], 0);
    ASSERT_INT_EQ("device train mask p0", vec->gpu_train_mask[0], 1);
    ASSERT_INT_EQ("device train mask p1", vec->gpu_train_mask[1], 0);

    static_vec_close(vec);
}

static void test_pvp_state_snapshot_restores_logical_state(void) {
    printf("--- PvP state snapshot restores logical state ---\n");

    StaticVec* vec = pvp_test_vec(4, 1);
    Env* src = &vec->envs[0];
    Env* dst = &vec->envs[1];

    src->pvp.players[0].current_hitpoints = 57;
    src->pvp.players[1].current_hitpoints = 63;
    src->pvp.players[0].attack_timer = 2;
    src->pvp.players[1].frozen_ticks = 8;
    src->pvp.tick = 42;
    src->pvp.rng_state = 1234567u;
    src->pvp.pending_actions[HEAD_ATTACK] = ATTACK_ICE;
    src->pvp.last_executed_actions[NUM_ACTION_HEADS + HEAD_OVERHEAD] =
        ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC;
    src->pvp.pvp_runtime.walk_dest_x[0] = src->pvp.players[0].x + 1;
    src->pvp.pvp_runtime.walk_dest_y[0] = src->pvp.players[0].y;
    src->pvp.pvp_runtime.opponent.type = OPP_NIGHTMARE_NH;

    pvp_generate_slot_observations_and_masks(&src->pvp, 0);
    pvp_generate_slot_observations_and_masks(&src->pvp, 1);
    ocean_write_obs(&src->pvp);
    ocean_write_obs_p1(&src->pvp);
    pvp_env_copy_action_masks_to_rollout(src);
    pvp_state_store(src, &src->state);

    void* dst_collision_map = dst->pvp.collision_map;
    void* dst_agent_obs = dst->pvp.ocean_io.agent_obs;
    void* dst_agent_obs_p1 = dst->pvp.ocean_io.agent_obs_p1;
    unsigned char* dst_action_mask = dst->pvp.action_masks;
    memset(dst->pvp._obs_buf, 0x5a, sizeof(dst->pvp._obs_buf));
    memset(dst->pvp._masks_buf, 0xa5, sizeof(dst->pvp._masks_buf));

    dst->state = src->state;
    puffer_state_refresh(dst);

    ASSERT_TRUE("snapshot smaller than full env",
        sizeof(PvpStateSnapshot) < sizeof(OsrsEnv));
    ASSERT_TRUE("restore keeps collision map pointer",
        dst->pvp.collision_map == dst_collision_map);
    ASSERT_TRUE("restore keeps p0 rollout obs pointer",
        dst->pvp.ocean_io.agent_obs == dst_agent_obs);
    ASSERT_TRUE("restore keeps p1 rollout obs pointer",
        dst->pvp.ocean_io.agent_obs_p1 == dst_agent_obs_p1);
    ASSERT_TRUE("restore keeps action mask pointer",
        dst->pvp.action_masks == dst_action_mask);
    ASSERT_INT_EQ("restore tick", dst->pvp.tick, src->pvp.tick);
    ASSERT_INT_EQ("restore rng", (int)dst->pvp.rng_state, (int)src->pvp.rng_state);
    ASSERT_INT_EQ("restore p0 hp",
        dst->pvp.players[0].current_hitpoints,
        src->pvp.players[0].current_hitpoints);
    ASSERT_INT_EQ("restore p1 frozen",
        remaining_ticks(dst->pvp.players[1].frozen_ticks),
        remaining_ticks(src->pvp.players[1].frozen_ticks));
    ASSERT_INT_EQ("restore pending attack",
        dst->pvp.pending_actions[HEAD_ATTACK],
        src->pvp.pending_actions[HEAD_ATTACK]);
    ASSERT_INT_EQ("restore runtime opponent",
        (int)dst->pvp.pvp_runtime.opponent.type,
        (int)src->pvp.pvp_runtime.opponent.type);
    ASSERT_TRUE("restore p0 rollout obs",
        memcmp(dst->obs_ptr[0], src->obs_ptr[0], OBS_SIZE * sizeof(float)) == 0);
    ASSERT_TRUE("restore p1 rollout obs",
        memcmp(dst->obs_ptr[1], src->obs_ptr[1], OBS_SIZE * sizeof(float)) == 0);
    ASSERT_TRUE("restore p0 mask",
        memcmp(dst->action_mask_ptr[0], src->action_mask_ptr[0], ACTION_MASK_SIZE) == 0);
    ASSERT_TRUE("restore p1 mask",
        memcmp(dst->action_mask_ptr[1], src->action_mask_ptr[1], ACTION_MASK_SIZE) == 0);

    static_vec_close(vec);
}

static void test_native_init_loads_collision_map_and_walkable_spawns(void) {
    printf("--- PvP native init loads collision map and walkable spawns ---\n");

    Env env;
    memset(&env, 0, sizeof(env));

    Dict* kwargs = pvp_kwargs();
    my_init(&env, kwargs);

    const CollisionMap* cmap = (const CollisionMap*)env.pvp.collision_map;
    ASSERT_TRUE("native init collision map", cmap != NULL);
    ASSERT_INT_EQ("native init default random starts",
        (int)env.pvp.pvp_runtime.start_mode, PVP_START_RANDOMIZED);
    ASSERT_TRUE("agent spawn walkable",
        collision_tile_walkable(cmap, 0, env.pvp.players[0].x, env.pvp.players[0].y));
    ASSERT_TRUE("opponent spawn walkable",
        collision_tile_walkable(cmap, 0, env.pvp.players[1].x, env.pvp.players[1].y));
    ASSERT_TRUE("spawns are distinct",
        env.pvp.players[0].x != env.pvp.players[1].x ||
        env.pvp.players[0].y != env.pvp.players[1].y);

    free(kwargs->items);
    free(kwargs);
    c_close(&env);
}

static void test_fixed_spawn_override_uses_walkable_pair(void) {
    printf("--- PvP fixed spawn override uses walkable pair ---\n");

    Env env;
    memset(&env, 0, sizeof(env));

    Dict* kwargs = pvp_kwargs();
    dict_set(kwargs, "fixed_spawns", 1);
    my_init(&env, kwargs);

    const CollisionMap* cmap = (const CollisionMap*)env.pvp.collision_map;
    ASSERT_INT_EQ("fixed spawn override mode",
        (int)env.pvp.pvp_runtime.start_mode, PVP_START_FIXED_PAIR);
    ASSERT_INT_EQ("agent fixed spawn x", env.pvp.players[0].x, 3041);
    ASSERT_INT_EQ("agent fixed spawn y", env.pvp.players[0].y, 3530);
    ASSERT_INT_EQ("opponent fixed spawn x", env.pvp.players[1].x, 3046);
    ASSERT_INT_EQ("opponent fixed spawn y", env.pvp.players[1].y, 3531);
    ASSERT_TRUE("agent fixed spawn walkable",
        collision_tile_walkable(cmap, 0, env.pvp.players[0].x, env.pvp.players[0].y));
    ASSERT_TRUE("opponent fixed spawn walkable",
        collision_tile_walkable(cmap, 0, env.pvp.players[1].x, env.pvp.players[1].y));

    free(kwargs->items);
    free(kwargs);
    c_close(&env);
}

static void test_seeded_resets_replay_varied_setup_sequence(void) {
    printf("--- PvP seeded resets replay varied setup sequence ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv a;
    OsrsEnv b;
    pvp_setup_seeded_reset_env(&a, cmap, 73);
    pvp_setup_seeded_reset_env(&b, cmap, 73);

    int varied = 0;
    PvpResetSignature first = {0};
    for (int i = 0; i < 8; i++) {
        pvp_reset(&a);
        pvp_reset(&b);
        PvpResetSignature sa = pvp_reset_signature(&a);
        PvpResetSignature sb = pvp_reset_signature(&b);
        ASSERT_TRUE("same seed replays reset signature",
            pvp_reset_signature_equal(sa, sb));
        if (i == 0) {
            first = sa;
        } else if (!pvp_reset_signature_equal(sa, first)) {
            varied = 1;
        }
    }

    ASSERT_TRUE("seeded reset sequence varies fight setup", varied);
    collision_map_free(cmap);
}

static void test_seeded_pid_shuffles_during_episode(void) {
    printf("--- PvP seeded PID shuffles during episode ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    pvp_reset(&env);

    int initial_pid = env.pid_holder;
    env.pid_shuffle_countdown = 1;
    pvp_step(&env);

    ASSERT_INT_EQ("seeded PID shuffle flips holder", env.pid_holder, 1 - initial_pid);
    ASSERT_TRUE("seeded PID shuffle resets countdown", env.pid_shuffle_countdown >= 100);

    collision_map_free(cmap);
}

static void test_movement_masks_respect_blocked_tiles(void) {
    printf("--- PvP movement masks respect blocked tiles ---\n");

    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    CollisionMap* cmap = collision_map_create();
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    Player* target = &env.players[1];
    pvp_set_player_spawn(agent, 3041, 3530);
    pvp_set_player_spawn(target, 3042, 3530);
    agent->last_obs_target_x = target->x;
    agent->last_obs_target_y = target->y;
    target->frozen_ticks = 8;
    collision_mark_blocked(cmap, 0, target->x, target->y);

    compute_action_masks(&env, 0);

    int move_offset = action_head_offset(HEAD_MOVE);
    ASSERT_INT_EQ("blocked HEAD_MOVE east mask",
        env.action_masks[move_offset + 7], 0);

    collision_map_free(cmap);
}

static void test_slotclick_schema_and_inventory_mask(void) {
    printf("--- PvP slot-click schema and inventory mask ---\n");

    ASSERT_INT_EQ("PvP action schema",
        PVP_ACTION_SCHEMA, PVP_ACTION_SCHEMA_SLOTCLICK_EXPLICIT_ANCIENTS_V11);
    ASSERT_INT_EQ("PvP obs schema",
        PVP_OBS_SCHEMA, PVP_OBS_SCHEMA_EXPLICIT_ANCIENTS_V11);
    ASSERT_INT_EQ("PvP action head count", NUM_ACTION_HEADS, 13);
    ASSERT_INT_EQ("equip click dim", ACTION_HEAD_DIMS[HEAD_EQUIP_0], OSRS_INVENTORY_SIZE + 1);
    ASSERT_INT_EQ("attack dim", ACTION_HEAD_DIMS[HEAD_ATTACK], ATTACK_DIM);
    ASSERT_INT_EQ("special dim", ACTION_HEAD_DIMS[HEAD_SPECIAL], SPECIAL_DIM);
    ASSERT_INT_EQ("action mask size", ACTION_MASK_SIZE,
        PVP_EQUIP_CLICKS_PER_TICK * EQUIP_CLICK_DIM +
        ATTACK_DIM + SPECIAL_DIM + OVERHEAD_DIM + FOOD_DIM + POTION_DIM +
        KARAMBWAN_DIM + VENG_DIM + OFFENSIVE_DIM + MOVE_DIM);
    ASSERT_INT_EQ("item feature dim", OSRS_ITEM_FEATURE_DIM, 56);

    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    CollisionMap* cmap = collision_map_create();
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    int slot = osrs_player_inventory_find(agent, ITEM_RUNE_CROSSBOW);
    ASSERT_TRUE("crossbow inventory slot", slot >= 0);

    compute_action_masks(&env, 0);
    generate_slot_observations(&env, 0);

    int equip_offset = action_head_offset(HEAD_EQUIP_0);
    int special_offset = action_head_offset(HEAD_SPECIAL);
    ASSERT_INT_EQ("equip noop valid", env.action_masks[equip_offset], 1);
    ASSERT_INT_EQ("crossbow slot-click valid",
        env.action_masks[equip_offset + slot + 1], 1);
    ASSERT_INT_EQ("special noop valid",
        env.action_masks[special_offset + SPECIAL_NOOP], 1);

    for (int slot_idx = 0; slot_idx < OSRS_INVENTORY_SIZE; slot_idx++) {
        float* row = env.observations + PVP_INVENTORY_OBS_OFFSET +
            slot_idx * OSRS_ITEM_FEATURE_DIM;
        int row_can_equip = row[31] > 0.5f ? 1 : 0;
        for (int head = 0; head < PVP_EQUIP_CLICKS_PER_TICK; head++) {
            int head_offset = action_head_offset(HEAD_EQUIP_0 + head);
            ASSERT_INT_EQ("inventory can_equip mirrors equip head mask",
                row_can_equip, env.action_masks[head_offset + slot_idx + 1]);
        }
    }

    collision_map_free(cmap);
}

static void test_two_handed_full_inventory_affordance_matches_masks(void) {
    printf("--- PvP two-handed full inventory affordance matches masks ---\n");

    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    CollisionMap* cmap = collision_map_create();
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    osrs_player_inventory_clear(agent);
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_WHIP);
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_SHIELD, ITEM_DRAGON_DEFENDER);
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        agent->inventory[slot] = ITEM_DRAGON_DAGGER;
    }
    agent->inventory[0] = ITEM_AGS;

    generate_slot_observations(&env, 0);
    compute_action_masks(&env, 0);

    float* row = env.observations + PVP_INVENTORY_OBS_OFFSET;
    ASSERT_FLOAT_NEAR("full inventory two-handed row can_equip",
        row[31], 0.0f, 1e-6f);
    for (int head = 0; head < PVP_EQUIP_CLICKS_PER_TICK; head++) {
        int head_offset = action_head_offset(HEAD_EQUIP_0 + head);
        ASSERT_INT_EQ("full inventory two-handed equip mask",
            env.action_masks[head_offset + 1], 0);
    }

    agent->inventory[1] = ITEM_NONE;
    generate_slot_observations(&env, 0);
    compute_action_masks(&env, 0);

    ASSERT_FLOAT_NEAR("free slot two-handed row can_equip",
        row[31], 1.0f, 1e-6f);
    for (int head = 0; head < PVP_EQUIP_CLICKS_PER_TICK; head++) {
        int head_offset = action_head_offset(HEAD_EQUIP_0 + head);
        ASSERT_INT_EQ("free slot two-handed equip mask",
            env.action_masks[head_offset + 1], 1);
    }

    collision_map_free(cmap);
}

static void test_attack_mask_allows_post_equip_weapon_target_click(void) {
    printf("--- PvP attack mask allows post-equip weapon target click ---\n");

    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    CollisionMap* cmap = collision_map_create();
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    Player* target = &env.players[1];
    pvp_set_player_spawn(agent, 3041, 3530);
    pvp_set_player_spawn(target, 3043, 3530);
    agent->last_obs_target_x = target->x;
    agent->last_obs_target_y = target->y;
    agent->attack_timer = 0;
    agent->frozen_ticks = 8;
    osrs_player_inventory_clear(agent);
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_AHRIM_STAFF);

    compute_action_masks(&env, 0);

    int combat_offset = action_head_offset(HEAD_COMBAT);
    ASSERT_INT_EQ("frozen staff bash out of range masked",
        env.action_masks[combat_offset + ATTACK_ATK], 0);

    int crossbow_slot = osrs_player_inventory_add(agent, ITEM_RUNE_CROSSBOW);
    ASSERT_TRUE("crossbow added", crossbow_slot >= 0);

    compute_action_masks(&env, 0);

    ASSERT_INT_EQ("post-equip ranged target-click valid",
        env.action_masks[combat_offset + ATTACK_ATK], 1);

    collision_map_free(cmap);
}

static int pvp_test_eager_attack_head_reachable_for_player(
    const CollisionMap* cmap,
    Player* attacker,
    Player* target,
    int can_move_now
) {
    AttackStyle weapon_style = get_slot_weapon_attack_style(attacker);
    if (weapon_style == ATTACK_STYLE_NONE) return 0;

    AttackStyle actual_style = weapon_style == ATTACK_STYLE_MAGIC
        ? ATTACK_STYLE_MELEE
        : weapon_style;
    OsrsAttackReachQuery reach = pvp_attack_reach_query(
        cmap, attacker, target, actual_style);
    return osrs_attack_can_reach(&reach) || can_move_now;
}

static int pvp_test_eager_attack_head_reachable_after_equip(
    const CollisionMap* cmap,
    const Player* attacker,
    Player* target,
    int inventory_slot,
    int can_move_now,
    const PvpInventoryAffordances* affordances
) {
    if (!affordances->can_equip[inventory_slot]) return 0;
    if (!affordances->is_weapon[inventory_slot]) return 0;

    Player next = *attacker;
    if (!osrs_player_equip_from_inventory_slot(&next, inventory_slot)) {
        fprintf(stderr,
            "pvp_test_eager_attack_head_reachable_after_equip: slot %d precheck failed\n",
            inventory_slot);
        abort();
    }
    return pvp_test_eager_attack_head_reachable_for_player(
        cmap, &next, target, can_move_now);
}

static int pvp_test_eager_attack_head_reachable_after_any_weapon_equip(
    const CollisionMap* cmap,
    const Player* attacker,
    Player* target,
    int can_move_now,
    const PvpInventoryAffordances* affordances
) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        if (pvp_test_eager_attack_head_reachable_after_equip(
                cmap, attacker, target, slot, can_move_now, affordances)) {
            return 1;
        }
    }
    return 0;
}

static int pvp_test_special_arm_after_any_weapon_equip_reference(const Player* p) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        uint8_t item_idx = p->inventory[slot];
        if (!osrs_player_can_equip_from_inventory_slot(p, slot)) continue;
        if (osrs_item_gear_slot(item_idx) != GEAR_SLOT_WEAPON) continue;
        if (pvp_special_arm_available_for_weapon(item_idx, p->special_energy)) {
            return 1;
        }
    }
    return 0;
}

static void pvp_test_eager_attack_head_mask(
    OsrsEnv* env,
    int agent_idx,
    const PvpInventoryAffordances* affordances,
    unsigned char out[ATTACK_DIM]
) {
    Player* p = &env->players[agent_idx];
    Player* t = &env->players[1 - agent_idx];
    int attack_ready = remaining_ticks(p->attack_timer) == 0;
    int can_move_now = can_move(p);
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;
    int weapon_reachable = pvp_test_eager_attack_head_reachable_for_player(
        cmap, p, t, can_move_now) ||
        pvp_test_eager_attack_head_reachable_after_any_weapon_equip(
            cmap, p, t, can_move_now, affordances);
    OsrsAttackReachQuery magic_reach = pvp_attack_reach_query(
        cmap, p, t, ATTACK_STYLE_MAGIC);
    int magic_reachable = osrs_attack_can_reach(&magic_reach) || can_move_now;
    int gmaul_spec_ready = p->spec_armed &&
        p->equipped[GEAR_SLOT_WEAPON] == ITEM_GRANITE_MAUL &&
        is_granite_maul_attack_available(p);

    memset(out, 0, ATTACK_DIM);
    out[ATTACK_NONE] = 1;
    out[ATTACK_ATK] = (attack_ready || gmaul_spec_ready) && weapon_reachable;
    out[ATTACK_ICE_RUSH] =
        attack_ready && magic_reachable && pvp_spell_action_can_cast(p, ATTACK_ICE_RUSH);
    out[ATTACK_ICE_BURST] =
        attack_ready && magic_reachable && pvp_spell_action_can_cast(p, ATTACK_ICE_BURST);
    out[ATTACK_ICE_BLITZ] =
        attack_ready && magic_reachable && pvp_spell_action_can_cast(p, ATTACK_ICE_BLITZ);
    out[ATTACK_ICE_BARRAGE] =
        attack_ready && magic_reachable && pvp_spell_action_can_cast(p, ATTACK_ICE_BARRAGE);
    out[ATTACK_BLOOD_RUSH] =
        attack_ready && magic_reachable && pvp_spell_action_can_cast(p, ATTACK_BLOOD_RUSH);
    out[ATTACK_BLOOD_BURST] =
        attack_ready && magic_reachable && pvp_spell_action_can_cast(p, ATTACK_BLOOD_BURST);
    out[ATTACK_BLOOD_BLITZ] =
        attack_ready && magic_reachable && pvp_spell_action_can_cast(p, ATTACK_BLOOD_BLITZ);
    out[ATTACK_BLOOD_BARRAGE] =
        attack_ready && magic_reachable && pvp_spell_action_can_cast(p, ATTACK_BLOOD_BARRAGE);
}

typedef enum {
    PVP_ATTACK_MASK_ATTACK_TIMER_ACTIVE,
    PVP_ATTACK_MASK_CAN_MOVE_THROUGH_BLOCKED_LOS,
    PVP_ATTACK_MASK_FROZEN_BLOCKED_LOS,
    PVP_ATTACK_MASK_CURRENT_WEAPON,
    PVP_ATTACK_MASK_POST_EQUIP_WEAPON,
    PVP_ATTACK_MASK_GMAUL_SPEC,
    PVP_ATTACK_MASK_BARRAGE
} PvpAttackMaskParityCase;

static const char* pvp_attack_mask_parity_case_label(PvpAttackMaskParityCase test_case) {
    switch (test_case) {
        case PVP_ATTACK_MASK_ATTACK_TIMER_ACTIVE:
            return "attack timer active";
        case PVP_ATTACK_MASK_CAN_MOVE_THROUGH_BLOCKED_LOS:
            return "can move through blocked LOS";
        case PVP_ATTACK_MASK_FROZEN_BLOCKED_LOS:
            return "frozen blocked LOS";
        case PVP_ATTACK_MASK_CURRENT_WEAPON:
            return "current weapon";
        case PVP_ATTACK_MASK_POST_EQUIP_WEAPON:
            return "post-equip weapon";
        case PVP_ATTACK_MASK_GMAUL_SPEC:
            return "gmaul spec";
        case PVP_ATTACK_MASK_BARRAGE:
            return "barrage";
        default:
            fprintf(stderr, "invalid PvP attack mask parity case: %d\n", test_case);
            abort();
    }
}

static void pvp_setup_attack_mask_parity_case(
    OsrsEnv* env,
    CollisionMap* cmap,
    PvpAttackMaskParityCase test_case
) {
    memset(env, 0, sizeof(*env));
    pvp_init(env);
    env->collision_map = cmap;
    pvp_seed(env, 73);
    pvp_reset(env);

    Player* agent = &env->players[0];
    Player* target = &env->players[1];
    pvp_set_player_spawn(agent, 3041, 3530);
    pvp_set_player_spawn(target, 3043, 3530);
    agent->last_obs_target_x = target->x;
    agent->last_obs_target_y = target->y;
    target->last_obs_target_x = agent->x;
    target->last_obs_target_y = agent->y;
    agent->current_magic = 0;
    agent->attack_timer = 0;
    agent->spec_armed = 0;
    agent->special_energy = 100;
    osrs_player_inventory_clear(agent);
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_WHIP);

    switch (test_case) {
        case PVP_ATTACK_MASK_ATTACK_TIMER_ACTIVE:
            agent->attack_timer = 3;
            break;
        case PVP_ATTACK_MASK_CAN_MOVE_THROUGH_BLOCKED_LOS:
            osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_AHRIM_STAFF);
            agent->current_magic = 99;
            collision_mark_occupant(cmap, 0, 3042, 3530, 1, 1, 1);
            break;
        case PVP_ATTACK_MASK_FROZEN_BLOCKED_LOS:
            osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_AHRIM_STAFF);
            agent->current_magic = 99;
            agent->frozen_ticks = 8;
            collision_mark_occupant(cmap, 0, 3042, 3530, 1, 1, 1);
            break;
        case PVP_ATTACK_MASK_CURRENT_WEAPON:
            pvp_set_player_spawn(target, 3042, 3530);
            agent->frozen_ticks = 8;
            break;
        case PVP_ATTACK_MASK_POST_EQUIP_WEAPON:
            osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_AHRIM_STAFF);
            agent->frozen_ticks = 8;
            ASSERT_TRUE("post-equip crossbow added",
                osrs_player_inventory_add(agent, ITEM_RUNE_CROSSBOW) >= 0);
            break;
        case PVP_ATTACK_MASK_GMAUL_SPEC:
            pvp_set_player_spawn(target, 3042, 3530);
            osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_GRANITE_MAUL);
            agent->attack_timer = 3;
            agent->frozen_ticks = 8;
            agent->spec_armed = 1;
            break;
        case PVP_ATTACK_MASK_BARRAGE:
            osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_AHRIM_STAFF);
            agent->current_magic = 99;
            agent->frozen_ticks = 8;
            break;
        default:
            fprintf(stderr, "invalid PvP attack mask parity case: %d\n", test_case);
            abort();
    }
}

static void test_attack_mask_lazy_reach_matches_eager_reference(void) {
    printf("--- PvP attack mask lazy reach matches eager reference ---\n");

    PvpAttackMaskParityCase cases[] = {
        PVP_ATTACK_MASK_ATTACK_TIMER_ACTIVE,
        PVP_ATTACK_MASK_CAN_MOVE_THROUGH_BLOCKED_LOS,
        PVP_ATTACK_MASK_FROZEN_BLOCKED_LOS,
        PVP_ATTACK_MASK_CURRENT_WEAPON,
        PVP_ATTACK_MASK_POST_EQUIP_WEAPON,
        PVP_ATTACK_MASK_GMAUL_SPEC,
        PVP_ATTACK_MASK_BARRAGE,
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        CollisionMap* cmap = collision_map_create();
        OsrsEnv env;
        pvp_setup_attack_mask_parity_case(&env, cmap, cases[i]);

        PvpInventoryAffordances affordances;
        pvp_collect_inventory_affordances(&env.players[0], &affordances);
        unsigned char expected[ATTACK_DIM];
        pvp_test_eager_attack_head_mask(&env, 0, &affordances, expected);

        compute_action_masks_with_inventory_affordances(&env, 0, &affordances, NULL);

        int attack_offset = action_head_offset(HEAD_ATTACK);
        for (int action = 0; action < ATTACK_DIM; action++) {
            ASSERT_INT_EQ(pvp_attack_mask_parity_case_label(cases[i]),
                env.action_masks[attack_offset + action], expected[action]);
        }

        collision_map_free(cmap);
    }
}

static void test_attack_reach_short_circuits_mobile_without_collision_map(void) {
    printf("--- PvP attack reach short-circuits mobile without collision map ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_attack_mask_parity_case(
        &env, cmap, PVP_ATTACK_MASK_CAN_MOVE_THROUGH_BLOCKED_LOS);

    Player* agent = &env.players[0];
    Player* target = &env.players[1];
    PvpInventoryAffordances affordances;
    pvp_collect_inventory_affordances(agent, &affordances);

    ASSERT_INT_EQ("mobile current weapon short-circuit",
        pvp_attack_head_reachable_for_player(NULL, agent, target, 1), 1);
    ASSERT_INT_EQ("mobile magic short-circuit",
        pvp_magic_attack_reachable_for_player(NULL, agent, target, 1), 1);

    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_NONE);
    osrs_player_inventory_clear(agent);
    ASSERT_TRUE("mobile post-equip crossbow added",
        osrs_player_inventory_add(agent, ITEM_RUNE_CROSSBOW) >= 0);
    pvp_collect_inventory_affordances(agent, &affordances);
    ASSERT_INT_EQ("mobile post-equip weapon short-circuit",
        pvp_attack_head_reachable_after_any_weapon_equip(
            NULL, agent, target, 1, &affordances), 1);

    collision_map_free(cmap);
}

static void test_special_mask_allows_post_equip_weapon_spec_arm(void) {
    printf("--- PvP special mask allows post-equip weapon spec arm ---\n");

    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    CollisionMap* cmap = collision_map_create();
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    osrs_player_inventory_clear(agent);
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_AHRIM_STAFF);
    agent->special_energy = 100;
    agent->spec_armed = 0;

    compute_action_masks(&env, 0);

    int special_offset = action_head_offset(HEAD_SPECIAL);
    ASSERT_INT_EQ("staff has no special arm",
        env.action_masks[special_offset + SPECIAL_ARM], 0);

    int dagger_slot = osrs_player_inventory_add(agent, ITEM_DRAGON_DAGGER);
    ASSERT_TRUE("dagger added", dagger_slot >= 0);

    PvpInventoryAffordances affordances;
    memset(&affordances, 0xA5, sizeof(affordances));
    pvp_collect_inventory_affordances(agent, &affordances);
    ASSERT_INT_EQ("cached post-equip special arm",
        affordances.has_equippable_affordable_spec_weapon,
        pvp_test_special_arm_after_any_weapon_equip_reference(agent));

    compute_action_masks(&env, 0);

    ASSERT_INT_EQ("post-equip special arm valid",
        env.action_masks[special_offset + SPECIAL_ARM], 1);

    collision_map_free(cmap);
}

static void test_inventory_observation_item_facts(void) {
    printf("--- PvP inventory observation item facts ---\n");

    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    CollisionMap* cmap = collision_map_create();
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    osrs_player_inventory_clear(agent);
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_AHRIM_STAFF);
    agent->inventory[0] = ITEM_VOIDWAKER;
    agent->special_energy = 100;
    generate_slot_observations(&env, 0);

    float* row = env.observations + PVP_INVENTORY_OBS_OFFSET;
    ASSERT_FLOAT_NEAR("inventory present", row[0], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("inventory item id normalized",
        row[1], (float)ITEM_VOIDWAKER / (float)(NUM_ITEMS - 1), 1e-6f);
    ASSERT_FLOAT_NEAR("inventory melee style", row[5], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("inventory spec cost", row[10], 0.5f, 1e-6f);
    ASSERT_FLOAT_NEAR("inventory can equip", row[31], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("inventory physical slot",
        row[32], 1.0f / (float)OSRS_INVENTORY_SIZE, 1e-6f);
    ASSERT_FLOAT_NEAR("inventory weapon slot onehot",
        row[33 + GEAR_SLOT_WEAPON], 1.0f, 1e-6f);
    ASSERT_TRUE("inventory split slash attack", row[45] > 0.0f);
    ASSERT_TRUE("inventory post-equip melee attack delta", row[50] > 0.0f);

    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_VOIDWAKER);
    generate_slot_observations(&env, 0);
    ASSERT_FLOAT_NEAR("current weapon special affordance",
        env.observations[182], 1.0f, 1e-6f);

    collision_map_free(cmap);
}

static void test_mage_inventory_observation_item_facts(void) {
    printf("--- PvP mage inventory observation item facts ---\n");

    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    CollisionMap* cmap = collision_map_create();
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    osrs_player_inventory_clear(agent);
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_WHIP);
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_BODY, ITEM_BLACK_DHIDE_BODY);
    agent->inventory[0] = ITEM_KODAI_WAND;
    agent->inventory[1] = ITEM_ANCESTRAL_TOP;
    generate_slot_observations(&env, 0);

    float* staff_row = env.observations + PVP_INVENTORY_OBS_OFFSET;
    ASSERT_FLOAT_NEAR("mage staff present", staff_row[0], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("mage staff item id normalized",
        staff_row[1], (float)ITEM_KODAI_WAND / (float)(NUM_ITEMS - 1), 1e-6f);
    ASSERT_FLOAT_NEAR("mage staff style magic", staff_row[7], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("mage staff can equip", staff_row[31], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("mage staff weapon slot onehot",
        staff_row[33 + GEAR_SLOT_WEAPON], 1.0f, 1e-6f);
    ASSERT_TRUE("mage staff attack stat visible", staff_row[20] > 0.0f);
    ASSERT_TRUE("mage staff post-equip magic delta visible", staff_row[52] > 0.0f);

    float* top_row = staff_row + OSRS_ITEM_FEATURE_DIM;
    ASSERT_FLOAT_NEAR("mage top present", top_row[0], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("mage top item id normalized",
        top_row[1], (float)ITEM_ANCESTRAL_TOP / (float)(NUM_ITEMS - 1), 1e-6f);
    ASSERT_FLOAT_NEAR("mage top can equip", top_row[31], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("mage top body slot onehot",
        top_row[33 + GEAR_SLOT_BODY], 1.0f, 1e-6f);
    ASSERT_TRUE("mage top attack stat visible", top_row[20] > 0.0f);
    ASSERT_TRUE("mage top post-equip magic delta visible", top_row[52] > 0.0f);

    collision_map_free(cmap);
}

static void assert_float_rows_near(
    const char* label,
    const float* actual,
    const float* expected,
    int len,
    uint8_t item_idx
) {
    for (int i = 0; i < len; i++) {
        tests_run++;
        if (fabsf(actual[i] - expected[i]) <= 1e-6f) {
            tests_passed++;
        } else {
            tests_failed++;
            printf("  FAIL: %s item %u idx %d got %.6f expected %.6f\n",
                label, (unsigned)item_idx, i, actual[i], expected[i]);
        }
    }
}

static void test_item_observation_templates_match_direct_writers(void) {
    printf("--- PvP item observation templates match direct writers ---\n");

    GearBonuses current = {
        .stab_attack = 1,
        .slash_attack = 2,
        .crush_attack = 3,
        .magic_attack = 4,
        .ranged_attack = 5,
        .stab_defence = 6,
        .slash_defence = 7,
        .crush_defence = 8,
        .magic_defence = 9,
        .ranged_defence = 10,
        .melee_strength = 11,
        .ranged_strength = 12,
        .magic_strength = 13,
        .attack_speed = 4,
        .attack_range = 5,
    };
    GearBonuses post = {
        .stab_attack = 21,
        .slash_attack = 23,
        .crush_attack = 25,
        .magic_attack = 27,
        .ranged_attack = 29,
        .stab_defence = 31,
        .slash_defence = 33,
        .crush_defence = 35,
        .magic_defence = 37,
        .ranged_defence = 39,
        .melee_strength = 41,
        .ranged_strength = 43,
        .magic_strength = 45,
        .attack_speed = 6,
        .attack_range = 7,
    };

    for (int item = 0; item <= NUM_ITEMS; item++) {
        uint8_t item_idx = item < NUM_ITEMS ? (uint8_t)item : ITEM_NONE;
        float direct[OSRS_ITEM_FEATURE_DIM];
        float cached[OSRS_ITEM_FEATURE_DIM];
        float post_equip_deltas[6];
        pvp_write_post_equip_bonus_deltas(&current, &post, post_equip_deltas);
        pvp_write_item_policy_features(
            item_idx, 7, 1, &current, &post, direct);
        pvp_write_item_policy_features_cached(
            item_idx, 7, 1, post_equip_deltas, cached);
        assert_float_rows_near(
            "item policy template", cached, direct, OSRS_ITEM_FEATURE_DIM, item_idx);

        float direct_self[OSRS_ITEM_FEATURE_DIM];
        float cached_self[PVP_EQUIPPED_SELF_FEATURE_DIM];
        pvp_write_item_policy_features(
            item_idx, -1, 0, NULL, NULL, direct_self);
        pvp_write_equipped_self_item_features_cached(item_idx, cached_self);
        assert_float_rows_near(
            "equipped self template",
            cached_self,
            direct_self,
            PVP_EQUIPPED_SELF_FEATURE_DIM,
            item_idx);

        float direct_target[NUM_ITEM_STATS];
        float cached_target[PVP_EQUIPPED_TARGET_FEATURE_DIM];
        get_item_stats_normalized(item_idx, direct_target);
        pvp_write_target_item_stats_cached(item_idx, cached_target);
        assert_float_rows_near(
            "target item stats template",
            cached_target,
            direct_target,
            PVP_EQUIPPED_TARGET_FEATURE_DIM,
            item_idx);
    }
}

static void setup_affordance_projection_player(Player* player, int state) {
    memset(player, 0, sizeof(*player));
    memset(player->equipped, ITEM_NONE, sizeof(player->equipped));
    memset(player->inventory, ITEM_NONE, sizeof(player->inventory));

    if (state == 0) {
        player->equipped[GEAR_SLOT_WEAPON] = ITEM_WHIP;
        player->equipped[GEAR_SLOT_SHIELD] = ITEM_DRAGON_DEFENDER;
        player->equipped[GEAR_SLOT_BODY] = ITEM_AHRIMS_ROBETOP;
        player->equipped[GEAR_SLOT_LEGS] = ITEM_AHRIMS_ROBESKIRT;
        player->equipped[GEAR_SLOT_RING] = ITEM_BERSERKER_RING;
    } else if (state == 1) {
        player->equipped[GEAR_SLOT_WEAPON] = ITEM_AGS;
        player->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        player->equipped[GEAR_SLOT_BODY] = ITEM_KARILS_TOP;
        player->equipped[GEAR_SLOT_LEGS] = ITEM_BLACK_DHIDE_CHAPS;
        player->equipped[GEAR_SLOT_RING] = ITEM_LIGHTBEARER;
    } else if (state == 2) {
        player->equipped[GEAR_SLOT_WEAPON] = ITEM_NONE;
        player->equipped[GEAR_SLOT_SHIELD] = ITEM_DRAGON_DEFENDER;
        player->equipped[GEAR_SLOT_BODY] = ITEM_MYSTIC_TOP;
        player->equipped[GEAR_SLOT_LEGS] = ITEM_MYSTIC_BOTTOM;
        player->equipped[GEAR_SLOT_RING] = ITEM_SEERS_RING_I;
    } else {
        player->equipped[GEAR_SLOT_WEAPON] = ITEM_RUNE_CROSSBOW;
        player->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        player->equipped[GEAR_SLOT_BODY] = ITEM_BANDOS_CHESTPLATE;
        player->equipped[GEAR_SLOT_LEGS] = ITEM_DHAROKS_PLATELEGS;
        player->equipped[GEAR_SLOT_RING] = ITEM_RING_OF_RECOIL;
    }

    osrs_refresh_player_equipment(player);
}

static void fill_affordance_projection_inventory(
    Player* player,
    uint8_t item_idx,
    int full_inventory
) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        player->inventory[slot] = full_inventory ? ITEM_DRAGON_DAGGER : ITEM_NONE;
    }
    player->inventory[0] = item_idx;
}

static void test_inventory_affordance_projection_matches_copy_equip(void) {
    printf("--- PvP inventory affordance projection matches copy equip ---\n");

    Player player;
    for (int state = 0; state < 4; state++) {
        for (int full_inventory = 0; full_inventory <= 1; full_inventory++) {
            for (int item = 0; item < NUM_ITEMS; item++) {
                setup_affordance_projection_player(&player, state);
                fill_affordance_projection_inventory(
                    &player, (uint8_t)item, full_inventory);

                PvpInventoryAffordances affordances;
                memset(&affordances, 0xA5, sizeof(affordances));
                pvp_collect_inventory_affordances(&player, &affordances);

                int expected_gear_slot = osrs_item_gear_slot((uint8_t)item);
                int expected_can_equip =
                    osrs_player_can_equip_from_inventory_slot(&player, 0);
                int expected_is_weapon = expected_gear_slot == GEAR_SLOT_WEAPON;
                int expected_spec_arm =
                    pvp_test_special_arm_after_any_weapon_equip_reference(&player);

                assert_projection_int_eq("gear slot",
                    affordances.gear_slot[0], expected_gear_slot, state, (uint8_t)item);
                assert_projection_int_eq("weapon flag",
                    affordances.is_weapon[0], expected_is_weapon, state, (uint8_t)item);
                assert_projection_int_eq("can equip",
                    affordances.can_equip[0], expected_can_equip, state, (uint8_t)item);
                assert_projection_int_eq("special arm cache",
                    affordances.has_equippable_affordable_spec_weapon,
                    expected_spec_arm, state, (uint8_t)item);

                if (expected_can_equip) {
                    Player next = player;
                    ASSERT_TRUE("copy equip succeeds",
                        osrs_player_equip_from_inventory_slot(&next, 0));
                    EquipmentBonuses current_bonuses;
                    EquipmentBonuses next_bonuses;
                    osrs_sum_equipment_bonuses(player.equipped, &current_bonuses);
                    osrs_sum_equipment_bonuses(next.equipped, &next_bonuses);
                    GearBonuses current_gear =
                        osrs_gear_bonuses_from_equipment_bonuses(&current_bonuses);
                    GearBonuses next_gear =
                        osrs_gear_bonuses_from_equipment_bonuses(&next_bonuses);
                    float expected_deltas[6];
                    pvp_write_post_equip_bonus_deltas(
                        &current_gear, &next_gear, expected_deltas);
                    assert_projection_int_eq("has post deltas",
                        affordances.has_post_equip_deltas[0], 1, state, (uint8_t)item);
                    assert_float_rows_near("post equip deltas",
                        affordances.post_equip_deltas[0],
                        expected_deltas,
                        6,
                        (uint8_t)item);
                } else {
                    assert_projection_int_eq("no post deltas",
                        affordances.has_post_equip_deltas[0], 0, state, (uint8_t)item);
                }
            }
        }
    }

    setup_affordance_projection_player(&player, 2);
    fill_affordance_projection_inventory(&player, ITEM_AGS, 1);
    PvpInventoryAffordances affordances;
    memset(&affordances, 0xA5, sizeof(affordances));
    pvp_collect_inventory_affordances(&player, &affordances);
    assert_projection_int_eq("empty weapon full inventory two-handed can equip",
        affordances.can_equip[0], 1, 2, ITEM_AGS);

    setup_affordance_projection_player(&player, 0);
    fill_affordance_projection_inventory(&player, ITEM_AGS, 1);
    memset(&affordances, 0xA5, sizeof(affordances));
    pvp_collect_inventory_affordances(&player, &affordances);
    assert_projection_int_eq("occupied weapon full inventory two-handed cannot equip",
        affordances.can_equip[0], 0, 0, ITEM_AGS);
}

static void test_pvp_log_emits_command_diagnostics(void) {
    printf("--- PvP log emits command diagnostics ---\n");

    Log log;
    memset(&log, 0, sizeof(log));
    log.wins = 1.0f;
    log.damage_dealt = 40.0f;
    log.damage_received = 10.0f;
    log.performance_score = 0.75f;
    log.attacks_landed = 4.0f;
    log.expected_damage_prevented = 9.5f;
    log.ko_chance_count = 3.0f;
    log.ko_chance_prob = 0.55f;
    log.equip_click_attempts = 5.0f;
    log.equip_click_noop_rate = 0.2f;
    log.special_arm_attempts = 2.0f;
    log.special_arm_noop_rate = 0.5f;
    log.target_click_attempts = 6.0f;
    log.target_click_no_fire_rate = 0.25f;
    log.spell_attack_attempts = 3.0f;
    log.spell_attack_no_fire_rate = 0.33333334f;
    log.selected_melee_attack_rate = 0.15f;
    log.selected_ranged_attack_rate = 0.25f;
    log.selected_magic_attack_rate = 0.60f;
    log.target_click_chase_ticks = 7.0f;
    log.explicit_move_ticks = 2.0f;
    log.target_click_pre_move_dist = 3.0f;
    log.target_click_post_move_dist = 1.0f;
    log.target_click_success_pre_move_dist = 2.0f;
    log.target_click_success_post_move_dist = 1.0f;
    log.spell_attack_pre_move_dist = 6.0f;
    log.spell_attack_post_move_dist = 5.0f;
    log.spell_attack_success_pre_move_dist = 7.0f;
    log.spell_attack_success_post_move_dist = 6.0f;
    log.weapon_attack_rate = 0.6f;
    log.melee_attack_rate = 0.2f;
    log.ranged_attack_rate = 0.3f;
    log.magic_attack_rate = 0.5f;
    log.attack_after_equip_rate = 0.4f;
    log.spec_after_equip_rate = 0.1f;

    Dict* out = create_dict(128);
    my_log(&log, out);

    ASSERT_FLOAT_NEAR("log obs schema",
        dict_get(out, "obs_schema_id")->value, (float)PVP_OBS_SCHEMA, 1e-6f);
    ASSERT_FLOAT_NEAR("log damage per hit",
        dict_get(out, "damage_per_hit")->value, 10.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("log equip noop",
        dict_get(out, "equip_click_noop_rate")->value, 0.2f, 1e-6f);
    ASSERT_FLOAT_NEAR("log target click no fire",
        dict_get(out, "target_click_no_fire_rate")->value, 0.25f, 1e-6f);
    ASSERT_FLOAT_NEAR("log expected damage prevented",
        dict_get(out, "expected_damage_prevented")->value, 9.5f, 1e-6f);
    ASSERT_FLOAT_NEAR("log KO chance count",
        dict_get(out, "ko_chance_count")->value, 3.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("log KO chance prob",
        dict_get(out, "ko_chance_prob")->value, 0.55f, 1e-6f);
    ASSERT_FLOAT_NEAR("log weapon attack rate",
        dict_get(out, "weapon_attack_rate")->value, 0.6f, 1e-6f);
    ASSERT_FLOAT_NEAR("log selected magic attack rate",
        dict_get(out, "selected_magic_attack_rate")->value, 0.60f, 1e-6f);
    ASSERT_FLOAT_NEAR("log target click chase ticks",
        dict_get(out, "target_click_chase_ticks")->value, 7.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("log explicit move ticks",
        dict_get(out, "explicit_move_ticks")->value, 2.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("log target click pre move dist",
        dict_get(out, "target_click_pre_move_dist")->value, 3.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("log spell success post move dist",
        dict_get(out, "spell_attack_success_post_move_dist")->value, 6.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("log spec after equip",
        dict_get(out, "spec_after_equip_rate")->value, 0.1f, 1e-6f);

    free(out->items);
    free(out);
}

static void test_terminal_log_accumulates_command_diagnostics(void) {
    printf("--- PvP terminal log accumulates command diagnostics ---\n");

    Env env;
    memset(&env, 0, sizeof(env));
    env.tag = 3;
    env.pvp.log.wins = 1.0f;
    env.pvp.log.episode_return = 1.0f;
    env.pvp.log.episode_length = 64.0f;
    env.pvp.log.expected_damage_prevented = 12.0f;
    env.pvp.log.equip_click_attempts = 8.0f;
    env.pvp.log.equip_click_noop_rate = 0.25f;
    env.pvp.log.special_arm_attempts = 3.0f;
    env.pvp.log.special_arm_noop_rate = 0.33333334f;
    env.pvp.log.target_click_attempts = 10.0f;
    env.pvp.log.target_click_no_fire_rate = 0.4f;
    env.pvp.log.spell_attack_attempts = 5.0f;
    env.pvp.log.spell_attack_no_fire_rate = 0.2f;
    env.pvp.log.selected_melee_attack_rate = 0.2f;
    env.pvp.log.selected_ranged_attack_rate = 0.3f;
    env.pvp.log.selected_magic_attack_rate = 0.5f;
    env.pvp.log.target_click_chase_ticks = 9.0f;
    env.pvp.log.explicit_move_ticks = 4.0f;
    env.pvp.log.target_click_pre_move_dist = 5.0f;
    env.pvp.log.target_click_post_move_dist = 2.0f;
    env.pvp.log.target_click_success_pre_move_dist = 4.0f;
    env.pvp.log.target_click_success_post_move_dist = 1.0f;
    env.pvp.log.spell_attack_pre_move_dist = 6.0f;
    env.pvp.log.spell_attack_post_move_dist = 5.0f;
    env.pvp.log.spell_attack_success_pre_move_dist = 8.0f;
    env.pvp.log.spell_attack_success_post_move_dist = 7.0f;
    env.pvp.log.weapon_attack_rate = 0.7f;
    env.pvp.log.melee_attack_rate = 0.1f;
    env.pvp.log.ranged_attack_rate = 0.2f;
    env.pvp.log.magic_attack_rate = 0.4f;
    env.pvp.log.attack_after_equip_rate = 0.5f;
    env.pvp.log.spec_after_equip_rate = 0.125f;
    env.pvp.log.n = 1.0f;

    pvp_env_accumulate_terminal_log(&env);

    ASSERT_FLOAT_NEAR("accumulated equip attempts",
        env.log.equip_click_attempts, 8.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated expected damage prevented",
        env.log.expected_damage_prevented, 12.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated special attempts",
        env.log.special_arm_attempts, 3.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated target attempts",
        env.log.target_click_attempts, 10.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated spell attempts",
        env.log.spell_attack_attempts, 5.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated weapon rate",
        env.log.weapon_attack_rate, 0.7f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated selected ranged rate",
        env.log.selected_ranged_attack_rate, 0.3f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated target chase ticks",
        env.log.target_click_chase_ticks, 9.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated explicit move ticks",
        env.log.explicit_move_ticks, 4.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated target success pre move dist",
        env.log.target_click_success_pre_move_dist, 4.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated spell post move dist",
        env.log.spell_attack_post_move_dist, 5.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated melee rate",
        env.log.melee_attack_rate, 0.1f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated ranged rate",
        env.log.ranged_attack_rate, 0.2f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated magic rate",
        env.log.magic_attack_rate, 0.4f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated attack after equip",
        env.log.attack_after_equip_rate, 0.5f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated spec after equip",
        env.log.spec_after_equip_rate, 0.125f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated tagged bank score",
        env.log.hist_score_bank[2], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("accumulated n",
        env.log.n, 1.0f, 1e-6f);
    ASSERT_INT_EQ("boundary reached", env.boundary_reached, 1);
}

static void test_no_weapon_observation_has_zero_attack_profile(void) {
    printf("--- PvP no weapon observation has zero attack profile ---\n");

    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    CollisionMap* cmap = collision_map_create();
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_NONE);
    generate_slot_observations(&env, 0);

    ASSERT_FLOAT_NEAR("no weapon cycle", env.observations[123], 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("no weapon range", env.observations[124], 0.0f, 1e-6f);

    collision_map_free(cmap);
}

static void test_collision_los_blocks_impenetrable_tiles(void) {
    printf("--- PvP collision LOS blocks impenetrable tiles ---\n");

    CollisionMap* cmap = collision_map_create();
    collision_mark_occupant(cmap, 0, 3042, 3530, 1, 1, 1);

    ASSERT_INT_EQ("clear side has LOS",
        test_pvp_projectile_can_reach(cmap, 3040, 3530, 3041, 3530, 10),
        1);
    ASSERT_INT_EQ("impenetrable middle tile blocks LOS",
        test_pvp_projectile_can_reach(cmap, 3041, 3530, 3043, 3530, 10),
        0);

    collision_map_free(cmap);
}

static void setup_pvp_los_test_env(OsrsEnv* env, CollisionMap* cmap) {
    memset(env, 0, sizeof(*env));
    pvp_init(env);
    env->collision_map = cmap;
    pvp_seed(env, 73);
    pvp_reset(env);

    Player* agent = &env->players[0];
    Player* target = &env->players[1];
    pvp_set_player_spawn(agent, 3041, 3530);
    pvp_set_player_spawn(target, 3043, 3530);
    agent->last_obs_target_x = target->x;
    agent->last_obs_target_y = target->y;
    target->last_obs_target_x = agent->x;
    target->last_obs_target_y = agent->y;
    apply_loadout(agent, LOADOUT_MAGE);
    agent->current_magic = 99;
    agent->attack_timer = 0;
    collision_mark_occupant(cmap, 0, 3042, 3530, 1, 1, 1);
}

static void test_magic_attack_execution_respects_collision_los(void) {
    printf("--- PvP magic attack execution respects collision LOS ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    setup_pvp_los_test_env(&env, cmap);
    Player* agent = &env.players[0];
    Player* target = &env.players[1];
    agent->frozen_ticks = 8;

    int actions[NUM_ACTION_HEADS];
    memset(actions, 0, sizeof(actions));
    actions[HEAD_COMBAT] = ATTACK_ICE;

    ASSERT_INT_EQ("magic reach blocked by LOS",
        test_pvp_projectile_can_reach(cmap, agent->x, agent->y, target->x, target->y,
            get_attack_range(agent, ATTACK_STYLE_MAGIC)), 0);
    execute_attack_combat(&env, 0, actions);

    ASSERT_INT_EQ("no pending hit through LOS blocker", target->num_pending_hits, 0);
    ASSERT_INT_EQ("attack did not click through LOS blocker", agent->clicks_this_tick, 0);

    collision_map_free(cmap);
}

static void test_pvp_barrage_uses_shared_five_tick_cadence(void) {
    printf("--- PvP barrage uses shared five tick cadence ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    Player* target = &env.players[1];
    pvp_set_player_spawn(agent, 3041, 3530);
    pvp_set_player_spawn(target, 3042, 3530);
    apply_loadout(agent, LOADOUT_MAGE);
    agent->current_magic = 99;
    agent->attack_timer = 0;
    agent->attack_timer_uncapped = 0;
    agent->has_attack_timer = 0;

    generate_slot_observations(&env, 0);
    ASSERT_FLOAT_NEAR("mage attack speed observation",
        env.observations[123], 5.0f, 1e-6f);

    perform_attack(&env, 0, 1, ATTACK_STYLE_MAGIC, 0, 1, 1);

    ASSERT_INT_EQ("barrage post-action timer", agent->attack_timer, 4);
    ASSERT_INT_EQ("barrage uncapped post-action timer",
        agent->attack_timer_uncapped, 4);
    ASSERT_INT_EQ("barrage timer flag", agent->has_attack_timer, 1);

    collision_map_free(cmap);
}

static void test_pvp_explicit_spell_masks_by_current_magic(void) {
    printf("--- PvP explicit spell masks by current magic ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    Player* target = &env.players[1];
    pvp_set_player_spawn(agent, 3041, 3530);
    pvp_set_player_spawn(target, 3044, 3530);
    apply_loadout(agent, LOADOUT_MAGE);
    agent->attack_timer = 0;
    agent->is_lunar_spellbook = 0;

    int attack_offset = action_head_offset(HEAD_ATTACK);

    agent->current_magic = ICE_RUSH_LEVEL - 1;
    generate_slot_observations(&env, 0);
    compute_action_masks(&env, 0);
    ASSERT_INT_EQ("ice rush below level masked",
        env.action_masks[attack_offset + ATTACK_ICE_RUSH], 0);
    ASSERT_INT_EQ("blood rush available below ice rush",
        env.action_masks[attack_offset + ATTACK_BLOOD_RUSH], 1);
    ASSERT_FLOAT_NEAR("ice tier unavailable obs",
        env.observations[58], 0.0f, 1e-6f);

    agent->current_magic = ICE_RUSH_LEVEL;
    generate_slot_observations(&env, 0);
    compute_action_masks(&env, 0);
    ASSERT_INT_EQ("ice rush at level valid",
        env.action_masks[attack_offset + ATTACK_ICE_RUSH], 1);
    ASSERT_INT_EQ("ice burst below level masked",
        env.action_masks[attack_offset + ATTACK_ICE_BURST], 0);
    ASSERT_FLOAT_NEAR("ice rush tier obs",
        env.observations[58], 0.25f, 1e-6f);

    agent->current_magic = ICE_BURST_LEVEL;
    generate_slot_observations(&env, 0);
    compute_action_masks(&env, 0);
    ASSERT_INT_EQ("ice burst at level valid",
        env.action_masks[attack_offset + ATTACK_ICE_BURST], 1);
    ASSERT_INT_EQ("ice blitz below level masked",
        env.action_masks[attack_offset + ATTACK_ICE_BLITZ], 0);
    ASSERT_FLOAT_NEAR("ice burst tier obs",
        env.observations[58], 0.50f, 1e-6f);

    agent->current_magic = ICE_BLITZ_LEVEL;
    generate_slot_observations(&env, 0);
    compute_action_masks(&env, 0);
    ASSERT_INT_EQ("ice blitz at level valid",
        env.action_masks[attack_offset + ATTACK_ICE_BLITZ], 1);
    ASSERT_INT_EQ("ice barrage below level masked",
        env.action_masks[attack_offset + ATTACK_ICE_BARRAGE], 0);
    ASSERT_FLOAT_NEAR("ice blitz tier obs",
        env.observations[58], 0.75f, 1e-6f);

    agent->current_magic = ICE_BARRAGE_LEVEL;
    generate_slot_observations(&env, 0);
    compute_action_masks(&env, 0);
    ASSERT_INT_EQ("ice barrage at level valid",
        env.action_masks[attack_offset + ATTACK_ICE_BARRAGE], 1);
    ASSERT_FLOAT_NEAR("ice barrage tier obs",
        env.observations[58], 1.0f, 1e-6f);

    agent->current_magic = BLOOD_BLITZ_LEVEL;
    generate_slot_observations(&env, 0);
    compute_action_masks(&env, 0);
    ASSERT_INT_EQ("blood rush valid",
        env.action_masks[attack_offset + ATTACK_BLOOD_RUSH], 1);
    ASSERT_INT_EQ("blood burst valid",
        env.action_masks[attack_offset + ATTACK_BLOOD_BURST], 1);
    ASSERT_INT_EQ("blood blitz valid",
        env.action_masks[attack_offset + ATTACK_BLOOD_BLITZ], 1);
    ASSERT_INT_EQ("blood barrage below level masked",
        env.action_masks[attack_offset + ATTACK_BLOOD_BARRAGE], 0);
    ASSERT_FLOAT_NEAR("blood blitz tier obs",
        env.observations[59], 0.75f, 1e-6f);

    collision_map_free(cmap);
}

static void test_pvp_explicit_spell_profiles(void) {
    printf("--- PvP explicit spell profiles ---\n");

    PvpAncientSpellProfile ice_rush =
        pvp_spell_profile_for_action(ATTACK_ICE_RUSH);
    ASSERT_INT_EQ("ice rush required level", ice_rush.required_magic, ICE_RUSH_LEVEL);
    ASSERT_INT_EQ("ice rush max hit", ice_rush.max_hit, 16);
    ASSERT_INT_EQ("ice rush freeze ticks", ice_rush.freeze_ticks, 8);
    ASSERT_INT_EQ("ice rush visual", ice_rush.visual_spell,
        OSRS_COMBAT_VISUAL_SPELL_ICE_RUSH);

    PvpAncientSpellProfile ice_burst =
        pvp_spell_profile_for_action(ATTACK_ICE_BURST);
    ASSERT_INT_EQ("ice burst max hit", ice_burst.max_hit, ICE_BURST_MAX_HIT);
    ASSERT_INT_EQ("ice burst freeze ticks", ice_burst.freeze_ticks, 16);

    PvpAncientSpellProfile ice_blitz =
        pvp_spell_profile_for_action(ATTACK_ICE_BLITZ);
    ASSERT_INT_EQ("ice blitz max hit", ice_blitz.max_hit, ICE_BLITZ_MAX_HIT);
    ASSERT_INT_EQ("ice blitz freeze ticks", ice_blitz.freeze_ticks, 24);

    PvpAncientSpellProfile ice_barrage =
        pvp_spell_profile_for_action(ATTACK_ICE_BARRAGE);
    ASSERT_INT_EQ("ice barrage max hit", ice_barrage.max_hit, ICE_BARRAGE_MAX_HIT);
    ASSERT_INT_EQ("ice barrage freeze ticks",
        ice_barrage.freeze_ticks, BARRAGE_FREEZE_TICKS);

    PvpAncientSpellProfile blood_rush =
        pvp_spell_profile_for_action(ATTACK_BLOOD_RUSH);
    ASSERT_INT_EQ("blood rush max hit", blood_rush.max_hit, BLOOD_RUSH_MAX_HIT);
    ASSERT_INT_EQ("blood rush heal percent", blood_rush.heal_percent, 10);

    PvpAncientSpellProfile blood_burst =
        pvp_spell_profile_for_action(ATTACK_BLOOD_BURST);
    ASSERT_INT_EQ("blood burst max hit", blood_burst.max_hit, BLOOD_BURST_MAX_HIT);
    ASSERT_INT_EQ("blood burst heal percent", blood_burst.heal_percent, 15);

    PvpAncientSpellProfile blood_blitz =
        pvp_spell_profile_for_action(ATTACK_BLOOD_BLITZ);
    ASSERT_INT_EQ("blood blitz max hit", blood_blitz.max_hit, BLOOD_BLITZ_MAX_HIT);
    ASSERT_INT_EQ("blood blitz heal percent", blood_blitz.heal_percent, 20);

    PvpAncientSpellProfile blood_barrage =
        pvp_spell_profile_for_action(ATTACK_BLOOD_BARRAGE);
    ASSERT_INT_EQ("blood barrage max hit",
        blood_barrage.max_hit, BLOOD_BARRAGE_MAX_HIT);
    ASSERT_INT_EQ("blood barrage heal percent", blood_barrage.heal_percent, 25);
    ASSERT_INT_EQ("expected blood blitz base hit",
        pvp_magic_base_hit_for_expected_damage(NULL, blood_blitz.visual_spell),
        BLOOD_BLITZ_MAX_HIT);
}

static void test_pvp_drained_magic_obs_and_restore_mask(void) {
    printf("--- PvP drained magic obs and restore mask ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    agent->base_magic = 99;
    agent->current_magic = ICE_BURST_LEVEL;
    agent->restore_doses = 1;
    agent->potion_timer = 0;

    generate_slot_observations(&env, 0);
    compute_action_masks(&env, 0);

    int potion_offset = action_head_offset(HEAD_POTION);
    ASSERT_TRUE("drained magic obs below full",
        env.observations[38] < 1.0f);
    ASSERT_FLOAT_NEAR("best ice spell tier is burst",
        env.observations[58], 0.50f, 1e-6f);
    ASSERT_INT_EQ("restore valid when magic drained",
        env.action_masks[potion_offset + POTION_RESTORE], 1);

    collision_map_free(cmap);
}

static void test_attack_masks_respect_frozen_collision_los(void) {
    printf("--- PvP attack masks respect frozen collision LOS ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    setup_pvp_los_test_env(&env, cmap);
    Player* agent = &env.players[0];
    agent->frozen_ticks = 8;

    compute_action_masks(&env, 0);

    int combat_offset = action_head_offset(HEAD_COMBAT);
    ASSERT_INT_EQ("frozen ice rush through LOS blocker masked",
        env.action_masks[combat_offset + ATTACK_ICE_RUSH], 0);
    ASSERT_INT_EQ("frozen ice barrage through LOS blocker masked",
        env.action_masks[combat_offset + ATTACK_ICE_BARRAGE], 0);
    ASSERT_INT_EQ("frozen blood rush through LOS blocker masked",
        env.action_masks[combat_offset + ATTACK_BLOOD_RUSH], 0);
    ASSERT_INT_EQ("frozen blood barrage through LOS blocker masked",
        env.action_masks[combat_offset + ATTACK_BLOOD_BARRAGE], 0);

    agent->frozen_ticks = 0;
    compute_action_masks(&env, 0);
    ASSERT_INT_EQ("mobile ice barrage attack-click remains valid",
        env.action_masks[combat_offset + ATTACK_ICE_BARRAGE], 1);

    collision_map_free(cmap);
}

static void test_mobile_attack_click_chases_around_collision_los(void) {
    printf("--- PvP mobile attack click chases around collision LOS ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    setup_pvp_los_test_env(&env, cmap);
    Player* agent = &env.players[0];

    int actions[NUM_ACTION_HEADS];
    memset(actions, 0, sizeof(actions));
    actions[HEAD_COMBAT] = ATTACK_ICE;

    int chased = 0;
    int reached_los = 0;
    for (int i = 0; i < 8; i++) {
        PvpAttackMoveIntent intent = pvp_attack_move_intent(&env, 0, actions);
        OsrsPlayerStepResult step = pvp_step_player_movement(&env, 0, intent);
        chased |= step.chased_target;
        ASSERT_TRUE("chase avoids blocked LOS tile",
            !(agent->x == 3042 && agent->y == 3530));
        ASSERT_TRUE("chase remains walkable",
            collision_tile_walkable(cmap, 0, agent->x, agent->y));
        if (test_pvp_projectile_can_reach(cmap, agent->x, agent->y,
                env.players[1].x, env.players[1].y,
                get_attack_range(agent, ATTACK_STYLE_MAGIC))) {
            reached_los = 1;
            break;
        }
    }

    ASSERT_INT_EQ("attack click starts target interaction",
        osrs_interaction_active(&agent->interaction), 1);
    ASSERT_INT_EQ("attack click chased target",
        chased, 1);
    ASSERT_INT_EQ("chase reaches projectile LOS",
        reached_los, 1);

    collision_map_free(cmap);
}

static void test_target_click_staff_bash_chases_into_melee_range(void) {
    printf("--- PvP target click staff bash chases into melee range ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    Player* target = &env.players[1];
    pvp_set_player_spawn(agent, 3041, 3530);
    pvp_set_player_spawn(target, 3043, 3530);
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_AHRIM_STAFF);

    int actions[NUM_ACTION_HEADS];
    memset(actions, 0, sizeof(actions));
    actions[HEAD_ATTACK] = ATTACK_ATK;

    PvpAttackMoveIntent intent = pvp_attack_move_intent(&env, 0, actions);
    OsrsPlayerStepResult step = pvp_step_player_movement(&env, 0, intent);

    ASSERT_INT_EQ("staff target-click chased target", step.chased_target, 1);
    ASSERT_INT_EQ("staff target-click moved east", agent->x, 3042);
    ASSERT_INT_EQ("staff target-click stayed on row", agent->y, 3530);

    OsrsAttackReachQuery reach = pvp_attack_reach_query(
        cmap, agent, target, ATTACK_STYLE_MELEE);
    ASSERT_INT_EQ("staff target-click reached melee", osrs_attack_can_reach(&reach), 1);

    collision_map_free(cmap);
}

static void test_target_click_overrides_stale_walk_destination(void) {
    printf("--- PvP target click overrides stale walk destination ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    Player* target = &env.players[1];
    pvp_set_player_spawn(agent, 3041, 3530);
    pvp_set_player_spawn(target, 3043, 3530);
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_WHIP);
    env.pvp_runtime.walk_dest_x[0] = 3040;
    env.pvp_runtime.walk_dest_y[0] = 3530;

    int actions[NUM_ACTION_HEADS];
    memset(actions, 0, sizeof(actions));
    actions[HEAD_ATTACK] = ATTACK_ATK;

    PvpAttackMoveIntent intent = pvp_attack_move_intent(&env, 0, actions);
    OsrsPlayerStepResult step = pvp_step_player_movement(&env, 0, intent);

    ASSERT_INT_EQ("target-click chased instead of old walk", step.chased_target, 1);
    ASSERT_INT_EQ("target-click moved toward target", agent->x, 3042);
    ASSERT_INT_EQ("target-click cleared old walk x", env.pvp_runtime.walk_dest_x[0], -1);
    ASSERT_INT_EQ("target-click cleared old walk y", env.pvp_runtime.walk_dest_y[0], -1);

    collision_map_free(cmap);
}

static void test_shared_player_step_destination_clears_interaction(void) {
    printf("--- shared player step destination clears interaction ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    Player* target = &env.players[1];
    pvp_set_player_spawn(agent, 3041, 3530);
    pvp_set_player_spawn(target, 3043, 3530);
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_WHIP);
    osrs_interaction_set(&agent->interaction, 1);

    int dest_x = 3040;
    int dest_y = 3530;
    PvpAttackMoveIntent intent = {
        .env = &env,
        .agent_idx = 0,
        .target_slot = 1,
        .style = ATTACK_STYLE_MELEE,
        .range = 1,
    };
    OsrsEncounterArena arena = pvp_build_arena(&env);
    OsrsPlayerStepResult step = osrs_encounter_player_step(&(OsrsPlayerStepInput){
        .player = agent,
        .interaction = &agent->interaction,
        .target_lookup = pvp_lookup_attack_target,
        .target_ctx = &intent,
        .move_kind = OSRS_PLAYER_MOVE_DESTINATION,
        .target_move_policy = OSRS_PLAYER_TARGET_MOVE_EXPLICIT_FIRST,
        .dest_x = &dest_x,
        .dest_y = &dest_y,
        .arena = arena,
    });

    ASSERT_INT_EQ("shared step explicit moved", step.explicit_moved, 1);
    ASSERT_INT_EQ("shared step did not chase target", step.chased_target, 0);
    ASSERT_INT_EQ("shared step cleared interaction result", step.interaction_active, 0);
    ASSERT_INT_EQ("shared step cleared interaction state",
        osrs_interaction_active(&agent->interaction), 0);
    ASSERT_INT_EQ("shared step moved toward destination", agent->x, 3040);

    collision_map_free(cmap);
}

static void test_pvp_local_pathfind_window_only_handles_near_destinations(void) {
    printf("--- PvP local pathfind window only handles near destinations ---\n");

    Player player;
    memset(&player, 0, sizeof(player));
    player.x = 3041;
    player.y = 3530;

    int base_x = 0;
    int base_y = 0;
    int w = 0;
    int h = 0;
    int ok = pvp_local_pathfind_window(
        &player, 3043, 3530, &base_x, &base_y, &w, &h);

    ASSERT_INT_EQ("near pathfind window enabled", ok, 1);
    ASSERT_TRUE("near pathfind window width bounded", w <= PATHFIND_ARENA_MAX);
    ASSERT_TRUE("near pathfind window height bounded", h <= PATHFIND_ARENA_MAX);
    ASSERT_TRUE("near pathfind window contains source",
        player.x >= base_x && player.x < base_x + w &&
        player.y >= base_y && player.y < base_y + h);
    ASSERT_TRUE("near pathfind window contains destination",
        3043 >= base_x && 3043 < base_x + w &&
        3530 >= base_y && 3530 < base_y + h);

    ok = pvp_local_pathfind_window(
        &player, 3100, 3530, &base_x, &base_y, &w, &h);
    ASSERT_INT_EQ("far pathfind window falls back to full BFS", ok, 0);
}

static void assert_pvp_local_path_matches_full(
    const CollisionMap* cmap,
    int src_x,
    int src_y,
    int dest_x,
    int dest_y
) {
    Player player;
    memset(&player, 0, sizeof(player));
    player.x = src_x;
    player.y = src_y;

    int base_x = 0;
    int base_y = 0;
    int w = 0;
    int h = 0;
    int ok = pvp_local_pathfind_window(
        &player, dest_x, dest_y, &base_x, &base_y, &w, &h);
    ASSERT_INT_EQ("representative local path window exists", ok, 1);
    if (!ok) return;

    PathResult full = encounter_pathfind(
        cmap, 0, 0, src_x, src_y, dest_x, dest_y, NULL, NULL);
    PathResult local = encounter_pathfind_arena(
        cmap, 0, 0, src_x, src_y, dest_x, dest_y, NULL, NULL,
        base_x, base_y, w, h);

    ASSERT_INT_EQ("local path found matches full", local.found, full.found);
    ASSERT_INT_EQ("local path dx matches full", local.next_dx, full.next_dx);
    ASSERT_INT_EQ("local path dy matches full", local.next_dy, full.next_dy);
    ASSERT_INT_EQ("local fallback x matches full", local.dest_x, full.dest_x);
    ASSERT_INT_EQ("local fallback y matches full", local.dest_y, full.dest_y);
}

static void test_pvp_local_pathfind_matches_full_for_head_moves(void) {
    printf("--- PvP local pathfind matches full BFS for head moves ---\n");

    Env env;
    memset(&env, 0, sizeof(env));

    Dict* kwargs = pvp_kwargs();
    my_init(&env, kwargs);

    const CollisionMap* cmap = (const CollisionMap*)env.pvp.collision_map;
    int starts[][2] = {
        {3041, 3530},
        {3046, 3531},
        {3048, 3537},
        {3055, 3540},
        {3070, 3542},
        {3095, 3550},
    };
    int start_count = (int)(sizeof(starts) / sizeof(starts[0]));

    for (int i = 0; i < start_count; i++) {
        int src_x = starts[i][0];
        int src_y = starts[i][1];
        if (!collision_tile_walkable(cmap, 0, src_x, src_y))
            continue;

        for (int action = 1; action < MOVE_DIM; action++) {
            int dest_x = src_x + ENCOUNTER_MOVE_TARGET_DX[action];
            int dest_y = src_y + ENCOUNTER_MOVE_TARGET_DY[action];
            if (!is_in_wilderness(dest_x, dest_y))
                continue;
            if (!collision_tile_walkable(cmap, 0, dest_x, dest_y))
                continue;
            assert_pvp_local_path_matches_full(
                cmap, src_x, src_y, dest_x, dest_y);
        }
    }

    free(kwargs->items);
    free(kwargs);
    c_close(&env);
}

static void test_persistent_staff_target_click_executes_melee(void) {
    printf("--- PvP persistent staff target click executes melee ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    env.collision_map = cmap;
    pvp_seed(&env, 73);
    pvp_reset(&env);

    Player* agent = &env.players[0];
    Player* target = &env.players[1];
    pvp_set_player_spawn(agent, 3041, 3530);
    pvp_set_player_spawn(target, 3042, 3530);
    osrs_player_set_equipment_slot(agent, GEAR_SLOT_WEAPON, ITEM_AHRIM_STAFF);
    agent->attack_timer = 0;
    osrs_interaction_set(&agent->interaction, 1);

    int actions[NUM_ACTION_HEADS];
    memset(actions, 0, sizeof(actions));

    execute_attack_combat(&env, 0, actions);

    ASSERT_INT_EQ("persistent staff target-click used melee",
        agent->attack_style_this_tick, ATTACK_STYLE_MELEE);
    ASSERT_INT_EQ("persistent staff target-click attacked",
        agent->just_attacked, 1);

    collision_map_free(cmap);
}

static void test_static_binding_exposes_separate_action_mask(void) {
    printf("--- PvP static binding exposes separate action mask ---\n");

    Env env;
    memset(&env, 0, sizeof(env));
    StaticVec vec;
    memset(&vec, 0, sizeof(vec));

    float observations[NUM_AGENTS * OBS_SIZE];
    float actions[NUM_AGENTS * NUM_ATNS];
    float rewards[NUM_AGENTS];
    float terminals[NUM_AGENTS];
    unsigned char action_mask[NUM_AGENTS * MY_ACTION_MASK];
    memset(observations, 0, sizeof(observations));
    memset(actions, 0, sizeof(actions));
    memset(rewards, 0, sizeof(rewards));
    memset(terminals, 0, sizeof(terminals));
    memset(action_mask, 0, sizeof(action_mask));

    static_obs_set(&vec.observations, observations, NUM_AGENTS);
    vec.actions = actions;
    vec.rewards = rewards;
    vec.terminals = terminals;
    vec.action_mask = action_mask;
    vec.total_agents = NUM_AGENTS;
    vec.action_mask_size = MY_ACTION_MASK;

    Dict* kwargs = pvp_kwargs();
    my_init(&env, kwargs);
    my_setup_perm(&vec, &env, 0);
    c_reset(&env);

    ASSERT_INT_EQ("obs size keeps embedded mask tail", OBS_SIZE, OCEAN_OBS_SIZE);
    ASSERT_INT_EQ("separate mask size", MY_ACTION_MASK, ACTION_MASK_SIZE);
    ASSERT_TRUE("slot 0 action mask pointer", env.action_mask_ptr[0] == action_mask);
    ASSERT_TRUE("slot 1 action mask pointer",
        env.action_mask_ptr[1] == action_mask + ACTION_MASK_SIZE);

    for (int agent = 0; agent < NUM_AGENTS; agent++) {
        for (int i = 0; i < ACTION_MASK_SIZE; i++) {
            int mask_value = action_mask[agent * ACTION_MASK_SIZE + i];
            int obs_value = (int)observations[
                agent * OBS_SIZE + SLOT_NUM_OBSERVATIONS + i];
            ASSERT_INT_EQ("separate mask mirrors embedded mask", mask_value, obs_value);
        }
    }

    int offset = 0;
    for (int head = 0; head < NUM_ACTION_HEADS; head++) {
        int valid = 0;
        for (int action = 0; action < ACTION_HEAD_DIMS[head]; action++) {
            valid += action_mask[offset + action] != 0;
        }
        ASSERT_TRUE("each action head has a valid action", valid > 0);
        offset += ACTION_HEAD_DIMS[head];
    }
    ASSERT_INT_EQ("mask offset reaches ACTION_MASK_SIZE", offset, ACTION_MASK_SIZE);

    free(kwargs->items);
    free(kwargs);
    c_close(&env);
}

static void test_static_binding_one_agent_skips_unexported_p1_generation(void) {
    printf("--- PvP static binding one-agent skips unexported p1 generation ---\n");

    Env env;
    memset(&env, 0, sizeof(env));
    StaticVec vec;
    memset(&vec, 0, sizeof(vec));

    float observations[OBS_SIZE];
    float actions[NUM_ATNS];
    float rewards[1];
    float terminals[1];
    unsigned char action_mask[MY_ACTION_MASK];
    memset(observations, 0, sizeof(observations));
    memset(actions, 0, sizeof(actions));
    memset(rewards, 0, sizeof(rewards));
    memset(terminals, 0, sizeof(terminals));
    memset(action_mask, 0, sizeof(action_mask));

    static_obs_set(&vec.observations, observations, 1);
    vec.actions = actions;
    vec.rewards = rewards;
    vec.terminals = terminals;
    vec.action_mask = action_mask;
    vec.total_agents = 1;
    vec.action_mask_size = MY_ACTION_MASK;

    Dict* kwargs = pvp_kwargs();
    dict_set(kwargs, "use_rollout_opponent", 0);
    my_init(&env, kwargs);
    my_setup_perm(&vec, &env, 0);
    memset(env.pvp._masks_buf + ACTION_MASK_SIZE, 0xA5, ACTION_MASK_SIZE);
    c_reset(&env);

    ASSERT_TRUE("slot 0 obs pointer exported", env.obs_ptr[0] == observations);
    ASSERT_TRUE("slot 1 obs pointer absent", env.obs_ptr[1] == NULL);
    ASSERT_TRUE("slot 1 mask pointer absent", env.action_mask_ptr[1] == NULL);
    ASSERT_INT_EQ("generated agent mask narrows to p0", env.pvp.action_masks_agents, 0x1);

    int p0_valid = 0;
    for (int i = 0; i < ACTION_MASK_SIZE; i++) {
        p0_valid += action_mask[i] != 0;
        ASSERT_INT_EQ("internal p1 mask remains untouched",
            env.pvp._masks_buf[ACTION_MASK_SIZE + i], 0xA5);
    }
    ASSERT_TRUE("p0 exported mask has valid actions", p0_valid > 0);

    free(kwargs->items);
    free(kwargs);
    c_close(&env);
}

static void test_pvp_obs_norm_sparse_indices_cover_non_identity_divisors(void) {
    printf("--- PvP obs norm sparse indices cover non-identity divisors ---\n");

    ensure_obs_norm_initialized();

    ASSERT_INT_EQ("slot obs size", SLOT_NUM_OBSERVATIONS, 2251);
    ASSERT_INT_EQ("action mask size", ACTION_MASK_SIZE,
        PVP_EQUIP_CLICKS_PER_TICK * EQUIP_CLICK_DIM +
        ATTACK_DIM + SPECIAL_DIM + OVERHEAD_DIM + FOOD_DIM + POTION_DIM +
        KARAMBWAN_DIM + VENG_DIM + OFFENSIVE_DIM + MOVE_DIM);
    ASSERT_INT_EQ("ocean obs size", OCEAN_OBS_SIZE,
        SLOT_NUM_OBSERVATIONS + ACTION_MASK_SIZE);

    int expected_count = 0;
    for (int obs_idx = 0; obs_idx < SLOT_NUM_OBSERVATIONS; obs_idx++) {
        expected_count += OBS_NORM_DIVISORS[obs_idx] != 1.0f;
    }
    ASSERT_INT_EQ("sparse norm index count", OBS_NORM_NON_IDENTITY_COUNT, expected_count);

    for (int sparse_idx = 0; sparse_idx < OBS_NORM_NON_IDENTITY_COUNT; sparse_idx++) {
        int obs_idx = OBS_NORM_NON_IDENTITY_INDICES[sparse_idx];
        ASSERT_TRUE("sparse norm index in bounds",
            obs_idx >= 0 && obs_idx < SLOT_NUM_OBSERVATIONS);
        ASSERT_TRUE("sparse norm index divisor non-identity",
            OBS_NORM_DIVISORS[obs_idx] != 1.0f);
    }

    for (int obs_idx = 0; obs_idx < SLOT_NUM_OBSERVATIONS; obs_idx++) {
        if (OBS_NORM_DIVISORS[obs_idx] == 1.0f) {
            continue;
        }

        int found = 0;
        for (int sparse_idx = 0; sparse_idx < OBS_NORM_NON_IDENTITY_COUNT; sparse_idx++) {
            found |= OBS_NORM_NON_IDENTITY_INDICES[sparse_idx] == obs_idx;
        }
        ASSERT_TRUE("non-identity divisor appears in sparse index list", found);
    }
}

static void test_pvp_obs_sparse_normalization_matches_full_division(void) {
    printf("--- PvP obs sparse normalization matches full division ---\n");

    OsrsEnv env;
    memset(&env, 0, sizeof(env));
    pvp_init(&env);

    float p0_obs[OCEAN_OBS_SIZE];
    float p1_obs[OCEAN_OBS_SIZE];
    float expected_p0[OCEAN_OBS_SIZE];
    float expected_p1[OCEAN_OBS_SIZE];
    memset(p0_obs, 0, sizeof(p0_obs));
    memset(p1_obs, 0, sizeof(p1_obs));
    memset(expected_p0, 0, sizeof(expected_p0));
    memset(expected_p1, 0, sizeof(expected_p1));

    env.ocean_io.agent_obs = p0_obs;
    env.ocean_io.agent_obs_p1 = p1_obs;

    for (int obs_idx = 0; obs_idx < NUM_AGENTS * SLOT_NUM_OBSERVATIONS; obs_idx++) {
        env.observations[obs_idx] = (float)((obs_idx % 97) - 41) * 0.25f;
    }
    for (int mask_idx = 0; mask_idx < NUM_AGENTS * ACTION_MASK_SIZE; mask_idx++) {
        env.action_masks[mask_idx] = (unsigned char)((mask_idx % 3) == 0);
    }

    ensure_obs_norm_initialized();
    for (int obs_idx = 0; obs_idx < SLOT_NUM_OBSERVATIONS; obs_idx++) {
        expected_p0[obs_idx] =
            env.observations[obs_idx] / OBS_NORM_DIVISORS[obs_idx];
        expected_p1[obs_idx] =
            env.observations[SLOT_NUM_OBSERVATIONS + obs_idx] /
            OBS_NORM_DIVISORS[obs_idx];
    }
    for (int mask_idx = 0; mask_idx < ACTION_MASK_SIZE; mask_idx++) {
        expected_p0[SLOT_NUM_OBSERVATIONS + mask_idx] =
            (float)env.action_masks[mask_idx];
        expected_p1[SLOT_NUM_OBSERVATIONS + mask_idx] =
            (float)env.action_masks[ACTION_MASK_SIZE + mask_idx];
    }

    ocean_write_obs(&env);
    ocean_write_obs_p1(&env);

    for (int obs_idx = 0; obs_idx < OCEAN_OBS_SIZE; obs_idx++) {
        ASSERT_FLOAT_NEAR("p0 sparse normalized obs",
            p0_obs[obs_idx], expected_p0[obs_idx], 1e-6f);
        ASSERT_FLOAT_NEAR("p1 sparse normalized obs",
            p1_obs[obs_idx], expected_p1[obs_idx], 1e-6f);
    }
}

static void test_static_binding_sets_scripted_opponents(void) {
    printf("--- PvP static binding sets scripted opponents ---\n");

    Env envs[2];
    memset(envs, 0, sizeof(envs));
    StaticVec vec;
    memset(&vec, 0, sizeof(vec));
    vec.envs = envs;
    vec.size = 2;

    int scripted_opps[2] = { OPP_MASTER_NH, -1 };
    static_vec_set_env_scripted_opps(&vec, scripted_opps);

    ASSERT_INT_EQ("env 0 scripted opponent", envs[0].scripted_opp_type, OPP_MASTER_NH);
    ASSERT_INT_EQ("env 1 scripted opponent", envs[1].scripted_opp_type, -1);
}

static void test_binding_pfsp_stats_round_trip(void) {
    printf("--- PvP binding PFSP stats round trip ---\n");

    Env envs[2];
    memset(envs, 0, sizeof(envs));
    StaticVec vec;
    memset(&vec, 0, sizeof(vec));
    vec.envs = envs;
    vec.size = 2;

    envs[0].pvp.pvp_runtime.pfsp.pool_size = 1;
    envs[1].pvp.pvp_runtime.pfsp.pool_size = 1;

    int pool[2] = { OPP_MASTER_NH, OPP_SAVANT_NH };
    int cum_weights[2] = { 400, 1000 };
    binding_set_pfsp_weights(&vec, pool, cum_weights, 2);

    envs[0].pvp.pvp_runtime.pfsp.wins[0] = 1.0f;
    envs[0].pvp.pvp_runtime.pfsp.episodes[0] = 2.0f;
    envs[1].pvp.pvp_runtime.pfsp.wins[1] = 3.0f;
    envs[1].pvp.pvp_runtime.pfsp.episodes[1] = 4.0f;

    float wins[MAX_OPPONENT_POOL];
    float episodes[MAX_OPPONENT_POOL];
    memset(wins, 0, sizeof(wins));
    memset(episodes, 0, sizeof(episodes));
    int pool_size = 0;
    binding_get_pfsp_stats(&vec, wins, episodes, &pool_size);

    ASSERT_INT_EQ("pfsp pool size", pool_size, 2);
    ASSERT_FLOAT_NEAR("pfsp wins 0", wins[0], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("pfsp episodes 0", episodes[0], 2.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("pfsp wins 1", wins[1], 3.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("pfsp episodes 1", episodes[1], 4.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("pfsp stats reset", envs[0].pvp.pvp_runtime.pfsp.episodes[0], 0.0f, 1e-6f);

}

static void test_expected_damage_prayer_modifier(void) {
    printf("--- PvP expected damage prayer modifier ---\n");

    float off_prayer = pvp_expected_reduced_uniform_damage(
        0, 100, PRAYER_NONE, ATTACK_STYLE_RANGED);
    float on_prayer = pvp_expected_reduced_uniform_damage(
        0, 100, PRAYER_PROTECT_RANGED, ATTACK_STYLE_RANGED);

    ASSERT_FLOAT_NEAR("off-prayer uniform EV", off_prayer, 50.0f, 1e-6f);
    ASSERT_TRUE("on-prayer EV is lower", on_prayer < off_prayer);
    ASSERT_FLOAT_NEAR("on-prayer EV ratio", on_prayer / off_prayer, 0.59f, 0.02f);
}

static void pvp_setup_ranged_pressure_counterfactual(Player* attacker, Player* defender) {
    osrs_player_inventory_clear(attacker);
    osrs_player_set_equipment_slot(attacker, GEAR_SLOT_WEAPON, ITEM_RUNE_CROSSBOW);
    osrs_player_set_equipment_slot(attacker, GEAR_SLOT_AMMO, ITEM_DIAMOND_BOLTS_E);
    attacker->current_gear = GEAR_RANGED;

    osrs_player_inventory_clear(defender);
    osrs_player_set_equipment_slot(defender, GEAR_SLOT_BODY, ITEM_KARILS_TOP);
    osrs_player_set_equipment_slot(defender, GEAR_SLOT_LEGS, ITEM_DHAROKS_PLATELEGS);
    osrs_player_inventory_add(defender, ITEM_MYSTIC_TOP);
    osrs_player_inventory_add(defender, ITEM_MYSTIC_BOTTOM);
    defender->prayer = PRAYER_NONE;
}

static void test_expected_damage_prevention_uses_available_gear_and_prayer(void) {
    printf("--- PvP expected damage prevention uses available gear and prayer ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    pvp_reset(&env);
    Player* defender = &env.players[0];
    Player* attacker = &env.players[1];
    pvp_setup_ranged_pressure_counterfactual(attacker, defender);

    float no_prayer_ev = pvp_expected_attack_damage(
        attacker, defender, ATTACK_STYLE_RANGED, 0, ITEM_NONE, 0, PRAYER_NONE);
    float gear_saved = pvp_expected_damage_prevented_by_available_defence(
        attacker, defender, ATTACK_STYLE_RANGED, 0, ITEM_NONE, 0, no_prayer_ev);

    defender->prayer = PRAYER_PROTECT_RANGED;
    float protected_ev = pvp_expected_attack_damage(
        attacker, defender, ATTACK_STYLE_RANGED, 0, ITEM_NONE, 0, defender->prayer);
    float protected_saved = pvp_expected_damage_prevented_by_available_defence(
        attacker, defender, ATTACK_STYLE_RANGED, 0, ITEM_NONE, 0, protected_ev);

    ASSERT_TRUE("defensive gear saves expected damage", gear_saved > 0.0f);
    ASSERT_TRUE("correct prayer lowers actual EV", protected_ev < no_prayer_ev);
    ASSERT_TRUE("correct prayer increases prevented EV", protected_saved > gear_saved);

    defender->expected_damage_prevented = 0.0f;
    defender->expected_damage_prevented_tick = 0.0f;
    env.shaping.incoming_damage_avoidance_reward_coef = 1.0f;
    perform_attack(&env, 1, 0, ATTACK_STYLE_RANGED, 0, 0, 5);
    ASSERT_TRUE("attack registers prevented EV",
        defender->expected_damage_prevented_tick > 0.0f);
    ASSERT_FLOAT_NEAR("tick and total prevented EV match",
        defender->expected_damage_prevented_tick,
        defender->expected_damage_prevented,
        1e-6f);

    collision_map_free(cmap);
}

static void test_expected_damage_zero_accuracy(void) {
    printf("--- PvP expected damage zero accuracy ---\n");

    float ev = pvp_expected_spec_damage(
        ITEM_AGS, 0, 50, 100000, PRAYER_NONE, ATTACK_STYLE_MELEE);

    ASSERT_FLOAT_NEAR("zero accuracy EV", ev, 0.0f, 1e-6f);
}

static void test_expected_damage_standard_uniform(void) {
    printf("--- PvP expected damage standard uniform ---\n");

    float ev = pvp_expected_reduced_uniform_damage(
        0, 10, PRAYER_NONE, ATTACK_STYLE_MELEE);

    ASSERT_FLOAT_NEAR("uniform 0..10 EV", ev, 5.0f, 1e-6f);
}

static void test_ko_chance_uniform_formula_matches_tracker(void) {
    printf("--- PvP KO chance uniform formula matches tracker ---\n");

    float chance = pvp_ko_chance_uniform_damage(
        1.0f, 0, 50, 20, PRAYER_NONE, ATTACK_STYLE_MELEE);
    float protected_chance = pvp_ko_chance_uniform_damage(
        1.0f, 0, 50, 20, PRAYER_PROTECT_MELEE, ATTACK_STYLE_MELEE);

    ASSERT_FLOAT_NEAR("unprotected KO chance",
        chance, 31.0f / 51.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("protected KO chance",
        protected_chance, 17.0f / 51.0f, 1e-6f);
}

static void test_ko_chance_tracker_survival_aggregation(void) {
    printf("--- PvP KO chance tracker survival aggregation ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    pvp_setup_seeded_reset_env(&env, cmap, 73);
    pvp_reset(&env);

    register_ko_chance(&env, 0, 0.20f);
    register_ko_chance(&env, 0, 0.50f);

    ASSERT_FLOAT_NEAR("KO chance tick capless sum",
        env.players[0].ko_chance_prob_tick, 0.70f, 1e-6f);
    ASSERT_FLOAT_NEAR("KO chance count",
        env.players[0].ko_chance_count, 2.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("KO chance survival product",
        env.players[0].ko_chance_survival_prob, 0.40f, 1e-6f);

    collision_map_free(cmap);
}

static void test_ko_chance_representative_specs(void) {
    printf("--- PvP KO chance representative specs ---\n");

    float ags = pvp_ko_chance_spec_damage(
        ITEM_AGS, 100000, 40, 0, 20, PRAYER_NONE, ATTACK_STYLE_MELEE);
    float claws = pvp_ko_chance_spec_damage(
        ITEM_DRAGON_CLAWS, 100000, 40, 0, 20, PRAYER_NONE, ATTACK_STYLE_MELEE);
    float dark_bow = pvp_ko_chance_spec_damage(
        ITEM_DARK_BOW, 100000, 40, 0, 16, PRAYER_NONE, ATTACK_STYLE_RANGED);
    float voidwaker_off_prayer = pvp_ko_chance_spec_damage(
        ITEM_VOIDWAKER, 100000, 40, 0, 40, PRAYER_NONE, ATTACK_STYLE_MELEE);
    float voidwaker_on_prayer = pvp_ko_chance_spec_damage(
        ITEM_VOIDWAKER, 100000, 40, 0, 40, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MELEE);

    ASSERT_FLOAT_NEAR("AGS KO chance",
        ags, osrs_hit_chance(200000, 0) * 25.0f / 45.0f, 1e-6f);
    ASSERT_TRUE("claws KO chance positive", claws > 0.0f);
    ASSERT_FLOAT_NEAR("dark bow guaranteed min KO chance",
        dark_bow, 1.0f, 1e-4f);
    ASSERT_TRUE("voidwaker magic prayer reduces KO chance",
        voidwaker_on_prayer < voidwaker_off_prayer);
}

static void test_expected_damage_representative_specs(void) {
    printf("--- PvP expected damage representative specs ---\n");

    float ags = pvp_expected_spec_damage(
        ITEM_AGS, 100000, 40, 0, PRAYER_NONE, ATTACK_STYLE_MELEE);
    float ags_expected = osrs_hit_chance(200000, 0)
        * pvp_expected_reduced_uniform_damage(0, 55, PRAYER_NONE, ATTACK_STYLE_MELEE);
    float claws = pvp_expected_spec_damage(
        ITEM_DRAGON_CLAWS, 100000, 40, 0, PRAYER_NONE, ATTACK_STYLE_MELEE);
    float dark_bow_miss = pvp_expected_spec_damage(
        ITEM_DARK_BOW, 0, 40, 100000, PRAYER_NONE, ATTACK_STYLE_RANGED);
    float voidwaker_off_prayer = pvp_expected_spec_damage(
        ITEM_VOIDWAKER, 100000, 40, 0, PRAYER_NONE, ATTACK_STYLE_MELEE);
    float voidwaker_on_prayer = pvp_expected_spec_damage(
        ITEM_VOIDWAKER, 100000, 40, 0, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MELEE);

    ASSERT_FLOAT_NEAR("AGS EV formula", ags, ags_expected, 1e-5f);
    ASSERT_TRUE("claws EV positive", claws > 0.0f);
    ASSERT_FLOAT_NEAR("dark bow miss min EV", dark_bow_miss, 16.0f, 1e-6f);
    ASSERT_TRUE("voidwaker magic prayer reduces EV", voidwaker_on_prayer < voidwaker_off_prayer);
}

int main(void) {
    setbuf(stdout, NULL);
    test_terminal_reward_survives_auto_reset();
    test_terminal_loss_and_draw_rewards_zero();
    test_timeout_and_simultaneous_death_are_draws();
    test_p1_reset_obs_refreshes_after_auto_reset();
    test_target_hp_observation_is_current();
    test_shaping_disabled_emits_only_terminal_reward();
    test_expected_damage_reward_works_without_legacy_shaping();
    test_incoming_damage_avoidance_reward_works_without_legacy_shaping();
    test_ko_supply_reward_works_without_legacy_shaping();
    test_ko_chance_reward_works_without_legacy_shaping();
    test_reward_coefficients_parse_from_binding_kwargs();
    test_shaping_uses_encounter_overhead_and_spec_prayer_style();
    test_scripted_legacy_movement_maps_to_head_move();
    test_duplicate_equip_clicks_apply_once();
    test_food_brew_karambwan_can_resolve_same_tick();
    test_static_vec_train_mask_round_trip();
    test_pvp_state_snapshot_restores_logical_state();
    test_native_init_loads_collision_map_and_walkable_spawns();
    test_fixed_spawn_override_uses_walkable_pair();
    test_seeded_resets_replay_varied_setup_sequence();
    test_seeded_pid_shuffles_during_episode();
    test_movement_masks_respect_blocked_tiles();
    test_slotclick_schema_and_inventory_mask();
    test_two_handed_full_inventory_affordance_matches_masks();
    test_attack_mask_allows_post_equip_weapon_target_click();
    test_attack_mask_lazy_reach_matches_eager_reference();
    test_attack_reach_short_circuits_mobile_without_collision_map();
    test_special_mask_allows_post_equip_weapon_spec_arm();
    test_inventory_observation_item_facts();
    test_mage_inventory_observation_item_facts();
    test_item_observation_templates_match_direct_writers();
    test_inventory_affordance_projection_matches_copy_equip();
    test_pvp_log_emits_command_diagnostics();
    test_terminal_log_accumulates_command_diagnostics();
    test_no_weapon_observation_has_zero_attack_profile();
    test_collision_los_blocks_impenetrable_tiles();
    test_magic_attack_execution_respects_collision_los();
    test_pvp_barrage_uses_shared_five_tick_cadence();
    test_pvp_explicit_spell_masks_by_current_magic();
    test_pvp_explicit_spell_profiles();
    test_pvp_drained_magic_obs_and_restore_mask();
    test_attack_masks_respect_frozen_collision_los();
    test_mobile_attack_click_chases_around_collision_los();
    test_target_click_staff_bash_chases_into_melee_range();
    test_target_click_overrides_stale_walk_destination();
    test_shared_player_step_destination_clears_interaction();
    test_pvp_local_pathfind_window_only_handles_near_destinations();
    test_pvp_local_pathfind_matches_full_for_head_moves();
    test_persistent_staff_target_click_executes_melee();
    test_static_binding_exposes_separate_action_mask();
    test_static_binding_one_agent_skips_unexported_p1_generation();
    test_pvp_obs_norm_sparse_indices_cover_non_identity_divisors();
    test_pvp_obs_sparse_normalization_matches_full_division();
    test_static_binding_sets_scripted_opponents();
    test_binding_pfsp_stats_round_trip();
    test_expected_damage_prayer_modifier();
    test_expected_damage_prevention_uses_available_gear_and_prayer();
    test_expected_damage_zero_accuracy();
    test_expected_damage_standard_uniform();
    test_ko_chance_uniform_formula_matches_tracker();
    test_ko_chance_tracker_survival_aggregation();
    test_ko_chance_representative_specs();
    test_expected_damage_representative_specs();

    printf("\n=== results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed != 0) {
        return 1;
    }
    return 0;
}
