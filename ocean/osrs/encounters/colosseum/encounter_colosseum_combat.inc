#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_combat.inc
 * @brief Combat resolution + NPC AI, mirroring inferno combat.
 *
 * Scaffold: the NPC attack routine and boss tick are stubs that advance timers
 * but deal no damage. The lead fills in style selection, prayer checks,
 * projectile queueing, and the Sol Heredit state machine.
 */

static void col_apply_npc_death(ColosseumState* s, int idx) {
    s->log.total_npc_kills++;
    col_deactivate_npc(s, idx);
}

/** resolve player attack projectiles landing on NPCs. */
static void col_resolve_player_projectiles_on_npcs(ColosseumState* s) {
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active) continue;
        for (int h = 0; h < npc->pending_hits.count; h++) {
            EncounterPendingHit* ph = &npc->pending_hits.hits[h];
            int landed = 0, hit_damage = 0;
            float dmg_acc = 0.0f;
            int frozen = npc->frozen_ticks;
            if (encounter_resolve_npc_pending_hit(
                    ph, &npc->hp, &landed, &hit_damage, &frozen, NULL, &dmg_acc)) {
                npc->hit_landed_this_tick = landed;
                npc->hit_damage = hit_damage;
                npc->hit_was_successful_this_tick = (hit_damage > 0);
                s->tick_scratch.damage_dealt += dmg_acc;
                if (npc->type == COLO_SOL_HEREDIT)
                    s->tick_scratch.damage_boss += dmg_acc;
                if (npc->hp <= 0) {
                    col_apply_npc_death(s, i);
                    break;
                }
            }
        }
    }
}

/** resolve NPC attack projectiles landing on the player. */
static void col_resolve_player_pending_hits(ColosseumState* s) {
    int prayer_correct = 0;
    int off_prayer = 0;
    float dmg_received = 0.0f;
    encounter_resolve_player_pending_hits(
        &s->player_pending_hits, &s->player, s->player.prayer,
        &dmg_received, &prayer_correct, &off_prayer);
    s->tick_scratch.damage_received += dmg_received;
    s->tick_scratch.prayer_correct += prayer_correct;
}

/**
 * Per-NPC attack routine. Scaffold stub: ticks the attack timer but queues no
 * hits. The lead adds style selection, prayer checks, and projectile queueing.
 */
static void col_npc_attack_ctx(ColosseumState* s, ColosseumContext* ctx, int idx) {
    (void)s;
    (void)ctx;
    ColoNPC* npc = &s->npcs[idx];
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return;
    if (npc->attack_timer > 0) npc->attack_timer--;
}

/**
 * Sol Heredit boss tick. Scaffold stub: the lead implements the phase machine,
 * attack-combo queue, triple-parry, grapple, and molten-sand hazards here.
 */
static void col_sol_tick(ColosseumState* s, ColosseumContext* ctx) {
    (void)s;
    (void)ctx;
}

/** the NPC update loop, mirroring inferno's tick_npcs. */
static void col_tick_npcs_ctx(ColosseumState* s, ColosseumContext* ctx) {
    col_sol_tick(s, ctx);
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active) continue;

        if (npc->death_ticks > 0) {
            npc->death_ticks--;
            if (npc->death_ticks == 0) col_deactivate_npc(s, i);
            continue;
        }
        if (npc->stun_timer > 0) npc->stun_timer--;
        if (npc->frozen_ticks > 0) npc->frozen_ticks--;

        col_npc_move_ctx(s, ctx, i);
        col_npc_attack_ctx(s, ctx, i);
    }
}
