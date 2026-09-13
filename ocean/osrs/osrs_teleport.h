#ifndef OSRS_TELEPORT_H
#define OSRS_TELEPORT_H

#define OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS 9

typedef enum {
    OSRS_TELEPORT_WORLD_STANDARD,
    OSRS_TELEPORT_WORLD_PVP,
} OsrsTeleportWorld;

typedef enum {
    OSRS_TELEPORT_COMBAT_UNRESTRICTED,
    OSRS_TELEPORT_COMBAT_PVP_AREA,
} OsrsTeleportCombatContext;

typedef struct {
    int blocked_until_tick;
} OsrsTeleportState;

static inline OsrsTeleportCombatContext osrs_teleport_combat_context(
    OsrsTeleportWorld world, int in_wilderness
) {
    return world == OSRS_TELEPORT_WORLD_PVP || in_wilderness ?
        OSRS_TELEPORT_COMBAT_PVP_AREA : OSRS_TELEPORT_COMBAT_UNRESTRICTED;
}

static inline OsrsTeleportState osrs_teleport_record_offensive_pvp_special(
    OsrsTeleportState state, OsrsTeleportCombatContext context, int tick
) {
    if (context == OSRS_TELEPORT_COMBAT_PVP_AREA)
        state.blocked_until_tick = tick + OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS;
    return state;
}

static inline int osrs_teleport_allowed(OsrsTeleportState state, int tick) {
    return tick >= state.blocked_until_tick;
}

#endif
