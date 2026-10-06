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
#include "../gearswapper/gs_infer.h"
#include "../gearswapper/gs_java_items.h"

enum { LINE = 1 << 16, TOKENS = 256, COMBAT_LEVEL = 126 };

static const char* const WEAPON_NAMES[GS_WEAPONS] = {"VOIDWAKER", "GRANITE_MAUL", "ARMADYL_GODSWORD", "ANCIENT_GODSWORD", "DRAGON_CLAWS", "DRAGON_DAGGER",
    "ABYSSAL_DAGGER", "VESTAS_LONGSWORD", "STATIUS_WARHAMMER", "DRAGON_WARHAMMER", "DARK_BOW", "BALLISTA", "ARMADYL_CROSSBOW", "ZARYTE_CROSSBOW",
    "TOXIC_BLOWPIPE", "MAGIC_SHORTBOW_I", "MAGIC_SHORTBOW", "VOLATILE_NIGHTMARE_STAFF", "DHAROKS_GREATAXE", "SOULREAPER_AXE", "OTHER"};
static const char* const STYLE_NAMES[3] = {"MELEE", "RANGED", "MAGIC"};
static const char* const PID_NAMES[3] = {"LOCAL", "OPPONENT", "UNKNOWN"};
static const char* const VENGEANCE_NAMES[4] = {"UNKNOWN", "UP", "DOWN", "MAYBE"};
static const char* const BAR_NAMES[3] = {"READ", "FRESH", "BEHIND_A_MEAL"};
static const char* const RECOIL_NAMES[2] = {"UNSEEN", "SEEN"};
static const char* const PHASE_NAMES[4] = {"UNKNOWN", "CAST", "PROCCED", "DISARMED"};
static const char* const DAMAGE_NAMES[5] = {"STAB", "SLASH", "CRUSH", "RANGED", "MAGIC"};

static int split(char* line, char** tokens) {
    int n = 0;
    for (char* t = strtok(line, " \n"); t && n < TOKENS; t = strtok(NULL, " \n")) tokens[n++] = t;
    return n;
}

static GsHeadIcon head_of(const char* name) {
    return !strcmp(name, "NONE") ? GS_HEAD_NONE : !strcmp(name, "MELEE") ? GS_HEAD_MELEE : !strcmp(name, "RANGED") ? GS_HEAD_RANGED : !strcmp(name, "MAGIC") ? GS_HEAD_MAGIC : GS_HEAD_OTHER;
}

static void parse_state(char** t, int n, GsClient* c) {
    int i = 2;
    memset(c, 0, sizeof(*c));
    c->hitpoints = atoi(t[i++]); c->attack = atoi(t[i++]); c->strength = atoi(t[i++]); c->defence = atoi(t[i++]);
    c->ranged = atoi(t[i++]); c->magic = atoi(t[i++]); c->prayer = atoi(t[i++]);
    c->prayers = strtoull(t[i++], NULL, 10);
    c->spec_energy_varp = atoi(t[i++]); c->sa_attack = atoi(t[i++]); c->com_mode = atoi(t[i++]);
    c->lx = atoi(t[i++]); c->ly = atoi(t[i++]); c->local_interacting = atoi(t[i++]);
    for (int k = 0; k < GS_WORN_SLOTS; k++) c->worn[k] = atoi(t[i++]);
    c->player_count = atoi(t[i++]);
    assert(c->player_count <= GS_MAX_PLAYERS);
    for (int k = 0; k < c->player_count; k++) { c->player_id[k] = atoi(t[i++]); c->player_interacting[k] = atoi(t[i++]); }
    c->tracked = atoi(t[i++]);
    if (c->tracked >= 0) {
        c->tracked_interacting = atoi(t[i++]);
        c->ox = atoi(t[i++]); c->oy = atoi(t[i++]); c->ratio = atoi(t[i++]); c->scale = atoi(t[i++]); c->oweapon = atoi(t[i++]);
        c->overhead = head_of(t[i++]);
        c->equipment_count = atoi(t[i++]);
        assert(c->equipment_count <= GS_MAX_EQUIPMENT);
        for (int k = 0; k < c->equipment_count; k++) c->equipment[k] = atoi(t[i++]);
    } else c->ratio = c->scale = -1;
    assert(i == n);
}

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
    int unseen[GS_UNSEEN_LIMIT];
    memcpy(unseen, s->unseen, sizeof(int) * (size_t)s->unseen_count);
    qsort(unseen, (size_t)s->unseen_count, sizeof(int), cmp_int);
    char list[1024] = "[";
    int unique = 0;
    for (int i = 0; i < s->unseen_count; i++) {
        if (i && unseen[i] == unseen[i - 1]) continue;
        sprintf(list + strlen(list), "%s%d", unique++ ? ", " : "", unseen[i]);
    }
    strcat(list, "]");
    char swing[96];
    if (s->has_opponent_swing) sprintf(swing, "Optional[%d..%d@%d]", s->opponent_swing.least, s->opponent_swing.most, s->opponent_swing.lands);
    else sprintf(swing, "Optional.empty");
    char arsenal[2048] = "";
    int order[GS_ARSENAL_SIZE];
    for (int i = 0; i < s->arsenal_count; i++) order[i] = i;
    for (int i = 1; i < s->arsenal_count; i++)
        for (int j = i; j > 0 && s->arsenal_id[order[j - 1]] > s->arsenal_id[order[j]]; j--) { int x = order[j]; order[j] = order[j - 1]; order[j - 1] = x; }
    for (int k = 0; k < s->arsenal_count; k++) {
        const GsArmament* a = &s->arsenal[order[k]];
        char spec[40];
        format_optional_int(spec, a->has_spec, a->spec);
        sprintf(arsenal + strlen(arsenal), "%d=%s,%s,%d,%s,%d,%d;", s->arsenal_id[order[k]], WEAPON_NAMES[a->weapon], STYLE_NAMES[a->style], a->normal, spec, a->interval, a->attack_bonus);
    }
    char ate[16], reach[16], food[16];
    sprintf(ate, "%d", s->opponent_ate); sprintf(reach, "%d", s->opponent_in_reach_since); sprintf(food, "%d", s->opponent_hard_food_ready);
    sprintf(out, "pid %s %s | timer %s | spec %s typical %s possible %s from %d | veng %s %s..%d | local %s | hp %s ate %s reach %s bar %s dmg %d unseen %s food %s recoil %s swing %s soul %d@%d arsenal %s",
        PID_NAMES[s->pid.owner], age, timer, s->opponent.spec.broken ? "BROKEN_PRIOR" : "TYPICAL", typical, possible, s->opponent.spec.anchor,
        PHASE_NAMES[s->opponent.vengeance.phase], earliest, s->opponent.vengeance.latest, local, hp, ate, reach, BAR_NAMES[s->bar], s->damage_since_bar, list, food,
        RECOIL_NAMES[s->recoil], swing, s->soul_stacks.held, s->soul_stacks.tick, arsenal);
    sprintf(out + strlen(out), " | reflect %s", s->swing_reflect == GS_REFLECT_WORTH ? "WORTH" : "WEAK");
    if (gs_living_rival(s) && s->tick == tick)
        sprintf(out + strlen(out), " %s cost %d", gs_weapon_reflect(s, tick) == GS_REFLECT_WORTH ? "WORTH" : "WEAK", gs_opponent_spec_cost(s));
}

static void format_attack(char* out, const char* tag, const GsAttack* a) {
    sprintf(out + strlen(out), "%s %s %d %d %d %d %.17g\n", tag, STYLE_NAMES[a->style], a->least, a->most, a->cost, a->interval, a->accuracy);
}

static void format_situation(const GsEatSituation* s, char* out) {
    out[0] = 0;
    sprintf(out + strlen(out), "S %d %d %s %d %s %d %d %d %.17g %.17g %s %.17g %.17g %.17g %d %s %s %d\n", s->tick, s->hitpoints, PID_NAMES[s->pid], s->distance,
        STYLE_NAMES[s->local_style], s->local_most, s->local_normal, s->local_interval, s->local_opening_accuracy, s->local_accuracy,
        s->offence == GS_ENGAGED ? "ENGAGED" : "IDLE", s->tolerance, s->eat_cost, s->stakes, s->floor, VENGEANCE_NAMES[s->vengeance], s->recoil_seen ? "SEEN" : "UNSEEN", s->energy_high);
    if (s->opponent_timer.seen) sprintf(out + strlen(out), "OT 1 %d\n", s->opponent_timer.next);
    else sprintf(out + strlen(out), "OT 0 0\n");
    if (s->local_timer.seen) sprintf(out + strlen(out), "LT 1 %d %d\n", s->local_timer.next, s->local_timer.earliest);
    else sprintf(out + strlen(out), "LT 0 0 %d\n", s->tick + 1);
    sprintf(out + strlen(out), "OH %d %d %d\n", s->opponent_hp.low, s->opponent_hp.high, s->opponent_hp.max);
    for (int i = 0; i < s->attack_count; i++) format_attack(out, "OA", &s->attacks[i]);
    if (s->has_maul) format_attack(out, "GM", &s->maul);
    for (int i = 0; i < s->hidden_count; i++) {
        sprintf(out + strlen(out), "HID %d ", s->hidden[i].is_maul);
        char one[256] = "";
        format_attack(one, "", &s->hidden[i].attack);
        one[strlen(one) - 1] = 0;
        sprintf(out + strlen(out), "%s %.17g\n", one + 1, s->hidden[i].probability);
    }
    if (s->has_swing) sprintf(out + strlen(out), "OSW %d %d %d %.17g\n", s->swing.lands, s->swing.least, s->swing.most, s->swing.accuracy);
    if (s->has_local_swing) sprintf(out + strlen(out), "LSW %d %d %d %.17g\n", s->local_swing.lands, s->local_swing.least, s->local_swing.most, s->local_swing.accuracy);
    if (s->warning.since == GS_NONE) strcat(out, "W none");
    else sprintf(out + strlen(out), "W %d", s->warning.since);
    int sorted[16];
    memcpy(sorted, s->warning.unseen, sizeof(int) * (size_t)s->warning.unseen_count);
    qsort(sorted, (size_t)s->warning.unseen_count, sizeof(int), cmp_int);
    for (int i = 0; i < s->warning.unseen_count; i++) sprintf(out + strlen(out), " %d", sorted[i]);
    strcat(out, "\n");
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
    int sorted[16];
    memcpy(sorted, s->warning.unseen, sizeof(int) * (size_t)s->warning.unseen_count);
    qsort(sorted, (size_t)s->warning.unseen_count, sizeof(int), cmp_int);
    for (int i = 0; i < s->warning.unseen_count; i++) sprintf(out + strlen(out), " %d", sorted[i]);
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
        int n = split(one, t);
        if (!strcmp(t[0], "SS")) sprintf(out + strlen(out), "SS %s %s %s %s %s %s %s %s\n", t[1], t[2], t[3], t[4], t[5], t[6], t[9], t[10]);
        else if (!strcmp(t[0], "UW")) {
            if (n == 1) strcat(out, "UW\n");
            else sprintf(out + strlen(out), "UW %s %d\n", t[2], atoi(t[3]) - 1);
        }
        else if (!strcmp(t[0], "IN") || !strcmp(t[0], "W")) strcat(out, line), strcat(out, "\n");
    }
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
    int nx = split(x, tx), ny = split(y, ty);
    if (nx != ny) return 0;
    for (int i = 0; i < nx; i++) if (!same_token(tx[i], ty[i])) return 0;
    return 1;
}

typedef struct { long ticks, situations, situation_mismatches, inference_lines, inference_mismatches, shown, spec_records, spec_mismatches; } Stats;

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

static void register_items(void) {
    for (size_t i = 0; i < sizeof(GS_JAVA_ITEMS) / sizeof(*GS_JAVA_ITEMS); i++) {
        const GsJavaItemRow* r = &GS_JAVA_ITEMS[i];
        int stats[14] = {r->astab, r->aslash, r->acrush, r->arange, r->amagic, r->dstab, r->dslash, r->dcrush, r->drange, r->dmagic, r->str, r->rstr, 0, 0};
        gs_item_register(r->id, r->name, r->has_stats, stats, r->mdmg, r->aspeed);
        const GsItem* item = gs_item(r->id);
        assert((int)item->weapon == r->weapon);
        assert(item->style == r->style);
        assert(gs_base_speed(r->id) == r->base_speed);
    }
}

static void strip_tags(const char* in, char* out) {
    int skipping = 0;
    for (; *in; in++) {
        if (*in == '<') skipping = 1;
        else if (*in == '>') skipping = 0;
        else if (!skipping) *out++ = *in;
    }
    *out = 0;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    register_items();
    FILE* f = fopen(argv[1], "r");
    assert(f);
    static char line[LINE], copy[LINE], record[LINE * 4];
    GsInfer inf;
    GsClient c = {0};
    Stats stats = {0};
    gs_infer_init(&inf);
    int hp_xp_known = 0, hp_xp = 0, recordings = 0, tracker_mismatch = 0;
    char expected_inference[LINE] = "";
    int pending_tick = -1, capturing = 0, captured = 0;
    char mine_situation[LINE * 2] = "", mine_spec[LINE * 2] = "", spec_record[LINE * 4] = "";
    int have_spec = 0, spec_captured = 0;
    int have_mine = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "RECORDING", 9)) {
            recordings++;
            gs_infer_init(&inf);
            hp_xp_known = 0;
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
                        have_spec = 0;
                    }
                } else if (capturing == 1) captured = 1;
                capturing = 0;
            }
            else if (capturing == 3) {}
            else if (capturing == 1 && (!strncmp(line, "F ", 2) || !strncmp(line, "M ", 2) || !strncmp(line, "V ", 2))) {}
            else if (capturing == 2 && (!strncmp(line, "PL ", 3) || !strncmp(line, "AC ", 3) || !strncmp(line, "D ", 2))) {}
            else strcat(capturing == 1 ? record : spec_record, line);
            continue;
        }
        if (!strncmp(line, "SS ", 3)) {
            if (!spec_captured) { capturing = 2; strcpy(spec_record, line); }
            else capturing = 3;
            continue;
        }
        strcpy(copy, line);
        char* t[TOKENS];
        int n = split(copy, t);
        if (n == 0) continue;
        const char* tag = t[0];
        if (!strcmp(tag, "S") && n > 3 && isalpha((unsigned char)t[3][0])) {
            if (!captured) { capturing = 1; strcpy(record, line); }
            else capturing = 3;
            continue;
        }
        if (!strcmp(tag, "S")) {
            parse_state(t, n, &c);
            if (c.tracked != inf.tracker_current) tracker_mismatch++;
            inf.c = c;
            continue;
        }
        if (!strcmp(tag, "K")) {
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
        }
        else if (!strcmp(tag, "I")) gs_on_interacting(&inf, atoi(t[2]), atoi(t[3]));
        else if (!strcmp(tag, "A")) gs_on_animation(&inf, atoi(t[1]), atoi(t[2]), atoi(t[3]));
        else if (!strcmp(tag, "H")) gs_on_hitsplat(&inf, atoi(t[1]), atoi(t[2]), atoi(t[3]), atoi(t[4]), !strcmp(t[5], "true"));
        else if (!strcmp(tag, "O")) { if (!strcmp(t[3], "true")) gs_on_overhead_text(&inf, atoi(t[1]), atoi(t[2])); }
        else if (!strcmp(tag, "G")) gs_on_graphic(&inf, atoi(t[1]), atoi(t[2]));
        else if (!strcmp(tag, "C")) {
            if (!strcmp(t[2], "GAMEMESSAGE") || !strcmp(t[2], "SPAM")) {
                char* text = strstr(line, t[2]) + strlen(t[2]) + 1;
                char clean[LINE];
                strip_tags(text, clean);
                clean[strcspn(clean, "\n")] = 0;
                gs_on_chat(&inf, clean);
            }
        }
        else if (!strcmp(tag, "X")) {
            int xp = atoi(t[2]);
            int gained = xp - (hp_xp_known ? hp_xp : xp);
            hp_xp_known = 1;
            hp_xp = xp;
            if (gained > 0) gs_on_hitpoints_gain(&inf, atoi(t[1]), gained, COMBAT_LEVEL);
        }
        else if (!strcmp(tag, "N")) { int units[3] = {atoi(t[2]), atoi(t[3]), atoi(t[4])}; gs_on_inventory(&inf, atoi(t[1]), units); }
        else if (!strcmp(tag, "T")) {
            int tick = atoi(t[1]);
            stats.ticks++;
            gs_on_game_tick(&inf, tick);
            have_mine = 0;
            if (inf.timed >= 0 && inf.c.ratio != 0) {
                GsEatSituation s;
                gs_eater_observe(&inf, tick);
                gs_eat_situation(&inf, tick, inf.c.local_interacting >= 0 ? GS_ENGAGED : GS_IDLE, 0.01, 0.01, 1.0, &s);
                format_situation(&s, mine_situation);
                have_mine = 1;
            }
            pending_tick = tick;
            record[0] = 0;
            captured = 0;
        }
        else if (!strcmp(tag, "INF")) {
            char mine[LINE];
            format_inference(&inf, pending_tick, mine);
            char theirs[LINE];
            strcpy(theirs, line + 4 + strlen(t[1]) + 1);
            theirs[strcspn(theirs, "\n")] = 0;
            stats.inference_lines++;
            if (!same_line(mine, theirs)) {
                stats.inference_mismatches++;
                if (stats.shown < 12) { fprintf(stderr, "INF tick %d mismatch\n  C:    %s\n  Java: %s\n", pending_tick, mine, theirs); stats.shown++; }
            }
            if (have_mine) {
                stats.situations++;
                char theirs_situation[LINE * 2];
                if (record[0] == 0) { stats.situation_mismatches++; if (stats.shown < 12) { fprintf(stderr, "tick %d: Java built no situation\n", pending_tick); stats.shown++; } }
                else {
                    strcpy(theirs_situation, record);
                    if (compare_record(mine_situation, theirs_situation, pending_tick, &stats, "SITUATION")) stats.situation_mismatches++;
                }
            } else if (record[0] != 0) { stats.situation_mismatches++; if (stats.shown < 12) { fprintf(stderr, "tick %d: Java built a situation C did not\n", pending_tick); stats.shown++; } }
            record[0] = 0;
            (void)expected_inference;
        }
        else if (pending_tick >= 0 && record[0] == 0 && !strcmp(tag, "S")) {}
    }
    printf("recordings %d, ticks %ld, tracker mismatches %d\n", recordings, stats.ticks, tracker_mismatch);
    printf("inference lines %ld, mismatches %ld\n", stats.inference_lines, stats.inference_mismatches);
    printf("situations %ld, mismatches %ld\n", stats.situations, stats.situation_mismatches);
    printf("spec inputs %ld, mismatches %ld\n", stats.spec_records, stats.spec_mismatches);
    return stats.inference_mismatches || stats.situation_mismatches || stats.spec_mismatches ? 1 : 0;
}
