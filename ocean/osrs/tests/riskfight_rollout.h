#ifndef RISKFIGHT_ROLLOUT_H
#define RISKFIGHT_ROLLOUT_H
#include "riskfight_opp.h"
#include "riskfight_ours_state.h"
#include "riskfight_observe.h"

enum { MAX_STEPS = 512, PROFILE_COUNT = RISKFIGHT_HUMAN_TRAIN_PROFILES + RISKFIGHT_HUMAN_HELDOUT_PROFILES };
typedef enum { MODEL_PROFILES, MODEL_FIT } ModelKind;
typedef enum { OURS_REPLAY, OURS_EAT_AT_THRESHOLD, OURS_EAT_AT_RISK, OURS_RULES, OURS_IDLE, OURS_LOOKAHEAD } OursKind;

typedef struct {
    ModelKind kind;
    const OppFit* fit;
    int profile_first, profile_count;
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

static int policy_rules(const OursPolicy* policy) { return policy->kind == OURS_RULES || policy->kind == OURS_LOOKAHEAD; }

typedef struct {
    int steps;
    uint8_t his_eat[MAX_STEPS];
    uint32_t his_hit_mask;
    int his_hits, his_damage, his_vw_specs, his_maul_specs, his_vengs;
    int our_damage, our_hits;
    int first_eat, our_death, his_death, our_meals;
    int hp_us, hp_him, bar_him, start_hp;
    int trace_hp[MAX_STEPS], trace_damage[MAX_STEPS], trace_direct[MAX_STEPS], trace_our_hp[MAX_STEPS];
    uint8_t trace_ate[MAX_STEPS], trace_our_swing[MAX_STEPS], trace_his_swing[MAX_STEPS], trace_pid[MAX_STEPS], trace_our_spec[MAX_STEPS];
} Outcome;

typedef struct {
    RiskfightState state;
    RiskfightContext context;
    Outcome* outcome;
    int tick_base, step_damage, step_max_hit, step_direct;
    Hist hist;
    Ours ours;
    Observer obs;
    StepEvents previous;
    double spec_predicted, spec_realized, spec_counted;
    int seen_since_eat;
} Sim;

#include "riskfight_ours.h"

static void sim_init(Sim* sim) {
    memset(sim, 0, sizeof(*sim));
    riskfight_init_context((EncounterContext*)&sim->context);
    riskfight_init_state((EncounterState*)&sim->state, (EncounterContext*)&sim->context);
    sim->context.self_play = 1;
    ours_init(&sim->ours);
    riskfight_finalize_context((EncounterState*)&sim->state, (EncounterContext*)&sim->context);
    /* This constructor creates a fresh synthetic clean cohort. Loaded source
     * snapshots subsequently replace this declaration with their own status. */
    for (int side = 0; side < 2; side++)
        sim->state.poison_source[side] = OSRS_POISON_CLEAR_PRIOR;
}

static void sim_clear(Sim* sim) {
    observer_clear(&sim->obs);
    free(sim->ours.arena.base);
    sim->ours.arena = (Arena){0};
}

static void sim_observe_hit(void* context, const OsrsPvpHitEvent* event) {
    Sim* sim = context;
    Outcome* out = sim->outcome;
    int step = event->tick - RISKFIGHT_LOAD_TICK;
    if (event->kind == OSRS_HIT_DIRECT && event->source == 1 && event->target == 0) {
        g_his_events++;
        g_his_positive += event->hitpoints_lost > 0;
        out->his_hits++;
        if (step >= 0 && step < 32) out->his_hit_mask |= 1u << step;
        out->his_damage += event->hitpoints_lost;
    }
    if (event->kind == OSRS_HIT_DIRECT && event->source == 0 && event->target == 1) {
        g_our_events++;
        g_our_positive += event->hitpoints_lost > 0;
        if (event->hitpoints_lost > 0 && event->hitpoints_lost < 160) g_hit_hist[event->hitpoints_lost]++;
        out->our_hits++;
        out->our_damage += event->hitpoints_lost;
    }
    if (sim->obs.hit_count >= OBSERVE_HITS) {
        fprintf(stderr, "observed hit capacity exceeded\n"); abort();
    }
    sim->obs.hits[sim->obs.hit_count++] = (ObservedHit){event->source, event->target, event->kind, event->hitpoints_lost,
        osrs_hitsplat_type(event->source == 0, event->damage, event->rolled_max)};
    if (event->target == 1) {
        sim->step_damage += event->hitpoints_lost;
        if (event->kind == OSRS_HIT_DIRECT && event->source == 0) sim->step_direct += event->hitpoints_lost;
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
    int count = model->profile_count ? model->profile_count : PROFILE_COUNT;
    RiskfightProfile profile = RISKFIGHT_HUMAN_PROFILES[model->profile_first + osrs_lowbias32(g_rollout_seed) % count];
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
    } else if (policy_rules(policy)) {
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

static int consumption_visible(const RiskfightState* s, int agent) { return riskfight_meal_visible(s, agent); }

#include "riskfight_lookahead.h"

/** Runs k steps from the loaded sim state. tick_base is the tape tick the state is the end of. */
static void run_steps(Sim* sim, const Tape* tape, int tick_base, int k, const OppModel* model, const OursPolicy* policy,
    uint32_t seed, Outcome* out) {
    RiskfightState* s = &sim->state;
    uint32_t pending_rng = osrs_lowbias32(seed ^ 0x70656e64u) | 1;
    riskfight_sample_pending(s, &pending_rng);
    memset(out, 0, sizeof(*out));
    out->start_hp = s->env.players[1].current_hitpoints;
    out->first_eat = -1; out->our_death = -1; out->his_death = -1;
    sim->outcome = out;
    g_tape = tape;
    g_rollout_seed = seed;
    sim->tick_base = tick_base;
    sim->previous = (StepEvents){0};
    sim->seen_since_eat = 99;
    if (policy_rules(policy)) {
        assert(tape->observer_series);
        observer_assign(&sim->obs, &tape->observer_series[tick_base - tape->lo]);
        ours_reset(&sim->ours, seed ^ 0x9e3779b9u, policy->timing, policy->stakes, policy->truth);
        sim->ours.lookahead = policy->kind == OURS_LOOKAHEAD;
        observer_branch(&sim->state, &sim->obs);
    } else sim->ours.played_count = 0;
    if (tape->hist_series) sim->hist = ((const Hist*)tape->hist_series)[tick_base - tape->lo];
    s->env.pvp_runtime.hit_observer = sim_observe_hit;
    s->env.pvp_runtime.hit_observer_context = sim;
    for (int step = 0; step < k && !s->env.episode_over; step++) {
        int actions[RF_HEADS];
        opponent_actions(model, sim, step, seed + (uint32_t)step * 2654435761u, actions);
        riskfight_policy_commands(s, 1, actions, &sim->context.policy_commands[1]);
        g_cmd_trace = 0;
        if (getenv("GS_COMBO_TRACE") && policy_rules(policy) && sim->ours.played_count > 0 && sim->ours.played[sim->ours.played_count - 1].choice == GS_CHOICE_MAUL_VOIDWAKER && step - sim->ours.played[sim->ours.played_count - 1].step <= 9 && step >= sim->ours.played[sim->ours.played_count - 1].step) g_cmd_trace = 1;
        queue_ours(sim, tape, policy, tick_base + 1 + step, step);
        int vw = s->spec_voidwaker[1], maul = s->spec_maul[1], our_specs = s->spec_voidwaker[0] + s->spec_maul[0];
        if (g_ablation.pid) {
            char holder = tape_tick(tape, tick_base + 1 + step < tape->hi ? tick_base + 1 + step : tape->hi)->pid;
            if (holder == 'L' || holder == 'l') s->env.pid_holder = 0;
            else if (holder == 'O' || holder == 'o') s->env.pid_holder = 1;
        }
        sim->step_damage = sim->step_max_hit = sim->step_direct = 0;
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
        sim->seen_since_eat = events.ate_visible ? 0 : sim->seen_since_eat + 1;
        if (policy_rules(policy)) observer_step(s, &sim->obs, tick_base + 1 + step);
#ifdef GS_HP_BELIEF
        if (policy_rules(policy) && sim->obs.inf.watched_count > 0) { int c = sim->obs.inf.watched[sim->obs.inf.watched_count - 1]; g_cls[c][events.ate ? 1 : 0]++; }
#endif
        if (getenv("GS_COMBO_TRACE") && policy_rules(policy) && sim->ours.played_count > 0 && sim->ours.played[sim->ours.played_count - 1].choice == GS_CHOICE_MAUL_VOIDWAKER && step - sim->ours.played[sim->ours.played_count - 1].step <= 9 && step >= sim->ours.played[sim->ours.played_count - 1].step) {
            static int shown;
            const Player* u = &s->env.players[0];
            if (step == sim->ours.played[sim->ours.played_count - 1].step) shown++;
            if (shown <= 6) fprintf(stderr, "combo#%d +%d (vw %d maul %d) weapon %d armed %d energy %d atk_timer %d dmg_on_him %d pending %d interacting %d specs %d\n", shown, step - sim->ours.played[sim->ours.played_count - 1].step, ITEM_VOIDWAKER, ITEM_GRANITE_MAUL_ORNATE, u->equipped[GEAR_SLOT_WEAPON], u->spec_armed, u->special_energy, u->attack_timer, sim->step_damage, sim->ours.pending_count, osrs_interaction_active(&u->interaction), s->spec_voidwaker[0] + s->spec_maul[0]);
        }
        sim->previous = events;
        if (g_trace) {
            const Player* t = &s->env.players[1];
            printf("  step %d his hp %d dmg_on_him %d ate %d veng %d | our hp %d\n", step, t->current_hitpoints, sim->step_damage, events.ate, t->veng_active, s->env.players[0].current_hitpoints);
        }
        g_sim_hp_ticks[imin(12, imax(0, s->env.players[1].current_hitpoints) / 10)]++;
        out->trace_hp[step] = s->env.players[1].current_hitpoints;
        out->trace_damage[step] = sim->step_damage;
        out->trace_direct[step] = sim->step_direct;
        out->trace_our_hp[step] = s->env.players[0].current_hitpoints;
        out->trace_ate[step] = (uint8_t)(events.ate ? (events.ate_visible ? 2 : 1) : 0);
        out->trace_our_swing[step] = (uint8_t)events.swung;
        out->trace_his_swing[step] = (uint8_t)(s->visible[1].last_attack_tick == s->env.tick - 1);
        out->trace_pid[step] = (uint8_t)s->env.pid_holder;
        out->steps = step + 1;
        out->his_eat[step] = (uint8_t)consumption_visible(s, 1);
        if (out->his_eat[step] && out->first_eat < 0) out->first_eat = step;
        out->trace_our_spec[step] = (uint8_t)(s->spec_voidwaker[0] + s->spec_maul[0] - our_specs);
        out->his_vw_specs += s->spec_voidwaker[1] - vw;
        out->his_maul_specs += s->spec_maul[1] - maul;
        out->his_vengs += s->env.players[1].cast_veng_this_tick;
        if (s->env.players[0].current_hitpoints <= 0 && out->our_death < 0) out->our_death = step;
        if (s->env.players[1].current_hitpoints <= 0 && out->his_death < 0) out->his_death = step;
    }
    uint8_t thrown[64] = {0};
    for (int t = 0; t < out->steps; t++) {
        if (!out->trace_our_spec[t]) continue;
        for (int i = sim->ours.played_count - 1; i >= 0; i--)
            if (sim->ours.played[i].step <= t) { if (t - sim->ours.played[i].step <= 8) thrown[i] = 1; break; }
    }
    for (int i = 0; i < sim->ours.played_count; i++) {
        int step = sim->ours.played[i].step;
        if (step + OURS_HORIZON > k) continue;
        if (g_thrown_only && !thrown[i]) continue;
        if (g_vw_only && sim->ours.played[i].choice != GS_CHOICE_VOIDWAKER &&
            sim->ours.played[i].choice != GS_CHOICE_VOIDWAKER_BURST) continue;
        sim->spec_counted++;
        sim->spec_predicted += sim->ours.played[i].chance;
        sim->spec_realized += out->his_death >= step && out->his_death <= step + OURS_HORIZON;
        int band = sim->ours.played[i].his_hitpoints <= 44 ? 0 : sim->ours.played[i].his_hitpoints <= 87 ? 1 : 2;
        int dmg = 0, ate_in = 0;
        for (int t = step; t <= step + OURS_HORIZON && t < out->steps; t++) { dmg += out->trace_damage[t]; ate_in |= out->trace_ate[t] != 0; }
        int fb = sim->ours.played[i].his_hitpoints <= 44 ? 0 : sim->ours.played[i].his_hitpoints <= 60 ? 1 : sim->ours.played[i].his_hitpoints <= 87 ? 2 : 3, fj = 10;
        for (int t = step; t < step + 10 && t < out->steps; t++) if (out->trace_ate[t]) { fj = t - step; break; }
        g_fm[fb][fj]++;
        if (getenv("GS_COMBO_DBG") && sim->ours.played[i].choice == GS_CHOICE_MAUL_VOIDWAKER) {
            static int shown;
            if (shown++ < 8) {
                fprintf(stderr, "combo at step %d plan_land %d plan_last %d chance %.2f hp %d:", step, sim->ours.played[i].plan_land, sim->ours.played[i].plan_last, sim->ours.played[i].chance, sim->ours.played[i].his_hitpoints);
                for (int t = step; t <= step + 10 && t < out->steps; t++) fprintf(stderr, " [%d d%d s%d w%d e%d]", t - step, out->trace_damage[t], out->trace_our_spec[t], out->trace_our_swing[t], out->trace_ate[t]);
                fprintf(stderr, "\n");
            }
        }
        {
            int c = sim->ours.played[i].choice, w = sim->ours.played[i].plan_last + 1, tot = 0, first = -1;
            for (int t = step; t <= step + w + 1 && t < out->steps; t++) { tot += out->trace_damage[t]; if (first < 0 && out->trace_damage[t] > 0) first = t - step; }
            if (c == GS_CHOICE_MAUL_VOIDWAKER) { int sp = 0; for (int t = step; t <= step + 8 && t < out->steps; t++) sp += out->trace_our_spec[t]; g_cb_split[sim->ours.played[i].preempted][sp >= 2]++; }
            { double m = sim->ours.played[i].bar_mid; int rb = m <= 44 ? 0 : m <= 60 ? 1 : m <= 87 ? 2 : 3, ate3 = 0; for (int t = step; t <= step + 3 && t < out->steps; t++) ate3 |= out->trace_ate[t] != 0; g_react_sim[rb][ate3]++; }
            g_pc_n[c]++; g_pc_plan[c] += sim->ours.played[i].plan_mean; g_pc_act[c] += tot; g_pc_land[c] += sim->ours.played[i].plan_land;
            g_pc_actland[c] += first < 0 ? sim->ours.played[i].plan_land : first; g_pc_any[c] += first >= 0;
            for (int t = step; t <= step + 8 && t < out->steps; t++) g_pc_specs[c] += out->trace_our_spec[t];
            g_pc_chance[c] += sim->ours.played[i].chance; g_pc_dead[c] += out->his_death >= step && out->his_death <= step + OURS_HORIZON;
        }
        { int d = (int)(sim->ours.played[i].pit * 10); if (d > 9) d = 9; g_pit_played[d]++; g_pit_played_n++; }
        g_dm_n[band]++;
        g_dm_sum[band] += dmg;
        g_dm_any[band] += dmg > 0;
        g_dm_lethal[band] += dmg >= sim->ours.played[i].his_hitpoints;
        g_dm_ate[band] += ate_in;
        g_dm_dead[band] += out->his_death >= step && out->his_death <= step + OURS_HORIZON;
        g_spec_counted[band]++;
        g_spec_predicted[band] += sim->ours.played[i].chance;
        g_spec_realized[band] += out->his_death >= step && out->his_death <= step + OURS_HORIZON;
        double expected = sim->ours.played[i].expected;
        int planner_band = expected <= 60 ? 0 : expected <= 75 ? 1 : expected <= 90 ? 2 : 3;
        g_exp_counted[planner_band]++;
        g_exp_mean[planner_band] += expected;
        g_exp_true[planner_band] += sim->ours.played[i].his_hitpoints;
        g_exp_predicted[planner_band] += sim->ours.played[i].chance;
        g_exp_realized[planner_band] += out->his_death >= step && out->his_death <= step + OURS_HORIZON;
    }
    out->our_meals = s->consume_ticks[0];
    g_sim_escapes += s->escaped[1];
    g_sim_rollouts++;
    out->hp_us = s->env.players[0].current_hitpoints;
    out->hp_him = s->env.players[1].current_hitpoints;
    out->bar_him = s->visible[0].health_bar;
}
#endif
