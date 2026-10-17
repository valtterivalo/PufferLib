#ifndef OSRS_RISKFIGHT_BAG_H
#define OSRS_RISKFIGHT_BAG_H
#include "riskfight_model.h"

/** The bag as the net sees it. A slot's position means nothing in OSRS (the user keeps restores in 4 and 8, a kit puts its food first), so the net is shown the bag in
 * a canonical order and its slot actions (the gear heads and food, drink, combo: action k picks entry k - 1) index that order. The order sorts the cells by content
 * code (an item, or a consumable at its dose: the code fixes both), empty cells last, the physical slot breaking ties. Two bags holding the same items in different
 * slots give the net the same observation and mask, and the same action clicks the same kind of item in both.
 *
 * This header is the one place that knows the mapping. The net's observation and mask (riskfight_policy.h) are written through riskfight_bag_order; a net's actions
 * become physical slots through riskfight_bag_to_physical where they enter the sim (osrs_riskfight.h puf_step, the encounter's step, gs_rl_server, rl_explain,
 * the rollout's net hook).
 * riskfight_step and riskfight_policy_commands, like the scripts (riskfight_write_script_observation) and OppFit, work on physical slots. */
typedef struct {
    uint8_t slot[OSRS_INVENTORY_SIZE]; /* slot[i]: the physical slot of canonical entry i */
} RiskfightBagOrder;

/** Slot order as it is: what the scripts read (riskfight_write_script_observation). */
static const RiskfightBagOrder RF_BAG_PHYSICAL = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27}};

/* The bag key keeps the slot in 5 bits; a typedef so the check compiles as C and as CUDA C++. */
typedef char riskfight_bag_slot_fits_5_bits[OSRS_INVENTORY_SIZE <= 32 ? 1 : -1];

static inline uint32_t riskfight_bag_key(const OsrsInventoryCell* cells, int slot) {
    uint32_t code = cells[slot].content_code;
    return ((code == 0 ? (uint32_t)UINT16_MAX + 1 : code) << 5) | (uint32_t)slot;
}

static inline RiskfightBagOrder riskfight_bag_order(const Player* p) {
    const OsrsInventoryCell* cells = p->inventory_cells;
    RiskfightBagOrder order;
    uint32_t keys[OSRS_INVENTORY_SIZE];
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        uint32_t key = riskfight_bag_key(cells, slot);
        int at = slot;
        while (at > 0 && keys[at - 1] > key) {
            keys[at] = keys[at - 1];
            order.slot[at] = order.slot[at - 1];
            at--;
        }
        keys[at] = key;
        order.slot[at] = (uint8_t)slot;
    }
    return order;
}

/** The heads whose actions 1..OSRS_INVENTORY_SIZE name a bag entry (RF_UNEQUIP and 0 name none). */
static inline int riskfight_bag_head(int head) {
    return RF_GEAR_SLOT_BY_HEAD[head] >= 0 || (head >= RF_FOOD && head <= RF_COMBO);
}

/** A net's actions (canonical entries) as the physical slots of `p`'s bag, in place. */
static inline void riskfight_bag_to_physical(const Player* p, int* actions) {
    RiskfightBagOrder order = riskfight_bag_order(p);
    for (int head = 0; head < RF_HEADS; head++)
        if (riskfight_bag_head(head) && actions[head] >= 1 && actions[head] <= OSRS_INVENTORY_SIZE) actions[head] = order.slot[actions[head] - 1] + 1;
}

/** Physical-slot actions as the canonical entries a net would send, in place: the inverse of riskfight_bag_to_physical (a script's choice read against the net's mask). */
static inline void riskfight_bag_to_canonical(const Player* p, int* actions) {
    RiskfightBagOrder order = riskfight_bag_order(p);
    uint8_t entry[OSRS_INVENTORY_SIZE];
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) entry[order.slot[i]] = (uint8_t)i;
    for (int head = 0; head < RF_HEADS; head++)
        if (riskfight_bag_head(head) && actions[head] >= 1 && actions[head] <= OSRS_INVENTORY_SIZE) actions[head] = entry[actions[head] - 1] + 1;
}
#endif
