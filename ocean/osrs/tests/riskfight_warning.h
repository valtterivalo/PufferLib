#ifndef RISKFIGHT_WARNING_H
#define RISKFIGHT_WARNING_H
#include "riskfight_tape.h"

/* Port of AutoEater.opponentWarned from the Light repo: the tick his hitpoints came within our next hit, or we
 * wielded the spec weapon that reaches them if later, absent once he has eaten since. */
enum { WARNING_NONE = -1000000, UNSEEN_HEAL = 8, FOOD_CAP_TICKS = 16, FIGHTING_OVERHEAL = 115, OVERHEALED = 121, ASSUMED_MAX_HP = 99,
    SMALLEST_HEAL = 16 };
enum { WARNING_UNSEEN = 64 };
typedef enum { BAR_READ, BAR_FRESH, BAR_BEHIND_A_MEAL } BarState;

typedef struct {
    int low, high;
    int in_reach_since, wielded_weapon, wielded_since, spec_wielded_since, ate, struck_tick, damage_since_bar;
    BarState bar;
    int unseen[WARNING_UNSEEN], unseen_count;
} Warning;

static void warning_animated(Warning* w, int tick) {
    assert(w->unseen_count < WARNING_UNSEEN);
    w->unseen[w->unseen_count++] = tick;
}

static Warning warning_start(void) {
    return (Warning){.low = 1, .high = ASSUMED_MAX_HP, .in_reach_since = WARNING_NONE, .wielded_weapon = -1, .wielded_since = WARNING_NONE,
        .spec_wielded_since = WARNING_NONE, .ate = WARNING_NONE, .struck_tick = WARNING_NONE};
}

static void warning_ate(Warning* w, int tick) {
    int low = w->low + SMALLEST_HEAL;
    w->low = low < ASSUMED_MAX_HP ? low : ASSUMED_MAX_HP;
    w->high = ASSUMED_MAX_HP;
    w->bar = BAR_BEHIND_A_MEAL;
    w->ate = tick;
}

static void warning_hitsplat(Warning* w) { w->bar = BAR_FRESH; }

static void warning_struck(Warning* w, int tick, int amount) {
    w->struck_tick = tick;
    w->damage_since_bar += amount;
}

static int warning_bar_low(int ratio, int scale) {
    return ratio == 0 ? 0 : ratio == 1 ? 1 : (ASSUMED_MAX_HP * (ratio - 1) + scale - 2) / (scale - 1);
}

static int warning_bar_high(int ratio, int scale) {
    if (ratio == 0) return 0;
    int high = (ASSUMED_MAX_HP * ratio - 1) / (scale - 1);
    return high < ASSUMED_MAX_HP ? high : ASSUMED_MAX_HP;
}

/** One GameTick: the bar as shown (ratio -1 when none), our weapon, whether it has a spec, and the reach of our next hit. */
static void warning_observe(Warning* w, int tick, int ratio, int scale, int weapon, int has_spec, int reach) {
    if (ratio >= 0 && w->bar != BAR_BEHIND_A_MEAL) {
        int low = warning_bar_low(ratio, scale), high = warning_bar_high(ratio, scale);
        if (w->bar == BAR_FRESH) {
            if (low - (w->high - w->damage_since_bar) >= UNSEEN_HEAL) w->ate = tick - 1;
            int kept = 0;
            for (int i = 0; i < w->unseen_count; i++) if (w->unseen[i] >= tick) w->unseen[kept++] = w->unseen[i];
            w->unseen_count = kept;
            w->damage_since_bar = 0;
            w->bar = BAR_READ;
        }
        int overheal = w->struck_tick >= tick - FOOD_CAP_TICKS ? FIGHTING_OVERHEAL : OVERHEALED;
        if (ratio == scale) { w->low = low; w->high = overheal; }
        else { w->low = low; w->high = high; }
    }
    if (weapon != w->wielded_weapon) {
        w->wielded_weapon = weapon;
        w->wielded_since = tick;
    }
    w->spec_wielded_since = has_spec ? w->wielded_since : WARNING_NONE;
    w->in_reach_since = w->high > reach ? WARNING_NONE : w->in_reach_since <= w->ate ? tick : w->in_reach_since;
}

/** The warning tick, or WARNING_NONE. */
static int warning_since(const Warning* w) {
    int warned = w->in_reach_since > w->spec_wielded_since ? w->in_reach_since : w->spec_wielded_since;
    return w->in_reach_since != WARNING_NONE && warned > w->ate ? warned : WARNING_NONE;
}
#endif
