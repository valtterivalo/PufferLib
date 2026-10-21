typedef enum {
    TFA_BORK_PEND_NAPALM_LAND = 1,
    TFA_BORK_PEND_BOMB_LAND,
    TFA_BORK_PEND_SCORCH,
    TFA_BORK_PEND_SCORCH_CLEAR,
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

typedef struct {
    int clock;
    int next_gong;
    int next_smork;
    int smork_long;
    int next_bomb;
    int stomp_tick;
    int stunned;
    int bork_dead;
    int smorks_dead;
    int target;
    int start_x, start_y;
    int pending_count;
    TfaBorkPending pending[TFA_BORK_MAX_PENDING];
} TfaBorkState;
