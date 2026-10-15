/* How much of the macro values' error is the baseline continuation (scripted clicks, then hold)?
 *
 *   cc -std=c11 -O2 -I. -o evaluate_continuation ocean/osrs/tests/evaluate_continuation.c -lm      (from the native root, no NDEBUG)
 *   evaluate_continuation continuation SCENARIOS EPISODES HORIZON WORLDS train|heldout SEED JOBS INNER_WORLDS PROXY_WORLDS
 *   evaluate_continuation lock SCENARIO_INDEX HORIZON SEED
 *
 * Each decision point is a synthetic scenario of the evaluator corpus (evaluate_riskfight_policy.c, `corpus`). For every feasible macro m:
 *   predicted hold   : the deployed la_override value on belief worlds (hold continuation)
 *   predicted proxy  : the same worlds and seeds, but after the scripted clicks the C rules proxy plays closed-loop (g_la_continue_mode = 1)
 *   truth hold       : the macro's script rolled out from the true sim state, then hold (same horizon as the model)
 *   truth proxy      : the same with the rules proxy as continuation
 *   truth policy     : the macro committed at step 0, then the deployed policy decides every tick (policy-iteration continuation, nested lookahead)
 * Values are KO * (his stake / our stake) - death, the units of la_evaluate. truth-* use EPISODES paired episode seeds (opponent drawn from the
 * evaluation pool, hidden state exact), so truth hold vs truth policy isolates the continuation; predicted vs truth adds belief error. */
#define RISKFIGHT_EVALUATOR_LIBRARY
#include "evaluate_riskfight_policy.c"

typedef struct {
    int feasible[LA_MACROS], best;
    double ratio, pv[LA_MACROS], pvp[LA_MACROS], pk[LA_MACROS], pd[LA_MACROS], bv[LA_MACROS]; /* pv: hold@WORLDS, pvp/bv: hold/proxy continuation @PROXY_WORLDS (same worlds) */
    double th[LA_MACROS][3], tb[LA_MACROS][3], tp[LA_MACROS][3]; /* ko rate, death rate, mean value */
    double th_sq[LA_MACROS], tp_sq[LA_MACROS];
} CtRow;
typedef struct { uint32_t index; CtRow row; } CtMessage;

static double ct_value(int ko, int death, double ratio) { return ko * ratio - death; }

static void ct_decide(Sim* ds, const Sim* base, int mode, LaDecision* out) {
    g_la_continue_mode = mode;
    sc_load(ds, base, 1, 0);
    if (mode == 1) { g_la_proxy.ours = ds->ours; g_la_proxy.ours.lookahead = 0; g_la_proxy.observer = &base->obs; g_la_proxy.step = 0; }
    la_override(ds, &ds->ours, 0, base->tick_base);
    g_la_continue_mode = 0;
    *out = g_la_decision;
    observer_clear(&ds->obs);
}

static int ct_inner, ct_proxy_worlds;
static void ct_row(CtRow* row, Sim* base, Sim* ds, Sim* rollout, Sim* episode, const OppModel* held, int episodes, int horizon, uint32_t seed) {
    static LaDecision hold, hold_small, proxy;
    memset(row, 0, sizeof(*row));
    ct_decide(ds, base, 0, &hold);
    int worlds = g_la_worlds; g_la_worlds = g_la_stage = ct_proxy_worlds; /* the proxy rollout costs ~65 ms: its worlds are the first PROXY_WORLDS of the same stream */
    ct_decide(ds, base, 0, &hold_small);
    ct_decide(ds, base, 1, &proxy);
    g_la_worlds = g_la_stage = worlds;
    GsStakes stakes = base->obs.stakes;
    double ratio = (double)stakes.him / (double)stakes.us;
    row->ratio = ratio; row->best = hold.best;
    int tick = base->tick_base;
    for (int m = 0; m < LA_MACROS; m++) {
        if (!hold.feasible[m]) continue;
        if (!proxy.feasible[m] || !hold_small.feasible[m] || hold.score[m].worlds != g_la_worlds) eval_fail("macro feasibility changed with the continuation mode");
        row->feasible[m] = 1;
        row->pv[m] = hold.score[m].value / hold.score[m].worlds; row->pk[m] = hold.score[m].kill / hold.score[m].worlds; row->pd[m] = hold.score[m].death / hold.score[m].worlds;
        row->pvp[m] = hold_small.score[m].value / hold_small.score[m].worlds; row->bv[m] = proxy.score[m].value / proxy.score[m].worlds;
        for (int e = 0; e < episodes; e++) {
            uint32_t episode_seed = osrs_lowbias32(seed + 7919u * (uint32_t)e + 101u * (uint32_t)m) | 1;
            LaOutcome h = la_rollout(rollout, &base->state, &base->hist, tick, held, &hold.scripts[m], episode_seed);
            g_la_continue_mode = 1; g_la_proxy.step = 0; g_la_proxy.observer = &base->obs;
            LaOutcome b = la_rollout(rollout, &base->state, &base->hist, tick, held, &hold.scripts[m], episode_seed);
            g_la_continue_mode = 0;
            int outer_worlds = g_la_worlds; g_la_worlds = g_la_stage = ct_inner; /* the nested deployed policy decides at its live world count */
            ScOutcome p = sc_episode(episode, base, SC_DEPLOYED, &hold.scripts[m], held, episode_seed, horizon);
            g_la_worlds = g_la_stage = outer_worlds;
            double vh = ct_value(h.kill, h.death, ratio), vp = ct_value(p.ko, p.death, ratio);
            row->th[m][0] += h.kill; row->th[m][1] += h.death; row->th[m][2] += vh; row->th_sq[m] += vh * vh;
            row->tb[m][0] += b.kill; row->tb[m][1] += b.death; row->tb[m][2] += ct_value(b.kill, b.death, ratio);
            row->tp[m][0] += p.ko; row->tp[m][1] += p.death; row->tp[m][2] += vp; row->tp_sq[m] += vp * vp;
        }
        for (int k = 0; k < 3; k++) { row->th[m][k] /= episodes; row->tb[m][k] /= episodes; row->tp[m][k] /= episodes; }
        row->th_sq[m] /= episodes; row->tp_sq[m] /= episodes;
    }
}

/* The deployed tie rule: continue unless another macro is strictly better. */
static int ct_argmax(const CtRow* row, const double* value) {
    int best = LA_CONTINUE;
    for (int m = 1; m < LA_MACROS; m++) if (row->feasible[m] && value[m] > value[best]) best = m;
    return best;
}
static void ct_values(const double (*table)[3], double* out) { for (int m = 0; m < LA_MACROS; m++) out[m] = table[m][2]; }

typedef struct { long n; double ko, death, kill_hold; } CtBin;

static void ct_report(const CtMessage* rows, int count, int episodes) {
    long pairs = 0, decisions = 0, flips[6] = {0};
    double d_pred = 0, d_th_tp = 0, s_th_tp = 0, d_th_tb = 0, s_th_tb = 0, d_p_tp = 0, d_p_th = 0, se_th = 0, se_tp = 0, d_pb_tp = 0;
    CtBin kbin[5] = {{0}}, dbin[4] = {{0}}; double kb_pred[5] = {0}, db_pred[4] = {0};
    static const double kcut[4] = {0.05, 0.2, 0.5, 0.8}, dcut[3] = {0.01, 0.05, 0.2};
    double cal_pk = 0, cal_tp_k = 0, cal_th_k = 0, cal_pd = 0, cal_tp_d = 0, cal_th_d = 0;
    for (int r = 0; r < count; r++) {
        const CtRow* row = &rows[r].row; double pv[LA_MACROS], bv[LA_MACROS], th[LA_MACROS], tb[LA_MACROS], tp[LA_MACROS];
        ct_values(row->th, th); ct_values(row->tb, tb); ct_values(row->tp, tp);
        double pvp[LA_MACROS];
        for (int m = 0; m < LA_MACROS; m++) { pv[m] = row->pv[m]; pvp[m] = row->pvp[m]; bv[m] = row->bv[m]; }
        for (int m = 0; m < LA_MACROS; m++) if (row->feasible[m]) {
            pairs++;
            d_pred += fabs(row->pvp[m] - row->bv[m]);
            d_th_tp += fabs(row->th[m][2] - row->tp[m][2]); s_th_tp += row->tp[m][2] - row->th[m][2];
            d_th_tb += fabs(row->th[m][2] - row->tb[m][2]); s_th_tb += row->tb[m][2] - row->th[m][2];
            d_p_tp += fabs(row->pv[m] - row->tp[m][2]); d_p_th += fabs(row->pv[m] - row->th[m][2]); d_pb_tp += fabs(row->bv[m] - row->tp[m][2]);
            se_th += sqrt(fmax(0, row->th_sq[m] - row->th[m][2] * row->th[m][2]) / episodes);
            se_tp += sqrt(fmax(0, row->tp_sq[m] - row->tp[m][2] * row->tp[m][2]) / episodes);
        }
        int c_pred = ct_argmax(row, pv), c_ps = ct_argmax(row, pvp), c_pb = ct_argmax(row, bv), c_th = ct_argmax(row, th), c_tb = ct_argmax(row, tb), c_tp = ct_argmax(row, tp);
        if (c_pred != row->best) eval_fail("harness argmax disagrees with la_evaluate");
        decisions++;
        flips[0] += c_ps != c_pb; flips[1] += c_th != c_tp; flips[2] += c_th != c_tb; flips[3] += c_pred != c_tp; flips[4] += c_pred != c_th; flips[5] += c_tb != c_tp;
        int b = row->best;
        double pk = row->pk[b], pd = row->pd[b];
        cal_pk += pk; cal_tp_k += row->tp[b][0]; cal_th_k += row->th[b][0]; cal_pd += pd; cal_tp_d += row->tp[b][1]; cal_th_d += row->th[b][1];
        int kb = 0; while (kb < 4 && pk >= kcut[kb]) kb++;
        kbin[kb].n++; kbin[kb].ko += row->tp[b][0]; kbin[kb].kill_hold += row->th[b][0]; kb_pred[kb] += pk;
        int db = 0; while (db < 3 && pd >= dcut[db]) db++;
        dbin[db].n++; dbin[db].death += row->tp[b][1]; dbin[db].kill_hold += row->th[b][1]; db_pred[db] += pd;
    }
    printf("continuation\tdecision_points=%ld\tmacro_pairs=%ld\tmean_feasible_macros=%.2f\ttruth_episodes_per_macro=%d\n", decisions, pairs, (double)pairs / decisions, episodes);
    printf("table\terror of the macro values (units: KO*stake_ratio - death; mean over decision point x feasible macro)\n");
    printf("row\tmean_abs_dV predicted: hold vs rules-proxy continuation (same belief worlds)\t%.5f\n", d_pred / pairs);
    printf("row\tmean_abs_dV truth: hold vs deployed-policy continuation (pure continuation error)\t%.5f\tsigned(policy-hold)=%+.5f\tmc_se(hold)=%.5f\tmc_se(policy)=%.5f\n", d_th_tp / pairs, s_th_tp / pairs, se_th / pairs, se_tp / pairs);
    printf("row\tmean_abs_dV truth: hold vs rules-proxy continuation\t%.5f\tsigned(proxy-hold)=%+.5f\n", d_th_tb / pairs, s_th_tb / pairs);
    printf("row\tmean_abs_error predicted(hold,belief) vs truth-policy (total: belief + continuation)\t%.5f\n", d_p_tp / pairs);
    printf("row\tmean_abs_error predicted(hold,belief) vs truth-hold (belief/sampling only)\t%.5f\n", d_p_th / pairs);
    printf("row\tmean_abs_error predicted(proxy,belief) vs truth-policy\t%.5f\n", d_pb_tp / pairs);
    printf("table\tdecision flips (argmax changes, deployed tie rule: continue unless strictly better), %% of decision points\n");
    const char* names[6] = {"predicted hold -> predicted proxy-continuation", "truth hold -> truth policy-continuation", "truth hold -> truth proxy-continuation",
        "predicted hold (deployed choice) -> truth policy-continuation", "predicted hold -> truth hold", "truth proxy -> truth policy"};
    for (int i = 0; i < 6; i++) printf("row\t%s\t%.2f%%\t(%ld/%ld)\n", names[i], 100.0 * flips[i] / decisions, flips[i], decisions);
    printf("table\tcalibration of the chosen macro: predicted vs realised (realised = macro at step 0 then the deployed policy, exact hidden state)\n");
    printf("row\toverall kill predicted %.4f realised(policy) %.4f realised(hold on truth) %.4f | death predicted %.4f realised(policy) %.4f realised(hold on truth) %.4f\n",
        cal_pk / decisions, cal_tp_k / decisions, cal_th_k / decisions, cal_pd / decisions, cal_tp_d / decisions, cal_th_d / decisions);
    static const char* kname[5] = {"<0.05", "0.05-0.2", "0.2-0.5", "0.5-0.8", ">=0.8"}, *dname[4] = {"<0.01", "0.01-0.05", "0.05-0.2", ">=0.2"};
    for (int i = 0; i < 5; i++) if (kbin[i].n) printf("row\tkill bin %s\tn=%ld\tpredicted %.4f\trealised(policy) %.4f\trealised(hold,truth) %.4f\n", kname[i], kbin[i].n, kb_pred[i] / kbin[i].n, kbin[i].ko / kbin[i].n, kbin[i].kill_hold / kbin[i].n);
    for (int i = 0; i < 4; i++) if (dbin[i].n) printf("row\tdeath bin %s\tn=%ld\tpredicted %.4f\trealised(policy) %.4f\trealised(hold,truth) %.4f\n", dname[i], dbin[i].n, db_pred[i] / dbin[i].n, dbin[i].death / dbin[i].n, dbin[i].kill_hold / dbin[i].n);
}

static void ct_continuation(char** argv) {
    uint32_t scenarios = eval_uint(argv[2], 2, 1000000), episodes = eval_uint(argv[3], 1, 100000), seed = eval_uint(argv[7], 1, UINT32_MAX);
    int horizon = (int)eval_uint(argv[4], 1, 32), worlds = (int)eval_uint(argv[5], 2, 1000000), jobs = (int)eval_uint(argv[8], 1, 256);
    ct_inner = (int)eval_uint(argv[9], 2, 1000000); ct_proxy_worlds = (int)eval_uint(argv[10], 2, 1000000);
    OppModel train = eval_pool("train"), held = eval_pool(argv[6]); eval_config(scenarios, seed, horizon, worlds); g_la_model = &train;
    printf("config\tscenarios=%u\tepisodes=%u\thorizon=%d\tworlds=%d\tinner_worlds=%d\tproxy_worlds=%d\tsplit=%s\tseed=%u\tjobs=%d\n", scenarios, episodes, horizon, worlds, ct_inner, ct_proxy_worlds, argv[6], seed, jobs);
    int fds[256];
    for (int j = 0; j < jobs; j++) {
        int pipefd[2]; if (pipe(pipefd)) eval_fail("pipe");
        pid_t pid = fork(); if (pid < 0) eval_fail("fork");
        if (pid == 0) {
            close(pipefd[0]);
            Sim* base = eval_alloc(sizeof(*base)); Sim* ds = eval_alloc(sizeof(*ds)); Sim* rollout = eval_alloc(sizeof(*rollout)); Sim* episode = eval_alloc(sizeof(*episode));
            sim_init(base); sim_init(ds); sim_init(rollout); sim_init(episode);
            CtMessage* message = eval_alloc(sizeof(*message));
            for (uint32_t i = j; i < scenarios; i += (uint32_t)jobs) {
                Scenario sc; uint32_t draw = i;
                for (;;) { sc_make(&sc, seed, draw); if (!sc_start(base, &sc, &held)) break; draw += scenarios; }
                message->index = i;
                ct_row(&message->row, base, ds, rollout, episode, &held, (int)episodes, horizon, osrs_lowbias32(seed + i * 104729u) | 1);
                if (write(pipefd[1], message, sizeof(*message)) != (ssize_t)sizeof(*message)) eval_fail("row write");
            }
            _exit(0);
        }
        close(pipefd[1]); fds[j] = pipefd[0];
    }
    CtMessage* rows = eval_alloc(scenarios * sizeof(*rows)); int count = 0;
    for (int j = 0; j < jobs; j++) {
        for (;;) {
            CtMessage* message = &rows[count]; size_t got = 0;
            while (got < sizeof(*message)) { ssize_t n = read(fds[j], (char*)message + got, sizeof(*message) - got); if (n < 0) eval_fail("shard read"); if (!n) break; got += (size_t)n; }
            if (!got) break;
            if (got != sizeof(*message)) eval_fail("short shard row");
            count++;
        }
        close(fds[j]);
    }
    for (int j = 0; j < jobs; j++) { int status; if (wait(&status) < 0 || !WIFEXITED(status) || WEXITSTATUS(status)) eval_fail("shard failed"); }
    if ((uint32_t)count != scenarios) eval_fail("missing decision points");
    ct_report(rows, count, (int)episodes);
}

/* lock: eat-then-hit inside the meal lock. Per tick under the true sim: our hp, swings, attack timer and the three food clocks, for the meal macros. */
static void ct_lock(char** argv) {
    uint32_t index = eval_uint(argv[2], 0, 1000000), seed = eval_uint(argv[4], 1, UINT32_MAX); int horizon = (int)eval_uint(argv[3], 1, 32);
    OppModel train = eval_pool("train"), held = eval_pool("heldout"); eval_config(2, seed, horizon, 64); g_la_model = &train;
    Sim* base = eval_alloc(sizeof(*base)); Sim* ds = eval_alloc(sizeof(*ds)); Sim* work = eval_alloc(sizeof(*work)); sim_init(base); sim_init(ds); sim_init(work);
    Scenario sc; uint32_t draw = index;
    for (;;) { sc_make(&sc, seed, draw); if (!sc_start(base, &sc, &held)) break; draw += 1000000; }
    static LaDecision dec; ct_decide(ds, base, 0, &dec);
    const Player* us0 = &base->state.env.players[0];
    printf("scenario\t%u\ttick=%d\tour_hp=%d\this_hp=%d\tattack_timer=%d\tfood=%d\tkarambwan=%d\tpotion=%d\n", index, base->tick_base, us0->current_hitpoints, base->state.env.players[1].current_hitpoints,
        us0->attack_timer, us0->food_timer, us0->karambwan_timer, us0->potion_timer);
    static const LaMacro macros[] = {LA_NONE, LA_EAT_NOW, LA_EAT_TRIPLE, LA_EAT_STAGGER};
    /* Probe: a second anglerfish one tick after the first (inside its 3-tick food clock) must be refused, a karambwan and a brew on that tick accepted. */
    LaScript probe = {.production = 1}; int hard[2] = {-1, -1};
    for (int i = 0, n = 0; i < OSRS_INVENTORY_SIZE && n < 2; i++)
        if (osrs_inventory_cell_metadata(&base->state.env.players[0].inventory_cells[i])->consumable_kind == OSRS_CONSUMABLE_ANGLERFISH) hard[n++] = i;
    int fast = slot_of_kind(&base->state.env.players[0], OSRS_CONSUMABLE_KARAMBWAN), brew = slot_of_kind(&base->state.env.players[0], OSRS_CONSUMABLE_BREW);
    if (hard[1] >= 0 && fast >= 0 && brew >= 0) {
        int slots[4] = {hard[0], hard[1], fast, brew}, due[4] = {1, 2, 2, 2};
        for (int i = 0; i < 4; i++) { probe.due[probe.n] = base->tick_base + due[i]; probe.command[probe.n++] = click_command(slots[i]); }
    }
    for (size_t k = 0; k < sizeof(macros) / sizeof(*macros) + 1; k++) {
        int is_probe = k == sizeof(macros) / sizeof(*macros);
        LaMacro m = is_probe ? LA_NONE : macros[k]; if (is_probe && !probe.n) { puts("macro\tlock probe\tneeds two anglerfish, a karambwan and a brew"); continue; }
        if (!is_probe && !dec.feasible[m]) { printf("macro\t%s\tinfeasible\n", LA_NAME[m]); continue; }
        if (is_probe) puts("macro\tlock probe\tanglerfish@+1, anglerfish@+2 (food lock), karambwan@+2, brew@+2");
        else printf("macro\t%s\tclicks=%d\tpredicted_kill=%.3f\tpredicted_death=%.3f\tvalue=%+.4f\n", LA_NAME[m], dec.scripts[m].n, dec.score[m].kill / dec.score[m].worlds, dec.score[m].death / dec.score[m].worlds, dec.score[m].value / dec.score[m].worlds);
        LaScript script = is_probe ? probe : dec.scripts[m]; int cursor = 0, resume = base->tick_base + 1;
        static Outcome out; memset(&out, 0, sizeof(out)); work->outcome = &out;
        work->state = base->state; work->hist = base->hist; work->tick_base = base->tick_base; observer_assign(&work->obs, &base->obs);
        RiskfightState* s = &work->state; HumanInput* hi = &work->context.policy_commands[0]; g_rollout_seed = seed;
        s->env.pvp_runtime.hit_observer = sim_observe_hit; s->env.pvp_runtime.hit_observer_context = work;
        s->env.rng_state = osrs_lowbias32(seed) | 1;
        for (int step = 0; step < horizon && !s->env.episode_over; step++) {
            int actions[RF_HEADS]; opponent_actions(&held, work, step, seed + (uint32_t)step * 2654435761u, actions);
            riskfight_policy_commands(s, 1, actions, &work->context.policy_commands[1]);
            human_input_clear_pending(hi);
            la_script_step(&script, s, hi, base->tick_base + 1 + step, &cursor, &resume, 0, 0);
            int clicks = hi->commands.count; work->step_damage = work->step_max_hit = work->step_direct = 0; work->obs.hit_count = 0;
            riskfight_step_queues(s, &work->context, &hi->commands, &work->context.policy_commands[1].commands);
            const Player* us = &s->env.players[0];
            printf("tick\t+%d\tclicks=%d\thp=%d\tour_swing=%d\this_swing=%d\tattack_timer=%d\tfood=%d\tkarambwan=%d\tpotion=%d\tdmg_on_him=%d\this_hit_landed=%d\n", step + 1, clicks, us->current_hitpoints,
                us->attack_style_this_tick != ATTACK_STYLE_NONE, s->visible[0].last_attack_tick == s->env.tick - 1, us->attack_timer, us->food_timer, us->karambwan_timer, us->potion_timer, work->step_damage, s->env.players[1].hit_landed_this_tick);
            observer_step(s, &work->obs, base->tick_base + 1 + step);
        }
        observer_clear(&work->obs);
    }
}

int main(int argc, char** argv) {
    if (argc == 11 && !strcmp(argv[1], "continuation")) ct_continuation(argv);
    else if (argc == 5 && !strcmp(argv[1], "lock")) ct_lock(argv);
    else eval_fail("usage: continuation SCENARIOS EPISODES HORIZON WORLDS train|heldout SEED JOBS INNER_WORLDS PROXY_WORLDS; lock SCENARIO_INDEX HORIZON SEED");
    if (fflush(stdout) || ferror(stdout)) eval_fail("output write");
    return 0;
}
