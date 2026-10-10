#define main existing_actions_main
#include "test_nh_pvp_actions.c"
#undef main

int main(void) {
    if (existing_actions_main()) return 1;
    for (int actor = 0; actor < NUM_AGENTS; actor++) {
        for (int pid = 0; pid < NUM_AGENTS; pid++) {
            reset_case(actor, pid, ITEM_WHIP);
            Player* attacker = &state.env.players[actor];
            Player* defender = &state.env.players[1 - actor];
            OsrsPvpLaunchHistory* history = &state.env.pvp_runtime.launch_history[actor];
            attacker->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_RUNE_CROSSBOW);
            defender->prayer = PRAYER_PROTECT_RANGED;
            int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
            actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
            actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_WEAPON)] =
                nh_pvp_equipment_action_for_item(GEAR_SLOT_WEAPON, ITEM_RUNE_CROSSBOW);
            step_case(actor, actions);
            CHECK("real launch records actual ranged style and launch protection",
                history->count == 1 && history->records[0].style == ATTACK_STYLE_RANGED &&
                history->records[0].target_prayer == PRAYER_PROTECT_RANGED);
            CHECK("launch context records weapon before submitted switch",
                history->records[0].observed_weapon == ATTACK_STYLE_MELEE);
            CHECK("idle counterpart has no fabricated launch",
                state.env.pvp_runtime.launch_history[1 - actor].count == 0);
            step_case(actor, actions);
            CHECK("cooldown and submitted attack do not manufacture event", history->count == 1);

            reset_case(actor, pid, ITEM_WHIP);
            defender = &state.env.players[1 - actor];
            defender->prayer = PRAYER_PROTECT_RANGED;
            int joint[NUM_AGENTS][NH_PVP_NUM_ACTION_HEADS] = {{0}};
            joint[actor][NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
            joint[1 - actor][NH_PVP_HEAD_OVERHEAD] = ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC;
            nh_pvp_set_external_actions(&state, joint[1]);
            nh_pvp_step((EncounterState*)&state, (EncounterContext*)&context, joint[0]);
            history = &state.env.pvp_runtime.launch_history[actor];
            CHECK("reaction context snapshots displayed prayer before either submitted action",
                history->count == 1 && history->records[0].observed_target_prayer == PRAYER_PROTECT_RANGED &&
                history->records[0].target_prayer == PRAYER_PROTECT_MAGIC);

            reset_case(actor, pid, ITEM_VOIDWAKER);
            attacker = &state.env.players[actor];
            defender = &state.env.players[1 - actor];
            history = &state.env.pvp_runtime.launch_history[actor];
            defender->prayer = PRAYER_PROTECT_MAGIC;
            memset(actions, 0, sizeof(actions));
            actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
            actions[NH_PVP_HEAD_SPECIAL] = 1;
            step_case(actor, actions);
            CHECK("Voidwaker launch records magic damage with melee weapon cue",
                history->count == 1 && history->records[0].style == ATTACK_STYLE_MAGIC &&
                history->records[0].observed_weapon == ATTACK_STYLE_MELEE &&
                history->records[0].target_prayer == PRAYER_PROTECT_MAGIC);

            reset_case(actor, pid, ITEM_GRANITE_MAUL);
            history = &state.env.pvp_runtime.launch_history[actor];
            state.env.pvp_runtime.decision_weapon[actor] = ATTACK_STYLE_MELEE;
            perform_attack(&state.env, actor, 1 - actor, ATTACK_STYLE_MELEE, 1, 0, 1);
            perform_attack(&state.env, actor, 1 - actor, ATTACK_STYLE_MELEE, 1, 0, 1);
            CHECK("two actual same-tick maul launches occupy two records",
                history->count == 2 && history->next == 2 &&
                history->records[0].style == ATTACK_STYLE_MELEE &&
                history->records[1].style == ATTACK_STYLE_MELEE);

            reset_case(actor, pid, ITEM_WHIP);
            history = &state.env.pvp_runtime.launch_history[actor];
            for (int launch = 0; launch < HISTORY_SIZE + 2; launch++) {
                attacker = &state.env.players[actor];
                defender = &state.env.players[1 - actor];
                attacker->attack_timer = 0;
                attacker->has_attack_timer = 0;
                defender->current_hitpoints = 999;
                defender->prayer = launch % 2 ? PRAYER_PROTECT_MAGIC : PRAYER_PROTECT_MELEE;
                memset(actions, 0, sizeof(actions));
                actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
                step_case(actor, actions);
                CHECK("each successful launch advances bounded ring",
                    history->count == min_int(launch + 1, HISTORY_SIZE) &&
                    history->next == (launch + 1) % HISTORY_SIZE);
            }
            CHECK("wrapped ring retains last-five launch prayers",
                history->records[0].target_prayer == PRAYER_PROTECT_MAGIC &&
                history->records[1].target_prayer == PRAYER_PROTECT_MELEE &&
                history->records[2].target_prayer == PRAYER_PROTECT_MELEE);
            nh_pvp_reset((EncounterState*)&state, (EncounterContext*)&context, 73);
            CHECK("episode reset clears both launch histories",
                state.env.pvp_runtime.launch_history[0].count == 0 &&
                state.env.pvp_runtime.launch_history[0].next == 0 &&
                state.env.pvp_runtime.launch_history[1].count == 0 &&
                state.env.pvp_runtime.launch_history[1].next == 0);
        }
    }
    return osrs_test_summary();
}
