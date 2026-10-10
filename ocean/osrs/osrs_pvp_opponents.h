#ifndef OSRS_PVP_OPPONENTS_H
#define OSRS_PVP_OPPONENTS_H

#include "osrs_policy.h"
#include "osrs_pvp_actions.h"
#include "osrs_recorded_nh_profile.h"
#include "osrs_player_inventory_use.h"

#define OPP_STYLE_MAGE    0
#define OPP_STYLE_RANGED  1
#define OPP_STYLE_MELEE   2
#define OPP_STYLE_SPEC    3

static const float OPP_EAT_CRITICAL_THRESHOLD = 0.30f;
static const float OPP_EAT_DOUBLE_THRESHOLD = 0.50f;
static const float OPP_EAT_BREW_THRESHOLD = 0.70f;

static inline PvpEquipmentPlan opp_equipment_plan(int style) {
    if (style == OPP_STYLE_MAGE) return PVP_EQUIPMENT_MAGIC;
    if (style == OPP_STYLE_RANGED) return PVP_EQUIPMENT_RANGED;
    if (style == OPP_STYLE_SPEC) return PVP_EQUIPMENT_SPEC_MELEE;
    return PVP_EQUIPMENT_MELEE;
}

static inline void opp_apply_equipment_plan(
    int* actions,
    const Player* self,
    PvpEquipmentPlan plan
) {
    pvp_emit_equipment_plan_actions(actions, self, plan);
    if (plan == PVP_EQUIPMENT_SPEC_MELEE ||
            plan == PVP_EQUIPMENT_SPEC_RANGED ||
            plan == PVP_EQUIPMENT_SPEC_MAGIC ||
            plan == PVP_EQUIPMENT_GMAUL)
        actions[OSRS_HEAD_SPECIAL] = 1;
}

static inline void opp_apply_gear_switch(
    int* actions,
    const Player* self,
    int style
) {
    opp_apply_equipment_plan(actions, self, opp_equipment_plan(style));
}

static inline void opp_emit_consumable(
    int* actions, const Player* player, OsrsConsumableKind kind
) {
    for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++) {
        const OsrsItemContentMetadata* metadata =
            osrs_inventory_cell_metadata(&player->inventory_cells[cell]);
        if (metadata->consumable_kind != kind) continue;
        if (metadata->click_action == OSRS_CLICK_EAT) actions[OSRS_HEAD_EAT] = cell + 1;
        else if (metadata->click_action == OSRS_CLICK_DRINK) actions[OSRS_HEAD_DRINK] = cell + 1;
        return;
    }
}

typedef struct {
    int can_food;
    int can_brew;
    int can_karambwan;
    int can_restore;
} OppConsumables;

static inline OppConsumables opp_get_consumables(Player* self) {
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;
    OppConsumables c;
    c.can_food = osrs_player_can_eat_food_type(self, FOOD_SHARK) && hp_pct < 1.0f;
    c.can_brew = pvp_drink_kind_available(self, OSRS_CONSUMABLE_BREW);
    c.can_karambwan = osrs_player_can_eat_food_type(self, FOOD_KARAMBWAN) && hp_pct < 1.0f;
    c.can_restore = pvp_drink_kind_available(self, OSRS_CONSUMABLE_SUPER_RESTORE);
    return c;
}

static inline void opp_emit_preferred_food(
    int* actions,
    const Player* self,
    OppConsumables consumables
) {
    if (consumables.can_food) {
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_SHARK_FOOD);
    } else if (consumables.can_karambwan) {
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_KARAMBWAN);
    }
}

static inline int opp_attack_ready(Player* self) {
    return self->attack_timer <= 0;
}

static inline int opp_is_drained(Player* self) {
    return self->current_strength < self->base_strength ||
           self->current_attack < self->base_attack ||
           self->current_defence < self->base_defence ||
           self->current_ranged < self->base_ranged ||
           self->current_magic < self->base_magic;
}

static void opp_apply_boost_potion(int* actions,
                                    Player* self, int attack_style) {
    if (actions[OSRS_HEAD_DRINK] != 0) return;
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    if (opp_is_drained(self) && hp_pct > 0.90f &&
            pvp_drink_kind_available(self, OSRS_CONSUMABLE_SUPER_RESTORE)) {
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_SUPER_RESTORE);
        return;
    }

    if (hp_pct <= 0.90f) return;

    if (attack_style == OPP_STYLE_MELEE || attack_style == OPP_STYLE_SPEC) {
        if (self->current_strength <= self->base_strength &&
                pvp_drink_kind_available(self, OSRS_CONSUMABLE_SUPER_COMBAT)) {
            opp_emit_consumable(actions, self, OSRS_CONSUMABLE_SUPER_COMBAT);
        }
    } else if (attack_style == OPP_STYLE_RANGED) {
        if (self->current_ranged <= self->base_ranged &&
                pvp_drink_kind_available(self, OSRS_CONSUMABLE_RANGING)) {
            opp_emit_consumable(actions, self, OSRS_CONSUMABLE_RANGING);
        }
    }
}

static inline int opp_check_eating_queued(int* actions) {
    return actions[OSRS_HEAD_EAT] != 0;
}

static void opp_apply_consumables(int* actions, Player* self) {
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;
    float prayer_pct = (float)self->current_prayer / (float)self->base_prayer;
    OppConsumables cons = opp_get_consumables(self);

    if (hp_pct < OPP_EAT_CRITICAL_THRESHOLD && cons.can_brew && (cons.can_food || cons.can_karambwan)) {
        opp_emit_preferred_food(actions, self, cons);
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_BREW);
    } else if (hp_pct < OPP_EAT_DOUBLE_THRESHOLD && cons.can_food && cons.can_brew) {
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_SHARK_FOOD);
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_BREW);
    } else if (hp_pct < OPP_EAT_DOUBLE_THRESHOLD && (cons.can_food || cons.can_karambwan)) {
        opp_emit_preferred_food(actions, self, cons);
    } else if (self->current_magic < self->base_magic && cons.can_restore) {
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_SUPER_RESTORE);
    } else if (hp_pct < OPP_EAT_BREW_THRESHOLD && cons.can_brew) {
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_BREW);
    } else if (hp_pct < 0.60f && cons.can_food) {
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_SHARK_FOOD);
    } else if (hp_pct < 0.60f && cons.can_karambwan) {
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_KARAMBWAN);
    } else if (opp_is_drained(self) && hp_pct < 0.90f && cons.can_brew) {
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_BREW);
    } else if (prayer_pct < 0.30f && cons.can_restore) {
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_SUPER_RESTORE);
    } else if (opp_is_drained(self) && cons.can_restore) {
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_SUPER_RESTORE);
    }

}

static inline int opp_set_refresh_for_prayer(OverheadPrayer p) {
    switch (p) {
        case PRAYER_PROTECT_MAGIC:  return ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC;
        case PRAYER_PROTECT_RANGED: return ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED;
        case PRAYER_PROTECT_MELEE:  return ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE;
        case PRAYER_SMITE:          return ENCOUNTER_OVERHEAD_SET_REFRESH_SMITE;
        case PRAYER_REDEMPTION:     return ENCOUNTER_OVERHEAD_SET_REFRESH_REDEMPTION;
        default:                    return ENCOUNTER_OVERHEAD_NO_CHANGE;
    }
}

static inline OppVisibleState opp_forecast_visible(
    OppVisibleHistory history, OppVisibleState current
) {
    OppVisibleState forecast = current;
    if (history.phase == OPP_HISTORY_READY) {
        if (current.weapon == history.older.weapon &&
                current.weapon != history.latest.weapon)
            forecast.weapon = history.latest.weapon;
        if (current.prayer == history.older.prayer &&
                current.prayer != history.latest.prayer)
            forecast.prayer = history.latest.prayer;
    }
    return forecast;
}

typedef struct {
    AttackStyle damage;
    AttackStyle offensive;
} OppAttackStyles;

static inline OppAttackStyles opp_submitted_attack_styles(
    uint8_t weapon, int primary, int spell, int special, int spec_armed,
    int special_energy, int can_ice, int can_blood
) {
    if (primary < OSRS_PRIMARY_MOVE_ACTIONS || primary >= OSRS_PRIMARY_DIM(1))
        return (OppAttackStyles){ATTACK_STYLE_NONE, ATTACK_STYLE_NONE};
    if (spell == OSRS_SPELL_ICE_BARRAGE || spell == OSRS_SPELL_BLOOD_BARRAGE) {
        AttackStyle style = (spell == OSRS_SPELL_ICE_BARRAGE ? can_ice : can_blood)
            ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_NONE;
        return (OppAttackStyles){style, style};
    }
    AttackStyle style = (AttackStyle)get_item_attack_style(weapon);
    int armed = special == 1 || (special != 2 && spec_armed);
    if (style == ATTACK_STYLE_MAGIC &&
            !(weapon == ITEM_VOLATILE_STAFF && armed && special_energy >= osrs_spec_cost(weapon)))
        style = ATTACK_STYLE_MELEE;
    if (weapon == ITEM_VOIDWAKER && armed && special_energy >= osrs_spec_cost(weapon))
        return (OppAttackStyles){ATTACK_STYLE_MAGIC, ATTACK_STYLE_MELEE};
    return (OppAttackStyles){style, style};
}

static void opp_pressure_move(
    int* actions, const Player* self, const Player* target, int desired_distance
) {
    if (self->frozen_ticks > 0) return;
    const EncounterArenaTopology* topology = pvp_route_topology_owner.topology;
    int best = 0;
    int distance = chebyshev_distance(self->x, self->y, target->x, target->y);
    int best_error = abs_int(distance - desired_distance);
    int dx = self->x - target->x;
    int dy = self->y - target->y;
    int best_separation = dx * dx + dy * dy;
    for (int action = 1; action < OSRS_PRIMARY_MOVE_ACTIONS; action++) {
        int x = self->x + ENCOUNTER_MOVE_TARGET_DX[action];
        int y = self->y + ENCOUNTER_MOVE_TARGET_DY[action];
        if (!pvp_topology_destination_selectable(topology, x, y)) continue;
        EncounterRouteInput input = {
            .topology = topology, .source_x = self->x, .source_y = self->y,
            .actor_size = 1, .target_x = x, .target_y = y, .target_size = 1,
            .target_kind = ENCOUNTER_ROUTE_TARGET_TILE,
            .movement_mode = ENCOUNTER_ROUTE_MOVEMENT_RUN,
            .cost_policy = ENCOUNTER_ROUTE_COST_OSRS,
        };
        EncounterRouteResult route;
        int local = pvp_local_move_route_lookup(
            &pvp_route_topology_owner, &input, &route);
        assert(local);
        if (route.outcome != ROUTE_REACHED_TARGET || route.distance == 0) continue;
        int next_x = self->x + route.first_dx + route.run_dx;
        int next_y = self->y + route.first_dy + route.run_dy;
        int error = abs_int(chebyshev_distance(
            next_x, next_y, target->x, target->y) - desired_distance);
        int next_dx = next_x - target->x;
        int next_dy = next_y - target->y;
        int separation = next_dx * next_dx + next_dy * next_dy;
        if (error < best_error || (error == best_error && error > 0 &&
                (desired_distance > distance ? separation > best_separation
                    : separation < best_separation))) {
            best = action;
            best_error = error;
            best_separation = separation;
        }
    }
    actions[OSRS_HEAD_PRIMARY] = best;
}

static void opp_recorded_nh(
    OsrsEnv* env, OpponentState* opp, int* actions
) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    AttackStyle target_style = (AttackStyle)get_item_attack_style(
        target->equipped[GEAR_SLOT_WEAPON]);
    RecordedNhWeights defense = recorded_nh_defense_weights(
        opp->visible_history.latest.weapon, target_style, self->prayer);
    uint32_t defense_draw = (uint32_t)rand_int(env, (int)defense.total);
    OverheadPrayer prayer = (OverheadPrayer)recorded_nh_sample(defense, defense_draw);
    OppVisibleState forecast = opp_forecast_visible(opp->visible_history,
        (OppVisibleState){target_style, target->prayer});
    if (forecast.weapon != target_style && forecast.weapon != ATTACK_STYLE_NONE)
        prayer = forecast.weapon == ATTACK_STYLE_MELEE ? PRAYER_PROTECT_MELEE
            : forecast.weapon == ATTACK_STYLE_RANGED ? PRAYER_PROTECT_RANGED
                : PRAYER_PROTECT_MAGIC;
    if (prayer != self->prayer)
        actions[OSRS_HEAD_OVERHEAD] = prayer == PRAYER_NONE
            ? ENCOUNTER_OVERHEAD_OFF : opp_set_refresh_for_prayer(prayer);
    opp->visible_history.older = opp->visible_history.latest;
    opp->visible_history.latest = (OppVisibleState){target_style, target->prayer};
    opp->visible_history.phase = opp->visible_history.phase == OPP_HISTORY_EMPTY
        ? OPP_HISTORY_ONE : OPP_HISTORY_READY;

    opp_apply_consumables(actions, self);
    int distance = chebyshev_distance(self->x, self->y, target->x, target->y);
    if (opp_attack_ready(self) && !opp_check_eating_queued(actions)) {
        unsigned allowed = 0;
        if (distance > 0 && distance <= COMBAT_SPELL_ATTACK_RANGE &&
                (can_cast_ice_spell(self) || can_cast_blood_spell(self)))
            allowed |= 1u << RECORDED_NH_MAGIC;
        for (int attack = RECORDED_NH_RANGED;
                attack < RECORDED_NH_ATTACK_COUNT; attack++) {
            PvpEquipmentPlan plan = attack == RECORDED_NH_RANGED
                ? PVP_EQUIPMENT_RANGED : attack == RECORDED_NH_MELEE
                    ? PVP_EQUIPMENT_MELEE : PVP_EQUIPMENT_SPEC_MELEE;
            uint8_t gear[NUM_DYNAMIC_GEAR_SLOTS];
            pvp_resolve_equipment_plan(self, plan, gear);
            AttackStyle style = attack == RECORDED_NH_RANGED
                ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE;
            int range = pvp_weapon_attack_range(gear[0], style,
                style == ATTACK_STYLE_RANGED ? FIGHT_STYLE_RAPID : FIGHT_STYLE_ACCURATE);
            if (get_item_attack_style(gear[0]) != style ||
                    distance == 0 || distance > range) continue;
            if (style == ATTACK_STYLE_MELEE && range == 1 &&
                    !is_in_melee_range(self, target)) continue;
            if (attack == RECORDED_NH_MELEE_SPECIAL &&
                    (!item_is_spec_weapon(gear[0]) ||
                     self->special_energy < osrs_spec_cost(gear[0]))) continue;
            allowed |= 1u << attack;
        }
        if (allowed) {
            RecordedNhWeights attack_weights = recorded_nh_attack_weights(
                distance, self->last_attack_style, target->prayer, allowed);
            uint32_t attack_draw = (uint32_t)rand_int(env, (int)attack_weights.total);
            RecordedNhAttack attack = (RecordedNhAttack)recorded_nh_sample(
                attack_weights, attack_draw);
            int style = attack == RECORDED_NH_MAGIC ? OPP_STYLE_MAGE
                : attack == RECORDED_NH_RANGED ? OPP_STYLE_RANGED
                    : attack == RECORDED_NH_MELEE ? OPP_STYLE_MELEE : OPP_STYLE_SPEC;
            opp_apply_boost_potion(actions, self, style);
            opp_apply_gear_switch(actions, self, style);
            actions[OSRS_HEAD_SPECIAL] = attack == RECORDED_NH_MELEE_SPECIAL ? 1 : 2;
            actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
            if (attack == RECORDED_NH_MAGIC) {
                int needs_healing = self->current_hitpoints <
                    self->base_hitpoints * OPP_EAT_DOUBLE_THRESHOLD;
                actions[OSRS_HEAD_SPELL] = can_cast_blood_spell(self) &&
                    (needs_healing || !can_cast_ice_spell(self))
                        ? OSRS_SPELL_BLOOD_BARRAGE : OSRS_SPELL_ICE_BARRAGE;
            }
            return;
        }
        int approach_range = can_cast_ice_spell(self) || can_cast_blood_spell(self)
            ? COMBAT_SPELL_ATTACK_RANGE : 1;
        for (int style = OPP_STYLE_RANGED; style <= OPP_STYLE_MELEE; style++) {
            uint8_t gear[NUM_DYNAMIC_GEAR_SLOTS];
            pvp_resolve_equipment_plan(self, opp_equipment_plan(style), gear);
            AttackStyle attack = style == OPP_STYLE_RANGED ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE;
            if (get_item_attack_style(gear[0]) == attack)
                approach_range = max_int(approach_range, pvp_weapon_attack_range(gear[0], attack,
                    attack == ATTACK_STYLE_RANGED ? FIGHT_STYLE_RAPID : FIGHT_STYLE_ACCURATE));
        }
        opp_pressure_move(actions, self, target, distance == 0 ? 1 : approach_range);
        return;
    }
    if (self->frozen_ticks > 0) return;
    AttackStyle own_style = (AttackStyle)get_item_attack_style(
        self->equipped[GEAR_SLOT_WEAPON]);
    unsigned move_allowed = (1u << RECORDED_NH_MOVE_COUNT) - 1u;
    if (distance == 0) move_allowed &= ~(1u << RECORDED_NH_CLOSER);
    RecordedNhWeights movement = recorded_nh_move_weights(
        distance, own_style, target_style, move_allowed);
    uint32_t movement_draw = (uint32_t)rand_int(env, (int)movement.total);
    RecordedNhMove move = (RecordedNhMove)recorded_nh_sample(movement, movement_draw);
    if (move != RECORDED_NH_SAME_DISTANCE)
        opp_pressure_move(actions, self, target,
            distance + (move == RECORDED_NH_CLOSER ? -1 : 1));
}

static void opp_pressure_nh(OsrsEnv* env, OpponentState* opp, int* actions, OpponentType type) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    OppVisibleState current = {
        (AttackStyle)get_item_attack_style(target->equipped[GEAR_SLOT_WEAPON]),
        target->prayer,
    };
    OppVisibleState forecast = opp_forecast_visible(opp->visible_history, current);
    opp->visible_history.older = opp->visible_history.latest;
    opp->visible_history.latest = current;
    opp->visible_history.phase = opp->visible_history.phase == OPP_HISTORY_EMPTY
        ? OPP_HISTORY_ONE : OPP_HISTORY_READY;
    int actor = opp == &env->pvp_runtime.opponent_p0 ? 0 : 1;
    int target_launch = env->pvp_runtime.visible[1 - actor].last_attack_tick;
    if (target_launch >= 0 && target_launch != opp->pressure_control.target_launch_tick) {
        opp->pressure_control.target_launch_tick = target_launch;
        if (target->last_attack_style != ATTACK_STYLE_NONE)
            opp->pressure_control.attack_counts[target->last_attack_style]++;
    }
    unsigned most_attacks = 0;
    for (int style = ATTACK_STYLE_MELEE; style <= ATTACK_STYLE_MAGIC; style++) {
        if (opp->pressure_control.attack_counts[style] > most_attacks) {
            most_attacks = opp->pressure_control.attack_counts[style];
            forecast.weapon = (AttackStyle)style;
        }
    }
    OverheadPrayer defense = forecast.weapon == ATTACK_STYLE_MAGIC ? PRAYER_PROTECT_MAGIC
        : forecast.weapon == ATTACK_STYLE_RANGED ? PRAYER_PROTECT_RANGED
            : forecast.weapon == ATTACK_STYLE_MELEE ? PRAYER_PROTECT_MELEE : PRAYER_NONE;
    if (defense != self->prayer)
        actions[OSRS_HEAD_OVERHEAD] = defense == PRAYER_NONE
            ? ENCOUNTER_OVERHEAD_OFF : opp_set_refresh_for_prayer(defense);

    int last_launch = env->pvp_runtime.visible[actor].last_attack_tick;
    if (opp->pressure_control.phase == OPP_ICE_SUBMITTED) {
        opp->pressure_control.phase = last_launch != opp->pressure_control.last_launch_tick &&
                self->last_attack_style == ATTACK_STYLE_MAGIC
            ? OPP_ICE_LAUNCHED : OPP_ICE_SEEK;
        if (opp->pressure_control.phase == OPP_ICE_LAUNCHED) {
            opp->pressure_control.target_x = target->x;
            opp->pressure_control.target_y = target->y;
        }
    }
    opp->pressure_control.last_launch_tick = last_launch;
    if (target->x != opp->pressure_control.target_x || target->y != opp->pressure_control.target_y)
        opp->pressure_control.phase = OPP_ICE_SEEK;

    OppConsumables consumables = opp_get_consumables(self);
    if (self->current_magic < self->base_magic &&
            self->current_hitpoints >= self->base_hitpoints * OPP_EAT_DOUBLE_THRESHOLD &&
            consumables.can_restore) {
        opp_emit_consumable(actions, self, OSRS_CONSUMABLE_SUPER_RESTORE);
    } else {
        opp_apply_consumables(actions, self);
    }
    int distance = chebyshev_distance(self->x, self->y, target->x, target->y);
    if (!opp_attack_ready(self) || opp_check_eating_queued(actions)) {
        if (type == OPP_FREEZE_UNDERLAP_NH &&
                opp->pressure_control.phase == OPP_ICE_LAUNCHED)
            opp_pressure_move(actions, self, target, 0);
        else if (type == OPP_BLOOD_PRESSURE_NH &&
                forecast.weapon == ATTACK_STYLE_MELEE)
            opp_pressure_move(actions, self, target, COMBAT_SPELL_ATTACK_RANGE / 2);
        else if (distance > COMBAT_SPELL_ATTACK_RANGE)
            opp_pressure_move(actions, self, target, COMBAT_SPELL_ATTACK_RANGE);
        return;
    }

    int spell = type == OPP_FREEZE_UNDERLAP_NH && can_cast_ice_spell(self)
        ? OSRS_SPELL_ICE_BARRAGE : can_cast_blood_spell(self)
            ? OSRS_SPELL_BLOOD_BARRAGE : OSRS_SPELL_NONE;
    int chosen = -1;
    const int repertoire[] = {OPP_STYLE_MAGE, OPP_STYLE_RANGED, OPP_STYLE_SPEC, OPP_STYLE_MELEE};
    for (int entry = 0; entry < 4; entry++) {
        int style = repertoire[entry];
        if (style == OPP_STYLE_MAGE && spell == OSRS_SPELL_NONE) continue;
        PvpEquipmentPlan plan = opp_equipment_plan(style);
        uint8_t gear[NUM_DYNAMIC_GEAR_SLOTS];
        pvp_resolve_equipment_plan(self, plan, gear);
        if (style == OPP_STYLE_RANGED && get_item_attack_style(gear[0]) != ATTACK_STYLE_RANGED)
            continue;
        if ((style == OPP_STYLE_MELEE || style == OPP_STYLE_SPEC) &&
                get_item_attack_style(gear[0]) != ATTACK_STYLE_MELEE) continue;
        if (style == OPP_STYLE_SPEC && (!item_is_spec_weapon(gear[0]) ||
                self->special_energy < osrs_spec_cost(gear[0]))) continue;
        OppAttackStyles attack = opp_submitted_attack_styles(
            gear[0], OSRS_PRIMARY_MOVE_ACTIONS,
            style == OPP_STYLE_MAGE ? spell : OSRS_SPELL_NONE,
            style == OPP_STYLE_SPEC ? 1 : 2, self->spec_armed,
            self->special_energy, can_cast_ice_spell(self), can_cast_blood_spell(self));
        OverheadPrayer protection = attack.damage == ATTACK_STYLE_MAGIC ? PRAYER_PROTECT_MAGIC
            : attack.damage == ATTACK_STYLE_RANGED ? PRAYER_PROTECT_RANGED : PRAYER_PROTECT_MELEE;
        if (forecast.prayer == protection) continue;
        int range = style == OPP_STYLE_MAGE ? COMBAT_SPELL_ATTACK_RANGE
            : pvp_weapon_attack_range(gear[0], attack.damage,
                style == OPP_STYLE_RANGED ? FIGHT_STYLE_RAPID : FIGHT_STYLE_ACCURATE);
        if (chosen < 0 || distance <= range) chosen = style;
        if (distance <= range) break;
    }
    if (chosen < 0) {
        opp_pressure_move(actions, self, target, 1);
        return;
    }
    opp_apply_gear_switch(actions, self, chosen);
    actions[OSRS_HEAD_SPECIAL] = chosen == OPP_STYLE_SPEC ? 1 : 2;
    actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
    if (chosen == OPP_STYLE_MAGE) {
        actions[OSRS_HEAD_SPELL] = spell;
        if (spell == OSRS_SPELL_ICE_BARRAGE) opp->pressure_control.phase = OPP_ICE_SUBMITTED;
    }
}

static const OpponentType MIXED_NH_POOL[] = {
    OPP_GENERALIST_NH, OPP_RECORDED_ADAPTIVE_NH,
    OPP_BLOOD_PRESSURE_NH, OPP_FREEZE_UNDERLAP_NH,
};
static const int MIXED_NH_CUM_WEIGHTS[] = {25, 50, 75, 100};

static void opp_generalist_nh(OsrsEnv* env, OpponentState* opp, int* actions, OpponentType type) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    OppVisibleState current = {(AttackStyle)get_item_attack_style(
        target->equipped[GEAR_SLOT_WEAPON]), target->prayer};
    OppVisibleState forecast = opp_forecast_visible(opp->visible_history, current);
    opp->visible_history.older = opp->visible_history.latest;
    opp->visible_history.latest = current;
    opp->visible_history.phase = opp->visible_history.phase == OPP_HISTORY_EMPTY
        ? OPP_HISTORY_ONE : OPP_HISTORY_READY;
    OverheadPrayer defense = forecast.weapon == ATTACK_STYLE_MELEE ? PRAYER_PROTECT_MELEE
        : forecast.weapon == ATTACK_STYLE_RANGED ? PRAYER_PROTECT_RANGED
            : forecast.weapon == ATTACK_STYLE_MAGIC ? PRAYER_PROTECT_MAGIC : PRAYER_NONE;
    if (type == OPP_UNPREDICTABLE_ONETICK) {
        if (opp->pending_prayer != defense) {
            opp->pending_prayer = defense;
            int delay = rand_int(env, 100);
            opp->prayer_delay = delay < 70 ? 0 : delay < 90 ? 1 : delay < 98 ? 2 : 3;
        }
        if (opp->prayer_delay > 0) opp->prayer_delay--;
        else if (defense != self->prayer)
            actions[OSRS_HEAD_OVERHEAD] = defense == PRAYER_NONE
                ? ENCOUNTER_OVERHEAD_OFF : opp_set_refresh_for_prayer(defense);
    } else if (defense != self->prayer) {
        actions[OSRS_HEAD_OVERHEAD] = defense == PRAYER_NONE
            ? ENCOUNTER_OVERHEAD_OFF : opp_set_refresh_for_prayer(defense);
    }
    opp_apply_consumables(actions, self);
    if (type == OPP_VENG_FIGHTER && !self->veng_active &&
            remaining_ticks(self->veng_cooldown) == 0 && self->current_magic >= OSRS_VENGEANCE_MAGIC_LEVEL)
        actions[OSRS_HEAD_SPELL] = OSRS_SPELL_VENGEANCE;
    int actor = opp == &env->pvp_runtime.opponent_p0 ? 0 : 1;
    int combo_ready = opp->combo_control.phase == OPP_COMBO_SUBMITTED &&
        env->pvp_runtime.visible[actor].last_attack_tick != opp->combo_control.launch_tick &&
        self->special_energy < opp->combo_control.energy;
    opp->combo_control.phase = OPP_COMBO_IDLE;
    int distance = chebyshev_distance(self->x, self->y, target->x, target->y);
    if (!opp_attack_ready(self) || opp_check_eating_queued(actions)) {
        if (type == OPP_UNPREDICTABLE_ONETICK) {
            opp_apply_equipment_plan(actions, self, PVP_EQUIPMENT_TANK);
            if (!opp->fake_switch_pending && rand_int(env, 100) < 30) {
                int fake = rand_int(env, 3);
                opp_apply_gear_switch(actions, self, fake);
                opp->fake_switch_pending = 1;
            }
        }
        if (type == OPP_GMAUL_COMBO && combo_ready && !opp_check_eating_queued(actions) &&
                self->special_energy >= osrs_spec_cost(ITEM_GRANITE_MAUL)) {
            int maul_cell = pvp_inventory_cell_with_item(self, ITEM_GRANITE_MAUL);
            if (maul_cell >= 0 && osrs_can_equip_from_cell(self, self->inventory_cells, maul_cell)) {
                opp_apply_equipment_plan(actions, self, PVP_EQUIPMENT_GMAUL);
                actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
                return;
            }
        }
        if (type == OPP_RANGE_KITER) opp_pressure_move(actions, self, target, 5);
        else if (distance > 3) opp_pressure_move(actions, self, target, 3);
        return;
    }
    opp->fake_switch_pending = 0;
    int choices[4], count = 0, approach_range = 0;
    const int repertoire[] = {OPP_STYLE_MAGE, OPP_STYLE_RANGED, OPP_STYLE_MELEE, OPP_STYLE_SPEC};
    for (int index = 0; index < 4; index++) {
        int style = repertoire[index];
        if (style == OPP_STYLE_MAGE && (type == OPP_VENG_FIGHTER ||
                !(can_cast_ice_spell(self) || can_cast_blood_spell(self)))) continue;
        uint8_t gear[NUM_DYNAMIC_GEAR_SLOTS];
        pvp_resolve_equipment_plan(self, opp_equipment_plan(style), gear);
        if (style == OPP_STYLE_RANGED && get_item_attack_style(gear[0]) != ATTACK_STYLE_RANGED) continue;
        if ((style == OPP_STYLE_MELEE || style == OPP_STYLE_SPEC) &&
                get_item_attack_style(gear[0]) != ATTACK_STYLE_MELEE) continue;
        if (style == OPP_STYLE_SPEC && (!item_is_spec_weapon(gear[0]) ||
                self->special_energy < osrs_spec_cost(gear[0]))) continue;
        int spell = style == OPP_STYLE_MAGE ? (can_cast_ice_spell(self)
            ? OSRS_SPELL_ICE_BARRAGE : OSRS_SPELL_BLOOD_BARRAGE) : OSRS_SPELL_NONE;
        OppAttackStyles attack = opp_submitted_attack_styles(gear[0], OSRS_PRIMARY_MOVE_ACTIONS,
            spell, style == OPP_STYLE_SPEC ? 1 : 2, self->spec_armed,
            self->special_energy, can_cast_ice_spell(self), can_cast_blood_spell(self));
        OverheadPrayer protection = attack.damage == ATTACK_STYLE_MAGIC ? PRAYER_PROTECT_MAGIC
            : attack.damage == ATTACK_STYLE_RANGED ? PRAYER_PROTECT_RANGED : PRAYER_PROTECT_MELEE;
        if (forecast.prayer == protection) continue;
        int range = style == OPP_STYLE_MAGE ? COMBAT_SPELL_ATTACK_RANGE
            : pvp_weapon_attack_range(gear[0], attack.damage,
                style == OPP_STYLE_RANGED ? FIGHT_STYLE_RAPID : FIGHT_STYLE_ACCURATE);
        approach_range = max_int(approach_range, range);
        if (distance > range || (range == 1 && !is_in_melee_range(self, target))) continue;
        choices[count++] = style;
    }
    if (count == 0) {
        opp_pressure_move(actions, self, target, distance == 0 ? 1 : approach_range);
        return;
    }
    int chosen = choices[rand_int(env, count)];
    if (type == OPP_RANGE_KITER && distance >= 3 && rand_int(env, 100) < 80)
        for (int index = 0; index < count; index++)
            if (choices[index] == OPP_STYLE_RANGED) chosen = OPP_STYLE_RANGED;
    if (type == OPP_GMAUL_COMBO) {
        for (int index = 0; index < count; index++)
            if (choices[index] == OPP_STYLE_SPEC) chosen = OPP_STYLE_SPEC;
        if (chosen == OPP_STYLE_SPEC && player_has_item_in_slot(self, GEAR_SLOT_WEAPON, ITEM_GRANITE_MAUL)) {
            uint8_t gear[NUM_DYNAMIC_GEAR_SLOTS];
            pvp_resolve_equipment_plan(self, PVP_EQUIPMENT_SPEC_MELEE, gear);
            opp->combo_control.phase = self->special_energy >= osrs_spec_cost(gear[0]) + osrs_spec_cost(ITEM_GRANITE_MAUL)
                ? OPP_COMBO_SUBMITTED : OPP_COMBO_IDLE;
            opp->combo_control.launch_tick = env->pvp_runtime.visible[actor].last_attack_tick;
            opp->combo_control.energy = self->special_energy;
        }
    }
    opp_apply_boost_potion(actions, self, chosen);
    opp_apply_gear_switch(actions, self, chosen);
    actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
    actions[OSRS_HEAD_SPECIAL] = chosen == OPP_STYLE_SPEC ? 1 : 2;
    if (chosen == OPP_STYLE_MAGE) {
        actions[OSRS_HEAD_SPELL] = can_cast_blood_spell(self) &&
            (self->current_hitpoints < self->base_hitpoints * OPP_EAT_DOUBLE_THRESHOLD || !can_cast_ice_spell(self))
                ? OSRS_SPELL_BLOOD_BARRAGE : OSRS_SPELL_ICE_BARRAGE;
    }
}

static void opponent_reset(OsrsEnv* env, OpponentState* opp) {
    assert(osrs_nh_is_scripted_opponent(opp->type));
    OpponentType type = opp->type;
    memset(opp, 0, sizeof(*opp));
    opp->type = type;
    if (type == OPP_MIXED_NH) {
        int draw = rand_int(env, 100);
        for (unsigned i = 0; i < sizeof(MIXED_NH_POOL) / sizeof(*MIXED_NH_POOL); i++) {
            if (draw < MIXED_NH_CUM_WEIGHTS[i]) {
                opp->active_sub_policy = MIXED_NH_POOL[i];
                break;
            }
        }
    }
    opp->visible_history = (OppVisibleHistory){OPP_HISTORY_EMPTY,
        {ATTACK_STYLE_NONE, PRAYER_NONE}, {ATTACK_STYLE_NONE, PRAYER_NONE}};
    opp->pressure_control = (OppPressureControl){.phase = OPP_ICE_SEEK,
        .last_launch_tick = -1, .target_launch_tick = -1};
    if (type == OPP_VENG_FIGHTER)
        env->players[opp == &env->pvp_runtime.opponent_p0 ? 0 : 1].is_lunar_spellbook = 1;
}

static void generate_opponent_action(OsrsEnv* env, OpponentState* opp) {
    assert(osrs_nh_is_scripted_opponent(opp->type));
    int* actions = env->pending_actions + OSRS_BASE_NUM_ACTION_HEADS;
    memset(actions, 0, OSRS_BASE_NUM_ACTION_HEADS * sizeof(*actions));
    OpponentType active = opp->type == OPP_MIXED_NH ? opp->active_sub_policy : opp->type;
    switch (active) {
        case OPP_UNPREDICTABLE_ONETICK:
        case OPP_GENERALIST_NH:
        case OPP_VENG_FIGHTER:
        case OPP_GMAUL_COMBO:
        case OPP_RANGE_KITER:
            opp_generalist_nh(env, opp, actions, active);
            break;
        case OPP_RECORDED_ADAPTIVE_NH:
            opp_recorded_nh(env, opp, actions);
            break;
        case OPP_BLOOD_PRESSURE_NH:
        case OPP_FREEZE_UNDERLAP_NH:
            opp_pressure_nh(env, opp, actions, active);
            break;
        default:
            abort();
    }
    Player* self = &env->players[1];
    uint8_t weapon = self->equipped[GEAR_SLOT_WEAPON];
    int cell = actions[OSRS_HEAD_EQUIP_SLOT(GEAR_SLOT_WEAPON)] - 1;
    if (cell >= 0) {
        if (osrs_can_equip_from_cell(self, self->inventory_cells, cell)) {
            weapon = osrs_inventory_cell_metadata(&self->inventory_cells[cell])->item_idx;
        } else {
            actions[OSRS_HEAD_EQUIP_SLOT(GEAR_SLOT_WEAPON)] = 0;
            actions[OSRS_HEAD_SPECIAL] = 2;
        }
    }
    OppAttackStyles styles = opp_submitted_attack_styles(
        weapon, actions[OSRS_HEAD_PRIMARY],
        actions[OSRS_HEAD_SPELL], actions[OSRS_HEAD_SPECIAL], self->spec_armed,
        self->special_energy, can_cast_ice_spell(self), can_cast_blood_spell(self));
    if (styles.offensive != ATTACK_STYLE_NONE)
        actions[OSRS_HEAD_OFFENSIVE] = styles.offensive == ATTACK_STYLE_MAGIC
            ? ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY
            : styles.offensive == ATTACK_STYLE_RANGED
                ? ENCOUNTER_OFFENSIVE_SET_REFRESH_RIGOUR : ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY;
}

static inline void pvp_swap_players_and_actions(OsrsEnv* env) {
    Player player = env->players[0];
    env->players[0] = env->players[1];
    env->players[1] = player;
    for (int head = 0; head < OSRS_BASE_NUM_ACTION_HEADS; head++) {
        int action = env->pending_actions[head];
        env->pending_actions[head] =
            env->pending_actions[OSRS_BASE_NUM_ACTION_HEADS + head];
        env->pending_actions[OSRS_BASE_NUM_ACTION_HEADS + head] = action;
    }
}

static inline void generate_opponent_action_for_player0(
    OsrsEnv* env,
    OpponentState* opp
) {
    pvp_swap_players_and_actions(env);
    generate_opponent_action(env, opp);
    pvp_swap_players_and_actions(env);
}

#endif
