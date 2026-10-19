/* Eat or skip while he is low: a checkpoint's meal choice at our low hitpoints against the outcome of each branch. The env is [bot_eval]'s (our kit drawn per
 * session as in training, live_loadout 4; pool opponents played by the script, oppfit 0; price drawn from the live table, price_table 1; recorded fight starts).
 * A decision counts when he is below 40 hitpoints, our hitpoints are 1-60 and at most his chance range (riskfight_chance_range), and the mask offers a meal
 * (food or combo slot). At each one the state is cloned: one branch forces a meal (the first allowed food slot, plus the combo slot when allowed), the other
 * forces none (food, combo and drink cleared); the net's other heads are kept, and the net then plays the rest of the fight argmax in both. Each branch is
 * played ROLLS times, roll r > 0 with the combat dice reseeded (OsrsEnv.rng_state), the same dice for both branches of a roll (paired).
 *
 *   probe_lowhp_meals CKPT HIDDEN LAYERS SESSIONS [seed] [rolls, default 6]
 *
 * Per our hitpoints band (<= 20, 21-40, 41-60) it prints:
 *  - pooled over the band's decisions and rolls: n; the net's greedy meal rate (rf_net_act hard: the argmax slot of each head, a meal when the food, combo or
 *    drink head picks a slot); KO and death shares of the fight under each branch; the pooled break-even price (KO gain over extra deaths of skipping: "skip
 *    below P" when skipping gains kills for deaths). These are band averages: they say nothing of any one decision.
 *  - per decision: the forced meal's benefit at the decision's own price, d = (KO - price x death | meal) - (same | none), its mean over the rolls; the band's
 *    mean d with its standard error over decisions; the decisions where the meal is better (mean d over 2 standard errors of its rolls above 0), worse (below),
 *    or not told apart by ROLLS rolls; the greedy meal rate in each; and of the decisions told apart, the share where greedy picks the better branch.
 * Build as the suite builds osrs_riskfight.h users:
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o probe_lowhp_meals ocean/osrs/tests/probe_lowhp_meals.c riskfight_oppfit_bridge.o -lm -lpthread */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include "rf_net.h"

enum { BANDS = 3, BRANCHES = 2, MAX_DECISIONS = 1 << 20 };
static float obs[RF_OBS_SIZE], obs_saved[RF_OBS_SIZE], actions[RF_HEADS], reward, terminal, reset_flag;
static unsigned char mask[RF_MASK_SIZE], mask_saved[RF_MASK_SIZE];

typedef struct { double n, kill, death; } Outcome;
typedef struct { unsigned char band, greedy_eats; float price; double d_sum, d_sq; } Decision;
static Decision decisions[MAX_DECISIONS];

static int first_allowed(int head) {
    for (int i = 1; i < RF_ACTION_DIMS[head]; i++) if (mask[RF_HEAD_OFFSET[head] + i]) return i;
    return 0;
}

/** Plays branch `force` (0 meal, 1 no meal) from the current state to the end of this fight and counts its outcome. */
static void play_out(Env* env, RfNet* net, uint32_t* rng, int force, const int* greedy, Outcome* out) {
    const Log before = env->log;
    const int fights = env->session_fights;
    int chosen[RF_HEADS];
    memcpy(chosen, greedy, sizeof(chosen));
    if (force == 0) {
        int food = first_allowed(RF_FOOD), combo = first_allowed(RF_COMBO);
        if (food) chosen[RF_FOOD] = food;
        if (combo) chosen[RF_COMBO] = combo;
    } else chosen[RF_FOOD] = chosen[RF_COMBO] = chosen[RF_DRINK] = 0;
    for (int step = 0; env->session_fights == fights && env->log.n == before.n; step++) {
        assert(step < 5000);
        if (step) rf_net_act(net, obs, mask, rng, 1, RF_ACTION_DIMS, RF_HEADS, chosen);
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)chosen[h];
        puf_step(env);
        if (terminal || reset_flag) rf_net_reset(net);
    }
    out->n++;
    out->kill += env->log.kills - before.kills;
    out->death += env->log.deaths + env->log.mutual_deaths - before.deaths - before.mutual_deaths;
}

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: probe_lowhp_meals CKPT HIDDEN LAYERS SESSIONS [seed] [rolls]\n"); return 2; }
    const int sessions = atoi(argv[4]);
    const unsigned seed = argc > 5 ? (unsigned)atoi(argv[5]) : 1;
    const int rolls = argc > 6 ? atoi(argv[6]) : 6;
    if (rolls < 2) { fprintf(stderr, "probe_lowhp_meals: rolls must be at least 2 (a decision's spread)\n"); return 2; }
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = 3000},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = 0}, {.key = "price_table", .value = 1}, {.key = "live_loadout", .value = 4},
        {.key = "deployable", .value = 1}, {.key = "motor", .value = 1}, {.key = "domain_randomization", .value = 1}, {.key = "oppfit_probability", .value = 0},
        {.key = "opponent_pool", .value = 1}, {.key = "fight_starts", .value = 1}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    Env* env = calloc(1, sizeof(*env));
    Env* saved = calloc(1, sizeof(*saved));
    env->rng = seed;
    puf_init(env, &kwargs);
    RfNet* net = rf_net_load(argv[1], RF_OBS_SIZE, atoi(argv[2]), atoi(argv[3]), RF_ACTION_DIMS, RF_HEADS);
    RfNet* net_saved = malloc(sizeof(*net_saved));
    env->agents[0] = (Agent){.observations = obs, .actions = actions, .rewards = &reward, .terminals = &terminal, .resets = &reset_flag, .action_mask = mask, .policy = 0};
    uint32_t rng = 0x9e3779b9u ^ seed;
    puf_reset(env);
    rf_net_reset(net);
    Outcome branch[BANDS][BRANCHES] = {{{0}}};
    long count = 0;
    while (env->log.n < sessions) {
        int greedy[RF_HEADS];
        rf_net_act(net, obs, mask, &rng, 1, RF_ACTION_DIMS, RF_HEADS, greedy);
        const RiskfightState* st = &env->state;
        if (env->wait_left == 0 && !st->env.episode_over) {
            const int hp = st->env.players[0].current_hitpoints;
            if (hp > 0 && hp <= 60 && st->env.players[1].current_hitpoints < 40 && hp <= riskfight_chance_range(st, 0) &&
                (first_allowed(RF_FOOD) || first_allowed(RF_COMBO))) {
                assert(count < MAX_DECISIONS);
                const int band = hp <= 20 ? 0 : hp <= 40 ? 1 : 2;
                Decision* dec = &decisions[count++];
                *dec = (Decision){band, greedy[RF_FOOD] || greedy[RF_COMBO] || greedy[RF_DRINK], st->death_price, 0, 0};
                /* The clone shares the command queues' heap buffers: grow them first so no branch reallocates a buffer the saved copy still points at. */
                for (int seat = 0; seat < 2; seat++) human_command_queue_reserve(&env->context.policy_commands[seat].commands, 256);
                memcpy(saved, env, sizeof(*env));
                memcpy(net_saved, net, sizeof(*net));
                memcpy(obs_saved, obs, sizeof(obs));
                memcpy(mask_saved, mask, sizeof(mask));
                for (int r = 0; r < rolls; r++) {
                    double value[BRANCHES];
                    for (int b = 0; b < BRANCHES; b++) {
                        if (r > 0) env->state.env.rng_state = osrs_lowbias32(saved->state.env.rng_state ^ (0x5bd1e995u * (uint32_t)r));
                        Outcome one = {0};
                        uint32_t branch_rng = rng;
                        play_out(env, net, &branch_rng, b, greedy, &one);
                        branch[band][b].n += one.n;
                        branch[band][b].kill += one.kill;
                        branch[band][b].death += one.death;
                        value[b] = one.kill - dec->price * one.death;
                        memcpy(env, saved, sizeof(*env));
                        memcpy(net, net_saved, sizeof(*net));
                        memcpy(obs, obs_saved, sizeof(obs));
                        memcpy(mask, mask_saved, sizeof(mask));
                    }
                    const double d = value[0] - value[1];
                    dec->d_sum += d;
                    dec->d_sq += d * d;
                }
            }
        }
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)greedy[h];
        puf_step(env);
        if (terminal || reset_flag) rf_net_reset(net);
    }
    static const char* const names[BANDS] = {"<=20", "21-40", "41-60"};
    printf("%s, %d sessions, seed %u, %d rolls: decisions with him < 40, our hp <= his range and <= 60, a meal allowed\n", argv[1], sessions, seed, rolls);
    printf("pooled: our hp | n | greedy eats | eat KO / death | skip KO / death | pooled break-even price\n");
    for (int band = 0; band < BANDS; band++) {
        const Outcome* e = &branch[band][0];
        const Outcome* k = &branch[band][1];
        if (!e->n) { printf("%s | 0\n", names[band]); continue; }
        const double ko_e = e->kill / e->n, d_e = e->death / e->n, ko_k = k->kill / k->n, d_k = k->death / k->n;
        long n = 0, eats = 0;
        for (long i = 0; i < count; i++) if (decisions[i].band == band) { n++; eats += decisions[i].greedy_eats; }
        const double gain = ko_k - ko_e, extra = d_k - d_e;
        char even[32];
        if (gain > 0 && extra > 0) snprintf(even, sizeof(even), "skip below %.2f", gain / extra);
        else if (gain < 0 && extra < 0) snprintf(even, sizeof(even), "skip above %.2f", gain / extra);
        else snprintf(even, sizeof(even), "%s", gain >= 0 && extra <= 0 ? "skip always" : "eat always");
        printf("%s | %ld | %.3f | %.3f / %.3f | %.3f / %.3f | %s\n", names[band], n, (double)eats / n, ko_e, d_e, ko_k, d_k, even);
    }
    printf("per decision: our hp | mean meal benefit d (se) | meal better / worse / not told apart | greedy eats in each | greedy picks the better, of those told apart\n");
    for (int band = 0; band < BANDS; band++) {
        long n = 0, cls[3] = {0}, cls_eats[3] = {0}, right = 0;
        double sum = 0, sq = 0;
        for (long i = 0; i < count; i++) {
            const Decision* x = &decisions[i];
            if (x->band != band) continue;
            const double mean = x->d_sum / rolls, var = (x->d_sq - rolls * mean * mean) / (rolls - 1);
            const double se = sqrt(var > 0 ? var / rolls : 0);
            const int c = se == 0 ? (mean > 0 ? 0 : mean < 0 ? 1 : 2) : mean > 2 * se ? 0 : mean < -2 * se ? 1 : 2;
            n++;
            sum += mean;
            sq += mean * mean;
            cls[c]++;
            cls_eats[c] += x->greedy_eats;
            right += (c == 0 && x->greedy_eats) || (c == 1 && !x->greedy_eats);
        }
        if (!n) continue;
        const double mean = sum / n, se = sqrt((sq / n - mean * mean) / (n > 1 ? n - 1 : 1));
        char rate[3][16], told[16];
        for (int c = 0; c < 3; c++) snprintf(rate[c], sizeof(rate[c]), cls[c] ? "%.3f" : "-", cls[c] ? (double)cls_eats[c] / cls[c] : 0.0);
        snprintf(told, sizeof(told), cls[0] + cls[1] ? "%.3f" : "-", cls[0] + cls[1] ? (double)right / (cls[0] + cls[1]) : 0.0);
        printf("%s | %+.3f (%.3f) | %.3f / %.3f / %.3f | %s / %s / %s | %s\n", names[band], mean, se, (double)cls[0] / n, (double)cls[1] / n, (double)cls[2] / n,
            rate[0], rate[1], rate[2], told);
    }
    puf_close(env);
    return 0;
}
