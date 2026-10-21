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
    int breath_timer;
    int breath_target;
    int breath_land_tick;
    int target;
    int pylon_hit_tick;
    int shield_broke_tick;
    int swoosh_tick;
    int cross_tick;
} TfaGuardianState;
