/* The tactics block (RF_TACTICS_START, riskfight_write_tactics_observation) on the user's Dharok kit (RISKFIGHT_DHAROK_LOADOUT: Voidwaker and defender worn with the
 * helm, platebody and platelegs, the ornate maul and the greataxe in a full bag):
 * - the greataxe max at hitpoints - 10 and - 20 (floor 1) is the Dharok formula max * (10000 + 99 * (99 - hp)) / 10000 of the max at full hitpoints, and equals the
 *   OURS row's max while the set is worn; with the platebody stripped into the bag the block still reads the set's max (and flags the strip) while the row drops;
 * - the knockout chances are 0 when his lower bound exceeds the max, the Voidwaker special's is its hit chance (1) when his upper bound is at most its least damage,
 *   and two maul specials match a brute-force enumeration of both rolls;
 * - the OURS row identities: Voidwaker wielded, then the maul and the axe by item index, the fourth row empty;
 * - can_wield_2h is 0 with the full bag and the defender on, 1 once a cell frees.
 *   cc -std=gnu11 -O2 -I. -o /tmp/test_riskfight_tactics_obs ocean/osrs/tests/test_riskfight_tactics_obs.c -lm      (cwd = repo root) */
#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static Player* us(void) { return &state.env.players[0]; }

static const float* observe(void) {
    static float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    for (int i = 0; i < RF_OBS_SIZE; i++) assert(obs[i] >= -1 && obs[i] <= 1);
    return obs;
}
static const float* tactics(void) { return observe() + RF_TACTICS_START; }
static int damage(float scaled) { return (int)lroundf(scaled * RF_THREAT_DAMAGE_SCALE); }
static int near(double a, double b) { return fabs(a - b) <= 1e-6; }

static void reset(void) {
    context.self_play = 1;
    context.loadout[0] = &RISKFIGHT_DHAROK_LOADOUT;
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 123);
    context.loadout[0] = NULL;
    assert(us()->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER && us()->equipped[GEAR_SLOT_SHIELD] != ITEM_NONE);
}

static int first_slot_of(uint8_t item) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (!osrs_inventory_cell_is_empty(&us()->inventory_cells[slot]) && osrs_inventory_cell_metadata(&us()->inventory_cells[slot])->item_idx == item) return slot;
    return -1;
}

static int first_slot_of_kind(OsrsConsumableKind kind) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (!osrs_inventory_cell_is_empty(&us()->inventory_cells[slot]) && osrs_inventory_cell_metadata(&us()->inventory_cells[slot])->consumable_kind == kind) return slot;
    return -1;
}

/** The OURS row of `item` in an observation (row 0 the wielded weapon, then the bag's by item index). */
static const float* ours_row(const float* obs, uint8_t item) {
    const float* t = obs + RF_TACTICS_START;
    int kind = item == ITEM_VOIDWAKER ? 0 : pvp_is_maul(item) ? 1 : 2;
    for (int row = 0; row < RF_THREAT_OURS_SLOTS; row++)
        if (t[RF_TACTICS_IDENTITY + row * RF_TACTICS_KINDS + kind] == 1) return obs + RF_THREAT_START + RF_THREAT_OURS_OFFSET + row * RF_THREAT_OURS_WIDTH;
    assert(0 && "no OURS row holds the weapon");
    return NULL;
}

static int dharok(int max, int hp) { return max * (10000 + 99 * (99 - hp)) / 10000; }

static void test_axe_max(void) {
    reset();
    us()->current_hitpoints = 99;
    const float* obs = observe();
    const float* t = obs + RF_TACTICS_START;
    int full = damage(t[RF_TACTICS_AXE_MAX]);
    assert(full > 0 && damage(ours_row(obs, ITEM_DHAROKS_GREATAXE)[1]) == full && t[RF_TACTICS_STRIPPED] == 0);
    us()->current_hitpoints = 50;
    obs = observe();
    t = obs + RF_TACTICS_START;
    assert(damage(t[RF_TACTICS_AXE_MAX]) == dharok(full, 50) && damage(ours_row(obs, ITEM_DHAROKS_GREATAXE)[1]) == dharok(full, 50));
    assert(damage(t[RF_TACTICS_AXE_MAX + 1]) == dharok(full, 40) && damage(t[RF_TACTICS_AXE_MAX + 2]) == dharok(full, 30));
    us()->current_hitpoints = 15;
    t = tactics();
    assert(damage(t[RF_TACTICS_AXE_MAX + 1]) == dharok(full, 5) && damage(t[RF_TACTICS_AXE_MAX + 2]) == dharok(full, 1));
    /* The platebody stripped into a freed cell: the block keeps the set's max, the row (the set broken) does not. */
    us()->current_hitpoints = 50;
    int cell = first_slot_of_kind(OSRS_CONSUMABLE_MARLIN);
    assert(cell >= 0);
    us()->inventory_cells[cell] = osrs_inventory_cell_from_item(us()->equipped[GEAR_SLOT_BODY]);
    us()->equipped[GEAR_SLOT_BODY] = ITEM_NONE;
    osrs_refresh_player_equipment(us());
    obs = observe();
    t = obs + RF_TACTICS_START;
    assert(t[RF_TACTICS_STRIPPED] == 1 && damage(t[RF_TACTICS_AXE_MAX]) == dharok(full, 50) && damage(ours_row(obs, ITEM_DHAROKS_GREATAXE)[1]) < dharok(full, 50));
    printf("axe max: %d at 99, %d at 50, %d at 40 (one orb click), %d at 30, floor 1 at 15 - 20; the stripped set reads the worn set's max\n",
        full, dharok(full, 50), dharok(full, 40), dharok(full, 30));
}

static void test_identity(void) {
    reset();
    const float* t = tactics();
    uint8_t first = ITEM_GRANITE_MAUL_ORNATE < ITEM_DHAROKS_GREATAXE ? ITEM_GRANITE_MAUL_ORNATE : ITEM_DHAROKS_GREATAXE;
    int expected[RF_THREAT_OURS_SLOTS] = {0, first == ITEM_GRANITE_MAUL_ORNATE ? 1 : 2, first == ITEM_GRANITE_MAUL_ORNATE ? 2 : 1, -1};
    for (int row = 0; row < RF_THREAT_OURS_SLOTS; row++)
        for (int k = 0; k < RF_TACTICS_KINDS; k++) assert(t[RF_TACTICS_IDENTITY + row * RF_TACTICS_KINDS + k] == (k == expected[row]));
    assert(t[RF_TACTICS_ORB] == 1 && t[RF_TACTICS_HP_ABOVE_1] == 1);
    us()->current_hitpoints = 1;
    assert(tactics()[RF_TACTICS_HP_ABOVE_1] == 0);
    printf("identity: Voidwaker wielded, then the maul and the axe by item index, the fourth row empty\n");
}

static void test_can_wield_2h(void) {
    reset();
    assert(first_slot_of(ITEM_DHAROKS_GREATAXE) >= 0);
    assert(tactics()[RF_TACTICS_CAN_WIELD_2H] == 0); /* 28 cells full, the defender would have nowhere to go */
    us()->inventory_cells[first_slot_of_kind(OSRS_CONSUMABLE_MARLIN)] = osrs_inventory_cell_empty();
    assert(tactics()[RF_TACTICS_CAN_WIELD_2H] == 1);
    printf("can_wield_2h: 0 with the bag full and the defender worn, 1 once a cell frees\n");
}

/** Two maul specials by brute force: each lands with `hit`, then deals 0..max uniformly; his hitpoints uniform over [lower, upper]. */
static double two_specs(double hit, int max, int lower, int upper) {
    double sum = 0;
    for (int hp = lower; hp <= upper; hp++) {
        double p = 0;
        for (int a = -1; a <= max; a++)
            for (int b = -1; b <= max; b++) {
                double pa = a < 0 ? 1 - hit : hit / (max + 1), pb = b < 0 ? 1 - hit : hit / (max + 1);
                if ((a < 0 ? 0 : a) + (b < 0 ? 0 : b) >= hp) p += pa * pb;
            }
        sum += p;
    }
    return sum / (upper - lower + 1);
}

static void test_ko(void) {
    reset();
    us()->current_hitpoints = 99;
    state.visible[0].health_bar = -1; /* no bar: 99 to 121 */
    const float* obs = observe();
    const float* t = obs + RF_TACTICS_START;
    const float* timing = obs + RF_TIMING_START;
    const float *vw = ours_row(obs, ITEM_VOIDWAKER), *maul = ours_row(obs, ITEM_GRANITE_MAUL_ORNATE), *axe = ours_row(obs, ITEM_DHAROKS_GREATAXE);
    int lower = damage(timing[3]), upper = damage(timing[4]);
    assert(lower == 99 && upper == 121);
    assert(damage(t[RF_TACTICS_AXE_MAX]) < lower && damage(vw[1]) < lower && damage(vw[5]) < lower && damage(maul[5]) < lower);
    for (int i = 0; i < 4; i++) assert(t[RF_TACTICS_KO + i] == 0);
    assert(near(t[RF_TACTICS_KO + 4], two_specs(maul[6], damage(maul[5]), lower, upper)));
    printf("ko at 99-121: axe/vw/vw spec/maul 0 (maxes %d/%d/%d/%d), two maul specials %.4f as enumerated\n",
        damage(t[RF_TACTICS_AXE_MAX]), damage(vw[1]), damage(vw[5]), damage(maul[5]), t[RF_TACTICS_KO + 4]);
    /* His bar low: his upper bound at most the Voidwaker special's least damage, max / 2. */
    int least = osrs_voidwaker_min_hit(damage(vw[1]));
    state.visible[0].health_bar = osrs_health_bar_ratio(5, 99, OSRS_PLAYER_HEALTH_BAR_SCALE);
    obs = observe();
    t = obs + RF_TACTICS_START;
    timing = obs + RF_TIMING_START;
    lower = damage(timing[3]); upper = damage(timing[4]);
    assert(upper <= least && vw[6] == 1);
    assert(near(t[RF_TACTICS_KO + 2], 1.0 * vw[6]));
    assert(near(t[RF_TACTICS_KO + 4], two_specs(maul[6], damage(maul[5]), lower, upper)));
    assert(t[RF_TACTICS_KO] > 0 && t[RF_TACTICS_KO] < axe[2] + 1e-6f);
    printf("ko at %d-%d: vw spec %.3f (least %d), axe %.3f, two maul specials %.4f as enumerated\n", lower, upper, t[RF_TACTICS_KO + 2], least, t[RF_TACTICS_KO],
        t[RF_TACTICS_KO + 4]);
}

int main(void) {
    test_axe_max();
    test_identity();
    test_can_wield_2h();
    test_ko();
    puts("riskfight tactics observation: all passed");
    return 0;
}
