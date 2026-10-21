/* Soulreaper axe stacks and Behead, Zaryte crossbow Evoke and its bolt effects: the sim against numbers worked out by hand from the wiki
 * (Soulreaper_axe, Zaryte_crossbow, Ruby_bolts_(e), Ruby_dragon_bolts_(e), Diamond_bolts_(e)), and the threat block that reports them.
 * cc -std=gnu11 -O2 -I. -o /tmp/test_soulreaper_zcb ocean/osrs/tests/test_soulreaper_zcb.c -lm */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static int near(double a, double b, double eps) { return fabs(a - b) <= eps; }

static void dress(Player* p, uint16_t weapon, uint16_t ammo) {
    memset(p->equipped, ITEM_NONE, sizeof(p->equipped));
    p->equipped[GEAR_SLOT_WEAPON] = weapon;
    p->equipped[GEAR_SLOT_AMMO] = ammo;
    osrs_refresh_player_equipment(p);
    pvp_refresh_visible_gear(p);
}

/* Attacker 0 and defender 1 of the hand examples: attack and strength 118 on Piety, aggressive, hitpoints 99; the defender 99 defence on Piety (x1.25) in a
 * Fighter torso: slash roll (floor(99 * 1.25) + 8) * (85 + 64) = 131 * 149 = 19519. */
static void arena(uint16_t weapon, uint16_t ammo) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 99);
    Player* a = &state.env.players[0];
    Player* d = &state.env.players[1];
    a->current_attack = a->current_strength = 118;
    a->current_ranged = 112;
    a->current_hitpoints = 99;
    a->offensive_prayer = weapon == ITEM_ZARYTE_CROSSBOW ? OFFENSIVE_PRAYER_RIGOUR : OFFENSIVE_PRAYER_PIETY;
    a->fight_style = weapon == ITEM_ZARYTE_CROSSBOW ? FIGHT_STYLE_RAPID : FIGHT_STYLE_AGGRESSIVE;
    a->soul_stacks = 0;
    dress(a, weapon, ammo);
    d->current_defence = 99;
    d->offensive_prayer = OFFENSIVE_PRAYER_PIETY;
    d->fight_style = FIGHT_STYLE_AGGRESSIVE;
    memset(d->equipped, ITEM_NONE, sizeof(d->equipped));
    d->equipped[GEAR_SLOT_BODY] = ITEM_FIGHTER_TORSO;
    osrs_refresh_player_equipment(d);
    pvp_refresh_visible_gear(d);
    a->x = d->x - 1; a->y = d->y;
}

static int attack_once(int special, int distance, int defender_hp, int* pending_damage) {
    Player* a = &state.env.players[0];
    Player* d = &state.env.players[1];
    a->num_pending_hits = 0;
    a->special_energy = 100;
    d->current_hitpoints = defender_hp;
    perform_attack(&state.env, 0, 1, a->equipped[GEAR_SLOT_WEAPON] == ITEM_ZARYTE_CROSSBOW ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE, special, 0, distance);
    assert(a->num_pending_hits == 1);
    *pending_damage = a->pending_hits[0].damage;
    return a->pending_hits[0].hit_success;
}

static void test_stack_strength(void) {
    arena(ITEM_SOULREAPER_AXE, ITEM_NONE);
    Player* a = &state.env.players[0];
    /* Strength 125 (wiki, since 22 July 2026). Piety 1.23 plus 0.06 per stack on the Strength level 118, +3 aggressive, +8:
     *   0: floor(118 * 1.23) = 145 -> 156, max floor((156 * (125 + 64) + 320) / 640) = 46;   1: 152 -> 163 -> 48;   2: 159 -> 170 -> 50;
     *   3: 166 -> 177 -> 52;   4: 173 -> 184 -> 54;   5: floor(118 * 1.53) = 180 -> 191 -> 56. */
    static const int hand[6] = {46, 48, 50, 52, 54, 56};
    assert(ITEM_DATABASE[ITEM_SOULREAPER_AXE].melee_strength == 125);
    OsrsThreatActor who = osrs_threat_actor_assumed(ATTACK_STYLE_MELEE);
    for (int s = 0; s <= 5; s++) {
        a->soul_stacks = s;
        assert(calculate_base_max_hit(a, ATTACK_STYLE_MELEE, 1.0f, 30) == hand[s]);
        who.soul_stacks = s;
        who.strength = 118;
        OsrsThreatWeapon w = osrs_threat_weapon(a->equipped, ITEM_SOULREAPER_AXE, &who);
        assert(w.max_hit == hand[s]);
    }
    /* Stacks do nothing for another weapon. */
    a->soul_stacks = 5;
    dress(a, ITEM_WHIP, ITEM_NONE);
    int whip5 = calculate_base_max_hit(a, ATTACK_STYLE_MELEE, 1.0f, 30);
    a->soul_stacks = 0;
    assert(calculate_base_max_hit(a, ATTACK_STYLE_MELEE, 1.0f, 30) == whip5);
    printf("soul stacks: max hit 46 48 50 52 54 56 at 0..5 stacks, none for a whip\n");
}

static void test_stack_build_and_decay(void) {
    arena(ITEM_SOULREAPER_AXE, ITEM_NONE);
    Player* a = &state.env.players[0];
    static const int max_before[6] = {46, 48, 50, 52, 54, 56};
    int dmg;
    /* Each swing, hit or miss, adds a stack after its damage is rolled: no swing deals more than the maximum of the stacks held before it, and the cap is five. */
    for (int swing = 0; swing < 8; swing++) {
        int before = a->soul_stacks;
        for (int rep = 0; rep < 300; rep++) { /* the roll never exceeds the pre-swing maximum */
            a->soul_stacks = before;
            attack_once(0, 1, 99, &dmg);
            assert(dmg <= max_before[before] && a->soul_stacks == (before < 5 ? before + 1 : 5));
        }
        a->soul_stacks = before < 5 ? before + 1 : 5;
    }
    /* A miss builds a stack too. */
    a->soul_stacks = 0;
    a->current_attack = 1;
    int missed = 0;
    for (int rep = 0; rep < 200 && !missed; rep++) { a->soul_stacks = 0; missed = !attack_once(0, 1, 99, &dmg) && a->soul_stacks == 1; }
    assert(missed);
    a->current_attack = 118;
    /* No hitpoints are paid for stacks. */
    assert(a->current_hitpoints == 99);
    /* Decay: a swing sets 50 ticks; the stack is lost on the 50th tick without another axe swing, then one more per 50 ticks; a swing with another weapon does not
     * reset the clock, and switching weapons keeps the stacks. */
    a->soul_stacks = 0;
    attack_once(0, 1, 99, &dmg);
    attack_once(0, 1, 99, &dmg);
    attack_once(0, 1, 99, &dmg);
    assert(a->soul_stacks == 3);
    for (int tick = 1; tick <= 49; tick++) { update_player_timers(a, 0); assert(a->soul_stacks == 3); }
    dress(a, ITEM_WHIP, ITEM_NONE);
    assert(a->soul_stacks == 3);
    update_player_timers(a, 0);
    assert(a->soul_stacks == 2);
    for (int tick = 1; tick <= 49; tick++) { update_player_timers(a, 0); assert(a->soul_stacks == 2); }
    update_player_timers(a, 0);
    assert(a->soul_stacks == 1);
    for (int tick = 0; tick < 50; tick++) update_player_timers(a, 0);
    assert(a->soul_stacks == 0);
    for (int tick = 0; tick < 200; tick++) update_player_timers(a, 0);
    assert(a->soul_stacks == 0 && a->current_hitpoints == 99);
    printf("soul stacks: +1 per swing (cap 5, misses count), -1 per 50 ticks, kept on a switch, no hitpoint cost\n");
}

static void test_behead(void) {
    arena(ITEM_SOULREAPER_AXE, ITEM_NONE);
    Player* a = &state.env.players[0];
    /* Plain slash attack roll: (floor(118 * 1.2) + 8) * (134 + 64) = 149 * 198 = 29502. Behead with s stacks rolls 29502 * (100 + 12 s) / 100 against 19519, and
     * works from the stack-free maximum 46: max 46 * (100 + 6 s) / 100, min 46 * 6 s / 100. s = 5: 47203, hit 1 - 19521 / (2 * 47204) = 0.79322, damage 13..59.
     * s = 3: 40122, hit 1 - 19521 / (2 * 40123) = 0.75672, damage 8..54. s = 0: the plain roll 29502, hit 1 - 19521 / (2 * 29503) = 0.66918, damage 0..46. */
    const struct { int stacks; double hit; int lo, hi; } cases[] = {{5, 0.79322, 13, 59}, {3, 0.75672, 8, 54}, {0, 0.66918 * 46 / 47, 1, 46}}; /* with no stacks a landed 0 counts as no hit: P(damage > 0) = 0.66918 * 46 / 47, damage 1..46 */
    for (size_t c = 0; c < sizeof(cases) / sizeof(*cases); c++) {
        int hits = 0, lo = 1000, hi = -1, n = 120000, dmg;
        double sum = 0;
        for (int i = 0; i < n; i++) {
            a->soul_stacks = cases[c].stacks;
            a->special_energy = 100;
            int landed = attack_once(1, 1, 99, &dmg);
            assert(a->soul_stacks == 0);        /* all consumed */
            assert(a->special_energy == 100);    /* no energy */
            assert(a->used_special_this_tick);
            if (landed) { hits++; sum += dmg; if (dmg < lo) lo = dmg; if (dmg > hi) hi = dmg; }
            else assert(dmg == 0);
        }
        if (!near((double)hits / n, cases[c].hit, 0.006) || lo != cases[c].lo || hi != cases[c].hi || !near(sum / hits, (cases[c].lo + cases[c].hi) / 2.0, 0.35)) {
            printf("behead %d stacks: hit %.4f (hand %.5f) damage %d..%d (hand %d..%d) mean %.2f\n", cases[c].stacks, (double)hits / n, cases[c].hit, lo, hi, cases[c].lo, cases[c].hi, sum / hits);
            abort();
        }
    }
    /* The threat calculator reports the same numbers. */
    OsrsThreatActor who = osrs_threat_actor_assumed(ATTACK_STYLE_MELEE);
    OsrsDefenceRolls d = osrs_defence_rolls(state.env.players[1].equipped, &who);
    who.soul_stacks = 5;
    OsrsThreatWeapon w = osrs_threat_weapon(a->equipped, ITEM_SOULREAPER_AXE, &who);
    assert(w.max_hit == 56 && w.spec_cost == 0 && w.spec_hits == 1 && w.spec_max == 59 && w.spec_min == 13 && w.spec_att_roll == 47203 && w.spec_speed == 5);
    assert(near(osrs_threat_spec_hit_chance(&w, &d), 0.79322, 1e-4));
    assert(near(osrs_threat_hit_chance(&w, &d), 1 - (19519 + 2) / (2.0 * (149 * 198 + 1)), 1e-4)); /* the normal roll: 0.66918 */
    printf("behead: hit 0.793 / 0.757 / 0.655 (damage > 0) and damage 13..59 / 8..54 / 1..46 at 5 / 3 / 0 stacks, no energy, stacks gone\n");
}

/* A scripted Soulreaper fighter: his stacks as the viewer counts them equal the real ones on every tick; he beheads at five stacks; stacks stay in 0..5. */
static void test_fight_and_inference(void) {
    const RiskfightKit* kit = NULL;
    for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++) if (!strcmp(RISKFIGHT_POOL[i].name, "soulreaper axe")) kit = &RISKFIGHT_POOL[i];
    assert(kit);
    int beheads = 0, swings = 0, ticks = 0, max_seen = 0;
    for (int e = 0; e < 40; e++) {
        riskfight_init_context((EncounterContext*)&context);
        riskfight_put_int((EncounterState*)&state, (EncounterContext*)&context, "live_loadout", 1);
        riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
        context.loadout[1] = &RISKFIGHT_POOL_LOADOUT[kit - RISKFIGHT_POOL];
        riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 500 + (uint32_t)e);
        riskfight_kit_apply(&state, 1, kit, 77u + (uint32_t)e);
        riskfight_observe_visible(&state, 0, 1);
        state.opponent_profile = &RISKFIGHT_HUMAN_PROFILES[e % RISKFIGHT_HUMAN_TRAIN_PROFILES];
        while (!state.env.episode_over) {
            int actions[2 * RF_HEADS] = {0};
            float obs[RF_OBS_SIZE];
            riskfight_write_script_observation(&state, 0, obs);
            riskfight_script(obs, &RISKFIGHT_HUMAN_PROFILES[(e + 5) % RISKFIGHT_HUMAN_TRAIN_PROFILES], state.script_seed[0], state.consume_ticks[0], actions);
            riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
            const Player* him = &state.env.players[1];
            assert(him->soul_stacks >= 0 && him->soul_stacks <= 5 && him->current_hitpoints > 0 ? 1 : 1);
            riskfight_write_observation(&state, 0, obs);
            int inferred = (int)lroundf(obs[RF_THREAT_START + RF_THREAT_STACKS_OFFSET] * 5);
            if (inferred != him->soul_stacks) { printf("fight %d tick %d: viewer counts %d stacks, he holds %d\n", e, state.env.tick, inferred, him->soul_stacks); abort(); }
            assert(him->soul_stacks <= 5);
            if (him->attack_style_this_tick != ATTACK_STYLE_NONE) swings++;
            beheads += him->used_special_this_tick && him->equipped[GEAR_SLOT_WEAPON] == ITEM_SOULREAPER_AXE;
            if (him->soul_stacks > max_seen) max_seen = him->soul_stacks;
            ticks++;
        }
    }
    assert(beheads > 0 && max_seen == 5);
    printf("soulreaper fighter: %d ticks, %d swings, %d Beheads, stacks counted by the viewer equal his real ones on every tick (max %d)\n", ticks, swings, beheads, max_seen);
}

static void test_soul_threat_block(void) {
    arena(ITEM_ABYSSAL_TENTACLE, ITEM_NONE);
    Player* him = &state.env.players[1];
    Player* us = &state.env.players[0];
    (void)us;
    memset(him->equipped, ITEM_NONE, sizeof(him->equipped));
    him->equipped[GEAR_SLOT_WEAPON] = ITEM_SOULREAPER_AXE;
    osrs_refresh_player_equipment(him);
    pvp_refresh_visible_gear(him);
    riskfight_observe_visible(&state, 0, 1);
    RiskfightThreat* t = &state.visible[0].threat;
    float obs[RF_OBS_SIZE];
    state.env.tick = 10;
    /* Five swings of the axe on ticks 10..14, read at 15: five stacks, max hit from the five-stack roll (our viewer assumes 118 and Piety: 56, as in the sim). */
    for (int tick = 10; tick < 15; tick++) riskfight_threat_note_swing(t, ITEM_SOULREAPER_AXE, tick, 0);
    state.env.tick = 15;
    riskfight_threat_refresh(t, state.visible[0].equipment, 15);
    riskfight_write_observation(&state, 0, obs);
    const float* b = obs + RF_THREAT_START;
    OsrsThreatActor who = osrs_threat_actor_assumed(ATTACK_STYLE_MELEE);
    OsrsThreatWeapon plain = osrs_threat_weapon(state.visible[0].equipment, ITEM_SOULREAPER_AXE, &who);
    assert(plain.max_hit == 46);
    assert(near(b[RF_THREAT_STACKS_OFFSET], 1.0, 1e-6));
    assert(near(b[2] * RF_THREAT_DAMAGE_SCALE, 56, 1e-3));       /* the max hit with five stacks */
    assert(b[8] == 0 && near(b[9] * RF_THREAT_DAMAGE_SCALE, 59, 1e-3) && b[10] > b[3] && near(b[11] * RF_THREAT_SPEED_SCALE, 5, 1e-6)); /* Behead: no energy, 59, more accurate */
    /* Read 50 ticks later: one stack lost (stacks held 4 at tick 65, 3 at 115); a Behead (special swing) takes all. */
    state.env.tick = 65; riskfight_threat_refresh(t, state.visible[0].equipment, 65); riskfight_write_observation(&state, 0, obs);
    assert(near(obs[RF_THREAT_START + RF_THREAT_STACKS_OFFSET], 0.8, 1e-6) && near(obs[RF_THREAT_START + 2] * RF_THREAT_DAMAGE_SCALE, 54, 1e-3));
    state.env.tick = 66; riskfight_threat_refresh(t, state.visible[0].equipment, 66);
    riskfight_threat_note_swing(t, ITEM_SOULREAPER_AXE, 66, 1);
    state.env.tick = 67; riskfight_threat_refresh(t, state.visible[0].equipment, 67); riskfight_write_observation(&state, 0, obs);
    assert(obs[RF_THREAT_START + RF_THREAT_STACKS_OFFSET] == 0 && near(obs[RF_THREAT_START + 2] * RF_THREAT_DAMAGE_SCALE, 46, 1e-3));
    printf("soul threat block: stacks 5 -> max 56, Behead 59 / no energy; 4 after 50 ticks; 0 after a Behead\n");
}

/* ---- Zaryte crossbow */
static void test_zcb_range(void) {
    assert(ITEM_DATABASE[ITEM_ZARYTE_CROSSBOW].attack_range == 8 && ITEM_DATABASE[ITEM_ZARYTE_CROSSBOW].attack_speed == 6);
    arena(ITEM_ZARYTE_CROSSBOW, ITEM_RUBY_DRAGON_BOLTS_E);
    assert(get_attack_range(&state.env.players[0], ATTACK_STYLE_RANGED) == 8);
    printf("zcb: attack range 8\n");
}

static void test_bolt_functions(void) {
    uint32_t rng = 1;
    /* Ruby dragon bolts (e): certain through Evoke once the accuracy roll landed: 22% of 99 = 21 (floor 21.78), cap 110; 22% of 121 = 26; against 600 hitpoints the cap
     * 110 holds. The attacker pays 10% of its current hitpoints: 99 -> 9, 121 -> 12; under 10 hitpoints it cannot spare one and nothing happens. */
    BoltProcResult r = osrs_resolve_bolt_proc(ITEM_RUBY_DRAGON_BOLTS_E, 7, 1, 42, 112, 99, 99, 1, 1, 0, &rng);
    assert(r.proc_triggered && r.modified_damage == 21 && r.attacker_hp_cost == 9);
    r = osrs_resolve_bolt_proc(ITEM_RUBY_DRAGON_BOLTS_E, 7, 1, 42, 112, 121, 121, 1, 1, 0, &rng);
    assert(r.proc_triggered && r.modified_damage == 26 && r.attacker_hp_cost == 12);
    r = osrs_resolve_bolt_proc(ITEM_RUBY_DRAGON_BOLTS_E, 7, 1, 42, 112, 600, 99, 1, 1, 0, &rng);
    assert(r.modified_damage == 110);
    r = osrs_resolve_bolt_proc(ITEM_RUBY_DRAGON_BOLTS_E, 7, 1, 42, 112, 99, 9, 1, 1, 0, &rng);
    assert(!r.proc_triggered && r.modified_damage == 7 && r.attacker_hp_cost == 0);
    r = osrs_resolve_bolt_proc(ITEM_RUBY_DRAGON_BOLTS_E, 0, 0, 42, 112, 99, 99, 1, 1, 0, &rng); /* the special missed: no effect */
    assert(!r.proc_triggered);
    /* Not the crossbow's passive: 20% of 99 = 19, cap 100. */
    r = osrs_resolve_bolt_proc(ITEM_RUBY_DRAGON_BOLTS_E, 7, 1, 42, 112, 99, 99, 0, 1, 0, &rng);
    assert(r.modified_damage == 19 && osrs_resolve_bolt_proc(ITEM_RUBY_DRAGON_BOLTS_E, 7, 1, 42, 112, 900, 99, 0, 1, 0, &rng).modified_damage == 100);
    /* Diamond: max 42 -> 126% = 52 from a Zaryte, 115% = 48 otherwise; uniform 0..max; certain through Evoke after a landed roll. */
    int top_zcb = 0, top_plain = 0;
    for (int i = 0; i < 20000; i++) {
        r = osrs_resolve_bolt_proc(ITEM_DIAMOND_DRAGON_BOLTS_E, 5, 1, 42, 112, 99, 99, 1, 1, 0, &rng);
        assert(r.proc_triggered && r.modified_damage >= 0 && r.modified_damage <= 52);
        top_zcb = r.modified_damage > top_zcb ? r.modified_damage : top_zcb;
        r = osrs_resolve_bolt_proc(ITEM_DIAMOND_DRAGON_BOLTS_E, 5, 1, 42, 112, 99, 99, 0, 1, 0, &rng);
        assert(r.proc_triggered && r.modified_damage <= 48);
        top_plain = r.modified_damage > top_plain ? r.modified_damage : top_plain;
    }
    assert(top_zcb == 52 && top_plain == 48);
    /* Chances of an unforced shot, rolled whether or not the accuracy roll passed: ruby 11% (needs the hitpoints), diamond 5% (PvP). */
    int ruby = 0, diamond = 0, n = 400000;
    for (int i = 0; i < n; i++) {
        ruby += osrs_resolve_bolt_proc(ITEM_RUBY_DRAGON_BOLTS_E, 0, 0, 42, 112, 99, 99, 0, 0, 0, &rng).proc_triggered;
        diamond += osrs_resolve_bolt_proc(ITEM_DIAMOND_BOLTS_E, 0, 0, 42, 112, 99, 99, 0, 0, 0, &rng).proc_triggered;
    }
    assert(near((double)ruby / n, 0.11, 0.003) && near((double)diamond / n, 0.05, 0.003));
    printf("bolts: ruby 21 / 26 / 110 for 99 / 121 / 600 hitpoints (cost 9 / 12), diamond max 52 / 48, rates %.3f / %.3f\n", (double)ruby / n, (double)diamond / n);
}

static void test_evoke(void) {
    /* Zaryte crossbow (wiki: ranged +110, speed 6, Evoke costs 75% and doubles accuracy, then the bolt's effect is certain on a hit), Rigour, ranging 112, rapid.
     * Attack roll (floor(112 * 1.2) + 8) * (110 + 64) = 142 * 174 = 24708; Evoke doubles it to 49416. Defender ranged roll 131 * (67 + 64) = 17161:
     * hit = 1 - (17161 + 2) / (2 * 49417) = 0.82634. On a hit: ruby dragon bolts (e) deal 22% of the defender's 99 hitpoints = 21 and cost the attacker 9 of its 99. */
    arena(ITEM_ZARYTE_CROSSBOW, ITEM_RUBY_DRAGON_BOLTS_E);
    Player* a = &state.env.players[0];
    int n = 100000, hits = 0, dmg;
    for (int i = 0; i < n; i++) {
        a->current_hitpoints = 99;
        int landed = attack_once(1, 4, 99, &dmg);
        assert(a->special_energy == 25);                   /* 75% */
        if (landed) { hits++; assert(dmg == 21 && a->current_hitpoints == 90); }
        else assert(dmg == 0 && a->current_hitpoints == 99);
    }
    assert(near((double)hits / n, 0.82634, 0.005));
    /* The same shot with diamond dragon bolts (e): a landed Evoke deals 0..52 (126% of 42) and costs nothing; hits counted by damage > 0. */
    arena(ITEM_ZARYTE_CROSSBOW, ITEM_DIAMOND_DRAGON_BOLTS_E);
    a = &state.env.players[0];
    int top = 0;
    hits = 0;
    for (int i = 0; i < n; i++) {
        int landed = attack_once(1, 4, 99, &dmg);
        assert(a->current_hitpoints == 99);
        if (landed) { hits++; assert(dmg >= 0 && dmg <= 52); top = dmg > top ? dmg : top; }
    }
    assert(top == 52 && near((double)hits / n, 0.82634 * 52 / 53, 0.005)); /* a landed 0 reads as no hit */
    /* A plain shot with ruby bolts (e): the effect rolls 11% of the time, hit or miss, with the passive's 22% (21); the attacker pays 9 each time. */
    arena(ITEM_ZARYTE_CROSSBOW, ITEM_RUBY_DRAGON_BOLTS_E);
    a = &state.env.players[0];
    int procs = 0;
    for (int i = 0; i < n; i++) {
        a->current_hitpoints = 99;
        int landed = attack_once(0, 4, 99, &dmg);
        if (a->current_hitpoints == 90) { procs++; assert(landed && dmg == 21); }
        else assert(a->current_hitpoints == 99);
    }
    assert(near((double)procs / n, 0.11, 0.004));
    printf("evoke: hit 0.826, ruby 21 for 9 hitpoints, diamond 0..52; plain shot ruby proc %.3f\n", (double)procs / n);
}

static void test_zcb_threat(void) {
    /* The block: a Zaryte crossbow with the assumed ruby dragon bolts (e) against our 99 hitpoints: normal max max(42, 21), Evoke max(42, 21) = 42 (the roll's max stays
     * above the effect), against 121 the effect is 26. Against 600 it would be 110, shown through osrs_threat_effect_max. */
    OsrsThreatActor ranged = osrs_threat_actor_assumed(ATTACK_STYLE_RANGED);
    uint16_t eq[NUM_GEAR_SLOTS];
    memset(eq, ITEM_NONE, sizeof(eq));
    OsrsThreatWeapon zcb = osrs_threat_weapon(eq, ITEM_ZARYTE_CROSSBOW, &ranged);
    assert(zcb.bolt == ITEM_RUBY_DRAGON_BOLTS_E && zcb.zcb && zcb.max_hit == 42);
    assert(osrs_threat_effect_max(&zcb, zcb.spec_max, 99) == 42 && osrs_threat_effect_max(&zcb, zcb.spec_max, 121) == 42);
    assert(osrs_threat_effect_max(&zcb, 10, 99) == 21 && osrs_threat_effect_max(&zcb, 10, 600) == 110);
    OsrsThreatWeapon acb = osrs_threat_weapon(eq, ITEM_ARMADYL_CROSSBOW, &ranged);
    assert(!acb.zcb && osrs_threat_effect_max(&acb, 10, 600) == 100 && osrs_threat_effect_max(&acb, 10, 99) == 19);
    /* In a diamond-bolt pool kit seen through the assumption (ruby) the numbers of a player's guess: unchanged by what he really carries. */
    riskfight_init_context((EncounterContext*)&context);
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 5);
    Player* him = &state.env.players[1];
    memset(him->equipped, ITEM_NONE, sizeof(him->equipped));
    him->equipped[GEAR_SLOT_WEAPON] = ITEM_ZARYTE_CROSSBOW;
    osrs_refresh_player_equipment(him);
    pvp_refresh_visible_gear(him);
    riskfight_observe_visible(&state, 0, 1);
    state.env.players[0].current_hitpoints = 600; /* a hypothetical overheal: the effect is read off our current hitpoints */
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    assert(near(obs[RF_THREAT_START + 9] * RF_THREAT_DAMAGE_SCALE, 110, 1e-3) && near(obs[RF_THREAT_START + 2] * RF_THREAT_DAMAGE_SCALE, 110, 1e-3));
    state.env.players[0].current_hitpoints = 121;
    riskfight_write_observation(&state, 0, obs);
    assert(near(obs[RF_THREAT_START + 9] * RF_THREAT_DAMAGE_SCALE, 42, 1e-3) && obs[RF_THREAT_START + 8] == 0.75f);
    printf("zcb threat: spec cost 0.75, spec max 42 at 121 hitpoints and 110 at 600, ruby effect included\n");
}

int main(void) {
    test_stack_strength();
    test_stack_build_and_decay();
    test_behead();
    test_soul_threat_block();
    test_fight_and_inference();
    test_zcb_range();
    test_bolt_functions();
    test_evoke();
    test_zcb_threat();
    printf("soulreaper and zaryte crossbow: passed\n");
    return 0;
}
