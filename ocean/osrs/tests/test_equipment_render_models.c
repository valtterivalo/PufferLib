#include "../osrs_items.h"
#include "../osrs_models.h"
#include "../osrs_assets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t hide_mask_for_item(int item_index) {
    return item_hide_body_mask(ITEM_DATABASE[item_index].item_id);
}

static uint32_t equip_slot_for_item(int item_index) {
    return item_render_equip_slot(ITEM_DATABASE[item_index].item_id);
}

static uint32_t render_flags_for_item(int item_index) {
    return item_render_flags(ITEM_DATABASE[item_index].item_id);
}

static uint32_t ready_anim_for_item(int item_index) {
    return item_render_ready_anim(ITEM_DATABASE[item_index].item_id);
}

static uint32_t walk_anim_for_item(int item_index) {
    return item_render_walk_anim(ITEM_DATABASE[item_index].item_id);
}

static uint32_t run_anim_for_item(int item_index) {
    return item_render_run_anim(ITEM_DATABASE[item_index].item_id);
}

static uint32_t wield_model_for_item(int item_index) {
    return item_to_wield_model(ITEM_DATABASE[item_index].item_id);
}

typedef struct {
    int slot;
    uint32_t model_id;
} ExpectedSlotModel;

typedef struct {
    int body_part;
    uint32_t model_id;
} ExpectedBodyModel;

static const ExpectedBodyModel DEFAULT_VISIBLE_BODY_MODELS[] = {
    { BODY_PART_HEAD, 0xF0000u },
    { BODY_PART_JAW, 0xF0001u },
    { BODY_PART_TORSO, 0xF0002u },
    { BODY_PART_ARMS, 0xF0003u },
    { BODY_PART_HANDS, 0xF0004u },
    { BODY_PART_LEGS, 0xF0005u },
    { BODY_PART_FEET, 0xF0006u },
};

static int model_file_contains_model(const char* path, uint32_t model_id) {
    FILE* f = fopen(path, "rb");
    assert(f);

    uint32_t magic = 0;
    uint32_t count = 0;
    assert(fread(&magic, sizeof(magic), 1, f) == 1);
    assert(fread(&count, sizeof(count), 1, f) == 1);
    assert(magic == MDL2_MAGIC || magic == MDL3_MAGIC || magic == MDL4_MAGIC);

    uint32_t* offsets = malloc((size_t)count * sizeof(uint32_t));
    assert(offsets);
    assert(fread(offsets, sizeof(uint32_t), count, f) == count);

    int found = 0;
    for (uint32_t i = 0; i < count; i++) {
        assert(fseek(f, (long)offsets[i], SEEK_SET) == 0);
        uint32_t candidate = 0;
        assert(fread(&candidate, sizeof(candidate), 1, f) == 1);
        if (candidate == model_id) {
            found = 1;
            break;
        }
    }

    free(offsets);
    assert(fclose(f) == 0);
    return found;
}

static void test_equipment_texture_animation_rows_load(void) {
    ModelCache cache = {0};
    model_cache_load_texture_anims(&cache, OSRS_ASSET("equipment.models"));

    assert(cache.texture_anim_count == 22);
    assert(cache.texture_anims);
    assert(cache.texture_anims[0].texture_id == 17);
    assert(cache.texture_anims[0].x == 256);
    assert(cache.texture_anims[0].y == 384);
    assert(cache.texture_anims[0].w == 128);
    assert(cache.texture_anims[0].h == 384);
    assert(cache.texture_anims[0].direction == 1);
    assert(cache.texture_anims[0].speed == 2);
    assert(cache.texture_anims[0].pad == 128);

    free(cache.texture_anims);
}

static void test_model_cache_get_uses_index_with_safe_fallback(void) {
    OsrsModel models[2] = {0};
    models[0].model_id = 7;
    models[1].model_id = 300;

    int index_by_id[16];
    for (int i = 0; i < 16; i++) {
        index_by_id[i] = -1;
    }
    index_by_id[7] = 1;

    ModelCache cache = {
        .models = models,
        .index_by_id = index_by_id,
        .count = 2,
        .index_limit = 16,
    };

    assert(model_cache_get(&cache, 7) == &models[0]);
    assert(model_cache_get(&cache, 300) == &models[1]);
    assert(model_cache_get(&cache, 404) == NULL);
}

static void test_model_append_overflow_status_is_explicit(void) {
    int16_t base_vertices[6] = {0};
    OsrsModel model = {
        .model_id = 4242,
        .base_vertices = base_vertices,
        .base_vert_count = 2,
    };
    model.mesh.triangleCount = 2;

    assert(osrs_model_append_check(3, 8, &model, 5, 10) == OSRS_MODEL_APPEND_OK);
    assert(osrs_model_append_check(4, 8, &model, 5, 10) ==
        OSRS_MODEL_APPEND_BASE_VERTEX_OVERFLOW);
    assert(osrs_model_append_check(3, 9, &model, 5, 10) ==
        OSRS_MODEL_APPEND_FACE_OVERFLOW);
    assert(osrs_model_append_check(0, 0, NULL, 5, 10) == OSRS_MODEL_APPEND_EMPTY);
}

static void assert_runtime_model_present(int item_index) {
    uint32_t model_id = wield_model_for_item(item_index);
    assert(model_id != ITEM_RENDER_MODEL_MISSING);
    assert(model_file_contains_model(OSRS_ASSET("equipment.models"), model_id));
}

static void assert_render_slot_matches_db(int item_index) {
    assert(equip_slot_for_item(item_index) == ITEM_DATABASE[item_index].slot);
}

static void clear_equipment(uint8_t equipped[NUM_GEAR_SLOTS]) {
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) {
        equipped[i] = ITEM_NONE;
    }
}

static void assert_body_models_present(const OsrsPlayerAppearance* appearance) {
    for (int bp = 0; bp < BODY_PART_COUNT; bp++) {
        if (!appearance->body_visible[bp]) continue;
        assert(appearance->body_model_ids[bp] != ITEM_RENDER_MODEL_MISSING);
        assert(model_file_contains_model(
            OSRS_ASSET("equipment.models"), appearance->body_model_ids[bp]));
    }
}

static void assert_item_models_present(const OsrsPlayerAppearance* appearance) {
    for (int i = 0; i < OSRS_VISIBLE_EQUIP_SLOT_COUNT; i++) {
        int slot = OSRS_VISIBLE_EQUIP_SLOTS[i];
        if (!appearance->item_visible[slot]) continue;
        assert(appearance->item_model_ids[slot] != ITEM_RENDER_MODEL_MISSING);
        assert(model_file_contains_model(
            OSRS_ASSET("equipment.models"), appearance->item_model_ids[slot]));
    }
}

static void assert_resolved_models_present(const uint8_t equipped[NUM_GEAR_SLOTS]) {
    OsrsPlayerAppearance appearance = osrs_resolve_player_appearance(equipped);
    assert_body_models_present(&appearance);
    assert_item_models_present(&appearance);
}

static uint32_t expected_slot_model_or_missing(
    const ExpectedSlotModel* expected,
    int expected_count,
    int slot
) {
    for (int i = 0; i < expected_count; i++) {
        if (expected[i].slot == slot) return expected[i].model_id;
    }
    return ITEM_RENDER_MODEL_MISSING;
}

static uint32_t expected_body_model_or_missing(
    const ExpectedBodyModel* expected,
    int expected_count,
    int body_part
) {
    for (int i = 0; i < expected_count; i++) {
        if (expected[i].body_part == body_part) return expected[i].model_id;
    }
    return ITEM_RENDER_MODEL_MISSING;
}

static void assert_expected_player_model_ids(
    const uint8_t equipped[NUM_GEAR_SLOTS],
    const ExpectedBodyModel* expected_body_models,
    int expected_body_model_count,
    const ExpectedSlotModel* expected_slot_models,
    int expected_slot_model_count
) {
    OsrsPlayerAppearance appearance = osrs_resolve_player_appearance(equipped);

    for (int bp = 0; bp < BODY_PART_COUNT; bp++) {
        uint32_t expected = expected_body_model_or_missing(
            expected_body_models, expected_body_model_count, bp);
        if (expected == ITEM_RENDER_MODEL_MISSING) {
            assert(!appearance.body_visible[bp]);
        } else {
            assert(appearance.body_visible[bp]);
            assert(appearance.body_model_ids[bp] == expected);
        }
    }

    for (int i = 0; i < OSRS_VISIBLE_EQUIP_SLOT_COUNT; i++) {
        int slot = OSRS_VISIBLE_EQUIP_SLOTS[i];
        uint32_t expected = expected_slot_model_or_missing(
            expected_slot_models, expected_slot_model_count, slot);
        if (expected == ITEM_RENDER_MODEL_MISSING) {
            assert(!appearance.item_visible[slot]);
        } else {
            assert(appearance.item_visible[slot]);
            assert(appearance.item_model_ids[slot] == expected);
        }
    }
}

static void set_equipment(
    uint8_t equipped[NUM_GEAR_SLOTS],
    const uint8_t loadout[NUM_GEAR_SLOTS]
) {
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) {
        equipped[i] = loadout[i];
    }
}

static const uint8_t MAX_MAGE_LOADOUT[NUM_GEAR_SLOTS] = {
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

static const uint8_t MAX_RANGE_LONG_LOADOUT[NUM_GEAR_SLOTS] = {
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

static const uint8_t MAX_RANGE_FAST_LOADOUT[NUM_GEAR_SLOTS] = {
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

static const uint8_t BUDGET_MAGE_LOADOUT[NUM_GEAR_SLOTS] = {
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

static const uint8_t BUDGET_RANGE_LONG_LOADOUT[NUM_GEAR_SLOTS] = {
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

static const uint8_t BUDGET_RANGE_FAST_LOADOUT[NUM_GEAR_SLOTS] = {
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

static const ExpectedSlotModel MAX_MAGE_MODELS[] = {
    { GEAR_SLOT_HEAD, 917571 },
    { GEAR_SLOT_CAPE, 917575 },
    { GEAR_SLOT_NECK, 917549 },
    { GEAR_SLOT_WEAPON, 917524 },
    { GEAR_SLOT_SHIELD, 917606 },
    { GEAR_SLOT_BODY, 917607 },
    { GEAR_SLOT_LEGS, 917608 },
    { GEAR_SLOT_HANDS, 917567 },
    { GEAR_SLOT_FEET, 917568 },
};

static const ExpectedSlotModel MAX_RANGE_LONG_MODELS[] = {
    { GEAR_SLOT_HEAD, 917571 },
    { GEAR_SLOT_CAPE, 917575 },
    { GEAR_SLOT_NECK, 917574 },
    { GEAR_SLOT_WEAPON, 917570 },
    { GEAR_SLOT_BODY, 917572 },
    { GEAR_SLOT_LEGS, 917573 },
    { GEAR_SLOT_HANDS, 917576 },
    { GEAR_SLOT_FEET, 917568 },
};

static const ExpectedSlotModel MAX_RANGE_FAST_MODELS[] = {
    { GEAR_SLOT_HEAD, 917571 },
    { GEAR_SLOT_CAPE, 917575 },
    { GEAR_SLOT_NECK, 917574 },
    { GEAR_SLOT_WEAPON, 917577 },
    { GEAR_SLOT_BODY, 917572 },
    { GEAR_SLOT_LEGS, 917573 },
    { GEAR_SLOT_HANDS, 917576 },
    { GEAR_SLOT_FEET, 917568 },
};

static const ExpectedSlotModel BUDGET_MAGE_MODELS[] = {
    { GEAR_SLOT_HEAD, 917584 },
    { GEAR_SLOT_CAPE, 917575 },
    { GEAR_SLOT_NECK, 917549 },
    { GEAR_SLOT_WEAPON, 917589 },
    { GEAR_SLOT_SHIELD, 917604 },
    { GEAR_SLOT_BODY, 917543 },
    { GEAR_SLOT_LEGS, 917544 },
    { GEAR_SLOT_HANDS, 917567 },
    { GEAR_SLOT_FEET, 917590 },
};

static const ExpectedSlotModel BUDGET_RANGE_LONG_MODELS[] = {
    { GEAR_SLOT_HEAD, 917584 },
    { GEAR_SLOT_CAPE, 917575 },
    { GEAR_SLOT_NECK, 917574 },
    { GEAR_SLOT_WEAPON, 917588 },
    { GEAR_SLOT_BODY, 917586 },
    { GEAR_SLOT_LEGS, 917587 },
    { GEAR_SLOT_HANDS, 917517 },
    { GEAR_SLOT_FEET, 917590 },
};

static const ExpectedSlotModel BUDGET_RANGE_FAST_MODELS[] = {
    { GEAR_SLOT_HEAD, 917584 },
    { GEAR_SLOT_CAPE, 917575 },
    { GEAR_SLOT_NECK, 917574 },
    { GEAR_SLOT_WEAPON, 917577 },
    { GEAR_SLOT_BODY, 917586 },
    { GEAR_SLOT_LEGS, 917587 },
    { GEAR_SLOT_HANDS, 917517 },
    { GEAR_SLOT_FEET, 917590 },
};

static const ExpectedSlotModel MIXED_WAND_SHIELD_MODELS[] = {
    { GEAR_SLOT_WEAPON, 917589 },
    { GEAR_SLOT_SHIELD, 917604 },
};

static const ExpectedSlotModel MIXED_BOWFA_SHIELD_MODELS[] = {
    { GEAR_SLOT_WEAPON, 917588 },
};

static void test_expected_player_model_ids(void) {
    uint8_t equipped[NUM_GEAR_SLOTS];

    set_equipment(equipped, MAX_MAGE_LOADOUT);
    assert_expected_player_model_ids(equipped, NULL, 0,
        MAX_MAGE_MODELS, (int)(sizeof(MAX_MAGE_MODELS) / sizeof(MAX_MAGE_MODELS[0])));

    set_equipment(equipped, MAX_RANGE_LONG_LOADOUT);
    assert_expected_player_model_ids(equipped, NULL, 0,
        MAX_RANGE_LONG_MODELS,
        (int)(sizeof(MAX_RANGE_LONG_MODELS) / sizeof(MAX_RANGE_LONG_MODELS[0])));

    set_equipment(equipped, MAX_RANGE_FAST_LOADOUT);
    assert_expected_player_model_ids(equipped, NULL, 0,
        MAX_RANGE_FAST_MODELS,
        (int)(sizeof(MAX_RANGE_FAST_MODELS) / sizeof(MAX_RANGE_FAST_MODELS[0])));

    set_equipment(equipped, BUDGET_MAGE_LOADOUT);
    assert_expected_player_model_ids(equipped, NULL, 0,
        BUDGET_MAGE_MODELS,
        (int)(sizeof(BUDGET_MAGE_MODELS) / sizeof(BUDGET_MAGE_MODELS[0])));

    set_equipment(equipped, BUDGET_RANGE_LONG_LOADOUT);
    assert_expected_player_model_ids(equipped, NULL, 0,
        BUDGET_RANGE_LONG_MODELS,
        (int)(sizeof(BUDGET_RANGE_LONG_MODELS) / sizeof(BUDGET_RANGE_LONG_MODELS[0])));

    set_equipment(equipped, BUDGET_RANGE_FAST_LOADOUT);
    assert_expected_player_model_ids(equipped, NULL, 0,
        BUDGET_RANGE_FAST_MODELS,
        (int)(sizeof(BUDGET_RANGE_FAST_MODELS) / sizeof(BUDGET_RANGE_FAST_MODELS[0])));

    clear_equipment(equipped);
    equipped[GEAR_SLOT_WEAPON] = ITEM_DRAGON_HUNTER_WAND;
    equipped[GEAR_SLOT_SHIELD] = ITEM_CRYSTAL_SHIELD;
    assert_expected_player_model_ids(
        equipped,
        DEFAULT_VISIBLE_BODY_MODELS,
        (int)(sizeof(DEFAULT_VISIBLE_BODY_MODELS) / sizeof(DEFAULT_VISIBLE_BODY_MODELS[0])),
        MIXED_WAND_SHIELD_MODELS,
        (int)(sizeof(MIXED_WAND_SHIELD_MODELS) / sizeof(MIXED_WAND_SHIELD_MODELS[0])));

    clear_equipment(equipped);
    equipped[GEAR_SLOT_WEAPON] = ITEM_BOW_OF_FAERDHINEN;
    equipped[GEAR_SLOT_SHIELD] = ITEM_CRYSTAL_SHIELD;
    assert_expected_player_model_ids(
        equipped,
        DEFAULT_VISIBLE_BODY_MODELS,
        (int)(sizeof(DEFAULT_VISIBLE_BODY_MODELS) / sizeof(DEFAULT_VISIBLE_BODY_MODELS[0])),
        MIXED_BOWFA_SHIELD_MODELS,
        (int)(sizeof(MIXED_BOWFA_SHIELD_MODELS) / sizeof(MIXED_BOWFA_SHIELD_MODELS[0])));
}

int main(void) {
    test_equipment_texture_animation_rows_load();
    test_model_cache_get_uses_index_with_safe_fallback();
    test_model_append_overflow_status_is_explicit();

    assert(hide_mask_for_item(ITEM_TWISTED_BOW) == 0);
    assert(hide_mask_for_item(ITEM_KODAI_WAND) == 0);
    assert(hide_mask_for_item(ITEM_CRYSTAL_HELM) ==
        ((1u << BODY_PART_HEAD) | (1u << BODY_PART_JAW)));
    assert(hide_mask_for_item(ITEM_BARROWS_GLOVES) == (1u << BODY_PART_HANDS));
    assert(hide_mask_for_item(ITEM_MYSTIC_BOOTS) == (1u << BODY_PART_FEET));
    assert(hide_mask_for_item(ITEM_AHRIMS_ROBESKIRT) == (1u << BODY_PART_LEGS));
    assert(hide_mask_for_item(ITEM_AHRIMS_ROBETOP) ==
        ((1u << BODY_PART_TORSO) | (1u << BODY_PART_ARMS)));
    assert(hide_mask_for_item(ITEM_CRYSTAL_BODY) ==
        ((1u << BODY_PART_TORSO) | (1u << BODY_PART_ARMS)));
    assert(hide_mask_for_item(ITEM_ECHO_BOOTS) == (1u << BODY_PART_FEET));
    assert(hide_mask_for_item(ITEM_DRAGON_HUNTER_WAND) == 0);

    assert(wield_model_for_item(ITEM_DRAGON_HUNTER_WAND) != ITEM_RENDER_MODEL_MISSING);
    assert(wield_model_for_item(ITEM_ECHO_BOOTS) != ITEM_RENDER_MODEL_MISSING);
    assert(wield_model_for_item(ITEM_BOW_OF_FAERDHINEN) != ITEM_RENDER_MODEL_MISSING);
    assert(wield_model_for_item(ITEM_VENATOR_RING) == ITEM_RENDER_MODEL_MISSING);
    assert_runtime_model_present(ITEM_ELYSIAN_SPIRIT_SHIELD);
    assert_runtime_model_present(ITEM_VIRTUS_ROBE_TOP);
    assert_runtime_model_present(ITEM_VIRTUS_ROBE_BOTTOM);

    assert_render_slot_matches_db(ITEM_MASORI_MASK_F);
    assert_render_slot_matches_db(ITEM_DIZANAS_QUIVER);
    assert_render_slot_matches_db(ITEM_OCCULT_NECKLACE);
    assert_render_slot_matches_db(ITEM_DRAGON_ARROWS);
    assert_render_slot_matches_db(ITEM_KODAI_WAND);
    assert_render_slot_matches_db(ITEM_ELYSIAN_SPIRIT_SHIELD);
    assert_render_slot_matches_db(ITEM_VIRTUS_ROBE_TOP);
    assert_render_slot_matches_db(ITEM_VIRTUS_ROBE_BOTTOM);
    assert_render_slot_matches_db(ITEM_CONFLICTION_GAUNTLETS);
    assert_render_slot_matches_db(ITEM_AVERNIC_TREADS);
    assert_render_slot_matches_db(ITEM_VENATOR_RING);
    assert_render_slot_matches_db(ITEM_BOW_OF_FAERDHINEN);
    assert_render_slot_matches_db(ITEM_DRAGON_HUNTER_WAND);
    assert_render_slot_matches_db(ITEM_ECHO_BOOTS);

    assert((render_flags_for_item(ITEM_TWISTED_BOW) & ITEM_RENDER_FLAG_TWO_HANDED) != 0);
    assert((render_flags_for_item(ITEM_BOW_OF_FAERDHINEN) & ITEM_RENDER_FLAG_TWO_HANDED) != 0);
    assert((render_flags_for_item(ITEM_TOXIC_BLOWPIPE) & ITEM_RENDER_FLAG_TWO_HANDED) != 0);
    assert((render_flags_for_item(ITEM_DRAGON_HUNTER_WAND) & ITEM_RENDER_FLAG_TWO_HANDED) == 0);
    assert((render_flags_for_item(ITEM_ECHO_BOOTS) & ITEM_RENDER_FLAG_WEARPOS_AUTHORITY) != 0);
    assert(ready_anim_for_item(ITEM_AGS) == 7053);
    assert(walk_anim_for_item(ITEM_AGS) == 7052);
    assert(run_anim_for_item(ITEM_AGS) == 7043);
    assert(ready_anim_for_item(ITEM_KODAI_WAND) == ITEM_RENDER_MODEL_MISSING);
    assert(walk_anim_for_item(ITEM_KODAI_WAND) == ITEM_RENDER_MODEL_MISSING);
    assert(run_anim_for_item(ITEM_KODAI_WAND) == ITEM_RENDER_MODEL_MISSING);

    uint8_t equipped[NUM_GEAR_SLOTS];
    clear_equipment(equipped);
    equipped[GEAR_SLOT_WEAPON] = ITEM_BOW_OF_FAERDHINEN;
    equipped[GEAR_SLOT_SHIELD] = ITEM_CRYSTAL_SHIELD;
    OsrsPlayerAppearance bowfa = osrs_resolve_player_appearance(equipped);
    assert(bowfa.item_visible[GEAR_SLOT_WEAPON]);
    assert(!bowfa.item_visible[GEAR_SLOT_SHIELD]);

    clear_equipment(equipped);
    equipped[GEAR_SLOT_WEAPON] = ITEM_DRAGON_HUNTER_WAND;
    equipped[GEAR_SLOT_SHIELD] = ITEM_CRYSTAL_SHIELD;
    OsrsPlayerAppearance wand = osrs_resolve_player_appearance(equipped);
    assert(wand.item_visible[GEAR_SLOT_WEAPON]);
    assert(wand.item_visible[GEAR_SLOT_SHIELD]);

    clear_equipment(equipped);
    equipped[GEAR_SLOT_WEAPON] = ITEM_KODAI_WAND;
    equipped[GEAR_SLOT_SHIELD] = ITEM_ELYSIAN_SPIRIT_SHIELD;
    equipped[GEAR_SLOT_BODY] = ITEM_VIRTUS_ROBE_TOP;
    equipped[GEAR_SLOT_LEGS] = ITEM_VIRTUS_ROBE_BOTTOM;
    equipped[GEAR_SLOT_HANDS] = ITEM_CONFLICTION_GAUNTLETS;
    OsrsPlayerAppearance max_mage = osrs_resolve_player_appearance(equipped);
    assert(max_mage.item_visible[GEAR_SLOT_WEAPON]);
    assert(max_mage.item_visible[GEAR_SLOT_SHIELD]);
    assert(max_mage.item_visible[GEAR_SLOT_BODY]);
    assert(max_mage.item_visible[GEAR_SLOT_LEGS]);
    assert(max_mage.item_visible[GEAR_SLOT_HANDS]);
    assert(!max_mage.body_visible[BODY_PART_TORSO]);
    assert(!max_mage.body_visible[BODY_PART_ARMS]);
    assert(!max_mage.body_visible[BODY_PART_HANDS]);
    assert(!max_mage.body_visible[BODY_PART_LEGS]);
    assert(max_mage.body_visible[BODY_PART_HEAD]);

    clear_equipment(equipped);
    equipped[GEAR_SLOT_BODY] = ITEM_CRYSTAL_BODY;
    equipped[GEAR_SLOT_FEET] = ITEM_ECHO_BOOTS;
    OsrsPlayerAppearance budget_body = osrs_resolve_player_appearance(equipped);
    assert(!budget_body.body_visible[BODY_PART_TORSO]);
    assert(!budget_body.body_visible[BODY_PART_ARMS]);
    assert(!budget_body.body_visible[BODY_PART_FEET]);
    assert(budget_body.body_visible[BODY_PART_LEGS]);

    set_equipment(equipped, MAX_MAGE_LOADOUT);
    assert_resolved_models_present(equipped);
    set_equipment(equipped, MAX_RANGE_LONG_LOADOUT);
    assert_resolved_models_present(equipped);
    set_equipment(equipped, MAX_RANGE_FAST_LOADOUT);
    assert_resolved_models_present(equipped);
    set_equipment(equipped, BUDGET_MAGE_LOADOUT);
    assert_resolved_models_present(equipped);
    set_equipment(equipped, BUDGET_RANGE_LONG_LOADOUT);
    assert_resolved_models_present(equipped);
    set_equipment(equipped, BUDGET_RANGE_FAST_LOADOUT);
    assert_resolved_models_present(equipped);
    test_expected_player_model_ids();

    return 0;
}
