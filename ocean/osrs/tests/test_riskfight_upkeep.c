/* Upkeep mechanics of the rollout sim (riskfight_step_queues), the path every lookahead rollout runs: prayer drain per tick, what an empty prayer does, prayer
 * potion / super restore / brew / super combat effects and the potion lock. Deterministic, no opponent actions.
 *
 *   cc -std=c11 -O2 -I. -o test_riskfight_upkeep ocean/osrs/tests/test_riskfight_upkeep.c -lm      (cwd = repo root) */
#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static RiskfightState state;
static RiskfightContext context;
static HumanInput input[2];

static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
    context.self_play = 1;
    human_input_clear_pending(&input[0]);
    human_input_clear_pending(&input[1]);
}
/* One tick of the production step with `clicks` (inventory slots, -1 ends the list) queued for our side and nothing for his. */
static void tick(const int* clicks) {
    human_input_clear_pending(&input[0]);
    human_input_clear_pending(&input[1]);
    for (int i = 0; clicks && clicks[i] >= 0; i++)
        human_input_queue_command(&input[0], (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = clicks[i]});
    riskfight_step_queues(&state, &context, &input[0].commands, &input[1].commands);
}
static void put(int slot, OsrsConsumableKind kind, int doses) {
    state.env.players[0].inventory_cells[slot] = osrs_inventory_cell_from_content_code(osrs_inventory_content_code_from_consumable(kind, (uint8_t)doses));
}
static int doses(int slot) { return osrs_inventory_cell_metadata(&state.env.players[0].inventory_cells[slot])->dose_count; }
static int prayer_bonus_of_gear(const Player* p) { return encounter_player_prayer_bonus(p); }

/* (a) drain: every tick adds the active prayers' drain effects to a counter, a point is lost each time the counter exceeds 60 + 2 x prayer bonus. */
static int test_drain_rates(void) {
    struct { OverheadPrayer overhead; OffensivePrayer offensive; int effect; } cases[] = {
        {PRAYER_PROTECT_MELEE, OFFENSIVE_PRAYER_PIETY, 36}, {PRAYER_SMITE, OFFENSIVE_PRAYER_PIETY, 36}, {PRAYER_NONE, OFFENSIVE_PRAYER_PIETY, 24},
        {PRAYER_SMITE, OFFENSIVE_PRAYER_NONE, 12}, {PRAYER_REDEMPTION, OFFENSIVE_PRAYER_NONE, 6}, {PRAYER_PROTECT_MAGIC, OFFENSIVE_PRAYER_MELEE_LOW, 18},
    };
    for (int bonus_case = 0; bonus_case < 2; bonus_case++) {
        for (size_t c = 0; c < sizeof(cases) / sizeof(*cases); c++) {
            reset();
            Player* p = &state.env.players[0];
            if (!bonus_case) for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) p->equipped[slot] = ITEM_NONE;
            int bonus = prayer_bonus_of_gear(p);
            CHECK(bonus_case == 0 ? bonus == 0 : bonus > 0); /* the default loadout wears prayer-bonus gear */
            int resistance = 60 + 2 * bonus;
            p->current_prayer = 99; p->prayer_drain_counter = 0;
            p->prayer = cases[c].overhead; p->offensive_prayer = cases[c].offensive;
            for (int n = 1; n <= 200; n++) {
                tick(NULL);
                int accrued = cases[c].effect * n;
                int lost = (accrued - 1) / resistance; /* the counter has to EXCEED the resistance */
                if (99 - lost > 0) CHECK(p->current_prayer == 99 - lost);
                else { CHECK(p->current_prayer == 0); break; }
            }
        }
    }
    /* the tick a prayer is switched on costs nothing: the flags are set by the action, the drain skips that tick */
    reset();
    Player* p = &state.env.players[0];
    p->current_prayer = 50; p->prayer_drain_counter = 0; p->prayer = PRAYER_NONE; p->offensive_prayer = OFFENSIVE_PRAYER_NONE;
    encounter_apply_offensive_action(&p->offensive_prayer, ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY);
    p->offensive_prayer_just_activated = 1;
    encounter_drain_all_prayers(p, 0);
    CHECK(p->prayer_drain_counter == 0 && p->current_prayer == 50);
    encounter_drain_all_prayers(p, 0);
    CHECK(p->prayer_drain_counter == 24);
    return 0;
}

/* (b) an empty prayer switches every prayer off for the rest of the rollout: Piety's levels, Protect's block, Smite. */
static int test_empty_prayer(void) {
    reset();
    Player* p = &state.env.players[0];
    Player* him = &state.env.players[1];
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) p->equipped[slot] = ITEM_NONE;
    p->equipped[GEAR_SLOT_WEAPON] = ITEM_ABYSSAL_TENTACLE;
    osrs_refresh_player_equipment(p);
    p->current_prayer = 2; p->prayer_drain_counter = 0;
    p->prayer = PRAYER_PROTECT_MELEE; p->offensive_prayer = OFFENSIVE_PRAYER_PIETY;
    int boosted_max = calculate_max_hit(p, ATTACK_STYLE_MELEE, 1.0f, 0);
    int boosted_attack = calculate_effective_attack(p, ATTACK_STYLE_MELEE);
    Player plain = *p;
    plain.offensive_prayer = OFFENSIVE_PRAYER_NONE;
    CHECK(boosted_max > calculate_max_hit(&plain, ATTACK_STYLE_MELEE, 1.0f, 0));
    CHECK(boosted_attack > calculate_effective_attack(&plain, ATTACK_STYLE_MELEE));
    /* a swing launched while Protect is up carries it; an attack on us afterwards must not */
    him->equipped[GEAR_SLOT_WEAPON] = ITEM_ABYSSAL_TENTACLE;
    osrs_refresh_player_equipment(him);
    him->x = p->x; him->y = p->y + 1; /* inside melee range */
    perform_attack(&state.env, 1, 0, ATTACK_STYLE_MELEE, 0, 0, 1);
    CHECK(him->num_pending_hits == 1 && him->pending_hits[0].defender_prayer_at_attack == PRAYER_PROTECT_MELEE);
    him->num_pending_hits = 0;
    int emptied_at = -1;
    for (int n = 1; n <= 10 && emptied_at < 0; n++) {
        tick(NULL);
        if (p->current_prayer == 0) emptied_at = n;
    }
    CHECK(emptied_at == 4); /* 36 per tick against a resistance of 60: a point on tick 2, the second on tick 4 */
    CHECK(p->prayer == PRAYER_NONE && p->offensive_prayer == OFFENSIVE_PRAYER_NONE);
    CHECK(calculate_max_hit(p, ATTACK_STYLE_MELEE, 1.0f, 0) == calculate_max_hit(&plain, ATTACK_STYLE_MELEE, 1.0f, 0));
    CHECK(calculate_max_hit(p, ATTACK_STYLE_MELEE, 1.0f, 0) < boosted_max);
    CHECK(calculate_effective_attack(p, ATTACK_STYLE_MELEE) == calculate_effective_attack(&plain, ATTACK_STYLE_MELEE));
    him->x = p->x; him->y = p->y + 1;
    perform_attack(&state.env, 1, 0, ATTACK_STYLE_MELEE, 0, 0, 1);
    CHECK(him->num_pending_hits >= 1 && him->pending_hits[him->num_pending_hits - 1].defender_prayer_at_attack == PRAYER_NONE);
    for (int n = 0; n < 40; n++) tick(NULL);
    CHECK(p->current_prayer == 0 && p->prayer == PRAYER_NONE && p->offensive_prayer == OFFENSIVE_PRAYER_NONE);
    /* prayer clicks on an empty prayer do nothing in the rollout sim */
    human_input_clear_pending(&input[0]);
    human_input_queue_command(&input[0], (HumanCommand){.kind = HUMAN_COMMAND_OFFENSIVE_PRAYER, .offensive_prayer = ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY});
    riskfight_step_queues(&state, &context, &input[0].commands, &input[1].commands);
    CHECK(p->offensive_prayer == OFFENSIVE_PRAYER_NONE);
    /* Smite on an empty prayer is gone too */
    reset();
    p = &state.env.players[0];
    p->current_prayer = 1; p->prayer = PRAYER_SMITE; p->offensive_prayer = OFFENSIVE_PRAYER_NONE;
    for (int n = 0; n < 6; n++) tick(NULL); /* 12 per tick: the counter passes 60 on the sixth */
    CHECK(p->current_prayer == 0 && p->prayer == PRAYER_NONE);
    return 0;
}

/* (c) prayer potion and super restore restore prayer; the super restore also restores drained stats (never above base, never HP). */
static int test_restore_potions(void) {
    reset();
    Player* p = &state.env.players[0];
    put(0, OSRS_CONSUMABLE_PRAYER_RESTORE, 4); put(1, OSRS_CONSUMABLE_SUPER_RESTORE, 4); put(2, OSRS_CONSUMABLE_SUPER_RESTORE, 1);
    p->current_prayer = 10; p->prayer = PRAYER_NONE; p->offensive_prayer = OFFENSIVE_PRAYER_NONE;
    p->current_attack = 80; p->current_strength = 70; p->current_defence = 90; p->current_magic = 70; p->current_ranged = 60;
    int hp = p->current_hitpoints;
    CHECK(p->base_prayer == 99 && p->base_attack == 99);
    int click_prayer[] = {0, -1};
    tick(click_prayer);
    CHECK(p->current_prayer == 10 + 7 + 99 / 4 && doses(0) == 3);
    CHECK(p->current_attack == 80 && p->current_strength == 70 && p->current_magic == 70); /* a prayer potion restores prayer only */
    CHECK(p->potion_timer == 2 && p->food_timer == 2); /* set to 3 by the drink, one tick elapsed */
    /* the lock: a second drink on the next two ticks is refused (no dose spent), the third tick after the first takes it */
    int click_restore[] = {1, -1};
    tick(click_restore); CHECK(doses(1) == 4 && p->current_prayer == 10 + 7 + 99 / 4);
    tick(click_restore); CHECK(doses(1) == 4 && p->current_prayer == 10 + 7 + 99 / 4);
    tick(click_restore);
    CHECK(doses(1) == 3);
    CHECK(p->current_prayer == 10 + 7 + 99 / 4 + 8 + 99 / 4);
    CHECK(p->current_attack == 99 && p->current_strength == 99 && p->current_defence == 99 && p->current_magic == 99 && p->current_ranged == 92);
    CHECK(p->current_hitpoints <= hp); /* no healing from a restore */
    /* the prayer points never pass the base */
    p->current_prayer = 95; p->potion_timer = 0;
    tick(click_restore);
    CHECK(p->current_prayer == 99 && doses(1) == 2);
    /* stats above base are left alone */
    p->potion_timer = 0; p->current_attack = 118; p->current_strength = 70;
    tick(click_restore);
    CHECK(p->current_attack == 118 && p->current_strength == 99);
    return 0;
}

/* (d) brew drains attack/strength/ranged/magic by 10% + 2 and boosts defence by 20% + 2 (capped at base + that), super combat boosts attack/strength/defence by 5 + 15%. */
static int test_brew_and_combat(void) {
    reset();
    Player* p = &state.env.players[0];
    put(0, OSRS_CONSUMABLE_BREW, 4); put(1, OSRS_CONSUMABLE_SUPER_COMBAT, 4);
    p->current_hitpoints = 50;
    p->current_attack = 118; p->current_strength = 118; p->current_magic = 99; p->current_ranged = 99; p->current_defence = 99;
    int brew_hp = p->base_hitpoints * 15 / 100 + 2;
    int click_brew[] = {0, -1};
    tick(click_brew);
    CHECK(p->current_hitpoints == 50 + brew_hp);
    CHECK(p->current_attack == 118 - (118 * 10 / 100 + 2) && p->current_strength == 118 - (118 * 10 / 100 + 2));
    CHECK(p->current_magic == 99 - (99 * 10 / 100 + 2) && p->current_ranged == 99 - (99 * 10 / 100 + 2));
    CHECK(p->current_defence == 99 + (99 * 20 / 100 + 2));
    int drained_attack = p->current_attack, drained_magic = p->current_magic;
    CHECK(drained_attack == 105 && drained_magic == 88);
    /* a second brew after the lock drains from the already drained levels, defence stays at its cap */
    tick(NULL); tick(NULL);
    tick(click_brew);
    CHECK(p->current_attack == drained_attack - (drained_attack * 10 / 100 + 2) && p->current_defence == 99 + (99 * 20 / 100 + 2));
    CHECK(p->current_magic == drained_magic - (drained_magic * 10 / 100 + 2));
    /* super combat lifts attack/strength/defence by 5 + 15% of base, capped at base + that, and leaves magic alone */
    tick(NULL); tick(NULL);
    int magic = p->current_magic, before_attack = p->current_attack;
    int click_combat[] = {1, -1};
    tick(click_combat);
    int boost = 5 + 99 * 15 / 100;
    CHECK(before_attack == 93 && p->current_attack == 93 + boost && p->current_strength == 93 + boost); /* an addition, below the cap of base + boost */
    CHECK(p->current_defence == 99 + (99 * 20 / 100 + 2)); /* the brew boost is above the combat cap and is kept */
    CHECK(p->current_magic == magic);
    /* from a drained state it adds the boost, never above the cap */
    reset();
    p = &state.env.players[0];
    put(1, OSRS_CONSUMABLE_SUPER_COMBAT, 4);
    p->current_attack = 80; p->current_strength = 99; p->current_defence = 90;
    tick(click_combat);
    CHECK(p->current_attack == 80 + boost && p->current_strength == 99 + boost && p->current_defence == 90 + boost);
    return 0;
}

/* (e) potion lock 3 ticks, a drink costs no attack delay (potions are not food: only the click interrupting the attack), food does cost it, a drink locks hard food. */
static int test_lock_and_attack_delay(void) {
    reset();
    Player* p = &state.env.players[0];
    put(0, OSRS_CONSUMABLE_BREW, 4); put(1, OSRS_CONSUMABLE_ANGLERFISH, 0); put(2, OSRS_CONSUMABLE_KARAMBWAN, 0);
    p->current_hitpoints = 40;
    p->has_attack_timer = 1; p->attack_timer = 5; p->attack_timer_uncapped = 5;
    int click_brew[] = {0, -1}, click_hard[] = {1, -1}, click_fast[] = {2, -1};
    tick(click_brew);
    CHECK(p->potion_timer == 2);
    CHECK(p->attack_timer == 4 && p->attack_timer_uncapped == 4); /* one tick elapsed, nothing added */
    /* hard food right after a drink is refused for the lock, a karambwan is allowed */
    tick(click_hard); CHECK(p->current_hitpoints < 99 && !osrs_inventory_cell_is_empty(&p->inventory_cells[1]));
    tick(click_fast); CHECK(osrs_inventory_cell_is_empty(&p->inventory_cells[2]));
    /* a hard meal costs the attack delay the potion did not */
    reset();
    p = &state.env.players[0];
    put(1, OSRS_CONSUMABLE_ANGLERFISH, 0);
    p->current_hitpoints = 40;
    p->has_attack_timer = 1; p->attack_timer = 5; p->attack_timer_uncapped = 5;
    tick(click_hard);
    CHECK(p->attack_timer == 5 - 1 + 3);
    /* the lock is exactly three ticks: a drink at T allows the next at T + 3 */
    reset();
    p = &state.env.players[0];
    put(0, OSRS_CONSUMABLE_SUPER_COMBAT, 4);
    p->current_attack = 50;
    int click_combat[] = {0, -1};
    tick(click_combat); CHECK(doses(0) == 3);
    tick(click_combat); CHECK(doses(0) == 3);
    tick(click_combat); CHECK(doses(0) == 3);
    tick(click_combat); CHECK(doses(0) == 2);
    /* a drink interrupts the attack interaction the way every inventory use does: the click is only paid when the attack is not re-sent */
    reset();
    p = &state.env.players[0];
    put(0, OSRS_CONSUMABLE_SUPER_COMBAT, 4);
    osrs_interaction_set(&p->interaction, 1);
    tick(click_combat);
    CHECK(doses(0) == 3 && !osrs_interaction_active(&p->interaction));
    return 0;
}

int main(void) {
    int failed = test_drain_rates() | test_empty_prayer() | test_restore_potions() | test_brew_and_combat() | test_lock_and_attack_delay();
    if (failed) return 1;
    puts("upkeep mechanics ok");
    return 0;
}
