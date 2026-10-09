/* Differential test of the C inference port (gs_infer.h) against the Java plugin.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/test_gs_infer ocean/osrs/tests/test_gs_infer.c -lm
 *   /tmp/test_gs_infer WIRE.tap
 *
 * The wire is written by `./gradlew :gear-swapper:fightReplay -Precording=... -Pwiretap=FILE` in the Light repo: every event the plugin
 * handlers receive with the client state before it (S lines), the eat decisions the plugin built (AutoEat.tap records) and the inference
 * state after each GameTick (INF lines). The test feeds the events to the C port and compares both. */
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include "../gearswapper/gs_wire.h"
#include "../gearswapper/gs_plan.h"

enum { LINE = 1 << 16, TOKENS = 256, COMBAT_LEVEL = 126 };

static const char* const WEAPON_NAMES[GS_WEAPONS] = {"VOIDWAKER", "GRANITE_MAUL", "ARMADYL_GODSWORD", "ANCIENT_GODSWORD", "DRAGON_CLAWS", "DRAGON_DAGGER",
    "ABYSSAL_DAGGER_BH", "ABYSSAL_DAGGER", "VESTAS_SPEAR", "DRAGON_MACE_BH", "DRAGON_MACE", "VESTAS_LONGSWORD", "STATIUS_WARHAMMER", "MORRIGANS_THROWING_AXE", "MORRIGANS_JAVELIN", "ZURIELS_STAFF", "NOXIOUS_HALBERD",
    "DRAGON_WARHAMMER", "DARK_BOW", "BALLISTA", "ARMADYL_CROSSBOW", "ZARYTE_CROSSBOW",
    "TOXIC_BLOWPIPE", "MAGIC_SHORTBOW_I", "MAGIC_SHORTBOW", "VOLATILE_NIGHTMARE_STAFF", "DHAROKS_GREATAXE", "SOULREAPER_AXE", "OTHER"};
static const char* const STYLE_NAMES[3] = {"MELEE", "RANGED", "MAGIC"};
static const char* const PID_NAMES[3] = {"LOCAL", "OPPONENT", "UNKNOWN"};
static const char* const VENGEANCE_NAMES[4] = {"UNKNOWN", "UP", "DOWN", "MAYBE"};
static const char* const BAR_NAMES[3] = {"READ", "FRESH", "BEHIND_A_MEAL"};
static const char* const RECOIL_NAMES[2] = {"UNSEEN", "SEEN"};
static const char* const PHASE_NAMES[4] = {"UNKNOWN", "CAST", "PROCCED", "DISARMED"};
static const char* const DAMAGE_NAMES[5] = {"STAB", "SLASH", "CRUSH", "RANGED", "MAGIC"};

static void format_timer(char* out, GsTimerState s) {
    if (!s.seen) sprintf(out, "UNKNOWN");
    else if (s.earliest == s.latest) sprintf(out, "due %d", s.latest);
    else sprintf(out, "due %d..%d", s.earliest, s.latest);
}

static void format_range(char* out, GsEnergyRange r) {
    if (r.low == r.high) sprintf(out, "%d", r.low);
    else sprintf(out, "%d-%d", r.low, r.high);
}

static void format_optional_int(char* out, int present, int value) {
    if (present) sprintf(out, "OptionalInt[%d]", value);
    else sprintf(out, "OptionalInt.empty");
}

static int cmp_int(const void* a, const void* b) { return *(const int*)a - *(const int*)b; }

static const char* reflect_name(GsReflect r) { return r == GS_REFLECT_SPEC ? "SPEC" : r == GS_REFLECT_WORTH ? "WORTH" : "WEAK"; }

static void format_inference(GsInfer* s, int tick, char* out) {
    char timer[64], local[64], typical[32], possible[32], earliest[40];
    format_timer(timer, s->opponent.timer);
    format_timer(local, s->local_timer);
    format_range(typical, s->opponent.spec.typical);
    format_range(possible, s->opponent.spec.possible);
    format_optional_int(earliest, s->opponent.vengeance.has_earliest, s->opponent.vengeance.earliest);
    char age[16];
    if (s->pid.owner == GS_PID_UNKNOWN) sprintf(age, "-");
    else sprintf(age, "%d", tick - s->pid.observed_tick);
    char hp[32];
    if (s->opponent_hitpoints.low == s->opponent_hitpoints.high) sprintf(hp, "%d", s->opponent_hitpoints.low);
    else sprintf(hp, "%d-%d", s->opponent_hitpoints.low, s->opponent_hitpoints.high);
#ifdef GS_HP_BELIEF
    char list[GS_WATCHED_LIMIT + 64];
    if (s->opponent_bar.low == s->opponent_bar.high) sprintf(list, "bandread %d watched ", s->opponent_bar.low);
    else sprintf(list, "bandread %d-%d watched ", s->opponent_bar.low, s->opponent_bar.high);
    if (!s->watched_count) strcat(list, "-");
    for (int i = 0; i < s->watched_count; i++) sprintf(list + strlen(list), "%c", "QAH"[s->watched[i]]);
#else
    int unseen[GS_UNSEEN_LIMIT];
    memcpy(unseen, s->unseen, sizeof(int) * (size_t)s->unseen_count);
    qsort(unseen, (size_t)s->unseen_count, sizeof(int), cmp_int);
    char list[1024] = "unseen [";
    int unique = 0;
    for (int i = 0; i < s->unseen_count; i++) {
        if (i && unseen[i] == unseen[i - 1]) continue;
        sprintf(list + strlen(list), "%s%d", unique++ ? ", " : "", unseen[i]);
    }
    strcat(list, "]");
#endif
    char landings[160] = "[";
    for (int i = 0; i < s->opponent.spec.landing_count; i++) sprintf(landings + strlen(landings), "%s%d", i ? ", " : "", s->opponent.spec.landings[i]);
    strcat(landings, "]");
    char swing[96];
    if (s->has_opponent_swing) {
        GsSwing thrown = gs_opponent_swing_for(s, gs_pid_at(s, tick));
        sprintf(swing, "Optional[%d..%d@%dx%d]", thrown.least, thrown.most, thrown.lands, thrown.arrows);
    }
    else sprintf(swing, "Optional.empty");
    char arsenal[2048] = "";
    int order[GS_ARSENAL_SIZE];
    gs_arsenal_order(s, order);
    for (int k = 0; k < s->arsenal_count; k++) {
        const GsArmament* a = &s->arsenal[order[k]];
        char spec[40];
        format_optional_int(spec, a->has_spec, a->spec);
        sprintf(arsenal + strlen(arsenal), "%d=%s,%s,%d,%s,%d,%d;", s->arsenal_id[order[k]], WEAPON_NAMES[a->weapon], STYLE_NAMES[a->style], a->normal, spec, a->interval, a->attack_bonus);
    }
    char ate[16], reach[16], food[16];
    sprintf(ate, "%d", s->opponent_ate); sprintf(reach, "%d", s->opponent_in_reach_since); sprintf(food, "%d", s->opponent_hard_food_ready);
    sprintf(out, "pid %s %s | timer %s | spec %s typical %s possible %s from %d landing %s | veng %s %s..%d | local %s | hp %s ate %s reach %s bar %s dmg %d %s food %s recoil %s swing %s soul %d@%d immune %d surplus %d,%d,%.1f arsenal %s",
        PID_NAMES[s->pid.owner], age, timer, s->opponent.spec.broken ? "BROKEN_PRIOR" : "TYPICAL", typical, possible, s->opponent.spec.anchor, landings,
        PHASE_NAMES[s->opponent.vengeance.phase], earliest, s->opponent.vengeance.latest, local, hp, ate, reach, BAR_NAMES[s->bar], s->damage_since_bar, list, food,
        RECOIL_NAMES[s->recoil], swing, s->soul_stacks.held, s->soul_stacks.tick, s->opponent.immune_through, s->surplus.strength, s->surplus.ranged_strength,
        s->surplus.magic_damage, arsenal);
    sprintf(out + strlen(out), " | reflect %s", reflect_name(s->swing_reflect));
    if (gs_living_rival(s) && s->tick == tick)
        sprintf(out + strlen(out), " %s cost %d", reflect_name(gs_weapon_reflect(s, tick)), gs_opponent_spec_cost(s));
}

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
    if (s->has_swing) sprintf(out + strlen(out), "OSW %d %d %d %.17g %d\n", s->swing.lands, s->swing.least, s->swing.most, s->swing.accuracy, s->swing.arrows);
    if (s->has_local_swing) sprintf(out + strlen(out), "LSW %d %d %d %.17g %d\n", s->local_swing.lands, s->local_swing.least, s->local_swing.most, s->local_swing.accuracy, s->local_swing.arrows);
    if (s->has_owed) sprintf(out + strlen(out), "OE %d %d\n", s->owed.tick, s->owed.most);
    if (s->warning.since == GS_NONE) strcat(out, "W none");
    else sprintf(out + strlen(out), "W %d", s->warning.since);
#ifdef GS_HP_BELIEF
    strcat(out, " ");
    if (!s->warning.watched_count) strcat(out, "-");
    for (int i = 0; i < s->warning.watched_count; i++) sprintf(out + strlen(out), "%c", "QAH"[s->warning.watched[i]]);
#else
    int sorted[64];
    memcpy(sorted, s->warning.unseen, sizeof(int) * (size_t)s->warning.unseen_count);
    qsort(sorted, (size_t)s->warning.unseen_count, sizeof(int), cmp_int);
    for (int i = 0; i < s->warning.unseen_count; i++) sprintf(out + strlen(out), " %d", sorted[i]);
#endif
    strcat(out, "\n");
#ifdef GS_HP_BELIEF
    sprintf(out + strlen(out), "OB %d %d %d\n", s->opponent_bar.low, s->opponent_bar.high, s->opponent_bar.max);
#endif
}

static void format_spec_inputs(const GsSpecSituation* s, const GsUnderway* underway, char* out) {
    sprintf(out, "SS %d %d %d %d %s %s %d %d\n", s->hitpoints.low, s->hitpoints.high, s->hitpoints.max, s->hard_food_ready, VENGEANCE_NAMES[s->vengeance],
        PID_NAMES[s->pid], s->recoil_charges, s->tick);
    if (underway->certain) sprintf(out + strlen(out), "UW %d %d\n", underway->lands, underway->damage);
    else strcat(out, "UW\n");
    if (s->has_incoming) {
        sprintf(out + strlen(out), "IN %d %d %d %.17g ", s->incoming.lands, s->incoming.least, s->incoming.most, s->incoming.accuracy);
        if (s->incoming.attack == GS_NONE) strcat(out, "none\n");
        else sprintf(out + strlen(out), "%d\n", s->incoming.attack);
    }
    if (s->warning.since == GS_NONE) strcat(out, "W none");
    else sprintf(out + strlen(out), "W %d", s->warning.since);
#ifdef GS_HP_BELIEF
    strcat(out, " ");
    if (!s->warning.watched_count) strcat(out, "-");
    for (int i = 0; i < s->warning.watched_count; i++) sprintf(out + strlen(out), "%c", "QAH"[s->warning.watched[i]]);
#else
    int sorted[64];
    memcpy(sorted, s->warning.unseen, sizeof(int) * (size_t)s->warning.unseen_count);
    qsort(sorted, (size_t)s->warning.unseen_count, sizeof(int), cmp_int);
    for (int i = 0; i < s->warning.unseen_count; i++) sprintf(out + strlen(out), " %d", sorted[i]);
#endif
    strcat(out, "\n");
}

/* The Java SS, UW, IN and W lines reduced to what the inference decides: the rebound, energy and plans are ours. */
static void canonical_java_spec(const char* record, char* out) {
    char copy[LINE * 2];
    strcpy(copy, record);
    out[0] = 0;
    char* save;
    for (char* line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char one[LINE];
        strcpy(one, line);
        char* t[TOKENS];
        int n = gs_wire_split(one, t);
        if (!strcmp(t[0], "SS")) sprintf(out + strlen(out), "SS %s %s %s %s %s %s %s %s\n", t[1], t[2], t[3], t[4], t[5], t[6], t[10], t[11]);
        else if (!strcmp(t[0], "UW")) {
            if (n == 1) strcat(out, "UW\n");
            else sprintf(out + strlen(out), "UW %s %d\n", t[2], atoi(t[3]) - 1);
        }
        else if (!strcmp(t[0], "IN") || !strcmp(t[0], "W")) strcat(out, line), strcat(out, "\n");
    }
}

typedef struct { long plan_records, plan_mismatches, extra_plans; long ticks, situations, situation_mismatches, inference_lines, inference_mismatches, shown, spec_records, spec_mismatches; } Stats;

static const char* const PLAN_NAMES[4] = {"HOLD", "VOIDWAKER", "MAUL_VOIDWAKER", "MAUL"};

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
    for (char* l = strtok_r(java_plans, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char one[LINE * 2];
        strcpy(one, l);
        char* t[GS_WIRE_TOKENS * 64];
        char* tok = strtok(one, " ");
        int n = 0;
        while (tok && n < (int)(sizeof(t) / sizeof(*t))) { t[n++] = tok; tok = strtok(NULL, " "); }
        stats->plan_records++;
        int match = -1;
        for (int i = 0; i < count; i++) if (!strcmp(PLAN_NAMES[mine[i].choice], t[1])) match = i;
        int bad = match < 0;
        if (!bad) {
            int at = 2;
            for (int h = 0; h < mine[match].hit_count && !bad; h++) {
                if (at + 4 > n || strcmp(t[at], "H")) { bad = 1; break; }
                int len = atoi(t[at + 2]);
                if (len != mine[match].hits[h].damages || strcmp(t[at + 3], mine[match].hits[h].origin == GS_ORIGIN_OURS ? "OURS" : "REFLECTED")) { bad = 1; break; }
                for (int d = 0; d < len; d++) { double j = strtod(t[at + 4 + d], NULL); if (memcmp(&j, &mine[match].hits[h].damage[d], sizeof(double))) { bad = 1; break; } }
                at += 4 + len;
            }
            if (!bad && at != n) bad = 1;
            found[match] = 1;
        }
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


static int compare_record(const char* mine, const char* theirs, int tick, Stats* stats, const char* kind) {
    char a[LINE * 2], b[LINE * 2];
    strcpy(a, mine); strcpy(b, theirs);
    char* save_a; char* save_b;
    char* la = strtok_r(a, "\n", &save_a);
    char* lb = strtok_r(b, "\n", &save_b);
    int bad = 0;
    while (la || lb) {
        if (!la || !lb || !same_line(la, lb)) {
            if (stats->shown < 12) fprintf(stderr, "%s tick %d mismatch\n  C:    %s\n  Java: %s\n", kind, tick, la ? la : "(none)", lb ? lb : "(none)");
            stats->shown++;
            bad = 1;
            break;
        }
        la = strtok_r(NULL, "\n", &save_a);
        lb = strtok_r(NULL, "\n", &save_b);
    }
    return bad;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    gs_register_java_items();
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
            gs_infer_init(&inf);
            wire = (GsWire){0};
            continue;
        }
        if (capturing) {
            if (!strncmp(line, "END", 3)) {
                if (capturing == 2) {
                    spec_captured = 1;
                    stats.spec_records++;
                    if (!have_spec) { stats.spec_mismatches++; if (stats.shown < 12) { fprintf(stderr, "tick %d: Java planned a spec C did not\n", pending_tick); stats.shown++; } }
                    else {
                        char theirs[LINE * 2];
                        canonical_java_spec(spec_record, theirs);
                        if (compare_record(mine_spec, theirs, pending_tick, &stats, "SPEC")) stats.spec_mismatches++;
                        compare_plans(plan_lines, &inf, pending_tick, &stats);
                        have_spec = 0;
                    }
                } else if (capturing == 1) captured = 1;
                capturing = 0;
                plan_lines[0] = 0;
            }
            else if (capturing == 3) {}
            else if (capturing == 1 && (!strncmp(line, "F ", 2) || !strncmp(line, "M ", 2) || !strncmp(line, "V ", 2) || !strncmp(line, "VH ", 3))) {}
            else if (capturing == 2 && !strncmp(line, "PL ", 3)) strcat(plan_lines, line);
            else if (capturing == 2 && (!strncmp(line, "AC ", 3) || !strncmp(line, "D ", 2))) {}
            else strcat(capturing == 1 ? record : spec_record, line);
            continue;
        }
        if (!strncmp(line, "SS ", 3)) {
            if (!spec_captured) { capturing = 2; strcpy(spec_record, line); }
            else capturing = 3;
            continue;
        }
        strcpy(copy, line);
        char* t[GS_WIRE_TOKENS];
        int n = gs_wire_split(copy, t);
        if (n == 0) continue;
        if (!strcmp(t[0], "S") && n > 3 && isalpha((unsigned char)t[3][0])) {
            if (!captured) { capturing = 1; strcpy(record, line); }
            else capturing = 3;
            continue;
        }
        if (!strcmp(t[0], "K")) {
            if (have_spec) { stats.spec_mismatches++; if (stats.shown < 12) { fprintf(stderr, "tick %d: C planned a spec Java did not\n", pending_tick); stats.shown++; } }
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
        if (!strcmp(t[0], "INF")) {
            char mine[LINE], theirs[LINE];
            format_inference(&inf, pending_tick, mine);
            strcpy(theirs, line + 4 + strlen(t[1]) + 1);
            theirs[strcspn(theirs, "\n")] = 0;
            stats.inference_lines++;
            if (!same_line(mine, theirs)) {
                stats.inference_mismatches++;
                if (stats.shown < 12) { fprintf(stderr, "INF tick %d mismatch\n  C:    %s\n  Java: %s\n", pending_tick, mine, theirs); stats.shown++; }
            }
            if (have_mine && record[0] != 0) {
                char first[LINE];
                strcpy(first, record);
                char* tokens[GS_WIRE_TOKENS];
                gs_wire_split(first, tokens);
                gs_eat_situation(&inf, pending_tick, inf.c.local_interacting >= 0 ? GS_ENGAGED : GS_IDLE, atof(tokens[12]), atof(tokens[14]), &mine_situation_state);
                format_situation(&mine_situation_state, mine_situation);
            }
            if (have_mine) {
                stats.situations++;
                if (record[0] == 0) { stats.situation_mismatches++; if (stats.shown < 12) { fprintf(stderr, "tick %d: Java built no situation\n", pending_tick); stats.shown++; } }
                else if (compare_record(mine_situation, record, pending_tick, &stats, "SITUATION")) stats.situation_mismatches++;
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
                gs_eater_observe(&inf, tick);
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
    fclose(f);
    printf("recordings %d, ticks %ld, tracker mismatches %d\n", recordings, stats.ticks, tracker_mismatch);
    printf("inference lines %ld, mismatches %ld\n", stats.inference_lines, stats.inference_mismatches);
    printf("situations %ld, mismatches %ld\n", stats.situations, stats.situation_mismatches);
    printf("spec inputs %ld, mismatches %ld\n", stats.spec_records, stats.spec_mismatches);
    printf("plans %ld, damage mismatches %ld, plans only C builds %ld\n", stats.plan_records, stats.plan_mismatches, stats.extra_plans);
    return stats.inference_mismatches || stats.situation_mismatches || stats.spec_mismatches || stats.plan_mismatches ? 1 : 0;
}
