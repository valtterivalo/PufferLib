#ifndef OSRS_RISKFIGHT_MODEL_H
#define OSRS_RISKFIGHT_MODEL_H

#include "../../osrs_env.h"
#include "../../osrs_player_inventory_use.h"
#include "../../osrs_encounter_visual_events.h"

typedef enum { RISKFIGHT_TRADER, RISKFIGHT_CAUTIOUS, RISKFIGHT_AGGRESSIVE } RiskfightOpponent;
typedef enum { RISKFIGHT_ONGOING, RISKFIGHT_KILL, RISKFIGHT_DEATH,
    RISKFIGHT_ESCAPE, RISKFIGHT_MUTUAL_DEATH } RiskfightOutcome;
enum {
    RF_WEAPON, RF_SHIELD, RF_RING, RF_FOOD, RF_DRINK, RF_COMBO,
    RF_ORB, RF_VENGEANCE, RF_SPECIAL, RF_PRIMARY, RF_PRAYER, RF_STYLE,
    RF_HEADS,
    RF_STOP = OSRS_PRIMARY_DIM(1),
    RF_ATTACK = OSRS_PRIMARY_MOVE_ACTIONS,
    RF_TELEPORT = RF_STOP + 1,
    RF_MASK_SIZE = 6 * (OSRS_INVENTORY_SIZE + 1) + 2 + 2 + 3 +
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
#define RF_ACTION_DIMS_INIT {29,29,29,29,29,29,2,2,3,28,5,4}
static const int RF_ACTION_DIMS[RF_HEADS] = RF_ACTION_DIMS_INIT;

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
    int escaped[2];
    float rewards[2];
} RiskfightState;

typedef struct {
    const CollisionMap* collision_map;
    const EncounterArenaTopology* route_topology;
    OsrsActorRouteCache routes[2];
    HumanInput policy_commands[2];
    RiskfightOpponent opponent;
    int self_play;
    int human_player;
} RiskfightContext;

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
    v->x = opponent->x;
    v->y = opponent->y;
    v->interacting = osrs_interaction_active(&opponent->interaction);
    v->health_bar = (opponent->current_hitpoints * 30 + opponent->base_hitpoints - 1) /
        opponent->base_hitpoints;
    if (v->health_bar > 30) v->health_bar = 30;
    if (reset) { v->last_attack_tick = -1; return; }
    int index = (s->env.tick - 1) % RF_HISTORY_TICKS;
    float* event = v->events[index];
    event[3] = opponent->hit_landed_this_tick;
    event[4] = opponent->hit_damage;
    event[5] = opponent->cast_veng_this_tick && !opponent->just_attacked;
    event[6] = opponent->ate_food_this_tick || opponent->ate_karambwan_this_tick;
    event[7] = v->health_bar;

}

static void riskfight_reset(EncounterState* state, EncounterContext* context, uint32_t seed) {
    RiskfightState* s = (RiskfightState*)state;
    RiskfightContext* ctx = (RiskfightContext*)context;
    uint32_t rng = seed ? seed : s->env.rng_state;
    memset(s, 0, sizeof(*s));
    s->env.rng_state = rng;
    s->env.winner = -1;
    s->env.pid_holder = (int)(xorshift32(&s->env.rng_state) % 2);
    static const uint8_t equipment[NUM_GEAR_SLOTS] = {
        [GEAR_SLOT_AMMO] = ITEM_NONE,
        [GEAR_SLOT_HEAD] = ITEM_DHAROKS_HELM,
        [GEAR_SLOT_CAPE] = ITEM_INFERNAL_CAPE,
        [GEAR_SLOT_NECK] = ITEM_AMULET_OF_RANCOUR,
        [GEAR_SLOT_WEAPON] = ITEM_ABYSSAL_TENTACLE,
        [GEAR_SLOT_BODY] = ITEM_DHAROKS_PLATEBODY,
        [GEAR_SLOT_SHIELD] = ITEM_AVERNIC_DEFENDER,
        [GEAR_SLOT_LEGS] = ITEM_DHAROKS_PLATELEGS,
        [GEAR_SLOT_HANDS] = ITEM_FEROCIOUS_GLOVES,
        [GEAR_SLOT_FEET] = ITEM_AVERNIC_TREADS,
        [GEAR_SLOT_RING] = ITEM_ULTOR_RING,
    };
    for (int i = 0; i < 2; i++) {
        Player* p = &s->env.players[i];
        init_player(p);
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
