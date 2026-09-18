#ifndef OSRS_RISKFIGHT_TACTICIAN_H
#define OSRS_RISKFIGHT_TACTICIAN_H
#include "../../osrs_pvp_threat.h"

typedef struct {
    int damage;
    int ticks_until;
    int incoming_animation;
} RiskfightThreatWindow;

typedef struct {
    int food, drink, combo;
    int healed_hp;
    int clicks;
} RiskfightEatPlan;

typedef enum { RISKFIGHT_TIMING_TRADE, RISKFIGHT_TIMING_FLOOR } RiskfightAttackTiming;

typedef struct {
    int extra_eat_hp;
    int finisher_hp_margin;
    int reflection_reserve_percent;
    RiskfightAttackTiming timing;
    OsrsEscapeHealing minimum_healing;
} RiskfightTacticianProfile;

static const RiskfightTacticianProfile RISKFIGHT_PROFILE_BALANCED = {0, 0, 100, RISKFIGHT_TIMING_TRADE, OSRS_ESCAPE_DOUBLE_EATS};
static const RiskfightTacticianProfile RISKFIGHT_PROFILE_PRESSURE = {0, 8, 85, RISKFIGHT_TIMING_TRADE, OSRS_ESCAPE_DOUBLE_EATS};
static const RiskfightTacticianProfile RISKFIGHT_PROFILE_CAUTIOUS = {8, -8, 110, RISKFIGHT_TIMING_TRADE, OSRS_ESCAPE_TRIPLE_EATS};
static const RiskfightTacticianProfile RISKFIGHT_PROFILE_HELDOUT = {4, 4, 95, RISKFIGHT_TIMING_TRADE, OSRS_ESCAPE_DOUBLE_EATS};

static const RiskfightTacticianProfile RISKFIGHT_PROFILE_FLOOR = {0, 0, 100, RISKFIGHT_TIMING_FLOOR, OSRS_ESCAPE_DOUBLE_EATS};

typedef enum { RISKFIGHT_CONTINUE, RISKFIGHT_LAST_ATTACK, RISKFIGHT_RETREAT } RiskfightExitDecision;

static RiskfightExitDecision riskfight_exit_decision(const Player* after_eating, RiskfightThreatWindow threat, OsrsEscapeHealing minimum_healing,
    int observed_pressure, int eat_clicks, int attacking, int reflection_damage) {
    OsrsEscapeSupplies supplies = osrs_escape_supplies(after_eating);
    int depleted = supplies.healing < minimum_healing || osrs_escape_no_boost(&supplies);
    if (threat.ticks_until == 0 && after_eating->current_hitpoints <= threat.damage &&
            eat_clicks == 0 && (observed_pressure || supplies.healing == OSRS_ESCAPE_NO_HEALING))
        return RISKFIGHT_RETREAT;
    if (!depleted) return RISKFIGHT_CONTINUE;
    int incoming = threat.ticks_until == 0 ? threat.damage : 0;
    if (attacking && after_eating->attack_timer <= 1 &&
            after_eating->current_hitpoints > incoming + reflection_damage)
        return RISKFIGHT_LAST_ATTACK;
    if (threat.ticks_until > after_eating->attack_timer &&
            after_eating->current_hitpoints > reflection_damage)
        return RISKFIGHT_CONTINUE;
    return RISKFIGHT_RETREAT;
}

// Dharok axe discipline: tentacle is the default; the axe is a low-HP
// finisher only. Recorded play (session 20260913T124331: tentacle anim 1658
// at 19629, axe anim 2067 at 19630 for the KO) shows a one-tick lethal
// switch, not camping. At 99-121 HP the 4-piece multiplier is 1.0 and the
// 7-tick 2H axe is strictly -EV vs the 4-tick tentacle, so require the
// post-eat HP to sit below base (actually boosted) before the axe may
// compete. All three gates must hold:
//   (a) attacking this tick (own_ready, RF_ATTACK, attack window, eating
//       plans handled by the caller via attacking_this_tick),
//   (b) dharok actually boosted (own_hp_after_eating < base_hp),
//   (c) finisher/trade +EV on axe normal_max: fresh low opponent_hp_upper
//       within axe_max + margin, OR last-attack exit, OR reflecting an
//       incoming animation (the same exception specs already enjoy).
// opponent_hp_upper is the fresh upper bound; callers pass the inferred
// range upper (upper == 121 means unknown/full — never a finisher).
// Default margin 60 calibrates to the recorded axe-swing HP window
// (victim upper bound 105, lethal 68; ±10 covers bar quantization).
static int riskfight_dharok_finisher(int attacking_this_tick, int own_hp_after_eating,
    int own_base_hp, int axe_max, int opponent_hp_upper, int finisher_hp_margin,
    RiskfightExitDecision exit, int reflecting, int incoming_animation) {
    if (!attacking_this_tick) return 0;
    if (own_hp_after_eating >= own_base_hp) return 0;
    if (exit == RISKFIGHT_LAST_ATTACK) return 1;
    if (reflecting && incoming_animation) return 1;
    return opponent_hp_upper < own_base_hp &&
        opponent_hp_upper <= axe_max + finisher_hp_margin;
}


typedef enum {
    RISKFIGHT_HP_FRESH,
    RISKFIGHT_HP_RETAINED,
    RISKFIGHT_HP_CONSUMPTION_AMBIGUOUS,
    RISKFIGHT_HP_HISTORY_MISSING,
} RiskfightHpEvidence;

typedef struct {
    OsrsHealthBarRange range;
    RiskfightHpEvidence evidence;
} RiskfightInferredHp;

static RiskfightInferredHp riskfight_inferred_opponent_hp(const float* obs) {
    int bar = (int)lroundf(obs[RF_OPPONENT_START + NUM_GEAR_SLOTS] * OSRS_PLAYER_HEALTH_BAR_SCALE);
    RiskfightInferredHp hp = {osrs_health_bar_range(bar, OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 121),
        RISKFIGHT_HP_HISTORY_MISSING};
    assert(hp.range.kind == OSRS_HEALTH_BAR_KNOWN);
    int consumed = 0;
    for (int ago = 0; ago < RF_HISTORY_TICKS; ago++) {
        const float* event = obs + RF_HISTORY_START + ago * RF_EVENT_WIDTH;
        consumed |= event[6] != 0;
        if (event[3] == 0) continue;
        if (consumed) {
            hp.range.upper = 121;
            hp.evidence = RISKFIGHT_HP_CONSUMPTION_AMBIGUOUS;
        } else {
            hp.evidence = ago == 0 ? RISKFIGHT_HP_FRESH : RISKFIGHT_HP_RETAINED;
            int possible_regen = (ago + ENCOUNTER_STAT_DRIFT_TICKS - 1) / ENCOUNTER_STAT_DRIFT_TICKS;
            hp.range.upper += possible_regen;
            if (hp.range.upper > 121) hp.range.upper = 121;
        }
        return hp;
    }
    hp.range.upper = 121;
    hp.evidence = consumed ? RISKFIGHT_HP_CONSUMPTION_AMBIGUOUS : RISKFIGHT_HP_HISTORY_MISSING;
    return hp;
}

static Player riskfight_observed_self(const float* obs) {
    Player p;
    memset(&p, 0, sizeof(p));
    encounter_init_maxed_player_combat_stats(&p, 99);
    p.current_hitpoints = (int)lroundf(obs[0] * 121);
    p.current_prayer = (int)lroundf(obs[1] * 99);
    p.current_attack = (int)lroundf(obs[2] * 118);
    p.current_strength = (int)lroundf(obs[3] * 118);
    p.current_defence = (int)lroundf(obs[4] * 120);
    p.current_magic = (int)lroundf(obs[5] * 99);
    p.special_energy = (int)lroundf(obs[6] * 100);
    p.attack_timer = (int)lroundf(obs[7] * 10);
    p.food_timer = (int)lroundf(obs[8] * 3);
    p.potion_timer = (int)lroundf(obs[9] * 3);
    p.karambwan_timer = (int)lroundf(obs[10] * 3);
    // obs[23] is now maul prepared_hits/2 (schema 3); the timer flag it
    // replaced is derivable: a live cooldown implies the timer exists.
    p.has_attack_timer = p.attack_timer != 0;
    p.offensive_prayer = p.current_prayer > 0 ? OFFENSIVE_PRAYER_PIETY : OFFENSIVE_PRAYER_NONE;
    p.fight_style = FIGHT_STYLE_AGGRESSIVE;
    for (int i = 0; i < NUM_GEAR_SLOTS; i++)
        p.equipped[i] = (uint8_t)lroundf(obs[RF_EQUIPPED_START + i] * RF_OBSERVATION_ITEM_SCALE);
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        p.inventory_cells[i] = osrs_inventory_cell_from_content_code(
            osrs_inventory_cell_obs_code_decode(obs[RF_INVENTORY_START + i * RF_INVENTORY_WIDTH]));
    return p;
}

static RiskfightThreatWindow riskfight_threat_window(const float* obs) {
    Player assumed;
    memset(&assumed, 0, sizeof(assumed));
    encounter_init_maxed_player_combat_stats(&assumed, 99);
    encounter_super_combat_boost(&assumed);
    assumed.offensive_prayer = OFFENSIVE_PRAYER_PIETY;
    assumed.fight_style = FIGHT_STYLE_AGGRESSIVE;
    for (int i = 0; i < NUM_GEAR_SLOTS; i++)
        assumed.equipped[i] = (uint8_t)lroundf(obs[RF_OPPONENT_START + i] * RF_OBSERVATION_ITEM_SCALE);
    assumed.equipped[GEAR_SLOT_RING] = ITEM_ULTOR_RING;
    const float* opponent = obs + RF_OPPONENT_START + NUM_GEAR_SLOTS;
    int bar = (int)lroundf(opponent[0] * OSRS_PLAYER_HEALTH_BAR_SCALE);
    OsrsHealthBarRange hp = osrs_health_bar_range(bar, OSRS_PLAYER_HEALTH_BAR_SCALE,
        assumed.base_hitpoints, 121);
    assert(hp.kind == OSRS_HEALTH_BAR_KNOWN);
    int hp_lower = bar == 0 ? 1 : hp.lower;
    OsrsMeleeThreat threat = osrs_melee_threat(assumed.equipped,
        calculate_effective_strength(&assumed, ATTACK_STYLE_MELEE), assumed.base_hitpoints, hp_lower, 100);
    int age = (int)lroundf(opponent[5] * RF_OBSERVATION_ATTACK_AGE_SCALE);
    int ticks = opponent[4] ? (int)lroundf(opponent[6] * RF_OBSERVATION_ATTACK_AGE_SCALE) : 0;
    int animation = opponent[4] && age == 1;
    if (animation || threat.instant_special_max > 0) ticks = 0;
    int damage = threat.normal_plus_instant_max;
    const float* event = obs + RF_HISTORY_START;
    if (event[0] > 0) {
        int launched_weapon = (int)lroundf(event[1] * RF_OBSERVATION_ITEM_SCALE);
        assumed.equipped[GEAR_SLOT_WEAPON] = launched_weapon;
        if (item_is_two_handed(launched_weapon)) assumed.equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        OsrsMeleeThreat pending = osrs_melee_threat(assumed.equipped,
            calculate_effective_strength(&assumed, ATTACK_STYLE_MELEE), assumed.base_hitpoints, 1, 100);
        int pending_max = pending.normal_plus_instant_max;
        if (pending.special_stack_max > pending_max) pending_max = pending.special_stack_max;
        if (pending_max > damage) damage = pending_max;
        ticks = 0;
        animation = 1;
    }
    if (threat.special_stack_max > damage) damage = threat.special_stack_max;
    return (RiskfightThreatWindow){damage, ticks, animation};
}

static int riskfight_player_find_food(const Player* p, OsrsConsumableKind kind) {
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[i])->consumable_kind == kind) return i + 1;
    return 0;
}

static RiskfightEatPlan riskfight_eat_candidate(Player* p, int food, int drink, int combo) {
    RiskfightEatPlan plan = {food, drink, combo, p->current_hitpoints, 0};
    OsrsInventoryUseState use = {0};
    int slots[] = {food, drink, combo};
    for (int i = 0; i < 3; i++) {
        if (!slots[i]) continue;
        OsrsInventoryUseResult result = osrs_player_use_inventory(p, &use, NULL, slots[i] - 1, 0);
        if (result != OSRS_INVENTORY_USE_CONSUMED) {
            plan.clicks = -1;
            return plan;
        }
        plan.clicks++;
    }
    plan.healed_hp = p->current_hitpoints;
    return plan;
}

static RiskfightEatPlan riskfight_choose_eat(const Player* p, int target) {
    if (p->current_hitpoints >= target) return (RiskfightEatPlan){0, 0, 0, p->current_hitpoints, 0};
    int foods[] = {0, riskfight_player_find_food(p, OSRS_CONSUMABLE_SUMMER_PIE),
        riskfight_player_find_food(p, OSRS_CONSUMABLE_MARLIN)};
    int brew = riskfight_player_find_food(p, OSRS_CONSUMABLE_BREW);
    int halibut = riskfight_player_find_food(p, OSRS_CONSUMABLE_HALIBUT);
    RiskfightEatPlan best = {0, 0, 0, p->current_hitpoints, 0};
    for (int f = 0; f < 3; f++) for (int b = 0; b <= (brew != 0); b++) for (int h = 0; h <= (halibut != 0); h++) {
        if (f > 0 && !foods[f]) continue;
        Player next = *p;
        RiskfightEatPlan candidate = riskfight_eat_candidate(&next, foods[f], b ? brew : 0, h ? halibut : 0);
        if (candidate.clicks < 0) continue;
        int safe = candidate.healed_hp >= target;
        int best_safe = best.healed_hp >= target;
        if ((safe && !best_safe) || (safe && best_safe &&
                (candidate.healed_hp < best.healed_hp ||
                 (candidate.healed_hp == best.healed_hp && candidate.clicks < best.clicks))) ||
                (!safe && !best_safe && candidate.healed_hp > best.healed_hp)) best = candidate;
    }
    return best;
}

static RiskfightEatPlan riskfight_timed_eat(const Player* p, RiskfightThreatWindow threat) {
    int target = threat.damage + 1;
    if (threat.ticks_until == 0) return riskfight_choose_eat(p, target);
    Player future = *p;
    future.food_timer = future.food_timer > threat.ticks_until ? future.food_timer - threat.ticks_until : 0;
    future.potion_timer = future.potion_timer > threat.ticks_until ? future.potion_timer - threat.ticks_until : 0;
    future.karambwan_timer = future.karambwan_timer > threat.ticks_until ? future.karambwan_timer - threat.ticks_until : 0;
    RiskfightEatPlan later = riskfight_choose_eat(&future, target);
    if (later.healed_hp >= target) return (RiskfightEatPlan){0, 0, 0, p->current_hitpoints, 0};
    return riskfight_choose_eat(p, target - (later.healed_hp - p->current_hitpoints));
}

static int riskfight_floor_attack_window(const float* obs, const Player* self,
        RiskfightThreatWindow threat) {
    const float* event = obs + RF_HISTORY_START;
    if (threat.incoming_animation || event[6] || (event[3] && event[4] > 0)) return 1;
    if (self->attack_timer > 1) return 0;
    const float* opponent = obs + RF_OPPONENT_START + NUM_GEAR_SLOTS;
    if (opponent[3] && threat.ticks_until <= 1) return 0;
    EquipmentBonuses gear;
    osrs_sum_equipment_bonuses(self->equipped, &gear);
    int speed = gear.attack_speed;
    int tick = (int)lroundf(obs[20] * RF_OBSERVATION_TICK_SCALE);
    int side = opponent[1] < 0 ? speed : 0;
    return (tick + side) % (2 * speed) < 2;
}

static void riskfight_tactician_profile(const float* obs, int* actions,
        RiskfightTacticianProfile profile) {
    Player self = riskfight_observed_self(obs);
    OsrsInventoryIndex inventory = osrs_inventory_index(self.inventory_cells);
    if (self.current_hitpoints <= 0) return;
    RiskfightThreatWindow threat = riskfight_threat_window(obs);
    RiskfightThreatWindow eating_threat = threat;
    eating_threat.damage += profile.extra_eat_hp;
    RiskfightEatPlan eat = riskfight_timed_eat(&self, eating_threat);
    actions[RF_PRIMARY] = RF_ATTACK;
    actions[RF_PRAYER] = self.offensive_prayer;
    actions[RF_STYLE] = self.fight_style;
    actions[RF_FOOD] = eat.food;
    actions[RF_DRINK] = eat.drink;
    actions[RF_COMBO] = eat.combo;
    int eating = eat.clicks > 0;
    if (eating) actions[RF_PRIMARY] = RF_STOP;
    Player after_eating = self;
    riskfight_eat_candidate(&after_eating, eat.food, eat.drink, eat.combo);
    int veng_ready = !obs[11] && obs[12] <= 1.0f / OSRS_VENGEANCE_COOLDOWN &&
        after_eating.current_magic >= OSRS_VENGEANCE_MAGIC_LEVEL && obs[19] > 0;
    if (veng_ready && threat.ticks_until == 0) actions[RF_VENGEANCE] = 1;
    int reflecting = obs[11] || actions[RF_VENGEANCE];
    int own_ready = self.attack_timer <= 1;
    if (!eating && own_ready && reflecting && threat.ticks_until == 1) actions[RF_PRIMARY] = RF_STOP;
    int attack_window = profile.timing == RISKFIGHT_TIMING_TRADE ||
        riskfight_floor_attack_window(obs, &self, threat);
    if (!attack_window) actions[RF_PRIMARY] = RF_STOP;
    OsrsEscapeSupplies remaining = osrs_escape_supplies(&after_eating);
    int conserve_escape = remaining.healing < profile.minimum_healing || osrs_escape_no_boost(&remaining);
    int equipped_weapon = self.equipped[GEAR_SLOT_WEAPON];
    int chosen_weapon = ITEM_ABYSSAL_TENTACLE;
    int best_max = -1;
    const int weapons[] = {ITEM_ABYSSAL_TENTACLE, ITEM_DHAROKS_GREATAXE, ITEM_VOIDWAKER, ITEM_GRANITE_MAUL_ORNATE};
    RiskfightInferredHp opponent_hp = riskfight_inferred_opponent_hp(obs);
    int opponent_hp_upper = opponent_hp.range.upper;
    for (int i = 0; i < 4; i++) {
        int weapon = weapons[i];
        if (equipped_weapon != weapon && !inventory.item_slot_plus_one[weapon]) continue;
        Player candidate = self;
        int weapon_slot = inventory.item_slot_plus_one[weapon] - 1;
        if (equipped_weapon != weapon && osrs_equip_from_cell(&candidate, candidate.inventory_cells, weapon_slot) < 0) continue;
        int shield_slot = inventory.item_slot_plus_one[ITEM_AVERNIC_DEFENDER] - 1;
        if (!item_is_two_handed(weapon) && shield_slot >= 0)
            osrs_equip_from_cell(&candidate, candidate.inventory_cells, shield_slot);
        const int armour[] = {ITEM_DHAROKS_HELM, ITEM_DHAROKS_PLATEBODY, ITEM_DHAROKS_PLATELEGS};
        for (int a = 0; a < 3; a++) {
            int slot = inventory.item_slot_plus_one[armour[a]] - 1;
            if (slot >= 0) osrs_equip_from_cell(&candidate, candidate.inventory_cells, slot);
        }
        candidate.current_hitpoints = after_eating.current_hitpoints;
        candidate.current_strength = after_eating.current_strength;
        OsrsMeleeThreat hit = osrs_melee_threat(candidate.equipped,
            calculate_effective_strength(&candidate, ATTACK_STYLE_MELEE), candidate.base_hitpoints,
            candidate.current_hitpoints, candidate.special_energy);
        int spec = !conserve_escape && i >= 2 && hit.special_count > 0 && !eating && attack_window &&
            (own_ready || hit.instant_special_max > 0) &&
            (opponent_hp_upper <= hit.special_stack_max + profile.finisher_hp_margin || (reflecting && threat.incoming_animation));
        if (i >= 2 && !spec) continue;
        // Axe (index 1) is a finisher, not a default: it only competes when
        // the dharok-finisher gates hold. Exit is not decided until below,
        // so evaluate the helper with RISKFIGHT_CONTINUE here and re-check
        // LAST_ATTACK after the exit decision (axe stays tentacle unless the
        // re-check promotes it).
        int axe_max = hit.normal_max;
        if (i == 1 && !riskfight_dharok_finisher(
                !eating && own_ready && actions[RF_PRIMARY] == RF_ATTACK && attack_window,
                after_eating.current_hitpoints, after_eating.base_hitpoints,
                axe_max, opponent_hp_upper, 60,
                RISKFIGHT_CONTINUE, reflecting, threat.incoming_animation))
            continue;
        int max_hit = spec ? hit.special_stack_max : hit.normal_max;
        if (max_hit > best_max) {
            best_max = max_hit;
            chosen_weapon = weapon;
            actions[RF_SPECIAL] = spec ? hit.special_count : 0;
        }
    }
    const float* opp_veng_state = obs + RF_OPPONENT_START + NUM_GEAR_SLOTS;
    // Opponent vengeance now observed (schema 4, opp[7..8]); without an
    // active veng there is no reflect to reserve HP against.
    int opp_veng = opp_veng_state[7] != 0;
    if (!eating && ((own_ready && actions[RF_PRIMARY] == RF_ATTACK) || actions[RF_SPECIAL])) {
        DamageResult returned = osrs_apply_post_mitigation_pipeline(best_max,
            opponent_hp_upper, 0, opp_veng, 1, 0);
        int return_damage = ((returned.veng_damage + returned.recoil_damage) *
            profile.reflection_reserve_percent + 99) / 100;
        int contesting = profile.timing == RISKFIGHT_TIMING_TRADE ||
            opp_veng_state[3] || threat.incoming_animation;
        int required_hp = return_damage + (threat.ticks_until == 0 && contesting ? threat.damage : 0) + 1;
        if (self.current_hitpoints < required_hp) {
            if (threat.ticks_until == 0) eat = riskfight_choose_eat(&self, required_hp);
            actions[RF_FOOD] = eat.food;
            actions[RF_DRINK] = eat.drink;
            actions[RF_COMBO] = eat.combo;
            actions[RF_PRIMARY] = RF_STOP;
            actions[RF_SPECIAL] = 0;
            eating = eat.clicks > 0;
            after_eating = self;
            riskfight_eat_candidate(&after_eating, eat.food, eat.drink, eat.combo);
            if (after_eating.current_magic < OSRS_VENGEANCE_MAGIC_LEVEL) actions[RF_VENGEANCE] = 0;
        }
    }
    actions[RF_WEAPON] = inventory.item_slot_plus_one[chosen_weapon];
    if (!item_is_two_handed(chosen_weapon)) actions[RF_SHIELD] = inventory.item_slot_plus_one[ITEM_AVERNIC_DEFENDER];
    actions[RF_RING] = inventory.item_slot_plus_one[
        !own_ready && threat.ticks_until == 0 ? ITEM_RING_OF_RECOIL : ITEM_ULTOR_RING];
    Player equipped = self;
    for (int head = RF_WEAPON; head <= RF_RING; head++)
        if (actions[head]) osrs_equip_from_cell(&equipped, equipped.inventory_cells, actions[head] - 1);
    int free_slots = 0;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) free_slots += osrs_inventory_cell_is_empty(&equipped.inventory_cells[i]);
    const int armour_heads[] = {RF_HEAD, RF_BODY, RF_LEGS};
    const int armour_items[] = {ITEM_DHAROKS_HELM, ITEM_DHAROKS_PLATEBODY, ITEM_DHAROKS_PLATELEGS};
    int bait = !eating && !actions[RF_SPECIAL] && !own_ready && reflecting && threat.incoming_animation && self.current_hitpoints > threat.damage;
    for (int i = 0; i < 3; i++) {
        if (bait && free_slots > 0 && self.equipped[RF_GEAR_SLOT_BY_HEAD[armour_heads[i]]] != ITEM_NONE) {
            actions[armour_heads[i]] = RF_UNEQUIP;
            free_slots--;
        } else if (!bait) actions[armour_heads[i]] = inventory.item_slot_plus_one[armour_items[i]];
    }
    if (!eating && self.potion_timer == 0 && self.current_hitpoints > threat.damage) {
        if (self.current_prayer <= 40 || self.current_attack < self.base_attack || self.current_strength < self.base_strength ||
            self.current_magic < self.base_magic)
            actions[RF_DRINK] = inventory.consumable_slot_plus_one[OSRS_CONSUMABLE_SANFEW];
        else if (threat.ticks_until > 1 && self.current_strength < 110)
            actions[RF_DRINK] = inventory.consumable_slot_plus_one[OSRS_CONSUMABLE_SUPER_COMBAT];
    }
    DamageResult reflection = osrs_apply_post_mitigation_pipeline(best_max,
        opponent_hp_upper, 0, opp_veng, 1, 0);
    const float* observed_opponent = obs + RF_OPPONENT_START + NUM_GEAR_SLOTS;
    int observed_pressure = threat.incoming_animation ||
        (profile.timing == RISKFIGHT_TIMING_TRADE && observed_opponent[3]);
    RiskfightExitDecision exit = riskfight_exit_decision(&after_eating, eating_threat,
        profile.minimum_healing, observed_pressure, eat.clicks,
        actions[RF_PRIMARY] == RF_ATTACK, reflection.veng_damage + reflection.recoil_damage);
    if (exit == RISKFIGHT_RETREAT) {
        actions[RF_PRIMARY] = RF_TELEPORT;
        actions[RF_SPECIAL] = 0;
        actions[RF_ORB] = 0;
    } else if (exit == RISKFIGHT_LAST_ATTACK) actions[RF_SPECIAL] = 0;
    // LAST_ATTACK re-check: the loop above ran with CONTINUE, so promote a
    // boosted axe for the final trade when the finisher gates now hold.
    if (exit == RISKFIGHT_LAST_ATTACK && chosen_weapon == ITEM_ABYSSAL_TENTACLE &&
            !eating && own_ready && actions[RF_PRIMARY] == RF_ATTACK && attack_window &&
            after_eating.current_hitpoints < after_eating.base_hitpoints &&
            inventory.item_slot_plus_one[ITEM_DHAROKS_GREATAXE] && !actions[RF_WEAPON]) {
        actions[RF_WEAPON] = inventory.item_slot_plus_one[ITEM_DHAROKS_GREATAXE];
        actions[RF_SHIELD] = 0;
    }
}
static void riskfight_tactician(const float* obs, int* actions) {
    riskfight_tactician_profile(obs, actions, RISKFIGHT_PROFILE_BALANCED);
}
#endif
