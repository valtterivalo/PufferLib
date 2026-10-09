#include "../gearswapper/gs_wire.h"

int main(void) {
    gs_register_java_items();
    int defender = -1;
    for (size_t i = 0; i < sizeof(GS_JAVA_ITEMS) / sizeof(*GS_JAVA_ITEMS); i++) {
        const GsJavaItemRow* row = &GS_JAVA_ITEMS[i];
        if (!strcmp(row->name, "Dragon defender")) { defender = row->id; break; }
    }
    assert(defender >= 0);
    GsClient with_shield = {0};
    with_shield.equipment_count = GS_MAX_EQUIPMENT;
    with_shield.equipment[GS_KIT_SHIELD] = defender + GS_ITEM_OFFSET;
    GsClient without_shield = with_shield;
    without_shield.equipment[GS_KIT_SHIELD] = 0;
    int weapons[] = {GS_ITEM_ARMADYL_GODSWORD, GS_ITEM_GRANITE_MAUL_PLUS};
    for (size_t i = 0; i < sizeof(weapons) / sizeof(*weapons); i++) {
        GsArmament with, without;
        assert(gs_item(weapons[i])->two_handed);
        assert(gs_armed(&with_shield, (GsBonuses){0}, weapons[i], &with));
        assert(gs_armed(&without_shield, (GsBonuses){0}, weapons[i], &without));
        assert(with.normal == without.normal && with.spec == without.spec);
        assert(with.attack_bonus == without.attack_bonus);
        assert(with.bonuses.strength == without.bonuses.strength);
    }
    GsArmament with, without;
    assert(!gs_item(GS_ITEM_VOIDWAKER)->two_handed);
    assert(gs_armed(&with_shield, (GsBonuses){0}, GS_ITEM_VOIDWAKER, &with));
    assert(gs_armed(&without_shield, (GsBonuses){0}, GS_ITEM_VOIDWAKER, &without));
    assert(with.attack_bonus > without.attack_bonus);
    assert(with.bonuses.strength > without.bonuses.strength);
    GsInfer full;
    gs_infer_init(&full);
    full.c.hitpoints = full.c.attack = full.c.strength = full.c.defence = full.c.ranged = full.c.magic = 99;
    full.c.worn[GS_WORN_WEAPON] = -1;
    full.c.oweapon = 0;
    for (size_t i = 0; i < sizeof(GS_JAVA_ITEMS) / sizeof(*GS_JAVA_ITEMS) && full.arsenal_count < GS_ARSENAL_SIZE; i++) {
        const GsJavaItemRow* row = &GS_JAVA_ITEMS[i];
        GsWeapon weapon = gs_weapon_kind(row->id);
        if (weapon == GS_W_VOIDWAKER || weapon == GS_W_ARMADYL_GODSWORD || weapon == GS_W_GRANITE_MAUL) continue;
        GsArmament armament;
        if (gs_armed(&full.c, (GsBonuses){0}, row->id, &armament) && armament.has_spec)
            gs_arsenal_merge(&full, row->id, armament);
    }
    assert(full.arsenal_count == GS_ARSENAL_SIZE);
    GsEatSituation situation;
    gs_eat_situation(&full, 100, GS_IDLE, 0.01, 1, &situation);
    assert(situation.attack_count == 2 * GS_ARSENAL_SIZE);
    assert(situation.hidden_count == 3);
    puts("Hidden AGS and maul exclude shields, Voidwaker retains shields, full arsenal fits");
    return 0;
}
