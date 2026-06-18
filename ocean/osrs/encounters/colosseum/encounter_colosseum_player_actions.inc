#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_player_actions.inc
 * @brief Action head layout + player tick, mirroring inferno player_actions.
 *
 * Action heads (36): MOVE, PRAYER, TARGET, 28 inventory-click heads, SPEC,
 * MODIFIER_SELECT, GRAPPLE_PARRY, OFFENSIVE, SPELL. Inventory heads share the
 * same 29-way domain: 0=noop, k=click inventory cell k-1.
 */

#define COLO_HEAD_MOVE            0  /* 25: idle + 8 walk + 16 run */
#define COLO_HEAD_PRAYER          1  /* no_change, off, set_refresh_melee/ranged/magic */
#define COLO_HEAD_TARGET          2  /* COLO_OBS_NPCS+1: none or obs slot */
#define COLO_HEAD_INV_CLICK_0     3
#define COLO_HEAD_INV_CLICK_1     4
#define COLO_HEAD_INV_CLICK_2     5
#define COLO_HEAD_INV_CLICK_3     6
#define COLO_HEAD_INV_CLICK_4     7
#define COLO_HEAD_INV_CLICK_5     8
#define COLO_HEAD_INV_CLICK_6     9
#define COLO_HEAD_INV_CLICK_7    10
#define COLO_HEAD_INV_CLICK_8    11
#define COLO_HEAD_INV_CLICK_9    12
#define COLO_HEAD_INV_CLICK_10   13
#define COLO_HEAD_INV_CLICK_11   14
#define COLO_HEAD_INV_CLICK_12   15
#define COLO_HEAD_INV_CLICK_13   16
#define COLO_HEAD_INV_CLICK_14   17
#define COLO_HEAD_INV_CLICK_15   18
#define COLO_HEAD_INV_CLICK_16   19
#define COLO_HEAD_INV_CLICK_17   20
#define COLO_HEAD_INV_CLICK_18   21
#define COLO_HEAD_INV_CLICK_19   22
#define COLO_HEAD_INV_CLICK_20   23
#define COLO_HEAD_INV_CLICK_21   24
#define COLO_HEAD_INV_CLICK_22   25
#define COLO_HEAD_INV_CLICK_23   26
#define COLO_HEAD_INV_CLICK_24   27
#define COLO_HEAD_INV_CLICK_25   28
#define COLO_HEAD_INV_CLICK_26   29
#define COLO_HEAD_INV_CLICK_27   30
#define COLO_HEAD_SPEC           31  /* none, arm equipped spec, disarm */
#define COLO_HEAD_MODIFIER_SELECT 32  /* none + pick draft option 0/1/2 */
#define COLO_HEAD_GRAPPLE_PARRY  33  /* none + the 5 grapple callout slots (A12) */
#define COLO_HEAD_OFFENSIVE      34  /* no_change, off, piety, rigour, augury */
#define COLO_HEAD_SPELL          35  /* none, summon-thrall, death-charge (Arceuus) */

#define COLO_INV_CLICK_HEADS 28
#define COLO_INV_CLICK_DIM 29
#define COLO_OVERHEAD_DIM ENCOUNTER_OVERHEAD_DIM_PVE
#define COLO_SPEC_DIM 3
#define COLO_SPELL_DIM 3   /* none, summon-thrall, death-charge */

#define COLO_OVERHEAD_NO_CHANGE ENCOUNTER_OVERHEAD_NO_CHANGE
#define COLO_OVERHEAD_OFF ENCOUNTER_OVERHEAD_OFF
#define COLO_OVERHEAD_MELEE ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE
#define COLO_OVERHEAD_RANGED ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED
#define COLO_OVERHEAD_MAGIC ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC

/* COLO_HEAD_SPELL action indices */
#define COLO_SPELL_SUMMON_THRALL 1
#define COLO_SPELL_DEATH_CHARGE  2

#define COLO_MODIFIER_SELECT_DIM (1 + COLO_MODIFIER_DRAFT_OPTIONS)  /* 4 */
/** grapple-parry head: index 0 = no click, index k = click grapple slot (k-1).
    A12: the domain is the 5-slot ColoGrappleSlot callout set, matching
    SolHereditState.grapple_body_slot. */
#define COLO_GRAPPLE_PARRY_DIM (1 + COLO_NUM_GRAPPLE_SLOTS)  /* 6 */

#define COLO_ACTION_DIMS_INIT { \
    ENCOUNTER_MOVE_ACTIONS, \
    COLO_OVERHEAD_DIM, \
    COLO_OBS_NPCS + 1, \
    COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, \
    COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, \
    COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, \
    COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, \
    COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, \
    COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, \
    COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, COLO_INV_CLICK_DIM, \
    COLO_SPEC_DIM, \
    COLO_MODIFIER_SELECT_DIM, \
    COLO_GRAPPLE_PARRY_DIM, \
    ENCOUNTER_OFFENSIVE_DIM, \
    COLO_SPELL_DIM \
}
static const int COLO_ACTION_DIMS[COLO_NUM_ACTION_HEADS] = COLO_ACTION_DIMS_INIT;
#define COLO_ACTION_MASK_SIZE (ENCOUNTER_MOVE_ACTIONS + COLO_OVERHEAD_DIM + \
    COLO_OBS_NPCS + 1 + COLO_INV_CLICK_HEADS * COLO_INV_CLICK_DIM + COLO_SPEC_DIM + \
    COLO_MODIFIER_SELECT_DIM + COLO_GRAPPLE_PARRY_DIM + ENCOUNTER_OFFENSIVE_DIM + \
    COLO_SPELL_DIM)

static int col_action_head_mask_offset(int head) {
    int offset = 0;
    for (int h = 0; h < head; h++)
        offset += COLO_ACTION_DIMS[h];
    return offset;
}


/** E13: derive active divine stat floors from Colosseum's live timers. */
static EncounterStatDriftPins col_divine_stat_drift_pins(const ColosseumState* s) {
    EncounterStatDriftPins pins = encounter_stat_drift_no_pins();
    if (s->divine_combat_timer > 0) {
        pins = encounter_merge_stat_drift_pins(
            pins, encounter_divine_super_combat_pins(&s->player));
    }
    if (s->divine_ranged_timer > 0) {
        pins = encounter_merge_stat_drift_pins(
            pins, encounter_divine_ranging_pins(&s->player));
    }
    return pins;
}

/** E13: active divine boosts prevent their potion stats from draining below
    the boosted floor. Returns 1 when loadout max hits need recomputing. */
static int col_enforce_divine_stat_floors(ColosseumState* s) {
    return encounter_enforce_stat_drift_pins(&s->player, col_divine_stat_drift_pins(s));
}

/** Apply colosseum's divine floor tail and recompute every loadout after stat changes. */
static void col_finish_stat_moving_consumable(ColosseumState* s) {
    col_enforce_divine_stat_floors(s);
    col_mark_live_loadout_dirty(s);
}

/** E13: expire a divine combat timer and drop its potion stats to base. */
static int col_tick_divine_combat_timer(ColosseumState* s) {
    if (s->divine_combat_timer <= 0) return 0;
    s->divine_combat_timer--;
    if (s->divine_combat_timer > 0) return 0;
    s->player.current_attack = s->player.base_attack;
    s->player.current_strength = s->player.base_strength;
    s->player.current_defence = s->player.base_defence;
    return 1;
}

/** E13: expire a divine ranging timer and drop Ranged to base. */
static int col_tick_divine_ranged_timer(ColosseumState* s) {
    if (s->divine_ranged_timer <= 0) return 0;
    s->divine_ranged_timer--;
    if (s->divine_ranged_timer > 0) return 0;
    s->player.current_ranged = s->player.base_ranged;
    return 1;
}

/** E13: live-gameplay stat drift and divine timers freeze during draft and
    ready gaps, matching the existing surge and venom gap precedent. */
static void col_tick_live_stat_drift_and_divines(ColosseumState* s) {
    int changed = 0;
    changed |= col_tick_divine_combat_timer(s);
    changed |= col_tick_divine_ranged_timer(s);
    changed |= encounter_tick_stat_drift(
        &s->player, &s->stat_drift_timer, col_divine_stat_drift_pins(s));
    if (changed) col_mark_live_loadout_dirty(s);
}

/** apply the 10 HP divine potion self-damage and arm the duration timer. */
static void col_apply_divine_combat_potion_effect(ColosseumState* s) {
    encounter_damage_player(&s->player, 10, &s->tick_scratch.damage_received);
    s->divine_combat_timer = ENCOUNTER_DIVINE_POTION_TICKS;
}

/** apply the 10 HP divine potion self-damage and arm the duration timer. */
static void col_apply_divine_ranged_potion_effect(ColosseumState* s) {
    encounter_damage_player(&s->player, 10, &s->tick_scratch.damage_received);
    s->divine_ranged_timer = ENCOUNTER_DIVINE_POTION_TICKS;
}

/** apply prayer actions + drain, before movement and attack. an offensive
    prayer change recomputes every loadout's prayer multipliers (L12). */
static void col_player_pretick(ColosseumState* s, ColosseumContext* ctx, const int* actions) {
    OverheadPrayer prev_prayer = s->player.prayer;
    OffensivePrayer prev_offensive = s->player.offensive_prayer;
    int prayer_before = s->player.current_prayer;
    if (encounter_apply_overhead_action(&s->player.prayer, actions[COLO_HEAD_PRAYER]))
        s->player.prayer_just_activated = 1;
    if (encounter_apply_offensive_action(&s->player.offensive_prayer,
            actions[COLO_HEAD_OFFENSIVE]))
        s->player.offensive_prayer_just_activated = 1;
    encounter_drain_all_prayers(&s->player, encounter_player_prayer_bonus(&s->player));
    if (s->player.offensive_prayer != prev_offensive)
        col_mark_live_loadout_dirty(s);
    if (ctx && ctx->config.action_debug_log) {
        fprintf(stderr,
            "colo_action tick=%d pretick move=%d prayer_action=%d target=%d "
            "offensive_action=%d spell=%d prayer=%d->%d offensive=%d->%d pp=%d->%d\n",
            s->tick, actions[COLO_HEAD_MOVE], actions[COLO_HEAD_PRAYER],
            actions[COLO_HEAD_TARGET], actions[COLO_HEAD_OFFENSIVE],
            actions[COLO_HEAD_SPELL], prev_prayer, s->player.prayer,
            prev_offensive, s->player.offensive_prayer, prayer_before,
            s->player.current_prayer);
    }
}


/** apply the active weapon set's loadout to the player. */
static void col_apply_weapon_set(ColosseumState* s, ColoWeaponSet set) {
    if (set < 0 || set >= COLO_NUM_WEAPON_SETS)
        col_abort_invalid_weapon_set(set);
    if (s->weapon_set == set) return;
    GearSet gear = (set == COLO_GEAR_RANGED) ? GEAR_RANGED
                 : (set == COLO_GEAR_MAGIC) ? GEAR_MAGE
                 : GEAR_MELEE;
    encounter_apply_loadout(&s->player,
        col_loadouts_for_profile(s->active_loadout_profile)[set], gear);
    s->weapon_set = set;
    col_mark_live_loadout_dirty(s);
}

static void col_sync_weapon_set_from_equipped_weapon(ColosseumState* s) {
    AttackStyle style = col_equipped_weapon_attack_style(&s->player);
    s->weapon_set = style == ATTACK_STYLE_RANGED ? COLO_GEAR_RANGED
        : style == ATTACK_STYLE_MAGIC ? COLO_GEAR_MAGIC
        : COLO_GEAR_MELEE;
    s->player.current_gear = col_gear_set_for_attack_style(style);
    s->player.visible_gear = s->player.current_gear;
}

static ColoInvCell col_inventory_cell_from_item(uint8_t item_idx) {
    if (item_idx == ITEM_NONE) return (ColoInvCell){0};
    if (item_idx >= NUM_ITEMS) {
        fprintf(stderr, "colosseum: invalid item index %u for inventory cell\n", item_idx);
        abort();
    }
    return (ColoInvCell){
        .osrs_id = ITEM_DATABASE[item_idx].item_id,
        .item_idx = item_idx,
        .dose = 0,
    };
}

static int col_first_empty_inventory_cell(const ColosseumState* s, int except_slot) {
    for (int i = 0; i < COLO_INVENTORY_DISPLAY_SLOTS; i++) {
        if (i == except_slot) continue;
        if (s->inventory_cells[i].osrs_id == 0 &&
                s->inventory_cells[i].item_idx == ITEM_NONE) return i;
    }
    return -1;
}

static int col_inventory_cell_holds_equipped_item(const ColosseumState* s, int cell_idx) {
    const ColoInvCell* cell = &s->inventory_cells[cell_idx];
    if (cell->item_idx == ITEM_NONE) return 0;
    int slot = osrs_item_gear_slot(cell->item_idx);
    if (slot < 0) return 0;
    return s->player.equipped[slot] == cell->item_idx;
}

static int col_can_equip_from_cell(const ColosseumState* s, int cell_idx) {
    const ColoInvCell* cell = &s->inventory_cells[cell_idx];
    if (cell->item_idx == ITEM_NONE) return 0;
    if (col_inventory_cell_holds_equipped_item(s, cell_idx)) return 0;

    uint8_t item_idx = cell->item_idx;
    int gear_slot = osrs_item_gear_slot(item_idx);
    if (gear_slot < 0) return 0;
    if (gear_slot == GEAR_SLOT_WEAPON && item_is_two_handed(item_idx) &&
            s->player.equipped[GEAR_SLOT_SHIELD] != ITEM_NONE &&
            s->player.equipped[GEAR_SLOT_WEAPON] != ITEM_NONE &&
            col_first_empty_inventory_cell(s, cell_idx) < 0) return 0;
    if (gear_slot == GEAR_SLOT_SHIELD &&
            item_is_two_handed(s->player.equipped[GEAR_SLOT_WEAPON]) &&
            col_first_empty_inventory_cell(s, cell_idx) < 0) return 0;
    return 1;
}

static void col_equip_from_cell(ColosseumState* s, int cell_idx) {
    if (!col_can_equip_from_cell(s, cell_idx)) return;

    ColoInvCell* cell = &s->inventory_cells[cell_idx];
    uint8_t item_idx = cell->item_idx;
    int gear_slot = osrs_item_gear_slot(item_idx);
    if (gear_slot < 0) {
        fprintf(stderr, "colosseum equip: item %u has no gear slot\n", item_idx);
        abort();
    }
    uint8_t displaced = s->player.equipped[gear_slot];

    if (gear_slot == GEAR_SLOT_WEAPON && item_is_two_handed(item_idx)) {
        uint8_t shield = s->player.equipped[GEAR_SLOT_SHIELD];
        if (shield != ITEM_NONE) {
            if (displaced == ITEM_NONE) {
                displaced = shield;
            } else {
                int empty = col_first_empty_inventory_cell(s, cell_idx);
                if (empty < 0) return;
                s->inventory_cells[empty] = col_inventory_cell_from_item(shield);
            }
            s->player.equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        }
    } else if (gear_slot == GEAR_SLOT_SHIELD &&
            item_is_two_handed(s->player.equipped[GEAR_SLOT_WEAPON])) {
        displaced = s->player.equipped[GEAR_SLOT_WEAPON];
        s->player.equipped[GEAR_SLOT_WEAPON] = ITEM_NONE;
    }

    s->player.equipped[gear_slot] = item_idx;
    *cell = col_inventory_cell_from_item(displaced);
    if (gear_slot == GEAR_SLOT_WEAPON) s->player.spec_armed = 0;
    osrs_refresh_player_equipment(&s->player);
    col_sync_weapon_set_from_equipped_weapon(s);
    col_mark_live_loadout_dirty(s);
}

static int col_can_use_consumable_kind(
    const ColosseumState* s,
    OsrsConsumableKind kind
) {
    if (s->player.potion_timer > 0) return 0;
    switch (kind) {
        case OSRS_CONSUMABLE_BREW:
            return s->player.current_hitpoints < s->player.base_hitpoints;
        case OSRS_CONSUMABLE_SUPER_RESTORE:
        case OSRS_CONSUMABLE_SANFEW: {
            int pray_missing = s->player.base_prayer - s->player.current_prayer;
            int restore_amount = kind == OSRS_CONSUMABLE_SANFEW
                ? osrs_sanfew_restore_amount(s->player.base_prayer)
                : osrs_super_restore_amount(s->player.base_prayer);
            int stats_drained =
                s->player.current_attack < s->player.base_attack ||
                s->player.current_strength < s->player.base_strength ||
                s->player.current_defence < s->player.base_defence ||
                s->player.current_ranged < s->player.base_ranged ||
                s->player.current_magic < s->player.base_magic;
            int pray_worth = pray_missing >= (restore_amount + 1) / 2;
            int venom_cure = kind == OSRS_CONSUMABLE_SANFEW && s->player_venom > 0;
            return stats_drained || pray_worth || venom_cure;
        }
        case OSRS_CONSUMABLE_SUPER_COMBAT:
            return s->player.current_attack >= s->player.base_attack &&
                s->player.current_attack <= s->player.base_attack + 5;
        case OSRS_CONSUMABLE_DIVINE_COMBAT:
            return s->player.current_hitpoints > 10 &&
                s->player.current_attack >= s->player.base_attack &&
                s->player.current_attack <= s->player.base_attack + 5;
        case OSRS_CONSUMABLE_RANGING:
            return s->player.current_ranged >= s->player.base_ranged &&
                s->player.current_ranged <= s->player.base_ranged + 5;
        case OSRS_CONSUMABLE_DIVINE_RANGING:
            return s->player.current_hitpoints > 10 &&
                s->player.current_ranged >= s->player.base_ranged &&
                s->player.current_ranged <= s->player.base_ranged + 5;
        case OSRS_CONSUMABLE_SURGE:
            return s->surge_cooldown == 0 && s->player.special_energy < 100;
        case OSRS_CONSUMABLE_GUTHIX_REST:
            return s->player.current_hitpoints < s->player.base_hitpoints ||
                s->player_venom > 0 || s->player_poison > 0;
        case OSRS_CONSUMABLE_SATURATED_HEART:
            return s->player.current_magic <= s->player.base_magic;
        case OSRS_CONSUMABLE_SHARK_FOOD:
            return s->player.food_timer == 0 &&
                s->player.current_hitpoints < s->player.base_hitpoints;
        case OSRS_CONSUMABLE_KARAMBWAN:
            return s->player.karambwan_timer == 0 &&
                s->player.current_hitpoints < s->player.base_hitpoints;
        case OSRS_CONSUMABLE_NONE:
            return 0;
    }
    abort();
}

static int col_inventory_cell_actionable(const ColosseumState* s, int cell_idx) {
    const ColoInvCell* cell = &s->inventory_cells[cell_idx];
    OsrsInventoryClickResolution r = osrs_inventory_click_interpret(
        cell->item_idx, cell->osrs_id, OSRS_CLICK_TICK_FIRST);
    if (r.click_action == OSRS_CLICK_EQUIP) return col_can_equip_from_cell(s, cell_idx);
    if (r.click_action == OSRS_CLICK_DRINK)
        return cell->dose > 0 && col_can_use_consumable_kind(s, r.consumable_kind);
    if (r.click_action == OSRS_CLICK_EAT)
        return col_can_use_consumable_kind(s, r.consumable_kind);
    return 0;
}

static void col_empty_inventory_cell(ColosseumState* s, int cell_idx) {
    s->inventory_cells[cell_idx] = (ColoInvCell){
        .osrs_id = 0,
        .item_idx = ITEM_NONE,
        .dose = 0,
    };
}

static void col_decrement_consumable_cell(
    ColosseumState* s,
    int cell_idx,
    OsrsInventoryClickResolution resolution
) {
    ColoInvCell* cell = &s->inventory_cells[cell_idx];
    if (resolution.click_action == OSRS_CLICK_EAT || resolution.raw_osrs_id_after_drink == 0) {
        col_empty_inventory_cell(s, cell_idx);
        return;
    }
    cell->osrs_id = resolution.raw_osrs_id_after_drink;
    cell->item_idx = ITEM_NONE;
    cell->dose = osrs_consumable_dose_count_after_drink(cell->dose);
}

static void col_apply_food_cell(ColosseumState* s, OsrsConsumableKind kind) {
    FoodType type;
    if (kind == OSRS_CONSUMABLE_SHARK_FOOD) type = FOOD_SHARK;
    else if (kind == OSRS_CONSUMABLE_KARAMBWAN) type = FOOD_KARAMBWAN;
    else {
        fprintf(stderr, "colosseum: consumable kind %d is not food\n", (int)kind);
        abort();
    }

    EatResult r = osrs_eat_food(type, s->player.current_hitpoints,
        s->player.base_hitpoints, osrs_player_food_timer(&s->player, type));
    if (!r.consumed) return;
    if (type == FOOD_KARAMBWAN) {
        s->player.karambwan_timer = 2;
        s->player.food_timer = 3;
        s->player.potion_timer = 3;
        s->player.ate_karambwan_this_tick = 1;
        s->player.last_karambwan_heal = r.hp_healed;
    } else {
        s->player.food_timer = 3;
        s->player.ate_food_this_tick = 1;
        s->player.last_food_heal = r.hp_healed;
    }
    s->player.current_hitpoints += r.hp_healed;
    if (s->player.current_hitpoints > s->player.base_hitpoints)
        s->player.current_hitpoints = s->player.base_hitpoints;
}

static void col_apply_drink_cell(ColosseumState* s, OsrsConsumableKind kind) {
    switch (kind) {
        case OSRS_CONSUMABLE_BREW:
            encounter_apply_brew_heal_and_timer(
                &s->player, osrs_brew_heal_amount(s->player.base_hitpoints));
            encounter_apply_consumable_stat_effect(
                &s->player, ENCOUNTER_CONSUMABLE_STAT_EFFECT_BREW_DRAIN);
            col_finish_stat_moving_consumable(s);
            return;
        case OSRS_CONSUMABLE_SUPER_RESTORE:
            encounter_add_prayer_restore(
                &s->player, osrs_super_restore_amount(s->player.base_prayer));
            encounter_apply_consumable_stat_effect(
                &s->player, ENCOUNTER_CONSUMABLE_STAT_EFFECT_RESTORE);
            encounter_cap_prayer_restore(&s->player);
            encounter_finish_potion_dose(&s->player, &s->player.restore_doses);
            col_finish_stat_moving_consumable(s);
            return;
        case OSRS_CONSUMABLE_SANFEW:
            encounter_add_prayer_restore(
                &s->player, osrs_sanfew_restore_amount(s->player.base_prayer));
            encounter_apply_consumable_stat_effect(
                &s->player, ENCOUNTER_CONSUMABLE_STAT_EFFECT_SANFEW);
            s->player_venom = 0;
            s->player_venom_timer = 0;
            s->player_poison = 0;
            s->player_poison_timer = 0;
            encounter_cap_prayer_restore(&s->player);
            encounter_finish_potion_dose(&s->player, &s->player.restore_doses);
            col_finish_stat_moving_consumable(s);
            return;
        case OSRS_CONSUMABLE_SUPER_COMBAT:
            encounter_super_combat_boost(&s->player);
            col_enforce_divine_stat_floors(s);
            encounter_finish_potion_dose(&s->player, &s->player.combat_potion_doses);
            col_mark_live_loadout_dirty(s);
            return;
        case OSRS_CONSUMABLE_DIVINE_COMBAT:
            encounter_super_combat_boost(&s->player);
            col_apply_divine_combat_potion_effect(s);
            col_enforce_divine_stat_floors(s);
            encounter_finish_potion_dose(&s->player, &s->player.combat_potion_doses);
            col_mark_live_loadout_dirty(s);
            return;
        case OSRS_CONSUMABLE_RANGING:
            encounter_ranging_boost(&s->player);
            col_enforce_divine_stat_floors(s);
            encounter_finish_potion_dose(&s->player, &s->player.ranged_potion_doses);
            col_mark_live_loadout_dirty(s);
            return;
        case OSRS_CONSUMABLE_DIVINE_RANGING:
            encounter_ranging_boost(&s->player);
            col_apply_divine_ranged_potion_effect(s);
            col_enforce_divine_stat_floors(s);
            encounter_finish_potion_dose(&s->player, &s->player.ranged_potion_doses);
            col_mark_live_loadout_dirty(s);
            return;
        case OSRS_CONSUMABLE_SURGE:
            s->player.special_energy += COLO_SURGE_RESTORE_AMOUNT;
            if (s->player.special_energy > 100) s->player.special_energy = 100;
            s->surge_doses--;
            s->surge_cooldown = COLO_SURGE_COOLDOWN_TICKS;
            s->player.potion_timer = 3;
            return;
        case OSRS_CONSUMABLE_GUTHIX_REST:
            s->player.current_hitpoints += 5;
            if (s->player.current_hitpoints > s->player.base_hitpoints)
                s->player.current_hitpoints = s->player.base_hitpoints;
            s->player_venom = 0;
            s->player_venom_timer = 0;
            s->player_poison = 0;
            s->player_poison_timer = 0;
            s->player.potion_timer = 3;
            return;
        case OSRS_CONSUMABLE_SATURATED_HEART:
            encounter_apply_saturated_heart_boost(&s->player);
            col_mark_live_loadout_dirty(s);
            return;
        case OSRS_CONSUMABLE_NONE:
        case OSRS_CONSUMABLE_SHARK_FOOD:
        case OSRS_CONSUMABLE_KARAMBWAN:
            fprintf(stderr, "colosseum: consumable kind %d is not drinkable\n", (int)kind);
            abort();
    }
    abort();
}

typedef struct {
    int clicked_cells[COLO_INV_CLICK_HEADS];
    int clicked_count;
    int duplicate_cells[COLO_INV_CLICK_HEADS];
    int duplicate_count;
    int equip_cell_by_slot[NUM_GEAR_SLOTS];
    int equip_order_by_slot[NUM_GEAR_SLOTS];
    int eat_cell;
    int eat_order;
    OsrsInventoryClickResolution eat_resolution;
    int drink_cell;
    int drink_order;
    OsrsInventoryClickResolution drink_resolution;
} ColoInventoryTickIntent;

static ColoInventoryTickIntent col_inventory_tick_intent_empty(void) {
    ColoInventoryTickIntent intent;
    memset(&intent, 0, sizeof(intent));
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        intent.equip_cell_by_slot[slot] = -1;
        intent.equip_order_by_slot[slot] = -1;
    }
    intent.eat_cell = -1;
    intent.eat_order = -1;
    intent.drink_cell = -1;
    intent.drink_order = -1;
    return intent;
}

static void col_record_inventory_click_debug_cell(int* cells, int* count, int cell_idx) {
    if (*count >= COLO_INV_CLICK_HEADS) {
        fprintf(stderr, "colosseum inventory debug cell overflow\n");
        abort();
    }
    cells[*count] = cell_idx;
    *count += 1;
}

static ColoInventoryTickIntent col_resolve_inventory_tick_intent(
    const ColosseumState* s,
    const int* actions
) {
    ColoInventoryTickIntent intent = col_inventory_tick_intent_empty();
    int clicked[COLO_INVENTORY_DISPLAY_SLOTS] = {0};
    for (int head = 0; head < COLO_INV_CLICK_HEADS; head++) {
        int action = actions[COLO_HEAD_INV_CLICK_0 + head];
        if (action <= 0 || action > COLO_INVENTORY_DISPLAY_SLOTS) continue;
        int cell_idx = action - 1;
        if (clicked[cell_idx]) {
            (void)osrs_inventory_click_interpret(
                s->inventory_cells[cell_idx].item_idx,
                s->inventory_cells[cell_idx].osrs_id,
                OSRS_CLICK_TICK_DUPLICATE);
            col_record_inventory_click_debug_cell(
                intent.duplicate_cells, &intent.duplicate_count, cell_idx);
            continue;
        }
        clicked[cell_idx] = 1;
        col_record_inventory_click_debug_cell(
            intent.clicked_cells, &intent.clicked_count, cell_idx);

        const ColoInvCell* cell = &s->inventory_cells[cell_idx];
        OsrsInventoryClickResolution resolution = osrs_inventory_click_interpret(
            cell->item_idx, cell->osrs_id, OSRS_CLICK_TICK_FIRST);
        if (resolution.click_action == OSRS_CLICK_EQUIP) {
            if (!col_can_equip_from_cell(s, cell_idx)) continue;
            int gear_slot = osrs_item_gear_slot(cell->item_idx);
            if (gear_slot < 0 || gear_slot >= NUM_GEAR_SLOTS) {
                fprintf(stderr, "colosseum equip intent: invalid gear slot %d\n", gear_slot);
                abort();
            }
            intent.equip_cell_by_slot[gear_slot] = cell_idx;
            intent.equip_order_by_slot[gear_slot] = head;
        } else if (resolution.click_action == OSRS_CLICK_DRINK) {
            if (intent.drink_cell >= 0) continue;
            if (cell->dose > 0 && col_can_use_consumable_kind(s, resolution.consumable_kind)) {
                intent.drink_cell = cell_idx;
                intent.drink_order = head;
                intent.drink_resolution = resolution;
            }
        } else if (resolution.click_action == OSRS_CLICK_EAT) {
            if (intent.eat_cell >= 0) continue;
            if (col_can_use_consumable_kind(s, resolution.consumable_kind)) {
                intent.eat_cell = cell_idx;
                intent.eat_order = head;
                intent.eat_resolution = resolution;
            }
        }
    }
    return intent;
}

static int col_inventory_tick_intent_has_effect(const ColoInventoryTickIntent* intent) {
    if (intent->eat_cell >= 0 || intent->drink_cell >= 0) return 1;
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++)
        if (intent->equip_cell_by_slot[slot] >= 0) return 1;
    return 0;
}

static int col_next_inventory_apply_order(const ColoInventoryTickIntent* intent) {
    int best = COLO_INV_CLICK_HEADS + 1;
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        int order = intent->equip_order_by_slot[slot];
        if (order >= 0 && order < best) best = order;
    }
    if (intent->eat_order >= 0 && intent->eat_order < best) best = intent->eat_order;
    if (intent->drink_order >= 0 && intent->drink_order < best) best = intent->drink_order;
    return best == COLO_INV_CLICK_HEADS + 1 ? -1 : best;
}

static void col_apply_inventory_tick_intent(
    ColosseumState* s,
    ColoInventoryTickIntent* intent
) {
    if (col_inventory_tick_intent_has_effect(intent))
        osrs_interaction_check_interrupt(&s->interaction, OSRS_IACT_EQUIP);

    int order;
    while ((order = col_next_inventory_apply_order(intent)) >= 0) {
        for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
            if (intent->equip_order_by_slot[slot] != order) continue;
            col_equip_from_cell(s, intent->equip_cell_by_slot[slot]);
            intent->equip_order_by_slot[slot] = -1;
            intent->equip_cell_by_slot[slot] = -1;
        }
        if (intent->eat_order == order) {
            col_apply_food_cell(s, intent->eat_resolution.consumable_kind);
            col_empty_inventory_cell(s, intent->eat_cell);
            intent->eat_order = -1;
            intent->eat_cell = -1;
        }
        if (intent->drink_order == order) {
            col_apply_drink_cell(s, intent->drink_resolution.consumable_kind);
            col_decrement_consumable_cell(s, intent->drink_cell, intent->drink_resolution);
            intent->drink_order = -1;
            intent->drink_cell = -1;
        }
    }
}

static void col_log_inventory_tick_intent(
    const ColosseumState* s,
    const ColoInventoryTickIntent* intent,
    const int* actions,
    int interrupted
) {
    fprintf(stderr, "colo_action tick=%d inventory raw=", s->tick);
    for (int head = 0; head < COLO_INV_CLICK_HEADS; head++)
        fprintf(stderr, "%s%d", head == 0 ? "" : ",",
            actions[COLO_HEAD_INV_CLICK_0 + head]);
    fprintf(stderr, " clicked=");
    for (int i = 0; i < intent->clicked_count; i++)
        fprintf(stderr, "%s%d", i == 0 ? "" : ",", intent->clicked_cells[i]);
    fprintf(stderr, " duplicates=");
    for (int i = 0; i < intent->duplicate_count; i++)
        fprintf(stderr, "%s%d", i == 0 ? "" : ",", intent->duplicate_cells[i]);
    fprintf(stderr, " equips=");
    int wrote = 0;
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        if (intent->equip_cell_by_slot[slot] < 0) continue;
        fprintf(stderr, "%sslot%d:cell%d", wrote ? "," : "",
            slot, intent->equip_cell_by_slot[slot]);
        wrote = 1;
    }
    fprintf(stderr, " eat_cell=%d drink_cell=%d interrupted=%d\n",
        intent->eat_cell, intent->drink_cell, interrupted);
}


/** a thrall-eligible NPC slot: live, non-hazard, and NOT Sol Heredit (immune). */
static int col_npc_is_thrall_target(const ColosseumState* s, int slot) {
    if (slot < 0 || slot >= COLO_MAX_NPCS) return 0;
    const ColoNPC* npc = &s->npcs[slot];
    return col_npc_is_live_enemy(npc) && npc->type != COLO_SOL_HEREDIT;
}

/** the player's current attack target as an NPC slot, or -1. */
static int col_player_target_slot(const ColosseumState* s) {
    if (!osrs_interaction_active(&s->interaction)) return -1;
    return s->interaction.target_slot;
}

/** summon (or re-summon) the Arceuus thrall onto a thrall-eligible target. The
    17-tick recast gate is the caller's responsibility (the mask + handler). */
static void col_summon_thrall(ColosseumState* s, int target_slot) {
    s->thrall_active = 1;
    s->thrall_target_slot = target_slot;
    s->thrall_lifetime_left = COLO_THRALL_LIFETIME_TICKS[s->active_loadout_profile];
    s->thrall_attack_timer = COLO_THRALL_TICK;
    s->thrall_recast_cd = COLO_THRALL_RECAST_CD;
}

/** advance the thrall + Death-Charge timers one tick. The thrall retargets to the
    player's current target each tick (holding its slot if the player has none),
    fires a uniform 0..3 magic hit every 4 ticks routed through the player-credited
    NPC queue, and despawns when its lifetime expires. The Death-Charge window
    closes silently on expiry (no cooldown until a kill consumes it). */
static void col_tick_thrall_and_death_charge(ColosseumState* s, ColosseumContext* ctx) {
    if (s->thrall_recast_cd > 0) s->thrall_recast_cd--;
    if (s->death_charge_cd > 0) s->death_charge_cd--;
    if (s->death_charge_window_left > 0) s->death_charge_window_left--;

    if (s->thrall_active) {
        s->thrall_lifetime_left--;
        if (s->thrall_lifetime_left <= 0) {
            s->thrall_active = 0;
            s->thrall_target_slot = -1;
            s->thrall_lifetime_left = 0;
            s->thrall_attack_timer = 0;
            return;
        }
        int player_target = col_player_target_slot(s);
        if (col_npc_is_thrall_target(s, player_target))
            s->thrall_target_slot = player_target;
        else if (!col_npc_is_thrall_target(s, s->thrall_target_slot))
            s->thrall_target_slot = -1;

        if (s->thrall_attack_timer > 0) s->thrall_attack_timer--;
        if (s->thrall_attack_timer == 0) {
            s->thrall_attack_timer = COLO_THRALL_TICK;
            if (col_npc_is_thrall_target(s, s->thrall_target_slot)) {
                int dmg = encounter_rand_int(&s->rng_state, COLO_THRALL_MAX_HIT + 1);
                col_queue_npc_pending_hit(s, s->thrall_target_slot, dmg, 2,
                    ATTACK_STYLE_MAGIC, ENCOUNTER_SPELL_NONE);
                if (ctx) ctx->thrall_attacked_this_tick = 1;
            }
        }
    }
}

typedef struct {
    ColosseumState* s;
    const ColosseumContext* ctx;
} ColoWalkCtx;

static int col_tile_walkable(void* ctx, int x, int y) {
    ColoWalkCtx* wc = (ColoWalkCtx*)ctx;
    return col_player_walkable(wc->s, x, y);
}

static int col_pathfind_blocked(void* ctx, int abs_x, int abs_y) {
    ColoWalkCtx* wc = (ColoWalkCtx*)ctx;
    int x = abs_x - wc->ctx->world_offset_x;
    int y = abs_y - wc->ctx->world_offset_y;
    if (col_static_blocked(x, y)) return 1;
    if (col_sol_clamp_active(wc->s) && !col_in_boss_arena(wc->s, x, y)) return 1;
    return 0;
}

static int col_npc_collision_flag_at(const ColosseumState* s, int x, int y) {
    int gx, gy;
    if (!col_grid_index(x, y, &gx, &gy)) return 0;
    return s->npc_collision_flags[gx][gy] != 0;
}

/** the player's effective attack reach, after Myopia's range reduction. */
static int col_player_attack_range(ColosseumState* s) {
    const EncounterLoadoutStats* ls = col_live_loadout_stats(s);
    int base = ls->attack_range > 0 ? ls->attack_range : 1;
    return col_mod_myopia_range(s, base);
}

/** attack-target lookup callback: returns the NPC at an obs slot. The reach the
    chase uses is the PLAYER's effective range (Myopia-reduced), not the NPC's. */
static int col_lookup_player_attack_target(void* ctx, int target_slot, OsrsAttackTarget* out) {
    ColosseumState* s = (ColosseumState*)ctx;
    if (target_slot < 0 || target_slot >= COLO_MAX_NPCS) return 0;
    ColoNPC* npc = &s->npcs[target_slot];
    if (!col_npc_is_live_target(npc)) return 0;
    out->slot = target_slot;
    out->x = npc->x;
    out->y = npc->y;
    out->size = col_npc_effective_size(npc);
    out->attack_range = col_player_attack_range(s);
    return 1;
}

/** map an obs slot (action target index) to an NPC array index, or -1. */
static int col_obs_slot_to_npc(const ColosseumState* s, int obs_slot) {
    if (obs_slot < 0 || obs_slot >= COLO_OBS_NPCS) return -1;
    return s->current_obs_slots[obs_slot];
}


/** A14: a weapon's melee style = the argmax of its stab/slash/crush attack
    bonuses (the scythe/fang are slash, elder maul is crush, so Sol's slash-5
    weakness and the right defence lookups are live). */
static MeleeStyle col_item_melee_style(uint8_t weapon_item) {
    const Item* weapon = get_item(weapon_item);
    if (!weapon) return MELEE_STYLE_STAB;
    MeleeStyle style = MELEE_STYLE_STAB;
    int best = weapon->attack_stab;
    if (weapon->attack_slash > best) { best = weapon->attack_slash; style = MELEE_STYLE_SLASH; }
    if (weapon->attack_crush > best) { style = MELEE_STYLE_CRUSH; }
    return style;
}

/** effective NPC defence level after player spec drains (elder maul). */
static int col_npc_effective_def_level(const ColoNPC* npc, const ColoNpcStats* ns) {
    int def = ns->def_level - npc->def_drained;
    return def < 0 ? 0 : def;
}

/** melee defence bonus on `ns` against a given attacking melee style. */
static int col_melee_def_bonus(const ColoNpcStats* ns, MeleeStyle ms) {
    return ms == MELEE_STYLE_SLASH ? ns->slash_def
         : ms == MELEE_STYLE_CRUSH ? ns->crush_def
         : ns->stab_def;
}

/**
 * Resolve a player attack on the targeted NPC. Rolls accuracy + damage via the
 * shared combat math and queues a pending hit. Honors the Fremennik RPS weakness
 * (style match = forced max), the B7 perfect-parry guaranteed max (armed by a
 * last-tick grapple parry, consumed by the very next attack), and the A14
 * melee-style defence lookup (the weapon's style picks stab/slash/crush def).
 * A21+B9: 1-HP hazard entities (totem/bee) die to ANY attack of any style —
 * the hit is guaranteed for their full HP, no rolls, and it does not consume
 * an armed perfect-parry max; the attack still spends the weapon cycle.
 */
/** L2/L3: fire the armed special attack with the spec weapon's loadout stats
    (melee gear base, weapon slot swapped). Queues one pending hit per splat
    (claws cascade = 4), spends energy via the shared resolver cost, applies
    the resolver's SGS heal/prayer restore and the elder maul 35%% defence
    drain, consumes Sol perfect-parry guaranteed max for specs too (E2), and
    runs the post-attack item effects (blood fury heal). */
static void col_player_spec_attack(ColosseumState* s, int npc_slot) {
    ColoNPC* npc = &s->npcs[npc_slot];
    const ColoNpcStats* ns = &COLO_NPC_STATS[npc->type];
    uint8_t item = s->player.equipped[GEAR_SLOT_WEAPON];
    const EncounterLoadoutStats* ls = col_live_loadout_stats(s);
    const OsrsEquipmentEffectProfile* effects = col_live_effects(s);

    s->player.spec_armed = 0;
    if (!encounter_use_spec(&s->player, osrs_spec_cost(item))) return;

    int eff_def = col_npc_effective_def_level(npc, ns);
    int def_bonus = col_melee_def_bonus(ns, col_item_melee_style(item));
    int def_roll = (eff_def + 9) * (def_bonus + 64);
    int att_roll = osrs_player_att_roll(ls->eff_level, ls->attack_bonus);

    SpecResult r = osrs_resolve_spec(item, att_roll, ls->max_hit, def_roll,
                                     eff_def, &s->rng_state);
    if (s->wave == COLO_WAVE_BOSS && s->sol.started && s->sol.next_attack_guaranteed_max) {
        osrs_spec_result_force_max(&r, item, ls->max_hit, eff_def);
        s->sol.next_attack_guaranteed_max = 0;
        s->sol.guaranteed_max_ticks = 0;
    }

    int total = 0;
    int num_hits = r.num_hits > 0 ? r.num_hits : 1;
    for (int i = 0; i < num_hits; i++) {
        col_queue_npc_pending_hit(s, npc_slot, r.damage[i], 1,
                                  ATTACK_STYLE_MELEE, ENCOUNTER_SPELL_NONE);
        total += r.damage[i];
    }

    if (r.def_drain > 0) {
        npc->def_drained += r.def_drain;
        if (npc->def_drained > ns->def_level) npc->def_drained = ns->def_level;
    }
    if (r.heal > 0) {
        s->player.current_hitpoints += r.heal;
        if (s->player.current_hitpoints > s->player.base_hitpoints)
            s->player.current_hitpoints = s->player.base_hitpoints;
    }
    if (r.prayer_restore > 0) {
        s->player.current_prayer += r.prayer_restore;
        if (s->player.current_prayer > s->player.base_prayer)
            s->player.current_prayer = s->player.base_prayer;
    }

    OsrsPostAttackEffects post = osrs_finalize_attack_effects(
        effects, &s->player.item_effect_state, item,
        ATTACK_STYLE_MELEE, OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1,
        0, total > 0, total, &s->rng_state);
    if (post.heal_amount > 0) {
        s->player.current_hitpoints += post.heal_amount;
        if (s->player.current_hitpoints > s->player.base_hitpoints)
            s->player.current_hitpoints = s->player.base_hitpoints;
    }

    s->player.attack_timer = ls->attack_speed > 0 ? ls->attack_speed : 4;
    s->player_attack_npc_idx = npc_slot;
    s->player_attack_dmg = total;
    s->player_attack_style_id = ATTACK_STYLE_MELEE;
    s->player_attack_timing = (EncounterProjectileTiming){
        .damage_delay_ticks = 1,
        .visual_start_delay_ticks = 0,
        .visual_duration_ticks = 1,
    };
    s->player.attack_style_this_tick = ATTACK_STYLE_MELEE;
    s->player.used_special_this_tick = 1;
    s->tick_scratch.player_attacked = 1;
    s->tick_scratch.attacks_fired++;
}

/** scythe resolution caps: up to 3 size-based hits on the primary (100/50/25)
    plus up to 2 distinct extra 1x1 targets in the frontal arc (each full 100%). */
#define COLO_SCYTHE_MAX_HITS 5

typedef struct {
    int npc_slot;
    int splat_rank;
} ColScytheResolvedHit;

static int col_signum(int value) {
    return (value > 0) - (value < 0);
}

static int col_npc_occupies_tile(const ColoNPC* npc, int x, int y) {
    int size = col_npc_effective_size(npc);
    return x >= npc->x && x < npc->x + size &&
           y >= npc->y && y < npc->y + size;
}

static int col_scythe_tile_target(
    const ColosseumState* s,
    int primary_slot,
    int x,
    int y
) {
    const ColoNPC* primary = &s->npcs[primary_slot];
    if (col_npc_is_live_target(primary) && col_npc_occupies_tile(primary, x, y))
        return primary_slot;

    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (i == primary_slot) continue;
        const ColoNPC* npc = &s->npcs[i];
        if (!col_npc_is_live_enemy(npc)) continue;
        if (col_npc_occupies_tile(npc, x, y))
            return i;
    }
    return -1;
}

static void col_scythe_front_arc_tiles(
    const ColosseumState* s,
    const ColoNPC* primary,
    int tile_x[3],
    int tile_y[3]
) {
    int size = col_npc_effective_size(primary);
    int closest_x = s->player.x;
    int closest_y = s->player.y;
    if (closest_x < primary->x) closest_x = primary->x;
    else if (closest_x >= primary->x + size) closest_x = primary->x + size - 1;
    if (closest_y < primary->y) closest_y = primary->y;
    else if (closest_y >= primary->y + size) closest_y = primary->y + size - 1;

    int dx = col_signum(closest_x - s->player.x);
    int dy = col_signum(closest_y - s->player.y);
    if (dx == 0 && dy == 0) {
        fprintf(stderr, "invalid scythe target direction\n");
        abort();
    }

    tile_x[0] = s->player.x + dx;
    tile_y[0] = s->player.y + dy;
    if (dx != 0 && dy != 0) {
        tile_x[1] = s->player.x + dx;
        tile_y[1] = s->player.y;
        tile_x[2] = s->player.x;
        tile_y[2] = s->player.y + dy;
        return;
    }

    int left_x = -dy;
    int left_y = dx;
    tile_x[1] = tile_x[0] + left_x;
    tile_y[1] = tile_y[0] + left_y;
    tile_x[2] = tile_x[0] - left_x;
    tile_y[2] = tile_y[0] - left_y;
}

/** Resolve the two distinct OSRS scythe mechanics into independent hits:
    (A) the PRIMARY target takes size-based multihit — 1/2/3 hits at splat ranks
        0/1/2 (100%/50%/25% of max), capped at 3 (osrs_scythe_splats_for_target_size);
    (B) the 1x3 frontal arc additionally catches SEPARATE live enemies, each of
        which takes one full 100% hit (splat rank 0), deduped by slot.
    The caller rolls accuracy + strength independently per resolved hit. Matches
    .refs/osrs-dps-calc PlayerVsNPCCalc.ts (plain max>>i, no even/div-4 bonus). */
static int col_resolve_scythe_hits(
    const ColosseumState* s,
    int primary_slot,
    ColScytheResolvedHit out[COLO_SCYTHE_MAX_HITS]
) {
    const ColoNPC* primary = &s->npcs[primary_slot];
    int count = 0;

    int primary_splats = osrs_scythe_splats_for_target_size(col_npc_effective_size(primary));
    for (int rank = 0; rank < primary_splats; rank++) {
        out[count++] = (ColScytheResolvedHit){
            .npc_slot = primary_slot,
            .splat_rank = rank,
        };
    }

    if (col_npc_occupies_tile(primary, s->player.x, s->player.y))
        return count;

    int tile_x[3];
    int tile_y[3];
    col_scythe_front_arc_tiles(s, primary, tile_x, tile_y);
    for (int t = 0; t < 3 && count < COLO_SCYTHE_MAX_HITS; t++) {
        int target_slot = col_scythe_tile_target(s, primary_slot, tile_x[t], tile_y[t]);
        if (target_slot < 0 || target_slot == primary_slot) continue;
        int already = 0;
        for (int j = 0; j < count; j++)
            if (out[j].npc_slot == target_slot) { already = 1; break; }
        if (already) continue;
        out[count++] = (ColScytheResolvedHit){
            .npc_slot = target_slot,
            .splat_rank = 0,
        };
    }
    return count;
}

static int col_npc_target_def_roll(
    const ColoNPC* npc,
    const ColoNpcStats* ns,
    AttackStyle style,
    MeleeStyle weapon_melee_style
) {
    int def_bonus;
    if (style == ATTACK_STYLE_MAGIC) {
        def_bonus = ns->magic_def_bonus;
    } else if (style == ATTACK_STYLE_RANGED) {
        def_bonus = ns->ranged_def_bonus;
    } else {
        def_bonus = col_melee_def_bonus(ns, weapon_melee_style);
    }
    return (col_npc_effective_def_level(npc, ns) + 9) * (def_bonus + 64);
}

static void col_player_attack_target(ColosseumState* s, int npc_slot) {
    ColoNPC* npc = &s->npcs[npc_slot];
    const ColoNpcStats* ns = &COLO_NPC_STATS[npc->type];
    const EncounterLoadoutStats* ls = col_live_loadout_stats(s);

    if (!col_type_is_hazard_entity(npc->type) && s->player.spec_armed) {
        col_player_spec_attack(s, npc_slot);
        return;
    }

    /* L11: per-set item effects (tbow target-magic scaling, bowfa + crystal
       armour) scale the base roll and max hit before any branch uses them. */
    const OsrsEquipmentEffectProfile* effects = col_live_effects(s);
    uint8_t weapon_item = s->player.equipped[GEAR_SLOT_WEAPON];
    int projectile_dist = encounter_projectile_distance(
        s->player.x, s->player.y, 1,
        npc->x, npc->y, col_npc_effective_size(npc),
        ENCOUNTER_PROJECTILE_DISTANCE_CLOSEST_TILE);
    EncounterProjectileTiming hit_timing =
        col_player_projectile_timing(ls->style, weapon_item, projectile_dist);
    int base_att_roll = osrs_player_att_roll(ls->eff_level, ls->attack_bonus);
    MeleeStyle weapon_melee_style = col_item_melee_style(weapon_item);
    OsrsPreparedAttackEffects prepared = osrs_prepare_attack_effects_for_melee_style(
        effects, &s->player.item_effect_state, weapon_item, ls->style,
        weapon_melee_style, OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1,
        base_att_roll, ls->max_hit,
        osrs_target_effect_context_magic(ns->magic_level, ns->magic_att_bonus),
        s->player.current_hitpoints, s->player.base_hitpoints);

    ColScytheResolvedHit resolved_hits[COLO_SCYTHE_MAX_HITS];
    int resolved_count = 1;
    resolved_hits[0] = (ColScytheResolvedHit){ .npc_slot = npc_slot, .splat_rank = 0 };
    if (weapon_item == ITEM_SCYTHE_OF_VITUR && !col_type_is_hazard_entity(npc->type))
        resolved_count = col_resolve_scythe_hits(s, npc_slot, resolved_hits);
    if (resolved_count <= 0) {
        fprintf(stderr, "scythe attack resolved no targets\n");
        abort();
    }

    int ticks = ls->attack_range > 1 ? 3 : 1;
    int damage = 0;
    for (int splat = 0; splat < resolved_count; splat++) {
        ColScytheResolvedHit hit = resolved_hits[splat];
        ColoNPC* hit_npc = &s->npcs[hit.npc_slot];
        const ColoNpcStats* hit_ns = &COLO_NPC_STATS[hit_npc->type];
        int rps_max_hit = hit_ns->player_style_that_max_hits != ATTACK_STYLE_NONE &&
                          hit_ns->player_style_that_max_hits == ls->style;
        int target_def_roll = col_npc_target_def_roll(
            hit_npc, hit_ns, ls->style, weapon_melee_style);
        int splat_max = prepared.max_hit >> hit.splat_rank;
        int splat_damage;
        if (col_type_is_hazard_entity(hit_npc->type)) {
            splat_damage = hit_npc->hp;
        } else if (s->sol.next_attack_guaranteed_max) {
            splat_damage = splat_max;
            s->sol.next_attack_guaranteed_max = 0;
            s->sol.guaranteed_max_ticks = 0;
        } else if (rps_max_hit) {
            splat_damage = splat_max;
        } else {
            splat_damage = osrs_roll_prepared_attack_damage(
                &prepared, target_def_roll, splat_max, &s->rng_state);
        }
        col_queue_npc_pending_hit(s, hit.npc_slot, splat_damage, ticks, ls->style,
                                  ENCOUNTER_SPELL_NONE);
        damage += splat_damage;
    }

    /* blood fury heal (beginner melee neck) via the shared post-attack effects */
    OsrsPostAttackEffects post = osrs_finalize_attack_effects(
        effects, &s->player.item_effect_state, weapon_item, ls->style,
        OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1, 0,
        damage > 0, damage, &s->rng_state);
    if (post.heal_amount > 0) {
        s->player.current_hitpoints += post.heal_amount;
        if (s->player.current_hitpoints > s->player.base_hitpoints)
            s->player.current_hitpoints = s->player.base_hitpoints;
    }

    s->player.attack_timer = ls->attack_speed > 0 ? ls->attack_speed : 4;
    s->player_attack_npc_idx = npc_slot;
    s->player_attack_dmg = damage;
    s->player_attack_style_id = ls->style;
    s->player_attack_timing = hit_timing;
    s->player.attack_style_this_tick = ls->style;
    s->tick_scratch.player_attacked = 1;
    s->tick_scratch.attacks_fired++;
}


/** the main per-tick player routine. OSRS order: target -> move -> attack. */
static void col_tick_player_ctx(
    ColosseumState* s,
    ColosseumContext* ctx,
    const int* actions,
    int can_attack
) {
    /* spec energy regen runs every tick, gaps included (OSRS regen never pauses) */
    encounter_tick_spec_regen(&s->player);

    if (s->player.potion_timer > 0) s->player.potion_timer--;
    if (s->player.food_timer > 0) s->player.food_timer--;
    if (s->player.karambwan_timer > 0) s->player.karambwan_timer--;
    if (encounter_tick_saturated_heart(&s->player))
        col_mark_live_loadout_dirty(s);

    if (!ctx->config.mask_inventory_heads) {
        ColoInventoryTickIntent intent = col_resolve_inventory_tick_intent(s, actions);
        if (ctx->config.action_debug_log)
            col_log_inventory_tick_intent(
                s, &intent, actions, col_inventory_tick_intent_has_effect(&intent));
        col_apply_inventory_tick_intent(s, &intent);
    }

    int spec_act = actions[COLO_HEAD_SPEC];
    uint8_t equipped_weapon = s->player.equipped[GEAR_SLOT_WEAPON];
    int spec_cost = osrs_spec_cost(equipped_weapon);
    if (spec_act == 1 && spec_cost > 0 && s->player.special_energy >= spec_cost) {
        s->player.spec_armed = 1;
    } else if (spec_act == 2) {
        s->player.spec_armed = 0;
    }

    /* Arceuus spells (L: thralls + Death Charge). summon-thrall is gated on
       recast_cd==0 && a thrall-eligible (live, non-Sol) current target; a summon
       while a thrall is already up REPLACES it (only one thrall at a time, OSRS),
       still paying the 17-tick recast. death-charge arms the 100-tick window when
       ready (cd==0 && not already armed). Frozen while a draft is open (B6), like
       move/target; the thrall tick + timers are separately can_attack-gated so
       nothing acts during any gap. The mask enforces these gates; the handler
       re-checks defensively. */
    int spell_act = s->modifiers.draft_pending ? 0 : actions[COLO_HEAD_SPELL];
    if (spell_act == COLO_SPELL_SUMMON_THRALL && s->thrall_recast_cd == 0) {
        int target = col_player_target_slot(s);
        if (col_npc_is_thrall_target(s, target)) col_summon_thrall(s, target);
    } else if (spell_act == COLO_SPELL_DEATH_CHARGE &&
            s->death_charge_cd == 0 && s->death_charge_window_left == 0) {
        s->death_charge_window_left = COLO_DEATH_CHARGE_WINDOW;
    }

    /* modifier draft: index 0 = STILL DECIDING (no skip exists, B6); index k
       activates draft option (k-1). Only acts while a draft is pending (the
       mask gates the rest). */
    int modifier_act = actions[COLO_HEAD_MODIFIER_SELECT];
    if (modifier_act > 0 && s->modifiers.draft_pending)
        col_modifier_apply_selection(s, modifier_act - 1);

    /* B6: the player is FROZEN while a draft is open — movement, targeting,
       and attacks are ignored until the pick (which closes draft_pending the
       same tick, so post-pick actions take effect from the next tick). */
    if (s->modifiers.draft_pending) return;

    /* grapple parry: during an active Sol grapple, action index k clicks
       grapple slot (k-1) of the 5-slot A12 domain. A correct slot parries;
       clicking on the last tick is a perfect parry. */
    int grapple_act = actions[COLO_HEAD_GRAPPLE_PARRY];
    if (grapple_act > 0 && grapple_act <= COLO_NUM_GRAPPLE_SLOTS)
        col_sol_grapple_parry_attempt(s, grapple_act - 1);

    /* target acquisition: translate the obs-slot target action into an NPC slot */
    int target_act = actions[COLO_HEAD_TARGET];
    int has_new_target = 0;
    int new_target_slot = -1;
    if (target_act > 0) {
        int npc_slot = col_obs_slot_to_npc(s, target_act - 1);
        if (npc_slot >= 0 && s->npcs[npc_slot].active) {
            has_new_target = 1;
            new_target_slot = npc_slot;
        }
    }

    int has_explicit_move = (actions[COLO_HEAD_MOVE] > 0 || s->player_dest_x >= 0);
    OsrsPlayerMoveKind move_kind = OSRS_PLAYER_MOVE_NONE;
    int move_act = actions[COLO_HEAD_MOVE];
    if (has_explicit_move) {
        if (s->player_dest_x >= 0) move_kind = OSRS_PLAYER_MOVE_DESTINATION;
        else if (move_act > 0 && move_act < ENCOUNTER_MOVE_ACTIONS)
            move_kind = OSRS_PLAYER_MOVE_ACTION;
    }
    int player_x_before_step = s->player.x;
    int player_y_before_step = s->player.y;
    int npc_flag_before_step =
        col_npc_collision_flag_at(s, player_x_before_step, player_y_before_step);
    int interaction_before_step = osrs_interaction_active(&s->interaction)
        ? s->interaction.target_slot : -1;
    ColoWalkCtx wc = { s, ctx };
    OsrsLosQuery los_query = col_player_los_query(s);
    OsrsPlayerStepInput step_input = {
        .player = &s->player,
        .interaction = &s->interaction,
        .target_lookup = col_lookup_player_attack_target,
        .target_ctx = s,
        .has_new_target = has_new_target,
        .new_target_slot = new_target_slot,
        .move_kind = move_kind,
        .move_action = move_act,
        .dest_x = &s->player_dest_x,
        .dest_y = &s->player_dest_y,
        .arena = {
            .collision_map = ctx->collision_map,
            .world_offset_x = ctx->world_offset_x,
            .world_offset_y = ctx->world_offset_y,
            .is_walkable = col_tile_walkable,
            .walkable_ctx = &wc,
            .extra_blocked = col_pathfind_blocked,
            .blocked_ctx = &wc,
            .los_query = &los_query,
            .arena_base_x = COLO_ARENA_MIN_X,
            .arena_base_y = COLO_ARENA_MIN_Y,
            .arena_w = COLO_ARENA_WIDTH,
            .arena_h = COLO_ARENA_HEIGHT,
        },
    };
    OsrsPlayerStepResult step_result = osrs_encounter_player_step(&step_input);
    if (ctx->config.action_debug_log) {
        fprintf(stderr,
            "colo_action tick=%d movement move_kind=%d move_action=%d "
            "dest=%d,%d has_new_target=%d new_target=%d interaction=%d->%d "
            "pos=%d,%d->%d,%d moved=%d explicit=%d chased=%d can_attack=%d\n",
            s->tick, move_kind, move_act, s->player_dest_x, s->player_dest_y,
            has_new_target, new_target_slot, interaction_before_step,
            step_result.target_slot, player_x_before_step, player_y_before_step,
            s->player.x, s->player.y, step_result.moved,
            step_result.explicit_moved, step_result.chased_target,
            step_result.can_attack);
        fprintf(stderr,
            "colo_action tick=%d movement_walkability npc_flag=%d->%d "
            "walkable=%d->%d path_extra=%d->%d\n",
            s->tick, npc_flag_before_step,
            col_npc_collision_flag_at(s, s->player.x, s->player.y),
            col_player_walkable(s, player_x_before_step, player_y_before_step),
            col_player_walkable(s, s->player.x, s->player.y),
            col_pathfind_blocked(&wc,
                player_x_before_step + ctx->world_offset_x,
                player_y_before_step + ctx->world_offset_y),
            col_pathfind_blocked(&wc,
                s->player.x + ctx->world_offset_x,
                s->player.y + ctx->world_offset_y));
    }
    col_rebuild_player_collision_flags(s);

    /* attack */
    if (can_attack && s->player.attack_timer > 0) s->player.attack_timer--;
    if (can_attack && osrs_interaction_active(&s->interaction) && s->player.attack_timer == 0) {
        int slot = s->interaction.target_slot;
        ColoNPC* npc = &s->npcs[slot];
        if (npc->active) {
            int player_range = col_player_attack_range(s);
            if (encounter_player_can_attack(s->player.x, s->player.y,
                    npc->x, npc->y, col_npc_effective_size(npc),
                    player_range, &los_query)) {
                /* render-only: capture the armed spec kind before the attack
                   consumes it, so the player render swaps to the spec weapon. */
                int armed_kind = (!col_type_is_hazard_entity(npc->type) &&
                                  s->player.spec_armed)
                    ? s->player.equipped[GEAR_SLOT_WEAPON] : 0;
                col_player_attack_target(s, slot);
                if (ctx && armed_kind > 0 && s->player.used_special_this_tick)
                    ctx->player_spec_fired_kind = armed_kind;
            }
        }
    }

    /* the thrall auto-attacks + Death-Charge timers advance only during live
       gameplay (paused across wave/ready gaps, like the surge cooldown). */
    if (can_attack) col_tick_thrall_and_death_charge(s, ctx);
}

static void col_tick_player(ColosseumState* s, const int* actions, int can_attack) {
    col_tick_player_ctx(s, col_legacy_context(), actions, can_attack);
}
