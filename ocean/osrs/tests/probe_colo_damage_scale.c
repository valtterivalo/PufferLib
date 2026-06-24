/**
 * @file probe_colo_damage_scale.c
 * @brief player_damage_received_scale sink-coverage probe (Fortis Colosseum).
 *
 * Drives BOTH colosseum-owned incoming-damage paths and asserts the scale rule:
 *   - DIRECT path: col_damage_player_from (the chokepoint for boss/molten/venom/
 *     poison/manticore/bee/skyfall direct hits).
 *   - QUEUED path: col_push_player_pending_hit -> col_resolve_player_pending_hits
 *     (NPC ranged/magic/melee projectiles; the shared resolver decrements HP from
 *     the pre-scaled hit.damage).
 *
 * For each path: scale=1.0 must be byte-identical to no-scale, scale=0.5 must
 * roughly halve (round-half-up), scale=0.0 must zero the HP loss (full invuln).
 * Also confirms the round-half-up rule is exact at 1.0 for every dmg in [0,255].
 *
 * BUILD:
 *   cc -std=c11 -O2 -I. -o /tmp/probe_colo_damage_scale \
 *       ocean/osrs/tests/probe_colo_damage_scale.c -lm
 * RUN:
 *   /tmp/probe_colo_damage_scale
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got != want) {
        printf("FAIL %s: got %d want %d\n", name, got, want);
        failures++;
    } else {
        printf("ok   %s: %d\n", name, got);
    }
}

/** Apply one direct hit of `dmg` at scale `scale`; return HP lost. */
static int direct_hp_loss(uint32_t seed, int dmg, float scale) {
    static ColosseumContext ctx;
    static ColosseumState s;
    col_init_context_typed(&ctx);
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, seed);
    s.active_player_damage_received_scale = scale;
    int hp0 = s.player.current_hitpoints;
    col_damage_player_from(&s, dmg, COLO_MANTICORE);
    return hp0 - s.player.current_hitpoints;
}

/** Queue one resolved-at-throw projectile of raw `dmg` at scale `scale`, resolve
    it, return HP lost. delay=1 so it lands on the next resolve. */
static int queued_hp_loss(uint32_t seed, int dmg, float scale) {
    static ColosseumContext ctx;
    static ColosseumState s;
    col_init_context_typed(&ctx);
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, seed);
    s.active_player_damage_received_scale = scale;
    int hp0 = s.player.current_hitpoints;
    int prayed = 0;
    /* prayer NONE so the hit is not prayed away; hit_success=1 keeps the rolled dmg. */
    s.player.prayer = PRAYER_NONE;
    EncounterPendingHit hit = encounter_pending_hit_resolved_at_throw(
        dmg, 1, ATTACK_STYLE_RANGED, s.player.prayer, COLO_SERPENT_SHAMAN, 0, 1, &prayed);
    int landed_raw = hit.damage; /* post-prayer pre-scale damage that WOULD land */
    col_push_player_pending_hit(&s, hit);
    /* advance one tick so the delay-1 hit is due, then resolve. */
    s.tick++;
    col_resolve_player_pending_hits(&s);
    (void)landed_raw;
    return hp0 - s.player.current_hitpoints;
}

/** Arm Doom, drive one off-prayer instant-melee hit at `scale`, return doom_stacks.
    The leak fix gates Doom accrual on the APPLIED (scaled) damage: at scale=0.0 a
    0-HP hit must add NO Doom stack, else the agent dies to col_mod_doom_lethal even
    at full invuln (the partial-invuln premise breaks on any Doom wave). */
static int doom_stacks_after_melee(uint32_t seed, int dmg, float scale) {
    static ColosseumContext ctx;
    static ColosseumState s;
    col_init_context_typed(&ctx);
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, seed);
    s.active_player_damage_received_scale = scale;
    s.modifiers.active_mask |= (1u << COLO_MOD_DOOM);
    s.modifiers.tier[COLO_MOD_DOOM] = 1;
    s.player.prayer = PRAYER_NONE;  /* off-prayer vs melee -> hit lands pre-scale dmg */
    s.doom_stacks = 0;
    col_apply_instant_melee_hit(&s, &ctx, 0, COLO_JAGUAR_WARRIOR, dmg, 1);
    return s.doom_stacks;
}

static int round_half_up(int dmg, float scale) {
    if (scale >= 1.0f) return dmg;
    if (scale <= 0.0f) return 0;
    int v = (int)((float)dmg * scale + 0.5f);
    return v < 0 ? 0 : v;
}

int main(void) {
    const uint32_t SEED = 0xC0DEu;
    const int DMG = 20;

    /* DIRECT path. */
    int d_full = direct_hp_loss(SEED, DMG, 1.0f);
    int d_half = direct_hp_loss(SEED, DMG, 0.5f);
    int d_zero = direct_hp_loss(SEED, DMG, 0.0f);
    check("direct scale=1.0 == raw dmg", d_full, DMG);
    check("direct scale=0.5 == round_half_up", d_half, round_half_up(DMG, 0.5f));
    check("direct scale=0.0 == 0 (invuln)", d_zero, 0);

    /* QUEUED projectile path. The resolved-at-throw helper may itself reduce dmg
       (accuracy/prayer); compute the unscaled landed value from a scale=1.0 run
       and assert the scaled runs track it. */
    int q_full = queued_hp_loss(SEED, DMG, 1.0f);
    int q_half = queued_hp_loss(SEED, DMG, 0.5f);
    int q_zero = queued_hp_loss(SEED, DMG, 0.0f);
    check("queued scale=1.0 landed > 0 (sanity)", q_full > 0 ? 1 : 0, 1);
    check("queued scale=0.5 == round_half_up(landed)", q_half, round_half_up(q_full, 0.5f));
    check("queued scale=0.0 == 0 (invuln)", q_zero, 0);

    /* DOOM leak (the leak-completeness refutation): an off-prayer instant-melee hit
       must accrue a Doom stack only on the APPLIED (scaled) damage. At scale=0.0 the
       hit deals 0 HP and must add NO Doom stack, so scale=0.0 is truly Doom-immune
       like invuln_mode; otherwise the agent still dies to col_mod_doom_lethal. */
    int doom_full = doom_stacks_after_melee(SEED, DMG, 1.0f);
    int doom_zero = doom_stacks_after_melee(SEED, DMG, 0.0f);
    check("doom scale=1.0 accrues one stack", doom_full, 1);
    check("doom scale=0.0 accrues NO stack (invuln-equivalent)", doom_zero, 0);

    /* Round-half-up exactness at scale=1.0 for every dmg in [0,255]: the no-op
       path must be the identity (this is what guarantees golden bit-identity). */
    int identity_ok = 1;
    for (int dmg = 0; dmg <= 255; dmg++) {
        ColosseumState s;
        memset(&s, 0, sizeof(s));
        s.active_player_damage_received_scale = 1.0f;
        if (col_scale_incoming_damage(&s, dmg) != dmg) { identity_ok = 0; break; }
    }
    check("scale=1.0 identity over dmg[0,255]", identity_ok, 1);

    /* scale=0.0 zeros every dmg. */
    int zero_ok = 1;
    for (int dmg = 0; dmg <= 255; dmg++) {
        ColosseumState s;
        memset(&s, 0, sizeof(s));
        s.active_player_damage_received_scale = 0.0f;
        if (col_scale_incoming_damage(&s, dmg) != 0) { zero_ok = 0; break; }
    }
    check("scale=0.0 zeros dmg[0,255]", zero_ok, 1);

    if (failures) {
        printf("\n%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("\nALL PASS\n");
    return 0;
}
