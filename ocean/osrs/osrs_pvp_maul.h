#ifndef OSRS_PVP_MAUL_H
#define OSRS_PVP_MAUL_H

static inline int pvp_is_maul(int weapon) {
    return weapon == ITEM_GRANITE_MAUL || weapon == ITEM_GRANITE_MAUL_ORNATE;
}

static inline void pvp_maul_weapon_changed(OsrsEnv* env, int agent, int old_weapon) {
    Player* p = &env->players[agent];
    int weapon = p->equipped[GEAR_SLOT_WEAPON];
    if (weapon == old_weapon) return;
    env->pvp_runtime.maul[agent] = osrs_granite_maul_weapon_changed(env->pvp_runtime.maul[agent]);
    if (pvp_is_maul(weapon) || pvp_is_maul(old_weapon)) p->spec_armed = 0;
}

static inline void pvp_maul_special_click(OsrsEnv* env, int agent) {
    OsrsGraniteMaulState state = osrs_granite_maul_special_click(env->pvp_runtime.maul[agent], env->tick);
    env->pvp_runtime.maul[agent] = state;
    env->players[agent].spec_armed = state.preparation == OSRS_GRANITE_MAUL_SELECTED;
}

static inline int pvp_maul_target_available(const OsrsEnv* env, int agent, int target) {
    return target >= 0 && target < NUM_AGENTS && target != agent &&
        env->players[agent].current_hitpoints > 0 && env->players[target].current_hitpoints > 0 &&
        is_in_melee_range(&env->players[agent], &env->players[target]);
}

static inline int pvp_maul_resolve(OsrsEnv* env, int agent, OsrsGraniteMaulResolution resolution) {
    env->pvp_runtime.maul[agent] = resolution.state;
    Player* p = &env->players[agent];
    p->spec_armed = resolution.state.preparation == OSRS_GRANITE_MAUL_SELECTED;
    int cost = osrs_spec_cost(p->equipped[GEAR_SLOT_WEAPON]);
    int hits = 0;
    for (; hits < resolution.requested_hits && p->special_energy >= cost; hits++) {
        perform_attack(env, agent, resolution.target, ATTACK_STYLE_MELEE, 1, 0,
            chebyshev_distance(p->x, p->y, env->players[resolution.target].x, env->players[resolution.target].y));
    }
    if (hits > 0) osrs_interaction_set(&p->interaction, resolution.target);
    return hits;
}

static inline int pvp_maul_target_click(OsrsEnv* env, int agent, int target) {
    if (!pvp_is_maul(env->players[agent].equipped[GEAR_SLOT_WEAPON])) return 0;
    env->pvp_runtime.maul[agent] = osrs_granite_maul_queue_target(
        env->pvp_runtime.maul[agent], env->tick, target);
    if (!pvp_maul_target_available(env, agent, target)) return 0;
    return pvp_maul_resolve(env, agent, osrs_granite_maul_target_click(
        env->pvp_runtime.maul[agent], env->tick, target));
}

static inline int pvp_maul_finish_inputs(OsrsEnv* env, int agent) {
    if (!pvp_is_maul(env->players[agent].equipped[GEAR_SLOT_WEAPON])) return 0;
    OsrsGraniteMaulState state = env->pvp_runtime.maul[agent];
    return pvp_maul_resolve(env, agent, osrs_granite_maul_finish_inputs(state, env->tick,
        pvp_maul_target_available(env, agent, state.last_attack_target)));
}

static inline int pvp_maul_continue_attack(OsrsEnv* env, int agent) {
    Player* p = &env->players[agent];
    if (!pvp_is_maul(p->equipped[GEAR_SLOT_WEAPON])) return 0;
    OsrsGraniteMaulState state = osrs_granite_maul_expire(env->pvp_runtime.maul[agent], env->tick);
    env->pvp_runtime.maul[agent] = state;
    p->spec_armed = state.preparation == OSRS_GRANITE_MAUL_SELECTED;
    int target = p->interaction.target_slot;
    if (!pvp_maul_target_available(env, agent, target)) return 0;
    if (state.pending_target != target &&
        !(state.preparation == OSRS_GRANITE_MAUL_SELECTED && can_attack_now(p))) return 0;
    return pvp_maul_target_click(env, agent, target);
}

#endif
