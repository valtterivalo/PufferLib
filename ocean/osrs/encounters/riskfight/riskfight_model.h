#ifndef OSRS_RISKFIGHT_MODEL_H
#define OSRS_RISKFIGHT_MODEL_H

#include <limits.h>
#include "../../osrs_env.h"
#include "../../osrs_health_bar.h"
#include "../../osrs_player_inventory_use.h"
#include "../../osrs_encounter_visual_events.h"
#include "../../osrs_pvp_escape.h"
#include "../../tests/riskfight_motor.h"
#include "riskfight_threat.h"

enum { RF_PRICE_MAX_X10 = 25 };
/* Every reward leaves the env divided by this ONE constant, the largest supported death price: a kill is 1 / Pmax, a death -price / Pmax, so PufferLib's clamp to
 * [-1, 1] never bites. It is global on purpose: scaling per fight would reweight fights against each other inside a session. */
#define RF_REWARD_SCALE (10.0f / RF_PRICE_MAX_X10)
typedef enum { RISKFIGHT_HUMAN, RISKFIGHT_HUMAN_HELDOUT, RISKFIGHT_ALL_IN, RISKFIGHT_ESCAPER, RISKFIGHT_OPPONENTS } RiskfightOpponent;
enum { RISKFIGHT_CONSUME_BANDS = 5, RISKFIGHT_AXE_BANDS = 3, RISKFIGHT_TAB_SUPPLY_BANDS = 3, RISKFIGHT_TAB_SUPPLY_TICKS = 8 };
/** Per-mille rates of one scripted fighter. Consume bands split own HP at 40, 65, 73 and 90,
 * axe bands split the opponent bar (scale 30) at 12 and 24, tab rates are indexed by
 * [consume ticks this round / 8, capped][own HP below 40]. */
typedef struct {
    uint16_t consume_pm[RISKFIGHT_CONSUME_BANDS];
    uint16_t vw_pm, maul_pm;
    uint16_t axe_pm[RISKFIGHT_AXE_BANDS];
    uint16_t orb_axe_pm, veng_pm;
    uint16_t tab_pm[RISKFIGHT_TAB_SUPPLY_BANDS][2];
} RiskfightProfile;
#include "riskfight_profiles.h"
static const RiskfightProfile RISKFIGHT_ALL_IN_PROFILE = {{300, 80, 0, 0, 0}, 1000, 1000, {1000, 1000, 1000}, 1000, 1000, {{0, 0}, {0, 0}, {0, 0}}};
static const RiskfightProfile RISKFIGHT_ESCAPER_PROFILE = {{500, 300, 200, 100, 100}, 150, 3, {200, 100, 50}, 100, 1000, {{20, 100}, {50, 250}, {100, 500}}};

/** Profile drawn from an opponent pool by a uniform 32-bit roll. */
static const RiskfightProfile* riskfight_pick_profile(RiskfightOpponent pool, uint32_t roll) {
    switch (pool) {
    case RISKFIGHT_HUMAN: return &RISKFIGHT_HUMAN_PROFILES[roll % RISKFIGHT_HUMAN_TRAIN_PROFILES];
    case RISKFIGHT_HUMAN_HELDOUT:
        return &RISKFIGHT_HUMAN_PROFILES[RISKFIGHT_HUMAN_TRAIN_PROFILES + roll % RISKFIGHT_HUMAN_HELDOUT_PROFILES];
    case RISKFIGHT_ALL_IN: return &RISKFIGHT_ALL_IN_PROFILE;
    case RISKFIGHT_ESCAPER: return &RISKFIGHT_ESCAPER_PROFILE;
    case RISKFIGHT_OPPONENTS: break;
    }
    assert(0 && "unknown opponent pool");
    return NULL;
}
typedef enum { RISKFIGHT_ONGOING, RISKFIGHT_KILL, RISKFIGHT_DEATH,
    RISKFIGHT_ESCAPE, RISKFIGHT_MUTUAL_DEATH, RISKFIGHT_TIMEOUT,
    RISKFIGHT_RAGGED /* a ragger (riskfight_session.h) harassed us and teleported away: no stake, and we regear */ } RiskfightOutcome;
enum { RISKFIGHT_ROUND_TICKS = 500 };
enum {
    RF_WEAPON, RF_SHIELD, RF_RING, RF_FOOD, RF_DRINK, RF_COMBO,
    RF_ORB, RF_VENGEANCE, RF_SPECIAL, RF_PRIMARY, RF_PRAYER, RF_STYLE,
    RF_HEAD, RF_CAPE, RF_NECK, RF_BODY, RF_LEGS, RF_HANDS, RF_FEET, RF_AMMO,
    RF_OVERHEAD,
    RF_HEADS,
    RF_UNEQUIP = OSRS_INVENTORY_SIZE + 1,
    RF_STOP = OSRS_PRIMARY_DIM(1),
    RF_ATTACK = OSRS_PRIMARY_MOVE_ACTIONS,
    RF_TELEPORT = RF_STOP + 1,
    RF_MASK_SIZE = NUM_GEAR_SLOTS * (OSRS_INVENTORY_SIZE + 2) +
        3 * (OSRS_INVENTORY_SIZE + 1) + 2 + 2 + 3 +
        OSRS_PRIMARY_DIM(1) + 2 + 5 + 4 + 7,
    RF_HISTORY_TICKS = 16,
    RF_EVENT_WIDTH = 8,
    RF_SELF_TELEPORT_LOCK = 24,
    RF_SELF_SIZE = 25,
    RF_INVENTORY_WIDTH = 6,
    RF_INVENTORY_START = RF_SELF_SIZE,
    RF_EQUIPPED_START = RF_INVENTORY_START + OSRS_INVENTORY_SIZE * RF_INVENTORY_WIDTH,
    RF_OPPONENT_START = RF_EQUIPPED_START + NUM_GEAR_SLOTS,
    RF_HISTORY_START = RF_OPPONENT_START + NUM_GEAR_SLOTS + 14,
    RF_PRICE = RF_HISTORY_START + RF_HISTORY_TICKS * RF_EVENT_WIDTH,   /* death price: what our death costs us over what his kill wins us, scaled by 1 / RF_PRICE_MAX */
    /* Appended blocks, in order: threat features (riskfight_threat.h), then the session block. */
    RF_THREAT_START = RF_PRICE + 1,
    /* Session block (riskfight_write_session_observation): [0] session ticks left over RF_SESSION_HORIZON_TICKS, capped at 1;
     * [1] his special energy as a player counts it (100 minus the specs seen, plus the regeneration since), over 100; [2] 1 while the session is waiting between
     * fights (every tick of the wait is a step of no choice, the mask allows only the no-op), [3] the ticks of the wait left over RF_WAIT_SCALE_TICKS, capped at 1,
     * [4] our reputation (rude teleports of this session, decaying) over RF_REP_OBS_SCALE, capped at 1, [5] how annoyed he is with us: our overeats of this fight over
     * RF_OVEREAT_PATIENCE + 1, capped at 1 (1: he leaves), [6] our hitpoints over his chance range, (hp - range) / 121 in [-1, 1]: positive is an overeat now,
     * [7] 1 when a teleport now would be kosher, 0 when rude (riskfight_norms.h: both specials spent, or a supply class of ours out). */
    RF_SESSION_START = RF_THREAT_START + RF_THREAT_SIZE,
    RF_SESSION_SIZE = 8,
    RF_OBS_SIZE = RF_SESSION_START + RF_SESSION_SIZE,
};
/* Ticks left beyond which the end of the session no longer changes what is worth doing: several fight cycles. */
enum { RF_SESSION_HORIZON_TICKS = 1500 };
/* The regear delay is 250 ticks at its median and 640 at its 90th percentile, more with a reputation: waits beyond 1000 ticks read 1 in the wait-left feature. */
enum { RF_WAIT_SCALE_TICKS = 1000 };
/* Over-eating is rude (the user's account): once the opponent has seen more than RF_OVEREAT_PATIENCE overeats in a fight he teleports within RF_ANNOYED_LEAVE_TICKS ticks.
 * The patience sits just above the user's own play, measured with this very definition (riskfight_chance_range, his Vengeance counted up only after a seen cast that has
 * not proc'd) on his October 2026 recordings (scripts/measure_overeats.py, ocean/osrs/tests/overeat_tapes.c): p90 of overeats per fight 10. Rule: the smallest integer above
 * his p90. The earlier definition, which counted his Vengeance up from the fight's start until a proc, gave 121 fights, 1661 meals, 421 (25%) overeats, p90 9 and a
 * patience of 10. */
enum { RF_OVEREAT_PATIENCE = 11, RF_ANNOYED_LEAVE_TICKS = 4 };
/* The reputation that reads 1 in the session block (riskfight_session.h): the user's account of how a few rude teleports weigh. */
#define RF_REP_OBS_SCALE 4.0f
/* Ticks left of a session whose end is not modelled: what riskfight_reset leaves and the live server keeps (a live session has no budget). */
enum { RF_SESSION_OPEN = INT_MAX };
/* The prayer level of the user's account (his trace O lines read 90 at full); both seats have it, since in self-play either seat plays his part. */
enum { RF_PRAYER_LEVEL = 90 };
#define RF_ACTION_DIMS_INIT {30,30,30,29,29,29,2,2,3,28,5,4,30,30,30,30,30,30,30,30,7}
static const int RF_ACTION_DIMS[RF_HEADS] = RF_ACTION_DIMS_INIT;
static const int RF_GEAR_SLOT_BY_HEAD[RF_HEADS] = {
    GEAR_SLOT_WEAPON, GEAR_SLOT_SHIELD, GEAR_SLOT_RING,
    -1, -1, -1, -1, -1, -1, -1, -1, -1,
    GEAR_SLOT_HEAD, GEAR_SLOT_CAPE, GEAR_SLOT_NECK, GEAR_SLOT_BODY,
    GEAR_SLOT_LEGS, GEAR_SLOT_HANDS, GEAR_SLOT_FEET, GEAR_SLOT_AMMO, -1,
};

typedef struct {
    uint8_t equipment[NUM_GEAR_SLOTS];
    int health_bar;
    int x, y;
    int interacting;
    int last_attack_tick;
    int last_attack_speed;
    int consume_food_est, consume_potion_est, consume_karam_est, consume_delay_est;
    int pending_consume_tick, pending_bar_before, pending_damage_since;
    int last_cast_tick, last_proc_tick;
    float events[RF_HISTORY_TICKS][RF_EVENT_WIDTH];
    RiskfightThreat threat; /* his weapons as the viewer knows them: the swings seen, the slots of riskfight_threat.h */
    /* His special energy as a player counts it: starts at 100, loses the cost of every special swing seen, regenerates 10 every 50 ticks once below 100. */
    int spec_est, spec_regen_phase;
    /* The live server's memory of his last swing (weapon item index, fight tick): a special count can arrive a tick after its swing and is charged to that weapon. */
    int last_swing_item, last_swing_tick;
} RiskfightVisibleOpponent;

typedef struct {
    OsrsEnv env;
    int pending_pid_active;
    int pending_pid_count[2][2];
    PendingHit pending_pid[2][2][MAX_PENDING_HITS];
    PendingHitMetadata pending_pid_metadata[2][2][MAX_PENDING_HITS];
    OsrsInventoryUseState inventory_use[2];
    OsrsPoisonState poison[2];
    OsrsPoisonSource poison_source[2];
    RiskfightVisibleOpponent visible[2];
    RiskfightOutcome outcome[2];
    const RiskfightProfile* opponent_profile;
    uint32_t script_seed[2];
    int consume_ticks[2];
    uint8_t anim_carry[2]; /* the player showed an animation (swing, meal, cast or block) on the previous tick: a meal now may hide behind it */
    uint8_t anim_now[2];   /* the same for the tick just stepped; anim_carry takes it at the start of the next step */
    int escaped[2];
    OsrsEscapeSupplies escape_supplies[2][2];
    int escape_tick[2];
    OsrsUnequipResult last_unequip_result[2];
    float rewards[2];
    float episode_returns[2];
    float damage_rewards[2];
    float direct_ko_chance_mass[2];
    float chance_rewards[2];
    int drink_brew[2];
    int drink_sanfew[2];
    int drink_combat[2];
    int eat_marlin[2];
    int eat_halibut[2];
    int eat_pie[2];
    int spec_voidwaker[2];
    int spec_maul[2];
    int combat_opp[2];
    int maul_opp[2];
    int ticks_tentacle[2];
    int ticks_axe[2];
    int ticks_voidwaker[2];
    int ticks_maul[2];
    /* Scripted opponent reaction lag in ticks (domain randomization): his actions are those of opp_lag ticks ago. */
    int opp_lag;
    int opp_ring[4][RF_HEADS];
    /* Stakes: the death price us/him (Stakes.ratio is him/us) of this fight; the reward of a death is minus this, of a kill 1. */
    float death_price;
    /* Specials each side used this fight (any weapon), and the session ticks left (RF_SESSION_OPEN outside a session). */
    int specs_used[2];
    int session_ticks_left;
    int session_wait_left; /* ticks of the wait between two fights still to pass (0 in a fight); the state is the one the last fight ended in */
    float session_rep;     /* our reputation as the session holds it (riskfight_session.h); 0 where the session is not modelled (the live server) */
    /* A ragger (riskfight_session.h): the pool's ranged kit in seat 1 that swings ragger_limit times and teleports away. */
    int ragger, ragger_limit, ragger_swings;
    /* Over-eating (riskfight_session.h): meals of each side eaten while its hitpoints at the start of the tick were above his chance range, this fight; the hitpoints
     * each side had when its inputs of the tick began; and the opponent's reaction: annoyed (he has had enough) and the tick he teleports. */
    int overeats[2], tick_hitpoints[2];
    int annoyed, annoyed_leave_tick;
} RiskfightState;
typedef struct {
    uint8_t equipment[NUM_GEAR_SLOTS];
    uint16_t inventory[OSRS_INVENTORY_SIZE];
    int hitpoints;
    int vengeance_sacks;
} RiskfightLoadout;

/* Our hand when motor modelling is on: clicks of one decision are pressed in a chain whose gaps are the measured motor clock; a click is processed on the first
 * tick after the decision when it is pressed before the 465 ms cutoff, later otherwise. While a chain is unfinished new clicks are dropped, as the plugin does
 * not ask while a run is committed. */
typedef struct { HumanCommand cmd; int land; } RiskfightMotorClick;
typedef struct { int enabled, pending_count; uint32_t rng; RiskfightMotorClick pending[24]; } RiskfightMotor;

typedef struct {
    const EncounterArenaTopology* route_topology;
    const RiskfightLoadout* loadout[2];
    OsrsActorRouteCache routes[2];
    HumanInput policy_commands[2];
    RiskfightOpponent opponent;
    int kit_mix; /* training resets draw our kit between RISKFIGHT_LIVE_LOADOUT and RISKFIGHT_VW_LOADOUT (live_loadout=3) */
    const RiskfightLoadout* opp_default; /* his kit when he takes no pool kit, as live_loadout set it */
    int opponent_pool; /* a reset draws his kit from the opponent pool (riskfight_kits.h) unless opp_plain; the script, OppFit or a net may play him */
    int opp_ragger;    /* the next new opponent is a ragger: a ranged pool kit (needs opponent_pool) */
    int opp_plain;     /* the next new opponent keeps loadout[1] as the caller set it instead of drawing a pool kit (a net-played seat 1 draws one of our kits) */
    const RiskfightKit* opp_kit; /* the kit of this fight, NULL outside the pool */
    uint32_t opp_roll;           /* the draw of his unseen ring, kept with the kit across the rounds of a session */
    int keep_opp_kit;            /* the next reset is another round against the same opponent: his kit and ring stay */
    int self_play;               /* seat 1 is a second agent; with opp_driven it is the script or OppFit, not that agent */
    int opp_driven;              /* self-play only: the script or the OppFit hook plays seat 1 this fight, the actions of the second agent are ignored */
    int opp_omit;                /* self-play only: this tick the driven opponent does nothing (scripted_omission, anneals to 0) */
    int human_player;
    float damage_reward_coeff;
    float chance_reward_coeff;
    RiskfightMotor motor;
    /* His actions from a fitted policy instead of the script: opp_hook fills them for the tick about to be processed, opp_after sees the tick that ran. */
    void* opp_user;
    void (*opp_hook)(void* user, const void* state, int* actions);
    void (*opp_after)(void* user, void* state);
} RiskfightContext;

/** Stake-weighted outcome: a kill wins his gear (1), a death loses ours (price), both at once nets 1 - price; a timeout, an escape and a fight still going are draws (0). */
static inline float riskfight_outcome_reward(RiskfightOutcome outcome, float price) {
    return outcome == RISKFIGHT_KILL ? 1 : outcome == RISKFIGHT_DEATH ? -price :
        outcome == RISKFIGHT_MUTUAL_DEATH ? 1 - price : 0;
}

static void riskfight_write_observation(const RiskfightState*, int, float*);
static void riskfight_write_script_observation(const RiskfightState*, int, float*);
static void riskfight_script(const float*, const RiskfightProfile*, uint32_t, int, int*);
static void riskfight_write_action_mask(const RiskfightState*, int, unsigned char*);

static uint16_t riskfight_supply_code(OsrsConsumableKind kind, int doses) {
    return osrs_inventory_content_code_from_consumable(kind, (uint8_t)doses);
}

/* Restricts the action mask to what the plugin can click: no prayers, style, orb, armour swaps other than weapon/shield/ring, no movement; the teleport tab stays. Set once at start-up. */
static int riskfight_deployable;
static RiskfightLoadout RISKFIGHT_DEFAULT_LOADOUT;
/* The bag and gear of the recorded live fights (tape 20261005T193654 KeengCr1mson): Voidwaker, ornate granite maul, anglerfish, karambwan, brews,
 * restores, a super combat, a Vengeance sack and a teleport; both sides carry it. Built at load from names, not item codes. */
static RiskfightLoadout RISKFIGHT_LIVE_LOADOUT;
/* The kit of the live fight 20261016T141036 (our side): the same gear but the dragon defender, and no two-handed weapon in the bag: 14 blighted
 * anglerfish (sim anglerfish), 7 karambwan, 2 super restores, 2 brews, a super combat, the Vengeance sack and the house tablet, all 28 slots full
 * until the first meal. His side keeps RISKFIGHT_LIVE_LOADOUT (the opponent is observed to carry the maul). */
static RiskfightLoadout RISKFIGHT_VW_LOADOUT;
/* The VW kit with RF_VW_PIES summer pies in place of as many anglerfish (the training kit draw includes it, so the policy learns pies when it carries them): a pie is
 * 2 bites of 11, a 1-tick eat delay (wiki: Summer pie, a 1-tick fast food), so from 73 a pie to 84 still leaves the next tick for a triple-eat. */
static RiskfightLoadout RISKFIGHT_VW_PIES_LOADOUT;
enum { RF_VW_PIES = 4 };

__attribute__((constructor)) static void riskfight_build_default_loadout(void) {
    RiskfightLoadout loadout = {.hitpoints = 121, .vengeance_sacks = 100};
    loadout.equipment[GEAR_SLOT_AMMO] = ITEM_NONE;
    loadout.equipment[GEAR_SLOT_HEAD] = ITEM_DHAROKS_HELM;
    loadout.equipment[GEAR_SLOT_CAPE] = ITEM_INFERNAL_CAPE;
    loadout.equipment[GEAR_SLOT_NECK] = ITEM_AMULET_OF_RANCOUR;
    loadout.equipment[GEAR_SLOT_WEAPON] = ITEM_ABYSSAL_TENTACLE;
    loadout.equipment[GEAR_SLOT_BODY] = ITEM_DHAROKS_PLATEBODY;
    loadout.equipment[GEAR_SLOT_SHIELD] = ITEM_AVERNIC_DEFENDER;
    loadout.equipment[GEAR_SLOT_LEGS] = ITEM_DHAROKS_PLATELEGS;
    loadout.equipment[GEAR_SLOT_HANDS] = ITEM_FEROCIOUS_GLOVES;
    loadout.equipment[GEAR_SLOT_FEET] = ITEM_AVERNIC_TREADS;
    loadout.equipment[GEAR_SLOT_RING] = ITEM_ULTOR_RING;
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
    int slot = 0;
    for (size_t k = 0; k < sizeof(supplies) / sizeof(*supplies); k++)
        for (int n = 0; n < supplies[k].count; n++)
            loadout.inventory[slot++] = riskfight_supply_code(supplies[k].kind, supplies[k].doses);
    const uint8_t switches[] = {ITEM_DHAROKS_GREATAXE, ITEM_VOIDWAKER,
        ITEM_GRANITE_MAUL_ORNATE, ITEM_RING_OF_RECOIL};
    for (size_t k = 0; k < sizeof(switches); k++)
        loadout.inventory[slot++] = osrs_inventory_content_code_from_item(switches[k]);
    loadout.inventory[slot++] = riskfight_supply_code(OSRS_CONSUMABLE_LOCATOR_ORB, 0);
    assert(slot == 27);
    RISKFIGHT_DEFAULT_LOADOUT = loadout;
}

static void riskfight_build_vw_loadout(void);
__attribute__((constructor)) static void riskfight_build_live_loadout(void) {
    RiskfightLoadout loadout = {.hitpoints = 121, .vengeance_sacks = 27};
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) loadout.equipment[i] = ITEM_NONE;
    loadout.equipment[GEAR_SLOT_HEAD] = ITEM_HELM_NEITIZNOT;
    loadout.equipment[GEAR_SLOT_CAPE] = ITEM_INFERNAL_CAPE;
    loadout.equipment[GEAR_SLOT_NECK] = ITEM_AMULET_OF_STRENGTH;
    loadout.equipment[GEAR_SLOT_WEAPON] = ITEM_VOIDWAKER;
    loadout.equipment[GEAR_SLOT_SHIELD] = ITEM_AVERNIC_DEFENDER;
    loadout.equipment[GEAR_SLOT_BODY] = ITEM_FIGHTER_TORSO;
    loadout.equipment[GEAR_SLOT_LEGS] = ITEM_DRAGON_PLATELEGS;
    loadout.equipment[GEAR_SLOT_HANDS] = ITEM_BARROWS_GLOVES;
    loadout.equipment[GEAR_SLOT_FEET] = ITEM_DRAGON_BOOTS;
    loadout.equipment[GEAR_SLOT_RING] = ITEM_RING_OF_RECOIL;
    const struct { OsrsConsumableKind kind; int doses, count; } supplies[] = {
        {OSRS_CONSUMABLE_ANGLERFISH, 0, 12}, {OSRS_CONSUMABLE_KARAMBWAN, 0, 7}, {OSRS_CONSUMABLE_BREW, 4, 2},
        {OSRS_CONSUMABLE_SUPER_RESTORE, 4, 2}, {OSRS_CONSUMABLE_SUPER_COMBAT, 4, 1},
        {OSRS_CONSUMABLE_TELEPORT, 0, 1}, {OSRS_CONSUMABLE_VENGEANCE_SACK, 0, 1},
    };
    int slot = 0;
    for (size_t k = 0; k < sizeof(supplies) / sizeof(*supplies); k++)
        for (int n = 0; n < supplies[k].count; n++)
            loadout.inventory[slot++] = riskfight_supply_code(supplies[k].kind, supplies[k].doses);
    loadout.inventory[slot++] = osrs_inventory_content_code_from_item(ITEM_GRANITE_MAUL_ORNATE);
    assert(slot == 27);
    RISKFIGHT_LIVE_LOADOUT = loadout;
    riskfight_build_vw_loadout();
}

static void riskfight_build_vw_loadout(void) { /* called by riskfight_build_live_loadout: it copies the live kit, so the order is fixed */
    RiskfightLoadout loadout = RISKFIGHT_LIVE_LOADOUT;
    loadout.equipment[GEAR_SLOT_SHIELD] = ITEM_DRAGON_DEFENDER;
    loadout.equipment[GEAR_SLOT_LEGS] = ITEM_NONE; /* worn slot 7 of the live request is -1 */
    const struct { OsrsConsumableKind kind; int doses, count; } supplies[] = {
        {OSRS_CONSUMABLE_ANGLERFISH, 0, 14}, {OSRS_CONSUMABLE_KARAMBWAN, 0, 7}, {OSRS_CONSUMABLE_SUPER_RESTORE, 4, 2},
        {OSRS_CONSUMABLE_BREW, 4, 2}, {OSRS_CONSUMABLE_SUPER_COMBAT, 4, 1},
        {OSRS_CONSUMABLE_VENGEANCE_SACK, 0, 1}, {OSRS_CONSUMABLE_TELEPORT, 0, 1},
    };
    int slot = 0;
    for (size_t k = 0; k < sizeof(supplies) / sizeof(*supplies); k++)
        for (int n = 0; n < supplies[k].count; n++)
            loadout.inventory[slot++] = riskfight_supply_code(supplies[k].kind, supplies[k].doses);
    assert(slot == 28);
    RISKFIGHT_VW_LOADOUT = loadout;
    RiskfightLoadout with_pies = loadout;
    int swapped = 0;
    for (int i = 0; i < 28 && swapped < RF_VW_PIES; i++)
        if (osrs_item_content_metadata(loadout.inventory[i])->consumable_kind == OSRS_CONSUMABLE_ANGLERFISH) { with_pies.inventory[i] = riskfight_supply_code(OSRS_CONSUMABLE_SUMMER_PIE, 2); swapped++; }
    assert(swapped == RF_VW_PIES);
    RISKFIGHT_VW_PIES_LOADOUT = with_pies;
}

#include "riskfight_kits.h"

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
    ctx->route_topology = pvp_route_topology_finalize();
}

/** Observed heal range -> max (food, potion, karambwan, attack delay) lock ticks over bag-legal consume combos. */
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

/** One tick of the regeneration of his special energy as a player counts it (the sim's own clock: +OSRS_SPEC_REGEN_AMOUNT every OSRS_SPEC_REGEN_INTERVAL ticks below 100). */
static inline void riskfight_spec_est_regen(RiskfightVisibleOpponent* v) {
    if (v->spec_est >= 100) { v->spec_regen_phase = 0; return; }
    if (++v->spec_regen_phase < OSRS_SPEC_REGEN_INTERVAL) return;
    v->spec_regen_phase = 0;
    v->spec_est = v->spec_est + OSRS_SPEC_REGEN_AMOUNT > 100 ? 100 : v->spec_est + OSRS_SPEC_REGEN_AMOUNT;
}

/** A special swing of his was seen: it costs `cost` of the estimate, and the regeneration clock starts when the estimate leaves 100. */
static inline void riskfight_spec_est_seen(RiskfightVisibleOpponent* v, int cost) {
    if (v->spec_est >= 100) v->spec_regen_phase = 0;
    v->spec_est = v->spec_est > cost ? v->spec_est - cost : 0;
}

static void riskfight_observe_visible(RiskfightState* s, int viewer, int reset) {
    const Player* opponent = &s->env.players[1 - viewer];
    RiskfightVisibleOpponent* v = &s->visible[viewer];
    memcpy(v->equipment, opponent->equipped, sizeof(v->equipment));
    v->equipment[GEAR_SLOT_RING] = ITEM_NONE;
    v->equipment[GEAR_SLOT_AMMO] = ITEM_NONE;
    riskfight_threat_refresh(&v->threat, v->equipment, s->env.tick);
    v->x = opponent->x;
    v->y = opponent->y;
    v->interacting = osrs_interaction_active(&opponent->interaction);
    int bar_before = v->health_bar;
    if (reset || opponent->hit_landed_this_tick)
        v->health_bar = osrs_health_bar_ratio(opponent->current_hitpoints,
            opponent->base_hitpoints, OSRS_PLAYER_HEALTH_BAR_SCALE);
    if (!reset) {
        if (opponent->cast_veng_this_tick) v->last_cast_tick = s->env.tick;
        if (opponent->said_taste_vengeance_this_tick) v->last_proc_tick = s->env.tick;
    }
    if (reset) {
        v->last_cast_tick = v->last_proc_tick = -1000;
        v->spec_est = 100;
        v->spec_regen_phase = 0;
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
    event[5] = opponent->cast_veng_this_tick;
    event[6] = consuming;
    event[7] = v->health_bar;
    int bar_event = opponent->hit_landed_this_tick;
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
    ctx->motor.pending_count = 0;
    ctx->motor.rng = osrs_lowbias32(rng ^ 0x6d6f746fu) | 1;
    if (!ctx->opponent_pool) ctx->opp_kit = NULL;
    else {
        if (!ctx->keep_opp_kit) {
            float roll = (float)(osrs_lowbias32(rng ^ 0x6b697473u) >> 8) / 16777216.0f;
            ctx->opp_kit = ctx->opp_plain ? NULL : ctx->opp_ragger ? riskfight_pool_pick_ranged(roll) : riskfight_pool_pick(roll);
            ctx->opp_roll = osrs_lowbias32(rng ^ 0x72696e67u);
        }
        if (ctx->opp_kit) ctx->loadout[1] = &RISKFIGHT_POOL_LOADOUT[ctx->opp_kit - RISKFIGHT_POOL];
    }
    memset(s, 0, sizeof(*s));
    s->env.rng_state = rng;
    s->death_price = 1;
    s->session_ticks_left = RF_SESSION_OPEN;
    s->env.winner = -1;
    s->env.pvp_runtime.teleport_world = OSRS_TELEPORT_WORLD_PVP;
    pvp_reset_priority(&s->env, OSRS_PRIORITY_PVP_WORLD);
    for (int i = 0; i < 2; i++) {
        const RiskfightLoadout* loadout = ctx->loadout[i] ? ctx->loadout[i] : &RISKFIGHT_DEFAULT_LOADOUT;
        Player* p = &s->env.players[i];
        init_player(p);
        s->env.pvp_runtime.maul[i] = osrs_granite_maul_init();
        encounter_init_maxed_player_combat_stats(p, RF_PRAYER_LEVEL);
        p->has_blood_fury = 0;
        p->is_lunar_spellbook = 1;
        p->veng_active = 1;
        p->current_hitpoints = loadout->hitpoints;
        encounter_super_combat_boost(p);
        p->x = FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH / 2 + i;
        p->y = FIGHT_AREA_BASE_Y + FIGHT_AREA_HEIGHT / 2;
        p->dest_x = p->x; p->dest_y = p->y;
        memcpy(p->equipped, loadout->equipment, sizeof(loadout->equipment));
        osrs_refresh_player_equipment(p);
        pvp_refresh_visible_gear(p);
        s->inventory_use[i].divine_combat_ticks = OSRS_DIVINE_DURATION;
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
            p->inventory_cells[slot] = osrs_inventory_cell_from_content_code(loadout->inventory[slot]);
        s->inventory_use[i].orb_used_tick = -1;
        s->inventory_use[i].vengeance_consumed_tick = -1;
        s->inventory_use[i].vengeance_sacks = loadout->vengeance_sacks;
        s->env.pvp_runtime.walk_dest_x[i] = -1;
        s->env.pvp_runtime.walk_dest_y[i] = -1;
        osrs_actor_route_cache_clear(&ctx->routes[i]);
        s->script_seed[i] = osrs_lowbias32(s->env.rng_state + (uint32_t)(i + 1) * 0x9e3779b9U);
    }
    if (ctx->opp_kit) riskfight_kit_apply(s, ctx->opp_kit, ctx->opp_roll);
    s->opponent_profile = riskfight_pick_profile(ctx->opponent, osrs_lowbias32(s->env.rng_state + 3 * 0x9e3779b9U));
    riskfight_observe_visible(s, 0, 1);
    riskfight_observe_visible(s, 1, 1);
}

#endif
