#ifndef OSRS_RISKFIGHT_POLICY_H
#define OSRS_RISKFIGHT_POLICY_H
#include "riskfight_model.h"

enum {
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
        (float)s->env.pvp_runtime.maul[agent].prepared_hits / 2.0f,
        (float)max_int(0, s->env.pvp_runtime.teleport[agent].blocked_until_tick - s->env.tick) /
            OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS,
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
    opponent[7] = (float)opp->veng_active;
    opponent[8] = opp->veng_cooldown / 50.0f;
    opponent[9] = opp->special_energy / 100.0f;
    opponent[10] = v->consume_delay_est / 10.0f;
    opponent[11] = v->consume_food_est / 3.0f;
    opponent[12] = v->consume_potion_est / 3.0f;
    opponent[13] = v->consume_karam_est / 3.0f;
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

static void riskfight_write_action_mask(const RiskfightState* s, int agent, unsigned char* mask) {
    const Player* p = &s->env.players[agent];
    memset(mask, 0, RF_MASK_SIZE);
    unsigned char* heads[RF_HEADS];
    unsigned char* gear_masks[NUM_GEAR_SLOTS];
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
    for (int action = 1; action < RF_ACTION_DIMS[RF_OVERHEAD]; action++)
        heads[RF_OVERHEAD][action] = p->current_prayer > 0;
}

/** First inventory slot (1-based, 0 when absent) per consumable kind and per item. */
typedef struct {
    int kind_slot[OSRS_CONSUMABLE_COUNT];
    uint8_t item_slot[ITEM_NONE + 1];
} RiskfightObservedInventory;

static RiskfightObservedInventory riskfight_observed_inventory(const float* obs) {
    RiskfightObservedInventory inventory = {{0}};
    for (int slot = OSRS_INVENTORY_SIZE - 1; slot >= 0; slot--) {
        const OsrsItemContentMetadata* m = osrs_item_content_metadata(
            osrs_inventory_cell_obs_code_decode(obs[RF_INVENTORY_START + slot * RF_INVENTORY_WIDTH]));
        inventory.kind_slot[m->consumable_kind] = slot + 1;
        inventory.item_slot[m->item_idx] = slot + 1;
    }
    return inventory;
}

typedef struct {
    int hitpoints, prayer, attack, strength, defence, magic;
    int attack_timer, food_timer, potion_timer, karambwan_timer;
    uint8_t weapon;
} RiskfightObservedSelf;

static RiskfightObservedSelf riskfight_observed_self(const float* obs) {
    return (RiskfightObservedSelf){
        .hitpoints = (int)lroundf(obs[0] * 121), .prayer = (int)lroundf(obs[1] * 99),
        .attack = (int)lroundf(obs[2] * 118), .strength = (int)lroundf(obs[3] * 118),
        .defence = (int)lroundf(obs[4] * 120), .magic = (int)lroundf(obs[5] * 99),
        .attack_timer = (int)lroundf(obs[7] * 10), .food_timer = (int)lroundf(obs[8] * 3),
        .potion_timer = (int)lroundf(obs[9] * 3), .karambwan_timer = (int)lroundf(obs[10] * 3),
        .weapon = (uint8_t)lroundf(obs[RF_EQUIPPED_START + GEAR_SLOT_WEAPON] * RF_OBSERVATION_ITEM_SCALE),
    };
}

/** Per-episode script roll source. Scripts stay pure functions of (obs, profile, seed). */
static uint32_t riskfight_sample_hash(uint32_t seed, uint32_t tick, uint32_t salt) {
    uint32_t x = seed ^ (tick * 0x9E3779B1u + salt * 0x85EBCA6Bu + 0xC2B2AE35u);
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
    return x;
}
static int riskfight_sample_event(uint32_t seed, uint32_t tick, uint32_t salt, int per_mille) {
    return (int)(riskfight_sample_hash(seed, tick, salt) % 1000u) < per_mille;
}

/** Heals below 73 HP, a restore when drained, a combat repot when the boost has faded. */
static void riskfight_script_consume(const RiskfightObservedSelf* self,
    const RiskfightObservedInventory* inventory, int* actions) {
    int hp = self->hitpoints;
    if (hp < 73 && self->food_timer == 0) {
        actions[RF_FOOD] = inventory->kind_slot[OSRS_CONSUMABLE_MARLIN];
        if (!actions[RF_FOOD]) actions[RF_FOOD] = inventory->kind_slot[OSRS_CONSUMABLE_SUMMER_PIE];
    }
    if (hp < 45 && self->karambwan_timer == 0) actions[RF_COMBO] = inventory->kind_slot[OSRS_CONSUMABLE_HALIBUT];
    if (self->potion_timer != 0) return;
    if (hp < 65) actions[RF_DRINK] = inventory->kind_slot[OSRS_CONSUMABLE_BREW];
    if (actions[RF_DRINK]) return;
    int drained = self->prayer <= 40 || self->attack < 99 || self->strength < 99 ||
        self->defence < 99 || self->magic < 99;
    if (drained) {
        actions[RF_DRINK] = inventory->kind_slot[OSRS_CONSUMABLE_SANFEW];
        if (!actions[RF_DRINK]) actions[RF_DRINK] = inventory->kind_slot[OSRS_CONSUMABLE_SUPER_RESTORE];
    } else if (hp >= 65 && (self->attack < 110 || self->strength < 110 || self->defence < 110)) {
        actions[RF_DRINK] = inventory->kind_slot[OSRS_CONSUMABLE_SUPER_COMBAT];
    }
}

/** Weapon rolls are keyed on the tick the next attack comes off cooldown, so one attack gets one decision.
 * consume_ticks counts this round's ticks on which the fighter ate or drank. */
static void riskfight_script(const float* obs, const RiskfightProfile* profile, uint32_t seed,
    int consume_ticks, int* actions) {
    memset(actions, 0, RF_HEADS * sizeof(int));
    uint32_t tick = (uint32_t)lroundf(obs[20] * RF_OBSERVATION_TICK_SCALE);
    RiskfightObservedSelf self = riskfight_observed_self(obs);
    RiskfightObservedInventory inventory = riskfight_observed_inventory(obs);
    uint32_t attack_tick = tick + (uint32_t)self.attack_timer;
    int hp = self.hitpoints;
    const float* opponent = obs + RF_OPPONENT_START + NUM_GEAR_SLOTS;
    actions[RF_PRIMARY] = RF_ATTACK;
    actions[RF_PRAYER] = OFFENSIVE_PRAYER_PIETY;
    actions[RF_STYLE] = FIGHT_STYLE_AGGRESSIVE;
    int consume_band = (hp >= 40) + (hp >= 65) + (hp >= 73) + (hp >= 90);
    if (riskfight_sample_event(seed, tick, 11, profile->consume_pm[consume_band]))
        riskfight_script_consume(&self, &inventory, actions);
    if (actions[RF_FOOD] || actions[RF_DRINK] || actions[RF_COMBO]) actions[RF_PRIMARY] = RF_STOP;
    actions[RF_VENGEANCE] = !obs[11] && obs[12] <= 0.02f && riskfight_sample_event(seed, tick, 13, profile->veng_pm);

    int opp_bar = (int)lroundf(opponent[0] * OSRS_PLAYER_HEALTH_BAR_SCALE);
    OsrsHealthBarRange opp_hp = osrs_health_bar_range(opp_bar, OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 121);
    int opp_upper = opp_hp.kind == OSRS_HEALTH_BAR_KNOWN ? opp_hp.upper : 121;
    int holding_maul = pvp_is_maul(self.weapon);
    int axe_band = (opp_bar >= 12) + (opp_bar >= 24);
    int axe_swing = hp < 99 &&
        riskfight_sample_event(seed, attack_tick, 23, profile->axe_pm[axe_band]);
    int vw_spec = obs[6] >= 0.5f && opponent[0] < 0.65f &&
        riskfight_sample_event(seed, attack_tick, 21, profile->vw_pm);
    int maul_spec = !vw_spec && obs[6] >= 0.99f && opponent[0] < 0.65f && opp_upper <= 76 && hp < 99 &&
        riskfight_sample_event(seed, tick, 22, profile->maul_pm);
    uint8_t weapon = vw_spec ? ITEM_VOIDWAKER : maul_spec ? ITEM_GRANITE_MAUL_ORNATE :
        axe_swing ? ITEM_DHAROKS_GREATAXE : ITEM_ABYSSAL_TENTACLE;
    actions[RF_SPECIAL] = maul_spec ? 2 : !holding_maul && vw_spec != (obs[13] >= 0.5f);
    actions[RF_WEAPON] = inventory.item_slot[weapon];
    if (!item_is_two_handed(weapon))
        actions[RF_SHIELD] = inventory.item_slot[ITEM_AVERNIC_DEFENDER];
    actions[RF_ORB] = weapon == ITEM_DHAROKS_GREATAXE && self.attack_timer == 1 && hp > 20 &&
        riskfight_sample_event(seed, attack_tick, 24, profile->orb_axe_pm);
    actions[RF_RING] = inventory.item_slot[obs[7] > 0.1f ? ITEM_RING_OF_RECOIL : ITEM_ULTOR_RING];
    if (obs[RF_SELF_TELEPORT_LOCK] > 0) return;
    int supply_band = min_int(consume_ticks / RISKFIGHT_TAB_SUPPLY_TICKS, RISKFIGHT_TAB_SUPPLY_BANDS - 1);
    int out_of_heals = !inventory.kind_slot[OSRS_CONSUMABLE_MARLIN] &&
        !inventory.kind_slot[OSRS_CONSUMABLE_SUMMER_PIE] &&
        !inventory.kind_slot[OSRS_CONSUMABLE_HALIBUT] && !inventory.kind_slot[OSRS_CONSUMABLE_BREW];
    if ((hp < 30 && out_of_heals) ||
            riskfight_sample_event(seed, tick, 31, profile->tab_pm[supply_band][hp < 40]))
        actions[RF_PRIMARY] = RF_TELEPORT;
}
#endif
