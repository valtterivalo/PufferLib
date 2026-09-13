#ifndef OSRS_ENTITY_PRIORITY_H
#define OSRS_ENTITY_PRIORITY_H

#include <assert.h>
#include <stdint.h>

typedef enum {
    OSRS_PRIORITY_FIXED,
    OSRS_PRIORITY_STANDARD,
    OSRS_PRIORITY_PVP_WORLD,
} OsrsPriorityPolicy;

typedef struct {
    uint32_t rank;
    int shuffle_ticks;
} OsrsEntityPriority;

typedef uint32_t (*OsrsPriorityRandomU32)(uint32_t* state);

static inline int osrs_entity_priority_interval(OsrsPriorityPolicy policy,
    uint32_t* rng, OsrsPriorityRandomU32 draw_u32) {
    switch (policy) {
        case OSRS_PRIORITY_FIXED: return 0;
        case OSRS_PRIORITY_STANDARD: return 100 + (int)(draw_u32(rng) % 51);
        case OSRS_PRIORITY_PVP_WORLD: return 40 + (int)(draw_u32(rng) % 21);
    }
    assert(0 && "unknown priority policy");
    return 0;
}

/** Independent random ranks approximate the undocumented server rank distribution. */
static inline void osrs_entity_priority_init(OsrsEntityPriority* priority,
    OsrsPriorityPolicy policy, uint32_t* rng, OsrsPriorityRandomU32 draw_u32) {
    priority->rank = draw_u32(rng);
    priority->shuffle_ticks = osrs_entity_priority_interval(policy, rng, draw_u32);
}

static inline void osrs_entity_priority_tick(OsrsEntityPriority* priority,
    OsrsPriorityPolicy policy, uint32_t* rng, OsrsPriorityRandomU32 draw_u32) {
    if (policy == OSRS_PRIORITY_FIXED) return;
    assert(priority->shuffle_ticks > 0);
    if (--priority->shuffle_ticks == 0)
        osrs_entity_priority_init(priority, policy, rng, draw_u32);
}

static inline int osrs_entity_priority_compare(const OsrsEntityPriority* left,
    int left_index, const OsrsEntityPriority* right, int right_index) {
    if (left->rank != right->rank)
        return left->rank < right->rank ? -1 : 1;
    return (left_index > right_index) - (left_index < right_index);
}

#endif
