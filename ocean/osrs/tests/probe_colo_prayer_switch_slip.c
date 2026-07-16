/**
 * @file probe_colo_prayer_switch_slip.c
 * @brief Positive check for the prayer_switch_fail_prob scaffold knob. Driven
 * through the real put_float CLI/binding string path. At p=1.0 an overhead
 * SWITCH must slip back to the previous overhead and leave prayer_just_activated
 * clear; at p=0.0 the switch must apply. Golden covers only the off path.
 *
 * BUILD: cc -std=c11 -O2 -I. -o /tmp/probe_prayer_slip \
 *     ocean/osrs/tests/probe_colo_prayer_switch_slip.c -lm
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

static void prep(ColosseumContext* ctx, ColosseumState* s, float fail_prob) {
    col_init_context_typed(ctx);
    memset(s, 0, sizeof(*s));
    s->rng_state = 0xC0FFEEu;
    /* exercise the real CLI/binding string path, not a direct config write */
    ENCOUNTER_COLOSSEUM.put_float((EncounterState*)s, (EncounterContext*)ctx,
        "prayer_switch_fail_prob", fail_prob);
    s->player.prayer = PRAYER_PROTECT_RANGED;
    s->player.current_prayer = 990;  /* prayer points, else drain turns the overhead off this tick */
    s->player.prayer_just_activated = 0;
}

int main(void) {
    int actions[COLO_NUM_ACTION_HEADS] = {0};
    actions[COLO_HEAD_PRAYER] = ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC;

    ColosseumContext ctx_off; ColosseumState s_off;
    prep(&ctx_off, &s_off, 0.0f);
    col_player_pretick(&s_off, &ctx_off, actions);
    int applied = (s_off.player.prayer == PRAYER_PROTECT_MAGIC);

    ColosseumContext ctx_on; ColosseumState s_on;
    prep(&ctx_on, &s_on, 1.0f);
    col_player_pretick(&s_on, &ctx_on, actions);
    int reverted = (s_on.player.prayer == PRAYER_PROTECT_RANGED);
    int flag_clear = (s_on.player.prayer_just_activated == 0);

    printf("p=0.0: prayer=%d (expect MAGIC=%d) applied=%d\n",
        s_off.player.prayer, PRAYER_PROTECT_MAGIC, applied);
    printf("p=1.0: prayer=%d (expect RANGED=%d) reverted=%d just_activated=%d\n",
        s_on.player.prayer, PRAYER_PROTECT_RANGED, reverted,
        s_on.player.prayer_just_activated);
    if (applied && reverted && flag_clear) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
