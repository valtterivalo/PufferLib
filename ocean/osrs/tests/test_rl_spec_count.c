/* Field 10 of the V line counts the specials he launched on the tick (a double granite maul is 2, one animation and two hits). The live server charges
 * count x osrs_spec_cost(weapon) to his special energy, on the swing's tick or the next, and the sim's own observation of his energy counts the same way.
 *   cc -std=c11 -O2 -I. -o /tmp/t ocean/osrs/tests/test_rl_spec_count.c -lm && /tmp/t   (cwd = repo root) */
#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"

#define MAUL_RAW 24225 /* ornate granite maul */

static RiskfightVisibleOpponent fresh(void) {
    RiskfightVisibleOpponent v;
    memset(&v, 0, sizeof(v));
    v.last_cast_tick = v.last_proc_tick = -1000;
    v.last_attack_tick = -1;
    v.pending_consume_tick = -1;
    v.health_bar = OSRS_PLAYER_HEALTH_BAR_SCALE;
    v.spec_est = 100;
    return v;
}

static void observe(RiskfightVisibleOpponent* v, int swing_raw, int style, int spec, int n) {
    static Request q;
    RtSeen seen = {swing_raw, style, 0, 0, 0, 0, -1, -1, 0, spec};
    rt_observe(v, &seen, n, &q);
}

static float session_block_energy(const RiskfightVisibleOpponent* v) {
    static RiskfightState s;
    riskfight_reset((EncounterState*)&s, (EncounterContext*)&(RiskfightContext){0}, 5);
    s.visible[0] = *v;
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&s, 0, obs);
    return obs[RF_SESSION_START + 1];
}

static RiskfightState sim;
static RiskfightContext sim_context;

static HumanCommand inv(int slot) { return (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = slot}; }
static HumanCommand spec_click(void) { return (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE}; }
static HumanCommand target(void) { return (HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1}; }

static void sim_maul(int clicks) {
    riskfight_reset((EncounterState*)&sim, (EncounterContext*)&sim_context, 12345);
    sim_context.self_play = 1;
    for (int i = 0; i < 2; i++) sim.env.players[i].veng_active = 0;
    HumanCommand double_spec[] = {inv(24), spec_click(), spec_click(), target()};
    HumanCommand single_spec[] = {inv(24), spec_click(), target()};
    HumanCommandQueue queue = clicks == 2 ? (HumanCommandQueue){.items = double_spec, .count = 4} : (HumanCommandQueue){.items = single_spec, .count = 3};
    HumanCommandQueue empty = {0};
    riskfight_step_queues(&sim, &sim_context, &queue, &empty);
}

int main(void) {
    observer_register_items();
    assert(osrs_spec_cost(ITEM_GRANITE_MAUL_ORNATE) == 50);
    assert(ITEM_DATABASE[ITEM_GRANITE_MAUL_ORNATE].item_id == MAUL_RAW);
    /* Live: a double maul counted 2 on its swing's tick leaves him at 0 from 100; a single leaves 50. */
    {
        RiskfightVisibleOpponent v = fresh();
        observe(&v, MAUL_RAW, 1, 2, 20);
        assert(v.spec_est == 0 && session_block_energy(&v) == 0);
        v = fresh();
        observe(&v, MAUL_RAW, 1, 1, 20);
        assert(v.spec_est == 50);
    }
    /* The count arrives a tick late, whole or split over both ticks: charged to the last swing's weapon either way. */
    {
        RiskfightVisibleOpponent v = fresh();
        observe(&v, MAUL_RAW, 1, 0, 20);
        assert(v.spec_est == 100);
        observe(&v, -1, 0, 2, 21);
        assert(v.spec_est == 0);
        v = fresh();
        observe(&v, MAUL_RAW, 1, 1, 20);
        observe(&v, -1, 0, 1, 21);
        assert(v.spec_est == 0);
    }
    /* A Behead is one special that costs nothing; a late count still reads as the Behead for the stacks. */
    {
        RiskfightVisibleOpponent v = fresh();
        for (int n = 20; n < 25; n++) observe(&v, 28338, 1, 0, n);
        assert(riskfight_threat_stacks(&v.threat, 25) == 5);
        observe(&v, 28338, 1, 0, 25);
        observe(&v, -1, 0, 1, 26);
        assert(v.spec_est == 100 && riskfight_threat_stacks(&v.threat, 27) == 0);
    }
    /* The sim: a double maul of his is two specials seen by us, a single one is one; the specs_used count follows. */
    {
        sim_maul(2);
        assert(sim.visible[1].spec_est == 0 && sim.specs_used[0] == 2);
        float obs[RF_OBS_SIZE];
        riskfight_write_observation(&sim, 1, obs);
        assert(obs[RF_SESSION_START + 1] == 0);
        RiskfightVisibleOpponent live = fresh();
        observe(&live, MAUL_RAW, 1, 2, 20);
        assert(live.spec_est == sim.visible[1].spec_est);
        sim_maul(1);
        assert(sim.visible[1].spec_est == 50 && sim.specs_used[0] == 1);
    }
    printf("spec count: double maul 100 -> 0 live and in the sim, single 100 -> 50, late count charged to the last swing\n");
    return 0;
}
