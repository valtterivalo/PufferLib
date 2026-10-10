#include "ocean/osrs/encounters/encounter_nh_pvp.h"
#include "ocean/osrs/tests/osrs_test_check.h"
#include "src/puffercpu.c"
#include "ocean/osrs/osrs_combat_visuals.h"

static NhPvpState state;
static NhPvpContext context;
static OsrsEnv original;
static float before[NH_PVP_NUM_OBS], after[NH_PVP_NUM_OBS];

static void reset(void) {
    ENCOUNTER_NH_PVP.init_state((EncounterState*)&state, (EncounterContext*)&context);
    ENCOUNTER_NH_PVP.init_context((EncounterContext*)&context);
    ENCOUNTER_NH_PVP.put_int((EncounterState*)&state, (EncounterContext*)&context, "use_c_opponent", 0);
    ENCOUNTER_NH_PVP.finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    ENCOUNTER_NH_PVP.reset((EncounterState*)&state, (EncounterContext*)&context, 73);
    state.env.auto_reset = 0;
    state.env.tick = 10;
    for (int actor = 0; actor < 2; actor++) {
        state.env.players[actor].x = FIGHT_AREA_BASE_X + 10 + 5 * actor;
        state.env.players[actor].y = FIGHT_AREA_BASE_Y + 10;
        state.env.players[actor].has_attack_timer = 0;
    }
}

static void observations(void) {
    pvp_write_observations(before, &state.env, 0);
    original = state.env;
}

static void test_item_association(void) {
    reset();
    Player* player = &state.env.players[0];
    player->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_ANCESTRAL_TOP);
    player->inventory_cells[1] = osrs_inventory_cell_from_item(ITEM_BLACK_DHIDE_BODY);
    observations();
    int feature = NH_PVP_INVENTORY_START + 15 + 3;
    CHECK("cell carries actual item magic attack bonus",
        before[feature] == ITEM_DATABASE[ITEM_ANCESTRAL_TOP].attack_magic / STAT_NORM_ATTACK);
    CHECK("second cell retains different candidate bonus",
        before[feature + NH_PVP_INVENTORY_FEATURES] == ITEM_DATABASE[ITEM_BLACK_DHIDE_BODY].attack_magic / STAT_NORM_ATTACK);
    OsrsInventoryCell swap = player->inventory_cells[0];
    player->inventory_cells[0] = player->inventory_cells[1];
    player->inventory_cells[1] = swap;
    pvp_write_observations(after, &state.env, 0);
    CHECK("inventory swap preserves association rather than pooled maximum",
        memcmp(before + NH_PVP_INVENTORY_START, after + NH_PVP_INVENTORY_START + NH_PVP_INVENTORY_FEATURES,
            NH_PVP_INVENTORY_FEATURES * sizeof(float)) == 0 &&
        memcmp(before + NH_PVP_INVENTORY_START + NH_PVP_INVENTORY_FEATURES, after + NH_PVP_INVENTORY_START,
            NH_PVP_INVENTORY_FEATURES * sizeof(float)) == 0);
    float weights[NH_PVP_NUM_OBS] = {0}, output;
    weights[feature] = 2;
    weights[feature + NH_PVP_INVENTORY_FEATURES] = -1;
    Linear encoder = {.output = &output, .weights = weights, .batch_size = 1,
        .input_dim = NH_PVP_NUM_OBS, .output_dim = 1};
    linear(&encoder, before);
    float first = output;
    linear(&encoder, after);
    CHECK("CPU dense encoder sees candidate position without item-code weights",
        first != output && output == 2 * after[feature] - after[feature + NH_PVP_INVENTORY_FEATURES]);
}

static void test_visible_boundary(void) {
    reset();
    observations();
    Player* target = &state.env.players[1];
    target->current_hitpoints = 1;
    target->base_hitpoints = 1;
    target->current_prayer = 0;
    target->special_energy = 0;
    target->current_magic = 1;
    target->current_defence = 1;
    target->attack_timer = 50;
    target->food_timer = 10;
    target->frozen_ticks = 32;
    target->freeze_immunity_ticks = 5;
    slot_equip_item(target, GEAR_SLOT_RING, ITEM_BERSERKER_RING_I);
    slot_equip_item(target, GEAR_SLOT_AMMO, ITEM_DRAGON_ARROWS);
    memset(target->inventory_cells, 0, sizeof(target->inventory_cells));
    pvp_write_observations(after, &state.env, 0);
    CHECK("hidden target state ring ammo and inventory remain absent", memcmp(before, after, sizeof(before)) == 0);
    state.env = original;
    slot_equip_item(&state.env.players[1], GEAR_SLOT_WEAPON, ITEM_WHIP);
    observations();
    slot_equip_item(&state.env.players[1], GEAR_SLOT_WEAPON, ITEM_VOIDWAKER);
    pvp_write_observations(after, &state.env, 0);
    CHECK("same-style target weapon identity is distinguishable", before[NH_PVP_TARGET_EQUIPPED_START + 3] != after[NH_PVP_TARGET_EQUIPPED_START + 3]);
    slot_equip_item(&state.env.players[1], GEAR_SLOT_BODY, ITEM_ANCESTRAL_TOP);
    observations();
    slot_equip_item(&state.env.players[1], GEAR_SLOT_BODY, ITEM_BLACK_DHIDE_BODY);
    pvp_write_observations(after, &state.env, 0);
    CHECK("visible armor exposes actual public bonuses", before[NH_PVP_TARGET_BONUS_START + 8] != after[NH_PVP_TARGET_BONUS_START + 8]);
    state.env = original;
    state.env.pvp_runtime.visible[1].last_freeze_tick = state.env.tick - 2;
    observations();
    state.env.players[1].frozen_ticks = 0;
    pvp_write_observations(after, &state.env, 0);
    CHECK("completed freeze application does not reveal remaining timer", memcmp(before, after, sizeof(before)) == 0 &&
        after[NH_PVP_TARGET_FREEZE_START] == 1 && after[NH_PVP_TARGET_FREEZE_START + 1] == 2.0f / 32);
    reset();
    PendingHit freeze = {.kind = OSRS_HIT_DIRECT, .attack_type = ATTACK_STYLE_MAGIC,
        .hit_success = 1, .freeze_ticks = 32};
    apply_damage(&state.env, 0, 1, &freeze);
    pvp_write_observations(after, &state.env, 0);
    CHECK("actual freeze application refreshes the visible event",
        state.env.players[1].frozen_ticks == 32 && after[NH_PVP_TARGET_FREEZE_START] == 1 &&
        after[NH_PVP_TARGET_FREEZE_START + 1] == 0);
    state.env.pvp_runtime.visible[1].move_dx = 1;
    pvp_write_observations(after, &state.env, 0);
    CHECK("completed displacement identifies actual movement", after[NH_PVP_TARGET_START + 11] == 1);
    state.env.pvp_runtime.visible[1].move_dx = 0;
    state.env.players[1].is_moving = 1;
    pvp_write_observations(after, &state.env, 0);
    CHECK("queued frozen movement does not claim displacement", after[NH_PVP_TARGET_START + 11] == 0);
}

static void test_completed_movement(void) {
    reset();
    Player* target = &state.env.players[1];
    int x = target->x, y = target->y;
    target->frozen_ticks = 5;
    state.env.pvp_runtime.walk_dest_x[1] = x + 1;
    state.env.pvp_runtime.walk_dest_y[1] = y;
    pvp_step(&state.env, context.route_topology, context.player_route_cache);
    pvp_write_observations(after, &state.env, 0);
    CHECK("frozen route execution records no completed movement",
        target->x == x && target->y == y && after[NH_PVP_TARGET_START + 11] == 0);
    target->frozen_ticks = 0;
    pvp_step(&state.env, context.route_topology, context.player_route_cache);
    pvp_write_observations(after, &state.env, 0);
    CHECK("unfrozen route execution records completed movement",
        target->x != x && after[NH_PVP_TARGET_START + 11] == 1);
}

static void test_own_capabilities(void) {
    reset();
    Player* player = &state.env.players[0];
    slot_equip_item(player, GEAR_SLOT_WEAPON, ITEM_AHRIM_STAFF);
    observations();
    CHECK("staff ordinary capability describes melee whack", before[36] == 1 && before[38] == 0 && before[39] == 1.0f / 15);
    CHECK("manual spells separately expose legal cast range and speed",
        before[NH_PVP_SELF_CONTROL_START] == 1 && before[NH_PVP_SELF_CONTROL_START + 1] == COMBAT_SPELL_ATTACK_RANGE / 15.0f &&
        before[NH_PVP_SELF_CONTROL_START + 2] == COMBAT_SPELL_CAST_SPEED / 8.0f && before[NH_PVP_SELF_CONTROL_START + 4] == 1);
    state.env.pvp_runtime.walk_dest_x[0] = player->x + 3;
    state.env.pvp_runtime.walk_dest_y[0] = player->y;
    state.env.pvp_runtime.maul[0].preparation = OSRS_GRANITE_MAUL_SELECTED;
    state.env.pvp_runtime.maul[0].prepared_hits = 2;
    pvp_write_observations(after, &state.env, 0);
    CHECK("own route and maul controls no longer alias idle",
        after[NH_PVP_SELF_CONTROL_START + 8] == 1 && after[NH_PVP_SELF_CONTROL_START + 9] == 3.0f / FIGHT_AREA_WIDTH &&
        after[NH_PVP_SELF_CONTROL_START + 12] == 1 && after[NH_PVP_SELF_CONTROL_START + 14] == 1);
    player->is_lunar_spellbook = 1;
    pvp_write_observations(after, &state.env, 0);
    CHECK("lunar book disables both manual ancient capabilities", after[NH_PVP_SELF_CONTROL_START] == 0 && after[NH_PVP_SELF_CONTROL_START + 4] == 0);
}

static void test_own_gear_bonuses(void) {
    reset();
    Player* player = &state.env.players[0];
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) slot_equip_item(player, slot, ITEM_NONE);
    memset(player->inventory_cells, 0, sizeof(player->inventory_cells));
    slot_equip_item(player, GEAR_SLOT_WEAPON, ITEM_RUNE_CROSSBOW);
    player->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_BLACK_DHIDE_BODY);
    observations();
    CHECK("candidate body exposes signed Magic penalty before equip",
        before[NH_PVP_INVENTORY_START + 15 + 3] < 0);
    CHECK("body equip executes through canonical inventory transfer",
        osrs_equip_from_cell(player, player->inventory_cells, 0) == GEAR_SLOT_BODY);
    pvp_write_observations(after, &state.env, 0);
    CHECK("equipped candidate leaves its inventory record",
        after[NH_PVP_INVENTORY_START] == 0 && before[NH_PVP_INVENTORY_START] != 0);
    CHECK("bow wielding retains explicit equipped Magic penalty",
        after[NH_PVP_SELF_BONUS_START + 3] < 0 &&
        after[NH_PVP_SELF_BONUS_START + 3] == get_attack_bonus(player, ATTACK_STYLE_MAGIC) / STAT_NORM_ATTACK &&
        after[37] == 1 && after[38] == 0);
    observations();
    slot_equip_item(player, GEAR_SLOT_RING, ITEM_BERSERKER_RING_I);
    slot_equip_item(player, GEAR_SLOT_AMMO, ITEM_DRAGON_ARROWS);
    pvp_write_observations(after, &state.env, 0);
    CHECK("own known ring and ammo contribute actual strength bonuses",
        after[NH_PVP_SELF_BONUS_START + 10] - before[NH_PVP_SELF_BONUS_START + 10] ==
            ITEM_DATABASE[ITEM_BERSERKER_RING_I].melee_strength / STAT_NORM_STRENGTH &&
        after[NH_PVP_SELF_BONUS_START + 11] - before[NH_PVP_SELF_BONUS_START + 11] ==
            ITEM_DATABASE[ITEM_DRAGON_ARROWS].ranged_strength / STAT_NORM_STRENGTH);
}

static void test_special_and_pending_events(void) {
    reset();
    observations();
    CHECK("reset has no observed special", before[NH_PVP_TARGET_SPECIAL_START] == 0 &&
        before[NH_PVP_TARGET_SPECIAL_START + 1] == 0);
    Player* target = &state.env.players[1];
    target->x = state.env.players[0].x + 1;
    target->y = state.env.players[0].y;
    slot_equip_item(target, GEAR_SLOT_WEAPON, ITEM_VOIDWAKER);
    perform_attack(&state.env, 1, 0, ATTACK_STYLE_MELEE, 0, 0, 1);
    pvp_write_observations(after, &state.env, 0);
    CHECK("ordinary launch does not manufacture a special cue", after[NH_PVP_TARGET_SPECIAL_START] == 0);
    perform_attack(&state.env, 1, 0, ATTACK_STYLE_MELEE, 1, 0, 1);
    pvp_write_observations(after, &state.env, 0);
    CHECK("actual special launch exposes public presence and age zero",
        after[NH_PVP_TARGET_SPECIAL_START] == 1 && after[NH_PVP_TARGET_SPECIAL_START + 1] == 0);
    state.env.tick += 2;
    perform_attack(&state.env, 1, 0, ATTACK_STYLE_MELEE, 0, 0, 1);
    pvp_write_observations(after, &state.env, 0);
    CHECK("normal attack does not refresh prior special age",
        after[NH_PVP_TARGET_SPECIAL_START] == 1 && after[NH_PVP_TARGET_SPECIAL_START + 1] == 2.0f / 8);
    reset();
    observations();
    CHECK("new episode clears special history", before[NH_PVP_TARGET_SPECIAL_START] == 0);
    CHECK("own absent queued cast is explicit", before[NH_PVP_SELF_CAST_INTENT_START] == 1 &&
        before[NH_PVP_SELF_CAST_INTENT_START + 1] == 0 && before[NH_PVP_SELF_CAST_INTENT_START + 2] == 0);
    state.env.pvp_runtime.pending_manual_cast[0] = NH_PVP_PENDING_ICE;
    state.env.pvp_runtime.pending_manual_cast[1] = NH_PVP_PENDING_BLOOD;
    pvp_write_observations(after, &state.env, 0);
    CHECK("queued Ice belongs to its own actor", after[NH_PVP_SELF_CAST_INTENT_START] == 0 &&
        after[NH_PVP_SELF_CAST_INTENT_START + 1] == 1 && after[NH_PVP_SELF_CAST_INTENT_START + 2] == 0);
    pvp_write_observations(after, &state.env, 1);
    CHECK("actor one independently observes its queued Blood", after[NH_PVP_SELF_CAST_INTENT_START] == 0 &&
        after[NH_PVP_SELF_CAST_INTENT_START + 1] == 0 && after[NH_PVP_SELF_CAST_INTENT_START + 2] == 1);
}

static void test_double_maul_mask(void) {
    reset();
    Player* player = &state.env.players[0];
    memset(player->inventory_cells, 0, sizeof(player->inventory_cells));
    slot_equip_item(player, GEAR_SLOT_SHIELD, ITEM_NONE);
    slot_equip_item(player, GEAR_SLOT_WEAPON, ITEM_GRANITE_MAUL_ORNATE);
    unsigned char mask[NH_PVP_ACTION_MASK_SIZE];
    int special = nh_pvp_action_head_offset(NH_PVP_HEAD_SPECIAL);
    int cost = osrs_spec_cost(ITEM_GRANITE_MAUL_ORNATE);
    player->special_energy = 2 * cost - 1;
    pvp_write_action_mask_bytes(mask, &state.env, 0, context.route_topology);
    CHECK("double maul cannot request two specials with insufficient energy", !mask[special + NH_PVP_SPECIAL_PREPARE_DOUBLE]);
    player->special_energy = 2 * cost;
    pvp_write_action_mask_bytes(mask, &state.env, 0, context.route_topology);
    CHECK("two funded maul specials are selectable", mask[special + NH_PVP_SPECIAL_PREPARE_DOUBLE]);
    slot_equip_item(player, GEAR_SLOT_WEAPON, ITEM_RUNE_CROSSBOW);
    pvp_write_action_mask_bytes(mask, &state.env, 0, context.route_topology);
    CHECK("ordinary ranged weapon cannot prepare maul specials", !mask[special + NH_PVP_SPECIAL_PREPARE_DOUBLE]);
    player->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_GRANITE_MAUL_ORNATE);
    pvp_write_action_mask_bytes(mask, &state.env, 0, context.route_topology);
    CHECK("same tick legal maul equip can prepare two funded specials", mask[special + NH_PVP_SPECIAL_PREPARE_DOUBLE]);
    state.env.pvp_runtime.maul[0].prepared_hits = 2;
    state.env.pvp_runtime.maul[0].preparation = OSRS_GRANITE_MAUL_DESELECTED;
    player->spec_armed = 0;
    pvp_write_action_mask_bytes(mask, &state.env, 0, context.route_topology);
    CHECK("disarm remains selectable for prepared maul without ordinary armed flag", mask[special + NH_PVP_SPECIAL_DISARM]);
}

static void test_projectiles(void) {
    const uint8_t weapons[] = {ITEM_AHRIM_STAFF, ITEM_AHRIM_STAFF, ITEM_AHRIM_STAFF,
        ITEM_AHRIM_STAFF, ITEM_AHRIM_STAFF, ITEM_RUNE_CROSSBOW, ITEM_VOLATILE_STAFF, ITEM_VOIDWAKER};
    const AttackStyle styles[] = {ATTACK_STYLE_MAGIC, ATTACK_STYLE_MAGIC, ATTACK_STYLE_MAGIC,
        ATTACK_STYLE_MAGIC, ATTACK_STYLE_MAGIC, ATTACK_STYLE_RANGED, ATTACK_STYLE_MAGIC, ATTACK_STYLE_MELEE};
    const int levels[] = {99, BLOOD_BARRAGE_LEVEL, BLOOD_BURST_LEVEL, BLOOD_BLITZ_LEVEL, BLOOD_RUSH_LEVEL, 99, 99, 99};
    const int magic_types[] = {OSRS_MAGIC_ATTACK_ANCIENT_ICE, OSRS_MAGIC_ATTACK_ANCIENT_BLOOD,
        OSRS_MAGIC_ATTACK_ANCIENT_BLOOD, OSRS_MAGIC_ATTACK_ANCIENT_BLOOD, OSRS_MAGIC_ATTACK_ANCIENT_BLOOD, 0, 0, 0};
    const OsrsPvpProjectileCue cues[] = {OSRS_PVP_PROJECTILE_ICE, OSRS_PVP_PROJECTILE_NONE, OSRS_PVP_PROJECTILE_NONE,
        OSRS_PVP_PROJECTILE_BLOOD, OSRS_PVP_PROJECTILE_BLOOD, OSRS_PVP_PROJECTILE_RANGED, OSRS_PVP_PROJECTILE_NONE, OSRS_PVP_PROJECTILE_NONE};
    const char* spell_names[] = {"Ice Barrage", "Blood Barrage", "Blood Burst", "Blood Blitz", "Blood Rush"};
    for (int probe = 0; probe < 8; probe++) {
        reset();
        Player* attacker = &state.env.players[1];
        slot_equip_item(attacker, GEAR_SLOT_WEAPON, weapons[probe]);
        attacker->special_energy = 100;
        attacker->current_magic = levels[probe];
        if (probe == 7) attacker->x = state.env.players[0].x + 1;
        perform_attack(&state.env, 1, 0, styles[probe], probe >= 6, magic_types[probe], probe == 7 ? 1 : 5);
        CHECK("actual execution queues the correct observable projectile family", attacker->num_pending_hits > 0 &&
            state.env.pvp_runtime.projectiles[1][0].cue == cues[probe] && state.env.pvp_runtime.projectiles[1][0].launch_tick == state.env.tick);
        if (probe < 5) {
            const OsrsCombatVisualRow* visual = osrs_combat_visual_find_row(
                OSRS_COMBAT_VISUAL_KIND_SPELL, OSRS_COMBAT_PROJECTILE_MISSING, spell_names[probe],
                ATTACK_STYLE_MAGIC, OSRS_COMBAT_VISUAL_STANCE_ANY, 0, 0);
            CHECK("projectile cues match the actual shared spell travel graphics",
                visual && (visual->projectile.travel_spotanim_id != OSRS_COMBAT_PROJECTILE_MISSING) ==
                    (cues[probe] != OSRS_PVP_PROJECTILE_NONE));
        }
        observations();
        for (int hit = 0; hit < attacker->num_pending_hits; hit++) {
            attacker->pending_hits[hit].damage = 70;
            attacker->pending_hits[hit].hit_success ^= 1;
            attacker->pending_hits[hit].ticks_until_hit = 100;
            attacker->pending_hits[hit].freeze_ticks = 100;
        }
        pvp_write_observations(after, &state.env, 0);
        CHECK("projectile cues exclude rolled damage hit result and landing timer", memcmp(before, after, sizeof(before)) == 0);
        if (cues[probe] != OSRS_PVP_PROJECTILE_NONE)
            CHECK("actual incoming projectile is represented in its public channel", before[NH_PVP_PROJECTILE_START + cues[probe] - 1] > 0);
        else
            CHECK("no travel graphic means no incoming projectile indicator",
                after[NH_PVP_PROJECTILE_START] == 0 && after[NH_PVP_PROJECTILE_START + 1] == 0 &&
                after[NH_PVP_PROJECTILE_START + 2] == 0);
    }
}

int main(void) {
    CHECK("NH interface versions and dense shape are explicit", NH_PVP_OBSERVATION_VERSION == 3 && NH_PVP_ACTION_SCHEMA_VERSION == 3 && NH_PVP_NUM_OBS == 1003);
    test_item_association();
    test_visible_boundary();
    test_completed_movement();
    test_own_capabilities();
    test_own_gear_bonuses();
    test_special_and_pending_events();
    test_double_maul_mask();
    test_projectiles();
    return osrs_test_summary();
}
