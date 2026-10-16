#ifndef OSRS_RISKFIGHT_KITS_H
#define OSRS_RISKFIGHT_KITS_H
/* The opponent pool, built once at load: what he can show up wearing and carrying (riskfight_pool.h holds the row type and the draw).
 *
 *   empirical  508 setups mined from our 72 recorded fights with composition and the 1,056 spectated duel fighters (scripts/mine_riskfight_pool.py ->
 *              riskfight_pool_data.h), weighted by fights, an own fight counting RF_KIT_TAPE_WEIGHT times a spectated one. A setup the sim cannot wear or
 *              play is left out and counted (RISKFIGHT_POOL_EXCLUDED_*), so the unknown-item list reads off the build.
 *   archetype  setups that are common in the wilderness and thin in the recordings: Soulreaper axe, Dharok's, a Voidwaker mirror of our own kit, the granite
 *              maul plain and ornate, and ranged gear (zaryte and armadyl crossbows, ballista, blowpipe, dragon knives).
 *   edge       deliberate corners: no armour, no supplies, bare hands, a tank in Justiciar, Dharok's at 22 hitpoints, a five-weapon switcher.
 * The three groups hold RF_POOL_MASS of the draws; inside a group a kit counts by its own weight. His ring is not visible on the client: a kit without a
 * ring gets one per fight (riskfight_kit_apply). Behaviour comes from the scripted profiles and OppFit, not from the pool. */
#include "riskfight_pool.h"
#include "riskfight_pool_data.h"
#include "../../osrs_pvp_threat.h"

typedef enum { RF_BAG_MELEE, RF_BAG_RANGED, RF_BAG_BARE } RiskfightBag;

enum { RF_KIT_TAPE_WEIGHT = 10, RF_KIT_RANGED_START = 4, RF_KIT_LIVE_SACKS = 27 };
static const float RF_POOL_MASS[3] = {0.70f, 0.22f, 0.08f}; /* empirical, archetype, edge */

/** A ragger's kit: drawn by weight among the pool's kits that start with a ranged weapon (the sim cannot cast, so a ragging mage or ranger turns into one of them). */
static int riskfight_kit_starts_ranged(const RiskfightKit* k) { return k->equipment[GEAR_SLOT_WEAPON] != ITEM_NONE && get_item_attack_style(k->equipment[GEAR_SLOT_WEAPON]) == ATTACK_STYLE_RANGED; }
static const RiskfightKit* riskfight_pool_pick_ranged(float roll) {
    float total = 0;
    for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++) if (riskfight_kit_starts_ranged(&RISKFIGHT_POOL[i])) total += RISKFIGHT_POOL[i].weight;
    assert(total > 0 && roll >= 0 && roll < 1);
    float at = roll * total, sum = 0;
    const RiskfightKit* last = NULL;
    for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++) {
        if (!riskfight_kit_starts_ranged(&RISKFIGHT_POOL[i])) continue;
        last = &RISKFIGHT_POOL[i];
        sum += last->weight;
        if (at < sum) return last;
    }
    return last;
}

static RiskfightLoadout RISKFIGHT_POOL_LOADOUT[RF_POOL_MAX];
static int RISKFIGHT_POOL_EXCLUDED_ROWS, RISKFIGHT_POOL_EXCLUDED_FIGHTS, RISKFIGHT_POOL_EXCLUDED_MAGIC_ROWS;

/** Client worn slot (the pool data's order) to the sim's gear slot. */
static const int RF_KIT_GEAR_OF_WORN[RISKFIGHT_POOL_WORN] = {GEAR_SLOT_HEAD, GEAR_SLOT_CAPE, GEAR_SLOT_NECK, GEAR_SLOT_WEAPON, GEAR_SLOT_BODY, GEAR_SLOT_SHIELD, -1,
    GEAR_SLOT_LEGS, -1, GEAR_SLOT_HANDS, GEAR_SLOT_FEET, -1, GEAR_SLOT_RING};

static int riskfight_gear_slot_of_item(const Item* item) {
    switch (item->slot) {
        case SLOT_HEAD: return GEAR_SLOT_HEAD; case SLOT_CAPE: return GEAR_SLOT_CAPE; case SLOT_NECK: return GEAR_SLOT_NECK;
        case SLOT_WEAPON: return GEAR_SLOT_WEAPON; case SLOT_BODY: return GEAR_SLOT_BODY; case SLOT_SHIELD: return GEAR_SLOT_SHIELD;
        case SLOT_LEGS: return GEAR_SLOT_LEGS; case SLOT_HANDS: return GEAR_SLOT_HANDS; case SLOT_FEET: return GEAR_SLOT_FEET;
        case SLOT_RING: return GEAR_SLOT_RING; default: return GEAR_SLOT_AMMO;
    }
}

/** A weapon the riskfight sim can swing: bare hands, or a melee or ranged weapon (riskfight_attack has no spell choice, so a staff would swing as melee), and not
 * the poisoning dagger, whose application riskfight_attack aborts on until its immunity timing is verified. */
static int riskfight_kit_weapon_ok(uint8_t weapon) {
    if (weapon == ITEM_NONE) return 1;
    int style = get_item_attack_style(weapon);
    return weapon < NUM_ITEMS && weapon != ITEM_ABYSSAL_DAGGER_BH_P_PLUS_PLUS && (style == ATTACK_STYLE_MELEE || style == ATTACK_STYLE_RANGED);
}

/** Fills the fields derived from the weapons and checks the kit is one the sim can set up. Aborts on a kit that is not consistent: a bug in a table. */
static void riskfight_kit_finish(RiskfightKit* k) {
    assert(k->weapons[0] == k->equipment[GEAR_SLOT_WEAPON]);
    int ranged = 0, fit_ok = 1;
    for (int i = 0; i < k->weapon_count; i++) {
        uint8_t w = k->weapons[i];
        if (!riskfight_kit_weapon_ok(w)) { fprintf(stderr, "kit %s: weapon %d cannot be swung in riskfight\n", k->name, w); abort(); }
        int style = w == ITEM_NONE ? ATTACK_STYLE_MELEE : get_item_attack_style(w);
        ranged |= style == ATTACK_STYLE_RANGED;
        int kind_ok = w == ITEM_NONE || (style == ATTACK_STYLE_MELEE && w != ITEM_SOULREAPER_AXE && w != ITEM_DHAROKS_GREATAXE &&
            (osrs_spec_cost(w) == 0 || w == ITEM_VOIDWAKER || w == ITEM_GRANITE_MAUL || w == ITEM_GRANITE_MAUL_ORNATE || w == ITEM_AGS || w == ITEM_DRAGON_CLAWS));
        fit_ok &= kind_ok;
    }
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        uint8_t item = k->equipment[slot];
        if (item == ITEM_NONE || slot == GEAR_SLOT_WEAPON) continue;
        if (riskfight_gear_slot_of_item(&ITEM_DATABASE[item]) != slot) { fprintf(stderr, "kit %s: %s is not a %d item\n", k->name, ITEM_DATABASE[item].name, slot); abort(); }
    }
    if (k->equipment[GEAR_SLOT_WEAPON] != ITEM_NONE && item_is_two_handed(k->equipment[GEAR_SLOT_WEAPON]) && k->equipment[GEAR_SLOT_SHIELD] != ITEM_NONE) {
        fprintf(stderr, "kit %s: a shield under a two-handed weapon\n", k->name);
        abort();
    }
    uint8_t ammo = osrs_ammo_assumption(k->equipment[GEAR_SLOT_WEAPON]);
    if (ammo != ITEM_NONE && k->equipment[GEAR_SLOT_AMMO] == ITEM_NONE) k->equipment[GEAR_SLOT_AMMO] = ammo;
    if (k->ranged_level == 0 && ranged) k->ranged_level = OSRS_THREAT_ASSUMED_RANGED;
    if (ranged && k->start_offset == 1) k->start_offset = RF_KIT_RANGED_START;
    if (ranged && k->bag == RF_BAG_MELEE) k->bag = RF_BAG_RANGED;
    k->oppfit_ok = fit_ok && !ranged;
}

/** The loadout of a kit: its worn gear, its other weapons in the bag, and the supplies of its bag kind filling what is left (live kit: 12 anglerfish, 7 karambwan,
 * 2 brews, 2 restores, a super combat or ranging potion, a teleport tab and a Vengeance sack around the maul). One slot stays free when a two-handed weapon in the
 * bag must take the shield off. */
static void riskfight_kit_loadout(const RiskfightKit* k, RiskfightLoadout* out) {
    memset(out, 0, sizeof(*out));
    memcpy(out->equipment, k->equipment, NUM_GEAR_SLOTS);
    out->hitpoints = k->hitpoints;
    int bag_weapons = k->weapon_count - 1, two_handed_in_bag = 0;
    for (int i = 1; i < k->weapon_count; i++) two_handed_in_bag |= k->weapons[i] != ITEM_NONE && item_is_two_handed(k->weapons[i]);
    int capacity = OSRS_INVENTORY_SIZE - (two_handed_in_bag && k->equipment[GEAR_SLOT_SHIELD] != ITEM_NONE);
    int slot = 0;
    if (k->bag != RF_BAG_BARE) {
        const OsrsConsumableKind potion = k->bag == RF_BAG_RANGED ? OSRS_CONSUMABLE_RANGING : OSRS_CONSUMABLE_SUPER_COMBAT;
        const struct { OsrsConsumableKind kind; int doses, count; } fixed[] = {
            {OSRS_CONSUMABLE_KARAMBWAN, 0, 7}, {OSRS_CONSUMABLE_BREW, 4, 2}, {OSRS_CONSUMABLE_SUPER_RESTORE, 4, 2}, {potion, 4, 1},
            {OSRS_CONSUMABLE_TELEPORT, 0, 1}, {OSRS_CONSUMABLE_VENGEANCE_SACK, 0, 1},
        };
        int fixed_count = 0;
        for (size_t i = 0; i < sizeof(fixed) / sizeof(*fixed); i++) fixed_count += fixed[i].count;
        int anglers = capacity - bag_weapons - fixed_count;
        assert(anglers >= 1);
        for (int n = 0; n < anglers; n++) out->inventory[slot++] = riskfight_supply_code(OSRS_CONSUMABLE_ANGLERFISH, 0);
        for (size_t i = 0; i < sizeof(fixed) / sizeof(*fixed); i++)
            for (int n = 0; n < fixed[i].count; n++) out->inventory[slot++] = riskfight_supply_code(fixed[i].kind, fixed[i].doses);
        out->vengeance_sacks = RF_KIT_LIVE_SACKS;
    }
    for (int i = 1; i < k->weapon_count; i++) out->inventory[slot++] = osrs_inventory_content_code_from_item(k->weapons[i]);
    assert(slot <= capacity);
}

/* ---- Builders of the three groups. */
static RiskfightKit riskfight_kit_new(const char* name, const char* source, RiskfightKitKind kind, float weight, int bag) {
    RiskfightKit k = {.name = name, .source = source, .kind = kind, .weight = weight, .bag = bag, .start_offset = 1, .hitpoints = 121};
    memset(k.equipment, ITEM_NONE, sizeof(k.equipment));
    memset(k.weapons, ITEM_NONE, sizeof(k.weapons));
    return k;
}
static void riskfight_kit_wear(RiskfightKit* k, int gear_slot, uint8_t item) { k->equipment[gear_slot] = item; }
static void riskfight_kit_arms(RiskfightKit* k, int count, const uint8_t* weapons) {
    assert(count >= 1 && count <= RF_KIT_WEAPONS);
    memcpy(k->weapons, weapons, (size_t)count);
    k->weapon_count = count;
    k->equipment[GEAR_SLOT_WEAPON] = weapons[0];
}
#define RF_ARMS(k, ...) do { const uint8_t arms_[] = {__VA_ARGS__}; riskfight_kit_arms(&(k), (int)sizeof(arms_), arms_); } while (0)

static void riskfight_kit_gear(RiskfightKit* k, uint8_t head, uint8_t cape, uint8_t neck, uint8_t body, uint8_t shield, uint8_t legs, uint8_t hands, uint8_t feet, uint8_t ring) {
    k->equipment[GEAR_SLOT_HEAD] = head; k->equipment[GEAR_SLOT_CAPE] = cape; k->equipment[GEAR_SLOT_NECK] = neck; k->equipment[GEAR_SLOT_BODY] = body;
    k->equipment[GEAR_SLOT_SHIELD] = shield; k->equipment[GEAR_SLOT_LEGS] = legs; k->equipment[GEAR_SLOT_HANDS] = hands; k->equipment[GEAR_SLOT_FEET] = feet;
    k->equipment[GEAR_SLOT_RING] = ring;
}

static int riskfight_kit_from_row(const RiskfightPoolRow* row, RiskfightKit* k) {
    *k = riskfight_kit_new("empirical", row->source, RF_KIT_EMPIRICAL, (float)(row->tape_fights * RF_KIT_TAPE_WEIGHT + row->duel_fights), RF_BAG_MELEE);
    uint8_t weapons[RF_KIT_WEAPONS] = {ITEM_NONE};
    int count = 0;
    uint16_t raw_weapons[RF_KIT_WEAPONS] = {row->weapon};
    for (int i = 0; i < RISKFIGHT_POOL_SWITCHES && i + 1 < RF_KIT_WEAPONS; i++) raw_weapons[i + 1] = row->switches[i];
    for (int i = 0; i < RF_KIT_WEAPONS; i++) {
        if (i > 0 && raw_weapons[i] == 0) continue;
        int item = raw_weapons[i] == 0 ? ITEM_NONE : osrs_item_index_from_raw_id(raw_weapons[i]);
        if (item < 0 || item >= NUM_ITEMS) return 0;
        if (item != ITEM_NONE && get_item_attack_style((uint8_t)item) == ATTACK_STYLE_MAGIC) { RISKFIGHT_POOL_EXCLUDED_MAGIC_ROWS++; return 0; }
        if (!riskfight_kit_weapon_ok((uint8_t)item)) return 0;
        weapons[count++] = (uint8_t)item;
    }
    riskfight_kit_arms(k, count, weapons);
    for (int w = 0; w < RISKFIGHT_POOL_WORN; w++) {
        if (RF_KIT_GEAR_OF_WORN[w] < 0 || RF_KIT_GEAR_OF_WORN[w] == GEAR_SLOT_WEAPON || row->worn[w] == 0) continue;
        int item = osrs_item_index_from_raw_id(row->worn[w]);
        if (item < 0 || item >= NUM_ITEMS) return 0;
        if (riskfight_gear_slot_of_item(&ITEM_DATABASE[item]) != RF_KIT_GEAR_OF_WORN[w]) return 0;
        k->equipment[RF_KIT_GEAR_OF_WORN[w]] = (uint8_t)item;
    }
    if (item_is_two_handed(k->equipment[GEAR_SLOT_WEAPON]) && k->equipment[GEAR_SLOT_SHIELD] != ITEM_NONE) k->equipment[GEAR_SLOT_SHIELD] = ITEM_NONE; /* the shield seen under a switch */
    return 1;
}

static void riskfight_pool_archetypes(RiskfightKit* out, int* count) {
    const char* src = "wilderness meta, thin in the recordings";
    RiskfightKit k;
#define RF_ADD() out[(*count)++] = k
    k = riskfight_kit_new("soulreaper axe", src, RF_KIT_ARCHETYPE, 3, RF_BAG_MELEE);
    riskfight_kit_gear(&k, ITEM_TORVA_FULL_HELM, ITEM_INFERNAL_CAPE, ITEM_AMULET_OF_TORTURE, ITEM_TORVA_PLATEBODY, ITEM_NONE, ITEM_TORVA_PLATELEGS, ITEM_FEROCIOUS_GLOVES, ITEM_PRIMORDIAL_BOOTS, ITEM_ULTOR_RING);
    RF_ARMS(k, ITEM_SOULREAPER_AXE);
    RF_ADD();
    k = riskfight_kit_new("dharok's", src, RF_KIT_ARCHETYPE, 4, RF_BAG_MELEE);
    riskfight_kit_gear(&k, ITEM_DHAROKS_HELM, ITEM_FIRE_CAPE, ITEM_FURY, ITEM_DHAROKS_PLATEBODY, ITEM_NONE, ITEM_DHAROKS_PLATELEGS, ITEM_BARROWS_GLOVES, ITEM_DRAGON_BOOTS, ITEM_RING_OF_RECOIL);
    RF_ARMS(k, ITEM_DHAROKS_GREATAXE);
    RF_ADD();
    k = riskfight_kit_new("voidwaker mirror", "our own kit (RISKFIGHT_LIVE_LOADOUT), the recorded live fights", RF_KIT_ARCHETYPE, 4, RF_BAG_MELEE);
    riskfight_kit_gear(&k, ITEM_HELM_NEITIZNOT, ITEM_INFERNAL_CAPE, ITEM_AMULET_OF_STRENGTH, ITEM_FIGHTER_TORSO, ITEM_AVERNIC_DEFENDER, ITEM_DRAGON_PLATELEGS, ITEM_BARROWS_GLOVES, ITEM_DRAGON_BOOTS, ITEM_RING_OF_RECOIL);
    RF_ARMS(k, ITEM_VOIDWAKER, ITEM_GRANITE_MAUL_ORNATE);
    RF_ADD();
    k = riskfight_kit_new("ornate maul + whip", src, RF_KIT_ARCHETYPE, 3, RF_BAG_MELEE);
    riskfight_kit_gear(&k, ITEM_NEITIZNOT_FACEGUARD, ITEM_FIRE_CAPE, ITEM_FURY, ITEM_BANDOS_CHESTPLATE, ITEM_AVERNIC_DEFENDER, ITEM_BANDOS_TASSETS, ITEM_BARROWS_GLOVES, ITEM_BANDOS_BOOTS, ITEM_BERSERKER_RING);
    RF_ARMS(k, ITEM_WHIP, ITEM_GRANITE_MAUL_ORNATE);
    RF_ADD();
    k = riskfight_kit_new("plain maul + tentacle", src, RF_KIT_ARCHETYPE, 2, RF_BAG_MELEE);
    riskfight_kit_gear(&k, ITEM_HELM_NEITIZNOT, ITEM_INFERNAL_CAPE, ITEM_AMULET_OF_TORTURE, ITEM_FIGHTER_TORSO, ITEM_DRAGON_DEFENDER, ITEM_RUNE_PLATELEGS, ITEM_FEROCIOUS_GLOVES, ITEM_PRIMORDIAL_BOOTS, ITEM_RING_OF_RECOIL);
    RF_ARMS(k, ITEM_ABYSSAL_TENTACLE, ITEM_GRANITE_MAUL);
    RF_ADD();
    k = riskfight_kit_new("zaryte crossbow", src, RF_KIT_ARCHETYPE, 3, RF_BAG_RANGED);
    riskfight_kit_gear(&k, ITEM_MASORI_MASK_F, ITEM_AVAS_ASSEMBLER, ITEM_NECKLACE_OF_ANGUISH, ITEM_MASORI_BODY_F, ITEM_SPECTRAL_SPIRIT_SHIELD, ITEM_MASORI_CHAPS_F, ITEM_ZARYTE_VAMBRACES, ITEM_PEGASIAN_BOOTS, ITEM_VENATOR_RING);
    RF_ARMS(k, ITEM_ZARYTE_CROSSBOW);
    RF_ADD();
    k = riskfight_kit_new("armadyl crossbow", src, RF_KIT_ARCHETYPE, 3, RF_BAG_RANGED);
    riskfight_kit_gear(&k, ITEM_BLESSED_COIF, ITEM_AVAS_ACCUMULATOR, ITEM_FURY, ITEM_KARILS_TOP, ITEM_DRAGONFIRE_SHIELD, ITEM_BLACK_DHIDE_CHAPS, ITEM_BARROWS_GLOVES, ITEM_PEGASIAN_BOOTS, ITEM_RING_OF_SUFFERING_RI);
    RF_ARMS(k, ITEM_ARMADYL_CROSSBOW);
    k.equipment[GEAR_SLOT_AMMO] = ITEM_DIAMOND_DRAGON_BOLTS_E;
    RF_ADD();
    k = riskfight_kit_new("heavy ballista", src, RF_KIT_ARCHETYPE, 2, RF_BAG_RANGED);
    riskfight_kit_gear(&k, ITEM_CRYSTAL_HELM, ITEM_AVAS_ASSEMBLER, ITEM_NECKLACE_OF_ANGUISH, ITEM_CRYSTAL_BODY, ITEM_NONE, ITEM_CRYSTAL_LEGS, ITEM_ZARYTE_VAMBRACES, ITEM_PEGASIAN_BOOTS, ITEM_RING_OF_RECOIL);
    RF_ARMS(k, ITEM_HEAVY_BALLISTA);
    RF_ADD();
    k = riskfight_kit_new("toxic blowpipe", src, RF_KIT_ARCHETYPE, 2, RF_BAG_RANGED);
    riskfight_kit_gear(&k, ITEM_BLESSED_COIF, ITEM_AVAS_ACCUMULATOR, ITEM_FURY, ITEM_BLACK_DHIDE_BODY, ITEM_NONE, ITEM_BLACK_DHIDE_CHAPS, ITEM_ZARYTE_VAMBRACES, ITEM_PEGASIAN_BOOTS, ITEM_RING_OF_RECOIL);
    RF_ARMS(k, ITEM_TOXIC_BLOWPIPE);
    RF_ADD();
    k = riskfight_kit_new("dragon knives", src, RF_KIT_ARCHETYPE, 2, RF_BAG_RANGED);
    riskfight_kit_gear(&k, ITEM_BLESSED_COIF, ITEM_AVAS_ASSEMBLER, ITEM_FURY, ITEM_KARILS_TOP, ITEM_AVERNIC_DEFENDER, ITEM_BLACK_DHIDE_CHAPS, ITEM_BARROWS_GLOVES, ITEM_PEGASIAN_BOOTS, ITEM_RING_OF_RECOIL);
    RF_ARMS(k, ITEM_DRAGON_KNIFE);
    RF_ADD();
}

static void riskfight_pool_edges(RiskfightKit* out, int* count) {
    const char* src = "deliberate edge case";
    RiskfightKit k;
    k = riskfight_kit_new("edge: no armour, maul and whip", src, RF_KIT_EDGE, 1, RF_BAG_MELEE);
    RF_ARMS(k, ITEM_WHIP, ITEM_GRANITE_MAUL_ORNATE);
    RF_ADD();
    k = riskfight_kit_new("edge: justiciar tank", src, RF_KIT_EDGE, 1, RF_BAG_MELEE);
    riskfight_kit_gear(&k, ITEM_JUSTICIAR_FACEGUARD, ITEM_FIRE_CAPE, ITEM_FURY, ITEM_JUSTICIAR_CHESTGUARD, ITEM_ELYSIAN_SPIRIT_SHIELD, ITEM_JUSTICIAR_LEGGUARDS, ITEM_BARROWS_GLOVES, ITEM_DRAGON_BOOTS, ITEM_RING_OF_RECOIL);
    RF_ARMS(k, ITEM_WHIP);
    RF_ADD();
    k = riskfight_kit_new("edge: dharok's at 22 hitpoints", src, RF_KIT_EDGE, 1, RF_BAG_MELEE);
    riskfight_kit_gear(&k, ITEM_DHAROKS_HELM, ITEM_FIRE_CAPE, ITEM_FURY, ITEM_DHAROKS_PLATEBODY, ITEM_NONE, ITEM_DHAROKS_PLATELEGS, ITEM_BARROWS_GLOVES, ITEM_DRAGON_BOOTS, ITEM_RING_OF_RECOIL);
    RF_ARMS(k, ITEM_DHAROKS_GREATAXE);
    k.hitpoints = 22;
    RF_ADD();
    k = riskfight_kit_new("edge: five-weapon switcher", src, RF_KIT_EDGE, 1, RF_BAG_MELEE);
    riskfight_kit_gear(&k, ITEM_HELM_NEITIZNOT, ITEM_INFERNAL_CAPE, ITEM_AMULET_OF_TORTURE, ITEM_FIGHTER_TORSO, ITEM_AVERNIC_DEFENDER, ITEM_DRAGON_PLATELEGS, ITEM_FEROCIOUS_GLOVES, ITEM_PRIMORDIAL_BOOTS, ITEM_RING_OF_RECOIL);
    RF_ARMS(k, ITEM_ABYSSAL_TENTACLE, ITEM_VOIDWAKER, ITEM_GRANITE_MAUL_ORNATE, ITEM_AGS, ITEM_DRAGON_CLAWS);
    RF_ADD();
    k = riskfight_kit_new("edge: bare hands, no supplies", src, RF_KIT_EDGE, 1, RF_BAG_BARE);
    RF_ARMS(k, ITEM_NONE);
    RF_ADD();
    k = riskfight_kit_new("edge: voidwaker mirror, no supplies", src, RF_KIT_EDGE, 1, RF_BAG_BARE);
    riskfight_kit_gear(&k, ITEM_HELM_NEITIZNOT, ITEM_INFERNAL_CAPE, ITEM_AMULET_OF_STRENGTH, ITEM_FIGHTER_TORSO, ITEM_AVERNIC_DEFENDER, ITEM_DRAGON_PLATELEGS, ITEM_BARROWS_GLOVES, ITEM_DRAGON_BOOTS, ITEM_RING_OF_RECOIL);
    RF_ARMS(k, ITEM_VOIDWAKER, ITEM_GRANITE_MAUL_ORNATE);
    RF_ADD();
    k = riskfight_kit_new("edge: crossbow without armour", src, RF_KIT_EDGE, 1, RF_BAG_RANGED);
    RF_ARMS(k, ITEM_ZARYTE_CROSSBOW);
    RF_ADD();
}
#undef RF_ADD

__attribute__((constructor)) static void riskfight_build_pool(void) {
    static RiskfightKit group[3][RF_POOL_MAX];
    int count[3] = {0, 0, 0};
    for (size_t r = 0; r < sizeof(RISKFIGHT_POOL_ROWS) / sizeof(*RISKFIGHT_POOL_ROWS); r++) {
        RiskfightKit k;
        if (!riskfight_kit_from_row(&RISKFIGHT_POOL_ROWS[r], &k)) {
            RISKFIGHT_POOL_EXCLUDED_ROWS++;
            RISKFIGHT_POOL_EXCLUDED_FIGHTS += RISKFIGHT_POOL_ROWS[r].fights;
            continue;
        }
        riskfight_kit_finish(&k);
        assert(count[0] < RF_POOL_MAX);
        group[0][count[0]++] = k;
    }
    riskfight_pool_archetypes(group[1], &count[1]);
    riskfight_pool_edges(group[2], &count[2]);
    for (int g = 1; g < 3; g++)
        for (int i = 0; i < count[g]; i++) riskfight_kit_finish(&group[g][i]);
    if (count[0] + count[1] + count[2] > RF_POOL_MAX) { fprintf(stderr, "riskfight pool: %d kits do not fit RF_POOL_MAX %d\n", count[0] + count[1] + count[2], RF_POOL_MAX); abort(); }
    for (int g = 0; g < 3; g++) {
        float total = 0;
        for (int i = 0; i < count[g]; i++) total += group[g][i].weight;
        for (int i = 0; i < count[g]; i++) {
            group[g][i].weight = RF_POOL_MASS[g] * group[g][i].weight / total;
            riskfight_kit_loadout(&group[g][i], &RISKFIGHT_POOL_LOADOUT[RISKFIGHT_POOL_COUNT]);
            riskfight_pool_add(&group[g][i]);
        }
    }
}

/** What a pool kit needs set on his side after the shared reset: where he stands, a ranger's boosted level, and a ring when the kit shows none (the client does
 * not show rings). `roll` is a uniform 32-bit draw. */
static void riskfight_kit_apply(RiskfightState* s, const RiskfightKit* kit, uint32_t roll) {
    Player* him = &s->env.players[1];
    him->x += kit->start_offset - 1;
    him->dest_x = him->x;
    if (kit->ranged_level) him->current_ranged = kit->ranged_level;
    if (kit->equipment[GEAR_SLOT_RING] == ITEM_NONE) {
        int ranged = kit->ranged_level > 0;
        uint8_t pick[5] = {ITEM_RING_OF_RECOIL, ranged ? ITEM_VENATOR_RING : ITEM_ULTOR_RING, ranged ? ITEM_RING_OF_SUFFERING_RI : ITEM_BERSERKER_RING_I, ITEM_NONE};
        static const uint16_t share[2][4] = {{550, 150, 150, 150}, {350, 300, 150, 200}}; /* per mille; the recorded fights show a reflecting ring in 53 of 72 */
        int r = (int)((roll >> 8) % 1000u), at = 0, sum = share[ranged][0];
        while (r >= sum && at < 3) sum += share[ranged][++at];
        him->equipped[GEAR_SLOT_RING] = pick[at];
    }
    osrs_refresh_player_equipment(him);
    pvp_refresh_visible_gear(him);
}
#endif
