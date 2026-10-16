#ifndef OSRS_RISKFIGHT_SESSION_H
#define OSRS_RISKFIGHT_SESSION_H
#include "riskfight_step.h"

/* A session is a fixed budget of game ticks spent on fights and on the time between them. What follows a fight is a consequence in the world, not a reward term:
 *   kill      we keep our supplies, a new opponent arrives after a search delay S
 *   death     the price is paid, we refill, a respawn and regear delay D passes, a new opponent
 *   teleport  the fight ends and the side that left refills. Unless the teleport was rude (a side's own, after its own special, while the other still had
 *             special energy for his) the pair meets again with probability RF_REMATCH_PROBABILITY, after M, each side with the supplies it had left (the one
 *             that teleported refilled). Otherwise (a rude teleport, or no rematch) he is gone and the next fight is with a new opponent: after the restock
 *             delay R when we left, which as measured (teleport to the next fight with a new opponent) already holds the search, after S when he left.
 *   a timeout he is gone, a new opponent after S, our supplies kept
 * Both seats follow these rules (a second agent's teleport is as rude as ours); only the clock is ours, as the session is seat 0's.
 * Supplies are everything in the inventory, the worn gear, boosted levels, special energy (regenerating over the time passed) and Vengeance. Hitpoints
 * persist as what the player eats back during the wait (riskfight_idle_eat), at the cost of the food. */

/* Game ticks of an hour (0.6 s each): sessions are scored in kills minus price times deaths per hour. */
enum { RF_TICKS_PER_HOUR = 6000 };
enum { RF_DELAY_DECILES = 11 };
/** A delay in game ticks as its deciles (minimum, 10th percentile ... maximum); a draw interpolates linearly between them. */
typedef struct { uint16_t ticks[RF_DELAY_DECILES]; } RiskfightDelay;

#include "riskfight_delays.h"

static int riskfight_delay_draw(const RiskfightDelay* delay, uint32_t* rng) {
    float position = (RF_DELAY_DECILES - 1) * ((xorshift32(rng) >> 8) / 16777216.0f);
    int below = (int)position;
    float fraction = position - below;
    return (int)(delay->ticks[below] + fraction * (delay->ticks[below + 1] - delay->ticks[below]) + 0.5f);
}

/** A teleport of `side` after its own special while the other side could still spec: the other takes it as a rude exit. Read at the end of the fight. */
static int riskfight_rude_teleport_of(const RiskfightState* s, int side) {
    return s->escaped[side] && s->specs_used[side] > 0 && !osrs_escape_no_special(&s->escape_supplies[side][1 - side]);
}
/** Our (seat 0) rude teleport: the logged one. */
static int riskfight_rude_teleport(const RiskfightState* s) { return riskfight_rude_teleport_of(s, 0); }

typedef struct {
    int delay;     /* ticks of the session spent before the next fight starts */
    int refill[2]; /* the side restocks: it died or teleported */
    int rematch;   /* the same opponent (his profile, price and kit) comes back */
    int rude;      /* a rude teleport by either side ended the fight: no rematch */
} RiskfightTransition;

/** What follows the fight that just ended, drawing the delays and the rematch from `rng`. The rules treat the seats alike: whoever leaves rudely, or dies, or
 * teleports, is restocked, and a pair that parted politely meets again with probability RF_REMATCH_PROBABILITY, each side carrying what it had left (the
 * side that restocked comes back refilled). The clock is the session's: it is seat 0's, the one that carries the session, so the delay is the one seat 0
 * waits (a new opponent replaces seat 1, he has no session of his own to wait out). */
static RiskfightTransition riskfight_session_transition(const RiskfightState* s, uint32_t* rng) {
    RiskfightTransition t = {0};
    switch (s->outcome[0]) {
        case RISKFIGHT_KILL:
        case RISKFIGHT_TIMEOUT:
            t.delay = riskfight_delay_draw(&RF_DELAY_SEARCH, rng);
            break;
        case RISKFIGHT_DEATH:
        case RISKFIGHT_MUTUAL_DEATH:
            t.refill[0] = 1;
            t.delay = riskfight_delay_draw(&RF_DELAY_RESPAWN, rng);
            break;
        case RISKFIGHT_ESCAPE:
            for (int side = 0; side < 2; side++) t.refill[side] = s->escaped[side];
            t.rude = riskfight_rude_teleport_of(s, 0) || riskfight_rude_teleport_of(s, 1);
            int rematch = encounter_rand_float(rng) < RF_REMATCH_PROBABILITY;
            t.rematch = rematch && !t.rude;
            t.delay = riskfight_delay_draw(t.rematch ? &RF_DELAY_REMATCH : s->escaped[0] ? &RF_DELAY_RESTOCK : &RF_DELAY_SEARCH, rng);
            break;
        case RISKFIGHT_ONGOING:
            abort();
    }
    return t;
}

/** One side as the fight left it. */
typedef struct {
    Player player;
    OsrsInventoryUseState use;
    int seen_spec_est, seen_spec_phase; /* the other side's estimate of this side's special energy */
} RiskfightCarry;

static void riskfight_carry_save(const RiskfightState* s, int side, RiskfightCarry* carry) {
    carry->player = s->env.players[side];
    carry->use = s->inventory_use[side];
    carry->seen_spec_est = s->visible[1 - side].spec_est;
    carry->seen_spec_phase = s->visible[1 - side].spec_regen_phase;
}

/** Between fights a player eats back up to his full hitpoints: his bar is full again in 69% of the measured rematches (55 pairs), he eats in 98% of the gaps
 * (41 pairs inside one recording), and we ate in the gaps where we did not restock (scripts/measure_session_delays.py, section J). The food he eats is gone,
 * so what persists is what the session has spent; hitpoints do not carry a fight's damage over a gap. */
static void riskfight_idle_eat(Player* p, OsrsInventoryUseState* use, const OsrsTeleportState* teleport, int tick) {
    if (p->current_hitpoints >= p->base_hitpoints || p->food_timer > 0) return;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        const OsrsItemContentMetadata* meta = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
        if (meta->click_action != OSRS_CLICK_EAT || !osrs_can_eat_consumable_kind(p, (OsrsConsumableKind)meta->consumable_kind)) continue;
        if (osrs_player_use_inventory_with_food_clock(p, use, teleport, slot, tick, OSRS_FOOD_CONTINUOUS_CLOCK) == OSRS_INVENTORY_USE_CONSUMED) return;
    }
}

/** Puts a side's supplies back into a freshly reset fight and lets `idle_ticks` of the session pass over them: special energy regenerates, boosts and
 * overheal drift, the Vengeance cooldown runs, he eats (riskfight_idle_eat). Positions, timers, queues and the fight's own state stay as the reset made them. */
static void riskfight_carry_restore(RiskfightState* s, int side, const RiskfightCarry* carry, int idle_ticks) {
    Player* p = &s->env.players[side];
    const Player* kept = &carry->player;
    memcpy(p->inventory_cells, kept->inventory_cells, sizeof(p->inventory_cells));
    memcpy(p->equipped, kept->equipped, sizeof(p->equipped));
    p->current_hitpoints = kept->current_hitpoints;
    p->current_attack = kept->current_attack;
    p->current_strength = kept->current_strength;
    p->current_defence = kept->current_defence;
    p->current_ranged = kept->current_ranged;
    p->current_magic = kept->current_magic;
    p->current_prayer = kept->current_prayer;
    p->special_energy = kept->special_energy;
    p->spec_regen_active = kept->spec_regen_active;
    p->veng_active = kept->veng_active;
    p->veng_cooldown = kept->veng_cooldown;
    osrs_refresh_player_equipment(p);
    pvp_refresh_visible_gear(p);
    p->item_effect_state = kept->item_effect_state;
    const int has_attack_timer = p->has_attack_timer, attack_timer = p->attack_timer, attack_timer_uncapped = p->attack_timer_uncapped;
    OsrsInventoryUseState* use = &s->inventory_use[side];
    use->vengeance_sacks = carry->use.vengeance_sacks;
    use->divine_combat_ticks = carry->use.divine_combat_ticks;
    use->stat_drift_timer = carry->use.stat_drift_timer;
    for (int tick = 0; tick < idle_ticks; tick++) {
        if (p->food_timer > 0) p->food_timer--;
        if (p->potion_timer > 0) p->potion_timer--;
        if (p->karambwan_timer > 0) p->karambwan_timer--;
        if (p->veng_cooldown > 0) p->veng_cooldown--;
        if (p->spec_regen_active && p->special_energy < 100) osrs_tick_special_regen(p);
        else p->item_effect_state.special_regen_ticks = 0;
        osrs_player_inventory_tick(p, use);
        riskfight_idle_eat(p, use, &s->env.pvp_runtime.teleport[side], tick - idle_ticks);
    }
    /* The wait is over: nothing of it lingers into tick 0 of the fight. */
    p->food_timer = p->potion_timer = p->karambwan_timer = 0;
    p->has_attack_timer = has_attack_timer;
    p->attack_timer = attack_timer;
    p->attack_timer_uncapped = attack_timer_uncapped;
    use->potion_animation_tick_plus_one = 0;
    reset_tick_flags(p);
}

/** Starts the next fight of a session from a reset state: the sides the transition keeps get their supplies back, aged by the delay. The estimate each
 * side holds of the other's special energy follows the other side's supplies, aged the same way. Observations of the gear are retaken. */
static void riskfight_session_begin_fight(RiskfightState* s, const RiskfightCarry carry[2], const RiskfightTransition* t) {
    int keep[2] = {!t->refill[0], t->rematch && !t->refill[1]};
    for (int side = 0; side < 2; side++)
        if (keep[side]) riskfight_carry_restore(s, side, &carry[side], t->delay);
    riskfight_observe_visible(s, 0, 1);
    riskfight_observe_visible(s, 1, 1);
    for (int side = 0; side < 2; side++) {
        if (!keep[side]) continue;
        RiskfightVisibleOpponent* seen = &s->visible[1 - side];
        seen->spec_est = carry[side].seen_spec_est;
        seen->spec_regen_phase = carry[side].seen_spec_phase;
        for (int tick = 0; tick < t->delay; tick++) riskfight_spec_est_regen(seen);
    }
}
#endif
