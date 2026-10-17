#ifndef OSRS_RISKFIGHT_SESSION_H
#define OSRS_RISKFIGHT_SESSION_H
#include "riskfight_step.h"

/* A session is a fixed budget of game ticks spent on fights and on the time between them. What follows a fight is a consequence in the world, not a reward term.
 * Every fight starts after a regear at the bank (the user's account: everyone regears): both sides full supplies, special energy 100, prayer and stats restored,
 * 121 hitpoints (an anglerfish overheal), nothing carried over; with fight_starts the start is instead one of the user's recorded ones (riskfight_start.h: who had
 * swung, hitpoints, his bar, specials, Vengeance, supplies used, distance). The time between two fights is one regear delay:
 *   rematch   after a teleport that was not rude, with probability RF_REMATCH_PROBABILITY: the same opponent (his kit, profile, price) after M, the measured rematch
 *             delay. A rude teleport (riskfight_norms.h: a side's own, unless both sides had spent their special or the teleporter had run out of a supply class)
 *             is never followed by a rematch, and neither is a fight against a ragger.
 *   otherwise a new opponent after R, the measured new-opponent delay, times (1 + search * rep): after a kill, a death, a teleport without a rematch, a
 *             timeout, a ragger leaving.
 *   timeout   a draw (reward 0, no price), counted in the logs; the session clock runs on.
 * Over-eating (the user's account): eating a meal while our hitpoints are above his chance range (riskfight_chance_range) annoys him. Past RF_OVEREAT_PATIENCE
 * overeats in a fight he teleports within RF_ANNOYED_LEAVE_TICKS, never rematches, and the exit costs a reputation point, like a rude teleport of ours. The patience is
 * just above the user's own recordings (p90 of overeats per fight, see RF_OVEREAT_PATIENCE); a script plays an annoyed opponent the same way whoever drives him.
 * Reputation (the user's account: how people treat him): a session-level `rep`. Each rude teleport of ours adds 1; it halves every half_life ticks. A session
 * starts with rep 0 in most cases and up to RF_REP_START_MAX in the rest, so the whole range is seen within a horizon. It lengthens the search for a new opponent
 * and, with probability min(RF_REP_RAG_CAP, rag * rep), the next new opponent is a ragger. search, rag and half_life are the env keys rep_search, rep_rag and
 * rep_half_life (RiskfightRepRules). They are not measurable: the defaults are chosen so that one rude teleport costs a visible share of a session (rep 1:
 * searches 1.5x, 15% of new opponents rag) and three cost most of it (searches 2.5x, 45% rag).
 * Ragger: a ranged kit of the pool (the sim does not cast, so a ragging mage is played as a ranger) played by the script, who swings RF_RAGGER_MIN_SWINGS to
 * RF_RAGGER_MAX_SWINGS times and teleports away. That fight is RISKFIGHT_RAGGED: no stake, and we pay the regear R like after any other end. He is fought as a normal
 * opponent: a kill still wins his gear and we can teleport out ourselves.
 * Both seats follow the rules of teleporting (a second agent's rude teleport is as rude as ours); only the clock and the reputation are the session's, seat 0's. */

/* Game ticks of an hour (0.6 s each): sessions are scored in kills minus price times deaths per hour. */
enum { RF_TICKS_PER_HOUR = 6000 };
enum { RF_DELAY_DECILES = 11 };
/** A delay in game ticks as its deciles (minimum, 10th percentile ... maximum). */
typedef struct { uint16_t ticks[RF_DELAY_DECILES]; } RiskfightDelay;

#include "riskfight_delays.h"

/* The user's account of the session world; none of these is measurable. The first three are the defaults of the env keys (RiskfightRepRules). */
enum { RF_REP_HALF_LIFE_TICKS = 1500 };       /* rep halves over half a session */
static const float RF_REP_SEARCH = 0.5f;       /* the search for a new opponent takes R * (1 + RF_REP_SEARCH * rep) */
static const float RF_REP_RAG = 0.15f;         /* probability that a new opponent is a ragger, per point of rep ... */
static const float RF_REP_RAG_CAP = 0.6f;      /* ... up to this */
static const float RF_REP_START_PROBABILITY = 0.3f; /* a session starts with a reputation in this share of the cases ... */
static const float RF_REP_START_MAX = 3.0f;    /* ... drawn uniformly from (0, RF_REP_START_MAX] */
enum { RF_RAGGER_MIN_SWINGS = 3, RF_RAGGER_MAX_SWINGS = 6 };

/** What the reputation costs: the search for a new opponent takes R * (1 + search * rep), a new opponent rags with min(RF_REP_RAG_CAP, rag * rep), and rep is
 * multiplied by decay every tick (2^(-1 / half_life)). */
typedef struct { float search, rag, decay; } RiskfightRepRules;

static inline RiskfightRepRules riskfight_rep_rules(float search, float rag, float half_life_ticks) {
    return (RiskfightRepRules){search, rag, exp2f(-1.0f / half_life_ticks)};
}

/** The reputation after one tick of decay. */
static inline float riskfight_rep_decay(const RiskfightRepRules* rules, float rep) {
    return rep * rules->decay;
}

/** The probability that a new opponent drawn at reputation `rep` is a ragger. */
static inline float riskfight_rag_probability(const RiskfightRepRules* rules, float rep) {
    return fminf(RF_REP_RAG_CAP, rules->rag * rep);
}

/** A delay in game ticks as its deciles (minimum, 10th percentile ... maximum); a draw interpolates linearly between them. */
static int riskfight_delay_draw(const RiskfightDelay* delay, uint32_t* rng) {
    float position = (RF_DELAY_DECILES - 1) * ((xorshift32(rng) >> 8) / 16777216.0f);
    int below = (int)position;
    float fraction = position - below;
    return (int)(delay->ticks[below] + fraction * (delay->ticks[below + 1] - delay->ticks[below]) + 0.5f);
}

/** A teleport of `side` that was not kosher (riskfight_norms.h), judged on the supplies of both sides at the tick it was clicked: the other side takes it as a rude
 * exit. Read at the end of the fight. The rule is the same in both seats, for a net, the script or OppFit. A rude exit of his changes nothing for us but the end
 * of the fight and the rematch: rude play makes the other player stop fighting the rude one (the user's account), so the pairing ends the same way whoever left, and
 * we pay the longer wait for a new opponent. The reputation is ours alone (how people treat us) and is not touched by what he does. */
static int riskfight_rude_teleport_of(const RiskfightState* s, int side) {
    return s->escaped[side] && !riskfight_teleport_kosher(&s->escape_supplies[side][side], &s->escape_supplies[side][1 - side]);
}
/** He left because we overate: he teleported after losing patience (riskfight_step.h), and a fight that ended some other way before that does not count. */
static int riskfight_annoyed_exit(const RiskfightState* s) { return s->annoyed && s->outcome[0] == RISKFIGHT_ESCAPE && s->escaped[1] && !s->escaped[0]; }
/** Our (seat 0) rude teleport: the logged one. */
static int riskfight_rude_teleport(const RiskfightState* s) { return riskfight_rude_teleport_of(s, 0); }

typedef struct {
    int delay;     /* ticks of the session spent before the next fight starts */
    int rematch;   /* the same opponent (his profile, price and kit) comes back */
    int rude;      /* a rude teleport by either side ended the fight: no rematch */
} RiskfightTransition;

/** What follows the fight that just ended, drawing the delay and the rematch from `rng`; `rep` is our reputation after this fight, priced by `rules`. */
static RiskfightTransition riskfight_session_transition(const RiskfightState* s, uint32_t* rng, float rep, const RiskfightRepRules* rules) {
    RiskfightTransition t = {0};
    if (s->outcome[0] == RISKFIGHT_ONGOING) abort();

    if (s->outcome[0] == RISKFIGHT_ESCAPE) {
        t.rude = riskfight_rude_teleport_of(s, 0) || riskfight_rude_teleport_of(s, 1);
        int rematch = encounter_rand_float(rng) < RF_REMATCH_PROBABILITY;
        t.rematch = rematch && !t.rude && !s->ragger && !s->annoyed;
    }
    t.delay = t.rematch ? riskfight_delay_draw(&RF_DELAY_REMATCH, rng) :
        (int)(riskfight_delay_draw(&RF_DELAY_RESTOCK, rng) * (1.0f + rules->search * rep) + 0.5f);
    return t;
}

#endif
