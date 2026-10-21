/* His reaction to damage, in the recordings and in the sim, on the same windows.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/reaction_replay ocean/osrs/tests/reaction_replay.c -lm      (repo root; cwd = repo root)
 *   /tmp/reaction_replay [-r REPS] [-m model.fit eat_mixture.fit] [-o OUT.tsv] [-n] SPECTATOR_DUELS|- tape...
 *
 * A damage window is alignment.py's (tools/calib/alignment.py of the Light repo): a damaging tick on a fighter with no damage on the three ticks before it opens a window
 * over its tick and the three after. The recordings give each window its hitpoint bar before, the damage on each of its four ticks, the visible meals around it and whether he
 * died. The sim replays every window: his hitpoints are drawn inside the bar before, his history (warning, last meal, energy) is the recording's, the damage of the window
 * lands on its ticks through the sim's own pending hits, and what he reads of us (our swing timer, Vengeance, bar, weapon and kit) follows the recording tick by tick.
 * The sim decides everything of his: whether he eats (opp_fit_actions, the deployed hazard), what the meal heals, whether it is visible, whether he dies.
 *
 * OUT.tsv has one row per window of the recordings (kind D) and one per window and repetition of the sim (kind S), columns in HEADER below. */
#define _POSIX_C_SOURCE 200809L
#include "riskfight_observe.h"
#include "riskfight_spectator.h"

enum { MAX_WINDOWS = 40000, FOLLOW = 8, PRE = 2, MAX_DUELS = 4000, LEAD_MAX = 30 };
typedef enum { P_LONE, P_STACK, P_SPLIT1, P_SPLIT2, P_SPLIT3 } Pattern;
static const char* const PATTERN_NAME[5] = {"lone", "stack", "split+1", "split+2", "split+3"};
#define BAR_HP (99.0 / 30.0)

enum { NO_RISE = -9999 };
typedef struct {
    int source, style, session, duel, side, row, tick, r0, d[4], n[4], total, pattern, killed, isolated, eatmask;
    int death_row; /* offset of the first dead row within 0..FOLLOW, -1 */
    int bar_age;   /* ticks from the last row that showed his bar to the tick before the window, -1 when none was shown */
    int rise;      /* alignment.py's gross rise in tenths of a hitpoint between the first two damaging ticks of a split window (the bar after the second splat plus its damage, less the bar after the first), NO_RISE where a bar is missing or the window is not a split */
    int hit_ratio, meals; /* the bar at his last hit and the meals seen since: the fit's belief about his hitpoints (sample_hp) */
    int lead;      /* ticks from the last row on which a hit landed on him with his bar shown (the last time his hitpoints were read) to the window; 0 when there is none within LEAD_MAX */
    int lead_ratio, lead_eats; /* the bar then, and the visible meals between */
} Win;

static Win* g_win;
static int g_win_count;
static const SpecDuel* g_duels[MAX_DUELS];
static int g_duel_count;
static FitRow* g_series[MAX_DUELS][2];
static int g_duel_fold[MAX_DUELS];  /* the fold of its recording in fit_opponent.c: sessions of a source sorted by name, rank modulo FOLDS */
enum { FOLDS = 8 };
static EatMixture g_fold_mixture[FOLDS];
static int g_use_folds;

static int damaging(const SpecRow* r, int side) { return r->s[side].dmg > 0; }

/** Windows of one fighter of one duel, from the rows alignment.py reads. */
static void find_windows(const SpecDuel* d, int side, int duel_index, const FitRow* fr) {
    int n = d->row_count;
    int* bar = malloc((size_t)n * sizeof(int));
    int* age = malloc((size_t)n * sizeof(int));
    int last = -1, last_row = -1;
    for (int i = 0; i < n; i++) { if (d->rows[i].s[side].bar >= 0) { last = d->rows[i].s[side].bar; last_row = i; } bar[i] = last; age[i] = last_row < 0 ? -1 : i - last_row; }
    for (int i = 4; i < n; i++) {
        const SpecRow* r = &d->rows[i];
        if (!damaging(r, side)) continue;
        if (damaging(&d->rows[i - 1], side) || damaging(&d->rows[i - 2], side) || damaging(&d->rows[i - 3], side)) continue;
        int span = n - 1 - i < FOLLOW ? n - 1 - i : FOLLOW, bad = 0;
        for (int o = -3; o <= span; o++) {
            if (d->rows[i + o].tick != r->tick + o) bad = 1;
            if (d->rows[i + o].third) bad = 1;
        }
        if (bad) continue;
        Win w = {0};
        w.source = d->source; w.style = d->dharok[side]; w.session = d->session; w.duel = duel_index; w.side = side; w.row = i; w.tick = r->tick;
        w.r0 = bar[i - 1];
        w.death_row = -1;
        w.bar_age = age[i - 1];
        w.hit_ratio = fr[i].hit_ratio; w.meals = fr[i].meals;
        for (int j = i - 1; j >= 0 && i - j <= LEAD_MAX && d->rows[j].tick == r->tick - (i - j); j--)
            if (d->rows[j].s[side].nhit > 0 && d->rows[j].s[side].bar >= 0) {
                w.lead = i - j; w.lead_ratio = d->rows[j].s[side].bar;
                for (int m = j + 1; m < i; m++) w.lead_eats += d->rows[m].s[side].eat829;
                break;
            }
        int offsets[4], count = 0;
        for (int o = 0; o < 4 && i + o < n; o++) {
            w.d[o] = d->rows[i + o].s[side].dmg;
            w.n[o] = d->rows[i + o].s[side].nhit;
            w.total += w.d[o];
            if (w.d[o] > 0) offsets[count++] = o;
        }
        for (int o = 0; o <= span; o++) if (d->rows[i + o].s[side].dead && w.death_row < 0) w.death_row = o;
        w.killed = w.death_row >= 0 && w.death_row <= 3;
        if (i + 3 > n - 1 && !w.killed) continue;
        w.pattern = count == 1 ? (w.n[0] >= 2 ? P_STACK : P_LONE) : P_SPLIT1 + offsets[1] - 1;
        w.rise = NO_RISE;
        if (count > 1 && bar[i + offsets[0]] >= 0 && bar[i + offsets[1]] >= 0)
            w.rise = (int)floor(((bar[i + offsets[1]] * BAR_HP + w.d[offsets[1]]) - bar[i + offsets[0]] * BAR_HP) * 10 + 0.5);
        w.isolated = count == 1;
        for (int o = 1; o <= FOLLOW; o++) if (i + o < n && damaging(&d->rows[i + o], side)) w.isolated = 0;
        for (int o = -PRE; o <= span; o++) if (d->rows[i + o].s[side].eat829) w.eatmask |= 1 << (o + PRE);
        assert(g_win_count < MAX_WINDOWS);
        g_win[g_win_count++] = w;
    }
    free(bar);
    free(age);
}

/* ---------- the sim ---------- */

typedef struct { RiskfightState state; RiskfightContext context; } SimWorld;
typedef struct { int damage_on_him; } HitSink;

static void hit_sink(void* context, const OsrsPvpHitEvent* event) {
    if (event->target == 1) ((HitSink*)context)->damage_on_him += event->hitpoints_lost;
}

static RiskfightLoadout g_loadout[2];

static void build_loadouts(void) {
    for (int side = 0; side < 2; side++) {
        RiskfightLoadout* l = &g_loadout[side];
        memset(l, 0, sizeof(*l));
        l->hitpoints = side == 1 ? 99 : 121;
        l->vengeance_sacks = 100;
        for (int g = 0; g < NUM_GEAR_SLOTS; g++) l->equipment[g] = ITEM_NONE;
        int slot = 0;
        if (side == 1) {
            l->equipment[GEAR_SLOT_WEAPON] = ITEM_VOIDWAKER;
            const struct { OsrsConsumableKind kind; int doses, count; } supplies[] = {
                {OSRS_CONSUMABLE_ANGLERFISH, 0, 14}, {OSRS_CONSUMABLE_KARAMBWAN, 0, 4}, {OSRS_CONSUMABLE_BREW, 4, 3}, {OSRS_CONSUMABLE_SUPER_RESTORE, 4, 2}};
            for (size_t k = 0; k < sizeof(supplies) / sizeof(*supplies); k++)
                for (int n = 0; n < supplies[k].count; n++) l->inventory[slot++] = riskfight_supply_code(supplies[k].kind, supplies[k].doses);
            const uint16_t spares[] = {ITEM_AGS, ITEM_DRAGON_CLAWS, ITEM_GRANITE_MAUL_ORNATE};
            for (size_t k = 0; k < sizeof(spares) / sizeof(*spares); k++) l->inventory[slot++] = osrs_inventory_content_code_from_item(spares[k]);
        }
    }
}

static const int GEAR_OF_KIT[KIT_SLOTS] = {GEAR_SLOT_HEAD, GEAR_SLOT_CAPE, GEAR_SLOT_NECK, GEAR_SLOT_WEAPON, GEAR_SLOT_BODY, GEAR_SLOT_SHIELD, -1, GEAR_SLOT_LEGS, -1, GEAR_SLOT_HANDS, GEAR_SLOT_FEET, -1};

/** What the recording shows of the attacker's kit on this row, as the sim's equipment: the item the plugin reads the reach from. */
static void set_attacker_kit(Player* us, const SpecDuel* d, int attacker, const SpecRow* row) {
    const int* kit = spec_kit(d, attacker, row->tick);
    for (int k = 0; k < KIT_SLOTS; k++) {
        if (GEAR_OF_KIT[k] < 0) continue;
        int raw = kit[k] >= GS_ITEM_OFFSET ? kit[k] - GS_ITEM_OFFSET : -1;
        us->equipped[GEAR_OF_KIT[k]] = k == 3 ? raw_id_to_item(row->s[attacker].wep) : raw_id_to_item(raw);
    }
}

typedef struct { int eat_visible, eat_true, death, hp_window, rise, lead_eats; } SimOut;

static uint32_t mix(uint32_t a, uint32_t b) { return osrs_lowbias32(a * 0x9e3779b9u + b + 0x7f4a7c15u); }

/** One replay of window `win`, from the row after the last time his hitpoints were read (his bar at a hit; the window's own first row when there is none) through the
 * ticks 0..FOLLOW from the row of its first damage. The ticks before the window run with no damage: the sim's meals fill the time since the bar was read. */
static void replay_window(const OppFit* fit, const SpecDuel* d, const FitRow* fr, const Win* win, int hp_start, uint32_t seed, SimOut* out) {
    static SimWorld* world;
    if (!world) world = calloc(1, sizeof(*world));
    if (g_use_folds) g_eat_model = g_fold_mixture[g_duel_fold[win->duel]];
    SimWorld* w = world;
    riskfight_init_context((EncounterContext*)&w->context);
    riskfight_init_state((EncounterState*)&w->state, (EncounterContext*)&w->context);
    w->context.self_play = 1;
    w->context.loadout[0] = &g_loadout[0];
    w->context.loadout[1] = &g_loadout[1];
    riskfight_finalize_context((EncounterState*)&w->state, (EncounterContext*)&w->context);
    for (int side = 0; side < 2; side++) w->state.poison_source[side] = OSRS_POISON_CLEAR_PRIOR;
    riskfight_reset((EncounterState*)&w->state, (EncounterContext*)&w->context, seed | 1);
    RiskfightState* s = &w->state;
    Player* us = &s->env.players[0];
    Player* him = &s->env.players[1];
    int first = win->lead ? win->row - win->lead + 1 : win->row;
    him->current_hitpoints = hp_start;
    him->veng_active = 0;
    him->special_energy = fr[first].h.last_energy;
    riskfight_observe_visible(s, 0, 1);
    riskfight_observe_visible(s, 1, 1);
    int shown = win->lead ? win->lead_ratio : win->hit_ratio;
    if (shown >= 0) s->visible[0].health_bar = shown; /* the bar he last showed, not his hitpoints now */
    Hist hist = fr[first].h;
    HitSink sink;
    s->env.pvp_runtime.hit_observer = hit_sink;
    s->env.pvp_runtime.hit_observer_context = &sink;
    int attacker = 1 - win->side;
    int last = d->row_count - 1 < win->row + FOLLOW ? d->row_count - 1 : win->row + FOLLOW;
    g_rollout_seed = seed;
    memset(out, 0, sizeof(*out));
    out->death = -1;
    out->rise = NO_RISE;
    int bars[FOLLOW + 1], done = 0;
    for (int row = first; row <= last; row++) {
        int rel = row - win->row, tick = d->rows[row].tick;
        const OppIn* seen = &fr[row].in;
        /* What he reads of us follows the recording. */
        set_attacker_kit(us, d, attacker, &d->rows[row]);
        us->has_attack_timer = seen->our_tau >= 0;
        us->attack_timer = seen->our_tau >= 0 ? seen->our_tau : 0;
        us->veng_active = seen->our_veng;
        s->visible[1].health_bar = seen->our_bar_ratio;
        if (rel == 0) out->hp_window = him->current_hitpoints;
        int actions[RF_HEADS];
        opp_fit_actions(fit, s, &hist, tick, mix(seed, (uint32_t)(row - first)), actions);
        us->veng_active = 0; /* no echo of ours lands on him: all damage on him is the recording's */
        riskfight_policy_commands(s, 1, actions, &w->context.policy_commands[1]);
        HumanCommandQueue none = {0};
        if (rel >= 0 && rel <= 3 && win->d[rel] > 0) {
            queue_hit(&s->env, 0, 1, us, him, win->d[rel], ATTACK_STYLE_NONE, 0, 0, 1, 0, 0, 0, 0, 0);
            s->env.pending_hit_metadata[0][us->num_pending_hits - 1].damage_mode = 1;
        }
        int vw = s->spec_voidwaker[1], maul = s->spec_maul[1];
        sink.damage_on_him = 0;
        riskfight_step_queues(s, &w->context, &none, &w->context.policy_commands[1].commands);
        int ate_true = him->ate_food_this_tick || him->ate_karambwan_this_tick || s->inventory_use[1].potion_animation_tick_plus_one == s->env.tick;
        StepEvents events = {.ate_visible = riskfight_meal_visible(s, 1), .ate = ate_true, .swung = s->visible[0].last_attack_tick == s->env.tick - 1,
            .vw = s->spec_voidwaker[1] - vw, .maul = s->spec_maul[1] - maul, .damage_on_him = sink.damage_on_him, .hit_on_him = him->hit_landed_this_tick};
        hist_step(&hist, s, tick, &events);
        if (rel < 0) out->lead_eats += events.ate_visible;
        if (rel >= -PRE) {
            if (events.ate_visible) out->eat_visible |= 1 << (rel + PRE);
            if (ate_true) out->eat_true |= 1 << (rel + PRE);
        }
        if (rel >= 0) { bars[rel] = s->visible[0].health_bar; done = rel + 1; }
        if (him->current_hitpoints <= 0 && out->death < 0) out->death = rel;
        if (s->env.episode_over || him->current_hitpoints <= 0) break;
    }
    /* alignment.py's rise between the first two damaging ticks, from the bar the sim showed on them */
    int offsets[4], count = 0;
    for (int o = 0; o < 4; o++) if (win->d[o] > 0) offsets[count++] = o;
    if (count > 1 && offsets[1] < done && out->death < 0 && bars[offsets[0]] >= 0 && bars[offsets[1]] >= 0)
        out->rise = (int)floor(((bars[offsets[1]] * BAR_HP + win->d[offsets[1]]) - bars[offsets[0]] * BAR_HP) * 10 + 0.5);
    riskfight_destroy_context((EncounterContext*)&w->context);
}

static int g_heal_per_meal = 24;

/** His hitpoints where the replay starts: inside the bar at the last hit that showed it (the replay then runs the meals since); without such a hit as the fit believes them
 * (fit_opponent.c sample_hp): inside the bar at his last hit, raised by 24 (-h) for each meal seen since, its prior without a bar. */
static int draw_hp(const Win* win, uint32_t seed) {
    uint32_t rng = osrs_lowbias32(seed) | 1;
    if (win->lead) {
        OsrsHealthBarRange read = osrs_health_bar_range(win->lead_ratio, 30, 99, 121);
        if (read.kind == OSRS_HEALTH_BAR_KNOWN && read.upper > 0) return read.lower + (int)(xorshift32(&rng) % (uint32_t)(read.upper - read.lower + 1));
    }
    OsrsHealthBarRange bar = osrs_health_bar_range(win->hit_ratio, 30, 99, 121);
    if (win->hit_ratio < 0 || bar.kind != OSRS_HEALTH_BAR_KNOWN) return 60 + (int)(xorshift32(&rng) % 62);
    int hi = bar.upper + g_heal_per_meal * win->meals;
    if (hi > 121) hi = 121;
    return bar.lower + (int)(xorshift32(&rng) % (uint32_t)(hi - bar.lower + 1));
}

/* ---------- main ---------- */

static void add_duel(const SpecDuel* d, int side_count) {
    assert(g_duel_count < MAX_DUELS);
    int index = g_duel_count++;
    g_duels[index] = d;
    for (int side = 0; side < side_count; side++) {
        g_series[index][side] = malloc((size_t)d->row_count * sizeof(FitRow));
        Hist final;
        fit_series(d, side, g_series[index][side], &final);
        find_windows(d, side, index, g_series[index][side]);
    }
}

static const char* const HEADER = "kind\tsource\tstyle\tsession\ttick\tr0\td0\td1\td2\td3\tn0\tn1\tn2\tn3\tpattern\ttotal\tisolated\tkilled\tdeath\teatmask\teattrue\thp0\trep\tbar_age\trise\tlead\tlead_eats";

/** One row: the window (its tick, bar before, damage and splat counts as delivered) and what happened in it. */
static void emit(FILE* out, char kind, const Win* w, const char* pattern, const int d[4], const int n[4], int isolated, int killed, int death, int eatmask, int eattrue, int hp0, int rep, int rise,
    int lead_eats) {
    fprintf(out, "%c\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", kind, w->source, w->style, w->session, w->tick, w->r0, d[0], d[1], d[2], d[3],
        n[0], n[1], n[2], n[3], pattern, d[0] + d[1] + d[2] + d[3], isolated, killed, death, eatmask, eattrue, hp0, rep, w->bar_age, rise, w->lead, lead_eats);
}

int main(int argc, char** argv) {
    int reps = 8, no_eat = 0, injections = 0;
    const char* fold_dir = NULL;
    const char* model = "ocean/osrs/tests/fixtures/model.fit";
    const char* eat = "ocean/osrs/tests/fixtures/eat_mixture.fit";
    const char* out_path = NULL;
    int a = 1;
    for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++) {
        if (!strcmp(argv[a], "-r")) reps = atoi(argv[++a]);
        else if (!strcmp(argv[a], "-m")) { model = argv[++a]; eat = argv[++a]; }
        else if (!strcmp(argv[a], "-o")) out_path = argv[++a];
        else if (!strcmp(argv[a], "-x")) injections = 1;
        else if (!strcmp(argv[a], "-n")) no_eat = 1;
        else if (!strcmp(argv[a], "-s")) g_eat_scale = atof(argv[++a]);
        else if (!strcmp(argv[a], "-h")) g_heal_per_meal = atoi(argv[++a]);
        else if (!strcmp(argv[a], "-F")) { fold_dir = argv[++a]; }
        else { fprintf(stderr, "unknown option %s\n", argv[a]); return 2; }
    }
    if (argc - a < 2) { fprintf(stderr, "usage: reaction_replay [-r REPS] [-m model eat | -F FITDIR] [-o OUT] [-n] [-x] [-s EAT_SCALE] [-h HEAL_PER_MEAL] SPECTATOR_DUELS|- tape...\n"); return 2; }
    const char* spectator = argv[a++];
    observer_register_items();
    g_win = malloc((size_t)MAX_WINDOWS * sizeof(Win));
    assert(g_win);
    int sessions = 0;
    char names[SP_MAX_SESSIONS * 2][64];
    for (; a < argc; a++) {
        static Tape tape;
        SpecDuel* duel = calloc(1, sizeof(SpecDuel));
        tape_load(&tape, argv[a]);
        duel_from_tape(&tape, duel);
        int found = -1;
        for (int i = 0; i < sessions; i++) if (!strcmp(names[i], tape.session)) found = i;
        if (found < 0) { snprintf(names[sessions], 64, "%s", tape.session); found = sessions++; }
        duel->session = found; duel->source = 0;
        g_duel_fold[g_duel_count] = found;
        add_duel(duel, 1);
        tape_free(&tape);
    }
    int spectator_sessions = 0;
    char spectator_names[SP_MAX_SESSIONS * 2][64];
    if (strcmp(spectator, "-")) {
        static SpecCorpus corpus;
        spec_load(&corpus, spectator);
        spectator_sessions = corpus.session_count;
        for (int i = 0; i < corpus.session_count; i++) snprintf(spectator_names[i], 64, "%s", corpus.sessions[i]);
        for (int i = 0; i < corpus.count; i++) {
            SpecDuel* duel = &corpus.duels[i];
            g_duel_fold[g_duel_count] = duel->session;
            duel->session += 1000;
            add_duel(duel, 2);
        }
    }
    static OppFit fit;
    if (fold_dir) {
        g_use_folds = 1;
        /* held out: each recording is replayed under the eat mixture fitted on the other folds (fit_opponent.c writes fold_K/eat_mixture.fit beside model.fit) */
        char path[512], eat_path[512];
        snprintf(path, sizeof(path), "%s/model.fit", fold_dir);
        snprintf(eat_path, sizeof(eat_path), "%s/eat_mixture.fit", fold_dir);
        opp_load(path, eat_path, &fit, &g_eat_model);
        for (int f = 0; f < FOLDS; f++) {
            snprintf(eat_path, sizeof(eat_path), "%s/fold_%d/eat_mixture.fit", fold_dir, f);
            opp_read(eat_path, &g_fold_mixture[f], sizeof(EatMixture));
        }
        int rank_own[SP_MAX_SESSIONS * 2], rank_spec[SP_MAX_SESSIONS * 2];
        for (int i = 0; i < sessions; i++) { rank_own[i] = 0; for (int j = 0; j < sessions; j++) rank_own[i] += strcmp(names[j], names[i]) < 0; }
        for (int i = 0; i < spectator_sessions; i++) { rank_spec[i] = 0; for (int j = 0; j < spectator_sessions; j++) rank_spec[i] += strcmp(spectator_names[j], spectator_names[i]) < 0; }
        for (int i = 0; i < g_duel_count; i++) g_duel_fold[i] = (g_duels[i]->source == 0 ? rank_own[g_duel_fold[i]] : rank_spec[g_duel_fold[i]]) % FOLDS;
    } else opp_load(model, eat, &fit, &g_eat_model);
    g_no_eat = no_eat;
    /* the deployed sim: gs_lookahead_server.c */
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99; g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    build_loadouts();
    FILE* out = out_path ? fopen(out_path, "w") : stdout;
    assert(out);
    fprintf(out, "%s\n", HEADER);
    for (int i = 0; i < g_win_count; i++) {
        const Win* w = &g_win[i];
        emit(out, 'D', w, PATTERN_NAME[w->pattern], w->d, w->n, w->isolated, w->killed, w->death_row, w->eatmask, -1, -1, -1, w->rise, w->lead_eats);
    }
    for (int i = 0; i < g_win_count; i++) {
        const Win* w = &g_win[i];
        for (int rep = 0; rep < reps; rep++) {
            uint32_t seed = mix(mix((uint32_t)i, (uint32_t)rep), 0x5eed);
            SimOut o;
            replay_window(&fit, g_duels[w->duel], g_series[w->duel][w->side], w, draw_hp(w, seed ^ 0x48500000u), seed, &o);
            emit(out, 'S', w, PATTERN_NAME[w->pattern], w->d, w->n, w->isolated, o.death >= 0 && o.death <= 3, o.death, o.eat_visible, o.eat_true, o.hp_window, rep, o.rise, o.lead_eats);
        }
    }
    /* Matched injections: the window's lead-in as in the replays (his hitpoints read at his last hit, the meals the sim eats since), then its total damage delivered stacked on
     * one tick or as two halves g ticks apart. A probe with no damage gives his hitpoints when the window opens; only draws where the total reaches them and the first half
     * does not are kept (a stack then kills unless he eats first; a split only if he does not eat in the gap). */
    if (injections) {
        static const char* const NAME[4] = {"stack", "split+1", "split+2", "split+3"};
        for (int i = 0; i < g_win_count; i++) {
            const Win* w = &g_win[i];
            if (!w->lead && w->hit_ratio < 0) continue;
            for (int rep = 0; rep < reps; rep++) {
                uint32_t seed = mix(mix((uint32_t)i, (uint32_t)rep), 0x1a1ec7);
                int start = draw_hp(w, seed ^ 0x48500000u);
                Win none = *w;
                memset(none.d, 0, sizeof(none.d));
                SimOut probe;
                replay_window(&fit, g_duels[w->duel], g_series[w->duel][w->side], &none, start, seed, &probe);
                int hp = probe.hp_window, half = w->total / 2;
                if (w->total < hp || half >= hp) continue;
                for (int p = 0; p < 4; p++) {
                    Win x = *w;
                    memset(x.d, 0, sizeof(x.d));
                    memset(x.n, 0, sizeof(x.n));
                    if (p == 0) { x.d[0] = w->total; x.n[0] = 1; }
                    else { x.d[0] = half; x.d[p] = w->total - half; x.n[0] = x.n[p] = 1; }
                    SimOut o;
                    replay_window(&fit, g_duels[w->duel], g_series[w->duel][w->side], &x, start, seed, &o);
                    emit(out, 'X', w, NAME[p], x.d, x.n, 0, o.death >= 0 && o.death <= 3, o.death, o.eat_visible, o.eat_true, o.hp_window, rep, o.rise, o.lead_eats);
                }
            }
        }
    }
    if (out_path) fclose(out);
    fprintf(stderr, "%d windows, %d reps\n", g_win_count, reps);
    return 0;
}
