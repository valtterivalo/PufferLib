#ifndef OSRS_RISKFIGHT_STEP_H
#define OSRS_RISKFIGHT_STEP_H
#include "riskfight_policy.h"
#include "../../osrs_poison.h"

static void riskfight_stop(RiskfightState* s, int agent) {
    Player* p = &s->env.players[agent];
    osrs_interaction_clear(&p->interaction);
    s->env.pvp_runtime.walk_dest_x[agent] = -1;
    s->env.pvp_runtime.walk_dest_y[agent] = -1;
    p->is_moving = 0;
}

static void riskfight_record_attack(RiskfightState* s, int agent, int hits, int instant) {
    if (hits == 0) return;
    Player* p = &s->env.players[agent];
    int weapon = p->equipped[GEAR_SLOT_WEAPON];
    RiskfightVisibleOpponent* visible = &s->visible[1 - agent];
    float* event = visible->events[s->env.tick % RF_HISTORY_TICKS];
    event[0] += hits;
    event[1] = (float)osrs_item_byte_code((uint16_t)weapon);
    event[2] = p->attack_style_this_tick;
    riskfight_threat_note_swing(&visible->threat, (uint16_t)weapon, s->env.tick, p->used_special_this_tick);
    visible->last_swing_item = weapon;
    visible->last_swing_tick = s->env.tick;
    if (p->used_special_this_tick) visible->last_spec_tick = s->env.tick;
    if (agent == 1) s->ragger_swings++;
    if (!instant) riskfight_his_swing_seen(visible, s->env.tick, get_slot_gear_bonuses(p)->attack_speed);
}
/* `count` special attacks of `agent` with `weapon` this tick: counted, and seen by the other side, whose estimate of his special energy pays the cost of each.
 * A double granite maul is one animation and two hits, so two specials; every other special is one (a Behead costs nothing and still counts). */
static void riskfight_special_used(RiskfightState* s, int agent, int weapon, int count) {
    s->specs_used[agent] += count;
    for (int i = 0; i < count; i++) riskfight_spec_est_seen(&s->visible[1 - agent], osrs_spec_cost(weapon));
}
static void riskfight_count_maul_spec(RiskfightState* s, int agent, int hits) {
    if (hits <= 0) return;
    s->spec_maul[agent]++;
    riskfight_special_used(s, agent, s->env.players[agent].equipped[GEAR_SLOT_WEAPON], hits);
}

static void riskfight_attack(RiskfightState* s, RiskfightContext* ctx, int agent) {
    Player* p = &s->env.players[agent];
    Player* opponent = &s->env.players[1 - agent];
    int weapon = p->equipped[GEAR_SLOT_WEAPON];
    int cost = osrs_spec_cost(weapon);
    int special = !pvp_is_maul(weapon) && p->spec_armed && ((cost > 0 && p->special_energy >= cost) || osrs_spec_is_stack_special(weapon));
    AttackStyle style = get_slot_weapon_attack_style(p) == ATTACK_STYLE_RANGED ?
        ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE;
    int distance = chebyshev_distance(p->x, p->y, opponent->x, opponent->y);
    int in_range = style == ATTACK_STYLE_MELEE ? is_in_melee_range(p, opponent) :
        distance > 0 && distance <= get_attack_range(p, style);
    if (!osrs_interaction_active(&p->interaction) ||
        p->current_hitpoints <= 0 || opponent->current_hitpoints <= 0 ||
        s->escaped[agent] || s->escaped[1 - agent] ||
        !in_range || !can_attack_now(p) ||
        s->env.pvp_runtime.maul[agent].last_special_tick == s->env.tick) return;
    /* Melee delay is zero, but the second actor lands next tick because its
     * recipient's incoming phase has already run. Immunity ticks before inputs. */
    if (weapon == ITEM_ABYSSAL_DAGGER_BH_P_PLUS_PLUS &&
        s->poison[1 - agent].immunity_ticks <= (agent != s->env.pid_holder)) {
        fprintf(stderr, "unverified weapon-poison application tick=%d attacker=%d defender=%d\n",
            s->env.tick, agent, 1 - agent);
        abort();
    }
    if (special) riskfight_special_used(s, agent, weapon, 1);
    if (special && weapon == ITEM_VOIDWAKER) s->spec_voidwaker[agent]++;
    perform_attack(&s->env, agent, 1 - agent, style, special, 0, distance);
    riskfight_record_attack(s, agent, 1, 0);
    if (special) osrs_spec_disarm(&p->spec_armed);
}

static int riskfight_encode_offensive_prayer(int offensive) {
    switch ((OffensivePrayer)offensive) {
        case OFFENSIVE_PRAYER_NONE:   return ENCOUNTER_OFFENSIVE_OFF;
        case OFFENSIVE_PRAYER_PIETY:  return ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY;
        case OFFENSIVE_PRAYER_RIGOUR: return ENCOUNTER_OFFENSIVE_SET_REFRESH_RIGOUR;
        case OFFENSIVE_PRAYER_AUGURY: return ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY;
        default:                      return ENCOUNTER_OFFENSIVE_NO_CHANGE;
    }
}

/* Return whether this command actually consumed food or a potion, not a
 * debug-kind counter delta (which does not cover every inventory consumable). */
static int riskfight_execute_command(RiskfightState* s, RiskfightContext* ctx,
    int agent, const HumanCommand* command) {
    Player* p = &s->env.players[agent];
    if (p->current_hitpoints <= 0 || s->escaped[agent]) return 0;
    switch (command->kind) {
        case HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK:
        case HUMAN_COMMAND_EAT:
        case HUMAN_COMMAND_DRINK:
        case HUMAN_COMMAND_EQUIP_INVENTORY_ITEM: {
            int old_weapon = p->equipped[GEAR_SLOT_WEAPON];
            int slot = command->inventory_slot;
            if (command->item_osrs_id > 0) {
                slot = -1;
                for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
                    if (osrs_inventory_cell_metadata(&p->inventory_cells[i])->raw_osrs_id == command->item_osrs_id) {
                        slot = i;
                        break;
                    }
                if (slot < 0 && command->kind == HUMAN_COMMAND_EQUIP_INVENTORY_ITEM) {
                    for (int i = 0; i < NUM_GEAR_SLOTS; i++)
                        if (ITEM_DATABASE[p->equipped[i]].item_id == command->item_osrs_id) return 0;
                }
                if (slot < 0) {
                    fprintf(stderr, "production command item unavailable: %d\n", command->item_osrs_id);
                    abort();
                }
            }
            if (slot < 0 || slot >= OSRS_INVENTORY_SIZE) return 0;
            OsrsConsumableKind pre_kind = OSRS_CONSUMABLE_NONE;
            OsrsClickAction pre_click = OSRS_CLICK_NONE;
            {
                const OsrsItemContentMetadata* pre_meta =
                    osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
                pre_click = (OsrsClickAction)pre_meta->click_action;
                if (pre_click == OSRS_CLICK_DRINK || pre_click == OSRS_CLICK_EAT)
                    pre_kind = (OsrsConsumableKind)pre_meta->consumable_kind;
            }
            if (osrs_inventory_cell_metadata(&p->inventory_cells[slot])->gear_slot >= 0)
                s->last_unequip_result[agent] = OSRS_UNEQUIP_SUCCESS;
            OsrsInventoryUseResult use_result = osrs_player_use_inventory_with_food_clock(p, &s->inventory_use[agent],
                &s->env.pvp_runtime.teleport[agent], slot,
                s->env.tick, OSRS_FOOD_CONTINUOUS_CLOCK);
            s->escaped[agent] = use_result == OSRS_INVENTORY_USE_ESCAPED;
            if (s->escaped[agent]) {
                s->escape_tick[agent] = s->env.tick;
                for (int i = 0; i < 2; i++)
                    s->escape_supplies[agent][i] = osrs_escape_supplies(&s->env.players[i]);
            }
            /* A meal (food, karambwan, pie or brew) eaten while the hitpoints the tick began with were already above his chance range then is an overeat. The tick's
             * start sets the bar for every meal of the tick: a triple-eat heals as one decision, and a brew does not shrink the range it is judged against. */
            if (use_result == OSRS_INVENTORY_USE_CONSUMED && (pre_click == OSRS_CLICK_EAT || pre_kind == OSRS_CONSUMABLE_BREW) &&
                    s->tick_hitpoints[agent] > s->tick_range[agent])
                s->overeats[agent]++;
            if (use_result == OSRS_INVENTORY_USE_CONSUMED) {
                switch (pre_kind) {
                    case OSRS_CONSUMABLE_BREW: s->drink_brew[agent]++; break;
                    case OSRS_CONSUMABLE_SANFEW:
                        s->drink_sanfew[agent]++;
                        osrs_poison_cure(&s->poison[agent], OSRS_SANFEW_IMMUNITY_TICKS);
                        if (agent == 0) s->poison_source[agent] = OSRS_POISON_CAUSAL_STATE;
                        break;
                    case OSRS_CONSUMABLE_SUPER_COMBAT:
                    case OSRS_CONSUMABLE_DIVINE_COMBAT: s->drink_combat[agent]++; break;
                    case OSRS_CONSUMABLE_MARLIN: s->eat_marlin[agent]++; break;
                    case OSRS_CONSUMABLE_HALIBUT: s->eat_halibut[agent]++; break;
                    case OSRS_CONSUMABLE_SUMMER_PIE: s->eat_pie[agent]++; break;
                    default: break;
                }
            }
            pvp_maul_weapon_changed(&s->env, agent, old_weapon);
            pvp_refresh_visible_gear(p);
            return use_result == OSRS_INVENTORY_USE_CONSUMED &&
                (pre_click == OSRS_CLICK_EAT || pre_click == OSRS_CLICK_DRINK);
        }
        case HUMAN_COMMAND_UNEQUIP: {
            int old_weapon = p->equipped[GEAR_SLOT_WEAPON];
            s->last_unequip_result[agent] = osrs_unequip_to_inventory(p, p->inventory_cells, command->gear_slot);
            pvp_maul_weapon_changed(&s->env, agent, old_weapon);
            pvp_refresh_visible_gear(p);
            break;
        }
        case HUMAN_COMMAND_STOP:
            riskfight_stop(s, agent);
            break;
        case HUMAN_COMMAND_ATTACK_NPC:
            osrs_interaction_set(&p->interaction, 1 - agent);
            s->env.pvp_runtime.walk_dest_x[agent] = -1;
            s->env.pvp_runtime.walk_dest_y[agent] = -1;
            if (!s->escaped[1 - agent]) {
                int maul_hits = pvp_maul_target_click(&s->env, agent, 1 - agent);
                riskfight_count_maul_spec(s, agent, maul_hits);
                riskfight_record_attack(s, agent, maul_hits, 1);
            }
            break;
        case HUMAN_COMMAND_WALK:
            riskfight_stop(s, agent);
            s->env.pvp_runtime.walk_dest_x[agent] = command->world_x;
            s->env.pvp_runtime.walk_dest_y[agent] = command->world_y;
            break;
        case HUMAN_COMMAND_SPEC_TOGGLE:
            if (command->spec_mode == HUMAN_SPEC_DISARM) {
                s->env.pvp_runtime.maul[agent] =
                    osrs_granite_maul_clear_preparation(s->env.pvp_runtime.maul[agent]);
                osrs_spec_disarm(&p->spec_armed);
                break;
            }
            if (command->spec_mode == HUMAN_SPEC_ENABLE && p->spec_armed) break;
            if (pvp_is_maul(p->equipped[GEAR_SLOT_WEAPON])) {
                pvp_maul_special_click(&s->env, agent);
            } else
                p->spec_armed = !p->spec_armed;
            break;
        case HUMAN_COMMAND_VENGEANCE:
            osrs_player_cast_inventory_vengeance(p, &s->inventory_use[agent], s->env.tick);
            break;
        case HUMAN_COMMAND_OFFENSIVE_PRAYER:
            if (p->current_prayer > 0)
                encounter_apply_offensive_action(&p->offensive_prayer, command->offensive_prayer);
            break;
        case HUMAN_COMMAND_OVERHEAD_PRAYER:
            if (p->current_prayer > 0)
                encounter_apply_overhead_action(&p->prayer, command->overhead_prayer);
            break;
        case HUMAN_COMMAND_FIGHT_STYLE:
            p->fight_style = (FightStyle)command->fight_style;
            break;
        case HUMAN_COMMAND_ITEM_ON_ITEM:
            osrs_inventory_swap_cells(p->inventory_cells,
                command->inventory_slot, command->target_inventory_slot);
            break;
        case HUMAN_COMMAND_NONE:
        case HUMAN_COMMAND_SPELL_TARGET:
        case HUMAN_COMMAND_SET_AUTOCAST:
        case HUMAN_COMMAND_ITEM_ON_WIDGET:
        case HUMAN_COMMAND_SPELL_ON_WIDGET:
            break;
    }
    return 0;
}

/** Head actions as commands. Bag actions name physical slots (action k clicks slot k - 1): a net's actions go through riskfight_bag_to_physical first. */
static void riskfight_policy_commands(const RiskfightState* s, int agent,
    const int* actions, HumanInput* hi) {
    human_input_clear_pending(hi);
    /* The style and prayer heads of a net stop at accurate..defensive and up to Piety; a scripted ranger fights rapid on Rigour, which no net picks. */
    for (int head = 0; head < RF_HEADS; head++) {
        int limit = head == RF_STYLE ? FIGHT_STYLE_RAPID + 1 : head == RF_PRAYER ? OFFENSIVE_PRAYER_AUGURY + 1 : RF_ACTION_DIMS[head];
        assert(actions[head] >= 0 && actions[head] < limit);
    }
    for (int head = 0; head < RF_HEADS; head++) {
        int gear_slot = RF_GEAR_SLOT_BY_HEAD[head];
        if (gear_slot < 0 || !actions[head]) continue;
        if (actions[head] == RF_UNEQUIP)
            human_input_queue_unequip(hi, gear_slot);
        else human_input_queue_command(hi, (HumanCommand){
            .kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK,
            .inventory_slot = actions[head] - 1,
        });
    }
    for (int head = RF_FOOD; head <= RF_COMBO; head++) {
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
        .offensive_prayer = riskfight_encode_offensive_prayer(actions[RF_PRAYER])});
    human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_OVERHEAD_PRAYER,
        .overhead_prayer = actions[RF_OVERHEAD]});
    human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_FIGHT_STYLE,
        .fight_style = actions[RF_STYLE]});
    int primary = actions[RF_PRIMARY];
    for (int n = 0; n < actions[RF_SPECIAL]; n++)
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE});
    if (primary == RF_STOP)
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_STOP});
    else if (primary == RF_ATTACK && riskfight_attack_click(&s->env.players[agent], actions[RF_SPECIAL], actions))
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1 - agent});
    else if (primary > 0 && primary < RF_ATTACK) {
        int dx = ENCOUNTER_MOVE_TARGET_DX[primary];
        int dy = ENCOUNTER_MOVE_TARGET_DY[primary];
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_WALK,
            .world_x = s->env.players[agent].x + dx, .world_y = s->env.players[agent].y + dy});
    }
}


static int riskfight_is_click(const HumanCommand* c) {
    switch (c->kind) {
        case HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK: case HUMAN_COMMAND_EAT: case HUMAN_COMMAND_DRINK: case HUMAN_COMMAND_EQUIP_INVENTORY_ITEM:
        case HUMAN_COMMAND_UNEQUIP: case HUMAN_COMMAND_VENGEANCE: case HUMAN_COMMAND_SPEC_TOGGLE: case HUMAN_COMMAND_ATTACK_NPC:
        case HUMAN_COMMAND_WALK: case HUMAN_COMMAND_STOP: return 1;
        default: return 0;
    }
}

/** Replaces the commands of one decision of `seat` by what its hand lands on the tick about to be processed, and marks its next decision busy (RiskfightState.hand_busy,
 * its mask the no-op) while clicks of this chain are still unlanded: a busy decision's clicks would be dropped. */
static void riskfight_motor_filter(RiskfightContext* ctx, RiskfightState* s, int seat, HumanInput* hi) {
    RiskfightMotor* m = &ctx->motor[seat];
    if (!m->enabled) return;
    int decision = s->env.tick;
    HumanCommand fresh[48], instant[48];
    int fresh_count = 0, instant_count = 0;
    for (int i = 0; i < hi->commands.count; i++) {
        const HumanCommand* c = &hi->commands.items[i];
        if (riskfight_is_click(c)) { assert(fresh_count < 48); fresh[fresh_count++] = *c; }
        else { assert(instant_count < 48); instant[instant_count++] = *c; }
    }
    human_input_clear_pending(hi);
    human_input_clear_commands(hi);
    for (int i = 0; i < instant_count; i++) human_input_queue_command(hi, instant[i]);
    int busy = m->pending_count > 0;
    if (!busy) {
        double t0 = (decision - 1) * (double)MOTOR_TICK_MS, press = 0;
        for (int k = 0; k < fresh_count && m->pending_count < 24; k++) {
            uint32_t x = m->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; m->rng = x;
            press += motor_gap_ms(((x >> 8) + 0.5) / 16777216.0, k);
            m->pending[m->pending_count++] = (RiskfightMotorClick){fresh[k], motor_land_tick(decision - 1, press)};
        }
        (void)t0;
    }
    int kept = 0;
    for (int i = 0; i < m->pending_count; i++) {
        if (m->pending[i].land <= decision) human_input_queue_command(hi, m->pending[i].cmd);
        else m->pending[kept++] = m->pending[i];
    }
    m->pending_count = kept;
    s->hand_busy[seat] = kept > 0;
}

static void riskfight_finish(RiskfightState* s) {
    int dead[2] = {s->env.players[0].current_hitpoints <= 0, s->env.players[1].current_hitpoints <= 0};
    int escaped = s->escaped[0] || s->escaped[1];
    s->env.episode_over = pvp_death_is_settled(&s->env) || escaped || s->env.tick >= RISKFIGHT_ROUND_TICKS;
    for (int i = 0; i < 2; i++) {
        s->outcome[i] = !s->env.episode_over ? RISKFIGHT_ONGOING :
            dead[0] && dead[1] ? RISKFIGHT_MUTUAL_DEATH :
            dead[i] ? RISKFIGHT_DEATH : dead[1 - i] ? RISKFIGHT_KILL :
            s->ragger && s->escaped[1] && !s->escaped[0] ? RISKFIGHT_RAGGED : escaped ? RISKFIGHT_ESCAPE : RISKFIGHT_TIMEOUT;
        s->rewards[i] = riskfight_outcome_reward(s->outcome[i], riskfight_seat_price(s, i));
    }
    s->env.winner = s->outcome[0] == RISKFIGHT_KILL ? 0 :
        s->outcome[1] == RISKFIGHT_KILL ? 1 : -1;
}

typedef struct {
    int direct_hitpoints_lost[2];
    OsrsPvpHitObserver next;
    void* next_context;
} RiskfightRewardObserver;

static void riskfight_observe_reward_damage(void* context, const OsrsPvpHitEvent* event) {
    RiskfightRewardObserver* observer = (RiskfightRewardObserver*)context;
    if (event->kind == OSRS_HIT_DIRECT && event->source != event->target)
        observer->direct_hitpoints_lost[event->source] += event->hitpoints_lost;
    if (observer->next) observer->next(observer->next_context, event);
}

typedef struct {
    RiskfightState* state;
    OsrsPvpAttackChanceObserver next;
    void* next_context;
} RiskfightChanceObserver;

static void riskfight_observe_attack_chance(void* context, const OsrsPvpAttackChanceEvent* event) {
    RiskfightChanceObserver* observer = (RiskfightChanceObserver*)context;
    float* chance = &observer->state->direct_ko_chance_mass[event->source];
    *chance += (1.0f - *chance) * event->ko_probability;
    if (observer->next) observer->next(observer->next_context, event);
}

/* A meal's animation is visible to the other side with a probability that depends on what else the eater shows (fit_meal_visibility.py over our
 * own meals, whose animations the other side sees exactly as we see his; Fact 7 has the spectator side of the same hiding): never on a swing tick
 * (Fact 6), p(quiet: no animation the tick before, no hit landing) 0.89 (961 of 1,077 meals), p(an animation carried from the tick before) 0.45
 * (114 of 252), p(a hit lands, its block animation competing) 0.49 (151 of 311); 0 of 10 on a swing tick. Refit on the 159 tapes of c-engine, Oct 16. The old rule hid a meal exactly on swing and hit ticks and showed every other one. */
static const double RF_MEAL_VISIBLE_QUIET = 0.89, RF_MEAL_VISIBLE_CARRY = 0.45, RF_MEAL_VISIBLE_HIT = 0.49;

static int riskfight_meal_visible(const RiskfightState* s, int agent) {
    const Player* p = &s->env.players[agent];
    if (!osrs_consumption_animation_visible(p->current_hitpoints, p->attack_style_this_tick, p->ate_food_this_tick,
            p->ate_karambwan_this_tick, s->inventory_use[agent].potion_animation_tick_plus_one == s->env.tick)) return 0;
    double visible = p->hit_landed_this_tick ? RF_MEAL_VISIBLE_HIT : s->anim_carry[agent] ? RF_MEAL_VISIBLE_CARRY : RF_MEAL_VISIBLE_QUIET;
    return (riskfight_sample_hash(s->script_seed[agent], (uint32_t)s->env.tick, 91) % 1000000u) < (uint32_t)(visible * 1e6);
}

static void riskfight_actor_inputs(RiskfightState* s, RiskfightContext* ctx,
    int agent, const HumanCommandQueue* queue) {
    if (s->env.players[agent].current_hitpoints <= 0 || s->escaped[agent]) return;
    int consumed = 0;
    s->tick_hitpoints[agent] = s->env.players[agent].current_hitpoints;
    s->tick_range[agent] = riskfight_chance_range(s, agent);
    for (int n = 0; n < queue->count; n++)
        consumed |= riskfight_execute_command(s, ctx, agent, &queue->items[n]);
    s->consume_ticks[agent] += consumed;
    if (!s->escaped[0] && !s->escaped[1] && s->env.players[agent].current_hitpoints > 0) {
        int maul_hits = pvp_maul_finish_inputs(&s->env, agent);
        riskfight_count_maul_spec(s, agent, maul_hits);
        riskfight_record_attack(s, agent, maul_hits, 1);
    }
}

static void riskfight_step_queues(RiskfightState* s, RiskfightContext* ctx,
    const HumanCommandQueue* first, const HumanCommandQueue* second) {
    assert(!s->env.episode_over);
    RiskfightRewardObserver reward_observer = {
        .next = s->env.pvp_runtime.hit_observer,
        .next_context = s->env.pvp_runtime.hit_observer_context,
    };
    if (ctx->damage_reward_coeff != 0) {
        s->env.pvp_runtime.hit_observer = riskfight_observe_reward_damage;
        s->env.pvp_runtime.hit_observer_context = &reward_observer;
    }
    float previous_chance[2] = {s->direct_ko_chance_mass[0], s->direct_ko_chance_mass[1]};
    RiskfightChanceObserver chance_observer = {
        .state = s,
        .next = s->env.pvp_runtime.attack_chance_observer,
        .next_context = s->env.pvp_runtime.attack_chance_observer_context,
    };
    s->env.pvp_runtime.attack_chance_observer = riskfight_observe_attack_chance;
    s->env.pvp_runtime.attack_chance_observer_context = &chance_observer;
    memset(s->rewards, 0, sizeof(s->rewards));
    const HumanCommandQueue* queues[] = {first, second};
    for (int i = 0; i < 2; i++) {
        Player* p = &s->env.players[i];
        memset(s->visible[i].events[s->env.tick % RF_HISTORY_TICKS], 0, RF_EVENT_WIDTH * sizeof(float));
        s->anim_carry[i] = s->anim_now[i];
        reset_tick_flags(p);
        p->hit_damage = 0;
        p->render_hit_count = 0;
        p->hit_landed_this_tick = 0;
        update_player_timers(p, encounter_player_prayer_bonus(p));
        if (p->pvp_overheal_block > 0) p->pvp_overheal_block--;
        riskfight_spec_est_regen(&s->visible[1 - i]);
        osrs_poison_tick_immunity(&s->poison[i]);
    }
    /* Inputs of both actors (meals, casts, equips, clicks) resolve before either actor's incoming hits: a meal heals before the damage landing on
     * its tick, whichever actor is processed first (Fact 3, 28 of 28 order-discriminating own-exact-HP cases). The local-PID recording
     * (seq37192, 37252-37270: cast on t206 precedes the previous-tick opponent landing) is the first-actor case of the same rule. */
    for (int turn = 0; turn < 2; turn++) {
        int i = s->env.pid_holder ^ turn;
        riskfight_actor_inputs(s, ctx, i, queues[i]);
    }
    for (int turn = 0; turn < 2; turn++) {
        int i = s->env.pid_holder ^ turn;
        if (s->escaped[i]) {
            s->env.players[1 - i].num_pending_hits = 0;
            memset(s->env.pending_hit_metadata[1 - i], 0, sizeof(s->env.pending_hit_metadata[1 - i]));
        } else {
            int active = s->env.players[i].veng_active;
            int poison_damage = osrs_poison_tick(&s->poison[i]);
            if (poison_damage > 0 && s->env.players[i].current_hitpoints > 0) {
                Player* target = &s->env.players[i];
                int before = target->current_hitpoints;
                PendingHit poison_hit = {.damage = poison_damage, .kind = OSRS_HIT_POISON};
                target->current_hitpoints = max_int(0, before - poison_damage);
                target->hit_landed_this_tick = 1;
                target->hit_damage += poison_damage;
                target->damage_applied_this_tick += poison_damage;
                pvp_render_hit_events_add(target, poison_damage);
                float scale = (float)poison_damage / target->base_hitpoints;
                target->total_damage_received += scale;
                target->damage_received_scale += scale;
                /* Initial status has no observed attacker identity. A self-source
                 * status event does not invent opponent credit or direct reward. */
                pvp_observe_hit(&s->env, i, i, &poison_hit, poison_damage, before, 0);
            }
            pvp_process_incoming_hits(&s->env, i);
            if (active && !s->env.players[i].veng_active)
                s->inventory_use[i].vengeance_consumed_tick = s->env.tick;
        }
        if (s->env.players[i].current_hitpoints <= 0) continue;
        pvp_step_player_movement(&s->env, i, ctx->route_topology, &ctx->routes[i]);
        pvp_resolve_same_tile(&s->env, s->env.pid_holder, 1 - s->env.pid_holder,
            ctx->route_topology);
        int idle_actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
        execute_attack_movement(&s->env, i, idle_actions, ctx->route_topology, &ctx->routes[i]);
        pvp_resolve_same_tile(&s->env, s->env.pid_holder, 1 - s->env.pid_holder,
            ctx->route_topology);
        if (!s->escaped[0] && !s->escaped[1]) {
            int maul_hits = pvp_maul_continue_attack(&s->env, i);
            riskfight_count_maul_spec(s, i, maul_hits);
            riskfight_record_attack(s, i, maul_hits, 1);
        }
        riskfight_attack(s, ctx, i);
        pvp_resolve_same_tile(&s->env, s->env.pid_holder, 1 - s->env.pid_holder,
            ctx->route_topology);
    }
    for (int i = 0; i < 2; i++) {
        Player* p = &s->env.players[i];
        if (p->food_timer > 0) p->food_timer--;
        if (p->potion_timer > 0) p->potion_timer--;
        if (p->karambwan_timer > 0) p->karambwan_timer--;
        osrs_player_inventory_tick(p, &s->inventory_use[i]);
        if (!s->env.episode_over) {
            int weapon = p->equipped[GEAR_SLOT_WEAPON];
            if (weapon == ITEM_ABYSSAL_TENTACLE) s->ticks_tentacle[i]++;
            else if (weapon == ITEM_DHAROKS_GREATAXE) s->ticks_axe[i]++;
            else if (weapon == ITEM_VOIDWAKER) s->ticks_voidwaker[i]++;
            else if (pvp_is_maul(weapon)) s->ticks_maul[i]++;
            int drained = p->current_attack < 110 || p->current_strength < 110 ||
                p->current_defence < p->base_defence + 11; /* the script's repot rule (riskfight_script_consume) */
            if (drained && p->potion_timer == 0) {
                int has_combat = 0;
                for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
                    OsrsConsumableKind k = (OsrsConsumableKind)
                        osrs_inventory_cell_metadata(&p->inventory_cells[slot])->consumable_kind;
                    if (k == OSRS_CONSUMABLE_SUPER_COMBAT || k == OSRS_CONSUMABLE_DIVINE_COMBAT) {
                        has_combat = 1;
                        break;
                    }
                }
                if (has_combat) s->combat_opp[i]++;
            }
            if (pvp_is_maul(weapon) && p->special_energy >= osrs_spec_cost(weapon)) s->maul_opp[i]++;
        }
    }
    for (int i = 0; i < 2; i++) {
        const Player* p = &s->env.players[i];
        s->anim_now[i] = p->attack_style_this_tick != ATTACK_STYLE_NONE || p->ate_food_this_tick || p->ate_karambwan_this_tick ||
            s->inventory_use[i].potion_animation_tick_plus_one == s->env.tick + 1 || p->hit_landed_this_tick || p->cast_veng_this_tick;
    }
    s->pid_played = s->env.pid_holder;
    s->env.tick++;
    pvp_tick_priority(&s->env);
    riskfight_finish(s);
    if (ctx->damage_reward_coeff != 0) {
        s->env.pvp_runtime.hit_observer = reward_observer.next;
        s->env.pvp_runtime.hit_observer_context = reward_observer.next_context;
    }
    s->env.pvp_runtime.attack_chance_observer = chance_observer.next;
    s->env.pvp_runtime.attack_chance_observer_context = chance_observer.next_context;
    for (int i = 0; i < 2; i++) {
        float chance_reward = ctx->chance_reward_coeff * (s->direct_ko_chance_mass[i] - previous_chance[i]);
        float damage_reward = ctx->damage_reward_coeff * reward_observer.direct_hitpoints_lost[i];
        float stake = s->rewards[i] + damage_reward + chance_reward;
        s->rewards[i] = stake * RF_REWARD_SCALE;
        s->episode_returns[i] += stake;
        s->damage_rewards[i] += damage_reward;
        s->chance_rewards[i] += chance_reward;
        riskfight_observe_visible(s, i, 0);
    }
}

/** The prayers the plugin keeps up for a deployed net (riskfight_deployable masks the prayer heads: the net never clicks one): the normal answer to being attacked is
 * to put on Smite and Piety, and the plugin does so 1 to 3 ticks (uniform) after the fight starts (puf_step holds them off until the step its draw names). Written
 * into the net's actions before they are stepped, as a refresh every tick from then on, which is what the plugin's "prayer on" amounts to for the sim: they drain
 * prayer as in the game (encounter_drain_all_prayers: Piety and Smite together,
 * shortened by the prayer bonus of the gear), go off when the bar runs dry (riskfight_execute_command refuses to set one with no prayer left) and come back on the
 * tick after a restore. Scripts keep their own prayers, so this is for the net's seat only (the callers that hold net actions). Before this the masked heads sent
 * action 0 every tick: Piety forced OFF, no overhead, so a deployed net trained against a script on Piety fought with no prayer at all, and the live O line
 * (piety 1, smite) was a state training never showed it. */
static void riskfight_plugin_prayers(int* actions) {
    actions[RF_PRAYER] = OFFENSIVE_PRAYER_PIETY;
    actions[RF_OVERHEAD] = ENCOUNTER_OVERHEAD_SET_REFRESH_SMITE;
}

/** The script playing `seat` on the opponent's profile; a Dharok pool opponent in seat 1 eats as spectated Dharok fighters do (RiskfightProfile.dharok_eating), as
 * the spectated fighter of his persona (RiskfightState.opponent_persona). */
static void riskfight_opponent_script(const RiskfightState* s, int seat, int* actions) {
    float obs[RF_OBS_SIZE];
    riskfight_write_script_observation(s, seat, obs);
    RiskfightProfile profile = *s->opponent_profile;
    profile.dharok_eating = seat == 1 && riskfight_dharok_opponent(s);
    profile.dharok_persona = profile.dharok_eating ? s->opponent_persona : 0;
    riskfight_script(obs, &profile, s->script_seed[seat], s->consume_ticks[seat], actions);
}

/** `actions`: the heads of seat 0, then of seat 1 when he is a second agent, on physical slots like riskfight_policy_commands (a net's go through
 * riskfight_bag_to_physical first: osrs_riskfight.h puf_step). */
static void riskfight_step(EncounterState* state, EncounterContext* context, const int* actions) {
    RiskfightState* s = (RiskfightState*)state;
    RiskfightContext* ctx = (RiskfightContext*)context;
    int opponent_actions[RF_HEADS];
    /* Seat 1 is a second agent (self-play) unless the script or OppFit plays him this fight; both read him as they would a lone opponent. */
    const int seat_agent = ctx->self_play && !ctx->opp_driven;
    if (!seat_agent) {
        if (ctx->opp_hook) ctx->opp_hook(ctx->opp_user, s, opponent_actions);
        else riskfight_opponent_script(s, 1, opponent_actions);
        if (ctx->opp_omit) memset(opponent_actions, 0, sizeof(opponent_actions));
        /* Over-eating is rude: once we have overeaten more than RF_OVEREAT_PATIENCE times this fight he has had enough and leaves within a few ticks. */
        if (!s->annoyed && s->overeats[0] > RF_OVEREAT_PATIENCE) {
            s->annoyed = 1;
            s->annoyed_leave_tick = s->env.tick + osrs_lowbias32(s->script_seed[1] ^ 0x616e6e6fu) % RF_ANNOYED_LEAVE_TICKS;
        }
        if (s->annoyed && s->env.tick >= s->annoyed_leave_tick) {
            memset(opponent_actions, 0, sizeof(opponent_actions));
            opponent_actions[RF_PRIMARY] = RF_TELEPORT;
        }
        /* A ragger has swung his share: he leaves. */
        if (s->ragger && s->ragger_swings >= s->ragger_limit) {
            memset(opponent_actions, 0, sizeof(opponent_actions));
            opponent_actions[RF_PRIMARY] = RF_TELEPORT;
        }
        if (s->opp_lag > 0) {
            for (int k = 3; k > 0; k--) memcpy(s->opp_ring[k], s->opp_ring[k - 1], sizeof(s->opp_ring[k]));
            memcpy(s->opp_ring[0], opponent_actions, sizeof(s->opp_ring[0]));
            memcpy(opponent_actions, s->opp_ring[s->opp_lag], sizeof(opponent_actions));
        }
    }
    /* The special toggles of a decision that wields a granite maul follow the maul's knockout gate (riskfight_spec_clicks), and a brew after the same decision's
     * food is dropped when that food lifts us to where the brew would reach base (riskfight_drink_click), for the seats a net plays. */
    int ours[RF_HEADS], his[RF_HEADS];
    memcpy(ours, actions, sizeof(ours));
    ours[RF_DRINK] = riskfight_drink_click(s, 0, ours);
#ifdef RF_SPEC_CLICKS_OVERRIDE /* a probe's diagnostic (probe_maul_chain.c): never defined in the env or the server */
    ours[RF_SPECIAL] = RF_SPEC_CLICKS_OVERRIDE(s, 0, ours);
#else
    ours[RF_SPECIAL] = riskfight_spec_clicks(s, 0, ours);
#endif
    /* A seat under data_escapes leaves when the scripts' rule says so: its mask was the no-op this tick, and the tab is clicked as a script clicks it. */
    const int our_escape = riskfight_data_escape(s, 0), his_escape = seat_agent && riskfight_data_escape(s, 1);
    if (our_escape) ours[RF_PRIMARY] = RF_TELEPORT;
    if (seat_agent) {
        memcpy(his, actions + RF_HEADS, sizeof(his));
        his[RF_SPECIAL] = riskfight_spec_clicks(s, 1, his);
        his[RF_DRINK] = riskfight_drink_click(s, 1, his);
        if (his_escape) his[RF_PRIMARY] = RF_TELEPORT;
    } else memcpy(his, opponent_actions, sizeof(his));
    riskfight_policy_commands(s, 0, ours, &ctx->policy_commands[0]);
    /* A net in seat 1 clicks through the same hand as in seat 0: one policy plays both seats, so both must see the same game. The scripts' escape lands
     * as theirs do. */
    if (!our_escape) riskfight_motor_filter(ctx, s, 0, &ctx->policy_commands[0]);
    riskfight_policy_commands(s, 1, his, &ctx->policy_commands[1]);
    if (seat_agent && !his_escape) riskfight_motor_filter(ctx, s, 1, &ctx->policy_commands[1]);
    riskfight_step_queues(s, ctx, &ctx->policy_commands[0].commands, &ctx->policy_commands[1].commands);
    if (ctx->opp_after) ctx->opp_after(ctx->opp_user, s);
}

static void riskfight_step_human(EncounterState* state, EncounterContext* context, HumanInput* hi) {
    RiskfightState* s = (RiskfightState*)state;
    RiskfightContext* ctx = (RiskfightContext*)context;
    int human = ctx->human_player;
    int actions[RF_HEADS];
    riskfight_opponent_script(s, 1 - human, actions);
    riskfight_policy_commands(s, 1 - human, actions, &ctx->policy_commands[1 - human]);
    riskfight_step_queues(s, ctx,
        human == 0 ? &hi->commands : &ctx->policy_commands[0].commands,
        human == 1 ? &hi->commands : &ctx->policy_commands[1].commands);
    human_input_clear_pending(hi);
}
#endif
