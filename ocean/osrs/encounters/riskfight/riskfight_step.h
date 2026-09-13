#ifndef OSRS_RISKFIGHT_STEP_H
#define OSRS_RISKFIGHT_STEP_H
#include "riskfight_policy.h"

static void riskfight_stop(RiskfightState* s, int agent) {
    Player* p = &s->env.players[agent];
    osrs_interaction_clear(&p->interaction);
    s->env.pvp_runtime.walk_dest_x[agent] = -1;
    s->env.pvp_runtime.walk_dest_y[agent] = -1;
    p->is_moving = 0;
}

static void riskfight_attack(RiskfightState* s, int agent, int instant_only) {
    Player* p = &s->env.players[agent];
    Player* opponent = &s->env.players[1 - agent];
    uint8_t weapon = p->equipped[GEAR_SLOT_WEAPON];
    int cost = osrs_spec_cost(weapon);
    int special = p->spec_armed && cost > 0 && p->special_energy >= cost;
    int instant = special && (weapon == ITEM_GRANITE_MAUL_ORNATE || weapon == ITEM_GRANITE_MAUL);
    if (!osrs_interaction_active(&p->interaction) ||
        p->current_hitpoints <= 0 || opponent->current_hitpoints <= 0 ||
        s->escaped[agent] || s->escaped[1 - agent] ||
        !is_in_melee_range(p, opponent)) return;
    if (instant_only && !instant) return;
    if (!instant && !can_attack_now(p)) return;
    perform_attack(&s->env, agent, 1 - agent, ATTACK_STYLE_MELEE, special, 0,
        chebyshev_distance(p->x, p->y, opponent->x, opponent->y));
    RiskfightVisibleOpponent* visible = &s->visible[1 - agent];
    float* event = visible->events[s->env.tick % RF_HISTORY_TICKS];
    event[0] += 1;
    event[1] = weapon;
    event[2] = p->attack_style_this_tick;
    if (!instant) {
        visible->last_attack_tick = s->env.tick;
        visible->last_attack_speed = ITEM_DATABASE[weapon].attack_speed;
    }
    if (special) osrs_spec_disarm(&p->spec_armed);
}

static void riskfight_execute_command(RiskfightState* s, RiskfightContext* ctx,
    int agent, const HumanCommand* command) {
    Player* p = &s->env.players[agent];
    if (p->current_hitpoints <= 0 || s->escaped[agent]) return;
    switch (command->kind) {
        case HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK:
        case HUMAN_COMMAND_EAT:
        case HUMAN_COMMAND_DRINK:
        case HUMAN_COMMAND_EQUIP_INVENTORY_ITEM:
            if (command->inventory_slot < 0 || command->inventory_slot >= OSRS_INVENTORY_SIZE) return;
            s->escaped[agent] = osrs_player_use_inventory(p, &s->inventory_use[agent],
                command->inventory_slot, s->env.tick) == OSRS_INVENTORY_USE_ESCAPED;
            pvp_refresh_visible_gear(p);
            break;
        case HUMAN_COMMAND_STOP:
            riskfight_stop(s, agent);
            break;
        case HUMAN_COMMAND_WALK:
            riskfight_stop(s, agent);
            s->env.pvp_runtime.walk_dest_x[agent] = command->world_x;
            s->env.pvp_runtime.walk_dest_y[agent] = command->world_y;
            break;
        case HUMAN_COMMAND_ATTACK_NPC:
            osrs_interaction_set(&p->interaction, 1 - agent);
            s->env.pvp_runtime.walk_dest_x[agent] = -1;
            s->env.pvp_runtime.walk_dest_y[agent] = -1;
            break;
        case HUMAN_COMMAND_SPEC_TOGGLE:
            p->spec_armed = 1;
            riskfight_attack(s, agent, 1);
            break;
        case HUMAN_COMMAND_VENGEANCE:
            osrs_player_cast_inventory_vengeance(p, &s->inventory_use[agent], s->env.tick);
            break;
        case HUMAN_COMMAND_OFFENSIVE_PRAYER:
            if (p->current_prayer > 0)
                p->offensive_prayer = (OffensivePrayer)command->offensive_prayer;
            break;
        case HUMAN_COMMAND_FIGHT_STYLE:
            p->fight_style = (FightStyle)command->fight_style;
            break;
        case HUMAN_COMMAND_ITEM_ON_ITEM:
            osrs_inventory_swap_cells(p->inventory_cells,
                command->inventory_slot, command->target_inventory_slot);
            break;
        case HUMAN_COMMAND_NONE:
        case HUMAN_COMMAND_OVERHEAD_PRAYER:
        case HUMAN_COMMAND_SPELL_TARGET:
        case HUMAN_COMMAND_SET_AUTOCAST:
        case HUMAN_COMMAND_ITEM_ON_WIDGET:
        case HUMAN_COMMAND_SPELL_ON_WIDGET:
            break;
    }
    (void)ctx;
}

static void riskfight_policy_commands(const RiskfightState* s, int agent,
    const int* actions, HumanInput* hi) {
    human_input_clear_pending(hi);
    for (int head = 0; head < RF_HEADS; head++)
        assert(actions[head] >= 0 && actions[head] < RF_ACTION_DIMS[head]);
    for (int head = RF_WEAPON; head <= RF_COMBO; head++) {
        if (actions[head]) human_input_queue_command(hi, (HumanCommand){
            .kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK,
            .inventory_slot = actions[head] - 1,
        });
    }
    if (actions[RF_ORB] || actions[RF_PRIMARY] == RF_TELEPORT) {
        OsrsConsumableKind kind = actions[RF_PRIMARY] == RF_TELEPORT ?
            OSRS_CONSUMABLE_TELEPORT : OSRS_CONSUMABLE_LOCATOR_ORB;
        for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
            if (osrs_inventory_cell_metadata(&s->env.players[agent].inventory_cells[i])->consumable_kind == kind) {
                human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK,
                    .inventory_slot = i});
                break;
            }
    }
    if (actions[RF_VENGEANCE])
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_VENGEANCE});
    human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_OFFENSIVE_PRAYER,
        .offensive_prayer = actions[RF_PRAYER]});
    human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_FIGHT_STYLE,
        .fight_style = actions[RF_STYLE]});
    int primary = actions[RF_PRIMARY];
    if (primary == RF_STOP)
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_STOP});
    else if (primary == RF_ATTACK)
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1 - agent});
    else if (primary > 0 && primary < RF_ATTACK) {
        int dx = ENCOUNTER_MOVE_TARGET_DX[primary];
        int dy = ENCOUNTER_MOVE_TARGET_DY[primary];
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_WALK,
            .world_x = s->env.players[agent].x + dx, .world_y = s->env.players[agent].y + dy});
    }
    for (int n = 0; n < actions[RF_SPECIAL]; n++)
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE});
}

static void riskfight_finish(RiskfightState* s) {
    int dead[2] = {s->env.players[0].current_hitpoints <= 0, s->env.players[1].current_hitpoints <= 0};
    s->env.episode_over = pvp_death_is_settled(&s->env) || s->escaped[0] || s->escaped[1];
    for (int i = 0; i < 2; i++) {
        s->outcome[i] = !s->env.episode_over ? RISKFIGHT_ONGOING :
            dead[0] && dead[1] ? RISKFIGHT_MUTUAL_DEATH :
            dead[i] ? RISKFIGHT_DEATH : dead[1 - i] ? RISKFIGHT_KILL :
            s->env.episode_over ? RISKFIGHT_ESCAPE : RISKFIGHT_ONGOING;
        s->rewards[i] = s->outcome[i] == RISKFIGHT_KILL ? 1 :
            s->outcome[i] == RISKFIGHT_DEATH ? -1 : 0;
    }
    s->env.winner = s->rewards[0] > 0 ? 0 : s->rewards[1] > 0 ? 1 : -1;
}

static void riskfight_step_queues(RiskfightState* s, RiskfightContext* ctx,
    const HumanCommandQueue* first, const HumanCommandQueue* second) {
    assert(!s->env.episode_over);
    const HumanCommandQueue* queues[] = {first, second};
    for (int i = 0; i < 2; i++) {
        Player* p = &s->env.players[i];
        memset(s->visible[i].events[s->env.tick % RF_HISTORY_TICKS], 0, RF_EVENT_WIDTH * sizeof(float));
        reset_tick_flags(p);
        p->hit_damage = 0;
        p->hit_landed_this_tick = 0;
        EquipmentBonuses bonuses;
        osrs_sum_equipment_bonuses(p->equipped, &bonuses);
        update_player_timers(p, bonuses.prayer);
    }
    for (int turn = 0; turn < 2; turn++) {
        int i = s->env.pid_holder ^ turn;
        const HumanCommandQueue* queue = queues[i];
        for (int n = 0; n < queue->count; n++)
            riskfight_execute_command(s, ctx, i, &queue->items[n]);
    }
    for (int turn = 0; turn < 2; turn++) {
        int i = s->env.pid_holder ^ turn;
        if (!s->escaped[0] && !s->escaped[1]) {
            int active = s->env.players[i].veng_active;
            pvp_process_incoming_hits(&s->env, i);
            if (active && !s->env.players[i].veng_active)
                s->inventory_use[i].vengeance_consumed_tick = s->env.tick;
        }
        if (s->env.players[i].current_hitpoints <= 0) continue;
        pvp_step_player_movement(&s->env, i, ctx->route_topology, &ctx->routes[i]);
        int idle_actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
        execute_attack_movement(&s->env, i, idle_actions, ctx->route_topology, &ctx->routes[i]);
        riskfight_attack(s, i, 0);
    }
    for (int i = 0; i < 2; i++) {
        Player* p = &s->env.players[i];
        if (p->food_timer > 0) p->food_timer--;
        if (p->potion_timer > 0) p->potion_timer--;
        if (p->karambwan_timer > 0) p->karambwan_timer--;
        osrs_player_inventory_tick(p, &s->inventory_use[i]);
    }
    s->env.tick++;
    pvp_tick_priority(&s->env);
    riskfight_finish(s);
    for (int i = 0; i < 2; i++) riskfight_observe_visible(s, i, 0);
}

static void riskfight_step(EncounterState* state, EncounterContext* context, const int* actions) {
    RiskfightState* s = (RiskfightState*)state;
    RiskfightContext* ctx = (RiskfightContext*)context;
    int opponent_actions[RF_HEADS];
    if (!ctx->self_play) {
        float obs[RF_OBS_SIZE];
        riskfight_write_observation(s, 1, obs);
        riskfight_script(obs, ctx->opponent, opponent_actions);
    }
    riskfight_policy_commands(s, 0, actions, &ctx->policy_commands[0]);
    riskfight_policy_commands(s, 1, ctx->self_play ? actions + RF_HEADS : opponent_actions, &ctx->policy_commands[1]);
    riskfight_step_queues(s, ctx, &ctx->policy_commands[0].commands, &ctx->policy_commands[1].commands);
}

static void riskfight_step_human(EncounterState* state, EncounterContext* context, HumanInput* hi) {
    RiskfightState* s = (RiskfightState*)state;
    RiskfightContext* ctx = (RiskfightContext*)context;
    int human = ctx->human_player;
    float obs[RF_OBS_SIZE];
    int actions[RF_HEADS];
    riskfight_write_observation(s, 1 - human, obs);
    riskfight_script(obs, ctx->opponent, actions);
    riskfight_policy_commands(s, 1 - human, actions, &ctx->policy_commands[1 - human]);
    riskfight_step_queues(s, ctx,
        human == 0 ? &hi->commands : &ctx->policy_commands[0].commands,
        human == 1 ? &hi->commands : &ctx->policy_commands[1].commands);
    human_input_clear_pending(hi);
}
#endif
