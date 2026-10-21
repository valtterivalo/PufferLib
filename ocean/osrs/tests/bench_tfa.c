/* Env ns per team step: scripted actions precomputed outside the timed region, obs and masks for every agent. */
#include "tfa_scripted.h"
#include <time.h>

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

int main(int argc, char** argv) {
    int room = argc > 1 ? atoi(argv[1]) : 0, team = argc > 2 ? atoi(argv[2]) : 3, steps = argc > 3 ? atoi(argv[3]) : 200000;
    TfaState* s = calloc(1, sizeof(TfaState));
    TfaContext* ctx = calloc(1, sizeof(TfaContext));
    tfa_init_context((EncounterContext*)ctx);
    ctx->config.team_size = team;
    ctx->config.start_room = room;
    tfa_finalize_context((EncounterState*)s, (EncounterContext*)ctx);
    tfa_reset_typed(s, ctx, 7);
    float obs[TFA_NUM_OBS];
    unsigned char mask[TFA_ACTION_MASK_SIZE];
    int acts[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS];
    double step_ns = 0, obs_ns = 0, script_ns = 0;
    int episodes = 0;
    for (int i = 0; i < steps; i++) {
        double t0 = now_ns();
        for (int p = 0; p < TFA_MAX_PLAYERS; p++) tfa_heuristic_actions(s, p, acts + p * TFA_NUM_ACTION_HEADS);
        double t1 = now_ns();
        tfa_step_team(s, ctx, acts);
        if (s->episode_over) { tfa_reset_typed(s, ctx, 0); episodes++; }
        double t2 = now_ns();
        for (int p = 0; p < team; p++) {
            tfa_write_obs_for(s, ctx, p, obs);
            tfa_write_mask_for(s, ctx, p, mask);
        }
        double t3 = now_ns();
        script_ns += t1 - t0; step_ns += t2 - t1; obs_ns += t3 - t2;
    }
    printf("room %d team %d: step %.0f ns, obs+mask %.0f ns (%.0f per agent), scripted %.0f ns, episodes %d\n",
        room, team, step_ns / steps, obs_ns / steps, obs_ns / steps / team, script_ns / steps, episodes);
    return 0;
}
