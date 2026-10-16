/* Threat calculators (osrs_pvp_threat.h) and the threat observation block (riskfight_threat.h):
 *   1. three weapons against numbers worked out by hand from the wiki formulas and the wiki item rows;
 *   2. the pure calculators equal the sim's calculate_hit_chance / calculate_base_max_hit on real Players, for melee, ranged and magic gear;
 *   3. osrs_spec_accuracy equals osrs_resolve_spec, rolled 200,000 times per special weapon;
 *   4. the observation block of a sim state: layout, shown weapons, Dharok scaling with his bar.
 * cc -std=gnu11 -O2 -I. -o /tmp/test_threat_features ocean/osrs/tests/test_threat_features.c -lm */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static int near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static void bare(uint8_t equipment[NUM_GEAR_SLOTS]) { memset(equipment, ITEM_NONE, NUM_GEAR_SLOTS); }

/* The defender of the hand examples: defence 99 on Piety (x1.25), a Fighter torso (slash 85, crush 62, ranged 67), nothing else. */
static OsrsDefenceRolls torso_defender(void) {
    uint8_t eq[NUM_GEAR_SLOTS];
    bare(eq);
    eq[GEAR_SLOT_BODY] = ITEM_FIGHTER_TORSO;
    OsrsThreatActor d = {99, 99, 99, 99, 99, OFFENSIVE_PRAYER_PIETY, FIGHT_STYLE_AGGRESSIVE};
    return osrs_defence_rolls(eq, &d);
}

static void test_hand_numbers(void) {
    /* Defence: floor(99 * 1.25) = 123, + 8 = 131; rolls 131 * (bonus + 64): slash 85 -> 19519, crush 62 -> 16506, ranged 67 -> 17161. */
    OsrsDefenceRolls d = torso_defender();
    assert(d.melee[MELEE_BONUS_SLASH] == 19519 && d.melee[MELEE_BONUS_CRUSH] == 16506 && d.ranged == 17161);

    /* Abyssal whip (wiki: slash +82, strength +82, speed 4) with an Amulet of strength (+10), attack and strength 118 on Piety, aggressive.
     * Strength: floor(118 * 1.23) = 145, + 3 + 8 = 156; max = floor(0.5 + 156 * (82 + 10 + 64) / 640) = floor(38.525) = 38.
     * Attack: floor(118 * 1.20) = 141, + 0 + 8 = 149; roll 149 * (82 + 64) = 21754; hit = 1 - (19519 + 2) / (2 * 21755) = 0.55134. */
    uint8_t eq[NUM_GEAR_SLOTS];
    bare(eq);
    eq[GEAR_SLOT_NECK] = ITEM_AMULET_OF_STRENGTH;
    OsrsThreatActor melee = osrs_threat_actor_assumed(ATTACK_STYLE_MELEE);
    OsrsThreatWeapon whip = osrs_threat_weapon(eq, ITEM_WHIP, &melee);
    assert(whip.style == ATTACK_STYLE_MELEE && whip.speed == 4 && whip.melee_type == MELEE_BONUS_SLASH);
    assert(whip.max_hit == 38 && whip.att_roll == 21754 && whip.spec_cost == 0 && !whip.dharok);
    assert(near(osrs_threat_hit_chance(&whip, &d), 0.55134f, 1e-4f));

    /* Granite maul (ornate handle; wiki: crush +81, strength +79, speed 7, special costs 50% and is instant) with the same amulet.
     * max = floor(0.5 + 156 * (79 + 10 + 64) / 640) = floor(37.79) = 37; the special rolls the normal roll 149 * (81 + 64) = 21605 for the same 37, speed 1;
     * hit = 1 - (16506 + 2) / (2 * 21606) = 0.61797. */
    OsrsThreatWeapon maul = osrs_threat_weapon(eq, ITEM_GRANITE_MAUL_ORNATE, &melee);
    assert(maul.speed == 7 && maul.max_hit == 37 && maul.melee_type == MELEE_BONUS_CRUSH && maul.att_roll == 21605);
    assert(maul.spec_cost == 50 && maul.spec_hits == 1 && maul.spec_max == 37 && maul.spec_speed == 1 && maul.spec_type == MELEE_BONUS_CRUSH);
    assert(near(osrs_threat_hit_chance(&maul, &d), 0.61797f, 1e-4f) && near(osrs_threat_spec_hit_chance(&maul, &d), 0.61797f, 1e-4f));
    assert(osrs_spec_cost(ITEM_GRANITE_MAUL) == 60 && osrs_threat_weapon(eq, ITEM_GRANITE_MAUL, &melee).spec_cost == 60);

    /* Zaryte crossbow (wiki: ranged +110, speed 6, Evoke costs 75% and doubles accuracy) on Rigour, ranging 112, rapid, ruby dragon bolts (e) (+122).
     * Attack: floor(112 * 1.20) = 134, + 0 + 8 = 142; roll 142 * (110 + 64) = 24708; strength: floor(112 * 1.23) = 137 + 8 = 145;
     * max = floor(0.5 + 145 * (122 + 64) / 640) = floor(42.65) = 42. Rapid: speed 6 - 1 = 5.
     * hit = 1 - (17161 + 2) / (2 * 24709) = 0.65270; the special doubles the roll: 49416, hit = 1 - 17163 / (2 * 49417) = 0.82634, max 42. */
    OsrsThreatActor ranged = osrs_threat_actor_assumed(ATTACK_STYLE_RANGED);
    OsrsThreatWeapon zcb = osrs_threat_weapon(eq, ITEM_ZARYTE_CROSSBOW, &ranged);
    assert(zcb.style == ATTACK_STYLE_RANGED && zcb.speed == 5 && zcb.att_roll == 24708 && zcb.max_hit == 42);
    assert(zcb.spec_cost == 75 && zcb.spec_hits == 1 && zcb.spec_max == 42 && zcb.spec_att_roll == 49416 && zcb.spec_speed == 5);
    assert(near(osrs_threat_hit_chance(&zcb, &d), 0.65270f, 1e-4f) && near(osrs_threat_spec_hit_chance(&zcb, &d), 0.82634f, 1e-4f));

    /* Voidwaker's special rolls nothing and hits 50%..150% of the max (wiki: 'Disrespect'; its normal max with the amulet: 156 * (80 + 10 + 64) / 640 = 37.5 -> 38). */
    OsrsThreatWeapon vw = osrs_threat_weapon(eq, ITEM_VOIDWAKER, &melee);
    assert(vw.max_hit == 38 && vw.spec_cost == 50 && vw.spec_max == 57 && osrs_threat_spec_hit_chance(&vw, &d) == 1.0f);

    /* A Dharok's set at 1 hitpoint: max * (10000 + 99 * 98) / 10000. */
    uint8_t dh[NUM_GEAR_SLOTS];
    bare(dh);
    dh[GEAR_SLOT_HEAD] = ITEM_DHAROKS_HELM; dh[GEAR_SLOT_BODY] = ITEM_DHAROKS_PLATEBODY; dh[GEAR_SLOT_LEGS] = ITEM_DHAROKS_PLATELEGS;
    OsrsThreatWeapon axe = osrs_threat_weapon(dh, ITEM_DHAROKS_GREATAXE, &melee);
    assert(axe.dharok && axe.speed == 7);
    assert(osrs_threat_max_hit_at(&axe, 99, 99) == axe.max_hit && osrs_threat_max_hit_at(&axe, 99, 1) == axe.max_hit * (10000 + 99 * 98) / 10000);
    OsrsThreatWeapon naked_axe = osrs_threat_weapon(eq, ITEM_DHAROKS_GREATAXE, &melee);
    assert(!naked_axe.dharok);
}

static Player make_player(const uint8_t eq[NUM_GEAR_SLOTS], const OsrsThreatActor* a) {
    Player p;
    init_player(&p);
    memcpy(p.equipped, eq, NUM_GEAR_SLOTS);
    p.current_attack = a->attack; p.current_strength = a->strength; p.current_ranged = a->ranged;
    p.current_magic = a->magic; p.current_defence = a->defence;
    p.offensive_prayer = a->prayer; p.fight_style = a->stance;
    osrs_refresh_player_equipment(&p);
    pvp_refresh_visible_gear(&p);
    return p;
}

static int gear_slot_of(int db_slot) {
    switch (db_slot) {
        case SLOT_HEAD: return GEAR_SLOT_HEAD; case SLOT_CAPE: return GEAR_SLOT_CAPE; case SLOT_NECK: return GEAR_SLOT_NECK;
        case SLOT_BODY: return GEAR_SLOT_BODY; case SLOT_SHIELD: return GEAR_SLOT_SHIELD; case SLOT_LEGS: return GEAR_SLOT_LEGS;
        case SLOT_HANDS: return GEAR_SLOT_HANDS; case SLOT_FEET: return GEAR_SLOT_FEET; case SLOT_RING: return GEAR_SLOT_RING;
        case SLOT_AMMO: return GEAR_SLOT_AMMO; default: return GEAR_SLOT_WEAPON;
    }
}

/* The calculators equal the sim's on Players: random gear from the item table and every weapon with a combat style. */
static void test_equals_sim(void) {
    uint32_t rng = 12345;
    int weapons[NUM_ITEMS], weapon_count = 0;
    for (int i = 0; i < NUM_ITEMS; i++)
        if (ITEM_DATABASE[i].slot == SLOT_WEAPON && get_item_attack_style((uint8_t)i) != ATTACK_STYLE_NONE) weapons[weapon_count++] = i;
    int checked[4] = {0};
    for (int trial = 0; trial < 4000; trial++) {
        uint8_t eq[NUM_GEAR_SLOTS];
        for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
            int pick, tries = 0;
            do { pick = (int)(xorshift32(&rng) % (NUM_ITEMS + 8)); } while (pick < NUM_ITEMS && ++tries < 1000 &&
                (ITEM_DATABASE[pick].slot == SLOT_WEAPON || gear_slot_of(ITEM_DATABASE[pick].slot) != slot));
            eq[slot] = pick >= NUM_ITEMS || tries >= 1000 ? ITEM_NONE : (uint8_t)pick;
        }
        uint8_t weapon = (uint8_t)weapons[xorshift32(&rng) % (unsigned)weapon_count];
        eq[GEAR_SLOT_WEAPON] = weapon;
        int style = get_item_attack_style(weapon);
        OsrsThreatActor a = osrs_threat_actor_assumed(style);
        a.attack = 90 + (int)(xorshift32(&rng) % 29); a.strength = 90 + (int)(xorshift32(&rng) % 29); a.ranged = 90 + (int)(xorshift32(&rng) % 23);
        a.magic = 80 + (int)(xorshift32(&rng) % 20); a.defence = 80 + (int)(xorshift32(&rng) % 40);
        a.prayer = (OffensivePrayer)(xorshift32(&rng) % 7); a.stance = (FightStyle)(xorshift32(&rng) % 5);
        if (style == ATTACK_STYLE_MAGIC) a.stance = FIGHT_STYLE_ACCURATE;
        OsrsThreatActor d = {99, 99, 99, 99, 80 + (int)(xorshift32(&rng) % 40), (OffensivePrayer)(xorshift32(&rng) % 7), (FightStyle)(xorshift32(&rng) % 4)};
        uint8_t deq[NUM_GEAR_SLOTS];
        for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) deq[slot] = (xorshift32(&rng) % 3) ? eq[slot] : ITEM_NONE;
        deq[GEAR_SLOT_WEAPON] = ITEM_NONE;
        /* The sim wields what the loadout says: the loadout the pure code builds (ammunition, no shield under a two-hander). */
        uint8_t wielded[NUM_GEAR_SLOTS];
        osrs_threat_loadout(eq, weapon, wielded);
        Player attacker = make_player(wielded, &a), defender = make_player(deq, &d);
        attacker.current_gear = style == ATTACK_STYLE_MELEE ? GEAR_MELEE : style == ATTACK_STYLE_RANGED ? GEAR_RANGED : GEAR_MAGE;
        OsrsThreatWeapon w = osrs_threat_weapon(eq, weapon, &a);
        OsrsDefenceRolls rolls = osrs_defence_rolls(deq, &d);
        float sim_chance = calculate_hit_chance(&attacker, &defender, (AttackStyle)style, 1.0f);
        int sim_max = calculate_base_max_hit(&attacker, (AttackStyle)style, 1.0f, 30);
        float pure_chance = osrs_threat_hit_chance(&w, &rolls);
        if (!near(sim_chance, pure_chance, 1e-6f) || sim_max != w.max_hit) {
            printf("mismatch %s style %d: chance sim %.6f pure %.6f max sim %d pure %d\n", ITEM_DATABASE[weapon].name, style, sim_chance, pure_chance, sim_max, w.max_hit);
            abort();
        }
        assert(w.speed == get_slot_gear_bonuses(&attacker)->attack_speed + osrs_stance_speed_mod(a.stance));
        /* The cheap route of the observation, swapping the weapon into the bonuses of what is worn, equals summing the loadout again, byte for byte. */
        uint8_t worn[NUM_GEAR_SLOTS];
        memcpy(worn, eq, NUM_GEAR_SLOTS);
        worn[GEAR_SLOT_WEAPON] = (xorshift32(&rng) & 3) == 0 ? ITEM_NONE : (uint8_t)weapons[xorshift32(&rng) % (unsigned)weapon_count];
        if (worn[GEAR_SLOT_WEAPON] != ITEM_NONE && item_is_two_handed(worn[GEAR_SLOT_WEAPON])) worn[GEAR_SLOT_SHIELD] = ITEM_NONE;
        EquipmentBonuses worn_sum;
        osrs_sum_equipment_bonuses(worn, &worn_sum);
        int dharok_armour = 0;
        for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++)
            dharok_armour += slot != GEAR_SLOT_WEAPON && worn[slot] != ITEM_NONE && (ITEM_DATABASE[worn[slot]].effect_mask & OSRS_ITEM_EFFECT_DHAROK_PIECE) != 0;
        OsrsThreatWeapon full = osrs_threat_weapon(worn, weapon, &a), swapped = osrs_threat_weapon_swapped(&worn_sum, dharok_armour, worn, weapon, &a);
        if (memcmp(&full, &swapped, sizeof(full)) != 0) { printf("swapped != full for %s over worn %s: max %d/%d roll %d/%d speed %d/%d\n", ITEM_DATABASE[weapon].name, worn[GEAR_SLOT_WEAPON] == ITEM_NONE ? "-" : ITEM_DATABASE[worn[GEAR_SLOT_WEAPON]].name, full.max_hit, swapped.max_hit, full.att_roll, swapped.att_roll, full.speed, swapped.speed); abort(); }
        if (style == ATTACK_STYLE_MELEE) assert(w.dharok == (attacker.equipment_effect_profile.dharok_piece_count >= 4));
        checked[style]++;
    }
    assert(checked[1] > 500 && checked[2] > 100 && checked[3] > 100);
    printf("threat == sim on %d melee, %d ranged, %d magic loadouts\n", checked[1], checked[2], checked[3]);
}

/* A special's accuracy: the multipliers of osrs_spec_accuracy against 200,000 osrs_resolve_spec calls. */
static void test_spec_accuracy_matches_resolver(void) {
    const int base_att = 28000, def_roll = 20000, big = 1000;
    int tested = 0;
    for (int item = 0; item < NUM_ITEMS; item++) {
        if (osrs_spec_cost(item) == 0) continue;
        /* Voidwaker rolls nothing; claws and Dark bow fold a miss into minimum hits, so their first roll is not what damage shows. */
        if (item == ITEM_VOIDWAKER || item == ITEM_DRAGON_CLAWS || item == ITEM_DARK_BOW) {
            assert(osrs_spec_accuracy(item).certain == (item == ITEM_VOIDWAKER));
            continue;
        }
        OsrsSpecAccuracy a = osrs_spec_accuracy(item);
        float expected = osrs_hit_chance(base_att * a.att_num / a.att_den, def_roll * a.def_num / a.def_den);
        /* The dagger and the shortbow roll their two hits one by one: damage shows if either lands. */
        if (item == ITEM_DRAGON_DAGGER || item == ITEM_MAGIC_SHORTBOW_I) expected = 1 - (1 - expected) * (1 - expected);
        if (item == ITEM_VOLATILE_STAFF) expected *= 58.0f / 59.0f; /* a fixed 0..58 roll shows no damage 1 time in 59 */
        uint32_t rng = 777u + (uint32_t)item;
        int hits = 0, n = 200000;
        for (int i = 0; i < n; i++) {
            SpecResult r = osrs_resolve_spec(item, base_att, big, def_roll, 99, &rng);
            hits += r.total_damage > 0;
        }
        float seen = (float)hits / n;
        if (!near(seen, expected, 0.01f)) { printf("spec %s: resolver hits %.4f, table predicts %.4f\n", ITEM_DATABASE[item].name, seen, expected); abort(); }
        tested++;
    }
    assert(tested >= 15);
    printf("spec accuracy table == osrs_resolve_spec on %d weapons\n", tested);
}

static RiskfightState state;
static RiskfightContext context;

static void test_observation_block(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 4242);
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    assert(RF_SESSION_START == RF_THREAT_START + RF_THREAT_SIZE && RF_OBS_SIZE == RF_SESSION_START + RF_SESSION_SIZE);
    const float* t = obs + RF_THREAT_START;
    /* Default kits: he wields the Abyssal tentacle (speed 4); the Dharok's set is worn but the axe is in the bag. */
    assert(t[0] == 1 && t[1] == 0 && t[5] == 1 && near(t[4], 0.4f, 1e-6f));
    OsrsThreatActor melee = osrs_threat_actor_assumed(ATTACK_STYLE_MELEE);
    OsrsThreatWeapon expect = osrs_threat_weapon(state.visible[0].equipment, ITEM_ABYSSAL_TENTACLE, &melee);
    assert(near(t[2], (float)expect.max_hit / RF_THREAT_DAMAGE_SCALE, 1e-6f));
    const float* ours = t + RF_THREAT_OURS_OFFSET;
    assert(ours[0] == 1 && ours[2] > 0 && ours[2] < 1);
    /* A swing of the Dharok's axe puts it in a slot as shown, with its own numbers. */
    riskfight_threat_note_swing(&state.visible[0].threat, ITEM_DHAROKS_GREATAXE);
    riskfight_threat_refresh(&state.visible[0].threat, state.visible[0].equipment);
    riskfight_write_observation(&state, 0, obs);
    int found = 0;
    for (int i = 1; i < RF_THREAT_HIS_SLOTS; i++) {
        const float* o = t + i * RF_THREAT_HIS_WIDTH;
        if (o[0] == 1 && o[1] == 1) { found = 1; assert(near(o[4], 0.7f, 1e-6f) && o[5] == 1); }
    }
    assert(found);
    /* With the Dharok's set and axe on him, his bar moves the max: a bar of 1 reads a lower bound of 1 hitpoint. */
    state.visible[0].equipment[GEAR_SLOT_WEAPON] = ITEM_DHAROKS_GREATAXE;
    state.visible[0].equipment[GEAR_SLOT_HEAD] = ITEM_DHAROKS_HELM;
    state.visible[0].equipment[GEAR_SLOT_BODY] = ITEM_DHAROKS_PLATEBODY;
    state.visible[0].equipment[GEAR_SLOT_LEGS] = ITEM_DHAROKS_PLATELEGS;
    riskfight_threat_refresh(&state.visible[0].threat, state.visible[0].equipment);
    state.visible[0].health_bar = 30;
    riskfight_write_observation(&state, 0, obs);
    float full = obs[RF_THREAT_START + 2];
    state.visible[0].health_bar = 1;
    riskfight_write_observation(&state, 0, obs);
    assert(obs[RF_THREAT_START + 2] > full * 1.5f);
    for (int i = 0; i < RF_OBS_SIZE; i++) assert(obs[i] >= -1 && obs[i] <= 1.0001f);
    printf("observation block: %d floats at %d\n", RF_THREAT_SIZE, RF_THREAT_START);
}

int main(void) {
    test_hand_numbers();
    test_equals_sim();
    test_spec_accuracy_matches_resolver();
    test_observation_block();
    printf("threat features: passed\n");
    return 0;
}
