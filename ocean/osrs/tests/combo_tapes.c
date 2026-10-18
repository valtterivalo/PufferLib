/* His hit + instant maul special combo: what the visible-opponent model knows of it, tick by tick, on recorded request streams (scripts/measure_combos.py).
 *
 *   cc -std=gnu11 -O2 -I. -Isrc -Wno-comment -o /tmp/combo_tapes ocean/osrs/tests/combo_tapes.c -lm -lpthread       (repository root; run with cwd = repository root)
 *   /tmp/combo_tapes REQUESTS...            (the streams scripts/measure_overeats.py writes)
 *
 * Every request file is replayed through the live server's own path (rt_parse, rt_decide with no net), as overeat_tapes.c does. After each tick one row, tab separated:
 *   file fight tick hp spec_est maul_p maul_cost maul_max opening_max
 * hp: our exact hitpoints after the tick; spec_est: his estimated special energy after it; maul_p: the threat block's probability that he carries a granite maul
 * (1 wielded or swung, below 1 the pool's prior for a maul hidden in the bag, 0 none), maul_cost / maul_max: its special's cost and most damage against us; opening_max:
 * the largest normal max hit of the weapons he wields or has swung (certain slots), the opening hit of a combo. */
#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"

int main(int argc, char** argv) {
    g_fight.net = NULL;
    g_fight.rng = 0x9e3779b9u;
    g_fight.hard = 1;
    riskfight_deployable = 1;
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99; g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    observer_register_items();
    sim_init(&g_base);
    static Request q;
    for (int a = 1; a < argc; a++) {
        FILE* in = fopen(argv[a], "r");
        if (!in) { perror(argv[a]); return 2; }
        RtSeen seen;
        int new_fight, fight_start, obs_only, fight_index = -1, last_start = -1;
        while (rt_parse(in, &q, &seen, &new_fight, &fight_start, &obs_only)) {
            request_declare_priors(&q);
            const char* unavailable = server_unavailable(&q);
            if (unavailable) {
                fprintf(stderr, "UNAVAILABLE %s %d %s\n", argv[a], q.tick, unavailable);
                continue;
            }
            RtRecord records[GS_WIRE_MAX_RECORDS];
            int count;
            rt_decide(&q, &seen, new_fight, fight_start, obs_only, records, &count);
            RiskfightState* s = &g_base.state;
            const RiskfightVisibleOpponent* v = &s->visible[0];
            if (g_fight.start_tick != last_start) fight_index++;
            RiskfightBagWeapons none = {{0}, 0};
            float threat[RF_THREAT_SIZE];
            riskfight_threat_write(&s->env.players[0], &none, &v->threat, v->health_bar, threat);
            float maul_p = 0;
            int maul_cost = 0, maul_max = 0, opening_max = 0;
            for (int i = 0; i < RF_THREAT_HIS_SLOTS; i++) {
                const float* o = threat + i * RF_THREAT_HIS_WIDTH;
                if (o[0] <= 0) continue;
                int cost = (int)lroundf(o[8] * 100), spec = (int)lroundf(o[9] * RF_THREAT_DAMAGE_SCALE);
                if (cost > 0 && lroundf(o[11] * RF_THREAT_SPEED_SCALE) == 1 && o[0] > maul_p) { maul_p = o[0]; maul_cost = cost; maul_max = spec; }
                int hit = (int)lroundf(o[2] * RF_THREAT_DAMAGE_SCALE);
                if (o[0] >= 1 && hit > opening_max) opening_max = hit;
            }
            printf("%s\t%d\t%d\t%d\t%d\t%.3f\t%d\t%d\t%d\n", argv[a], fight_index, q.tick, s->env.players[0].current_hitpoints, v->spec_est, maul_p, maul_cost,
                maul_max, opening_max);
            last_start = g_fight.start_tick;
        }
        fclose(in);
    }
    return 0;
}
