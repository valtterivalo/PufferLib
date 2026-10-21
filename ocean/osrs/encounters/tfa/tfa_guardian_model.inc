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
