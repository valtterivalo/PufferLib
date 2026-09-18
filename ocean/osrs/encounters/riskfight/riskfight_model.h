#ifndef OSRS_RISKFIGHT_MODEL_H
#define OSRS_RISKFIGHT_MODEL_H

#include "../../osrs_env.h"
#include "../../osrs_health_bar.h"
#include "../../osrs_player_inventory_use.h"
#include "../../osrs_encounter_visual_events.h"
#include "../../osrs_pvp_escape.h"

typedef enum { RISKFIGHT_TRADER, RISKFIGHT_CAUTIOUS, RISKFIGHT_AGGRESSIVE, RISKFIGHT_MIXED, RISKFIGHT_TACTICIAN, RISKFIGHT_PRESSURE, RISKFIGHT_SURVIVAL, RISKFIGHT_HELDOUT, RISKFIGHT_FLOOR } RiskfightOpponent;
typedef enum { RISKFIGHT_ONGOING, RISKFIGHT_KILL, RISKFIGHT_DEATH,
    RISKFIGHT_ESCAPE, RISKFIGHT_MUTUAL_DEATH } RiskfightOutcome;
enum {
    RF_WEAPON, RF_SHIELD, RF_RING, RF_FOOD, RF_DRINK, RF_COMBO,
    RF_ORB, RF_VENGEANCE, RF_SPECIAL, RF_PRIMARY, RF_PRAYER, RF_STYLE,
    RF_HEAD, RF_CAPE, RF_NECK, RF_BODY, RF_LEGS, RF_HANDS, RF_FEET, RF_AMMO,
    RF_HEADS,
    RF_UNEQUIP = OSRS_INVENTORY_SIZE + 1,
    RF_STOP = OSRS_PRIMARY_DIM(1),
    RF_ATTACK = OSRS_PRIMARY_MOVE_ACTIONS,
    RF_TELEPORT = RF_STOP + 1,
    RF_MASK_SIZE = NUM_GEAR_SLOTS * (OSRS_INVENTORY_SIZE + 2) +
        3 * (OSRS_INVENTORY_SIZE + 1) + 2 + 2 + 3 +
        OSRS_PRIMARY_DIM(1) + 2 + 5 + 4,
    RF_HISTORY_TICKS = 16,
    RF_EVENT_WIDTH = 8,
    RF_SELF_SIZE = 24,
    RF_INVENTORY_WIDTH = 6,
    RF_INVENTORY_START = RF_SELF_SIZE,
    RF_EQUIPPED_START = RF_INVENTORY_START + OSRS_INVENTORY_SIZE * RF_INVENTORY_WIDTH,
    RF_OPPONENT_START = RF_EQUIPPED_START + NUM_GEAR_SLOTS,
    RF_HISTORY_START = RF_OPPONENT_START + NUM_GEAR_SLOTS + 14,
    RF_OBS_SIZE = RF_HISTORY_START + RF_HISTORY_TICKS * RF_EVENT_WIDTH,
};
#define RF_ACTION_DIMS_INIT {30,30,30,29,29,29,2,2,3,28,5,4,30,30,30,30,30,30,30,30}
static const int RF_ACTION_DIMS[RF_HEADS] = RF_ACTION_DIMS_INIT;
static const int RF_GEAR_SLOT_BY_HEAD[RF_HEADS] = {
    GEAR_SLOT_WEAPON, GEAR_SLOT_SHIELD, GEAR_SLOT_RING,
    -1, -1, -1, -1, -1, -1, -1, -1, -1,
    GEAR_SLOT_HEAD, GEAR_SLOT_CAPE, GEAR_SLOT_NECK, GEAR_SLOT_BODY,
    GEAR_SLOT_LEGS, GEAR_SLOT_HANDS, GEAR_SLOT_FEET, GEAR_SLOT_AMMO,
};

typedef struct {
    uint8_t equipment[NUM_GEAR_SLOTS];
    int health_bar;
    int x, y;
    int interacting;
    int last_attack_tick;
    int last_attack_speed;
    // Inferred opponent consumption locks (schema 6): estimated remainders
    // derived solely from the public consume bit + bar deltas. Unknown
    // (no bar refresh yet) reads as 0 (can-eat).
    int consume_food_est, consume_potion_est, consume_karam_est, consume_delay_est;
    // Pending consume awaiting a bar refresh: tick opened, bar before the
    // heal, and accounted damage since (-1 tick = none open).
    int pending_consume_tick, pending_bar_before, pending_damage_since;
    float events[RF_HISTORY_TICKS][RF_EVENT_WIDTH];
} RiskfightVisibleOpponent;

typedef struct {
    OsrsEnv env;
    OsrsInventoryUseState inventory_use[2];
    RiskfightVisibleOpponent visible[2];
    RiskfightOutcome outcome[2];
    RiskfightOpponent mixed_opponent;
    int escaped[2];
    OsrsEscapeSupplies escape_supplies[2][2];
    int escape_tick[2];
    OsrsUnequipResult last_unequip_result[2];
    float rewards[2];
    float episode_returns[2];
    float damage_rewards[2];
    float direct_ko_chance_mass[2];
    float chance_rewards[2];
    float teleport_penalties[2];
    // DEBUG-ONLY shaping accumulators (parallel to damage/chance rewards).
    float debug_maul_rewards[2];
    float debug_axe_rewards[2];
    // Consumption + weapon metrics (policy-side only, never observed):
    // drink/eat/spec event counts per episode and per-tick equipped-weapon
    // samples. Reset in riskfight_reset; puf_step aggregates agent 0.
    int drink_brew[2];
    int drink_sanfew[2];
    int drink_combat[2];
    int eat_marlin[2];
    int eat_halibut[2];
    int eat_pie[2];
    int spec_voidwaker[2];
    int spec_maul[2];
    // Opportunity denominators (per tick, pre-terminal, agent-indexed):
    // combat_opp = drained (atk/str/def < 110... see step site) + timer free.
    // maul_opp = maul equipped + energy>=50. axe_opp = finisher gate open.
    int combat_opp[2];
    int maul_opp[2];
    int axe_opp[2];
    int ticks_tentacle[2];
    int ticks_axe[2];
    int ticks_voidwaker[2];
    int ticks_maul[2];
} RiskfightState;
typedef struct {
    const CollisionMap* collision_map;
    const EncounterArenaTopology* route_topology;
    OsrsActorRouteCache routes[2];
    HumanInput policy_commands[2];
    RiskfightOpponent opponent;
    int self_play;
    int human_player;
    float damage_reward_coeff;
    float chance_reward_coeff;
    float teleport_penalty;
    // DEBUG-ONLY shaping probes (default 0, never ship nonzero): paid maul
    // double event bonus + low-HP axe swing bonus. See step sites.
    float maul_double_reward;
    float axe_hit_reward;
} RiskfightContext;

static inline float riskfight_outcome_reward(RiskfightOutcome outcome) {
    return outcome == RISKFIGHT_KILL ? 1 : outcome == RISKFIGHT_DEATH ? -1 : 0;
}

static void riskfight_write_observation(const RiskfightState*, int, float*);
static void riskfight_script(const float*, RiskfightOpponent, int*);
static void riskfight_write_action_mask(const RiskfightState*, int, float*);

static void riskfight_init_context(EncounterContext* context) {
    RiskfightContext* ctx = (RiskfightContext*)context;
    memset(ctx, 0, sizeof(*ctx));
    for (int i = 0; i < 2; i++) human_input_init(&ctx->policy_commands[i]);
}

static void riskfight_destroy_context(EncounterContext* context) {
    RiskfightContext* ctx = (RiskfightContext*)context;
    for (int i = 0; i < 2; i++) free(ctx->policy_commands[i].commands.items);
}

static void riskfight_init_state(EncounterState* state, EncounterContext* context) {
    (void)context;
    memset(state, 0, sizeof(RiskfightState));
    ((RiskfightState*)state)->env.rng_state = 1;
}

static EncounterState* riskfight_create(void) {
    RiskfightState* s = (RiskfightState*)malloc(sizeof(*s));
    assert(s);
    riskfight_init_state((EncounterState*)s, NULL);
    return (EncounterState*)s;
}
static void riskfight_destroy(EncounterState* s) { free(s); }
static void riskfight_finalize_context(EncounterState* state, EncounterContext* context) {
    (void)state;
    RiskfightContext* ctx = (RiskfightContext*)context;
    ctx->route_topology = pvp_route_topology_finalize(ctx->collision_map);
}

// Bag-legal nominal heals -> (food, potion, karam, delay) effects. Drink
// adds no attack delay and arms food+potion locks; food +3 delay; halibut
// +2 delay stacked with food (5 total). Ambiguous heals are pre-unioned
// rows (e.g. 24 = marlin alone or marlin+0-heal potion), so lookup is a
// direct map. 31 = pie+halibut with or without a 0-heal potion folded in.
static void riskfight_classify_consume_heal(int lo, int hi,
        int* food, int* potion, int* karam, int* delay) {
    static const int heals[] = {0, 11, 16, 20, 24, 27, 31, 36, 40, 44, 47, 60};
    static const int eff_food[] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    static const int eff_potion[] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    static const int eff_karam[] = {0, 0, 0, 2, 0, 0, 2, 2, 0, 2, 2, 2};
    static const int eff_delay[] = {0, 3, 0, 2, 3, 3, 5, 2, 3, 5, 5, 5};
    int n = (int)(sizeof(heals) / sizeof(heals[0]));
    int f = 0, po = 0, k = 0, d = 0, matched = 0;
    for (int i = 0; i < n; i++) {
        if (heals[i] < lo || heals[i] > hi) continue;
        matched = 1;
        if (eff_food[i] > f) f = eff_food[i];
        if (eff_potion[i] > po) po = eff_potion[i];
        if (eff_karam[i] > k) k = eff_karam[i];
        if (eff_delay[i] > d) d = eff_delay[i];
    }
    if (!matched) { f = 3; po = 3; k = 2; d = 5; }
    *food = f; *potion = po; *karam = k; *delay = d;
}

static void riskfight_observe_visible(RiskfightState* s, int viewer, int reset) {
    const Player* opponent = &s->env.players[1 - viewer];
    RiskfightVisibleOpponent* v = &s->visible[viewer];
    memcpy(v->equipment, opponent->equipped, sizeof(v->equipment));
    v->equipment[GEAR_SLOT_RING] = ITEM_NONE;
    v->equipment[GEAR_SLOT_AMMO] = ITEM_NONE;
    v->x = opponent->x;
    v->y = opponent->y;
    v->interacting = osrs_interaction_active(&opponent->interaction);
    int bar_before = v->health_bar;
    if (reset || opponent->hit_landed_this_tick)
        v->health_bar = osrs_health_bar_ratio(opponent->current_hitpoints,
            opponent->base_hitpoints, OSRS_PLAYER_HEALTH_BAR_SCALE);
    if (reset) {
        v->last_attack_tick = -1;
        v->consume_food_est = 0; v->consume_potion_est = 0;
        v->consume_karam_est = 0; v->consume_delay_est = 0;
        v->pending_consume_tick = -1; v->pending_bar_before = 0;
        v->pending_damage_since = 0;
        return;
    }
    if (v->consume_food_est > 0) v->consume_food_est--;
    if (v->consume_potion_est > 0) v->consume_potion_est--;
    if (v->consume_karam_est > 0) v->consume_karam_est--;
    if (v->consume_delay_est > 0) v->consume_delay_est--;
    int index = (s->env.tick - 1) % RF_HISTORY_TICKS;
    float* event = v->events[index];
    event[3] = opponent->hit_landed_this_tick;
    event[4] = opponent->hit_damage;
    int consuming = osrs_consumption_animation_visible(opponent->current_hitpoints,
        opponent->attack_style_this_tick, opponent->ate_food_this_tick,
        opponent->ate_karambwan_this_tick,
        s->inventory_use[1 - viewer].potion_animation_tick_plus_one == s->env.tick);
    event[5] = opponent->cast_veng_this_tick && !opponent->just_attacked && !consuming;
    event[6] = consuming;
    event[7] = v->health_bar;
    int bar_event = opponent->hit_landed_this_tick;
    // Open before accounting so a same-tick eat+hit attributes this tick's
    // damage to the new pending (otherwise the heal is underestimated).
    if (consuming && v->pending_consume_tick < 0) {
        v->pending_consume_tick = s->env.tick;
        v->pending_bar_before = bar_before;
        v->pending_damage_since = 0;
    }
    if (bar_event && v->pending_consume_tick >= 0)
        v->pending_damage_since += opponent->hit_damage;
    if (bar_event && v->pending_consume_tick >= 0) {
        if (opponent->current_hitpoints > 0) {
            OsrsHealthBarRange before = osrs_health_bar_range(v->pending_bar_before,
                OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 121);
            OsrsHealthBarRange after = osrs_health_bar_range(v->health_bar,
                OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 121);
            if (before.kind == OSRS_HEALTH_BAR_KNOWN && after.kind == OSRS_HEALTH_BAR_KNOWN) {
                OsrsHealthChangeRange ch = osrs_health_bar_unobserved_change(before, after,
                    v->pending_damage_since);
                int f, po, k, d;
                riskfight_classify_consume_heal(ch.lower, ch.upper, &f, &po, &k, &d);
                // effect - elapsed: observer decrements once per step,
                // mirroring sim cadence.
                int elapsed = s->env.tick - v->pending_consume_tick;
                v->consume_food_est = f - elapsed > 0 ? f - elapsed : 0;
                v->consume_potion_est = po - elapsed > 0 ? po - elapsed : 0;
                v->consume_karam_est = k - elapsed > 0 ? k - elapsed : 0;
                v->consume_delay_est = d - elapsed > 0 ? d - elapsed : 0;
            }
            v->pending_consume_tick = -1;
            v->pending_damage_since = 0;
        }
    } else if (bar_event) {
        v->pending_damage_since = 0;
    }
}


static void riskfight_reset(EncounterState* state, EncounterContext* context, uint32_t seed) {
    RiskfightState* s = (RiskfightState*)state;
    RiskfightContext* ctx = (RiskfightContext*)context;
    uint32_t rng = seed ? seed : s->env.rng_state;
    memset(s, 0, sizeof(*s));
    s->env.rng_state = rng;
    s->env.winner = -1;
    s->env.pvp_runtime.teleport_world = OSRS_TELEPORT_WORLD_PVP;
    pvp_reset_priority(&s->env, OSRS_PRIORITY_PVP_WORLD);
    if (ctx->opponent == RISKFIGHT_MIXED && !ctx->self_play)
        s->mixed_opponent = (RiskfightOpponent)rand_int(&s->env, RISKFIGHT_MIXED);
    uint8_t equipment[NUM_GEAR_SLOTS] = {0};
    equipment[GEAR_SLOT_AMMO] = ITEM_NONE;
    equipment[GEAR_SLOT_HEAD] = ITEM_DHAROKS_HELM;
    equipment[GEAR_SLOT_CAPE] = ITEM_INFERNAL_CAPE;
    equipment[GEAR_SLOT_NECK] = ITEM_AMULET_OF_RANCOUR;
    equipment[GEAR_SLOT_WEAPON] = ITEM_ABYSSAL_TENTACLE;
    equipment[GEAR_SLOT_BODY] = ITEM_DHAROKS_PLATEBODY;
    equipment[GEAR_SLOT_SHIELD] = ITEM_AVERNIC_DEFENDER;
    equipment[GEAR_SLOT_LEGS] = ITEM_DHAROKS_PLATELEGS;
    equipment[GEAR_SLOT_HANDS] = ITEM_FEROCIOUS_GLOVES;
    equipment[GEAR_SLOT_FEET] = ITEM_AVERNIC_TREADS;
    equipment[GEAR_SLOT_RING] = ITEM_ULTOR_RING;
    for (int i = 0; i < 2; i++) {
        Player* p = &s->env.players[i];
        init_player(p);
        s->env.pvp_runtime.maul[i] = osrs_granite_maul_init();
        encounter_init_maxed_player_combat_stats(p, 99);
        p->has_blood_fury = 0;
        p->is_lunar_spellbook = 1;
        p->veng_active = 1;
        p->current_hitpoints = 121;
        encounter_super_combat_boost(p);
        p->x = FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH / 2 + i;
        p->y = FIGHT_AREA_BASE_Y + FIGHT_AREA_HEIGHT / 2;
        p->dest_x = p->x; p->dest_y = p->y;
        memcpy(p->equipped, equipment, sizeof(equipment));
        osrs_refresh_player_equipment(p);
        pvp_refresh_visible_gear(p);
        // Divine pre-pot: full 500-tick clock, no HP cost. The bag carries
        // regular super combat for brew-drain re-boosts (no clock refresh).
        s->inventory_use[i].divine_combat_ticks = OSRS_DIVINE_DURATION;
        int slot = 0;
        const struct { OsrsConsumableKind kind; int doses, count; } supplies[] = {
            {OSRS_CONSUMABLE_SUMMER_PIE, 2, 3},
            {OSRS_CONSUMABLE_HALIBUT, 0, 4},
            {OSRS_CONSUMABLE_BREW, 4, 2},
            {OSRS_CONSUMABLE_MARLIN, 0, 8},
            {OSRS_CONSUMABLE_SANFEW, 4, 2},
            {OSRS_CONSUMABLE_SUPER_COMBAT, 4, 1},
            {OSRS_CONSUMABLE_TELEPORT, 0, 1},
            {OSRS_CONSUMABLE_VENGEANCE_SACK, 0, 1},
        };
        for (size_t k = 0; k < sizeof(supplies) / sizeof(*supplies); k++)
            for (int n = 0; n < supplies[k].count; n++)
                p->inventory_cells[slot++] = osrs_inventory_cell_from_content_code(osrs_inventory_content_code_from_consumable(
                    supplies[k].kind, supplies[k].doses));
        const uint8_t switches[] = {ITEM_DHAROKS_GREATAXE, ITEM_VOIDWAKER,
            ITEM_GRANITE_MAUL_ORNATE, ITEM_RING_OF_RECOIL};
        for (size_t k = 0; k < sizeof(switches); k++)
            p->inventory_cells[slot++] = osrs_inventory_cell_from_item(switches[k]);
        p->inventory_cells[slot++] = osrs_inventory_cell_from_content_code(osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_LOCATOR_ORB, 0));
        assert(slot == 27);
        s->inventory_use[i].orb_used_tick = -1;
        s->inventory_use[i].vengeance_consumed_tick = -1;
        s->inventory_use[i].vengeance_sacks = 100;
        s->env.pvp_runtime.walk_dest_x[i] = -1;
        s->env.pvp_runtime.walk_dest_y[i] = -1;
        osrs_actor_route_cache_clear(&ctx->routes[i]);
    }
    riskfight_observe_visible(s, 0, 1);
    riskfight_observe_visible(s, 1, 1);
}

#endif
