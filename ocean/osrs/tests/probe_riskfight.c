/* Closed-loop fresh-start fights of one of our policies against the scripted human-profile population, the same
 * population and reset the RL env (ocean/osrs_riskfight) trains on.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/probe_riskfight ocean/osrs/tests/probe_riskfight.c -lm
 *   /tmp/probe_riskfight POLICY SPLIT FIRST_SEED EPISODES [MODEL.fit EAT_MIXTURE.fit WORLDS HORIZON]
 *   POLICY = rules | lookahead | script | atrisk | idle     SPLIT = train | heldout
 * Run from the repo root. Prints one tab-separated summary line. */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "riskfight_rollout.h"
#include "rf_net.h"

static RiskfightAlign g_align, g_total;
static void probe_hook(const void* p, int step) {
    (void)step;
    const Sim* sim = p;
    const HumanCommandQueue* q = &sim->context.policy_commands[0].commands;
    int stop = 0;
    for (int i = 0; i < q->count; i++) stop |= q->items[i].kind == HUMAN_COMMAND_STOP;
    riskfight_align_observe(&sim->state, 0, stop, &g_align);
}

static RfNet* g_net;
static uint32_t g_net_rng = 12345;
static int g_net_hard;
static void net_policy(const RiskfightState* s, int* actions) {
    float obs[RF_OBS_SIZE];
    unsigned char mask[RF_MASK_SIZE];
    riskfight_write_observation(s, 0, obs);
    riskfight_write_action_mask(s, 0, mask);
    rf_net_act(g_net, obs, mask, &g_net_rng, g_net_hard, RF_ACTION_DIMS, RF_HEADS, actions);
}
static Outcome g_out;
static Sim g_sim;

int main(int argc, char** argv) {
    assert(argc == 5 || argc == 9 || argc == 8 && !strcmp(argv[1], "net"));
    const char* name = argv[1];
    int heldout = !strcmp(argv[2], "heldout");
    uint32_t first = (uint32_t)strtoul(argv[3], NULL, 10);
    int episodes = atoi(argv[4]);
    OursPolicy policy = {OURS_RULES, 0, TIMING_MEASURED, STAKES_COMPUTED, getenv("PROBE_TRUTH") ? atoi(getenv("PROBE_TRUTH")) : 0};
    if (!strcmp(name, "lookahead")) policy.kind = OURS_LOOKAHEAD;
    else if (!strcmp(name, "script")) policy.kind = OURS_SCRIPT;
    else if (!strcmp(name, "atrisk")) { policy.kind = OURS_EAT_AT_RISK; policy.eat_below = 8; }
    else if (!strcmp(name, "idle")) policy.kind = OURS_IDLE;
    else if (!strcmp(name, "net")) policy.kind = OURS_NET;
    else assert(!strcmp(name, "rules"));
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99;
    g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    observer_register_items();
    static OppFit fit;
    static OppModel la_model;
    if (policy.kind == OURS_NET) {
        /* net SPLIT FIRST_SEED EPISODES CHECKPOINT HIDDEN LAYERS: sampled actions unless PROBE_HARD is set. */
        g_net = rf_net_load(argv[5], RF_OBS_SIZE, atoi(argv[6]), atoi(argv[7]), RF_ACTION_DIMS, RF_HEADS);
        g_policy_hook = net_policy;
        g_net_hard = getenv("PROBE_HARD") != NULL;
    }
    const char* fit_path = argc == 9 ? argv[5] : getenv("PROBE_FIT");
    const char* mix_path = argc == 9 ? argv[6] : getenv("PROBE_MIX");
    if (fit_path) {
        opp_load(fit_path, mix_path, &fit, &g_eat_model);
        if (argc == 9) { g_la_worlds = atoi(argv[7]); g_la_horizon = atoi(argv[8]); }
        la_model = (OppModel){MODEL_FIT, &fit};
        g_la_model = &la_model;
        g_la_cache = 0;
    }
    OppModel model = getenv("PROBE_OPPFIT") ? la_model : heldout ?
        (OppModel){MODEL_PROFILES, NULL, RISKFIGHT_HUMAN_TRAIN_PROFILES, RISKFIGHT_HUMAN_HELDOUT_PROFILES} :
        (OppModel){MODEL_PROFILES, NULL, 0, RISKFIGHT_HUMAN_TRAIN_PROFILES};
    sim_init(&g_sim);
    if (getenv("PROBE_LIVE")) for (int i = 0; i < 2; i++) g_sim.context.loadout[i] = &RISKFIGHT_LIVE_LOADOUT;
    riskfight_deployable = getenv("PROBE_DEPLOY") != NULL;
    g_step_hook = probe_hook;
    g_attack_first = getenv("PROBE_ATTACK_FIRST") != NULL;
    Tape tape = {0};
    static Observer observer;
    static Hist hist;
    tape.observer_series = &observer;
    tape.hist_series = &hist;
    double outcome_count[6] = {0}, ticks = 0, meals = 0, drinks = 0, spec_vw = 0, spec_maul = 0, sum_hp_dealt = 0;
    double started = la_seconds();
    for (int e = 0; e < episodes; e++) {
        uint32_t seed = first + (uint32_t)e;
        riskfight_reset((EncounterState*)&g_sim.state, (EncounterContext*)&g_sim.context, osrs_lowbias32(seed) | 1);
        for (int side = 0; side < 2; side++) g_sim.state.poison_source[side] = OSRS_POISON_CLEAR_PRIOR;
        if (getenv("PROBE_HP")) for (int i = 0; i < 2; i++) g_sim.state.env.players[i].current_hitpoints = atoi(getenv("PROBE_HP"));
        if (g_net) { rf_net_reset(g_net); g_net_rng = osrs_lowbias32(seed ^ 0x6e6574u) | 1; }
        memset(&observer, 0, sizeof(observer));
        gs_infer_init(&observer.inf);
        observer.bar_hit_tick = INT_MIN / 2;
        observer.rival_tracked = -1;
        hist = hist_start();
        g_script_profile = &RISKFIGHT_HUMAN_PROFILES[getenv("PROBE_PROFILE") ? atoi(getenv("PROBE_PROFILE")) : osrs_lowbias32(seed ^ 0xabcdefu) % RISKFIGHT_HUMAN_TRAIN_PROFILES];
        memset(g_la_memo, 0, sizeof(g_la_memo));
        g_sim.hist = hist;
        riskfight_align_reset(&g_align);
        run_steps(&g_sim, &tape, 0, MAX_STEPS, &model, &policy, osrs_lowbias32(seed ^ 0x6576616cu), &g_out);
        const RiskfightState* s = &g_sim.state;
        outcome_count[s->outcome[0]]++;
        ticks += s->env.tick;
        meals += s->eat_marlin[0] + s->eat_halibut[0] + s->eat_pie[0];
        drinks += s->drink_brew[0] + s->drink_sanfew[0] + s->drink_combat[0];
        spec_vw += s->spec_voidwaker[0]; spec_maul += s->spec_maul[0];
        sum_hp_dealt += g_out.our_damage;
        g_total.swings += g_align.swings; g_total.aligned += g_align.aligned; g_total.adjacent += g_align.adjacent;
        g_total.spec_swings += g_align.spec_swings; g_total.spec_aligned += g_align.spec_aligned; g_total.spec_adjacent += g_align.spec_adjacent;
        g_total.stops += g_align.stops; g_total.ready_idle += g_align.ready_idle; g_total.vengs += g_align.vengs; g_total.stop_spec += g_align.stop_spec; g_total.veng_spec += g_align.veng_spec; g_total.stop_veng_spec += g_align.stop_veng_spec; g_total.spec_delay += g_align.spec_delay; g_total.eats += g_align.eats; g_total.norm_delay += g_align.norm_delay;
    }
    double n = episodes, wall = la_seconds() - started;
    printf("policy\t%s\tsplit\t%s\tepisodes\t%d\tkill\t%.4f\tdeath\t%.4f\tescape\t%.4f\tmutual\t%.4f\ttimeout\t%.4f\tticks\t%.1f\tmeals\t%.2f\tdrinks\t%.2f\t"
        "vw_spec\t%.2f\tmaul_spec\t%.2f\tdealt\t%.1f\tswings\t%.2f\taligned\t%.2f\tadjacent\t%.2f\tspec_swings\t%.2f\tspec_aligned\t%.2f\tspec_adjacent\t%.2f\t"
        "stops\t%.2f\tready_idle\t%.2f\tvengs\t%.2f\tstop_spec\t%.3f\tveng_spec\t%.3f\tstop_veng_spec\t%.3f\tspec_delay\t%.3f\tnorm_delay\t%.3f\teats\t%.2f\twall_s\t%.1f\n",
        name, argv[2], episodes, outcome_count[RISKFIGHT_KILL] / n, outcome_count[RISKFIGHT_DEATH] / n, outcome_count[RISKFIGHT_ESCAPE] / n,
        outcome_count[RISKFIGHT_MUTUAL_DEATH] / n, outcome_count[RISKFIGHT_TIMEOUT] / n, ticks / n, meals / n, drinks / n, spec_vw / n, spec_maul / n, sum_hp_dealt / n,
        g_total.swings / n, g_total.aligned / n, g_total.adjacent / n, g_total.spec_swings / n, g_total.spec_aligned / n, g_total.spec_adjacent / n,
        g_total.stops / n, g_total.ready_idle / n, g_total.vengs / n, g_total.stop_spec / n, g_total.veng_spec / n, g_total.stop_veng_spec / n, g_total.spec_delay / n, g_total.norm_delay / n, g_total.eats / n, wall);
    return 0;
}
