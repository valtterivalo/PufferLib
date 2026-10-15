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
#include "riskfight_spectator.h"
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/wait.h>

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
    const char* names[3] = {"rules_proxy_C_uncertified", "deployed_macro_policy", "recorded_click_replay_not_admissible_policy"};
    OursPolicy policies[3] = {{OURS_RULES, 0, TIMING_MEASURED, STAKES_COMPUTED, truth}, {OURS_LOOKAHEAD, 0, TIMING_MEASURED, STAKES_COMPUTED, truth}, {OURS_REPLAY, 0, TIMING_MEASURED, STAKES_COMPUTED, 0}};
    printf("baseline\tC_rules_proxy_Java_planner_stand_in_uncertified\tdeployed=la_decide_every_tick_continue_is_committed_remainder_hold_continuation\tauto_retaliate=OFF_PRIOR_simulation_conditioned_not_observed\tablation=%s\tevaluation_profiles=%s\tselector_profiles=train\n", truth ? "privileged_PID_energy_vengeance_HP_timer" : "matched_observed_information", argv[9]);
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
    eval_metric(names[1], "paired_value_minus_rules_proxy", &gain, 0);
    printf("decision_regret\tfinite_policy_set=rules_proxy,deployed\tfinite_horizon=%d\tempirical_best_mean_minus_rules_proxy=%.12g\tproduction_regret=unavailable\n", horizon, fmax(0, stats[1].value.mean - stats[0].value.mean));
    sim_clear(base); sim_clear(work); free(base); free(work); free(world); free(cut.anims); tape_free(&tape);
}
/* ---- Synthetic scenario corpus (no admissible recorded tape/wire pair exists for the current parsers: the retained tapes have no exact local clock and
 * the 20261011 wires predate the spec-timing grammar). A scenario is a RiskfightSnapshot drawn from (corpus seed, index), loaded through the
 * production loader, then warmed for SC_WARM ticks with both sides auto-attacking so the plugin-side observer infers timers, PID, bar and gear
 * from sim events exactly as in a rollout. The decision point is the end of the warm-up. ---- */
enum { SC_TICK0 = 100 };
/* EVAL_UPKEEP=prayer|stats|magic: scenarios in which our upkeep is at stake (and the deployed policy is compared with and without the drink macros):
 *   prayer  Protect Melee and Piety on with 10..18 prayer points, the points run dry within the episode;
 *   stats   attack, strength and magic brewed down below base (80..96, 70..88), defence brew-boosted, Vengeance down;
 *   magic   only magic brewed down (84..90) with attack and strength boosted, Vengeance down and not castable for it.
 * Our bag then also holds two prayer potions, a super restore and a super combat. */
typedef enum { SC_UPKEEP_NONE, SC_UPKEEP_PRAYER, SC_UPKEEP_STATS, SC_UPKEEP_MAGIC } ScUpkeep;
static ScUpkeep g_upkeep_mode;
typedef struct { RiskfightSnapshot snap; int warm; } Scenario;
static uint32_t sc_draw(uint32_t* r, uint32_t low, uint32_t high) { return low + la_next(r) % (high - low + 1); }
static RiskfightLoadout sc_loadout(uint32_t* r, int ours) {
    RiskfightLoadout l = RISKFIGHT_DEFAULT_LOADOUT;
    l.hitpoints = 121; l.vengeance_sacks = 100; l.equipment[GEAR_SLOT_RING] = ITEM_RING_OF_RECOIL;
    memset(l.inventory, 0, sizeof(l.inventory));
    uint32_t weapon_roll = sc_draw(r, 0, 9);
    uint8_t weapon = ours ? ITEM_VOIDWAKER : weapon_roll < 4 ? ITEM_VOIDWAKER : weapon_roll < 8 ? ITEM_ABYSSAL_TENTACLE : ITEM_DHAROKS_GREATAXE;
    l.equipment[GEAR_SLOT_WEAPON] = weapon;
    if (item_is_two_handed(weapon)) l.equipment[GEAR_SLOT_SHIELD] = ITEM_NONE;
    /* He carries one karambwan: the sim lets him eat a second after 2 ticks, the plugin's HP-history meal model (3 ticks) then has no support and aborts. */
    int hard = (int)sc_draw(r, ours ? 2 : 6, ours ? 8 : 12), fast = (int)sc_draw(r, 1, 4), brew = (int)sc_draw(r, 1, 3), slot = 0;
    if (!ours) fast = 1;
    for (int i = 0; i < hard; i++) l.inventory[slot++] = riskfight_supply_code(OSRS_CONSUMABLE_ANGLERFISH, 0);
    for (int i = 0; i < fast; i++) l.inventory[slot++] = riskfight_supply_code(OSRS_CONSUMABLE_KARAMBWAN, 0);
    for (int i = 0; i < brew; i++) l.inventory[slot++] = riskfight_supply_code(OSRS_CONSUMABLE_BREW, 4);
    if (ours && g_upkeep_mode) {
        l.inventory[slot++] = riskfight_supply_code(OSRS_CONSUMABLE_PRAYER_RESTORE, 4);
        l.inventory[slot++] = riskfight_supply_code(OSRS_CONSUMABLE_PRAYER_RESTORE, 4);
        l.inventory[slot++] = riskfight_supply_code(OSRS_CONSUMABLE_SUPER_RESTORE, 4);
        l.inventory[slot++] = riskfight_supply_code(OSRS_CONSUMABLE_SUPER_COMBAT, 4);
    }
    if (weapon != ITEM_VOIDWAKER) l.inventory[slot++] = osrs_inventory_content_code_from_item(ITEM_VOIDWAKER);
    l.inventory[slot++] = osrs_inventory_content_code_from_item(ITEM_GRANITE_MAUL_ORNATE);
    return l;
}
static void sc_make(Scenario* sc, uint32_t corpus_seed, uint32_t index) {
    uint32_t r = osrs_lowbias32(corpus_seed * 2654435761u + index * 40503u + 17) | 1;
    memset(sc, 0, sizeof(*sc));
    RiskfightSnapshot* s = &sc->snap;
    s->pid_holder = (int)sc_draw(&r, 0, 1); s->pending_known = 1; s->seed = osrs_lowbias32(la_next(&r)) | 1;
    s->loadout[0] = sc_loadout(&r, 1); s->loadout[1] = sc_loadout(&r, 0);
    int hp[2] = {(int)sc_draw(&r, 25, 99), (int)sc_draw(&r, 12, 99)};
    for (int i = 0; i < 2; i++) {
        int energy_roll = (int)sc_draw(&r, 0, 9);
        int energy = i == 0 ? (energy_roll < 8 ? 50 + 10 * (int)sc_draw(&r, 0, 5) : 10 * (int)sc_draw(&r, 0, 4)) : 10 * (int)sc_draw(&r, 0, 10);
        int timer = (int)sc_draw(&r, 0, 4) - 1;
        s->side[i] = (RiskfightSideSnapshot){.hitpoints = hp[i], .prayer = 99, .attack = 118, .strength = 118, .defence = 118, .magic = 99,
            .attack_history = RISKFIGHT_ATTACK_OBSERVED, .attack_timer = timer, .attack_timer_uncapped = timer,
            .special_energy = energy, .special_regen_phase = (int)sc_draw(&r, 0, 49), .veng_active = sc_draw(&r, 0, 1), .veng_cooldown = 0,
            .offensive_prayer = OFFENSIVE_PRAYER_PIETY, .x = i, .y = 0, .interacting = 1,
            .seen_health_bar = osrs_health_bar_ratio(hp[i], 99, OSRS_PLAYER_HEALTH_BAR_SCALE), .seen_last_attack_ago = -1, .poison_source = OSRS_POISON_CLEAR_PRIOR};
    }
    sc->warm = (int)sc_draw(&r, 6, 12);
    RiskfightSideSnapshot* us = &s->side[0];
    us->veng_active = g_upkeep_mode == SC_UPKEEP_NONE ? us->veng_active : 0;
    if (g_upkeep_mode == SC_UPKEEP_PRAYER) {
        us->overhead_prayer = PRAYER_PROTECT_MELEE;
        us->prayer = (int)sc_draw(&r, 10, 18);
    } else if (g_upkeep_mode == SC_UPKEEP_STATS) {
        us->attack = (int)sc_draw(&r, 80, 96); us->strength = (int)sc_draw(&r, 80, 96);
        us->magic = (int)sc_draw(&r, 70, 88); us->defence = 120;
    } else if (g_upkeep_mode == SC_UPKEEP_MAGIC) {
        us->magic = (int)sc_draw(&r, 84, 90);
    }
}
/* Builds the decision-point Sim for a scenario: loaded state, warmed observer and history. Deterministic in (scenario, truth model). */
static int sc_start(Sim* sim, const Scenario* sc, const OppModel* truth) {
    static Outcome scratch;
    riskfight_load_state(&sim->state, &sim->context, &sc->snap);
    sim->tick_base = SC_TICK0; sim->outcome = &scratch; sim->seen_since_eat = 99; sim->previous = (StepEvents){0};
    sim->hist = hist_start();
    const Player* us = &sim->state.env.players[0];
    Observer* ob = &sim->obs;
    observer_clear(ob); memset(ob, 0, sizeof(*ob)); gs_infer_init(&ob->inf);
    ob->inf.tracker_current = 1; ob->bar_hit_tick = SC_TICK0 - 3;
    ob->bar_ratio = osrs_health_bar_ratio(sim->state.env.players[1].current_hitpoints, 99, SCALE_BAR);
    observer_sync(&sim->state, ob, SC_TICK0 - 3);
    gs_on_animation(&ob->inf, SC_TICK0 - 3, 0, attack_animation(us));
    observer_sync(&sim->state, ob, SC_TICK0);
    gs_on_game_tick(&ob->inf, SC_TICK0);
    observer_after_tick(ob, SC_TICK0);
    observer_branch(&sim->state, ob);
    sim->state.env.pvp_runtime.hit_observer = sim_observe_hit; sim->state.env.pvp_runtime.hit_observer_context = sim;
    g_rollout_seed = sc->snap.seed;
    for (int step = 0; step < sc->warm; step++) {
        RiskfightState* s = &sim->state; int actions[RF_HEADS];
        opponent_actions(truth, sim, step, sc->snap.seed + (uint32_t)step * 2654435761u, actions);
        riskfight_policy_commands(s, 1, actions, &sim->context.policy_commands[1]);
        human_input_clear_pending(&sim->context.policy_commands[0]);
        int vw = s->spec_voidwaker[1], maul = s->spec_maul[1];
        sim->step_damage = sim->step_max_hit = sim->step_direct = 0; ob->hit_count = 0;
        riskfight_step_queues(s, &sim->context, &sim->context.policy_commands[0].commands, &sim->context.policy_commands[1].commands);
        if (s->env.episode_over || s->env.players[0].current_hitpoints <= 0 || s->env.players[1].current_hitpoints <= 0) return 1; /* a fight that ends here has no decision point */
        observer_step(s, ob, SC_TICK0 + 1 + step);
        const Player* him = &s->env.players[1];
        StepEvents events = {.ate_visible = consumption_visible(s, 1), .ate = him->ate_food_this_tick || him->ate_karambwan_this_tick,
            .swung = s->visible[0].last_attack_tick == s->env.tick - 1, .vw = s->spec_voidwaker[1] - vw, .maul = s->spec_maul[1] - maul,
            .damage_on_him = sim->step_damage, .max_hit_on_him = sim->step_max_hit, .hit_on_him = him->hit_landed_this_tick};
        hist_step(&sim->hist, s, SC_TICK0 + 1 + step, &events);
        sim->seen_since_eat = events.ate_visible ? 0 : sim->seen_since_eat + 1;
    }
    sim->tick_base = SC_TICK0 + sc->warm;
    if (sim->obs.inf.timed < 0 || !sim->obs.inf.c.forecast_available) return 2;
    if (la_gear_status(la_belief_of(sim, sim->tick_base).gear).status != LA_GEAR_SUPPORTED) return 3;
    if (sim->obs.stakes.us <= 0 || sim->obs.stakes.him <= 0) return 4;
    return 0;
}
typedef struct { int ko, death; double value; } ScOutcome;
typedef enum { SC_RULES, SC_DEPLOYED, SC_DEPLOYED_NO_UPKEEP } ScPolicy;
/* Puts the decision-point state of `base` into `sim` with a fresh Ours: the plugin has just attached, nothing committed. */
static void sc_load(Sim* sim, const Sim* base, int lookahead, uint32_t seed) {
    sim->state = base->state; sim->hist = base->hist; sim->tick_base = base->tick_base; sim->seen_since_eat = base->seen_since_eat; sim->previous = (StepEvents){0};
    observer_assign(&sim->obs, &base->obs);
    ours_reset(&sim->ours, seed ^ 0x9e3779b9u, TIMING_MEASURED, STAKES_COMPUTED, 0);
    sim->ours.lookahead = lookahead;
}
/* One closed-loop episode from a decision-point Sim. `first` (optional) is the macro committed at step 0; the policy decides from step 1 on. */
static ScOutcome sc_episode(Sim* sim, const Sim* base, ScPolicy policy, const LaScript* first, const OppModel* truth, uint32_t seed, int horizon) {
    static Outcome out;
    memset(&out, 0, sizeof(out)); sim->outcome = &out;
    g_la_upkeep = policy != SC_DEPLOYED_NO_UPKEEP;
    sc_load(sim, base, policy != SC_RULES, seed);
    if (first) { sim->ours.native_program = *first; sim->ours.native_active = first->n > 0; sim->ours.native_resume = base->tick_base + 1; }
    RiskfightState* s = &sim->state; g_rollout_seed = seed;
    uint32_t pending_rng = osrs_lowbias32(seed ^ 0x70656e64u) | 1;
    riskfight_sample_pending(s, &pending_rng);
    s->env.pvp_runtime.hit_observer = sim_observe_hit; s->env.pvp_runtime.hit_observer_context = sim;
    ScOutcome result = {0}; HumanInput* hi = &sim->context.policy_commands[0];
    for (int step = 0; step < horizon && !s->env.episode_over; step++) {
        int actions[RF_HEADS];
        opponent_actions(truth, sim, step, seed + (uint32_t)step * 2654435761u, actions);
        riskfight_policy_commands(s, 1, actions, &sim->context.policy_commands[1]);
        if (first && step == 0) {
            human_input_clear_pending(hi);
            if (sim->ours.native_active) {
                la_script_step(&sim->ours.native_program, s, hi, sim->tick_base + 1, &sim->ours.native_cursor, &sim->ours.native_resume, 0, 0);
                sim->ours.native_active = sim->ours.native_cursor < sim->ours.native_program.n;
            }
        } else ours_act(sim, &sim->ours, hi, step);
        int vw = s->spec_voidwaker[1], maul = s->spec_maul[1];
        sim->step_damage = sim->step_max_hit = sim->step_direct = 0; sim->obs.hit_count = 0;
        riskfight_step_queues(s, &sim->context, &hi->commands, &sim->context.policy_commands[1].commands);
        const Player* him = &s->env.players[1];
        int tick = sim->tick_base + 1 + step;
        StepEvents events = {.ate_visible = consumption_visible(s, 1), .ate = him->ate_food_this_tick || him->ate_karambwan_this_tick || s->inventory_use[1].potion_animation_tick_plus_one == s->env.tick,
            .swung = s->visible[0].last_attack_tick == s->env.tick - 1, .vw = s->spec_voidwaker[1] - vw, .maul = s->spec_maul[1] - maul,
            .damage_on_him = sim->step_damage, .max_hit_on_him = sim->step_max_hit, .hit_on_him = him->hit_landed_this_tick};
        hist_step(&sim->hist, s, tick, &events);
        sim->seen_since_eat = events.ate_visible ? 0 : sim->seen_since_eat + 1;
        if (s->env.players[1].current_hitpoints <= 0) result.ko = 1;
        if (s->env.players[0].current_hitpoints <= 0) result.death = 1;
        if (result.ko || result.death) break; /* the kill tick is never observed: nothing is decided after it */
        observer_step(s, &sim->obs, tick);
    }
    observer_clear(&sim->obs);
    result.value = result.ko * (double)base->obs.stakes.him - result.death * (double)base->obs.stakes.us;
    return result;
}
typedef struct { uint32_t index; double sum[3][3]; } ScRow; /* per policy: ko, death, value summed over episodes */
typedef struct { long chosen[LA_MACROS], calls, unavailable, rejected[5]; double seconds; } ScFooter;
/* corpus SCENARIOS EPISODES HORIZON SELECT_WORLDS SEED train|heldout JOBS NATIVE_REV JAVA_REV MANIFEST
 * Rules proxy vs the deployed macro policy over the synthetic scenario corpus, paired by scenario and episode seed, forked across JOBS shards. */
static void eval_corpus(int argc, char** argv) {
    uint32_t scenarios = eval_uint(argv[2], 2, 1000000), episodes = eval_uint(argv[3], 1, 100000), seed = eval_uint(argv[7], 1, UINT32_MAX);
    int horizon = (int)eval_uint(argv[4], 1, 32), worlds = (int)eval_uint(argv[5], 2, 1000000), jobs = (int)eval_uint(argv[8], 1, 256);
    const char* upkeep = getenv("EVAL_UPKEEP");
    if (upkeep) {
        if (!strcmp(upkeep, "prayer")) g_upkeep_mode = SC_UPKEEP_PRAYER;
        else if (!strcmp(upkeep, "stats")) g_upkeep_mode = SC_UPKEEP_STATS;
        else if (!strcmp(upkeep, "magic")) g_upkeep_mode = SC_UPKEEP_MAGIC;
        else eval_fail("EVAL_UPKEEP must be prayer, stats or magic");
    }
    int arms = g_upkeep_mode ? 3 : 2;
    OppModel train = eval_pool("train"), held = eval_pool(argv[6]); eval_config(scenarios, seed, horizon, worlds); g_la_model = &train;
    eval_provenance(argv[9], argv[10], argv[11], argc, argv);
    printf("corpus\tsynthetic_scenarios=%u\tepisodes_per_scenario=%u\twarm_ticks=6..12\tour_hp=25..99\this_hp=12..99\tpolicies=rules_proxy,deployed_macro%s\tpairing=scenario_and_episode_seed\tupkeep=%s\n", scenarios, episodes,
        g_upkeep_mode ? ",deployed_without_upkeep_macros" : "", upkeep ? upkeep : "none");
    int fds[256];
    for (int j = 0; j < jobs; j++) {
        int pipefd[2]; if (pipe(pipefd)) eval_fail("pipe");
        pid_t pid = fork(); if (pid < 0) eval_fail("fork");
        if (pid == 0) {
            close(pipefd[0]);
            Sim* base = eval_alloc(sizeof(*base)); Sim* work = eval_alloc(sizeof(*work)); sim_init(base); sim_init(work);
            long rejected[5] = {0};
            for (uint32_t i = j; i < scenarios; i += (uint32_t)jobs) {
                Scenario sc; uint32_t draw = i;
                for (;;) { /* admitted scenario i is the first candidate of its stream whose warm-up leaves a live, tracked, supported, priced fight */
                    sc_make(&sc, seed, draw); int why = sc_start(base, &sc, &held);
                    if (!why) break;
                    rejected[why]++; draw += scenarios;
                }
                ScRow row = {.index = i};
                for (uint32_t e = 0; e < episodes; e++) {
                    uint32_t episode = osrs_lowbias32(seed + i * 7919u + e) | 1;
                    for (int p = 0; p < arms; p++) {
                        ScOutcome o = sc_episode(work, base, p == 0 ? SC_RULES : p == 1 ? SC_DEPLOYED : SC_DEPLOYED_NO_UPKEEP, NULL, &held, episode, horizon);
                        row.sum[p][0] += o.ko; row.sum[p][1] += o.death; row.sum[p][2] += o.value;
                    }
                }
                if (write(pipefd[1], &row, sizeof(row)) != (ssize_t)sizeof(row)) eval_fail("row write");
            }
            ScFooter foot = {.calls = g_la.calls, .unavailable = g_la_unavailable, .seconds = g_la.seconds};
            memcpy(foot.rejected, rejected, sizeof(rejected));
            memcpy(foot.chosen, g_la.chosen, sizeof(foot.chosen));
            ScRow marker = {.index = UINT32_MAX};
            if (write(pipefd[1], &marker, sizeof(marker)) != (ssize_t)sizeof(marker) || write(pipefd[1], &foot, sizeof(foot)) != (ssize_t)sizeof(foot)) eval_fail("footer write");
            _exit(0);
        }
        close(pipefd[1]); fds[j] = pipefd[0];
    }
    EvalStats pooled[3] = {0}; EvalMoment dko = {0}, ddeath = {0}, dvalue = {0}, uko = {0}, udeath = {0}, uvalue = {0}; ScFooter total = {0}; uint32_t seen = 0;
    for (int j = 0; j < jobs; j++) {
        for (;;) {
            ScRow row; ssize_t got = read(fds[j], &row, sizeof(row));
            if (got != (ssize_t)sizeof(row)) eval_fail("shard row read");
            if (row.index == UINT32_MAX) {
                ScFooter foot; if (read(fds[j], &foot, sizeof(foot)) != (ssize_t)sizeof(foot)) eval_fail("shard footer read");
                for (int m = 0; m < LA_MACROS; m++) total.chosen[m] += foot.chosen[m];
                for (int w = 0; w < 5; w++) total.rejected[w] += foot.rejected[w];
                total.calls += foot.calls; total.unavailable += foot.unavailable; total.seconds += foot.seconds;
                break;
            }
            seen++;
            for (int p = 0; p < arms; p++) {
                eval_add(&pooled[p].ko, row.sum[p][0] / episodes); eval_add(&pooled[p].death, row.sum[p][1] / episodes);
                eval_add(&pooled[p].survival, 1 - row.sum[p][1] / episodes); eval_add(&pooled[p].draw, 1 - (row.sum[p][0] + row.sum[p][1]) / episodes);
                eval_add(&pooled[p].value, row.sum[p][2] / episodes);
            }
            eval_add(&dko, (row.sum[1][0] - row.sum[0][0]) / episodes); eval_add(&ddeath, (row.sum[1][1] - row.sum[0][1]) / episodes);
            eval_add(&dvalue, (row.sum[1][2] - row.sum[0][2]) / episodes);
            if (arms == 3) {
                eval_add(&uko, (row.sum[1][0] - row.sum[2][0]) / episodes); eval_add(&udeath, (row.sum[1][1] - row.sum[2][1]) / episodes);
                eval_add(&uvalue, (row.sum[1][2] - row.sum[2][2]) / episodes);
            }
        }
        close(fds[j]);
    }
    for (int j = 0; j < jobs; j++) { int status; if (wait(&status) < 0 || !WIFEXITED(status) || WEXITSTATUS(status)) eval_fail("shard failed"); }
    if (seen != scenarios) eval_fail("missing scenarios");
    printf("note\tmetric rows are scenario means (n=scenarios); KO/death/survival/draw CIs are scenario-clustered normal, not Wilson\n");
    const char* names[3] = {"rules_proxy_C_uncertified", "deployed_macro_policy", "deployed_without_upkeep_macros"};
    for (int p = 0; p < arms; p++) {
        eval_metric(names[p], "KO_scenario_mean", &pooled[p].ko, 0); eval_metric(names[p], "death_scenario_mean", &pooled[p].death, 0);
        eval_metric(names[p], "finite_horizon_draw_scenario_mean", &pooled[p].draw, 0); eval_metric(names[p], "expected_stake_value", &pooled[p].value, 0);
    }
    eval_metric(names[1], "paired_KO_minus_rules", &dko, 0); eval_metric(names[1], "paired_death_minus_rules", &ddeath, 0);
    eval_metric(names[1], "paired_value_minus_rules", &dvalue, 0);
    if (arms == 3) {
        eval_metric(names[1], "paired_KO_minus_without_upkeep", &uko, 0); eval_metric(names[1], "paired_death_minus_without_upkeep", &udeath, 0);
        eval_metric(names[1], "paired_value_minus_without_upkeep", &uvalue, 0);
    }
    printf("admission\tscenarios=%u\trejected_ended_in_warmup=%ld\trejected_no_rival=%ld\trejected_gear=%ld\trejected_stakes=%ld\n", scenarios, total.rejected[1], total.rejected[2], total.rejected[3], total.rejected[4]);
    printf("deployed_choices\tcalls=%ld\tunavailable_ticks=%ld\tmean_ms=%.3f", total.calls, total.unavailable, total.calls ? 1e3 * total.seconds / total.calls : 0.0);
    for (int m = 0; m < LA_MACROS; m++) if (total.chosen[m]) printf("\t%s=%ld", LA_NAME[m], total.chosen[m]);
    puts("");
}

#ifndef RISKFIGHT_EVALUATOR_LIBRARY /* tests/evaluate_continuation.c reuses everything above */
int main(int argc, char** argv) {
    double started = la_seconds();
    if (argc == 11 && !strcmp(argv[1], "plan")) eval_plan(argc, argv);
    else if (argc == 14 && !strcmp(argv[1], "trace")) eval_trace(argc, argv);
    else if (argc == 12 && !strcmp(argv[1], "corpus")) eval_corpus(argc, argv);
    else eval_fail("usage: plan REQUEST N H SELECT_WORLDS SEED train|heldout NATIVE_REV JAVA_REV MANIFEST; trace TAPE WIRE TICK N H SELECT_WORLDS SEED train|heldout none|all NATIVE_REV JAVA_REV MANIFEST; corpus SCENARIOS EPISODES H SELECT_WORLDS train|heldout SEED JOBS NATIVE_REV JAVA_REV MANIFEST");
    printf("completion\tstatus=success\telapsed_monotonic_seconds=%.9f\tlookahead_calls=%ld\tlookahead_rollouts=%ld\n", la_seconds() - started, g_la.calls, g_la.rollouts);
    if (fflush(stdout) || ferror(stdout)) eval_fail("output write");
    return 0;
}
#endif
