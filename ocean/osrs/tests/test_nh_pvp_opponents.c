#include "../../osrs_pvp/osrs_pvp.h"
#include "osrs_test_check.h"

static Env env;
static float observations[OBS_SIZE], actions[NUM_ATNS], reward, terminal;
static unsigned char mask[NH_PVP_ACTION_MASK_SIZE];

static void reset(OpponentType bot) {
    memset(&env, 0, sizeof(env));
    memset(actions, 0, sizeof(actions));
    DictItem items[] = {
        {.key = "opponent_type", .value = bot},
        {.key = "gear_tier", .value = 3},
        {.key = "is_lms", .value = 1},
        {.key = "use_c_opponent", .value = 1},
        {.key = "self_play", .value = 0},
        {.key = "auto_reset", .value = 0},
        {.key = "shaping_enabled", .value = 0},
        {.key = "shaping_scale", .value = 0},
    };
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    env.rng = 123;
    puf_init(&env, &kwargs);
    env.agents[0] = (Agent){.observations = observations, .actions = actions,
        .rewards = &reward, .terminals = &terminal, .action_mask = mask};
    puf_reset(&env);
    OsrsEnv* pvp = &env.state.env;
    OpponentState* opponent = &pvp->pvp_runtime.opponent;
    opponent->read_chance = 1;
    opponent->prayer_accuracy = 1;
    opponent->mistake_rate = 0;
    opponent->action_delay_chance = 0;
    opponent->offensive_prayer_rate = 1;
    opponent->offensive_prayer_miss = 0;
    for (int i = 0; i < 2; i++) {
        pvp->players[i].x = FIGHT_AREA_BASE_X + 10 + i;
        pvp->players[i].y = FIGHT_AREA_BASE_Y + 10;
        pvp->players[i].special_energy = 0;
    }
}

int main(void) {
    for (int probe = 0; probe < 12; probe++) {
        reset(OPP_NIGHTMARE_NH);
        OsrsEnv* pvp = &env.state.env;
        Player* player = &pvp->players[0];
        pvp_apply_equipment_plan(player, PVP_EQUIPMENT_MAGIC);
        actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
        AttackStyle expected = ATTACK_STYLE_MELEE;
        if (probe < 2) {
            int equipment[NUM_ATNS] = {0};
            pvp_emit_equipment_plan_actions(equipment, player, PVP_EQUIPMENT_RANGED);
            for (int head = 0; head < NUM_ATNS; head++) actions[head] = equipment[head];
            actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
            actions[OSRS_HEAD_SPELL] = probe == 0 ? OSRS_SPELL_ICE_BARRAGE : OSRS_SPELL_BLOOD_BARRAGE;
            expected = ATTACK_STYLE_MAGIC;
        } else if (probe == 3) {
            actions[OSRS_HEAD_PRIMARY] = 0;
            actions[OSRS_HEAD_SPELL] = OSRS_SPELL_ICE_BARRAGE;
            expected = ATTACK_STYLE_NONE;
        } else if (probe >= 4 && probe <= 6) {
            player->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_VOIDWAKER);
            osrs_equip_from_cell(player, player->inventory_cells, 0);
            pvp_refresh_visible_gear(player);
            player->special_energy = 100;
            player->spec_armed = probe != 4;
            actions[OSRS_HEAD_SPECIAL] = probe == 4 ? 1 : probe == 5 ? 0 : 2;
            expected = probe == 6 ? ATTACK_STYLE_MELEE : ATTACK_STYLE_MAGIC;
        } else if (probe == 7) {
            actions[OSRS_HEAD_SPELL] = OSRS_SPELL_ICE_BARRAGE;
            player->current_magic = 0;
            expected = ATTACK_STYLE_NONE;
        } else if (probe >= 8) {
            player->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_VOLATILE_STAFF);
            osrs_equip_from_cell(player, player->inventory_cells, 0);
            pvp_refresh_visible_gear(player);
            player->special_energy = osrs_spec_cost(ITEM_VOLATILE_STAFF) - (probe == 11);
            player->spec_armed = probe == 9 || probe == 10;
            actions[OSRS_HEAD_SPECIAL] = probe == 9 ? 0 : probe == 10 ? 2 : 1;
            expected = probe < 10 ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_MELEE;
        }
        puf_step(&env);
        CHECK("submitted read agrees with actual execution", pvp->pvp_runtime.opponent.read_agent_style == expected &&
            player->attack_style_this_tick == expected);
        if (expected != ATTACK_STYLE_NONE)
            CHECK("read protects the actual executed style", player->attack_was_on_prayer);
        else
            CHECK("successful reads preserve absent attack style", pvp->pvp_runtime.opponent.has_read_this_tick);
        puf_close(&env);
    }

    for (int moving = 0; moving < 2; moving++) {
        reset(OPP_NIGHTMARE_NH);
        actions[OSRS_HEAD_PRIMARY] = moving ? 1 : 0;
        actions[OSRS_HEAD_OVERHEAD] = ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED;
        puf_step(&env);
        OpponentState* opponent = &env.state.env.pvp_runtime.opponent;
        CHECK("nonattack reads retain movement and overhead intentions",
            opponent->has_read_this_tick && opponent->read_agent_style == ATTACK_STYLE_NONE &&
            opponent->read_agent_moving == moving && opponent->read_agent_prayer == PRAYER_PROTECT_RANGED);
        puf_close(&env);
    }

    for (int magic = 0; magic < 2; magic++) {
        reset(OPP_NIGHTMARE_NH);
        OsrsEnv* pvp = &env.state.env;
        Player* opponent = &pvp->players[1];
        opponent->inventory_cells[0] = osrs_inventory_cell_from_item(
            magic ? ITEM_VOLATILE_STAFF : ITEM_VOIDWAKER);
        osrs_equip_from_cell(opponent, opponent->inventory_cells, 0);
        pvp_refresh_visible_gear(opponent);
        for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++) {
            const OsrsItemContentMetadata* item = osrs_inventory_cell_metadata(&opponent->inventory_cells[cell]);
            if (item->gear_slot == GEAR_SLOT_WEAPON)
                opponent->inventory_cells[cell] = osrs_inventory_cell_empty();
        }
        opponent->special_energy = 100;
        pvp->players[0].current_hitpoints = 40;
        int submitted[NUM_ATNS] = {0};
        ENCOUNTER_NH_PVP.step(NH_PVP_ENV_STATE(&env), NH_PVP_ENV_CONTEXT(&env), submitted);
        CHECK("magic damage uses its weapon's offensive boost",
            opponent->used_special_this_tick && opponent->attack_style_this_tick == ATTACK_STYLE_MAGIC &&
            opponent->offensive_prayer == (magic ? OFFENSIVE_PRAYER_AUGURY : OFFENSIVE_PRAYER_PIETY));
        puf_close(&env);
    }

    for (int bot = OPP_PANICKING; bot <= OPP_RANGE_KITER; bot++) {
        reset((OpponentType)bot);
        Player* opponent = &env.state.env.players[1];
        do puf_step(&env); while (!opponent->just_attacked && !terminal);
        CHECK("scripted bot reaches an attack", opponent->just_attacked);
        OffensivePrayer expected = opponent->attack_style_this_tick == ATTACK_STYLE_MAGIC
            ? OFFENSIVE_PRAYER_AUGURY : opponent->attack_style_this_tick == ATTACK_STYLE_RANGED
                ? OFFENSIVE_PRAYER_RIGOUR : OFFENSIVE_PRAYER_PIETY;
        CHECK("scripted bot uses an offensive prayer matching its attack", opponent->offensive_prayer == expected);
        puf_close(&env);
    }

    for (int miss = 0; miss < 2; miss++) {
        reset(OPP_NIGHTMARE_NH);
        OsrsEnv* pvp = &env.state.env;
        pvp->pvp_runtime.opponent.offensive_prayer_rate = miss;
        pvp->pvp_runtime.opponent.offensive_prayer_miss = miss;
        pvp->players[1].offensive_prayer = OFFENSIVE_PRAYER_PIETY;
        puf_step(&env);
        CHECK("prayer use and missed switching have separate effects",
            pvp->players[1].offensive_prayer == (miss ? OFFENSIVE_PRAYER_PIETY : OFFENSIVE_PRAYER_NONE));
        CHECK("prayer misses do not suppress gear switches",
            get_slot_weapon_attack_style(&pvp->players[1]) == ATTACK_STYLE_MAGIC);
        puf_close(&env);
    }
    return osrs_test_summary();
}
