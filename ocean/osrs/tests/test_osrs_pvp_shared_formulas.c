#include <assert.h>
#include <string.h>

#include "../osrs_env.h"
#include "osrs_test_check.h"

static Player make_player(void) {
    Player player;
    memset(&player, 0, sizeof(player));
    player.current_attack = 99;
    player.current_strength = 99;
    player.current_ranged = 99;
    player.current_magic = 99;
    return player;
}

int main(void) {
    {
        Player player = make_player();
        player.slot_cached_bonuses.ranged_strength = 80;
        player.fight_style = FIGHT_STYLE_ACCURATE;
        assert(calculate_effective_strength(&player, ATTACK_STYLE_RANGED) == 110);
        assert(calculate_base_max_hit(&player, ATTACK_STYLE_RANGED, 1.0f, 0) == 25);

        player.fight_style = FIGHT_STYLE_RAPID;
        assert(calculate_effective_strength(&player, ATTACK_STYLE_RANGED) == 107);
        assert(calculate_base_max_hit(&player, ATTACK_STYLE_RANGED, 1.0f, 0) == 24);

        uint8_t loadout[NUM_GEAR_SLOTS];
        memset(loadout, ITEM_NONE, sizeof(loadout));
        loadout[GEAR_SLOT_WEAPON] = ITEM_TWISTED_BOW;
        loadout[GEAR_SLOT_AMMO] = ITEM_DRAGON_ARROWS;

        EncounterLoadoutStats accurate;
        EncounterLoadoutStats rapid;
        encounter_compute_loadout_stats(loadout, ATTACK_STYLE_RANGED,
            OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_ACCURATE, 0, &accurate);
        encounter_compute_loadout_stats(loadout, ATTACK_STYLE_RANGED,
            OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_RAPID, 0, &rapid);
        assert(accurate.eff_level == 110 && accurate.max_hit == 25);
        assert(rapid.eff_level == 107 && rapid.max_hit == 24);

        encounter_update_loadout_level(&accurate, OFFENSIVE_PRAYER_NONE, 99, 99);
        encounter_update_loadout_level(&rapid, OFFENSIVE_PRAYER_NONE, 99, 99);
        assert(accurate.eff_level == 110 && accurate.max_hit == 25);
        assert(rapid.eff_level == 107 && rapid.max_hit == 24);
    }
    {
        Player player = make_player();
        player.slot_cached_bonuses.magic_strength = 42;
        player.offensive_prayer = OFFENSIVE_PRAYER_AUGURY;
        assert(calculate_base_max_hit(&player, ATTACK_STYLE_MAGIC, 1.0f, 34) == 49);
        player.offensive_prayer = OFFENSIVE_PRAYER_NONE;
        assert(calculate_base_max_hit(&player, ATTACK_STYLE_MAGIC, 1.0f, 34) == 48);

        uint8_t loadout[NUM_GEAR_SLOTS];
        memset(loadout, ITEM_NONE, sizeof(loadout));
        loadout[GEAR_SLOT_HEAD] = ITEM_ANCESTRAL_HAT;
        loadout[GEAR_SLOT_BODY] = ITEM_ANCESTRAL_TOP;
        loadout[GEAR_SLOT_LEGS] = ITEM_ANCESTRAL_BOTTOM;
        loadout[GEAR_SLOT_NECK] = ITEM_OCCULT_NECKLACE;
        loadout[GEAR_SLOT_WEAPON] = ITEM_TUMEKENS_SHADOW;

        EncounterLoadoutStats stats;
        encounter_compute_loadout_stats(loadout, ATTACK_STYLE_MAGIC,
            OFFENSIVE_PRAYER_AUGURY, 99, FIGHT_STYLE_ACCURATE, 34, &stats);
        assert(stats.strength_bonus == 42 && stats.max_hit == 49);
        encounter_update_loadout_level(&stats, OFFENSIVE_PRAYER_AUGURY, 99, 99);
        assert(stats.strength_bonus == 42 && stats.max_hit == 49);
        encounter_update_loadout_level(&stats, OFFENSIVE_PRAYER_NONE, 99, 99);
        assert(stats.strength_bonus == 42 && stats.max_hit == 48);
        assert(osrs_offensive_magic_dmg_bonus(OFFENSIVE_PRAYER_AUGURY) == 4);
        assert(osrs_offensive_magic_dmg_bonus(OFFENSIVE_PRAYER_MAGIC_LOW) == 2);
    }
    {
        const OffensivePrayer prayers[] = {
            OFFENSIVE_PRAYER_NONE,
            OFFENSIVE_PRAYER_MAGIC_LOW,
            OFFENSIVE_PRAYER_AUGURY,
        };

        Player player = make_player();
        EncounterLoadoutStats loadout = {
            .style = ATTACK_STYLE_MAGIC,
            .fight_style = FIGHT_STYLE_ACCURATE,
        };
        for (int base_damage = 1; base_damage <= 60; base_damage++) {
            for (int gear_bonus = 0; gear_bonus <= 100; gear_bonus++) {
                for (size_t i = 0; i < sizeof(prayers) / sizeof(prayers[0]); i++) {
                    int prayer_bonus = osrs_offensive_magic_dmg_bonus(prayers[i]);
                    int expected = base_damage * (100 + gear_bonus + prayer_bonus) / 100;

                    player.slot_cached_bonuses.magic_strength = gear_bonus;
                    player.offensive_prayer = prayers[i];
                    assert(calculate_base_max_hit(&player, ATTACK_STYLE_MAGIC,
                        1.0f, base_damage) == expected);

                    loadout.spell_base_damage = base_damage;
                    loadout.strength_bonus = gear_bonus;
                    encounter_update_loadout_level(&loadout, prayers[i], 99, 99);
                    assert(loadout.strength_bonus == gear_bonus && loadout.max_hit == expected);
                    assert(osrs_magic_max_hit(base_damage, gear_bonus, prayers[i], 1.0f) == expected);
                }
            }
        }
        assert(osrs_magic_max_hit(31, 42, OFFENSIVE_PRAYER_AUGURY, 1.5f) == 67);
    }
    {
        Player attacker = make_player();
        attacker.fight_style = FIGHT_STYLE_ACCURATE;
        assert(calculate_effective_attack(
            &attacker, ATTACK_STYLE_MAGIC, PVP_MAGIC_ACCURACY_MANUAL_CAST) == 108);
        assert(calculate_effective_attack(
            &attacker, ATTACK_STYLE_MAGIC, PVP_MAGIC_ACCURACY_WEAPON_STYLE) == 110);

        const int cast_spells[] = {OSRS_SPELL_ICE_BARRAGE, OSRS_SPELL_BLOOD_BARRAGE};
        const int cast_kinds[] = {
            OSRS_MAGIC_ATTACK_ANCIENT_ICE,
            OSRS_MAGIC_ATTACK_ANCIENT_BLOOD,
        };
        for (size_t i = 0; i < sizeof(cast_spells) / sizeof(cast_spells[0]); i++) {
            OsrsEnv env;
            memset(&env, 0, sizeof(env));
            env.tick = 10;
            env.rng_state = 8512;
            for (int actor = 0; actor < NUM_AGENTS; actor++) {
                init_player(&env.players[actor]);
                memset(env.players[actor].equipped, ITEM_NONE,
                    sizeof(env.players[actor].equipped));
                env.players[actor].x = FIGHT_AREA_BASE_X + 10 + actor * 9;
                env.players[actor].y = FIGHT_AREA_BASE_Y + 10;
                env.players[actor].frozen_ticks = 1;
                osrs_refresh_player_equipment(&env.players[actor]);
            }
            env.players[0].current_magic = 99;
            env.players[0].fight_style = FIGHT_STYLE_ACCURATE;
            env.players[1].current_defence = 99;
            int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
            actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
            actions[OSRS_HEAD_SPELL] = cast_spells[i];
            execute_switches(&env, 0, actions, NULL);
            execute_attack_combat(&env, 0, actions, NULL);
            Player* p = &env.players[0];
            assert(p->just_attacked && p->magic_type_this_tick == cast_kinds[i]);
            assert(p->num_pending_hits == 1 && !p->pending_hits[0].hit_success);

            uint32_t expected_rng = 8512;
            float roll = (float)xorshift32(&expected_rng) / (float)UINT32_MAX;
            float manual_chance = calculate_hit_chance(
                p, &env.players[1], ATTACK_STYLE_MAGIC, 1.0f,
                PVP_MAGIC_ACCURACY_MANUAL_CAST);
            float weapon_chance = calculate_hit_chance(
                p, &env.players[1], ATTACK_STYLE_MAGIC, 1.0f,
                PVP_MAGIC_ACCURACY_WEAPON_STYLE);
            assert(manual_chance <= roll && roll < weapon_chance);
            assert(env.rng_state == expected_rng);
        }
    }
    {
        OsrsEnv env;
        memset(&env, 0, sizeof(env));
        env.tick = 10;
        env.rng_state = 229;
        for (int i = 0; i < NUM_AGENTS; i++) {
            init_player(&env.players[i]);
            memset(env.players[i].equipped, ITEM_NONE, sizeof(env.players[i].equipped));
            env.players[i].x = FIGHT_AREA_BASE_X + 10 + i;
            env.players[i].y = FIGHT_AREA_BASE_Y + 10;
            env.players[i].frozen_ticks = 1;
            osrs_refresh_player_equipment(&env.players[i]);
        }
        slot_equip_item(&env.players[0], GEAR_SLOT_WEAPON, ITEM_VOLATILE_STAFF);
        env.players[0].special_energy = 100;
        env.players[0].fight_style = FIGHT_STYLE_ACCURATE;
        env.players[1].current_defence = 99;
        assert(calculate_effective_attack(
            &env.players[0], ATTACK_STYLE_MAGIC, PVP_MAGIC_ACCURACY_WEAPON_STYLE) == 110);

        int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
        actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
        actions[OSRS_HEAD_SPECIAL] = 1;
        execute_switches(&env, 0, actions, NULL);
        execute_attack_combat(&env, 0, actions, NULL);
        assert(env.players[0].just_attacked);
        assert(env.players[0].last_attack_style == ATTACK_STYLE_MAGIC);
        assert(env.players[0].used_special_this_tick);
        assert(env.players[0].num_pending_hits == 1);
        assert(env.players[0].pending_hits[0].is_special);
        assert(env.players[0].pending_hits[0].hit_success);
        assert(env.players[0].pending_hits[0].damage == 6);
        uint32_t expected_rng = 229;
        assert((xorshift32(&expected_rng) & 0xffffu) == 48523);
        assert(xorshift32(&expected_rng) == 2466456951u);
        assert(env.rng_state == expected_rng);
    }
    return 0;
}
