#include "tfa_scripted.h"
#include "osrs_test_check.h"

static void run_team(int team_size, int episodes) {
    TfaState* s = calloc(1, sizeof(TfaState));
    TfaContext* ctx = calloc(1, sizeof(TfaContext));
    tfa_init_context((EncounterContext*)ctx);
    ctx->config.team_size = team_size;
    tfa_finalize_context((EncounterState*)s, (EncounterContext*)ctx);
    int clears = 0, wipes = 0, timeouts = 0;
    long ticks = 0;
    float obs[TFA_NUM_OBS];
    unsigned char mask[TFA_ACTION_MASK_SIZE];
    for (int e = 0; e < episodes; e++) {
        tfa_reset_typed(s, ctx, 1000 + e);
        int acts[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS];
        while (!s->episode_over) {
            for (int p = 0; p < TFA_MAX_PLAYERS; p++) {
                tfa_scripted_actions(s, p, acts + p * TFA_NUM_ACTION_HEADS);
                if (p >= team_size) continue;
                tfa_write_mask_for(s, ctx, p, mask);
                int off = 0;
                for (int h = 0; h < TFA_NUM_ACTION_HEADS; h++) {
                    int a = acts[p * TFA_NUM_ACTION_HEADS + h];
                    if (!mask[off + a]) acts[p * TFA_NUM_ACTION_HEADS + h] = 0;
                    off += TFA_ACTION_DIMS[h];
                }
            }
            tfa_step_team(s, ctx, acts);
            for (int p = 0; p < team_size; p++) tfa_write_obs_for(s, ctx, p, obs);
        }
        if (getenv("TFA_DEBUG") && e < 3) {
            const TfaNpc* g = &s->npcs[0];
            printf("  ep %d tick %d outcome %d phase %d kd %d enraged %d hp %d/%d shield %d/%d",
                e, s->tick, s->outcome, s->boss.guardian.phase, s->boss.guardian.knockdowns,
                s->boss.guardian.enraged, g->hp, g->max_hp, g->shield, g->max_shield);
            for (int p = 0; p < team_size; p++)
                printf(" | p%d dealt %.0f taken %.0f", p, s->players[p].damage_dealt_total, s->players[p].damage_taken_total);
            printf("\n");
        }
        ticks += s->tick;
        clears += s->outcome == TFA_OUTCOME_CLEARED;
        wipes += s->outcome == TFA_OUTCOME_WIPED;
        timeouts += s->outcome == TFA_OUTCOME_TIMEOUT;
    }
    printf("team %d: clears %d wipes %d timeouts %d mean ticks %.1f\n",
        team_size, clears, wipes, timeouts, (double)ticks / episodes);
    CHECK("every episode ends with an outcome", clears + wipes + timeouts == episodes);
    free(s);
    free(ctx);
}

int main(void) {
    for (int team = 1; team <= TFA_MAX_PLAYERS; team++) run_team(team, 50);
    return osrs_test_summary();
}
