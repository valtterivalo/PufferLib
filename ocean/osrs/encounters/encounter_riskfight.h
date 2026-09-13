#ifndef ENCOUNTER_RISKFIGHT_H
#define ENCOUNTER_RISKFIGHT_H
#include "riskfight/riskfight_step.h"

static void riskfight_obs(EncounterState* state, EncounterContext* context, float* out) {
    riskfight_write_observation((RiskfightState*)state, ((RiskfightContext*)context)->human_player, out);
}
static void riskfight_mask(EncounterState* state, EncounterContext* context, float* out) {
    riskfight_write_action_mask((RiskfightState*)state, ((RiskfightContext*)context)->human_player, out);
}
static float riskfight_reward(EncounterState* state, EncounterContext* context) {
    return ((RiskfightState*)state)->rewards[((RiskfightContext*)context)->human_player];
}
static int riskfight_terminal(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((RiskfightState*)state)->env.episode_over;
}
static int riskfight_entity_count(EncounterState* state, EncounterContext* context) {
    (void)state; (void)context; return 2;
}
static void* riskfight_entity(EncounterState* state, EncounterContext* context, int index) {
    (void)context;
    return &((RiskfightState*)state)->env.players[index];
}
static void riskfight_render_entities(EncounterState* state, EncounterContext* context,
    RenderEntity* out, int capacity, int* count) {
    RiskfightState* s = (RiskfightState*)state;
    *count = capacity < 2 ? capacity : 2;
    for (int i = 0; i < *count; i++) {
        Player* p = (Player*)riskfight_entity(state, context, i);
        osrs_render_entity_from_player_entity(p, &out[i]);
        out[i].ate_food_this_tick |= s->env.tick > 0 &&
            s->inventory_use[i].potion_animation_tick_plus_one == s->env.tick;
        out[i].attack_target_entity_idx = osrs_interaction_active(&p->interaction) ? 1 - i : -1;
    }
}
static void riskfight_put_int(EncounterState* state, EncounterContext* context, const char* key, int value) {
    RiskfightContext* ctx = (RiskfightContext*)context;
    if (strcmp(key, "opponent_type") == 0) {
        assert(value >= RISKFIGHT_TRADER && value <= RISKFIGHT_FLOOR);
        ctx->opponent = (RiskfightOpponent)value;
    } else if (strcmp(key, "self_play") == 0) {
        assert(value == 0 || value == 1); ctx->self_play = value;
    } else if (strcmp(key, "human_player") == 0) {
        assert(value == 0 || value == 1); ctx->human_player = value;
    } else if (strcmp(key, "seed") == 0) {
        assert(value != 0); ((RiskfightState*)state)->env.rng_state = (uint32_t)value;
    } else encounter_abort_unknown_config("riskfight", "int", key);
}
static void riskfight_put_float(EncounterState* state, EncounterContext* context, const char* key, float value) {
    (void)state;
    RiskfightContext* ctx = (RiskfightContext*)context;
    assert(value >= 0);
    if (strcmp(key, "damage_reward_coeff") == 0) ctx->damage_reward_coeff = value;
    else if (strcmp(key, "chance_reward_coeff") == 0) ctx->chance_reward_coeff = value;
    else if (strcmp(key, "teleport_penalty") == 0) ctx->teleport_penalty = value;
    else encounter_abort_unknown_config("riskfight", "float", key);
}
static void riskfight_put_ptr(EncounterState* state, EncounterContext* context, const char* key, void* value) {
    (void)state;
    if (strcmp(key, "collision_map") == 0) ((RiskfightContext*)context)->collision_map = (const CollisionMap*)value;
    else encounter_abort_unknown_config("riskfight", "ptr", key);
}
static void* riskfight_log(EncounterState* state, EncounterContext* context) {
    (void)context; return &((RiskfightState*)state)->env.log;
}
static int riskfight_tick(EncounterState* state, EncounterContext* context) {
    (void)context; return ((RiskfightState*)state)->env.tick;
}
static int riskfight_winner(EncounterState* state, EncounterContext* context) {
    (void)context; return ((RiskfightState*)state)->env.winner;
}
static void riskfight_render_post_tick(EncounterState* state, EncounterContext* context, EncounterOverlay* overlay) {
    RiskfightState* s = (RiskfightState*)state;
    int human = ((RiskfightContext*)context)->human_player;
    overlay->status_text_active = s->last_unequip_result[human] == OSRS_UNEQUIP_FULL;
    snprintf(overlay->status_text, sizeof(overlay->status_text), "%s",
        overlay->status_text_active ? "Not enough space in your inventory." : "");
}
static const EncounterDef ENCOUNTER_RISKFIGHT = {
    .name = "riskfight", .display_name = "Riskfight",
    .obs_size = RF_OBS_SIZE, .num_action_heads = RF_HEADS,
    .action_head_dims = RF_ACTION_DIMS, .mask_size = RF_MASK_SIZE,
    .state_size = sizeof(RiskfightState), .context_size = sizeof(RiskfightContext),
    .init_context = riskfight_init_context, .destroy_context = riskfight_destroy_context,
    .init_state = riskfight_init_state, .finalize_context = riskfight_finalize_context,
    .create = riskfight_create, .destroy = riskfight_destroy, .reset = riskfight_reset,
    .step = riskfight_step, .step_human_commands = riskfight_step_human,
    .human_equipment_mode = HUMAN_EQUIPMENT_UNEQUIP_COMMANDS,
    .write_obs = riskfight_obs, .write_mask = riskfight_mask,
    .get_reward = riskfight_reward, .is_terminal = riskfight_terminal,
    .get_entity_count = riskfight_entity_count, .get_entity = riskfight_entity,
    .fill_render_entities = riskfight_render_entities,
    .put_int = riskfight_put_int, .put_float = riskfight_put_float, .put_ptr = riskfight_put_ptr,
    .arena_base_x = FIGHT_AREA_BASE_X, .arena_base_y = FIGHT_AREA_BASE_Y,
    .arena_width = FIGHT_AREA_WIDTH, .arena_height = FIGHT_AREA_HEIGHT,
    .head_move = RF_PRIMARY, .head_prayer = -1, .head_target = RF_PRIMARY,
    .render_post_tick = riskfight_render_post_tick,
    .get_log = riskfight_log, .get_tick = riskfight_tick, .get_winner = riskfight_winner,
};
__attribute__((constructor)) static void riskfight_register(void) {
    encounter_register(&ENCOUNTER_RISKFIGHT);
}
#endif
