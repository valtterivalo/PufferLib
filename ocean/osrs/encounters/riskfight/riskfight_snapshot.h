#ifndef OSRS_RISKFIGHT_SNAPSHOT_H
#define OSRS_RISKFIGHT_SNAPSHOT_H
#include "riskfight_step.h"

enum { RISKFIGHT_LOAD_TICK = 256 };

typedef struct {
    int hitpoints, prayer, attack, strength, defence, magic;
    int special_energy, special_regen_phase, spec_armed;
    int veng_active, veng_cooldown;
    int attack_timer, food_timer, potion_timer, karambwan_timer;
    int recoil_damage_used;
    OffensivePrayer offensive_prayer;
    OverheadPrayer overhead_prayer;
    int x, y, interacting;
    int seen_last_attack_ago, seen_last_attack_speed;
    int seen_consume_food, seen_consume_potion, seen_consume_karam, seen_consume_delay;
} RiskfightSideSnapshot;

/** Fight state at the end of one tick. attack_timer is the Player value, -1 when the weapon is ready.
 * seen_* is what the other side has inferred about this side. pid_holder moves first. */
typedef struct {
    RiskfightLoadout loadout[2];
    RiskfightSideSnapshot side[2];
    int pid_holder;
    uint32_t seed;
} RiskfightSnapshot;

static void riskfight_load_state(RiskfightState* s, RiskfightContext* ctx, const RiskfightSnapshot* snapshot) {
    const RiskfightLoadout* previous[2] = {ctx->loadout[0], ctx->loadout[1]};
    for (int i = 0; i < 2; i++) ctx->loadout[i] = &snapshot->loadout[i];
    riskfight_reset((EncounterState*)s, (EncounterContext*)ctx, snapshot->seed);
    for (int i = 0; i < 2; i++) ctx->loadout[i] = previous[i];
    s->env.tick = RISKFIGHT_LOAD_TICK;
    while (s->env.pid_holder != snapshot->pid_holder)
        pvp_reset_priority(&s->env, OSRS_PRIORITY_PVP_WORLD);
    for (int i = 0; i < 2; i++) {
        const RiskfightSideSnapshot* side = &snapshot->side[i];
        Player* p = &s->env.players[i];
        p->current_hitpoints = side->hitpoints;
        p->current_prayer = side->prayer;
        p->current_attack = side->attack;
        p->current_strength = side->strength;
        p->current_defence = side->defence;
        p->current_magic = side->magic;
        p->special_energy = side->special_energy;
        p->spec_regen_active = side->special_energy < 100;
        p->item_effect_state.special_regen_ticks = side->special_regen_phase;
        p->spec_armed = side->spec_armed;
        p->veng_active = side->veng_active;
        p->veng_cooldown = side->veng_cooldown;
        p->has_attack_timer = 1;
        p->attack_timer = side->attack_timer;
        p->attack_timer_uncapped = side->attack_timer;
        p->food_timer = side->food_timer;
        p->potion_timer = side->potion_timer;
        p->karambwan_timer = side->karambwan_timer;
        p->offensive_prayer = side->offensive_prayer;
        p->prayer = side->overhead_prayer;
        p->x = FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH / 2 + side->x;
        p->y = FIGHT_AREA_BASE_Y + FIGHT_AREA_HEIGHT / 2 + side->y;
        p->dest_x = p->x;
        p->dest_y = p->y;
        if (side->interacting) osrs_interaction_set(&p->interaction, 1 - i);
        p->item_effect_state.recoil_damage_used = side->recoil_damage_used;
        if (p->equipment_effect_profile.recoil_source == OSRS_RECOIL_SOURCE_RING_OF_RECOIL)
            p->item_effect_state.recoil_charges = RECOIL_MAX_CHARGES - side->recoil_damage_used;
        s->inventory_use[i].divine_combat_ticks = 0;
    }
    for (int viewer = 0; viewer < 2; viewer++) {
        const RiskfightSideSnapshot* seen = &snapshot->side[1 - viewer];
        RiskfightVisibleOpponent* v = &s->visible[viewer];
        riskfight_observe_visible(s, viewer, 1);
        assert(seen->seen_last_attack_ago < RISKFIGHT_LOAD_TICK);
        v->last_attack_tick = seen->seen_last_attack_ago >= 0 ? RISKFIGHT_LOAD_TICK - seen->seen_last_attack_ago : -1;
        v->last_attack_speed = seen->seen_last_attack_speed;
        v->consume_food_est = seen->seen_consume_food;
        v->consume_potion_est = seen->seen_consume_potion;
        v->consume_karam_est = seen->seen_consume_karam;
        v->consume_delay_est = seen->seen_consume_delay;
    }
}
#endif
