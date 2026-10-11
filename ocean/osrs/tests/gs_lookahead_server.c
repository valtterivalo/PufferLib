/* The lookahead as a process: one request per decision on stdin, one scored line per request on stdout.
 *
 *   cc -std=c11 -O2 -I. -o gs_lookahead_server ocean/osrs/tests/gs_lookahead_server.c -lm
 *   gs_lookahead_server MODEL.fit EAT_MIXTURE.fit [WORLDS HORIZON]
 *
 * Request lines end in END. P KNOWN n followed by n D records, or P UNKNOWN.
 * P VARIANTS localCount opponentCount retains both concrete-holder queues; each D
 * adds a holder (0 LOCAL, 1 OPPONENT) before source. Fixed D records omit holder.
 * D [holder] source delay kind style special success mode damage min max accuracyBp prayer smite.
 * R KNOWN n followed by n C records, or R UNKNOWN. C due kind arg timing:
 * kinds 14/15/16 add count and candidates for eat/equip/unequip live recipes;
 * negative eat candidates denote a dose family, positive candidates an exact raw ID.
 * timing 0 samples an unsent lower bound; 1 preserves a known landing (guards cost no click).
 * Empty queues/plans require explicit KNOWN 0. Response: R id micros best, then each macro's feasible kill death value worlds. */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "riskfight_rollout.h"
#include <errno.h>
#include <limits.h>
/* Requests are an external boundary; these checks must run under NDEBUG too. */
#undef assert
#define assert(condition) do { if (!(condition)) { fprintf(stderr, "invalid lookahead request: %s\n", #condition); exit(2); } } while (0)

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
    int pending_known, pending_expected, pending_read, pending_count[2];
    PendingHit pending[2][MAX_PENDING_HITS];
    PendingHitMetadata pending_metadata[2][MAX_PENDING_HITS];
    int pending_pid_active, pending_pid_expected[2], pending_pid_read[2], pending_pid_count[2][2];
    PendingHit pending_pid[2][2][MAX_PENDING_HITS];
    PendingHitMetadata pending_pid_metadata[2][2][MAX_PENDING_HITS];
    int rules_known, rules_expected, rules_unsupported;
    int big_hit, since_vw, since_maul, ready_age, last_swing;
} Request;

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
static const char* request_unavailable(const Request* q) {
    if (!q->pending_known) return "pending_unknown";
    if (!q->rules_known || q->rules_unsupported) return "production_plan_unknown";
    for (int i = 0; i < TAPE_BAG; i++) if (q->bag[i] == 24589) return "local_food_unsupported";
    for (int i = 0; i < q->rules.n; i++) if (q->rules.op[i] == 8) {
        int matches = 0;
        for (int slot = 0; slot < TAPE_BAG; slot++) {
            int held = canonical(q->bag[slot]), matched = 0;
            for (int c = 0; c < q->rules.candidate_count[i]; c++) {
                int candidate = q->rules.candidates[i][c];
                if (candidate > 0) matched |= held == candidate;
                else matched |= osrs_item_content_metadata(raw_id_to_content(held))->consumable_kind ==
                    osrs_item_content_metadata(raw_id_to_content(-candidate))->consumable_kind;
            }
            matches += matched;
        }
        if (matches > 1) return "production_plan_unknown"; /* Cursor-path choice is not in the snapshot contract. */
    }
    return NULL;
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
            for (int i = 0; i < TAPE_WORN; i++) q->worn[i] = request_int(&p);
            request_end(p);
        } else if (!strcmp(tag, "G")) {
            assert(!(seen & 16)); seen |= 16;
            for (int i = 0; i < TAPE_BAG; i++) q->bag[i] = request_int(&p);
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
        else if (!strcmp(tag, "P") || !strcmp(tag, "R")) {
            int pending = tag[0] == 'P', bit = pending ? 32 : 64;
            assert(!(seen & bit)); seen |= bit;
            char source[16];
            assert(sscanf(p, "%15s%n", source, &used) == 1); p += used;
            int variants = pending && !strcmp(source, "VARIANTS");
            int known = variants || !strcmp(source, "KNOWN");
            assert(known || !strcmp(source, "UNKNOWN"));
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
            else { q->rules_known = known; q->rules_expected = count; q->rules.production = 1; }
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
            assert(h->kind >= OSRS_HIT_DIRECT && h->kind <= OSRS_HIT_VENGEANCE);
            assert(h->attack_type >= ATTACK_STYLE_NONE && h->attack_type <= ATTACK_STYLE_MAGIC);
            assert((h->is_special == 0 || h->is_special == 1) && (h->hit_success == 0 || h->hit_success == 1));
            assert(metadata->damage_mode >= 0 && metadata->damage_mode <= 2);
            assert(h->damage >= 0 && h->damage <= 1000 && metadata->damage_min >= 0 && metadata->damage_max >= metadata->damage_min && metadata->damage_max <= 1000);
            assert(metadata->accuracy_bp >= 0 && metadata->accuracy_bp <= 10000 && metadata->smite_snapshot >= 0 && metadata->smite_snapshot <= 2);
            assert(h->defender_prayer_at_attack >= PRAYER_NONE && h->defender_prayer_at_attack <= PRAYER_REDEMPTION);
            assert(h->kind == OSRS_HIT_DIRECT || metadata->damage_mode == 1);
            q->pending_read++;
        } else if (!strcmp(tag, "C")) {
            assert((seen & 64) && q->rules_known && q->rules.n < q->rules_expected);
            int due = request_int(&p), kind = request_int(&p), arg = request_int(&p);
            int timing = request_int(&p);
            assert(timing == 0 || timing == 1);
            assert(due > q->tick && kind >= 0 && kind <= 16);
            int i = q->rules.n++;
            if (timing == 1 && (kind < 6 || kind > 10)) {
                for (int j = 0; j < i; j++)
                    if (q->rules.fixed[j] && (q->rules.op[j] < 3 || q->rules.op[j] >= 8)) assert(due >= q->rules.due[j]);
            }
            q->rules.due[i] = due;
            q->rules.arg[i] = arg;
            q->rules.fixed[i] = timing;
            switch (kind) {
            case 0: assert(arg >= 0 && arg < OSRS_INVENTORY_SIZE); q->rules.command[i] = click_command(arg); break;
            case 1: assert(arg == 0); q->rules.command[i] = attack_command(); break;
            case 2: assert(arg == 0); q->rules.op[i] = 1; break;
            case 3: assert(arg == 0); q->rules.command[i] = vengeance_command(); break;
            case 4: assert(arg == 0); q->rules.op[i] = 2; break;
            case 5: assert(arg >= 0 && arg < TAPE_WORN && WORN_TO_GEAR[arg] >= 0); q->rules.command[i] = (HumanCommand){.kind = HUMAN_COMMAND_UNEQUIP, .gear_slot = WORN_TO_GEAR[arg]}; break;
            case 6: assert(arg >= 0 && arg <= 1000); q->rules.op[i] = 3; break;
            case 7: assert(arg >= -1 && arg <= 1000); q->rules.op[i] = 4; break;
            case 8: assert(arg >= 0 && arg <= 100); q->rules.op[i] = 5; break;
            case 9: assert(arg >= 0 && arg <= 121); q->rules.op[i] = 6; break;
            case 10: assert(arg == 0); q->rules.op[i] = 7; break;
            case 11: {
                LaItem item = la_item_of(canonical(arg));
                assert(item.kind == LA_ITEM_MAPPED);
                q->rules.command[i] = (HumanCommand){.kind = HUMAN_COMMAND_EQUIP_INVENTORY_ITEM,
                    .item_osrs_id = ITEM_DATABASE[item.item].item_id};
                break;
            }
            case 12:
                assert(canonical(arg) != INERT_ID);
                assert(raw_id_to_content(canonical(arg)) != 0);
                q->rules.command[i] = (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK,
                    .item_osrs_id = canonical(arg)};
                break;
            case 13: assert(arg == 0); q->rules.command[i] = spec_command(); break;
            case 14: case 15: case 16: {
                assert(arg == 0);
                int count = request_int(&p);
                assert(count >= (kind == 14 ? 1 : 0) && count <= OSRS_INVENTORY_SIZE);
                q->rules.op[i] = kind - 6;
                q->rules.candidate_count[i] = count;
                for (int c = 0; c < count; c++) {
                    int raw = request_int(&p);
                    assert(raw != 0 && raw != INT_MIN && (kind == 14 || raw > 0));
                    int mapped = canonical(raw < 0 ? -raw : raw);
                    uint16_t code = raw_id_to_content(mapped);
                    if (mapped == INERT_ID || mapped != (raw < 0 ? -raw : raw) || code == 0 ||
                            (kind == 14 && osrs_item_content_metadata(code)->consumable_kind == OSRS_CONSUMABLE_NONE) ||
                            (kind != 14 && la_item_of(mapped).kind != LA_ITEM_MAPPED)) q->rules_unsupported = 1;
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
            return;
        } else assert(0 && "unknown request tag");
    }
    assert(seen == 0 && feof(in));
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
    (void)tape_snapshot(&tape, q->tick, &DEFAULT_PRIORS, &rng, snap); /* Explicit request queue replaces the tape source. */
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

int main(int argc, char** argv) {
    assert(argc >= 3 && argc <= 5);
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
    if (argc > 3) { char* arg = argv[3]; g_la_worlds = request_int(&arg); request_end(arg); }
    if (argc > 4) { char* arg = argv[4]; g_la_horizon = request_int(&arg); request_end(arg); }
    assert(g_la_worlds > 0 && g_la_horizon > 0 && g_la_horizon <= 32); /* Outcome.his_hit_mask is uint32_t. */
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
        const char* unavailable = request_unavailable(&q);
        if (unavailable) {
            printf("UNAVAILABLE %d %s\n", q.id, unavailable);
            fflush(stdout);
            continue;
        }
        if (q.attack_history == RISKFIGHT_ATTACK_UNKNOWN || q.attack_clock_low != q.attack_clock_high) {
            printf("UNAVAILABLE %d %s\n", q.id, q.attack_history == RISKFIGHT_ATTACK_UNKNOWN
                ? "local_attack_history_unknown" : "local_attack_history_interval");
            fflush(stdout);
            if (g_log) fflush(g_log);
            continue;
        }
        LaGearIssue issue = la_gear_status(q.belief.gear);
        LaGearStatus gear_status = issue.status;
        if (gear_status != LA_GEAR_SUPPORTED) {
            printf("UNAVAILABLE %d %s", q.id, LA_GEAR_STATUS_NAME[gear_status]);
            if (gear_status == LA_GEAR_ARMOR_UNSUPPORTED) printf(" %s %d", GS_ARMOR_SLOT_NAME[issue.slot], issue.raw);
            else if (gear_status != LA_GEAR_UNKNOWN && gear_status != LA_GEAR_ARMOR_UNKNOWN) printf(" %d", issue.raw);
            printf("\n");
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
