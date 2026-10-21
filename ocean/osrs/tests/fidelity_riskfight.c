/* Own-recording fidelity of the Riskfight sim.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/fidelity_riskfight ocean/osrs/tests/fidelity_riskfight.c -lm      (repo root; run with cwd = repo root)
 *   /tmp/fidelity_riskfight [-s SEEDS] [-e EXACT_SEEDS] [-o fights.tsv] [-d divergences.tsv] tape...
 *
 * For every exported fight tape (tools/rollout_export.py) the sim is seeded from the first tick that exposes his gear, exactly as the calibrator seeds
 * (riskfight_seed.h tape_snapshot_construct: gear, bag, resources, veng, spec, recoil, hit points drawn inside his bar band; hidden values from the declared
 * priors), then both sides are driven tick by tick with what the recording shows:
 *   ours : the tape M lines at their landed tick (clicks, spec, Vengeance, attack), a swing intent (attack click) on the ticks of our recorded swing
 *          animations and Stop on every other tick, our overhead prayer and Piety of the T row.
 *   his  : from his recorded animations: weapon swaps read off the E rows (equip click), spec toggles before a recorded spec swing, one meal per eat animation,
 *          the Vengeance cast, a teleport click, an attack intent on each recorded swing tick and Stop otherwise.
 *   both : relative positions and the PID holder of the T rows each tick (no movement model is under test here).
 * Two modes. FREE: hits and damage are the sim's own rolls, run under SEEDS seeds; the comparison is distributional (hit chance, damage per landed hit
 * with a PIT of each recorded damage in the sim's per-fight distribution, our hit points per tick, his hit points against the bar band, echoes, outcome).
 * EXACT: the recorded damage of every landed attack is forced into the sim at landing time (OsrsPvpDamageOverride), so the sim decides only WHEN a hit
 * lands, what the echoes are, what heals and where hit points end up; each tick is compared with the recording and the first divergent tick and reason are
 * listed per category (our hit points, landing, echo, swing, consumption, his hit points against the bar band, command the harness could not issue).
 * Hits launched before the seed tick that land on the first window tick are injected as pending hits with their recorded damage and excluded from the
 * statistics. Unknown items (raw ids without a native row) are carried as ITEM_NONE / empty content: the TSV flags such fights.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include "riskfight_seed.h"

enum { MAXH = 8, MAX_HIST = 200, PIT_BINS = 10, OUTCOMES = 6 };
typedef enum { DIV_HP_US, DIV_LAND, DIV_ECHO, DIV_SWING, DIV_CONSUME, DIV_HIS_BAND, DIV_CMD, DIV_KINDS } DivKind;
static const char* const DIV_NAME[DIV_KINDS] = {"hp_us", "landing", "echo", "swing", "consume", "his_band", "command"};
static const char* const OUTCOME_NAME[OUTCOMES] = {"none", "kill", "death", "mutual", "his_escape", "our_escape"};

/* Attack animations of the recordings: each of these starts a swing that lands a splat on the target (3,700 of 3,728 of ours with 390 and 1,803 of 1,814 of his, the
 * others 97 to 100%). The list once lacked the spear, rapier, crossbow, hammer and halberd ones (2080, 2081, 5865, 1665, 8145, 4230, 386, 1378, 6600, 393, 10427: all 151
 * occurrences coincide with a splat on the target), so a swing of his with those weapons was never driven and its landings read as unexplained. */
static int is_attack_anim(int a) {
    static const int list[] = {390, 1658, 2066, 2067, 10172, 10173, 376, 401, 1062, 7045, 7644, 11057, 426, 11275, 1060, 440, 1667, 7514, 7515, 3297, 3298, 3299, 3300,
        2080, 2081, 5865, 1665, 8145, 4230, 386, 1378, 6600, 393, 10427};
    for (size_t i = 0; i < sizeof(list) / sizeof(*list); i++) if (list[i] == a) return 1;
    return 0;
}
static int is_spec_anim(int a) {
    static const int list[] = {11275, 1667, 7644, 7514, 7515, 1062, 1060, 3299, 3300, 1378};
    for (size_t i = 0; i < sizeof(list) / sizeof(*list); i++) if (list[i] == a) return 1;
    return 0;
}
static int is_teleport_anim(int a) { return a == 4069 || a == 4071 || a == 714; }

/* What the recording shows at one tick label. Sides: defender 0 = us, 1 = him; actor 0 = us, 1 = him. */
typedef struct { int n; int amount[MAXH]; char klass[MAXH]; } RecHits;
typedef struct {
    RecHits attack[2], echo[2];
    int swing[2], spec_swing[2], attack_anim[2];
    int eat, veng_cast, teleport;
    int weapon_raw; /* his wielded raw id from the E row, -2 unobserved, -1 none */
    int act_first, act_count;
} RecTick;
/* pid[tick - lo]: the PID holder (0 us, 1 him) the recording implies (nearest evidence tick), -1 without evidence in the fight. Evidence (g_pid_mode): echo timing of Fact 4, and
 * with -P 1/2 the landing tick of each melee or ranged swing (an attacker processed first lands a melee hit on its swing tick and a ranged one a tick later, a second one a tick
 * after that): the PID shuffles during a fight and echoes are sparse. Mode 2 leaves the tick's own swing evidence out of its PID. */
static int g_pid_mode = 2;
typedef struct { const Tape* tape; RecTick* at; int8_t* pid; uint8_t* meal; } Rec;

/* His meal kinds. The eat animation is the same for hard food (attack delay 3), karambwan (2) and a potion (0). Between two recorded swings s0, s1 of speed sp the
 * delay D = s1 - s0 - sp is the sum of the delays of the meals eaten in between, so the kinds are the lock-feasible combination of the eat ticks that sums to D
 * (a tick may carry a combo: H, K, P, HK, HP, PK, HPK as bits 1, 2, 4). Intervals no combination explains stay unknown (mask 0: the harness's default pick). */
static int meal_delay(int mask) { return (mask & 1 ? 3 : 0) + (mask & 2 ? 2 : 0); }
static int meal_feasible(int mask, int t, const int last[3]) {
    if (mask & 1 && (t < last[0] + 3 || t < last[1] + 3 || t < last[2] + 3)) return 0;
    if (mask & 2 && t < last[1] + 3) return 0;
    if (mask & 4 && (t < last[2] + 3 || t < last[1] + 3)) return 0;
    return 1;
}
static int meal_assign(const int* ticks, int n, int i, int want, int last[3], uint8_t* out) {
    static const int combos[7] = {1, 2, 4, 3, 5, 6, 7};
    if (i == n) return want == 0;
    for (int c = 0; c < 7; c++) {
        int mask = combos[c];
        if (meal_delay(mask) > want || !meal_feasible(mask, ticks[i], last)) continue;
        int saved[3] = {last[0], last[1], last[2]};
        for (int b = 0; b < 3; b++) if (mask >> b & 1) last[b] = ticks[i];
        out[i] = (uint8_t)mask;
        if (meal_assign(ticks, n, i + 1, want - meal_delay(mask), last, out)) return 1;
        memcpy(last, saved, sizeof(saved));
    }
    return 0;
}
static void meal_infer(Rec* rec, const Tape* tape) {
    int n = tape->hi - tape->lo + 1;
    rec->meal = calloc((size_t)n, 1);
    if (!rec->meal) abort();
    int last[3] = {-100, -100, -100}, previous_swing = -1;
    int eats[16], eat_count = 0;
    for (int t = tape->lo; t <= tape->hi; t++) {
        const RecTick* rt = &rec->at[t - tape->lo];
        if (rt->eat && eat_count < 16) eats[eat_count++] = t;
        if (!rt->swing[1]) continue;
        int speed = tape_tick(tape, t)->swing_him_speed;
        if (previous_swing >= 0 && eat_count > 0 && speed > 0 && t - previous_swing - speed >= 0 && eat_count <= 4) {
            uint8_t picked[16] = {0};
            int trial[3] = {last[0], last[1], last[2]};
            if (meal_assign(eats, eat_count, 0, t - previous_swing - speed, trial, picked)) {
                for (int i = 0; i < eat_count; i++) rec->meal[eats[i] - tape->lo] = picked[i];
                memcpy(last, trial, sizeof(last));
            }
        }
        previous_swing = t;
        eat_count = 0;
    }
}

static void rec_hit_add(RecHits* h, int amount, char klass) {
    if (h->n == MAXH) { fprintf(stderr, "more than %d recorded landings on one tick\n", MAXH); abort(); }
    h->amount[h->n] = amount; h->klass[h->n++] = klass;
}

static void rec_build(Rec* rec, const Tape* tape) {
    int n = tape->hi - tape->lo + 1;
    rec->tape = tape;
    rec->at = calloc((size_t)n, sizeof(RecTick));
    if (!rec->at) abort();
    for (int i = 0; i < n; i++) {
        const TapeTick* t = &tape->ticks[i];
        rec->at[i].weapon_raw = t->opponent_gear_source && t->opponent_gear_count > 3 ? t->opponent_gear[3] : -2;
        rec->at[i].act_first = -1;
    }
    for (int i = 0; i < tape->anim_count; i++) {
        const TapeAnim* a = &tape->anims[i];
        if (a->tick < tape->lo || a->tick > tape->hi) continue;
        RecTick* r = &rec->at[a->tick - tape->lo];
        int side = a->side == SIDE_US ? 0 : 1;
        if (is_attack_anim(a->anim)) { if (!r->swing[side]) r->attack_anim[side] = a->anim; r->swing[side]++; r->spec_swing[side] += is_spec_anim(a->anim); }
        if (side == 1) {
            r->eat += a->anim == ANIM_EAT;
            r->veng_cast += a->anim == ANIM_VENG_A || a->anim == ANIM_VENG_B;
            r->teleport += is_teleport_anim(a->anim);
        }
    }
    for (int i = 0; i < tape->hit_count; i++) {
        const TapeHit* h = &tape->hits[i];
        if (h->tick < tape->lo || h->tick > tape->hi || h->klass == 'P') continue;
        RecTick* r = &rec->at[h->tick - tape->lo];
        int defender = h->side == SIDE_US ? 0 : 1;
        if (h->klass == 'A' || h->klass == 'B') rec_hit_add(&r->attack[defender], h->amount, h->klass);
        else rec_hit_add(&r->echo[defender], h->amount, h->klass);
    }
    meal_infer(rec, tape);
    /* PID from echoes: an echo on S of a hit taken by R = 1 - S lands on the tick of that hit iff R was processed before S (Fact 4). */
    {
        rec->pid = malloc((size_t)n);
        int8_t* evidence = malloc((size_t)n);
        if (!rec->pid || !evidence) abort();
        memset(evidence, -1, (size_t)n);
        for (int y = tape->lo; y <= tape->hi; y++) {
            for (int s = 0; s < 2; s++) {
                const RecHits* echoes = &rec->at[y - tape->lo].echo[s];
                for (int e = 0; e < echoes->n; e++) {
                    int same = 0, next = 0, amount = echoes->amount[e];
                    const RecHits* now = &rec->at[y - tape->lo].attack[1 - s];
                    for (int i = 0; i < now->n; i++) same |= now->amount[i] > 0 && (amount == now->amount[i] / 10 + 1 || amount == now->amount[i] * 3 / 4);
                    if (y > tape->lo) {
                        const RecHits* before = &rec->at[y - 1 - tape->lo].attack[1 - s];
                        for (int i = 0; i < before->n; i++) next |= before->amount[i] > 0 && (amount == before->amount[i] / 10 + 1 || amount == before->amount[i] * 3 / 4);
                    }
                    if (same && !next) evidence[y - tape->lo] = (int8_t)(1 - s);
                    if (next && !same) evidence[y - 1 - tape->lo] = (int8_t)s;
                }
            }
        }
        int8_t* land = malloc((size_t)n);
        if (!land) abort();
        memset(land, -1, (size_t)n);
        if (g_pid_mode > 0) {
            for (int t = 0; t < n; t++) {
                for (int a = 0; a < 2; a++) {
                    const RecTick* at = &rec->at[t];
                    int anim = at->attack_anim[a];
                    if (at->swing[a] != 1 || at->spec_swing[a] || anim == 1667 || anim == 6600 || !is_attack_anim(anim)) continue;
                    if ((t >= 1 && rec->at[t - 1].swing[a]) || (t >= 2 && rec->at[t - 2].swing[a])) continue;
                    int ranged = anim == 426 || anim == 4230, offset = -1;
                    for (int o = 0; o < 4 && t + o < n && offset < 0; o++) if (rec->at[t + o].attack[1 - a].n > 0) offset = o;
                    if (offset < 0) continue;
                    int first = ranged ? offset == 1 : offset == 0, second = ranged ? offset == 2 : offset == 1;
                    if (first || second) land[t] = (int8_t)(first ? a : 1 - a);
                }
            }
        }
        for (int t = 0; t < n; t++) {
            int best = -1;
            for (int d = 0; d < n && best < 0; d++) {
                int early = t - d, late = t + d;
                if (early >= 0 && evidence[early] >= 0) best = evidence[early];
                else if (early >= 0 && land[early] >= 0 && (d > 0 || g_pid_mode == 1)) best = land[early];
                else if (late < n && evidence[late] >= 0) best = evidence[late];
                else if (late < n && land[late] >= 0 && (d > 0 || g_pid_mode == 1)) best = land[late];
            }
            rec->pid[t] = (int8_t)best;
        }
        free(land);
        free(evidence);
    }
    for (int i = 0; i < tape->action_count; i++) {
        const TapeAction* a = &tape->actions[i];
        if (a->tick < tape->lo || a->tick > tape->hi) continue;
        RecTick* r = &rec->at[a->tick - tape->lo];
        if (r->act_first < 0) r->act_first = i;
        r->act_count++;
    }
}

static int rec_outcome(const Tape* tape) {
    int dead_us = 0, dead_him = 0;
    for (int t = tape->lo; t <= tape->hi; t++) {
        const TapeTick* row = tape_tick(tape, t);
        dead_us |= row->hp <= 0;
        dead_him |= row->bar_now_ratio == 0 && row->bar_now_scale > 1;
    }
    if (dead_us && dead_him) return 3;
    if (dead_us) return 2;
    if (dead_him) return 1;
    return tape_escape_tick(tape) != INT_MAX ? 4 : 0;
}

typedef struct { int tick, attacker, target, kind, damage; } SimHit;
typedef struct {
    const Tape* tape;
    const Rec* rec;
    int t0, steps, exact, exclude_label1[2];
    int* used; /* [steps][2] recorded attack landings consumed by the sim, by label offset and defender */
    int* extra; /* [steps][2] sim attack landings without a recorded counterpart */
    SimHit* hits; int hit_count, hit_cap;
    int* hp[2]; int* bar_sim; uint8_t* swing[2]; uint8_t* consumed[2];
    int steps_done, outcome, his_weapon_raw, clock_unknown, trace_active;
    int div_first[DIV_KINDS], div_ticks[DIV_KINDS], div_last_tick[DIV_KINDS];
    char div_text[DIV_KINDS][200];
    int unknown_weapon_noted[8], unknown_weapon_count;
    long ch_ok[16], ch_n[16], ev_rec[8], ev_sim[8], ev_both[8];
} Run;

static void run_alloc(Run* r, const Tape* tape, const Rec* rec, int t0) {
    memset(r, 0, sizeof(*r));
    r->tape = tape; r->rec = rec; r->t0 = t0; r->steps = tape->hi - t0;
    r->used = calloc((size_t)r->steps * 2, sizeof(int));
    r->extra = calloc((size_t)r->steps * 2, sizeof(int));
    for (int i = 0; i < 2; i++) {
        r->hp[i] = calloc((size_t)r->steps, sizeof(int));
        r->swing[i] = calloc((size_t)r->steps, 1);
        r->consumed[i] = calloc((size_t)r->steps, 1);
    }
    r->bar_sim = calloc((size_t)r->steps, sizeof(int));
    r->hit_cap = 256;
    r->hits = malloc((size_t)r->hit_cap * sizeof(SimHit));
    if (!r->used || !r->extra || !r->hp[0] || !r->hp[1] || !r->swing[0] || !r->swing[1] || !r->consumed[0] || !r->consumed[1] || !r->bar_sim || !r->hits) abort();
}
static void run_free(Run* r) {
    free(r->used); free(r->extra); free(r->hits); free(r->bar_sim);
    for (int i = 0; i < 2; i++) { free(r->hp[i]); free(r->swing[i]); free(r->consumed[i]); }
}
static void run_reset(Run* r) {
    memset(r->used, 0, (size_t)r->steps * 2 * sizeof(int));
    memset(r->extra, 0, (size_t)r->steps * 2 * sizeof(int));
    r->hit_count = 0; r->steps_done = 0; r->outcome = 0; r->unknown_weapon_count = 0;
    memset(r->ch_ok, 0, sizeof(r->ch_ok)); memset(r->ch_n, 0, sizeof(r->ch_n));
    memset(r->ev_rec, 0, sizeof(r->ev_rec)); memset(r->ev_sim, 0, sizeof(r->ev_sim)); memset(r->ev_both, 0, sizeof(r->ev_both));
    for (int i = 0; i < DIV_KINDS; i++) { r->div_first[i] = -1; r->div_ticks[i] = 0; r->div_last_tick[i] = -1; r->div_text[i][0] = 0; }
}

static void note_div(Run* r, DivKind kind, int tick, const char* fmt, ...) {
    if (r->div_last_tick[kind] != tick) { r->div_ticks[kind]++; r->div_last_tick[kind] = tick; }
    if (r->div_first[kind] >= 0) return;
    r->div_first[kind] = tick;
    va_list ap; va_start(ap, fmt);
    vsnprintf(r->div_text[kind], sizeof(r->div_text[kind]), fmt, ap);
    va_end(ap);
}

static int label_of(const Run* r, int sim_tick) { return r->t0 + 1 + (sim_tick - RISKFIGHT_LOAD_TICK); }

static void observe_hit_cb(void* context, const OsrsPvpHitEvent* e) {
    Run* r = context;
    if (r->hit_count == r->hit_cap) {
        r->hit_cap *= 2;
        r->hits = realloc(r->hits, (size_t)r->hit_cap * sizeof(SimHit));
        if (!r->hits) abort();
    }
    r->hits[r->hit_count++] = (SimHit){label_of(r, e->tick), e->source, e->target, e->kind, e->damage};
}

/* Exact mode: the next recorded attack landing on this defender at this label, in recorded order. */
static int override_cb(void* context, int tick, int attacker, int defender, const PendingHit* hit) {
    (void)attacker; (void)hit;
    Run* r = context;
    int label = label_of(r, tick);
    if (label > r->tape->hi) return -1;
    int k = label - (r->t0 + 1);
    const RecHits* rec = &r->rec->at[label - r->tape->lo].attack[defender];
    int* used = &r->used[k * 2 + defender];
    if (*used < rec->n) return rec->amount[(*used)++];
    r->extra[k * 2 + defender]++;
    return -1;
}

/* Slots of the player's inventory by what they hold. */
static int find_raw_slot(const Player* p, int raw) {
    uint16_t canonical = osrs_item_canonical_raw_id((uint16_t)raw);
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[i])->raw_osrs_id == canonical && canonical) return i;
    return -1;
}
static int find_kind_slot(const Player* p, OsrsConsumableKind kind) {
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[i])->consumable_kind == kind) return i;
    return -1;
}

typedef struct { HumanCommand items[24]; int count; } Cmds;
static void push(Cmds* c, HumanCommand cmd) {
    if (c->count == 24) { fprintf(stderr, "command queue overflow\n"); abort(); }
    c->items[c->count++] = cmd;
}
static void push_click(Cmds* c, int slot) { push(c, (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = slot}); }
static void push_attack(Cmds* c, int target) { push(c, (HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = target}); }

/* Our recorded click: the slot it hit if the sim holds the same item there (or the same consumable family), else the item by raw id. */
static int our_click_slot(const Player* p, const TapeAction* a) {
    if (a->slot >= 0 && a->slot < OSRS_INVENTORY_SIZE) {
        const OsrsItemContentMetadata* here = osrs_inventory_cell_metadata(&p->inventory_cells[a->slot]);
        uint16_t canonical = osrs_item_canonical_raw_id((uint16_t)a->raw_id);
        if (here->raw_osrs_id == canonical && canonical) return a->slot;
        if (raw_content_known(a->raw_id)) {
            const OsrsItemContentMetadata* wanted = osrs_item_content_metadata(osrs_inventory_content_code_from_raw_osrs_id((uint16_t)a->raw_id));
            if (wanted->consumable_kind && wanted->consumable_kind == here->consumable_kind) return a->slot;
        }
    }
    return raw_content_known(a->raw_id) ? find_raw_slot(p, a->raw_id) : -1;
}

static void build_commands(Run* r, int x, RiskfightState* s, Cmds cmds[2]) {
    const Tape* tape = r->tape;
    const RecTick* rt = &r->rec->at[x - tape->lo];
    Player* us = &s->env.players[0];
    Player* him = &s->env.players[1];
    cmds[0].count = cmds[1].count = 0;
    for (int i = 0; i < rt->act_count; i++) {
        const TapeAction* a = &tape->actions[rt->act_first + i];
        if (a->kind == ACTION_CLICK) {
            int slot = our_click_slot(us, a);
            if (slot >= 0) push_click(&cmds[0], slot);
            else note_div(r, DIV_CMD, x, "our click slot %d raw %d: item not in the sim inventory", a->slot, a->raw_id);
        } else if (a->kind == ACTION_ATTACK) push_attack(&cmds[0], 1);
        else if (a->kind == ACTION_SPEC) push(&cmds[0], (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE});
        else push(&cmds[0], (HumanCommand){.kind = HUMAN_COMMAND_VENGEANCE});
    }
    if (rt->swing[0]) push_attack(&cmds[0], 1);
    else push(&cmds[0], (HumanCommand){.kind = HUMAN_COMMAND_STOP});

    int want = rt->weapon_raw;
    if (want >= 0 && want != r->his_weapon_raw) {
        int slot = raw_content_known(want) ? find_raw_slot(him, want) : -1;
        if (slot >= 0) push_click(&cmds[1], slot);
        else {
            int seen = 0;
            for (int i = 0; i < r->unknown_weapon_count; i++) seen |= r->unknown_weapon_noted[i] == want;
            if (!seen) {
                if (r->unknown_weapon_count < 8) r->unknown_weapon_noted[r->unknown_weapon_count++] = want;
                note_div(r, DIV_CMD, x, "his weapon raw %d cannot be wielded in the sim (unknown id or not in his inventory)", want);
            }
        }
        r->his_weapon_raw = want;
    }
    if (rt->spec_swing[1]) {
        if (!him->spec_armed) push(&cmds[1], (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE});
    } else if (rt->swing[1] && him->spec_armed)
        push(&cmds[1], (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE, .spec_mode = HUMAN_SPEC_DISARM});
    int mask = r->rec->meal[x - tape->lo];
    if (rt->eat && mask) {
        static const struct { int bit; OsrsConsumableKind kind; } order[3] = {{1, OSRS_CONSUMABLE_ANGLERFISH}, {4, OSRS_CONSUMABLE_BREW}, {2, OSRS_CONSUMABLE_KARAMBWAN}};
        for (int i = 0; i < 3; i++) {
            if (!(mask & order[i].bit)) continue;
            int slot = find_kind_slot(him, order[i].kind);
            if (slot >= 0) push_click(&cmds[1], slot);
            else note_div(r, DIV_CMD, x, "his meal kind %d inferred from his swing delay has no supply in the sim", order[i].bit);
        }
    } else for (int e = 0; e < rt->eat; e++) {
        /* Kind not explained by the swing delays: the first one the locks allow, hard food then karambwan then brew. */
        int slot = -1;
        int hard_locked = him->food_timer > 0 || e > 0;
        if (!hard_locked) slot = find_kind_slot(him, OSRS_CONSUMABLE_ANGLERFISH);
        if (slot < 0 && him->karambwan_timer == 0 && e < 2) slot = find_kind_slot(him, OSRS_CONSUMABLE_KARAMBWAN);
        if (slot < 0 && him->potion_timer == 0 && e < 3) slot = find_kind_slot(him, OSRS_CONSUMABLE_BREW);
        if (slot >= 0) push_click(&cmds[1], slot);
        else note_div(r, DIV_CMD, x, "his meal (animation %d) has no unlocked supply in the sim", ANIM_EAT);
    }
    if (rt->veng_cast) push(&cmds[1], (HumanCommand){.kind = HUMAN_COMMAND_VENGEANCE});
    if (rt->teleport) {
        int slot = find_kind_slot(him, OSRS_CONSUMABLE_TELEPORT);
        if (slot >= 0) push_click(&cmds[1], slot);
    }
    if (rt->swing[1]) push_attack(&cmds[1], 0);
    else push(&cmds[1], (HumanCommand){.kind = HUMAN_COMMAND_STOP});
}

static int cmp_int(const void* a, const void* b) { return *(const int*)a - *(const int*)b; }

/* Compare one finished step with the recording (exact mode reports the divergences; free mode only feeds the statistics). */
enum { CH_HP_US, CH_HIS_BAND, CH_SWING_US, CH_SWING_HIM, CH_CONSUME_US, CH_CONSUME_HIM, CH_LAND_US, CH_LAND_HIM, CH_ECHO_US, CH_ECHO_HIM, CH_VENG_US, CH_VENG_HIM,
    CH_SPEC_US, CH_SPEC_HIM, CH_COUNT };
static const char* const CH_NAME[CH_COUNT] = {"our hit points", "his hit points in bar band", "our swing ticks", "his swing ticks", "our consumption ticks", "his consumption ticks",
    "attack landings on us (count)", "attack landings on him (count)", "echoes on us", "echoes on him", "our Vengeance state", "his Vengeance state", "our spec energy", "his spec energy"};
static void chan(Run* r, int c, int ok) { r->ch_n[c]++; r->ch_ok[c] += ok; }
static void chane(Run* r, int e, int rec, int sim) { r->ev_rec[e] += rec; r->ev_sim[e] += sim; r->ev_both[e] += rec < sim ? rec : sim; }

/* Per-tick event rows of the teacher-forced pass (-m file), one tab-separated line per tick: what the recording and the sim show for landings, echoes, swings and weapons,
 * read by tests/classify_fidelity.py. Lists are comma-separated; a recorded landing or echo is `class damage`, a simulated one `kind:damage`. */
static FILE* g_events;
static void dump_rec_hits(FILE* out, const RecHits* h) {
    for (int i = 0; i < h->n; i++) fprintf(out, "%c%d,", h->klass[i], h->amount[i]);
}
static void dump_sim_hits(FILE* out, const Run* r, int from, int target, int echoes) {
    for (int i = from; i < r->hit_count; i++) {
        const SimHit* h = &r->hits[i];
        if (h->target != target) continue;
        if (echoes ? (h->kind == OSRS_HIT_RECOIL || h->kind == OSRS_HIT_VENGEANCE) : h->kind == OSRS_HIT_DIRECT) fprintf(out, "%d:%d,", h->kind, h->damage);
    }
}
static void dump_tick_row(Run* r, int x, int k, RiskfightState* s, int hits_before) {
    const Tape* tape = r->tape;
    const RecTick* rt = &r->rec->at[x - tape->lo];
    const TapeTick* row = tape_tick(tape, x);
    FILE* out = g_events;
    fprintf(out, "%s_%d\t%d\t%d\t%d", tape->session, tape->index, x, s->env.pid_holder, r->rec->pid[x - tape->lo]);
    for (int d = 0; d < 2; d++) { fputc('\t', out); dump_rec_hits(out, &rt->attack[d]); }
    for (int d = 0; d < 2; d++) { fputc('\t', out); dump_rec_hits(out, &rt->echo[d]); }
    for (int d = 0; d < 2; d++) { fputc('\t', out); dump_sim_hits(out, r, hits_before, d, 0); }
    for (int d = 0; d < 2; d++) { fputc('\t', out); dump_sim_hits(out, r, hits_before, d, 1); }
    for (int a = 0; a < 2; a++) fprintf(out, "\t%d%s", rt->attack_anim[a], rt->spec_swing[a] ? "s" : "");
    for (int a = 0; a < 2; a++) fprintf(out, "\t%d", r->swing[a][k]);
    for (int a = 0; a < 2; a++) fprintf(out, "\t%d", ITEM_DATABASE[s->env.players[a].equipped[GEAR_SLOT_WEAPON]].item_id);
    fprintf(out, "\t%d\t%d\t%d\t%d\t%d\t%d", rt->weapon_raw, s->env.players[0].veng_active, s->env.players[1].veng_active, row->veng_us, row->veng_him, rt->eat);
    for (int a = 0; a < 2; a++) fprintf(out, "\t%d/%d", s->env.players[a].attack_timer, s->env.players[a].equipped[GEAR_SLOT_RING] == ITEM_RING_OF_RECOIL);
    fputc('\n', out);
}

static void compare_step(Run* r, int k, int x, RiskfightState* s, int hits_before, int consumed_before[2]) {
    const Tape* tape = r->tape;
    const TapeTick* row = tape_tick(tape, x);
    const RecTick* rt = &r->rec->at[x - tape->lo];
    int hp0 = s->env.players[0].current_hitpoints, hp1 = s->env.players[1].current_hitpoints;
    if (g_trace && r->exact && r->trace_active) {
        printf("t%d pid%d hp us %d/%d him %d | rec land us %d him %d echo us %d him %d swing %d%d eat %d | sim:", x, s->env.pid_holder, hp0, row->hp, hp1,
            rt->attack[0].n, rt->attack[1].n, rt->echo[0].n, rt->echo[1].n, rt->swing[0] > 0, rt->swing[1] > 0, rt->eat);
        for (int i = hits_before; i < r->hit_count; i++) printf(" [k%d %d>%d %d]", r->hits[i].kind, r->hits[i].attacker, r->hits[i].target, r->hits[i].damage);
        printf(" swing %d%d | rec hits:", r->swing[0][k], r->swing[1][k]);
        for (int d = 0; d < 2; d++) { for (int i = 0; i < rt->attack[d].n; i++) printf(" %c%d@%d", rt->attack[d].klass[i], rt->attack[d].amount[i], d); for (int i = 0; i < rt->echo[d].n; i++) printf(" %c%d@%d", rt->echo[d].klass[i], rt->echo[d].amount[i], d); }
        printf(" | tm us %d/%d him %d/%d spd %d/%d wpn %d/%d food %d/%d", s->env.players[0].attack_timer, s->env.players[0].has_attack_timer, s->env.players[1].attack_timer,
            s->env.players[1].has_attack_timer, get_slot_gear_bonuses(&s->env.players[0])->attack_speed, get_slot_gear_bonuses(&s->env.players[1])->attack_speed,
            ITEM_DATABASE[s->env.players[0].equipped[GEAR_SLOT_WEAPON]].item_id, ITEM_DATABASE[s->env.players[1].equipped[GEAR_SLOT_WEAPON]].item_id,
            s->env.players[0].food_timer, s->env.players[1].food_timer);
        printf(" veng us %d/%d him %d/%d spec us %d/%d him %d/%d", s->env.players[0].veng_active, row->veng_us, s->env.players[1].veng_active, row->veng_him,
            s->env.players[0].special_energy, row->spec_us, s->env.players[1].special_energy, row->spec_him);
        printf(" pot %d kar %d vcd %d mag %d meal %d", s->env.players[1].potion_timer, s->env.players[1].karambwan_timer, s->env.players[0].veng_cooldown, s->env.players[0].current_magic, r->rec->meal[x - tape->lo]);
        printf("\n");
    }
    if (hp0 != row->hp) note_div(r, DIV_HP_US, x, "our hit points sim %d recording %d", hp0, row->hp);
    int sim_attack[2] = {0, 0}, rec_attack[2] = {rt->attack[0].n, rt->attack[1].n};
    int echo_sim[2][MAXH], echo_rec[2][MAXH], echo_sim_n[2] = {0, 0};
    for (int i = hits_before; i < r->hit_count; i++) {
        const SimHit* h = &r->hits[i];
        if (h->kind == OSRS_HIT_DIRECT) sim_attack[h->target]++;
        else if ((h->kind == OSRS_HIT_RECOIL || h->kind == OSRS_HIT_VENGEANCE) && echo_sim_n[h->target] < MAXH) echo_sim[h->target][echo_sim_n[h->target]++] = h->damage;
    }
    int echo_ok[2] = {1, 1};
    for (int d = 0; d < 2; d++) {
        if (sim_attack[d] != rec_attack[d])
            note_div(r, DIV_LAND, x, "attack landings on %s: sim %d, recording %d", d ? "him" : "us", sim_attack[d], rec_attack[d]);
        int n = rt->echo[d].n;
        for (int i = 0; i < n; i++) echo_rec[d][i] = rt->echo[d].amount[i];
        qsort(echo_sim[d], (size_t)echo_sim_n[d], sizeof(int), cmp_int);
        qsort(echo_rec[d], (size_t)n, sizeof(int), cmp_int);
        int same = echo_sim_n[d] == n;
        for (int i = 0; same && i < n; i++) same = echo_sim[d][i] == echo_rec[d][i];
        echo_ok[d] = same;
        if (!same) {
            char sim_s[64] = "", rec_s[64] = "";
            for (int i = 0; i < echo_sim_n[d]; i++) snprintf(sim_s + strlen(sim_s), sizeof(sim_s) - strlen(sim_s), "%d ", echo_sim[d][i]);
            for (int i = 0; i < n; i++) snprintf(rec_s + strlen(rec_s), sizeof(rec_s) - strlen(rec_s), "%d ", echo_rec[d][i]);
            note_div(r, DIV_ECHO, x, "echoes on %s: sim [%s] recording [%s]", d ? "him" : "us", sim_s, rec_s);
        }
    }
    for (int a = 0; a < 2; a++) {
        int sim_swung = r->swing[a][k];
        if ((rt->swing[a] > 0) != sim_swung) {
            const Player* p = &s->env.players[a];
            note_div(r, DIV_SWING, x, "%s swing: recording %d sim %d (sim timer %d has_timer %d interacting %d hp %d)", a ? "his" : "our",
                rt->swing[a], sim_swung, p->attack_timer, p->has_attack_timer, osrs_interaction_active(&p->interaction), p->current_hitpoints);
        }
    }
    int his_ate_rec = rt->eat > 0;
    if (his_ate_rec != (s->consume_ticks[1] > consumed_before[1]))
        note_div(r, DIV_CONSUME, x, "his meal: recording %d sim consumed %d", rt->eat, s->consume_ticks[1] - consumed_before[1]);
    int our_expect = 0;
    for (int i = 0; i < rt->act_count; i++) {
        const TapeAction* a = &tape->actions[rt->act_first + i];
        if (a->kind != ACTION_CLICK || !raw_content_known(a->raw_id)) continue;
        const OsrsItemContentMetadata* m = osrs_item_content_metadata(osrs_inventory_content_code_from_raw_osrs_id((uint16_t)a->raw_id));
        our_expect |= m->click_action == OSRS_CLICK_EAT || m->click_action == OSRS_CLICK_DRINK;
    }
    if (our_expect != (s->consume_ticks[0] > consumed_before[0]))
        note_div(r, DIV_CONSUME, x, "our meal/potion click: recording %d sim consumed %d", our_expect, s->consume_ticks[0] - consumed_before[0]);
    if (row->bar_now_ratio >= 0 && row->bar_now_scale > 1) {
        OsrsHealthBarRange band = osrs_health_bar_range(row->bar_now_ratio, row->bar_now_scale, 99, 121);
        if (band.kind == OSRS_HEALTH_BAR_KNOWN && (hp1 < band.lower || hp1 > band.upper))
            note_div(r, DIV_HIS_BAND, x, "his hit points sim %d outside the bar band %d-%d (bar %d/%d)", hp1, band.lower, band.upper, row->bar_now_ratio, row->bar_now_scale);
    }
    chan(r, CH_HP_US, hp0 == row->hp);
    chan(r, CH_SWING_US, (rt->swing[0] > 0) == r->swing[0][k]);
    chan(r, CH_SWING_HIM, (rt->swing[1] > 0) == r->swing[1][k]);
    chan(r, CH_CONSUME_US, our_expect == (s->consume_ticks[0] > consumed_before[0]));
    chan(r, CH_CONSUME_HIM, his_ate_rec == (s->consume_ticks[1] > consumed_before[1]));
    chan(r, CH_LAND_US, sim_attack[0] == rec_attack[0]);
    chan(r, CH_LAND_HIM, sim_attack[1] == rec_attack[1]);
    chan(r, CH_ECHO_US, echo_ok[0]);
    chan(r, CH_ECHO_HIM, echo_ok[1]);
    chan(r, CH_VENG_US, s->env.players[0].veng_active == row->veng_us);
    chan(r, CH_VENG_HIM, s->env.players[1].veng_active == row->veng_him);
    chan(r, CH_SPEC_US, s->env.players[0].special_energy == row->spec_us);
    chan(r, CH_SPEC_HIM, s->env.players[1].special_energy == row->spec_him);
    chane(r, 0, rt->swing[0] > 0, r->swing[0][k]);
    chane(r, 1, rt->swing[1] > 0, r->swing[1][k]);
    chane(r, 2, rec_attack[0], sim_attack[0]);
    chane(r, 3, rec_attack[1], sim_attack[1]);
    chane(r, 4, rt->echo[0].n, echo_sim_n[0]);
    chane(r, 5, rt->echo[1].n, echo_sim_n[1]);
    chane(r, 6, our_expect, s->consume_ticks[0] > consumed_before[0]);
    chane(r, 7, his_ate_rec, s->consume_ticks[1] > consumed_before[1]);
    if (g_events && r->exact == 2) dump_tick_row(r, x, k, s, hits_before);
    if (row->bar_now_ratio >= 0 && row->bar_now_scale > 1) {
        OsrsHealthBarRange band = osrs_health_bar_range(row->bar_now_ratio, row->bar_now_scale, 99, 121);
        if (band.kind == OSRS_HEALTH_BAR_KNOWN) chan(r, CH_HIS_BAND, hp1 >= band.lower && hp1 <= band.upper);
    }
}

/* Seed the sim at tick t0 and drive it to the end of the window. Returns 0 when the tape cannot be seeded. */
static int run_fight(Run* r, uint32_t seed, int exact, RiskfightState* state, RiskfightContext* ctx) {
    const Tape* tape = r->tape;
    int t0 = r->t0;
    run_reset(r);
    r->exact = exact;
    uint32_t rng = seed | 1;
    for (int i = 0; i < 4; i++) xorshift32(&rng);
    static RiskfightSnapshot snap;
    if (!tape_snapshot_construct(tape, t0, &DEFAULT_PRIORS, &rng, &snap)) return 0;
    const TapeTick* row0 = tape_tick(tape, t0);
    /* Declared priors where the recording cannot say: poison. */
    for (int side = 0; side < 2; side++) {
        OsrsPoisonState poison = {0};
        OsrsPoisonSource source = side == 0 ? tape_local_poison(row0, &poison) : OSRS_POISON_CLEAR_PRIOR;
        if (source == OSRS_POISON_UNKNOWN_STATE) { source = OSRS_POISON_CLEAR_PRIOR; poison = (OsrsPoisonState){0}; }
        snap.side[side].poison = poison;
        snap.side[side].poison_source = source;
    }
    /* Attack clocks from the last recorded swing of each side; a weapon without a known clock is an explicit unknown (no history). */
    r->clock_unknown = 0;
    for (int side = 0; side < 2; side++) {
        int swing = side == 0 ? row0->swing_us_tick : row0->swing_him_tick, speed = side == 0 ? row0->swing_us_speed : row0->swing_him_speed;
        RiskfightSideSnapshot* su = &snap.side[side];
        if (swing >= tape->lo && swing > t0 - ARRIVAL_AGE && speed > 0) {
            int clock = swing + speed - t0 - 1;
            su->attack_history = RISKFIGHT_ATTACK_OBSERVED;
            su->attack_timer = clock < -1 ? -1 : clock;
            su->attack_timer_uncapped = clock;
        } else {
            su->attack_history = RISKFIGHT_ATTACK_ABSENT;
            su->attack_timer = -1;
            su->attack_timer_uncapped = -1;
            r->clock_unknown |= swing >= tape->lo && speed <= 0;
        }
    }
    /* His recorded weapons enter his inventory so the equip clicks driven from the E rows have something to equip. */
    RiskfightLoadout* him = &snap.loadout[1];
    for (int t = t0; t <= tape->hi; t++) {
        int raw = r->rec->at[t - tape->lo].weapon_raw;
        if (raw < 0 || !raw_content_known(raw)) continue;
        uint16_t code = osrs_inventory_content_code_from_raw_osrs_id((uint16_t)raw);
        int have = 0, empty = -1, replace = -1;
        for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
            have |= him->inventory[i] == code;
            if (!him->inventory[i] && empty < 0) empty = i;
            if (osrs_item_content_metadata(him->inventory[i])->consumable_kind == OSRS_CONSUMABLE_ANGLERFISH) replace = i;
        }
        uint16_t item = raw_id_to_item(raw);
        if (have || item == ITEM_NONE || him->equipment[GEAR_SLOT_WEAPON] == item) continue;
        if (empty >= 0) him->inventory[empty] = code;
        else if (replace >= 0) him->inventory[replace] = code;
    }
    /* His ring of recoil: the recording decides (a recoil echo on us anywhere in the window means he wears one; damaging hits on him and none means he does not);
     * without either the seed's prior draw stays. */
    {
        int recoil_echo = 0, damaged = 0;
        for (int x = t0 + 1; x <= tape->hi; x++) {
            const RecTick* rt = &r->rec->at[x - tape->lo];
            for (int i = 0; i < rt->echo[0].n; i++) recoil_echo |= rt->echo[0].klass[i] == 'R';
            for (int i = 0; i < rt->attack[1].n; i++) damaged |= rt->attack[1].amount[i] > 0;
        }
        if (recoil_echo) snap.loadout[1].equipment[GEAR_SLOT_RING] = ITEM_RING_OF_RECOIL;
        else if (damaged) snap.loadout[1].equipment[GEAR_SLOT_RING] = ITEM_NONE;
    }
    /* Pending hits launched on tick t0 that land on t0+1: no launch is in the window, so they are given (recorded damage) and left out of the statistics. */
    /* His hit points: drawn inside the first bar band the recording shows after the seed tick, then raised by the damage recorded on him up to that tick (no meal
     * is assumed in between). Without any bar in the window the calibrator's uniform prior stays. The PID before the first recorded pairing is the first known one. */
    for (int x = t0 + 1; x <= tape->hi; x++) {
        const TapeTick* bar = tape_tick(tape, x);
        if (bar->bar_now_ratio < 0 || bar->bar_now_scale <= 1) continue;
        OsrsHealthBarRange band = osrs_health_bar_range(bar->bar_now_ratio, bar->bar_now_scale, 99, 121);
        if (band.kind != OSRS_HEALTH_BAR_KNOWN) continue;
        int damage = 0;
        for (int y = t0 + 1; y <= x; y++) damage += tape_damage_on_him(tape, y);
        int hp = band.lower + (int)(uniform(&rng) * (band.upper - band.lower + 1)) + damage;
        snap.loadout[1].hitpoints = snap.side[1].hitpoints = hp > 121 ? 121 : hp;
        break;
    }
    snap.pid_holder = r->rec->pid[t0 - tape->lo] >= 0 ? r->rec->pid[t0 - tape->lo] : snap.pid_holder;
    if (r->rec->pid[t0 - tape->lo] < 0) {
        for (int x = t0; x <= tape->hi; x++) {
            char pid = tape_tick(tape, x)->pid;
            if (pid == 'L' || pid == 'l' || pid == 'O' || pid == 'o') { snap.pid_holder = pid == 'L' || pid == 'l' ? 0 : 1; break; }
        }
    }
    snap.pending_known = 1;
    memset(r->exclude_label1, 0, sizeof(r->exclude_label1));
    if (t0 + 1 <= tape->hi) {
        const RecTick* first = &r->rec->at[t0 + 1 - tape->lo];
        const RecTick* at_seed = &r->rec->at[t0 - tape->lo];
        for (int attacker = 0; attacker < 2; attacker++) {
            int defender = 1 - attacker;
            if (!at_seed->swing[attacker]) continue;
            for (int i = 0; i < first->attack[defender].n; i++) {
                int* count = &snap.pending_count[attacker];
                if (*count == MAX_PENDING_HITS) abort();
                snap.pending[attacker][*count] = (PendingHit){.ticks_until_hit = 0, .attack_type = ATTACK_STYLE_NONE, .hit_success = 1, .damage = first->attack[defender].amount[i]};
                snap.pending_metadata[attacker][*count] = (PendingHitMetadata){.damage_mode = 1};
                (*count)++;
                r->exclude_label1[defender] = 1;
            }
        }
    }
    riskfight_load_state(state, ctx, &snap);
    state->env.pvp_runtime.hit_observer = observe_hit_cb;
    state->env.pvp_runtime.hit_observer_context = r;
    state->env.pvp_runtime.damage_override = exact ? override_cb : NULL;
    state->env.pvp_runtime.damage_override_context = r;
    r->his_weapon_raw = row0->opponent_gear[3];
    /* A ranged side whose commonest recorded swing gap is one tick under its weapon's speed fights on Rapid. */
    for (int side = 0; side < 2; side++) {
        int gaps[8] = {0}, previous = -1;
        for (int x = t0 + 1; x <= tape->hi; x++) {
            if (!r->rec->at[x - tape->lo].swing[side] || r->rec->at[x - tape->lo].spec_swing[side]) continue;
            if (previous >= 0 && x - previous < 8) gaps[x - previous]++;
            previous = x;
        }
        int mode = 1;
        for (int g = 2; g < 8; g++) if (gaps[g] > gaps[mode]) mode = g;
        Player* p = &state->env.players[side];
        if (g_trace && r->trace_active) printf("[rapid check side %d mode %d count %d style %d speed %d]\n", side, mode, gaps[mode], (int)get_slot_weapon_attack_style(p), get_slot_gear_bonuses(p)->attack_speed);
        if (gaps[mode] >= 3 && get_slot_weapon_attack_style(p) == ATTACK_STYLE_RANGED && mode == get_slot_gear_bonuses(p)->attack_speed - 1)
            riskfight_execute_command(state, ctx, side, &(HumanCommand){.kind = HUMAN_COMMAND_FIGHT_STYLE, .fight_style = FIGHT_STYLE_RAPID});
    }
    int base_x = FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH / 2, base_y = FIGHT_AREA_BASE_Y + FIGHT_AREA_HEIGHT / 2;
    Cmds cmds[2];
    for (int k = 0; k < r->steps && !state->env.episode_over; k++) {
        int x = t0 + 1 + k;
        const TapeTick* row = tape_tick(tape, x);
        Player* us = &state->env.players[0];
        Player* him_p = &state->env.players[1];
        /* Recorded geometry, PID holder and our prayers for this tick. */
        us->x = base_x; us->y = base_y;
        him_p->x = base_x + row->ox - row->ux; him_p->y = base_y + row->oy - row->uy;
        us->dest_x = us->x; us->dest_y = us->y; him_p->dest_x = him_p->x; him_p->dest_y = him_p->y;
        if (r->rec->pid[x - tape->lo] >= 0) state->env.pid_holder = r->rec->pid[x - tape->lo];
        else if (row->pid == 'L' || row->pid == 'l') state->env.pid_holder = 0;
        else if (row->pid == 'O' || row->pid == 'o') state->env.pid_holder = 1;
        else state->env.pid_holder = snap.pid_holder;
        us->prayer = overhead_of(row->overhead);
        us->offensive_prayer = row->piety ? OFFENSIVE_PRAYER_PIETY : OFFENSIVE_PRAYER_NONE;
        if (exact == 2) {
            const TapeTick* prev = tape_tick(tape, x - 1);
            us->current_hitpoints = prev->hp;
            us->veng_active = prev->veng_us; us->veng_cooldown = prev->veng_cooldown_us; us->special_energy = prev->spec_us;
            him_p->veng_active = prev->veng_him; him_p->veng_cooldown = prev->veng_cooldown_him; him_p->special_energy = prev->spec_him;
            /* His hit points are not recorded; where the last bar shown bounds them the replay keeps the sim inside that band (so he does not die before the recording says). */
            if (prev->bar_now_ratio > 0 && prev->bar_now_scale > 1) {
                OsrsHealthBarRange band = osrs_health_bar_range(prev->bar_now_ratio, prev->bar_now_scale, 99, 121);
                if (band.kind == OSRS_HEALTH_BAR_KNOWN && him_p->current_hitpoints > 0) {
                    if (him_p->current_hitpoints < band.lower) him_p->current_hitpoints = band.lower;
                    if (him_p->current_hitpoints > band.upper) him_p->current_hitpoints = band.upper;
                }
            }
        }
        build_commands(r, x, state, cmds);
        HumanCommandQueue queues[2] = {{.items = cmds[0].items, .count = cmds[0].count}, {.items = cmds[1].items, .count = cmds[1].count}};
        int hits_before = r->hit_count;
        int consumed_before[2] = {state->consume_ticks[0], state->consume_ticks[1]};
        riskfight_step_queues(state, ctx, &queues[0], &queues[1]);
        for (int a = 0; a < 2; a++)
            r->swing[a][k] = state->visible[1 - a].events[(state->env.tick - 1) % RF_HISTORY_TICKS][0] > 0;
        for (int a = 0; a < 2; a++) r->consumed[a][k] = state->consume_ticks[a] > consumed_before[a];
        r->hp[0][k] = state->env.players[0].current_hitpoints;
        r->hp[1][k] = state->env.players[1].current_hitpoints;
        r->bar_sim[k] = state->visible[0].health_bar;
        r->steps_done = k + 1;
        compare_step(r, k, x, state, hits_before, consumed_before);
        /* Recorded attack landings the sim did not produce this tick. */
        (void)hits_before;
    }
    int d0 = state->env.players[0].current_hitpoints <= 0, d1 = state->env.players[1].current_hitpoints <= 0;
    r->outcome = d0 && d1 ? 3 : d0 ? 2 : d1 ? 1 : state->escaped[1] ? 4 : state->escaped[0] ? 5 : 0;
    return 1;
}

/* ---- aggregation ---- */
typedef struct { int* v; long n, cap; } IntVec;
static void ivec_push(IntVec* v, int x) {
    if (v->n == v->cap) { v->cap = v->cap ? v->cap * 2 : 1024; v->v = realloc(v->v, (size_t)v->cap * sizeof(int)); if (!v->v) abort(); }
    v->v[v->n++] = x;
}
typedef struct {
    long fights, seeded_fights;
    long rec_land[2], rec_pos[2];
    double sim_land[2], sim_pos[2]; /* per-seed means summed over fights */
    IntVec rec_dmg[2], sim_dmg[2];
    double pit[2][PIT_BINS], pit_n[2];
    double hp_abs, hp_signed, hp_n, hp_exact;
    double band_in, band_n, band_dist;
    double bar_match, bar_n;
    long rec_echo[2][2], sim_echo[2][2]; /* [defender][recoil, vengeance], sim per-seed mean summed */
    double sim_echo_f[2][2];
    double outcome_agree; long outcome_n; double outcome_rec[OUTCOMES], outcome_sim[OUTCOMES];
    double swing_rec[2], swing_hit[2], swing_sim_extra[2];
    long ex_fights, ex_clean[DIV_KINDS], ex_clean_all, ex_ticks, ex_div_ticks[DIV_KINDS], ex_seed_runs, ex_seed_clean;
    long ch_ok[16], ch_n[16], tch_ok[16], tch_n[16], tev_rec[8], tev_sim[8], tev_both[8];
    long ex_first_hist[DIV_KINDS];
} Agg;

static int vec_cmp(const void* a, const void* b) { return *(const int*)a - *(const int*)b; }
static double vec_mean(const IntVec* v) { double s = 0; for (long i = 0; i < v->n; i++) s += v->v[i]; return v->n ? s / (double)v->n : NAN; }
static double vec_q(IntVec* v, double q) {
    if (!v->n) return NAN;
    qsort(v->v, (size_t)v->n, sizeof(int), vec_cmp);
    return v->v[(long)(q * (double)(v->n - 1))];
}
static double vec_share_ge(const IntVec* v, int x) { long c = 0; for (long i = 0; i < v->n; i++) c += v->v[i] >= x; return v->n ? (double)c / (double)v->n : NAN; }

typedef struct { double rec_hit_chance[2], sim_hit_chance[2]; } FightHit;

static int tape_unknown_weapon(const Tape* tape) {
    for (int i = 0; i < tape->unknown_count; i++) if (tape->unknown[i].where & TAPE_UNKNOWN_WORN) return 1;
    return 0;
}

int main(int argc, char** argv) {
    int seeds = 64, exact_seeds = 8, first = 1;
    const char* tsv_path = NULL;
    const char* div_path = NULL;
    for (; first < argc && argv[first][0] == '-'; first += 2) {
        if (!strcmp(argv[first], "-v")) { g_trace = 1; first--; continue; }
        if (first + 1 >= argc) { fprintf(stderr, "option %s needs a value\n", argv[first]); return 2; }
        if (!strcmp(argv[first], "-s")) seeds = atoi(argv[first + 1]);
        else if (!strcmp(argv[first], "-e")) exact_seeds = atoi(argv[first + 1]);
        else if (!strcmp(argv[first], "-o")) tsv_path = argv[first + 1];
        else if (!strcmp(argv[first], "-d")) div_path = argv[first + 1];
        else if (!strcmp(argv[first], "-m")) { g_events = fopen(argv[first + 1], "w"); if (!g_events) { perror("open -m"); return 1; } }
        else if (!strcmp(argv[first], "-P")) g_pid_mode = atoi(argv[first + 1]);
        else { fprintf(stderr, "unknown option %s\n", argv[first]); return 2; }
    }
    if (first >= argc || seeds < 1 || exact_seeds < 1) {
        fprintf(stderr, "usage: fidelity_riskfight [-s SEEDS] [-e EXACT_SEEDS] [-P PID_MODE 0 echo only, 1 echo+landings, 2 leave-one-out (default)] [-o fights.tsv] [-d divergences.tsv] [-m events.tsv] tape...\n");
        return 2;
    }
    g_ablation.his_defence = 99;
    static Agg agg[2]; /* 0 all seeded tapes, 1 tapes without unknown item ids */
    static RiskfightState state;
    static RiskfightContext ctx;
    riskfight_init_context((EncounterContext*)&ctx);
    riskfight_init_state((EncounterState*)&state, (EncounterContext*)&ctx);
    ctx.self_play = 1;
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&ctx);
    FILE* tsv = tsv_path ? fopen(tsv_path, "w") : NULL;
    FILE* dfile = div_path ? fopen(div_path, "w") : NULL;
    if ((tsv_path && !tsv) || (div_path && !dfile)) { perror("open output"); return 1; }
    if (tsv) fprintf(tsv, "tape\topponent\tunknown_items\tclock_unknown\tseed_tick\tticks\trec_swings_us\trec_swings_him\t"
        "rec_land_him\trec_pos_him\tsim_land_him\tsim_pos_him\trec_land_us\trec_pos_us\tsim_land_us\tsim_pos_us\t"
        "our_hp_mae\tour_hp_bias\this_band_in\trec_outcome\tsim_outcome_share\tsim_swing_us\tsim_swing_him\t"
        "exact_seed_clean\texact_first_div\texact_first_kind\texact_text\n");
    if (dfile) fprintf(dfile, "tape\tkind\tfirst_tick\tticks_diverging\ttext\n");
    int skipped = 0;
    for (int ti = first; ti < argc; ti++) {
        Tape tape;
#ifndef FID_NO_STAT_ITEMS /* the pre-fix engine has no stat-priced rows */
        osrs_stat_items_clear();
#endif
        tape_load(&tape, argv[ti]);
        const char* base = strrchr(argv[ti], '/') ? strrchr(argv[ti], '/') + 1 : argv[ti];
        int t0 = -1;
        for (int t = tape.lo; t < tape.hi; t++) if (tape_tick(&tape, t)->opponent_gear_source) { t0 = t; break; }
        int clean = tape.unknown_count == 0;
        Agg* sets[2] = {&agg[0], clean ? &agg[1] : NULL};
        for (int a = 0; a < 2; a++) if (sets[a]) sets[a]->fights++;
        if (t0 < 0) {
            skipped++;
            if (tsv) fprintf(tsv, "%s\t%s\t%d\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\tskipped: his gear never observed\t-\t-\t-\t-\t-\t-\t-\n", base, tape.name, tape.unknown_count);
            tape_free(&tape);
            continue;
        }
        Rec rec;
        rec_build(&rec, &tape);
        Run run;
        run_alloc(&run, &tape, &rec, t0);
        for (int a = 0; a < 2; a++) if (sets[a]) sets[a]->seeded_fights++;
        uint32_t tape_salt = osrs_lowbias32((uint32_t)atoi(tape.session) ^ ((uint32_t)tape.index * 2654435761u) ^ (uint32_t)tape.lo);

        /* Recorded numbers of this fight (the first window tick's launched hits are excluded). */
        long rec_land[2] = {0, 0}, rec_pos[2] = {0, 0}, rec_swings[2] = {0, 0};
        IntVec rec_d[2] = {{0}, {0}};
        long rec_echo[2][2] = {{0, 0}, {0, 0}};
        for (int x = t0 + 1; x <= tape.hi; x++) {
            const RecTick* rt = &rec.at[x - tape.lo];
            for (int d = 0; d < 2; d++) {
                if (!(x == t0 + 1 && 0)) {
                    for (int i = 0; i < rt->attack[d].n; i++) {
                        rec_land[d]++; rec_pos[d] += rt->attack[d].amount[i] > 0;
                        if (rt->attack[d].amount[i] > 0) ivec_push(&rec_d[d], rt->attack[d].amount[i]);
                    }
                }
                for (int i = 0; i < rt->echo[d].n; i++) rec_echo[d][rt->echo[d].klass[i] == 'V']++;
            }
            for (int a = 0; a < 2; a++) rec_swings[a] += rt->swing[a] > 0;
        }
        /* Remove the injected first-tick hits from the recorded counts and samples. */
        {
            const RecTick* first_rt = &rec.at[t0 + 1 - tape.lo];
            Run probe; (void)probe;
            for (int d = 0; d < 2; d++) {
                if (!(rec.at[t0 - tape.lo].swing[1 - d])) continue;
                for (int i = 0; i < first_rt->attack[d].n; i++) {
                    rec_land[d]--; rec_pos[d] -= first_rt->attack[d].amount[i] > 0;
                    if (first_rt->attack[d].amount[i] > 0) {
                        for (long j = 0; j < rec_d[d].n; j++) if (rec_d[d].v[j] == first_rt->attack[d].amount[i]) { rec_d[d].v[j] = rec_d[d].v[--rec_d[d].n]; break; }
                    }
                }
            }
        }
        int rec_out = rec_outcome(&tape);

        /* FREE mode over many seeds. */
        double sim_land[2] = {0, 0}, sim_pos[2] = {0, 0}, sim_swing[2] = {0, 0};
        double hp_abs = 0, hp_signed = 0, hp_n = 0, hp_exact = 0, band_in = 0, band_n = 0, band_dist = 0, bar_match = 0, bar_n = 0;
        double outcome_share[OUTCOMES] = {0};
        long hist[2][MAX_HIST] = {{0}};
        double sim_echo[2][2] = {{0, 0}, {0, 0}};
        IntVec sim_d[2] = {{0}, {0}};
        int seeded = 0;
        for (int s = 0; s < seeds; s++) {
            if (!run_fight(&run, osrs_lowbias32(tape_salt + (uint32_t)s * 0x9E3779B9u), 0, &state, &ctx)) break;
            seeded++;
            for (int i = 0; i < run.hit_count; i++) {
                const SimHit* h = &run.hits[i];
                if (h->kind == OSRS_HIT_DIRECT) {
                    if (h->tick == t0 + 1 && run.exclude_label1[h->target]) continue;
                    sim_land[h->target]++; sim_pos[h->target] += h->damage > 0;
                    if (h->damage > 0) { hist[h->target][h->damage < MAX_HIST ? h->damage : MAX_HIST - 1]++; ivec_push(&sim_d[h->target], h->damage); }
                } else if (h->kind == OSRS_HIT_RECOIL || h->kind == OSRS_HIT_VENGEANCE) sim_echo[h->target][h->kind == OSRS_HIT_VENGEANCE]++;
            }
            for (int k = 0; k < run.steps_done; k++) {
                const TapeTick* row = tape_tick(&tape, t0 + 1 + k);
                int err = run.hp[0][k] - row->hp;
                hp_abs += abs(err); hp_signed += err; hp_n++; hp_exact += err == 0;
                if (row->bar_now_ratio >= 0 && row->bar_now_scale > 1) {
                    OsrsHealthBarRange band = osrs_health_bar_range(row->bar_now_ratio, row->bar_now_scale, 99, 121);
                    if (band.kind == OSRS_HEALTH_BAR_KNOWN) {
                        int hp1 = run.hp[1][k], dist = hp1 < band.lower ? band.lower - hp1 : hp1 > band.upper ? hp1 - band.upper : 0;
                        band_n++; band_in += dist == 0; band_dist += dist;
                    }
                }
                if (row->bar_ratio >= 0) { bar_n++; bar_match += run.bar_sim[k] == row->bar_ratio; }
                sim_swing[0] += run.swing[0][k]; sim_swing[1] += run.swing[1][k];
            }
            outcome_share[run.outcome]++;
        }
        if (!seeded) {
            skipped++;
            if (tsv) fprintf(tsv, "%s\t%s\t%d\t-\t%d\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\tskipped: tape cannot be seeded\t-\t-\t-\t-\t-\t-\t-\n", base, tape.name, tape.unknown_count, t0);
            run_free(&run); free(rec.at); free(rec.pid); free(rec.meal); tape_free(&tape);
            continue;
        }
        for (int d = 0; d < 2; d++) { sim_land[d] /= seeded; sim_pos[d] /= seeded; sim_swing[d] /= seeded; for (int a = 0; a < 2; a++) sim_echo[d][a] /= seeded; }
        for (int o = 0; o < OUTCOMES; o++) outcome_share[o] /= seeded;

        /* EXACT mode. */
        int exact_clean = 0, exact_total = 0;
        int first_div = -1; DivKind first_kind = DIV_HP_US; char first_text[200] = "";
        int seed0_ticks[DIV_KINDS] = {0}, seed0_first[DIV_KINDS]; char seed0_text[DIV_KINDS][200];
        for (int k = 0; k < DIV_KINDS; k++) { seed0_first[k] = -1; seed0_text[k][0] = 0; }
        for (int s = 0; s < exact_seeds; s++) {
            run.trace_active = s == 0;
            if (!run_fight(&run, osrs_lowbias32(tape_salt + 0x51ED270Bu + (uint32_t)s * 0x9E3779B9u), 1, &state, &ctx)) break;
            exact_total++;
            /* Recorded attack landings the sim never produced. */
            for (int k = 0; k < run.steps_done; k++) {
                int x = t0 + 1 + k;
                for (int d = 0; d < 2; d++) {
                    int missing = rec.at[x - tape.lo].attack[d].n - run.used[k * 2 + d];
                    (void)missing;
                }
            }
            int any = 0;
            for (int kd = 0; kd < DIV_KINDS; kd++) any |= kd != DIV_CMD && run.div_first[kd] >= 0;
            exact_clean += !any;
            if (s == 0) {
                for (int kd = 0; kd < DIV_KINDS; kd++) {
                    seed0_ticks[kd] = run.div_ticks[kd]; seed0_first[kd] = run.div_first[kd];
                    snprintf(seed0_text[kd], sizeof(seed0_text[kd]), "%s", run.div_text[kd]);
                    if (kd != DIV_CMD && run.div_first[kd] >= 0 && (first_div < 0 || run.div_first[kd] < first_div)) {
                        first_div = run.div_first[kd]; first_kind = (DivKind)kd; snprintf(first_text, sizeof(first_text), "%s", run.div_text[kd]);
                    }
                }
                agg[0].ex_ticks += run.steps_done;
                for (int c = 0; c < CH_COUNT; c++) { agg[0].ch_ok[c] += run.ch_ok[c]; agg[0].ch_n[c] += run.ch_n[c]; if (clean) { agg[1].ch_ok[c] += run.ch_ok[c]; agg[1].ch_n[c] += run.ch_n[c]; } }
                if (clean) agg[1].ex_ticks += run.steps_done;
            }
        }
        /* Teacher-forced pass (seed 0): each step starts from the recorded state of the previous tick, so a channel's error is that tick's mechanics alone. */
        run.trace_active = g_trace;
        if (g_trace) printf("--- teacher-forced pass %s\n", base);
        if (run_fight(&run, osrs_lowbias32(tape_salt + 0x51ED270Bu), 2, &state, &ctx))
            for (int c = 0; c < CH_COUNT; c++) { agg[0].tch_ok[c] += run.ch_ok[c]; agg[0].tch_n[c] += run.ch_n[c]; if (clean) { agg[1].tch_ok[c] += run.ch_ok[c]; agg[1].tch_n[c] += run.ch_n[c]; } }
        for (int e = 0; e < 8; e++) {
            agg[0].tev_rec[e] += run.ev_rec[e]; agg[0].tev_sim[e] += run.ev_sim[e]; agg[0].tev_both[e] += run.ev_both[e];
            if (clean) { agg[1].tev_rec[e] += run.ev_rec[e]; agg[1].tev_sim[e] += run.ev_sim[e]; agg[1].tev_both[e] += run.ev_both[e]; }
        }
        if (dfile) for (int kd = 0; kd < DIV_KINDS; kd++)
            if (seed0_first[kd] >= 0) fprintf(dfile, "%s\t%s\t%d\t%d\t%s\n", base, DIV_NAME[kd], seed0_first[kd], seed0_ticks[kd], seed0_text[kd]);

        /* PIT of each recorded damage in the sim's per-fight distribution. */
        double pit_bins[2][PIT_BINS] = {{0}};
        double pit_n[2] = {0, 0};
        for (int d = 0; d < 2; d++) {
            long total = 0; for (int v = 1; v < MAX_HIST; v++) total += hist[d][v];
            if (!total) continue;
            for (long i = 0; i < rec_d[d].n; i++) {
                int x = rec_d[d].v[i] < MAX_HIST ? rec_d[d].v[i] : MAX_HIST - 1;
                long below = 0; for (int v = 1; v < x; v++) below += hist[d][v];
                double pit = ((double)below + 0.5 * (double)hist[d][x]) / (double)total;
                int bin = (int)(pit * PIT_BINS); if (bin >= PIT_BINS) bin = PIT_BINS - 1;
                pit_bins[d][bin]++; pit_n[d]++;
            }
        }

        for (int a = 0; a < 2; a++) {
            Agg* g = sets[a];
            if (!g) continue;
            for (int d = 0; d < 2; d++) {
                g->rec_land[d] += rec_land[d]; g->rec_pos[d] += rec_pos[d];
                g->sim_land[d] += sim_land[d]; g->sim_pos[d] += sim_pos[d];
                for (long i = 0; i < rec_d[d].n; i++) ivec_push(&g->rec_dmg[d], rec_d[d].v[i]);
                for (long i = 0; i < sim_d[d].n; i++) ivec_push(&g->sim_dmg[d], sim_d[d].v[i]);
                for (int b = 0; b < PIT_BINS; b++) g->pit[d][b] += pit_bins[d][b];
                g->pit_n[d] += pit_n[d];
                for (int e = 0; e < 2; e++) { g->rec_echo[d][e] += rec_echo[d][e]; g->sim_echo_f[d][e] += sim_echo[d][e]; }
                g->swing_rec[d] += (double)rec_swings[d]; g->swing_hit[d] += sim_swing[d];
            }
            g->hp_abs += hp_abs / seeded; g->hp_signed += hp_signed / seeded; g->hp_n += hp_n / seeded; g->hp_exact += hp_exact / seeded;
            g->band_in += band_in / seeded; g->band_n += band_n / seeded; g->band_dist += band_dist / seeded;
            g->bar_match += bar_match / seeded; g->bar_n += bar_n / seeded;
            g->outcome_agree += outcome_share[rec_out]; g->outcome_n++;
            g->outcome_rec[rec_out]++;
            for (int o = 0; o < OUTCOMES; o++) g->outcome_sim[o] += outcome_share[o];
            g->ex_fights++; g->ex_seed_runs += exact_total; g->ex_seed_clean += exact_clean;
            int all_clean = 1;
            for (int kd = 0; kd < DIV_KINDS; kd++) {
                g->ex_div_ticks[kd] += seed0_ticks[kd];
                if (kd != DIV_CMD) { g->ex_clean[kd] += seed0_first[kd] < 0; all_clean &= seed0_first[kd] < 0; }
            }
            g->ex_clean_all += all_clean;
        }
        if (tsv) {
            fprintf(tsv, "%s\t%s\t%d\t%d\t%d\t%d\t%ld\t%ld\t%ld\t%ld\t%.2f\t%.2f\t%ld\t%ld\t%.2f\t%.2f\t%.2f\t%.2f\t%.3f\t%s\t",
                base, tape.name, tape.unknown_count, run.clock_unknown, t0, tape.hi - t0, rec_swings[0], rec_swings[1],
                rec_land[1], rec_pos[1], sim_land[1], sim_pos[1], rec_land[0], rec_pos[0], sim_land[0], sim_pos[0],
                hp_n ? hp_abs / hp_n : NAN, hp_n ? hp_signed / hp_n : NAN, band_n ? band_in / band_n : NAN, OUTCOME_NAME[rec_out]);
            for (int o = 0; o < OUTCOMES; o++) if (outcome_share[o] > 0) fprintf(tsv, "%s%s=%.2f", o && 0 ? "," : "", OUTCOME_NAME[o], outcome_share[o]);
            fprintf(tsv, "\t%.1f\t%.1f\t%d/%d\t%d\t%s\t%s\n", sim_swing[0], sim_swing[1], exact_clean, exact_total, first_div,
                first_div >= 0 ? DIV_NAME[first_kind] : "-", first_div >= 0 ? first_text : "-");
        }
        free(rec_d[0].v); free(rec_d[1].v); free(sim_d[0].v); free(sim_d[1].v);
        run_free(&run); free(rec.at); free(rec.pid); free(rec.meal); tape_free(&tape);
    }
    if (tsv) fclose(tsv);
    if (dfile) fclose(dfile);
    if (g_events) fclose(g_events);

    const Agg* a = &agg[0];
    printf("fidelity_riskfight: %ld tapes (%ld seeded, %d skipped), %d free seeds, %d exact seeds | our attacks on him: hit chance rec %.3f sim %.3f (n rec %ld) | "
        "his on us: rec %.3f sim %.3f (n rec %ld) | our hp MAE %.2f bias %+.2f exact-tick share %.3f | outcome agree %.3f | exact clean fights %ld/%ld\n",
        a->fights, a->seeded_fights, skipped, seeds, exact_seeds,
        a->rec_land[1] ? (double)a->rec_pos[1] / (double)a->rec_land[1] : NAN, a->sim_land[1] > 0 ? a->sim_pos[1] / a->sim_land[1] : NAN, a->rec_land[1],
        a->rec_land[0] ? (double)a->rec_pos[0] / (double)a->rec_land[0] : NAN, a->sim_land[0] > 0 ? a->sim_pos[0] / a->sim_land[0] : NAN, a->rec_land[0],
        a->hp_n ? a->hp_abs / a->hp_n : NAN, a->hp_n ? a->hp_signed / a->hp_n : NAN, a->hp_n ? a->hp_exact / a->hp_n : NAN,
        a->outcome_n ? a->outcome_agree / (double)a->outcome_n : NAN, a->ex_clean_all, a->ex_fights);

    static const char* const SETS[2] = {"all seeded tapes", "tapes without unknown item ids"};
    for (int si = 0; si < 2; si++) {
        Agg* g = &agg[si];
        printf("\n### %s: %ld tapes (%ld seeded)\n\n| quantity | recording | sim |\n|---|---|---|\n", SETS[si], g->fights, g->seeded_fights);
        for (int d = 1; d >= 0; d--) {
            const char* who = d ? "our attacks on him" : "his attacks on us";
            double rec_pos_share = g->rec_land[d] ? (double)g->rec_pos[d] / (double)g->rec_land[d] : NAN;
            double sim_pos_share = g->sim_land[d] > 0 ? g->sim_pos[d] / g->sim_land[d] : NAN;
            printf("| %s: landed attacks (n; sim per seed) | %ld | %.1f |\n", who, g->rec_land[d], g->sim_land[d]);
            printf("| %s: share with damage > 0 | %.3f | %.3f |\n", who, rec_pos_share, sim_pos_share);
            printf("| %s: damaging hit mean / median / p90 | %.1f / %.0f / %.0f | %.1f / %.0f / %.0f |\n", who,
                vec_mean(&g->rec_dmg[d]), vec_q(&g->rec_dmg[d], 0.5), vec_q(&g->rec_dmg[d], 0.9),
                vec_mean(&g->sim_dmg[d]), vec_q(&g->sim_dmg[d], 0.5), vec_q(&g->sim_dmg[d], 0.9));
            printf("| %s: damaging hit share 40+ (max-hit proxy) | %.3f | %.3f |\n", who, vec_share_ge(&g->rec_dmg[d], 40), vec_share_ge(&g->sim_dmg[d], 40));
            double mean_pit = 0;
            for (int b = 0; b < PIT_BINS; b++) mean_pit += g->pit[d][b] * (b + 0.5) / PIT_BINS;
            printf("| %s: PIT of recorded damage in the fight's sim distribution (n %.0f; mean, 10 bins %%) | mean %.3f | ", who, g->pit_n[d], g->pit_n[d] ? mean_pit / g->pit_n[d] : NAN);
            for (int b = 0; b < PIT_BINS; b++) printf("%.0f%s", g->pit_n[d] ? 100 * g->pit[d][b] / g->pit_n[d] : NAN, b + 1 < PIT_BINS ? " " : "");
            printf(" |\n");
            printf("| %s: recoil echoes / Vengeance echoes (n; sim per seed) | %ld / %ld | %.1f / %.1f |\n", who, g->rec_echo[d][0], g->rec_echo[d][1], g->sim_echo_f[d][0], g->sim_echo_f[d][1]);
        }
        printf("| our swings (n; sim per seed) | %.0f | %.1f |\n", g->swing_rec[0], g->swing_hit[0]);
        printf("| his swings (n; sim per seed) | %.0f | %.1f |\n", g->swing_rec[1], g->swing_hit[1]);
        printf("| our hit points per tick: mean abs error / mean signed error / share exact (ticks %.0f) | - | %.2f / %+.2f / %.3f |\n", g->hp_n, g->hp_n ? g->hp_abs / g->hp_n : NAN, g->hp_n ? g->hp_signed / g->hp_n : NAN, g->hp_n ? g->hp_exact / g->hp_n : NAN);
        printf("| his hit points inside the bar band on bar-refresh ticks (ticks %.0f), mean miss in hp | - | %.3f, %.2f |\n", g->band_n, g->band_n ? g->band_in / g->band_n : NAN, g->band_n ? g->band_dist / g->band_n : NAN);
        printf("| his visible bar ratio equal to the recorded bar (ticks %.0f) | - | %.3f |\n", g->bar_n, g->bar_n ? g->bar_match / g->bar_n : NAN);
        printf("| outcome (kill / death / mutual / his escape / none): fights recording | %.0f / %.0f / %.0f / %.0f / %.0f | %.1f / %.1f / %.1f / %.1f / %.1f |\n",
            g->outcome_rec[1], g->outcome_rec[2], g->outcome_rec[3], g->outcome_rec[4], g->outcome_rec[0],
            g->outcome_sim[1], g->outcome_sim[2], g->outcome_sim[3], g->outcome_sim[4], g->outcome_sim[0]);
        printf("| outcome agreement (share of seeds, mean over fights) | - | %.3f |\n", g->outcome_n ? g->outcome_agree / (double)g->outcome_n : NAN);
        printf("| exact mode: fights with no divergence in any category (seed 0) / seeds fully clean | - | %ld of %ld / %ld of %ld |\n", g->ex_clean_all, g->ex_fights, g->ex_seed_clean, g->ex_seed_runs);
        for (int kd = 0; kd < DIV_KINDS; kd++)
            printf("| exact mode, %s: fights without divergence / diverging ticks of %ld | - | %ld of %ld / %ld |\n", DIV_NAME[kd], g->ex_ticks,
                kd == DIV_CMD ? 0 : g->ex_clean[kd], g->ex_fights, g->ex_div_ticks[kd]);
        for (int c = 0; c < CH_COUNT; c++)
            printf("| exact-mode channel, %s: ticks agreeing, free run / teacher-forced (n ticks %ld) | - | %.2f%% / %.2f%% |\n", CH_NAME[c], g->ch_n[c],
                g->ch_n[c] ? 100.0 * (double)g->ch_ok[c] / (double)g->ch_n[c] : NAN, g->tch_n[c] ? 100.0 * (double)g->tch_ok[c] / (double)g->tch_n[c] : NAN);
        static const char* const EV_NAME[8] = {"our swings", "his swings", "attack landings on us", "attack landings on him", "echoes on us", "echoes on him", "our consumption ticks", "his consumption ticks"};
        for (int e = 0; e < 8; e++)
            printf("| teacher-forced events, %s: recorded / sim / matched | %ld | %ld / %ld |\n", EV_NAME[e], g->tev_rec[e], g->tev_sim[e], g->tev_both[e]);
    }
    return 0;
}
