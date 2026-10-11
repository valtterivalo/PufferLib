#include "../gearswapper/gs_wire.h"

static GsPendingLaunch launch(int tick, int lands, int least, int most, int arrows) {
    GsPendingLaunch result = {.tick = tick};
    for (int p = 0; p < 3; p++) result.by_pid[p] = (GsSwing){lands, least, most, 1, arrows};
    return result;
}

static GsLocalLaunch local_launch(int tick, int lands, int most) {
    GsLocalLaunch result = {.tick = tick};
    for (int p = 0; p < 3; p++) result.by_pid[p] = (GsSwing){lands, 0, most, 0.7, 1};
    return result;
}

static void outgoing_and_reflections(void) {
    GsInfer s;
    gs_infer_init(&s);
    s.timed = s.tracker_current = s.c.local_interacting = 1;
    s.pid.owner = GS_PID_LOCAL;
    s.local_launches[s.local_launch_count++] = local_launch(100, 104, 60);
    s.local_launches[s.local_launch_count++] = local_launch(102, 105, 70);
    gs_on_hitpoints_gain(&s, 102, 66, 100); /* 44 damage, correlated to launch tick 102. */
    assert(s.local_launch_count == 2 && !s.local_launches[0].exact);
    assert(s.local_launches[1].exact && s.local_launches[1].by_pid[GS_PID_LOCAL].least == 44);
    assert(gs_pending_damage_known(&s, 102, GS_PID_LOCAL));
    gs_on_hitsplat(&s, 103, 1, 44, GS_ATTACK_HITSPLAT_ME, 1, 1);
    assert(s.local_launch_count == 2 && s.landed_owned); /* Observed ownership does not retire the outgoing queue. */
    gs_local_pending_settle(&s, 104);
    assert(s.local_launch_count == 1 && gs_local_pending_known(&s, 104));
    gs_on_hitsplat(&s, 105, 1, 44, GS_ATTACK_HITSPLAT_ME, 1, 1);
    gs_on_overhead_text(&s, 105, 1);
    assert(s.reflection_count == 1 && s.reflection_swings[0].lands == 106);
    assert(s.reflection_swings[0].least == 33 && gs_reflection_known(&s, 105, GS_PID_LOCAL));
    assert(!gs_reflection_known(&s, 105, GS_PID_UNKNOWN));
    assert(!gs_reflection_known(&s, 500, GS_PID_UNKNOWN)); /* A future query is not settlement. */
    gs_on_hitsplat(&s, 105, 1, 12, GS_ATTACK_HITSPLAT_ME, 1, 1);
    assert(s.reflection_count == 0 && !gs_reflection_known(&s, 105, GS_PID_LOCAL));
    assert(!gs_reflection_known(&s, 500, GS_PID_LOCAL));
    gs_local_pending_settle(&s, 105);
    assert(!gs_reflection_known(&s, 105, GS_PID_LOCAL));
    gs_local_pending_settle(&s, 106);
    assert(gs_pending_damage_known(&s, 106, GS_PID_LOCAL));
    gs_eater_reset(&s);
    assert(gs_pending_damage_known(&s, 105, GS_PID_LOCAL));

    s.local_launches[s.local_launch_count++] = local_launch(200, 200, 50);
    gs_on_hitpoints_gain(&s, 200, 30, 100);
    gs_local_pending_settle(&s, 200);
    assert(s.local_launch_count == 0 && gs_local_pending_known(&s, 200));
    gs_eater_reset(&s);
    gs_on_hitpoints_gain(&s, 300, 30, 100);
    assert(!gs_local_pending_known(&s, 300));
    gs_local_pending_settle(&s, 301);
    assert(!gs_local_pending_known(&s, 400)); /* Unmatched XP cannot expire into known-empty. */
    gs_eater_reset(&s);
    s.local_launches[s.local_launch_count++] = local_launch(400, 403, 50);
    s.local_launches[s.local_launch_count++] = local_launch(400, 403, 70);
    gs_on_hitpoints_gain(&s, 400, 30, 100);
    assert(!gs_local_pending_known(&s, 400));
    assert(!s.local_launches[0].exact && !s.local_launches[1].exact);
    gs_local_pending_settle(&s, 500);
    assert(!gs_pending_damage_known(&s, 500, GS_PID_LOCAL));
    gs_eater_reset(&s);
    s.local_launches[s.local_launch_count++] = local_launch(600, 602, 50);
    gs_on_hitpoints_gain(&s, 600, 30, 100);
    gs_on_hitpoints_gain(&s, 600, 15, 100);
    assert(!gs_local_pending_known(&s, 600));
    gs_eater_reset(&s);
    s.pid.owner = GS_PID_UNKNOWN;
    gs_on_overhead_text(&s, 700, 0);
    assert(s.echo_unknown_lands == 701);
    assert(!gs_pending_damage_known(&s, 800, GS_PID_LOCAL)); /* Querying ahead is not a fresh observation. */
    assert(!gs_reflection_known(&s, 800, GS_PID_LOCAL));
    gs_local_pending_settle(&s, 700);
    assert(!gs_pending_damage_known(&s, 700, GS_PID_LOCAL));
    gs_local_pending_settle(&s, 701);
    assert(gs_pending_damage_known(&s, 701, GS_PID_LOCAL));
    s.pid.owner = GS_PID_LOCAL;
    gs_unknown_applied_echo(&s, 710, 0);
    assert(s.echo_unknown_lands == 711);
    gs_local_pending_settle(&s, 710);
    assert(!gs_pending_damage_known(&s, 710, GS_PID_LOCAL));
    gs_local_pending_settle(&s, 711);
    assert(gs_pending_damage_known(&s, 711, GS_PID_LOCAL));
    gs_unknown_applied_echo(&s, 720, 1);
    assert(s.echo_unknown_lands == 720);
    gs_local_pending_settle(&s, 720);
    assert(gs_pending_damage_known(&s, 720, GS_PID_LOCAL));
    gs_on_overhead_text(&s, 730, 1); /* Unowned opponent proc has the same finite echo clock. */
    assert(s.vengeance_proc_lands == 731);
    assert(!gs_reflection_known(&s, 800, GS_PID_LOCAL));
    gs_local_pending_settle(&s, 730);
    assert(!gs_pending_damage_known(&s, 730, GS_PID_LOCAL));
    gs_local_pending_settle(&s, 731);
    assert(gs_pending_damage_known(&s, 731, GS_PID_LOCAL));
    s.pending_launch_unknown = 1;
    gs_unknown_applied_echo(&s, 740, 0);
    gs_local_pending_settle(&s, 741);
    assert(gs_reflection_known(&s, 741, GS_PID_LOCAL));
    assert(!gs_pending_damage_known(&s, 741, GS_PID_LOCAL)); /* Unsupported future damage stays sticky. */
    gs_eater_reset(&s);
    s.pid.owner = GS_PID_UNKNOWN;
    s.c.worn[GS_WORN_RING] = GS_ITEM_RING_OF_RECOIL;
    s.recoil_charges = 1;
    gs_on_hitsplat(&s, 750, 0, 10, GS_ATTACK_HITSPLAT_ME, 1, 1);
    assert(s.recoil_charges == 0 && s.echo_unknown_lands == 751);
    assert(!gs_reflection_known(&s, 800, GS_PID_LOCAL)); /* The last ring charge can still echo. */
    gs_local_pending_settle(&s, 750);
    assert(!gs_pending_damage_known(&s, 750, GS_PID_LOCAL));
    gs_local_pending_settle(&s, 751);
    assert(gs_pending_damage_known(&s, 751, GS_PID_LOCAL));
    s.c.worn[GS_WORN_RING] = 0;
    s.pid.owner = GS_PID_LOCAL;
    gs_eater_reset(&s);
    assert(gs_pending_damage_known(&s, 800, GS_PID_LOCAL));
    s.c.worn[GS_WORN_WEAPON] = GS_ITEM_VOIDWAKER;
    s.c.attack = s.c.strength = s.c.ranged = s.c.magic = 99;
    gs_on_hitpoints_gain(&s, 900, 30, 100);
    assert(!gs_local_pending_known(&s, 900));
    gs_eater_local_animated(&s, GS_KIND_ATTACK, -1, 900, GS_PID_LOCAL);
    assert(gs_local_pending_known(&s, 900) && s.local_launches[0].exact);
    gs_eater_reset(&s);
    int spec_animation = -1;
    for (size_t i = 0; i < sizeof(GS_ANIMATIONS) / sizeof(*GS_ANIMATIONS); i++)
        if (GS_ANIMATIONS[i].spec_cost == 50) { spec_animation = GS_ANIMATIONS[i].animation; break; }
    assert(spec_animation >= 0);
    gs_eater_local_animated(&s, GS_KIND_ATTACK, spec_animation, 1000, GS_PID_LOCAL);
    assert(gs_local_pending_known(&s, 1000) && s.local_launch_count == 1);
    assert(s.local_launches[0].by_pid[GS_PID_LOCAL].least > 0);
    assert(s.local_launches[0].by_pid[GS_PID_LOCAL].accuracy == 1);
    gs_eater_reset(&s);
    s.local_launches[s.local_launch_count++] = local_launch(1100, 1102, 50);
    gs_on_hitpoints_gain(&s, 1100, 60, 100);
    gs_on_hitsplat(&s, 1102, 1, 40, GS_ATTACK_HITSPLAT_ME, 1, 1);
    gs_on_overhead_text(&s, 1102, 1);
    assert(s.reflection_count == 1);
    gs_on_hitsplat(&s, 1102, 0, 30, GS_ATTACK_HITSPLAT_ME, 1, 1);
    assert(s.reflection_count == 1); /* Ambiguous early raw splat cannot retire an echo. */
    gs_local_pending_settle(&s, 1103);
    assert(s.reflection_count == 0);
    gs_on_hitsplat(&s, 1103, 0, 30, GS_ATTACK_HITSPLAT_ME, 1, 1);
    assert(s.reflection_count == 0); /* An observed echo cannot create another reflection. */
    gs_infer_clear(&s);
}

static void frozen_local_distributions(void) {
    GsInfer s;
    gs_infer_init(&s);
    s.timed = s.tracker_current = s.c.local_interacting = 1;
    s.pid.owner = GS_PID_LOCAL;
    s.c.attack = s.c.strength = s.c.ranged = s.c.magic = 99;
    s.c.worn[GS_WORN_WEAPON] = GS_ITEM_VOIDWAKER;
    int spec_animation = -1;
    for (size_t i = 0; i < sizeof(GS_ANIMATIONS) / sizeof(*GS_ANIMATIONS); i++)
        if (GS_ANIMATIONS[i].spec_cost == 50) { spec_animation = GS_ANIMATIONS[i].animation; break; }
    assert(spec_animation >= 0);
    int normal = gs_local_normal(&s.c, GS_ITEM_VOIDWAKER);
    s.c.overhead = GS_HEAD_MAGIC;
    gs_eater_local_animated(&s, GS_KIND_ATTACK, spec_animation, 100, GS_PID_LOCAL);
    assert(gs_local_pending_known(&s, 100) && s.local_launch_count == 1);
    for (int p = 0; p < 3; p++) {
        GsSwing swing = s.local_launches[0].by_pid[p];
        assert(swing.least == (int)((normal / 2) * 0.6));
        assert(swing.most == (int)((normal * 3 / 2) * 0.6));
        assert(swing.accuracy == 1 && swing.arrows == 1);
        assert(swing.lands == s.local_launches[0].by_pid[GS_PID_LOCAL].lands);
    }
    int protected_top = s.local_launches[0].by_pid[GS_PID_LOCAL].most;
    s.c.overhead = GS_HEAD_MELEE;
    assert(s.local_launches[0].by_pid[GS_PID_LOCAL].most == protected_top);
    gs_on_hitpoints_gain(&s, 100, 66, 100);
    for (int p = 0; p < 3; p++)
        assert(s.local_launches[0].by_pid[p].least == 44 && s.local_launches[0].by_pid[p].most == 44);
    assert(s.local_launches[0].exact && gs_local_pending_known(&s, 100));

    gs_eater_reset(&s);
    s.c.worn[GS_WORN_WEAPON] = GS_ITEM_GRANITE_MAUL_PLUS;
    GsOwn own = {.maul_id = GS_ITEM_GRANITE_MAUL_PLUS};
    int maul_most;
    double maul_accuracy;
    assert(gs_maul(&s, &own, &maul_most, &maul_accuracy));
    gs_eater_local_animated(&s, GS_KIND_INSTANT_SPEC, GS_GRANITE_MAUL_SPEC_ANIMATION, 200, GS_PID_LOCAL);
    assert(gs_local_pending_known(&s, 200) && s.local_launch_count == 1);
    for (int p = 0; p < 3; p++) {
        GsSwing swing = s.local_launches[0].by_pid[p];
        assert(swing.least == 0 && swing.most == maul_most && swing.arrows == 1);
        assert(swing.accuracy == maul_accuracy && swing.accuracy > 0 && swing.accuracy < 1);
    }
    s.c.overhead = GS_HEAD_NONE;
    assert(s.local_launches[0].by_pid[GS_PID_LOCAL].most == maul_most);

    const int unsupported[] = {GS_ITEM_ARMADYL_GODSWORD, 13652, 1215, 11235};
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(*unsupported); i++) {
        gs_eater_reset(&s);
        s.c.worn[GS_WORN_WEAPON] = unsupported[i];
        gs_eater_local_animated(&s, GS_KIND_ATTACK, spec_animation, 300, GS_PID_LOCAL);
        assert(!gs_local_pending_known(&s, 300) && s.local_launch_count == 0);
        gs_local_pending_settle(&s, 400);
        assert(!gs_local_pending_known(&s, 400));
    }

    for (int damage = 0; damage <= 4; damage++) {
        gs_eater_reset(&s);
        s.local_launches[s.local_launch_count++] = local_launch(500, 502, 50);
        gs_on_hitpoints_gain(&s, 500, (3 * damage + 1) / 2, 100);
        gs_on_hitsplat(&s, 502, 1, damage, GS_ATTACK_HITSPLAT_ME, 1, 1);
        assert(s.landed_owned);
        gs_on_overhead_text(&s, 502, 1);
        assert(s.reflection_count == 1);
        int expected = damage > 0 ? imax(1, damage * 3 / 4) : 0;
        assert(s.reflection_swings[0].least == expected && s.reflection_swings[0].most == expected);
    }
    gs_infer_clear(&s);
}

static void frozen_observed_launch_pid(void) {
    GsInfer s;
    gs_infer_init(&s);
    s.timed = s.tracker_current = s.c.local_interacting = 1;
    s.pid.owner = GS_PID_LOCAL;
    s.c.oweapon = GS_ITEM_VOIDWAKER;
    s.c.opponent_gear = (GsObservedGear){.source = GS_GEAR_COMPOSITION_OBSERVED, .weapon = GS_ITEM_VOIDWAKER, .shield = -1};
    for (int i = 0; i < GS_ARMOR_SLOTS; i++) s.c.opponent_gear.armor[i] = -1;
    gs_eater_opponent_animated(&s, GS_KIND_ATTACK, 11275, 100);
    assert(s.pending_launch_count == 1 && !s.pending_launch_unknown);
    int lands = s.pending_launches[0].by_pid[GS_PID_LOCAL].lands;
    s.pid.owner = GS_PID_OPPONENT;
    for (int p = 0; p < 3; p++) assert(s.pending_launches[0].by_pid[p].lands == lands);
    gs_infer_clear(&s);
}

static void canonical_and_extended_pending_scopes(void) {
    GsInfer s;
    gs_infer_init(&s);
    s.timed = s.tracker_current = s.c.local_interacting = 1;
    s.pid.owner = GS_PID_LOCAL;
    s.recoil = GS_RECOIL_SEEN;
    gs_on_hitsplat(&s, 100, 1, 20, GS_ATTACK_HITSPLAT_ME, 1, 1);
    assert(s.echo_unknown && !s.pending_launch_count && !s.reflection_count);
    assert(gs_pending_attacks_known(&s, 100, GS_PID_LOCAL));
    assert(!gs_pending_damage_known(&s, 100, GS_PID_LOCAL));
    assert(!gs_pending_damage_known(&s, 500, GS_PID_LOCAL));
    s.pending_launch_unknown = 1;
    assert(!gs_pending_attacks_known(&s, 100, GS_PID_LOCAL));
    assert(!gs_pending_damage_known(&s, 100, GS_PID_LOCAL));
    s.pending_launch_unknown = 0;
    gs_on_overhead_text(&s, 100, 1);
    assert(s.reflection_count == 1);
    assert(gs_pending_attacks_known(&s, 100, GS_PID_LOCAL));
    assert(!gs_pending_attacks_known(&s, 100, GS_PID_UNKNOWN));
    gs_on_hitsplat(&s, 100, 1, 12, GS_ATTACK_HITSPLAT_ME, 1, 1);
    assert(!gs_pending_attacks_known(&s, 100, GS_PID_LOCAL));
    gs_local_pending_settle(&s, 101);
    assert(gs_pending_attacks_known(&s, 101, GS_PID_LOCAL));
    assert(gs_pending_damage_known(&s, 101, GS_PID_LOCAL));
    gs_eater_reset(&s);
    s.pid.owner = GS_PID_OPPONENT;
    gs_on_hitsplat(&s, 200, 1, 20, GS_ATTACK_HITSPLAT_ME, 1, 1);
    gs_on_hitsplat(&s, 200, 1, 12, GS_ATTACK_HITSPLAT_ME, 1, 1);
    gs_on_overhead_text(&s, 200, 1);
    assert(s.landed_count == 2 && !s.reflection_count);
    gs_local_pending_settle(&s, 200);
    assert(!gs_pending_attacks_known(&s, 200, GS_PID_OPPONENT));
    assert(!gs_pending_attacks_known(&s, 500, GS_PID_OPPONENT));
    assert(!gs_pending_damage_known(&s, 200, GS_PID_OPPONENT));
    gs_local_pending_settle(&s, 201);
    assert(gs_pending_attacks_known(&s, 201, GS_PID_OPPONENT));
    assert(gs_pending_damage_known(&s, 201, GS_PID_OPPONENT));
    gs_infer_clear(&s);
}

static void oracle_annotations_do_not_replay_causal_ticks(void) {
    GsInfer s;
    gs_infer_init(&s);
    GsWire wire = {0};
    assert(gs_wire_apply(&s, &wire, "T 200\n") == 'T');
    const char* headers[] = {
        "SS 76 99 121 201 DOWN OPPONENT 0.11899791231732777 UP -2147483648 0 200 100\n",
        "S 200 76 OPPONENT 1 MELEE 42 42 4 1 1 IDLE 0.01 0.11899791231732777 0.8543016520722135 -2147483648 UP SEEN55\n"
    };
    for (int i = 0; i < 2; i++) {
        GsInfer before = s;
        assert(gs_wire_apply(&s, &wire, headers[i]) == 0 && wire.oracle_block);
        assert(gs_wire_apply(&s, &wire, "T 99 1 1 1 2\n") == 0);
        assert(!memcmp(&s, &before, sizeof(s)));
        assert(gs_wire_apply(&s, &wire, "END\n") == 0 && !wire.oracle_block);
        gs_wire_finish(&wire);
    }
    assert(gs_wire_apply(&s, &wire, "T 201\n") == 'T' && s.tick == 201);
    gs_wire_finish(&wire);
    gs_infer_clear(&s);
}

int main(void) {
    gs_register_java_items();
    oracle_annotations_do_not_replay_causal_ticks();
    outgoing_and_reflections();
    frozen_local_distributions();
    frozen_observed_launch_pid();
    canonical_and_extended_pending_scopes();
    GsInfer infer;
    gs_infer_init(&infer);
    infer.pending_launches[infer.pending_launch_count++] = launch(100, 104, 30, 30, 2);
    infer.pending_launches[infer.pending_launch_count++] = launch(102, 105, 20, 20, 1);
    gs_pending_settle(&infer, 103);
    assert(infer.pending_launch_count == 2);
    gs_pending_landed(&infer, 104, 30, GS_PID_LOCAL);
    assert(!infer.pending_launch_unknown);
    assert(infer.pending_launches[0].by_pid[GS_PID_LOCAL].lands == 105);
    assert(infer.pending_launches[0].by_pid[GS_PID_LOCAL].arrows == 1);
    assert(infer.pending_launches[1].by_pid[GS_PID_LOCAL].arrows == 1);
    gs_pending_landed(&infer, 105, 20, GS_PID_LOCAL);
    assert(infer.pending_launches[0].by_pid[GS_PID_LOCAL].arrows == 1);
    assert(infer.pending_launches[1].by_pid[GS_PID_LOCAL].arrows == 0);
    gs_pending_settle(&infer, 105);
    assert(infer.pending_launch_count == 0);

    infer.pending_launches[infer.pending_launch_count++] = launch(106, 108, 0, 30, 1);
    infer.pending_launches[infer.pending_launch_count++] = launch(107, 108, 0, 30, 1);
    gs_pending_landed(&infer, 108, 20, GS_PID_LOCAL);
    assert(infer.pending_launch_unknown);
    assert(infer.pending_launch_count == 2);
    gs_eater_reset(&infer);
    assert(!infer.pending_launch_unknown && infer.pending_launch_count == 0);

    Arena arena = arena_make((size_t)1 << 20);
    GsEatSituation situation = {0};
    situation.tick = 100;
    situation.pid = GS_PID_LOCAL;
    situation.hitpoints = 50;
    situation.swing_count = 2;
    situation.swings[0] = (GsSwing){102, 30, 30, 1, 1};
    situation.swings[1] = (GsSwing){102, 30, 30, 1, 1};
    GsVec heals = vec_make(&arena, sizeof(GsHeal));
    GsVec scenarios = eat_scenarios(&arena, &situation, 102, GS_SCHEDULE_EARLIEST, &heals);
    assert(scenarios.count == 1);
    GsScenario scenario = VEC_AT(GsScenario, scenarios, 0);
    assert(scenario.count == 2);
    assert(eat_odds(&arena, scenario.hits, 1, &heals, 0, 50, GS_PID_LOCAL, GS_NONE).risk == 0);
    assert(eat_odds(&arena, scenario.hits, 2, &heals, 0, 50, GS_PID_LOCAL, GS_NONE).risk == 1);
    free(arena.base);
    return 0;
}
