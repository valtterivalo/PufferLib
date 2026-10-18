/* Plays whole sessions of the riskfight env with a trained checkpoint (CPU inference, rf_net.h) and prints the session log: kills, deaths, teleports, what we
 * and he had done and had left when we teleported. The training dashboard shows only a fixed subset of the log; this shows all of it, per session.
 *
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o session_probe ocean/osrs/tests/session_probe.c riskfight_oppfit_bridge.o -lm -lpthread
 *   RF_OPPFIT_MODEL=... RF_OPPFIT_EAT=... session_probe CHECKPOINT HIDDEN LAYERS SESSIONS [argmax|sample] [oppfit_probability] [seed] [session_ticks] [opponent_pool]
 *       [death_price, 0 = drawn] [fight_starts] [price_table] [live_loadout, default 3]
 *
 * The env is the training recipe's (self_play=0, live kits mixed, deployable, motor, domain randomization, price drawn per fight; opponent_pool=1 draws
 * his kit from the pool; fight_starts 1 starts every fight as a recorded one, price_table 1 draws the price from the live table instead of the wide log-uniform).
 * With fight_starts the probe also prints how the fights it played began (our hitpoints, his bar, who had swung, specials, Vengeance), the check that starts vary.
 * live_loadout 3 draws our kit per session from the three Voidwaker kits; 4 draws the Dharok kit half the time, and the probe then also prints every session
 * metric split by our kit (Dharok vs VW), the Dharok play per fight (orb clicks, strips, axe swings with our hitpoints and the full set, maul specials) and the
 * meal-gate check: meals eaten at hitpoints above his chance range (the env's overeats) per fight, 0 under the gate. */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include "rf_net.h"

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: session_probe CHECKPOINT HIDDEN LAYERS SESSIONS [argmax|sample] [oppfit_probability] [seed] [session_ticks] [opponent_pool] [death_price, 0 = drawn] [fight_starts] [price_table] [live_loadout]\n"); return 2; }
    int sessions = atoi(argv[4]);
    int hard = !(argc > 5 && !strcmp(argv[5], "sample"));
    double oppfit = argc > 6 ? atof(argv[6]) : 0.5;
    unsigned seed = argc > 7 ? (unsigned)atoi(argv[7]) : 1;
    double session_ticks = argc > 8 ? atof(argv[8]) : 3000;
    double pool = argc > 9 ? atof(argv[9]) : 0;
    double price = argc > 10 ? atof(argv[10]) : 0;
    double starts = argc > 11 ? atof(argv[11]) : 0;
    double price_table = argc > 12 ? atof(argv[12]) : 0;
    double live_loadout = argc > 13 ? atof(argv[13]) : 3;
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = session_ticks},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = price}, {.key = "live_loadout", .value = live_loadout}, {.key = "deployable", .value = 1},
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
    /* live_loadout 4: the log of each kit's sessions (the kit is drawn once per session, so the session's log goes to the kit worn on its last step), and the
     * Dharok play counted off the state around each fight step. A step that ends a fight into the next one at once (no wait, or the session's end) resets the
     * state inside puf_step: its swing and strip are not seen, and such steps are counted. Kit index 1 is Dharok, 0 the Voidwaker kits. */
    const int split = live_loadout == 4;
    static_assert(sizeof(Log) % sizeof(float) == 0, "Log holds floats only");
    Log kit_log[2] = {0}, session_start = env->log;
    long kit_orbs[2] = {0}, kit_strips[2] = {0}, kit_axe[2] = {0}, kit_axe_full[2] = {0}, kit_unseen[2] = {0};
    double kit_axe_hp[2] = {0};
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
        const int kit = env->context.loadout[0] == &RISKFIGHT_DHAROK_LOADOUT, fight_step = env->wait_left == 0;
        const Player* us = &env->state.env.players[0];
        const int pre_hp = us->current_hitpoints, pre_dressed = us->equipped[GEAR_SLOT_BODY] != ITEM_NONE && us->equipped[GEAR_SLOT_LEGS] != ITEM_NONE;
        const int pre_fights = env->session_fights;
        const float pre_n = env->log.n;
        puf_step(env);
        steps++;
        if (split && fight_step) {
            kit_orbs[kit] += chosen[RF_ORB] != 0;
            if (env->log.n != pre_n || (env->session_fights != pre_fights && env->wait_left == 0)) kit_unseen[kit]++;
            else {
                const RiskfightState* st = &env->state;
                const Player* p = &st->env.players[0];
                kit_strips[kit] += pre_dressed && (p->equipped[GEAR_SLOT_BODY] == ITEM_NONE || p->equipped[GEAR_SLOT_LEGS] == ITEM_NONE);
                /* Our swing of the step's tick: seen by him (a non-instant attack sets last_attack_tick), with the weapon its event records. */
                const int t = st->env.tick - 1;
                if (st->visible[1].last_attack_tick == t && (int)st->visible[1].events[t % RF_HISTORY_TICKS][1] == ITEM_DHAROKS_GREATAXE) {
                    kit_axe[kit]++;
                    kit_axe_hp[kit] += pre_hp;
                    kit_axe_full[kit] += p->equipment_effect_profile.dharok_piece_count == 4;
                }
            }
        }
        if (split && env->log.n != pre_n) {
            float* dst = (float*)&kit_log[kit];
            const float *now = (const float*)&env->log, *then = (const float*)&session_start;
            for (size_t i = 0; i < sizeof(Log) / sizeof(float); i++) dst[i] += now[i] - then[i];
            session_start = env->log;
        }
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
    if (split) {
        static const char* const kit_name[2] = {"VW", "Dharok"};
        for (int k = 1; k >= 0; k--) {
            const Log* g = &kit_log[k];
            const double n = g->n, f = g->rounds;
            if (n == 0) { printf("kit %s: no sessions\n", kit_name[k]); continue; }
            printf("kit %s: sessions %.0f fights %.0f; per session kills %.2f deaths %.2f mutual %.2f net_stake/h %.3f rude %.2f overeats %.2f; fight-end steps unseen %ld\n",
                kit_name[k], n, f, g->kills / n, g->deaths / n, g->mutual_deaths / n, g->net_stake / n, g->rude_teleports / n, g->overeats / n, kit_unseen[k]);
            printf("  meal gate check: meals at hitpoints above his chance range %.3f per fight (%.0f in all; 0 under the gate)%s\n",
                f > 0 ? g->overeats / f : 0.0, g->overeats, g->overeats > 0 ? "  ** GATE BROKEN **" : "");
            if (k == 1)
                printf("  per fight: orb clicks %.3f strips %.3f axe swings %.3f (our hp at the swing %.1f, all four pieces worn %.3f of them) maul specials %.3f\n",
                    f > 0 ? kit_orbs[k] / f : 0.0, f > 0 ? kit_strips[k] / f : 0.0, f > 0 ? kit_axe[k] / f : 0.0, kit_axe[k] ? kit_axe_hp[k] / kit_axe[k] : 0.0,
                    kit_axe[k] ? (double)kit_axe_full[k] / kit_axe[k] : 0.0, f > 0 ? g->spec_maul / f : 0.0);
        }
    }
    for (size_t i = 0; i < out.size; i++) printf("log %s %.4f\n", out.items[i].key, out.items[i].value / (sessions > 0 ? sessions : 1));
    puf_close(env);
    return 0;
}
