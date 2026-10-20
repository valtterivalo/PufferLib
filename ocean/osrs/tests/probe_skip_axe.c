/* Eat or press on in the Dharok's kit: a checkpoint's meal decisions against a sustained aggressive line. The env is [bot_eval]'s (our kit drawn per session as in
 * training, live_loadout 4; pool opponents played by the script, oppfit 0; price drawn from the live table, price_table 1; recorded fight starts), as in
 * probe_lowhp_meals. A decision counts when our loadout is of the Dharok class (riskfight_loadout_class), the mask offers a meal (a food or combo slot, or a
 * brew in the drink head) and the net's greedy choice (rf_net_act hard) is a meal (food, combo, or a brew drunk). At each one the state is cloned and four
 * branches are played to the end of the fight:
 *   A greedy  the net's own choice, then the net argmax;
 *   B skip-1  the net's choice with food, combo and drink cleared, then the net argmax (probe_lowhp_meals' none branch);
 *   C skip-K + axe  for the first K hand-free decisions from this one (this one included): food, combo and drink cleared unless our hitpoints are at most
 *            FLOOR (there the net's choice stands); the Dharok's greataxe wielded from the bag when the mask offers it and kept in hand (no weapon or shield
 *            swap, no special while it is wielded or being wielded); the primary head attack when offered; every other head the net's argmax. After the K
 *            decisions the net plays on argmax.
 *   D skip-K  C's meal rule alone: the net's weapon, special and every other head.
 *   E axe-K   C's axe rule alone: the net's meals and every other head.
 *   F axe-1   E's axe rule until the first swing with the axe (at most K hand-free decisions), then the net (which switches back when it wants).
 *   G bait    no meals above BAIT_HP (argument 10, default 35) with Vengeance cast when offered, then one triple eat at or below it, then the net.
 * MODE 1 (argument 9) takes instead a sixth of the hand-free decisions in our Dharok's kit where the mask offers the axe's wield, whatever the net picks.
 * Each branch is played ROLLS times, roll r > 0 with the combat dice reseeded (OsrsEnv.rng_state), the same dice for every branch of a roll (paired).
 *
 *   probe_skip_axe CKPT HIDDEN LAYERS SESSIONS [seed] [rolls, default 6] [K, default 8] [FLOOR, default 20]
 *
 * Per our hitpoints band (<= 20 for reference, 21-40, 41-60, 61-80, > 80) it prints n decisions; each branch's KO and death share of the fight (pooled over
 * decisions and rolls); for B and C the mean d = (KO - price x death | branch) - (same | A) at the decision's own drawn price, averaged over the rolls and then
 * the decisions, with a 90% interval over sessions resampled (a session's decisions share fights and states; they are not independent); and, as a sanity
 * check of the line, the mean meals sent (food, combo or drink head nonzero) in the K-decision window per branch, and in C the mean window decisions where
 * the axe's wield was sent.
 * Build as the suite builds osrs_riskfight.h users (riskfight_oppfit_bridge.o as ocean/osrs/tests/run_suite.sh builds it):
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o probe_skip_axe ocean/osrs/tests/probe_skip_axe.c riskfight_oppfit_bridge.o -lm -lpthread */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include "rf_net.h"

enum { BANDS = 5, BRANCHES = 7, MAX_DECISIONS = 1 << 20, BOOT = 1000 };
enum { BR_GREEDY, BR_SKIP1, BR_AXE, BR_SKIPK, BR_AXEONLY, BR_AXE1, BR_BAIT };
static float obs[RF_OBS_SIZE], obs_saved[RF_OBS_SIZE], actions[RF_HEADS], reward, terminal, reset_flag;
static unsigned char mask[RF_MASK_SIZE], mask_saved[RF_MASK_SIZE];

typedef struct { double n, kill, death, window_meals, window_axe; } Outcome;
typedef struct { unsigned char band; float price; int session; double d_sum[BRANCHES]; } Decision;
static Decision decisions[MAX_DECISIONS];

static int allowed(int head, int value) { return mask[RF_HEAD_OFFSET[head] + value]; }

static const OsrsItemContentMetadata* entry_item(const Player* p, const RiskfightBagOrder* order, int entry) {
    return osrs_inventory_cell_metadata(&p->inventory_cells[order->slot[entry - 1]]);
}

/** Whether the mask offers a meal: a food or combo slot, or a brew in the drink head. */
static int meal_offered(const Player* p) {
    const RiskfightBagOrder order = riskfight_bag_order(p);
    for (int e = 1; e <= OSRS_INVENTORY_SIZE; e++) {
        if (allowed(RF_FOOD, e) || allowed(RF_COMBO, e)) return 1;
        if (allowed(RF_DRINK, e) && entry_item(p, &order, e)->consumable_kind == OSRS_CONSUMABLE_BREW) return 1;
    }
    return 0;
}

/** Whether `chosen` eats a meal: food, combo, or a brew drunk. */
static int meal_chosen(const Player* p, const int* chosen) {
    if (chosen[RF_FOOD] || chosen[RF_COMBO]) return 1;
    if (!chosen[RF_DRINK]) return 0;
    const RiskfightBagOrder order = riskfight_bag_order(p);
    return entry_item(p, &order, chosen[RF_DRINK])->consumable_kind == OSRS_CONSUMABLE_BREW;
}

/** The weapon head's canonical entry of the Dharok's greataxe when the mask offers its wield, else 0. */
static int axe_entry(const Player* p) {
    const RiskfightBagOrder order = riskfight_bag_order(p);
    for (int e = 1; e <= OSRS_INVENTORY_SIZE; e++)
        if (allowed(RF_WEAPON, e) && entry_item(p, &order, e)->item_idx == ITEM_DHAROKS_GREATAXE) return e;
    return 0;
}

/** The first entry the mask offers on `head`, 0 when none; on the drink head only a brew. */
static int first_offered(const Player* p, int head) {
    const RiskfightBagOrder order = riskfight_bag_order(p);
    for (int e = 1; e <= OSRS_INVENTORY_SIZE; e++)
        if (allowed(head, e) && (head != RF_DRINK || entry_item(p, &order, e)->consumable_kind == OSRS_CONSUMABLE_BREW)) return e;
    return 0;
}

static int g_bait_hp = 35;

/** Plays branch `branch` from the current state to the end of this fight and counts its outcome; `greedy` is the net's choice at the decision. */
static void play_out(Env* env, RfNet* net, uint32_t* rng, int branch, const int* greedy, int window, int floor_hp, Outcome* out) {
    const Log before = env->log;
    const int fights = env->session_fights;
    int chosen[RF_HEADS], free_seen = 0, axe_swung = 0, baited = 0;
    memcpy(chosen, greedy, sizeof(chosen));
    for (int step = 0; env->session_fights == fights && env->log.n == before.n; step++) {
        assert(step < 5000);
        if (step) rf_net_act(net, obs, mask, rng, 1, RF_ACTION_DIMS, RF_HEADS, chosen);
        const Player* us = &env->state.env.players[0];
        const int in_window = env->wait_left == 0 && !env->state.hand_busy[0] && free_seen++ < window;
        if (step == 0 && branch == BR_SKIP1) chosen[RF_FOOD] = chosen[RF_COMBO] = chosen[RF_DRINK] = 0;
        if (in_window && (branch == BR_AXE || branch == BR_SKIPK) && us->current_hitpoints > floor_hp) chosen[RF_FOOD] = chosen[RF_COMBO] = chosen[RF_DRINK] = 0;
        const int one_swing = branch == BR_AXE1 && !axe_swung;
        if ((in_window && (branch == BR_AXE || branch == BR_AXEONLY)) || (one_swing && free_seen <= window)) {
            /* The axe in hand and kept there: wielded from the bag, else no weapon or shield swap (the net would switch back to its Voidwaker) and no
             * special (one would spend the bag's spec weapon). */
            const int axe = axe_entry(us);
            if (axe) { chosen[RF_WEAPON] = axe; out->window_axe++; }
            else if (us->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE) chosen[RF_WEAPON] = 0;
            if (axe || us->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE) chosen[RF_SHIELD] = chosen[RF_SPECIAL] = 0;
            if (allowed(RF_PRIMARY, RF_ATTACK)) chosen[RF_PRIMARY] = RF_ATTACK;
        }
        /* G bait: no meals while our hitpoints are above BAIT_HP (Vengeance cast whenever offered), for at most 3 K hand-free decisions; at BAIT_HP or below one
         * triple eat (food, combo, brew, whatever the mask offers), then the net. */
        if (branch == BR_BAIT && !baited && env->wait_left == 0 && !env->state.hand_busy[0] && free_seen <= 3 * window) {
            if (us->current_hitpoints > g_bait_hp) {
                chosen[RF_FOOD] = chosen[RF_COMBO] = chosen[RF_DRINK] = 0;
                if (allowed(RF_VENGEANCE, 1)) chosen[RF_VENGEANCE] = 1;
            } else {
                chosen[RF_FOOD] = first_offered(us, RF_FOOD);
                chosen[RF_COMBO] = first_offered(us, RF_COMBO);
                chosen[RF_DRINK] = first_offered(us, RF_DRINK);
                baited = 1;
            }
        }
        if (in_window) out->window_meals += chosen[RF_FOOD] || chosen[RF_COMBO] || chosen[RF_DRINK];
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)chosen[h];
        puf_step(env);
        axe_swung |= env->state.env.players[0].just_attacked && env->state.env.players[0].equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE;
        if (terminal || reset_flag) rf_net_reset(net);
    }
    out->n++;
    out->kill += env->log.kills - before.kills;
    out->death += env->log.deaths + env->log.mutual_deaths - before.deaths - before.mutual_deaths;
}

/** The mean of per-session sums over per-session counts and its 90% interval over sessions resampled with replacement. */
static void session_interval(const double* sum, const long* n, int sessions, uint32_t seed, double* mean, double* lo, double* hi) {
    double total = 0;
    long m = 0;
    for (int s = 0; s < sessions; s++) { total += sum[s]; m += n[s]; }
    *mean = total / m;
    static double boot[BOOT];
    uint32_t r = seed;
    for (int b = 0; b < BOOT; b++) {
        double bs = 0;
        long bn = 0;
        for (int s = 0; s < sessions; s++) {
            r = osrs_lowbias32(r + 0x9e3779b9u);
            const int pick = (int)(r % (uint32_t)sessions);
            bs += sum[pick];
            bn += n[pick];
        }
        boot[b] = bn ? bs / bn : 0;
    }
    for (int i = 1; i < BOOT; i++) for (int j = i; j > 0 && boot[j - 1] > boot[j]; j--) { double t = boot[j]; boot[j] = boot[j - 1]; boot[j - 1] = t; }
    *lo = boot[BOOT / 20];
    *hi = boot[BOOT - BOOT / 20 - 1];
}

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: probe_skip_axe CKPT HIDDEN LAYERS SESSIONS [seed] [rolls] [K] [FLOOR] [MODE] [BAIT_HP]\n"); return 2; }
    const int sessions = atoi(argv[4]);
    const unsigned seed = argc > 5 ? (unsigned)atoi(argv[5]) : 1;
    const int rolls = argc > 6 ? atoi(argv[6]) : 6;
    const int window = argc > 7 ? atoi(argv[7]) : 8;
    const int floor_hp = argc > 8 ? atoi(argv[8]) : 20;
    const int mode = argc > 9 ? atoi(argv[9]) : 0;
    if (argc > 10) g_bait_hp = atoi(argv[10]);
    if (sessions < 1 || rolls < 1 || window < 1) { fprintf(stderr, "probe_skip_axe: SESSIONS, rolls and K must be at least 1\n"); return 2; }
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = 3000},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = 0}, {.key = "price_table", .value = 1}, {.key = "live_loadout", .value = 4},
        {.key = "deployable", .value = 1}, {.key = "motor", .value = 1}, {.key = "domain_randomization", .value = 0}, {.key = "oppfit_probability", .value = 0},
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
        if (env->wait_left == 0 && !st->env.episode_over && riskfight_loadout_class(env->context.loadout[0]) == RF_CLASS_DHAROK) {
            const Player* us = &st->env.players[0];
            const int hp = us->current_hitpoints;
            const int pick = mode == 0 ? meal_offered(us) && meal_chosen(us, greedy)
                : !st->hand_busy[0] && axe_entry(us) && osrs_lowbias32((uint32_t)st->env.tick * 2654435761u ^ seed) % 6 == 0;
            if (hp > 0 && pick) {
                assert(count < MAX_DECISIONS);
                const int band = hp <= 20 ? 0 : hp <= 40 ? 1 : hp <= 60 ? 2 : hp <= 80 ? 3 : 4;
                Decision* dec = &decisions[count++];
                const int his_bar = st->visible[0].health_bar; /* his bar as we see it, for the ROW line */
                *dec = (Decision){.band = band, .price = st->death_price, .session = (int)env->log.n};
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
                        play_out(env, net, &branch_rng, b, greedy, window, floor_hp, &one);
                        Outcome* o = &branch[band][b];
                        o->n += one.n;
                        o->kill += one.kill;
                        o->death += one.death;
                        o->window_meals += one.window_meals;
                        o->window_axe += one.window_axe;
                        value[b] = one.kill - dec->price * one.death;
                        memcpy(env, saved, sizeof(*env));
                        memcpy(net, net_saved, sizeof(*net));
                        memcpy(obs, obs_saved, sizeof(obs));
                        memcpy(mask, mask_saved, sizeof(mask));
                    }
                    for (int b = 0; b < BRANCHES; b++) dec->d_sum[b] += value[b] - value[BR_GREEDY];
                }
                /* One row per decision for pooling runs offline: band, session, the mean d of B, C and D over the rolls. */
                printf("ROW %d %d %.4f %.4f %.4f %.4f %.4f %d %.4f\n", band, dec->session, dec->d_sum[BR_SKIP1] / rolls, dec->d_sum[BR_AXE] / rolls, dec->d_sum[BR_SKIPK] / rolls,
                    dec->d_sum[BR_AXEONLY] / rolls, dec->d_sum[BR_AXE1] / rolls, his_bar, dec->d_sum[BR_BAIT] / rolls);
            }
        }
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)greedy[h];
        puf_step(env);
        if (terminal || reset_flag) rf_net_reset(net);
    }
    static const char* const names[BANDS] = {"<=20", "21-40", "41-60", "61-80", ">80"};
    printf("%s, %d sessions, seed %u, %d rolls, K %d, floor %d, mode %d: our Dharok's kit, %s\n", argv[1], sessions, seed, rolls, window, floor_hp, mode,
        mode == 0 ? "a meal offered and greedy eats" : "a sixth of the decisions where the axe's wield is offered");
    printf("our hp | n | KO / death A greedy, B skip-1, C skip-K+axe, D skip-K, E axe-K | d B-A [90%% over sessions] | d C-A | d D-A | d E-A | window meals A / B / C / D / E | C axe wields\n");
    double* s_sum = malloc(sizeof(double) * (size_t)sessions);
    long* s_n = malloc(sizeof(long) * (size_t)sessions);
    for (int band = 0; band < BANDS; band++) {
        long n = 0;
        for (long i = 0; i < count; i++) n += decisions[i].band == band;
        if (!n) { printf("%s | 0\n", names[band]); continue; }
        char share[BRANCHES][32], d[BRANCHES][48] = {{0}};
        for (int b = 0; b < BRANCHES; b++) {
            const Outcome* o = &branch[band][b];
            snprintf(share[b], sizeof(share[b]), "%.3f / %.3f", o->kill / o->n, o->death / o->n);
            if (b == BR_GREEDY) continue;
            memset(s_sum, 0, sizeof(double) * (size_t)sessions);
            memset(s_n, 0, sizeof(long) * (size_t)sessions);
            for (long i = 0; i < count; i++) {
                const Decision* x = &decisions[i];
                if (x->band != band) continue;
                s_sum[x->session] += x->d_sum[b] / rolls;
                s_n[x->session]++;
            }
            double mean, lo, hi;
            session_interval(s_sum, s_n, sessions, 0x2545f491u ^ (uint32_t)(band * BRANCHES + b), &mean, &lo, &hi);
            snprintf(d[b], sizeof(d[b]), "%+.3f [%+.3f, %+.3f]", mean, lo, hi);
        }
        const Outcome* o = branch[band];
        printf("%s | %ld | %s, %s, %s, %s, %s | %s | %s | %s | %s | %.2f / %.2f / %.2f / %.2f / %.2f | %.2f\n", names[band], n, share[0], share[1], share[2], share[3],
            share[4], d[BR_SKIP1], d[BR_AXE], d[BR_SKIPK], d[BR_AXEONLY], o[0].window_meals / o[0].n, o[1].window_meals / o[1].n, o[2].window_meals / o[2].n,
            o[3].window_meals / o[3].n, o[4].window_meals / o[4].n, o[2].window_axe / o[2].n);
    }
    free(s_sum);
    free(s_n);
    puf_close(env);
    return 0;
}
