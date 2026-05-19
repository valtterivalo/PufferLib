#include "../osrs_items.h"
#include "../osrs_models.h"
#include "../osrs_assets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t hide_mask_for_item(int item_index) {
    return item_hide_body_mask(ITEM_DATABASE[item_index].item_id);
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

static void assert_runtime_model_present(int item_index) {
    uint32_t model_id = wield_model_for_item(item_index);
    assert(model_id != ITEM_RENDER_MODEL_MISSING);
    assert(model_file_contains_model(OSRS_ASSET("equipment.models"), model_id));
}

static void clear_equipment(uint8_t equipped[NUM_GEAR_SLOTS]) {
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) {
        equipped[i] = ITEM_NONE;
    }
}

int main(void) {
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

    return 0;
}
