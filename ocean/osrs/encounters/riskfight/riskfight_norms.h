#ifndef OSRS_RISKFIGHT_NORMS_H
#define OSRS_RISKFIGHT_NORMS_H
/* The norms of teleporting, as the user states them (FINDINGS.md, 2026-10-17). A teleport is KOSHER or RUDE. A rude one costs what riskfight_session.h says: no
 * rematch with the opponent, and +1 reputation when it is ours.
 *
 *   kosher  the fight has run its course. Either
 *             (a) both players have spent their special: neither has the energy for the cheapest special weapon he carries, so nobody leaves with a special
 *                 unused and nobody was cut off from using his, or
 *             (b) the teleporter has run out of a supply class: no hard food, no prayer restore doses, or no combat boost left after brewing down
 *                 (osrs_escape_no_hard_food, osrs_escape_no_restore, osrs_escape_no_boost: the classes are defined from the inventory kinds in osrs_pvp_escape.h).
 *             The supply case holds whatever the opponent has left: a player who cannot keep eating, keep his prayer up or keep his boost cannot be asked to stay.
 *             Kosher is not advisable: the user's account is that leaving is "still not always" right, since a good kill chance may remain.
 *   rude    every other teleport, among them:
 *             - right after a normal opening, before anyone specced ("raises a ton of question marks; there is absolutely no point"),
 *             - after our own special while he still has the energy for his (the old definition of rude, kept: it is the case (a) fails on his side),
 *             - with our own special unused while he has spent his (a fails on ours).
 *
 * The user's account of the opposite behaviour: the normal answer to being attacked is to put on Smite and Piety and fight back, and rude play makes the other player
 * stop fighting us and, repeated, hurts our name: fights become hard to find. Both seats are bound by the same norms (a second agent's, a script's and OppFit's rude
 * teleport is as rude as ours); only the clock and the reputation are the session's, seat 0's (riskfight_session.h).
 *
 * Two readers, one rule:
 *   the WORLD (riskfight_teleport_kosher): classifies a teleport that happened, from the true supplies of both sides at that tick (riskfight_step.h snapshots them).
 *   the PLAYER (riskfight_teleport_kosher_now): the policy's observation of whether a teleport now would be kosher, from what a player knows: his own bag, his own
 *     special energy, and an estimate of the other side's. His is the special energy counted from the specials seen (RiskfightVisibleOpponent.spec_est) against the
 *     cheapest special among the weapons the threat block says he wields, has swung, or likely carries (the pool prior, at least RF_KOSHER_LIKELY). When none is
 *     known or likely, a player still assumes he carries one (nearly every fighter does) at the commonest cost, RF_KOSHER_ASSUMED_SPEC (50): a teleport after our
 *     specials is then kosher only once his estimate is below 50. Where the reading misses or invents a weapon the two disagree, and the world is the one that counts. */
#include "../../osrs_pvp_escape.h"
#include "riskfight_threat.h"

/* A weapon the pool says he may carry counts as "likely" at or above this probability: more likely than not. Weapons he wields or has swung are certain (p 1). */
static const float RF_KOSHER_LIKELY = 0.5f;
/* The special a player assumes the other side carries when none of his is known or likely: the commonest cost. */
enum { RF_KOSHER_ASSUMED_SPEC = 50 };

/** Kosher given our own supplies and whether the other side could still special (see the rule above). */
static inline int riskfight_teleport_kosher_given(const OsrsEscapeSupplies* own, int other_could_special) {
    const int specials_spent = osrs_escape_no_special(own) && !other_could_special;
    return specials_spent || osrs_escape_no_hard_food(own) || osrs_escape_no_restore(own) || osrs_escape_no_boost(own);
}

/** The world's classification of a teleport of the side holding `own`: true supplies of both sides at the tick it was clicked. */
static inline int riskfight_teleport_kosher(const OsrsEscapeSupplies* own, const OsrsEscapeSupplies* other) {
    return riskfight_teleport_kosher_given(own, !osrs_escape_no_special(other));
}

/** The cheapest special of the weapons the threat block `threat` (his side, RF_THREAT_HIS_SLOTS slots) holds as known or likely, in energy; 0 when none has a special
 * that costs energy (a Behead costs none). */
static inline int riskfight_threat_cheapest_spec(const float* threat) {
    int cheapest = 0;
    for (int i = 0; i < RF_THREAT_HIS_SLOTS; i++) {
        const float* o = threat + i * RF_THREAT_HIS_WIDTH;
        if (o[0] < RF_KOSHER_LIKELY) continue;
        int cost = (int)lroundf(o[8] * 100);
        if (cost > 0 && (!cheapest || cost < cheapest)) cheapest = cost;
    }
    return cheapest;
}

/** The player's reading: would a teleport of `me` now be kosher, with his special energy estimated at `spec_est` and his weapons in the threat block `threat`. With no
 * special of his known or likely, he is assumed to carry one costing RF_KOSHER_ASSUMED_SPEC. */
static inline int riskfight_teleport_kosher_now(const Player* me, const float* threat, int spec_est) {
    const OsrsEscapeSupplies own = osrs_escape_supplies(me);
    const int known = riskfight_threat_cheapest_spec(threat);
    const int cheapest = known ? known : RF_KOSHER_ASSUMED_SPEC;
    return riskfight_teleport_kosher_given(&own, spec_est >= cheapest);
}
#endif
