#ifndef OSRS_HEALTH_BAR_H
#define OSRS_HEALTH_BAR_H
#include <assert.h>
#include <stdint.h>

enum { OSRS_PLAYER_HEALTH_BAR_SCALE = 30 };

typedef enum {
    OSRS_HEALTH_BAR_KNOWN,
    OSRS_HEALTH_BAR_MISSING,
    OSRS_HEALTH_BAR_INVALID,
} OsrsHealthBarRangeKind;

typedef struct {
    OsrsHealthBarRangeKind kind;
    int lower, upper;
} OsrsHealthBarRange;

/** Current HP to capped server health ratio, with zero reserved for death. */
static inline int osrs_health_bar_ratio(int hp, int base_hp, int scale) {
    assert(hp >= 0 && base_hp > 0 && scale > 0);
    if (hp == 0) return 0;
    int64_t ratio = 1 + (int64_t)(scale - 1) * hp / base_hp;
    return ratio < scale ? (int)ratio : scale;
}

/** Server ratio to inclusive HP bounds, with explicit full-bar overheal ceiling. */
static inline OsrsHealthBarRange osrs_health_bar_range(
    int ratio, int scale, int base_hp, int overheal_cap
) {
    if (base_hp <= 0 || overheal_cap < base_hp || ratio < -1 || scale < -1)
        return (OsrsHealthBarRange){OSRS_HEALTH_BAR_INVALID, 0, 0};
    if (ratio == -1 || scale == -1)
        return (OsrsHealthBarRange){OSRS_HEALTH_BAR_MISSING, 0, 0};
    if (scale == 0 || ratio > scale)
        return (OsrsHealthBarRange){OSRS_HEALTH_BAR_INVALID, 0, 0};
    if (ratio == 0) return (OsrsHealthBarRange){OSRS_HEALTH_BAR_KNOWN, 0, 0};
    if (scale == 1) return (OsrsHealthBarRange){OSRS_HEALTH_BAR_KNOWN, 1, overheal_cap};
    int lower = ratio == 1 ? 1 : (int)(((int64_t)base_hp * (ratio - 1) + scale - 2) / (scale - 1));
    int upper = ratio == scale ? overheal_cap : (int)(((int64_t)base_hp * ratio - 1) / (scale - 1));
    if (lower > upper) return (OsrsHealthBarRange){OSRS_HEALTH_BAR_INVALID, 0, 0};
    return (OsrsHealthBarRange){OSRS_HEALTH_BAR_KNOWN, lower, upper};
}
#endif
