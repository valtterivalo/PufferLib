#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    for (int pid = 0; pid < 2; pid++) {
        for (int attacker = 0; attacker < 2; attacker++) {
            riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
            context.self_play = 1;
            state.env.pid_holder = pid;
            for (int i = 0; i < 2; i++) {
                Player* p = &state.env.players[i];
                p->x = p->dest_x = FIGHT_AREA_BASE_X;
                p->y = p->dest_y = FIGHT_AREA_BASE_Y;
                p->attack_timer = -1;
                p->current_hitpoints = 99;
            }
            osrs_interaction_set(&state.env.players[attacker].interaction, 1 - attacker);
            int actions[2 * RF_HEADS] = {0};
            riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
            assert(state.env.players[0].x != state.env.players[1].x ||
                state.env.players[0].y != state.env.players[1].y);
            assert(state.env.players[attacker].attack_timer > 0);
        }
    }
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight overlap regression passed");
}
