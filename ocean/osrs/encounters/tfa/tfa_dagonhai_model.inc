typedef enum {
    TFA_DAGONHAI_SHIELDED = 0,
    TFA_DAGONHAI_IMPS,
    TFA_DAGONHAI_FINALE,
} TfaDagonhaiPhase;

typedef enum {
    TFA_TOME_NONE = 0,
    TFA_TOME_BANDOS,
    TFA_TOME_ZAMORAK,
    TFA_TOME_SARADOMIN,
} TfaTome;

typedef struct {
    int clock;
    int phase;
    int pool, pool_max;
    int breaks;
    int ritual;
    int empowered;
    int focus;
    int next_attack;
    int caster_slot;
    int channel_end;
    int next_channel;
    int tome;
    int tome_land;
    int next_tome;
    int gust_slot;
    int imps_left;
    int alive;
} TfaDagonhaiState;
