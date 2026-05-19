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
#include "ocean/osrs/osrs_binary_io.h"
#include "ocean/osrs/osrs_spotanims.h"

static int tests_run = 0;
static int tests_failed = 0;

#define TEST_MDL2_MAGIC 0x4D444C32u
#define TEST_MDL3_MAGIC 0x4D444C33u
#define TEST_MDL4_MAGIC 0x4D444C34u

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

static int sequence_has_transform_type(
    AnimCache* first,
    AnimCache* second,
    int seq_id,
    int type
) {
    AnimCache* caches[] = { first, second };
    for (int ci = 0; ci < 2; ci++) {
        AnimCache* cache = caches[ci];
        AnimSequence* seq = anim_get_sequence(cache, (uint16_t)seq_id);
        if (!cache || !seq) continue;
        for (int fi = 0; fi < seq->frame_count; fi++) {
            AnimSequenceFrame* frame = &seq->frames[fi];
            AnimFrameBase* fb = anim_get_framebase(cache, frame->frame.framebase_id);
            if (!fb) continue;
            for (int ti = 0; ti < frame->frame.transform_count; ti++) {
                uint8_t slot = frame->frame.transforms[ti].slot_index;
                if (slot < fb->slot_count && fb->types[slot] == type) return 1;
            }
        }
    }
    return 0;
}

static int model_file_has_face_alpha_labels(const char* path, uint32_t model_id) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    uint32_t magic = 0;
    uint32_t count = 0;
    osrs_read_exact(f, &magic, 4, 1, path, "model magic");
    osrs_read_exact(f, &count, 4, 1, path, "model count");
    if (magic != TEST_MDL2_MAGIC && magic != TEST_MDL3_MAGIC && magic != TEST_MDL4_MAGIC) {
        fclose(f);
        return 0;
    }

    uint32_t* offsets = osrs_malloc_or_abort(count * sizeof(uint32_t), "test model offsets");
    osrs_read_exact(f, offsets, 4, count, path, "model offsets");
    int has_texcoords = magic == TEST_MDL3_MAGIC || magic == TEST_MDL4_MAGIC;
    int has_alpha_labels = magic == TEST_MDL4_MAGIC;
    int found = 0;
    for (uint32_t i = 0; i < count; i++) {
        osrs_seek_or_abort(f, (long)offsets[i], path);
        uint32_t row_model_id = 0;
        uint16_t vert_count = 0;
        uint16_t face_count = 0;
        uint16_t base_vert_count = 0;
        osrs_read_exact(f, &row_model_id, 4, 1, path, "model id");
        osrs_read_exact(f, &vert_count, 2, 1, path, "expanded vertex count");
        osrs_read_exact(f, &face_count, 2, 1, path, "face count");
        osrs_read_exact(f, &base_vert_count, 2, 1, path, "base vertex count");
        if (row_model_id != model_id) continue;

        long skip = (long)vert_count * 3L * (long)sizeof(float)
            + (long)vert_count * 4L
            + (has_texcoords ? (long)vert_count * 2L * (long)sizeof(float) : 0L)
            + (long)base_vert_count * 3L * (long)sizeof(int16_t)
            + (long)base_vert_count
            + (long)face_count * 3L * (long)sizeof(uint16_t)
            + (long)face_count;
        fseek(f, skip, SEEK_CUR);

        if (!has_alpha_labels) break;
        fseek(f, face_count, SEEK_CUR);
        for (uint16_t face = 0; face < face_count; face++) {
            int label = fgetc(f);
            if (label >= 0 && label != 255) {
                found = 1;
                break;
            }
        }
        break;
    }
    free(offsets);
    fclose(f);
    return found;
}

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
    tests_run++;
    const OsrsSpotAnimDef* ice_barrage = osrs_spotanim_find(spotanims, 369);
    if (!ice_barrage || !sequence_has_transform_type(
            equipment, inferno, ice_barrage->animation_id, 5)) {
        tests_failed++;
        printf("  FAIL: ice barrage impact sequence lacks alpha transforms\n");
    }

    printf("--- projectile model export coverage ---\n");
    tests_run++;
    if (!model_file_has_face_alpha_labels(
            OSRS_ASSET("projectiles.models"), OSRS_SPOTANIM_MODEL_BASE + 369u)) {
        tests_failed++;
        printf("  FAIL: ice barrage impact model lacks face alpha labels\n");
    }

    osrs_spotanims_free(spotanims);

    if (tests_failed > 0) {
        printf("\n%d/%d animation export checks failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("\n%d/%d animation export checks passed\n", tests_run, tests_run);
    return 0;
}
