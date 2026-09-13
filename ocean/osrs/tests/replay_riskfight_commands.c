#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../encounters/encounter_riskfight.h"

typedef enum { REPLAY_HUMAN, REPLAY_POLICY } ReplayAdapter;
typedef struct {
    const HumanCommand* commands[2];
    int count[2];
    int actions[2 * RF_HEADS];
} CommandFrame;
typedef struct {
    OsrsPvpHitEvent* hits;
    size_t count;
    RiskfightState* frames;
    size_t frame_count;
} CommandTrace;

static RiskfightState state;
static RiskfightContext context;
static CommandTrace trace;
static ReplayAdapter adapter;

#define CLICK(slot) {.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = (slot)}
#define SPEC {.kind = HUMAN_COMMAND_SPEC_TOGGLE}
#define TARGET {.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1}
#define STOP {.kind = HUMAN_COMMAND_STOP}
#define VENGEANCE {.kind = HUMAN_COMMAND_VENGEANCE}

static void collect_hit(void* user, const OsrsPvpHitEvent* event) {
    CommandTrace* output = user;
    OsrsPvpHitEvent* hits = realloc(output->hits, (output->count + 1) * sizeof(*hits));
    assert(hits);
    output->hits = hits;
    output->hits[output->count++] = *event;
}

static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
    context.self_play = 1;
    state.env.pvp_runtime.hit_observer = collect_hit;
    state.env.pvp_runtime.hit_observer_context = &trace;
    for (int actor = 0; actor < 2; actor++) {
        HumanCommand prayer = {.kind = HUMAN_COMMAND_OFFENSIVE_PRAYER, .offensive_prayer = 0};
        HumanCommand style = {.kind = HUMAN_COMMAND_FIGHT_STYLE, .fight_style = 0};
        riskfight_execute_command(&state, &context, actor, &prayer);
        riskfight_execute_command(&state, &context, actor, &style);
    }
}

static void frame(CommandFrame input) {
    if (adapter == REPLAY_POLICY) {
        riskfight_step((EncounterState*)&state, (EncounterContext*)&context, input.actions);
    } else {
        HumanCommandQueue queues[2];
        for (int actor = 0; actor < 2; actor++)
            queues[actor] = (HumanCommandQueue){
                .items = (HumanCommand*)input.commands[actor], .count = input.count[actor]};
        riskfight_step_queues(&state, &context, &queues[0], &queues[1]);
    }
    RiskfightState* frames = realloc(trace.frames, (trace.frame_count + 1) * sizeof(*frames));
    assert(frames);
    trace.frames = frames;
    RiskfightState* snapshot = &trace.frames[trace.frame_count++];
    *snapshot = state;
    snapshot->env.pvp_runtime.hit_observer = NULL;
    snapshot->env.pvp_runtime.hit_observer_context = NULL;
}

static int attack_events(int actor) {
    return (int)state.visible[1 - actor].events[(state.env.tick - 1) % RF_HISTORY_TICKS][0];
}

static void synthetic_triple_eat_orb_stop(void) {
    reset();
    state.env.players[0].current_hitpoints = 20;
    frame((CommandFrame){
        .commands = {(HumanCommand[]){CLICK(9), CLICK(7), CLICK(3), TARGET}}, .count = {4},
        .actions = {[RF_FOOD] = 10, [RF_DRINK] = 8, [RF_COMBO] = 4, [RF_PRIMARY] = RF_ATTACK},
    });
    assert(state.env.players[0].current_hitpoints == 80);
    assert(osrs_interaction_active(&state.env.players[0].interaction));
    assert(osrs_inventory_cell_is_empty(&state.env.players[0].inventory_cells[9]));
    assert(osrs_inventory_cell_is_empty(&state.env.players[0].inventory_cells[3]));
    assert(osrs_inventory_cell_metadata(&state.env.players[0].inventory_cells[7])->dose_count == 3);
    frame((CommandFrame){.commands = {(HumanCommand[]){CLICK(26)}}, .count = {1},
        .actions = {[RF_ORB] = 1}});
    assert(state.env.players[0].current_hitpoints == 70);
    assert(!osrs_interaction_active(&state.env.players[0].interaction));
    assert(state.env.players[0].veng_active);
    frame((CommandFrame){.commands = {(HumanCommand[]){TARGET}}, .count = {1},
        .actions = {[RF_PRIMARY] = RF_ATTACK}});
    assert(osrs_interaction_active(&state.env.players[0].interaction));
    frame((CommandFrame){.commands = {(HumanCommand[]){STOP}}, .count = {1},
        .actions = {[RF_PRIMARY] = RF_STOP}});
    frame((CommandFrame){0});
    assert(!osrs_interaction_active(&state.env.players[0].interaction));
    assert(attack_events(0) == 0);
}

static void synthetic_vengeance_recast(void) {
    reset();
    state.env.pid_holder = 1;
    frame((CommandFrame){
        .commands = {(HumanCommand[]){VENGEANCE}, (HumanCommand[]){CLICK(23), SPEC, TARGET}},
        .count = {1, 3},
        .actions = {[RF_VENGEANCE] = 1, [RF_HEADS + RF_WEAPON] = 24,
            [RF_HEADS + RF_SPECIAL] = 1, [RF_HEADS + RF_PRIMARY] = RF_ATTACK},
    });
    assert(!state.env.players[0].veng_active);
    assert(state.inventory_use[0].vengeance_sacks == 100);
    frame((CommandFrame){
        .commands = {(HumanCommand[]){CLICK(9), CLICK(3), VENGEANCE}, (HumanCommand[]){STOP}},
        .count = {3, 1},
        .actions = {[RF_FOOD] = 10, [RF_COMBO] = 4, [RF_VENGEANCE] = 1,
            [RF_HEADS + RF_PRIMARY] = RF_STOP},
    });
    assert(state.env.players[0].veng_active);
    assert(state.inventory_use[0].vengeance_sacks == 99);
    while (!can_attack_now(&state.env.players[1])) frame((CommandFrame){0});
    frame((CommandFrame){.commands = {NULL, (HumanCommand[]){SPEC, TARGET}}, .count = {0, 2},
        .actions = {[RF_HEADS + RF_SPECIAL] = 1, [RF_HEADS + RF_PRIMARY] = RF_ATTACK}});
    assert(!state.env.players[0].veng_active);
    frame((CommandFrame){.commands = {NULL, (HumanCommand[]){STOP}}, .count = {0, 1},
        .actions = {[RF_HEADS + RF_PRIMARY] = RF_STOP}});
    int reflections = 0;
    for (size_t i = 0; i < trace.count; i++)
        if (trace.hits[i].kind == OSRS_HIT_VENGEANCE && trace.hits[i].source == 0) reflections++;
    assert(reflections == 2);
    assert(state.inventory_use[0].vengeance_sacks == 99);
}

static void conditioned_recorded_maul_teleport_boundary(void) {
    reset();
    state.env.tick = 217;
    state.env.players[0].veng_active = 0;
    state.env.players[1].veng_active = 0;
    frame((CommandFrame){.commands = {(HumanCommand[]){CLICK(24), SPEC, TARGET}}, .count = {3},
        .actions = {[RF_WEAPON] = 25, [RF_SPECIAL] = 1, [RF_PRIMARY] = RF_ATTACK}});
    assert(attack_events(0) == 1 && state.env.players[0].used_special_this_tick);
    assert(state.env.pvp_runtime.teleport[0].blocked_until_tick == 226);
    frame((CommandFrame){0});
    assert(attack_events(0) == 1 && !state.env.players[0].used_special_this_tick);
    frame((CommandFrame){.commands = {(HumanCommand[]){STOP}}, .count = {1},
        .actions = {[RF_PRIMARY] = RF_STOP}});
    while (state.env.tick < 225) frame((CommandFrame){0});
    frame((CommandFrame){.commands = {(HumanCommand[]){CLICK(20)}}, .count = {1},
        .actions = {[RF_PRIMARY] = RF_TELEPORT}});
    assert(!state.env.episode_over && !state.escaped[0]);
    assert(!osrs_inventory_cell_is_empty(&state.env.players[0].inventory_cells[20]));
    frame((CommandFrame){.commands = {(HumanCommand[]){CLICK(20)}}, .count = {1},
        .actions = {[RF_PRIMARY] = RF_TELEPORT}});
    assert(state.escaped[0] && state.env.episode_over);
    assert(state.outcome[0] == RISKFIGHT_ESCAPE && state.rewards[0] == 0);
    assert(osrs_inventory_cell_is_empty(&state.env.players[0].inventory_cells[20]));
}

static void synthetic_double_maul(void) {
    reset();
    state.env.players[0].veng_active = 0;
    state.env.players[1].veng_active = 0;
    frame((CommandFrame){.commands = {(HumanCommand[]){CLICK(24), SPEC, SPEC, TARGET}}, .count = {4},
        .actions = {[RF_WEAPON] = 25, [RF_SPECIAL] = 2, [RF_PRIMARY] = RF_ATTACK}});
    assert(state.env.players[0].special_energy == 0);
    assert(attack_events(0) == 2);
    assert(trace.count == 2);
    for (size_t i = 0; i < trace.count; i++) {
        assert(trace.hits[i].tick == 0);
        assert(trace.hits[i].source == 0 && trace.hits[i].target == 1);
        assert(trace.hits[i].kind == OSRS_HIT_DIRECT);
    }
    frame((CommandFrame){.commands = {(HumanCommand[]){STOP}}, .count = {1},
        .actions = {[RF_PRIMARY] = RF_STOP}});
    assert(attack_events(0) == 0);
    assert(trace.count == 2);
}

static void conditioned_recorded_armour_cycle(void) {
    reset();
    state.env.tick = 23807;
    Player* player = &state.env.players[0];
    player->inventory_cells[9] = osrs_inventory_cell_empty();
    player->inventory_cells[10] = osrs_inventory_cell_empty();
    player->veng_active = 0;
    frame((CommandFrame){
        .commands = {(HumanCommand[]){
            {.kind = HUMAN_COMMAND_UNEQUIP, .gear_slot = GEAR_SLOT_HEAD},
            {.kind = HUMAN_COMMAND_UNEQUIP, .gear_slot = GEAR_SLOT_BODY},
            {.kind = HUMAN_COMMAND_UNEQUIP, .gear_slot = GEAR_SLOT_LEGS}}}, .count = {3},
        .actions = {[RF_HEAD] = RF_UNEQUIP, [RF_BODY] = RF_UNEQUIP, [RF_LEGS] = RF_UNEQUIP},
    });
    assert(player->equipped[GEAR_SLOT_HEAD] == ITEM_NONE);
    assert(player->equipped[GEAR_SLOT_BODY] == ITEM_NONE);
    assert(player->equipped[GEAR_SLOT_LEGS] == ITEM_NONE);
    frame((CommandFrame){.commands = {(HumanCommand[]){VENGEANCE}}, .count = {1},
        .actions = {[RF_VENGEANCE] = 1}});
    assert(player->veng_active);
    frame((CommandFrame){.commands = {(HumanCommand[]){CLICK(23), SPEC, TARGET}}, .count = {3},
        .actions = {[RF_WEAPON] = 24, [RF_SPECIAL] = 1, [RF_PRIMARY] = RF_ATTACK}});
    assert(player->special_energy == 50);
    frame((CommandFrame){.commands = {(HumanCommand[]){STOP}}, .count = {1},
        .actions = {[RF_PRIMARY] = RF_STOP}});
    while (state.env.tick < 23813) frame((CommandFrame){0});
    frame((CommandFrame){.commands = {(HumanCommand[]){CLICK(9), CLICK(10), CLICK(27)}}, .count = {3},
        .actions = {[RF_HEAD] = 10, [RF_BODY] = 11, [RF_LEGS] = 28}});
    assert(player->equipped[GEAR_SLOT_HEAD] == ITEM_DHAROKS_HELM);
    assert(player->equipped[GEAR_SLOT_BODY] == ITEM_DHAROKS_PLATEBODY);
    assert(player->equipped[GEAR_SLOT_LEGS] == ITEM_DHAROKS_PLATELEGS);
}

static void verify(const char* name, const char* evidence, void (*scenario)(void)) {
    static RiskfightState expected;
    CommandTrace expected_trace = {0};
    const ReplayAdapter runs[] = {REPLAY_HUMAN, REPLAY_POLICY, REPLAY_HUMAN};
    for (size_t run = 0; run < sizeof(runs) / sizeof(runs[0]); run++) {
        adapter = runs[run];
        trace = (CommandTrace){0};
        scenario();
        state.env.pvp_runtime.hit_observer = NULL;
        state.env.pvp_runtime.hit_observer_context = NULL;
        if (run == 0) {
            expected = state;
            expected_trace = trace;
        } else {
            assert(memcmp(&state, &expected, sizeof(state)) == 0);
            assert(trace.frame_count == expected_trace.frame_count);
            for (size_t i = 0; i < trace.frame_count; i++)
                assert(memcmp(&trace.frames[i], &expected_trace.frames[i], sizeof(trace.frames[i])) == 0);
            assert(trace.count == expected_trace.count);
            for (size_t i = 0; i < trace.count; i++)
                assert(memcmp(&trace.hits[i], &expected_trace.hits[i], sizeof(trace.hits[i])) == 0);
            free(trace.hits);
            free(trace.frames);
        }
    }
    printf("{\"scenario\":\"%s\",\"evidence\":\"%s\",\"seed\":12345,"
        "\"every_frame_human_policy_equal\":true,\"seeded_replay_equal\":true,"
        "\"final_tick\":%d,\"final_hp\":[%d,%d],\"hit_trace_scope\":\"OPPONENT_DAMAGE_ONLY\",\"hits\":[",
        name, evidence, expected.env.tick,
        expected.env.players[0].current_hitpoints, expected.env.players[1].current_hitpoints);
    for (size_t i = 0; i < expected_trace.count; i++) {
        const OsrsPvpHitEvent* hit = &expected_trace.hits[i];
        printf("%s{\"tick\":%d,\"source\":%d,\"target\":%d,\"kind\":%d,\"damage\":%d}",
            i ? "," : "", hit->tick, hit->source, hit->target, hit->kind, hit->damage);
    }
    puts("]}");
    free(expected_trace.hits);
    free(expected_trace.frames);
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    verify("triple_eat_orb_stop", "SYNTHETIC", synthetic_triple_eat_orb_stop);
    verify("precast_vengeance_recast_second_reflection", "SYNTHETIC", synthetic_vengeance_recast);
    verify("double_ornate_maul_two_payable_hits", "SYNTHETIC", synthetic_double_maul);
    verify("strip23807_vengeance23808_voidwaker23809_restore23813",
        "RECORDED_VISIBLE_TIMING_WITH_SYNTHETIC_COMMANDS_AND_DAMAGE", conditioned_recorded_armour_cycle);
    verify("maul217_ordinary218_teleport226", "RECORDED_TIMING_BOUNDARY_WITH_SYNTHETIC_COMMANDS",
        conditioned_recorded_maul_teleport_boundary);
    puts("{\"recorded_boundary_provenance\":{\"archive\":\"session_20260913T093112.716Z_20260913T093024Z.89354_81a22aa0-6e18-4b36-b953-a3c8cab6906a.jsonl.zst\","
        "\"source_sha256\":\"d844cb703d1f1525a880b987837c992c1112a4a0cddebc7c76ef2023d5c4866b\","
        "\"actor\":\"Amers\",\"special_sequence\":5540,\"teleport_sequences\":[5740,5741],"
        "\"assumptions\":\"Reset gear, HP, supplies, attack readiness and seeded damage are synthetic. Stop at219 and blocked teleport attempt225 are synthetic. Observed special217, ordinary218 and teleport226 constrain timing only. Escape is immediate in simulator, observed despawn229 is not reproduced.\"}}");
    riskfight_destroy_context((EncounterContext*)&context);
}
