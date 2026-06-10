/**
 * @file test_osrs_special_attacks.c
 * @brief Shared-layer tests for osrs_special_attacks.h, osrs_item_effects.h,
 *        and the consumable formula/application homes.
 *
 * These layers are consumed by colosseum, inferno, zulrah, and PvP, so their
 * contracts are tested HERE once, property-style, instead of per encounter:
 *   1. consumable amount formulas (osrs_consumables.h is the single home) +
 *      the Player-application laws (restore converges to base and never
 *      overshoots, boosts cap at base + boost, brew drain + restore round-trip);
 *   2. spec resolver: cost table, SGS heal/prayer wiki minimums on landed
 *      specs only, claws cascade bounds, elder maul / statius defence drains;
 *   3. item effects: identity law for an empty profile, tbow target-magic
 *      monotonicity, exact crystal armour scaling, blood fury proc rate and
 *      heal fraction (melee-only).
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/test_osrs_special_attacks \
 *       ocean/osrs/tests/test_osrs_special_attacks.c -lm
 *   /tmp/test_osrs_special_attacks
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ocean/osrs/osrs_encounter.h"
#include "ocean/osrs/osrs_special_attacks.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(label, cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s\n", (label)); } \
} while (0)

static Player make_maxed_player(void) {
    Player p;
    memset(&p, 0, sizeof(p));
    p.base_hitpoints = 99; p.current_hitpoints = 99;
    p.base_prayer = 99;    p.current_prayer = 99;
    p.base_attack = 99;    p.current_attack = 99;
    p.base_strength = 99;  p.current_strength = 99;
    p.base_defence = 99;   p.current_defence = 99;
    p.base_ranged = 99;    p.current_ranged = 99;
    p.base_magic = 99;     p.current_magic = 99;
    return p;
}

static void test_consumable_amounts_and_laws(void) {
    printf("test_consumable_amounts_and_laws\n");

    CHECK("super restore amount at 99 is 32", osrs_super_restore_amount(99) == 32);
    CHECK("sanfew amount at 99 is 33", osrs_sanfew_restore_amount(99) == 33);
    CHECK("sanfew out-restores super restore above level 80",
        osrs_sanfew_restore_amount(99) > osrs_super_restore_amount(99) &&
        osrs_sanfew_restore_amount(60) < osrs_super_restore_amount(60));
    CHECK("super combat boost at 99 is 19", osrs_super_combat_boost_amount(99) == 19);
    CHECK("ranging boost at 99 is 13", osrs_ranging_boost_amount(99) == 13);
    CHECK("brew heal at 99 is 16", osrs_brew_heal_amount(99) == 16);

    /* the drink layer and the amount helpers agree (one formula home) */
    CHECK("osrs_drink_potion super restore matches the amount helper",
        osrs_drink_potion(POTION_SUPER_RESTORE, 0, 99, 0).prayer_restored ==
        osrs_super_restore_amount(99));
    CHECK("osrs_drink_potion sanfew matches and cures venom",
        osrs_drink_potion(POTION_SANFEW, 0, 99, 0).prayer_restored ==
            osrs_sanfew_restore_amount(99) &&
        osrs_drink_potion(POTION_SANFEW, 0, 99, 0).venom_cured == 1);
    CHECK("osrs_drink_potion super combat matches the amount helper",
        osrs_drink_potion(POTION_SUPER_COMBAT, 0, 99, 0).level_boost ==
        osrs_super_combat_boost_amount(99));
    CHECK("osrs_brew_effect heal matches the amount helper",
        osrs_brew_effect(99, 99, 99, 99, 99).hp_healed == osrs_brew_heal_amount(99));

    /* law: restore caps at base from any drained start */
    Player p = make_maxed_player();
    p.current_attack = 1; p.current_strength = 40; p.current_ranged = 98;
    for (int i = 0; i < 20; i++) encounter_restore_stats(&p);
    CHECK("restore converges to base and never overshoots",
        p.current_attack == 99 && p.current_strength == 99 &&
        p.current_ranged == 99 && p.current_magic == 99);

    /* law: boosts cap at base + boost no matter how many doses */
    p = make_maxed_player();
    encounter_super_combat_boost(&p);
    encounter_super_combat_boost(&p);
    CHECK("super combat caps at base + boost (118)",
        p.current_attack == 118 && p.current_strength == 118 && p.current_defence == 118);
    encounter_ranging_boost(&p);
    encounter_ranging_boost(&p);
    CHECK("ranging caps at base + boost (112)", p.current_ranged == 112);

    /* law: brew drains recover fully under repeated restores */
    p = make_maxed_player();
    for (int i = 0; i < 4; i++) encounter_brew_drain_stats(&p);
    CHECK("brews drain offensive stats", p.current_attack < 99 && p.current_magic < 99);
    for (int i = 0; i < 20; i++) encounter_restore_stats(&p);
    CHECK("restores recover every brewed-down stat to base",
        p.current_attack == 99 && p.current_strength == 99 &&
        p.current_ranged == 99 && p.current_magic == 99);

    /* sanfew restores more per dose than super restore at base 99 */
    Player a = make_maxed_player(); a.current_attack = 1;
    Player b = make_maxed_player(); b.current_attack = 1;
    encounter_restore_stats(&a);
    encounter_sanfew_restore_stats(&b);
    CHECK("per-dose stat recovery follows the formulas (33 vs 32 at 99)",
        a.current_attack == 33 && b.current_attack == 34);
}

static void test_spec_costs_and_sgs(void) {
    printf("test_spec_costs_and_sgs\n");

    CHECK("cost table: claws/SGS/elder maul 50, DDS 25, statius 35, non-spec 0",
        osrs_spec_cost(ITEM_DRAGON_CLAWS) == 50 &&
        osrs_spec_cost(ITEM_SGS) == 50 &&
        osrs_spec_cost(ITEM_ELDER_MAUL) == 50 &&
        osrs_spec_cost(ITEM_DRAGON_DAGGER) == 25 &&
        osrs_spec_cost(ITEM_STATIUS_WARHAMMER) == 35 &&
        osrs_spec_cost(ITEM_SCYTHE_OF_VITUR) == 0);

    /* SGS: every LANDED spec heals >= 10 and restores >= 5 prayer (wiki
       minimums); a miss restores nothing. */
    uint32_t rng = 4242;
    int landed = 0, missed = 0;
    for (int i = 0; i < 4000; i++) {
        SpecResult r = osrs_resolve_spec(ITEM_SGS, 20000, 50, 12000, 99, &rng);
        if (r.total_damage > 0) {
            landed++;
            int expected_heal = r.total_damage / 2 > 10 ? r.total_damage / 2 : 10;
            int expected_pray = r.total_damage / 4 > 5 ? r.total_damage / 4 : 5;
            if (r.heal != expected_heal || r.prayer_restore != expected_pray) {
                CHECK("SGS heal/prayer follow max(d/2,10) / max(d/4,5)", 0);
                return;
            }
        } else {
            missed++;
            if (r.heal != 0 || r.prayer_restore != 0) {
                CHECK("a missed SGS spec restores nothing", 0);
                return;
            }
        }
    }
    CHECK("SGS sample hit both outcomes", landed > 0 && missed > 0);
    CHECK("SGS heal/prayer follow max(d/2,10) / max(d/4,5)", 1);
}

static void test_claws_and_def_drains(void) {
    printf("test_claws_and_def_drains\n");

    /* claws cascade: always 4 splats, total bounded by the roll-0 branch's
       2*max - 1 ceiling (+1 rounding slack), all splats non-negative. */
    uint32_t rng = 1337;
    int max_total = 0;
    int ok = 1;
    for (int i = 0; i < 5000; i++) {
        SpecResult r = osrs_resolve_spec(ITEM_DRAGON_CLAWS, 18000, 40, 9000, 99, &rng);
        if (r.num_hits != 4) ok = 0;
        int total = 0;
        for (int h = 0; h < 4; h++) {
            if (r.damage[h] < 0) ok = 0;
            total += r.damage[h];
        }
        if (total != r.total_damage) ok = 0;
        if (total > max_total) max_total = total;
    }
    CHECK("claws cascade shape holds across 5k resolves", ok);
    CHECK("claws total stays within the 2x max-hit ceiling",
        max_total <= 2 * 40 + 2 && max_total > 40);

    /* defence drains: elder maul 35%, statius 30%, both only on a hit */
    rng = 99;
    int maul_drained = 0, statius_drained = 0;
    for (int i = 0; i < 2000 && (!maul_drained || !statius_drained); i++) {
        SpecResult m = osrs_resolve_spec(ITEM_ELDER_MAUL, 20000, 40, 8000, 200, &rng);
        if (m.damage[0] > 0 && !maul_drained) {
            CHECK("elder maul drains 35% of target def", m.def_drain == 200 * 35 / 100);
            maul_drained = 1;
        }
        SpecResult st = osrs_resolve_spec(ITEM_STATIUS_WARHAMMER, 20000, 40, 8000, 200, &rng);
        if (st.damage[0] > 0 && !statius_drained) {
            CHECK("statius drains 30% of target def", st.def_drain == 200 * 30 / 100);
            statius_drained = 1;
        }
    }
    CHECK("both drain weapons landed in the sample", maul_drained && statius_drained);
}

static void test_item_effect_laws(void) {
    printf("test_item_effect_laws\n");

    OsrsItemEffectState state;
    osrs_item_effect_state_init(&state);

    /* identity law: an empty effect profile changes nothing */
    OsrsEquipmentEffectProfile empty;
    memset(&empty, 0, sizeof(empty));
    OsrsPreparedAttackEffects id = osrs_prepare_attack_effects(
        &empty, &state, ITEM_SCYTHE_OF_VITUR, ATTACK_STYLE_MELEE,
        OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1, 12345, 50,
        osrs_target_effect_context_magic(300, 80), 99, 99);
    CHECK("empty profile is the identity on rolls",
        id.attack_roll == 12345 && id.max_hit == 50 && id.use_double_accuracy == 0);

    /* tbow: monotone non-decreasing in target magic */
    OsrsEquipmentEffectProfile tbow_profile;
    memset(&tbow_profile, 0, sizeof(tbow_profile));
    tbow_profile.effect_mask = OSRS_ITEM_EFFECT_TWISTED_BOW;
    int prev_hit = -1, prev_roll = -1, monotone = 1;
    for (int magic = 1; magic <= 350; magic += 7) {
        OsrsPreparedAttackEffects e = osrs_prepare_attack_effects(
            &tbow_profile, &state, ITEM_TWISTED_BOW, ATTACK_STYLE_RANGED,
            OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1, 20000, 80,
            osrs_target_effect_context_magic(magic, 0), 99, 99);
        if (e.max_hit < prev_hit || e.attack_roll < prev_roll) monotone = 0;
        prev_hit = e.max_hit;
        prev_roll = e.attack_roll;
    }
    CHECK("tbow scaling is monotone in target magic", monotone);

    /* crystal armour: exact piece points and full-set bowfa scaling */
    CHECK("crystal points are helm 1 / legs 2 / body 3",
        osrs_crystal_armour_points(ITEM_CRYSTAL_HELM) == 1 &&
        osrs_crystal_armour_points(ITEM_CRYSTAL_LEGS) == 2 &&
        osrs_crystal_armour_points(ITEM_CRYSTAL_BODY) == 3);
    OsrsEquipmentEffectProfile crystal;
    memset(&crystal, 0, sizeof(crystal));
    crystal.effect_mask = OSRS_ITEM_EFFECT_CRYSTAL_ARMOUR;
    crystal.crystal_armour_points = 6;
    OsrsPreparedAttackEffects bowfa = osrs_prepare_attack_effects(
        &crystal, &state, ITEM_BOW_OF_FAERDHINEN, ATTACK_STYLE_RANGED,
        OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1, 20000, 40,
        osrs_target_effect_context_magic(100, 0), 99, 99);
    CHECK("full crystal scales bowfa by 26/20 accuracy and 46/40 damage",
        bowfa.attack_roll == 20000 * 26 / 20 && bowfa.max_hit == 40 * 46 / 40);

    /* scythe splat rule */
    CHECK("scythe splats: 1 vs 1x1, 2 vs 2x2, 3 vs 3x3+",
        osrs_scythe_splats_for_target_size(1) == 1 &&
        osrs_scythe_splats_for_target_size(2) == 2 &&
        osrs_scythe_splats_for_target_size(3) == 3 &&
        osrs_scythe_splats_for_target_size(5) == 3);

    /* blood fury: ~20% proc on melee damage, heal 30%, never on ranged */
    OsrsEquipmentEffectProfile fury;
    memset(&fury, 0, sizeof(fury));
    fury.effect_mask = OSRS_ITEM_EFFECT_BLOOD_FURY;
    uint32_t rng = 31337;
    int procs = 0, bad_heal = 0;
    for (int i = 0; i < 2000; i++) {
        OsrsPostAttackEffects post = osrs_finalize_attack_effects(
            &fury, &state, ITEM_OSMUMTENS_FANG, ATTACK_STYLE_MELEE,
            OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1, 0, 1, 30, &rng);
        if (post.heal_amount > 0) {
            procs++;
            if (post.heal_amount != 9) bad_heal = 1;
        }
    }
    CHECK("blood fury heals exactly 30% on every proc", !bad_heal);
    CHECK("blood fury procs near the 20% rate", procs > 280 && procs < 520);
    OsrsPostAttackEffects ranged_post = osrs_finalize_attack_effects(
        &fury, &state, ITEM_BOW_OF_FAERDHINEN, ATTACK_STYLE_RANGED,
        OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1, 0, 1, 30, &rng);
    CHECK("blood fury never procs on ranged damage", ranged_post.heal_amount == 0);
}

int main(void) {
    test_consumable_amounts_and_laws();
    test_spec_costs_and_sgs();
    test_claws_and_def_drains();
    test_item_effect_laws();

    printf("\n%d/%d passed", tests_passed, tests_run);
    if (tests_failed) printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
