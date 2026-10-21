/* The opponent pool (riskfight_kits.h): every kit is wieldable and consistent, the draw follows the weights, every kit plays a fight under the script, and the
 * prior the threat features read comes off the same table.
 * cc -std=gnu11 -O2 -I. -o /tmp/test_riskfight_pool ocean/osrs/tests/test_riskfight_pool.c -lm */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static int count_weapons_in_bag(const RiskfightLoadout* l, uint16_t item) {
    int n = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) n += osrs_item_content_metadata(l->inventory[slot])->item_idx == item;
    return n;
}

static void test_kits_consistent(void) {
    assert(RISKFIGHT_POOL_COUNT >= 40);
    float total = 0, mass[3] = {0};
    int count[3] = {0};
    for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++) {
        const RiskfightKit* k = &RISKFIGHT_POOL[i];
        const RiskfightLoadout* l = &RISKFIGHT_POOL_LOADOUT[i];
        total += k->weight; mass[k->kind] += k->weight; count[k->kind]++;
        assert(k->weight > 0 && k->weapon_count >= 1 && k->weapon_count <= RF_KIT_WEAPONS);
        assert(k->weapons[0] == k->equipment[GEAR_SLOT_WEAPON] && memcmp(k->equipment, l->equipment, NUM_GEAR_SLOTS * sizeof(uint16_t)) == 0);
        /* Every worn item sits in its own slot; a two-hander never sits over a shield. */
        for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
            if (k->equipment[slot] == ITEM_NONE) continue;
            assert(k->equipment[slot] < NUM_ITEMS);
            assert(riskfight_gear_slot_of_item(&ITEM_DATABASE[k->equipment[slot]]) == slot);
        }
        if (k->equipment[GEAR_SLOT_WEAPON] != ITEM_NONE) assert(!(item_is_two_handed(k->equipment[GEAR_SLOT_WEAPON]) && k->equipment[GEAR_SLOT_SHIELD] != ITEM_NONE));
        /* Every weapon can be swung by the sim; every ranged weapon that needs ammunition wears it; the others of the kit are in the bag, once each. */
        int bag_weapons = 0, ranged = 0;
        for (int w = 0; w < k->weapon_count; w++) {
            assert(riskfight_kit_weapon_ok(k->weapons[w]));
            for (int v = 0; v < w; v++) assert(k->weapons[v] != k->weapons[w] || k->weapons[w] == ITEM_NONE);
            if (w > 0) { assert(count_weapons_in_bag(l, k->weapons[w]) == 1); bag_weapons++; }
            ranged |= k->weapons[w] != ITEM_NONE && get_item_attack_style(k->weapons[w]) == ATTACK_STYLE_RANGED;
        }
        uint16_t ammo = osrs_ammo_assumption(k->equipment[GEAR_SLOT_WEAPON]);
        if (ammo != ITEM_NONE) assert(k->equipment[GEAR_SLOT_AMMO] != ITEM_NONE);
        if (ranged) assert(k->ranged_level >= 99 && k->start_offset > 1);
        /* The bag fits, and holds a free slot when a two-hander has to take the shield off. */
        int used = 0, free_slots = 0;
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) { used += l->inventory[slot] != 0; free_slots += l->inventory[slot] == 0; }
        assert(used >= bag_weapons);
        int two_handed_in_bag = 0;
        for (int w = 1; w < k->weapon_count; w++) two_handed_in_bag |= k->weapons[w] != ITEM_NONE && item_is_two_handed(k->weapons[w]);
        if (two_handed_in_bag && k->equipment[GEAR_SLOT_SHIELD] != ITEM_NONE) assert(free_slots >= 1);
        assert(k->bag != RF_BAG_BARE || used == bag_weapons);
    }
    assert(fabsf(total - 1) < 1e-3f);
    assert(fabsf(mass[RF_KIT_EMPIRICAL] - RF_POOL_MASS[0]) < 1e-3f && fabsf(mass[RF_KIT_ARCHETYPE] - RF_POOL_MASS[1]) < 1e-3f && fabsf(mass[RF_KIT_EDGE] - RF_POOL_MASS[2]) < 1e-3f);
    printf("pool: %d kits (%d empirical, %d archetype, %d edge); %d empirical rows (%d fights) left out for items the sim lacks, %d of them for a magic weapon\n",
        RISKFIGHT_POOL_COUNT, count[0], count[1], count[2], RISKFIGHT_POOL_EXCLUDED_ROWS, RISKFIGHT_POOL_EXCLUDED_FIGHTS, RISKFIGHT_POOL_EXCLUDED_MAGIC_ROWS);
}

/* Draws: the kit of a reset is the pool's, he wears and carries it, and the shares of the three groups follow the masses. */
static void test_draws(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_put_int((EncounterState*)&state, (EncounterContext*)&context, "opponent_pool", 1);
    int seen_kind[3] = {0}, seen_kit[RF_POOL_MAX] = {0}, n = 40000, ring_drawn = 0;
    for (int i = 0; i < n; i++) {
        riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 1000 + (uint32_t)i);
        const RiskfightKit* k = context.opp_kit;
        assert(k && k >= RISKFIGHT_POOL && k < RISKFIGHT_POOL + RISKFIGHT_POOL_COUNT);
        seen_kind[k->kind]++;
        seen_kit[k - RISKFIGHT_POOL]++;
        const Player* him = &state.env.players[1];
        for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++)
            if (slot != GEAR_SLOT_RING) assert(him->equipped[slot] == k->equipment[slot]);
        if (k->equipment[GEAR_SLOT_RING] != ITEM_NONE) assert(him->equipped[GEAR_SLOT_RING] == k->equipment[GEAR_SLOT_RING]);
        else ring_drawn += him->equipped[GEAR_SLOT_RING] != ITEM_NONE;
        assert(him->current_hitpoints == k->hitpoints);
        assert(him->x == FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH / 2 + k->start_offset);
        /* Everything a wielder of this kit clicks from the bag is wieldable now. */
        int has_empty = osrs_first_empty_inventory_cell(him->inventory_cells, -1) >= 0;
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
            const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&him->inventory_cells[slot]);
            if (m->gear_slot == GEAR_SLOT_WEAPON) assert(osrs_can_equip_metadata(him, m, has_empty));
        }
        /* His kit is what we see of him, and the threat block lists his weapon first. */
        float obs[RF_OBS_SIZE];
        riskfight_write_observation(&state, 0, obs);
        assert(state.visible[0].equipment[GEAR_SLOT_WEAPON] == k->equipment[GEAR_SLOT_WEAPON]);
        assert(obs[RF_THREAT_START] == 1);
    }
    float share[3];
    for (int g = 0; g < 3; g++) share[g] = (float)seen_kind[g] / n;
    assert(fabsf(share[0] - RF_POOL_MASS[0]) < 0.02f && fabsf(share[1] - RF_POOL_MASS[1]) < 0.02f && fabsf(share[2] - RF_POOL_MASS[2]) < 0.01f);
    int unseen = 0;
    for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++) unseen += seen_kit[i] == 0 && RISKFIGHT_POOL[i].kind != RF_KIT_EMPIRICAL;
    assert(unseen == 0);
    assert(ring_drawn > 0); /* kits that show no ring get one per fight: the client does not show rings */
    printf("draws: empirical %.3f archetype %.3f edge %.3f over %d resets\n", share[0], share[1], share[2], n);
}

/* The observation's cheap route (the Player's cached bonuses, weapon swaps by delta) against the slow one (every loadout summed from scratch). */
static void check_threat_block_against_reference(const RiskfightState* s, const float* obs) {
    const Player* p = &s->env.players[0];
    const RiskfightThreat* t = &s->visible[0].threat;
    OsrsThreatActor us = riskfight_threat_actor_of(p);
    OsrsDefenceRolls ours = osrs_defence_rolls(p->equipped, &us);
    const float* block = obs + RF_THREAT_START;
    for (int i = 0; i < t->count; i++) {
        const OsrsThreatWeapon* w = &t->slots[i].w;
        assert(fabsf(block[i * RF_THREAT_HIS_WIDTH + 3] - osrs_threat_hit_chance(w, &ours)) < 1e-6f);
        if (w->spec_hits > 0) assert(fabsf(block[i * RF_THREAT_HIS_WIDTH + 10] - osrs_threat_spec_hit_chance(w, &ours)) < 1e-6f);
    }
    uint16_t items[RF_THREAT_OURS_SLOTS];
    int n = 0;
    items[n++] = p->equipped[GEAR_SLOT_WEAPON];
    for (int item = 0; item < NUM_ITEMS + OSRS_STAT_ITEM_SLOTS && n < RF_THREAT_OURS_SLOTS; item++) {
        if (item == items[0]) continue;
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
            const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
            if (m->gear_slot == GEAR_SLOT_WEAPON && m->item_idx == item) { items[n++] = (uint8_t)item; break; }
        }
    }
    OsrsDefenceRolls his = t->his_defence;
    for (int i = 0; i < n; i++) {
        OsrsThreatWeapon w = osrs_threat_weapon(p->equipped, items[i], &us);
        const float* o = block + RF_THREAT_OURS_OFFSET + i * RF_THREAT_OURS_WIDTH;
        assert(o[0] == 1 && fabsf(o[1] - (float)osrs_threat_max_hit_at(&w, RF_THREAT_BASE_HITPOINTS, p->current_hitpoints) / RF_THREAT_DAMAGE_SCALE) < 1e-6f);
        assert(fabsf(o[2] - osrs_threat_hit_chance(&w, &his)) < 1e-6f && fabsf(o[3] - (float)w.speed / RF_THREAT_SPEED_SCALE) < 1e-6f);
        if (w.spec_hits > 0) assert(fabsf(o[6] - osrs_threat_spec_hit_chance(&w, &his)) < 1e-6f);
    }
    if (n < RF_THREAT_OURS_SLOTS) assert(block[RF_THREAT_OURS_OFFSET + n * RF_THREAT_OURS_WIDTH] == 0);
}

/* Every kit plays: 240 ticks against a scripted fighter on our side attacking, no abort; each kit swings (or one side is gone); ranged kits shoot ranged. */
static void test_every_kit_plays(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_put_int((EncounterState*)&state, (EncounterContext*)&context, "live_loadout", 1);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    int swung_kits = 0, ranged_swings = 0, specs = 0;
    for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++) {
        context.opponent_pool = 0;
        context.loadout[1] = &RISKFIGHT_POOL_LOADOUT[i];
        context.opp_kit = NULL;
        riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 7000 + (uint32_t)i);
        context.opp_kit = &RISKFIGHT_POOL[i];
        riskfight_kit_apply(&state, 1, context.opp_kit, 12345u + (uint32_t)i);
        riskfight_observe_visible(&state, 0, 1);
        state.opponent_profile = &RISKFIGHT_HUMAN_PROFILES[i % RISKFIGHT_HUMAN_TRAIN_PROFILES];
        int his_swings = 0, his_ranged = 0;
        for (int tick = 0; tick < 240 && !state.env.episode_over; tick++) {
            int actions[RF_HEADS] = {0};
            actions[RF_PRIMARY] = RF_ATTACK;
            riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
            float obs[RF_OBS_SIZE];
            check_threat_block_against_reference(&state, (riskfight_write_observation(&state, 0, obs), obs));
            for (int side = 0; side < 1; side++) { /* our view: his own (a ranger on Rigour reads 1.25 at the prayer input) only feeds his script */
                riskfight_write_observation(&state, side, obs);
                /* The threat block stays in [0, 1] and nothing is NaN (older inputs such as the age of his last swing scale past 1 by design). */
                for (int j = 0; j < RF_OBS_SIZE; j++) {
                    int in_block = j >= RF_THREAT_START && j < RF_SESSION_START; /* the session block ends in a margin that goes negative */
                    if (!(obs[j] >= (in_block ? 0 : -8) && obs[j] <= (in_block ? 1.0001f : 8))) { printf("kit %d (%s) tick %d obs[%d] = %f\n", i, RISKFIGHT_POOL[i].source, tick, j, obs[j]); abort(); }
                }
            }
            const Player* him = &state.env.players[1];
            his_swings += him->attack_style_this_tick != ATTACK_STYLE_NONE;
            his_ranged += him->attack_style_this_tick == ATTACK_STYLE_RANGED;
            specs += him->used_special_this_tick;
        }
        if (his_swings > 0) swung_kits++;
        ranged_swings += his_ranged;
        if (RISKFIGHT_POOL[i].ranged_level) assert(his_ranged > 0 || state.env.episode_over);
    }
    assert(swung_kits >= RISKFIGHT_POOL_COUNT * 9 / 10);
    printf("play: %d of %d kits swung in 240 ticks, %d ranged swings, %d specials of his\n", swung_kits, RISKFIGHT_POOL_COUNT, ranged_swings, specs);
}

/* The prior the threat block reads: a Dharok's wearer's other weapons come from the pool, summing over the kits that look like him. */
static void test_prior(void) {
    uint16_t visible[NUM_GEAR_SLOTS];
    memset(visible, ITEM_NONE, sizeof(visible));
    visible[GEAR_SLOT_HEAD] = ITEM_HELM_NEITIZNOT;
    visible[GEAR_SLOT_CAPE] = ITEM_INFERNAL_CAPE;
    visible[GEAR_SLOT_NECK] = ITEM_AMULET_OF_STRENGTH;
    visible[GEAR_SLOT_BODY] = ITEM_FIGHTER_TORSO;
    visible[GEAR_SLOT_SHIELD] = ITEM_AVERNIC_DEFENDER;
    visible[GEAR_SLOT_LEGS] = ITEM_DRAGON_PLATELEGS;
    visible[GEAR_SLOT_HANDS] = ITEM_BARROWS_GLOVES;
    visible[GEAR_SLOT_FEET] = ITEM_DRAGON_BOOTS;
    visible[GEAR_SLOT_WEAPON] = ITEM_VOIDWAKER;
    uint16_t items[8];
    float p[8];
    int n = riskfight_pool_prior(visible, items, p, 8);
    assert(n >= 1);
    /* Our own kit seen on him: the ornate maul is the weapon the kits that look like him carry. */
    assert(items[0] == ITEM_GRANITE_MAUL_ORNATE && p[0] > 0.3f && p[0] <= 1);
    for (int i = 1; i < n; i++) assert(p[i] <= p[i - 1] && items[i] != ITEM_VOIDWAKER);
    printf("prior for our own kit on him: ");
    for (int i = 0; i < n; i++) printf("%s %.2f  ", ITEM_DATABASE[items[i]].name, p[i]);
    printf("\n");
}

int main(void) {
    test_kits_consistent();
    test_draws();
    test_every_kit_plays();
    test_prior();
    printf("riskfight pool: passed\n");
    return 0;
}
