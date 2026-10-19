/* The spectated Dharok kits (riskfight_model.h RISKFIGHT_DHAROK_SPECTATED_LOADOUT: the abyssal tentacle wielded with a Voidwaker and a maul, a Voidwaker alone, or
 * an Armadyl godsword and a maul in the bag) end to end:
 * - shape: the user's Dharok kit with the tentacle wielded, 28 cells full, the supplies trimmed by one marlin for the extra weapon where a maul stays; class Dharok;
 * - the training draw of live_loadout 5: a Dharok draw is a spectated kit with dharok_spectated_share, evenly; live_loadout 4's draw never yields one;
 * - in the sim: the spec weapon is the bag's opener, wield + special arms it and spends 50 on the swing, the maul's special follows (a chain), and a two-hander
 *   (axe, maul, godsword) is offered and wields only once a cell is free (the defender goes to the bag);
 * - live (gs_rl_server): the tentacle and the godsword reach the sim as their own rows, not priors or inert filler, and wield + special + attack, and a 2h wield
 *   with a free cell, become CLICK_SLOT / SPEC_TOGGLE / ATTACK records on the right slots.
 *   cc -std=gnu11 -O2 -I. -Isrc -Wno-comment -o /tmp/t ocean/osrs/tests/test_riskfight_dharok_spectated.c -lm -lpthread && /tmp/t   (cwd = repo root) */
#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"
#include "ini.h" /* Dict: riskfight_training_config */
#include "../../osrs_riskfight/riskfight_training.h"

static RiskfightState state;
static RiskfightContext context;

static Player* us(void) { return &state.env.players[0]; }

static const uint8_t OPENER[RF_DHAROK_SPECTATED_KITS] = {ITEM_VOIDWAKER, ITEM_VOIDWAKER, ITEM_AGS};
static const int HAS_MAUL[RF_DHAROK_SPECTATED_KITS] = {1, 0, 1};
static const char* const NAME[RF_DHAROK_SPECTATED_KITS] = {"tentacle+VW+maul", "tentacle+VW", "tentacle+AGS+maul"};

static void reset_with(const RiskfightLoadout* kit, uint32_t seed) {
    context.self_play = 1;
    context.loadout[0] = kit;
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, seed);
    context.loadout[0] = NULL;
}

static int slot_of(const Player* p, OsrsConsumableKind kind, uint8_t item) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        if (osrs_inventory_cell_is_empty(&p->inventory_cells[slot])) continue;
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
        if (kind != OSRS_CONSUMABLE_NONE ? m->consumable_kind == kind : m->item_idx == item) return slot;
    }
    return -1;
}

/** Whether our mask offers `ours` (physical slots) head by head. */
static int offered(const RiskfightState* s, const int* ours) {
    unsigned char mask[RF_MASK_SIZE];
    riskfight_deployable = 1;
    riskfight_write_action_mask(s, 0, mask);
    riskfight_deployable = 0;
    int canonical[RF_HEADS];
    memcpy(canonical, ours, sizeof(canonical));
    riskfight_bag_to_canonical(&s->env.players[0], canonical);
    for (int head = 0; head < RF_HEADS; head++)
        if (!mask[RF_HEAD_OFFSET[head] + canonical[head]]) return 0;
    return 1;
}

static void step(const int* ours) {
    int actions[2 * RF_HEADS] = {0};
    memcpy(actions, ours, RF_HEADS * sizeof(int));
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    assert(!state.env.episode_over);
}

static void test_shapes(void) {
    const RiskfightLoadout* user = &RISKFIGHT_DHAROK_LOADOUT;
    for (int k = 0; k < RF_DHAROK_SPECTATED_KITS; k++) {
        const RiskfightLoadout* kit = &RISKFIGHT_DHAROK_SPECTATED_LOADOUT[k];
        int marlin = 0, halibut = 0, pie = 0, opener = 0, maul = 0, axe = 0, orb = 0, vw = 0;
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
            assert(kit->inventory[slot] != 0);
            const OsrsItemContentMetadata* m = osrs_item_content_metadata(kit->inventory[slot]);
            marlin += m->consumable_kind == OSRS_CONSUMABLE_MARLIN; halibut += m->consumable_kind == OSRS_CONSUMABLE_HALIBUT;
            pie += m->consumable_kind == OSRS_CONSUMABLE_SUMMER_PIE; orb += m->click_action == OSRS_CLICK_SELF_DAMAGE;
            opener += m->item_idx == OPENER[k]; maul += m->item_idx == ITEM_GRANITE_MAUL_ORNATE; axe += m->item_idx == ITEM_DHAROKS_GREATAXE;
            vw += m->item_idx == ITEM_VOIDWAKER;
        }
        assert(opener == 1 && maul == HAS_MAUL[k] && axe == 1 && orb == 1 && pie == 2 && halibut == 6 && marlin == (HAS_MAUL[k] ? 9 : 10));
        assert(vw == (OPENER[k] == ITEM_VOIDWAKER));
        for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++)
            assert(kit->equipment[slot] == (slot == GEAR_SLOT_WEAPON ? ITEM_ABYSSAL_TENTACLE : user->equipment[slot]));
        assert(kit->vengeance_sacks == user->vengeance_sacks && kit->hitpoints == user->hitpoints);
        assert(riskfight_loadout_class(kit) == RF_CLASS_DHAROK);
        reset_with(kit, 123);
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) assert(!osrs_inventory_cell_is_empty(&us()->inventory_cells[slot]));
        assert(us()->equipped[GEAR_SLOT_WEAPON] == ITEM_ABYSSAL_TENTACLE && us()->equipped[GEAR_SLOT_SHIELD] == ITEM_DRAGON_DEFENDER);
        assert(us()->equipment_effect_profile.dharok_piece_count == 3 && us()->base_defence == MAXED_BASE_DEFENCE);
        printf("kit %s: 28 cells, %d marlin, tentacle and defender worn with the Dharok set, class Dharok\n", NAME[k], marlin);
    }
}

static void test_draw(void) {
    DictItem items[] = {{.key = "session_ticks", .value = 3000}, {.key = "curveball_probability", .value = 0}, {.key = "dharok_spectated_share", .value = 0.5}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    RiskfightTraining training;
    memset(&training, 0, sizeof(training));
    training.config = riskfight_training_config(&kwargs);
    training.rng = 0x2545f491u;
    enum { DRAWS = 120000 };
    int user = 0, spectated[RF_DHAROK_SPECTATED_KITS] = {0}, vw = 0, four_spectated = 0;
    for (int i = 0; i < DRAWS; i++) {
        const RiskfightLoadout* drawn = riskfight_training_draw_kit(&training, RF_KIT_MIX_DHAROK_SPECTATED);
        if (drawn == &RISKFIGHT_DHAROK_LOADOUT) user++;
        else if (drawn >= RISKFIGHT_DHAROK_SPECTATED_LOADOUT && drawn < RISKFIGHT_DHAROK_SPECTATED_LOADOUT + RF_DHAROK_SPECTATED_KITS)
            spectated[drawn - RISKFIGHT_DHAROK_SPECTATED_LOADOUT]++;
        else { assert(drawn == &RISKFIGHT_LIVE_LOADOUT || drawn == &RISKFIGHT_VW_LOADOUT || drawn == &RISKFIGHT_VW_PIES_LOADOUT); vw++; }
        const RiskfightLoadout* four = riskfight_training_draw_kit(&training, RF_KIT_MIX_DHAROK);
        four_spectated += four >= RISKFIGHT_DHAROK_SPECTATED_LOADOUT && four < RISKFIGHT_DHAROK_SPECTATED_LOADOUT + RF_DHAROK_SPECTATED_KITS;
    }
    const double sigma_user = sqrt(DRAWS * 0.25 * 0.75), sigma_kit = sqrt(DRAWS / 12.0 * (11.0 / 12)), sigma_vw = sqrt(DRAWS * 0.25);
    assert(fabs(user - DRAWS * 0.25) <= 4.5 * sigma_user && fabs(vw - DRAWS * 0.5) <= 4.5 * sigma_vw && four_spectated == 0);
    for (int k = 0; k < RF_DHAROK_SPECTATED_KITS; k++) assert(fabs(spectated[k] - DRAWS / 12.0) <= 4.5 * sigma_kit);
    printf("draw (live_loadout 5, share 0.5): user's Dharok %.3f, %s %.3f, %s %.3f, %s %.3f, VW kits %.3f; live_loadout 4 never draws one\n", (double)user / DRAWS,
        NAME[0], (double)spectated[0] / DRAWS, NAME[1], (double)spectated[1] / DRAWS, NAME[2], (double)spectated[2] / DRAWS, (double)vw / DRAWS);
    /* The env key: live_loadout 5 sets the mix. */
    riskfight_put_int((EncounterState*)&state, (EncounterContext*)&context, "live_loadout", 5);
    assert(context.kit_mix == RF_KIT_MIX_DHAROK_SPECTATED && context.loadout[0] == &RISKFIGHT_VW_LOADOUT && context.loadout[1] == &RISKFIGHT_LIVE_LOADOUT);
    riskfight_put_int((EncounterState*)&state, (EncounterContext*)&context, "live_loadout", 0);
}

/** Wield the opener + 1 special + attack, at his bar 3 (the gate reads the bar; his hitpoints stay 121 so nothing dies); then the maul + 1 special. */
static void test_switch_spec_and_2h(int k) {
    reset_with(&RISKFIGHT_DHAROK_SPECTATED_LOADOUT[k], 77);
    state.env.players[1].current_hitpoints = 121;
    state.visible[0].health_bar = 3;
    assert(riskfight_spec_weapon(us()).item == OPENER[k] && us()->special_energy == 100);
    const int two_handed_opener = item_is_two_handed(OPENER[k]);
    int opener = slot_of(us(), OSRS_CONSUMABLE_NONE, OPENER[k]);
    int decision[RF_HEADS] = {0}, attack[RF_HEADS] = {0};
    decision[RF_WEAPON] = opener + 1;
    decision[RF_SPECIAL] = 1;
    decision[RF_PRIMARY] = RF_ATTACK;
    attack[RF_PRIMARY] = RF_ATTACK;
    /* A two-handed opener (the godsword) needs a free cell with the defender on: the bag is full. */
    assert(offered(&state, decision) == !two_handed_opener);
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    assert(obs[RF_TACTICS_START + RF_TACTICS_CAN_WIELD_2H] == 0);
    int freed = slot_of(us(), OSRS_CONSUMABLE_MARLIN, 0);
    us()->inventory_cells[freed] = osrs_inventory_cell_empty();
    riskfight_write_observation(&state, 0, obs);
    assert(obs[RF_TACTICS_START + RF_TACTICS_CAN_WIELD_2H] == 1);
    assert(offered(&state, decision));
    /* Every two-hander of the bag wields once a cell is free. */
    const uint8_t two_handers[] = {ITEM_DHAROKS_GREATAXE, ITEM_GRANITE_MAUL_ORNATE, ITEM_AGS};
    for (size_t i = 0; i < sizeof(two_handers); i++) {
        int slot = slot_of(us(), OSRS_CONSUMABLE_NONE, two_handers[i]);
        if (slot < 0) continue;
        int wield[RF_HEADS] = {0};
        wield[RF_WEAPON] = slot + 1;
        assert(offered(&state, wield));
    }
    step(decision);
    assert(us()->equipped[GEAR_SLOT_WEAPON] == OPENER[k] && (us()->equipped[GEAR_SLOT_SHIELD] == ITEM_NONE) == two_handed_opener);
    int tick = 0;
    while (us()->special_energy == 100 && tick++ < 8) step(attack);
    assert(us()->special_energy == 50 && state.specs_used[0] == 1);
    int spent_tick = tick;
    if (HAS_MAUL[k]) {
        int maul = slot_of(us(), OSRS_CONSUMABLE_NONE, ITEM_GRANITE_MAUL_ORNATE), chain[RF_HEADS] = {0};
        assert(maul >= 0);
        chain[RF_WEAPON] = maul + 1;
        chain[RF_SPECIAL] = 1;
        chain[RF_PRIMARY] = RF_ATTACK;
        state.visible[0].health_bar = 3; /* the opener's hitsplat showed his real bar: back to the low one the gate reads */
        assert(offered(&state, chain) && riskfight_spec_clicks(&state, 0, chain) == 1);
        step(chain);
        for (int t = 0; t < 2 && us()->special_energy > 0; t++) step(attack);
        assert(us()->equipped[GEAR_SLOT_WEAPON] == ITEM_GRANITE_MAUL_ORNATE && us()->special_energy == 0 && state.specs_used[0] == 2);
    }
    printf("sim %s: %s + special armed and spent 50 after %d attack ticks%s; two-handers offered once a cell is free (none with a full bag%s)\n", NAME[k],
        OPENER[k] == ITEM_AGS ? "AGS" : "VW", spent_tick, HAS_MAUL[k] ? ", then the maul's special the next decision (energy 0)" : "",
        two_handed_opener ? ", the godsword included" : "");
}

/* A hand-written request (the Dharok kit test's, test_riskfight_dharok_kit.c): our W line wields the tentacle, the G line is built from the kit's bag. */
static const char* const REQUEST_HEAD =
    "LA 7 804\n"
    "B 79 115 100 100 2 1 0.11899791231732777 1 806 808 804 3 0 1 24\n"
    "K 363066 1000000\n"
    "F 1 1 1 100\n"
    "O 92 72 118 118 120 99 100 0 0 0 0 22 0 3 0 0 0 1 1 0 4 -1 4 4587 0 0 -1 OBSERVED 3 3\n"
    "Q none 3 802 803 804\n"
    "W 4880 21295 1725 12006 4892 12954 -1 4898 -1 7462 11840 -1 2550\n";
static const char* const REQUEST_TAIL =
    "SHIELD OBSERVED 8849\n"
    "ARMOR OBSERVED 10828 6570 1725 10551 2497 7462 4131\n"
    "PS 0 KNOWN 0 0 0\n"
    "PS 1 CLEAR_PRIOR\n"
    "RA OBSERVED 1\n"
    "P KNOWN 1\n"
    "D 0 0 0 0 0 1 1 15 0 0 10000 0 -1\n"
    "R KNOWN 0\n"
    "END\n";

static void test_server(int k, int free_cell) {
    const RiskfightLoadout* kit = &RISKFIGHT_DHAROK_SPECTATED_LOADOUT[k];
    char text[4096];
    int n = snprintf(text, sizeof(text), "%sG", REQUEST_HEAD), marlin = -1;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        const OsrsItemContentMetadata* m = osrs_item_content_metadata(kit->inventory[i]);
        if (m->consumable_kind == OSRS_CONSUMABLE_MARLIN) marlin = i;
        int raw = m->raw_osrs_id;
        n += snprintf(text + n, sizeof(text) - n, " %d", raw);
    }
    n += snprintf(text + n, sizeof(text) - n, "\n%s", REQUEST_TAIL);
    assert(n < (int)sizeof(text) && marlin >= 0);
    static Request q;
    FILE* in = fmemopen(text, strlen(text), "r");
    read_request(in, &q);
    fclose(in);
    if (free_cell) q.bag[marlin] = -1;
    RtSeen seen = {-1, 0, 0, 0, 0, 0, -1, -1, 0, 0, rt_bag_servings(q.bag)};
    request_declare_priors(&q);
    assert(server_unavailable(&q) == NULL);
    RtRecord records[GS_WIRE_MAX_RECORDS];
    int count;
    rt_decide(&q, &seen, 1, q.tick, 0, records, &count);
    RiskfightState* s = &g_base.state;
    const Player* p = &s->env.players[0];
    /* The canonical bag: every cell the kit's own (the restores as their content), the opener and the tentacle as their rows. */
    assert(p->equipped[GEAR_SLOT_WEAPON] == ITEM_ABYSSAL_TENTACLE && p->equipped[GEAR_SLOT_HEAD] == ITEM_DHAROKS_HELM);
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        if (free_cell && i == marlin) { assert(osrs_inventory_cell_is_empty(&p->inventory_cells[i])); continue; }
        const OsrsItemContentMetadata* want = osrs_item_content_metadata(kit->inventory[i]), *have = osrs_inventory_cell_metadata(&p->inventory_cells[i]);
        assert(have->item_idx == want->item_idx && have->consumable_kind == want->consumable_kind);
    }
    s->visible[0].health_bar = 3; /* his bar low: the spec gate opens */
    const int opener = slot_of(p, OSRS_CONSUMABLE_NONE, OPENER[k]), two_handed = item_is_two_handed(OPENER[k]);
    int actions[RF_HEADS] = {0};
    actions[RF_WEAPON] = opener + 1;
    actions[RF_SPECIAL] = 1;
    actions[RF_PRIMARY] = RF_ATTACK;
    assert(offered(s, actions) == (!two_handed || free_cell));
    if (!two_handed || free_cell) {
        RtRecord out[GS_WIRE_MAX_RECORDS];
        int m = rt_records(actions, s, out);
        assert(m == 3 && out[0].kind == GS_WIRE_CLICK_SLOT && out[0].arg == opener && out[1].kind == GS_WIRE_SPEC_TOGGLE && out[2].kind == GS_WIRE_ATTACK);
    }
    /* The two-handers: offered only with a free cell; the wield is a CLICK_SLOT on the weapon's own slot. */
    const uint8_t two_handers[] = {ITEM_DHAROKS_GREATAXE, ITEM_GRANITE_MAUL_ORNATE, ITEM_AGS};
    for (size_t i = 0; i < sizeof(two_handers); i++) {
        int slot = slot_of(p, OSRS_CONSUMABLE_NONE, two_handers[i]);
        if (slot < 0) continue;
        int wield[RF_HEADS] = {0};
        wield[RF_WEAPON] = slot + 1;
        assert(offered(s, wield) == free_cell);
        if (!free_cell) continue;
        RtRecord out[GS_WIRE_MAX_RECORDS];
        int m = rt_records(wield, s, out);
        assert(m == 1 && out[0].kind == GS_WIRE_CLICK_SLOT && out[0].arg == slot);
    }
    printf("server %s (%s): tentacle worn, the bag as the kit's cells, %s\n", NAME[k], free_cell ? "a cell free" : "bag full",
        free_cell ? "opener wield + special + attack and every 2h wield are CLICK_SLOT / SPEC_TOGGLE / ATTACK records"
                  : two_handed ? "the godsword and the other 2h wields masked" : "VW wield + special + attack records, the 2h wields masked");
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    assert(canonical(12006) == 12006 && canonical(26484) == 12006 && canonical(11802) == 11802 && canonical(29605) == 11802);
    test_shapes();
    test_draw();
    for (int k = 0; k < RF_DHAROK_SPECTATED_KITS; k++) test_switch_spec_and_2h(k);
    g_fight.net = NULL;
    g_fight.rng = 0x9e3779b9u;
    g_fight.hard = 1;
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99; g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    observer_register_items();
    sim_init(&g_base);
    for (int k = 0; k < RF_DHAROK_SPECTATED_KITS; k++)
        for (int free_cell = 0; free_cell < 2; free_cell++) test_server(k, free_cell);
    riskfight_destroy_context((EncounterContext*)&context);
    puts("riskfight spectated Dharok kits PASS");
    return 0;
}
