#ifndef OSRS_RISKFIGHT_SESSION_H
#define OSRS_RISKFIGHT_SESSION_H
#include "riskfight_step.h"

/* A session is a fixed budget of game ticks spent on fights and on the time between them. What follows a fight is a consequence in the world, not a reward term:
 *   kill      we keep our supplies, a new opponent arrives after a search delay S
 *   death     the price is paid, we refill, a respawn and regear delay D passes, a new opponent
 *   teleport  the fight ends and we refill. Unless the teleport was rude the same opponent comes back for a rematch with probability RF_REMATCH_PROBABILITY,
 *             after M, with the supplies he had left. Otherwise (a rude teleport: ours after our own special, while he still had special energy for his, or no
 *             rematch) he is gone, and the next fight is with a new opponent after the restock delay R, which as measured (teleport to the next fight with a
 *             new opponent) already holds the search.
 *   his teleport, a timeout   he is gone (he refilled by teleporting), a new opponent after S, our supplies kept
 * Supplies are everything in the inventory, the worn gear, hitpoints, boosted levels, special energy (regenerating over the time passed) and Vengeance. */

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

/** Our teleport after our own special while he could still spec: the opponent takes it as a rude exit. Read at the end of the fight. */
static int riskfight_rude_teleport(const RiskfightState* s) {
    return s->escaped[0] && s->specs_used[0] > 0 && !osrs_escape_no_special(&s->escape_supplies[0][1]);
}

typedef struct {
    int delay;   /* ticks of the session spent before the next fight starts */
    int refill;  /* our side restocks: we died or teleported */
    int rematch; /* the same opponent, with the supplies he had left */
    int rude;
} RiskfightTransition;

/** What follows the fight that just ended (outcome of side 0), drawing the delays and the rematch from `rng`. */
static RiskfightTransition riskfight_session_transition(const RiskfightState* s, uint32_t* rng) {
    RiskfightTransition t = {0};
    switch (s->outcome[0]) {
        case RISKFIGHT_KILL:
        case RISKFIGHT_TIMEOUT:
            t.delay = riskfight_delay_draw(&RF_DELAY_SEARCH, rng);
            break;
        case RISKFIGHT_DEATH:
        case RISKFIGHT_MUTUAL_DEATH:
            t.refill = 1;
            t.delay = riskfight_delay_draw(&RF_DELAY_RESPAWN, rng);
            break;
        case RISKFIGHT_ESCAPE:
            if (s->escaped[0]) {
                t.refill = 1;
                t.rude = riskfight_rude_teleport(s);
                int rematch = encounter_rand_float(rng) < RF_REMATCH_PROBABILITY;
                t.rematch = rematch && !t.rude && !s->escaped[1];
                t.delay = riskfight_delay_draw(t.rematch ? &RF_DELAY_REMATCH : &RF_DELAY_RESTOCK, rng);
            } else t.delay = riskfight_delay_draw(&RF_DELAY_SEARCH, rng);
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

/** Puts a side's supplies back into a freshly reset fight and lets `idle_ticks` of the session pass over them: special energy regenerates, boosts and
 * overheal drift, the Vengeance cooldown runs. Positions, timers, queues and the fight's own state stay as the reset made them. */
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
    OsrsInventoryUseState* use = &s->inventory_use[side];
    use->vengeance_sacks = carry->use.vengeance_sacks;
    use->divine_combat_ticks = carry->use.divine_combat_ticks;
    use->stat_drift_timer = carry->use.stat_drift_timer;
    for (int tick = 0; tick < idle_ticks; tick++) {
        if (p->veng_cooldown > 0) p->veng_cooldown--;
        if (p->spec_regen_active && p->special_energy < 100) osrs_tick_special_regen(p);
        else p->item_effect_state.special_regen_ticks = 0;
        osrs_player_inventory_tick(p, use);
    }
}

/** Starts the next fight of a session from a reset state: the sides the transition keeps get their supplies back, aged by the delay. The estimate each
 * side holds of the other's special energy follows the other side's supplies, aged the same way. Observations of the gear are retaken. */
static void riskfight_session_begin_fight(RiskfightState* s, const RiskfightCarry carry[2], const RiskfightTransition* t) {
    int keep[2] = {!t->refill, t->rematch};
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
