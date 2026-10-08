#include "../osrs_env.h"
#include "osrs_test_check.h"

static OsrsEnv env;

typedef enum { OPEN_ARENA, LOW_OBSTACLE, PROJECTILE_OBSTACLE } FixtureGeometry;

static uint32_t flags(void* context, int x, int y) {
    FixtureGeometry geometry = *(FixtureGeometry*)context;
    if (x != 4 || geometry == OPEN_ARENA) return 0;
    return COLLISION_BLOCKED | (geometry == PROJECTILE_OBSTACLE
        ? COLLISION_IMPENETRABLE_BLOCKED : 0);
}

static uint32_t projectile_flags(void* context, int x, int y) {
    FixtureGeometry geometry = *(FixtureGeometry*)context;
    return x == 4 && geometry == PROJECTILE_OBSTACLE ? LOS_FULL_MASK : 0;
}

static void reset(uint8_t weapon, int actor, int dx, int dy) {
    memset(&env, 0, sizeof(env));
    env.tick = 10;
    env.rng_state = 12345;
    for (int side = 0; side < NUM_AGENTS; side++) {
        init_player(&env.players[side]);
        env.pvp_runtime.walk_dest_x[side] = -1;
        env.pvp_runtime.walk_dest_y[side] = -1;
        memset(env.players[side].equipped, ITEM_NONE, sizeof(env.players[side].equipped));
        env.players[side].x = 3;
        env.players[side].y = 3;
        env.players[side].frozen_ticks = 1;
        osrs_refresh_player_equipment(&env.players[side]);
    }
    env.players[1 - actor].x += dx;
    env.players[1 - actor].y += dy;
    slot_equip_item(&env.players[actor], GEAR_SLOT_WEAPON, weapon);
    int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
    execute_switches(&env, actor, actions, NULL);
}

int main(void) {
    const struct {
        uint8_t weapon;
        int dx, dy, spell;
        FixtureGeometry geometry;
        int attacks, speed;
        AttackStyle style;
    } cases[] = {
        {ITEM_NOXIOUS_HALBERD, 2, 0, OSRS_SPELL_NONE, OPEN_ARENA, 1, 5, ATTACK_STYLE_MELEE},
        {ITEM_NOXIOUS_HALBERD, 2, 2, OSRS_SPELL_NONE, OPEN_ARENA, 1, 5, ATTACK_STYLE_MELEE},
        {ITEM_NOXIOUS_HALBERD, -2, -1, OSRS_SPELL_NONE, OPEN_ARENA, 1, 5, ATTACK_STYLE_MELEE},
        {ITEM_NOXIOUS_HALBERD, 2, -1, OSRS_SPELL_NONE, OPEN_ARENA, 1, 5, ATTACK_STYLE_MELEE},
        {ITEM_NOXIOUS_HALBERD, 1, 2, OSRS_SPELL_NONE, OPEN_ARENA, 1, 5, ATTACK_STYLE_MELEE},
        {ITEM_NOXIOUS_HALBERD, 0, 2, OSRS_SPELL_NONE, OPEN_ARENA, 1, 5, ATTACK_STYLE_MELEE},
        {ITEM_NOXIOUS_HALBERD, 3, 0, OSRS_SPELL_NONE, OPEN_ARENA, 0, 0, ATTACK_STYLE_NONE},
        {ITEM_WHIP, 1, 1, OSRS_SPELL_NONE, OPEN_ARENA, 0, 0, ATTACK_STYLE_NONE},
        {ITEM_WHIP, 2, 0, OSRS_SPELL_NONE, OPEN_ARENA, 0, 0, ATTACK_STYLE_NONE},
        {ITEM_TOXIC_STAFF_OF_DEAD, 1, 0, OSRS_SPELL_NONE, OPEN_ARENA, 1, 4, ATTACK_STYLE_MELEE},
        {ITEM_TOXIC_STAFF_OF_DEAD, 2, 0, OSRS_SPELL_NONE, OPEN_ARENA, 0, 0, ATTACK_STYLE_NONE},
        {ITEM_TOXIC_STAFF_OF_DEAD, 2, 0, OSRS_SPELL_ICE_BARRAGE, OPEN_ARENA, 1, 5, ATTACK_STYLE_MAGIC},
        {ITEM_TOXIC_STAFF_OF_DEAD, 2, 0, OSRS_SPELL_BLOOD_BARRAGE, OPEN_ARENA, 1, 5, ATTACK_STYLE_MAGIC},
        {ITEM_VOIDWAKER, 8, 0, OSRS_SPELL_ICE_BARRAGE, OPEN_ARENA, 1, 5, ATTACK_STYLE_MAGIC},
        {ITEM_NONE, 8, 0, OSRS_SPELL_BLOOD_BARRAGE, OPEN_ARENA, 1, 5, ATTACK_STYLE_MAGIC},
        {ITEM_NOXIOUS_HALBERD, 8, 0, OSRS_SPELL_ICE_BARRAGE, OPEN_ARENA, 1, 5, ATTACK_STYLE_MAGIC},
        {ITEM_NOXIOUS_HALBERD, 2, 0, OSRS_SPELL_NONE, LOW_OBSTACLE, 1, 5, ATTACK_STYLE_MELEE},
        {ITEM_NOXIOUS_HALBERD, 2, 0, OSRS_SPELL_NONE, PROJECTILE_OBSTACLE, 0, 0, ATTACK_STYLE_NONE},
        {ITEM_BOW_OF_FAERDHINEN_C, 2, 0, OSRS_SPELL_NONE, LOW_OBSTACLE, 1, 4, ATTACK_STYLE_RANGED},
        {ITEM_BOW_OF_FAERDHINEN_C, 2, 0, OSRS_SPELL_NONE, PROJECTILE_OBSTACLE, 0, 0, ATTACK_STYLE_NONE},
        {ITEM_TOXIC_STAFF_OF_DEAD, 2, 0, OSRS_SPELL_ICE_BARRAGE, LOW_OBSTACLE, 1, 5, ATTACK_STYLE_MAGIC},
        {ITEM_TOXIC_STAFF_OF_DEAD, 2, 0, OSRS_SPELL_ICE_BARRAGE, PROJECTILE_OBSTACLE, 0, 0, ATTACK_STYLE_NONE},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        FixtureGeometry geometry = cases[i].geometry;
        EncounterArenaTopologyBuildSpec spec = {.origin_x = 0, .origin_y = 0,
            .width = 12, .height = 12, .max_footprint_size = 1, .revision = 1,
            .tile_flags = flags, .tile_flags_ctx = &geometry,
            .los_tile_flags = projectile_flags, .los_tile_flags_ctx = &geometry,
            .los_build_mode = ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_FLAGGED};
        EncounterArenaTopology* topology = encounter_arena_topology_build(&spec);
        encounter_arena_topology_finalize(topology);
        for (int actor = 0; actor < NUM_AGENTS; actor++) {
            for (int pid = 0; pid < NUM_AGENTS; pid++) {
                reset(cases[i].weapon, actor, cases[i].dx, cases[i].dy);
                env.pid_holder = pid;
                int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
                actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
                actions[OSRS_HEAD_SPELL] = cases[i].spell;
                execute_attack_combat(&env, actor, actions, topology, NULL);
                Player* player = &env.players[actor];
                CHECK("attack respects weapon reach and supplied topology", player->just_attacked == cases[i].attacks);
                CHECK("executed style matches cast or whack", player->last_attack_style == cases[i].style);
                if (cases[i].attacks) {
                    CHECK("spell and weapon cooldown stay separate", player->attack_timer == cases[i].speed - 1);
                    CHECK("executed attack queues a hit", player->num_pending_hits == 1);
                }
            }
        }
        free(topology);
    }
    FixtureGeometry geometry = OPEN_ARENA;
    EncounterArenaTopologyBuildSpec spec = {.origin_x = 0, .origin_y = 0,
        .width = 12, .height = 12, .max_footprint_size = 1, .revision = 2,
        .tile_flags = flags, .tile_flags_ctx = &geometry,
        .los_build_mode = ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_FLAGGED};
    EncounterArenaTopology* topology = encounter_arena_topology_build(&spec);
    encounter_arena_topology_finalize(topology);
    const struct {uint8_t weapon; int spell, speed; FightStyle stance;} timing[] = {
        {ITEM_BOW_OF_FAERDHINEN, OSRS_SPELL_NONE, 4, FIGHT_STYLE_RAPID},
        {ITEM_BOW_OF_FAERDHINEN_C, OSRS_SPELL_NONE, 4, FIGHT_STYLE_RAPID},
        {ITEM_BOW_OF_FAERDHINEN, OSRS_SPELL_NONE, 5, FIGHT_STYLE_ACCURATE},
        {ITEM_RUNE_CROSSBOW, OSRS_SPELL_NONE, 5, FIGHT_STYLE_RAPID},
        {ITEM_TOXIC_STAFF_OF_DEAD, OSRS_SPELL_NONE, 4, FIGHT_STYLE_ACCURATE},
        {ITEM_TOXIC_STAFF_OF_DEAD, OSRS_SPELL_ICE_BARRAGE, 5, FIGHT_STYLE_RAPID},
    };
    for (size_t i = 0; i < sizeof(timing) / sizeof(*timing); i++) {
        for (int actor = 0; actor < NUM_AGENTS; actor++) {
            for (int pid = 0; pid < NUM_AGENTS; pid++) {
                reset(timing[i].weapon, actor, 1, 0);
                env.pid_holder = pid;
                Player* player = &env.players[actor];
                player->fight_style = timing[i].stance;
                player->frozen_ticks = 100;
                int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
                actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
                actions[OSRS_HEAD_SPELL] = timing[i].spell;
                execute_attack_combat(&env, actor, actions, topology, NULL);
                CHECK("first attack starts the cooldown", player->just_attacked);
                for (int elapsed = 1; elapsed <= timing[i].speed; elapsed++) {
                    env.tick++;
                    reset_tick_flags(player);
                    update_timers(player);
                    execute_attack_combat(&env, actor, actions, topology, NULL);
                    CHECK("no early attack and exact earliest next attack", player->just_attacked == (elapsed == timing[i].speed));
                }
            }
        }
    }
    for (int actor = 0; actor < NUM_AGENTS; actor++) {
        reset(ITEM_NOXIOUS_HALBERD, actor, 3, 0);
        env.players[actor].frozen_ticks = 0;
        int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
        actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
        OsrsActorRouteCache cache = {0};
        execute_attack_movement(&env, actor, actions, topology, &cache);
        CHECK("halberd chase stops after reaching two-tile attack range", chebyshev_distance(env.players[actor].x, env.players[actor].y,
            env.players[1 - actor].x, env.players[1 - actor].y) == 2);
        execute_attack_combat(&env, actor, actions, topology, &cache);
        CHECK("halberd attacks after range-aware chase", env.players[actor].just_attacked);
        OsrsAttackTarget target;
        CHECK("shared route lookup finds the counterpart", pvp_lookup_attack_target(&env, 1 - actor, &target));
        CHECK("shared route lookup retains two-tile item range", target.attack_range == 2);
    }
    CHECK("Bowfa longrange respects maximum ranged reach", pvp_weapon_attack_range(ITEM_BOW_OF_FAERDHINEN,
        ATTACK_STYLE_RANGED, FIGHT_STYLE_LONGRANGE) == COMBAT_MAX_RANGED_ATTACK_RANGE);
    CHECK("crossbow longrange adds two tiles", pvp_weapon_attack_range(ITEM_RUNE_CROSSBOW,
        ATTACK_STYLE_RANGED, FIGHT_STYLE_LONGRANGE) == get_item(ITEM_RUNE_CROSSBOW)->attack_range + 2);
    for (uint32_t seed = 1; seed <= 16; seed++) {
        int damage = 0;
        uint32_t rng = 0;
        for (int variant = 0; variant < 2; variant++) {
            reset(variant ? ITEM_BOW_OF_FAERDHINEN_C : ITEM_BOW_OF_FAERDHINEN, 0, 2, 0);
            env.rng_state = seed;
            slot_equip_item(&env.players[0], GEAR_SLOT_BODY, variant ? ITEM_HEFIN_CRYSTAL_BODY : ITEM_CRYSTAL_BODY);
            slot_equip_item(&env.players[0], GEAR_SLOT_LEGS, variant ? ITEM_ITHELL_CRYSTAL_LEGS : ITEM_CRYSTAL_LEGS);
            OsrsEquipmentEffectProfile profile;
            osrs_derive_equipment_effect_profile(env.players[0].equipped, &profile);
            CHECK("cosmetic crystal variants preserve set effect", profile.crystal_armour_points == 5);
            int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
            actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
            execute_attack_combat(&env, 0, actions, topology, NULL);
            if (variant == 0) {
                damage = env.players[0].pending_hits[0].damage;
                rng = env.rng_state;
            } else {
                CHECK("corrupted Bowfa and cosmetic armour preserve damage rolls", env.players[0].pending_hits[0].damage == damage);
                CHECK("cosmetic variants preserve combat RNG consumption", env.rng_state == rng);
            }
        }
    }
    free(topology);
    return osrs_test_summary();
}
