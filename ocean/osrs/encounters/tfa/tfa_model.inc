#define TFA_MAX_PLAYERS 4
#define TFA_MAX_NPCS 64
#define TFA_NUM_TARGET_SLOTS 16
#define TFA_MAX_HAZARDS 512
#define TFA_ROOM_SIZE 64
#define TFA_DANGER_HORIZON 6
#define TFA_ARENA_MAX ENCOUNTER_ARENA_TOPOLOGY_MAX_DIMENSION
/* Prayer level of the recorded raider (real level 90 on 18980 of 18980 menu actions). */
#define TFA_PLAYER_PRAYER 90
#define TFA_ROOM_MAX_TICKS 3000

typedef enum {
    TFA_ROOM_GUARDIAN = 0,
    TFA_ROOM_BORK,
    TFA_ROOM_EZEKIEL,
    TFA_ROOM_DREADNOTH,
    TFA_ROOM_MOIA,
    TFA_ROOM_XARTH,
    TFA_ROOM_DAGONHAI,
    TFA_ROOM_OCELLUS,
    TFA_ROOM_COUNT
} TfaRoom;

/* Raid order: the Guardian opens, one boss of each pair, Ocellus closes. */
#define TFA_RAID_LENGTH 5

typedef enum {
    TFA_EPISODE_ROOM = 0,
    TFA_EPISODE_RAID = 1,
} TfaEpisodeMode;

typedef enum {
    TFA_LIFE_ABSENT = 0,
    TFA_LIFE_ALIVE,
    TFA_LIFE_DEAD,
} TfaLife;

typedef enum {
    TFA_OUTCOME_NONE = 0,
    TFA_OUTCOME_CLEARED,
    TFA_OUTCOME_WIPED,
    TFA_OUTCOME_TIMEOUT,
} TfaOutcome;

typedef enum {
    TFA_NPC_NONE = 0,
    TFA_NPC_GUARDIAN,
    TFA_NPC_GUARDIAN_PYLON,
    TFA_NPC_GUARDIAN_VINE,
    TFA_NPC_BORK,
    TFA_NPC_SMORKS,
    TFA_NPC_EZEKIEL,
    TFA_NPC_EZEKIEL_SWORD,
    TFA_NPC_DREADNOTH,
    TFA_NPC_JELLY,
    TFA_NPC_MOIA,
    TFA_NPC_BINDER,
    TFA_NPC_XARTH,
    TFA_NPC_XARTH_PILLAR,
    TFA_NPC_XARTH_CORPSE,
    TFA_NPC_DAGONHAI_ELDER,
    TFA_NPC_DAGONHAI_MONK,
    TFA_NPC_OCELLUS,
    TFA_NPC_VOID_RIFT,
    TFA_NPC_VOID_SIPHON,
    TFA_NPC_VOID_AUGMENTER,
    TFA_NPC_VOID_VINE,
    TFA_NPC_OCELLUS_BLADE,
    TFA_NPC_OCELLUS_PORTAL,
    TFA_NPC_DAGONHAI_CAULDRON,
    TFA_NPC_DAGONHAI_GUST,
    TFA_NPC_IMP,
    TFA_NPC_DEMONIC_RIFT,
    TFA_NPC_KIND_COUNT
} TfaNpcKind;

/* What an NPC is doing this tick, for observation and render. */
typedef enum {
    TFA_ACT_IDLE = 0,
    TFA_ACT_MELEE,
    TFA_ACT_RANGED,
    TFA_ACT_MAGIC,
    TFA_ACT_SPECIAL,
    TFA_ACT_STUNNED,
    TFA_ACT_COUNT
} TfaNpcAct;

typedef struct {
    uint8_t active;
    uint8_t kind;
    uint8_t size;
    uint8_t attackable;
    uint8_t act;
    int8_t target;
    int16_t monster;
    int32_t npc_id;
    int16_t x, y;
    int32_t hp, max_hp;
    int32_t shield, max_shield;
    int32_t anim_id;
    int32_t anim_tick;
    int16_t attack_timer;
    int16_t state;
    int16_t state_ticks;
    int16_t counter;
    uint32_t instance_id;
    int32_t damage_taken_tick;
    int16_t def_drain;
    int16_t frozen_ticks;
    int8_t overhead;
    int8_t hit_count_tick;
    int16_t hits_tick[OSRS_RENDER_HITS_MAX];
    int8_t hits_attacker[OSRS_RENDER_HITS_MAX];
    int8_t hits_style[OSRS_RENDER_HITS_MAX];
    EncounterPendingHitQueue hits;
} TfaNpc;

typedef enum {
    TFA_HAZ_NONE = 0,
    /* Lands once on its rect at start_tick. */
    TFA_HAZ_STRIKE,
    /* Damages every player standing on it each tick in [start_tick, end_tick). */
    TFA_HAZ_FLOOR,
    /* Guardian mushroom: arms, then bursts when a player comes within reach. */
    TFA_HAZ_MUSHROOM,
    /* A loc that damages nothing, removed by its room. */
    TFA_HAZ_LOC,
} TfaHazardKind;

typedef struct {
    uint8_t kind;
    uint8_t style;
    uint8_t w, h;
    int8_t owner;
    uint8_t effect;
    int16_t x, y;
    int32_t start_tick;
    int32_t end_tick;
    int16_t arm_ticks;
    int16_t min_damage, max_damage;
    int16_t protected_pct;
    int32_t gfx_id;
    int32_t loc_id;
    int16_t orientation;
} TfaHazard;

typedef enum {
    TFA_EFFECT_NONE = 0,
    TFA_EFFECT_POISON = 1,
    TFA_EFFECT_BURN = 2,
    TFA_EFFECT_TENDRIL = 3,
} TfaHazardEffect;

/* An NPC hit on its way to a player. Throw-checked hits carry their final damage;
   land-checked hits apply the protection prayer active on the landing tick. */
typedef struct {
    int8_t ticks_remaining;
    int8_t attack_style;
    int8_t check_at_land;
    int8_t protected_pct;
    int8_t source_slot;
    int16_t damage;
} TfaIncomingHit;

typedef struct {
    TfaIncomingHit hits[ENCOUNTER_MAX_PENDING_HITS];
    int count;
} TfaIncomingQueue;

typedef struct {
    Player player;
    uint8_t life;
    uint8_t stats_dirty;
    int16_t stat_clock;          /* ticks into the 100-tick stat cycle */
    int16_t death_tick;
    int dest_x, dest_y;
    int dest_explicit;
    TfaIncomingQueue incoming;
    EncounterLoadoutStats stats;
    OsrsInventoryUseState use;
    OsrsActorRouteCache route_cache;
    OsrsPoisonState poison;
    int16_t burn_ticks;
    int16_t weary_ticks;
    int16_t rooted_ticks;
    int16_t stunned_ticks;
    int16_t injured_ticks;
    uint8_t kit;
    uint8_t queued_spell;
    float reward;
    float damage_dealt_tick;
    float damage_taken_tick;
    float damage_dealt_total;
    float damage_taken_total;
    int attacked_this_tick;
    int attack_target_slot;
    int attack_damage;
    int attack_style;
    int attack_is_special;
    EncounterProjectileTiming attack_timing;
} TfaPlayer;

/* Render-only effects of this tick: projectiles from an NPC to a player (delay and duration in ticks),
   one-shot spotanims on a tile (delay in client cycles, the recorder's startCycle minus the creation cycle). */
typedef enum {
    TFA_FX_PROJECTILE = 1,
    TFA_FX_GROUND,
} TfaFxKind;

typedef struct {
    uint8_t kind;
    int8_t src_slot;
    int8_t dst_player;
    int16_t delay;
    int16_t duration;
    int16_t x, y;
    int32_t gfx_id;
    uint32_t model;
    int32_t anim;
} TfaFx;

#define TFA_MAX_FX 512

typedef struct {
    float room_clear;
    float raid_clear;
    float damage_dealt;
    float damage_taken;
    float death;
    float wipe;
} TfaRewardConfig;

typedef struct {
    int team_size;
    int episode_mode;
    int start_room;
    int path;
    int room_max_ticks;
    TfaRewardConfig reward;
} TfaConfig;

#include "tfa_guardian_model.inc"
#include "tfa_bork_model.inc"
#include "tfa_moia_model.inc"
#include "tfa_dagonhai_model.inc"

typedef struct {
    int8_t x0, y0, x1, y1;
    int8_t owner;
} TfaDangerRect;

typedef struct {
    TfaPlayer players[TFA_MAX_PLAYERS];
    TfaNpc npcs[TFA_MAX_NPCS];
    TfaHazard hazards[TFA_MAX_HAZARDS];
    int hazard_hi;
    int8_t danger[TFA_MAX_PLAYERS + 1][TFA_ROOM_SIZE * TFA_ROOM_SIZE];
    TfaDangerRect danger_rects[TFA_MAX_HAZARDS];
    int danger_rect_count;
    int team_size;
    int tick;
    int room_tick;
    int room;
    int raid_index;
    int path;
    int episode_mode;
    int outcome;
    int episode_over;
    int rooms_cleared;
    uint32_t rng_state;
    uint32_t next_instance_id;
    float team_damage_dealt_tick;
    float episode_return;
    int room_cleared_this_tick;
    TfaFx fx[TFA_MAX_FX];
    int fx_count;
    union {
        TfaGuardianState guardian;
        TfaXarthState xarth;
        TfaOcellusState ocellus;
        TfaBorkState bork;
        TfaMoiaState moia;
        TfaDreadnothState dreadnoth;
        TfaEzekielState ezekiel;
        TfaDagonhaiState dagonhai;
    } boss;
} TfaState;

typedef struct {
    float episode_return;
    float episode_length;
    float rooms_cleared;
    float raid_clears;
    float wipes;
    float timeouts;
    float deaths;
    float damage_dealt;
    float damage_taken;
    float room_ticks[TFA_ROOM_COUNT];
    float room_clears[TFA_ROOM_COUNT];
    float room_attempts[TFA_ROOM_COUNT];
    float n;
} TfaLog;

typedef struct {
    TfaConfig config;
    const EncounterArenaTopology* topology[TFA_ROOM_COUNT];
    int teammate_actions[TFA_MAX_PLAYERS][OSRS_BASE_NUM_ACTION_HEADS];
    TfaLog log;
    int world_offset_x;
    int world_offset_y;
} TfaContext;

/* Raider kits from the recordings. The recorder's player (P0) wore and carried the same set at the start of 58 of 58 fights (item containers 94
   and 93): a Zaryte crossbow with ruby dragon bolts (e) (its 110 caps), Soulreaper axe and Dragon warhammer, an Ice ancient sceptre with
   ancestral robes, a blowpipe. The other raiders are only seen through their projectiles: dragon arrows 1120 (5-tick twisted bow cadence, 3940
   throws), Tonalztics of ralos specials 2727 opening the fights (47), ice barrage 368 on adds (872), blowpipe darts 1122. They get the twisted bow
   and the Tonalztics in place of the crossbow, bolts, buckler and warhammer. */
typedef enum { TFA_KIT_CROSSBOW = 0, TFA_KIT_BOW } TfaKit;

static const uint16_t TFA_KIT_WORN[2][NUM_GEAR_SLOTS] = {
    [TFA_KIT_CROSSBOW] = {
        ITEM_NEITIZNOT_FACEGUARD, ITEM_DIZANAS_QUIVER, ITEM_NECKLACE_OF_RUPTURE, ITEM_RUBY_DRAGON_BOLTS_E,
        ITEM_ZARYTE_CROSSBOW, ITEM_TWISTED_BUCKLER, ITEM_MASORI_BODY_F, ITEM_MASORI_CHAPS_F,
        ITEM_BARROWS_GLOVES, ITEM_AVERNIC_TREADS, ITEM_ULTOR_RING,
    },
    [TFA_KIT_BOW] = {
        ITEM_NEITIZNOT_FACEGUARD, ITEM_DIZANAS_QUIVER, ITEM_NECKLACE_OF_RUPTURE, ITEM_DRAGON_ARROWS,
        ITEM_TWISTED_BOW, ITEM_NONE, ITEM_MASORI_BODY_F, ITEM_MASORI_CHAPS_F,
        ITEM_BARROWS_GLOVES, ITEM_AVERNIC_TREADS, ITEM_ULTOR_RING,
    },
};

static const uint16_t TFA_KIT_SWITCHES[2][6] = {
    [TFA_KIT_CROSSBOW] = { ITEM_DRAGON_WARHAMMER, ITEM_TOXIC_BLOWPIPE, ITEM_TONALZTICS_OF_RALOS, ITEM_NONE, ITEM_NONE, ITEM_NONE },
    [TFA_KIT_BOW] = { ITEM_TONALZTICS_OF_RALOS, ITEM_TOXIC_BLOWPIPE, ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE },
};

static const uint16_t TFA_KIT_MELEE[] = { ITEM_SOULREAPER_AXE, ITEM_INFERNAL_CAPE, ITEM_AMULET_OF_RANCOUR, ITEM_AVERNIC_DEFENDER };
static const uint16_t TFA_KIT_MAGIC[] = { ITEM_ICE_ANCIENT_SCEPTRE_L, ITEM_ANCESTRAL_TOP, ITEM_ANCESTRAL_BOTTOM };
