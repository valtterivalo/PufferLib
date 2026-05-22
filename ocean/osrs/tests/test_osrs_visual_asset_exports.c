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
#include "ocean/osrs/data/item_models.h"
#include "ocean/osrs/data/npc_models_zulrah.h"
#include "ocean/osrs/data/player_models.h"

static int tests_run = 0;
static int tests_failed = 0;

#define TEST_MDL2_MAGIC 0x4D444C32u
#define TEST_MDL3_MAGIC 0x4D444C33u
#define TEST_MDL4_MAGIC 0x4D444C34u

static const int TEST_VISIBLE_EQUIP_SLOTS[] = {
    GEAR_SLOT_HEAD,
    GEAR_SLOT_CAPE,
    GEAR_SLOT_NECK,
    GEAR_SLOT_WEAPON,
    GEAR_SLOT_SHIELD,
    GEAR_SLOT_BODY,
    GEAR_SLOT_LEGS,
    GEAR_SLOT_HANDS,
    GEAR_SLOT_FEET,
};

static const uint8_t TEST_MAX_MAGE_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD] = ITEM_MASORI_MASK_F,
    [GEAR_SLOT_CAPE] = ITEM_DIZANAS_QUIVER,
    [GEAR_SLOT_NECK] = ITEM_OCCULT_NECKLACE,
    [GEAR_SLOT_AMMO] = ITEM_DRAGON_ARROWS,
    [GEAR_SLOT_WEAPON] = ITEM_KODAI_WAND,
    [GEAR_SLOT_SHIELD] = ITEM_ELYSIAN_SPIRIT_SHIELD,
    [GEAR_SLOT_BODY] = ITEM_VIRTUS_ROBE_TOP,
    [GEAR_SLOT_LEGS] = ITEM_VIRTUS_ROBE_BOTTOM,
    [GEAR_SLOT_HANDS] = ITEM_CONFLICTION_GAUNTLETS,
    [GEAR_SLOT_FEET] = ITEM_AVERNIC_TREADS,
    [GEAR_SLOT_RING] = ITEM_VENATOR_RING,
};

static const uint8_t TEST_MAX_RANGE_LONG_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD] = ITEM_MASORI_MASK_F,
    [GEAR_SLOT_CAPE] = ITEM_DIZANAS_QUIVER,
    [GEAR_SLOT_NECK] = ITEM_NECKLACE_OF_ANGUISH,
    [GEAR_SLOT_AMMO] = ITEM_DRAGON_ARROWS,
    [GEAR_SLOT_WEAPON] = ITEM_TWISTED_BOW,
    [GEAR_SLOT_SHIELD] = ITEM_NONE,
    [GEAR_SLOT_BODY] = ITEM_MASORI_BODY_F,
    [GEAR_SLOT_LEGS] = ITEM_MASORI_CHAPS_F,
    [GEAR_SLOT_HANDS] = ITEM_ZARYTE_VAMBRACES,
    [GEAR_SLOT_FEET] = ITEM_AVERNIC_TREADS,
    [GEAR_SLOT_RING] = ITEM_VENATOR_RING,
};

static const uint8_t TEST_MAX_RANGE_FAST_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD] = ITEM_MASORI_MASK_F,
    [GEAR_SLOT_CAPE] = ITEM_DIZANAS_QUIVER,
    [GEAR_SLOT_NECK] = ITEM_NECKLACE_OF_ANGUISH,
    [GEAR_SLOT_AMMO] = ITEM_DRAGON_DART,
    [GEAR_SLOT_WEAPON] = ITEM_TOXIC_BLOWPIPE,
    [GEAR_SLOT_SHIELD] = ITEM_NONE,
    [GEAR_SLOT_BODY] = ITEM_MASORI_BODY_F,
    [GEAR_SLOT_LEGS] = ITEM_MASORI_CHAPS_F,
    [GEAR_SLOT_HANDS] = ITEM_ZARYTE_VAMBRACES,
    [GEAR_SLOT_FEET] = ITEM_AVERNIC_TREADS,
    [GEAR_SLOT_RING] = ITEM_VENATOR_RING,
};

static const uint8_t TEST_BUDGET_MAGE_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD] = ITEM_CRYSTAL_HELM,
    [GEAR_SLOT_CAPE] = ITEM_DIZANAS_QUIVER,
    [GEAR_SLOT_NECK] = ITEM_OCCULT_NECKLACE,
    [GEAR_SLOT_AMMO] = ITEM_GOD_BLESSING,
    [GEAR_SLOT_WEAPON] = ITEM_DRAGON_HUNTER_WAND,
    [GEAR_SLOT_SHIELD] = ITEM_CRYSTAL_SHIELD,
    [GEAR_SLOT_BODY] = ITEM_AHRIMS_ROBETOP,
    [GEAR_SLOT_LEGS] = ITEM_AHRIMS_ROBESKIRT,
    [GEAR_SLOT_HANDS] = ITEM_CONFLICTION_GAUNTLETS,
    [GEAR_SLOT_FEET] = ITEM_ECHO_BOOTS,
    [GEAR_SLOT_RING] = ITEM_VENATOR_RING,
};

static const uint8_t TEST_BUDGET_RANGE_LONG_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD] = ITEM_CRYSTAL_HELM,
    [GEAR_SLOT_CAPE] = ITEM_DIZANAS_QUIVER,
    [GEAR_SLOT_NECK] = ITEM_NECKLACE_OF_ANGUISH,
    [GEAR_SLOT_AMMO] = ITEM_GOD_BLESSING,
    [GEAR_SLOT_WEAPON] = ITEM_BOW_OF_FAERDHINEN,
    [GEAR_SLOT_SHIELD] = ITEM_NONE,
    [GEAR_SLOT_BODY] = ITEM_CRYSTAL_BODY,
    [GEAR_SLOT_LEGS] = ITEM_CRYSTAL_LEGS,
    [GEAR_SLOT_HANDS] = ITEM_BARROWS_GLOVES,
    [GEAR_SLOT_FEET] = ITEM_ECHO_BOOTS,
    [GEAR_SLOT_RING] = ITEM_VENATOR_RING,
};

static const uint8_t TEST_BUDGET_RANGE_FAST_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD] = ITEM_CRYSTAL_HELM,
    [GEAR_SLOT_CAPE] = ITEM_DIZANAS_QUIVER,
    [GEAR_SLOT_NECK] = ITEM_NECKLACE_OF_ANGUISH,
    [GEAR_SLOT_AMMO] = ITEM_DRAGON_DART,
    [GEAR_SLOT_WEAPON] = ITEM_TOXIC_BLOWPIPE,
    [GEAR_SLOT_SHIELD] = ITEM_NONE,
    [GEAR_SLOT_BODY] = ITEM_CRYSTAL_BODY,
    [GEAR_SLOT_LEGS] = ITEM_CRYSTAL_LEGS,
    [GEAR_SLOT_HANDS] = ITEM_BARROWS_GLOVES,
    [GEAR_SLOT_FEET] = ITEM_ECHO_BOOTS,
    [GEAR_SLOT_RING] = ITEM_VENATOR_RING,
};

static const uint8_t TEST_PVP_BASIC_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD] = ITEM_HELM_NEITIZNOT,
    [GEAR_SLOT_CAPE] = ITEM_GOD_CAPE,
    [GEAR_SLOT_NECK] = ITEM_GLORY,
    [GEAR_SLOT_AMMO] = ITEM_DIAMOND_BOLTS_E,
    [GEAR_SLOT_WEAPON] = ITEM_WHIP,
    [GEAR_SLOT_SHIELD] = ITEM_DRAGON_DEFENDER,
    [GEAR_SLOT_BODY] = ITEM_BLACK_DHIDE_BODY,
    [GEAR_SLOT_LEGS] = ITEM_RUNE_PLATELEGS,
    [GEAR_SLOT_HANDS] = ITEM_BARROWS_GLOVES,
    [GEAR_SLOT_FEET] = ITEM_CLIMBING_BOOTS,
    [GEAR_SLOT_RING] = ITEM_BERSERKER_RING,
};

static const uint8_t TEST_PVP_MAGE_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD] = ITEM_ANCESTRAL_HAT,
    [GEAR_SLOT_CAPE] = ITEM_GOD_CAPE,
    [GEAR_SLOT_NECK] = ITEM_OCCULT_NECKLACE,
    [GEAR_SLOT_AMMO] = ITEM_DIAMOND_BOLTS_E,
    [GEAR_SLOT_WEAPON] = ITEM_ZURIELS_STAFF,
    [GEAR_SLOT_SHIELD] = ITEM_MAGES_BOOK,
    [GEAR_SLOT_BODY] = ITEM_ANCESTRAL_TOP,
    [GEAR_SLOT_LEGS] = ITEM_ANCESTRAL_BOTTOM,
    [GEAR_SLOT_HANDS] = ITEM_BARROWS_GLOVES,
    [GEAR_SLOT_FEET] = ITEM_ETERNAL_BOOTS,
    [GEAR_SLOT_RING] = ITEM_LIGHTBEARER,
};

static const uint8_t TEST_PVP_RANGE_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD] = ITEM_TORAGS_HELM,
    [GEAR_SLOT_CAPE] = ITEM_INFERNAL_CAPE,
    [GEAR_SLOT_NECK] = ITEM_FURY,
    [GEAR_SLOT_AMMO] = ITEM_OPAL_DRAGON_BOLTS,
    [GEAR_SLOT_WEAPON] = ITEM_ZARYTE_CROSSBOW,
    [GEAR_SLOT_SHIELD] = ITEM_BLESSED_SPIRIT_SHIELD,
    [GEAR_SLOT_BODY] = ITEM_KARILS_TOP,
    [GEAR_SLOT_LEGS] = ITEM_BANDOS_TASSETS,
    [GEAR_SLOT_HANDS] = ITEM_BARROWS_GLOVES,
    [GEAR_SLOT_FEET] = ITEM_CLIMBING_BOOTS,
    [GEAR_SLOT_RING] = ITEM_LIGHTBEARER,
};

static const uint8_t TEST_PVP_MELEE_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD] = ITEM_GUTHANS_HELM,
    [GEAR_SLOT_CAPE] = ITEM_INFERNAL_CAPE,
    [GEAR_SLOT_NECK] = ITEM_FURY,
    [GEAR_SLOT_AMMO] = ITEM_DIAMOND_BOLTS_E,
    [GEAR_SLOT_WEAPON] = ITEM_VESTAS,
    [GEAR_SLOT_SHIELD] = ITEM_DRAGON_DEFENDER,
    [GEAR_SLOT_BODY] = ITEM_KARILS_TOP,
    [GEAR_SLOT_LEGS] = ITEM_VERACS_PLATESKIRT,
    [GEAR_SLOT_HANDS] = ITEM_BARROWS_GLOVES,
    [GEAR_SLOT_FEET] = ITEM_CLIMBING_BOOTS,
    [GEAR_SLOT_RING] = ITEM_BERSERKER_RING,
};

typedef struct {
    const char* name;
    uint32_t id;
} RequiredModel;

typedef struct {
    const char* name;
    int id;
} RequiredAnim;

typedef struct {
    const char* name;
    const uint8_t* equipped;
} RequiredLoadout;

typedef struct {
    const char* name;
    const OsrsCombatProjectileProfile* profile;
} RequiredProjectileProfile;

static int has_anim(AnimCache* first, AnimCache* second, int seq_id) {
    return anim_get_sequence(first, (uint16_t)seq_id) ||
        anim_get_sequence(second, (uint16_t)seq_id);
}

static int has_anim_only(AnimCache* cache, int seq_id) {
    return anim_get_sequence(cache, (uint16_t)seq_id) != NULL;
}

#define ASSERT_ANIM_PRESENT(label, first, second, seq_id) do { \
    tests_run++; \
    if (!has_anim((first), (second), (seq_id))) { \
        tests_failed++; \
        printf("  FAIL: %s missing seq %d\n", (label), (seq_id)); \
    } \
} while (0)

#define ASSERT_ANIM_PRESENT_ONLY(label, cache, seq_id) do { \
    tests_run++; \
    if (!has_anim_only((cache), (seq_id))) { \
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

#define ASSERT_MODEL_PRESENT(label, path, model_id) do { \
    tests_run++; \
    if (!model_file_contains_model((path), (model_id))) { \
        tests_failed++; \
        printf("  FAIL: %s missing model %u in %s\n", \
            (label), (unsigned)(model_id), (path)); \
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

static int model_file_contains_model(const char* path, uint32_t model_id) {
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

    uint32_t* offsets = osrs_malloc_or_abort(count * sizeof(uint32_t),
        "test model offsets");
    osrs_read_exact(f, offsets, 4, count, path, "model offsets");
    int found = 0;
    for (uint32_t i = 0; i < count; i++) {
        osrs_seek_or_abort(f, (long)offsets[i], path);
        uint32_t row_model_id = 0;
        osrs_read_exact(f, &row_model_id, 4, 1, path, "model id");
        if (row_model_id == model_id) {
            found = 1;
            break;
        }
    }

    free(offsets);
    fclose(f);
    return found;
}

static int visual_model_exists_in_render_caches(uint32_t model_id) {
    return model_file_contains_model(OSRS_ASSET("projectiles.models"), model_id) ||
        model_file_contains_model(OSRS_ASSET("equipment.models"), model_id) ||
        model_file_contains_model(OSRS_ASSET("inferno.models"), model_id);
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

static const ItemModelMapping* item_mapping_for_db_index(uint8_t item_idx) {
    if (item_idx >= NUM_ITEMS) return NULL;
    uint16_t item_id = ITEM_DATABASE[item_idx].item_id;
    for (int i = 0; i < ITEM_MODEL_COUNT; i++) {
        if (ITEM_MODEL_MAP[i].item_id == item_id) return &ITEM_MODEL_MAP[i];
    }
    return NULL;
}

static void assert_loadout_models_present(
    const char* name,
    const uint8_t equipped[NUM_GEAR_SLOTS]
) {
    uint32_t hide_mask = 0;
    int suppress_shield = 0;
    uint8_t weapon = equipped[GEAR_SLOT_WEAPON];
    if (weapon < NUM_ITEMS) {
        const ItemModelMapping* weapon_mapping = item_mapping_for_db_index(weapon);
        suppress_shield = item_is_two_handed(weapon) ||
            (weapon_mapping &&
             (weapon_mapping->render_flags & ITEM_RENDER_FLAG_TWO_HANDED) != 0);
    }

    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        if (slot == GEAR_SLOT_SHIELD && suppress_shield) continue;
        uint8_t item_idx = equipped[slot];
        if (item_idx >= NUM_ITEMS) continue;
        const ItemModelMapping* mapping = item_mapping_for_db_index(item_idx);
        if (mapping) hide_mask |= mapping->hide_body_mask;
    }

    for (int part = 0; part < BODY_PART_COUNT; part++) {
        if ((hide_mask & (1u << part)) != 0) continue;
        ASSERT_MODEL_PRESENT(name, OSRS_ASSET("equipment.models"),
            DEFAULT_BODY_MODELS[part]);
    }

    for (size_t i = 0;
            i < sizeof(TEST_VISIBLE_EQUIP_SLOTS) / sizeof(TEST_VISIBLE_EQUIP_SLOTS[0]);
            i++) {
        int slot = TEST_VISIBLE_EQUIP_SLOTS[i];
        if (slot == GEAR_SLOT_SHIELD && suppress_shield) continue;
        uint8_t item_idx = equipped[slot];
        if (item_idx >= NUM_ITEMS || item_idx == ITEM_NONE) continue;
        const ItemModelMapping* mapping = item_mapping_for_db_index(item_idx);
        tests_run++;
        if (!mapping || mapping->wield_model == ITEM_RENDER_MODEL_MISSING) {
            tests_failed++;
            printf("  FAIL: %s item %s has no visible wield model\n",
                name, ITEM_DATABASE[item_idx].name);
            continue;
        }
        if (!model_file_contains_model(OSRS_ASSET("equipment.models"),
                mapping->wield_model)) {
            tests_failed++;
            printf("  FAIL: %s item %s missing wield model %u\n",
                name, ITEM_DATABASE[item_idx].name, (unsigned)mapping->wield_model);
        }
    }
}

static void assert_spotanim_render_asset(
    const char* label,
    const OsrsSpotAnimSet* spotanims,
    AnimCache* equipment,
    AnimCache* inferno,
    int gfx_id
) {
    if (gfx_id == OSRS_COMBAT_PROJECTILE_MISSING || gfx_id < 0) return;
    tests_run++;
    const OsrsSpotAnimDef* def = osrs_spotanim_find(spotanims, gfx_id);
    if (!def || def->model_id < 0) {
        tests_failed++;
        printf("  FAIL: %s missing spotanim %d\n", label, gfx_id);
        return;
    }

    uint32_t synthetic_model = OSRS_SPOTANIM_MODEL_BASE + (uint32_t)def->id;
    tests_run++;
    if (!visual_model_exists_in_render_caches(synthetic_model) &&
            !visual_model_exists_in_render_caches((uint32_t)def->model_id)) {
        tests_failed++;
        printf("  FAIL: %s spotanim %d missing model %u or %u\n",
            label, gfx_id, (unsigned)synthetic_model, (unsigned)def->model_id);
    }

    if (def->animation_id >= 0) {
        ASSERT_ANIM_PRESENT(label, equipment, inferno, def->animation_id);
    }
}

static void assert_projectile_profile_assets(
    const char* label,
    const OsrsCombatProjectileProfile* profile,
    const OsrsSpotAnimSet* spotanims,
    AnimCache* equipment,
    AnimCache* inferno
) {
    tests_run++;
    if (!profile) {
        tests_failed++;
        printf("  FAIL: %s missing projectile profile\n", label);
        return;
    }

    assert_spotanim_render_asset(label, spotanims, equipment, inferno,
        profile->launch_spotanim_id);
    assert_spotanim_render_asset(label, spotanims, equipment, inferno,
        profile->travel_spotanim_id);
    assert_spotanim_render_asset(label, spotanims, equipment, inferno,
        profile->impact_spotanim_id);

    if (profile->projectile_model_id != OSRS_COMBAT_PROJECTILE_MISSING) {
        tests_run++;
        if (!visual_model_exists_in_render_caches((uint32_t)profile->projectile_model_id)) {
            tests_failed++;
            printf("  FAIL: %s missing explicit projectile model %d\n",
                label, profile->projectile_model_id);
        }
    }
    if (profile->projectile_anim_id != OSRS_COMBAT_PROJECTILE_MISSING) {
        ASSERT_ANIM_PRESENT(label, equipment, inferno, profile->projectile_anim_id);
    }
}

static void assert_combat_visual_row_assets(
    const char* label,
    const OsrsCombatVisualRow* row,
    const OsrsSpotAnimSet* spotanims,
    AnimCache* equipment,
    AnimCache* inferno
) {
    tests_run++;
    if (!row) {
        tests_failed++;
        printf("  FAIL: %s missing combat visual row\n", label);
        return;
    }

    assert_projectile_profile_assets(label, &row->projectile, spotanims, equipment, inferno);
    assert_spotanim_render_asset(label, spotanims, equipment, inferno,
        row->aux_travel_spotanim_id);
    assert_spotanim_render_asset(label, spotanims, equipment, inferno,
        row->aux_impact_spotanim_id);
    assert_spotanim_render_asset(label, spotanims, equipment, inferno,
        row->double_launch_spotanim_id);

    if (row->aux_projectile_model_id != OSRS_COMBAT_PROJECTILE_MISSING) {
        tests_run++;
        if (!visual_model_exists_in_render_caches((uint32_t)row->aux_projectile_model_id)) {
            tests_failed++;
            printf("  FAIL: %s missing aux projectile model %d\n",
                label, row->aux_projectile_model_id);
        }
    }
    if (row->aux_projectile_anim_id != OSRS_COMBAT_PROJECTILE_MISSING) {
        ASSERT_ANIM_PRESENT(label, equipment, inferno, row->aux_projectile_anim_id);
    }
}

static void assert_visible_item_render_asset(uint8_t item_idx) {
    if (item_idx >= NUM_ITEMS) return;
    int slot = ITEM_DATABASE[item_idx].slot;
    if (slot == GEAR_SLOT_AMMO || slot == GEAR_SLOT_RING || slot < 0) return;

    const ItemModelMapping* mapping = item_mapping_for_db_index(item_idx);
    tests_run++;
    if (!mapping || mapping->wield_model == ITEM_RENDER_MODEL_MISSING) {
        tests_failed++;
        printf("  FAIL: pvp item %s has no visible wield model\n",
            ITEM_DATABASE[item_idx].name);
        return;
    }
    if (!model_file_contains_model(OSRS_ASSET("equipment.models"), mapping->wield_model)) {
        tests_failed++;
        printf("  FAIL: pvp item %s missing wield model %u\n",
            ITEM_DATABASE[item_idx].name, (unsigned)mapping->wield_model);
    }
}

static const OsrsCombatProjectileProfile* test_npc_projectile_profile(
    uint16_t npc_id,
    AttackStyle style
) {
    const OsrsCombatVisualRow* row = osrs_combat_visual_find_npc_id(npc_id, style);
    return row ? &row->projectile : NULL;
}

static void test_inferno_render_asset_contract(
    AnimCache* equipment,
    AnimCache* inferno,
    const OsrsSpotAnimSet* spotanims
) {
    printf("--- inferno render asset contract ---\n");

    const RequiredLoadout loadouts[] = {
        {"max mage", TEST_MAX_MAGE_LOADOUT},
        {"max long range", TEST_MAX_RANGE_LONG_LOADOUT},
        {"max fast range", TEST_MAX_RANGE_FAST_LOADOUT},
        {"budget mage", TEST_BUDGET_MAGE_LOADOUT},
        {"budget long range", TEST_BUDGET_RANGE_LONG_LOADOUT},
        {"budget fast range", TEST_BUDGET_RANGE_FAST_LOADOUT},
        {"pvp basic", TEST_PVP_BASIC_LOADOUT},
        {"pvp mage", TEST_PVP_MAGE_LOADOUT},
        {"pvp range", TEST_PVP_RANGE_LOADOUT},
        {"pvp melee", TEST_PVP_MELEE_LOADOUT},
    };
    for (size_t i = 0; i < sizeof(loadouts) / sizeof(loadouts[0]); i++) {
        assert_loadout_models_present(loadouts[i].name, loadouts[i].equipped);
    }

    for (size_t i = 0;
            i < sizeof(NPC_MODEL_MAP_INFERNO_GEN) / sizeof(NPC_MODEL_MAP_INFERNO_GEN[0]);
            i++) {
        const NpcModelMapping* npc = &NPC_MODEL_MAP_INFERNO_GEN[i];
        ASSERT_MODEL_PRESENT("inferno npc", OSRS_ASSET("inferno.models"),
            npc->model_id);
        ASSERT_ANIM_PRESENT("inferno npc idle", equipment, inferno,
            (int)npc->idle_anim);
        if (npc->attack_anim != 65535) {
            ASSERT_ANIM_PRESENT("inferno npc attack", equipment, inferno,
                (int)npc->attack_anim);
        }
        if (npc->walk_anim != 65535) {
            ASSERT_ANIM_PRESENT("inferno npc walk", equipment, inferno,
                (int)npc->walk_anim);
        }
    }

    printf("--- zulrah render asset contract ---\n");
    for (size_t i = 0;
            i < sizeof(NPC_MODEL_MAP_ZULRAH_GEN) / sizeof(NPC_MODEL_MAP_ZULRAH_GEN[0]);
            i++) {
        const NpcModelMapping* npc = &NPC_MODEL_MAP_ZULRAH_GEN[i];
        ASSERT_MODEL_PRESENT("zulrah npc", OSRS_ASSET("zulrah.models"),
            npc->model_id);
    }

    const RequiredAnim extra_anims[] = {
        {"nibbler defend", INF_GEN_ANIM_NIBBLER_DEFEND},
        {"nibbler death", INF_GEN_ANIM_NIBBLER_DEATH},
        {"bat death", INF_GEN_ANIM_BAT_DEATH},
        {"blob melee attack", INF_GEN_ANIM_BLOB_ATTACK_MELEE},
        {"blob ranged attack", INF_GEN_ANIM_BLOB_ATTACK_RANGED},
        {"blob death", INF_GEN_ANIM_BLOB_DEATH},
        {"meleer defend", INF_GEN_ANIM_MELEER_DEFEND},
        {"meleer death", INF_GEN_ANIM_MELEER_DEATH},
        {"meleer dig down", INF_GEN_ANIM_MELEER_DIG_DOWN},
        {"meleer dig up", INF_GEN_ANIM_MELEER_DIG_UP},
        {"ranger melee attack", INF_GEN_ANIM_RANGER_ATTACK_MELEE},
        {"ranger death", INF_GEN_ANIM_RANGER_DEATH},
        {"mager resurrect", INF_GEN_ANIM_MAGER_RESURRECT},
        {"mager melee attack", INF_GEN_ANIM_MAGER_ATTACK_MELEE},
        {"mager death", INF_GEN_ANIM_MAGER_DEATH},
        {"jad melee attack", INF_GEN_ANIM_JALTOK_JAD_ATTACK_MELEE},
        {"jad defend", INF_GEN_ANIM_JALTOK_JAD_DEFEND},
        {"jad magic attack", INF_GEN_ANIM_JALTOK_JAD_ATTACK_MAGIC},
        {"jad ranged attack", INF_GEN_ANIM_JALTOK_JAD_ATTACK_RANGED},
        {"jad death", INF_GEN_ANIM_JALTOK_JAD_DEATH},
        {"zuk death", INF_GEN_ANIM_TZKAL_ZUK_DEATH},
        {"zuk spawn", INF_GEN_ANIM_TZKAL_ZUK_SPAWN},
        {"zuk defend", INF_GEN_ANIM_TZKAL_ZUK_DEFEND},
        {"zuk shield hit", INF_GEN_ANIM_ZUK_SHIELD_HIT},
        {"zuk shield death", INF_GEN_ANIM_ZUK_SHIELD_DEATH},
    };
    for (size_t i = 0; i < sizeof(extra_anims) / sizeof(extra_anims[0]); i++) {
        ASSERT_ANIM_PRESENT(extra_anims[i].name, equipment, inferno, extra_anims[i].id);
    }

    const RequiredModel pillar_models[] = {
        {"pillar full", INF_PILLAR_MODEL_100},
        {"pillar 75", INF_PILLAR_MODEL_75},
        {"pillar 50", INF_PILLAR_MODEL_50},
        {"pillar 25", INF_PILLAR_MODEL_25},
    };
    for (size_t i = 0; i < sizeof(pillar_models) / sizeof(pillar_models[0]); i++) {
        ASSERT_MODEL_PRESENT(pillar_models[i].name, OSRS_ASSET("equipment.models"),
            pillar_models[i].id);
    }

    const RequiredProjectileProfile profiles[] = {
        {"twisted bow",
            osrs_combat_visual_ranged_projectile_profile(
                ITEM_TWISTED_BOW, OSRS_COMBAT_PROJECTILE_NONE)},
        {"bowfa",
            osrs_combat_visual_ranged_projectile_profile(
                ITEM_BOW_OF_FAERDHINEN, OSRS_COMBAT_PROJECTILE_NONE)},
        {"blowpipe",
            osrs_combat_visual_ranged_projectile_profile(
                ITEM_TOXIC_BLOWPIPE, OSRS_COMBAT_PROJECTILE_NONE)},
        {"pvp rune crossbow",
            osrs_combat_visual_ranged_projectile_profile(
                ITEM_RUNE_CROSSBOW, OSRS_COMBAT_PROJECTILE_NONE)},
        {"pvp armadyl crossbow",
            osrs_combat_visual_ranged_projectile_profile(
                ITEM_ARMADYL_CROSSBOW, OSRS_COMBAT_PROJECTILE_NONE)},
        {"pvp zaryte crossbow",
            osrs_combat_visual_ranged_projectile_profile(
                ITEM_ZARYTE_CROSSBOW, OSRS_COMBAT_PROJECTILE_NONE)},
        {"pvp dark bow",
            osrs_combat_visual_ranged_projectile_profile(
                ITEM_DARK_BOW, OSRS_COMBAT_PROJECTILE_NONE)},
        {"pvp heavy ballista",
            osrs_combat_visual_ranged_projectile_profile(
                ITEM_HEAVY_BALLISTA, OSRS_COMBAT_PROJECTILE_NONE)},
        {"pvp morrigans javelin",
            osrs_combat_visual_ranged_projectile_profile(
                ITEM_MORRIGANS_JAVELIN, OSRS_COMBAT_PROJECTILE_NONE)},
        {"ice barrage",
            osrs_combat_visual_spell_projectile(
                OSRS_COMBAT_VISUAL_SPELL_ICE_BARRAGE)},
        {"blood barrage",
            osrs_combat_visual_spell_projectile(
                OSRS_COMBAT_VISUAL_SPELL_BLOOD_BARRAGE)},
        {"bat ranged",
            test_npc_projectile_profile(7692, ATTACK_STYLE_RANGED)},
        {"blob melee",
            test_npc_projectile_profile(7693, ATTACK_STYLE_MELEE)},
        {"blob ranged",
            test_npc_projectile_profile(7693, ATTACK_STYLE_RANGED)},
        {"blob magic",
            test_npc_projectile_profile(7693, ATTACK_STYLE_MAGIC)},
        {"ranger ranged",
            test_npc_projectile_profile(7698, ATTACK_STYLE_RANGED)},
        {"mager magic",
            test_npc_projectile_profile(7699, ATTACK_STYLE_MAGIC)},
        {"jad magic",
            test_npc_projectile_profile(7700, ATTACK_STYLE_MAGIC)},
        {"jad ranged",
            test_npc_projectile_profile(7700, ATTACK_STYLE_RANGED)},
        {"zuk",
            test_npc_projectile_profile(7706, ATTACK_STYLE_MAGIC)},
        {"zuk healer",
            test_npc_projectile_profile(7708, ATTACK_STYLE_MAGIC)},
    };
    for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        assert_projectile_profile_assets(
            profiles[i].name, profiles[i].profile, spotanims, equipment, inferno);
    }

    const uint8_t pvp_items[] = {
        ITEM_HELM_NEITIZNOT,
        ITEM_GOD_CAPE,
        ITEM_GLORY,
        ITEM_BLACK_DHIDE_BODY,
        ITEM_MYSTIC_TOP,
        ITEM_RUNE_PLATELEGS,
        ITEM_MYSTIC_BOTTOM,
        ITEM_WHIP,
        ITEM_RUNE_CROSSBOW,
        ITEM_AHRIM_STAFF,
        ITEM_DRAGON_DAGGER,
        ITEM_DRAGON_DEFENDER,
        ITEM_SPIRIT_SHIELD,
        ITEM_BARROWS_GLOVES,
        ITEM_CLIMBING_BOOTS,
        ITEM_GHRAZI_RAPIER,
        ITEM_INQUISITORS_MACE,
        ITEM_STAFF_OF_DEAD,
        ITEM_KODAI_WAND,
        ITEM_VOLATILE_STAFF,
        ITEM_ZURIELS_STAFF,
        ITEM_ARMADYL_CROSSBOW,
        ITEM_ZARYTE_CROSSBOW,
        ITEM_DRAGON_CLAWS,
        ITEM_AGS,
        ITEM_ANCIENT_GS,
        ITEM_GRANITE_MAUL,
        ITEM_ELDER_MAUL,
        ITEM_DARK_BOW,
        ITEM_HEAVY_BALLISTA,
        ITEM_VESTAS,
        ITEM_VOIDWAKER,
        ITEM_STATIUS_WARHAMMER,
        ITEM_MORRIGANS_JAVELIN,
        ITEM_ANCESTRAL_HAT,
        ITEM_ANCESTRAL_TOP,
        ITEM_ANCESTRAL_BOTTOM,
        ITEM_AHRIMS_ROBETOP,
        ITEM_AHRIMS_ROBESKIRT,
        ITEM_KARILS_TOP,
        ITEM_BANDOS_TASSETS,
        ITEM_BLESSED_SPIRIT_SHIELD,
        ITEM_FURY,
        ITEM_OCCULT_NECKLACE,
        ITEM_INFERNAL_CAPE,
        ITEM_ETERNAL_BOOTS,
        ITEM_MAGES_BOOK,
        ITEM_TORAGS_PLATELEGS,
        ITEM_DHAROKS_PLATELEGS,
        ITEM_VERACS_PLATESKIRT,
        ITEM_TORAGS_HELM,
        ITEM_DHAROKS_HELM,
        ITEM_VERACS_HELM,
        ITEM_GUTHANS_HELM,
    };
    for (size_t i = 0; i < sizeof(pvp_items) / sizeof(pvp_items[0]); i++) {
        assert_visible_item_render_asset(pvp_items[i]);
    }

    assert_combat_visual_row_assets(
        "pvp dark bow special",
        osrs_combat_visual_find_special_projectile_item_id(
            OSRS_ITEM_ID_DARK_BOW, ATTACK_STYLE_RANGED),
        spotanims, equipment, inferno);
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
    ASSERT_ANIM_PRESENT("zulrah death", equipment, zulrah, ZULRAH_ANIM_DEATH);
    ASSERT_ANIM_PRESENT_ONLY("zulrah-cache death", zulrah, ZULRAH_ANIM_DEATH);
    ASSERT_ANIM_PRESENT("snakeling idle", equipment, zulrah, SNAKELING_ANIM_IDLE);
    ASSERT_ANIM_PRESENT("snakeling melee attack", equipment, zulrah, SNAKELING_ANIM_MELEE);
    ASSERT_ANIM_PRESENT("snakeling magic attack", equipment, zulrah, SNAKELING_ANIM_MAGIC);
    ASSERT_ANIM_PRESENT("snakeling death", equipment, zulrah, SNAKELING_ANIM_DEATH);
    ASSERT_ANIM_PRESENT("snakeling walk", equipment, zulrah, SNAKELING_ANIM_WALK);
    ASSERT_ANIM_PRESENT_ONLY("zulrah-cache snakeling idle", zulrah, SNAKELING_ANIM_IDLE);
    ASSERT_ANIM_PRESENT_ONLY("zulrah-cache snakeling melee attack", zulrah, SNAKELING_ANIM_MELEE);
    ASSERT_ANIM_PRESENT_ONLY("zulrah-cache snakeling magic attack", zulrah, SNAKELING_ANIM_MAGIC);
    ASSERT_ANIM_PRESENT_ONLY("zulrah-cache snakeling death", zulrah, SNAKELING_ANIM_DEATH);
    ASSERT_ANIM_PRESENT_ONLY("zulrah-cache snakeling walk", zulrah, SNAKELING_ANIM_WALK);

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
    ASSERT_SPOTANIM_PRESENT("trident cast", spotanims, 665);
    ASSERT_SPOTANIM_PRESENT("trident projectile", spotanims, 1040);
    ASSERT_SPOTANIM_PRESENT("trident impact", spotanims, 1042);
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

    test_inferno_render_asset_contract(equipment, inferno, spotanims);

    osrs_spotanims_free(spotanims);

    if (tests_failed > 0) {
        printf("\n%d/%d animation export checks failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("\n%d/%d animation export checks passed\n", tests_run, tests_run);
    return 0;
}
