/* Standalone finite-horizon experiments. Reuse the admitted Java request parser
 * and snapshot owner; do not transcribe the production-plan protocol here.
 * Compile without NDEBUG: source-only investigation, no universal-optimal claim.
 *
 * plan REQUEST EPISODES HORIZON SELECT_WORLDS SEED train|heldout NATIVE_REV JAVA_REV MANIFEST
 * trace TAPE WIRE TICK EPISODES HORIZON SELECT_WORLDS SEED train|heldout none|all NATIVE_REV JAVA_REV MANIFEST
 * MANIFEST lists one native-root-relative source dependency path per line.
 * Run from the native root. Revisions are declared lineage; source hashes are measured.
 */
#ifdef NDEBUG
#error "Evaluator requires always-on existing mechanics/protocol assertions"
#endif
#define main riskfight_protocol_server_main
#include "gs_lookahead_server.c"
#undef main
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>

typedef struct { uint64_t n; double mean, m2; } EvalMoment;
typedef struct { EvalMoment ko, death, survival, draw, value; } EvalStats;
static void eval_fail(const char* message) { fprintf(stderr, "evaluation failed: %s\n", message); exit(2); }
static void* eval_alloc(size_t n) { void* p = calloc(1, n); if (!p) eval_fail("allocation"); return p; }
static uint32_t eval_uint(const char* text, uint32_t low, uint32_t high) {
    char* end; errno = 0;
    unsigned long long n = strtoull(text, &end, 10);
    if (!*text || text[0] == '-' || *end || errno || n < low || n > high) eval_fail("invalid unsigned integer");
    return (uint32_t)n;
}
static void eval_add(EvalMoment* s, double x) {
    if (!isfinite(x)) eval_fail("nonfinite metric");
    s->n++; double d = x - s->mean; s->mean += d / (double)s->n; s->m2 += d * (x - s->mean);
}
static void eval_result(EvalStats* s, int ko, int death, double value) {
    eval_add(&s->ko, ko); eval_add(&s->death, death);
    eval_add(&s->survival, !death); eval_add(&s->draw, !ko && !death); eval_add(&s->value, value);
}
static void eval_metric(const char* policy, const char* metric, const EvalMoment* s, int probability) {
    double low, high;
    if (probability) {
        double z2 = 1.959963984540054 * 1.959963984540054;
        double den = 1 + z2 / s->n, center = (s->mean + z2 / (2 * s->n)) / den;
        double half = 1.959963984540054 * sqrt(s->mean * (1 - s->mean) / s->n + z2 / (4 * s->n * s->n)) / den;
        low = center - half; high = center + half;
    } else {
        double half = 1.959963984540054 * sqrt(s->m2 / (s->n - 1) / s->n);
        low = s->mean - half; high = s->mean + half;
    }
    printf("metric\t%s\t%s\tn=%" PRIu64 "\tmean=%.12g\tci95=[%.12g,%.12g]\tmethod=%s\n",
        policy, metric, s->n, s->mean, low, high, probability ? "Wilson" : "paired-seed-normal-asymptotic");
}
static void eval_print(const char* name, const EvalStats* s) {
    eval_metric(name, "KO", &s->ko, 1); eval_metric(name, "death", &s->death, 1);
    eval_metric(name, "survival", &s->survival, 1); eval_metric(name, "finite_horizon_draw", &s->draw, 1);
    eval_metric(name, "expected_stake_value", &s->value, 0);
}
static uint64_t eval_hash(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) eval_fail(path);
    uint64_t hash = UINT64_C(14695981039346656037); unsigned char bytes[16384]; size_t n;
    while ((n = fread(bytes, 1, sizeof(bytes), f)))
        for (size_t i = 0; i < n; i++) { hash ^= bytes[i]; hash *= UINT64_C(1099511628211); }
    if (ferror(f)) eval_fail("source read");
    if (fclose(f)) eval_fail("source close");
    return hash;
}
static void eval_provenance(const char* native, const char* java, const char* manifest, int argc, char** argv) {
    if (!*native || !*java || strpbrk(native, "\r\n\t") || strpbrk(java, "\r\n\t")) eval_fail("invalid lineage");
    printf("lineage\tnative_declared=%s\tjava_declared=%s\tevaluator=%s\n", native, java, __FILE__);
    char root[4096];
    if (!getcwd(root, sizeof(root))) eval_fail("native root lookup");
    printf("root\tnative_measured=%s\tjava_source_revision_declared=%s\n", root, java);
    printf("config"); for (int i = 0; i < argc; i++) {
        if (strpbrk(argv[i], "\r\n\t")) eval_fail("invalid configuration text");
        printf("\t%s", argv[i]);
    } puts("");
    printf("hash\tmanifest\tfnv1a64=%016" PRIx64 "\t%s\n", eval_hash(manifest), manifest);
    FILE* f = fopen(manifest, "r"); if (!f) eval_fail("manifest open");
    char path[4096]; int count = 0;
    while (fgets(path, sizeof(path), f)) {
        char* nl = strchr(path, '\n'); if (!nl) eval_fail("manifest line requires newline"); *nl = 0;
        if (!*path || path[0] == '/' || strpbrk(path, "\r\t") || strstr(path, "..")) eval_fail("manifest requires relative source paths");
        printf("hash\tsource\tfnv1a64=%016" PRIx64 "\t%s\n", eval_hash(path), path); count++;
    }
    if (ferror(f) || !count) eval_fail("empty or unreadable manifest");
    if (fclose(f)) eval_fail("manifest close");
    puts("contract\tfinite_horizon_only\tKO_and_death_can_overlap\tdraw=neither_dead_by_horizon_including_escape\tCI=Monte_Carlo_not_model_uncertainty\tpaired_seed_not_identical_RNG_consumption");
    printf("timing\tmotor=lognormal_gaps_Fact20\tcutoff_ms=%d\ttick_ms=%d\n", MOTOR_CUTOFF_MS, MOTOR_TICK_MS);
    puts("timing_scope\tplan=one_seeded_motor_schedule_per_scenario\ttrace=C_proxy_motor_rng_reseeded_per_episode\tno_claim_of_exact_live_latency");
}
static OppModel eval_pool(const char* split) {
    if (!strcmp(split, "train")) return (OppModel){MODEL_PROFILES, NULL, 0, RISKFIGHT_HUMAN_TRAIN_PROFILES};
    if (!strcmp(split, "heldout")) return (OppModel){MODEL_PROFILES, NULL, RISKFIGHT_HUMAN_TRAIN_PROFILES, RISKFIGHT_HUMAN_HELDOUT_PROFILES};
    eval_fail("split must be train or heldout"); return (OppModel){0};
}
static void eval_config(uint32_t episodes, uint32_t seed, int horizon, int worlds) {
    if (seed > UINT32_MAX - episodes) eval_fail("seed range overflows");
    if (getenv("GS_LA_COLDHIST") || getenv("GS_LA_SAVE"))
        eval_fail("inherited lookahead state overrides are not admitted");
    printf("seeds\tpaired_episode_range=[%u,%u]\tevaluation_draw=lowbias32(seed+i)\tepisode=lowbias32(draw^0x6576616c)\tselection_domain=0x73656c65\n", seed, seed + episodes - 1);
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99;
    g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    g_la_horizon = horizon; g_la_worlds = worlds; g_la_stage = worlds;
    g_la_cache = 0; g_la_collect_latency = 0; g_la_worth_mode = 0;
    g_la_worth_scale = 0; /* Compare terminal stake value, no invented continuation credit. */
    observer_register_items();
}
static Hist eval_request_hist(const Request* q) {
    Hist h = hist_start(); h.warning.in_reach_since = q->warned; h.warning.unseen_count = q->unseen_count;
    memcpy(h.warning.unseen, q->unseen, sizeof(q->unseen));
    h.since_eat = q->belief.seen_since_eat; h.big_hit_tick = q->big_hit;
    h.since_vw_spec = q->since_vw; h.since_maul_spec = q->since_maul;
    h.ready_age = q->ready_age; h.last_swing_tick = q->last_swing; return h;
}
static void eval_plan(int argc, char** argv) {
    uint32_t n = eval_uint(argv[3], 2, UINT32_MAX - 1), seed = eval_uint(argv[6], 1, UINT32_MAX);
    int horizon = (int)eval_uint(argv[4], 1, 32), worlds = (int)eval_uint(argv[5], 2, 1000000);
    OppModel train = eval_pool("train"), held = eval_pool(argv[7]); eval_config(n, seed, horizon, worlds);
    eval_provenance(argv[8], argv[9], argv[10], argc, argv);
    printf("hash\trequest\tfnv1a64=%016" PRIx64 "\t%s\n", eval_hash(argv[2]), argv[2]);
    FILE* f = fopen(argv[2], "r"); if (!f) eval_fail("request open");
    if (fgetc(f) == EOF) eval_fail("empty request"); rewind(f);
    Request* q = eval_alloc(sizeof(*q)); read_request(f, q);
    int c; while ((c = fgetc(f)) != EOF) if (!isspace(c)) eval_fail("exactly one request required");
    if (ferror(f) || fclose(f)) eval_fail("request read/close");
    const char* missing = request_unavailable(q); if (missing) eval_fail(missing);
    for (int side = 0; side < 2; side++)
        if (!q->poison_source[side])
            eval_fail("explicit PS KNOWN/IMMUNE/CLEAR_PRIOR required for each side");
    if (q->attack_history != RISKFIGHT_ATTACK_OBSERVED || q->attack_clock_low != q->attack_clock_high) eval_fail("exact observed local clock required");
    if (la_gear_status(q->belief.gear).status != LA_GEAR_SUPPORTED) eval_fail("unsupported opponent gear");
    if (q->him <= 0 || q->us <= 0) eval_fail("positive stakes required");
    Sim* base = eval_alloc(sizeof(*base)); Sim* work = eval_alloc(sizeof(*work)); sim_init(base); sim_init(work);
    RiskfightSnapshot snap; snapshot_of(q, &snap); riskfight_load_state(&base->state, &base->context, &snap);
    Hist hist = eval_request_hist(q); LaScript scripts[LA_MACROS]; int feasible[LA_MACROS], alive[LA_MACROS];
    la_scripts(&base->state, q->tick, &q->feasible, &q->rules, scripts, feasible);
    LaScore selection[LA_MACROS]; g_la_model = &train;
    uint32_t select_seed = osrs_lowbias32(seed ^ 0x73656c65u);
    LaMacro selected = la_evaluate(&base->state, &hist, &q->belief, q->tick, (GsStakes){.us = q->us, .him = q->him}, select_seed, scripts, feasible, selection, alive);
    printf("baseline\texact_admitted_request_program\torigin_requires_request_manifest\texecution=modelled_native\tsubsequent_policy=none\tselected=%s\tselector_seed=%u\tselector_profiles=train\tevaluation_profiles=%s\n", LA_NAME[selected], select_seed, argv[7]);
    puts("regret_contract\tindependent_evaluation_worlds\tfinite_admissible_macro_set\tmax_of_sample_means_has_maximizer_optimism\tno_CI_for_maximum\tno_universal_optimality");
    printf("selection_objective\tour_stake=%ld\topponent_stake=%ld\tratio=%.17g\tcontinuation_scale=%.17g\tvalue=ratio*KO-death+ratio*survivor_ticket_worth\n",
        q->us, q->him, (double)q->him / q->us, g_la_worth_scale);
    printf("evaluation_scope\t%s\n", !strcmp(argv[7], "heldout") ?
        "heldout_profiles_separate_from_training_selection" : "training_diagnostic_not_heldout_regret");
    printf("candidate_contract\tmacros=%d\tburst_admitted=%d\tvisible_alignment_windows=%d\tpause_admitted=%d\tpid=%d\tcontinuation=live_only\tglobal_optimality=not_claimed\n",
        LA_MACROS, g_la_burst, q->feasible.incoming_count, q->feasible.pause_ok, q->feasible.pid);
    for (int m = 0; m < LA_MACROS; m++) if (feasible[m]) {
        printf("candidate\t%s", LA_NAME[m]);
        for (int i = 0; i < scripts[m].n; i++)
            printf("\top%d@%d:arg%d:command%d", scripts[m].op[i], scripts[m].due[i], scripts[m].arg[i], scripts[m].command[i].kind);
        puts("");
        printf("selection\t%s\tworlds=%d\tKO=%.12g\tdeath=%.12g\tsurvivor_ticket_worth=%.12g\tvalue=%.12g\n",
            LA_NAME[m], selection[m].worlds, selection[m].kill / selection[m].worlds,
            selection[m].death / selection[m].worlds, selection[m].tickets / selection[m].worlds,
            selection[m].value / selection[m].worlds);
    }
    EvalStats stats[LA_MACROS] = {0}; EvalMoment paired[LA_MACROS] = {0};
    RiskfightState* world = eval_alloc(sizeof(*world));
    for (uint32_t i = 0; i < n; i++) {
        uint32_t draw = osrs_lowbias32(seed + i), wr = draw | 1; Hist branch;
        la_world(&base->state, &hist, &q->belief, q->tick, &wr, world, &branch);
        uint32_t episode = osrs_lowbias32(draw ^ 0x6576616cu); double values[LA_MACROS] = {0};
        for (int m = 0; m < LA_MACROS; m++) if (feasible[m]) {
            LaOutcome out = la_rollout(work, world, &branch, q->tick, &held, &scripts[m], episode);
            values[m] = out.kill * (double)q->him - out.death * (double)q->us;
            eval_result(&stats[m], out.kill, out.death, values[m]);
        }
        for (int m = 0; m < LA_MACROS; m++) if (feasible[m]) eval_add(&paired[m], values[m] - values[selected]);
    }
    double best = stats[selected].value.mean;
    for (int m = 0; m < LA_MACROS; m++) if (feasible[m]) {
        eval_print(LA_NAME[m], &stats[m]); eval_metric(LA_NAME[m], "paired_value_minus_selected", &paired[m], 0);
        best = fmax(best, stats[m].value.mean);
    }
    printf("decision_regret\tfinite_horizon=%d\tselected=%s\tempirical_best_mean_minus_selected=%.12g\n", horizon, LA_NAME[selected], best - stats[selected].value.mean);
    sim_clear(base); sim_clear(work); free(base); free(work); free(world); free(q);
}
static void eval_trace(int argc, char** argv) {
    int tick = (int)eval_uint(argv[4], 0, INT_MAX);
    uint32_t n = eval_uint(argv[5], 2, UINT32_MAX - 1), seed = eval_uint(argv[8], 1, UINT32_MAX);
    int horizon = (int)eval_uint(argv[6], 1, 32), worlds = (int)eval_uint(argv[7], 2, 1000000);
    OppModel train = eval_pool("train"), held = eval_pool(argv[9]); eval_config(n, seed, horizon, worlds); g_la_model = &train;
    int truth;
    if (!strcmp(argv[10], "none")) truth = TRUTH_NONE;
    else if (!strcmp(argv[10], "all")) truth = TRUTH_ALL;
    else eval_fail("truth must be none or all");
    eval_provenance(argv[11], argv[12], argv[13], argc, argv);
    printf("hash\ttape\tfnv1a64=%016" PRIx64 "\t%s\n", eval_hash(argv[2]), argv[2]);
    printf("hash\twire\tfnv1a64=%016" PRIx64 "\t%s\n", eval_hash(argv[3]), argv[3]);
    Tape tape; if (!tape_load(&tape, argv[2])) eval_fail("tape unavailable");
    if (tick < tape.lo || tick > tape.hi - horizon) eval_fail("tick/horizon outside tape");
    int frames = tape.hi - tape.lo + 1;
    tape.observer_series = eval_alloc((size_t)frames * sizeof(Observer)); tape.hist_series = eval_alloc((size_t)frames * sizeof(Hist));
    WireIndex index = {0}; wire_index_build(argv[3], &index); observer_series_build(argv[3], &index, &tape, tape.observer_series); hist_series(&tape, tape.hist_series);
    Observer* observed = &tape.observer_series[tick - tape.lo];
    if (!observed->inf.local_timer.seen || observed->inf.local_timer.earliest != observed->inf.local_timer.latest) eval_fail("exact observed local clock required");
    if (!tape_pending_available(&tape, tick)) eval_fail("causal pending queue unavailable");
    if (!observed->inf.c.forecast_available || la_gear_status(observed->inf.c.opponent_gear).status != LA_GEAR_SUPPORTED) eval_fail("baseline forecast/mechanics unavailable");
    if (observed->stakes.us <= 0 || observed->stakes.him <= 0) eval_fail("positive observed stakes required");
    const TapeTick* anchor = tape_tick(&tape, tick);
    for (int i = 0; i < TAPE_BAG; i++)
        if (anchor->bag[i] >= 0 && !raw_id_to_content(anchor->bag[i])) eval_fail("unmapped local inventory item");
    for (int i = 0; i < TAPE_WORN; i++)
        if (anchor->worn[i] >= 0 && la_item_of(anchor->worn[i]).kind == LA_ITEM_UNSUPPORTED) eval_fail("unmapped local worn item");
    /* Cut animation discovery before constructing the seed. his_kit otherwise
     * discovers weapons from the full recording, including future swings. */
    Tape cut = tape; cut.hi = tick; cut.anims = eval_alloc((size_t)(tape.anim_count + 1) * sizeof(TapeAnim)); cut.anim_count = 0;
    for (int i = 0; i < tape.anim_count; i++) if (tape.anims[i].tick <= tick) cut.anims[cut.anim_count++] = tape.anims[i];
    cut.kit_count = 0;
    for (int i = 0; i < cut.anim_count; i++) {
        if (cut.anims[i].side != SIDE_HIM) continue;
        int at = 0;
        while (at < cut.kit_count && cut.kit_anims[at][0] != cut.anims[i].anim) at++;
        if (at == cut.kit_count) {
            if (at == 64) eval_fail("causal weapon kit capacity");
            cut.kit_anims[at][0] = cut.anims[i].anim;
            cut.kit_anims[at][1] = 0; cut.kit_count++;
        }
        cut.kit_anims[at][1]++;
    }
    Sim* base = eval_alloc(sizeof(*base)); Sim* work = eval_alloc(sizeof(*work)); sim_init(base); sim_init(work);
    const char* names[3] = {"closed_loop_C_proxy_uncertified", "closed_loop_lookahead_C_proxy_follower", "recorded_click_replay_not_admissible_policy"};
    OursPolicy policies[3] = {{OURS_RULES, 0, TIMING_MEASURED, STAKES_COMPUTED, truth}, {OURS_LOOKAHEAD, 0, TIMING_MEASURED, STAKES_COMPUTED, truth}, {OURS_REPLAY, 0, TIMING_MEASURED, STAKES_COMPUTED, 0}};
    printf("baseline\tC_proxy_Java_parity_uncertified\tproduction_baseline=unavailable\tauto_retaliate=OFF_PRIOR_simulation_conditioned_not_observed\tablation=%s\tevaluation_profiles=%s\tselector_profiles=train\n", truth ? "privileged_PID_energy_vengeance_HP_timer" : "matched_observed_information", argv[9]);
    if (truth) puts("privileged_scope\tC_rule_decisions_only\tlookahead_world_sampler_remains_observed_belief\treplay_has_no_truth_override");
    EvalStats stats[3] = {0}; EvalMoment gain = {0}; Outcome out;
    RiskfightState* world = eval_alloc(sizeof(*world));
    for (uint32_t i = 0; i < n; i++) {
        uint32_t draw = osrs_lowbias32(seed + i), wr = draw | 1; RiskfightSnapshot snap;
        if (!tape_snapshot(&cut, tick, &DEFAULT_PRIORS, &wr, &snap)) eval_fail("snapshot unavailable");
        /* A last-tick closed-loop decision still forecasts a full horizon.
         * Admit the immunity lower bound for that nested forecast too. */
        for (int side = 0; side < 2; side++)
            if (snap.side[side].poison_source == OSRS_POISON_HORIZON_IMMUNITY &&
                snap.side[side].poison.immunity_ticks <= 2 * horizon - 1)
                eval_fail("trace poison immunity does not cover nested decision horizon");
        if (tape_poison_weapon(anchor->opponent_gear[3]) &&
            (snap.side[0].poison_source != OSRS_POISON_HORIZON_IMMUNITY ||
             snap.side[0].poison.immunity_ticks <= 2 * horizon - 1))
            eval_fail("poison weapon requires immune local target over nested horizon");
        riskfight_load_state(&base->state, &base->context, &snap); observer_assign(&base->obs, observed);
        base->hist = ((Hist*)tape.hist_series)[tick - tape.lo]; base->seen_since_eat = base->hist.since_eat;
        LaBelief belief = la_belief_of(base, tick); Hist branch;
        la_world(&base->state, &base->hist, &belief, tick, &wr, world, &branch);
        double values[3]; uint32_t episode = osrs_lowbias32(draw ^ 0x6576616cu);
        for (int p = 0; p < 3; p++) {
            work->state = *world; work->hist = branch; memset(g_la_memo, 0, sizeof(g_la_memo)); g_la_decision_count = 0;
            run_steps(work, &tape, tick, horizon, &held, &policies[p], episode, &out);
            values[p] = (out.his_death >= 0) * (double)observed->stakes.him - (out.our_death >= 0) * (double)observed->stakes.us;
            eval_result(&stats[p], out.his_death >= 0, out.our_death >= 0, values[p]);
        }
        eval_add(&gain, values[1] - values[0]);
    }
    for (int p = 0; p < 3; p++) eval_print(names[p], &stats[p]);
    eval_metric(names[1], "paired_value_minus_C_proxy", &gain, 0);
    printf("decision_regret\tfinite_policy_set=C_proxy,lookahead\tfinite_horizon=%d\tempirical_best_mean_minus_C_proxy=%.12g\tproduction_regret=unavailable\n", horizon, fmax(0, stats[1].value.mean - stats[0].value.mean));
    sim_clear(base); sim_clear(work); free(base); free(work); free(world); free(cut.anims); tape_free(&tape);
}
int main(int argc, char** argv) {
    double started = la_seconds();
    if (argc == 11 && !strcmp(argv[1], "plan")) eval_plan(argc, argv);
    else if (argc == 14 && !strcmp(argv[1], "trace")) eval_trace(argc, argv);
    else eval_fail("usage: plan REQUEST N H SELECT_WORLDS SEED train|heldout NATIVE_REV JAVA_REV MANIFEST; trace TAPE WIRE TICK N H SELECT_WORLDS SEED train|heldout none|all NATIVE_REV JAVA_REV MANIFEST; production closed-loop baseline is not available");
    printf("completion\tstatus=success\telapsed_monotonic_seconds=%.9f\tlookahead_calls=%ld\tlookahead_rollouts=%ld\n", la_seconds() - started, g_la.calls, g_la.rollouts);
    if (fflush(stdout) || ferror(stdout)) eval_fail("output write");
    return 0;
}
