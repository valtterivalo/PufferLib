/* Plays whole sessions of the riskfight env with a trained checkpoint (CPU inference, rf_net.h) and prints the session log: kills, deaths, teleports, what we
 * and he had done and had left when we teleported. The training dashboard shows only a fixed subset of the log; this shows all of it, per session.
 *
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o session_probe ocean/osrs/tests/session_probe.c riskfight_oppfit_bridge.o -lm -lpthread
 *   RF_OPPFIT_MODEL=... RF_OPPFIT_EAT=... session_probe CHECKPOINT HIDDEN LAYERS SESSIONS [argmax|sample] [oppfit_probability] [seed] [session_ticks] [opponent_pool]
 *       [death_price, 0 = drawn] [fight_starts] [price_table]
 *
 * The env is the training recipe's (self_play=0, live kits mixed, deployable, motor, domain randomization, price drawn per fight; opponent_pool=1 draws
 * his kit from the pool; fight_starts 1 starts every fight as a recorded one, price_table 1 draws the price from the live table instead of the wide log-uniform).
 * With fight_starts the probe also prints how the fights it played began (our hitpoints, his bar, who had swung, specials, Vengeance), the check that starts vary. */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include "rf_net.h"

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: session_probe CHECKPOINT HIDDEN LAYERS SESSIONS [argmax|sample] [oppfit_probability] [seed] [session_ticks] [opponent_pool] [death_price, 0 = drawn] [fight_starts] [price_table]\n"); return 2; }
    int sessions = atoi(argv[4]);
    int hard = !(argc > 5 && !strcmp(argv[5], "sample"));
    double oppfit = argc > 6 ? atof(argv[6]) : 0.5;
    unsigned seed = argc > 7 ? (unsigned)atoi(argv[7]) : 1;
    double session_ticks = argc > 8 ? atof(argv[8]) : 3000;
    double pool = argc > 9 ? atof(argv[9]) : 0;
    double price = argc > 10 ? atof(argv[10]) : 0;
    double starts = argc > 11 ? atof(argv[11]) : 0;
    double price_table = argc > 12 ? atof(argv[12]) : 0;
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = session_ticks},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = price}, {.key = "live_loadout", .value = 3}, {.key = "deployable", .value = 1},
        {.key = "motor", .value = 1}, {.key = "domain_randomization", .value = 1}, {.key = "oppfit_probability", .value = oppfit}, {.key = "opponent_pool", .value = pool},
        {.key = "fight_starts", .value = starts}, {.key = "price_table", .value = price_table}};
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
    /* The fights as they began: the first observation of a fight is at tick 1 after a recorded start, 0 after the regear. */
    const int start_tick = starts > 0 ? 1 : 0;
    long began = 0, hp_count[122] = {0}, bar_count[31] = {0}, opened[3] = {0}, spec_used = 0, veng_lost = 0, hit_landed = 0, in_flight = 0;
    double price_sum = 0, price_lo = 9, price_hi = 0;
    while (env->log.n < sessions) {
        if (env->wait_left == 0 && env->state.env.tick == start_tick && !env->state.env.episode_over) {
            const RiskfightState* st = &env->state;
            began++;
            hp_count[st->env.players[0].current_hitpoints]++;
            bar_count[st->visible[0].health_bar]++;
            const int us_swung = st->visible[1].last_attack_tick == start_tick - 1 && start_tick > 0, him_swung = st->visible[0].last_attack_tick == start_tick - 1 && start_tick > 0;
            if (us_swung && him_swung) opened[2]++;
            else if (him_swung) opened[1]++;
            else if (us_swung) opened[0]++;
            spec_used += st->env.players[0].special_energy < 100 || st->env.players[1].special_energy < 100;
            veng_lost += !st->env.players[0].veng_active;
            hit_landed += st->env.players[0].hit_landed_this_tick || st->env.players[1].hit_landed_this_tick;
            in_flight += st->env.players[0].num_pending_hits + st->env.players[1].num_pending_hits > 0;
            price_sum += st->death_price; price_lo = price_lo < st->death_price ? price_lo : st->death_price; price_hi = price_hi > st->death_price ? price_hi : st->death_price;
        }
        int chosen[RF_HEADS];
        rf_net_act(net, obs, mask, &rng, hard, RF_ACTION_DIMS, RF_HEADS, chosen);
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)chosen[h];
        puf_step(env);
        steps++;
        if (terminal) rf_net_reset(net);
    }
    Dict out = {0};
    puf_log(&env->log, &out);
    printf("sessions %d steps %ld mode %s oppfit %.2f session_ticks %.0f pool %.0f; %.1f%% of the steps are waits between fights\n", sessions, steps,
        hard ? "argmax" : "sample", oppfit, session_ticks, pool, 100.0 * env->log.wait_ticks / steps);
    const Log* l = &env->log;
    double fights = l->rounds, tp = l->self_teleports;
    printf("per session: fights %.2f kills %.2f deaths %.2f mutual %.2f our_teleports %.2f his_teleports %.2f timeouts %.2f rematches %.2f rude %.2f ragged %.2f overeats %.2f (%.3f per fight) annoyed_exits %.2f net_stake/h %.3f\n",
        fights / sessions, l->kills / sessions, l->deaths / sessions, l->mutual_deaths / sessions, tp / sessions, l->opponent_teleports / sessions,
        l->timeouts / sessions, l->rematches / sessions, l->rude_teleports / sessions, l->ragged / sessions, l->overeats / sessions, l->overeats / (fights > 0 ? fights : 1), l->annoyed_exits / sessions, l->net_stake / sessions);
    printf("fight length %.1f ticks\n", l->episode_length / (fights > 0 ? fights : 1));
    if (tp > 0) {
        printf("at our teleports (n=%.0f): our special used %.3f, his used %.3f, both %.3f, his energy could spec %.3f, we could not spec any more %.3f, rude %.3f\n", tp,
            l->tp_self_spec / tp, l->tp_opp_spec / tp, l->tp_both_spec / tp, l->tp_opp_could_spec / tp, l->tp_self_no_special / tp, l->rude_teleports / tp);
        printf("  supply classes out at our teleports: hard food %.3f, prayer restores %.3f, combat boost %.3f; his rude teleports per session %.3f\n",
            l->tp_no_hard_food / tp, l->tp_no_restore / tp, l->self_escape_no_boost / tp, l->opponent_rude_teleports / sessions);
        printf("  supplies left: hp %.1f, eat items %.2f, potion doses %.2f, fight tick %.1f; healing class none %.3f other %.3f double %.3f triple %.3f, no boost %.3f\n",
            l->tp_hp / tp, l->tp_food / tp, l->tp_potion_doses / tp, l->tp_fight_tick / tp, l->self_escape_healing[0] / tp, l->self_escape_healing[1] / tp,
            l->self_escape_healing[2] / tp, l->self_escape_healing[3] / tp, l->self_escape_no_boost / tp);
    }
    if (starts > 0) {
        long distinct_hp = 0, distinct_bar = 0, below_120 = 0;
        for (int h = 0; h < 122; h++) { distinct_hp += hp_count[h] > 0; below_120 += h < 120 ? hp_count[h] : 0; }
        for (int b = 0; b < 31; b++) distinct_bar += bar_count[b] > 0;
        printf("starts: %ld fights began; our hitpoints %ld distinct values, 121: %.3f, 120: %.3f, below 120: %.3f; his bar %ld distinct values, shown below full %.3f; opened us %ld him %ld both %ld;\n"
            "  a special used %.3f, our Vengeance gone %.3f, a hit landed on the first tick %.3f, hits in flight %.3f; price mean %.3f (%.3f .. %.3f)\n",
            began, distinct_hp, (double)hp_count[121] / began, (double)hp_count[120] / began, (double)below_120 / began, distinct_bar, 1.0 - (double)(bar_count[30] + bar_count[0]) / began,
            opened[0], opened[1], opened[2], (double)spec_used / began, (double)veng_lost / began, (double)hit_landed / began, (double)in_flight / began, price_sum / began, price_lo, price_hi);
    }
    printf("share of fights ending in: kill %.3f death %.3f teleport %.3f\n", l->kills / fights, (l->deaths + l->mutual_deaths) / fights, tp / fights);
    for (size_t i = 0; i < out.size; i++) printf("log %s %.4f\n", out.items[i].key, out.items[i].value / (sessions > 0 ? sessions : 1));
    puf_close(env);
    return 0;
}
