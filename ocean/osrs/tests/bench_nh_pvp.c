#define _POSIX_C_SOURCE 200809L
#include <time.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include "ocean/osrs/encounters/encounter_nh_pvp.h"
#include "src/ini.h"

typedef enum { BENCH_BASELINE, BENCH_SPARSE, BENCH_DENSE } BenchRewardMode;

typedef struct {
    NhPvpState state;
    NhPvpContext context;
    float observations[NH_PVP_NUM_OBS];
    unsigned char mask[NH_PVP_ACTION_MASK_SIZE];
    int actions[NH_PVP_NUM_ACTION_HEADS];
    uint64_t episodes, wins[3], attacks[4], spells[OSRS_MAGIC_ATTACK_POWERED_STAFF + 1], equips, moves, consumes;
    uint64_t reward_nonzero, reward_out_of_range;
    double reward_min, reward_max, reward_sum;
    double step_seconds, observation_seconds, mask_seconds, reset_seconds;
} BenchEnv;

static double bench_now(void) {
    struct timespec time;
    assert(clock_gettime(CLOCK_MONOTONIC, &time) == 0);
    return time.tv_sec + time.tv_nsec * 1e-9;
}

int main(int argc, char** argv) {
    assert(argc == 4 || argc == 6);
    int count = atoi(argv[1]);
    int threads = atoi(argv[2]);
    int ticks = atoi(argv[3]);
    assert(count > 0 && threads > 0 && ticks > 0);
    BenchRewardMode reward_mode = BENCH_BASELINE;
    RewardShapingConfig shaping = {0};
    if (argc == 6) {
        assert(strcmp(argv[4], "sparse") == 0 || strcmp(argv[4], "dense") == 0);
        reward_mode = strcmp(argv[4], "dense") == 0 ? BENCH_DENSE : BENCH_SPARSE;
        Ini ini = {0};
        puf_ini_load_file(&ini, argv[5]);
#define BENCH_REWARD_FLOAT_RANGE(key, field, default_value) \
        { \
            Dict* range = puf_ini_section(&ini, "sweep.env." key, 0); \
            double low = dict_get(range, "min"), high = dict_get(range, "max"); \
            shaping.field = fabs(low) > fabs(high) ? low : high; \
        }
        OSRS_PVP_REWARD_FLOAT_FIELDS(BENCH_REWARD_FLOAT_RANGE)
#undef BENCH_REWARD_FLOAT_RANGE
#define BENCH_REWARD_INT_RANGE(key, field, default_value) \
        shaping.field = (int)puf_ini_get(&ini, "sweep.env." key, "max");
        OSRS_PVP_REWARD_INT_FIELDS(BENCH_REWARD_INT_RANGE)
#undef BENCH_REWARD_INT_RANGE
        shaping.enabled = reward_mode == BENCH_DENSE;
        shaping.premature_eat_threshold = puf_ini_get(&ini, "sweep.env.premature_eat_threshold", "min");
        shaping.damage_burst_threshold = (int)puf_ini_get(&ini, "sweep.env.damage_burst_threshold", "min");
        shaping.click_penalty_threshold = (int)puf_ini_get(&ini, "sweep.env.click_penalty_threshold", "min");
        puf_ini_free(&ini);
    }
#ifdef _OPENMP
    omp_set_dynamic(0);
    omp_set_num_threads(threads);
#else
    assert(threads == 1);
#endif
    BenchEnv* envs = calloc(count, sizeof(*envs));
    assert(envs);
    double started = bench_now();
    for (int index = 0; index < count; index++) {
        BenchEnv* b = &envs[index];
        nh_pvp_init_context((EncounterContext*)&b->context);
        nh_pvp_init_state((EncounterState*)&b->state, (EncounterContext*)&b->context);
        nh_pvp_put_int((EncounterState*)&b->state, (EncounterContext*)&b->context, "gear_tier", 3);
        nh_pvp_put_int((EncounterState*)&b->state, (EncounterContext*)&b->context, "is_lms", 1);
        nh_pvp_put_int((EncounterState*)&b->state, (EncounterContext*)&b->context, "auto_reset", 0);
        nh_pvp_put_int((EncounterState*)&b->state, (EncounterContext*)&b->context, "opponent_type", OPP_MIXED_NH);
        nh_pvp_put_int((EncounterState*)&b->state, (EncounterContext*)&b->context, "opponent_p0_type", OPP_GENERALIST_NH);
        nh_pvp_put_int((EncounterState*)&b->state, (EncounterContext*)&b->context, "use_c_opponent", 1);
        nh_pvp_put_int((EncounterState*)&b->state, (EncounterContext*)&b->context, "use_c_opponent_p0", 1);
        b->state.env.shaping.enabled = 0;
        if (reward_mode != BENCH_BASELINE) b->state.env.shaping = shaping;
        nh_pvp_finalize_context((EncounterState*)&b->state, (EncounterContext*)&b->context);
        nh_pvp_reset((EncounterState*)&b->state, (EncounterContext*)&b->context, osrs_env_seed(73, index));
        b->state.env.pvp_runtime.use_c_opponent_p0 = 0;
    }
    double initialization_seconds = bench_now() - started;
    double control_seconds = 0, environment_seconds = 0, phase_started = 0;
    started = bench_now();
#pragma omp parallel shared(phase_started, control_seconds, environment_seconds)
    {
        for (int tick = 0; tick < ticks; tick++) {
#pragma omp single
            phase_started = bench_now();
#pragma omp for schedule(static)
            for (int index = 0; index < count; index++) {
                BenchEnv* b = &envs[index];
                OsrsEnv* env = &b->state.env;
                generate_opponent_action_for_player0(env, &env->pvp_runtime.opponent_p0);
                const int* raw = env->pending_actions;
                Player* player = &env->players[0];
                int* actions = b->actions;
                memset(actions, 0, sizeof(b->actions));
                int primary = raw[OSRS_HEAD_PRIMARY];
                int spell = raw[OSRS_HEAD_SPELL];
                assert(primary >= 0 && primary <= OSRS_PRIMARY_MOVE_ACTIONS);
                assert(spell >= OSRS_SPELL_NONE && spell <= OSRS_SPELL_VENGEANCE);
                actions[NH_PVP_HEAD_PRIMARY] = primary;
                if (primary == OSRS_PRIMARY_MOVE_ACTIONS) {
                    if (spell == OSRS_SPELL_ICE_BARRAGE) actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_ICE;
                    if (spell == OSRS_SPELL_BLOOD_BARRAGE) actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_BLOOD;
                } else assert(spell != OSRS_SPELL_ICE_BARRAGE && spell != OSRS_SPELL_BLOOD_BARRAGE);
                if (spell == OSRS_SPELL_VENGEANCE) actions[NH_PVP_HEAD_UTILITY] = NH_PVP_UTILITY_VENGEANCE;
                actions[NH_PVP_HEAD_OVERHEAD] = raw[OSRS_HEAD_OVERHEAD];
                actions[NH_PVP_HEAD_OFFENSIVE] = raw[OSRS_HEAD_OFFENSIVE];
                actions[NH_PVP_HEAD_SPECIAL] = raw[OSRS_HEAD_SPECIAL];
                uint8_t requested_weapon = player->equipped[GEAR_SLOT_WEAPON];
                for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
                    int cell = raw[OSRS_HEAD_EQUIP_SLOT(slot)] - 1;
                    if (cell < 0) continue;
                    assert(cell < OSRS_INVENTORY_SIZE);
                    const OsrsItemContentMetadata* metadata = osrs_inventory_cell_metadata(&player->inventory_cells[cell]);
                    assert(metadata->click_action == OSRS_CLICK_EQUIP && metadata->gear_slot == slot);
                    actions[NH_PVP_HEAD_EQUIP_SLOT(slot)] = nh_pvp_equipment_action_for_item(slot, metadata->item_idx);
                    if (slot == GEAR_SLOT_WEAPON) requested_weapon = metadata->item_idx;
                }
                assert(raw[OSRS_HEAD_SPECIAL] != 2 || !pvp_is_maul(requested_weapon));
                const int raw_heads[] = {OSRS_HEAD_EAT, OSRS_HEAD_DRINK};
                const int heads[] = {NH_PVP_HEAD_FOOD, NH_PVP_HEAD_DRINK, NH_PVP_HEAD_KARAMBWAN};
                const OsrsConsumableKind* kinds[] = {NH_PVP_FOOD_KINDS, NH_PVP_DRINK_KINDS, NH_PVP_KARAMBWAN_KINDS};
                for (int input = 0; input < 2; input++) {
                    int cell = raw[raw_heads[input]] - 1;
                    if (cell < 0) continue;
                    assert(cell < OSRS_INVENTORY_SIZE);
                    const OsrsItemContentMetadata* metadata = osrs_inventory_cell_metadata(&player->inventory_cells[cell]);
                    assert(metadata->click_action == (input == 0 ? OSRS_CLICK_EAT : OSRS_CLICK_DRINK));
                    int matched = 0;
                    for (int stage = 0; stage < 3; stage++)
                        for (int action = 1; action < NH_PVP_ACTION_DIMS[heads[stage]]; action++)
                            if (kinds[stage][action] == metadata->consumable_kind) {
                                actions[heads[stage]] = action;
                                matched++;
                            }
                    assert(matched == 1);
                }
                for (int head = 0; head < NH_PVP_NUM_ACTION_HEADS; head++)
                    assert(actions[head] >= 0 && actions[head] < NH_PVP_ACTION_DIMS[head]);
            }
#pragma omp single
            {
                double current = bench_now();
                control_seconds += current - phase_started;
                phase_started = current;
            }
#pragma omp for schedule(static)
            for (int index = 0; index < count; index++) {
                BenchEnv* b = &envs[index];
                OsrsEnv* env = &b->state.env;
                uint8_t weapons[2] = {env->players[0].equipped[GEAR_SLOT_WEAPON],
                    env->players[1].equipped[GEAR_SLOT_WEAPON]};
                double time = bench_now();
                nh_pvp_step((EncounterState*)&b->state, (EncounterContext*)&b->context, b->actions);
                b->step_seconds += bench_now() - time;
                time = bench_now();
                nh_pvp_write_obs((EncounterState*)&b->state, (EncounterContext*)&b->context, b->observations);
                b->observation_seconds += bench_now() - time;
                time = bench_now();
                pvp_write_action_mask_bytes(b->mask, env, 0, b->context.route_topology);
                b->mask_seconds += bench_now() - time;
                __asm__ volatile("" : : "g"(b->observations), "g"(b->mask) : "memory");
                for (int actor = 0; actor < NUM_AGENTS; actor++) {
                    Player* p = &env->players[actor];
                    if (p->just_attacked) {
                        b->attacks[p->attack_style_this_tick]++;
                        b->spells[p->magic_type_this_tick]++;
                    }
                    b->equips += p->equipped[GEAR_SLOT_WEAPON] != weapons[actor];
                    b->moves += env->pvp_runtime.visible[actor].move_dx != 0 || env->pvp_runtime.visible[actor].move_dy != 0;
                    b->consumes += p->ate_food_this_tick + p->ate_karambwan_this_tick + p->ate_brew_this_tick;
                    double reward = env->rewards[actor];
                    b->reward_min = fmin(b->reward_min, reward);
                    b->reward_max = fmax(b->reward_max, reward);
                    b->reward_sum += reward;
                    b->reward_nonzero += reward != 0;
                    b->reward_out_of_range += reward < -1 || reward > 1;
                }
                if (env->episode_over) {
                    b->episodes++;
                    b->wins[env->winner + 1]++;
                    time = bench_now();
                    env->pvp_runtime.use_c_opponent_p0 = 1;
                    nh_pvp_reset((EncounterState*)&b->state, (EncounterContext*)&b->context, 0);
                    env->pvp_runtime.use_c_opponent_p0 = 0;
                    b->reset_seconds += bench_now() - time;
                }
            }
#pragma omp single
            environment_seconds += bench_now() - phase_started;
        }
    }
    double wall_seconds = bench_now() - started;
    BenchEnv totals = {0};
    double sink = 0;
    for (int index = 0; index < count; index++) {
        BenchEnv* b = &envs[index];
        totals.episodes += b->episodes;
        for (int i = 0; i < 3; i++) totals.wins[i] += b->wins[i];
        for (int i = 0; i <= OSRS_MAGIC_ATTACK_POWERED_STAFF; i++) totals.spells[i] += b->spells[i];
        for (int i = 0; i < 4; i++) totals.attacks[i] += b->attacks[i];
        totals.equips += b->equips;
        totals.moves += b->moves;
        totals.consumes += b->consumes;
        totals.reward_min = fmin(totals.reward_min, b->reward_min);
        totals.reward_max = fmax(totals.reward_max, b->reward_max);
        totals.reward_sum += b->reward_sum;
        totals.reward_nonzero += b->reward_nonzero;
        totals.reward_out_of_range += b->reward_out_of_range;
        totals.step_seconds += b->step_seconds;
        totals.observation_seconds += b->observation_seconds;
        totals.mask_seconds += b->mask_seconds;
        totals.reset_seconds += b->reset_seconds;
        for (int i = 0; i < NH_PVP_NUM_OBS; i++) sink += b->observations[i];
        for (int i = 0; i < NH_PVP_ACTION_MASK_SIZE; i++) sink += b->mask[i];
        pvp_close(&b->state.env);
        nh_pvp_destroy_context((EncounterContext*)&b->context);
    }
    uint64_t steps = (uint64_t)count * ticks;
    printf("{\"envs\":%d,\"threads\":%d,\"ticks_per_env\":%d,\"steps\":%llu,\"obs\":%d,\"heads\":%d,\"mask\":%d,\"initialization_seconds\":%.9f,\"wall_seconds\":%.9f,\"controller_wall_seconds\":%.9f,\"environment_wall_seconds\":%.9f,\"wall_sps\":%.3f,\"environment_sps\":%.3f,\"step_call_seconds\":%.9f,\"observation_call_seconds\":%.9f,\"mask_call_seconds\":%.9f,\"reset_call_seconds\":%.9f,\"episodes\":%llu,\"outcomes_draw_p0_p1\":[%llu,%llu,%llu],\"attacks_none_melee_range_magic\":[%llu,%llu,%llu,%llu],\"spells_none_ice_blood_standard_powered\":[%llu,%llu,%llu,%llu,%llu],\"weapon_changes\":%llu,\"moved_actor_ticks\":%llu,\"consumes\":%llu,\"sink\":%.9f,\"reward_mode\":%d,\"reward_min\":%.9f,\"reward_max\":%.9f,\"reward_sum_both_actors\":%.9f,\"reward_nonzero_both_actors\":%llu,\"reward_out_of_range_both_actors\":%llu,\"reward_parameters\":{",
        count, threads, ticks, (unsigned long long)steps, NH_PVP_NUM_OBS, NH_PVP_NUM_ACTION_HEADS, NH_PVP_ACTION_MASK_SIZE,
        initialization_seconds, wall_seconds, control_seconds, environment_seconds, steps / wall_seconds, steps / environment_seconds,
        totals.step_seconds, totals.observation_seconds, totals.mask_seconds, totals.reset_seconds,
        (unsigned long long)totals.episodes, (unsigned long long)totals.wins[0], (unsigned long long)totals.wins[1], (unsigned long long)totals.wins[2],
        (unsigned long long)totals.attacks[0], (unsigned long long)totals.attacks[1], (unsigned long long)totals.attacks[2], (unsigned long long)totals.attacks[3],
        (unsigned long long)totals.spells[0], (unsigned long long)totals.spells[1], (unsigned long long)totals.spells[2], (unsigned long long)totals.spells[3], (unsigned long long)totals.spells[4],
        (unsigned long long)totals.equips, (unsigned long long)totals.moves, (unsigned long long)totals.consumes, sink,
        reward_mode, totals.reward_min, totals.reward_max, totals.reward_sum,
        (unsigned long long)totals.reward_nonzero, (unsigned long long)totals.reward_out_of_range);
    if (reward_mode != BENCH_BASELINE) {
#define BENCH_PRINT_REWARD_FLOAT(key, field, default_value) printf("\"%s\":%.9g,", key, (double)shaping.field);
        OSRS_PVP_REWARD_FLOAT_FIELDS(BENCH_PRINT_REWARD_FLOAT)
#undef BENCH_PRINT_REWARD_FLOAT
#define BENCH_PRINT_REWARD_INT(key, field, default_value) printf("\"%s\":%d,", key, shaping.field);
        OSRS_PVP_REWARD_INT_FIELDS(BENCH_PRINT_REWARD_INT)
#undef BENCH_PRINT_REWARD_INT
        printf("\"mode\":%d", reward_mode);
    }
    printf("},\"action_lane\":\"nh_semantic\"}\n");
    assert(totals.reward_out_of_range == 0);
    free(envs);
    return 0;
}
