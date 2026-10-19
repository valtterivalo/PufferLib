/* Plays the riskfight env with a trained checkpoint, as session_probe does, and writes its Dharok v Dharok fights in the spectator.duels format
 * (riskfight_duel_writer.h), both seats, for scripts/measure_risk_ledger.py.
 *
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o probe_risk_duels ocean/osrs/tests/probe_risk_duels.c riskfight_oppfit_bridge.o -lm -lpthread
 *   RF_OPPFIT_MODEL=... RF_OPPFIT_EAT=... probe_risk_duels CHECKPOINT HIDDEN LAYERS SESSIONS argmax|sample oppfit_probability seed session_ticks opponent_pool
 *       death_price fight_starts price_table live_loadout OUT
 *
 * The env arguments and recipe are session_probe's. OUT gets the duels, OUT.names one line per duel: `id us his`, his `oppfit` for the fitted opponent model, else
 * `script` and his Dharok persona (`script0`: the pooled rates). A fight is written when both kits are of the Dharok class (riskfight_loadout_class).
 *
 * The last tick of a fight is read through Env.fight_end, before the env logs the round and moves on. A fight the session's budget cuts has no outcome: it is
 * written up to its last tick before the cut (the cut tick is reset with the session), censored as a recording that ends is. */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include "rf_net.h"
#include "riskfight_duel_writer.h"

static RiskfightState pre; /* the state before the step being played */
static int fight_open, row_written;

static void on_fight_end(void* context, const Env* env) {
    (void)context;
    if (fight_open > 0) { rd_write_row(&env->state, &pre); row_written = 1; }
}

int main(int argc, char** argv) {
    if (argc != 15) {
        fprintf(stderr, "usage: probe_risk_duels CHECKPOINT HIDDEN LAYERS SESSIONS argmax|sample oppfit_probability seed session_ticks opponent_pool death_price "
            "fight_starts price_table live_loadout OUT\n");
        return 2;
    }
    int sessions = atoi(argv[4]);
    int hard = !strcmp(argv[5], "argmax");
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = atof(argv[8])},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = atof(argv[10])}, {.key = "live_loadout", .value = atof(argv[13])},
        {.key = "deployable", .value = 1}, {.key = "motor", .value = 1}, {.key = "domain_randomization", .value = 1}, {.key = "oppfit_probability", .value = atof(argv[6])},
        {.key = "opponent_pool", .value = atof(argv[9])}, {.key = "fight_starts", .value = atof(argv[11])}, {.key = "price_table", .value = atof(argv[12])}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    unsigned seed = (unsigned)atoi(argv[7]);
    Env* env = (Env*)calloc(1, sizeof(*env));
    env->rng = seed;
    puf_init(env, &kwargs);
    env->fight_end = on_fight_end;
    RfNet* net = rf_net_load(argv[1], RF_OBS_SIZE, atoi(argv[2]), atoi(argv[3]), RF_ACTION_DIMS, RF_HEADS);
    static float obs[RF_OBS_SIZE], actions[RF_HEADS], reward, terminal, reset;
    static unsigned char mask[RF_MASK_SIZE];
    env->agents[0] = (Agent){.observations = obs, .actions = actions, .rewards = &reward, .terminals = &terminal, .resets = &reset, .action_mask = mask, .policy = 0};
    uint32_t rng = 0x9e3779b9u ^ seed;
    char path[512];
    snprintf(path, sizeof(path), "%s.names", argv[14]);
    FILE* out = fopen(argv[14], "w");
    FILE* names = fopen(path, "w");
    if (!out || !names) { fprintf(stderr, "probe_risk_duels: can't write %s\n", argv[14]); return 2; }
    puf_reset(env);
    rf_net_reset(net);
    long fights = 0, written = 0, censored = 0;
    char his[32] = "";
    while (env->log.n < sessions) {
        int chosen[RF_HEADS];
        rf_net_act(net, obs, mask, &rng, hard, RF_ACTION_DIMS, RF_HEADS, chosen);
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)chosen[h];
        const int fight_step = env->wait_left == 0;
        if (fight_step) {
            pre = env->state;
            if (!fight_open) {
                fight_open = riskfight_loadout_class(env->context.loadout[0]) == RF_CLASS_DHAROK && riskfight_loadout_class(env->context.loadout[1]) == RF_CLASS_DHAROK
                    ? 1 : -1; /* -1: a fight not written, followed to its end */
                rd_begin();
                if (env->context.opp_hook) snprintf(his, sizeof(his), "oppfit");
                else snprintf(his, sizeof(his), "script%d", env->state.opponent_persona);
            }
        }
        const Log before = env->log;
        row_written = 0;
        puf_step(env);
        if (terminal || reset) rf_net_reset(net);
        if (!fight_step) continue;
        const int ended = (env->log.kills + env->log.deaths + env->log.mutual_deaths + env->log.escapes + env->log.timeouts + env->log.ragged) >
            (before.kills + before.deaths + before.mutual_deaths + before.escapes + before.timeouts + before.ragged);
        const int cut = !ended && env->log.n != before.n;
        if (ended && fight_open > 0 && !row_written) { fprintf(stderr, "probe_risk_duels: a fight ended without Env.fight_end\n"); return 2; }
        if (!ended && !cut && fight_open > 0) rd_write_row(&env->state, &pre);
        if (ended || cut) {
            fights += ended;
            if (fight_open > 0 && rd_row_count > 0) { rd_flush(out, names, (int)written, "us", his); written++; censored += cut; }
            fight_open = 0;
        }
    }
    fclose(out);
    fclose(names);
    printf("sessions %d fights %ld written (Dharok v Dharok) %ld, of them cut by the session's end %ld\n", sessions, fights, written, censored);
    return 0;
}
