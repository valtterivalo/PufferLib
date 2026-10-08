/* The lookahead as a process: one request per decision on stdin, one scored line per request on stdout.
 *
 *   cc -std=c11 -O2 -I. -o gs_lookahead_server ocean/osrs/tests/gs_lookahead_server.c -lm
 *   gs_lookahead_server MODEL.fit EAT_MIXTURE.fit [WORLDS HORIZON]
 *
 * Request lines, ending in END: LA id tick, B belief, K stakes, F feasibility, O our state, W worn raw ids, G bag raw ids, R rules script
 * (a count, then one C line per click: due kind slot). Response: R id micros best, then per macro feasible kill death value worlds. */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "riskfight_rollout.h"

enum { INERT_ID = 27281 };
static const int CANONICAL_FROM[] = {24592, 24595, 24598, 24601, 24603, 24605, 22441, 20513, 28067, 28070, 21287};
static const int CANONICAL_TO[] = {13441, 3144, 3024, 3026, 3028, 3030, 22322, 10551, 10551, 10828, 21295};
static const int SIM_IDS[] = {13441, 3144, 3024, 3026, 3028, 3030, 6685, 6687, 6689, 6691, 12695, 12697, 12699, 12701, 23691, 23694, 8013, 24621, 24225, 27690, 12954,
    22322, 10551, 10828, 21295, 1725, 1079, 4087, 7462, 11840, 2550, -1};

static int canonical(int id) {
    for (size_t i = 0; i < sizeof(CANONICAL_FROM) / sizeof(int); i++) if (CANONICAL_FROM[i] == id) id = CANONICAL_TO[i];
    for (size_t i = 0; i < sizeof(SIM_IDS) / sizeof(int); i++) if (SIM_IDS[i] == id) return id;
    return INERT_ID;
}

typedef struct {
    int id, tick;
    LaBelief belief;
    long him, us;
    LaFeasible feasible;
    TapeTick ours;
    int attack_timer, food_timer, potion_timer, karambwan_timer, armed, his_weapon, ours_last_ago, ours_speed, his_last_ago, his_speed;
    RiskfightAttackHistory attack_history;
    int attack_clock_low, attack_clock_high;
    int worn[TAPE_WORN], bag[TAPE_BAG];
    LaScript rules;
    int warned, unseen_count, unseen[WARNING_UNSEEN];
    int big_hit, since_vw, since_maul, ready_age, last_swing;
} Request;

static FILE* g_log;

static void read_request(FILE* in, Request* q) {
    memset(q, 0, sizeof(*q));
    q->big_hit = WARNING_NONE;
    q->since_vw = q->since_maul = 99;
    q->last_swing = -100;
    char line[4096];
    while (fgets(line, sizeof(line), in)) {
        if (g_log) fputs(line, g_log);
        char tag[8];
        int used;
        assert(sscanf(line, "%7s%n", tag, &used) == 1);
        char* p = line + used;
        if (!strcmp(tag, "LA")) assert(sscanf(p, "%d %d", &q->id, &q->tick) == 2);
        else if (!strcmp(tag, "B")) {
            LaBelief* b = &q->belief;
            assert(sscanf(p, "%d %d %d %d %d %d %lf %d %d %d %d %d %d %d %d", &b->hp_low, &b->hp_high, &b->energy_low, &b->energy_high, &b->vengeance, &b->pid, &b->flip,
                &b->timer_seen, &b->timer_first, &b->timer_last, &b->food_ready, &b->seen_since_eat, &b->seen_vw, &b->seen_maul, &b->bar_ratio) == 15);
        } else if (!strcmp(tag, "K")) assert(sscanf(p, "%ld %ld", &q->him, &q->us) == 2);
        else if (!strcmp(tag, "F")) assert(sscanf(p, "%d %d %d %d", &q->feasible.spec_ok, &q->feasible.eat_ok, &q->feasible.veng_ok, &q->feasible.energy) == 4);
        else if (!strcmp(tag, "O")) {
            TapeTick* t = &q->ours;
            int overhead_code, piety, parsed;
            char history[16];
            assert(sscanf(p, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %15s %d %d%n", &t->hp, &t->prayer, &t->attack, &t->strength, &t->defence, &t->magic,
                &t->spec_us, &t->spec_phase_us, &q->armed, &t->veng_us, &t->veng_cooldown_us, &t->sacks, &t->recoil_used, &q->attack_timer, &q->food_timer, &q->potion_timer,
                &q->karambwan_timer, &piety, &overhead_code, &q->ours_last_ago, &q->ours_speed, &q->his_last_ago, &q->his_speed, &q->his_weapon, &t->recoil_evidence, &t->ox, &t->oy, history, &q->attack_clock_low, &q->attack_clock_high, &parsed) == 30);
            assert(strspn(p + parsed, " \r\n") == strlen(p + parsed));
            if (!strcmp(history, "OBSERVED")) {
                assert(q->attack_clock_low <= q->attack_clock_high);
                q->attack_history = RISKFIGHT_ATTACK_OBSERVED;
            } else {
                assert(!strcmp(history, "UNKNOWN") && q->attack_clock_low == 0 && q->attack_clock_high == 0);
                q->attack_history = RISKFIGHT_ATTACK_UNKNOWN;
            }
            t->piety = piety;
            static const char* const overheads[] = {"NONE", "SMITE", "PROTECT_FROM_MELEE", "PROTECT_FROM_MISSILES", "PROTECT_FROM_MAGIC", "REDEMPTION"};
            strcpy(t->overhead, overheads[overhead_code]);
        } else if (!strcmp(tag, "W")) for (int i = 0; i < TAPE_WORN; i++) q->worn[i] = (int)strtol(p, &p, 10);
        else if (!strcmp(tag, "G")) for (int i = 0; i < TAPE_BAG; i++) q->bag[i] = (int)strtol(p, &p, 10);
        else if (!strcmp(tag, "Q")) {
            char since[16];
            assert(sscanf(p, "%15s %d%n", since, &q->unseen_count, &used) == 2);
            q->warned = strcmp(since, "none") ? atoi(since) : WARNING_NONE;
            p += used;
            for (int i = 0; i < q->unseen_count; i++) q->unseen[i] = (int)strtol(p, &p, 10);
        } else if (!strcmp(tag, "H")) assert(sscanf(p, "%d %d %d %d %d", &q->big_hit, &q->since_vw, &q->since_maul, &q->ready_age, &q->last_swing) == 5);
        else if (!strcmp(tag, "R")) q->rules.n = 0;
        else if (!strcmp(tag, "C")) {
            int due, kind, slot;
            assert(sscanf(p, "%d %d %d", &due, &kind, &slot) == 3 && q->rules.n < LA_SCRIPT);
            q->rules.due[q->rules.n] = due;
            q->rules.command[q->rules.n++] = kind == 0 ? click_command(slot) : kind == 1 ? attack_command() : kind == 2 ? spec_command() : vengeance_command();
        } else if (!strcmp(tag, "END")) return;
    }
    exit(0);
}

static Hist g_hist_storage[1];

static void snapshot_of(Request* q, RiskfightSnapshot* snap) {
    static Tape tape;
    memset(&tape, 0, sizeof(tape));
    static TapeTick tick;
    tick = q->ours;
    tick.tick = q->tick;
    tick.ux = 0; tick.uy = 0;
    tick.pid = '?';
    tick.last_bar_hit = q->tick;
    tick.bar_ratio = -1;
    tick.bar_scale = -1;
    tick.swing_us_tick = q->ours_last_ago >= 0 ? q->tick - q->ours_last_ago : -100;
    tick.swing_us_speed = q->ours_speed;
    tick.swing_him_tick = q->his_last_ago >= 0 ? q->tick - q->his_last_ago : -100;
    tick.swing_him_speed = q->his_speed;
    tick.spec_him = 100;
    for (int i = 0; i < TAPE_WORN; i++) tick.worn[i] = q->worn[i] < 0 || canonical(q->worn[i]) == INERT_ID ? -1 : canonical(q->worn[i]);
    for (int i = 0; i < TAPE_BAG; i++) tick.bag[i] = q->bag[i] < 0 ? -1 : canonical(q->bag[i]);
    tape.ticks = &tick;
    tape.lo = tape.hi = q->tick;
    g_hist_storage[0] = hist_start();
    tape.hist_series = g_hist_storage;
    g_tape = &tape;
    uint32_t rng = osrs_lowbias32((uint32_t)q->tick) | 1;
    tape_snapshot(&tape, q->tick, &DEFAULT_PRIORS, &rng, snap);
    assert(q->attack_history == RISKFIGHT_ATTACK_OBSERVED && q->attack_clock_low == q->attack_clock_high);
    assert(q->attack_timer == (q->attack_clock_low < -1 ? -1 : q->attack_clock_low));
    snap->side[0].attack_history = q->attack_history;
    snap->side[0].attack_timer = q->attack_timer;
    snap->side[0].attack_timer_uncapped = q->attack_clock_low;
    snap->side[0].food_timer = q->food_timer;
    snap->side[0].potion_timer = q->potion_timer;
    snap->side[0].karambwan_timer = q->karambwan_timer;
    snap->side[0].spec_armed = q->armed;
    snap->side[1].seen_health_bar = q->belief.bar_ratio;
    for (int i = 0; i < NUM_ITEMS && q->his_weapon >= 0; i++)
        if (ITEM_DATABASE[i].item_id == q->his_weapon) snap->loadout[1].equipment[GEAR_SLOT_WEAPON] = (uint8_t)i;
    if (item_is_two_handed(snap->loadout[1].equipment[GEAR_SLOT_WEAPON])) snap->loadout[1].equipment[GEAR_SLOT_SHIELD] = ITEM_NONE;
    g_tape = NULL;
}

int main(int argc, char** argv) {
    assert(argc >= 3);
    g_ablation.hp = HP_UNIFORM;
    g_ablation.escape_model = 1;
    g_ablation.his_defence = 99;
    g_ablation.panic = 0.8;
    g_ablation.panic_hp = 25;
    static OppFit fit;
    FILE* f = fopen(argv[1], "rb");
    assert(f && fread(&fit, sizeof(fit), 1, f) == 1);
    fclose(f);
    fit.eat_action = ACT_EAT_WARNING;
    static EatMixture mixture;
    f = fopen(argv[2], "rb");
    assert(f && fread(&mixture, sizeof(mixture), 1, f) == 1);
    fclose(f);
    g_eat_model.on = 1;
    g_eat_model.model = EAT_MIXTURE;
    g_eat_model.types = mixture.types;
    memcpy(g_eat_model.pi, mixture.pi, sizeof(mixture.pi));
    g_eat_model.logit = mixture.logit;
    if (argc > 3) g_la_worlds = atoi(argv[3]);
    if (argc > 4) g_la_horizon = atoi(argv[4]);
    assert_fitted(&fit, "server");
    static OppModel model;
    model = (OppModel){MODEL_FIT, &fit};
    g_la_model = &model;
    g_la_cache = 0;
    observer_register_items();
    static Sim base;
    sim_init(&base);
    static Request q;
    if (getenv("GS_SERVER_LOG")) g_log = fopen(getenv("GS_SERVER_LOG"), "w");
    for (;;) {
        read_request(stdin, &q);
        if (q.attack_history == RISKFIGHT_ATTACK_UNKNOWN || q.attack_clock_low != q.attack_clock_high) {
            printf("UNAVAILABLE %d %s\n", q.id, q.attack_history == RISKFIGHT_ATTACK_UNKNOWN
                ? "local_attack_history_unknown" : "local_attack_history_interval");
            fflush(stdout);
            if (g_log) fflush(g_log);
            continue;
        }
        double started = la_seconds();
        RiskfightSnapshot snap;
        snapshot_of(&q, &snap);
        riskfight_load_state(&base.state, &base.context, &snap);
        LaScript scripts[LA_MACROS];
        int feasible[LA_MACROS], alive[LA_MACROS];
        la_scripts(&base.state, q.tick, &q.feasible, &q.rules, scripts, feasible);
        Hist hist = hist_start();
        hist.warning.in_reach_since = q.warned;
        hist.warning.unseen_count = q.unseen_count;
        memcpy(hist.warning.unseen, q.unseen, sizeof(q.unseen));
        hist.since_eat = q.belief.seen_since_eat;
        hist.big_hit_tick = q.big_hit;
        hist.since_vw_spec = q.since_vw;
        hist.since_maul_spec = q.since_maul;
        hist.ready_age = q.ready_age;
        hist.last_swing_tick = q.last_swing;
        LaScore score[LA_MACROS];
        LaMacro best = la_evaluate(&base.state, &hist, &q.belief, q.tick, (GsStakes){q.him, q.us}, (uint32_t)q.id, scripts, feasible, score, alive);
        printf("R %d %.0f %d", q.id, (la_seconds() - started) * 1e6, (int)best);
        for (int m = 0; m < LA_MACROS; m++)
            printf(" %d %.4f %.4f %.4f %d", feasible[m] && score[m].worlds > 0, score[m].worlds ? score[m].kill / score[m].worlds : 0, score[m].worlds ? score[m].death / score[m].worlds : 0,
                score[m].worlds ? score[m].value / score[m].worlds : 0, score[m].worlds);
        printf("\n");
        fflush(stdout);
        if (g_log) fflush(g_log);
    }
}
