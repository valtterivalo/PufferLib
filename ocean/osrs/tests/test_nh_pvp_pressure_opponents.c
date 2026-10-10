#include "../../osrs_pvp/osrs_pvp.h"
#include "osrs_test_check.h"
#include "osrs_test_nh_pvp.h"

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
        OSRS_TEST_NH_PVP_REWARD_ITEMS
    };
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    env.rng = 123;
    puf_init(&env, &kwargs);
    env.agents[0] = (Agent){.observations = observations, .actions = actions,
        .rewards = &reward, .terminals = &terminal, .action_mask = mask};
    puf_reset(&env);
    OsrsEnv* pvp = &env.state.env;
    for (int actor = 0; actor < 2; actor++) {
        pvp->players[actor].x = FIGHT_AREA_BASE_X + 10 + actor;
        pvp->players[actor].y = FIGHT_AREA_BASE_Y + 10;
        pvp->players[actor].current_hitpoints = pvp->players[actor].base_hitpoints;
        pvp->players[actor].current_magic = pvp->players[actor].base_magic;
        pvp->players[actor].attack_timer = 0;
        pvp->players[actor].has_attack_timer = 1;
        pvp->players[actor].special_energy = 0;
    }
}

static void hidden_contract(OpponentType bot) {
    reset(bot);
    static OsrsEnv variants[2];
    int hidden[2][OSRS_BASE_NUM_ACTION_HEADS] = {{0}};
    hidden[1][OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
    hidden[1][OSRS_HEAD_SPELL] = OSRS_SPELL_ICE_BARRAGE;
    hidden[1][OSRS_HEAD_SPECIAL] = 1;
    hidden[1][OSRS_HEAD_OVERHEAD] = ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC;
    for (int actor = 0; actor < 2; actor++) {
        for (int timer = 0; timer < 5; timer++) {
            for (unsigned seed = 1; seed <= 64; seed++) {
                for (int version = 0; version < 2; version++) {
                    variants[version] = env.state.env;
                    OsrsEnv* pvp = &variants[version];
                    pvp->rng_state = seed;
                    pvp->actions = hidden[version];
                    OpponentState* opp = actor == 1 ? &pvp->pvp_runtime.opponent
                        : &pvp->pvp_runtime.opponent_p0;
                    opp->type = bot;
                    opponent_reset(pvp, opp);
                    opp->pressure_control = (OppPressureControl){
                        .phase = OPP_ICE_LAUNCHED, .last_launch_tick = 0,
                        .target_x = pvp->players[1 - actor].x,
                        .target_y = pvp->players[1 - actor].y,
                    };
                    pvp->players[actor].attack_timer = timer;
                    Player* target = &pvp->players[1 - actor];
                    target->current_hitpoints = version ? 1 : 99;
                    target->current_prayer = version ? 0 : 99;
                    target->current_magic = version ? 0 : 99;
                    target->current_attack = version ? 0 : 99;
                    target->current_strength = version ? 0 : 99;
                    target->current_ranged = version ? 0 : 99;
                    target->special_energy = version ? 100 : 0;
                    target->attack_timer = version ? 100 : -1;
                    target->food_timer = version ? 100 : 0;
                    target->potion_timer = version ? 100 : 0;
                    target->frozen_ticks = version ? 100 : 0;
                    target->freeze_immunity_ticks = version ? 100 : 0;
                    target->num_pending_hits = version;
                    target->pending_hits[0].damage = version ? 100 : 0;
                    target->pending_hits[0].attack_type = version ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_MELEE;
                    for (int head = 0; head < OSRS_BASE_NUM_ACTION_HEADS; head++)
                        pvp->pending_actions[(1 - actor) * OSRS_BASE_NUM_ACTION_HEADS + head] = hidden[version][head];
                    if (actor == 1) generate_opponent_action(pvp, opp);
                    else generate_opponent_action_for_player0(pvp, opp);
                }
                CHECK("full actor dispatch ignores hidden target state and submitted actions",
                    memcmp(variants[0].pending_actions + actor * OSRS_BASE_NUM_ACTION_HEADS,
                        variants[1].pending_actions + actor * OSRS_BASE_NUM_ACTION_HEADS,
                        sizeof(int) * OSRS_BASE_NUM_ACTION_HEADS) == 0 &&
                    variants[0].rng_state == variants[1].rng_state);
                OpponentState* a = actor == 1 ? &variants[0].pvp_runtime.opponent
                    : &variants[0].pvp_runtime.opponent_p0;
                OpponentState* b = actor == 1 ? &variants[1].pvp_runtime.opponent
                    : &variants[1].pvp_runtime.opponent_p0;
                CHECK("hidden target state cannot alter persistent controller history",
                    memcmp(a, b, sizeof(*a)) == 0);
            }
        }
    }
    puf_close(&env);
}

static void attack_history_contract(void) {
    OsrsPvpLaunchHistory history = {.count = HISTORY_SIZE};
    const AttackStyle alternating[] = {
        ATTACK_STYLE_RANGED, ATTACK_STYLE_MELEE, ATTACK_STYLE_RANGED,
        ATTACK_STYLE_MELEE, ATTACK_STYLE_RANGED,
    };
    for (int index = 0; index < HISTORY_SIZE; index++)
        history.records[index] = (OsrsPvpLaunchRecord){
            alternating[index], PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC};
    CHECK("recent R M alternation predicts melee after ranged",
        opp_predict_attack_style(&history, ATTACK_STYLE_MAGIC, PRAYER_NONE, ATTACK_STYLE_RANGED) == ATTACK_STYLE_MELEE);

    const AttackStyle patterns[][HISTORY_SIZE] = {
        {ATTACK_STYLE_RANGED, ATTACK_STYLE_MELEE, ATTACK_STYLE_RANGED, ATTACK_STYLE_MELEE, ATTACK_STYLE_RANGED},
        {ATTACK_STYLE_MELEE, ATTACK_STYLE_RANGED, ATTACK_STYLE_MELEE, ATTACK_STYLE_RANGED, ATTACK_STYLE_MELEE},
        {ATTACK_STYLE_MAGIC, ATTACK_STYLE_MAGIC, ATTACK_STYLE_MAGIC, ATTACK_STYLE_MAGIC, ATTACK_STYLE_MAGIC},
        {ATTACK_STYLE_MELEE, ATTACK_STYLE_RANGED, ATTACK_STYLE_MAGIC, ATTACK_STYLE_MELEE, ATTACK_STYLE_RANGED},
    };
    const AttackStyle predictions[] = {
        ATTACK_STYLE_MELEE, ATTACK_STYLE_RANGED, ATTACK_STYLE_MAGIC, ATTACK_STYLE_MAGIC,
    };
    for (int pattern = 0; pattern < 4; pattern++) {
        for (int next = 0; next < HISTORY_SIZE; next++) {
            history.next = next;
            for (int age = 0; age < HISTORY_SIZE; age++)
                history.records[(next + age) % HISTORY_SIZE] = (OsrsPvpLaunchRecord){
                    patterns[pattern][age], PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MELEE};
            CHECK("completed attack transitions survive ring rotation and fallback disagreement",
                opp_predict_attack_style(&history, ATTACK_STYLE_MELEE, PRAYER_NONE,
                    patterns[pattern][HISTORY_SIZE - 1]) == predictions[pattern]);
        }
    }

    history.next = 0;

    for (int index = 0; index < HISTORY_SIZE; index++)
        history.records[index] = (OsrsPvpLaunchRecord){
            ATTACK_STYLE_MELEE, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MELEE};
    history.records[4] = (OsrsPvpLaunchRecord){
        ATTACK_STYLE_RANGED, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC};
    CHECK("visible weapon bait ignores launches from another pre-action weapon",
        opp_predict_attack_style(&history, ATTACK_STYLE_MAGIC, PRAYER_NONE, ATTACK_STYLE_MELEE) == ATTACK_STYLE_RANGED);
    CHECK("missing weapon context preserves existing forecast",
        opp_predict_attack_style(&history, ATTACK_STYLE_RANGED, PRAYER_NONE, ATTACK_STYLE_MAGIC) == ATTACK_STYLE_MAGIC);

    for (int index = 0; index < HISTORY_SIZE; index++)
        history.records[index] = (OsrsPvpLaunchRecord){
            index % 2 == 0 ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE,
            PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MELEE,
            index % 2 == 0 ? PRAYER_PROTECT_MELEE : PRAYER_PROTECT_RANGED};
    CHECK("completed attacks learn response to displayed melee protection",
        opp_predict_attack_style(&history, ATTACK_STYLE_MELEE, PRAYER_PROTECT_MELEE,
            ATTACK_STYLE_MELEE) == ATTACK_STYLE_RANGED);
    CHECK("completed attacks learn response to displayed ranged protection",
        opp_predict_attack_style(&history, ATTACK_STYLE_MELEE, PRAYER_PROTECT_RANGED,
            ATTACK_STYLE_RANGED) == ATTACK_STYLE_MELEE);
    CHECK("unseen displayed protection retains learned attack transition",
        opp_predict_attack_style(&history, ATTACK_STYLE_MELEE, PRAYER_PROTECT_MAGIC,
            ATTACK_STYLE_RANGED) == ATTACK_STYLE_MELEE);

    for (int index = 0; index < HISTORY_SIZE; index++)
        history.records[index].target_prayer = index % 2 == 0
            ? PRAYER_PROTECT_RANGED : PRAYER_PROTECT_MAGIC;
    const AttackStyle styles[] = {
        ATTACK_STYLE_MELEE, ATTACK_STYLE_RANGED, ATTACK_STYLE_MAGIC,
    };
    CHECK("full recent history rewards melee only after five attacks without melee protection",
        opp_target_never_protected_style(&history, ATTACK_STYLE_MELEE) &&
        !opp_target_never_protected_style(&history, ATTACK_STYLE_RANGED) &&
        !opp_target_never_protected_style(&history, ATTACK_STYLE_MAGIC) &&
        opp_weighted_attack_index(styles, &history, 0) == 0 &&
        opp_weighted_attack_index(styles, &history, 1) == 0 &&
        opp_weighted_attack_index(styles, &history, 2) == 1 &&
        opp_weighted_attack_index(styles, &history, 3) == 2);
    history.count = HISTORY_SIZE - 1;
    CHECK("incomplete match history does not trigger attack bias",
        opp_attack_weight(&history, ATTACK_STYLE_MELEE) == 1);

    history.count = HISTORY_SIZE;
    for (int index = 0; index < HISTORY_SIZE; index++)
        history.records[index].target_prayer = PRAYER_PROTECT_MELEE;
    CHECK("reversed protection history reverses the attack weights",
        !opp_target_never_protected_style(&history, ATTACK_STYLE_MELEE) &&
        opp_target_never_protected_style(&history, ATTACK_STYLE_RANGED) &&
        opp_weighted_attack_index(styles, &history, 0) == 0 &&
        opp_weighted_attack_index(styles, &history, 1) == 1 &&
        opp_weighted_attack_index(styles, &history, 2) == 1 &&
        opp_weighted_attack_index(styles, &history, 3) == 2 &&
        opp_weighted_attack_index(styles, &history, 4) == 2);

    OpponentState actor_zero = {0}, actor_one = {0};
    OsrsEnv pvp = {0};
    pvp.pvp_runtime.opponent_p0 = actor_zero;
    pvp.pvp_runtime.opponent = actor_one;
    pvp.pvp_runtime.launch_history[0] = history;
    pvp.pvp_runtime.launch_history[1] = (OsrsPvpLaunchHistory){0};
    CHECK("player-zero and player-one controllers read their own launch histories",
        opp_own_launch_history(&pvp, &pvp.pvp_runtime.opponent_p0) ==
            &pvp.pvp_runtime.launch_history[0] &&
        opp_own_launch_history(&pvp, &pvp.pvp_runtime.opponent) ==
            &pvp.pvp_runtime.launch_history[1] &&
        opp_predict_attack_style(&pvp.pvp_runtime.launch_history[1], ATTACK_STYLE_MAGIC, PRAYER_NONE,
            ATTACK_STYLE_MAGIC) == ATTACK_STYLE_MAGIC);

    OppAttackStyles voidwaker = opp_submitted_attack_styles(
        ITEM_VOIDWAKER, OSRS_PRIMARY_MOVE_ACTIONS, OSRS_SPELL_NONE, 1, 0,
        100, 1, 1);
    CHECK("Voidwaker adaptation follows its actual magic damage style",
        voidwaker.damage == ATTACK_STYLE_MAGIC &&
        opp_attack_weight(&history, voidwaker.damage) == 2);
}

int main(void) {
    attack_history_contract();
    CHECK("pressure opponent identities survive the NH interface change",
        OPP_RECORDED_ADAPTIVE_NH == 35 && OPP_BLOOD_PRESSURE_NH == 36 &&
        OPP_FREEZE_UNDERLAP_NH == 37);
    for (unsigned i = 0; i < sizeof(NH_SCRIPTED_OPPONENTS) / sizeof(*NH_SCRIPTED_OPPONENTS); i++)
        hidden_contract(NH_SCRIPTED_OPPONENTS[i]);
    reset(OPP_BLOOD_PRESSURE_NH);
    OsrsEnv* pvp = &env.state.env;
    puf_set_bot_policy(&env, OPP_BLOOD_PRESSURE_NH);
    pvp->players[0].prayer = PRAYER_PROTECT_MELEE;
    puf_step(&env);
    CHECK("unprotected Blood executes with mage gear and Augury",
        pvp->players[1].just_attacked && pvp->players[1].magic_type_this_tick == 2 &&
        pvp->players[1].attack_style_this_tick == ATTACK_STYLE_MAGIC &&
        pvp->players[1].offensive_prayer == OFFENSIVE_PRAYER_AUGURY &&
        get_item_attack_style(pvp->players[1].equipped[GEAR_SLOT_WEAPON]) == ATTACK_STYLE_MAGIC);
    puf_close(&env);

    reset(OPP_BLOOD_PRESSURE_NH);
    pvp = &env.state.env;
    pvp->players[0].prayer = PRAYER_PROTECT_MAGIC;
    puf_step(&env);
    CHECK("ProtectMagic is punished by executed ranged and Rigour",
        pvp->players[1].just_attacked && pvp->players[1].attack_style_this_tick == ATTACK_STYLE_RANGED &&
        pvp->players[1].offensive_prayer == OFFENSIVE_PRAYER_RIGOUR);
    puf_close(&env);

    reset(OPP_BLOOD_PRESSURE_NH);
    pvp = &env.state.env;
    pvp_apply_equipment_plan(&pvp->players[0], PVP_EQUIPMENT_MAGIC);
    OsrsPvpLaunchHistory* history = &pvp->pvp_runtime.launch_history[0];
    history->count = HISTORY_SIZE;
    history->next = 0;
    for (int index = 0; index < HISTORY_SIZE; index++)
        history->records[index] = (OsrsPvpLaunchRecord){
            index % 2 == 0 ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE,
            PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC};
    generate_opponent_action(pvp, &pvp->pvp_runtime.opponent);
    CHECK("recent alternating launches predict melee after ranged in actual bot dispatch",
        pvp->pending_actions[OSRS_BASE_NUM_ACTION_HEADS + OSRS_HEAD_OVERHEAD] == ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE &&
        history->records[4].style == ATTACK_STYLE_RANGED);
    pvp->players[0].prayer = PRAYER_PROTECT_MAGIC;
    for (int index = 0; index < HISTORY_SIZE; index++)
        history->records[index] = (OsrsPvpLaunchRecord){ATTACK_STYLE_MELEE,
            PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MELEE};
    history->records[4] = (OsrsPvpLaunchRecord){ATTACK_STYLE_RANGED,
        PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC};
    generate_opponent_action(pvp, &pvp->pvp_runtime.opponent);
    CHECK("matching pre-action weapon context excludes unrelated melee bait launches",
        pvp->pending_actions[OSRS_BASE_NUM_ACTION_HEADS + OSRS_HEAD_OVERHEAD] == ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED);
    pvp->pvp_runtime.opponent.visible_history = (OppVisibleHistory){OPP_HISTORY_READY,
        {ATTACK_STYLE_MAGIC, PRAYER_PROTECT_MELEE}, {ATTACK_STYLE_RANGED, PRAYER_PROTECT_MAGIC}};
    pvp->players[0].prayer = PRAYER_PROTECT_MELEE;
    generate_opponent_action(pvp, &pvp->pvp_runtime.opponent);
    CHECK("completed overhead ABA predicts Magic protection and avoids Blood",
        pvp->pending_actions[OSRS_BASE_NUM_ACTION_HEADS + OSRS_HEAD_SPELL] == OSRS_SPELL_NONE);
    pvp_reset(pvp, env.context.route_topology);
    CHECK("match reset clears both launch histories",
        pvp->pvp_runtime.launch_history[0].count == 0 &&
        pvp->pvp_runtime.launch_history[1].count == 0);
    puf_close(&env);

    reset(OPP_BLOOD_PRESSURE_NH);
    pvp = &env.state.env;
    Player* self = &pvp->players[1];
    memset(self->inventory_cells, 0, sizeof(self->inventory_cells));
    slot_equip_item(self, GEAR_SLOT_WEAPON, ITEM_VOIDWAKER);
    self->is_lunar_spellbook = 1;
    self->special_energy = 100;
    pvp->players[0].prayer = PRAYER_PROTECT_MELEE;
    puf_step(&env);
    CHECK("Voidwaker pressure uses magic damage with Piety offense",
        self->just_attacked && self->used_special_this_tick &&
        self->attack_style_this_tick == ATTACK_STYLE_MAGIC &&
        self->offensive_prayer == OFFENSIVE_PRAYER_PIETY);
    puf_close(&env);

    reset(OPP_BLOOD_PRESSURE_NH);
    pvp = &env.state.env;
    self = &pvp->players[1];
    self->current_magic = self->base_magic - 30;
    self->current_hitpoints = self->base_hitpoints * 3 / 5;
    puf_step(&env);
    CHECK("pressure restores brewed magic before another noncritical brew",
        self->current_magic >= self->base_magic && self->consumable_used_this_tick);
    puf_close(&env);

    reset(OPP_BLOOD_PRESSURE_NH);
    pvp = &env.state.env;
    self = &pvp->players[1];
    self->x = 3063;
    self->y = 3557;
    pvp->players[0].x = 3063;
    pvp->players[0].y = 3555;
    pvp_apply_equipment_plan(&pvp->players[0], PVP_EQUIPMENT_MELEE);
    const EncounterArenaTopology* topology = pvp_route_topology_owner.topology;
    CHECK("known boundary fixture starts on legal actor tiles",
        pvp_topology_tile_walkable(topology, self->x, self->y) &&
        pvp_topology_tile_walkable(topology, pvp->players[0].x, pvp->players[0].y));
    int legal_moves = 0;
    for (int tick = 0; tick < 8; tick++) {
        self->attack_timer = 3;
        int x = self->x, y = self->y;
        puf_step(&env);
        int primary = pvp->last_executed_actions[OSRS_BASE_NUM_ACTION_HEADS + OSRS_HEAD_PRIMARY];
        if (primary == 0) continue;
        CHECK("cooldown kiting never submits blocked boundary destinations",
            primary < OSRS_PRIMARY_MOVE_ACTIONS &&
            pvp_topology_destination_selectable(topology,
                x + ENCOUNTER_MOVE_TARGET_DX[primary], y + ENCOUNTER_MOVE_TARGET_DY[primary]));
        CHECK("legal cooldown destination moves in the same tick", self->x != x || self->y != y);
        legal_moves++;
    }
    CHECK("boundary pressure escapes instead of repeating unselectable routes",
        legal_moves > 0 && chebyshev_distance(self->x, self->y,
            pvp->players[0].x, pvp->players[0].y) >= COMBAT_SPELL_ATTACK_RANGE / 2);
    puf_close(&env);

    reset(OPP_FREEZE_UNDERLAP_NH);
    pvp = &env.state.env;
    puf_set_bot_policy(&env, OPP_FREEZE_UNDERLAP_NH);
    pvp->players[0].frozen_ticks = 40;
    pvp->players[0].prayer = PRAYER_PROTECT_MELEE;
    pvp->players[1].x = pvp->players[0].x;
    int launches[3] = {0}, count = 0, under_ticks = 0;
    for (int tick = 0; tick < COMBAT_SPELL_CAST_SPEED * 2 + 1; tick++) {
        puf_step(&env);
        if (pvp->players[1].just_attacked) {
            CHECK("underlap cast steps out and executes Ice in same engine tick",
                pvp->players[1].magic_type_this_tick == 1 && pvp->players[1].last_attack_dist > 0 &&
                pvp->players[1].offensive_prayer == OFFENSIVE_PRAYER_AUGURY);
            CHECK("bounded fixture launch count", count < 3);
            launches[count++] = tick;
        }
        if (pvp->players[0].x == pvp->players[1].x && pvp->players[0].y == pvp->players[1].y)
            under_ticks++;
    }
    CHECK("engine stepout preserves canonical five tick spell cadence",
        count == 3 && launches[1] - launches[0] == COMBAT_SPELL_CAST_SPEED &&
        launches[2] - launches[1] == COMBAT_SPELL_CAST_SPEED);
    CHECK("confirmed own Ice permits cooldown underlap", under_ticks > 0);
    puf_close(&env);
    reset(OPP_FREEZE_UNDERLAP_NH);
    pvp = &env.state.env;
    generate_opponent_action(pvp, &pvp->pvp_runtime.opponent);
    CHECK("queued Ice is not treated as a completed cast",
        pvp->pvp_runtime.opponent.pressure_control.phase == OPP_ICE_SUBMITTED);
    pvp->players[1].attack_timer = 3;
    generate_opponent_action(pvp, &pvp->pvp_runtime.opponent);
    CHECK("failed Ice attempt cannot authorize underlap",
        pvp->pvp_runtime.opponent.pressure_control.phase == OPP_ICE_SEEK);
    pvp->pvp_runtime.opponent.pressure_control.phase = OPP_ICE_LAUNCHED;
    pvp->pvp_runtime.opponent.pressure_control.target_x = pvp->players[0].x;
    pvp->pvp_runtime.opponent.pressure_control.target_y = pvp->players[0].y;
    pvp->players[0].x++;
    generate_opponent_action(pvp, &pvp->pvp_runtime.opponent);
    CHECK("completed visible displacement cancels the underlap assumption",
        pvp->pvp_runtime.opponent.pressure_control.phase == OPP_ICE_SEEK);
    puf_close(&env);

    for (int bot = OPP_BLOOD_PRESSURE_NH; bot <= OPP_FREEZE_UNDERLAP_NH; bot++) {
        reset((OpponentType)bot);
        pvp = &env.state.env;
        pvp->pvp_runtime.use_c_opponent_p0 = 1;
        pvp->pvp_runtime.opponent_p0.type = OPP_GENERALIST_NH;
        opponent_reset(pvp, &pvp->pvp_runtime.opponent_p0);
        int attacks[4] = {0}, spells[3] = {0}, consumed = 0, ticks = 0;
        do {
            puf_step(&env);
            ticks++;
            Player* pressure = &pvp->players[1];
            consumed += pressure->consumable_used_this_tick != 0;
            int* executed = pvp->last_executed_actions + OSRS_BASE_NUM_ACTION_HEADS;
            if (executed[OSRS_HEAD_PRIMARY] < OSRS_PRIMARY_MOVE_ACTIONS) {
                for (int slot = 0; slot < NUM_DYNAMIC_GEAR_SLOTS; slot++)
                    CHECK("pressure cooldown does not spam equipment switches",
                        executed[OSRS_HEAD_EQUIP_SLOT(DYNAMIC_GEAR_SLOTS[slot])] == 0);
            }
            if (!pressure->just_attacked) continue;
            attacks[pressure->attack_style_this_tick]++;
            spells[pressure->magic_type_this_tick]++;
            AttackStyle offense = pressure->used_special_this_tick &&
                pressure->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER
                    ? ATTACK_STYLE_MELEE : pressure->attack_style_this_tick;
            CHECK("full fight pressure launches have coherent offensive prayer",
                pressure->offensive_prayer == encounter_offensive_prayer_for_style(offense));
            CHECK("full fight launches obey actual attack geometry", pressure->last_attack_dist > 0);
        } while (!terminal);
        CHECK("pressure full fight attacks and actively consumes", consumed > 0 &&
            attacks[ATTACK_STYLE_MAGIC] + attacks[ATTACK_STYLE_RANGED] + attacks[ATTACK_STYLE_MELEE] > 0);
        printf("PressureNH bot%d ticks%d melee%d ranged%d magic%d Ice%d Blood%d consumes%d\n",
            bot, ticks, attacks[ATTACK_STYLE_MELEE], attacks[ATTACK_STYLE_RANGED],
            attacks[ATTACK_STYLE_MAGIC], spells[1], spells[2], consumed);
        puf_close(&env);
    }
    return osrs_test_summary();
}
