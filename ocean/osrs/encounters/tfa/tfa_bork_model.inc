typedef enum {
    TFA_BORK_PEND_NAPALM_LAND = 1,
    TFA_BORK_PEND_BOMB_LAND,
    TFA_BORK_PEND_SCORCH,
    TFA_BORK_PEND_RING,
    TFA_BORK_PEND_PULSE,
} TfaBorkPendKind;

typedef struct {
    int tick;
    int kind;
    int x, y;
    int arg;
} TfaBorkPending;

#define TFA_BORK_MAX_PENDING 64

typedef enum {
    TFA_BORK_SMORK_AUTO,
    TFA_BORK_SMORK_NAPALM,
    TFA_BORK_SMORK_BOMB,
} TfaBorkSmorkMove;

typedef struct {
    int clock;
    int pace;                    /* 0 until the speed step ("Smorks go faster!"), then 1 */
    int next_gong;
    int next_smork;
    TfaBorkSmorkMove smork_move; /* what the Smorks do at next_smork */
    int autos_left;              /* autos before after_autos */
    TfaBorkSmorkMove after_autos;
    int next_bomb;               /* Smorks dead: Bork stomps on this clock */
    int stomp_tick;              /* 14550 */
    int stomp_facing, stomp_foot;/* the stomp's dust sources (TFA_BORK.stomp_feet) */
    int angry_tick;              /* 14549 after a stun */
    int freeze_from, resume_tick;/* Bork stands still in [freeze_from, resume_tick - 1), gongs again at resume_tick */
    int stunned;
    int bork_dead;
    int smorks_dead;
    int tank;                    /* Bork's target ("Bork smash <name> now.") */
    int smork_target;            /* the bomb lands on this raider's tile */
    int start_x, start_y;
    int pending_count;
    TfaBorkPending pending[TFA_BORK_MAX_PENDING];
} TfaBorkState;
