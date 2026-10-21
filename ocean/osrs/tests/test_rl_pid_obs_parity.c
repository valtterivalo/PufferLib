/* The PID block (RF_PID_START) live and in the sim, on the same fight: a sim fight (his greataxe swings, or Voidwaker specials, PID drawn each tick) is replayed
 * tick by tick into the RL server's own visible-opponent path (rt_observe) as the V and B lines the plugin would send for it: his swing's raw id, style and
 * special count, the hitsplats on him, his bar, and the PID reading (owner and flip odds; the plugin's Pid.java pairs melee animations with hitsplats by the
 * rule riskfight_pid_seen implements, so the sim viewer's reading stands in for the B line). On every tick the server's observation and the sim's must carry
 * the same PID block, the same newest history row and the same estimate of his special energy. */
#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"

static RiskfightState sim, live;
static RiskfightContext ctx;

static void fight(uint16_t weapon, int spec, uint32_t seed) {
    riskfight_reset((EncounterState*)&sim, (EncounterContext*)&ctx, seed);
    for (int side = 0; side < 2; side++) {
        Player* p = &sim.env.players[side];
        p->veng_active = 0;
        p->equipped[GEAR_SLOT_WEAPON] = side == 1 ? weapon : ITEM_VOIDWAKER;
        p->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        osrs_refresh_player_equipment(p);
        p->num_pending_hits = 0;
        osrs_interaction_clear(&p->interaction);
    }
    sim.env.players[1].x = sim.env.players[0].x + 1;
    sim.env.players[1].y = sim.env.players[0].y;
    /* The reset's kit wore a shield the viewer saw; the fight starts from the gear above, as a new live fight does (rt_fight_reset: no memory). */
    for (int k = 0; k < 2; k++) memset(&sim.visible[k].threat, 0, sizeof(sim.visible[k].threat));
    riskfight_observe_visible(&sim, 0, 1);
    riskfight_observe_visible(&sim, 1, 1);
    const RtSeen start = {-1, 0, 0, 0, 0, 0, -1, -1, 0, 0, {0, 0, 0}};
    rt_fight_reset(&g_fight, 0, &start);
    HumanCommand attack = {.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 0}, toggle = {.kind = HUMAN_COMMAND_SPEC_TOGGLE};
    HumanCommand ours_attack = {.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1};
    int swings = 0, in_flight = 0, specs = 0;
    for (int t = 0; t < 60 && !sim.env.episode_over; t++) {
        sim.env.players[0].current_hitpoints = sim.env.players[1].current_hitpoints = 99; /* nobody dies: the fight is about the clocks */
        HumanCommand his[2];
        int m = 0;
        if (t == 0) his[m++] = attack;
        if (spec && sim.env.players[1].special_energy >= 50 && !sim.env.players[1].spec_armed) his[m++] = toggle;
        HumanCommandQueue a = {.items = &ours_attack, .count = t == 0}, b = {.items = his, .count = m};
        riskfight_step_queues(&sim, &ctx, &a, &b);
        const RiskfightVisibleOpponent* v = &sim.visible[0];
        const Player* him = &sim.env.players[1];
        const int T = sim.env.tick - 1, n = T + 1;
        const float* event = v->events[T % RF_HISTORY_TICKS];
        RtSeen seen = {-1, 0, (int)event[3], (int)event[4], (int)event[5], (int)event[6], v->health_bar, OSRS_PLAYER_HEALTH_BAR_SCALE,
            him->said_taste_vengeance_this_tick, 0, {0, 0, 0}};
        if (v->last_swing_tick == T) {
            seen.swing_raw = ITEM_DATABASE[v->last_swing_item].item_id;
            seen.swing_style = (int)event[2];
            seen.swing_spec = v->last_spec_tick == T;
        }
        static Request q;
        memset(&q, 0, sizeof(q));
        q.belief.pid = v->pid_reading < 0 ? GS_PID_UNKNOWN : v->pid_reading == 0 ? GS_PID_LOCAL : GS_PID_OPPONENT;
        q.belief.flip = v->pid_flip;
        rt_observe(&g_fight.vis, &seen, n, &q);
        live = sim;
        RiskfightVisibleOpponent* lv = &live.visible[0];
        *lv = g_fight.vis;
        memcpy(lv->equipment, v->equipment, sizeof(lv->equipment));
        lv->x = v->x; lv->y = v->y; lv->interacting = v->interacting;
        lv->threat.valid = 0;
        riskfight_threat_refresh(&lv->threat, lv->equipment, n);
        float sim_obs[RF_OBS_SIZE], live_obs[RF_OBS_SIZE];
        riskfight_write_observation(&sim, 0, sim_obs);
        riskfight_write_observation(&live, 0, live_obs);
        for (int i = 0; i < RF_PID_SIZE; i++)
            if (sim_obs[RF_PID_START + i] != live_obs[RF_PID_START + i]) {
                fprintf(stderr, "tick %d PID block [%d]: sim %g live %g\n", T, i, sim_obs[RF_PID_START + i], live_obs[RF_PID_START + i]);
                for (int k = 0; k < 2; k++) {
                    const RiskfightVisibleOpponent* w = k ? lv : v;
                    const float* o = (k ? live_obs : sim_obs) + RF_THREAT_START;
                    fprintf(stderr, "%s count %d items %d %d %d bar %d spec_est %d | slot0 max %g spec %g\n", k ? "live" : "sim", w->threat.count, w->threat.slots[0].item,
                        w->threat.slots[1].item, w->threat.slots[2].item, w->health_bar, w->spec_est, o[2] * 128, o[9] * 128);
                }
                exit(1);
            }
        assert(!memcmp(sim_obs + RF_HISTORY_START, live_obs + RF_HISTORY_START, RF_EVENT_WIDTH * sizeof(float)));
        assert(sim_obs[RF_SESSION_START + 1] == live_obs[RF_SESSION_START + 1]);
        swings += v->last_attack_tick == T;
        in_flight += sim_obs[RF_PID_START + RF_PID_IN_FLIGHT] > 0.5f;
        specs += sim_obs[RF_PID_START + RF_PID_IN_FLIGHT_SPEC] > 0;
    }
    assert(swings >= 4 && in_flight >= 1 && (specs > 0) == spec);
    printf("%s: %d swings, %d read in flight, %d specials: PID block identical live and sim\n", spec ? "Voidwaker specials" : "greataxe", swings, in_flight, specs);
}

int main(void) {
    observer_register_items();
    g_fight.net = NULL;
    for (uint32_t seed = 1; seed <= 3; seed++) {
        fight(ITEM_DHAROKS_GREATAXE, 0, seed);
        fight(ITEM_VOIDWAKER, 1, seed);
    }
    return 0;
}
