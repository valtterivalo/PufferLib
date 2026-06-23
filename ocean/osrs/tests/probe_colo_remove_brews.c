/**
 * @file probe_colo_remove_brews.c
 * @brief Positive check for the remove_brews scaffold knob: with the flag on the
 * seeded inventory must hold zero brew doses across both loadouts; with it off it
 * must keep the loadout's brews. Golden covers only the off (bit-identical) path.
 *
 * BUILD: cc -std=c11 -O2 -I. -o /tmp/probe_brews ocean/osrs/tests/probe_colo_remove_brews.c -lm
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

static int reset_brew_doses(int remove_brews, uint32_t seed) {
    ColosseumContext ctx;
    ColosseumState s;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 0;
    ctx.config.loadout_profile_mode = 2;
    ctx.config.beginner_loadout_fraction = 0.5f;
    ctx.config.remove_brews = remove_brews;
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, seed);
    return s.player.brew_doses;
}

int main(void) {
    int off_min = 1 << 30, off_max = -1, on_min = 1 << 30, on_max = -1;
    for (uint32_t i = 1; i <= 60; i++) {
        uint32_t seed = i * 0x9E3779B1u;
        int off = reset_brew_doses(0, seed);
        int on = reset_brew_doses(1, seed);
        if (off < off_min) off_min = off;
        if (off > off_max) off_max = off;
        if (on < on_min) on_min = on;
        if (on > on_max) on_max = on;
    }
    printf("remove_brews=0: brew_doses min=%d max=%d (expect both loadouts, 4 and 24)\n",
        off_min, off_max);
    printf("remove_brews=1: brew_doses min=%d max=%d (expect 0 and 0)\n", on_min, on_max);
    if (on_max == 0 && off_min > 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
