/* The granite maul against a checkpoint's own play: at every decision where a maul special can be fired, the outcome of the net's choice against forced maul
 * lines, each played out by the net, paired on the same decisions and clones. The env is [bot_eval]'s with our kit drawn per session by live_loadout (default
 * 5: the Dharok kits, the user's and the spectated ones, half the time, else the three Voidwaker kits; the live kit and the Dharok kits carry the ornate maul);
 * pool opponents played by the script (oppfit 0); the price drawn from the live table (price_table 1); recorded fight starts.
 *
 *   probe_maul_chain CKPT HIDDEN LAYERS SESSIONS [seed] [live_loadout]
 *
 * Reach: our hand free (not hand_busy), the maul in hand or in the bag, the energy for one maul special, he in melee range. At each reach decision every line
 * that can be sent is played from its own clone of the state: its actions go through puf_step (bag order, motor, hand busy, PID and tick order as in training),
 * the net's other heads kept, then the net plays the rest of the fight argmax. A line can be sent when every head value it uses is offered by the mask and its
 * special clicks survive riskfight_spec_clicks (the maul's knockout gate; the server drops them the same way):
 *   N   the net's own greedy choice (played at every decision a line is sent);
 *   D   wield the maul, two specials, attack (100 energy);
 *   S   wield the maul, one special, attack;
 *   C   the kit's opener (Voidwaker or Armadyl godsword) armed and attack now, with the energy for it and a maul special after; once that special has swung, the
 *       maul on our next free decision with the specials the energy allows (its hit lands with the opener's when he holds the PID). If the opener has not swung
 *       within 8 decisions, or the maul can't be sent then, the net plays on: the share that followed through is printed;
 *   Cu  C with the maul follow-up ungated, the user's chain as played: only its wield needs the mask (the 2h rule); the specials skip the mask's knockout gate
 *       and riskfight_spec_clicks (RF_SPEC_CLICKS_OVERRIDE, a diagnostic of this probe; the env and the server keep the gate).
 * Per kit and his bar band as we see it (<= 40%, 40-70%, > 70%), per line over the decisions it was sent: n, the line's and N's KO / death / stake (KO - price
 * x death at the decision's drawn price) on the same clones, and the stake difference line - N with its standard error. Then the reasons a line could not be
 * sent at a reach decision (first failing check): energy (D: two specials; C: opener + maul; no opener or the maul in hand counts here), 2h (the maul's wield
 * not offered), spec mask (the special head value not offered: the mask's knockout gate or energy of the weapon in hand), clicks gate (riskfight_spec_clicks
 * dropped the toggles), other (another head not offered); and the hand-busy decisions that would otherwise reach.
 * Build as the suite builds osrs_riskfight.h users:
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o probe_maul_chain ocean/osrs/tests/probe_maul_chain.c riskfight_oppfit_bridge.o -lm -lpthread */
#include <math.h>
static int probe_ungated;
#define RF_SPEC_CLICKS_OVERRIDE(s, agent, act) (probe_ungated ? (act)[RF_SPECIAL] : riskfight_spec_clicks(s, agent, act))
#include "../../osrs_riskfight/osrs_riskfight.h"
#include "rf_net.h"

enum { LINES = 5, BANDS = 3, KITS = 4 + RF_DHAROK_SPECTATED_KITS, OPENER_WAIT = 8 };
enum { LINE_N, LINE_D, LINE_S, LINE_C, LINE_CU };
enum { WHY_OK, WHY_ENERGY, WHY_2H, WHY_SPEC_MASK, WHY_CLICKS, WHY_OTHER, WHYS };
static const char* const LINE_NAMES[LINES] = {"N", "D", "S", "C", "Cu"};
static const char* const WHY_NAMES[WHYS] = {"sent", "energy", "2h", "spec mask", "clicks gate", "other"};
static float obs[RF_OBS_SIZE], obs_saved[RF_OBS_SIZE], actions[RF_HEADS], reward, terminal, reset_flag;
static unsigned char mask[RF_MASK_SIZE], mask_saved[RF_MASK_SIZE];

typedef struct { double kill, death, stake; } Outcome;
typedef struct { double n, diff, diff2, follow; Outcome line, own; } Paired;
typedef struct { long reach, busy, why[LINES][WHYS]; Paired line[LINES]; } Cell;
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
static int entry_of(Env* env, int (*pick)(uint16_t)) {
    const Player* p = us(env);
    const RiskfightBagOrder order = riskfight_bag_order(p);
    for (int e = 0; e < OSRS_INVENTORY_SIZE; e++)
        if (pick(osrs_inventory_cell_metadata(&p->inventory_cells[order.slot[e]])->item_idx)) return e + 1;
    return 0;
}
static int is_maul(uint16_t item) { return pvp_is_maul(item); }
static int is_opener(uint16_t item) { return item == ITEM_VOIDWAKER || item == ITEM_AGS; }
static uint16_t item_at(Env* env, int entry) {
    return osrs_inventory_cell_metadata(&us(env)->inventory_cells[riskfight_bag_order(us(env)).slot[entry - 1]])->item_idx;
}

/** The first head of `out` the mask doesn't offer, as a reason (WHY_OK when all are offered). */
static int mask_why(const int* out) {
    if (out[RF_WEAPON] && !mask[RF_HEAD_OFFSET[RF_WEAPON] + out[RF_WEAPON]]) return WHY_2H;
    if (!mask[RF_HEAD_OFFSET[RF_SPECIAL] + out[RF_SPECIAL]]) return WHY_SPEC_MASK;
    for (int h = 0; h < RF_HEADS; h++) if (!mask[RF_HEAD_OFFSET[h] + out[h]]) return WHY_OTHER;
    return WHY_OK;
}

/** The maul line with `k` specials as canonical actions over `base`, and why it can't be sent (WHY_OK: it can). `gated` 0: the specials skip the mask's
 * special head and riskfight_spec_clicks (the Cu follow-up). */
static int maul_line(Env* env, const int* base, int k, int gated, int* out) {
    const Player* p = us(env);
    const int holding = pvp_is_maul(p->equipped[GEAR_SLOT_WEAPON]);
    const int entry = holding ? 0 : entry_of(env, is_maul);
    if (!holding && !entry) return WHY_OTHER;
    const uint16_t maul = holding ? p->equipped[GEAR_SLOT_WEAPON] : item_at(env, entry);
    if (k < 1 || p->special_energy < k * osrs_spec_cost(maul)) return WHY_ENERGY;
    memcpy(out, base, RF_HEADS * sizeof(int));
    out[RF_WEAPON] = entry;
    out[RF_SHIELD] = 0;
    out[RF_SPECIAL] = k;
    out[RF_PRIMARY] = RF_ATTACK;
    if (!gated) {
        int check[RF_HEADS];
        memcpy(check, out, sizeof(check));
        check[RF_SPECIAL] = 0;
        return mask_why(check);
    }
    const int why = mask_why(out);
    if (why != WHY_OK) return why;
    int physical[RF_HEADS];
    memcpy(physical, out, sizeof(physical));
    riskfight_bag_to_physical(p, physical);
    return riskfight_spec_clicks(&env->state, 0, physical) == k ? WHY_OK : WHY_CLICKS;
}

/** The opener line: the opener wielded (or in hand) and armed, attack; why it can't be sent. */
static int opener_line(Env* env, const int* base, int* out) {
    const Player* p = us(env);
    const int holding = is_opener(p->equipped[GEAR_SLOT_WEAPON]);
    const int entry = holding ? 0 : entry_of(env, is_opener);
    if ((!holding && !entry) || pvp_is_maul(p->equipped[GEAR_SLOT_WEAPON])) return WHY_ENERGY;
    const uint16_t opener = holding ? p->equipped[GEAR_SLOT_WEAPON] : item_at(env, entry);
    if (p->special_energy < osrs_spec_cost(opener) + osrs_spec_cost(ITEM_GRANITE_MAUL_ORNATE)) return WHY_ENERGY;
    memcpy(out, base, RF_HEADS * sizeof(int));
    out[RF_WEAPON] = entry;
    out[RF_SPECIAL] = !p->spec_armed;
    out[RF_PRIMARY] = RF_ATTACK;
    return mask_why(out);
}

/** Steps `chosen` (canonical) and returns whether the fight (or the session) ended. */
static int step(Env* env, RfNet* net, const int* chosen, int fights, float n) {
    for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)chosen[h];
    puf_step(env);
    probe_ungated = 0;
    if (terminal || reset_flag) rf_net_reset(net);
    return env->session_fights != fights || env->log.n != n;
}

/** Plays `first` then the net to the end of this fight (for C and Cu the maul follows the opener) and returns the stake; `followed` is set when it did. */
static double play_out(Env* env, RfNet* net, uint32_t* rng, int line, const int* first, float price, Outcome* out, int* followed) {
    const Log before = env->log;
    const int fights = env->session_fights, chain = line == LINE_C || line == LINE_CU;
    int energy = us(env)->special_energy, opener_swung = 0, waited = 0;
    *followed = 0;
    int ended = step(env, net, first, fights, before.n);
    int chosen[RF_HEADS];
    for (int steps = 0; !ended; steps++) {
        assert(steps < 5000);
        rf_net_act(net, obs, mask, rng, 1, RF_ACTION_DIMS, RF_HEADS, chosen);
        if (chain && !opener_swung) {
            if (us(env)->special_energy < energy) opener_swung = 1;
            else energy = us(env)->special_energy;
        }
        if (chain && waited <= OPENER_WAIT && !env->state.hand_busy[0]) {
            waited++;
            if (opener_swung) {
                int maul[RF_HEADS];
                const int gated = line == LINE_C;
                const int k = min_int(RF_ACTION_DIMS[RF_SPECIAL] - 1, us(env)->special_energy / osrs_spec_cost(ITEM_GRANITE_MAUL_ORNATE));
                *followed = maul_line(env, chosen, k, gated, maul) == WHY_OK || (k > 1 && maul_line(env, chosen, 1, gated, maul) == WHY_OK);
                if (*followed) {
                    memcpy(chosen, maul, sizeof(chosen));
                    probe_ungated = !gated;
                }
                waited = OPENER_WAIT + 1;
            }
        }
        ended = step(env, net, chosen, fights, before.n);
    }
    const double kill = env->log.kills - before.kills, death = env->log.deaths + env->log.mutual_deaths - before.deaths - before.mutual_deaths;
    out->kill += kill;
    out->death += death;
    out->stake += kill - price * death;
    return kill - price * death;
}

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: probe_maul_chain CKPT HIDDEN LAYERS SESSIONS [seed] [live_loadout]\n"); return 2; }
    const int sessions = atoi(argv[4]);
    const unsigned seed = argc > 5 ? (unsigned)atoi(argv[5]) : 1;
    const double live_loadout = argc > 6 ? atof(argv[6]) : 5;
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = 3000},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = 0}, {.key = "price_table", .value = 1}, {.key = "live_loadout", .value = live_loadout},
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
    while (env->log.n < sessions) {
        int greedy[RF_HEADS];
        rf_net_act(net, obs, mask, &rng, 1, RF_ACTION_DIMS, RF_HEADS, greedy);
        const RiskfightState* st = &env->state;
        const Player* p = &st->env.players[0];
        const int maul_reach = env->wait_left == 0 && !st->env.episode_over && (pvp_is_maul(p->equipped[GEAR_SLOT_WEAPON]) || entry_of(env, is_maul)) &&
            p->special_energy >= osrs_spec_cost(ITEM_GRANITE_MAUL_ORNATE) && is_in_melee_range((Player*)p, (Player*)&st->env.players[1]);
        const int bar = st->visible[0].health_bar;
        const int band = bar * 10 <= OSRS_PLAYER_HEALTH_BAR_SCALE * 4 ? 0 : bar * 10 <= OSRS_PLAYER_HEALTH_BAR_SCALE * 7 ? 1 : 2;
        Cell* cell = maul_reach ? &cells[kit_of(env->context.loadout[0])][band] : NULL;
        if (cell && st->hand_busy[0]) cell->busy++;
        if (cell && !st->hand_busy[0]) {
            cell->reach++;
            int lines[LINES][RF_HEADS], why[LINES] = {WHY_OK};
            why[LINE_D] = maul_line(env, greedy, 2, 1, lines[LINE_D]);
            why[LINE_S] = maul_line(env, greedy, 1, 1, lines[LINE_S]);
            why[LINE_C] = opener_line(env, greedy, lines[LINE_C]);
            why[LINE_CU] = why[LINE_C];
            memcpy(lines[LINE_CU], lines[LINE_C], sizeof(lines[LINE_C]));
            memcpy(lines[LINE_N], greedy, sizeof(greedy));
            for (int l = 1; l < LINES; l++) cell->why[l][why[l]]++;
            if (why[LINE_D] == WHY_OK || why[LINE_S] == WHY_OK || why[LINE_C] == WHY_OK) {
                const float price = st->death_price;
                /* The clone shares the command queues' heap buffers: grow them first so no line reallocates a buffer the saved copy still points at. */
                for (int seat = 0; seat < 2; seat++) human_command_queue_reserve(&env->context.policy_commands[seat].commands, 256);
                memcpy(saved, env, sizeof(*env));
                memcpy(net_saved, net, sizeof(*net));
                memcpy(obs_saved, obs, sizeof(obs));
                memcpy(mask_saved, mask, sizeof(mask));
                double stake[LINES];
                Outcome got[LINES] = {{0}};
                int followed[LINES] = {0};
                for (int l = 0; l < LINES; l++) {
                    if (why[l] != WHY_OK) continue;
                    uint32_t line_rng = rng;
                    stake[l] = play_out(env, net, &line_rng, l, lines[l], price, &got[l], &followed[l]);
                    memcpy(env, saved, sizeof(*env));
                    memcpy(net, net_saved, sizeof(*net));
                    memcpy(obs, obs_saved, sizeof(obs));
                    memcpy(mask, mask_saved, sizeof(mask));
                }
                for (int l = 1; l < LINES; l++) {
                    if (why[l] != WHY_OK) continue;
                    Paired* pl = &cell->line[l];
                    const double d = stake[l] - stake[LINE_N];
                    pl->n++;
                    pl->diff += d;
                    pl->diff2 += d * d;
                    pl->follow += followed[l];
                    pl->line.kill += got[l].kill; pl->line.death += got[l].death; pl->line.stake += got[l].stake;
                    pl->own.kill += got[LINE_N].kill; pl->own.death += got[LINE_N].death; pl->own.stake += got[LINE_N].stake;
                }
            }
        }
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)greedy[h];
        puf_step(env);
        if (terminal || reset_flag) rf_net_reset(net);
    }
    printf("%s, %d sessions, seed %u, live_loadout %.0f\n", argv[1], sessions, seed, live_loadout);
    printf("kit | his bar | line | n | line KO/death/stake | N on the same decisions | line - N stake (SE) | followed | reach:");
    for (int w = 0; w < WHYS; w++) printf(" %s%s", WHY_NAMES[w], w + 1 < WHYS ? " /" : "");
    printf(" (busy)\n");
    static const char* const BAND_NAMES[BANDS] = {"<=40%", "40-70%", ">70%"};
    for (int k = 0; k < KITS; k++)
        for (int b = 0; b < BANDS; b++) {
            const Cell* c = &cells[k][b];
            if (!c->reach && !c->busy) continue;
            for (int l = 1; l < LINES; l++) {
                const Paired* pl = &c->line[l];
                const double n = pl->n > 0 ? pl->n : 1, mean = pl->diff / n;
                const double se = pl->n > 1 ? sqrt((pl->diff2 / n - mean * mean) / (pl->n - 1)) : 0;
                printf("%s | %s | %s | %.0f | %.3f/%.3f/%+.3f | %.3f/%.3f/%+.3f | %+.3f (%.3f) | %s", KIT_NAMES[k], BAND_NAMES[b], LINE_NAMES[l], pl->n,
                    pl->line.kill / n, pl->line.death / n, pl->line.stake / n, pl->own.kill / n, pl->own.death / n, pl->own.stake / n, mean, se,
                    l >= LINE_C ? "" : "-");
                if (l >= LINE_C) printf("%.2f", pl->follow / n);
                printf(" | %ld:", c->reach);
                for (int w = 0; w < WHYS; w++) printf(" %ld", c->why[l][w]);
                printf(" (%ld)\n", c->busy);
            }
        }
    puf_close(env);
    return 0;
}
