/* The lookahead as a process: protocol v2. One request per decision on stdin, one scored line per request on stdout.
 *
 *   cc -std=c11 -O2 -I. -o gs_lookahead_server ocean/osrs/tests/gs_lookahead_server.c -lm      (cwd = repo root: the .cmap maps under ocean/osrs/data are relative)
 *   gs_lookahead_server MODEL.fit EAT_MIXTURE.fit [WORLDS HORIZON [THREADS]]      defaults: 512 worlds, horizon 15, 8 threads
 *
 * STARTUP: the server loads the fits, runs a synthetic decision stream through the whole path, then prints exactly one line
 *   READY <worst_warm_micros> <macro names joined with ';'>
 * before it reads stdin (worst_warm_micros = slowest decision after the first two cold ones). stderr carries one line per gear prior used.
 *
 * REQUEST: LA id tick, B belief, K stakes, F feasibility, O own state, W worn, G bag, SHIELD/ARMOR/Q/H/RA/PS observations, P pending hits, R plan, END.
 * P KNOWN n followed by n D records, or P UNKNOWN. P VARIANTS localCount opponentCount retains both concrete-holder queues; each D
 * adds a holder (0 LOCAL, 1 OPPONENT) before source. Fixed D records omit holder.
 * D [holder] source delay kind style special success mode damage min max accuracyBp prayer smite.
 * R KNOWN n followed by n C records: ONLY the unsent remainder of the plan this engine emitted earlier (an earlier response's A records echoed back,
 * due rebased to > tick). R KNOWN 0 = nothing committed. R UNKNOWN is rejected: Java never invents C records.
 * C due kind arg timing [count candidates...]  (the record grammar below). timing 0 samples an unsent lower bound; 1 preserves a known landing.
 * kind 0 click inventory slot arg, 1 attack, 2 spec enable, 3 vengeance, 4 spec disarm, 5 unequip worn slot arg, 6 ready (arg early),
 * 7 hit wait (arg min damage or -1), 8 energy>=arg else skip to group end, 9 hp>=arg skip next, 10 GROUP BOUNDARY, 11 equip raw item,
 * 12 inventory primary click raw item, 13 spec toggle, 14/15/16 eat/equip/unequip by recipe (count + candidates; negative eat candidates denote a dose
 * family, positive an exact raw id), 17 STOP, 18 AlignedReady(absolute attackTick, timing 1), 19 wait until our Voidwaker special swing has launched.
 * PS side KNOWN severity pulse immunity imports exact actor state.
 * PS side IMMUNE negativeVarp [observationAgeTicks] imports a conservative immunity lower bound; age defaults to zero.
 * Admission requires lowerBound > horizon + age (unknown pulse phase excluded).
 * PS side CLEAR_PRIOR declares a clean initial-state cohort, not observed truth. PS side UNKNOWN excludes poison-sensitive requests.
 * Missing PS is supported only in the legacy no-poison-weapon, clean-initial-status cohort.
 * BH p++ supports immune-target horizons only until multi-hit proc granularity and application timer phase are independently established.
 * RA OBSERVED raw172 (0 ON, 1 OFF), RA OFF_PRIOR declared simulation cohort, or RA UNKNOWN. Only OFF admits STOP-based programs.
 * Empty queues/plans require explicit KNOWN 0.
 *
 * RESPONSE (one line): R id server_micros best_index, then for each of the 18 macros `feasible kill death value worlds` (LA_NAME order), then
 * `A n` followed by n records `due kind arg timing [count candidates...]`: the chosen macro's plan in the same grammar as the C records.
 * Macro 0 `continue` keeps the committed remainder (its A records echo it). Macro 1 `no new clicks` is a CANCEL of the remainder (A 0).
 * Another macro replaces the remainder with its own script. Ties go to continue: another macro must be strictly better by value.
 * Unavailable: `UNAVAILABLE id reason [raw]`, only for missing own-side observations (poison state, auto-retaliate, attack history interval).
 * Declared priors (request_declare_priors): `P UNKNOWN` is served as an empty queue and an UNKNOWN own attack clock as ready, and the response ends ` ASSUME n name...`
 * (pending_empty, own_attack_ready), so a tick the plugin could not observe fully still gets a decision and says what it assumed.
 *
 * Opponent weapon/shield/armor/ammo are never UNAVAILABLE: unknown/unsupported ids get declared priors (see request_gear_priors), the raw ids stay
 * in the request and in the stderr prior line, never silently dropped. */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "riskfight_rollout.h"
#include <errno.h>
#include <limits.h>
/* Requests are an external boundary; these checks must run under NDEBUG too. */
#undef assert
#define assert(condition) do { if (!(condition)) { fprintf(stderr, "invalid lookahead request: %s\n", #condition); exit(2); } } while (0)

enum { INERT_ID = 27281, SERVER_WORLDS = 512, SERVER_THREADS = 8 };
static const int CANONICAL_FROM[] = {24592, 24595, 24598, 24601, 24603, 24605, 22441, 20513, 28067, 28070, 21287};
static const int CANONICAL_TO[] = {13441, 3144, 3024, 3026, 3028, 3030, 22322, 10551, 10551, 10828, 21295};
static const int SIM_IDS[] = {13441, 3144, 3024, 3026, 3028, 3030, 6685, 6687, 6689, 6691, 12695, 12697, 12699, 12701, 23691, 23694, 8013, 24621, 24225, 27690, 12954,
    22322, 10551, 10828, 21295, 1725, 1079, 4087, 7462, 11840, 2550, 2434, 139, 141, 143, /* prayer potion (4)..(1) */
    27861, 27857, 24617, 27867, 22114, 29280, 29560, 29564, 12904, 8850, 4131, 7460, -1};

static int canonical(int id) {
    if (id >= 0 && id <= UINT16_MAX) id = osrs_item_canonical_raw_id((uint16_t)id);
    for (size_t i = 0; i < sizeof(CANONICAL_FROM) / sizeof(int); i++) if (CANONICAL_FROM[i] == id) id = CANONICAL_TO[i];
    for (size_t i = 0; i < sizeof(SIM_IDS) / sizeof(int); i++) if (SIM_IDS[i] == id) return id;
    switch (id) {
        case INERT_ID: case 229: case 995: case 27997: case 30000: case 26651: case 28098:
            return INERT_ID;
        default:
            return -2;
    }
}

/* Declared gear priors. The sim item table covers a fixed set; any other worn item is simulated as the nearest supported item of the same slot:
 * weapons share attack style and two-handedness, carry no special attack or effect, and are nearest by attack speed, then bonuses; other slots are
 * nearest by bonuses among effect-free items, or bare when the id has no known stats. Unknown ids are told apart from known ones in the stderr line,
 * printed once per substitution; the raw id always stays in the request. */
typedef struct { int atk[5], def[5], str, rstr, mdmg, prayer, speed, two_handed, style; } GearStats;
enum { GS_PRIOR_MAX = 64 };
static int g_prior_seen[256][2], g_prior_seen_count;
static int g_prior_out[GS_PRIOR_MAX][3], g_prior_out_count; /* triples of the request being parsed */

static int prior_db_slot(int kit) {
    switch (kit) {
        case 0: return SLOT_HEAD; case 1: return SLOT_CAPE; case 2: return SLOT_NECK; case 3: return SLOT_WEAPON; case 4: return SLOT_BODY;
        case 5: return SLOT_SHIELD; case 7: return SLOT_LEGS; case 9: return SLOT_HANDS; case 10: return SLOT_FEET; case 12: return SLOT_RING;
        default: return -1;
    }
}
static const GsJavaItemRow* prior_row(int raw) {
    for (size_t i = 0; i < sizeof(GS_JAVA_ITEMS) / sizeof(*GS_JAVA_ITEMS); i++) if (GS_JAVA_ITEMS[i].id == raw) return &GS_JAVA_ITEMS[i];
    return NULL;
}
static GearStats prior_stats_row(const GsJavaItemRow* r) {
    int style = gs_style_of(r->astab, r->aslash, r->acrush, r->arange, r->rstr, r->amagic);
    return (GearStats){{r->astab, r->aslash, r->acrush, r->arange, r->amagic}, {r->dstab, r->dslash, r->dcrush, r->drange, r->dmagic}, r->str, r->rstr,
        (int)(r->mdmg + 0.5), r->prayer, r->aspeed, r->two_handed,
        style == GS_MAGIC ? ATTACK_STYLE_MAGIC : style == GS_RANGED ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE};
}
static GearStats prior_stats_item(int idx) {
    const Item* it = &ITEM_DATABASE[idx];
    int style = gs_style_of(it->attack_stab, it->attack_slash, it->attack_crush, it->attack_ranged, it->ranged_strength, it->attack_magic);
    return (GearStats){{it->attack_stab, it->attack_slash, it->attack_crush, it->attack_ranged, it->attack_magic},
        {it->defence_stab, it->defence_slash, it->defence_crush, it->defence_ranged, it->defence_magic}, it->melee_strength, it->ranged_strength,
        it->magic_damage, it->prayer, it->attack_speed, it->two_handed,
        style == GS_MAGIC ? ATTACK_STYLE_MAGIC : style == GS_RANGED ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE};
}
static int prior_distance(const GearStats* a, const GearStats* b, int weapon) {
    int d = 3 * (abs(a->str - b->str) + abs(a->rstr - b->rstr) + abs(a->mdmg - b->mdmg));
    for (int i = 0; i < 5; i++) d += abs(a->atk[i] - b->atk[i]);
    if (weapon) return d + 100 * abs(a->speed - b->speed);
    for (int i = 0; i < 5; i++) d += abs(a->def[i] - b->def[i]);
    return d + 2 * abs(a->prayer - b->prayer);
}
static int prior_eligible(int idx, int db_slot, const GearStats* want) {
    const Item* it = &ITEM_DATABASE[idx];
    if (it->slot != db_slot || it->effect_mask != OSRS_ITEM_EFFECT_NONE) return 0;
    if (db_slot != SLOT_WEAPON) return 1;
    return get_item_attack_style((uint8_t)idx) == want->style && it->two_handed == want->two_handed && osrs_spec_cost(idx) == 0 && it->attack_speed > 0;
}
static void prior_triple(int side, int wire_slot, int raw) {
    if (g_prior_out_count == GS_PRIOR_MAX) { fprintf(stderr, "more than %d gear priors in one request\n", GS_PRIOR_MAX); exit(2); }
    g_prior_out[g_prior_out_count][0] = side;
    g_prior_out[g_prior_out_count][1] = wire_slot;
    g_prior_out[g_prior_out_count++][2] = raw;
}
static void prior_note(int tick, int side, int wire_slot, int kit, int raw, int proxy, const char* how) {
    prior_triple(side, wire_slot, raw);
    for (int i = 0; i < g_prior_seen_count; i++) if (g_prior_seen[i][0] == kit * 100000 + raw && g_prior_seen[i][1] == proxy) return;
    if (g_prior_seen_count < 256) { g_prior_seen[g_prior_seen_count][0] = kit * 100000 + raw; g_prior_seen[g_prior_seen_count++][1] = proxy; }
    const GsJavaItemRow* row = prior_row(raw);
    int own = osrs_item_index_from_raw_id(raw);
    fprintf(stderr, "[prior] tick %d slot %d raw %d (%s) -> %d (%s): %s\n", tick, kit, raw, own >= 0 ? ITEM_DATABASE[own].name : row ? row->name : "id unknown to the item table",
        proxy, proxy >= 0 ? ITEM_DATABASE[osrs_item_index_from_raw_id(proxy)].name : "bare", how);
}
/* The raw id to simulate for raw worn in kit slot `kit` (wire slot as in request_gear_priors); -1 is bare. Items the sim can use are returned unchanged:
 * mapped, and for weapons also with an attack style (mapped weapons without one, e.g. godswords, cannot attack in the sim and take the weapon prior). */
static int prior_resolve(int tick, int side, int wire_slot, int kit, int raw, int observed_speed) {
    if (raw == -1) return raw;
    int db_slot = prior_db_slot(kit);
    assert(db_slot >= 0);
    LaItem own = la_item_of(raw);
    if (own.kind == LA_ITEM_UNSUPPORTED && gs_item_priced_from_stats(raw, observed_speed) >= 0) {
        prior_note(tick, side, wire_slot, kit, raw, raw, "priced from its own stats");
        return raw;
    }
    if (own.kind == LA_ITEM_MAPPED && (db_slot != SLOT_WEAPON || get_item_attack_style((uint8_t)own.item) != ATTACK_STYLE_NONE)) return raw;
    const GsJavaItemRow* row = prior_row(raw);
    int known = own.kind == LA_ITEM_MAPPED || (row && row->has_stats && row->slot == kit);
    GearStats want;
    const char* how;
    if (known) {
        want = own.kind == LA_ITEM_MAPPED ? prior_stats_item(own.item) : prior_stats_row(row);
        how = db_slot == SLOT_WEAPON ? "known stats, nearest same-style special-free weapon" : "known stats, nearest effect-free item of the slot";
    }
    else if (db_slot == SLOT_WEAPON) {
        want = prior_stats_item(ITEM_WHIP);
        want.speed = observed_speed > 0 ? observed_speed : want.speed;
        how = "no stats for the id: whip-class melee at the observed attack speed";
    } else { prior_note(tick, side, wire_slot, kit, raw, -1, "no stats for the id: bare slot"); return -1; }
    int best = -1, best_distance = INT_MAX;
    for (int idx = 0; idx < NUM_ITEMS; idx++) {
        if (!prior_eligible(idx, db_slot, &want)) continue;
        GearStats have = prior_stats_item(idx);
        int d = prior_distance(&want, &have, db_slot == SLOT_WEAPON);
        if (d < best_distance) { best_distance = d; best = idx; }
    }
    if (db_slot != SLOT_WEAPON) {
        GearStats bare = {0};
        if (prior_distance(&want, &bare, 0) < best_distance) best = -1;
    }
    assert(best >= 0 || db_slot != SLOT_WEAPON);
    int proxy = best < 0 ? -1 : ITEM_DATABASE[best].item_id;
    prior_note(tick, side, wire_slot, kit, raw, proxy, how);
    return proxy;
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
    int pending_known, pending_expected, pending_read, pending_count[2];
    PendingHit pending[2][MAX_PENDING_HITS];
    PendingHitMetadata pending_metadata[2][MAX_PENDING_HITS];
    int pending_pid_active, pending_pid_expected[2], pending_pid_read[2], pending_pid_count[2][2];
    PendingHit pending_pid[2][2][MAX_PENDING_HITS];
    PendingHitMetadata pending_pid_metadata[2][2][MAX_PENDING_HITS];
    int rules_expected;
    int prior_count, prior[GS_PRIOR_MAX][3]; /* side slot raw, see request_gear_priors */
    int big_hit, since_vw, since_maul, ready_age, last_swing;
    int poison_source[2]; /* 0 missing/unknown, 1 exact, 2 horizon immunity, 3 declared clean prior */
    int poison_seen[2];
    int poison_observation_age[2];
    OsrsPoisonState poison[2];
    int auto_retaliate_seen, auto_retaliate_source, auto_retaliate_varp; /* 0 unknown, 1 observed, 2 OFF prior */
    unsigned assumed; /* GS_ASSUME_* bits: own-side observations the plugin could not complete, replaced by a declared prior (request_declare_priors) */
} Request;

/* Declared priors for own-side observations that are genuinely unobservable on a tick (the response lists them as ` ASSUME n name...`). */
enum { GS_ASSUME_PENDING_EMPTY = 1, GS_ASSUME_OWN_ATTACK_READY = 2, GS_ASSUME_COUNT = 2 };
static const char* const GS_ASSUME_NAME[GS_ASSUME_COUNT] = {"pending_empty", "own_attack_ready"};

static FILE* g_log;

static int request_int(char** p) {
    char* end;
    errno = 0;
    long n = strtol(*p, &end, 10);
    assert(end != *p && !errno && n >= INT_MIN && n <= INT_MAX);
    *p = end;
    return (int)n;
}
static void request_end(const char* p) { assert(strspn(p, " \t\r\n") == strlen(p)); }
static int request_poison_weapon(int raw) { return raw == 27867 || raw == 12904; }
static int request_poison_attack(const Request* q, int side) {
    if (side == 1) return request_poison_weapon(q->his_weapon);
    for (int i = 0; i < TAPE_WORN; i++) if (request_poison_weapon(q->worn[i])) return 1;
    for (int i = 0; i < TAPE_BAG; i++) if (request_poison_weapon(q->bag[i])) return 1;
    for (int i = 0; i < q->rules.n; i++)
        if (request_poison_weapon(q->rules.arg[i])) return 1;
    return 0;
}
/* Declared priors, applied after parsing and before request_unavailable; each is named in the response (ASSUME) and once on stderr:
 *  pending_empty     `P UNKNOWN` (a hit launch, an echo or a proc the plugin could not pin down) is served as `P KNOWN 0`: no hit in flight beyond what the sim launches itself.
 *  own_attack_ready  our attack clock UNKNOWN (no swing of ours seen to time it from) is served as ready since before this tick (OBSERVED -1 -1), the state after any swing a full weapon
 *                    cooldown ago; a cooldown still running is the one thing this prior can get wrong, in the optimistic direction. */
static void request_declare_priors(Request* q) {
    static unsigned noted;
    if (!q->pending_known) {
        q->pending_known = 1;
        q->assumed |= GS_ASSUME_PENDING_EMPTY;
    }
    if (q->attack_history == RISKFIGHT_ATTACK_UNKNOWN) {
        q->attack_history = RISKFIGHT_ATTACK_OBSERVED;
        q->attack_clock_low = q->attack_clock_high = q->attack_timer = -1;
        q->assumed |= GS_ASSUME_OWN_ATTACK_READY;
    }
    for (int i = 0; i < GS_ASSUME_COUNT; i++)
        if ((q->assumed >> i & 1) && !(noted >> i & 1)) {
            noted |= 1u << i;
            fprintf(stderr, "[prior] tick %d declared %s\n", q->tick, GS_ASSUME_NAME[i]);
        }
}

/* Only genuinely missing own-side observations make a request unavailable; opponent gear and the plan never do. */
static const char* request_unavailable(const Request* q) {
    if (!q->pending_known) return "pending_unknown";
    for (int i = 0; i < q->rules.n; i++)
        if (q->rules.command[i].kind == HUMAN_COMMAND_STOP && !q->feasible.pause_ok)
            return q->auto_retaliate_source == 1 ? "auto_retaliate_on" : "auto_retaliate_unknown";
    int poison_attack[2] = {request_poison_attack(q, 0), request_poison_attack(q, 1)};
    for (int side = 0; side < 2; side++) {
        if (!q->poison_source[side] && (q->poison_seen[side] || poison_attack[0] || poison_attack[1]))
            return "poison_state_unknown";
        if (q->poison_source[side] == 2 &&
            q->poison[side].immunity_ticks <= g_la_horizon + q->poison_observation_age[side])
            return "poison_immunity_phase_unknown";
        if (poison_attack[1 - side] &&
            q->poison[side].immunity_ticks <= g_la_horizon + q->poison_observation_age[side])
            return "poison_application_unverified";
    }
    return NULL;
}

/* Replaces everything the sim has no item for with declared priors (opponent: weapon, shield, armor, unobserved composition; own side: worn items
 * by the same nearest-item rule, bag items as inert filler). Afterwards every id is one the sim can wear or hold. The triples `side slot raw` of this
 * request (side 1 opponent: slot 0 weapon, 1 shield, 2 + armor index; 13 ammo; side 0 own: worn kit slot, bag 100 + slot) feed the response PRIOR suffix. */
static void request_gear_priors(Request* q) {
    static int unobserved_noted;
    g_prior_out_count = 0;
    osrs_stat_items_clear(); /* request boundary: no world of an earlier request is alive */
    GsObservedGear* g = &q->belief.gear;
    if (g->source != GS_GEAR_COMPOSITION_OBSERVED) {
        if (g->source == GS_GEAR_UNKNOWN) { g->shield = -1; prior_triple(1, 1, -1); }
        for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++) { g->armor[slot] = -1; prior_triple(1, 2 + slot, -1); }
        if (!(unobserved_noted & (1 << g->source))) {
            unobserved_noted |= 1 << g->source;
            fprintf(stderr, "[prior] tick %d opponent %s not observed: bare\n", q->tick, g->source == GS_GEAR_UNKNOWN ? "shield and armor" : "armor");
        }
    }
    g->weapon = prior_resolve(q->tick, 1, 0, 3, g->weapon, q->his_speed);
    g->shield = prior_resolve(q->tick, 1, 1, 5, g->shield, q->his_speed);
    for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++) g->armor[slot] = prior_resolve(q->tick, 1, 2 + slot, GS_ARMOR_KIT_SLOT[slot], g->armor[slot], q->his_speed);
    if (g->shield != -1 && item_is_two_handed((uint8_t)la_item_of(g->weapon).item)) {
        prior_note(q->tick, 1, 1, 5, g->shield, -1, "shield under a two-handed weapon: bare");
        g->shield = -1;
    }
    uint8_t ammo = la_ammo_prior((uint8_t)la_item_of(g->weapon).item);
    if (ammo != ITEM_NONE && la_item_of(g->weapon).item != ITEM_MAGIC_SHORTBOW_I)
        prior_note(q->tick, 1, 13, 13, ITEM_DATABASE[la_item_of(g->weapon).item].item_id, ITEM_DATABASE[ammo].item_id, "declared ammo prior for the ranged weapon");
    for (int i = 0; i < TAPE_WORN; i++) {
        int c = canonical(q->worn[i]);
        q->worn[i] = c >= 0 && c != INERT_ID ? c : prior_resolve(q->tick, 0, i, i, q->worn[i], q->ours_speed);
    }
    for (int i = 0; i < TAPE_BAG; i++) {
        int c = canonical(q->bag[i]);
        if (c == -2) prior_note(q->tick, 0, 100 + i, 99, q->bag[i], INERT_ID, "own bag item the sim has no model for: inert filler");
        q->bag[i] = c == -2 ? INERT_ID : c;
    }
    q->prior_count = g_prior_out_count;
    memcpy(q->prior, g_prior_out, sizeof(g_prior_out[0]) * (size_t)g_prior_out_count);
}


static void request_alignment_windows(Request* q) {
    int holder = q->belief.pid == GS_PID_LOCAL ? 0 : 1;
    int count = q->pending_pid_active ? q->pending_pid_count[holder][1] : q->pending_count[1];
    const PendingHit* incoming = q->pending_pid_active ? q->pending_pid[holder][1] : q->pending[1];
    la_alignment_windows(incoming, count, &q->belief, q->tick, &q->feasible);
}
static void read_request(FILE* in, Request* q) {
    memset(q, 0, sizeof(*q));
    q->big_hit = WARNING_NONE;
    q->since_vw = q->since_maul = 99;
    q->last_swing = -100;
    char line[4096];
    unsigned seen = 0;
    while (fgets(line, sizeof(line), in)) {
        if (g_log) fputs(line, g_log);
        assert(strchr(line, '\n') != NULL);
        char tag[8];
        int used;
        assert(sscanf(line, "%7s%n", tag, &used) == 1);
        char* p = line + used;
        if (!strcmp(tag, "LA")) { assert(!(seen & 1)); seen |= 1; assert(sscanf(p, "%d %d", &q->id, &q->tick) == 2); }
        else if (!strcmp(tag, "B")) {
            LaBelief* b = &q->belief;
            assert(!(seen & 2)); seen |= 2;
            assert(sscanf(p, "%d %d %d %d %d %d %lf %d %d %d %d %d %d %d %d", &b->hp_low, &b->hp_high, &b->energy_low, &b->energy_high, &b->vengeance, &b->pid, &b->flip,
                &b->timer_seen, &b->timer_first, &b->timer_last, &b->food_ready, &b->seen_since_eat, &b->seen_vw, &b->seen_maul, &b->bar_ratio) == 15);
            assert(b->hp_low >= 0 && b->hp_high >= 1 && b->hp_low <= b->hp_high && b->hp_high <= 121);
            assert(b->energy_low >= 0 && b->energy_low <= b->energy_high && b->energy_high <= 100);
            assert(b->vengeance >= GS_VENGEANCE_UNKNOWN && b->vengeance <= GS_VENGEANCE_MAYBE);
            assert(b->pid >= GS_PID_LOCAL && b->pid <= GS_PID_UNKNOWN && isfinite(b->flip) && b->flip >= 0 && b->flip <= 1);
            assert(b->timer_seen == 0 || b->timer_seen == 1);
            assert(!b->timer_seen || b->timer_first <= b->timer_last);
        } else if (!strcmp(tag, "K")) assert(sscanf(p, "%ld %ld", &q->him, &q->us) == 2);
        else if (!strcmp(tag, "F")) assert(sscanf(p, "%d %d %d %d", &q->feasible.spec_ok, &q->feasible.eat_ok, &q->feasible.veng_ok, &q->feasible.energy) == 4);
        else if (!strcmp(tag, "O")) {
            TapeTick* t = &q->ours;
            assert(!(seen & 4)); seen |= 4;
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
            assert(overhead_code >= 0 && overhead_code < 6);
            static const char* const overheads[] = {"NONE", "SMITE", "PROTECT_FROM_MELEE", "PROTECT_FROM_MISSILES", "PROTECT_FROM_MAGIC", "REDEMPTION"};
            strcpy(t->overhead, overheads[overhead_code]);
        } else if (!strcmp(tag, "SHIELD")) {
            char source[16];
            int consumed;
            assert(sscanf(p, "%15s%n", source, &consumed) == 1);
            p += consumed;
            if (!strcmp(source, "OBSERVED")) {
                assert(sscanf(p, "%d%n", &q->belief.gear.shield, &consumed) == 1);
                assert(q->belief.gear.shield >= -1);
                q->belief.gear.source = GS_GEAR_WEAPON_SHIELD_OBSERVED;
                p += consumed;
            } else assert(!strcmp(source, "UNKNOWN"));
            assert(strspn(p, " \r\n") == strlen(p));
        } else if (!strcmp(tag, "ARMOR")) {
            char source[16];
            int consumed;
            assert(sscanf(p, "%15s%n", source, &consumed) == 1);
            p += consumed;
            if (!strcmp(source, "OBSERVED")) {
                assert(q->belief.gear.source == GS_GEAR_WEAPON_SHIELD_OBSERVED);
                for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++) {
                    assert(sscanf(p, "%d%n", &q->belief.gear.armor[slot], &consumed) == 1);
                    assert(q->belief.gear.armor[slot] >= -1);
                    p += consumed;
                }
                q->belief.gear.source = GS_GEAR_COMPOSITION_OBSERVED;
            } else assert(!strcmp(source, "UNKNOWN"));
            assert(strspn(p, " \r\n") == strlen(p));
        } else if (!strcmp(tag, "W")) {
            assert(!(seen & 8)); seen |= 8;
            for (int i = 0; i < TAPE_WORN; i++) {
                q->worn[i] = request_int(&p);
                assert(q->worn[i] >= -1 && q->worn[i] <= UINT16_MAX);
            }
            request_end(p);
        } else if (!strcmp(tag, "G")) {
            assert(!(seen & 16)); seen |= 16;
            for (int i = 0; i < TAPE_BAG; i++) {
                q->bag[i] = request_int(&p);
                assert(q->bag[i] >= -1 && q->bag[i] <= UINT16_MAX);
            }
            request_end(p);
        }
        else if (!strcmp(tag, "Q")) {
            char since[16];
            assert(sscanf(p, "%15s %d%n", since, &q->unseen_count, &used) == 2);
            assert(q->unseen_count >= 0 && q->unseen_count <= WARNING_UNSEEN);
            q->warned = strcmp(since, "none") ? atoi(since) : WARNING_NONE;
            p += used;
            for (int i = 0; i < q->unseen_count; i++) q->unseen[i] = request_int(&p);
            request_end(p);
        } else if (!strcmp(tag, "H")) assert(sscanf(p, "%d %d %d %d %d", &q->big_hit, &q->since_vw, &q->since_maul, &q->ready_age, &q->last_swing) == 5);
        else if (!strcmp(tag, "RA")) {
            assert(!q->auto_retaliate_seen);
            q->auto_retaliate_seen = 1;
            char source[16];
            assert(sscanf(p, "%15s%n", source, &used) == 1);
            p += used;
            if (!strcmp(source, "OBSERVED")) {
                q->auto_retaliate_source = 1;
                q->auto_retaliate_varp = request_int(&p);
                assert(q->auto_retaliate_varp == 0 || q->auto_retaliate_varp == 1);
                q->feasible.pause_ok = q->auto_retaliate_varp == 1;
            } else if (!strcmp(source, "OFF_PRIOR")) {
                q->auto_retaliate_source = 2;
                q->feasible.pause_ok = 1;
            } else assert(!strcmp(source, "UNKNOWN"));
            request_end(p);
        }
        else if (!strcmp(tag, "PS")) {
            int side = request_int(&p);
            assert(side >= 0 && side < 2 && !q->poison_seen[side]);
            q->poison_seen[side] = 1;
            char status[16];
            assert(sscanf(p, "%15s%n", status, &used) == 1);
            p += used;
            if (!strcmp(status, "KNOWN")) {
                q->poison_source[side] = 1;
                q->poison[side].severity = request_int(&p);
                q->poison[side].ticks_until_pulse = request_int(&p);
                q->poison[side].immunity_ticks = request_int(&p);
                assert(osrs_poison_state_valid(q->poison[side]));
            } else if (!strcmp(status, "IMMUNE")) {
                int varp = request_int(&p);
                assert(varp < 0 && varp != INT_MIN && varp >= -INT_MAX / OSRS_POISON_PERIOD);
                q->poison_source[side] = 2;
                q->poison[side].immunity_ticks = osrs_poison_immunity_lower_bound(varp);
                if (strspn(p, " \t\r\n") != strlen(p)) q->poison_observation_age[side] = request_int(&p);
                assert(q->poison_observation_age[side] >= 0 && q->poison_observation_age[side] <= 1000000);
            } else if (!strcmp(status, "CLEAR_PRIOR")) q->poison_source[side] = 3;
            else assert(!strcmp(status, "UNKNOWN"));
            request_end(p);
        }
        else if (!strcmp(tag, "P") || !strcmp(tag, "R")) {
            int pending = tag[0] == 'P', bit = pending ? 32 : 64;
            assert(!(seen & bit)); seen |= bit;
            char source[16];
            assert(sscanf(p, "%15s%n", source, &used) == 1); p += used;
            int variants = pending && !strcmp(source, "VARIANTS");
            int known = variants || !strcmp(source, "KNOWN");
            assert(known || (pending && !strcmp(source, "UNKNOWN"))); /* R UNKNOWN is gone: Java never invents committed plans. */
            int count = known ? request_int(&p) : 0;
            assert(count >= 0 && count <= (pending ? 2 * MAX_PENDING_HITS : LA_SCRIPT));
            if (variants) {
                q->pending_pid_active = 1;
                q->pending_pid_expected[0] = count;
                q->pending_pid_expected[1] = request_int(&p);
                assert(q->pending_pid_expected[1] >= 0 && q->pending_pid_expected[1] <= 2 * MAX_PENDING_HITS);
                count += q->pending_pid_expected[1];
            }
            request_end(p);
            if (pending) { q->pending_known = known; q->pending_expected = count; }
            else { q->rules_expected = count; q->rules.production = 1; }
        } else if (!strcmp(tag, "D")) {
            assert((seen & 32) && q->pending_known && q->pending_read < q->pending_expected);
            int holder = q->pending_pid_active ? request_int(&p) : 0;
            assert(holder >= 0 && holder < 2);
            if (q->pending_pid_active) assert(q->pending_pid_read[holder]++ < q->pending_pid_expected[holder]);
            int source = request_int(&p);
            assert(source >= 0 && source < 2);
            int* count = q->pending_pid_active ? &q->pending_pid_count[holder][source] : &q->pending_count[source];
            assert(*count < MAX_PENDING_HITS);
            int index = (*count)++;
            PendingHit* h = q->pending_pid_active ? &q->pending_pid[holder][source][index] : &q->pending[source][index];
            PendingHitMetadata* metadata = q->pending_pid_active ? &q->pending_pid_metadata[holder][source][index] : &q->pending_metadata[source][index];
            h->ticks_until_hit = request_int(&p);
            h->kind = request_int(&p);
            h->attack_type = request_int(&p);
            h->is_special = request_int(&p);
            h->hit_success = request_int(&p);
            metadata->damage_mode = request_int(&p);
            h->damage = request_int(&p);
            metadata->damage_min = request_int(&p);
            metadata->damage_max = request_int(&p);
            metadata->accuracy_bp = request_int(&p);
            h->defender_prayer_at_attack = request_int(&p);
            int smite = request_int(&p);
            assert(smite >= -1 && smite <= 1);
            metadata->smite_snapshot = smite + 1;
            request_end(p);
            assert(h->ticks_until_hit >= 0 && h->ticks_until_hit <= 1000);
            assert(h->kind >= OSRS_HIT_DIRECT && h->kind <= OSRS_HIT_POISON);
            assert(h->attack_type >= ATTACK_STYLE_NONE && h->attack_type <= ATTACK_STYLE_MAGIC);
            assert((h->is_special == 0 || h->is_special == 1) && (h->hit_success == 0 || h->hit_success == 1));
            assert(metadata->damage_mode >= 0 && metadata->damage_mode <= 2);
            assert(h->damage >= 0 && h->damage <= 1000 && metadata->damage_min >= 0 && metadata->damage_max >= metadata->damage_min && metadata->damage_max <= 1000);
            assert(metadata->accuracy_bp >= 0 && metadata->accuracy_bp <= 10000 && metadata->smite_snapshot >= 0 && metadata->smite_snapshot <= 2);
            assert(h->defender_prayer_at_attack >= PRAYER_NONE && h->defender_prayer_at_attack <= PRAYER_REDEMPTION);
            assert(h->kind == OSRS_HIT_DIRECT || metadata->damage_mode == 1);
            q->pending_read++;
        } else if (!strcmp(tag, "C")) {
            assert((seen & 64) && q->rules.n < q->rules_expected);
            int due = request_int(&p), kind = request_int(&p), arg = request_int(&p);
            int timing = request_int(&p);
            assert(timing == 0 || timing == 1);
            assert(due > q->tick && kind >= 0 && kind < GS_WIRE_KIND_COUNT);
            int i = q->rules.n++;
            if (timing == 1 && (kind < GS_WIRE_WAIT_READY || kind > GS_WIRE_GROUP) && kind != GS_WIRE_ALIGNED_READY && kind != GS_WIRE_WAIT_SPEC_LAUNCH) {
                for (int j = 0; j < i; j++)
                    if (q->rules.fixed[j] && (q->rules.op[j] < 3 ||
                        (q->rules.op[j] >= 8 && q->rules.op[j] <= 10))) assert(due >= q->rules.due[j]);
            }
            q->rules.due[i] = due;
            q->rules.arg[i] = arg;
            q->rules.fixed[i] = timing;
            switch (kind) {
            case GS_WIRE_CLICK_SLOT: assert(arg >= 0 && arg < OSRS_INVENTORY_SIZE); q->rules.command[i] = click_command(arg); break;
            case GS_WIRE_ATTACK: assert(arg == 0); q->rules.command[i] = attack_command(); break;
            case GS_WIRE_SPEC_ENABLE: assert(arg == 0); q->rules.op[i] = 1; break;
            case GS_WIRE_CAST_VENGEANCE: assert(arg == 0); q->rules.command[i] = vengeance_command(); break;
            case GS_WIRE_SPEC_DISARM: assert(arg == 0); q->rules.op[i] = 2; break;
            case GS_WIRE_UNEQUIP_SLOT: assert(arg >= 0 && arg < TAPE_WORN && WORN_TO_GEAR[arg] >= 0); q->rules.command[i] = (HumanCommand){.kind = HUMAN_COMMAND_UNEQUIP, .gear_slot = WORN_TO_GEAR[arg]}; break;
            case GS_WIRE_WAIT_READY: assert(arg >= 0 && arg <= 1000); q->rules.op[i] = 3; break;
            case GS_WIRE_WAIT_HIT: assert(arg >= -1 && arg <= 1000); q->rules.op[i] = 4; break;
            case GS_WIRE_COND_ENERGY: assert(arg >= 0 && arg <= 100); q->rules.op[i] = 5; break;
            case GS_WIRE_COND_HP: assert(arg >= 0 && arg <= 121); q->rules.op[i] = 6; break;
            case GS_WIRE_GROUP: assert(arg == 0); q->rules.op[i] = 7; break;
            case GS_WIRE_EQUIP_ITEM: {
                LaItem item = la_item_of(canonical(arg));
                assert(item.kind == LA_ITEM_MAPPED);
                q->rules.command[i] = (HumanCommand){.kind = HUMAN_COMMAND_EQUIP_INVENTORY_ITEM,
                    .item_osrs_id = ITEM_DATABASE[item.item].item_id};
                break;
            }
            case GS_WIRE_CLICK_ITEM:
                assert(canonical(arg) != INERT_ID);
                assert(raw_id_to_content(canonical(arg)) != 0);
                q->rules.command[i] = (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK,
                    .item_osrs_id = canonical(arg)};
                break;
            case GS_WIRE_SPEC_TOGGLE: assert(arg == 0); q->rules.command[i] = spec_command(); break;
            case GS_WIRE_STOP_ATTACK:
                assert(arg == 0);
                q->rules.command[i] = (HumanCommand){.kind = HUMAN_COMMAND_STOP};
                break;
            case GS_WIRE_ALIGNED_READY:
                assert(arg > q->tick && timing == 1);
                q->rules.op[i] = 11;
                break;
            case GS_WIRE_WAIT_SPEC_LAUNCH:
                assert(arg == 0);
                q->rules.op[i] = 12;
                break;
            case GS_WIRE_EAT_RECIPE: case GS_WIRE_EQUIP_RECIPE: case GS_WIRE_UNEQUIP_RECIPE: {
                assert(arg == 0);
                int count = request_int(&p);
                assert(count >= (kind == GS_WIRE_EAT_RECIPE ? 1 : 0) && count <= GS_WIRE_MAX_CANDIDATES);
                q->rules.op[i] = kind - GS_WIRE_EAT_RECIPE + 8;
                q->rules.candidate_count[i] = count;
                for (int c = 0; c < count; c++) {
                    int raw = request_int(&p);
                    assert(raw != 0 && raw != INT_MIN && (kind == GS_WIRE_EAT_RECIPE || raw > 0));
                    int mapped = canonical(raw < 0 ? -raw : raw);
                    uint16_t code = raw_id_to_content(mapped);
                    /* The engine emitted these itself: a candidate the sim cannot bind is a protocol violation, not a missing observation. */
                    assert(mapped != INERT_ID && mapped == (raw < 0 ? -raw : raw) && code != 0);
                    assert(kind != GS_WIRE_EAT_RECIPE || osrs_item_content_metadata(code)->consumable_kind != OSRS_CONSUMABLE_NONE);
                    assert(kind == GS_WIRE_EAT_RECIPE || la_item_of(mapped).kind == LA_ITEM_MAPPED);
                    q->rules.candidates[i][c] = raw < 0 ? -mapped : mapped;
                }
                break;
            }
            }
            request_end(p);
        } else if (!strcmp(tag, "END")) {
            request_end(p);
            assert(seen == 127 && q->pending_read == q->pending_expected && q->rules.n == q->rules_expected);
            assert(!q->pending_pid_active || (q->pending_pid_read[0] == q->pending_pid_expected[0] && q->pending_pid_read[1] == q->pending_pid_expected[1]));
            q->belief.gear.weapon = q->his_weapon;
            request_gear_priors(q);
            request_alignment_windows(q);
            return;
        } else assert(0 && "unknown request tag");
    }
    assert(seen == 0 && feof(in));
    exit(0);
}

static Hist g_hist_storage[1];

static void snapshot_of(Request* q, RiskfightSnapshot* snap) {
    assert(request_unavailable(q) == NULL);
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
    assert(la_gear_wearable(q->belief.gear));
    tick.opponent_gear_source = 1;
    tick.opponent_gear_count = TAPE_WORN;
    for (int slot = 0; slot < TAPE_WORN; slot++) tick.opponent_gear[slot] = -1;
    tick.opponent_gear[3] = q->belief.gear.weapon;
    tick.opponent_gear[5] = q->belief.gear.shield;
    for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++)
        tick.opponent_gear[GS_ARMOR_KIT_SLOT[slot]] = q->belief.gear.armor[slot];
    for (int i = 0; i < TAPE_WORN; i++) tick.worn[i] = q->worn[i];
    for (int i = 0; i < TAPE_BAG; i++) tick.bag[i] = q->bag[i];
    tape.ticks = &tick;
    tape.lo = tape.hi = q->tick;
    g_hist_storage[0] = hist_start();
    tape.hist_series = g_hist_storage;
    g_tape = &tape;
    uint32_t rng = osrs_lowbias32((uint32_t)q->tick) | 1;
    assert(tape_snapshot_construct(&tape, q->tick, &DEFAULT_PRIORS, &rng, snap));
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
    for (int side = 0; side < 2; side++) {
        snap->side[side].poison = q->poison[side];
        if (q->poison_source[side] == 2)
            snap->side[side].poison.immunity_ticks -= q->poison_observation_age[side];
        snap->side[side].poison_source = q->poison_source[side]
            ? (OsrsPoisonSource)q->poison_source[side] : OSRS_POISON_CLEAR_PRIOR;
    }
    snap->pending_known = q->pending_known;
    snap->pending_pid_active = q->pending_pid_active;
    memcpy(snap->pending_pid_count, q->pending_pid_count, sizeof(snap->pending_pid_count));
    memcpy(snap->pending_pid, q->pending_pid, sizeof(snap->pending_pid));
    memcpy(snap->pending_pid_metadata, q->pending_pid_metadata, sizeof(snap->pending_pid_metadata));
    memcpy(snap->pending_count, q->pending_count, sizeof(snap->pending_count));
    memcpy(snap->pending, q->pending, sizeof(snap->pending));
    memcpy(snap->pending_metadata, q->pending_metadata, sizeof(snap->pending_metadata));
    g_tape = NULL;
}

static Sim g_base;
static LaDecision g_decision;

/* One decision on the request already read: snapshot, load, macro scripts around the committed remainder, common-world rollouts. Returns micros. */
static double server_decide(Request* q) {
    double started = la_seconds();
    RiskfightSnapshot snap;
    snapshot_of(q, &snap);
    riskfight_load_state(&g_base.state, &g_base.context, &snap);
    Hist hist = hist_start();
    hist.warning.in_reach_since = q->warned;
    hist.warning.unseen_count = q->unseen_count;
    memcpy(hist.warning.unseen, q->unseen, sizeof(q->unseen));
    hist.since_eat = q->belief.seen_since_eat;
    hist.big_hit_tick = q->big_hit;
    hist.since_vw_spec = q->since_vw;
    hist.since_maul_spec = q->since_maul;
    hist.ready_age = q->ready_age;
    hist.last_swing_tick = q->last_swing;
    la_decide(&g_base.state, &hist, &q->belief, q->tick, (GsStakes){.us = q->us, .him = q->him}, &q->feasible, &q->rules, (uint32_t)q->id, &g_decision);
    return (la_seconds() - started) * 1e6;
}

/* One response line: scores of every macro, the chosen macro's plan as A records, `PRIOR k side slot raw ...` when gear priors were used, and `ASSUME n name...`
 * when an own-side observation was replaced by a declared prior (request_declare_priors). */
static void server_respond(FILE* out, const Request* q, double micros) {
    const LaDecision* d = &g_decision;
    fprintf(out, "R %d %.0f %d", q->id, micros, (int)d->best);
    for (int m = 0; m < LA_MACROS; m++)
        fprintf(out, " %d %.4f %.4f %.4f %d", d->feasible[m] && d->score[m].worlds > 0, d->score[m].worlds ? d->score[m].kill / d->score[m].worlds : 0,
            d->score[m].worlds ? d->score[m].death / d->score[m].worlds : 0, d->score[m].worlds ? d->score[m].value / d->score[m].worlds : 0, d->score[m].worlds);
    const LaScript* chosen = &d->scripts[d->best];
    fprintf(out, " A %d", chosen->n);
    for (int i = 0; i < chosen->n; i++) la_script_emit_record(out, chosen, i);
    if (q->prior_count) {
        fprintf(out, " PRIOR %d", q->prior_count);
        for (int i = 0; i < q->prior_count; i++) fprintf(out, " %d %d %d", q->prior[i][0], q->prior[i][1], q->prior[i][2]);
    }
    if (q->assumed) {
        fprintf(out, " ASSUME %d", __builtin_popcount(q->assumed));
        for (int i = 0; i < GS_ASSUME_COUNT; i++)
            if (q->assumed >> i & 1) fprintf(out, " %s", GS_ASSUME_NAME[i]);
    }
    fprintf(out, "\n");
}

/* The own-side observations a request must carry; everything else the sim has priors for. Returns the UNAVAILABLE reason or NULL. */
static const char* server_unavailable(const Request* q) {
    const char* reason = request_unavailable(q);
    if (reason) return reason;
    if (q->attack_history == RISKFIGHT_ATTACK_UNKNOWN) return "local_attack_history_unknown";
    if (q->attack_clock_low != q->attack_clock_high) return "local_attack_history_interval";
    return NULL;
}

#include "gs_lookahead_warm.h"

/* Runs the synthetic stream through the whole decision path so page faults, lazy tables and caches are paid before READY. */
static double server_warm(void) {
    enum { WARM = 24, COLD = 2 };
    double worst = 0;
    for (int i = 0; i < WARM; i++) {
        const char* text = WARM_REQUESTS[i % WARM_REQUEST_COUNT];
        FILE* in = fmemopen((void*)text, strlen(text), "r");
        static Request q;
        read_request(in, &q);
        fclose(in);
        q.id = i;
        const char* unavailable = server_unavailable(&q);
        if (unavailable) { fprintf(stderr, "warm-up request %d is unavailable: %s\n", i % WARM_REQUEST_COUNT, unavailable); exit(2); }
        double micros = server_decide(&q);
        if (i >= COLD && micros > worst) worst = micros;
    }
    return worst;
}

int main(int argc, char** argv) {
    assert(argc >= 3 && argc <= 6);
    g_ablation.hp = HP_UNIFORM;
    g_ablation.escape_model = 1;
    g_ablation.his_defence = 99;
    g_ablation.panic = 0.8;
    g_ablation.panic_hp = 25;
    static OppFit fit;
    opp_load(argv[1], argv[2], &fit, &g_eat_model);
    /* Defaults measured for this host class (bench_lookahead_server): 512 common worlds is the knee of decision regret, 8 threads keep the round trip ~2 ms. */
    g_la_worlds = SERVER_WORLDS;
    g_la_threads = SERVER_THREADS;
    if (argc > 3) { char* arg = argv[3]; g_la_worlds = request_int(&arg); request_end(arg); }
    if (argc > 4) { char* arg = argv[4]; g_la_horizon = request_int(&arg); request_end(arg); }
    if (argc > 5) { char* arg = argv[5]; g_la_threads = request_int(&arg); request_end(arg); }
    assert(g_la_worlds > 0 && g_la_horizon > 0 && g_la_horizon <= 32); /* Outcome.his_hit_mask is uint32_t. */
    assert(g_la_threads >= 1 && g_la_threads <= LA_MAX_THREADS);
    assert_fitted(&fit, "server");
    static OppModel model;
    model = (OppModel){MODEL_FIT, &fit};
    g_la_model = &model;
    g_la_cache = 0;
    observer_register_items();
    sim_init(&g_base);
    double worst = server_warm();
    printf("READY %.0f", worst);
    for (int m = 0; m < LA_MACROS; m++) printf("%c%s", m ? ';' : ' ', LA_NAME[m]);
    printf("\n");
    fflush(stdout);
    if (getenv("GS_SERVER_LOG")) g_log = fopen(getenv("GS_SERVER_LOG"), "w");
    static Request q;
    for (;;) {
        read_request(stdin, &q);
        request_declare_priors(&q);
        const char* unavailable = server_unavailable(&q);
        if (unavailable) {
            printf("UNAVAILABLE %d %s\n", q.id, unavailable);
            fflush(stdout);
            if (g_log) fflush(g_log);
            continue;
        }
        double micros = server_decide(&q);
        server_respond(stdout, &q, micros);
        fflush(stdout);
        if (g_log) fflush(g_log);
    }
}
