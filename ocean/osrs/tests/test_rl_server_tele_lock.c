/* The RL server shuts our teleport for the sim's 8 ticks after our special (osrs_teleport.h), seen live as our special energy falling between two requests.
 * The live request stream of fixtures/live_swings.requests (fight start 412, a house tab in the bag; our energy 100 through tick 428, 50 from 429 on: our Voidwaker special)
 * through the server's own path (rt_parse, rt_decide with no net), against the sim: a Voidwaker special executed on step 17, observed on ticks 18 onward.
 * Both must read the same RF_SELF_TELEPORT_LOCK and the same teleport bit of the mask on every tick: 7/8 down to 1/8 and shut on 18..24, 0 and open otherwise. */
#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"

enum { SPEC_TICK = 17, LAST_TICK = 48 };

static int teleport_open(const RiskfightState* s) {
    unsigned char mask[RF_MASK_SIZE];
    riskfight_write_action_mask(s, 0, mask);
    int offset = 0;
    for (int i = 0; i < RF_PRIMARY; i++) offset += RF_ACTION_DIMS[i];
    return mask[offset + RF_TELEPORT];
}

/* The sim's reading on fight ticks 1..LAST_TICK around a special executed while stepping SPEC_TICK. */
static void sim_reference(float* lock, int* open) {
    static RiskfightState s;
    static RiskfightContext ctx;
    riskfight_reset((EncounterState*)&s, (EncounterContext*)&ctx, 12345);
    ctx.self_play = 1;
    s.env.players[0].veng_active = s.env.players[1].veng_active = 0;
    HumanCommand wield = {.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = 24}, toggle = {.kind = HUMAN_COMMAND_SPEC_TOGGLE},
        attack = {.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1};
    riskfight_execute_command(&s, &ctx, 0, &wield);
    float obs[RF_OBS_SIZE];
    for (int t = 1; t <= LAST_TICK; t++) {
        s.env.tick = t;
        if (t == SPEC_TICK + 1) assert(s.env.players[0].special_energy == 50);
        riskfight_write_observation(&s, 0, obs);
        lock[t] = obs[RF_SELF_TELEPORT_LOCK];
        open[t] = teleport_open(&s);
        if (t == SPEC_TICK) {
            riskfight_execute_command(&s, &ctx, 0, &toggle);
            riskfight_execute_command(&s, &ctx, 0, &attack);
        }
    }
}

int main(void) {
    static float sim_lock[LAST_TICK + 1];
    static int sim_open[LAST_TICK + 1];
    sim_reference(sim_lock, sim_open);

    g_fight.net = NULL;
    g_fight.rng = 0x9e3779b9u;
    g_fight.hard = 1;
    riskfight_deployable = 1;
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99; g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    observer_register_items();
    sim_init(&g_base);
    FILE* in = fopen("ocean/osrs/tests/fixtures/live_swings.requests", "r");
    if (!in) { perror("fixtures/live_swings.requests"); return 2; }
    static Request q;
    RtSeen seen;
    int new_fight, fight_start, obs_only, checked = 0;
    while (rt_parse(in, &q, &seen, &new_fight, &fight_start, &obs_only)) {
        request_declare_priors(&q);
        assert(!server_unavailable(&q));
        int n = q.tick - 412 + 1;
        assert(n <= LAST_TICK);
        assert(q.ours.spec_us == (n <= SPEC_TICK ? 100 : 50));
        RtRecord records[GS_WIRE_MAX_RECORDS];
        int count;
        rt_decide(&q, &seen, new_fight, fight_start, 0, records, &count);
        assert(g_base.state.env.tick == n);
        float obs[RF_OBS_SIZE];
        riskfight_write_observation(&g_base.state, 0, obs);
        const float lock = obs[RF_SELF_TELEPORT_LOCK];
        const int open = teleport_open(&g_base.state);
        if (lock != sim_lock[n] || open != sim_open[n]) {
            fprintf(stderr, "tick %d: server lock %g open %d, sim lock %g open %d\n", n, lock, open, sim_lock[n], sim_open[n]);
            return 1;
        }
        if (n <= SPEC_TICK || n >= SPEC_TICK + OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS) assert(lock == 0 && open);
        else assert(lock == (float)(SPEC_TICK + OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS - n) / OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS && !open);
        checked++;
    }
    assert(checked == LAST_TICK);
    /* A new fight forgets the special. */
    rt_fight_reset(&g_fight, 1000, &seen);
    assert(g_fight.last_spec_tick == -1 && g_fight.previous_spec == -1);
    printf("rl server teleport lock: %d ticks, shut on %d..%d after a special stepped on %d, as the sim\n", checked, SPEC_TICK + 1,
        SPEC_TICK + OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS - 1, SPEC_TICK);
    return 0;
}
