#ifndef OSRS_RISKFIGHT_POLICY_H
#define OSRS_RISKFIGHT_POLICY_H
#include "riskfight_model.h"
#include "riskfight_bag.h"
#include "riskfight_norms.h"

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

/** Per content code: obs code, heal fraction, food kind, combo kind, dose fraction. Built once at load. */
static float RF_INVENTORY_ROWS[OSRS_ITEM_CONTENT_COUNT][RF_INVENTORY_WIDTH - 1];

__attribute__((constructor)) static void riskfight_build_inventory_rows(void) {
    for (int code = 0; code < OSRS_ITEM_CONTENT_COUNT; code++) {
        const OsrsItemContentMetadata* m = osrs_item_content_metadata((uint16_t)code);
        float* row = RF_INVENTORY_ROWS[code];
        row[0] = osrs_inventory_cell_obs_code_encode((uint16_t)code);
        row[1] = osrs_consumable_hp_heal_amount((OsrsConsumableKind)m->consumable_kind,
            OSRS_ITEM_OBS_TABLE_BASE_HITPOINTS) / 121.0f;
        row[2] = m->click_action == OSRS_CLICK_EAT ?
            (m->consumable_kind == OSRS_CONSUMABLE_SUMMER_PIE ? 1.0f :
             m->consumable_kind == OSRS_CONSUMABLE_HALIBUT ? 2.0f : 3.0f) / 3.0f : 0;
        row[3] = m->click_action == OSRS_CLICK_EAT ?
            (m->consumable_kind == OSRS_CONSUMABLE_HALIBUT ? 2.0f : 3.0f) / 3.0f : 0;
        row[4] = m->dose_count / 4.0f;
    }
}

/** His chance range: the most damage he could deal us before our next meal resolves, read off the threat block `threat` (riskfight_threat_write of the observer):
 * the larger of the normal max hit and, with the energy for it (his estimated `spec_est`; a Behead with five stacks), the special's max over every weapon he wields
 * or has swung, plus, while his Vengeance is up, the echo of our own best hit (3/4). The ring of recoil is not visible, so its reflection is not counted. */
static int riskfight_chance_range_of(const float* threat, int spec_est, int his_veng) {
    int range = 0;
    const int stacks_full = threat[RF_THREAT_STACKS_OFFSET] * OSRS_SOUL_STACKS_MAX > OSRS_SOUL_STACKS_MAX - 0.5f;
    for (int i = 0; i < RF_THREAT_HIS_SLOTS; i++) {
        const float* o = threat + i * RF_THREAT_HIS_WIDTH;
        if (o[0] < 1) continue; /* a weapon the pool only says he may carry */
        int hit = (int)lroundf(o[2] * RF_THREAT_DAMAGE_SCALE);
        int spec = (int)lroundf(o[9] * RF_THREAT_DAMAGE_SCALE);
        int cost = (int)lroundf(o[8] * 100);
        if (spec > hit && (cost > 0 ? spec_est >= cost : stacks_full)) hit = spec;
        if (hit > range) range = hit;
    }
    if (his_veng) range += (int)lroundf(threat[RF_THREAT_OURS_OFFSET + 1] * RF_THREAT_DAMAGE_SCALE) * 3 / 4;
    return range;
}

/** The chance range of `agent`'s opponent against `agent` as the sim holds it now (written the way the observation writes it). */
static int riskfight_chance_range(const RiskfightState* s, int agent) {
    float threat[RF_THREAT_SIZE];
    RiskfightBagWeapons none = {{0}, 0};
    const RiskfightVisibleOpponent* v = &s->visible[agent];
    riskfight_threat_write(&s->env.players[agent], &none, &v->threat, v->health_bar, threat);
    return riskfight_chance_range_of(threat, v->spec_est, v->last_cast_tick >= v->last_proc_tick);
}

/** The three floats of the session block that move while the session waits between fights (the sim is not stepped then): ticks left, wait left, reputation. */
static inline void riskfight_session_clock(const RiskfightState* s, float out[3]) {
    out[0] = s->session_ticks_left >= RF_SESSION_HORIZON_TICKS ? 1.0f : fmaxf(0.0f, (float)s->session_ticks_left / RF_SESSION_HORIZON_TICKS);
    out[1] = s->session_wait_left >= RF_WAIT_SCALE_TICKS ? 1.0f : (float)s->session_wait_left / RF_WAIT_SCALE_TICKS;
    out[2] = fminf(1.0f, s->session_rep / RF_REP_OBS_SCALE);
}

/** The session block (RF_SESSION_START). [2] and [3] mark a wait between fights and how much of it is left. [0] session ticks left over RF_SESSION_HORIZON_TICKS, capped at 1: what a fight is worth depends on the time left only
 * through how many more fights fit, which stops mattering a few fight cycles out, so an open-ended session (live) reads 1, the value most of a training
 * session shows too. [1] his special energy as a player counts it: whether he can still spec decides whether leaving after our own spec is rude. [4] our reputation
 * (RF_REP_OBS_SCALE rude teleports read 1): it lengthens the wait for a new opponent and makes ragging more likely. [7] 1 when a teleport now would be kosher
 * (riskfight_norms.h), 0 when it would be rude: it costs no rematch and a reputation point. A player knows this: his own bag and special energy, his estimate of the
 * other side's special energy ([1]) against the special weapons the threat block says he holds. In the live server the same code reads the G line's bag. */
static void riskfight_write_session_observation(const RiskfightState* s, int agent, float* obs) {
    float* out = obs + RF_SESSION_START;
    float clock[3];
    riskfight_session_clock(s, clock);
    out[0] = clock[0];
    out[1] = s->visible[agent].spec_est / 100.0f;
    out[2] = s->session_wait_left > 0;
    out[3] = clock[1];
    out[4] = clock[2];
    out[5] = fminf(1.0f, (float)s->overeats[agent] / (RF_OVEREAT_PATIENCE + 1));
    const RiskfightVisibleOpponent* v = &s->visible[agent];
    int range = riskfight_chance_range_of(obs + RF_THREAT_START, v->spec_est, v->last_cast_tick >= v->last_proc_tick);
    out[6] = fmaxf(-1.0f, fminf(1.0f, (float)(s->env.players[agent].current_hitpoints - range) / 121.0f));
    out[7] = riskfight_teleport_kosher_now(&s->env.players[agent], obs + RF_THREAT_START, v->spec_est);
}

/** The observation up to the price, and the weapons of the agent's bag when `bag_weapons` is given (the threat block needs them). The bag rows follow `order`. */
static void riskfight_write_observation_core(const RiskfightState* s, int agent, float* obs, const RiskfightBagOrder* order, RiskfightBagWeapons* bag_weapons) {
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
    assert(p->base_hitpoints == OSRS_ITEM_OBS_TABLE_BASE_HITPOINTS);
    for (int entry = 0; entry < OSRS_INVENTORY_SIZE; entry++) {
        uint16_t code = p->inventory_cells[order->slot[entry]].content_code;
        float* row = obs + RF_INVENTORY_START + entry * RF_INVENTORY_WIDTH;
        memcpy(row, RF_INVENTORY_ROWS[code], sizeof(RF_INVENTORY_ROWS[code]));
        const OsrsItemContentMetadata* m = osrs_item_content_metadata(code);
        row[5] = m->consumable_kind == OSRS_CONSUMABLE_VENGEANCE_SACK ?
            (float)use->vengeance_sacks / RF_OBSERVATION_SACK_SCALE : code != 0;
        if (bag_weapons && m->gear_slot == GEAR_SLOT_WEAPON && m->item_idx != p->equipped[GEAR_SLOT_WEAPON]) riskfight_threat_note_bag_weapon(bag_weapons, m->item_idx);
    }
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        obs[RF_EQUIPPED_START + slot] = (float)p->equipped[slot] / RF_OBSERVATION_ITEM_SCALE;
        obs[RF_OPPONENT_START + slot] = (float)v->equipment[slot] / RF_OBSERVATION_ITEM_SCALE;
    }
    float* opponent = obs + RF_OPPONENT_START + NUM_GEAR_SLOTS;
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
    int since_cast = s->env.tick - v->last_cast_tick;
    opponent[7] = since_cast < 50 && v->last_proc_tick < v->last_cast_tick;
    opponent[8] = since_cast < 50 ? since_cast / 50.0f : 0;
    opponent[9] = 0;
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
    obs[RF_PRICE] = s->death_price * 10.0f / RF_PRICE_MAX_X10;
}

/** What the scripts read: the observation without its appended blocks (they read nothing from them), at the cost of the base. The bag in physical slot order: scripts
 * click physical slots. */
static void riskfight_write_script_observation(const RiskfightState* s, int agent, float* obs) {
    riskfight_write_observation_core(s, agent, obs, &RF_BAG_PHYSICAL, NULL);
    riskfight_threat_write_stacks(&s->env.players[agent], &s->visible[agent].threat, obs + RF_THREAT_START); /* the Soulreaper stacks are the one thing a script reads there */
}

/** The full observation, the net's: the base with the bag in canonical order (riskfight_bag.h), then the appended blocks in order (threat features, session). */
static void riskfight_write_observation(const RiskfightState* s, int agent, float* obs) {
    RiskfightBagWeapons bag_weapons = {{0}, 0};
    const RiskfightBagOrder order = riskfight_bag_order(&s->env.players[agent]);
    riskfight_write_observation_core(s, agent, obs, &order, &bag_weapons);
    riskfight_threat_write(&s->env.players[agent], &bag_weapons, &s->visible[agent].threat, s->visible[agent].health_bar, obs + RF_THREAT_START);
    riskfight_write_session_observation(s, agent, obs);
}

/** Head offsets into the mask, and the mask every state starts from: action 0 of every head and all
 * actions of the non-inventory heads. Built once at load. */
static int RF_HEAD_OFFSET[RF_HEADS];
static unsigned char RF_MASK_TEMPLATE[RF_MASK_SIZE];

__attribute__((constructor)) static void riskfight_build_mask_template(void) {
    int offset = 0;
    for (int head = 0; head < RF_HEADS; head++) {
        RF_HEAD_OFFSET[head] = offset;
        RF_MASK_TEMPLATE[offset] = 1;
        if (RF_GEAR_SLOT_BY_HEAD[head] < 0 && head > RF_COMBO)
            memset(RF_MASK_TEMPLATE + offset, 1, RF_ACTION_DIMS[head]);
        offset += RF_ACTION_DIMS[head];
    }
    assert(offset == RF_MASK_SIZE);
}

/** The net's mask: its bag actions index the canonical order of riskfight_bag.h. */
static void riskfight_write_action_mask(const RiskfightState* s, int agent, unsigned char* mask) {
    const Player* p = &s->env.players[agent];
    const RiskfightBagOrder order = riskfight_bag_order(p);
    memcpy(mask, RF_MASK_TEMPLATE, RF_MASK_SIZE);
    unsigned char* heads[RF_HEADS];
    unsigned char* gear_masks[NUM_GEAR_SLOTS];
    int has_empty = osrs_first_empty_inventory_cell(p->inventory_cells, -1) >= 0;
    for (int head = 0; head < RF_HEADS; head++) {
        heads[head] = mask + RF_HEAD_OFFSET[head];
        int gear_slot = RF_GEAR_SLOT_BY_HEAD[head];
        if (gear_slot < 0) continue;
        gear_masks[gear_slot] = heads[head];
        heads[head][RF_UNEQUIP] = p->equipped[gear_slot] != ITEM_NONE && has_empty;
    }
    int has_teleport = 0, one_handed_switch = 0, has_shield = 0, two_handed_wieldable = 0;
    /* One spec of the wielded spec weapon, else of the cheapest spec weapon in the bag: the plain granite maul costs 60, the ornate 50. */
    int wielded_spec_cost = osrs_spec_cost(p->equipped[GEAR_SLOT_WEAPON]), bag_spec_cost = 0;
    for (int entry = 0; entry < OSRS_INVENTORY_SIZE; entry++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[order.slot[entry]]);
        if (m->gear_slot >= 0) {
            int allowed = osrs_can_equip_metadata(p, m, has_empty);
            gear_masks[m->gear_slot][entry + 1] = allowed;
            if (m->gear_slot == GEAR_SLOT_WEAPON) {
                int cost = osrs_spec_cost(m->item_idx);
                if (cost > 0 && (!bag_spec_cost || cost < bag_spec_cost)) bag_spec_cost = cost;
                if (!item_is_two_handed(m->item_idx)) one_handed_switch |= allowed;
                else two_handed_wieldable |= allowed;
            }
            if (m->gear_slot == GEAR_SLOT_SHIELD) has_shield = 1;
        }
        if (m->click_action == OSRS_CLICK_EAT) {
            int head = m->consumable_kind == OSRS_CONSUMABLE_HALIBUT || m->consumable_kind == OSRS_CONSUMABLE_KARAMBWAN ? RF_COMBO : RF_FOOD;
            /* An anglerfish at full hitpoints inside the PvP overheal lock heals nothing (the lock only forbids the overheal), like a shark at full: not offered. */
            int wasted = m->consumable_kind == OSRS_CONSUMABLE_ANGLERFISH && p->pvp_overheal_block > 0 && p->current_hitpoints >= p->base_hitpoints;
            heads[head][entry + 1] = !wasted && osrs_can_eat_consumable_kind(p, (OsrsConsumableKind)m->consumable_kind);
        }
        heads[RF_DRINK][entry + 1] = m->click_action == OSRS_CLICK_DRINK && p->potion_timer == 0 &&
            (m->consumable_kind != OSRS_CONSUMABLE_DIVINE_COMBAT || p->current_hitpoints > OSRS_DIVINE_DAMAGE);
        has_teleport |= m->click_action == OSRS_CLICK_TELEPORT;
    }
    if (has_shield && one_handed_switch && item_is_two_handed(p->equipped[GEAR_SLOT_WEAPON])) {
        for (int entry = 0; entry < OSRS_INVENTORY_SIZE; entry++)
            if (osrs_inventory_cell_metadata(&p->inventory_cells[order.slot[entry]])->gear_slot == GEAR_SLOT_SHIELD)
                heads[RF_SHIELD][entry + 1] = 1;
    }
    heads[RF_VENGEANCE][1] = !p->veng_active && p->veng_cooldown <= 1 &&
        p->current_magic >= OSRS_VENGEANCE_MAGIC_LEVEL && s->inventory_use[agent].vengeance_sacks > 0;
    int spec_cost = wielded_spec_cost > 0 ? wielded_spec_cost : bag_spec_cost > 0 ? bag_spec_cost : 50;
    for (int action = 1; action < RF_ACTION_DIMS[RF_SPECIAL]; action++)
        heads[RF_SPECIAL][action] = p->special_energy >= action * spec_cost;
    heads[RF_PRIMARY][RF_TELEPORT] = has_teleport &&
        osrs_teleport_allowed(s->env.pvp_runtime.teleport[agent], s->env.tick);
    for (int action = 1; action < RF_ACTION_DIMS[RF_PRAYER]; action++)
        heads[RF_PRAYER][action] = p->current_prayer > 0;
    for (int action = 1; action < RF_ACTION_DIMS[RF_OVERHEAD]; action++)
        heads[RF_OVERHEAD][action] = p->current_prayer > 0;
    if (riskfight_deployable) {
        for (int head = 0; head < RF_HEADS; head++) {
            int keep = head == RF_WEAPON || head == RF_SHIELD || head == RF_RING || (head >= RF_FOOD && head <= RF_COMBO) ||
                head == RF_VENGEANCE || head == RF_SPECIAL || head == RF_PRIMARY;
            if (!keep) memset(heads[head] + 1, 0, RF_ACTION_DIMS[head] - 1);
        }
        for (int action = 1; action < RF_ACTION_DIMS[RF_PRIMARY]; action++)
            heads[RF_PRIMARY][action] = action == RF_ATTACK || action == RF_STOP || (action == RF_TELEPORT && heads[RF_PRIMARY][RF_TELEPORT]);
        /* The shield comes off only to make way for a two-handed weapon in the bag that could be wielded; the weapon and the ring never come off to fight bare-handed. */
        heads[RF_SHIELD][RF_UNEQUIP] &= two_handed_wieldable;
        heads[RF_WEAPON][RF_UNEQUIP] = 0;
        heads[RF_RING][RF_UNEQUIP] = 0;
    }
}

/** The mask of a tick of waiting between fights: the no-op of every head and nothing else (the tick is a step of no choice). */
static void riskfight_write_wait_mask(unsigned char* mask) {
    memset(mask, 0, RF_MASK_SIZE);
    for (int head = 0; head < RF_HEADS; head++) mask[RF_HEAD_OFFSET[head]] = 1;
}

/** Everything riskfight_write_action_mask reads, reduced to what its result depends on (hitpoints only through the three comparisons it makes, the timers only
 * through "is zero", the veng cooldown only through "<= 1"). The mask changes on about one agent-tick in six, so the env, which keeps the last mask in the agent's
 * buffer, rewrites it only when the key differs. `valid` is 0 while the buffer holds something else (the wait mask); `buffer` guards against the buffer moving.
 * ADD EVERY NEW INPUT OF riskfight_write_action_mask HERE: test_riskfight_env_trace.c compares the buffer with a fresh mask every step. */
typedef struct {
    uint16_t cells[OSRS_INVENTORY_SIZE];
    uint8_t equipped[NUM_GEAR_SLOTS];
    uint8_t below_full, at_full, above_divine, food_ready, karambwan_ready, potion_ready, overheal_block, prayer, magic_ok, veng_active, veng_cooled, has_sacks,
        teleport_ok, deployable, valid;
    int special_energy;
    const unsigned char* buffer;
} RiskfightMaskKey;

static inline void riskfight_mask_key(const RiskfightState* s, int agent, const unsigned char* buffer, RiskfightMaskKey* key) {
    const Player* p = &s->env.players[agent];
    memset(key, 0, sizeof(*key));
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) key->cells[slot] = p->inventory_cells[slot].content_code;
    memcpy(key->equipped, p->equipped, NUM_GEAR_SLOTS);
    key->below_full = p->current_hitpoints < p->base_hitpoints;
    key->at_full = p->current_hitpoints >= p->base_hitpoints;
    key->above_divine = p->current_hitpoints > OSRS_DIVINE_DAMAGE;
    key->food_ready = p->food_timer == 0;
    key->karambwan_ready = p->karambwan_timer == 0;
    key->potion_ready = p->potion_timer == 0;
    key->overheal_block = p->pvp_overheal_block > 0;
    key->prayer = p->current_prayer > 0;
    key->magic_ok = p->current_magic >= OSRS_VENGEANCE_MAGIC_LEVEL;
    key->veng_active = p->veng_active != 0;
    key->veng_cooled = p->veng_cooldown <= 1;
    key->has_sacks = s->inventory_use[agent].vengeance_sacks > 0;
    key->teleport_ok = osrs_teleport_allowed(s->env.pvp_runtime.teleport[agent], s->env.tick);
    key->deployable = riskfight_deployable != 0;
    key->valid = 1;
    key->special_energy = p->special_energy;
    key->buffer = buffer;
}

/** riskfight_write_action_mask into the agent's buffer, skipped when `mask` still holds the mask of this very key. */
static inline void riskfight_write_action_mask_keyed(const RiskfightState* s, int agent, RiskfightMaskKey* held, unsigned char* mask) {
    RiskfightMaskKey key;
    riskfight_mask_key(s, agent, mask, &key);
    if (!memcmp(&key, held, sizeof(key))) return;
    riskfight_write_action_mask(s, agent, mask);
    *held = key;
}

/** First inventory slot (1-based, 0 when absent) per consumable kind and per item. */
typedef struct {
    int kind_slot[OSRS_CONSUMABLE_COUNT];
    uint8_t item_slot[ITEM_NONE + 1];
    uint8_t main_weapon, shield;
    uint8_t weapons[RF_KIT_WEAPONS + 1]; /* every weapon in the bag, in slot order */
    int weapon_count;
} RiskfightObservedInventory;

static int riskfight_is_switch_weapon(uint8_t weapon) {
    return weapon == ITEM_VOIDWAKER || weapon == ITEM_DHAROKS_GREATAXE || pvp_is_maul(weapon);
}

static RiskfightObservedInventory riskfight_observed_inventory(const float* obs) {
    RiskfightObservedInventory inventory = {{0}, {0}, ITEM_NONE, ITEM_NONE, {0}, 0};
    for (int slot = OSRS_INVENTORY_SIZE - 1; slot >= 0; slot--) {
        const OsrsItemContentMetadata* m = osrs_item_content_metadata(
            osrs_inventory_cell_obs_code_decode(obs[RF_INVENTORY_START + slot * RF_INVENTORY_WIDTH]));
        inventory.kind_slot[m->consumable_kind] = slot + 1;
        inventory.item_slot[m->item_idx] = slot + 1;
        if (m->gear_slot == GEAR_SLOT_WEAPON && !riskfight_is_switch_weapon(m->item_idx))
            inventory.main_weapon = m->item_idx;
        if (m->gear_slot == GEAR_SLOT_WEAPON && inventory.weapon_count <= RF_KIT_WEAPONS) inventory.weapons[inventory.weapon_count++] = m->item_idx;
        if (m->gear_slot == GEAR_SLOT_SHIELD) inventory.shield = m->item_idx;
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
        actions[RF_FOOD] = inventory->kind_slot[OSRS_CONSUMABLE_ANGLERFISH];
        if (!actions[RF_FOOD]) actions[RF_FOOD] = inventory->kind_slot[OSRS_CONSUMABLE_MARLIN];
        if (!actions[RF_FOOD]) actions[RF_FOOD] = inventory->kind_slot[OSRS_CONSUMABLE_SUMMER_PIE];
    }
    if (hp < 45 && self->karambwan_timer == 0) {
        actions[RF_COMBO] = inventory->kind_slot[OSRS_CONSUMABLE_KARAMBWAN];
        if (!actions[RF_COMBO]) actions[RF_COMBO] = inventory->kind_slot[OSRS_CONSUMABLE_HALIBUT];
    }
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
    int consume_band = (hp >= 40) + (hp >= 65) + (hp >= 73) + (hp >= 90);
    if (riskfight_sample_event(seed, tick, 11, profile->consume_pm[consume_band]))
        riskfight_script_consume(&self, &inventory, actions);
    if (actions[RF_FOOD] || actions[RF_DRINK] || actions[RF_COMBO]) actions[RF_PRIMARY] = RF_STOP;
    actions[RF_VENGEANCE] = !obs[11] && obs[12] <= 0.02f && riskfight_sample_event(seed, tick, 13, profile->veng_pm);

    int opp_bar = (int)lroundf(opponent[0] * OSRS_PLAYER_HEALTH_BAR_SCALE);
    OsrsHealthBarRange opp_hp = osrs_health_bar_range(opp_bar, OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 121);
    int opp_upper = opp_hp.kind == OSRS_HEALTH_BAR_KNOWN ? opp_hp.upper : 121;
    int holding_maul = pvp_is_maul(self.weapon);
    /* What his kit holds, read off what he wields and carries. Voidwaker, the mauls and the axe have plans of their own (below); every other weapon is a plain one:
     * the main weapon is the plain weapon with the cheapest special (a whip, else the crossbow whose own special is the lower), another plain weapon with a
     * special is the one he switches to for it. A kit of Voidwaker, mauls and axe alone keeps the old plan: Voidwaker main, the others for their specials. */
    uint8_t kit_weapons[RF_KIT_WEAPONS + 2];
    int kit_count = 0, has_vw = 0, has_axe = 0;
    uint8_t maul_item = ITEM_NONE, plain_main = ITEM_NONE, plain_spec = ITEM_NONE;
    kit_weapons[kit_count++] = self.weapon;
    for (int i = 0; i < inventory.weapon_count; i++) kit_weapons[kit_count++] = inventory.weapons[i];
    for (int i = 0; i < kit_count; i++) {
        uint8_t w = kit_weapons[i];
        if (w == ITEM_NONE) continue;
        has_vw |= w == ITEM_VOIDWAKER;
        has_axe |= w == ITEM_DHAROKS_GREATAXE;
        if (pvp_is_maul(w) && (maul_item == ITEM_NONE || w == ITEM_GRANITE_MAUL_ORNATE)) maul_item = w;
        if (riskfight_is_switch_weapon(w)) continue;
        int key = osrs_spec_cost(w) * 256 + w;
        if (plain_main == ITEM_NONE || key < osrs_spec_cost(plain_main) * 256 + plain_main) plain_main = w;
        if (plain_spec == ITEM_NONE || key > osrs_spec_cost(plain_spec) * 256 + plain_spec) plain_spec = w;
    }
    if (plain_spec == plain_main) plain_spec = ITEM_NONE;
    uint8_t generic_spec = plain_spec != ITEM_NONE ? plain_spec : plain_main != ITEM_NONE && osrs_spec_cost(plain_main) > 0 ? plain_main : ITEM_NONE;
    int generic_cost = generic_spec == ITEM_NONE ? 100 : osrs_spec_cost(generic_spec);
    int axe_band = (opp_bar >= 12) + (opp_bar >= 24);
    int axe_swing = has_axe && hp < 99 &&
        riskfight_sample_event(seed, attack_tick, 23, profile->axe_pm[axe_band]);
    int vw_spec = has_vw && obs[6] >= 0.5f && opponent[0] < 0.65f &&
        riskfight_sample_event(seed, attack_tick, 21, profile->vw_pm);
    int maul_cost = maul_item == ITEM_NONE ? 100 : osrs_spec_cost(maul_item);
    /* The ornate maul waits for the energy of two specials and lands both at once; the plain one costs 60 and gets one. */
    int maul_specs = maul_item == ITEM_NONE ? 0 : maul_item == ITEM_GRANITE_MAUL_ORNATE ? 2 : 1;
    int maul_spec = !vw_spec && maul_specs && obs[6] * 100 >= maul_cost * maul_specs - 1 && opponent[0] < 0.65f && opp_upper <= 76 && hp < 99 &&
        riskfight_sample_event(seed, tick, 22, profile->maul_pm);
    int generic_roll = generic_spec != ITEM_NONE && !vw_spec && !maul_spec && obs[6] * 100 >= generic_cost - 0.5f && opponent[0] < 0.65f &&
        riskfight_sample_event(seed, attack_tick, 25, profile->vw_pm);
    int voidwaker_main = plain_main == ITEM_NONE && inventory.main_weapon == ITEM_NONE && (self.weapon == ITEM_VOIDWAKER || inventory.item_slot[ITEM_VOIDWAKER]);
    uint8_t main_weapon = plain_main != ITEM_NONE ? plain_main : !riskfight_is_switch_weapon(self.weapon) ? self.weapon :
        voidwaker_main ? ITEM_VOIDWAKER : inventory.main_weapon != ITEM_NONE ? inventory.main_weapon : self.weapon;
    uint8_t weapon = vw_spec ? ITEM_VOIDWAKER : maul_spec ? maul_item : generic_roll ? generic_spec :
        axe_swing ? ITEM_DHAROKS_GREATAXE : main_weapon;
    /* Behead: with all five stacks held and the other side hurt, at the same rate as his other specials; it costs no energy. */
    int stacks = (int)lroundf(obs[RF_THREAT_START + RF_THREAT_STACKS_OFFSET + 1] * OSRS_SOUL_STACKS_MAX);
    int behead = weapon == ITEM_SOULREAPER_AXE && !vw_spec && !maul_spec && !generic_roll && stacks == OSRS_SOUL_STACKS_MAX && opponent[0] < 0.65f &&
        riskfight_sample_event(seed, attack_tick, 26, profile->vw_pm);
    int armed_wanted = vw_spec || generic_roll || behead;
    actions[RF_SPECIAL] = maul_spec ? maul_specs : !holding_maul && armed_wanted != (obs[13] >= 0.5f);
    /* Ranged weapons are fought on Rigour and rapid, the rest on Piety and aggressive. */
    int ranged_weapon = weapon != ITEM_NONE && get_item_attack_style(weapon) == ATTACK_STYLE_RANGED;
    actions[RF_PRAYER] = ranged_weapon ? OFFENSIVE_PRAYER_RIGOUR : OFFENSIVE_PRAYER_PIETY;
    actions[RF_STYLE] = ranged_weapon ? FIGHT_STYLE_RAPID : FIGHT_STYLE_AGGRESSIVE;
    actions[RF_WEAPON] = inventory.item_slot[weapon];
    if (!item_is_two_handed(weapon) && inventory.shield != ITEM_NONE)
        actions[RF_SHIELD] = inventory.item_slot[inventory.shield];
    actions[RF_ORB] = weapon == ITEM_DHAROKS_GREATAXE && self.attack_timer == 1 && hp > 20 &&
        riskfight_sample_event(seed, attack_tick, 24, profile->orb_axe_pm);
    actions[RF_RING] = inventory.item_slot[obs[7] > 0.1f ? ITEM_RING_OF_RECOIL : ITEM_ULTOR_RING];
    if (obs[RF_SELF_TELEPORT_LOCK] > 0) return;
    int supply_band = min_int(consume_ticks / RISKFIGHT_TAB_SUPPLY_TICKS, RISKFIGHT_TAB_SUPPLY_BANDS - 1);
    int out_of_heals = !inventory.kind_slot[OSRS_CONSUMABLE_ANGLERFISH] && !inventory.kind_slot[OSRS_CONSUMABLE_KARAMBWAN] &&
        !inventory.kind_slot[OSRS_CONSUMABLE_MARLIN] &&
        !inventory.kind_slot[OSRS_CONSUMABLE_SUMMER_PIE] &&
        !inventory.kind_slot[OSRS_CONSUMABLE_HALIBUT] && !inventory.kind_slot[OSRS_CONSUMABLE_BREW];
    if ((hp < 30 && out_of_heals) ||
            riskfight_sample_event(seed, tick, 31, profile->tab_pm[supply_band][hp < 40]))
        actions[RF_PRIMARY] = RF_TELEPORT;
}
#endif
