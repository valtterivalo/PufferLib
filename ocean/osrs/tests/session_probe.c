/* Plays whole sessions of the riskfight env with a trained checkpoint (CPU inference, rf_net.h) and prints the session log: kills, deaths, teleports, what we
 * and he had done and had left when we teleported. The training dashboard shows only a fixed subset of the log; this shows all of it, per session.
 *
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o session_probe ocean/osrs/tests/session_probe.c riskfight_oppfit_bridge.o -lm -lpthread
 *   RF_OPPFIT_MODEL=... RF_OPPFIT_EAT=... session_probe CHECKPOINT HIDDEN LAYERS SESSIONS [argmax|sample] [oppfit_probability] [seed] [session_ticks]
 *
 * The env is the training recipe's (self_play=0, live kits mixed, deployable, motor, domain randomization, price drawn per fight). */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include "rf_net.h"

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: session_probe CHECKPOINT HIDDEN LAYERS SESSIONS [argmax|sample] [oppfit_probability] [seed] [session_ticks]\n"); return 2; }
    int sessions = atoi(argv[4]);
    int hard = !(argc > 5 && !strcmp(argv[5], "sample"));
    double oppfit = argc > 6 ? atof(argv[6]) : 0.5;
    unsigned seed = argc > 7 ? (unsigned)atoi(argv[7]) : 1;
    double session_ticks = argc > 8 ? atof(argv[8]) : 3000;
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = session_ticks},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = 0}, {.key = "live_loadout", .value = 3}, {.key = "deployable", .value = 1},
        {.key = "motor", .value = 1}, {.key = "domain_randomization", .value = 1}, {.key = "oppfit_probability", .value = oppfit}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    Env* env = (Env*)calloc(1, sizeof(*env));
    env->rng = seed;
    puf_init(env, &kwargs);
    RfNet* net = rf_net_load(argv[1], RF_OBS_SIZE, atoi(argv[2]), atoi(argv[3]), RF_ACTION_DIMS, RF_HEADS);
    static float obs[RF_OBS_SIZE], actions[RF_HEADS], reward, terminal;
    static unsigned char mask[RF_MASK_SIZE];
    env->agents[0] = (Agent){.observations = obs, .actions = actions, .rewards = &reward, .terminals = &terminal, .action_mask = mask, .policy = 0};
    uint32_t rng = 0x9e3779b9u ^ seed;
    puf_reset(env);
    rf_net_reset(net);
    long steps = 0;
    while (env->log.n < sessions) {
        int chosen[RF_HEADS];
        rf_net_act(net, obs, mask, &rng, hard, RF_ACTION_DIMS, RF_HEADS, chosen);
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)chosen[h];
        puf_step(env);
        steps++;
        if (terminal) rf_net_reset(net);
    }
    Dict out = {0};
    puf_log(&env->log, &out);
    printf("sessions %d steps %ld mode %s oppfit %.2f session_ticks %.0f\n", sessions, steps, hard ? "argmax" : "sample", oppfit, session_ticks);
    const Log* l = &env->log;
    double fights = l->rounds, tp = l->self_teleports;
    printf("per session: fights %.2f kills %.2f deaths %.2f mutual %.2f our_teleports %.2f his_teleports %.2f timeouts %.2f rematches %.2f rude %.2f net_stake/h %.3f\n",
        fights / sessions, l->kills / sessions, l->deaths / sessions, l->mutual_deaths / sessions, tp / sessions, l->opponent_teleports / sessions,
        l->timeouts / sessions, l->rematches / sessions, l->rude_teleports / sessions, l->net_stake / sessions);
    printf("fight length %.1f ticks\n", l->episode_length / (fights > 0 ? fights : 1));
    if (tp > 0) {
        printf("at our teleports (n=%.0f): our special used %.3f, his used %.3f, both %.3f, his energy could spec %.3f, we could not spec any more %.3f, rude %.3f\n", tp,
            l->tp_self_spec / tp, l->tp_opp_spec / tp, l->tp_both_spec / tp, l->tp_opp_could_spec / tp, l->tp_self_no_special / tp, l->rude_teleports / tp);
        printf("  supplies left: hp %.1f, eat items %.2f, potion doses %.2f, fight tick %.1f; healing class none %.3f other %.3f double %.3f triple %.3f, no boost %.3f\n",
            l->tp_hp / tp, l->tp_food / tp, l->tp_potion_doses / tp, l->tp_fight_tick / tp, l->self_escape_healing[0] / tp, l->self_escape_healing[1] / tp,
            l->self_escape_healing[2] / tp, l->self_escape_healing[3] / tp, l->self_escape_no_boost / tp);
    }
    printf("share of fights ending in: kill %.3f death %.3f teleport %.3f\n", l->kills / fights, (l->deaths + l->mutual_deaths) / fights, tp / fights);
    for (size_t i = 0; i < out.size; i++) printf("log %s %.4f\n", out.items[i].key, out.items[i].value / (sessions > 0 ? sessions : 1));
    puf_close(env);
    return 0;
}
