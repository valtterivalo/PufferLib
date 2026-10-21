typedef enum {
    TFA_GUARDIAN_FLYING = 0,
    TFA_GUARDIAN_LANDING,
    TFA_GUARDIAN_GROUNDED,
    TFA_GUARDIAN_RISING,
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
    TFA_OCELLUS_P1 = 0,
    TFA_OCELLUS_P2,
    TFA_OCELLUS_P3,
    TFA_OCELLUS_P4,
} TfaOcellusPhase;

#define TFA_OC_MAX_EVENTS 64
#define TFA_OC_MAX_LINES 10

typedef struct {
    int32_t tick;
    int16_t kind;
    int16_t arg;
    int32_t arg2;
} TfaOcEvent;

typedef struct {
    uint8_t live;
    int8_t dx, dy;
    int16_t ox, oy;
    int32_t next_pulse;
    int32_t end;
} TfaOcLine;

typedef struct {
    int phase;
    int phase_start;
    TfaOcEvent events[TFA_OC_MAX_EVENTS];
    TfaOcLine lines[TFA_OC_MAX_LINES];
    int want_prayer;
    int next_attack_tick;
    int zero_tick;
    int corner;
    int middles;
    int window;
    int meter;
    int line_start[TFA_MAX_PLAYERS];
    int line_last[TFA_MAX_PLAYERS];
    int line_acc[TFA_MAX_PLAYERS];
    int blade_acc;
    int carry_damage;
    int portal_x[4], portal_y[4];
    int quad_index;
    int destroyed;
    int slot;
    int groups;
    int specials;
    int untethers;
    int spins;
    int pause_until;
    int debris_x[TFA_MAX_PLAYERS], debris_y[TFA_MAX_PLAYERS];
    int deflect;
    int soul_split;
    int p4_cycle;
    int p4_step;
    int dead;
} TfaOcellusState;
