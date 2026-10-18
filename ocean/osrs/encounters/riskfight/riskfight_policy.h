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

/** His chance range: the most damage he could deal us in one tick before our next meal resolves, read off the threat block `threat` (riskfight_threat_write of
 * the observer), the larger of
 *   one opening hit with a weapon he wields or has swung: a normal max hit, or a special he has the energy for (his estimated `spec_est`; a Behead with five
 *   stacks), and
 *   every instant special (the granite maul's) his energy pays for, all landing on the same tick. The maul counts when the pool only says he may carry it (any
 *   slot of the block): it stays hidden in the bag until the special,
 * plus, while his Vengeance is up, the echo of our own best hit (3/4). The ring of recoil is not visible, so its reflection is not counted. The larger, not the sum:
 * the maul special follows his swing a tick later, so the opening's hitsplat and the maul's land on different ticks and we eat in between (the user's account; all
 * 11 recorded hit-then-maul combos, scripts/measure_combos.py: with his PID the opening lands on the swing's tick and the maul on the next, without it both a tick
 * later; the one shared tick is a dragon claws opening whose second pair lands with the maul, under the larger term too). Against a Soulreaper axe and a
 * maul at full energy this is two maul specials, about 90: where the user camps (85-92). `opening` 0 drops the opening hit (his clock does not free a swing yet,
 * riskfight_his_ready): only the instant specials, which ignore the clock, and the echo remain. */
static int riskfight_tick_damage_bound(const float* threat, int spec_est, int his_veng, int opening) {
    const int stacks_full = threat[RF_THREAT_STACKS_OFFSET] * OSRS_SOUL_STACKS_MAX > OSRS_SOUL_STACKS_MAX - 0.5f;
    int instant_cost = 0, instant_max = 0; /* the instant special with the most damage per energy */
    for (int i = 0; i < RF_THREAT_HIS_SLOTS; i++) {
        const float* o = threat + i * RF_THREAT_HIS_WIDTH;
        int cost = (int)lroundf(o[8] * 100), spec = (int)lroundf(o[9] * RF_THREAT_DAMAGE_SCALE);
        if (o[0] <= 0 || cost <= 0 || lroundf(o[11] * RF_THREAT_SPEED_SCALE) != 1) continue;
        if (!instant_cost || spec * instant_cost > instant_max * cost) { instant_cost = cost; instant_max = spec; }
    }
    int range = instant_cost ? spec_est / instant_cost * instant_max : 0;
    for (int i = 0; opening && i < RF_THREAT_HIS_SLOTS; i++) {
        const float* o = threat + i * RF_THREAT_HIS_WIDTH;
        if (o[0] < 1) continue;
        int hit = (int)lroundf(o[2] * RF_THREAT_DAMAGE_SCALE);
        int spec = (int)lroundf(o[9] * RF_THREAT_DAMAGE_SCALE);
        int cost = (int)lroundf(o[8] * 100);
        int best = hit;
        if ((cost > 0 ? spec_est >= cost : (spec > 0 && stacks_full)) && spec > best) best = spec;
        if (best > range) range = best;
    }
    if (his_veng) range += (int)lroundf(threat[RF_THREAT_OURS_OFFSET + 1] * RF_THREAT_DAMAGE_SCALE) * 3 / 4;
    return range;
}
static inline int riskfight_chance_range_of(const float* threat, int spec_est, int his_veng) {
    return riskfight_tick_damage_bound(threat, spec_est, his_veng, 1);
}

/** Whether his Vengeance counts as up for the chance range: up from the fight's start (fighters cast it before the fight: the user's recordings show a proc before any
 * cast in 107 of 123 fights) until it is seen down (a proc, or a hit landing on him for damage without one), then up again after a seen cast (riskfight_veng_seen). */
static inline int riskfight_his_veng_up(const RiskfightVisibleOpponent* v) {
    return v->veng_up;
}

/** The chance range of `agent`'s opponent against `agent` as the sim holds it now (written the way the observation writes it). */
static int riskfight_chance_range(const RiskfightState* s, int agent) {
    float threat[RF_THREAT_SIZE];
    RiskfightBagWeapons none = {{0}, 0};
    const RiskfightVisibleOpponent* v = &s->visible[agent];
    riskfight_threat_write(&s->env.players[agent], &none, &v->threat, v->health_bar, threat);
    return riskfight_chance_range_of(threat, v->spec_est, riskfight_his_veng_up(v));
}

/** The three floats of `agent`'s session block that move while the session waits between fights (the sim is not stepped then): ticks left, wait left, its reputation. */
static inline void riskfight_session_clock(const RiskfightState* s, int agent, float out[3]) {
    out[0] = s->session_ticks_left >= RF_SESSION_HORIZON_TICKS ? 1.0f : fmaxf(0.0f, (float)s->session_ticks_left / RF_SESSION_HORIZON_TICKS);
    out[1] = s->session_wait_left >= RF_WAIT_SCALE_TICKS ? 1.0f : (float)s->session_wait_left / RF_WAIT_SCALE_TICKS;
    out[2] = fminf(1.0f, s->session_rep[agent] / RF_REP_OBS_SCALE);
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
    riskfight_session_clock(s, agent, clock);
    out[0] = clock[0];
    out[1] = s->visible[agent].spec_est / 100.0f;
    out[2] = s->session_wait_left > 0;
    out[3] = clock[1];
    out[4] = clock[2];
    out[5] = fminf(1.0f, (float)s->overeats[agent] / (RF_OVEREAT_PATIENCE + 1));
    const RiskfightVisibleOpponent* v = &s->visible[agent];
    int range = riskfight_chance_range_of(obs + RF_THREAT_START, v->spec_est, riskfight_his_veng_up(v));
    out[6] = fmaxf(-1.0f, fminf(1.0f, (float)(s->env.players[agent].current_hitpoints - range) / 121.0f));
    out[7] = riskfight_teleport_kosher_now(&s->env.players[agent], obs + RF_THREAT_START, v->spec_est);
}

/** The timing block (RF_TIMING_START, layout in riskfight_model.h): his readiness and the most he can deal on the next tick, and his hitpoints bounds, from what a
 * viewer sees, written by the sim and the live server alike (riskfight_write_observation). Reads the threat block, so it comes after it. */
static void riskfight_write_timing_observation(const RiskfightState* s, int agent, float* obs) {
    const RiskfightVisibleOpponent* v = &s->visible[agent];
    float* out = obs + RF_TIMING_START;
    int ready_min = riskfight_his_ready(v, s->env.tick, 0);
    out[0] = fminf(1.0f, (float)ready_min / RF_OBSERVATION_ATTACK_AGE_SCALE);
    out[1] = fminf(1.0f, (float)riskfight_his_ready(v, s->env.tick, 1) / RF_OBSERVATION_ATTACK_AGE_SCALE);
    out[2] = fminf(1.0f, (float)riskfight_tick_damage_bound(obs + RF_THREAT_START, v->spec_est, riskfight_his_veng_up(v), ready_min <= 1) / RF_THREAT_DAMAGE_SCALE);
    out[3] = (float)riskfight_threat_his_hp_lower(v->health_bar) / RF_THREAT_DAMAGE_SCALE;
    out[4] = (float)riskfight_threat_his_hp_upper(v->health_bar) / RF_THREAT_DAMAGE_SCALE;
}

/** P(X >= need) for one attack X: 0 on a miss (chance 1 - `hit`), else uniform on the integers [lo, hi]. */
static inline double riskfight_attack_tail(double hit, int lo, int hi, int need) {
    if (need <= 0) return 1;
    if (need <= lo) return hit;
    if (need > hi) return 0;
    return hit * (hi - need + 1) / (hi - lo + 1);
}

/** The chance `attacks` (1 or 2) independent attacks of riskfight_attack_tail's law deal at least his hitpoints, uniform over the integers [hp_lower, hp_upper].
 * Two attacks: the first misses (the second alone must do it) or lands k uniform on [lo, hi] (the second must deal hitpoints - k), exact. */
static float riskfight_ko_chance(float hit, int lo, int hi, int attacks, int hp_lower, int hp_upper) {
    assert(attacks == 1 || attacks == 2);
    assert(0 <= lo && lo <= hi && hp_lower <= hp_upper);
    double sum = 0;
    for (int hp = hp_lower; hp <= hp_upper; hp++) {
        double alone = riskfight_attack_tail(hit, lo, hi, hp);
        if (attacks == 1) { sum += alone; continue; }
        double after_hit = 0;
        for (int k = lo; k <= hi; k++) after_hit += riskfight_attack_tail(hit, lo, hi, hp - k);
        sum += (1 - hit) * alone + hit * after_hit / (hi - lo + 1);
    }
    return (float)(sum / (hp_upper - hp_lower + 1));
}

/** The tactics block (RF_TACTICS_START, layout in riskfight_model.h), written by the sim and the live server alike (riskfight_write_observation). Reads the threat
 * block's OURS rows (`bag` the weapons that fill them) and his hitpoints bounds, so it comes after the threat block. */
static void riskfight_write_tactics_observation(const RiskfightState* s, int agent, const RiskfightBagWeapons* bag, float* obs) {
    const Player* p = &s->env.players[agent];
    const RiskfightVisibleOpponent* v = &s->visible[agent];
    float* out = obs + RF_TACTICS_START;
    memset(out, 0, RF_TACTICS_SIZE * sizeof(float));
    /* Our bag: the greataxe, the stripped Dharok's pieces, the orb, a two-hander we could wield. */
    int has_empty = osrs_first_empty_inventory_cell(p->inventory_cells, -1) >= 0;
    int axe = p->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE, orb = 0, can_wield_2h = 0;
    uint8_t stripped[NUM_GEAR_SLOTS];
    memset(stripped, ITEM_NONE, sizeof(stripped));
    for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[cell]);
        orb |= m->click_action == OSRS_CLICK_SELF_DAMAGE;
        if (m->gear_slot < 0) continue;
        axe |= m->item_idx == ITEM_DHAROKS_GREATAXE;
        if (m->gear_slot == GEAR_SLOT_WEAPON && item_is_two_handed(m->item_idx)) can_wield_2h |= osrs_can_equip_metadata(p, m, has_empty);
        uint8_t worn_piece = p->equipped[m->gear_slot];
        if ((m->gear_slot == GEAR_SLOT_BODY || m->gear_slot == GEAR_SLOT_LEGS) && (ITEM_DATABASE[m->item_idx].effect_mask & OSRS_ITEM_EFFECT_DHAROK_PIECE) &&
            (worn_piece == ITEM_NONE || !(ITEM_DATABASE[worn_piece].effect_mask & OSRS_ITEM_EFFECT_DHAROK_PIECE)))
            stripped[m->gear_slot] = m->item_idx;
    }
    out[RF_TACTICS_STRIPPED] = stripped[GEAR_SLOT_BODY] != ITEM_NONE || stripped[GEAR_SLOT_LEGS] != ITEM_NONE;
    int axe_max = 0;
    if (axe) {
        /* The set with the stripped pieces back on: their bonuses replace what the slot wears, each adds a Dharok's piece. */
        EquipmentBonuses worn;
        int dharok_armour;
        riskfight_threat_worn(p, &worn, &dharok_armour);
        uint8_t equipment[NUM_GEAR_SLOTS];
        memcpy(equipment, p->equipped, sizeof(equipment));
        const int pieces[2] = {GEAR_SLOT_BODY, GEAR_SLOT_LEGS};
        for (int i = 0; i < 2; i++) {
            int slot = pieces[i];
            if (stripped[slot] == ITEM_NONE) continue;
            if (equipment[slot] != ITEM_NONE) osrs_threat_bonus_item(&worn, equipment[slot], -1);
            osrs_threat_bonus_item(&worn, stripped[slot], 1);
            equipment[slot] = stripped[slot];
            dharok_armour++;
        }
        OsrsThreatActor us = riskfight_threat_actor_of(p);
        OsrsThreatWeapon w = osrs_threat_weapon_swapped(&worn, dharok_armour, equipment, ITEM_DHAROKS_GREATAXE, &us);
        for (int clicks = 0; clicks < 3; clicks++) {
            int hit_max = osrs_threat_max_hit_at(&w, RF_THREAT_BASE_HITPOINTS, max_int(1, p->current_hitpoints - 10 * clicks));
            if (clicks == 0) axe_max = hit_max;
            out[RF_TACTICS_AXE_MAX + clicks] = fminf(1.0f, (float)hit_max / RF_THREAT_DAMAGE_SCALE);
        }
    }
    /* The OURS rows: what each holds, and the rows of the weapons the knockout chances read. */
    const float *vw = NULL, *maul = NULL, *axe_row = NULL;
    const float* rows = obs + RF_THREAT_START + RF_THREAT_OURS_OFFSET;
    for (int i = 0; i <= bag->count; i++) {
        uint8_t item = i == 0 ? p->equipped[GEAR_SLOT_WEAPON] : bag->item[i - 1];
        const float* row = rows + i * RF_THREAT_OURS_WIDTH;
        int kind = item == ITEM_VOIDWAKER ? 0 : pvp_is_maul(item) ? 1 : item == ITEM_DHAROKS_GREATAXE ? 2 : 3;
        out[RF_TACTICS_IDENTITY + i * RF_TACTICS_KINDS + kind] = 1;
        if (kind == 0 && !vw) vw = row;
        if (kind == 1 && !maul) maul = row;
        if (kind == 2 && !axe_row) axe_row = row;
    }
    out[RF_TACTICS_CAN_WIELD_2H] = can_wield_2h;
    int lower = riskfight_threat_his_hp_lower(v->health_bar), upper = riskfight_threat_his_hp_upper(v->health_bar);
    float* ko = out + RF_TACTICS_KO;
    if (axe_row) ko[0] = riskfight_ko_chance(axe_row[2], 0, axe_max, 1, lower, upper);
    if (vw) {
        int normal_max = (int)lroundf(vw[1] * RF_THREAT_DAMAGE_SCALE), spec_max = (int)lroundf(vw[5] * RF_THREAT_DAMAGE_SCALE);
        ko[1] = riskfight_ko_chance(vw[2], 0, normal_max, 1, lower, upper);
        ko[2] = riskfight_ko_chance(vw[6], osrs_voidwaker_min_hit(normal_max), spec_max, 1, lower, upper);
    }
    if (maul) {
        int spec_max = (int)lroundf(maul[5] * RF_THREAT_DAMAGE_SCALE);
        ko[3] = riskfight_ko_chance(maul[6], 0, spec_max, 1, lower, upper);
        ko[4] = riskfight_ko_chance(maul[6], 0, spec_max, 2, lower, upper);
    }
    out[RF_TACTICS_HIS_VENG] = riskfight_his_veng_up(v);
    out[RF_TACTICS_ORB] = orb;
    out[RF_TACTICS_HP_ABOVE_1] = p->current_hitpoints > 1;
    /* Our clock in his_ready's convention: the step decrements the timer before the swing, which needs it below 0 (can_attack_now). */
    int ours = p->has_attack_timer ? max_int(0, p->attack_timer + 1) : 0;
    int window = riskfight_his_ready(v, s->env.tick, 0) - ours;
    out[RF_TACTICS_SAFE_WINDOW] = fmaxf(-1.0f, fminf(1.0f, (float)window / RF_OBSERVATION_ATTACK_AGE_SCALE));
    out[RF_TACTICS_HAND_BUSY] = s->hand_busy[agent] != 0;
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
    obs[RF_PRICE] = riskfight_seat_price(s, agent) * 10.0f / RF_PRICE_MAX_X10;
}

/** What the scripts read: the observation without its appended blocks (they read nothing from them), at the cost of the base. The bag in physical slot order: scripts
 * click physical slots. */
static void riskfight_write_script_observation(const RiskfightState* s, int agent, float* obs) {
    riskfight_write_observation_core(s, agent, obs, &RF_BAG_PHYSICAL, NULL);
    riskfight_threat_write_stacks(&s->env.players[agent], &s->visible[agent].threat, obs + RF_THREAT_START); /* the Soulreaper stacks are the one thing a script reads there */
}

/** The full observation, the net's: the base with the bag in canonical order (riskfight_bag.h), then the appended blocks in order (threat features, session, timing,
 * tactics). */
static void riskfight_write_observation(const RiskfightState* s, int agent, float* obs) {
    RiskfightBagWeapons bag_weapons = {{0}, 0};
    const RiskfightBagOrder order = riskfight_bag_order(&s->env.players[agent]);
    riskfight_write_observation_core(s, agent, obs, &order, &bag_weapons);
    riskfight_threat_write(&s->env.players[agent], &bag_weapons, &s->visible[agent].threat, s->visible[agent].health_bar, obs + RF_THREAT_START);
    riskfight_write_session_observation(s, agent, obs);
    riskfight_write_timing_observation(s, agent, obs);
    riskfight_write_tactics_observation(s, agent, &bag_weapons, obs);
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

/** Whether eating or drinking a bag consumable of `kind` now would change something (the user's rule), from the engine's own effect code:
 * - super restore, sanfew: a level it restores (attack, strength, defence, ranged, magic: encounter_apply_stat_restore) below base, or prayer missing at least the dose's prayer restore;
 *   prayer potion: that prayer rule alone (osrs_consumable_prayer_restore_amount, the amount osrs_player_inventory_drink_effect adds);
 * - super combat, divine super combat, ranging: a level it boosts below base + its boost (encounter_super_combat_boost, encounter_ranging_boost);
 * - brew: hitpoints below base + its heal (encounter_apply_brew_heal). Its defence boost alone is no reason: a brew above the heal cap drains attack and strength
 *   for at most a few points of defence, and the user counts it as over-eating (live, s15t85 sipped brews at 120 HP to lift defence 118 to 120);
 * - food, karambwan: a heal above 0 (osrs_eat_food: below base, an anglerfish outside the overheal lock below base + 22); a partial heal counts.
 * A drink the riskfight engine gives no effect (osrs_player_inventory_drink_effect aborts on it) is never useful. */
static int riskfight_consumable_useful(const Player* p, OsrsConsumableKind kind) {
    switch (kind) {
        case OSRS_CONSUMABLE_SUPER_RESTORE:
        case OSRS_CONSUMABLE_SANFEW:
            if (p->current_attack < p->base_attack || p->current_strength < p->base_strength || p->current_defence < p->base_defence ||
                p->current_ranged < p->base_ranged || p->current_magic < p->base_magic) return 1;
            /* fall through: the prayer rule */
        case OSRS_CONSUMABLE_PRAYER_RESTORE:
            return p->base_prayer - p->current_prayer >= osrs_consumable_prayer_restore_amount(kind, p->base_prayer);
        case OSRS_CONSUMABLE_SUPER_COMBAT:
        case OSRS_CONSUMABLE_DIVINE_COMBAT:
            return p->current_attack < p->base_attack + osrs_super_combat_boost_amount(p->base_attack) ||
                p->current_strength < p->base_strength + osrs_super_combat_boost_amount(p->base_strength) ||
                p->current_defence < p->base_defence + osrs_super_combat_boost_amount(p->base_defence);
        case OSRS_CONSUMABLE_RANGING:
            return p->current_ranged < p->base_ranged + osrs_ranging_boost_amount(p->base_ranged);
        case OSRS_CONSUMABLE_BREW:
            return p->current_hitpoints < p->base_hitpoints + osrs_brew_heal_amount(p->base_hitpoints);
        case OSRS_CONSUMABLE_MARLIN:
        case OSRS_CONSUMABLE_HALIBUT:
        case OSRS_CONSUMABLE_SUMMER_PIE:
        case OSRS_CONSUMABLE_SHARK_FOOD:
        case OSRS_CONSUMABLE_ANGLERFISH:
        case OSRS_CONSUMABLE_KARAMBWAN:
            return osrs_eat_food(osrs_consumable_food_type(kind), p->current_hitpoints, p->base_hitpoints, 0, p->pvp_overheal_block > 0).hp_healed > 0;
        default:
            return 0;
    }
}

/** The weapon a special-attack action spends energy on, and its cost: the wielded weapon when its special costs energy, else the cheapest spec weapon of the bag (the plain
 * granite maul costs 60, the ornate 50; the lower item index on a tie), else the wielded weapon at 50. */
typedef struct { uint8_t item; int cost; } RiskfightSpecWeapon;

static RiskfightSpecWeapon riskfight_spec_weapon(const Player* p) {
    uint8_t wielded = p->equipped[GEAR_SLOT_WEAPON];
    int wielded_cost = osrs_spec_cost(wielded);
    if (wielded_cost > 0) return (RiskfightSpecWeapon){wielded, wielded_cost};
    RiskfightSpecWeapon bag = {wielded, 0};
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
        if (m->gear_slot != GEAR_SLOT_WEAPON) continue;
        int cost = osrs_spec_cost(m->item_idx);
        if (cost > 0 && (!bag.cost || cost < bag.cost || (cost == bag.cost && m->item_idx < bag.item))) bag = (RiskfightSpecWeapon){m->item_idx, cost};
    }
    if (!bag.cost) bag.cost = 50;
    return bag;
}

/** The most our hits already in flight to him can deal, from what a player knows of them, never their rolled damage (hidden until the hitsplat; masking on it would
 * leak the roll): a direct hit at most the largest normal max of our weapons (the wielded one and the bag's, as the OURS rows rate them), a special hit the largest
 * spec max of them (a multi-hit special counts its whole max per hit: an upper bound); a recoil or Vengeance hit of ours carries the damage it echoes, which was seen.
 * The sim and the live server read the same fields (kind, is_special) and the same weapons. */
static int riskfight_in_flight_max(const RiskfightState* s, int agent) {
    const Player* p = &s->env.players[agent];
    int bar = s->visible[agent].health_bar, total = 0, normal = -1, special = -1;
    for (int i = 0; i < p->num_pending_hits; i++) {
        const PendingHit* h = &p->pending_hits[i];
        if (h->kind != OSRS_HIT_DIRECT) { total += h->damage; continue; }
        if (normal < 0) {
            RiskfightOurMaxes most = riskfight_threat_our_maxes(p, p->equipped[GEAR_SLOT_WEAPON], bar);
            for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
                const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
                if (m->gear_slot != GEAR_SLOT_WEAPON) continue;
                RiskfightOurMaxes w = riskfight_threat_our_maxes(p, m->item_idx, bar);
                if (w.max > most.max) most.max = w.max;
                if (w.spec_max > most.spec_max) most.spec_max = w.spec_max;
            }
            normal = most.max;
            special = most.spec_max;
        }
        total += h->is_special ? special : normal;
    }
    return total;
}

/** The two blatant mistakes the mask removes (the user's rules), read off what a player knows: the threat block's numbers and his health bar.
 * - meals: a meal (food, karambwan, pie bite, brew dose) is offered only while our hitpoints are at most his chance range (riskfight_chance_range): it is only ever
 *   eaten to stay out of his reach.
 * - spec_ko: bit `k` set when special action k (k SPEC_TOGGLE clicks, riskfight_policy_commands) may be taken. With a granite maul (an instant special per click) it
 *   is k specials: allowed when k x its spec max plus the most our hits in flight can deal (riskfight_in_flight_max) reaches the lower bound of his hitpoints from his
 *   bar (RF_THREAT_BASE_HITPOINTS while no bar is known). With any other weapon the clicks flip the armed bar: an action that leaves it armed (armed now XOR k odd)
 *   is allowed when one spec max plus the in-flight reach does; one that leaves it disarmed is always allowed (disarming is never blocked). The spec max is the OURS
 *   row's of the weapon the action would use (riskfight_spec_weapon). */
typedef struct { uint8_t meals, spec_ko; } RiskfightThreatGates;

static RiskfightThreatGates riskfight_threat_gates(const RiskfightState* s, int agent, RiskfightSpecWeapon spec) {
    const Player* p = &s->env.players[agent];
    const RiskfightVisibleOpponent* v = &s->visible[agent];
    RiskfightThreatGates gates = {p->current_hitpoints <= riskfight_chance_range(s, agent), 0};
    int spec_max = riskfight_threat_our_maxes(p, spec.item, v->health_bar).spec_max;
    int reach = riskfight_in_flight_max(s, agent), his_lower = riskfight_threat_his_hp_lower(v->health_bar);
    int instant = pvp_is_maul(spec.item);
    for (int k = 1; k < RF_ACTION_DIMS[RF_SPECIAL]; k++) {
        int armed_after = p->spec_armed != (k & 1);
        int allowed = instant ? k * spec_max + reach >= his_lower : !armed_after || spec_max + reach >= his_lower;
        if (allowed) gates.spec_ko |= 1u << k;
    }
    return gates;
}

/** The special toggles a decision clicks (`actions` on physical slots). The mask gates them on the weapon wielded when it is written (riskfight_spec_weapon), but
 * when the same decision wields an instant-spec weapon (a granite maul) from the bag the toggles land after the wield and spend the maul's energy. They are dropped
 * when that maul's own knockout gate is shut (riskfight_threat_gates' instant rule: k x its spec max plus our in-flight reach below the lower bound of his
 * hitpoints). The sim's step and gs_rl_server's records both go through this. */
static int riskfight_spec_clicks(const RiskfightState* s, int agent, const int* actions) {
    int k = actions[RF_SPECIAL], wield = actions[RF_WEAPON];
    if (!k || !wield || wield == RF_UNEQUIP) return k;
    const Player* p = &s->env.players[agent];
    uint8_t item = osrs_inventory_cell_metadata(&p->inventory_cells[wield - 1])->item_idx;
    if (!pvp_is_maul(item)) return k;
    const RiskfightVisibleOpponent* v = &s->visible[agent];
    int spec_max = riskfight_threat_our_maxes(p, item, v->health_bar).spec_max;
    return k * spec_max + riskfight_in_flight_max(s, agent) >= riskfight_threat_his_hp_lower(v->health_bar) ? k : 0;
}

/** Whether RF_ATTACK clicks the target: when we are not attacking him already (a weapon switch keeps the interaction, so there is nothing to re-click), or when the
 * decision clicks the special (`specials` toggles, riskfight_spec_clicks): the target click releases a granite maul's prepared specials (osrs_granite_maul.h) and
 * is how a player double-specs. The sim's step and gs_rl_server's records both go through this. */
static int riskfight_attack_click(const Player* p, int specials) {
    return !osrs_interaction_active(&p->interaction) || specials > 0;
}

/** The mask of a tick of waiting between fights, or of a decision our hand cannot click on (RiskfightState.hand_busy): the no-op of every head and nothing else
 * (the tick is a step of no choice). */
static void riskfight_write_wait_mask(unsigned char* mask) {
    memset(mask, 0, RF_MASK_SIZE);
    for (int head = 0; head < RF_HEADS; head++) mask[RF_HEAD_OFFSET[head]] = 1;
}

/** The net's mask: its bag actions index the canonical order of riskfight_bag.h. While our hand is busy, the wait mask. */
static void riskfight_write_action_mask(const RiskfightState* s, int agent, unsigned char* mask) {
    if (s->hand_busy[agent]) {
        riskfight_write_wait_mask(mask);
        return;
    }
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
    int has_teleport = 0, has_orb = 0, one_handed_switch = 0, has_shield = 0, two_handed_wieldable = 0;
    const RiskfightSpecWeapon spec = riskfight_spec_weapon(p);
    const RiskfightThreatGates gates = riskfight_threat_gates(s, agent, spec);
    for (int entry = 0; entry < OSRS_INVENTORY_SIZE; entry++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[order.slot[entry]]);
        if (m->gear_slot >= 0) {
            int allowed = osrs_can_equip_metadata(p, m, has_empty);
            gear_masks[m->gear_slot][entry + 1] = allowed;
            if (m->gear_slot == GEAR_SLOT_WEAPON) {
                if (!item_is_two_handed(m->item_idx)) one_handed_switch |= allowed;
                else two_handed_wieldable |= allowed;
            }
            if (m->gear_slot == GEAR_SLOT_SHIELD) has_shield = 1;
        }
        if (m->click_action == OSRS_CLICK_EAT) {
            int head = m->consumable_kind == OSRS_CONSUMABLE_HALIBUT || m->consumable_kind == OSRS_CONSUMABLE_KARAMBWAN ? RF_COMBO : RF_FOOD;
            /* A meal that heals nothing is not offered: an anglerfish at full hitpoints inside the PvP overheal lock (the lock only forbids the overheal) like a shark at full.
             * Nor is one above his chance range (riskfight_threat_gates). */
            heads[head][entry + 1] = gates.meals && riskfight_consumable_useful(p, (OsrsConsumableKind)m->consumable_kind) &&
                osrs_can_eat_consumable_kind(p, (OsrsConsumableKind)m->consumable_kind);
        }
        heads[RF_DRINK][entry + 1] = m->click_action == OSRS_CLICK_DRINK && p->potion_timer == 0 &&
            riskfight_consumable_useful(p, (OsrsConsumableKind)m->consumable_kind) &&
            (m->consumable_kind != OSRS_CONSUMABLE_BREW || gates.meals) &&
            (m->consumable_kind != OSRS_CONSUMABLE_DIVINE_COMBAT || p->current_hitpoints > OSRS_DIVINE_DAMAGE);
        has_teleport |= m->click_action == OSRS_CLICK_TELEPORT;
        has_orb |= m->click_action == OSRS_CLICK_SELF_DAMAGE;
    }
    if (has_shield && one_handed_switch && item_is_two_handed(p->equipped[GEAR_SLOT_WEAPON])) {
        for (int entry = 0; entry < OSRS_INVENTORY_SIZE; entry++)
            if (osrs_inventory_cell_metadata(&p->inventory_cells[order.slot[entry]])->gear_slot == GEAR_SLOT_SHIELD)
                heads[RF_SHIELD][entry + 1] = 1;
    }
    /* The orb's Feel only while it can take a hitpoint off: it never takes us below 1 (osrs_player_self_damage). */
    heads[RF_ORB][1] = has_orb && p->current_hitpoints > 1;
    heads[RF_VENGEANCE][1] = !p->veng_active && p->veng_cooldown <= 1 &&
        p->current_magic >= OSRS_VENGEANCE_MAGIC_LEVEL && s->inventory_use[agent].vengeance_sacks > 0;
    for (int action = 1; action < RF_ACTION_DIMS[RF_SPECIAL]; action++)
        heads[RF_SPECIAL][action] = p->special_energy >= action * spec.cost && ((gates.spec_ko >> action) & 1);
    heads[RF_PRIMARY][RF_TELEPORT] = has_teleport &&
        osrs_teleport_allowed(s->env.pvp_runtime.teleport[agent], s->env.tick);
    for (int action = 1; action < RF_ACTION_DIMS[RF_PRAYER]; action++)
        heads[RF_PRAYER][action] = p->current_prayer > 0;
    for (int action = 1; action < RF_ACTION_DIMS[RF_OVERHEAD]; action++)
        heads[RF_OVERHEAD][action] = p->current_prayer > 0;
    if (riskfight_deployable) {
        for (int head = 0; head < RF_HEADS; head++) {
            int keep = head == RF_WEAPON || head == RF_SHIELD || head == RF_RING || (head >= RF_FOOD && head <= RF_COMBO) ||
                head == RF_ORB || head == RF_VENGEANCE || head == RF_SPECIAL || head == RF_PRIMARY || head == RF_BODY || head == RF_LEGS;
            if (!keep) memset(heads[head] + 1, 0, RF_ACTION_DIMS[head] - 1);
        }
        for (int action = 1; action < RF_ACTION_DIMS[RF_PRIMARY]; action++)
            heads[RF_PRIMARY][action] = action == RF_ATTACK || action == RF_STOP || (action == RF_TELEPORT && heads[RF_PRIMARY][RF_TELEPORT]);
        /* The shield comes off only to make way for a two-handed weapon in the bag that could be wielded; the weapon and the ring never come off to fight bare-handed. */
        heads[RF_SHIELD][RF_UNEQUIP] &= two_handed_wieldable;
        heads[RF_WEAPON][RF_UNEQUIP] = 0;
        heads[RF_RING][RF_UNEQUIP] = 0;
        /* Body and legs come off (the strip: he hits more often, into our Vengeance, and our hitpoints drop for a Dharok swing) and go back on from the bag: a bag
         * piece is offered only while that slot is bare, so it puts back what came off and never swaps one worn piece for another. */
        for (int head = RF_BODY; head <= RF_LEGS; head++)
            if (p->equipped[RF_GEAR_SLOT_BY_HEAD[head]] != ITEM_NONE) memset(heads[head] + 1, 0, OSRS_INVENTORY_SIZE);
    }
}

/** Everything riskfight_write_action_mask reads, reduced to what its result depends on (hitpoints only through the comparisons it makes, the timers only
 * through "is zero", the veng cooldown only through "<= 1", the levels and the overheal lock only through each bag slot's riskfight_consumable_useful bit,
 * the threat block, his bar and our hits in flight only through riskfight_threat_gates).
 * The mask changes on about one agent-tick in six, so the env, which keeps the last mask in the agent's
 * buffer, rewrites it only when the key differs. `valid` is 0 while the buffer holds something else (the wait mask); `buffer` guards against the buffer moving.
 * ADD EVERY NEW INPUT OF riskfight_write_action_mask HERE: test_riskfight_env_trace.c compares the buffer with a fresh mask every step. */
typedef struct {
    uint16_t cells[OSRS_INVENTORY_SIZE];
    uint8_t equipped[NUM_GEAR_SLOTS];
    uint8_t below_full, above_divine, food_ready, karambwan_ready, potion_ready, prayer, magic_ok, veng_active, veng_cooled, has_sacks,
        teleport_ok, orb_ok, deployable, hand_busy, valid;
    RiskfightThreatGates gates;
    uint32_t useful; /* bit `slot`: riskfight_consumable_useful of that physical slot's eat or drink item */
    int special_energy;
    const unsigned char* buffer;
} RiskfightMaskKey;

static inline void riskfight_mask_key(const RiskfightState* s, int agent, const unsigned char* buffer, RiskfightMaskKey* key) {
    const Player* p = &s->env.players[agent];
    memset(key, 0, sizeof(*key));
    int has_orb = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        key->cells[slot] = p->inventory_cells[slot].content_code;
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
        if ((m->click_action == OSRS_CLICK_EAT || m->click_action == OSRS_CLICK_DRINK) && riskfight_consumable_useful(p, (OsrsConsumableKind)m->consumable_kind))
            key->useful |= 1u << slot;
        has_orb |= m->click_action == OSRS_CLICK_SELF_DAMAGE;
    }
    key->orb_ok = has_orb && p->current_hitpoints > 1; /* the RF_ORB bit */
    memcpy(key->equipped, p->equipped, NUM_GEAR_SLOTS);
    key->gates = riskfight_threat_gates(s, agent, riskfight_spec_weapon(p));
    key->below_full = p->current_hitpoints < p->base_hitpoints;
    key->above_divine = p->current_hitpoints > OSRS_DIVINE_DAMAGE;
    key->food_ready = p->food_timer == 0;
    key->karambwan_ready = p->karambwan_timer == 0;
    key->potion_ready = p->potion_timer == 0;
    key->prayer = p->current_prayer > 0;
    key->magic_ok = p->current_magic >= OSRS_VENGEANCE_MAGIC_LEVEL;
    key->veng_active = p->veng_active != 0;
    key->veng_cooled = p->veng_cooldown <= 1;
    key->has_sacks = s->inventory_use[agent].vengeance_sacks > 0;
    key->teleport_ok = osrs_teleport_allowed(s->env.pvp_runtime.teleport[agent], s->env.tick);
    key->hand_busy = s->hand_busy[agent] != 0;
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
    int base_defence; /* 99, or RF_DHAROK_KIT_BASE_DEFENCE when he carries RF_DHAROK_KIT_PIECES Dharok pieces (riskfight_base_defence's kit rule) */
    uint8_t weapon;
} RiskfightObservedSelf;

static RiskfightObservedSelf riskfight_observed_self(const float* obs) {
    int dharok_pieces = 0;
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++)
        dharok_pieces += riskfight_is_dharok_piece((int)lroundf(obs[RF_EQUIPPED_START + slot] * RF_OBSERVATION_ITEM_SCALE));
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        dharok_pieces += riskfight_is_dharok_piece(osrs_item_content_metadata(
            osrs_inventory_cell_obs_code_decode(obs[RF_INVENTORY_START + slot * RF_INVENTORY_WIDTH]))->item_idx);
    return (RiskfightObservedSelf){
        .hitpoints = (int)lroundf(obs[0] * 121), .prayer = (int)lroundf(obs[1] * 99),
        .attack = (int)lroundf(obs[2] * 118), .strength = (int)lroundf(obs[3] * 118),
        .defence = (int)lroundf(obs[4] * 120), .magic = (int)lroundf(obs[5] * 99),
        .attack_timer = (int)lroundf(obs[7] * 10), .food_timer = (int)lroundf(obs[8] * 3),
        .potion_timer = (int)lroundf(obs[9] * 3), .karambwan_timer = (int)lroundf(obs[10] * 3),
        .base_defence = dharok_pieces >= RF_DHAROK_KIT_PIECES ? RF_DHAROK_KIT_BASE_DEFENCE : MAXED_BASE_DEFENCE,
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

/** Heals below 73 HP (with `meals`: the profile's own eating), a restore when drained, a combat repot when the boost has faded. */
static void riskfight_script_consume(const RiskfightObservedSelf* self,
    const RiskfightObservedInventory* inventory, int meals, int* actions) {
    int hp = self->hitpoints;
    if (meals && hp < 73 && self->food_timer == 0) {
        actions[RF_FOOD] = inventory->kind_slot[OSRS_CONSUMABLE_ANGLERFISH];
        if (!actions[RF_FOOD]) actions[RF_FOOD] = inventory->kind_slot[OSRS_CONSUMABLE_MARLIN];
        if (!actions[RF_FOOD]) actions[RF_FOOD] = inventory->kind_slot[OSRS_CONSUMABLE_SUMMER_PIE];
    }
    if (meals && hp < 45 && self->karambwan_timer == 0) {
        actions[RF_COMBO] = inventory->kind_slot[OSRS_CONSUMABLE_KARAMBWAN];
        if (!actions[RF_COMBO]) actions[RF_COMBO] = inventory->kind_slot[OSRS_CONSUMABLE_HALIBUT];
    }
    if (self->potion_timer != 0) return;
    if (meals && hp < 65) actions[RF_DRINK] = inventory->kind_slot[OSRS_CONSUMABLE_BREW];
    if (actions[RF_DRINK]) return;
    /* Defence is judged against his base (riskfight_base_defence; the observation shows his kit, not his seat): drained below it, repotted 11 above it, as
     * attack and strength are at 99 and 110. */
    int drained = self->prayer <= 40 || self->attack < 99 || self->strength < 99 ||
        self->defence < self->base_defence || self->magic < 99;
    if (drained) {
        actions[RF_DRINK] = inventory->kind_slot[OSRS_CONSUMABLE_SANFEW];
        if (!actions[RF_DRINK]) actions[RF_DRINK] = inventory->kind_slot[OSRS_CONSUMABLE_SUPER_RESTORE];
    } else if (hp >= 65 && (self->attack < 110 || self->strength < 110 || self->defence < self->base_defence + 11)) {
        actions[RF_DRINK] = inventory->kind_slot[OSRS_CONSUMABLE_SUPER_COMBAT];
    }
}

/* A Dharok pool opponent's meals (RiskfightProfile.dharok_eating): he keeps his hitpoints low for the greataxe and eats on his own hitpoints and the other side's
 * attack clock, not on the crater thresholds above. Measured on the 261 spectated Dharok v Dharok duels by scripts/measure_dharok_eating.py (512 fighters; a meal
 * is a rise of his bar, read on nearly every tick, plus the damage he took, of 8 or more): meals per tick at hitpoints 0-9 ... 90-98 are 81, 130, 140, 155, 164,
 * 158, 93, 16, 4.4, 0.7 per mille, none from 99 up; single (under 30) / double (anglerfish and karambwan, under 48) / triple (and a brew dose) as
 * RF_DHAROK_MEAL_SINGLE_PM / _DOUBLE_PM, the rest triple. A single is an anglerfish (+7 bar points with no damage between the reads, 316 at 50-79 hitpoints) or a
 * brew dose (+4 and +5, 260; +6, 259, is either): RF_DHAROK_MEAL_BREW_PM of them a brew dose.
 * When he eats is the clock (measure_dharok_eating.py (c)): below 60 hitpoints the meals come the tick after the other side's swing (225-424 per mille) and on
 * the tick his next swing frees (256-280), 53-68 on the ticks between. A meal at a flat rate per tick left him low through the other side's swings, and
 * plain hits killed him: 27% of sim knockouts against 1.6% spectated. RF_DHAROK_MEAL_PM[bar / 6][phase] (RiskfightMealPhase) are the per-tick tries. */
typedef enum { RF_MEAL_AFTER_SWING, RF_MEAL_SWING_FREES, RF_MEAL_BETWEEN, RF_MEAL_PHASES } RiskfightMealPhase;
static const uint16_t RF_DHAROK_MEAL_PM[5][RF_MEAL_PHASES] = {
    {225, 256, 53}, {388, 273, 67}, {424, 280, 68}, {89, 57, 18}, {5, 6, 2},
};
static const uint16_t RF_DHAROK_MEAL_SINGLE_PM[10] = {150, 130, 100, 170, 310, 790, 970, 970, 920, 1000};
static const uint16_t RF_DHAROK_MEAL_DOUBLE_PM[10] = {40, 140, 280, 540, 650, 210, 20, 30, 80, 0};
enum { RF_DHAROK_MEAL_BREW_PM = 400 };
/* A Dharok pool opponent's teleport tab, on his own hitpoints rather than on supplies used (tab_pm): of the 261 spectated Dharok v Dharok duels, the 198 that end
 * without a knockout end with the lower fighter at hitpoints 36 (median), 67% at 46 or less. Ends per mille per fighter-tick, the lower side taken as the one
 * leaving, at hitpoints 0-19, 20-39, 40-59, 60 up: 28.5, 13.5, 3.5, 0.8; the human profiles' tab_pm gave him 6.8, 7.2, 3.1, 2.2. Low, he tabbed too rarely,
 * stayed in and plain hits finished him: 20% of sim knockouts against 1 of 63 spectated. */
static const uint16_t RF_DHAROK_TAB_PM[4] = {29, 14, 4, 1};

/* A Dharok pool opponent's orb (Feel, 10 off himself), per tick on his own hitpoints and the axe in hand, on every tick of the other side's attack clock. An orb is
 * a lone 10 with no swing of the other side on the ticks around it and no Vengeance proc of his, so it can't be told from his hit the tick after his swing nor next
 * to any other swing or proc of his: scripts/measure_dharok_play.py (f) leaves those ticks out of both the count and the denominator (30929 of 65552 spectated
 * Dharok v Dharok fighter-ticks stay, 1034 orbs). Per mille per visible fighter-tick at hitpoints 0-19 ... 80 up, without the axe in hand 45, 65, 61, 18, 37 and
 * with it 19, 35, 47, 33, 27, over the RF_KIT_ORB_PM share of fighters carrying an orb. By ticks since his swing the visible rate is flat within its noise
 * (2: 22, 3: 63, 4: 48, 5: 38, 6 up: 34, the ready tick: 37 per mille), so no tick of his clock is singled out. */
static const uint16_t RF_DHAROK_ORB_PM[2][5] = {{74, 109, 102, 30, 62}, {32, 59, 78, 55, 45}};

/** The phase of the other side's attack clock a meal decided now lands in: the tick after his swing (opponent[5], ticks since it, is 1), the tick his weapon frees
 * his next swing (it equals his weapon's speed), or between (also before his first swing). `opponent` the opponent block of the script observation. */
static RiskfightMealPhase riskfight_meal_phase(const float* obs, const float* opponent) {
    if (opponent[4] < 0.5f) return RF_MEAL_BETWEEN;
    int since = (int)lroundf(opponent[5] * RF_OBSERVATION_ATTACK_AGE_SCALE);
    if (since == 1) return RF_MEAL_AFTER_SWING;
    uint8_t weapon = (uint8_t)lroundf(obs[RF_OPPONENT_START + GEAR_SLOT_WEAPON] * RF_OBSERVATION_ITEM_SCALE);
    return weapon != ITEM_NONE && since == ITEM_DATABASE[weapon].attack_speed ? RF_MEAL_SWING_FREES : RF_MEAL_BETWEEN;
}

static void riskfight_script_dharok_meal(const float* obs, const RiskfightObservedSelf* self, const RiskfightObservedInventory* inventory, uint32_t seed,
    uint32_t tick, int* actions) {
    int hp = self->hitpoints;
    if (hp >= 99) return;
    const float* opponent = obs + RF_OPPONENT_START + NUM_GEAR_SLOTS;
    int band = min_int(4, hp * OSRS_PLAYER_HEALTH_BAR_SCALE / 99 / 6);
    if (!riskfight_sample_event(seed, tick, 32, RF_DHAROK_MEAL_PM[band][riskfight_meal_phase(obs, opponent)])) return;
    int size = (int)(riskfight_sample_hash(seed, tick, 33) % 1000u);
    int pieces = size < RF_DHAROK_MEAL_SINGLE_PM[hp / 10] ? 1 : size < RF_DHAROK_MEAL_SINGLE_PM[hp / 10] + RF_DHAROK_MEAL_DOUBLE_PM[hp / 10] ? 2 : 3;
    int brew = self->potion_timer == 0 ? inventory->kind_slot[OSRS_CONSUMABLE_BREW] : 0;
    if (pieces == 1 && brew && riskfight_sample_event(seed, tick, 34, RF_DHAROK_MEAL_BREW_PM)) {
        actions[RF_DRINK] = brew;
        return;
    }
    if (self->food_timer == 0) {
        actions[RF_FOOD] = inventory->kind_slot[OSRS_CONSUMABLE_ANGLERFISH];
        if (!actions[RF_FOOD]) actions[RF_FOOD] = inventory->kind_slot[OSRS_CONSUMABLE_MARLIN];
        if (!actions[RF_FOOD]) actions[RF_FOOD] = inventory->kind_slot[OSRS_CONSUMABLE_SUMMER_PIE];
    }
    if (pieces >= 2 && self->karambwan_timer == 0) {
        actions[RF_COMBO] = inventory->kind_slot[OSRS_CONSUMABLE_KARAMBWAN];
        if (!actions[RF_COMBO]) actions[RF_COMBO] = inventory->kind_slot[OSRS_CONSUMABLE_HALIBUT];
    }
    /* A brew dose is the third piece, or the meal itself when no food is left. */
    if ((pieces == 3 || !actions[RF_FOOD]) && brew) actions[RF_DRINK] = brew;
}

/* His hit followed by an instant maul special: the tick after a swing of his (attack timer at speed - 1, no meal in between), with the energy for k specials (k up to two)
 * and our hitpoints within his combo, he switches to the maul and lands the k specials on that tick (a swing of his against a seat that holds the PID lands on the same
 * tick). Rate per swing by margin = our hitpoints before his swing (the upper bound of our bar on the tick before it) - (his opening max + k x maul special max), both at
 * the levels a player assumes of him (osrs_threat_actor_assumed). Measured on the user's recordings (scripts/measure_combos.py: 123 fights with his composition seen,
 * the 263 swings of fighters who did a maul special in the fight, with the energy for one): margin < -40: 8 of 111 (72 per mille); -40 to 0: 3 of 146 (21); >= 0: 0 of 6.
 * Every recorded combo came one tick after the swing, none on the swing's own tick. */
static const int RF_COMBO_PM_DEEP = 72, RF_COMBO_PM_NEAR = 21, RF_COMBO_DEEP_MARGIN = -40;

/** Specials of the maul combo this tick (0: none), see RF_COMBO_PM_DEEP. `maul` the maul in his kit, ITEM_NONE without one. */
static int riskfight_script_combo(const float* obs, const RiskfightObservedSelf* self, uint8_t maul, uint32_t seed, uint32_t tick) {
    if (maul == ITEM_NONE || self->weapon == ITEM_NONE || pvp_is_maul(self->weapon) || tick < 2) return 0;
    int speed = ITEM_DATABASE[self->weapon].attack_speed + osrs_stance_speed_mod((FightStyle)lroundf(obs[16] * 3));
    if (self->attack_timer != speed - 1 || self->food_timer || self->karambwan_timer) return 0;
    int k = min_int(RF_ACTION_DIMS[RF_SPECIAL] - 1, (int)lroundf(obs[6] * 100) / osrs_spec_cost(maul));
    if (k < 1) return 0;
    int bar_before = (int)lroundf(obs[RF_HISTORY_START + RF_EVENT_WIDTH + 7] * RF_OBSERVATION_HEALTH_BAR_SCALE);
    OsrsHealthBarRange before = osrs_health_bar_range(bar_before, OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 121);
    int our_hp = before.kind == OSRS_HEALTH_BAR_KNOWN ? before.upper : 121;
    uint8_t equipment[NUM_GEAR_SLOTS];
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) equipment[slot] = (uint8_t)lroundf(obs[RF_EQUIPPED_START + slot] * RF_OBSERVATION_ITEM_SCALE);
    OsrsThreatActor opener = osrs_threat_actor_assumed(osrs_threat_style_of_weapon(self->weapon)), melee = osrs_threat_actor_assumed(ATTACK_STYLE_MELEE);
    OsrsThreatWeapon opening = osrs_threat_weapon(equipment, self->weapon, &opener);
    int opening_max = osrs_threat_effect_max(&opening, osrs_threat_max_hit_at(&opening, 99, self->hitpoints), our_hp);
    int margin = our_hp - (opening_max + k * osrs_threat_weapon(equipment, maul, &melee).spec_max);
    int per_mille = margin < RF_COMBO_DEEP_MARGIN ? RF_COMBO_PM_DEEP : margin < 0 ? RF_COMBO_PM_NEAR : 0;
    return riskfight_sample_event(seed, tick - 1, 27, per_mille) ? k : 0;
}

/* A Dharok fighter's strip: with the helm on, he takes his platebody and/or platelegs off into free bag cells, so the other side hits him more often (into his
 * Vengeance) and his hitpoints drop for the next greataxe swing, then puts them back on for the set effect before the swing. Measured on spectator.duels by
 * scripts/measure_dharok_play.py (599 Dharok fighters; a strip lasts >= 2 ticks and starts on a tick without a weapon change, the rest are switch artefacts):
 * 174 strips by 114 fighters over 74,179 full-set fighter-ticks; per full-set tick by his own bar (scale 30) 0-11: 9 of 5,882, 12-23: 80 of 38,064, 24-30: 85
 * of 30,233 (RF_STRIP_PER_100K); 161 of 174 with a weapon other than the axe in hand; body and legs 119, body alone 40, legs alone 15. Back on at 251 per mille
 * per stripped tick past the first (164 re-equips over 654 such ticks); of the 98 strips the axe followed within 20 ticks, 88 had both pieces back on first
 * and 10 swung stripped. Each piece needs a free cell (the game's rule, the action mask's too): a full bag cannot strip. */
static const int RF_STRIP_PER_100K[3] = {153, 210, 281};
enum { RF_STRIP_BOTH_PM = 684, RF_STRIP_BODY_UPTO_PM = 914, RF_STRIP_REEQUIP_PM = 251 };

static int riskfight_script_strip_roll(uint32_t seed, uint32_t tick, int bar) {
    return (int)(riskfight_sample_hash(seed, tick, 28) % 100000u) < RF_STRIP_PER_100K[(bar >= 12) + (bar >= 24)];
}

/** Platebody and platelegs of a Dharok fighter (RF_BODY, RF_LEGS), see RF_STRIP_PER_100K. `axe` is set when he holds the greataxe or wants it this tick: he never
 * strips then, and a stripped fighter puts both pieces back on at once. `switching`: another weapon goes in hand this tick, no strip on it (the measurement's
 * rule). Returns 1 while stripped: the axe waits for the re-equip, so the set is whole at the swing. */
static int riskfight_script_dharok_armour(const float* obs, uint32_t seed, uint32_t tick, int hp, int axe, int switching, int* actions) {
    uint8_t head = (uint8_t)lroundf(obs[RF_EQUIPPED_START + GEAR_SLOT_HEAD] * RF_OBSERVATION_ITEM_SCALE);
    uint8_t body = (uint8_t)lroundf(obs[RF_EQUIPPED_START + GEAR_SLOT_BODY] * RF_OBSERVATION_ITEM_SCALE);
    uint8_t legs = (uint8_t)lroundf(obs[RF_EQUIPPED_START + GEAR_SLOT_LEGS] * RF_OBSERVATION_ITEM_SCALE);
    if (!riskfight_is_dharok_piece(head)) return 0;
    int body_slot = 0, legs_slot = 0, empty = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        uint16_t code = osrs_inventory_cell_obs_code_decode(obs[RF_INVENTORY_START + slot * RF_INVENTORY_WIDTH]);
        const OsrsItemContentMetadata* m = osrs_item_content_metadata(code);
        empty += code == 0;
        if (!riskfight_is_dharok_piece(m->item_idx)) continue;
        if (m->gear_slot == GEAR_SLOT_BODY && !body_slot) body_slot = slot + 1;
        if (m->gear_slot == GEAR_SLOT_LEGS && !legs_slot) legs_slot = slot + 1;
    }
    int body_off = body == ITEM_NONE && body_slot, legs_off = legs == ITEM_NONE && legs_slot;
    int bar = osrs_health_bar_ratio(hp, 99, OSRS_PLAYER_HEALTH_BAR_SCALE);
    if (body_off || legs_off) {
        /* The first stripped tick is the one after the strip roll fired: he keeps the pieces off through it. */
        int reequip = axe || (!riskfight_script_strip_roll(seed, tick - 1, bar) && riskfight_sample_event(seed, tick, 29, RF_STRIP_REEQUIP_PM));
        if (reequip && body_off) actions[RF_BODY] = body_slot;
        if (reequip && legs_off) actions[RF_LEGS] = legs_slot;
        return 1;
    }
    if (axe || switching || !empty || !riskfight_is_dharok_piece(body) || !riskfight_is_dharok_piece(legs) || !riskfight_script_strip_roll(seed, tick, bar)) return 0;
    int pieces = (int)(riskfight_sample_hash(seed, tick, 30) % 1000u);
    int both = pieces < RF_STRIP_BOTH_PM && empty >= 2;
    if (both || pieces < RF_STRIP_BODY_UPTO_PM) actions[RF_BODY] = RF_UNEQUIP;
    if (both || pieces >= RF_STRIP_BODY_UPTO_PM) actions[RF_LEGS] = RF_UNEQUIP;
    return 0;
}

/* The greataxe per attack, measured by scripts/measure_dharok_play.py (d): of a Dharok fighter's swings without a special (anim, 12,189 on spectator.duels), the
 * share swung with the axe, per mille, by his own bar (hitpoints <40, 40-79, 80+), his Vengeance up (cast seen, not yet proc'd) and the other side's bar (scale
 * 30: <10, 10-19, 20+). A cell under 60 swings pools with its Vengeance pair, both take the pair's share (all of <40, 80+ at bar <10). The axe follows the
 * other side's bar most (87 / 334 / 526 by his bar), then his own hitpoints (50 / 194 / 640), then his Vengeance (127 down, 239 up). At the 26 axe knockouts
 * of the 261 Dharok v Dharok duels he is lower (hitpoints 36 against 56 at all axe swings) on a lower bar (12.7 against 17.4), which the table carries; the
 * rest does not depart from all axe swings: Vengeance up 0.19 (0.32), an orb drop in the 5 ticks up to the swing 0.12 (0.11), stripped 0 (0.03), the other
 * side swinging the tick before 0.27 (0.14, 26 knockouts). An axe swing with his reflect on the knockout is 1 of 63 knockouts: no rule times one. */
static const int16_t RF_AXE_SWING_PM[3][2][3] = {
    {{794, 736, 494}, {794, 736, 494}},
    {{490, 327, 104}, {554, 393, 192}},
    {{234, 135, 25}, {234, 218, 107}},
};
/* The training profiles' mean axe_pm per own band (AXE_WIELD_PM_OWN_BAND, scripts/mine_riskfight_behavior.py): a profile's axe_pm over it is his own propensity. */
static const int RF_AXE_WIELD_MEAN[3] = {75, 44, 15};
/* The Voidwaker special waits for the other side's bar at or below this (scripts/measure_dharok_play.py (e): of 451 spectated specials of Dharok fighters holding
 * a Voidwaker, 96% came at his bar 25 or less; the rate is 6-13 per 1,000 ticks up to bar 25 and under 1 from bar 27). The old gate, bar under 19.5 (0.65), cut
 * the 60-79 hitpoint specials that are half of the spectated ones, and the sim landed 0.0042 specials per fighter-tick against 0.0068. */
enum { RF_VW_SPEC_BAR_MAX = 25 };

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
    int combo_specs = riskfight_script_combo(obs, &self, maul_item, seed, tick);
    /* The combo's tick is all clicks of the switch and the specials: no meal on it. */
    int consume_band = (hp >= 40) + (hp >= 65) + (hp >= 73) + (hp >= 90);
    if (!combo_specs && riskfight_sample_event(seed, tick, 11, profile->consume_pm[consume_band]))
        riskfight_script_consume(&self, &inventory, !profile->dharok_eating, actions);
    if (!combo_specs && profile->dharok_eating) riskfight_script_dharok_meal(obs, &self, &inventory, seed, tick, actions);
    if (actions[RF_FOOD] || actions[RF_DRINK] || actions[RF_COMBO]) actions[RF_PRIMARY] = RF_STOP;
    uint8_t generic_spec = plain_spec != ITEM_NONE ? plain_spec : plain_main != ITEM_NONE && osrs_spec_cost(plain_main) > 0 ? plain_main : ITEM_NONE;
    int generic_cost = generic_spec == ITEM_NONE ? 100 : osrs_spec_cost(generic_spec);
    int vw_spec = !combo_specs && has_vw && obs[6] >= 0.5f && opp_bar <= RF_VW_SPEC_BAR_MAX &&
        riskfight_sample_event(seed, attack_tick, 21, profile->vw_pm);
    int maul_cost = maul_item == ITEM_NONE ? 100 : osrs_spec_cost(maul_item);
    /* The ornate maul waits for the energy of two specials and lands both at once; the plain one costs 60 and gets one. */
    int maul_specs = maul_item == ITEM_NONE ? 0 : maul_item == ITEM_GRANITE_MAUL_ORNATE ? 2 : 1;
    int maul_spec = combo_specs || (!vw_spec && maul_specs && obs[6] * 100 >= maul_cost * maul_specs - 1 && opponent[0] < 0.65f && opp_upper <= 76 && hp < 99 &&
        riskfight_sample_event(seed, tick, 22, profile->maul_pm));
    int generic_roll = generic_spec != ITEM_NONE && !vw_spec && !maul_spec && obs[6] * 100 >= generic_cost - 0.5f && opponent[0] < 0.65f &&
        riskfight_sample_event(seed, attack_tick, 25, profile->vw_pm);
    int voidwaker_main = plain_main == ITEM_NONE && inventory.main_weapon == ITEM_NONE && (self.weapon == ITEM_VOIDWAKER || inventory.item_slot[ITEM_VOIDWAKER]);
    uint8_t main_weapon = plain_main != ITEM_NONE ? plain_main : !riskfight_is_switch_weapon(self.weapon) ? self.weapon :
        voidwaker_main ? ITEM_VOIDWAKER : inventory.main_weapon != ITEM_NONE ? inventory.main_weapon : self.weapon;
    /* The greataxe swing: one roll per attack (keyed on attack_tick), RF_AXE_SWING_PM by his own hitpoints, his Vengeance up and the other side's bar, times the
     * profile's own axe propensity (profile->axe_pm over the training profiles' mean RF_AXE_WIELD_MEAN, scripts/mine_riskfight_behavior.py). */
    int axe_band = (hp >= 40) + (hp >= 80);
    int opp_axe_band = (opp_bar >= 10) + (opp_bar >= 20);
    int axe_pm = RF_AXE_SWING_PM[axe_band][obs[11] > 0.5f][opp_axe_band] * profile->axe_pm[axe_band] / RF_AXE_WIELD_MEAN[axe_band];
    int axe_swing = has_axe && riskfight_sample_event(seed, attack_tick, 23, axe_pm);
    uint8_t weapon = vw_spec ? ITEM_VOIDWAKER : maul_spec ? maul_item : generic_roll ? generic_spec :
        axe_swing ? ITEM_DHAROKS_GREATAXE : main_weapon;
    /* A stripped Dharok fighter puts body and legs back on before he takes the axe: the wield waits for the tick after (its roll is keyed on attack_tick). */
    int axe = weapon == ITEM_DHAROKS_GREATAXE || self.weapon == ITEM_DHAROKS_GREATAXE;
    if (riskfight_script_dharok_armour(obs, seed, tick, hp, axe, weapon != self.weapon, actions) && axe && self.weapon != ITEM_DHAROKS_GREATAXE)
        weapon = main_weapon;
    /* Behead: with all five stacks held and the other side hurt, at the same rate as his other specials; it costs no energy. */
    int stacks = (int)lroundf(obs[RF_THREAT_START + RF_THREAT_STACKS_OFFSET + 1] * OSRS_SOUL_STACKS_MAX);
    int behead = weapon == ITEM_SOULREAPER_AXE && !vw_spec && !maul_spec && !generic_roll && stacks == OSRS_SOUL_STACKS_MAX && opponent[0] < 0.65f &&
        riskfight_sample_event(seed, attack_tick, 26, profile->vw_pm);
    int armed_wanted = vw_spec || generic_roll || behead;
    actions[RF_SPECIAL] = maul_spec ? (combo_specs ? combo_specs : maul_specs) : !holding_maul && armed_wanted != (obs[13] >= 0.5f);
    /* Ranged weapons are fought on Rigour and rapid, the rest on Piety and aggressive. */
    int ranged_weapon = weapon != ITEM_NONE && get_item_attack_style(weapon) == ATTACK_STYLE_RANGED;
    actions[RF_PRAYER] = ranged_weapon ? OFFENSIVE_PRAYER_RIGOUR : OFFENSIVE_PRAYER_PIETY;
    actions[RF_STYLE] = ranged_weapon ? FIGHT_STYLE_RAPID : FIGHT_STYLE_AGGRESSIVE;
    actions[RF_WEAPON] = inventory.item_slot[weapon];
    if (!item_is_two_handed(weapon) && inventory.shield != ITEM_NONE)
        actions[RF_SHIELD] = inventory.item_slot[inventory.shield];
    int orb_pm = profile->dharok_eating ? RF_DHAROK_ORB_PM[self.weapon == ITEM_DHAROKS_GREATAXE][min_int(4, hp / 20)] :
        (weapon == ITEM_DHAROKS_GREATAXE && self.attack_timer == 1 && hp > 20 ? profile->orb_axe_pm : 0);
    actions[RF_ORB] = orb_pm && riskfight_sample_event(seed, profile->dharok_eating ? tick : attack_tick, 24, orb_pm);
    actions[RF_RING] = inventory.item_slot[obs[7] > 0.1f ? ITEM_RING_OF_RECOIL : ITEM_ULTOR_RING];
    if (obs[RF_SELF_TELEPORT_LOCK] > 0) return;
    int supply_band = min_int(consume_ticks / RISKFIGHT_TAB_SUPPLY_TICKS, RISKFIGHT_TAB_SUPPLY_BANDS - 1);
    int out_of_heals = !inventory.kind_slot[OSRS_CONSUMABLE_ANGLERFISH] && !inventory.kind_slot[OSRS_CONSUMABLE_KARAMBWAN] &&
        !inventory.kind_slot[OSRS_CONSUMABLE_MARLIN] &&
        !inventory.kind_slot[OSRS_CONSUMABLE_SUMMER_PIE] &&
        !inventory.kind_slot[OSRS_CONSUMABLE_HALIBUT] && !inventory.kind_slot[OSRS_CONSUMABLE_BREW];
    int tab_pm = profile->dharok_eating ? RF_DHAROK_TAB_PM[min_int(3, hp / 20)] : profile->tab_pm[supply_band][hp < 40];
    if ((hp < 30 && out_of_heals) || riskfight_sample_event(seed, tick, 31, tab_pm))
        actions[RF_PRIMARY] = RF_TELEPORT;
}
#endif
