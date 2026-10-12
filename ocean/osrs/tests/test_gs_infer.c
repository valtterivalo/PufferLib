/* Differential test of the C inference port (gs_infer.h) against the Java plugin.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/test_gs_infer ocean/osrs/tests/test_gs_infer.c -lm
 *   /tmp/test_gs_infer WIRE.tap
 *
 * Regenerate from current isolated Light sources (use fresh output paths; taps append):
 *   ./gradlew :gear-swapper:fightReplay -Precording=RECORDING -PautoEatRisk=3 \
 *       -Ptap=/tmp/current-eat.tap -Pspectap=/tmp/current-spec.tap -Pwiretap=/tmp/current-wire.tap
 * Then from the native root:
 *   cc -std=c11 -O2 -I. -o /tmp/test_gs_auto_eat ocean/osrs/tests/test_gs_auto_eat.c -lm
 *   cc -std=c11 -O2 -I. -o /tmp/test_gs_auto_spec ocean/osrs/tests/test_gs_auto_spec.c -lm
 *   /tmp/test_gs_auto_eat /tmp/current-eat.tap
 *   /tmp/test_gs_auto_spec /tmp/current-spec.tap
 *   /tmp/test_gs_infer /tmp/current-wire.tap
 *
 * Keep existing fixtures unchanged: old Java policy taps must expose drift, not be re-pinned to unsupported native inputs.
 * The source wire contains producer inputs, separate from counterfactual consumer tapes.
 * Pure rule equality does not certify actuator parity: Java hand-derived plan availability,
 * launch/attack-click return latency, partial spend/launch confirmation and accepted reservations
 * are not reconstructed by this inference adapter. GsMeal.resume is supplied by its owner;
 * follow-up hypothetical meals still lack measured future return geometry. INF is diagnostic,
 * while serialized causal inputs, typed pending queues and target belief are gated. */
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include "../gearswapper/gs_wire.h"
#include "../gearswapper/gs_plan.h"
#include "gs_target_oracle.h"

enum { LINE = 1 << 16, TOKENS = GS_WIRE_TOKENS };

static const char* const STYLE_NAMES[3] = {"MELEE", "RANGED", "MAGIC"};
static const char* const PID_NAMES[3] = {"LOCAL", "OPPONENT", "UNKNOWN"};
static const char* const VENGEANCE_NAMES[4] = {"UNKNOWN", "UP", "DOWN", "MAYBE"};

static int cmp_int(const void* a, const void* b) { return *(const int*)a - *(const int*)b; }



static void format_attack(char* out, const char* tag, const GsAttack* a, int maul) {
    sprintf(out + strlen(out), "%s %s %d %d %d %d %.17g %s %d\n", tag, STYLE_NAMES[a->style], a->least, a->most, a->cost, a->interval, a->accuracy,
        maul ? "MAUL" : a->godsword ? "GODSWORD_SPEC" : "SPEC", a->arrows);
}

static void format_situation(const GsEatSituation* s, char* out) {
    out[0] = 0;
    sprintf(out + strlen(out), "S %d %d %s %d %s %d %d %d %.17g %.17g %s %.17g %.17g %.17g %d %s %s %d\n", s->tick, s->hitpoints, PID_NAMES[s->pid], s->distance,
        STYLE_NAMES[s->local_style], s->local_most, s->local_normal, s->local_interval, s->local_opening_accuracy, s->local_accuracy,
        s->offence == GS_ENGAGED ? "ENGAGED" : "IDLE", s->tolerance, s->pid_flip, s->stakes, s->floor, VENGEANCE_NAMES[s->vengeance], s->recoil_seen ? "SEEN" : "UNSEEN", s->energy_high);
    sprintf(out + strlen(out), "DR %d %d %.17g %.17g\n", s->drained_normal, s->drained_most, s->drained_accuracy, s->drained_opening_accuracy);
    if (s->opponent_timer.seen) sprintf(out + strlen(out), "OT 1 %d\n", s->opponent_timer.next);
    else sprintf(out + strlen(out), "OT 0 0\n");
    if (s->local_timer.seen) sprintf(out + strlen(out), "LT 1 %d %d\n", s->local_timer.next, s->local_timer.earliest);
    else sprintf(out + strlen(out), "LT 0 0 %d\n", s->tick + 1);
    sprintf(out + strlen(out), "OH %d %d %d\n", s->opponent_hp.low, s->opponent_hp.high, s->opponent_hp.max);
    for (int i = 0; i < s->attack_count; i++) format_attack(out, "OA", &s->attacks[i], 0);
    if (s->has_maul) format_attack(out, "GM", &s->maul, 1);
    for (int i = 0; i < s->hidden_count; i++) {
        sprintf(out + strlen(out), "HID %d ", s->hidden[i].is_maul);
        char one[256] = "";
        format_attack(one, "", &s->hidden[i].attack, s->hidden[i].is_maul);
        one[strlen(one) - 1] = 0;
        sprintf(out + strlen(out), "%s\n", one + 1);
    }
    for (int i = 0; i < s->swing_count; i++) {
        const GsSwing* swing = &s->swings[i];
        sprintf(out + strlen(out), "OSW %d %d %d %.17g %d\n", swing->lands, swing->least, swing->most, swing->accuracy, swing->arrows);
    }
    if (s->has_local_swing) sprintf(out + strlen(out), "LSW %d %d %d %.17g %d\n", s->local_swing.lands, s->local_swing.least, s->local_swing.most, s->local_swing.accuracy, s->local_swing.arrows);
    if (s->has_owed) sprintf(out + strlen(out), "OE %d %d\n", s->owed.tick, s->owed.most);
    if (s->warning.since == GS_NONE) strcat(out, "W none");
    else sprintf(out + strlen(out), "W %d", s->warning.since);
    int sorted[64];
    memcpy(sorted, s->warning.unseen, sizeof(int) * (size_t)s->warning.unseen_count);
    qsort(sorted, (size_t)s->warning.unseen_count, sizeof(int), cmp_int);
    for (int i = 0; i < s->warning.unseen_count; i++) sprintf(out + strlen(out), " %d", sorted[i]);
    strcat(out, "\n");
}

static void format_spec_inputs(const GsSpecSituation* s, const GsUnderway* underway, char* out) {
    sprintf(out, "SS %d %d %d %d %s %s %.17g %d %d\n", s->hitpoints.low, s->hitpoints.high, s->hitpoints.max, s->hard_food_ready, VENGEANCE_NAMES[s->vengeance],
        PID_NAMES[s->pid], s->pid_flip, s->recoil_charges, s->tick);
    if (underway->certain) sprintf(out + strlen(out), "UW %d %d\n", underway->lands, underway->damage);
    else strcat(out, "UW\n");
    if (s->has_incoming) {
        sprintf(out + strlen(out), "IN %d %d %d %.17g ", s->incoming.lands, s->incoming.least, s->incoming.most, s->incoming.accuracy);
        if (s->incoming.attack == GS_NONE) strcat(out, "none\n");
        else sprintf(out + strlen(out), "%d\n", s->incoming.attack);
    }
    if (s->warning.since == GS_NONE) strcat(out, "W none");
    else sprintf(out + strlen(out), "W %d", s->warning.since);
    int sorted[64];
    memcpy(sorted, s->warning.unseen, sizeof(int) * (size_t)s->warning.unseen_count);
    qsort(sorted, (size_t)s->warning.unseen_count, sizeof(int), cmp_int);
    for (int i = 0; i < s->warning.unseen_count; i++) sprintf(out + strlen(out), " %d", sorted[i]);
    strcat(out, "\n");
}

/* The Java SS, UW, IN and W lines reduced to what the inference decides: the rebound, energy and plans are ours. */
static void canonical_java_spec(const char* record, char* out) {
    char copy[LINE * 4];
    strcpy(copy, record);
    out[0] = 0;
    char* save;
    for (char* line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char one[LINE];
        strcpy(one, line);
        char* t[TOKENS];
        int n = gs_wire_split(one, t);
        if (!strcmp(t[0], "SS")) {
            if (n != 13) abort();
            sprintf(out + strlen(out), "SS %s %s %s %s %s %s %s %s %s\n", t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[10], t[11]);
        }
        else if (!strcmp(t[0], "UW")) {
            if (n == 1) strcat(out, "UW\n");
            else {
                if (n < 7 || strcmp(t[1], "H") || strcmp(t[4], "OURS")) abort();
                int length = atoi(t[3]), certain = 1;
                if (length < 1 || n < 7 + length) abort();
                certain &= n == 7 + length && atoi(t[5]) == imax(1, length - 1)
                    && strtod(t[6], NULL) == (length > 1 ? 1 : 0);
                for (int damage = 0; damage < length; damage++)
                    certain &= strtod(t[7 + damage], NULL) == (damage == length - 1 ? 1 : 0);
                if (certain) sprintf(out + strlen(out), "UW %s %d\n", t[2], length - 1);
                else { strcat(out, line); strcat(out, "\n"); }
            }
        }
        else if (!strcmp(t[0], "IN") || !strcmp(t[0], "W")) strcat(out, line), strcat(out, "\n");
    }
}

typedef struct { long plan_records, plan_mismatches, extra_plans, unavailable_plan_damage, unavailable_spec_policy, unavailable_situation_policy, extended_pending_unavailable, extended_recoil_unavailable, extended_local_unavailable; long ticks, situations, situation_mismatches, inference_lines, inference_mismatches, shown, spec_records, spec_mismatches, pending_records, target_records, target_mismatches; } Stats;

static const char* const PLAN_NAMES[GS_CHOICE_COUNT] = {"HOLD", "VOIDWAKER", "MAUL_VOIDWAKER", "MAUL", "VOIDWAKER_BURST"};

/* The Java plans' damage distributions against ours, bit for bit. The ticks they land on come from the press model and are not compared. */
static void compare_plans(char* java_plans, GsInfer* inf, int tick, Stats* stats) {
    Arena arena = arena_make((size_t)1 << 22);
    GsSpecPlan mine[4];
    int own_maul, own_count, own_shield;
    gs_own_of(&inf->c, &own_maul, &own_count, &own_shield);
    GsOwn own = {own_maul, own_count, own_shield};
    int count = gs_spec_plans(&arena, inf, tick, 0, &own, mine);
    int found[4] = {0};
    char* save;
    unsigned source_choices = 0;
    for (char* l = strtok_r(java_plans, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char one[LINE * 2];
        strcpy(one, l);
        char* t[GS_WIRE_TOKENS];
        int n = gs_wire_split(one, t);
        if (n < 3) abort();
        stats->plan_records++;
        int source_choice = -1;
        for (int i = 0; i < GS_CHOICE_COUNT; i++) if (!strcmp(PLAN_NAMES[i], t[1])) source_choice = i;
        if (source_choice < 0 || (source_choices & (1u << source_choice))) abort();
        source_choices |= 1u << source_choice;
        int match = -1;
        for (int i = 0; i < count; i++) if (!strcmp(PLAN_NAMES[mine[i].choice], t[1])) match = i;
        if (match < 0) { stats->unavailable_plan_damage++; continue; }
        int bad = 0, unavailable = 0;
        int at = 3;
        for (int h = 0; h < mine[match].hit_count && !bad; h++) {
            if (at + 6 > n || strcmp(t[at], "H")) { bad = 1; break; }
            int len = atoi(t[at + 2]);
            if (len < 1 || at + 6 + len > n) abort();
            if ((atoi(t[at + 1]) > inf->opponent.immune_through)
                    != (mine[match].hits[h].lands > inf->opponent.immune_through)) {
                stats->unavailable_plan_damage++;
                unavailable = 1;
                break;
            }
            if (len != mine[match].hits[h].damages || strcmp(t[at + 3], mine[match].hits[h].origin == GS_ORIGIN_OURS ? "OURS" : "REFLECTED")) { bad = 1; break; }
            if (atoi(t[at + 4]) != mine[match].hits[h].first_positive_roll
                    || strtod(t[at + 5], NULL) != mine[match].hits[h].positive_each) { bad = 1; break; }
            for (int d = 0; d < len; d++) { double j = strtod(t[at + 6 + d], NULL); if (memcmp(&j, &mine[match].hits[h].damage[d], sizeof(double))) { bad = 1; break; } }
            at += 6 + len;
        }
        if (!bad && !unavailable && at != n) bad = 1;
        found[match] = 1;
        if (bad) { stats->plan_mismatches++; if (stats->shown < 12) { fprintf(stderr, "PLAN tick %d %s mismatch\n", tick, t[1]); stats->shown++; } }
    }
    for (int i = 0; i < count; i++) stats->extra_plans += !found[i];
    free(arena.base);
}

static int numeric(const char* t, double* v) {
    char* end;
    *v = strtod(t, &end);
    return *end == 0 && end != t;
}

static int same_token(const char* a, const char* b) {
    double x, y;
    if (numeric(a, &x) && numeric(b, &y)) return memcmp(&x, &y, sizeof(double)) == 0;
    return !strcmp(a, b);
}

static int same_line(const char* a, const char* b) {
    char x[LINE], y[LINE];
    strcpy(x, a); strcpy(y, b);
    char *tx[TOKENS], *ty[TOKENS];
    int nx = gs_wire_split(x, tx), ny = gs_wire_split(y, ty);
    if (nx != ny) return 0;
    for (int i = 0; i < nx; i++) if (!same_token(tx[i], ty[i])) return 0;
    return 1;
}


static int behavior_lines(char* record, char** lines) {
    int count = 0;
    char* save;
    for (char* line = strtok_r(record, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(line, "T ", 2) || !strncmp(line, "TH ", 3) || !strncmp(line, "TB ", 3)) continue;
        if (count == 128) abort();
        lines[count++] = line;
    }
    return count;
}

static int compare_record(const char* mine, const char* theirs, int tick, Stats* stats, const char* kind) {
    char a[LINE * 4], b[LINE * 4];
    strcpy(a, mine); strcpy(b, theirs);
    char* mine_lines[128], *their_lines[128];
    int matched[128] = {0};
    int own_count = behavior_lines(a, mine_lines), their_count = behavior_lines(b, their_lines);
    int bad = own_count != their_count;
    for (int i = 0; i < own_count; i++) {
        int found = -1;
        for (int j = 0; j < their_count; j++) if (!matched[j] && same_line(mine_lines[i], their_lines[j])) { found = j; break; }
        if (found >= 0) matched[found] = 1;
        else {
            if (stats->shown < 12) {
                fprintf(stderr, "%s tick %d unmatched input: %s\n", kind, tick, mine_lines[i]);
                if (!strncmp(mine_lines[i], "SS ", 3)) {
                    for (int j = 0; j < their_count; j++) if (!strncmp(their_lines[j], "SS ", 3))
                        fprintf(stderr, "  source SS (hp.low hp.high hp.max hardFoodReady vengeance pid pidFlip recoilCharges tick): %s\n", their_lines[j]);
                }
            }
            stats->shown++;
            bad = 1;
        }
    }
    return bad;
}

static int compare_target_record(const GsTarget* mine, const char* record, int tick, Stats* stats) {
    char copy[LINE * 4];
    strcpy(copy, record);
    GsTarget target = {0};
    char* save;
    for (char* line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char tag[4];
        int length = 0;
        while (line[length] && line[length] != ' ') length++;
        if (length > 2) continue;
        memcpy(tag, line, (size_t)length); tag[length] = 0;
        gs_target_read_line(&target, tag, line + length);
    }
    gs_target_validate(&target);
    int bad = mine->has_history != target.has_history || mine->has_belief != target.has_belief || mine->base_hp != target.base_hp;
    double maximum_error = 0;
    for (int kind = 0; kind < 2; kind++) {
        const GsTargetBranch* own = kind ? mine->belief : mine->history;
        const GsTargetBranch* theirs = kind ? target.belief : target.history;
        int own_count = kind ? mine->belief_count : mine->history_count;
        int their_count = kind ? target.belief_count : target.history_count;
        bad |= own_count != their_count;
        for (int i = 0; i < own_count; i++) {
            int match = -1;
            for (int j = 0; j < their_count; j++)
                if (own[i].ready.hard == theirs[j].ready.hard && own[i].ready.potion == theirs[j].ready.potion
                        && own[i].ready.fast == theirs[j].ready.fast) { match = j; break; }
            if (match < 0) { bad = 1; continue; }
            int levels = imax(own[i].levels, theirs[match].levels);
            for (int hp = 0; hp < levels; hp++) {
                double a = hp < own[i].levels ? own[i].hp[hp] : 0;
                double b = hp < theirs[match].levels ? theirs[match].hp[hp] : 0;
                maximum_error = fmax(maximum_error, fabs(a - b));
                bad |= kind ? fabs(a - b) > 1e-12 : a != b;
            }
        }
    }
    stats->target_records++;
    if (bad) {
        stats->target_mismatches++;
        if (stats->shown < 12) fprintf(stderr, "TARGET tick %d readiness/support/mass mismatch, max error %.17g\n", tick, maximum_error);
        stats->shown++;
    }
    gs_target_clear(&target);
    return bad;
}

static int compare_pending(const GsInfer* inf, char** t, int n, int tick) {
    GsPid pid = gs_pid_at(inf, tick);
    int known = gs_pending_attacks_known(inf, tick, pid);
    int bad = strcmp(t[2], known ? "KNOWN" : "UNKNOWN") != 0;
    GsSwing incoming[GS_MAX_PENDING_SWINGS], reflections[GS_MAX_PENDING_SWINGS];
    int incoming_count = 0, reflection_count = 0;
    for (int i = 0; i < inf->pending_launch_count; i++) {
        GsSwing hit = gs_pending_remaining(inf->pending_launches[i].by_pid[pid], tick);
        if (!hit.arrows) continue;
        int at = incoming_count++;
        while (at > 0 && incoming[at - 1].lands > hit.lands) { incoming[at] = incoming[at - 1]; at--; }
        incoming[at] = hit;
    }
    if (pid == GS_PID_LOCAL) for (int i = 0; i < inf->reflection_count; i++) {
        GsSwing hit = gs_pending_remaining(inf->reflection_swings[i], tick);
        if (hit.arrows) reflections[reflection_count++] = hit;
    }
    if (atoi(t[3]) != incoming_count || atoi(t[4]) != reflection_count) return 1;
    if (n != 5 + 6 * (incoming_count + reflection_count)) return 1;
    int at = 5;
    for (int kind = 0; kind < 2; kind++) {
        const GsSwing* hits = kind ? reflections : incoming;
        int count = kind ? reflection_count : incoming_count;
        for (int i = 0; i < count; i++, at += 6) {
            const GsSwing* hit = &hits[i];
            bad |= strcmp(t[at], kind ? "R" : "I") || atoi(t[at + 1]) != hit->lands
                || atoi(t[at + 2]) != hit->least || atoi(t[at + 3]) != hit->most
                || fabs(strtod(t[at + 4], NULL) - hit->accuracy) > 1e-12 || atoi(t[at + 5]) != hit->arrows;
        }
    }
    return bad;
}

static void unread_bar_provenance(void) {
    GsInfer s;
    gs_infer_init(&s);
    assert(s.bar == GS_BAR_UNREAD);
    s.c.worn[GS_WORN_WEAPON] = GS_ITEM_VOIDWAKER;
    s.c.strength = s.c.attack = s.c.ranged = s.c.magic = 99;
    s.c.ratio = s.c.scale = 30;
    gs_eater_observe(&s, 0);
    assert(!s.c.forecast_available && !s.c.opponent_combat_level);
    assert(s.bar == GS_BAR_READ && s.health->evidence_count == 1 && s.health->evidence[0].has_bar);
    gs_eater_reset(&s);
    s.c.opponent_combat_level = 126;
    s.c.ratio = -1;
    gs_eater_observe(&s, 1);
    assert(s.bar == GS_BAR_UNREAD);
    assert(s.opponent_hitpoints.low == GS_OVERHEALED_HP);
    s.c.ratio = s.c.scale = 30;
    gs_eater_observe(&s, 2);
    assert(s.bar == GS_BAR_READ);
    GsHealthRange read = s.opponent_hitpoints;
    s.c.ratio = 15;
    gs_eater_observe(&s, 3);
    assert(s.opponent_hitpoints.low == read.low && s.opponent_hitpoints.high == read.high);
    gs_eater_opponent_hitsplat(&s, 4);
    gs_eater_opponent_struck(&s, 4, 50);
    gs_eater_opponent_animated(&s, GS_KIND_EAT, 829, 4);
    assert(s.bar == GS_BAR_FRESH);
    gs_eater_observe(&s, 4);
    assert(s.bar == GS_BAR_READ && s.opponent_hitpoints.high < read.high);
    GsSpecSituation spec;
    GsUnderway underway;
    gs_spec_inputs(&s, 4, &spec, &underway);
    int hard_ready = INT_MAX;
    for (int i = 0; i < s.health->target.history_count; i++)
        hard_ready = imin(hard_ready, imax(4, s.health->target.history[i].ready.hard));
    assert(spec.hard_food_ready == hard_ready);
    const GsTargetBranch* retained = s.health->target.history;
    GsHealthRange unchanged = s.opponent_hitpoints;
    GsInfer clone;
    gs_infer_clone(&clone, &s);
    gs_eater_opponent_hitsplat(&clone, 5);
    gs_eater_opponent_struck(&clone, 5, 30);
    clone.c.ratio = 6;
    gs_eater_observe(&clone, 5);
    assert(clone.health != s.health && s.health->target.history == retained);
    assert(!memcmp(&s.opponent_hitpoints, &unchanged, sizeof(unchanged)));
    assert(clone.opponent_hitpoints.high < s.opponent_hitpoints.high);
    gs_infer_clear(&clone);
    gs_eater_reset(&s);
    assert(s.bar == GS_BAR_UNREAD);
}

static void blocked_hits_pair_without_erasing_damage(void) {
    for (int outgoing = 0; outgoing < 2; outgoing++) {
        for (int delay = 0; delay <= 1; delay++) {
            GsInfer s;
            gs_infer_init(&s);
            s.tracker_current = s.timed = 1;
            s.pid.local = (GsStruck){99, 20};
            s.pid.opponent = (GsStruck){99, 30};
            s.pid = outgoing ? gs_pid_local_attacked(s.pid, 100) : gs_pid_opponent_attacked(s.pid, 100);
            gs_on_hitsplat(&s, 100 + delay, outgoing ? 1 : 0, 0,
                outgoing ? GS_ATTACK_HITSPLAT_BLOCK_ME : GS_ATTACK_HITSPLAT_BLOCK_OTHER, outgoing, 0);
            assert(s.pid.owner == (outgoing == !delay ? GS_PID_LOCAL : GS_PID_OPPONENT));
            assert(s.pid.observed_tick == 100 + delay);
            assert(s.pid.local.tick == 99 && s.pid.local.amount == 20);
            assert(s.pid.opponent.tick == 99 && s.pid.opponent.amount == 30);
            gs_infer_clear(&s);
        }
    }
    GsPidState s = gs_pid_hit_on_opponent(gs_pid_unknown(), 200, 0);
    s = gs_pid_local_attacked(s, 200);
    assert(s.owner == GS_PID_LOCAL && s.observed_tick == 200);
    s = gs_pid_unknown();
    s = gs_pid_hit_on_local(s, 300, 20);
    s = gs_pid_hit_on_local(s, 301, 0);
    s = gs_pid_local_attacked(s, 301);
    s = gs_pid_hit_on_opponent(s, 301, 3);
    assert(s.owner == GS_PID_UNKNOWN && s.local.amount == 20);
    s = gs_pid_hit_on_opponent(s, 301, 15);
    assert(s.owner == GS_PID_UNKNOWN && s.opponent.amount == 0);
}

static void meals_use_earliest_complete_rescue_and_finite_risk(void) {
    Arena arena = arena_make((size_t)1 << 24);
    GsEatSituation s = {0};
    s.tick = 100;
    s.hitpoints = 99;
    s.pid = GS_PID_OPPONENT;
    s.opponent_timer = (GsTimer){1, 101, 101};
    s.attacks[s.attack_count++] = (GsAttack){GS_MELEE, 0, 20, 0, 4, 1, 0, 1};
    s.local_interval = 4;
    s.vengeance = GS_VENGEANCE_DOWN;
    s.opponent_hp = (GsHealthRange){99, 99, 99};
    s.floor = GS_NONE;
    s.warning.since = GS_NONE;
    GsVerdict no_food = gs_auto_eat_decide(&s, NULL, 0, NULL, 0, &arena);
    assert(no_food.decision == GS_WAIT && no_food.risk == 0);
    GsFood foods[] = {{GS_HARD_FOOD, 22, 101, 1, 0}, {GS_FAST_FOOD, 18, 101, 1, 0}};
    GsMeal meals[] = {
        {2, {0, 1}, {1, 3}, {1, 3}, 1, 4, 4},
        {1, {0}, {1}, {1}, 1, 2, 2}
    };
    GsEatCall call = {foods, 2, meals, 2, &arena};
    GsBranch rescue = eat_branch(&call, &s, NULL, INT_MIN);
    assert(branch_last(&rescue, 101) == 102);
    call.meal_count = 1;
    rescue = eat_branch(&call, &s, NULL, INT_MIN);
    assert(branch_last(&rescue, 101) == 104);
    arena.used = 0;
    s.hitpoints = 85;
    s.attacks[0] = (GsAttack){GS_MELEE, 50, 50, 50, 4, 1, 0, 1};
    s.has_maul = 1;
    s.maul = (GsAttack){GS_MELEE, 0, 40, 50, 4, 1, 0, 1};
    s.energy_high = 100;
    s.tolerance = 0.01;
    GsVerdict combo = gs_auto_eat_decide(&s, NULL, 0, NULL, 0, &arena);
    assert(combo.risk > s.tolerance && combo.decision == GS_EXPOSED);
    s.energy_high = 50;
    GsVerdict one_ticket = gs_auto_eat_decide(&s, NULL, 0, NULL, 0, &arena);
    assert(one_ticket.risk == 0 && one_ticket.decision == GS_WAIT);
    free(arena.base);
}

static void dagger_special_defends_slash_not_normal_stab(void) {
    GsClient c = {0};
    c.defence = c.magic = 99;
    c.worn[4] = 1127; /* Rune platebody: stab82, slash80, crush72. */
    GsArmament dagger;
    assert(gs_armed(&c, GS_NO_BONUSES, 27867, &dagger));
    assert(dagger.weapon == GS_W_ABYSSAL_DAGGER_BH && dagger.damage == GS_DAMAGE_STAB && dagger.attack_bonus == 85);
    double normal = gs_armament_accuracy(&c, &dagger);
    assert(fabs(normal - gs_chance(22648, 15622)) < 1e-12);
    double special = gs_spec_accuracy(&c, &dagger);
    assert(fabs(special - gs_chance(28310, 15408)) < 1e-12);
    assert(special != gs_chance(28310, 15622));
    dagger.weapon = GS_W_ABYSSAL_DAGGER;
    assert(fabs(gs_spec_accuracy(&c, &dagger) - special) < 1e-12);
    GsArmament mace;
    assert(gs_armed(&c, GS_NO_BONUSES, 27857, &mace));
    assert(mace.damage == GS_DAMAGE_CRUSH && mace.attack_bonus == 64);
    assert(fabs(gs_spec_accuracy(&c, &mace) - gs_chance(24320, 8731)) < 1e-12);
    dagger.weapon = GS_W_VESTAS_LONGSWORD;
    assert(fabs(gs_spec_accuracy(&c, &dagger) - gs_chance(22648, 3905)) < 1e-12);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    gs_register_java_items();
    unread_bar_provenance();
    blocked_hits_pair_without_erasing_damage();
    meals_use_earliest_complete_rescue_and_finite_risk();
    dagger_special_defends_slash_not_normal_stab();
    FILE* f = fopen(argv[1], "r");
    assert(f);
    static char plan_lines[LINE * 4], line[LINE], record[LINE * 4], mine_situation[LINE * 2], mine_spec[LINE * 2], spec_record[LINE * 4], copy[LINE];
    GsInfer inf;
    GsEatSituation mine_situation_state;
    GsWire wire = {0};
    Stats stats = {0};
    gs_infer_init(&inf);
    int recordings = 0, tracker_mismatch = 0, pending_tick = -1, capturing = 0, captured = 0, have_mine = 0, have_spec = 0, spec_captured = 0;
    while (fgets(line, sizeof(line), f)) {
        assert(strchr(line, '\n'));
        if (!strncmp(line, "RECORDING", 9)) {
            assert(capturing == 0 && record[0] == 0 && !have_spec);
            recordings++;
            gs_infer_clear(&inf);
            gs_infer_init(&inf);
            wire = (GsWire){0};
            continue;
        }
        if (capturing) {
            if (!strncmp(line, "END", 3)) {
                if (capturing == 2) {
                    spec_captured = 1;
                    stats.spec_records++;
                    if (!have_spec) stats.unavailable_spec_policy++;
                    GsSpecSituation spec;
                    GsUnderway underway;
                    gs_spec_inputs(&inf, inf.tick, &spec, &underway);
                    format_spec_inputs(&spec, &underway, mine_spec);
                    char theirs[LINE * 2];
                    canonical_java_spec(spec_record, theirs);
                    if (compare_record(mine_spec, theirs, pending_tick, &stats, "SPEC")) stats.spec_mismatches++;
                    compare_target_record(&spec.target, spec_record, pending_tick, &stats);
                    compare_plans(plan_lines, &inf, pending_tick, &stats);
                    have_spec = 0;
                } else if (capturing == 1) captured = 1;
                capturing = 0;
                plan_lines[0] = 0;
            }
            else if (capturing == 1 && (!strncmp(line, "F ", 2) || !strncmp(line, "M ", 2) || !strncmp(line, "V ", 2) || !strncmp(line, "VH ", 3))) {}
            else if (capturing == 2 && !strncmp(line, "PL ", 3)) {
                if (strlen(plan_lines) + strlen(line) >= sizeof(plan_lines)) abort();
                strcat(plan_lines, line);
            }
            else if (capturing == 2 && (!strncmp(line, "AC ", 3) || !strncmp(line, "D ", 2))) {}
            else {
                char* destination = capturing == 1 ? record : spec_record;
                if (strlen(destination) + strlen(line) >= sizeof(record)) abort();
                strcat(destination, line);
            }
            continue;
        }
        if (!strncmp(line, "SS ", 3)) {
            if (spec_captured) abort();
            capturing = 2; strcpy(spec_record, line);
            continue;
        }
        strcpy(copy, line);
        char* t[GS_WIRE_TOKENS];
        int n = gs_wire_split(copy, t);
        if (n == 0) continue;
        if (!strcmp(t[0], "S") && n > 3 && isalpha((unsigned char)t[3][0])) {
            if (captured) abort();
            capturing = 1; strcpy(record, line);
            continue;
        }
        if (!strcmp(t[0], "K")) {
            if (have_spec) stats.unavailable_spec_policy++;
            have_spec = 0;
            spec_captured = 0;
            if (gs_spec_plannable(&inf)) {
                GsSpecSituation spec;
                GsUnderway underway;
                gs_spec_inputs(&inf, inf.tick, &spec, &underway);
                format_spec_inputs(&spec, &underway, mine_spec);
                have_spec = 1;
            }
            continue;
        }
        if (!strcmp(t[0], "PENDING")) {
            assert(n >= 5 && atoi(t[1]) == pending_tick);
            stats.pending_records++;
            if (!gs_pending_damage_known(&inf, pending_tick, gs_pid_at(&inf, pending_tick)))
                stats.extended_pending_unavailable++;
            stats.extended_recoil_unavailable += inf.echo_unknown != 0;
            stats.extended_local_unavailable += !gs_local_pending_known(&inf, pending_tick);
            int bad = compare_pending(&inf, t, n, pending_tick);
            stats.inference_mismatches += bad;
            if (bad && stats.inference_mismatches <= 12) {
                fprintf(stderr, "PENDING tick %d source=%s native pid=%d launches=%d unknown=%d reflections=%d echo_unknown=%d proc=%d proc_lands=%d owned=%d landed_count=%d\n",
                    pending_tick, line, gs_pid_at(&inf, pending_tick), inf.pending_launch_count, inf.pending_launch_unknown,
                    inf.reflection_count, inf.echo_unknown, inf.vengeance_proc_tick, inf.vengeance_proc_lands, inf.landed_owned, inf.landed_count);
                GsPid pid = gs_pid_at(&inf, pending_tick);
                for (int i = 0; i < inf.pending_launch_count; i++) {
                    GsSwing hit = gs_pending_remaining(inf.pending_launches[i].by_pid[pid], pending_tick);
                    if (hit.arrows) fprintf(stderr, "  native incoming %d %d %d %.17g %d\n", hit.lands, hit.least, hit.most, hit.accuracy, hit.arrows);
                }
            }
            continue;
        }
        if (!strcmp(t[0], "INF")) {
            stats.inference_lines++;
            if (have_mine && record[0] != 0) {
                char first[LINE];
                size_t first_length = strcspn(record, "\n");
                if (first_length >= sizeof(first)) abort();
                memcpy(first, record, first_length);
                first[first_length] = 0;
                char* tokens[GS_WIRE_TOKENS];
                gs_wire_split(first, tokens);
                gs_eat_situation(&inf, pending_tick, inf.c.local_interacting >= 0 ? GS_ENGAGED : GS_IDLE, atof(tokens[12]), atof(tokens[14]), &mine_situation_state);
                format_situation(&mine_situation_state, mine_situation);
            }
            if (have_mine) {
                stats.situations++;
                if (record[0] == 0) stats.unavailable_situation_policy++;
                else if (compare_record(mine_situation, record, pending_tick, &stats, "SITUATION")) stats.situation_mismatches++;
                if (record[0]) compare_target_record(&mine_situation_state.target, record, pending_tick, &stats);
            } else if (record[0] != 0) { stats.situation_mismatches++; if (stats.shown < 12) { fprintf(stderr, "tick %d: Java built a situation C did not\n", pending_tick); stats.shown++; } }
            record[0] = 0;
            continue;
        }
        char applied = gs_wire_apply(&inf, &wire, line);
        if (applied == 'S' && inf.c.tracked != inf.tracker_current) tracker_mismatch++;
        if (applied == 'T') {
            int tick = atoi(t[1]);
            stats.ticks++;
            have_mine = 0;
            if (inf.timed >= 0 && inf.c.ratio != 0) {
                GsEatSituation s;
                gs_eat_situation(&inf, tick, inf.c.local_interacting >= 0 ? GS_ENGAGED : GS_IDLE, 0.01, 1.0, &s);
                format_situation(&s, mine_situation);
                have_mine = 1;
            }
            pending_tick = tick;
            record[0] = 0;
            captured = 0;
        }
    }
    assert(!ferror(f) && stats.ticks > 0);
    assert(capturing == 0 && record[0] == 0 && !have_spec);
    gs_wire_finish(&wire);
    fclose(f);
    gs_infer_clear(&inf);
    printf("recordings %d, ticks %ld, tracker mismatches %d\n", recordings, stats.ticks, tracker_mismatch);
    printf("source ticks %ld, typed pending records %ld, mismatches %ld\n", stats.inference_lines, stats.pending_records, stats.inference_mismatches);
    printf("situations %ld, mismatches %ld\n", stats.situations, stats.situation_mismatches);
    printf("spec inputs %ld, mismatches %ld\n", stats.spec_records, stats.spec_mismatches);
    printf("target histories/beliefs %ld, mismatches %ld\n", stats.target_records, stats.target_mismatches);
    printf("plan damage records %ld, mismatches %ld, context unavailable %ld\n", stats.plan_records, stats.plan_mismatches, stats.unavailable_plan_damage);
    printf("plan availability/timing UNAVAILABLE: causal Java hand inputs are absent; native measured planner only; C-only plans %ld\n", stats.extra_plans);
    printf("spec policy availability UNAVAILABLE: causal Java hand inputs are absent; presence differences %ld; all serialized spec inputs compared\n", stats.unavailable_spec_policy);
    printf("eat policy availability UNAVAILABLE: causal Java hand inputs are absent; native-only situations %ld; all serialized situations compared\n", stats.unavailable_situation_policy);
    printf("native extended pending availability: unavailable observations %ld (unresolved recoil %ld, unresolved outgoing launches %ld); source attack/vengeance queue compared separately\n", stats.extended_pending_unavailable, stats.extended_recoil_unavailable, stats.extended_local_unavailable);
    assert(stats.pending_records == stats.inference_lines && stats.inference_lines == stats.ticks);
    return tracker_mismatch || stats.inference_mismatches || stats.situation_mismatches || stats.spec_mismatches || stats.plan_mismatches || stats.target_mismatches ? 1 : 0;
}
