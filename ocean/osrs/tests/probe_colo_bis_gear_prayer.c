/**
 * @file probe_colo_bis_gear_prayer.c
 * @brief Verifies the BIS gear-oracle moves the offensive prayer to match the
 * chosen weapon set's style. Regression for the cross-style un-prayed-attack bug:
 * the oracle swapped weapon STYLE every tick but left s->player.offensive_prayer
 * stale, so encounter_offensive_prayer_mults returned identity (no buff) on any
 * cross-style swap and the fired attack ran un-prayed.
 *
 * INVARIANT: after col_apply_bis_gear_oracle commits, for the chosen weapon set the
 * player's offensive_prayer is the style-matched prayer (Piety/Rigour/Augury),
 * regardless of whatever (possibly off-style) prayer was active going in.
 *
 * BUILD: cc -std=c11 -O2 -I. -o /tmp/probe_bis_prayer ocean/osrs/tests/probe_colo_bis_gear_prayer.c -lm
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

/* the deliberately-WRONG prayer we plant before each oracle call: cycle through
   the three offensive prayers so at least one is cross-style for every set. */
static const OffensivePrayer STALE_PRAYERS[3] = {
    OFFENSIVE_PRAYER_PIETY, OFFENSIVE_PRAYER_RIGOUR, OFFENSIVE_PRAYER_AUGURY
};

int main(void) {
    int checks = 0, fails = 0;
    for (uint32_t i = 1; i <= 60; i++) {
        uint32_t seed = i * 0x9E3779B1u;
        ColosseumContext ctx;
        ColosseumState s;
        col_init_context_typed(&ctx);
        ctx.config.start_wave = 0;
        ctx.config.bis_gear_oracle_mode = 1;
        memset(&s, 0, sizeof(s));
        col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, seed);

        /* find any live enemy NPC to target */
        int target = -1;
        for (int n = 0; n < COLO_MAX_NPCS; n++) {
            if (col_npc_is_live_enemy(&s.npcs[n])) { target = n; break; }
        }
        if (target < 0) continue;
        osrs_interaction_set(&s.interaction, target);

        for (int p = 0; p < 3; p++) {
            s.player.offensive_prayer = STALE_PRAYERS[p];
            col_apply_bis_gear_oracle(&s);
            OffensivePrayer expected = col_offensive_prayer_for_attack_style(
                col_weapon_set_attack_style(s.weapon_set));
            checks++;
            if (s.player.offensive_prayer != expected) {
                fails++;
                printf("seed=%u stale=%d set=%d style=%d off=%d expected=%d\n",
                    seed, STALE_PRAYERS[p], (int)s.weapon_set,
                    (int)col_weapon_set_attack_style(s.weapon_set),
                    (int)s.player.offensive_prayer, (int)expected);
            }
        }
    }
    printf("checked %d oracle commits, %d mismatched prayers\n", checks, fails);
    if (checks > 0 && fails == 0) { printf("PASS\n"); return 0; }
    printf("FAIL\n");
    return 1;
}
