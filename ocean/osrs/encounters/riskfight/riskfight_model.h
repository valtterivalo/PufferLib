#ifndef OSRS_RISKFIGHT_MODEL_H
#define OSRS_RISKFIGHT_MODEL_H

#include "../../osrs_env.h"
#include "../../osrs_health_bar.h"
#include "../../osrs_player_inventory_use.h"
#include "../../osrs_encounter_visual_events.h"

typedef enum { RISKFIGHT_TRADER, RISKFIGHT_CAUTIOUS, RISKFIGHT_AGGRESSIVE, RISKFIGHT_MIXED, RISKFIGHT_TACTICIAN, RISKFIGHT_PRESSURE, RISKFIGHT_SURVIVAL, RISKFIGHT_HELDOUT } RiskfightOpponent;
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
    RF_HISTORY_START = RF_OPPONENT_START + NUM_GEAR_SLOTS + 7,
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
    float events[RF_HISTORY_TICKS][RF_EVENT_WIDTH];
} RiskfightVisibleOpponent;

typedef struct {
    OsrsEnv env;
    OsrsInventoryUseState inventory_use[2];
    RiskfightVisibleOpponent visible[2];
    RiskfightOutcome outcome[2];
    RiskfightOpponent mixed_opponent;
    int escaped[2];
    OsrsUnequipResult last_unequip_result[2];
    float rewards[2];
    float episode_returns[2];
    float damage_rewards[2];
    float teleport_penalties[2];
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
    float teleport_penalty;
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

static void riskfight_observe_visible(RiskfightState* s, int viewer, int reset) {
    const Player* opponent = &s->env.players[1 - viewer];
    RiskfightVisibleOpponent* v = &s->visible[viewer];
    memcpy(v->equipment, opponent->equipped, sizeof(v->equipment));
    v->equipment[GEAR_SLOT_RING] = ITEM_NONE;
    v->equipment[GEAR_SLOT_AMMO] = ITEM_NONE;
    v->x = opponent->x;
    v->y = opponent->y;
    v->interacting = osrs_interaction_active(&opponent->interaction);
    if (reset || opponent->hit_landed_this_tick)
        v->health_bar = osrs_health_bar_ratio(opponent->current_hitpoints,
            opponent->base_hitpoints, OSRS_PLAYER_HEALTH_BAR_SCALE);
    if (reset) { v->last_attack_tick = -1; return; }
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
        int slot = 0;
        const struct { OsrsConsumableKind kind; int doses, count; } supplies[] = {
            {OSRS_CONSUMABLE_SUMMER_PIE, 2, 3},
            {OSRS_CONSUMABLE_HALIBUT, 0, 4},
            {OSRS_CONSUMABLE_BREW, 4, 2},
            {OSRS_CONSUMABLE_MARLIN, 0, 8},
            {OSRS_CONSUMABLE_SANFEW, 4, 2},
            {OSRS_CONSUMABLE_DIVINE_COMBAT, 4, 1},
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
