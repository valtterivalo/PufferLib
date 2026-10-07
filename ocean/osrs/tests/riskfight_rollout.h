#ifndef RISKFIGHT_ROLLOUT_H
#define RISKFIGHT_ROLLOUT_H
#include "riskfight_opp.h"
#include "riskfight_ours_state.h"
#include "riskfight_observe.h"

enum { MAX_STEPS = 32, PROFILE_COUNT = RISKFIGHT_HUMAN_TRAIN_PROFILES + RISKFIGHT_HUMAN_HELDOUT_PROFILES };
typedef enum { MODEL_PROFILES, MODEL_FIT } ModelKind;
typedef enum { OURS_REPLAY, OURS_EAT_AT_THRESHOLD, OURS_EAT_AT_RISK, OURS_RULES, OURS_IDLE } OursKind;

typedef struct {
    ModelKind kind;
    const OppFit* fit;
} OppModel;

typedef enum { TIMING_MEASURED, TIMING_IDEAL } OursTiming;
typedef enum { STAKES_COMPUTED, STAKES_EQUAL } OursStakes;

/* Beliefs about him that an ablation reads from the sim instead of the plugin's inference. */
enum { TRUTH_NONE = 0, TRUTH_PID = 1, TRUTH_ENERGY = 2, TRUTH_VENGEANCE = 4, TRUTH_HP = 8, TRUTH_TIMER = 16, TRUTH_ALL = 31 };

typedef struct {
    OursKind kind;
    int eat_below;
    OursTiming timing;
    OursStakes stakes;
    int truth;
} OursPolicy;

typedef struct {
    int steps;
    uint8_t his_eat[MAX_STEPS];
    uint32_t his_hit_mask;
    int his_hits, his_damage, his_vw_specs, his_maul_specs, his_vengs;
    int our_damage, our_hits;
    int first_eat, our_death, his_death, our_meals;
    int hp_us, hp_him, bar_him;
} Outcome;

typedef struct {
    RiskfightState state;
    RiskfightContext context;
    Outcome* outcome;
    int tick_base, step_damage, step_max_hit;
    Hist hist;
    Ours ours;
    Observer obs;
    StepEvents previous;
    double spec_predicted, spec_realized, spec_counted;
} Sim;

#include "riskfight_ours.h"

static void sim_init(Sim* sim) {
    memset(sim, 0, sizeof(*sim));
    riskfight_init_context((EncounterContext*)&sim->context);
    riskfight_init_state((EncounterState*)&sim->state, (EncounterContext*)&sim->context);
    sim->context.self_play = 1;
    ours_init(&sim->ours);
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
    if (sim->obs.hit_count < OBSERVE_HITS)
        sim->obs.hits[sim->obs.hit_count++] = (ObservedHit){event->source, event->target, event->kind, event->hitpoints_lost};
    if (event->target == 1) {
        sim->step_damage += event->hitpoints_lost;
        if (event->hitpoints_lost > sim->step_max_hit) sim->step_max_hit = event->hitpoints_lost;
    }
}

static void opponent_actions(const OppModel* model, Sim* sim, int step, uint32_t seed, int* actions) {
    const RiskfightState* s = &sim->state;
    if (model->kind == MODEL_FIT) {
        opp_fit_actions(model->fit, s, &sim->hist, sim->tick_base + 1 + step, seed, actions);
        return;
    }
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(s, 1, obs);
    RiskfightProfile profile = RISKFIGHT_HUMAN_PROFILES[osrs_lowbias32(seed) % PROFILE_COUNT];
    riskfight_script(obs, &profile, s->script_seed[1], s->consume_ticks[1], actions);
}

static int is_consumable_raw(int raw) {
    return raw == RAW_ANGLERFISH || raw == RAW_KARAMBWAN || (raw >= 6685 && raw <= 6691) || (raw >= 3024 && raw <= 3030) || (raw >= 12695 && raw <= 12701);
}

static int slot_of_kind(const Player* p, OsrsConsumableKind kind) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[slot])->consumable_kind == kind) return slot;
    return -1;
}

/** Replays our recorded clicks other than meals, and eats when our hitpoints are within his worst single swing plus a margin. */
static void queue_ours_at_risk(Sim* sim, const Tape* tape, const OursPolicy* policy, int tick, int step) {
    (void)step;
    HumanInput* hi = &sim->context.policy_commands[0];
    Player* p = &sim->state.env.players[0];
    Player* him = &sim->state.env.players[1];
    for (int i = 0; i < tape->action_count; i++) {
        const TapeAction* a = &tape->actions[i];
        if (a->tick != tick || (a->kind == ACTION_CLICK && is_consumable_raw(a->raw_id))) continue;
        HumanCommand command = {0};
        switch (a->kind) {
            case ACTION_CLICK: command = (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = a->slot}; break;
            case ACTION_ATTACK: command = (HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1}; break;
            case ACTION_SPEC: command = (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE}; break;
            case ACTION_VENGEANCE: command = (HumanCommand){.kind = HUMAN_COMMAND_VENGEANCE}; break;
        }
        human_input_queue_command(hi, command);
    }
    AttackStyle style = get_slot_weapon_attack_style(him) == ATTACK_STYLE_RANGED ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE;
    int normal = calculate_max_hit(him, style, 1.0f, 30);
    int worst = him->special_energy >= ENERGY_FOR_SPEC && style == ATTACK_STYLE_MELEE ? osrs_voidwaker_max_hit(normal) : normal;
    if (p->current_hitpoints > worst + policy->eat_below) return;
    int hard = slot_of_kind(p, OSRS_CONSUMABLE_ANGLERFISH), karambwan = slot_of_kind(p, OSRS_CONSUMABLE_KARAMBWAN), brew = slot_of_kind(p, OSRS_CONSUMABLE_BREW);
    if (p->food_timer == 0 && hard >= 0)
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = hard});
    if (p->karambwan_timer == 0 && karambwan >= 0 && p->current_hitpoints <= worst)
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = karambwan});
    if (p->potion_timer == 0 && brew >= 0 && p->current_hitpoints <= worst)
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = brew});
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
    } else if (policy->kind == OURS_RULES) {
        ours_act(sim, &sim->ours, hi, step);
    } else if (policy->kind == OURS_EAT_AT_RISK) {
        queue_ours_at_risk(sim, tape, policy, tick, step);
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
static void run_steps(Sim* sim, const Tape* tape, int tick_base, int k, const OppModel* model, const OursPolicy* policy,
    uint32_t seed, Outcome* out) {
    RiskfightState* s = &sim->state;
    memset(out, 0, sizeof(*out));
    out->first_eat = -1; out->our_death = -1; out->his_death = -1;
    sim->outcome = out;
    g_tape = tape;
    sim->tick_base = tick_base;
    sim->previous = (StepEvents){0};
    if (policy->kind == OURS_RULES) {
        assert(tape->observer_series);
        sim->obs = ((const Observer*)tape->observer_series)[tick_base - tape->lo];
        ours_reset(&sim->ours, seed ^ 0x9e3779b9u, policy->timing, policy->stakes, policy->truth);
        observer_branch(&sim->state, &sim->obs);
    } else sim->ours.played_count = 0;
    if (tape->hist_series) sim->hist = ((const Hist*)tape->hist_series)[tick_base - tape->lo];
    s->env.pvp_runtime.hit_observer = sim_observe_hit;
    s->env.pvp_runtime.hit_observer_context = sim;
    for (int step = 0; step < k && !s->env.episode_over; step++) {
        int actions[RF_HEADS];
        opponent_actions(model, sim, step, seed + (uint32_t)step * 2654435761u, actions);
        riskfight_policy_commands(s, 1, actions, &sim->context.policy_commands[1]);
        queue_ours(sim, tape, policy, tick_base + 1 + step, step);
        int vw = s->spec_voidwaker[1], maul = s->spec_maul[1];
        if (g_ablation.pid) {
            char holder = tape_tick(tape, tick_base + 1 + step < tape->hi ? tick_base + 1 + step : tape->hi)->pid;
            if (holder == 'L' || holder == 'l') s->env.pid_holder = 0;
            else if (holder == 'O' || holder == 'o') s->env.pid_holder = 1;
        }
        sim->step_damage = sim->step_max_hit = 0;
        sim->obs.hit_count = 0;
        riskfight_step_queues(s, &sim->context, &sim->context.policy_commands[0].commands, &sim->context.policy_commands[1].commands);
        if (g_ablation.damage && s->env.players[1].current_hitpoints > 0 && !s->escaped[1]) {
            int adjust = sim->step_damage - tape_damage_on_him(tape, tick_base + 1 + step);
            Player* target = &s->env.players[1];
            target->current_hitpoints += adjust;
            if (target->current_hitpoints > 121) target->current_hitpoints = 121;
            if (target->current_hitpoints < 0) target->current_hitpoints = 0;
        }
        const Player* him = &s->env.players[1];
        int ate_true = him->ate_food_this_tick || him->ate_karambwan_this_tick || s->inventory_use[1].potion_animation_tick_plus_one == s->env.tick;
        StepEvents events = {.ate_visible = consumption_visible(s, 1), .ate = ate_true,
            .swung = s->visible[0].last_attack_tick == s->env.tick - 1, .vw = s->spec_voidwaker[1] - vw, .maul = s->spec_maul[1] - maul,
            .damage_on_him = sim->step_damage, .max_hit_on_him = sim->step_max_hit, .hit_on_him = him->hit_landed_this_tick};
        hist_step(&sim->hist, s, tick_base + 1 + step, &events);
        if (policy->kind == OURS_RULES) observer_step(s, &sim->obs, tick_base + 1 + step);
        sim->previous = events;
        if (g_trace) {
            const Player* t = &s->env.players[1];
            printf("  step %d his hp %d dmg_on_him %d ate %d veng %d | our hp %d\n", step, t->current_hitpoints, sim->step_damage, events.ate, t->veng_active, s->env.players[0].current_hitpoints);
        }
        out->steps = step + 1;
        out->his_eat[step] = (uint8_t)consumption_visible(s, 1);
        if (out->his_eat[step] && out->first_eat < 0) out->first_eat = step;
        out->his_vw_specs += s->spec_voidwaker[1] - vw;
        out->his_maul_specs += s->spec_maul[1] - maul;
        out->his_vengs += s->env.players[1].cast_veng_this_tick;
        if (s->env.players[0].current_hitpoints <= 0 && out->our_death < 0) out->our_death = step;
        if (s->env.players[1].current_hitpoints <= 0 && out->his_death < 0) out->his_death = step;
    }
    for (int i = 0; i < sim->ours.played_count; i++) {
        int step = sim->ours.played[i].step;
        if (step + OURS_HORIZON > k) continue;
        sim->spec_counted++;
        sim->spec_predicted += sim->ours.played[i].chance;
        sim->spec_realized += out->his_death >= step && out->his_death <= step + OURS_HORIZON;
    }
    out->our_meals = s->consume_ticks[0];
    out->hp_us = s->env.players[0].current_hitpoints;
    out->hp_him = s->env.players[1].current_hitpoints;
    out->bar_him = s->visible[0].health_bar;
}
#endif
