typedef enum {
    TFA_GUARDIAN_FLYING = 0,
    TFA_GUARDIAN_GROUNDED,
    TFA_GUARDIAN_DYING,
} TfaGuardianPhase;

typedef struct {
    int phase;
    int phase_ticks;
    int knockdowns;
    int ranged_timer;
    int special_timer;
    int vine_timer;
    int enraged;
    int breath_tick;
    int breath_target;
    int breath_land_tick;
    int target;
    int pylon_hit_tick;
    int shield_broke_tick;
    int swoosh_tick;
    int cross_tick;
} TfaGuardianState;

typedef struct {
    int clock;
    int next_attack;
    int cycle_start;
    int slot;
    int p2_started;
    int healed;
    int blast_land_tick;
    int double_magic_tick;
    int dead;
} TfaXarthState;

typedef enum {
    TFA_DREAD_EV_NONE = 0,
    TFA_DREAD_EV_BLOOD,
    TFA_DREAD_EV_JUMP_LAND,
    TFA_DREAD_EV_JUMP_THROW,
    TFA_DREAD_EV_GLOB,
} TfaDreadEventKind;

typedef struct {
    int32_t tick;
    int8_t kind;
    int8_t player;
    int16_t x, y;
} TfaDreadEvent;

#define TFA_DREAD_MAX_EVENTS 64
#define TFA_DREAD_MAX_SPAWN 12

typedef struct {
    int phase;
    int phase_slams;
    int slam_start;
    int slams_left;
    int next_slam;
    int spawn_tick;
    int spawn_n;
    int8_t spawn_x[TFA_DREAD_MAX_SPAWN], spawn_y[TFA_DREAD_MAX_SPAWN];
    int engaged;
    int walk_timer;
    int walk_window;
    int walk_target;
    int final_phase;
    int next_jump;
    int dying;
    int death_tick;
    int dead;
    int no_heal;
    TfaDreadEvent events[TFA_DREAD_MAX_EVENTS];
} TfaDreadnothState;

typedef enum {
    TFA_EZEKIEL_ROTATION = 0,
    TFA_EZEKIEL_FINALE,
    TFA_EZEKIEL_DYING,
} TfaEzekielPhase;

typedef enum {
    TFA_EZEKIEL_DAMAGE_SIM = 0,
    TFA_EZEKIEL_DAMAGE_FORCED,
} TfaEzekielDamageSource;

typedef enum {
    TFA_EZEKIEL_SLOT_NONE = 0,
    TFA_EZEKIEL_SLOT_WEAKEN,
    TFA_EZEKIEL_SLOT_HEAL,
} TfaEzekielSlot;

#define TFA_EZEKIEL_FORCED_VOLLEYS 64

typedef struct {
    int phase;
    int clock;
    int next_attack;
    int attacks_left;
    int magic_clock;
    int moved;
    int weak_until;
    int volleys;
    int volley_swords;
    int volley_start;
    int phase2;
    int three_next;
    int sword_damage_tick;
    int sword_damage;
    int heal_anim_clock;
    int heal_land_clock;
    int finale_arm_clock;
    int finale_roll_clock;
    int finale_start_clock;
    int next_wave_clock;
    int dying_ticks;
    int damage_source;
    int8_t forced_slot[TFA_EZEKIEL_FORCED_VOLLEYS];
    int8_t forced_jump[TFA_EZEKIEL_FORCED_VOLLEYS];
} TfaEzekielState;
