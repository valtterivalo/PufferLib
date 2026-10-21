/* Random legal actions on every head for every implemented room and team size; with -fsanitize=undefined it hunts UB.
   Also checks that one seed replays to the identical state. */
#include "tfa_scripted.h"

static uint64_t state_hash(const TfaState* s) {
    const unsigned char* b = (const unsigned char*)s;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < sizeof(*s); i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

static uint64_t run(int room, int team, uint32_t seed, int steps) {
    TfaState* s = calloc(1, sizeof(TfaState));
    TfaContext* ctx = calloc(1, sizeof(TfaContext));
    tfa_init_context((EncounterContext*)ctx);
    ctx->config.team_size = team;
    ctx->config.start_room = room;
    tfa_finalize_context((EncounterState*)s, (EncounterContext*)ctx);
    tfa_reset_typed(s, ctx, seed);
    uint32_t rng = seed;
    float obs[TFA_NUM_OBS];
    unsigned char mask[TFA_ACTION_MASK_SIZE];
    int acts[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS] = {0};
    for (int i = 0; i < steps; i++) {
        for (int p = 0; p < team; p++) {
            tfa_write_obs_for(s, ctx, p, obs);
            tfa_write_mask_for(s, ctx, p, mask);
            int off = 0;
            for (int h = 0; h < TFA_NUM_ACTION_HEADS; h++) {
                int legal[64], n = 0;
                for (int a = 0; a < TFA_ACTION_DIMS[h]; a++) if (mask[off + a]) legal[n++] = a;
                assert(n > 0 && n <= 64);
                acts[p * TFA_NUM_ACTION_HEADS + h] = legal[encounter_rand_int(&rng, n)];
                off += TFA_ACTION_DIMS[h];
            }
        }
        tfa_step_team(s, ctx, acts);
        if (s->episode_over) tfa_reset_typed(s, ctx, 0);
    }
    uint64_t h = state_hash(s);
    free(s);
    free(ctx);
    return h;
}

int main(void) {
    static const int rooms[] = { TFA_ROOM_GUARDIAN, TFA_ROOM_XARTH, TFA_ROOM_BORK };
    for (size_t r = 0; r < sizeof(rooms) / sizeof(*rooms); r++)
        for (int team = 1; team <= TFA_MAX_PLAYERS; team++) {
            uint64_t a = run(rooms[r], team, 11u + (uint32_t)team, 20000);
            uint64_t b = run(rooms[r], team, 11u + (uint32_t)team, 20000);
            printf("room %d team %d hash %016llx %s\n", rooms[r], team, (unsigned long long)a, a == b ? "replays" : "DIVERGES");
            assert(a == b);
        }
    return 0;
}
