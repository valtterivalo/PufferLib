#ifndef OSRS_RISKFIGHT_POLICY_H
#define OSRS_RISKFIGHT_POLICY_H
#include "riskfight_model.h"

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
        (float)s->env.tick, (float)p->x, (float)p->y, (float)p->has_attack_timer,
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
            (float)use->vengeance_sacks : !osrs_inventory_cell_is_empty(&p->inventory_cells[slot]);
    }
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        obs[RF_EQUIPPED_START + slot] = p->equipped[slot];
        obs[RF_OPPONENT_START + slot] = v->equipment[slot];
    }
    float* opponent = obs + RF_OPPONENT_START + NUM_GEAR_SLOTS;
    opponent[0] = v->health_bar / 30.0f;
    opponent[1] = v->x - p->x;
    opponent[2] = v->y - p->y;
    opponent[3] = v->interacting;
    opponent[4] = v->last_attack_tick >= 0;
    opponent[5] = v->last_attack_tick >= 0 ? s->env.tick - v->last_attack_tick : 0;
    opponent[6] = v->last_attack_tick >= 0 ?
        fmaxf(0, v->last_attack_tick + v->last_attack_speed - s->env.tick) : 0;
    for (int ago = 0; ago < RF_HISTORY_TICKS; ago++) {
        int index = (s->env.tick - 1 - ago + RF_HISTORY_TICKS) % RF_HISTORY_TICKS;
        float* out = obs + RF_HISTORY_START + ago * RF_EVENT_WIDTH;
        if (ago < s->env.tick) memcpy(out, v->events[index], RF_EVENT_WIDTH * sizeof(float));
        else memset(out, 0, RF_EVENT_WIDTH * sizeof(float));
    }
}

static int riskfight_inventory_head_accepts(const Player* p, int head, int slot) {
    const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
    if (head <= RF_RING) {
        const int gear_slots[] = {GEAR_SLOT_WEAPON, GEAR_SLOT_SHIELD, GEAR_SLOT_RING};
        if (m->gear_slot != gear_slots[head]) return 0;
        if (osrs_can_equip_from_cell(p, p->inventory_cells, slot)) return 1;
        if (head == RF_SHIELD && item_is_two_handed(p->equipped[GEAR_SLOT_WEAPON])) {
            for (int weapon_slot = 0; weapon_slot < OSRS_INVENTORY_SIZE; weapon_slot++) {
                const OsrsItemContentMetadata* weapon = osrs_inventory_cell_metadata(&p->inventory_cells[weapon_slot]);
                if (weapon->gear_slot == GEAR_SLOT_WEAPON && !item_is_two_handed(weapon->item_idx) &&
                    osrs_can_equip_from_cell(p, p->inventory_cells, weapon_slot)) return 1;
            }
        }
        return 0;
    }
    if (head == RF_DRINK) return m->click_action == OSRS_CLICK_DRINK && p->potion_timer == 0 &&
        (m->consumable_kind != OSRS_CONSUMABLE_DIVINE_COMBAT || p->current_hitpoints > OSRS_DIVINE_DAMAGE);
    if (head == RF_COMBO) return m->consumable_kind == OSRS_CONSUMABLE_HALIBUT &&
        osrs_can_eat_consumable_kind(p, (OsrsConsumableKind)m->consumable_kind);
    return m->click_action == OSRS_CLICK_EAT && m->consumable_kind != OSRS_CONSUMABLE_HALIBUT &&
        osrs_can_eat_consumable_kind(p, (OsrsConsumableKind)m->consumable_kind);
}

static void riskfight_write_action_mask(const RiskfightState* s, int agent, float* mask) {
    const Player* p = &s->env.players[agent];
    memset(mask, 0, RF_MASK_SIZE * sizeof(float));
    int offset = 0;
    for (int head = 0; head < RF_HEADS; head++) {
        mask[offset] = 1;
        for (int action = 1; action < RF_ACTION_DIMS[head]; action++) {
            int allowed = 1;
            if (head <= RF_COMBO) allowed = riskfight_inventory_head_accepts(p, head, action - 1);
            if (head == RF_VENGEANCE) allowed = !p->veng_active && p->veng_cooldown <= 1 &&
                p->current_magic >= OSRS_VENGEANCE_MAGIC_LEVEL && s->inventory_use[agent].vengeance_sacks > 0;
            if (head == RF_SPECIAL) allowed = p->special_energy >= action * 50;
            if (head == RF_PRIMARY && action == RF_TELEPORT) {
                allowed = osrs_teleport_allowed(s->env.pvp_runtime.teleport[agent], s->env.tick);
                int has_teleport = 0;
                for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
                    has_teleport |= osrs_inventory_cell_metadata(&p->inventory_cells[slot])->click_action == OSRS_CLICK_TELEPORT;
                allowed &= has_teleport;
            }
            if (head == RF_PRAYER) allowed = p->current_prayer > 0;
            mask[offset + action] = allowed;
        }
        offset += RF_ACTION_DIMS[head];
    }
    assert(offset == RF_MASK_SIZE);
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

static void riskfight_script(const float* obs, RiskfightOpponent type, int* actions) {
    memset(actions, 0, RF_HEADS * sizeof(int));
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
        if (!actions[RF_DRINK] && hp >= threshold && hp > OSRS_DIVINE_DAMAGE &&
                (attack < 110 || strength < 110 || defence < 110))
            actions[RF_DRINK] = riskfight_find_kind(obs, OSRS_CONSUMABLE_DIVINE_COMBAT);
    }
    if (!obs[11] && obs[12] <= 0.02f) actions[RF_VENGEANCE] = 1;
    uint8_t weapon = hp < 70 ? ITEM_DHAROKS_GREATAXE : ITEM_ABYSSAL_TENTACLE;
    if (type == RISKFIGHT_AGGRESSIVE && obs[6] >= 0.5f && opponent[0] < 0.65f) {
        weapon = obs[6] >= 1 && opponent[0] < 0.4f ? ITEM_GRANITE_MAUL_ORNATE : ITEM_VOIDWAKER;
        actions[RF_SPECIAL] = weapon == ITEM_GRANITE_MAUL_ORNATE ? 2 : 1;
    }
    actions[RF_WEAPON] = riskfight_find_gear(obs, weapon);
    if (!item_is_two_handed(weapon))
        actions[RF_SHIELD] = riskfight_find_gear(obs, ITEM_AVERNIC_DEFENDER);
    if (type == RISKFIGHT_AGGRESSIVE && opponent[4] && opponent[6] > 2 && hp > 55 && hp < 100)
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
