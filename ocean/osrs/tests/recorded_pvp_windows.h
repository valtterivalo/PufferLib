#ifndef RECORDED_PVP_WINDOWS_H
#define RECORDED_PVP_WINDOWS_H

typedef enum { RECORDED_BEFORE_PASSES, RECORDED_AFTER_SOURCE_PASS } RecordedAttackPhase;
typedef struct {
    int tick;
    int source;
    int damage;
    AttackStyle style;
    RecordedAttackPhase phase;
} ConditionedAttack;
typedef struct { int tick; int target; int damage; } RecordedHitsplat;
typedef struct {
    const char* name;
    const char* archive;
    const char* source_sha256;
    const char* assumptions;
    int start_tick;
    int end_tick;
    int hp_min[2];
    int hp_max[2];
    int vengeance[2];
    int recoil[2];
    const ConditionedAttack* attacks;
    size_t attack_count;
    const RecordedHitsplat* hits;
    size_t hit_count;
    int dead_actor;
    int expected_candidates;
} RecordedPvpWindow;

static const ConditionedAttack spec_attacks[] = {
    {89, 1, 15, ATTACK_STYLE_MELEE, RECORDED_BEFORE_PASSES},
    {89, 0, 65, ATTACK_STYLE_MAGIC, RECORDED_AFTER_SOURCE_PASS},
    {90, 0, 0, ATTACK_STYLE_MELEE, RECORDED_AFTER_SOURCE_PASS},
};
static const RecordedHitsplat spec_hits[] = {
    {89, 0, 15}, {90, 0, 7}, {90, 0, 48},
    {89, 1, 2}, {89, 1, 11}, {89, 1, 65}, {90, 1, 0},
};
static const ConditionedAttack ags_attacks[] = {
    {399, 0, 7, ATTACK_STYLE_MELEE, RECORDED_AFTER_SOURCE_PASS},
    {399, 1, 1, ATTACK_STYLE_MELEE, RECORDED_AFTER_SOURCE_PASS},
};
static const RecordedHitsplat ags_hits[] = {
    {400, 0, 1}, {400, 0, 5}, {400, 0, 1},
    {399, 1, 7}, {400, 1, 1}, {400, 1, 1},
};
static const ConditionedAttack lethal_attacks[] = {
    {483, 0, 45, ATTACK_STYLE_MELEE, RECORDED_AFTER_SOURCE_PASS},
};
static const RecordedHitsplat lethal_hits[] = {
    {484, 0, 4}, {484, 0, 22}, {483, 1, 45},
};
#define RECORDED_COUNT(array) (sizeof(array) / sizeof((array)[0]))
static const RecordedPvpWindow recorded_pvp_windows[] = {
    {
        "voidwaker_vengeance_maul",
        "session_20260913T072938.012Z_20260913T072839Z.64834_8380fc35-d98c-4f15-a1c5-6eb8b70da9de.jsonl.zst",
        "5b7351fe0752b148ab9fcd338a83b78de9b6ba159a82833329df7d53bdc0e88e",
        "Actors MongTsl and upthewazzoo. Initial HP 121 is a reconstruction. Both recoil and Vengeance active. Direct rolls and queue phases conditioned on the observed exchange.",
        89, 90, {121, 121}, {121, 121}, {1, 1}, {1, 1},
        spec_attacks, RECORDED_COUNT(spec_attacks), spec_hits, RECORDED_COUNT(spec_hits), -1, 1,
    },
    {
        "ags_reflection_fifo",
        "session_20260913T091427.086Z_20260913T091041Z.82177_ae74d80d-a875-42ee-9749-24d06977a0f8.jsonl.zst",
        "6f0209224b6bd99053c7647c352504d93ff7acbe705c8911535295820781184c",
        "Actors 3lephante and lul-zuiger. Initial HP 121 is a reconstruction. Both recoil and Vengeance active. Direct rolls and source-pass launch phases are conditioned inputs.",
        399, 400, {121, 121}, {121, 121}, {1, 1}, {1, 1},
        ags_attacks, RECORDED_COUNT(ags_attacks), ags_hits, RECORDED_COUNT(ags_hits), -1, 1,
    },
    {
        "lethal_overkill_reflections",
        "session_20260913T091427.086Z_20260913T091041Z.82177_ae74d80d-a875-42ee-9749-24d06977a0f8.jsonl.zst",
        "6f0209224b6bd99053c7647c352504d93ff7acbe705c8911535295820781184c",
        "Actors lul-zuiger and 3lephante. Attacker HP 121 is a reconstruction. Defender HP enumerated 1 through 45. Defender recoil and Vengeance active. Direct 45 and source-pass launch phase conditioned on footage.",
        483, 484, {121, 1}, {121, 45}, {0, 1}, {0, 1},
        lethal_attacks, RECORDED_COUNT(lethal_attacks), lethal_hits, RECORDED_COUNT(lethal_hits), 1, 1,
    },
};
#undef RECORDED_COUNT
#endif
