#ifndef OSRS_RISKFIGHT_POLICY_H
#define OSRS_RISKFIGHT_POLICY_H
#include "riskfight_model.h"

enum {
    RF_OBSERVATION_SCHEMA_VERSION = 5,
    RF_OBSERVATION_TICK_SCALE = 1024,
    RF_OBSERVATION_TILE_SCALE = 64,
    RF_OBSERVATION_ITEM_SCALE = 256,
    RF_OBSERVATION_DAMAGE_SCALE = 128,
    RF_OBSERVATION_HEALTH_BAR_SCALE = 32,
    RF_OBSERVATION_ATTACK_AGE_SCALE = 16,
    RF_OBSERVATION_ATTACK_COUNT_SCALE = 4,
    RF_OBSERVATION_STYLE_SCALE = 4,
    RF_OBSERVATION_SACK_SCALE = 128,
};

static void riskfight_write_observation(const RiskfightState* s, int agent, float* obs) {
    const Player* p = &s->env.players[agent];
    const OsrsInventoryUseState* use = &s->inventory_use[agent];
    const RiskfightVisibleOpponent* v = &s->visible[agent];
    const float self[RF_SELF_SIZE] = {
        p->current_hitpoints / 121.0f, p->current_prayer / 99.0f,
        p->current_attack / 118.0f, p->current_strength / 118.0f,
        p->current_defence / 120.0f, p->current_magic / 99.0f,
        p->special_energy / 100.0f, p->attack_timer / 10.0f,
        p->food_timer / 3.0f, p->potion_timer / 3.0f, p->karambwan_timer / 3.0f,
        (float)p->veng_active, p->veng_cooldown / 50.0f,
        (float)p->spec_armed, (float)osrs_interaction_active(&p->interaction),
        p->offensive_prayer / 4.0f, p->fight_style / 3.0f,
        p->item_effect_state.recoil_damage_used / 40.0f,
        use->divine_combat_ticks / 500.0f, use->vengeance_sacks / 100.0f,
        (float)s->env.tick / RF_OBSERVATION_TICK_SCALE,
        (float)(p->x - FIGHT_AREA_BASE_X - FIGHT_AREA_WIDTH / 2) / RF_OBSERVATION_TILE_SCALE,
        (float)(p->y - FIGHT_AREA_BASE_Y - FIGHT_AREA_HEIGHT / 2) / RF_OBSERVATION_TILE_SCALE,
        // Maul preparation state: SELECTED exposes spec_armed (obs 13), but
        // DESELECTED (double armed, 2 paid hits queued, 3-tick expiry) is
        // otherwise identical to idle. Without this the double-spec skill is
        // unlearnable: the commit tick looks exactly like doing nothing.
        // Scale: prepared_hits / 2 (0, 0.5, 1). Replaces the reserved
        // has_attack_timer bit (derivable from attack_timer anyway).
        (float)s->env.pvp_runtime.maul[agent].prepared_hits / 2.0f,
    };
    memcpy(obs, self, sizeof(self));
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
        float* row = obs + RF_INVENTORY_START + slot * RF_INVENTORY_WIDTH;
        row[0] = osrs_inventory_cell_obs_code_encode(p->inventory_cells[slot].content_code);
        row[1] = osrs_consumable_hp_heal_amount((OsrsConsumableKind)m->consumable_kind, p->base_hitpoints) / 121.0f;
        row[2] = m->click_action == OSRS_CLICK_EAT ?
            (m->consumable_kind == OSRS_CONSUMABLE_SUMMER_PIE ? 1.0f :
             m->consumable_kind == OSRS_CONSUMABLE_HALIBUT ? 2.0f : 3.0f) / 3.0f : 0;
        row[3] = m->click_action == OSRS_CLICK_EAT ?
            (m->consumable_kind == OSRS_CONSUMABLE_HALIBUT ? 2.0f : 3.0f) / 3.0f : 0;
        row[4] = m->dose_count / 4.0f;
        row[5] = m->consumable_kind == OSRS_CONSUMABLE_VENGEANCE_SACK ?
            (float)use->vengeance_sacks / RF_OBSERVATION_SACK_SCALE : !osrs_inventory_cell_is_empty(&p->inventory_cells[slot]);
    }
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        obs[RF_EQUIPPED_START + slot] = (float)p->equipped[slot] / RF_OBSERVATION_ITEM_SCALE;
        obs[RF_OPPONENT_START + slot] = (float)v->equipment[slot] / RF_OBSERVATION_ITEM_SCALE;
    }
    float* opponent = obs + RF_OPPONENT_START + NUM_GEAR_SLOTS;
    const Player* opp = &s->env.players[1 - agent];
    opponent[0] = v->health_bar / 30.0f;
    opponent[1] = (float)(v->x - p->x) / RF_OBSERVATION_TILE_SCALE;
    opponent[2] = (float)(v->y - p->y) / RF_OBSERVATION_TILE_SCALE;
    opponent[3] = v->interacting;
    opponent[4] = v->last_attack_tick >= 0;
    opponent[5] = v->last_attack_tick >= 0 ?
        (float)(s->env.tick - v->last_attack_tick) / RF_OBSERVATION_ATTACK_AGE_SCALE : 0;
    opponent[6] = v->last_attack_tick >= 0 ?
        fmaxf(0, v->last_attack_tick + v->last_attack_speed - s->env.tick) /
            RF_OBSERVATION_ATTACK_AGE_SCALE : 0;
    // Opponent vengeance state (schema 4, fully observable): both sides spawn
    // pre-vengeanced, casts are public anims, and exact-tick cooldown counting
    // is what a good player does anyway. Same scale as self obs[11..12].
    opponent[7] = (float)opp->veng_active;
    opponent[8] = opp->veng_cooldown / 50.0f;
    // Opponent spec energy (schema 5, obs 9): exact value, same scale as self
    // obs[6]. Spends are public (anim + energy math), regen is deterministic
    // (10/50 ticks, 10/25 with lightbearer whose ring is visible gear).
    // No surge potion exists in the riskfight bag, so no hidden restore.
    opponent[9] = opp->special_energy / 100.0f;
    // Opponent attack timer (schema 5, obs 10): exact cooldown incl. food
    // delay (shark +3, karambwan +2 stack onto the live timer). Same scale
    // as self obs[7].
    opponent[10] = opp->attack_timer / 10.0f;
    for (int ago = 0; ago < RF_HISTORY_TICKS; ago++) {
        int index = (s->env.tick - 1 - ago + RF_HISTORY_TICKS) % RF_HISTORY_TICKS;
        float* out = obs + RF_HISTORY_START + ago * RF_EVENT_WIDTH;
        if (ago < s->env.tick) {
            memcpy(out, v->events[index], RF_EVENT_WIDTH * sizeof(float));
            out[0] /= RF_OBSERVATION_ATTACK_COUNT_SCALE;
            out[1] /= RF_OBSERVATION_ITEM_SCALE;
            out[2] /= RF_OBSERVATION_STYLE_SCALE;
            out[4] /= RF_OBSERVATION_DAMAGE_SCALE;
            out[7] /= RF_OBSERVATION_HEALTH_BAR_SCALE;
        } else memset(out, 0, RF_EVENT_WIDTH * sizeof(float));
    }
}

static void riskfight_write_action_mask(const RiskfightState* s, int agent, float* mask) {
    const Player* p = &s->env.players[agent];
    memset(mask, 0, RF_MASK_SIZE * sizeof(float));
    float* heads[RF_HEADS];
    float* gear_masks[NUM_GEAR_SLOTS];
    int has_empty = osrs_first_empty_inventory_cell(p->inventory_cells, -1) >= 0;
    int offset = 0;
    for (int head = 0; head < RF_HEADS; head++) {
        heads[head] = mask + offset;
        heads[head][0] = 1;
        int gear_slot = RF_GEAR_SLOT_BY_HEAD[head];
        if (gear_slot >= 0) {
            gear_masks[gear_slot] = heads[head];
            heads[head][RF_UNEQUIP] = p->equipped[gear_slot] != ITEM_NONE && has_empty;
        } else if (head > RF_COMBO) {
            for (int action = 1; action < RF_ACTION_DIMS[head]; action++) heads[head][action] = 1;
        }
        offset += RF_ACTION_DIMS[head];
    }
    assert(offset == RF_MASK_SIZE);
    int has_teleport = 0, one_handed_switch = 0, shield_slot = -1;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
        if (m->gear_slot >= 0) {
            int allowed = osrs_can_equip_metadata(p, m, has_empty);
            gear_masks[m->gear_slot][slot + 1] = allowed;
            if (m->gear_slot == GEAR_SLOT_WEAPON && !item_is_two_handed(m->item_idx))
                one_handed_switch |= allowed;
            if (m->gear_slot == GEAR_SLOT_SHIELD) shield_slot = slot;
        }
        if (m->click_action == OSRS_CLICK_EAT) {
            int head = m->consumable_kind == OSRS_CONSUMABLE_HALIBUT ? RF_COMBO : RF_FOOD;
            heads[head][slot + 1] = osrs_can_eat_consumable_kind(p, (OsrsConsumableKind)m->consumable_kind);
        }
        heads[RF_DRINK][slot + 1] = m->click_action == OSRS_CLICK_DRINK && p->potion_timer == 0 &&
            (m->consumable_kind != OSRS_CONSUMABLE_DIVINE_COMBAT || p->current_hitpoints > OSRS_DIVINE_DAMAGE);
        has_teleport |= m->click_action == OSRS_CLICK_TELEPORT;
    }
    if (shield_slot >= 0 && one_handed_switch && item_is_two_handed(p->equipped[GEAR_SLOT_WEAPON])) {
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
            if (osrs_inventory_cell_metadata(&p->inventory_cells[slot])->gear_slot == GEAR_SLOT_SHIELD)
                heads[RF_SHIELD][slot + 1] = 1;
    }
    heads[RF_VENGEANCE][1] = !p->veng_active && p->veng_cooldown <= 1 &&
        p->current_magic >= OSRS_VENGEANCE_MAGIC_LEVEL && s->inventory_use[agent].vengeance_sacks > 0;
    for (int action = 1; action < RF_ACTION_DIMS[RF_SPECIAL]; action++)
        heads[RF_SPECIAL][action] = p->special_energy >= action * 50;
    heads[RF_PRIMARY][RF_TELEPORT] = has_teleport &&
        osrs_teleport_allowed(s->env.pvp_runtime.teleport[agent], s->env.tick);
    for (int action = 1; action < RF_ACTION_DIMS[RF_PRAYER]; action++)
        heads[RF_PRAYER][action] = p->current_prayer > 0;
}

static int riskfight_find_kind(const float* obs, OsrsConsumableKind kind) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        uint16_t code = osrs_inventory_cell_obs_code_decode(obs[RF_INVENTORY_START + slot * RF_INVENTORY_WIDTH]);
        if (osrs_item_content_metadata(code)->consumable_kind == kind) return slot + 1;
    }
    return 0;
}

static int riskfight_find_gear(const float* obs, uint8_t item) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        uint16_t code = osrs_inventory_cell_obs_code_decode(obs[RF_INVENTORY_START + slot * RF_INVENTORY_WIDTH]);
        if (osrs_item_content_metadata(code)->item_idx == item) return slot + 1;
    }
    return 0;
}

#include "riskfight_tactician.h"

static void riskfight_script(const float* obs, RiskfightOpponent type, int* actions) {
    memset(actions, 0, RF_HEADS * sizeof(int));
    if (type >= RISKFIGHT_TACTICIAN && type <= RISKFIGHT_FLOOR) {
        const RiskfightTacticianProfile profiles[] = {RISKFIGHT_PROFILE_BALANCED,
            RISKFIGHT_PROFILE_PRESSURE, RISKFIGHT_PROFILE_CAUTIOUS, RISKFIGHT_PROFILE_HELDOUT, RISKFIGHT_PROFILE_FLOOR};
        riskfight_tactician_profile(obs, actions, profiles[type - RISKFIGHT_TACTICIAN]);
        return;
    }
    float hp = obs[0] * 121;
    const float* opponent = obs + RF_OPPONENT_START + NUM_GEAR_SLOTS;
    int threshold = type == RISKFIGHT_CAUTIOUS ? 80 : type == RISKFIGHT_AGGRESSIVE ? 45 : 65;
    actions[RF_PRIMARY] = RF_ATTACK;
    actions[RF_PRAYER] = OFFENSIVE_PRAYER_PIETY;
    actions[RF_STYLE] = FIGHT_STYLE_AGGRESSIVE;
    if (hp < threshold) {
        if (obs[8] == 0) {
            actions[RF_FOOD] = riskfight_find_kind(obs, OSRS_CONSUMABLE_MARLIN);
            if (!actions[RF_FOOD]) actions[RF_FOOD] = riskfight_find_kind(obs, OSRS_CONSUMABLE_SUMMER_PIE);
        }
        if (obs[9] == 0) actions[RF_DRINK] = riskfight_find_kind(obs, OSRS_CONSUMABLE_BREW);
        if (hp < 45 && obs[10] == 0) actions[RF_COMBO] = riskfight_find_kind(obs, OSRS_CONSUMABLE_HALIBUT);
        if (actions[RF_FOOD] || actions[RF_DRINK] || actions[RF_COMBO]) actions[RF_PRIMARY] = RF_STOP;
    }
    if (!actions[RF_DRINK] && obs[9] == 0) {
        float attack = obs[2] * 118;
        float strength = obs[3] * 118;
        float defence = obs[4] * 120;
        int needs_restore = obs[1] * 99 <= 40 || attack < 98.5f ||
            strength < 98.5f || defence < 98.5f || obs[5] * 99 < 98.5f;
        if (needs_restore) {
            actions[RF_DRINK] = riskfight_find_kind(obs, OSRS_CONSUMABLE_SANFEW);
            if (!actions[RF_DRINK])
                actions[RF_DRINK] = riskfight_find_kind(obs, OSRS_CONSUMABLE_SUPER_RESTORE);
        }
        if (!actions[RF_DRINK] && hp >= threshold &&
                (attack < 110 || strength < 110 || defence < 110))
            actions[RF_DRINK] = riskfight_find_kind(obs, OSRS_CONSUMABLE_SUPER_COMBAT);
    }
    if (!obs[11] && obs[12] <= 0.02f) actions[RF_VENGEANCE] = 1;
    // Axe discipline shared with the tactician: tentacle default, axe only
    // as a boosted low-HP finisher (recorded one-tick lethal switch, not
    // camping). Legacy scripts lack exit/threat state, so evaluate with
    // CONTINUE, non-reflecting, and the fresh bar upper bound.
    uint8_t weapon = ITEM_ABYSSAL_TENTACLE;
    {
        int opp_bar = (int)lroundf(opponent[0] * OSRS_PLAYER_HEALTH_BAR_SCALE);
        OsrsHealthBarRange opp_hp =
            osrs_health_bar_range(opp_bar, OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 121);
        int opp_upper = opp_hp.kind == OSRS_HEALTH_BAR_KNOWN ? opp_hp.upper : 121;
        Player probe = riskfight_observed_self(obs);
        probe.equipped[GEAR_SLOT_WEAPON] = ITEM_DHAROKS_GREATAXE;
        probe.equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        int hp_lower = probe.current_hitpoints < 1 ? 1 : probe.current_hitpoints;
        OsrsMeleeThreat axe = osrs_melee_threat(probe.equipped,
            calculate_effective_strength(&probe, ATTACK_STYLE_MELEE),
            probe.base_hitpoints, hp_lower, probe.special_energy);
        int ready = probe.attack_timer <= 1;
        if (riskfight_dharok_finisher(ready, probe.current_hitpoints,
                probe.base_hitpoints, axe.normal_max, opp_upper, 60,
                RISKFIGHT_CONTINUE, 0, 0))
            weapon = ITEM_DHAROKS_GREATAXE;
    }
    // Granite-maul double (ornate, 50+50): the recorded KO line is
    // [equip maul, toggle, toggle, target-click] in ONE tick for ~76 into a
    // ~60-HP opponent (vs voidwaker ~61 ceiling at 50 energy). Teach it only
    // where the model can afford to learn it: full 100 energy, adjacent
    // (instant), fresh low opp bar, and boosted own HP (post-axe-trade zone).
    // Single toggle (SELECTED, obs13=1) is the visible half; the double
    // commit (DESELECTED, obs23=1) is now observable too (schema 3).
    if (type == RISKFIGHT_AGGRESSIVE && obs[6] >= 0.99f && opponent[0] < 0.65f) {
        int opp_bar = (int)lroundf(opponent[0] * OSRS_PLAYER_HEALTH_BAR_SCALE);
        OsrsHealthBarRange opp_hp =
            osrs_health_bar_range(opp_bar, OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 121);
        if (opp_hp.kind == OSRS_HEALTH_BAR_KNOWN && opp_hp.upper <= 76 && hp < 99) {
            weapon = ITEM_GRANITE_MAUL_ORNATE;
            actions[RF_SPECIAL] = 2;
        } else {
            weapon = ITEM_VOIDWAKER;
            actions[RF_SPECIAL] = 1;
        }
    } else if (type == RISKFIGHT_AGGRESSIVE && obs[6] >= 0.5f && opponent[0] < 0.65f) {
        weapon = ITEM_VOIDWAKER;
        actions[RF_SPECIAL] = 1;
    }
    actions[RF_WEAPON] = riskfight_find_gear(obs, weapon);
    if (!item_is_two_handed(weapon))
        actions[RF_SHIELD] = riskfight_find_gear(obs, ITEM_AVERNIC_DEFENDER);
    if (type == RISKFIGHT_AGGRESSIVE && opponent[4] && opponent[6] * RF_OBSERVATION_ATTACK_AGE_SCALE > 2 && hp > 55 && hp < 100)
        actions[RF_ORB] = 1;
    actions[RF_RING] = riskfight_find_gear(obs,
        obs[7] > 0.1f ? ITEM_RING_OF_RECOIL : ITEM_ULTOR_RING);
    if (type == RISKFIGHT_CAUTIOUS && hp < 30 &&
        !riskfight_find_kind(obs, OSRS_CONSUMABLE_MARLIN) &&
        !riskfight_find_kind(obs, OSRS_CONSUMABLE_SUMMER_PIE) &&
        !riskfight_find_kind(obs, OSRS_CONSUMABLE_HALIBUT) &&
        !riskfight_find_kind(obs, OSRS_CONSUMABLE_BREW))
        actions[RF_PRIMARY] = RF_TELEPORT;
}
#endif
