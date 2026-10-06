#ifndef GS_RULES_H
#define GS_RULES_H
#include "gs_auto_eat.h"

/* VengeanceCast.java, Bait.java and PrayerKeeper.java: the small pure rules around the knockout decisions. */
typedef enum { GS_REFLECT_WORTH, GS_REFLECT_WEAK } GsReflect;
typedef enum { GS_REASON_NONE, GS_REASON_PLAN, GS_REASON_CASTABLE, GS_REASON_ANSWER } GsCastReason;

static GsCastReason gs_vengeance_reason(GsCast planned, const GsRebound* rebound, int has_attack, int attack_lands, GsPid first, GsReflect weapon, GsReflect thrown) {
    if (planned == GS_CAST_NOW) return GS_REASON_PLAN;
    if (first != GS_PID_LOCAL) return rebound->kind == GS_REBOUND_CASTABLE && weapon == GS_REFLECT_WORTH ? GS_REASON_CASTABLE : GS_REASON_NONE;
    return thrown == GS_REFLECT_WORTH && has_attack && spec_rebound_casts_before(rebound, attack_lands, first) ? GS_REASON_ANSWER : GS_REASON_NONE;
}

typedef enum { GS_BAIT_HOLD, GS_BAIT_OFF, GS_BAIT_NO_RIVAL, GS_BAIT_PID, GS_BAIT_HIS_VENGEANCE, GS_BAIT_OUR_VENGEANCE, GS_BAIT_HIS_ENERGY, GS_BAIT_OUR_PLAN,
    GS_BAIT_EAT, GS_BAIT_BUSY } GsBait;
typedef enum { GS_OURS_UP, GS_OURS_COOLDOWN, GS_OURS_CLICKED, GS_OURS_BLOCKED, GS_OURS_MAGIC_LEVEL, GS_OURS_CASTABLE } GsOurVengeance;

static GsBait gs_bait_decide(GsDecision eat, GsPid pid, GsVengeance his_vengeance, GsOurVengeance ours, int his_energy_high, int his_spec_cost, int has_plan,
    int our_energy, int engaged, int run_is_meal, int run_is_spec) {
    if (pid != GS_PID_LOCAL) return GS_BAIT_PID;
    if (his_vengeance != GS_VENGEANCE_DOWN) return GS_BAIT_HIS_VENGEANCE;
    if (ours != GS_OURS_CASTABLE) return GS_BAIT_OUR_VENGEANCE;
    if (his_energy_high < his_spec_cost) return GS_BAIT_HIS_ENERGY;
    if (our_energy < 50 || (engaged && !has_plan)) return GS_BAIT_OUR_PLAN;
    if (eat != GS_WAIT) return GS_BAIT_EAT;
    return run_is_meal || run_is_spec ? GS_BAIT_BUSY : GS_BAIT_HOLD;
}

typedef enum { GS_MEMORY_HELD, GS_MEMORY_DROPPED } GsMemoryMode;
typedef struct { GsMemoryMode mode; uint32_t prayers; } GsPrayerMemory;
typedef enum { GS_UPKEEP_HOLD, GS_UPKEEP_DRINK } GsUpkeep;
typedef enum { GS_ESCAPE_STAY, GS_ESCAPE_DISARM, GS_ESCAPE_TELEPORT } GsEscape;
enum { GS_WILDERNESS_TELEPORT_LIMIT = 20, GS_SPEC_TELEPORT_DELAY = 8, GS_WILDERNESS_MIN_X = 2944, GS_WILDERNESS_MAX_X = 3391, GS_WILDERNESS_BASE_Y = 3520,
    GS_WILDERNESS_MIN_Y = 3523, GS_WILDERNESS_MAX_Y = 3967, GS_WILDERNESS_BAND = 8 };

static GsPrayerMemory gs_prayer_observe(GsPrayerMemory memory, uint32_t active, int points) {
    if (points <= 0) return (GsPrayerMemory){GS_MEMORY_DROPPED, memory.prayers | active};
    if (memory.mode == GS_MEMORY_DROPPED && (active & memory.prayers) != memory.prayers) return memory;
    return (GsPrayerMemory){GS_MEMORY_HELD, active};
}

/* Prayers to turn on again in order: Protect Item first, then by ordinal. Returns the count. */
static int gs_prayer_reenable(GsPrayerMemory memory, uint32_t active, int points, int protect_item, int* out) {
    if (memory.mode == GS_MEMORY_HELD || points <= 0) return 0;
    uint32_t missing = memory.prayers & ~active;
    int n = 0;
    if (missing >> protect_item & 1) out[n++] = protect_item;
    for (int p = 0; p < 32; p++)
        if ((missing >> p & 1) && p != protect_item) out[n++] = p;
    return n;
}

static GsUpkeep gs_prayer_upkeep(int missing, int restores, int drink_ok) { return drink_ok && restores <= missing ? GS_UPKEEP_DRINK : GS_UPKEEP_HOLD; }

static int gs_wilderness_level(int x, int y) {
    int inside = x >= GS_WILDERNESS_MIN_X && x <= GS_WILDERNESS_MAX_X && y >= GS_WILDERNESS_MIN_Y && y <= GS_WILDERNESS_MAX_Y;
    return inside ? (y - GS_WILDERNESS_BASE_Y) / GS_WILDERNESS_BAND + 1 : 0;
}

static GsEscape gs_prayer_escape(int points, int doses_left, int wilderness_level, int blocked, int has_since_spec, int ticks_since_spec, int pvp_world, int armed) {
    if (points > 0 || doses_left || wilderness_level > GS_WILDERNESS_TELEPORT_LIMIT || blocked || (has_since_spec && ticks_since_spec < GS_SPEC_TELEPORT_DELAY)) return GS_ESCAPE_STAY;
    return pvp_world && armed ? GS_ESCAPE_DISARM : GS_ESCAPE_TELEPORT;
}
#endif
