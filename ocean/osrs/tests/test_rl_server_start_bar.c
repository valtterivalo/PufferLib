/* The RL server's first observation of a fight shows his bar as the sim's first observation of the same start does (riskfight_start_apply, fight_starts as trained):
 * for every recorded start, the bar the client shows on that tick (the row's bar, hidden when -1) fed through rt_fight_reset and rt_observe gives the same
 * opponent bar and the same bar in the newest history event as the sim. The rows that show a bar with no hit on him that tick are the ones a full bar got wrong. */
#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"

int main(void) {
    observer_register_items();
    static Sim sim;
    sim_init(&sim);
    for (int i = 0; i < 2; i++) sim.context.loadout[i] = &RISKFIGHT_LIVE_LOADOUT;
    static Request q;
    const int bar_at = RF_OPPONENT_START + NUM_GEAR_SLOTS, history_bar_at = RF_HISTORY_START + 7;
    int shown_unhit = 0;
    for (int i = 0; i < RF_START_RECORD_COUNT; i++) {
        const RiskfightStartRecord* r = &RF_START_RECORDS[i];
        RiskfightState* s = &sim.state;
        riskfight_reset((EncounterState*)s, (EncounterContext*)&sim.context, osrs_lowbias32((uint32_t)i + 1) | 1);
        uint32_t rng = osrs_lowbias32((uint32_t)i + 77) | 1;
        riskfight_start_apply(s, &sim.context, r, 0, &rng);
        assert(s->env.tick == 1);
        static float sim_obs[RF_OBS_SIZE], server_obs[RF_OBS_SIZE];
        riskfight_write_observation(s, 0, sim_obs);

        /* The client on the start tick: his bar as the row read it, a hitsplat on him when one landed. */
        const RtSeen seen = {-1, 0, r->landed_him >= 0, r->landed_him >= 0 ? r->landed_him : 0, 0, 0, r->bar, r->bar >= 0 ? OSRS_PLAYER_HEALTH_BAR_SCALE : -1, 0, 0};
        static RtFight f;
        memset(&f, 0, sizeof(f));
        rt_fight_reset(&f, 100, &seen);
        rt_observe(&f.vis, &seen, 1, &q);
        const RiskfightVisibleOpponent sim_visible = s->visible[0];
        s->visible[0] = f.vis;
        riskfight_write_observation(s, 0, server_obs);
        s->visible[0] = sim_visible;

        if (server_obs[bar_at] != sim_obs[bar_at] || server_obs[history_bar_at] != sim_obs[history_bar_at]) {
            fprintf(stderr, "start %d (bar %d, landed on him %d): server bar %g history %g, sim %g %g\n", i, r->bar, r->landed_him, server_obs[bar_at], server_obs[history_bar_at],
                sim_obs[bar_at], sim_obs[history_bar_at]);
            return 1;
        }
        shown_unhit += r->bar >= 0 && r->bar < OSRS_PLAYER_HEALTH_BAR_SCALE && r->landed_him < 0;
    }
    assert(shown_unhit > 0);
    printf("start bar: %d starts, the server's first bar equals the sim's (%d show a partial bar with no hit on him that tick)\n", RF_START_RECORD_COUNT, shown_unhit);
    return 0;
}
