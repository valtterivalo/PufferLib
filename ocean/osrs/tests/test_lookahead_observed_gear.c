#define main calibrator_main
#include "calibrate_riskfight.c"
#undef main
#define main server_main
#include "gs_lookahead_server.c"
#undef main
#include "osrs_test_check.h"

int main(void) {
    gs_register_java_items();
    GsInfer infer = {0};
    GsWire wire = {0};
    const char* legacy = "S 394 121 118 118 119 99 99 90 0 1000 0 0 3162 3503 -1 10828 21295 1725 27690 10551 22322 -1 4087 -1 7462 11840 -1 2550 -1 24592 24592 24595 24595 24598 24595 24595 24595 24598 24592 24595 24595 24592 6685 24592 6685 -1 24592 24592 24592 24225 12695 24592 24592 24592 24592 24621 8013 1 0 -1 -1";
    const char* tracked = "S 394 121 118 118 119 99 99 90 0 1000 0 0 3162 3503 -1 10828 21295 1725 27690 10551 22322 -1 4087 -1 7462 11840 -1 2550 -1 24592 24592 24595 24595 24598 24595 24595 24595 24598 24592 24595 24595 24592 6685 24592 6685 -1 24592 24592 24592 24225 12695 24592 24592 24592 24592 24621 8013 2 0 -1 1 0 1 0 0 0 15 30 27690 NONE 6 0 0 0 29738 0 24370";
    gs_wire_apply(&infer, &wire, legacy);
    CHECK("old client states have unknown gear provenance", infer.c.opponent_gear.source == GS_GEAR_UNKNOWN);
    gs_wire_apply(&infer, &wire, tracked);
    GsClient unchanged = infer.c;
    char state[4096];
    snprintf(state, sizeof(state), "%s ANIMATION UNKNOWN GEAR OBSERVED 27690 22322 10828 21295 1725 10551 4087 7462 11840", tracked);
    gs_wire_apply(&infer, &wire, state);
    CHECK("wire carries current observed raw equipment", infer.c.opponent_gear.source == GS_GEAR_COMPOSITION_OBSERVED && infer.c.opponent_gear.weapon == 27690 && infer.c.opponent_gear.shield == 22322);
    infer.c.opponent_gear = (GsObservedGear){0};
    CHECK("gear provenance preserves legacy analytical inputs", !memcmp(&infer.c, &unchanged, sizeof(unchanged)));
    snprintf(state, sizeof(state), "%s ANIMATION UNKNOWN GEAR OBSERVED -1 -1 -1 -1 -1 -1 -1 -1 -1", tracked);
    gs_wire_apply(&infer, &wire, state);
    CHECK("wire preserves observed empty visible slots", infer.c.opponent_gear.source == GS_GEAR_COMPOSITION_OBSERVED && infer.c.opponent_gear.weapon == -1 && infer.c.opponent_gear.shield == -1);
    snprintf(state, sizeof(state), "%s ANIMATION UNKNOWN GEAR OBSERVED 27690 22322", tracked);
    gs_wire_apply(&infer, &wire, state);
    CHECK("legacy two-ID gear retains weapon provenance with incomplete armor", infer.c.opponent_gear.source == GS_GEAR_WEAPON_SHIELD_OBSERVED && la_gear_status(infer.c.opponent_gear).status == LA_GEAR_ARMOR_UNKNOWN);
    gs_wire_apply(&infer, &wire, legacy);
    CHECK("legacy source clears prior observed gear", infer.c.opponent_gear.source == GS_GEAR_UNKNOWN);
    GsObservedGear gear = {GS_GEAR_UNKNOWN, 27690, 22322, {10828, 21295, 1725, 10551, 4087, 7462, 11840}};
    CHECK("guessed numeric equipment is not observed", la_gear_status(gear).status == LA_GEAR_UNKNOWN);
    gear.source = GS_GEAR_COMPOSITION_OBSERVED;
    CHECK("physical Voidwaker and Avernic are supported", la_gear_status(gear).status == LA_GEAR_SUPPORTED);
    gear.armor[0] = 11665;
    LaGearIssue unsupported = la_gear_status(gear);
    CHECK("unsupported observed armor reports its exact slot and raw item", unsupported.status == LA_GEAR_ARMOR_UNSUPPORTED && unsupported.slot == 0 && unsupported.raw == 11665);
    gear.armor[0] = 10828;
    gear.weapon = 29796;
    CHECK("unknown weapon mechanics remain unavailable", la_gear_status(gear).status == LA_GEAR_WEAPON_UNSUPPORTED);
    gear.weapon = 27690; gear.shield = 9999;
    CHECK("unknown shield mechanics remain unavailable", la_gear_status(gear).status == LA_GEAR_SHIELD_UNSUPPORTED);
    gear.shield = 8850;
    uint16_t rune_defender_code = osrs_inventory_content_code_from_raw_osrs_id(8850);
    CHECK("Rune defender raw ID maps to its physical item", raw_id_to_item(8850) == ITEM_RUNE_DEFENDER);
    CHECK("Rune defender maps to the shield content row", OSRS_ITEM_CONTENT_METADATA[rune_defender_code].item_idx == ITEM_RUNE_DEFENDER && OSRS_ITEM_CONTENT_METADATA[rune_defender_code].gear_slot == GEAR_SLOT_SHIELD);
    CHECK("item-index and raw-ID lookups share the Rune defender code", osrs_inventory_content_code_from_item(ITEM_RUNE_DEFENDER) == rune_defender_code && osrs_inventory_content_code_from_raw_osrs_id(13441) == 216);
    CHECK("Rune defender has supported observed mechanics", la_gear_status(gear).status == LA_GEAR_SUPPORTED);
    CHECK("existing Rune defender content code remains 217", rune_defender_code == 217);
    static const struct { int raw; ItemIndex item; int kit_slot; uint32_t effects; } mappings[] = {
        {6570, ITEM_FIRE_CAPE, 1, OSRS_ITEM_EFFECT_NONE},
        {4131, ITEM_RUNE_BOOTS, 10, OSRS_ITEM_EFFECT_NONE},
        {7460, ITEM_RUNE_GLOVES, 9, OSRS_ITEM_EFFECT_NONE},
        {1704, ITEM_GLORY, 2, OSRS_ITEM_EFFECT_NONE},
        {4880, ITEM_DHAROKS_HELM, 0, OSRS_ITEM_EFFECT_DHAROK_PIECE},
        {4892, ITEM_DHAROKS_PLATEBODY, 4, OSRS_ITEM_EFFECT_DHAROK_PIECE},
        {4898, ITEM_DHAROKS_PLATELEGS, 7, OSRS_ITEM_EFFECT_DHAROK_PIECE},
        {4994, ITEM_VERACS_PLATESKIRT, 7, OSRS_ITEM_EFFECT_NONE},
        {24133, ITEM_INFERNAL_CAPE, 1, OSRS_ITEM_EFFECT_NONE},
        {24224, ITEM_INFERNAL_CAPE, 1, OSRS_ITEM_EFFECT_NONE},
        {28055, ITEM_DRAGON_BOOTS, 10, OSRS_ITEM_EFFECT_NONE},
        {28069, ITEM_FIGHTER_TORSO, 4, OSRS_ITEM_EFFECT_NONE}
    };
    for (size_t i = 0; i < sizeof(mappings) / sizeof(mappings[0]); i++) {
        LaItem resolved = la_item_of(mappings[i].raw);
        CHECK("classical and alias raw IDs resolve to the declared physical item", resolved.kind == LA_ITEM_MAPPED && resolved.item == mappings[i].item);
        const GsJavaItemRow* raw = NULL;
        for (size_t j = 0; j < sizeof(GS_JAVA_ITEMS) / sizeof(GS_JAVA_ITEMS[0]); j++)
            if (GS_JAVA_ITEMS[j].id == mappings[i].raw) { raw = &GS_JAVA_ITEMS[j]; break; }
        assert(raw);
        const Item* physical = &ITEM_DATABASE[resolved.item];
        int observed[] = {raw->astab, raw->aslash, raw->acrush, raw->arange, raw->amagic, raw->dstab, raw->dslash, raw->dcrush, raw->drange, raw->dmagic, raw->str, raw->rstr, raw->prayer, raw->aspeed};
        int simulated[] = {physical->attack_stab, physical->attack_slash, physical->attack_crush, physical->attack_ranged, physical->attack_magic, physical->defence_stab, physical->defence_slash, physical->defence_crush, physical->defence_ranged, physical->defence_magic, physical->melee_strength, physical->ranged_strength, physical->prayer, physical->attack_speed};
        CHECK("mapped armor preserves authoritative raw bonuses and slot", !memcmp(observed, simulated, sizeof(observed)) && raw->mdmg == physical->magic_damage && raw->slot == mappings[i].kit_slot);
        CHECK("mapped armor preserves its physical effect contract", physical->effect_mask == mappings[i].effects);
        if (i < 3) {
            CHECK("new gear observation codes append without shifting existing content", osrs_inventory_content_code_from_item(resolved.item) == 218 + i && osrs_inventory_content_code_from_raw_osrs_id(mappings[i].raw) == 218 + i);
        } else {
            int global_aliases = 0;
            for (int j = 0; j < NUM_ITEMS; j++) global_aliases += ITEM_DATABASE[j].item_id == mappings[i].raw;
            CHECK("raw aliases remain local to the forecast boundary", global_aliases == 0);
        }
    }
    CHECK("empty equipment remains distinct from unsupported mechanics", la_item_of(-1).kind == LA_ITEM_EMPTY && la_item_of(-1).item == ITEM_NONE && la_item_of(11665).kind == LA_ITEM_UNSUPPORTED);
    gear.armor[6] = 31097;
    CHECK("Avernic treads preserve official magic damage", gs_item(31097)->has_stats && gs_item(31097)->mdmg == 2 && gs_item(31097)->mdmg == ITEM_DATABASE[ITEM_AVERNIC_TREADS].magic_damage);
    CHECK("verified Avernic treads use their existing physical item", la_item_of(31097).kind == LA_ITEM_MAPPED && la_item_of(31097).item == ITEM_AVERNIC_TREADS && la_gear_status(gear).status == LA_GEAR_SUPPORTED);
    gear.armor[6] = 11840;
    gear.weapon = 20997; gear.shield = -1;
    CHECK("mapped bow without a forecast ammo prior is unavailable", la_gear_status(gear).status == LA_GEAR_AMMO_UNSUPPORTED);
    gear.weapon = gear.shield = -1;
    CHECK("observed empty slots are supported", la_gear_status(gear).status == LA_GEAR_SUPPORTED);
    FILE* stream = tmpfile();
    assert(stream);
    fputs("LA 1 895\nO 99 90 118 118 118 99 100 0 0 0 0 27 0 -1 0 0 0 1 0 1 4 1 4 27690 0 0 1 OBSERVED -6 -6\nEND\n", stream);
    fputs("LA 2 895\nO 99 90 118 118 118 99 100 0 0 0 0 27 0 -1 0 0 0 1 0 1 4 1 4 -1 0 0 1 OBSERVED -6 -6\nSHIELD OBSERVED -1\nARMOR OBSERVED -1 -1 -1 -1 -1 -1 -1\nEND\n", stream);
    rewind(stream);
    Request request;
    read_request(stream, &request);
    CHECK("legacy request has explicit unknown gear", request.id == 1 && la_gear_status(request.belief.gear).status == LA_GEAR_UNKNOWN);
    read_request(stream, &request);
    CHECK("known unarmed request never inherits whip", request.id == 2 && request.belief.gear.weapon == -1 && request.belief.gear.shield == -1 && la_gear_status(request.belief.gear).status == LA_GEAR_SUPPORTED);
    fclose(stream);
    static TapeTick tick;
    tick.tick = 895; tick.hp = 99; tick.attack = tick.strength = tick.defence = 118; tick.magic = 99; tick.prayer = 90; tick.spec_us = 100;
    tick.bar_ratio = tick.bar_now_ratio = 15; tick.bar_scale = tick.bar_now_scale = 30; tick.last_bar_hit = 895;
    strcpy(tick.overhead, "NONE");
    for (int i = 0; i < TAPE_WORN; i++) tick.worn[i] = -1;
    for (int i = 0; i < TAPE_BAG; i++) tick.bag[i] = -1;
    tick.worn[3] = 27690;
    static TapeAnim attack = {894, SIDE_HIM, 390};
    static Observer observed;
    observed.inf.local_timer = (GsTimerState){.seen = 1, .earliest = 899, .latest = 899};
    observed.inf.c.opponent_gear = (GsObservedGear){GS_GEAR_COMPOSITION_OBSERVED, 27690, 22322, {10828, 21295, 1725, 10551, 4087, 7462, 11840}};
    static Hist history;
    history = hist_start();
    Tape prefix = {.lo = 895, .hi = 895, .ticks = &tick, .anims = &attack, .anim_count = 1, .observer_series = &observed, .hist_series = &history, .kit_count = 1};
    prefix.kit_anims[0][0] = 390; prefix.kit_anims[0][1] = 11;
    Tape future = prefix;
    future.kit_anims[1][0] = 11275; future.kit_anims[1][1] = 1; future.kit_count = 2;
    static Sim sim;
    sim_init(&sim);
    static RiskfightState worlds[2];
    LaBelief belief = {.hp_low = 50, .hp_high = 50, .energy_low = 100, .energy_high = 100, .vengeance = GS_VENGEANCE_DOWN, .pid = GS_PID_LOCAL,
        .gear = {GS_GEAR_COMPOSITION_OBSERVED, 27690, 8850, {10828, 21295, 1725, 10551, 4087, 7462, 11840}}};
    Tape* sources[2] = {&prefix, &future};
    uint32_t end_rng[2];
    for (int arm = 0; arm < 2; arm++) {
        g_tape = sources[arm];
        uint32_t seed = 7;
        RiskfightSnapshot snapshot;
        tape_snapshot(sources[arm], 895, &DEFAULT_PRIORS, &seed, &snapshot);
        CHECK("future kit changes privileged seed main", snapshot.loadout[1].equipment[GEAR_SLOT_WEAPON] == (arm == 0 ? ITEM_DRAGON_SCIMITAR : ITEM_VOIDWAKER));
        riskfight_load_state(&sim.state, &sim.context, &snapshot);
        uint32_t rng = 12345;
        Hist h;
        la_world(&sim.state, &history, &belief, 895, &rng, &worlds[arm], &h);
        end_rng[arm] = rng;
        Player* p = &worlds[arm].env.players[1];
        CHECK("forecast uses observed current weapon and Rune defender", p->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER && p->equipped[GEAR_SLOT_SHIELD] == ITEM_RUNE_DEFENDER);
        EquipmentBonuses bonuses;
        osrs_sum_equipment_bonuses(p->equipped, &bonuses);
        CHECK("forecast equipment refreshes derived bonuses", p->slot_cached_bonuses.melee_strength == bonuses.melee_strength && p->slot_cached_bonuses.attack_speed == bonuses.attack_speed);
        uint8_t shield = p->equipped[GEAR_SLOT_SHIELD];
        p->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        EquipmentBonuses without_defender;
        osrs_sum_equipment_bonuses(p->equipped, &without_defender);
        p->equipped[GEAR_SLOT_SHIELD] = shield;
        CHECK("Rune defender seeded bonuses include attack, defence and strength", bonuses.attack_stab - without_defender.attack_stab == 20 && bonuses.attack_slash - without_defender.attack_slash == 19 && bonuses.attack_crush - without_defender.attack_crush == 18 && bonuses.attack_ranged - without_defender.attack_ranged == -2 && bonuses.attack_magic - without_defender.attack_magic == -3 && bonuses.defence_stab - without_defender.defence_stab == 20 && bonuses.defence_slash - without_defender.defence_slash == 19 && bonuses.defence_crush - without_defender.defence_crush == 18 && bonuses.defence_ranged - without_defender.defence_ranged == -2 && bonuses.defence_magic - without_defender.defence_magic == -3 && bonuses.melee_strength - without_defender.melee_strength == 5);
    }
    CHECK("future kit does not change forecast weapon world", la_fingerprint(&worlds[0]) == la_fingerprint(&worlds[1]));
    CHECK("paired worlds consume identical randomness", end_rng[0] == end_rng[1]);
    LaBelief other = belief;
    other.gear.weapon = 4587;
    CHECK("weapon changes invalidate forecast cache", la_key(&belief, &sim.state.env.players[0], 895, 100) != la_key(&other, &sim.state.env.players[0], 895, 100));
    other = belief; other.gear.shield = 12954;
    CHECK("shield changes invalidate forecast cache", la_key(&belief, &sim.state.env.players[0], 895, 100) != la_key(&other, &sim.state.env.players[0], 895, 100));
    other = belief; other.gear.armor[4] = 2497;
    CHECK("observed leg changes invalidate forecast cache", la_key(&belief, &sim.state.env.players[0], 895, 100) != la_key(&other, &sim.state.env.players[0], 895, 100));
    worlds[1].env.players[1].equipped[GEAR_SLOT_LEGS] = ITEM_BLACK_DHIDE_CHAPS;
    CHECK("fingerprint includes equipped armor", la_fingerprint(&worlds[0]) != la_fingerprint(&worlds[1]));
    worlds[1].env.players[1].equipped[GEAR_SLOT_SHIELD] = ITEM_DRAGON_DEFENDER;
    CHECK("fingerprint includes equipped shield", la_fingerprint(&worlds[0]) != la_fingerprint(&worlds[1]));
    LaBelief bow = belief;
    bow.gear = (GsObservedGear){GS_GEAR_COMPOSITION_OBSERVED, 12788, -1, {-1, -1, -1, -1, -1, -1, -1}};
    future = prefix;
    future.kit_anims[1][0] = 426; future.kit_anims[1][1] = 1; future.kit_count = 2;
    for (int arm = 0; arm < 2; arm++) {
        g_tape = sources[arm];
        uint32_t seed = 7;
        RiskfightSnapshot snapshot;
        tape_snapshot(sources[arm], 895, &DEFAULT_PRIORS, &seed, &snapshot);
        CHECK("future426 changes privileged ammo seed", snapshot.loadout[1].equipment[GEAR_SLOT_AMMO] == (arm == 0 ? ITEM_NONE : ITEM_DRAGON_ARROWS));
        riskfight_load_state(&sim.state, &sim.context, &snapshot);
        uint32_t rng = 12345;
        Hist h;
        la_world(&sim.state, &history, &bow, 895, &rng, &worlds[arm], &h);
        end_rng[arm] = rng;
        Player* p = &worlds[arm].env.players[1];
        CHECK("observedMSB uses existing55 strength hidden ammo prior", p->equipped[GEAR_SLOT_WEAPON] == ITEM_MAGIC_SHORTBOW_I && p->equipped[GEAR_SLOT_AMMO] == ITEM_AMETHYST_ARROW);
        CHECK("refreshed bow weapon drives ranged attack style", get_slot_weapon_attack_style(p) == ATTACK_STYLE_RANGED && p->fight_style == FIGHT_STYLE_ACCURATE);
    }
    CHECK("future426 cannot change bow forecast world", la_fingerprint(&worlds[0]) == la_fingerprint(&worlds[1]));
    CHECK("future426 cannot change ranged max hit", calculate_max_hit(&worlds[0].env.players[1], ATTACK_STYLE_RANGED, 1.0f, 30) == calculate_max_hit(&worlds[1].env.players[1], ATTACK_STYLE_RANGED, 1.0f, 30));
    CHECK("ammo repair does not perturb world randomness", end_rng[0] == end_rng[1]);
    g_tape = &future;
    uint32_t seed = 7;
    RiskfightSnapshot snapshot;
    tape_snapshot(&future, 895, &DEFAULT_PRIORS, &seed, &snapshot);
    riskfight_load_state(&sim.state, &sim.context, &snapshot);
    uint32_t rng = 12345;
    Hist h;
    la_world(&sim.state, &history, &belief, 895, &rng, &worlds[0], &h);
    CHECK("observed melee strips futureKIT ammo", worlds[0].env.players[1].equipped[GEAR_SLOT_AMMO] == ITEM_NONE);
    sim.state.env.players[1].equipped[GEAR_SLOT_WEAPON] = ITEM_DRAGON_SCIMITAR;
    FILE* dumped = tmpfile();
    assert(dumped);
    la_dump_request(dumped, &sim, &belief, 895, &(LaFeasible){0}, (GsStakes){1, 1}, &(LaScript){0});
    rewind(dumped);
    read_request(dumped, &request);
    CHECK("actual dump transports observed gear rather than privileged seed", request.his_weapon == 27690 && request.belief.gear.source == GS_GEAR_COMPOSITION_OBSERVED && request.belief.gear.shield == 8850);
    fclose(dumped);
    CHECK("request transports every observed armor slot", !memcmp(request.belief.gear.armor, belief.gear.armor, sizeof(belief.gear.armor)));
    LaBelief dhide = belief;
    dhide.gear.armor[4] = 2497;
    rng = 12345;
    la_world(&sim.state, &history, &dhide, 895, &rng, &worlds[1], &h);
    Player* actual = &worlds[1].env.players[1];
    CHECK("Oblv recorded black dhide legs replace copied Rune legs", actual->equipped[GEAR_SLOT_LEGS] == ITEM_BLACK_DHIDE_CHAPS);
    EquipmentBonuses dragon_legs, dhide_legs;
    osrs_sum_equipment_bonuses(worlds[0].env.players[1].equipped, &dragon_legs);
    osrs_sum_equipment_bonuses(actual->equipped, &dhide_legs);
    CHECK("recorded armor controls actual defense", dragon_legs.defence_slash - dhide_legs.defence_slash == 46 && dragon_legs.defence_magic - dhide_legs.defence_magic == -27);
    int carried = -1;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (osrs_inventory_cell_is_empty(&actual->inventory_cells[slot])) { carried = slot; break; }
    assert(carried >= 0);
    actual->inventory_cells[carried] = osrs_inventory_cell_from_item(ITEM_DRAGON_SCIMITAR);
    osrs_equip_from_cell(actual, actual->inventory_cells, carried);
    CHECK("weapon swaps preserve observed armor bonuses after refresh", actual->equipped[GEAR_SLOT_LEGS] == ITEM_BLACK_DHIDE_CHAPS && actual->slot_cached_bonuses.slash_defence == dhide_legs.defence_slash);
    LaBelief aliases = belief;
    aliases.gear.armor[1] = 24133;
    aliases.gear.armor[2] = 1704;
    aliases.gear.armor[3] = 28069;
    aliases.gear.armor[6] = 28055;
    GsObservedGear raw_gear = aliases.gear;
    rng = 12345;
    la_world(&sim.state, &history, &aliases, 895, &rng, &worlds[0], &h);
    actual = &worlds[0].env.players[1];
    CHECK("forecast aliases seed canonical equipment through the shared refresh", actual->equipped[GEAR_SLOT_CAPE] == ITEM_INFERNAL_CAPE && actual->equipped[GEAR_SLOT_NECK] == ITEM_GLORY && actual->equipped[GEAR_SLOT_BODY] == ITEM_FIGHTER_TORSO && actual->equipped[GEAR_SLOT_FEET] == ITEM_DRAGON_BOOTS);
    CHECK("forecast resolution preserves observed raw provenance", !memcmp(&raw_gear, &aliases.gear, sizeof(raw_gear)));
    LaBelief canonical = aliases;
    canonical.gear.armor[1] = 21295;
    canonical.gear.armor[2] = 1712;
    canonical.gear.armor[3] = 10551;
    canonical.gear.armor[6] = 11840;
    CHECK("raw alias changes remain explicit cache inputs", la_key(&aliases, &sim.state.env.players[0], 895, 100) != la_key(&canonical, &sim.state.env.players[0], 895, 100));
    rng = 12345;
    la_world(&sim.state, &history, &canonical, 895, &rng, &worlds[1], &h);
    CHECK("equivalent aliases produce identical physical worlds", la_fingerprint(&worlds[0]) == la_fingerprint(&worlds[1]));
    dumped = tmpfile();
    assert(dumped);
    la_dump_request(dumped, &sim, &aliases, 895, &(LaFeasible){0}, (GsStakes){1, 1}, &(LaScript){0});
    rewind(dumped);
    read_request(dumped, &request);
    fclose(dumped);
    CHECK("request transport retains actual raw aliases", !memcmp(&request.belief.gear, &aliases.gear, sizeof(aliases.gear)));
    g_tape = NULL;
    return osrs_test_summary();
}
