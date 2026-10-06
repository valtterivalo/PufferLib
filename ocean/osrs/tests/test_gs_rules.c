/* Differential test of the C ports of VengeanceCast.reason, Bait.decide and PrayerKeeper against the Java oracle.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/test_gs_rules ocean/osrs/tests/test_gs_rules.c -lm
 *   /tmp/test_gs_rules [ORACLE.tap]
 *
 * The oracle is `./gradlew :gear-swapper:pureRulesOracle -Pout=FILE` in the Light repo: every enumerated or drawn input with the Java verdict. */
#include <stdio.h>
#include "../gearswapper/gs_rules.h"

static const char* const CAST[] = {"NOW", "KEEP"};
static const char* const PIDS[] = {"LOCAL", "OPPONENT", "UNKNOWN"};
static const char* const VENG[] = {"UNKNOWN", "UP", "DOWN", "MAYBE"};
static const char* const OURS[] = {"UP", "COOLDOWN", "CLICKED", "BLOCKED", "MAGIC_LEVEL", "CASTABLE"};
static const char* const OWNERS[] = {"NONE", "HOTKEY", "MEAL", "MEAL_SPEC", "SPEC", "UPKEEP", "ESCAPE", "CAST", "RESTORE", "DISARM", "PRAYER", "BAIT"};
static const char* const REASONS[] = {"NONE", "PLAN", "CASTABLE", "ANSWER"};
static const char* const BAITS[] = {"HOLD", "OFF", "NO_RIVAL", "PID", "HIS_VENGEANCE", "OUR_VENGEANCE", "HIS_ENERGY", "OUR_PLAN", "EAT", "BUSY"};
static const char* const ESCAPES[] = {"STAY", "DISARM", "TELEPORT"};

#define INDEX(table, name) index_of(table, sizeof(table) / sizeof(*(table)), name)
static int index_of(const char* const* table, size_t n, const char* name) {
    for (size_t i = 0; i < n; i++) if (!strcmp(table[i], name)) return (int)i;
    fprintf(stderr, "unknown name %s\n", name);
    abort();
}

static uint32_t mask_of(const char* csv) {
    uint32_t mask = 0;
    if (!strcmp(csv, "-")) return 0;
    for (const char* p = csv; *p;) {
        mask |= 1u << strtol(p, (char**)&p, 10);
        if (*p == ',') p++;
    }
    return mask;
}

static void list_of(const char* csv, int* out, int* n) {
    *n = 0;
    if (!strcmp(csv, "-")) return;
    for (const char* p = csv; *p;) {
        out[(*n)++] = (int)strtol(p, (char**)&p, 10);
        if (*p == ',') p++;
    }
}

int main(int argc, char** argv) {
    FILE* f = fopen(argc > 1 ? argv[1] : "ocean/osrs/tests/fixtures/rules_oracle.tap", "r");
    assert(f);
    char line[1024];
    long total = 0, bad = 0;
    long per[8] = {0};
    while (fgets(line, sizeof(line), f)) {
        char tag[128], a[128], b[128], c[128], d[128], e[128], g[128], h[128], i[128], oe[128], en[128], arrow[8], r1[128], r2[128];
        int ok = 0;
        total++;
        if (sscanf(line, "VC %127s %127s %127s %127s %127s %127s %127s -> %127s", a, b, c, d, e, g, h, r1) == 8) {
            GsRebound rebound = {!strcmp(b, "UP") ? GS_REBOUND_UP : !strcmp(b, "CASTABLE") ? GS_REBOUND_CASTABLE : GS_REBOUND_SPENT, atoi(c)};
            int has_attack = strcmp(d, "none") != 0;
            GsCastReason reason = gs_vengeance_reason((GsCast)INDEX(CAST, a), &rebound, has_attack, has_attack ? atoi(d) : 0, (GsPid)INDEX(PIDS, e),
                !strcmp(g, "WORTH") ? GS_REFLECT_WORTH : GS_REFLECT_WEAK, !strcmp(h, "WORTH") ? GS_REFLECT_WORTH : GS_REFLECT_WEAK);
            ok = reason == (GsCastReason)INDEX(REASONS, r1);
            per[0]++;
        } else if (sscanf(line, "BAIT %127s %127s %127s %127s %127s %127s %127s %127s %127s %127s -> %127s", a, b, c, d, e, g, h, oe, en, i, r1) == 11) {
            int owner = INDEX(OWNERS, i);
            GsBait verdict = gs_bait_decide((GsDecision)(!strcmp(a, "EAT_NOW") ? GS_EAT_NOW : !strcmp(a, "WAIT") ? GS_WAIT : GS_EXPOSED), (GsPid)INDEX(PIDS, b),
                (GsVengeance)INDEX(VENG, c), (GsOurVengeance)INDEX(OURS, d), atoi(e), atoi(g), atoi(h), atoi(oe), !strcmp(en, "INTERACTING"), owner == 2 || owner == 3, owner == 3 || owner == 4);
            ok = verdict == (GsBait)INDEX(BAITS, r1);
            per[1]++;
        } else if (sscanf(line, "PRW %127s %127s -> %127s", a, b, r1) == 3) {
            ok = gs_wilderness_level(atoi(a), atoi(b)) == atoi(r1);
            per[2]++;
        } else if (sscanf(line, "PRU %127s %127s %127s -> %127s", a, b, c, r1) == 4) {
            ok = gs_prayer_upkeep(atoi(a), atoi(b), !strcmp(c, "OK")) == (!strcmp(r1, "DRINK") ? GS_UPKEEP_DRINK : GS_UPKEEP_HOLD);
            per[3]++;
        } else if (sscanf(line, "PRE %127s %127s %127s %127s %127s %127s %127s -> %127s", a, b, c, d, e, g, h, r1) == 8) {
            int has_since = strcmp(e, "none") != 0;
            ok = gs_prayer_escape(atoi(a), !strcmp(b, "LEFT"), atoi(c), !strcmp(d, "BLOCKED"), has_since, has_since ? atoi(e) : 0, !strcmp(g, "PVP"), !strcmp(h, "ARMED"))
                == (GsEscape)INDEX(ESCAPES, r1);
            per[4]++;
        } else if (sscanf(line, "PRR %127s %127s %127s %127s %127s %7s %127s", a, r2, c, d, e, arrow, tag) == 7) {
            int expected[32], expected_n, got[32];
            list_of(tag, expected, &expected_n);
            GsPrayerMemory memory = {!strcmp(a, "HELD") ? GS_MEMORY_HELD : GS_MEMORY_DROPPED, mask_of(r2)};
            int n = gs_prayer_reenable(memory, mask_of(c), atoi(d), atoi(e), got);
            ok = n == expected_n && !memcmp(got, expected, (size_t)n * sizeof(int));
            per[5]++;
        } else if (sscanf(line, "PRO %127s %127s %127s %127s -> %127s %127s", a, r2, c, d, b, tag) == 6) {
            GsPrayerMemory memory = {!strcmp(a, "HELD") ? GS_MEMORY_HELD : GS_MEMORY_DROPPED, mask_of(r2)};
            GsPrayerMemory next = gs_prayer_observe(memory, mask_of(c), atoi(d));
            ok = next.mode == (!strcmp(b, "HELD") ? GS_MEMORY_HELD : GS_MEMORY_DROPPED) && next.prayers == mask_of(tag);
            per[6]++;
        } else {
            fprintf(stderr, "unparsed: %s", line);
            abort();
        }
        bad += !ok;
        if (!ok && bad < 6) printf("MISMATCH %s", line);
    }
    printf("%ld cases, %ld mismatches (VC %ld, Bait %ld, wilderness %ld, upkeep %ld, escape %ld, reenable %ld, observe %ld)\n", total, bad, per[0], per[1], per[2], per[3], per[4], per[5], per[6]);
    return bad != 0;
}
