#ifndef GS_STAT_ITEMS_H
#define GS_STAT_ITEMS_H
/* Items the sim has no hand-written row for, priced from their own stats (the Java item table: bonuses, attack speed, style, two-handedness) instead
 * of the nearest supported item. No special attack and no item effect: those need a hand-written row. Weapons need a speed (the table's, else the
 * observed one); an id with no stats row, or no worn slot, is not priced (-1) and the caller declares a prior for it. */
#include "gs_items.h"
#include "gs_java_items.h"

/** Client worn slot (the wire's kit slot) to the sim's EquipmentSlot, -1 for slots the sim does not wear. */
static int gs_stat_item_slot(int kit) {
    switch (kit) {
        case 0: return SLOT_HEAD; case 1: return SLOT_CAPE; case 2: return SLOT_NECK; case 3: return SLOT_WEAPON; case 4: return SLOT_BODY;
        case 5: return SLOT_SHIELD; case 7: return SLOT_LEGS; case 9: return SLOT_HANDS; case 10: return SLOT_FEET; case 12: return SLOT_RING;
        case 13: return SLOT_AMMO;
        default: return -1;
    }
}

/** The sim item index for raw id `raw`: its fixed row when it has one, else a stat-priced row (registered now), else -1. */
static int gs_item_priced_from_stats(int raw, int observed_speed) {
    int fixed = osrs_item_index_from_raw_id(raw);
    if (fixed >= 0) return fixed;
    const GsJavaItemRow* row = NULL;
    for (size_t i = 0; i < sizeof(GS_JAVA_ITEMS) / sizeof(*GS_JAVA_ITEMS); i++)
        if (GS_JAVA_ITEMS[i].id == raw) { row = &GS_JAVA_ITEMS[i]; break; }
    if (!row || !row->has_stats) return -1;
    int slot = gs_stat_item_slot(row->slot);
    if (slot < 0) return -1;
    int style = gs_style_of(row->astab, row->aslash, row->acrush, row->arange, row->rstr, row->amagic);
    int attack_style = style == GS_MAGIC ? ATTACK_STYLE_MAGIC : style == GS_RANGED ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE;
    int speed = 0;
    if (slot == SLOT_WEAPON) {
        speed = row->aspeed > 0 ? row->aspeed : row->base_speed > 0 ? row->base_speed : observed_speed;
        if (speed <= 0) return -1;
    }
    Item item = {
        .item_id = (uint16_t)raw, .slot = (uint8_t)slot, .two_handed = (uint8_t)row->two_handed, .attack_speed = (uint8_t)speed,
        .attack_range = (uint8_t)(slot != SLOT_WEAPON ? 0 : attack_style == ATTACK_STYLE_MELEE ? 1 : attack_style == ATTACK_STYLE_RANGED ? 7 : 10),
        .attack_stab = (int16_t)row->astab, .attack_slash = (int16_t)row->aslash, .attack_crush = (int16_t)row->acrush,
        .attack_magic = (int16_t)row->amagic, .attack_ranged = (int16_t)row->arange,
        .defence_stab = (int16_t)row->dstab, .defence_slash = (int16_t)row->dslash, .defence_crush = (int16_t)row->dcrush,
        .defence_magic = (int16_t)row->dmagic, .defence_ranged = (int16_t)row->drange,
        .melee_strength = (int16_t)row->str, .ranged_strength = (int16_t)row->rstr, .magic_damage = (int16_t)(row->mdmg + 0.5),
        .prayer = (int16_t)row->prayer,
    };
    snprintf(item.name, sizeof(item.name), "%s", row->name);
    return osrs_stat_item_register(&item, slot == SLOT_WEAPON ? attack_style : 0);
}
#endif
