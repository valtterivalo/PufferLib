#ifndef GS_MACHINES_H
#define GS_MACHINES_H
/* Ports of the Light inference state machines (platform/interaction net.light.local.combat): Pid.State, AttackTimer.State,
 * SpecEnergy.State, Vengeance.State, OpponentInference, SoulStacks, with the Consumption table. Same names, same arithmetic. */
#include "gs_core.h"

typedef enum { GS_KIND_ATTACK, GS_KIND_INSTANT_SPEC, GS_KIND_EAT, GS_KIND_VENGEANCE_CAST, GS_KIND_OTHER } GsKind;

/* Pid.java */
typedef struct { int has_animation, animation, has_hit, hit; } GsPairing;
typedef struct { int tick, amount; } GsStruck;
typedef struct {
    GsPid owner;
    int observed_tick;
    GsPairing opponent_attacks, local_attacks;
    GsStruck local, opponent;
} GsPidState;


static GsPidState gs_pid_unknown(void) {
    GsPidState s = {.owner = GS_PID_UNKNOWN};
    s.local = s.opponent = (GsStruck){INT_MIN, 0};
    return s;
}

typedef struct { GsPairing pairing; int has_lead; GsLead lead; } GsPaired;

static GsPaired gs_pairing_animated(GsPairing p, int tick) {
    if (p.has_hit && p.hit == tick) return (GsPaired){{0}, 1, GS_LEAD_ATTACKER};
    GsPairing next = p;
    next.has_animation = 1;
    next.animation = tick;
    return (GsPaired){next, 0, 0};
}

static GsPaired gs_pairing_hit_landed(GsPairing p, int tick) {
    if (p.has_animation && tick == p.animation) return (GsPaired){{0}, 1, GS_LEAD_ATTACKER};
    if (p.has_animation && tick == p.animation + 1) return (GsPaired){{0}, 1, GS_LEAD_TARGET};
    GsPairing next = p;
    next.has_hit = 1;
    next.hit = tick;
    return (GsPaired){next, 0, 0};
}

static int gs_struck_echoed_by(GsStruck s, int tick, int amount) {
    return (tick == s.tick || tick == s.tick + 1) && (amount == s.amount / 10 + 1 || amount == s.amount * 3 / 4);
}

static GsPidState gs_pid_after_opponent_pairing(GsPidState s, GsPaired paired, int tick, GsStruck local) {
    GsPid seen = paired.has_lead ? (paired.lead == GS_LEAD_ATTACKER ? GS_PID_OPPONENT : GS_PID_LOCAL) : s.owner;
    GsPidState out = s;
    out.owner = seen;
    out.observed_tick = paired.has_lead ? tick : s.observed_tick;
    out.opponent_attacks = paired.pairing;
    out.local = local;
    return out;
}

static GsPidState gs_pid_after_local_pairing(GsPidState s, GsPaired paired, int tick, GsStruck opponent) {
    GsPid seen = paired.has_lead ? (paired.lead == GS_LEAD_ATTACKER ? GS_PID_LOCAL : GS_PID_OPPONENT) : s.owner;
    GsPidState out = s;
    out.owner = seen;
    out.observed_tick = paired.has_lead ? tick : s.observed_tick;
    out.local_attacks = paired.pairing;
    out.opponent = opponent;
    return out;
}

static GsPidState gs_pid_opponent_attacked(GsPidState s, int tick) {
    return gs_pid_after_opponent_pairing(s, gs_pairing_animated(s.opponent_attacks, tick), tick, s.local);
}

static GsPidState gs_pid_hit_on_local(GsPidState s, int tick, int amount) {
    if (amount == 0 || gs_struck_echoed_by(s.opponent, tick, amount)) return s;
    return gs_pid_after_opponent_pairing(s, gs_pairing_hit_landed(s.opponent_attacks, tick), tick, (GsStruck){tick, amount});
}

static GsPidState gs_pid_local_attacked(GsPidState s, int tick) {
    return gs_pid_after_local_pairing(s, gs_pairing_animated(s.local_attacks, tick), tick, s.opponent);
}

static GsPidState gs_pid_hit_on_opponent(GsPidState s, int tick, int amount) {
    if (amount == 0 || gs_struck_echoed_by(s.local, tick, amount)) return s;
    return gs_pid_after_local_pairing(s, gs_pairing_hit_landed(s.local_attacks, tick), tick, (GsStruck){tick, amount});
}


/* Consumption.java */
typedef struct { int delay, lowest, highest; } GsConsumptionRow;
static const GsConsumptionRow GS_CONSUMPTIONS[4] = {{0, INT_MIN, 16}, {2, 15, 20}, {3, 19, 30}, {5, 34, INT_MAX}};
enum { GS_HEAL_ERROR = 6 };

typedef struct { int shortest, longest; } GsDelays;

static GsDelays gs_consumptions_all(void) { return (GsDelays){0, 5}; }

static GsDelays gs_consumption_matching(int estimated_heal) {
    GsDelays out = {INT_MAX, INT_MIN};
    for (int i = 0; i < 4; i++) {
        long lowest = GS_CONSUMPTIONS[i].lowest, highest = GS_CONSUMPTIONS[i].highest;
        if (estimated_heal + GS_HEAL_ERROR >= lowest && estimated_heal - GS_HEAL_ERROR <= highest) {
            out.shortest = imin(out.shortest, GS_CONSUMPTIONS[i].delay);
            out.longest = imax(out.longest, GS_CONSUMPTIONS[i].delay);
        }
    }
    return out;
}

static int gs_consumption_hp(int ratio, int scale) { return (ratio * GS_ASSUMED_MAX_HP + scale / 2) / scale; }

/* AttackTimer.java */
typedef struct { int seen, earliest, latest; } GsTimerState;
typedef struct { int present, shortest, longest; } GsSpeed;
typedef enum { GS_READING_UNKNOWN, GS_READING_COOLDOWN, GS_READING_MAYBE, GS_READING_READY } GsReading;

static GsReading gs_timer_reading(GsTimerState s, int tick) {
    if (!s.seen) return GS_READING_UNKNOWN;
    if (tick >= s.latest) return GS_READING_READY;
    return tick >= s.earliest ? GS_READING_MAYBE : GS_READING_COOLDOWN;
}

static int gs_timer_until_earliest(GsTimerState s, int tick) {
    assert(s.seen);
    return imax(0, s.earliest - tick);
}

static int gs_timer_exact_next(GsTimerState s, int* out) {
    if (s.seen && s.earliest == s.latest) { *out = s.latest; return 1; }
    return 0;
}

static GsTimerState gs_timer_eat(GsTimerState s, GsDelays possible) {
    if (!s.seen) return s;
    return (GsTimerState){1, s.earliest + possible.shortest, s.latest + possible.longest};
}

static GsTimerState gs_timer_observe(GsTimerState s, GsKind kind, int tick, GsSpeed speed) {
    switch (kind) {
        case GS_KIND_ATTACK:
            return speed.present ? (GsTimerState){1, tick + speed.shortest, tick + speed.longest} : (GsTimerState){0, 0, 0};
        case GS_KIND_EAT: return gs_timer_eat(s, gs_consumptions_all());
        default: return s;
    }
}

static int gs_next_attack(GsTimerState s, int tick) { return tick + 1 + gs_timer_until_earliest(s, tick + 1); }

/* SpecEnergy.java */
enum { GS_ENERGY_FULL = 100, GS_ENERGY_REGEN = 10, GS_ENERGY_REGEN_TICKS = 50, GS_ENERGY_SURGE = 25, GS_ENERGY_SURGE_TICKS = 500 };
typedef struct { int low, high; } GsEnergyRange;
enum { GS_ENERGY_SURGE_DELAY = 6, GS_ENERGY_LANDINGS = 16 };
typedef struct { int anchor; GsEnergyRange typical, possible; int broken; int landing_count, landings[GS_ENERGY_LANDINGS]; } GsSpecEnergy;

static int gs_ceil_div(int dividend, int divisor) { return (dividend + divisor - 1) / divisor; }

static GsSpecEnergy gs_energy_full(int tick) {
    GsEnergyRange full = {GS_ENERGY_FULL, GS_ENERGY_FULL};
    return (GsSpecEnergy){tick, full, full, 0};
}

static int gs_energy_landed(const GsSpecEnergy* s, int tick) {
    int landed = 0;
    for (int i = 0; i < s->landing_count; i++) landed += s->landings[i] <= tick;
    return landed;
}

static GsEnergyRange gs_energy_typical(const GsSpecEnergy* s, int tick) {
    assert(tick >= s->anchor);
    int elapsed = tick - s->anchor;
    int surged = gs_energy_landed(s, tick) * GS_ENERGY_SURGE;
    return (GsEnergyRange){imin(GS_ENERGY_FULL, s->typical.low + elapsed / GS_ENERGY_REGEN_TICKS * GS_ENERGY_REGEN + surged),
        imin(GS_ENERGY_FULL, s->typical.high + gs_ceil_div(elapsed, GS_ENERGY_REGEN_TICKS) * GS_ENERGY_REGEN + surged)};
}

static GsEnergyRange gs_energy_possible(const GsSpecEnergy* s, int tick) {
    assert(tick >= s->anchor);
    int elapsed = tick - s->anchor;
    int surged = gs_energy_landed(s, tick) * GS_ENERGY_SURGE;
    return (GsEnergyRange){imin(GS_ENERGY_FULL, s->possible.low + elapsed / GS_ENERGY_REGEN_TICKS * GS_ENERGY_REGEN + surged),
        imin(GS_ENERGY_FULL, s->possible.high + 2 * gs_ceil_div(elapsed, GS_ENERGY_REGEN_TICKS) * GS_ENERGY_REGEN
            + gs_ceil_div(elapsed, GS_ENERGY_SURGE_TICKS) * GS_ENERGY_SURGE + surged)};
}

static GsEnergyRange gs_energy_range(const GsSpecEnergy* s, int tick) {
    return s->broken ? gs_energy_possible(s, tick) : gs_energy_typical(s, tick);
}

static GsEnergyRange gs_energy_after_spec(GsEnergyRange before, int cost) {
    if (before.high < cost) return (GsEnergyRange){0, GS_ENERGY_FULL - cost};
    return (GsEnergyRange){imax(before.low, cost) - cost, before.high - cost};
}

static GsSpecEnergy gs_energy_spec(GsSpecEnergy s, int tick, int cost) {
    GsEnergyRange typical = gs_energy_typical(&s, tick);
    GsEnergyRange possible = gs_energy_possible(&s, tick);
    int broken = typical.high < cost ? 1 : s.broken;
    GsSpecEnergy out = {tick, gs_energy_after_spec(typical, cost), gs_energy_after_spec(possible, cost), broken, 0, {0}};
    for (int i = 0; i < s.landing_count; i++) if (s.landings[i] > tick) out.landings[out.landing_count++] = s.landings[i];
    return out;
}

static GsSpecEnergy gs_energy_surge(GsSpecEnergy s, int tick) {
    assert(s.landing_count < GS_ENERGY_LANDINGS);
    s.landings[s.landing_count++] = tick + GS_ENERGY_SURGE_DELAY;
    return s;
}

/* Vengeance.java */
enum { GS_VENGEANCE_COOLDOWN = 50 };
typedef enum { GS_VPHASE_UNKNOWN, GS_VPHASE_CAST, GS_VPHASE_PROCCED, GS_VPHASE_DISARMED } GsVengeancePhase;
typedef struct { GsVengeancePhase phase; int has_earliest, earliest, latest, proc, blind; } GsVengeanceState;

static GsVengeanceState gs_vengeance_cast(int tick) {
    return (GsVengeanceState){GS_VPHASE_CAST, 1, tick + GS_VENGEANCE_COOLDOWN, tick + GS_VENGEANCE_COOLDOWN, 0};
}

static GsVengeanceState gs_vengeance_cast_after(GsVengeanceState s, int tick);
static GsVengeance gs_vengeance_reading(GsVengeanceState s, int tick) {
    switch (s.phase) {
        case GS_VPHASE_UNKNOWN: return GS_VENGEANCE_UNKNOWN;
        case GS_VPHASE_CAST: return GS_VENGEANCE_UP;
        case GS_VPHASE_PROCCED:
            if (s.has_earliest && tick < s.earliest) return GS_VENGEANCE_DOWN;
            if (s.has_earliest && s.earliest == s.latest && tick >= s.latest && s.blind == 0) return GS_VENGEANCE_DOWN;
            return tick < s.latest ? GS_VENGEANCE_MAYBE : GS_VENGEANCE_UP;
        case GS_VPHASE_DISARMED: return GS_VENGEANCE_DOWN;
    }
    abort();
}

/* Vengeance.cast(State, tick): a cast seen on the tick of a proc leaves the cooldown running from the cast. */
static GsVengeanceState gs_vengeance_cast_after(GsVengeanceState s, int tick) {
    if (s.phase == GS_VPHASE_PROCCED && s.proc == tick) return (GsVengeanceState){GS_VPHASE_PROCCED, 1, tick + GS_VENGEANCE_COOLDOWN, tick + GS_VENGEANCE_COOLDOWN, tick};
    return gs_vengeance_cast(tick);
}

static GsVengeanceState gs_vengeance_stacked(GsVengeanceState s, int tick) {
    if (s.phase == GS_VPHASE_PROCCED && s.has_earliest && tick >= s.earliest) s.blind++;
    return s;
}

static GsVengeanceState gs_vengeance_procced(GsVengeanceState s, int tick) {
    switch (s.phase) {
        case GS_VPHASE_CAST: return (GsVengeanceState){GS_VPHASE_PROCCED, s.has_earliest, s.earliest, s.latest, tick};
        case GS_VPHASE_PROCCED: return (GsVengeanceState){GS_VPHASE_PROCCED, 1, s.proc + GS_VENGEANCE_COOLDOWN, tick + GS_VENGEANCE_COOLDOWN, tick};
        default: return (GsVengeanceState){GS_VPHASE_PROCCED, 0, 0, tick + GS_VENGEANCE_COOLDOWN, tick};
    }
}

static GsVengeanceState gs_vengeance_unprocced(GsVengeanceState s, int hit_tick) {
    static const GsVengeanceState disarmed = {GS_VPHASE_DISARMED, 0, 0, 0, 0};
    switch (s.phase) {
        case GS_VPHASE_CAST: return hit_tick > s.earliest - GS_VENGEANCE_COOLDOWN ? disarmed : s;
        case GS_VPHASE_PROCCED:
            return s.proc >= hit_tick || gs_vengeance_reading(s, hit_tick) == GS_VENGEANCE_DOWN ? s : disarmed;
        default: return disarmed;
    }
}

/* SoulStacks.java */
enum { GS_BEHEAD_ANIMATION = 10173, GS_SOUL_MOST = 5, GS_SOUL_DECAY_TICKS = 50 };
typedef struct { int held, tick; } GsSoulStacks;
static const GsSoulStacks GS_NO_STACKS = {0, INT_MIN};
static int gs_soul_at(GsSoulStacks s, int now) { return s.held == 0 ? 0 : imax(0, s.held - (now - s.tick) / GS_SOUL_DECAY_TICKS); }
static GsSoulStacks gs_soul_swung(GsSoulStacks s, int now) { return (GsSoulStacks){imin(GS_SOUL_MOST, gs_soul_at(s, now) + 1), now}; }
static GsSoulStacks gs_soul_beheaded(int now) { return (GsSoulStacks){0, now}; }

/* OpponentInference.java */
enum { GS_SPEAR_WALL_TICKS = 8 };
typedef struct { GsTimerState before; int eats; } GsPending;
typedef struct {
    GsTimerState timer;
    GsSpecEnergy spec;
    int has_hp, hp;
    int has_pending;
    GsPending pending;
    GsVengeanceState vengeance;
    int immune_through;
} GsOpponentInference;

static GsOpponentInference gs_inference_tracked(int tick) {
    return (GsOpponentInference){.spec = gs_energy_full(tick), .immune_through = INT_MIN};
}

static GsOpponentInference gs_inference_animated(GsOpponentInference s, GsKind kind, int spec_cost, int spear_wall, int tick, GsSpeed speed) {
    GsOpponentInference out = s;
    if (spear_wall) out.immune_through = tick + GS_SPEAR_WALL_TICKS - 1;
    if (spec_cost > 0) out.spec = gs_energy_spec(s.spec, tick, spec_cost);
    out.timer = gs_timer_observe(s.timer, kind, tick, speed);
    if (kind == GS_KIND_VENGEANCE_CAST) out.vengeance = gs_vengeance_cast_after(s.vengeance, tick);
    if (kind == GS_KIND_ATTACK) out.has_pending = 0;
    else if (kind == GS_KIND_EAT) {
        out.has_pending = 1;
        out.pending = s.has_pending ? (GsPending){s.pending.before, s.pending.eats + 1} : (GsPending){s.timer, 1};
    }
    return out;
}

static GsOpponentInference gs_inference_revealed(GsOpponentInference s, int hp, int damage) {
    GsOpponentInference out = s;
    if (s.has_pending && s.pending.eats == 1 && s.has_hp) {
        int heal = hp - s.hp + damage;
        out.timer = gs_timer_eat(s.pending.before, gs_consumption_matching(heal));
    }
    out.has_hp = 1;
    out.hp = hp;
    out.has_pending = 0;
    return out;
}

static GsOpponentInference gs_inference_veng_procced(GsOpponentInference s, int tick) {
    s.vengeance = gs_vengeance_procced(s.vengeance, tick);
    return s;
}

static GsOpponentInference gs_inference_stacked(GsOpponentInference s, int tick) {
    s.vengeance = gs_vengeance_stacked(s.vengeance, tick);
    return s;
}

static GsOpponentInference gs_inference_surged(GsOpponentInference s, int tick) {
    s.spec = gs_energy_surge(s.spec, tick);
    return s;
}

static GsOpponentInference gs_inference_veng_cast(GsOpponentInference s, int tick) {
    s.vengeance = gs_vengeance_cast_after(s.vengeance, tick);
    return s;
}

static GsOpponentInference gs_inference_veng_unprocced(GsOpponentInference s, int hit_tick) {
    s.vengeance = gs_vengeance_unprocced(s.vengeance, hit_tick);
    return s;
}
#endif
