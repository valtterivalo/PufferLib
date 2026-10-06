#ifndef RISKFIGHT_ROLLOUT_H
#define RISKFIGHT_ROLLOUT_H
#include "riskfight_seed.h"

enum { MAX_STEPS = 32, BIG_HIT = 20, NO_BIG_HIT = -1000000, BIG_HIT_MEMORY = 8, HAZARD_BANDS = 6, HAZARD_DELAYS = 5, PROFILE_COUNT = RISKFIGHT_HUMAN_TRAIN_PROFILES + RISKFIGHT_HUMAN_HELDOUT_PROFILES };
typedef enum { MODEL_PROFILES, MODEL_HAZARD } ModelKind;
typedef enum { OURS_REPLAY, OURS_EAT_AT_THRESHOLD, OURS_IDLE } OursKind;

enum { PAUSE_LENGTHS = 64, RATES = 4 };

typedef struct {
    ModelKind kind;
    float hazard[HAZARD_BANDS][HAZARD_DELAYS];
    float rate_scale[RATES];
    float pause_chance;
    int pause_length[PAUSE_LENGTHS], pause_lengths;
} OppModel;

typedef struct {
    OursKind kind;
    int eat_below;
} OursPolicy;

typedef struct {
    int steps;
    uint8_t his_eat[MAX_STEPS];
    uint32_t his_hit_mask;
    int his_hits, his_damage, his_vw_specs, his_maul_specs, his_vengs;
    int our_damage, our_hits;
    int first_eat, our_death, his_death;
    int hp_us, hp_him;
} Outcome;

typedef struct {
    RiskfightState state;
    RiskfightContext context;
    Outcome* outcome;
    int last_big_hit_step, pause_until;
} Sim;

static void sim_init(Sim* sim) {
    memset(sim, 0, sizeof(*sim));
    riskfight_init_context((EncounterContext*)&sim->context);
    riskfight_init_state((EncounterState*)&sim->state, (EncounterContext*)&sim->context);
    sim->context.self_play = 1;
    riskfight_finalize_context((EncounterState*)&sim->state, (EncounterContext*)&sim->context);
}

static void sim_observe_hit(void* context, const OsrsPvpHitEvent* event) {
    Sim* sim = context;
    Outcome* out = sim->outcome;
    int step = event->tick - RISKFIGHT_LOAD_TICK;
    if (event->kind == OSRS_HIT_DIRECT && event->source == 1 && event->target == 0) {
        out->his_hits++;
        out->his_hit_mask |= 1u << step;
        out->his_damage += event->hitpoints_lost;
    }
    if (event->kind == OSRS_HIT_DIRECT && event->source == 0 && event->target == 1) {
        out->our_hits++;
        out->our_damage += event->hitpoints_lost;
    }
    if (event->target == 1 && event->hitpoints_lost >= BIG_HIT) sim->last_big_hit_step = step;
}

static int hazard_band(int hitpoints) {
    int r = osrs_health_bar_ratio(hitpoints, 99, 30);
    return r <= 8 ? 0 : r <= 13 ? 1 : r <= 19 ? 2 : r <= 24 ? 3 : r <= 29 ? 4 : 5;
}

static int hazard_delay(int since) {
    return since < 1 || since > 8 ? 0 : since == 1 ? 1 : since == 2 ? 2 : since == 3 ? 3 : 4;
}

static const float SERVING_SHARES[4] = {0.15f, 0.34f, 0.34f, 0.17f};

static void opponent_actions(const OppModel* model, Sim* sim, int step, uint32_t seed, int* actions) {
    const RiskfightState* s = &sim->state;
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(s, 1, obs);
    RiskfightProfile profile = RISKFIGHT_HUMAN_PROFILES[osrs_lowbias32(seed) % PROFILE_COUNT];
    if (model->kind == MODEL_HAZARD) {
        memset(profile.consume_pm, 0, sizeof(profile.consume_pm));
        profile.veng_pm = (uint16_t)fminf(1000, profile.veng_pm * model->rate_scale[0]);
        profile.vw_pm = (uint16_t)fminf(1000, profile.vw_pm * model->rate_scale[1]);
        profile.maul_pm = (uint16_t)fminf(1000, profile.maul_pm * model->rate_scale[2]);
    }
    riskfight_script(obs, &profile, s->script_seed[1], s->consume_ticks[1], actions);
    if (model->kind == MODEL_PROFILES) return;
    uint32_t pause_roll = riskfight_sample_hash(s->script_seed[1], s->env.tick, 43);
    if (step > 0 && (sim->outcome->his_hit_mask >> (step - 1) & 1) && model->pause_lengths &&
            (pause_roll % 10000) < (uint32_t)(model->pause_chance * 10000))
        sim->pause_until = step + model->pause_length[(pause_roll >> 14) % model->pause_lengths];
    if (step < sim->pause_until) actions[RF_PRIMARY] = RF_STOP;
    RiskfightObservedSelf self = riskfight_observed_self(obs);
    int since = sim->last_big_hit_step == NO_BIG_HIT ? 0 : step - sim->last_big_hit_step;
    float p = fminf(0.95f, model->hazard[hazard_band(self.hitpoints)][hazard_delay(since)] * model->rate_scale[3]);
    uint32_t roll = riskfight_sample_hash(s->script_seed[1], s->env.tick, 41);
    if (self.food_timer != 0 || (roll % 10000) >= (uint32_t)(p * 10000)) return;
    RiskfightObservedInventory inventory = riskfight_observed_inventory(obs);
    float serving = (float)(riskfight_sample_hash(s->script_seed[1], s->env.tick, 42) % 10000) / 10000.0f;
    int k = serving < SERVING_SHARES[0] ? 0 : serving < SERVING_SHARES[0] + SERVING_SHARES[1] ? 1 :
        serving < SERVING_SHARES[0] + SERVING_SHARES[1] + SERVING_SHARES[2] ? 2 : 3;
    actions[RF_FOOD] = inventory.kind_slot[OSRS_CONSUMABLE_ANGLERFISH];
    if (k == 1 || k == 3) actions[RF_COMBO] = inventory.kind_slot[OSRS_CONSUMABLE_KARAMBWAN];
    if ((k == 2 || k == 3) && self.potion_timer == 0) actions[RF_DRINK] = inventory.kind_slot[OSRS_CONSUMABLE_BREW];
    actions[RF_PRIMARY] = RF_STOP;
}

static void queue_ours(Sim* sim, const Tape* tape, const OursPolicy* policy, int tick, int step) {
    HumanInput* hi = &sim->context.policy_commands[0];
    human_input_clear_pending(hi);
    const Player* p = &sim->state.env.players[0];
    if (policy->kind == OURS_REPLAY) {
        for (int i = 0; i < tape->action_count; i++) {
            const TapeAction* a = &tape->actions[i];
            if (a->tick != tick) continue;
            HumanCommand command = {0};
            switch (a->kind) {
                case ACTION_CLICK: command = (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = a->slot}; break;
                case ACTION_ATTACK: command = (HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1}; break;
                case ACTION_SPEC: command = (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE}; break;
                case ACTION_VENGEANCE: command = (HumanCommand){.kind = HUMAN_COMMAND_VENGEANCE}; break;
            }
            human_input_queue_command(hi, command);
        }
    } else if (policy->kind == OURS_EAT_AT_THRESHOLD) {
        if (!osrs_interaction_active(&p->interaction) || step == 0)
            human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1});
        if (p->current_hitpoints < policy->eat_below && p->food_timer == 0)
            for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
                if (osrs_inventory_cell_metadata(&p->inventory_cells[slot])->consumable_kind == OSRS_CONSUMABLE_ANGLERFISH) {
                    human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = slot});
                    break;
                }
    }
}

static int consumption_visible(const RiskfightState* s, int agent) {
    const Player* p = &s->env.players[agent];
    return osrs_consumption_animation_visible(p->current_hitpoints, p->attack_style_this_tick, p->ate_food_this_tick,
        p->ate_karambwan_this_tick, s->inventory_use[agent].potion_animation_tick_plus_one == s->env.tick) && !p->hit_landed_this_tick;
}

/** Runs k steps from the loaded sim state. tick_base is the tape tick the state is the end of. */
static int big_hit_step_before(const Tape* tape, int tick_base) {
    for (int u = tick_base; u >= tick_base - BIG_HIT_MEMORY && u >= tape->lo; u--)
        for (int i = 0; i < tape->hit_count; i++)
            if (tape->hits[i].tick == u && tape->hits[i].side == SIDE_HIM && tape->hits[i].amount >= BIG_HIT) return u - tick_base - 1;
    return NO_BIG_HIT;
}

static void run_steps(Sim* sim, const Tape* tape, int tick_base, int k, const OppModel* model, const OursPolicy* policy,
    uint32_t seed, Outcome* out) {
    int last_big_hit_step = big_hit_step_before(tape, tick_base);
    RiskfightState* s = &sim->state;
    memset(out, 0, sizeof(*out));
    out->first_eat = -1; out->our_death = -1; out->his_death = -1;
    sim->outcome = out;
    sim->last_big_hit_step = last_big_hit_step;
    sim->pause_until = -1;
    s->env.pvp_runtime.hit_observer = sim_observe_hit;
    s->env.pvp_runtime.hit_observer_context = sim;
    for (int step = 0; step < k && !s->env.episode_over; step++) {
        int actions[RF_HEADS];
        opponent_actions(model, sim, step, seed + (uint32_t)step * 2654435761u, actions);
        riskfight_policy_commands(s, 1, actions, &sim->context.policy_commands[1]);
        queue_ours(sim, tape, policy, tick_base + 1 + step, step);
        int vw = s->spec_voidwaker[1], maul = s->spec_maul[1];
        riskfight_step_queues(s, &sim->context, &sim->context.policy_commands[0].commands, &sim->context.policy_commands[1].commands);
        out->steps = step + 1;
        out->his_eat[step] = (uint8_t)consumption_visible(s, 1);
        if (out->his_eat[step] && out->first_eat < 0) out->first_eat = step;
        out->his_vw_specs += s->spec_voidwaker[1] - vw;
        out->his_maul_specs += s->spec_maul[1] - maul;
        out->his_vengs += s->env.players[1].cast_veng_this_tick;
        if (s->env.players[0].current_hitpoints <= 0 && out->our_death < 0) out->our_death = step;
        if (s->env.players[1].current_hitpoints <= 0 && out->his_death < 0) out->his_death = step;
    }
    out->hp_us = s->env.players[0].current_hitpoints;
    out->hp_him = s->env.players[1].current_hitpoints;
}
#endif
