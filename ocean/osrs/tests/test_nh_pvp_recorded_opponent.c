#include "../../osrs_pvp/osrs_pvp.h"
#include "osrs_test_check.h"

static Env env;
static float observations[OBS_SIZE], actions[NUM_ATNS], reward, terminal;
static unsigned char mask[NH_PVP_ACTION_MASK_SIZE];

static void reset(OpponentType bot) {
    memset(&env, 0, sizeof(env));
    memset(actions, 0, sizeof(actions));
    DictItem items[] = {
        {.key = "opponent_type", .value = (double)bot},
        {.key = "gear_tier", .value = 3},
        {.key = "is_lms", .value = 1},
        {.key = "use_c_opponent", .value = 1},
        {.key = "self_play", .value = 0},
        {.key = "auto_reset", .value = 0},
        {.key = "shaping_enabled", .value = 0},
        {.key = "shaping_scale", .value = 0},
    };
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    env.rng = 123;
    puf_init(&env, &kwargs);
    env.agents[0] = (Agent){.observations = observations, .actions = actions,
        .rewards = &reward, .terminals = &terminal, .action_mask = mask};
    puf_reset(&env);
}

int main(void) {
    CHECK("adaptive bot and interface identity remain stable", OPP_GENERALIST_NH == 23 &&
        OPP_RANGE_KITER == 30 && OPP_SELFPLAY == 31 && OPP_MIXED_NH == 33 &&
        OPP_RECORDED_ADAPTIVE_NH == 35 && OBS_SIZE == 123 && NH_PVP_ACTION_MASK_SIZE == 423);
    unsigned attacks = (1u << RECORDED_NH_ATTACK_COUNT) - 1u;
    RecordedNhWeights prior = recorded_nh_attack_weights(
        RECORDED_NH_ATTACK_DISTANCE_COUNT, ATTACK_STYLE_NONE, PRAYER_NONE, attacks);
    CHECK("global attack prior preserves all397FITcues", prior.total == 397 &&
        prior.counts[RECORDED_NH_MAGIC] == 112 && prior.counts[RECORDED_NH_RANGED] == 208 &&
        prior.counts[RECORDED_NH_MELEE] == 65 && prior.counts[RECORDED_NH_MELEE_SPECIAL] == 12 &&
        prior.support == RECORDED_NH_GLOBAL_PRIOR);
    RecordedNhWeights exact = recorded_nh_attack_weights(
        1, ATTACK_STYLE_MAGIC, PRAYER_PROTECT_MAGIC, attacks);
    CHECK("attack context retains conditional FITcounts and prayer encoding", exact.total == 9 &&
        exact.support == RECORDED_NH_EXACT && exact.counts[RECORDED_NH_MAGIC] == 4 &&
        exact.counts[RECORDED_NH_RANGED] == 2 && exact.counts[RECORDED_NH_MELEE] == 2 &&
        exact.counts[RECORDED_NH_MELEE_SPECIAL] == 1);
    RecordedNhWeights defense = recorded_nh_defense_weights(
        ATTACK_STYLE_MAGIC, ATTACK_STYLE_RANGED, PRAYER_PROTECT_MAGIC);
    CHECK("weapon history retains conditional defensive inertia", defense.total == 46 &&
        defense.support == RECORDED_NH_EXACT && defense.counts[PRAYER_PROTECT_MAGIC] == 33 &&
        defense.counts[PRAYER_PROTECT_RANGED] == 12 && defense.counts[PRAYER_PROTECT_MELEE] == 1);
    uint32_t sampled[RECORDED_NH_OUTCOME_COUNT] = {0};
    for (uint32_t draw = 0; draw < prior.total; draw++)
        sampled[recorded_nh_sample(prior, draw)]++;
    CHECK("sampling partitions all draws by exact empirical mass",
        memcmp(sampled, prior.counts, sizeof(sampled)) == 0);
    for (unsigned allowed = 1; allowed <= attacks; allowed++) {
        RecordedNhWeights legal = recorded_nh_attack_weights(
            1, ATTACK_STYLE_MAGIC, PRAYER_PROTECT_MAGIC, allowed);
        for (uint32_t draw = 0; draw < legal.total; draw++)
            CHECK("masked empirical sampling emits only legal repertoire",
                allowed & (1u << recorded_nh_sample(legal, draw)));
    }
    RecordedNhWeights projected = recorded_nh_attack_weights(
        2, ATTACK_STYLE_MELEE, PRAYER_PROTECT_RANGED, 1u << RECORDED_NH_MELEE_SPECIAL);
    CHECK("sparse legal context explicitly uses empirical marginal mass",
        projected.total && projected.support != RECORDED_NH_EXACT &&
        recorded_nh_sample(projected, 0) == RECORDED_NH_MELEE_SPECIAL);

    OsrsEnv* original = &env.state.env;
    reset(OPP_RECORDED_ADAPTIVE_NH);
        static OsrsEnv variants[2];
        int hidden[2][OSRS_BASE_NUM_ACTION_HEADS] = {{0}};
        hidden[1][OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
        hidden[1][OSRS_HEAD_SPELL] = OSRS_SPELL_ICE_BARRAGE;
        hidden[1][OSRS_HEAD_SPECIAL] = 1;
        hidden[1][OSRS_HEAD_OVERHEAD] = ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE;
        for (int idle = 0; idle < 2; idle++) {
            for (uint32_t seed = 1; seed <= 128; seed++) {
                for (int i = 0; i < 2; i++) {
                    variants[i] = *original;
                    variants[i].rng_state = seed;
                    variants[i].actions = hidden[i];
                    variants[i].players[1].has_attack_timer = 1;
                    variants[i].players[1].attack_timer = idle ? 3 : -1;
                    AttackStyle completed = (AttackStyle)get_item_attack_style(
                        variants[i].players[0].equipped[GEAR_SLOT_WEAPON]);
                    variants[i].pvp_runtime.opponent.visible_history = (OppVisibleHistory){
                        OPP_HISTORY_READY, {completed, PRAYER_PROTECT_MAGIC},
                        {completed == ATTACK_STYLE_MELEE ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE,
                            PRAYER_PROTECT_MELEE}};
                    Player* target = &variants[i].players[0];
                    target->current_hitpoints = i ? 1 : target->base_hitpoints;
                    target->current_prayer = i ? 0 : target->base_prayer;
                    target->current_magic = i ? 0 : target->base_magic;
                    target->special_energy = i ? 100 : 0;
                    target->frozen_ticks = i ? 100 : 0;
                    target->freeze_immunity_ticks = i ? 100 : 0;
                    target->attack_timer = i ? 20 : 0;
                    target->num_pending_hits = i;
                    target->pending_hits[0].damage = i ? 100 : 0;
                    for (int head = 0; head < OSRS_BASE_NUM_ACTION_HEADS; head++)
                        variants[i].pending_actions[head] = hidden[i][head];
                    generate_opponent_action(&variants[i], &variants[i].pvp_runtime.opponent);
                }
                CHECK("full dispatch decisions and RNG depend only on completed visible target context",
                    memcmp(variants[0].pending_actions + OSRS_BASE_NUM_ACTION_HEADS,
                        variants[1].pending_actions + OSRS_BASE_NUM_ACTION_HEADS,
                        OSRS_BASE_NUM_ACTION_HEADS * sizeof(int)) == 0 &&
                    variants[0].rng_state == variants[1].rng_state &&
                    memcmp(&variants[0].pvp_runtime.opponent.visible_history,
                        &variants[1].pvp_runtime.opponent.visible_history,
                        sizeof(OppVisibleHistory)) == 0);
            }
        }
        puf_close(&env);
    reset(OPP_RECORDED_ADAPTIVE_NH);
    OppVisibleHistory alternating = {OPP_HISTORY_READY,
        {ATTACK_STYLE_RANGED, PRAYER_PROTECT_MAGIC},
        {ATTACK_STYLE_MELEE, PRAYER_PROTECT_MELEE}};
    original->pvp_runtime.opponent.visible_history = alternating;
    pvp_apply_equipment_plan(&original->players[0], PVP_EQUIPMENT_RANGED);
    original->players[0].x = FIGHT_AREA_BASE_X + 10;
    original->players[0].y = FIGHT_AREA_BASE_Y + 10;
    original->players[1].x = original->players[0].x + 1;
    original->players[1].y = original->players[0].y;
    int submitted[NUM_ATNS] = {0};
    pvp_emit_equipment_plan_actions(submitted, &original->players[0], PVP_EQUIPMENT_MELEE);
    submitted[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
    for (int head = 0; head < NUM_ATNS; head++) actions[head] = submitted[head];
    puf_step(&env);
    CHECK("adaptive defense predicts canonical executed next weapon from completed ABA history",
        original->players[0].just_attacked &&
        original->players[0].attack_style_this_tick == ATTACK_STYLE_MELEE &&
        original->players[0].attack_was_on_prayer &&
        original->players[1].prayer == PRAYER_PROTECT_MELEE);
    puf_close(&env);
    reset(OPP_RECORDED_ADAPTIVE_NH);
    puf_set_bot_policy(&env, OPP_RECORDED_ADAPTIVE_NH);
    CHECK("native scripted interface accepts appended adaptive bot35",
        original->pvp_runtime.opponent.type == OPP_RECORDED_ADAPTIVE_NH);
    original->pvp_runtime.use_c_opponent_p0 = 1;
    original->pvp_runtime.opponent_p0.type = OPP_GENERALIST_NH;
    opponent_reset(original, &original->pvp_runtime.opponent_p0);
    int styles[RECORDED_NH_STYLE_COUNT] = {0};
    int overheads[RECORDED_NH_OUTCOME_COUNT] = {0};
    int observed_ticks = 0;
    do {
        puf_step(&env);
        observed_ticks++;
        Player* recorded = &original->players[1];
        overheads[recorded->prayer]++;
        if (!recorded->just_attacked) continue;
        styles[recorded->attack_style_this_tick]++;
        AttackStyle offensive = recorded->used_special_this_tick &&
            recorded->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER
                ? ATTACK_STYLE_MELEE : recorded->attack_style_this_tick;
        CHECK("actual recorded attacks use their correct offensive prayer",
            recorded->offensive_prayer == encounter_offensive_prayer_for_style(offensive));
        CHECK("actual attacks retain nonoverlap canonical geometry",
            recorded->last_attack_dist > 0);
    } while (!terminal);
    CHECK("native full fight launches magic and ranged attacks",
        styles[ATTACK_STYLE_MAGIC] > 0 && styles[ATTACK_STYLE_RANGED] > 0);
    CHECK("native full fight uses ranged and magic defense",
        overheads[PRAYER_PROTECT_MAGIC] > 0 && overheads[PRAYER_PROTECT_RANGED] > 0);
    printf("RecordedAdaptiveNH full fight ticks%d, attacks M%d R%d C%d, prayers M%d R%d C%d\n",
        observed_ticks, styles[ATTACK_STYLE_MELEE], styles[ATTACK_STYLE_RANGED],
        styles[ATTACK_STYLE_MAGIC], overheads[PRAYER_PROTECT_MELEE],
        overheads[PRAYER_PROTECT_RANGED], overheads[PRAYER_PROTECT_MAGIC]);
    puf_close(&env);
    return osrs_test_summary();
}
