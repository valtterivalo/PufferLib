/* The granite maul against a checkpoint's own play: at every decision where a maul special can be fired, the outcome of the net's choice against forced maul
 * lines, each played out by the net. The env is [bot_eval]'s with our kit drawn per session by live_loadout (default 5: the Dharok kits, the user's and the
 * spectated ones, half the time, else the three Voidwaker kits; the live kit and the Dharok kits carry the ornate maul); pool opponents played by the script
 * (oppfit 0); the price drawn from the live table (price_table 1); recorded fight starts.
 *
 *   probe_maul_chain CKPT HIDDEN LAYERS SESSIONS [seed] [live_loadout]
 *
 * A decision counts when our hand is free (not hand_busy), the maul is wielded or in the bag with its wield offered by the mask (the 2h rule: a free cell for the
 * shield), we have the energy for one maul special, he is in melee range, and at least one forced line below can be sent: every head value it uses is offered
 * by the mask, and its special clicks survive riskfight_spec_clicks (the maul's knockout gate; the server drops them the same way). At each one the state is
 * cloned once per line; the line's actions go through puf_step (bag order, motor, hand busy, PID and tick order as in training), the net's other heads kept:
 *   N  the net's own greedy choice;
 *   D  wield the maul, two specials, attack (100 energy);
 *   S  wield the maul, one special, attack;
 *   C  the kit's opener (Voidwaker or Armadyl godsword) armed and attack now, with the energy for it and a maul special after; once that special has swung, the
 *      maul on our next free decision with the specials the energy allows (the instant maul's hit lands with the opener's when he holds the PID). If the opener
 *      has not swung within 8 decisions, or the maul's clicks would be dropped then, the net plays on: the share that followed through is printed.
 * Then the net plays the rest of the fight argmax. Per kit and his bar band as we see it (<= 40%, 40-70%, > 70%): n, per line KO / death / net stake
 * (KO - price x death at the decision's drawn price, averaged over the decisions the line could be sent), the best line by stake, and how often N's own choice
 * was that line or its stake within 0.01 of it. Also printed: reach, the decisions with a free hand, the maul in hand or in the bag, its energy and melee
 * range, whether or not a line could be sent (on the rest the mask's 2h rule or its and riskfight_spec_clicks' knockout gate shut every forced line). Build as the suite builds osrs_riskfight.h users:
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o probe_maul_chain ocean/osrs/tests/probe_maul_chain.c riskfight_oppfit_bridge.o -lm -lpthread */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include "rf_net.h"

enum { LINES = 4, BANDS = 3, KITS = 4 + RF_DHAROK_SPECTATED_KITS, OPENER_WAIT = 8 };
enum { LINE_N, LINE_D, LINE_S, LINE_C };
static const char* const LINE_NAMES[LINES] = {"N", "D", "S", "C"};
static float obs[RF_OBS_SIZE], obs_saved[RF_OBS_SIZE], actions[RF_HEADS], reward, terminal, reset_flag;
static unsigned char mask[RF_MASK_SIZE], mask_saved[RF_MASK_SIZE];

typedef struct { double n, kill, death, stake; } Outcome;
typedef struct { long reach, n, matched, follow, follow_n; Outcome line[LINES]; } Cell;
static Cell cells[KITS][BANDS];

static int kit_of(const RiskfightLoadout* l) {
    if (l == &RISKFIGHT_LIVE_LOADOUT) return 0;
    if (l == &RISKFIGHT_VW_LOADOUT) return 1;
    if (l == &RISKFIGHT_VW_PIES_LOADOUT) return 2;
    if (l == &RISKFIGHT_DHAROK_LOADOUT) return 3;
    for (int k = 0; k < RF_DHAROK_SPECTATED_KITS; k++) if (l == &RISKFIGHT_DHAROK_SPECTATED_LOADOUT[k]) return 4 + k;
    assert(0);
    return -1;
}
static const char* const KIT_NAMES[KITS] = {"live (VW, maul)", "VW", "VW pies", "Dharok (user)", "spectated tentacle VW maul", "spectated tentacle VW",
    "spectated tentacle AGS maul"};

static Player* us(Env* env) { return &env->state.env.players[0]; }

/** The canonical bag entry + 1 of `pick` items in our bag (0: none). */
static int entry_of(Env* env, int (*pick)(uint8_t)) {
    const Player* p = us(env);
    const RiskfightBagOrder order = riskfight_bag_order(p);
    for (int e = 0; e < OSRS_INVENTORY_SIZE; e++)
        if (pick(osrs_inventory_cell_metadata(&p->inventory_cells[order.slot[e]])->item_idx)) return e + 1;
    return 0;
}
static int is_maul(uint8_t item) { return pvp_is_maul(item); }
static int is_opener(uint8_t item) { return item == ITEM_VOIDWAKER || item == ITEM_AGS; }

/** The maul line with `k` specials as canonical actions over `base`, or 0 when it can't be sent: a head the mask doesn't offer, or the clicks dropped. */
static int maul_line(Env* env, const int* base, int k, int* out) {
    const Player* p = us(env);
    const int holding = pvp_is_maul(p->equipped[GEAR_SLOT_WEAPON]);
    const int entry = holding ? 0 : entry_of(env, is_maul);
    if (!holding && !entry) return 0;
    const uint8_t maul = holding ? p->equipped[GEAR_SLOT_WEAPON] : osrs_inventory_cell_metadata(&p->inventory_cells[riskfight_bag_order(p).slot[entry - 1]])->item_idx;
    if (k < 1 || p->special_energy < k * osrs_spec_cost(maul)) return 0;
    memcpy(out, base, RF_HEADS * sizeof(int));
    out[RF_WEAPON] = entry;
    out[RF_SHIELD] = 0;
    out[RF_SPECIAL] = k;
    out[RF_PRIMARY] = RF_ATTACK;
    for (int h = 0; h < RF_HEADS; h++) if (!mask[RF_HEAD_OFFSET[h] + out[h]]) return 0;
    int physical[RF_HEADS];
    memcpy(physical, out, sizeof(physical));
    riskfight_bag_to_physical(p, physical);
    return riskfight_spec_clicks(&env->state, 0, physical) == k;
}

/** The opener line: the opener wielded (or in hand) and armed, attack. 0 when it can't be sent or the energy can't also pay a maul special. */
static int opener_line(Env* env, const int* base, int* out) {
    const Player* p = us(env);
    const int holding = is_opener(p->equipped[GEAR_SLOT_WEAPON]);
    const int entry = holding ? 0 : entry_of(env, is_opener);
    const int maul_held = pvp_is_maul(p->equipped[GEAR_SLOT_WEAPON]);
    if ((!holding && !entry) || maul_held) return 0;
    const uint8_t opener = holding ? p->equipped[GEAR_SLOT_WEAPON] : osrs_inventory_cell_metadata(&p->inventory_cells[riskfight_bag_order(p).slot[entry - 1]])->item_idx;
    if (p->special_energy < osrs_spec_cost(opener) + osrs_spec_cost(ITEM_GRANITE_MAUL_ORNATE)) return 0;
    memcpy(out, base, RF_HEADS * sizeof(int));
    out[RF_WEAPON] = entry;
    out[RF_SPECIAL] = !p->spec_armed;
    out[RF_PRIMARY] = RF_ATTACK;
    for (int h = 0; h < RF_HEADS; h++) if (!mask[RF_HEAD_OFFSET[h] + out[h]]) return 0;
    return 1;
}

/** Steps `chosen` (canonical) and returns whether the fight (or the session) ended. */
static int step(Env* env, RfNet* net, const int* chosen, int fights, float n) {
    for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)chosen[h];
    puf_step(env);
    if (terminal || reset_flag) rf_net_reset(net);
    return env->session_fights != fights || env->log.n != n;
}

/** Plays `first` then the net to the end of this fight; for LINE_C the maul follows the opener. Returns whether a C line followed through. */
static int play_out(Env* env, RfNet* net, uint32_t* rng, int line, const int* first, float price, Outcome* out) {
    const Log before = env->log;
    const int fights = env->session_fights;
    int energy = us(env)->special_energy, opener_swung = line != LINE_C, followed = 0, waited = 0;
    int ended = step(env, net, first, fights, before.n);
    int chosen[RF_HEADS];
    for (int steps = 0; !ended; steps++) {
        assert(steps < 5000);
        rf_net_act(net, obs, mask, rng, 1, RF_ACTION_DIMS, RF_HEADS, chosen);
        if (!opener_swung && us(env)->special_energy < energy) opener_swung = 1;
        else if (!opener_swung) energy = us(env)->special_energy;
        if (line == LINE_C && !followed && waited <= OPENER_WAIT && !env->state.hand_busy[0]) {
            waited++;
            if (opener_swung) {
                int maul[RF_HEADS];
                const int k = min_int(RF_ACTION_DIMS[RF_SPECIAL] - 1, us(env)->special_energy / osrs_spec_cost(ITEM_GRANITE_MAUL_ORNATE));
                followed = maul_line(env, chosen, k, maul) || (k > 1 && maul_line(env, chosen, 1, maul));
                if (followed) memcpy(chosen, maul, sizeof(chosen));
                waited = OPENER_WAIT + 1;
            }
        }
        ended = step(env, net, chosen, fights, before.n);
    }
    const double kill = env->log.kills - before.kills, death = env->log.deaths + env->log.mutual_deaths - before.deaths - before.mutual_deaths;
    out->n++;
    out->kill += kill;
    out->death += death;
    out->stake += kill - price * death;
    return followed;
}

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: probe_maul_chain CKPT HIDDEN LAYERS SESSIONS [seed] [live_loadout]\n"); return 2; }
    const int sessions = atoi(argv[4]);
    const unsigned seed = argc > 5 ? (unsigned)atoi(argv[5]) : 1;
    const double live_loadout = argc > 6 ? atof(argv[6]) : 5;
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = 3000},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = 0}, {.key = "price_table", .value = 1}, {.key = "live_loadout", .value = live_loadout},
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
    while (env->log.n < sessions) {
        int greedy[RF_HEADS];
        rf_net_act(net, obs, mask, &rng, 1, RF_ACTION_DIMS, RF_HEADS, greedy);
        const RiskfightState* st = &env->state;
        const Player* p = &st->env.players[0];
        const int maul_reach = (pvp_is_maul(p->equipped[GEAR_SLOT_WEAPON]) || entry_of(env, is_maul)) &&
            p->special_energy >= osrs_spec_cost(ITEM_GRANITE_MAUL_ORNATE) && is_in_melee_range((Player*)p, (Player*)&st->env.players[1]);
        int lines[LINES][RF_HEADS], can[LINES] = {1, 0, 0, 0};
        if (env->wait_left == 0 && !st->env.episode_over && !st->hand_busy[0] && maul_reach) {
            can[LINE_D] = maul_line(env, greedy, 2, lines[LINE_D]);
            can[LINE_S] = maul_line(env, greedy, 1, lines[LINE_S]);
            can[LINE_C] = opener_line(env, greedy, lines[LINE_C]);
        }
        const int bar = st->visible[0].health_bar;
        const int band = bar * 10 <= OSRS_PLAYER_HEALTH_BAR_SCALE * 4 ? 0 : bar * 10 <= OSRS_PLAYER_HEALTH_BAR_SCALE * 7 ? 1 : 2;
        Cell* cell = &cells[kit_of(env->context.loadout[0])][band];
        cell->reach += env->wait_left == 0 && !st->env.episode_over && !st->hand_busy[0] && maul_reach;
        if (can[LINE_D] || can[LINE_S] || can[LINE_C]) {
            memcpy(lines[LINE_N], greedy, sizeof(greedy));
            const float price = st->death_price;
            cell->n++;
            /* The clone shares the command queues' heap buffers: grow them first so no line reallocates a buffer the saved copy still points at. */
            for (int seat = 0; seat < 2; seat++) human_command_queue_reserve(&env->context.policy_commands[seat].commands, 256);
            memcpy(saved, env, sizeof(*env));
            memcpy(net_saved, net, sizeof(*net));
            memcpy(obs_saved, obs, sizeof(obs));
            memcpy(mask_saved, mask, sizeof(mask));
            double stake[LINES];
            for (int l = 0; l < LINES; l++) {
                if (!can[l]) continue;
                uint32_t line_rng = rng;
                const double before = cell->line[l].stake;
                const int followed = play_out(env, net, &line_rng, l, lines[l], price, &cell->line[l]);
                stake[l] = cell->line[l].stake - before;
                if (l == LINE_C) { cell->follow_n++; cell->follow += followed; }
                memcpy(env, saved, sizeof(*env));
                memcpy(net, net_saved, sizeof(*net));
                memcpy(obs, obs_saved, sizeof(obs));
                memcpy(mask, mask_saved, sizeof(mask));
            }
            double best = stake[LINE_N];
            for (int l = 1; l < LINES; l++) if (can[l] && stake[l] > best) best = stake[l];
            cell->matched += stake[LINE_N] >= best - 0.01;
        }
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)greedy[h];
        puf_step(env);
        if (terminal || reset_flag) rf_net_reset(net);
    }
    printf("%s, %d sessions, seed %u, live_loadout %.0f: decisions where a maul special can be sent\n", argv[1], sessions, seed, live_loadout);
    printf("reach: free hand, the maul in hand or bag, its energy, in melee range; n: of those, a forced line could be sent\n");
    printf("kit | his bar | reach | n | N KO/death/stake (n) | D | S | C (followed through) | best line by stake | N as good as the decision's best line\n");
    static const char* const BAND_NAMES[BANDS] = {"<=40%", "40-70%", ">70%"};
    for (int k = 0; k < KITS; k++)
        for (int b = 0; b < BANDS; b++) {
            const Cell* c = &cells[k][b];
            if (!c->reach && !c->n) continue;
            printf("%s | %s | %ld | %ld", KIT_NAMES[k], BAND_NAMES[b], c->reach, c->n);
            if (!c->n) { printf("\n"); continue; }
            int best = -1;
            double best_stake = 0;
            for (int l = 0; l < LINES; l++) {
                const Outcome* o = &c->line[l];
                if (!o->n) { printf(" | -"); continue; }
                printf(" | %.3f/%.3f/%+.3f (%.0f)", o->kill / o->n, o->death / o->n, o->stake / o->n, o->n);
                if (l == LINE_C) printf(" %.2f", (double)c->follow / c->follow_n);
                if (best < 0 || o->stake / o->n > best_stake) { best = l; best_stake = o->stake / o->n; }
            }
            printf(" | %s | %.3f\n", LINE_NAMES[best], (double)c->matched / c->n);
        }
    puf_close(env);
    return 0;
}
