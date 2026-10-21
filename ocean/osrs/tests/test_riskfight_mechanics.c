/* Mechanics of the riskfight sim, on the user's Dharok kit (RISKFIGHT_DHAROK_LOADOUT: Voidwaker and defender worn, the ornate maul, the greataxe and the
 * locator orb in the bag, one cell freed so a two-hander can be wielded):
 * - busy hand: with the motor on, an orb click (and the attack) then the next decision's greataxe wield is never silently dropped. While clicks of the previous
 *   decision are unlanded the mask is the no-op of every head and the tactics block's hand_busy reads 1 (RiskfightState.hand_busy); on the first decision that
 *   is not busy the wield is offered, and it lands. Over many motor seeds, so both a busy and a free follow-up decision occur;
 * - spec gate: wield the maul + two special toggles at his full hitpoints (the mask offers k = 2: it rates the Voidwaker, whose k = 2 disarms) spends no energy,
 *   because two maul specials cannot reach his lower bound (riskfight_spec_clicks drops the toggles); the same decision with his bar low does spend;
 * - Dharok roll: a normal greataxe hit in the full set at low hitpoints rolls uniformly from 0 to the multiplied max: every integer of 0..max shows up and none above;
 * - inventory then attack: an inventory click with RF_ATTACK while attacking him queues the attack click after it (the click clears the interaction,
 *   riskfight_attack_click re-locks it); the food's attack delay stays: on a ready tick a drink + attack swings on that tick, hard food + attack 3 ticks later,
 *   and a drink without the attack click (the old behaviour) loses the interaction and swings a tick or more later.
 *   cc -std=gnu11 -O2 -I. -o /tmp/test_riskfight_mechanics ocean/osrs/tests/test_riskfight_mechanics.c -lm      (cwd = repo root) */
#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static Player* us(void) { return &state.env.players[0]; }

static void reset(uint32_t seed, int motor) {
    context.self_play = 1;
    context.motor[0].enabled = motor; /* our hand; seat 1 idles here, without one */
    context.loadout[0] = &RISKFIGHT_DHAROK_LOADOUT;
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, seed);
    context.loadout[0] = NULL;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (osrs_inventory_cell_metadata(&us()->inventory_cells[slot])->consumable_kind == OSRS_CONSUMABLE_MARLIN) {
            us()->inventory_cells[slot] = osrs_inventory_cell_empty();
            break;
        }
    assert(us()->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER);
}

static int first_slot_of(uint16_t item) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (!osrs_inventory_cell_is_empty(&us()->inventory_cells[slot]) && osrs_inventory_cell_metadata(&us()->inventory_cells[slot])->item_idx == item) return slot;
    return -1;
}

/** Our physical-slot actions stepped, seat 1 (a second agent) idle. */
static void step(const int* ours) {
    int actions[2 * RF_HEADS] = {0};
    memcpy(actions, ours, RF_HEADS * sizeof(int));
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    assert(!state.env.episode_over);
}

/** Whether our mask offers `ours` (physical slots) head by head. */
static int offered(const int* ours) {
    unsigned char mask[RF_MASK_SIZE];
    riskfight_write_action_mask(&state, 0, mask);
    int canonical[RF_HEADS];
    memcpy(canonical, ours, sizeof(canonical));
    riskfight_bag_to_canonical(us(), canonical);
    for (int head = 0; head < RF_HEADS; head++)
        if (!mask[RF_HEAD_OFFSET[head] + canonical[head]]) return 0;
    return 1;
}

static int busy_and_masked(void) {
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    unsigned char mask[RF_MASK_SIZE], wait[RF_MASK_SIZE];
    riskfight_write_action_mask(&state, 0, mask);
    riskfight_write_wait_mask(wait);
    int busy = state.hand_busy[0];
    assert(obs[RF_TACTICS_START + RF_TACTICS_HAND_BUSY] == (float)busy);
    assert((memcmp(mask, wait, sizeof(mask)) == 0) == busy);
    return busy;
}

static void test_orb_then_axe_never_dropped(void) {
    int busy_follow_ups = 0, free_follow_ups = 0;
    for (uint32_t seed = 1; seed <= 400; seed++) {
        reset(seed, 1);
        const int axe = first_slot_of(ITEM_DHAROKS_GREATAXE), hitpoints = us()->current_hitpoints;
        assert(axe >= 0 && !busy_and_masked());
        int orb[RF_HEADS] = {0};
        orb[RF_ORB] = 1;
        orb[RF_PRIMARY] = RF_ATTACK;
        assert(offered(orb));
        step(orb);
        /* The decisions after the orb: no-ops while the hand is busy, then the axe. */
        int none[RF_HEADS] = {0}, wield[RF_HEADS] = {0}, waited = 0;
        wield[RF_WEAPON] = axe + 1;
        while (busy_and_masked()) {
            assert(++waited <= 4);
            step(none);
        }
        if (waited) busy_follow_ups++; else free_follow_ups++;
        assert(offered(wield));
        step(wield);
        for (int tick = 0; us()->equipped[GEAR_SLOT_WEAPON] != ITEM_DHAROKS_GREATAXE; tick++) {
            assert(tick < 4 && busy_and_masked());
            step(none);
        }
        assert(us()->current_hitpoints <= hitpoints - 9); /* the orb landed too (he idles; 1 for a regeneration tick) */
    }
    assert(busy_follow_ups > 0 && free_follow_ups > 0);
    printf("busy hand: orb then axe over 400 motor seeds, %d follow-ups waited out a busy hand (masked to the no-op), %d were free; the axe always landed\n",
        busy_follow_ups, free_follow_ups);
}

/** Wield the maul and click the special twice, with ATTACK, then keep attacking. Returns our special energy after. */
static int maul_double_spec(int his_bar, int his_hitpoints, int expect_specials) {
    reset(77, 0);
    state.env.players[1].current_hitpoints = his_hitpoints;
    state.visible[0].health_bar = his_bar;
    const int maul = first_slot_of(ITEM_GRANITE_MAUL_ORNATE);
    assert(maul >= 0 && us()->special_energy == 100);
    int decision[RF_HEADS] = {0}, attack[RF_HEADS] = {0};
    decision[RF_WEAPON] = maul + 1;
    decision[RF_SPECIAL] = 2;
    decision[RF_PRIMARY] = RF_ATTACK;
    attack[RF_PRIMARY] = RF_ATTACK;
    assert(offered(decision)); /* the mask rates the wielded Voidwaker: k = 2 leaves it disarmed, always offered */
    assert(riskfight_spec_clicks(&state, 0, decision) == expect_specials);
    int actions[2 * RF_HEADS] = {0};
    memcpy(actions, decision, sizeof(decision));
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    assert(us()->equipped[GEAR_SLOT_WEAPON] == ITEM_GRANITE_MAUL_ORNATE);
    memcpy(actions, attack, sizeof(attack));
    for (int tick = 0; tick < 3 && !state.env.episode_over; tick++) riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    return us()->special_energy;
}

static void test_maul_double_spec_gate(void) {
    assert(maul_double_spec(OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 0) == 100);
    int low = maul_double_spec(3, 10, 2);
    assert(low < 100);
    printf("spec gate: wield maul + k = 2 at his full bar keeps 100 energy, at bar 3 spends (%d left)\n", low);
}

/** RF_ATTACK chosen on 10 ticks in a row queues one attack click, as gs_rl_server's rt_records sends one: none while we are already attacking him. */
static void test_attack_clicks_once(void) {
    reset(9, 0);
    int attack[RF_HEADS] = {0}, clicks = 0;
    attack[RF_PRIMARY] = RF_ATTACK;
    for (int tick = 0; tick < 10; tick++) {
        step(attack);
        const HumanCommandQueue* queued = &context.policy_commands[0].commands;
        for (int i = 0; i < queued->count; i++) clicks += queued->items[i].kind == HUMAN_COMMAND_ATTACK_NPC;
    }
    assert(clicks == 1 && osrs_interaction_active(&us()->interaction));
    printf("attack: RF_ATTACK on 10 ticks queues %d attack click\n", clicks);
}

/** Attacking him until the tick our weapon is ready (the next step would swing). */
static void attack_until_ready(void) {
    reset(9, 0);
    int attack[RF_HEADS] = {0};
    attack[RF_PRIMARY] = RF_ATTACK;
    step(attack);
    for (int tick = 0; us()->attack_timer > 0; tick++) {
        assert(tick < 10);
        step(attack);
    }
    assert(osrs_interaction_active(&us()->interaction));
    us()->current_hitpoints = 50;
}

static int first_slot_clicking(OsrsClickAction click) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (osrs_inventory_cell_metadata(&us()->inventory_cells[slot])->click_action == click) return slot;
    assert(0);
    return -1;
}

/** On a ready tick: the inventory click of `head` at `slot`, with RF_ATTACK when `attack`, then RF_ATTACK every tick. Returns the ticks after that tick to our
 * swing (0: on it). With `attack` the queue holds the inventory click before the attack click and the interaction is set after the tick. */
static int ticks_to_swing(int head, int slot, int attack) {
    int decision[RF_HEADS] = {0}, keep[RF_HEADS] = {0};
    decision[head] = slot + 1;
    decision[RF_PRIMARY] = attack ? RF_ATTACK : 0;
    keep[RF_PRIMARY] = RF_ATTACK;
    const int clock = us()->attack_timer_uncapped;
    step(decision);
    const HumanCommandQueue* queued = &context.policy_commands[0].commands;
    int click_at = -1, attack_at = -1;
    for (int i = 0; i < queued->count; i++) {
        if (queued->items[i].kind == HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK && queued->items[i].inventory_slot == slot) click_at = i;
        if (queued->items[i].kind == HUMAN_COMMAND_ATTACK_NPC) attack_at = i;
    }
    assert(click_at >= 0 && (attack ? attack_at > click_at : attack_at < 0));
    assert(osrs_interaction_active(&us()->interaction) == attack);
    if (head == RF_FOOD && attack) assert(us()->attack_timer_uncapped == clock + 3 - 1); /* hard food's +3 on the clock, one tick run */
    int waited = 0;
    for (; !us()->just_attacked; waited++) {
        assert(waited < 6);
        step(keep);
    }
    return waited;
}

static void test_inventory_then_attack_relocks(void) {
    attack_until_ready();
    const int drink_now = ticks_to_swing(RF_DRINK, first_slot_clicking(OSRS_CLICK_DRINK), 1);
    attack_until_ready();
    const int drink_old = ticks_to_swing(RF_DRINK, first_slot_clicking(OSRS_CLICK_DRINK), 0);
    attack_until_ready();
    const int food_now = ticks_to_swing(RF_FOOD, first_slot_clicking(OSRS_CLICK_EAT), 1);
    assert(drink_now == 0 && drink_old >= 1 && food_now == 3);
    printf("inventory then attack: on a ready tick a drink + attack swings on it (without the re-click %d ticks later), hard food + attack 3 ticks later\n",
        drink_old);
}

static void test_dharok_roll_covers_every_integer(void) {
    reset(5, 0);
    Player* p = us();
    Player* him = &state.env.players[1];
    p->equipped[GEAR_SLOT_WEAPON] = ITEM_DHAROKS_GREATAXE;
    p->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
    p->slot_gear_dirty = 1;
    osrs_ensure_player_equipment(p);
    assert(p->equipment_effect_profile.dharok_piece_count >= 4);
    p->current_hitpoints = 20;
    him->x = p->x + 1;
    him->y = p->y;
    him->prayer = PRAYER_NONE;
    him->current_defence = 1;
    const int base = calculate_base_max_hit(p, ATTACK_STYLE_MELEE, 1.0f, 30);
    const int max_hit = osrs_dharok_max_hit(base, p->base_hitpoints, p->current_hitpoints);
    assert(max_hit > base + base / 2);
    static int seen[256];
    assert(max_hit < 256);
    for (int roll = 0; roll < 200000; roll++) {
        p->num_pending_hits = 0;
        perform_attack(&state.env, 0, 1, ATTACK_STYLE_MELEE, 0, 0, 1);
        assert(p->num_pending_hits == 1);
        int damage = p->pending_hits[0].damage;
        assert(damage >= 0 && damage <= max_hit);
        seen[damage]++;
    }
    for (int d = 0; d <= max_hit; d++) assert(seen[d] > 0);
    printf("Dharok roll: base max %d, multiplied max %d at 20 hitpoints, every integer 0..%d rolled\n", base, max_hit, max_hit);
}

int main(void) {
    test_orb_then_axe_never_dropped();
    test_maul_double_spec_gate();
    test_attack_clicks_once();
    test_inventory_then_attack_relocks();
    test_dharok_roll_covers_every_integer();
    puts("Riskfight mechanics passed");
    return 0;
}
