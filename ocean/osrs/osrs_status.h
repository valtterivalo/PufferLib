#ifndef OSRS_STATUS_H
#define OSRS_STATUS_H

typedef enum {
    OSRS_AFFLICTION_NONE,
    OSRS_AFFLICTION_POISON,
    OSRS_AFFLICTION_VENOM,
} OsrsAfflictionKind;

typedef struct {
    OsrsAfflictionKind kind;
    int severity;
    int source;
    int damage_ticks;
    int poison_immunity_ticks;
    int venom_immunity_ticks;
    int burn_ticks[5];
    int burn_damage_ticks;
} OsrsStatusState;

typedef struct {
    int poison;
    int venom;
    int burn;
} OsrsStatusDamage;

typedef enum {
    OSRS_STATUS_CURE_ANTIPOISON,
    OSRS_STATUS_CURE_ANTIVENOM,
} OsrsStatusCure;

static inline int osrs_status_next_damage(const OsrsStatusState* status) {
    if (status->kind == OSRS_AFFLICTION_VENOM) return status->severity;
    return status->kind == OSRS_AFFLICTION_POISON ? (status->severity + 4) / 5 : 0;
}

static inline void osrs_status_apply_affliction(OsrsStatusState* status,
    OsrsAfflictionKind kind, int severity, int immune, int damage_ticks, int source) {
    if (immune || (kind == OSRS_AFFLICTION_VENOM ? status->venom_immunity_ticks
        : status->poison_immunity_ticks) > 0) return;
    if (status->kind == OSRS_AFFLICTION_VENOM) return;
    if (status->kind == OSRS_AFFLICTION_NONE) status->damage_ticks = damage_ticks;
    if (kind == OSRS_AFFLICTION_VENOM || severity > status->severity)
        status->severity = severity;
    status->kind = kind;
    status->source = source;
}

static inline void osrs_status_cure(OsrsStatusState* status, int poison_immunity,
    int venom_immunity, OsrsStatusCure cure) {
    if (status->kind == OSRS_AFFLICTION_VENOM && cure == OSRS_STATUS_CURE_ANTIPOISON) {
        status->severity *= 5;
        status->kind = OSRS_AFFLICTION_POISON;
    } else {
        status->kind = OSRS_AFFLICTION_NONE;
        status->severity = 0;
        status->damage_ticks = 0;
    }
    if (poison_immunity > status->poison_immunity_ticks)
        status->poison_immunity_ticks = poison_immunity;
    if (venom_immunity > status->venom_immunity_ticks)
        status->venom_immunity_ticks = venom_immunity;
}

static inline void osrs_status_apply_burn(OsrsStatusState* status, int duration) {
    for (int i = 0; i < 5; i++) {
        if (status->burn_ticks[i] != 0) continue;
        status->burn_ticks[i] = duration;
        if (status->burn_damage_ticks == 0) status->burn_damage_ticks = 4;
        return;
    }
}

static inline OsrsStatusDamage osrs_status_tick(OsrsStatusState* status) {
    OsrsStatusDamage damage = {0};
    if (status->poison_immunity_ticks > 0) status->poison_immunity_ticks--;
    if (status->venom_immunity_ticks > 0) status->venom_immunity_ticks--;
    if (status->kind != OSRS_AFFLICTION_NONE && --status->damage_ticks == 0) {
        int hit = osrs_status_next_damage(status);
        if (status->kind == OSRS_AFFLICTION_VENOM) {
            damage.venom = hit;
            if (status->severity < 20) status->severity += 2;
        } else {
            damage.poison = hit;
            if (--status->severity == 0) status->kind = OSRS_AFFLICTION_NONE;
        }
        status->damage_ticks = status->kind == OSRS_AFFLICTION_NONE ? 0 : 30;
    }
    if (status->burn_damage_ticks > 0) {
        int due = --status->burn_damage_ticks == 0;
        int remaining = 0;
        for (int i = 0; i < 5; i++) {
            if (status->burn_ticks[i] == 0) continue;
            if (due) damage.burn++;
            remaining += --status->burn_ticks[i] > 0;
        }
        if (due && remaining > 0) status->burn_damage_ticks = 4;
        if (remaining == 0) status->burn_damage_ticks = 0;
    }
    return damage;
}

#endif
