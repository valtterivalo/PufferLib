/**
 * @file encounter_nh_pvp.h
 * @brief NH (No Honor) PvP encounter — the original 1v1 LMS-style fight.
 *
 * Wraps the existing osrs_pvp_api.h (pvp_init/pvp_reset/pvp_step) as an
 * EncounterDef implementation. This is the first encounter and serves as
 * the reference for how to add new encounters.
 *
 * Entity layout: 2 players (agent + opponent).
 * Obs: SLOT_NUM_OBSERVATIONS features. Actions: NUM_ACTION_HEADS heads.
 * Mask: ACTION_MASK_SIZE logits.
 */

#ifndef ENCOUNTER_NH_PVP_H
#define ENCOUNTER_NH_PVP_H

#include "../osrs_encounter.h"
#include "../osrs_encounter_visual_events.h"
#include "../osrs_env.h"

/* obs/action dimensions from osrs_types.h. order must match HEAD_* indices. */
static const int NH_PVP_ACTION_DIMS[] = {
    LOADOUT_DIM, COMBAT_DIM, OVERHEAD_DIM,
    FOOD_DIM, POTION_DIM, KARAMBWAN_DIM, VENG_DIM, OFFENSIVE_DIM, MOVE_DIM
};


typedef struct {
    OsrsEnv env;
} NhPvpState;

typedef struct {
    int unused;
} NhPvpContext;

static void nh_pvp_translate_human_input(HumanInput* hi, int* actions, Player* agent, Player* target) {
    for (int h = 0; h < NUM_ACTION_HEADS; h++) actions[h] = 0;
    actions[HEAD_LOADOUT] = LOADOUT_KEEP;

    /* HEAD_COMBAT only carries attack intent now; movement flows via walk_dest
       in nh_pvp_step_human_commands. encounter_translate_attack_or_move would
       still emit MOVE_* values if the click is not on the opponent, so we
       only consult it for attacks. */
    if (hi->pending_attack) {
        if (hi->pending_spell == ATTACK_ICE) actions[HEAD_COMBAT] = ATTACK_ICE;
        else if (hi->pending_spell == ATTACK_BLOOD) actions[HEAD_COMBAT] = ATTACK_BLOOD;
        else actions[HEAD_COMBAT] = ATTACK_ATK;
    }
    encounter_translate_prayer(hi, actions, HEAD_OVERHEAD);
    encounter_translate_offensive_prayer(hi, actions, HEAD_OFFENSIVE);

    if (hi->pending_food) actions[HEAD_FOOD] = FOOD_EAT;
    if (hi->pending_potion > 0) actions[HEAD_POTION] = hi->pending_potion;
    if (hi->pending_karambwan) actions[HEAD_KARAMBWAN] = KARAM_EAT;
    if (hi->pending_veng) actions[HEAD_VENG] = VENG_CAST;
    if (hi->pending_spec) {
        AttackStyle style = (AttackStyle)get_item_attack_style(agent->equipped[GEAR_SLOT_WEAPON]);
        if (style == ATTACK_STYLE_MELEE) actions[HEAD_LOADOUT] = LOADOUT_SPEC_MELEE;
        else if (style == ATTACK_STYLE_RANGED) actions[HEAD_LOADOUT] = LOADOUT_SPEC_RANGE;
        else if (style == ATTACK_STYLE_MAGIC) actions[HEAD_LOADOUT] = LOADOUT_SPEC_MAGIC;
    }
    (void)target;
}


static EncounterState* nh_pvp_create(void) {
    NhPvpState* s = (NhPvpState*)calloc(1, sizeof(NhPvpState));
    pvp_init(&s->env);
    /* pvp_init sets internal buf pointers for game logic (observations, actions, etc.).
       also wire the ocean pointers to internal buffers so pvp_step can write obs/rewards
       without needing the PufferLib binding. */
    s->env.ocean_io.agent_obs = s->env._obs_buf;
    s->env.ocean_io.agent_actions = s->env._acts_buf;
    s->env.ocean_io.agent_rewards = s->env._rews_buf;
    s->env.ocean_io.agent_terminals = s->env._terms_buf;
    return (EncounterState*)s;
}

static void nh_pvp_destroy(EncounterState* state) {
    NhPvpState* s = (NhPvpState*)state;
    pvp_close(&s->env);
    free(s);
}

static void nh_pvp_init_context(EncounterContext* context) {
    (void)context;
}

static void nh_pvp_destroy_context(EncounterContext* context) {
    (void)context;
}

static void nh_pvp_reset(EncounterState* state, EncounterContext* context, uint32_t seed) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    if (seed != 0) {
        pvp_seed(&s->env, seed);
    }
    pvp_reset(&s->env);
}

static void nh_pvp_step(EncounterState* state, EncounterContext* context, const int* actions) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    /* pvp_step reads agent 0 actions from ocean_io.agent_actions. */
    memcpy(s->env.ocean_io.agent_actions, actions, NUM_ACTION_HEADS * sizeof(int));
    pvp_step(&s->env);
}

static void nh_pvp_step_human_commands(
    EncounterState* state,
    EncounterContext* context,
    HumanInput* hi
) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    int saved_use_c_opponent_p0 = s->env.pvp_runtime.use_c_opponent_p0;
    s->env.pvp_runtime.use_c_opponent_p0 = 0;
    /* click-anywhere: write raw click coords to agent 0's walk_dest before the
       step. BFS pathfinder walks toward it across as many ticks as needed and
       clears it on arrival. attack click still flows through HEAD_COMBAT. */
    if (hi->pending_move_x >= 0 && hi->pending_move_y >= 0) {
        s->env.pvp_runtime.walk_dest_x[0] = hi->pending_move_x;
        s->env.pvp_runtime.walk_dest_y[0] = hi->pending_move_y;
    }
    nh_pvp_translate_human_input(
        hi,
        s->env.ocean_io.agent_actions,
        &s->env.players[0],
        &s->env.players[1]);
    pvp_step(&s->env);
    s->env.pvp_runtime.use_c_opponent_p0 = saved_use_c_opponent_p0;
    /* only clear non-move pending fields; pending_move stays until the player
       arrives at the clicked tile so subsequent ticks keep extending walk_dest. */
    if (s->env.pvp_runtime.walk_dest_x[0] < 0 || s->env.pvp_runtime.walk_dest_y[0] < 0) {
        human_input_clear_move(hi);
    }
    hi->pending_attack = 0;
    hi->pending_spell = 0;
    hi->pending_prayer = 0;
    hi->pending_offensive_prayer = 0;
    hi->pending_food = 0;
    hi->pending_potion = 0;
    hi->pending_karambwan = 0;
    hi->pending_veng = 0;
    hi->pending_spec = 0;
}


static void nh_pvp_write_obs(EncounterState* state, EncounterContext* context, float* obs_out) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    /* observations are already computed by pvp_step into _obs_buf.
       copy agent 0's observations (SLOT_NUM_OBSERVATIONS floats). */
    memcpy(obs_out, s->env._obs_buf, SLOT_NUM_OBSERVATIONS * sizeof(float));
}

static void nh_pvp_write_mask(
    EncounterState* state,
    EncounterContext* context,
    float* mask_out
) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    /* masks are in _masks_buf, ACTION_MASK_SIZE bytes for agent 0.
       convert to float for the encounter interface. */
    for (int i = 0; i < ACTION_MASK_SIZE; i++) {
        mask_out[i] = (float)s->env._masks_buf[i];
    }
}

static float nh_pvp_get_reward(EncounterState* state, EncounterContext* context) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    return s->env._rews_buf[0];
}

static int nh_pvp_is_terminal(EncounterState* state, EncounterContext* context) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    return s->env.episode_over;
}


static int nh_pvp_get_entity_count(EncounterState* state, EncounterContext* context) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    return pvp_terminal_presentation_entity_count(&s->env);
}

static void* nh_pvp_get_entity(EncounterState* state, EncounterContext* context, int index) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    int player_idx = pvp_terminal_presentation_player_index(&s->env, index);
    if (player_idx < 0 || player_idx >= NUM_AGENTS) return NULL;
    if (pvp_terminal_presentation_active(&s->env))
        return &s->env.pvp_runtime.terminal_presentation.players[player_idx];
    return &s->env.players[player_idx];
}

static const char* nh_pvp_render_entity_name(OsrsEnv* env, int player_idx) {
    if (player_idx == 0) {
        if (env->pvp_runtime.use_c_opponent_p0)
            return osrs_pvp_opponent_state_display_name(&env->pvp_runtime.opponent_p0);
        return "Agent";
    }

    if (env->pvp_runtime.use_external_opponent_actions ||
            env->pvp_runtime.opponent.type == OPP_SELFPLAY) {
        return "Opponent Agent";
    }

    if (env->pvp_runtime.opponent.type != OPP_NONE ||
            env->pvp_runtime.opponent.active_sub_policy != OPP_NONE) {
        return osrs_pvp_opponent_state_display_name(&env->pvp_runtime.opponent);
    }

    return "Opponent";
}

static int nh_pvp_render_attack_target_idx(Player* player, int entity_count) {
    if (!osrs_interaction_active(&player->interaction)) return -1;
    int target = player->interaction.target_slot;
    if (target < 0 || target >= entity_count) return -1;
    return target;
}

static void nh_pvp_fill_render_entities(
    EncounterState* state,
    EncounterContext* context,
    RenderEntity* out,
    int max_entities,
    int* count
) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    int wanted = pvp_terminal_presentation_entity_count(&s->env);
    int n = wanted < max_entities ? wanted : max_entities;
    for (int i = 0; i < n; i++) {
        int player_idx = pvp_terminal_presentation_player_index(&s->env, i);
        Player* player = pvp_terminal_presentation_active(&s->env)
            ? &s->env.pvp_runtime.terminal_presentation.players[player_idx]
            : &s->env.players[player_idx];
        osrs_render_entity_from_player_entity(player, &out[i]);
        out[i].attack_target_entity_idx =
            s->env.pvp_runtime.terminal_presentation.phase ==
                PVP_TERMINAL_PRESENTATION_WINNER
            ? -1
            : nh_pvp_render_attack_target_idx(player, n);
        const char* name = player_idx == 1 &&
            pvp_terminal_presentation_active(&s->env)
            ? s->env.pvp_runtime.terminal_presentation.opponent_name
            : nh_pvp_render_entity_name(&s->env, player_idx);
        snprintf(out[i].display_name, sizeof(out[i].display_name), "%s", name);
    }
    *count = n;
}

static void nh_pvp_render_post_tick(
    EncounterState* state,
    EncounterContext* context,
    EncounterOverlay* overlay
) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    if (!pvp_terminal_presentation_active(&s->env)) return;

    PvpTerminalPresentation* p = &s->env.pvp_runtime.terminal_presentation;
    const char* winner_name = p->winner == 0 ? "Player 0" : p->opponent_name;
    snprintf(overlay->status_text, sizeof(overlay->status_text), "%s won", winner_name);
    overlay->status_text_active = 1;
}


static void nh_pvp_put_int(
    EncounterState* state,
    EncounterContext* context,
    const char* key,
    int value
) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    if (strcmp(key, "opponent_type") == 0) {
        s->env.pvp_runtime.opponent.type = (OpponentType)value;
    } else if (strcmp(key, "opponent_p0_type") == 0) {
        s->env.pvp_runtime.opponent_p0.type = (OpponentType)value;
    } else if (strcmp(key, "is_lms") == 0) {
        s->env.is_lms = value;
    } else if (strcmp(key, "use_c_opponent") == 0) {
        s->env.pvp_runtime.use_c_opponent = value;
    } else if (strcmp(key, "use_c_opponent_p0") == 0) {
        s->env.pvp_runtime.use_c_opponent_p0 = value;
    } else if (strcmp(key, "auto_reset") == 0) {
        s->env.auto_reset = value;
    } else if (strcmp(key, "fixed_spawns") == 0) {
        s->env.pvp_runtime.start_mode = pvp_start_mode_from_fixed_spawns(value);
    } else if (strcmp(key, "gear_tier") == 0) {
        if (value < 0) {
            s->env.pvp_runtime.gear_tier_weights[0] = 0.60f;
            s->env.pvp_runtime.gear_tier_weights[1] = 0.25f;
            s->env.pvp_runtime.gear_tier_weights[2] = 0.10f;
            s->env.pvp_runtime.gear_tier_weights[3] = 0.05f;
        } else {
            if (value > 3) {
                fprintf(stderr, "nh_pvp: invalid gear_tier %d\n", value);
                abort();
            }
            for (int t = 0; t < 4; t++)
                s->env.pvp_runtime.gear_tier_weights[t] = 0.0f;
            s->env.pvp_runtime.gear_tier_weights[value] = 1.0f;
        }
    } else if (strcmp(key, "seed") == 0) {
        pvp_seed(&s->env, (uint32_t)value);
    }
}

static void nh_pvp_put_float(
    EncounterState* state,
    EncounterContext* context,
    const char* key,
    float value
) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    if (strcmp(key, "shaping_scale") == 0) {
        s->env.shaping.shaping_scale = value;
    }
}

static void nh_pvp_put_ptr(
    EncounterState* state,
    EncounterContext* context,
    const char* key,
    void* value
) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    if (strcmp(key, "collision_map") == 0) {
        s->env.collision_map = value;
    }
}


static void* nh_pvp_get_log(EncounterState* state, EncounterContext* context) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    return &s->env.log;
}

static int nh_pvp_get_tick(EncounterState* state, EncounterContext* context) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    return s->env.tick;
}

static int nh_pvp_get_winner(EncounterState* state, EncounterContext* context) {
    (void)context;
    NhPvpState* s = (NhPvpState*)state;
    return s->env.winner;
}


static const EncounterDef ENCOUNTER_NH_PVP = {
    .name = "nh_pvp",
    .obs_size = SLOT_NUM_OBSERVATIONS,
    .num_action_heads = NUM_ACTION_HEADS,
    .action_head_dims = NH_PVP_ACTION_DIMS,
    .mask_size = ACTION_MASK_SIZE,
    .state_size = sizeof(NhPvpState),
    .context_size = sizeof(NhPvpContext),
    .init_context = nh_pvp_init_context,
    .destroy_context = nh_pvp_destroy_context,

    .create = nh_pvp_create,
    .destroy = nh_pvp_destroy,
    .reset = nh_pvp_reset,
    .step = nh_pvp_step,
    .step_human_commands = nh_pvp_step_human_commands,

    .write_obs = nh_pvp_write_obs,
    .write_mask = nh_pvp_write_mask,
    .get_reward = nh_pvp_get_reward,
    .is_terminal = nh_pvp_is_terminal,

    .get_entity_count = nh_pvp_get_entity_count,
    .get_entity = nh_pvp_get_entity,
    .fill_render_entities = nh_pvp_fill_render_entities,

    .put_int = nh_pvp_put_int,
    .put_float = nh_pvp_put_float,
    .put_ptr = nh_pvp_put_ptr,

    .translate_human_input = NULL,
    .is_human_targetable_npc_slot = NULL,
    .head_move = -1,
    .head_prayer = -1,
    .head_target = -1,

    .render_post_tick = nh_pvp_render_post_tick,
    .get_log = nh_pvp_get_log,
    .get_tick = nh_pvp_get_tick,
    .get_winner = nh_pvp_get_winner,
};

/* auto-register on include */
__attribute__((constructor))
static void nh_pvp_register(void) {
    encounter_register(&ENCOUNTER_NH_PVP);
}

#endif /* ENCOUNTER_NH_PVP_H */
