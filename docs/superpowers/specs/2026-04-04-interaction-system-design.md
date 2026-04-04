# shared entity interaction + spec toggle system

date: 2026-04-04
status: approved

## problem

entity interactions (attacking NPCs) are reimplemented per encounter with different
interrupt rules. special attacks are conflated with targeting — "use spec" implicitly
sets a target, which doesn't match real OSRS where spec is a UI toggle independent
of who you're attacking.

## OSRS interaction model (from reverse engineering docs)

interactions are persistent. clicking to attack an entity starts an interaction that:
- auto-walks the player toward the target every tick
- auto-attacks when in range and attack timer is ready
- persists until explicitly interrupted

**interruptions** (clear the interaction):
- explicit ground click (walk action)
- all inventory item actions (eat food, drink potion, switch gear)
- target death or becoming untargetable
- player death or teleport
- launching a new interaction (clicking different entity)

**NOT interruptions** (interaction persists):
- prayer toggle
- special attack toggle
- interface clicks (except specific ones like equipment stats button)
- delays (stun, freeze)

## OSRS special attack model

spec is a toggle on the player, not an interaction:
- player clicks spec orb → `spec_armed = 1`
- on next attack (against any target): weapon's spec fires instead of normal attack
- after use: `spec_armed = 0` (auto-disarms)
- player can click spec orb again to disarm without using it
- spec persists across ticks until used or manually disarmed

spec is independent of targeting. you can:
1. arm spec, then click a new target → first attack uses spec
2. be attacking a target, arm spec → next attack uses spec
3. arm spec, do nothing → stays armed until you attack or disarm

## new shared module: osrs_interaction.h

```c
/* interaction state — tracks persistent entity targeting.
   lives on the player/entity that is doing the interacting. */
typedef struct {
    int target_slot;    /* target entity slot index, -1 = no interaction */
} OsrsInteraction;

/* set interaction target. replaces any existing interaction. */
static inline void osrs_interaction_set(OsrsInteraction* ix, int target_slot) {
    ix->target_slot = target_slot;
}

/* clear interaction (interrupted). */
static inline void osrs_interaction_clear(OsrsInteraction* ix) {
    ix->target_slot = -1;
}

/* check if interaction is active. */
static inline int osrs_interaction_active(const OsrsInteraction* ix) {
    return ix->target_slot >= 0;
}

/* process interrupts: call with the action the player is taking this tick.
   returns 1 if the action interrupted the interaction (target was cleared). */
#define OSRS_IACT_NONE     0  /* no action / idle — does NOT interrupt */
#define OSRS_IACT_MOVE     1  /* explicit ground click — INTERRUPTS */
#define OSRS_IACT_EAT      2  /* eat food from inventory — INTERRUPTS */
#define OSRS_IACT_DRINK    3  /* drink potion from inventory — INTERRUPTS */
#define OSRS_IACT_EQUIP    4  /* equip/switch gear from inventory — INTERRUPTS */
#define OSRS_IACT_PRAYER   5  /* prayer toggle — does NOT interrupt */
#define OSRS_IACT_SPEC     6  /* spec toggle — does NOT interrupt */
#define OSRS_IACT_ATTACK   7  /* click to attack entity — SETS new interaction */

static inline int osrs_interaction_check_interrupt(OsrsInteraction* ix, int action_type) {
    switch (action_type) {
        case OSRS_IACT_MOVE:
        case OSRS_IACT_EAT:
        case OSRS_IACT_DRINK:
        case OSRS_IACT_EQUIP:
            osrs_interaction_clear(ix);
            return 1;
        default:
            return 0;
    }
}
```

## spec toggle on Player struct

add to osrs_types.h Player struct:
```c
int spec_armed;  /* 1 = next attack uses special, 0 = normal attack */
```

encounters check `spec_armed` when executing an attack:
```c
if (player.spec_armed && player_has_enough_spec_energy(...)) {
    // fire spec weapon
    player.spec_armed = 0;  // auto-disarm after use
} else {
    // fire normal attack
}
```

## action space changes

### current (wrong)
- spec is a separate action head: `ZUL_HEAD_SPEC` with dim 2 (none/spec)
- spec action implicitly targets + attacks
- `INF_HEAD_SPEC` same pattern

### new (correct)
- spec becomes a toggle within the action space: arm/disarm
- attack action head handles targeting (already does this)
- when attack fires and spec_armed=1, it uses spec automatically

for zulrah:
```
old: ZUL_HEAD_SPEC = {none, use_spec}  — fires spec + sets target
new: ZUL_HEAD_SPEC = {no_change, toggle}  — arms/disarms spec toggle
```

the RL agent learns: arm spec (toggle) → attack (target action) → spec fires automatically.
this is 2 decisions across 2 ticks, matching real OSRS. the agent can also pre-arm spec
and then attack on the same tick.

for inferno: same pattern. spec toggle is independent of target selection.

## encounter changes

### zulrah step function
1. process spec toggle: if spec action == toggle, flip spec_armed
2. process interaction interrupts (food/potion/gear → clear target)
3. process attack action: if attack != none, set interaction target
4. auto-attack: if interaction active + timer ready + in range:
   - if spec_armed + enough energy → fire spec, disarm
   - else → fire normal attack

### inferno step function
same pattern. also add inventory-interrupt rule (currently missing — inferno doesn't
clear target on brew/restore, but it should per real OSRS).

### PvP
PvP's implicit targeting stays as-is for now (always fighting opponent).
spec toggle can be added to PvP later — it already has a spec mechanism.

## renderer
no changes needed — `attack_target_entity_idx` is already set from
`ix->target_slot` via `encounter_resolve_attack_target` or direct assignment.
the interaction system just replaces the per-encounter targeting logic.

## verification

- all 3 builds pass
- zulrah: spec toggle works (arm → attack → spec fires → disarms)
- inferno: spec toggle works, inventory interrupts interaction
- visual: player faces target only when interaction is active
- all test suites pass
