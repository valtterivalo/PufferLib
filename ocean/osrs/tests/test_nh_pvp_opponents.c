#include "../../osrs_pvp/osrs_pvp.h"
#include "osrs_test_check.h"
#include "osrs_test_nh_pvp.h"
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static Env env;
static float observations[OBS_SIZE], actions[NUM_ATNS], reward, terminal;
static unsigned char mask[NH_PVP_ACTION_MASK_SIZE];

static void reset(OpponentType bot) {
    memset(&env, 0, sizeof(env));
    memset(actions, 0, sizeof(actions));
    DictItem items[] = {
        {.key = "opponent_type", .value = (double)bot}, {.key = "gear_tier", .value = 3},
        {.key = "is_lms", .value = 1}, {.key = "use_c_opponent", .value = 1},
        {.key = "self_play", .value = 0}, {.key = "auto_reset", .value = 0},
        OSRS_TEST_NH_PVP_REWARD_ITEMS
    };
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    env.rng = 123;
    puf_init(&env, &kwargs);
    env.agents[0] = (Agent){.observations = observations, .actions = actions,
        .rewards = &reward, .terminals = &terminal, .action_mask = mask};
    puf_reset(&env);
}

int main(void) {
    CHECK("sparse retained opponent catalogue", sizeof(NH_SCRIPTED_OPPONENTS) /
        sizeof(*NH_SCRIPTED_OPPONENTS) == 9 && OPP_GENERALIST_NH == 23 &&
        OPP_SELFPLAY == 31 && OPP_MIXED_NH == 33);
    for (int id = -1; id <= 38; id++) {
        int valid = id == OPP_NONE || id == OPP_SELFPLAY || osrs_nh_is_scripted_opponent((OpponentType)id);
        if (valid) {
            CHECK("valid sparse IDs preserve identity", osrs_nh_opponent_type(id) == id);
            continue;
        }
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            struct rlimit limit = {0, 0};
            assert(setrlimit(RLIMIT_CORE, &limit) == 0);
            osrs_nh_opponent_type(id);
            _exit(0);
        }
        int status;
        assert(waitpid(child, &status, 0) == child);
        CHECK("removed and unknown IDs abort instead of remapping", WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    }
    OppVisibleHistory history = {OPP_HISTORY_READY,
        {ATTACK_STYLE_MELEE, PRAYER_PROTECT_MAGIC}, {ATTACK_STYLE_RANGED, PRAYER_PROTECT_MELEE}};
    OppVisibleState forecast = opp_forecast_visible(history,
        (OppVisibleState){ATTACK_STYLE_MELEE, PRAYER_PROTECT_MAGIC});
    CHECK("completed ABA history predicts both visible weapon and overhead", forecast.weapon ==
        ATTACK_STYLE_RANGED && forecast.prayer == PRAYER_PROTECT_MELEE);
    reset(OPP_MIXED_NH);
    int members[4] = {0};
    for (int episode = 0; episode < 256; episode++) {
        puf_reset(&env);
        OpponentState* opp = &env.state.env.pvp_runtime.opponent;
        int member = -1;
        for (int i = 0; i < 4; i++) if (MIXED_NH_POOL[i] == opp->active_sub_policy) member = i;
        CHECK("balanced mixture resolves to the four named legal members", member >= 0);
        assert(member >= 0);
        members[member]++;
        OpponentType selected = opp->active_sub_policy;
        puf_step(&env);
        CHECK("episode identity remains fixed", opp->active_sub_policy == selected);
    }
    CHECK("reproducible episode stream visits all four members", members[0] && members[1] && members[2] && members[3]);
    for (int member = 0; member < 4; member++) {
        OpponentState* opp = &env.state.env.pvp_runtime.opponent;
        opp->type = MIXED_NH_POOL[member];
        opponent_reset(&env.state.env, opp);
        CHECK("mixture to direct transition clears sampled identity", opp->active_sub_policy == OPP_NONE);
    }
    puf_close(&env);
    reset(OPP_GENERALIST_NH);
    Player* restoring = &env.state.env.players[1];
    restoring->current_hitpoints = restoring->base_hitpoints;
    restoring->current_prayer = 1;
    restoring->current_ranged = restoring->base_ranged;
    int queued[OSRS_BASE_NUM_ACTION_HEADS] = {0};
    opp_apply_consumables(queued, restoring);
    opp_apply_boost_potion(queued, restoring, OPP_STYLE_RANGED);
    CHECK("queued prayer restore cannot be overwritten by a ranged boost", queued[OSRS_HEAD_DRINK] > 0 &&
        osrs_inventory_cell_metadata(&restoring->inventory_cells[queued[OSRS_HEAD_DRINK] - 1])->consumable_kind ==
            OSRS_CONSUMABLE_SUPER_RESTORE);
    int prayer_before = restoring->current_prayer;
    execute_switches(&env.state.env, 1, queued, env.context.route_topology);
    CHECK("queued restore actually restores prayer", restoring->current_prayer > prayer_before);
    puf_close(&env);
    reset(OPP_GMAUL_COMBO);
    OsrsEnv* burst = &env.state.env;
    burst->players[0].x = FIGHT_AREA_BASE_X + 10;
    burst->players[0].y = FIGHT_AREA_BASE_Y + 10;
    burst->players[1].x = burst->players[0].x + 1;
    burst->players[1].y = burst->players[0].y;
    burst->players[0].prayer = PRAYER_NONE;
    burst->players[1].inventory_cells[26] = osrs_inventory_cell_empty();
    burst->players[1].inventory_cells[27] = osrs_inventory_cell_from_item(ITEM_GRANITE_MAUL);
    burst->players[1].special_energy = 100;
    burst->players[1].attack_timer = -1;
    puf_step(&env);
    CHECK("burst arms followup only after first special actually executes",
        burst->players[1].used_special_this_tick && burst->pvp_runtime.opponent.combo_control.phase == OPP_COMBO_SUBMITTED);
    static OsrsEnv blocked;
    blocked = *burst;
    blocked.players[1].inventory_cells[26] = blocked.players[1].inventory_cells[27];
    generate_opponent_action(&blocked, &blocked.pvp_runtime.opponent);
    CHECK("full inventory blocks a two-handed maul followup without firing the old weapon special",
        blocked.pending_actions[OSRS_BASE_NUM_ACTION_HEADS + OSRS_HEAD_SPECIAL] == 0 &&
        blocked.pvp_runtime.opponent.combo_control.phase == OPP_COMBO_IDLE);
    int after_first = burst->players[1].special_energy;
    puf_step(&env);
    CHECK("burst executes canonical maul special during the existing cooldown",
        burst->players[1].used_special_this_tick && burst->players[1].equipped[GEAR_SLOT_WEAPON] == ITEM_GRANITE_MAUL &&
        burst->players[1].special_energy == after_first - osrs_spec_cost(ITEM_GRANITE_MAUL) &&
        burst->players[1].offensive_prayer == OFFENSIVE_PRAYER_PIETY);
    burst->pvp_runtime.opponent.combo_control.phase = OPP_COMBO_SUBMITTED;
    burst->pvp_runtime.opponent.combo_control.launch_tick = burst->pvp_runtime.visible[1].last_attack_tick;
    burst->players[1].attack_timer = 3;
    generate_opponent_action(burst, &burst->pvp_runtime.opponent);
    CHECK("queued but unexecuted first special cannot authorize a maul followup",
        burst->pvp_runtime.opponent.combo_control.phase == OPP_COMBO_IDLE &&
        burst->pending_actions[OSRS_BASE_NUM_ACTION_HEADS + OSRS_HEAD_SPECIAL] == 0);
    puf_close(&env);
    reset(OPP_GMAUL_COMBO);
    burst = &env.state.env;
    burst->players[0].x = FIGHT_AREA_BASE_X + 10;
    burst->players[0].y = FIGHT_AREA_BASE_Y + 10;
    burst->players[0].prayer = PRAYER_PROTECT_MAGIC;
    Player* full = &burst->players[1];
    full->x = burst->players[0].x + 1;
    full->y = burst->players[0].y;
    OsrsInventoryCell food = osrs_inventory_cell_from_raw_osrs_id(385);
    for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++) full->inventory_cells[cell] = food;
    full->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_GRANITE_MAUL);
    full->equipped[GEAR_SLOT_WEAPON] = ITEM_RUNE_CROSSBOW;
    full->equipped[GEAR_SLOT_SHIELD] = ITEM_DRAGON_DEFENDER;
    full->special_energy = 100;
    full->attack_timer = -1;
    pvp_refresh_visible_gear(full);
    osrs_refresh_player_equipment(full);
    CHECK("full inventory really rejects the requested two-handed weapon",
        !osrs_can_equip_from_cell(full, full->inventory_cells, 0));
    puf_step(&env);
    CHECK("rejected weapon switch cannot fire an old weapon special and resolves actual ranged prayer",
        full->just_attacked && full->attack_style_this_tick == ATTACK_STYLE_RANGED &&
        full->equipped[GEAR_SLOT_WEAPON] == ITEM_RUNE_CROSSBOW && full->special_energy == 100 &&
        full->offensive_prayer == OFFENSIVE_PRAYER_RIGOUR &&
        burst->last_executed_actions[OSRS_BASE_NUM_ACTION_HEADS + OSRS_HEAD_SPECIAL] == 2);
    puf_close(&env);
    static OsrsEnv traces[2];
    for (unsigned index = 0; index < sizeof(NH_SCRIPTED_OPPONENTS) / sizeof(*NH_SCRIPTED_OPPONENTS); index++) {
        reset(NH_SCRIPTED_OPPONENTS[index]);
        for (int seed = 1; seed <= 16; seed++) {
            traces[0] = env.state.env;
            traces[1] = env.state.env;
            for (int version = 0; version < 2; version++) {
                traces[version].rng_state = seed;
                opponent_reset(&traces[version], &traces[version].pvp_runtime.opponent);
            }
            for (int tick = 0; tick < 20; tick++) {
                for (int version = 0; version < 2; version++) {
                    traces[version].players[1].attack_timer = tick % 5 - 1;
                    generate_opponent_action(&traces[version], &traces[version].pvp_runtime.opponent);
                }
                CHECK("retained catalogue decisions state and RNG reproduce for identical streams",
                    memcmp(traces[0].pending_actions, traces[1].pending_actions, sizeof(traces[0].pending_actions)) == 0 &&
                    memcmp(&traces[0].pvp_runtime.opponent, &traces[1].pvp_runtime.opponent, sizeof(OpponentState)) == 0 &&
                    traces[0].rng_state == traces[1].rng_state);
            }
        }
        puf_close(&env);
    }
    for (unsigned index = 0; index < sizeof(NH_SCRIPTED_OPPONENTS) / sizeof(*NH_SCRIPTED_OPPONENTS); index++) {
        for (int actor = 0; actor < 2; actor++) {
            reset(OPP_GENERALIST_NH);
            OsrsEnv* pvp = &env.state.env;
            pvp->pvp_runtime.use_c_opponent_p0 = 1;
            pvp->pvp_runtime.opponent_p0.type = actor == 0 ? NH_SCRIPTED_OPPONENTS[index] : OPP_GENERALIST_NH;
            pvp->pvp_runtime.opponent.type = actor == 1 ? NH_SCRIPTED_OPPONENTS[index] : OPP_GENERALIST_NH;
            opponent_reset(pvp, &pvp->pvp_runtime.opponent_p0);
            opponent_reset(pvp, &pvp->pvp_runtime.opponent);
            CHECK("Vengeance spellbook belongs to the controlled actor", NH_SCRIPTED_OPPONENTS[index] != OPP_VENG_FIGHTER ||
                (pvp->players[actor].is_lunar_spellbook && !pvp->players[1-actor].is_lunar_spellbook));
            int launches = 0, magic = 0, ranged = 0, veng = 0;
            do {
                puf_step(&env);
                Player* self = &pvp->players[actor];
                veng += self->cast_veng_this_tick;
                CHECK("both actors retain legal spawn and movement", !encounter_arena_topology_tile_blocked(env.context.route_topology,
                    self->x, self->y));
                if (!self->just_attacked) continue;
                launches++;
                magic += self->attack_style_this_tick == ATTACK_STYLE_MAGIC;
                ranged += self->attack_style_this_tick == ATTACK_STYLE_RANGED;
                AttackStyle offense = self->used_special_this_tick && self->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER
                    ? ATTACK_STYLE_MELEE : self->attack_style_this_tick;
                CHECK("every retained actual launch has coherent offensive prayer", self->offensive_prayer ==
                    encounter_offensive_prayer_for_style(offense));
                CHECK("every retained actual launch uses nonoverlap combat geometry", self->last_attack_dist > 0);
            } while (!terminal);
            CHECK("each retained script engages to an actual terminal", launches > 0 && terminal && env.log.n == 1);
            CHECK("range repertoire executes ranged pressure", NH_SCRIPTED_OPPONENTS[index] != OPP_RANGE_KITER || ranged > 0);
            CHECK("Blood pressure executes actual magic", NH_SCRIPTED_OPPONENTS[index] != OPP_BLOOD_PRESSURE_NH || magic > 0);
            CHECK("Vengeance controller executes its own legal spell", NH_SCRIPTED_OPPONENTS[index] != OPP_VENG_FIGHTER || veng > 0);
            printf("bot%d actor%d ticks%d launches%d magic%d ranged%d veng%d\n", NH_SCRIPTED_OPPONENTS[index], actor,
                (int)env.log.episode_length, launches, magic, ranged, veng);
            puf_close(&env);
        }
    }
    return osrs_test_summary();
}
