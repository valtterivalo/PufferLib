#ifndef GS_RULES_H
#define GS_RULES_H
#include "gs_auto_eat.h"

/* VengeanceCast.java and PrayerKeeper.java: the small pure rules around the knockout decisions. */
typedef enum { GS_REFLECT_SPEC, GS_REFLECT_WORTH, GS_REFLECT_WEAK } GsReflect;
typedef enum { GS_ENERGY_COVERS, GS_ENERGY_SHORT } GsEnergyCover;
typedef enum { GS_REASON_NONE, GS_REASON_PLAN, GS_REASON_CASTABLE, GS_REASON_ANSWER } GsCastReason;

static GsCastReason gs_vengeance_reason(GsCast planned, const GsRebound* rebound, int has_attack, int attack_lands, GsPid first, GsReflect weapon, GsReflect thrown,
    GsEnergyCover energy) {
    if (planned == GS_CAST_NOW) return GS_REASON_PLAN;
    if (first != GS_PID_LOCAL) return rebound->kind == GS_REBOUND_CASTABLE && weapon == GS_REFLECT_WORTH ? GS_REASON_CASTABLE : GS_REASON_NONE;
    int worth = thrown == GS_REFLECT_SPEC || (thrown == GS_REFLECT_WORTH && energy == GS_ENERGY_SHORT);
    return worth && has_attack && spec_rebound_casts_before(rebound, attack_lands, first) ? GS_REASON_ANSWER : GS_REASON_NONE;
}

/* VengeanceCast.upkeep: restore a drained magic level while a cast is coming and his energy covers a spec */
static int gs_vengeance_restore(int drained, int coming, GsEnergyCover energy) { return drained && coming && energy == GS_ENERGY_COVERS; }

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
