#ifndef OSRS_RISKFIGHT_START_H
#define OSRS_RISKFIGHT_START_H
#include <stdint.h>
#include "riskfight_step.h"
#include "riskfight_starts_data.h"

/* How a fight starts. The regear at the bank (riskfight_session.h) leaves both sides whole and before any swing; the user's real fights do not start like that:
 * measured over every recorded fight (scripts/measure_fight_starts.py, fight_starts.tsv), the state at the end of the first tick on which either side swings has us at 121 hitpoints in
 * 63 of 161 fights, at 120 in 59 and lower in the rest, one side or the other already swinging, his bar showing, a special used, Vengeance gone, supplies used.
 * A start is one of those recorded rows, drawn whole (so the correlations of a real start survive: who opened, what it cost, what had been used) from the rows of a rematch
 * or of a first meeting (the two differ, see the report), and mapped onto the kit drawn for this fight:
 *   - the opening swings are played by the sim itself on the regeared state (the swing's own timers, special cost, the threat and health-bar bookkeeping of both
 *     seats), then the measured state replaces the outcome: hitpoints, boosted levels, prayer, special energy, Vengeance and its cooldown, his health bar, the hits
 *     that landed on the first tick (an overheal lock, a landed flag in the history) and those still in flight (a swing of the first tick that lands the next one, the
 *     Vengeance and recoil echoes of the first hits, queued with their measured final damage);
 *   - our supplies are the kit less what the user had used up (deficits against the most his recording held, capped by what the kit carries), and the Vengeance sacks he had left;
 *   - his hitpoints are inside the range his bar shows (a hidden or full bar: the hitpoints a regeared player starts a fight at, taken from our own rows that were not hit;
 *     a kit that starts hurt keeps its hitpoints),
 *     his special energy and Vengeance are the tape's estimates, his supplies and levels are the kit's and the domain randomization's (not observed);
 *   - the tiles between us are the measured distance, east of the player in the role of the user.
 * Roles: the seat that is us in a row is seat 0, except in a self-play env whose seat 1 is a net, where it is either seat with equal probability (both seats are treated alike). */

/** Clears what a prefix of steps earned. */
static void riskfight_clear_returns(RiskfightState* state) {
    memset(state->rewards, 0, sizeof(state->rewards));
    memset(state->episode_returns, 0, sizeof(state->episode_returns));
    memset(state->damage_rewards, 0, sizeof(state->damage_rewards));
    memset(state->direct_ko_chance_mass, 0, sizeof(state->direct_ko_chance_mass));
    memset(state->chance_rewards, 0, sizeof(state->chance_rewards));
}

/** The row of a fight against a rematch opponent or a new one, drawn uniformly. */
static const RiskfightStartRecord* riskfight_start_draw(uint32_t* rng, int rematch) {
    int count = 0;
    for (int i = 0; i < RF_START_RECORD_COUNT; i++) count += RF_START_RECORDS[i].rematch == rematch;
    assert(count > 0);
    int pick = encounter_rand_int(rng, count);
    for (int i = 0; i < RF_START_RECORD_COUNT; i++)
        if (RF_START_RECORDS[i].rematch == rematch && pick-- == 0) return &RF_START_RECORDS[i];
    abort();
}

/** The user's hitpoints at the start of a fight he was not hit in: what a regeared player starts at (121 or 120, and a few lower); his own hitpoints when his bar hides them. */
static int riskfight_start_regear_hitpoints(uint32_t* rng) {
    int count = 0;
    for (int i = 0; i < RF_START_RECORD_COUNT; i++) count += RF_START_RECORDS[i].landed_us < 0 && RF_START_RECORDS[i].fl_us_direct < 0 && RF_START_RECORDS[i].hp >= 99;
    assert(count > 0);
    int pick = encounter_rand_int(rng, count);
    for (int i = 0; i < RF_START_RECORD_COUNT; i++)
        if (RF_START_RECORDS[i].landed_us < 0 && RF_START_RECORDS[i].fl_us_direct < 0 && RF_START_RECORDS[i].hp >= 99 && pick-- == 0) return RF_START_RECORDS[i].hp;
    abort();
}

/* A Dharok pool opponent's hitpoints where the row shows no bar (riskfight_dharok_opponent): spectated Dharok fighters (scripts/measure_dharok_eating.py, 512
 * fighters of the 261 Dharok v Dharok duels) show a full bar on the first read in 339 (a regear: the hits before the bar first drops put most at 115-121), and
 * below it, by tens of hitpoints from 0-9 to 90-98, RF_START_DHAROK_LOW (the bar plus the damage of that tick). A row with a full bar keeps a regear's
 * hitpoints (99 or more, as the bar says); with no bar the whole measured distribution applies and the bar he shows follows it. */
enum { RF_START_DHAROK_FULL = 339 };
static const uint8_t RF_START_DHAROK_LOW[10] = {2, 0, 3, 4, 6, 5, 17, 39, 26, 71};

/** A Dharok pool opponent's start hitpoints: a regear's in the measured share, else a ten of RF_START_DHAROK_LOW by its count, uniform inside it (1-9 for the
 * first, 90-98 for the last). */
static int riskfight_start_dharok_hitpoints(uint32_t* rng) {
    int low = 0;
    for (int i = 0; i < 10; i++) low += RF_START_DHAROK_LOW[i];
    int pick = encounter_rand_int(rng, RF_START_DHAROK_FULL + low);
    if (pick < RF_START_DHAROK_FULL) return riskfight_start_regear_hitpoints(rng);
    pick -= RF_START_DHAROK_FULL;
    for (int i = 0; i < 10; i++) {
        if (pick < RF_START_DHAROK_LOW[i]) return i == 0 ? 1 + encounter_rand_int(rng, 9) : 10 * i + encounter_rand_int(rng, i == 9 ? 9 : 10);
        pick -= RF_START_DHAROK_LOW[i];
    }
    abort();
}

/** Takes `count` items of `kind` out of the bag, from the last slot back (an eaten fish or karambwan); at most what the bag holds. */
static void riskfight_start_use_items(Player* p, OsrsConsumableKind kind, int count) {
    for (int slot = OSRS_INVENTORY_SIZE - 1; slot >= 0 && count > 0; slot--)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[slot])->consumable_kind == kind) {
            p->inventory_cells[slot] = osrs_inventory_cell_empty();
            count--;
        }
}

/** Drinks `doses` doses of `kind` out of the bag, from the last slot back; at most what the bag holds. */
static void riskfight_start_use_doses(Player* p, OsrsConsumableKind kind, int doses) {
    for (int slot = OSRS_INVENTORY_SIZE - 1; slot >= 0 && doses > 0; slot--) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
        if (m->consumable_kind != kind) continue;
        int taken = doses < m->dose_count ? doses : m->dose_count;
        int left = m->dose_count - taken;
        p->inventory_cells[slot] = left > 0 ? osrs_inventory_cell_from_content_code(riskfight_supply_code(kind, (uint8_t)left)) : osrs_inventory_cell_empty();
        doses -= taken;
    }
}

/** A hit of `source` on `target` that lands on the next tick: a swing's hit with its measured final damage, or the Vengeance or recoil echo of a hit already landed. */
static void riskfight_start_queue_hit(RiskfightState* s, int source, int target, int damage, OsrsHitKind kind) {
    Player* attacker = &s->env.players[source];
    Player* defender = &s->env.players[target];
    AttackStyle style = kind != OSRS_HIT_DIRECT ? ATTACK_STYLE_NONE :
        get_slot_weapon_attack_style(attacker) == ATTACK_STYLE_RANGED ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE;
    queue_hit(&s->env, source, target, attacker, defender, damage, style, 0, 0, damage > 0 || kind != OSRS_HIT_DIRECT, 0, 0, 0, 0, 0);
    attacker->pending_hits[attacker->num_pending_hits - 1].kind = kind;
    if (kind == OSRS_HIT_DIRECT) s->env.pending_hit_metadata[source][attacker->num_pending_hits - 1].damage_mode = 1;
}

/** Plays the opening swings of the row on the regeared state: seat `us` swings when the row says we did, seat `him` likewise, a special when the swing was one and the weapon has one. */
static void riskfight_start_swings(RiskfightState* s, RiskfightContext* ctx, const RiskfightStartRecord* r, int us) {
    const int swing[2] = {r->opener_us, r->opener_him}, spec[2] = {r->spec_us, r->spec_him};
    /* Adjacent for the swings, whatever tile the kit starts him on (a ranger starts at range): the measured distance is applied after them. */
    Player* mine = &s->env.players[us];
    Player* his = &s->env.players[1 - us];
    his->x = mine->x + 1;
    his->y = mine->y;
    his->dest_x = his->x;
    his->dest_y = his->y;
    HumanInput* inputs[2] = {&ctx->policy_commands[0], &ctx->policy_commands[1]};
    for (int seat = 0; seat < 2; seat++) {
        HumanInput* hi = inputs[seat];
        human_input_clear_pending(hi);
        human_input_clear_commands(hi);
        const int role = seat == us ? 0 : 1;
        if (!swing[role]) continue;
        if (spec[role] && osrs_spec_cost(s->env.players[seat].equipped[GEAR_SLOT_WEAPON]) > 0)
            human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE});
        human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1 - seat});
    }
    riskfight_step_queues(s, ctx, &inputs[0]->commands, &inputs[1]->commands);
    for (int seat = 0; seat < 2; seat++) {
        const int role = seat == us ? 0 : 1;
        if (!swing[role]) continue;
        if (s->visible[1 - seat].last_attack_tick != 0 && s->specs_used[seat] == 0) {
            const Player* p = &s->env.players[seat];
            fprintf(stderr, "riskfight start: seat %d did not swing (weapon %d, distance %d, attack timer %d/%d, interacting %d, hitpoints %d, spec %d)\n", seat, p->equipped[GEAR_SLOT_WEAPON],
                chebyshev_distance(s->env.players[0].x, s->env.players[0].y, s->env.players[1].x, s->env.players[1].y), p->has_attack_timer, p->attack_timer,
                osrs_interaction_active(&p->interaction), p->current_hitpoints, spec[role]);
            abort();
        }
    }
    for (int seat = 0; seat < 2; seat++) {
        human_input_clear_pending(inputs[seat]);
        human_input_clear_commands(inputs[seat]);
    }
}

/** Puts the fight just reset (and randomized) into the start state of row `r`, seat `us` playing the user's part; `rng` draws his hitpoints inside his bar. */
static void riskfight_start_apply(RiskfightState* s, RiskfightContext* ctx, const RiskfightStartRecord* r, int us, uint32_t* rng) {
    const int him = 1 - us;
    assert(s->env.tick == 0 && (us == 0 || us == 1));
    Player* mine = &s->env.players[us];
    Player* his = &s->env.players[him];
    /* The swings are rolled on whole players, so that no roll ends the fight before its measured state replaces them (a kit that starts at 22 hitpoints dies to our first hit). */
    const int kit_hitpoints = his->current_hitpoints;
    mine->current_hitpoints = his->current_hitpoints = 121;
    riskfight_start_swings(s, ctx, r, us);

    /* What the first tick left in flight is the row's, not the rolls of the swings above. */
    for (int seat = 0; seat < 2; seat++) {
        s->env.players[seat].num_pending_hits = 0;
        memset(s->env.pending_hit_metadata[seat], 0, sizeof(s->env.pending_hit_metadata[seat]));
    }

    mine->current_hitpoints = r->hp;
    mine->current_attack = r->atk;
    mine->current_strength = r->str;
    mine->current_defence = r->df;
    mine->current_magic = r->mag;
    mine->current_prayer = r->pray;
    if (r->atk < 118 || r->str < 118 || r->df < 118) s->inventory_use[us].divine_combat_ticks = 0;
    mine->special_energy = r->e_spec;
    mine->spec_regen_active = r->e_spec < 100;
    mine->item_effect_state.special_regen_ticks = r->e_phase;
    mine->veng_active = r->veng;
    mine->veng_cooldown = r->vcd;
    s->inventory_use[us].vengeance_sacks = r->sacks;
    riskfight_start_use_items(mine, OSRS_CONSUMABLE_ANGLERFISH, r->d_angler);
    riskfight_start_use_items(mine, OSRS_CONSUMABLE_KARAMBWAN, r->d_karam);
    riskfight_start_use_doses(mine, OSRS_CONSUMABLE_BREW, r->d_brew);
    riskfight_start_use_doses(mine, OSRS_CONSUMABLE_SUPER_RESTORE, r->d_restore);
    riskfight_start_use_doses(mine, OSRS_CONSUMABLE_SUPER_COMBAT, r->d_combat);
    riskfight_start_use_items(mine, OSRS_CONSUMABLE_TELEPORT, r->d_tab);

    OsrsHealthBarRange range = osrs_health_bar_range(r->bar, OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 121);
    if (kit_hitpoints < 99) his->current_hitpoints = kit_hitpoints; /* a kit that starts hurt (the pool's Dharok's at 22) keeps it: that is the kit, not a regear */
    else if (range.kind == OSRS_HEALTH_BAR_MISSING && him == 1 && riskfight_dharok_opponent(s)) his->current_hitpoints = riskfight_start_dharok_hitpoints(rng);
    else if (range.kind == OSRS_HEALTH_BAR_MISSING || (range.kind == OSRS_HEALTH_BAR_KNOWN && range.upper == 121))
        his->current_hitpoints = riskfight_start_regear_hitpoints(rng);
    else {
        assert(range.kind == OSRS_HEALTH_BAR_KNOWN);
        his->current_hitpoints = range.lower + encounter_rand_int(rng, range.upper - range.lower + 1);
    }
    his->special_energy = r->h_spec;
    his->spec_regen_active = r->h_spec < 100;
    his->item_effect_state.special_regen_ticks = r->h_phase;
    his->veng_active = r->h_veng;
    his->veng_cooldown = 0;

    s->visible[him].spec_est = r->e_spec;
    s->visible[him].spec_regen_phase = r->e_phase;
    s->visible[us].spec_est = r->h_spec;
    s->visible[us].spec_regen_phase = r->h_phase;

    const int landed[2] = {r->landed_us, r->landed_him};
    for (int role = 0; role < 2; role++) {
        const int seat = role == 0 ? us : him;
        Player* p = &s->env.players[seat];
        p->hit_landed_this_tick = landed[role] >= 0;
        p->hit_damage = landed[role] >= 0 ? landed[role] : 0;
        p->pvp_overheal_block = landed[role] >= 0 ? OSRS_PVP_OVERHEAL_BLOCK_TICKS : 0;
        RiskfightVisibleOpponent* viewer = &s->visible[1 - seat];
        viewer->health_bar = osrs_health_bar_ratio(p->current_hitpoints, p->base_hitpoints, OSRS_PLAYER_HEALTH_BAR_SCALE);
        float* event = viewer->events[0];
        event[3] = (float)p->hit_landed_this_tick;
        event[4] = (float)p->hit_damage;
        event[7] = (float)viewer->health_bar;
    }

    const struct { int role, direct, veng, recoil; } flight[2] = {
        {0, r->fl_us_direct, r->fl_us_veng, r->fl_us_recoil}, {1, r->fl_him_direct, r->fl_him_veng, r->fl_him_recoil}};
    for (int i = 0; i < 2; i++) {
        const int target = flight[i].role == 0 ? us : him;
        if (flight[i].direct >= 0) riskfight_start_queue_hit(s, 1 - target, target, flight[i].direct, OSRS_HIT_DIRECT);
        if (flight[i].veng >= 0) riskfight_start_queue_hit(s, 1 - target, target, flight[i].veng, OSRS_HIT_VENGEANCE);
        if (flight[i].recoil >= 0) riskfight_start_queue_hit(s, 1 - target, target, flight[i].recoil, OSRS_HIT_RECOIL);
    }

    his->x = mine->x + r->dist;
    his->y = mine->y;
    his->dest_x = his->x;
    his->dest_y = his->y;
    s->visible[us].x = his->x;
    s->visible[us].y = his->y;
    riskfight_clear_returns(s);
}

#endif
