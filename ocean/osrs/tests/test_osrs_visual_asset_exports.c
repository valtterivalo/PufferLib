/**
 * @fileoverview Regression checks for OSRS visual animation export coverage.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/test_osrs_visual_asset_exports \
 *       ocean/osrs/tests/test_osrs_visual_asset_exports.c -lm
 *   /tmp/test_osrs_visual_asset_exports
 */

#include <stdio.h>

#include "ocean/osrs/encounters/encounter_inferno.h"
#include "ocean/osrs/osrs_assets.h"
#include "ocean/osrs/osrs_anim.h"
#include "ocean/osrs/osrs_spotanims.h"

static int tests_run = 0;
static int tests_failed = 0;

static int has_anim(AnimCache* first, AnimCache* second, int seq_id) {
    return anim_get_sequence(first, (uint16_t)seq_id) ||
        anim_get_sequence(second, (uint16_t)seq_id);
}

#define ASSERT_ANIM_PRESENT(label, first, second, seq_id) do { \
    tests_run++; \
    if (!has_anim((first), (second), (seq_id))) { \
        tests_failed++; \
        printf("  FAIL: %s missing seq %d\n", (label), (seq_id)); \
    } \
} while (0)

#define ASSERT_SPOTANIM_PRESENT(label, set, gfx_id) do { \
    tests_run++; \
    const OsrsSpotAnimDef* def = osrs_spotanim_find((set), (gfx_id)); \
    if (!def || def->model_id < 0) { \
        tests_failed++; \
        printf("  FAIL: %s missing gfx %d\n", (label), (gfx_id)); \
    } \
} while (0)

int main(void) {
    AnimCache* equipment = anim_cache_load(OSRS_ASSET("equipment.anims"));
    AnimCache* inferno = anim_cache_load(OSRS_ASSET("inferno.anims"));
    AnimCache* zulrah = anim_cache_load(OSRS_ASSET("zulrah.anims"));
    OsrsSpotAnimSet* spotanims = osrs_spotanims_load(OSRS_ASSET("spotanims.bin"));
    if (!equipment || !inferno || !zulrah || !spotanims) return 1;

    printf("--- player animation export coverage ---\n");
    ASSERT_ANIM_PRESENT("godsword ready", equipment, NULL, 7053);
    ASSERT_ANIM_PRESENT("godsword walk", equipment, NULL, 7052);
    ASSERT_ANIM_PRESENT("godsword run", equipment, NULL, 7043);
    ASSERT_ANIM_PRESENT("dragon dart attack", equipment, NULL, 7554);
    tests_run++;
    if (equipment->seq_count < 1000) {
        tests_failed++;
        printf("  FAIL: equipment animation export has only %d sequences\n",
            equipment->seq_count);
    }

    printf("--- inferno runtime animation export coverage ---\n");
    ASSERT_ANIM_PRESENT("jad magic attack", equipment, inferno, INF_ANIM_JALTOK_JAD_MAGIC_ATTACK);
    ASSERT_ANIM_PRESENT("jad ranged attack", equipment, inferno, INF_ANIM_JALTOK_JAD_RANGED_ATTACK);
    ASSERT_ANIM_PRESENT("jad melee attack", equipment, inferno, INF_ANIM_JALTOK_JAD_MELEE_ATTACK);
    ASSERT_ANIM_PRESENT("meleer dig down", equipment, inferno, INF_GEN_ANIM_MELEER_DIG_DOWN);
    ASSERT_ANIM_PRESENT("meleer dig up", equipment, inferno, INF_GEN_ANIM_MELEER_DIG_UP);

    printf("--- zulrah runtime animation export coverage ---\n");
    ASSERT_ANIM_PRESENT("zulrah attack", equipment, zulrah, ZULRAH_ANIM_ATTACK);
    ASSERT_ANIM_PRESENT("zulrah idle", equipment, zulrah, ZULRAH_ANIM_IDLE);
    ASSERT_ANIM_PRESENT("zulrah dive", equipment, zulrah, ZULRAH_ANIM_DIVE);
    ASSERT_ANIM_PRESENT("zulrah rise", equipment, zulrah, ZULRAH_ANIM_RISE);
    ASSERT_ANIM_PRESENT("snakeling idle", equipment, zulrah, SNAKELING_ANIM_IDLE);
    ASSERT_ANIM_PRESENT("snakeling melee attack", equipment, zulrah, SNAKELING_ANIM_MELEE);
    ASSERT_ANIM_PRESENT("snakeling magic attack", equipment, zulrah, SNAKELING_ANIM_MAGIC);
    ASSERT_ANIM_PRESENT("snakeling death", equipment, zulrah, SNAKELING_ANIM_DEATH);
    ASSERT_ANIM_PRESENT("snakeling walk", equipment, zulrah, SNAKELING_ANIM_WALK);

    printf("--- spotanim export coverage ---\n");
    tests_run++;
    if (spotanims->count < 1000) {
        tests_failed++;
        printf("  FAIL: spotanim export has only %d definitions\n", spotanims->count);
    }
    ASSERT_SPOTANIM_PRESENT("magic splash", spotanims, 85);
    ASSERT_SPOTANIM_PRESENT("ice barrage projectile", spotanims, 368);
    ASSERT_SPOTANIM_PRESENT("ice barrage impact", spotanims, 369);
    ASSERT_SPOTANIM_PRESENT("blood barrage impact", spotanims, 377);
    ASSERT_SPOTANIM_PRESENT("trident projectile", spotanims, 1040);
    ASSERT_SPOTANIM_PRESENT("dragon dart projectile", spotanims, 1122);

    osrs_spotanims_free(spotanims);

    if (tests_failed > 0) {
        printf("\n%d/%d animation export checks failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("\n%d/%d animation export checks passed\n", tests_run, tests_run);
    return 0;
}
