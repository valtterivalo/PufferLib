#define main calibrator_main
#include "calibrate_riskfight.c"
#undef main
#define main server_main
#include "gs_lookahead_server.c"
#undef main
#include "osrs_test_check.h"

int main(void) {
    GsInfer infer = {0};
    GsWire wire = {0};
    const char* legacy = "S 394 121 118 118 119 99 99 90 0 1000 0 0 3162 3503 -1 10828 21295 1725 27690 10551 22322 -1 4087 -1 7462 11840 -1 2550 -1 24592 24592 24595 24595 24598 24595 24595 24595 24598 24592 24595 24595 24592 6685 24592 6685 -1 24592 24592 24592 24225 12695 24592 24592 24592 24592 24621 8013 1 0 -1 -1";
    const char* tracked = "S 394 121 118 118 119 99 99 90 0 1000 0 0 3162 3503 -1 10828 21295 1725 27690 10551 22322 -1 4087 -1 7462 11840 -1 2550 -1 24592 24592 24595 24595 24598 24595 24595 24595 24598 24592 24595 24595 24592 6685 24592 6685 -1 24592 24592 24592 24225 12695 24592 24592 24592 24592 24621 8013 2 0 -1 1 0 1 0 0 0 15 30 27690 NONE 6 0 0 0 29738 0 24370";
    gs_wire_apply(&infer, &wire, legacy);
    CHECK("old client states have unknown gear provenance", infer.c.opponent_gear.source == GS_GEAR_UNKNOWN);
    gs_wire_apply(&infer, &wire, tracked);
    GsClient unchanged = infer.c;
    char state[4096];
    snprintf(state, sizeof(state), "%s ANIMATION UNKNOWN GEAR OBSERVED 27690 22322", tracked);
    gs_wire_apply(&infer, &wire, state);
    CHECK("wire carries current observed raw equipment", infer.c.opponent_gear.source == GS_GEAR_OBSERVED && infer.c.opponent_gear.weapon == 27690 && infer.c.opponent_gear.shield == 22322);
    infer.c.opponent_gear = (GsObservedGear){0};
    CHECK("gear provenance preserves legacy analytical inputs", !memcmp(&infer.c, &unchanged, sizeof(unchanged)));
    snprintf(state, sizeof(state), "%s ANIMATION UNKNOWN GEAR OBSERVED -1 -1", tracked);
    gs_wire_apply(&infer, &wire, state);
    CHECK("wire preserves observed unarmed and empty shield", infer.c.opponent_gear.source == GS_GEAR_OBSERVED && infer.c.opponent_gear.weapon == -1 && infer.c.opponent_gear.shield == -1);
    gs_wire_apply(&infer, &wire, legacy);
    CHECK("legacy source clears prior observed gear", infer.c.opponent_gear.source == GS_GEAR_UNKNOWN);
    GsObservedGear gear = {GS_GEAR_UNKNOWN, 27690, 22322};
    CHECK("guessed numeric equipment is not observed", la_gear_status(gear) == LA_GEAR_UNKNOWN);
    gear.source = GS_GEAR_OBSERVED;
    CHECK("physical Voidwaker and Avernic are supported", la_gear_status(gear) == LA_GEAR_SUPPORTED);
    gear.weapon = 29796;
    CHECK("unknown weapon mechanics remain unavailable", la_gear_status(gear) == LA_GEAR_WEAPON_UNSUPPORTED);
    gear.weapon = 27690; gear.shield = 8850;
    CHECK("unknown shield mechanics remain unavailable", la_gear_status(gear) == LA_GEAR_SHIELD_UNSUPPORTED);
    gear.weapon = 20997; gear.shield = -1;
    CHECK("mapped bow without a forecast ammo prior is unavailable", la_gear_status(gear) == LA_GEAR_AMMO_UNSUPPORTED);
    gear.weapon = gear.shield = -1;
    CHECK("observed empty slots are supported", la_gear_status(gear) == LA_GEAR_SUPPORTED);
    FILE* stream = tmpfile();
    assert(stream);
    fputs("LA 1 895\nO 99 90 118 118 118 99 100 0 0 0 0 27 0 -1 0 0 0 1 0 1 4 1 4 27690 0 0 1 OBSERVED -6 -6\nEND\n", stream);
    fputs("LA 2 895\nO 99 90 118 118 118 99 100 0 0 0 0 27 0 -1 0 0 0 1 0 1 4 1 4 -1 0 0 1 OBSERVED -6 -6\nSHIELD OBSERVED -1\nEND\n", stream);
    rewind(stream);
    Request request;
    read_request(stream, &request);
    CHECK("legacy request has explicit unknown gear", request.id == 1 && la_gear_status(request.belief.gear) == LA_GEAR_UNKNOWN);
    read_request(stream, &request);
    CHECK("known unarmed request never inherits whip", request.id == 2 && request.belief.gear.weapon == -1 && request.belief.gear.shield == -1 && la_gear_status(request.belief.gear) == LA_GEAR_SUPPORTED);
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
    observed.inf.c.opponent_gear = (GsObservedGear){GS_GEAR_OBSERVED, 27690, 22322};
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
        .gear = {GS_GEAR_OBSERVED, 27690, 22322}};
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
        CHECK("forecast uses observed current weapon and shield", p->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER && p->equipped[GEAR_SLOT_SHIELD] == ITEM_AVERNIC_DEFENDER);
        EquipmentBonuses bonuses;
        osrs_sum_equipment_bonuses(p->equipped, &bonuses);
        CHECK("forecast equipment refreshes derived bonuses", p->slot_cached_bonuses.melee_strength == bonuses.melee_strength && p->slot_cached_bonuses.attack_speed == bonuses.attack_speed);
    }
    CHECK("future kit does not change forecast weapon world", la_fingerprint(&worlds[0]) == la_fingerprint(&worlds[1]));
    CHECK("paired worlds consume identical randomness", end_rng[0] == end_rng[1]);
    LaBelief other = belief;
    other.gear.weapon = 4587;
    CHECK("weapon changes invalidate forecast cache", la_key(&belief, &sim.state.env.players[0], 895, 100) != la_key(&other, &sim.state.env.players[0], 895, 100));
    other = belief; other.gear.shield = 12954;
    CHECK("shield changes invalidate forecast cache", la_key(&belief, &sim.state.env.players[0], 895, 100) != la_key(&other, &sim.state.env.players[0], 895, 100));
    worlds[1].env.players[1].equipped[GEAR_SLOT_SHIELD] = ITEM_DRAGON_DEFENDER;
    CHECK("fingerprint includes equipped shield", la_fingerprint(&worlds[0]) != la_fingerprint(&worlds[1]));
    LaBelief bow = belief;
    bow.gear = (GsObservedGear){GS_GEAR_OBSERVED, 12788, -1};
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
    CHECK("actual dump transports observed gear rather than privileged seed", request.his_weapon == 27690 && request.belief.gear.source == GS_GEAR_OBSERVED && request.belief.gear.shield == 22322);
    fclose(dumped);
    g_tape = NULL;
    return osrs_test_summary();
}
