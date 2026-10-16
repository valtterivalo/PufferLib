/* What each archetype and edge kit does under the script: 200 fights of up to 500 ticks against a scripted fighter in our live kit, per kit: his swings,
 * specials, the distinct weapons he swung, hits he deals, the share of fights he wins / loses / leaves by tab.
 * cc -std=gnu11 -O2 -I. -o /tmp/probe_riskfight_pool ocean/osrs/tests/probe_riskfight_pool.c -lm ; /tmp/probe_riskfight_pool [episodes] */
#include <stdio.h>
#include <stdlib.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

int main(int argc, char** argv) {
    int episodes = argc > 1 ? atoi(argv[1]) : 200;
    riskfight_init_context((EncounterContext*)&context);
    riskfight_put_int((EncounterState*)&state, (EncounterContext*)&context, "live_loadout", 1);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    printf("| kit | his swings | specials | weapons swung | his hits landed | his dmg dealt | we win | he wins | he tabs | we tab | timeout |\n|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++) {
        const RiskfightKit* k = &RISKFIGHT_POOL[i];
        if (k->kind == RF_KIT_EMPIRICAL) continue;
        double swings = 0, specs = 0, weapons = 0, hits = 0, dmg = 0;
        int win = 0, lose = 0, his_tab = 0, our_tab = 0, timeout = 0;
        for (int e = 0; e < episodes; e++) {
            context.opponent_pool = 0; context.opp_kit = NULL;
            context.loadout[1] = &RISKFIGHT_POOL_LOADOUT[i];
            riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 1 + (uint32_t)(e * 7919 + i * 104729));
            context.opp_kit = k;
            riskfight_kit_apply(&state, k, 0x9e3779b9u * (uint32_t)(e + 1));
            riskfight_observe_visible(&state, 0, 1);
            state.opponent_profile = &RISKFIGHT_HUMAN_PROFILES[e % RISKFIGHT_HUMAN_TRAIN_PROFILES];
            uint8_t seen[RF_KIT_WEAPONS + 2] = {0};
            int seen_count = 0;
            while (!state.env.episode_over) {
                int actions[2 * RF_HEADS] = {0};
                float obs[RF_OBS_SIZE];
                riskfight_write_observation(&state, 0, obs);
                riskfight_script(obs, &RISKFIGHT_HUMAN_PROFILES[(e + 3) % RISKFIGHT_HUMAN_TRAIN_PROFILES], state.script_seed[0], state.consume_ticks[0], actions);
                context.self_play = 0;
                riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
                const Player* him = &state.env.players[1];
                if (him->attack_style_this_tick != ATTACK_STYLE_NONE) {
                    swings++;
                    specs += him->used_special_this_tick;
                    uint8_t w = him->equipped[GEAR_SLOT_WEAPON];
                    int known = 0;
                    for (int j = 0; j < seen_count; j++) known |= seen[j] == w;
                    if (!known && seen_count < RF_KIT_WEAPONS + 2) { seen[seen_count++] = w; weapons++; }
                }
                const Player* us = &state.env.players[0];
                hits += us->hit_landed_this_tick;
                dmg += us->hit_damage;
            }
            win += state.outcome[0] == RISKFIGHT_KILL; lose += state.outcome[0] == RISKFIGHT_DEATH;
            his_tab += state.escaped[1]; our_tab += state.escaped[0]; timeout += state.outcome[0] == RISKFIGHT_TIMEOUT;
        }
        printf("| %s | %.1f | %.2f | %.2f | %.1f | %.0f | %.2f | %.2f | %.2f | %.2f | %.2f |\n", k->name, swings / episodes, specs / episodes, weapons / episodes, hits / episodes, dmg / episodes,
            (double)win / episodes, (double)lose / episodes, (double)his_tab / episodes, (double)our_tab / episodes, (double)timeout / episodes);
    }
    return 0;
}
