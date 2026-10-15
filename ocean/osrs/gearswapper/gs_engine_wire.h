#ifndef GS_ENGINE_WIRE_H
#define GS_ENGINE_WIRE_H
/* The wire between the Light Gear Swapper plugin (observation + click executor) and the C decision engine (tests/gs_lookahead_server.c).
 * Spec: ocean/osrs/gearswapper/ENGINE_WIRE.md. This header is the single definition of the numeric parts of the protocol: the server, the in-sim
 * deployed policy and every C test include it; the Java side (EngineBridge/EnginePlan) mirrors it constant for constant.
 * Do not renumber a kind: bump GS_ENGINE_WIRE_VERSION and update both sides in one change. */

#define GS_ENGINE_WIRE_VERSION 2

/* One action record: `due kind arg timing [count candidate...]`.
 * due     absolute game tick on which the server must PROCESS the action (the tick after the one the plugin observed is request.tick + 1).
 *         The executor starts the motion so the click lands in the window processed on `due`; it never fires earlier.
 * timing  0: `due` is a lower bound; the executor fires as soon as its hand allows on or after `due` and the plan's later fixed records are not
 *            held up by it. 1: `due` is the intended landing tick (a plan from search); the executor treats it as a deadline-targeted click
 *            and reports a MISS (not a retry) when the hand cannot land it.
 * arg     kind specific, below. */
typedef enum {
    GS_WIRE_CLICK_SLOT       = 0,   /* arg inventory slot 0..27: primary click (Eat, Drink, Wield, Wear, Release by item) */
    GS_WIRE_ATTACK           = 1,   /* arg 0: attack the tracked opponent */
    GS_WIRE_SPEC_ENABLE      = 2,   /* arg 0: special attack bar on (never toggles an armed bar off) */
    GS_WIRE_CAST_VENGEANCE   = 3,   /* arg 0: cast Vengeance */
    GS_WIRE_SPEC_DISARM      = 4,   /* arg 0: special attack bar off */
    GS_WIRE_UNEQUIP_SLOT     = 5,   /* arg worn slot index 0..12 (WORN_TO_GEAR order) */
    GS_WIRE_WAIT_READY       = 6,   /* barrier: until our attack timer allows a swing on the processing tick; arg ticks early allowed */
    GS_WIRE_WAIT_HIT         = 7,   /* barrier: until our attack registered/landed; arg minimum damage or -1 */
    GS_WIRE_COND_ENERGY      = 8,   /* arg minimum spec energy percent; if below, skip to the end of the group */
    GS_WIRE_COND_HP          = 9,   /* arg hitpoints threshold; at or above it skip the next record */
    GS_WIRE_GROUP            = 10,  /* group boundary: the previous group completes (its clicks processed) before the next begins */
    GS_WIRE_EQUIP_ITEM       = 11,  /* arg raw item id: wield/wear that item from the bag */
    GS_WIRE_CLICK_ITEM       = 12,  /* arg raw item id: primary click on that item wherever it is in the bag */
    GS_WIRE_SPEC_TOGGLE      = 13,  /* arg 0: toggle the bar */
    GS_WIRE_EAT_RECIPE       = 14,  /* arg 0, then `count c1..ck`: first carried candidate; negative = dose family (any dose of |id|), positive = exact raw id */
    GS_WIRE_EQUIP_RECIPE     = 15,  /* same candidate form, equip the first carried candidate; skip if already worn */
    GS_WIRE_UNEQUIP_RECIPE   = 16,  /* same candidate form, remove the first worn candidate */
    GS_WIRE_STOP_ATTACK      = 17,  /* arg 0: clear our attack target (walk-here / interface stop); resume attack is a later GS_WIRE_ATTACK */
    GS_WIRE_ALIGNED_READY    = 18,  /* arg absolute attack tick, timing 1: wait for the aligned swing window, then the following ATTACK acquires */
    GS_WIRE_WAIT_SPEC_LAUNCH = 19,  /* arg 0: barrier: until our Voidwaker special swing has been observed (burst second ticket) */
    GS_WIRE_KIND_COUNT       = 20
} GsWireKind;

enum {
    GS_WIRE_MAX_RECORDS = 64,       /* records in one plan (R remainder or A reply) */
    GS_WIRE_MAX_CANDIDATES = 28,
    GS_WIRE_MACROS = 18,            /* scored alternatives per decision (names come from READY, index 0 is always "continue", 1 "no new clicks") */
    GS_WIRE_MACRO_CONTINUE = 0,
    GS_WIRE_MACRO_CANCEL = 1
};

/* Raw item id conventions on the wire, observed values only: -1 empty slot, >0 the client item id verbatim (unknown/unmapped ids are sent as is and
 * the server answers with a PRIOR note, never silently drops them). Opponent equipment that the plugin could not observe is the tag `UNKNOWN`. */
#define GS_WIRE_EMPTY_SLOT (-1)

#endif
