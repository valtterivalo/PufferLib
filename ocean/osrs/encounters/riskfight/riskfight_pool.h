#ifndef OSRS_RISKFIGHT_POOL_H
#define OSRS_RISKFIGHT_POOL_H
/* The opponent gear pool: the setups he can turn up in, with the weight each is drawn at. One table serves two readers: the training reset draws his kit from
 * it, and the threat features read their 'likely spec weapons' prior off it (riskfight_pool_prior), so the prior the policy is given is the distribution the
 * fights are drawn from. The table is filled once at load (riskfight_kits.h); this header holds the row type, the table and the prior, nothing else. */
#include "../../osrs_items.h"
#include "../../osrs_types.h"

enum { RF_POOL_MAX = 192, RF_KIT_WEAPONS = 5 };

typedef enum { RF_KIT_EMPIRICAL, RF_KIT_ARCHETYPE, RF_KIT_EDGE } RiskfightKitKind;

typedef struct {
    const char* name;
    const char* source;                  /* where the row comes from: the tapes/duels it was mined from, or the wiki/meta reason of an archetype */
    RiskfightKitKind kind;
    float weight;                        /* relative draw weight */
    uint8_t equipment[NUM_GEAR_SLOTS];   /* worn at the start; the weapon slot is the weapon he starts with, ring and ammunition included */
    uint8_t weapons[RF_KIT_WEAPONS];     /* every weapon of the kit, the started-with one first; ITEM_NONE past weapon_count */
    int weapon_count;
    int bag;                             /* RiskfightBag: what fills his inventory (riskfight_kits.h) */
} RiskfightKit;

static RiskfightKit RISKFIGHT_POOL[RF_POOL_MAX];
static int RISKFIGHT_POOL_COUNT;
static float RISKFIGHT_POOL_WEIGHT; /* sum of weights */

static void riskfight_pool_add(const RiskfightKit* kit) {
    if (RISKFIGHT_POOL_COUNT == RF_POOL_MAX) { fprintf(stderr, "riskfight pool full (%d): kit %s\n", RF_POOL_MAX, kit->name); abort(); }
    assert(kit->weight > 0 && kit->weapon_count >= 1 && kit->weapon_count <= RF_KIT_WEAPONS);
    RISKFIGHT_POOL[RISKFIGHT_POOL_COUNT++] = *kit;
    RISKFIGHT_POOL_WEIGHT += kit->weight;
}

/** Draws a kit by weight from a uniform roll in [0, 1). */
static const RiskfightKit* riskfight_pool_pick(float roll) {
    assert(RISKFIGHT_POOL_COUNT > 0 && roll >= 0 && roll < 1);
    float at = roll * RISKFIGHT_POOL_WEIGHT, sum = 0;
    for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++) {
        sum += RISKFIGHT_POOL[i].weight;
        if (at < sum) return &RISKFIGHT_POOL[i];
    }
    return &RISKFIGHT_POOL[RISKFIGHT_POOL_COUNT - 1]; /* roll * total rounded up to the total */
}

/* How much a kit counts as the setup in front of us: each visible armour slot that differs costs a factor, the wielded weapon must belong to the kit. */
static const float RF_POOL_SLOT_MISS = 0.2f, RF_POOL_WEAPON_MISS = 0.02f;
static const int RF_POOL_VISIBLE_SLOTS[] = {GEAR_SLOT_HEAD, GEAR_SLOT_CAPE, GEAR_SLOT_NECK, GEAR_SLOT_BODY, GEAR_SLOT_SHIELD, GEAR_SLOT_LEGS, GEAR_SLOT_HANDS, GEAR_SLOT_FEET};

/** The weapons of the pool besides the wielded one, with the probability each is in the kit of a fighter that looks like `visible`: the weight of every
 * kit, scaled by how well it matches what is worn, summed over the kits that hold the weapon. Writes at most `cap` (item, probability) pairs sorted by
 * probability, returns how many. */
static int riskfight_pool_prior(const uint8_t visible[NUM_GEAR_SLOTS], uint8_t* items, float* probability, int cap) {
    float mass[ITEM_NONE] = {0}, total = 0;
    uint8_t wielded = visible[GEAR_SLOT_WEAPON];
    for (int k = 0; k < RISKFIGHT_POOL_COUNT; k++) {
        const RiskfightKit* kit = &RISKFIGHT_POOL[k];
        float likelihood = kit->weight;
        for (size_t s = 0; s < sizeof(RF_POOL_VISIBLE_SLOTS) / sizeof(*RF_POOL_VISIBLE_SLOTS); s++)
            if (kit->equipment[RF_POOL_VISIBLE_SLOTS[s]] != visible[RF_POOL_VISIBLE_SLOTS[s]]) likelihood *= RF_POOL_SLOT_MISS;
        int holds_wielded = 0;
        for (int w = 0; w < kit->weapon_count; w++) holds_wielded |= kit->weapons[w] == wielded;
        if (!holds_wielded) likelihood *= RF_POOL_WEAPON_MISS;
        total += likelihood;
        for (int w = 0; w < kit->weapon_count; w++)
            if (kit->weapons[w] != wielded && kit->weapons[w] != ITEM_NONE) mass[kit->weapons[w]] += likelihood;
    }
    int count = 0;
    for (int pick = 0; pick < cap && total > 0; pick++) {
        int best = -1;
        for (int item = 0; item < ITEM_NONE; item++)
            if (mass[item] > 0 && (best < 0 || mass[item] > mass[best])) best = item;
        if (best < 0) break;
        items[count] = (uint8_t)best;
        probability[count++] = mass[best] / total;
        mass[best] = 0;
    }
    return count;
}
#endif
