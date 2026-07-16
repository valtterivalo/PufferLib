/**
 * @file osrs_env_profile.h
 * @brief Shared gated per-bucket profiler machinery for the OSRS env bindings.
 *
 * Template header (no include guard): the includer defines the parameter macros and an
 * OSRS_ENV_PROFILE_SLOTS(X) x-macro of X(enum_slot, "label") entries, then includes this
 * file once. It emits the slot enum (terminated by the COUNT enumerator), the parallel
 * names table, the enabled/now_ms/add/mark helpers, and the three exported
 * env_profile_{count,name,read_reset_ms} entry points. Only the slot list differs per
 * env; the machinery lives here so the two copies cannot drift. The thin *_PROFILE_*
 * convenience macros stay per-env (their names cannot be token-pasted in a #define, and
 * MARK binds the caller's local timer/enabled variables).
 *
 * Required parameter macros (consumed and #undef'd below):
 *   OSRS_ENV_PROFILE_PREFIX     lowercase symbol prefix (e.g. colosseum)
 *   OSRS_ENV_PROFILE_COUNT      count enumerator token (e.g. COLO_PROF_COUNT)
 *   OSRS_ENV_PROFILE_SLOT_TYPE  enum type name (e.g. ColosseumProfileSlot)
 *   OSRS_ENV_PROFILE_ENV_VAR    gate env var string (e.g. "PUFFER_COLOSSEUM_PROFILE")
 *   OSRS_ENV_PROFILE_EXPORT     export attribute macro (e.g. COLOSSEUM_ENV_EXPORT)
 *   OSRS_ENV_PROFILE_SLOTS(X)   x-macro list of X(enum_slot, "label")
 */

#include <stdlib.h>
#include <time.h>

#define OSRS_ENV_PROFILE_CAT_(a, b) a##b
#define OSRS_ENV_PROFILE_CAT(a, b) OSRS_ENV_PROFILE_CAT_(a, b)
#define OSRS_ENV_PROFILE_FN(suffix) OSRS_ENV_PROFILE_CAT(OSRS_ENV_PROFILE_PREFIX, suffix)
#define OSRS_ENV_PROFILE_G(suffix) \
    OSRS_ENV_PROFILE_CAT(OSRS_ENV_PROFILE_CAT(g_, OSRS_ENV_PROFILE_PREFIX), suffix)

typedef enum {
#define OSRS_ENV_PROFILE_X(slot, label) slot,
    OSRS_ENV_PROFILE_SLOTS(OSRS_ENV_PROFILE_X)
#undef OSRS_ENV_PROFILE_X
    OSRS_ENV_PROFILE_COUNT,
} OSRS_ENV_PROFILE_SLOT_TYPE;

static int OSRS_ENV_PROFILE_G(_profile_enabled) = -1;
static double OSRS_ENV_PROFILE_G(_profile_ms)[OSRS_ENV_PROFILE_COUNT];

static const char* OSRS_ENV_PROFILE_G(_profile_names)[OSRS_ENV_PROFILE_COUNT] = {
#define OSRS_ENV_PROFILE_X(slot, label) label,
    OSRS_ENV_PROFILE_SLOTS(OSRS_ENV_PROFILE_X)
#undef OSRS_ENV_PROFILE_X
};

static int OSRS_ENV_PROFILE_FN(_profile_enabled)(void) {
    if (OSRS_ENV_PROFILE_G(_profile_enabled) < 0) {
        const char* text = getenv(OSRS_ENV_PROFILE_ENV_VAR);
        OSRS_ENV_PROFILE_G(_profile_enabled) =
            (text && text[0] && text[0] != '0') ? 1 : 0;
    }
    return OSRS_ENV_PROFILE_G(_profile_enabled);
}

static double OSRS_ENV_PROFILE_FN(_profile_now_ms)(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void OSRS_ENV_PROFILE_FN(_profile_add)(int slot, double ms) {
    if (slot < 0 || slot >= OSRS_ENV_PROFILE_COUNT) abort();
    #pragma omp atomic update
    OSRS_ENV_PROFILE_G(_profile_ms)[slot] += ms;
}

static void OSRS_ENV_PROFILE_FN(_profile_mark)(int enabled, double* last_ms, int slot) {
    if (!enabled) return;
    double now = OSRS_ENV_PROFILE_FN(_profile_now_ms)();
    OSRS_ENV_PROFILE_FN(_profile_add)(slot, now - *last_ms);
    *last_ms = now;
}

OSRS_ENV_PROFILE_EXPORT int OSRS_ENV_PROFILE_FN(_env_profile_count)(void) {
    return OSRS_ENV_PROFILE_FN(_profile_enabled)() ? OSRS_ENV_PROFILE_COUNT : 0;
}

OSRS_ENV_PROFILE_EXPORT const char* OSRS_ENV_PROFILE_FN(_env_profile_name)(int slot) {
    if (slot < 0 || slot >= OSRS_ENV_PROFILE_COUNT) abort();
    return OSRS_ENV_PROFILE_G(_profile_names)[slot];
}

OSRS_ENV_PROFILE_EXPORT double OSRS_ENV_PROFILE_FN(_env_profile_read_reset_ms)(int slot) {
    if (slot < 0 || slot >= OSRS_ENV_PROFILE_COUNT) abort();
    double value;
    #pragma omp atomic read
    value = OSRS_ENV_PROFILE_G(_profile_ms)[slot];
    #pragma omp atomic write
    OSRS_ENV_PROFILE_G(_profile_ms)[slot] = 0.0;
    return value;
}

#undef OSRS_ENV_PROFILE_CAT_
#undef OSRS_ENV_PROFILE_CAT
#undef OSRS_ENV_PROFILE_FN
#undef OSRS_ENV_PROFILE_G
#undef OSRS_ENV_PROFILE_PREFIX
#undef OSRS_ENV_PROFILE_COUNT
#undef OSRS_ENV_PROFILE_SLOT_TYPE
#undef OSRS_ENV_PROFILE_ENV_VAR
#undef OSRS_ENV_PROFILE_EXPORT
#undef OSRS_ENV_PROFILE_SLOTS
