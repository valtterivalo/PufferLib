#include "../osrs/encounters/encounter_riskfight.h"

int main(void) {
    RiskfightContext context;
    riskfight_init_context((EncounterContext*)&context);
    RiskfightState* state = (RiskfightState*)riskfight_create();
    riskfight_finalize_context((EncounterState*)state, (EncounterContext*)&context);
    context.self_play = 1;
    puts("agent,opponent,fights,kills,deaths,escapes,mutual_deaths,net_stake_per_fight,mean_ticks");
    for (int agent = RISKFIGHT_TRADER; agent <= RISKFIGHT_AGGRESSIVE; agent++) {
        for (int opponent = RISKFIGHT_TRADER; opponent <= RISKFIGHT_AGGRESSIVE; opponent++) {
            int outcomes[5] = {0}, ticks = 0;
            for (int seed = 1; seed <= 16; seed++) {
                riskfight_reset((EncounterState*)state, (EncounterContext*)&context, (uint32_t)seed);
                while (!state->env.episode_over) {
                    float obs[RF_OBS_SIZE];
                    int actions[2 * RF_HEADS];
                    riskfight_write_observation(state, 0, obs);
                    riskfight_script(obs, (RiskfightOpponent)agent, actions);
                    riskfight_write_observation(state, 1, obs);
                    riskfight_script(obs, (RiskfightOpponent)opponent, actions + RF_HEADS);
                    riskfight_step((EncounterState*)state, (EncounterContext*)&context, actions);
                }
                outcomes[state->outcome[0]]++;
                ticks += state->env.tick;
            }
            printf("%d,%d,16,%d,%d,%d,%d,%.4f,%.2f\n", agent, opponent,
                outcomes[RISKFIGHT_KILL], outcomes[RISKFIGHT_DEATH], outcomes[RISKFIGHT_ESCAPE],
                outcomes[RISKFIGHT_MUTUAL_DEATH],
                (outcomes[RISKFIGHT_KILL] - outcomes[RISKFIGHT_DEATH]) / 16.0f, ticks / 16.0f);
        }
    }
    riskfight_destroy((EncounterState*)state);
    riskfight_destroy_context((EncounterContext*)&context);
}
