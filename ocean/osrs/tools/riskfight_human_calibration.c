/* Plays training human profiles against each other and prints the round statistics that
 * scripts/mine_riskfight_behavior.py reports as recorded_rounds, for side by side comparison.
 *
 *   cc -std=gnu11 -O2 -Iocean/osrs -Iocean -I. -Isrc -Iraylib-5.5_macos/include \
 *      -o /tmp/riskfight_human_calibration ocean/osrs/tools/riskfight_human_calibration.c -lm
 *   /tmp/riskfight_human_calibration 4000
 */

#include <stdlib.h>
#include "ocean/osrs/encounters/encounter_riskfight.h"

static int compare_int(const void* a, const void* b) {
    return *(const int*)a - *(const int*)b;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    int rounds = atoi(argv[1]);
    assert(rounds > 0);
    RiskfightContext context;
    riskfight_init_context((EncounterContext*)&context);
    RiskfightState* s = (RiskfightState*)riskfight_create();
    riskfight_finalize_context((EncounterState*)s, (EncounterContext*)&context);
    context.self_play = 1;
    int* lengths = malloc(rounds * sizeof(*lengths));
    assert(lengths);
    long deaths = 0, short_rounds = 0, fighter_ticks = 0, hits = 0, zeros = 0, big = 0, damage = 0;
    for (int r = 0; r < rounds; r++) {
        riskfight_reset((EncounterState*)s, (EncounterContext*)&context, (uint32_t)r + 1);
        const RiskfightProfile* profiles[2];
        for (int i = 0; i < 2; i++)
            profiles[i] = riskfight_pick_profile(RISKFIGHT_HUMAN, osrs_lowbias32(s->script_seed[i] ^ 0xA511E9B3u));
        while (!s->env.episode_over) {
            int actions[2 * RF_HEADS];
            for (int i = 0; i < 2; i++) {
                float obs[RF_OBS_SIZE];
                riskfight_write_observation(s, i, obs);
                riskfight_script(obs, profiles[i], s->script_seed[i], s->consume_ticks[i], actions + i * RF_HEADS);
            }
            riskfight_step((EncounterState*)s, (EncounterContext*)&context, actions);
            for (int i = 0; i < 2; i++) {
                const Player* p = &s->env.players[i];
                fighter_ticks++;
                if (!p->hit_landed_this_tick) continue;
                hits++;
                damage += p->hit_damage;
                zeros += p->hit_damage == 0;
                big += p->hit_damage >= 40;
            }
        }
        lengths[r] = s->env.tick;
        deaths += s->outcome[0] != RISKFIGHT_ESCAPE;
        short_rounds += s->env.tick < 50;
    }
    qsort(lengths, rounds, sizeof(*lengths), compare_int);
    printf("{\"rounds\": %d, \"death_share\": %.3f, \"under_50_ticks_share\": %.3f, "
        "\"length_p10_p50_p90\": [%d, %d, %d], \"damage_per_fighter_tick\": %.3f, "
        "\"zero_hit_share\": %.3f, \"hit_40_plus_share\": %.3f}\n",
        rounds, deaths / (double)rounds, short_rounds / (double)rounds,
        lengths[rounds / 10], lengths[rounds / 2], lengths[rounds * 9 / 10],
        damage / (double)fighter_ticks, zeros / (double)hits, big / (double)hits);
    free(lengths);
    riskfight_destroy((EncounterState*)s);
    riskfight_destroy_context((EncounterContext*)&context);
}
