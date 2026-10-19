/* The Dharok script on both seats (the opponents the nets train against, fighting each other as the spectated fighters do), written in the spectator.duels format
 * (riskfight_duel_writer.h) for scripts/measure_risk_ledger.py: the same reader on real Dharok v Dharok duels and on the sim's opponents against their like.
 *
 *   cc -std=gnu11 -O2 -I. -o probe_script_duels ocean/osrs/tests/probe_script_duels.c -lm
 *   probe_script_duels FIGHTS SEED OUT
 *
 * Each fight: both seats a pool kit holding the greataxe (by pool weight), a human profile with the Dharok rules (dharok_eating) and a drawn Dharok persona, the
 * fight begun from a recorded start (riskfight_start_draw): the dh harness of FINDINGS' real-against-sim comparisons. OUT.names: `id script<persona> script<persona>`. */
#include "../encounters/encounter_riskfight.h"
#include "riskfight_duel_writer.h"

static int has_axe(const RiskfightKit* k) {
    for (int i = 0; i < k->weapon_count; i++) if (k->weapons[i] == ITEM_DHAROKS_GREATAXE) return 1;
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 4) { fprintf(stderr, "usage: probe_script_duels FIGHTS SEED OUT\n"); return 2; }
    int fights = atoi(argv[1]);
    uint32_t seed = (uint32_t)atoi(argv[2]);
    char path[512];
    snprintf(path, sizeof(path), "%s.names", argv[3]);
    FILE* out = fopen(argv[3], "w");
    FILE* names = fopen(path, "w");
    if (!out || !names) { fprintf(stderr, "probe_script_duels: can't write %s\n", argv[3]); return 2; }
    int dk[RF_POOL_MAX], ndk = 0;
    float wsum = 0;
    for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++)
        if (has_axe(&RISKFIGHT_POOL[i])) { dk[ndk++] = i; wsum += RISKFIGHT_POOL[i].weight; }
    if (ndk == 0) { fprintf(stderr, "probe_script_duels: no pool kit holds the greataxe\n"); return 2; }
    static RiskfightState st, pre;
    static RiskfightContext ctx;
    riskfight_init_context((EncounterContext*)&ctx);
    riskfight_init_state((EncounterState*)&st, (EncounterContext*)&ctx);
    ctx.self_play = 1; ctx.opponent_pool = 1; ctx.keep_opp_kit = 1; ctx.opp_driven = 1;
    riskfight_finalize_context((EncounterState*)&st, (EncounterContext*)&ctx);
    uint32_t rng = seed * 2654435761u + 1;
    long ticks = 0;
    for (int f = 0; f < fights; f++) {
        int pick[2];
        for (int s = 0; s < 2; s++) {
            rng = osrs_lowbias32(rng + 0x9e3779b9u);
            float at = (rng >> 8) / 16777216.0f * wsum, sum = 0;
            pick[s] = dk[ndk - 1];
            for (int i = 0; i < ndk; i++) { sum += RISKFIGHT_POOL[dk[i]].weight; if (at < sum) { pick[s] = dk[i]; break; } }
        }
        ctx.loadout[0] = &RISKFIGHT_POOL_LOADOUT[pick[0]];
        ctx.opp_kit = &RISKFIGHT_POOL[pick[1]];
        rng = osrs_lowbias32(rng + 7); ctx.opp_roll = rng;
        rng = osrs_lowbias32(rng + 11);
        riskfight_reset((EncounterState*)&st, (EncounterContext*)&ctx, rng | 1);
        uint32_t start_rng = osrs_lowbias32(rng ^ 0x5374u) | 1;
        riskfight_start_apply(&st, &ctx, riskfight_start_draw(&start_rng, 0), 0, &start_rng);
        static RiskfightProfile prof[2];
        char label[2][32];
        for (int s = 0; s < 2; s++) {
            prof[s] = *riskfight_pick_profile(RISKFIGHT_HUMAN, osrs_lowbias32(rng ^ (0x51u + s)));
            prof[s].dharok_eating = 1;
            prof[s].dharok_persona = riskfight_dharok_persona_draw(osrs_lowbias32(rng ^ (0x70u + s)));
            snprintf(label[s], sizeof(label[s]), "script%d", prof[s].dharok_persona);
        }
        rd_begin();
        while (!st.env.episode_over) {
            int actions[2 * RF_HEADS];
            for (int i = 0; i < 2; i++) {
                float obs[RF_OBS_SIZE];
                riskfight_write_script_observation(&st, i, obs);
                riskfight_script(obs, &prof[i], st.script_seed[i], st.consume_ticks[i], actions + i * RF_HEADS);
            }
            pre = st;
            riskfight_step((EncounterState*)&st, (EncounterContext*)&ctx, actions);
            rd_write_row(&st, &pre);
            ticks++;
        }
        rd_flush(out, names, f, label[0], label[1]);
    }
    fclose(out);
    fclose(names);
    printf("fights %d ticks %ld\n", fights, ticks);
    return 0;
}
