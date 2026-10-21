/* Invulnerable scripted raiders (teams of 2 to 4: a lone twisted bow does not out-damage the phase 4 soul split) run Ocellus through all four phases: the room must reach phase 4 and end CLEARED. */
#include "tfa_scripted.h"
#include "osrs_test_check.h"

int main(void) {
    int cleared = 0;
    for (int team = 2; team <= TFA_MAX_PLAYERS; team++) {
        TfaState* s = calloc(1, sizeof(TfaState));
        TfaContext* ctx = calloc(1, sizeof(TfaContext));
        tfa_init_context((EncounterContext*)ctx);
        ctx->config.team_size = team;
        ctx->config.start_room = TFA_ROOM_OCELLUS;
        ctx->config.room_max_ticks = 100000;
        tfa_finalize_context((EncounterState*)s, (EncounterContext*)ctx);
        tfa_reset_typed(s, ctx, 7u + (uint32_t)team);
        int acts[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS];
        int reached_p4 = 0;
        while (!s->episode_over) {
            for (int p = 0; p < TFA_MAX_PLAYERS; p++) tfa_scripted_actions(s, p, acts + p * TFA_NUM_ACTION_HEADS);
            for (int p = 0; p < team; p++) s->players[p].player.current_hitpoints = 10000;
            tfa_step_team(s, ctx, acts);
            reached_p4 |= s->room == TFA_ROOM_OCELLUS && s->boss.ocellus.phase == TFA_OCELLUS_P4;
        }
        printf("team %d: outcome %d ticks %d p4 %d\n", team, s->outcome, s->tick, reached_p4);
        CHECK("phase 4 reached", reached_p4);
        CHECK("room cleared", s->outcome == TFA_OUTCOME_CLEARED);
        cleared += s->outcome == TFA_OUTCOME_CLEARED;
        free(s);
        free(ctx);
    }
    return osrs_test_summary();
}
