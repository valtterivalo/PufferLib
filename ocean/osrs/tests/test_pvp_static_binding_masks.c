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

static Dict* pvp_kwargs(void) {
    Dict* kwargs = create_dict(8);
    dict_set(kwargs, "seed", 73);
    dict_set(kwargs, "use_rollout_opponent", 1);
    dict_set(kwargs, "opponent_type", OPP_IMPROVED);
    dict_set(kwargs, "gear_tier", 3);
    dict_set(kwargs, "shaping_enabled", 1);
    dict_set(kwargs, "shaping_scale", 1.0);
    return kwargs;
}

static int action_head_offset(int head) {
    int offset = 0;
    for (int i = 0; i < head; i++) {
        offset += ACTION_HEAD_DIMS[i];
    }
    return offset;
}

static void test_native_init_loads_collision_map_and_walkable_spawns(void) {
    printf("--- PvP native init loads collision map and walkable spawns ---\n");

    Env env;
    memset(&env, 0, sizeof(env));

    Dict* kwargs = pvp_kwargs();
    my_init(&env, kwargs);

    const CollisionMap* cmap = (const CollisionMap*)env.pvp.collision_map;
    ASSERT_TRUE("native init collision map", cmap != NULL);
    ASSERT_INT_EQ("agent fixed spawn x", env.pvp.players[0].x, 3041);
    ASSERT_INT_EQ("agent fixed spawn y", env.pvp.players[0].y, 3530);
    ASSERT_INT_EQ("opponent fixed spawn x", env.pvp.players[1].x, 3046);
    ASSERT_INT_EQ("opponent fixed spawn y", env.pvp.players[1].y, 3531);
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

    ASSERT_INT_EQ("PvP action schema", PVP_ACTION_SCHEMA, PVP_ACTION_SCHEMA_SLOTCLICK_V9);
    ASSERT_INT_EQ("PvP action head count", NUM_ACTION_HEADS, 13);
    ASSERT_INT_EQ("equip click dim", ACTION_HEAD_DIMS[HEAD_EQUIP_0], OSRS_INVENTORY_SIZE + 1);
    ASSERT_INT_EQ("attack dim", ACTION_HEAD_DIMS[HEAD_ATTACK], ATTACK_DIM);
    ASSERT_INT_EQ("special dim", ACTION_HEAD_DIMS[HEAD_SPECIAL], SPECIAL_DIM);
    ASSERT_INT_EQ("action mask size", ACTION_MASK_SIZE, 171);

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

    int equip_offset = action_head_offset(HEAD_EQUIP_0);
    int special_offset = action_head_offset(HEAD_SPECIAL);
    ASSERT_INT_EQ("equip noop valid", env.action_masks[equip_offset], 1);
    ASSERT_INT_EQ("crossbow slot-click valid",
        env.action_masks[equip_offset + slot + 1], 1);
    ASSERT_INT_EQ("special noop valid",
        env.action_masks[special_offset + SPECIAL_NOOP], 1);

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
    agent->inventory[0] = ITEM_VOIDWAKER;
    generate_slot_observations(&env, 0);

    float* row = env.observations + PVP_INVENTORY_OBS_OFFSET;
    ASSERT_FLOAT_NEAR("inventory present", row[0], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("inventory item id normalized",
        row[1], (float)ITEM_VOIDWAKER / (float)(NUM_ITEMS - 1), 1e-6f);
    ASSERT_FLOAT_NEAR("inventory melee style", row[5], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("inventory spec cost", row[10], 0.5f, 1e-6f);
    ASSERT_FLOAT_NEAR("inventory can equip", row[31], 1.0f, 1e-6f);

    collision_map_free(cmap);
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

static void test_attack_masks_respect_frozen_collision_los(void) {
    printf("--- PvP attack masks respect frozen collision LOS ---\n");

    CollisionMap* cmap = collision_map_create();
    OsrsEnv env;
    setup_pvp_los_test_env(&env, cmap);
    Player* agent = &env.players[0];
    agent->frozen_ticks = 8;

    compute_action_masks(&env, 0);

    int combat_offset = action_head_offset(HEAD_COMBAT);
    ASSERT_INT_EQ("frozen ice through LOS blocker masked",
        env.action_masks[combat_offset + ATTACK_ICE], 0);
    ASSERT_INT_EQ("frozen blood through LOS blocker masked",
        env.action_masks[combat_offset + ATTACK_BLOOD], 0);

    agent->frozen_ticks = 0;
    compute_action_masks(&env, 0);
    ASSERT_INT_EQ("mobile ice attack-click remains valid",
        env.action_masks[combat_offset + ATTACK_ICE], 1);

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
    test_native_init_loads_collision_map_and_walkable_spawns();
    test_movement_masks_respect_blocked_tiles();
    test_slotclick_schema_and_inventory_mask();
    test_attack_mask_allows_post_equip_weapon_target_click();
    test_special_mask_allows_post_equip_weapon_spec_arm();
    test_inventory_observation_item_facts();
    test_no_weapon_observation_has_zero_attack_profile();
    test_collision_los_blocks_impenetrable_tiles();
    test_magic_attack_execution_respects_collision_los();
    test_pvp_barrage_uses_shared_five_tick_cadence();
    test_attack_masks_respect_frozen_collision_los();
    test_mobile_attack_click_chases_around_collision_los();
    test_target_click_staff_bash_chases_into_melee_range();
    test_target_click_overrides_stale_walk_destination();
    test_persistent_staff_target_click_executes_melee();
    test_static_binding_exposes_separate_action_mask();
    test_static_binding_sets_scripted_opponents();
    test_binding_pfsp_stats_round_trip();
    test_expected_damage_prayer_modifier();
    test_expected_damage_zero_accuracy();
    test_expected_damage_standard_uniform();
    test_expected_damage_representative_specs();

    printf("\n=== results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed != 0) {
        return 1;
    }
    return 0;
}
