#include "../osrs_env.h"
#include "../osrs_player_inventory_use.h"
#include "osrs_test_check.h"

typedef struct {
    const char* source_sha256;
    int actor_trace_id, presence_id;
    int ticks[3], amounts[3];
    OsrsHitKind hit_kind;
    OsrsAfflictionKind affliction;
    int severity;
} RecordedStatusFixture;

typedef struct {
    const RecordedStatusFixture* fixture;
    int events;
} RecordedStatusReplay;

static void observe_recorded_status(void* context, const OsrsPvpHitEvent* event) {
    RecordedStatusReplay* replay = context;
    const RecordedStatusFixture* fixture = replay->fixture;
    assert(replay->events < 3);
    int index = replay->events++;
    CHECK("recorded status tick", event->tick == fixture->ticks[index]);
    CHECK("recorded status typed amount", event->kind == fixture->hit_kind &&
        event->damage == fixture->amounts[index]);
    CHECK("recorded status does not invent attacker", event->source == -1);
}

int main(void) {
    OsrsStatusState state = {0};
    osrs_status_apply_affliction(&state, OSRS_AFFLICTION_POISON, 30, 0, 30, 0);
    for (int tick = 1; tick <= 900; tick++) {
        OsrsStatusDamage damage = osrs_status_tick(&state);
        CHECK("status", damage.poison == (tick % 30 == 0 ? (35 - tick / 30) / 5 : 0));
    }
    CHECK("status", state.kind == OSRS_AFFLICTION_NONE);
    osrs_status_apply_affliction(&state, OSRS_AFFLICTION_VENOM, 6, 0, 12, 0);
    for (int tick = 1; tick <= 300; tick++) {
        if (tick == 18) osrs_status_apply_affliction(&state, OSRS_AFFLICTION_VENOM, 6, 0, 30, 0);
        if (tick == 24) osrs_status_apply_affliction(&state, OSRS_AFFLICTION_POISON, 30, 0, 30, 0);
        OsrsStatusDamage damage = osrs_status_tick(&state);
        int value = 6 + 2 * ((tick - 12) / 30);
        CHECK("status", damage.venom == (tick >= 12 && (tick - 12) % 30 == 0
            ? (value < 20 ? value : 20) : 0));
    }
    int phase = state.damage_ticks;
    osrs_status_cure(&state, 600, 0, OSRS_STATUS_CURE_ANTIPOISON);
    CHECK("status", state.kind == OSRS_AFFLICTION_POISON);
    CHECK("status", osrs_status_next_damage(&state) == 20);
    CHECK("status", state.damage_ticks == phase);
    osrs_status_cure(&state, 600, 0, OSRS_STATUS_CURE_ANTIPOISON);
    CHECK("status", state.kind == OSRS_AFFLICTION_NONE);
    osrs_status_apply_affliction(&state, OSRS_AFFLICTION_POISON, 30, 0, 30, 0);
    CHECK("status", state.kind == OSRS_AFFLICTION_NONE);
    osrs_status_apply_affliction(&state, OSRS_AFFLICTION_VENOM, 6, 0, 30, 0);
    CHECK("status", state.kind == OSRS_AFFLICTION_VENOM);
    osrs_status_cure(&state, 1500, 360, OSRS_STATUS_CURE_ANTIVENOM);
    CHECK("status", state.kind == OSRS_AFFLICTION_NONE);
    for (int tick = 0; tick < 360; tick++) osrs_status_tick(&state);
    CHECK("status", state.venom_immunity_ticks == 0);
    CHECK("status", state.poison_immunity_ticks == 1140);
    osrs_status_apply_affliction(&state, OSRS_AFFLICTION_VENOM, 6, 1, 30, 0);
    CHECK("status", state.kind == OSRS_AFFLICTION_NONE);
    state = (OsrsStatusState){0};
    for (int i = 0; i < 5; i++) osrs_status_apply_burn(&state, 40);
    for (int tick = 1; tick <= 40; tick++) {
        if (tick == 20) osrs_status_apply_burn(&state, 40);
        OsrsStatusDamage damage = osrs_status_tick(&state);
        CHECK("status", damage.burn == (tick % 4 == 0 ? 5 : 0));
    }
    CHECK("status", state.burn_damage_ticks == 0);
    for (int i = 0; i < 5; i++) CHECK("status", state.burn_ticks[i] == 0);
    state = (OsrsStatusState){0};
    osrs_status_apply_burn(&state, 40);
    int total = 0;
    for (int tick = 1; tick <= 42; tick++) {
        if (tick == 3) osrs_status_apply_burn(&state, 40);
        total += osrs_status_tick(&state).burn;
    }
    CHECK("status", total == 20);
    static OsrsEnv env;
    for (int actor = 0; actor < 2; actor++) {
        memset(&env, 0, sizeof(env));
        for (int side = 0; side < 2; side++) {
            init_player(&env.players[side]);
            memset(env.players[side].equipped, ITEM_NONE, sizeof(env.players[side].equipped));
            osrs_refresh_player_equipment(&env.players[side]);
        }
        Player* attacker = &env.players[actor];
        Player* defender = &env.players[1 - actor];
        queue_hit(0, actor, 1 - actor, attacker, defender, 0, ATTACK_STYLE_MAGIC,
            0, 0, 1, 0, 0, 0, 0, 0);
        attacker->pending_hits[0].affliction = OSRS_AFFLICTION_VENOM;
        pvp_process_incoming_hits(&env, 1 - actor);
        CHECK("accurate zero venom landing", defender->status.kind == OSRS_AFFLICTION_VENOM);
        CHECK("fresh venom starts 30 ticks later", defender->status.damage_ticks == 30);
        defender->prayer = PRAYER_PROTECT_MAGIC;
        defender->veng_active = 1;
        slot_equip_item(defender, GEAR_SLOT_RING, ITEM_RING_OF_RECOIL);
        for (int tick = 1; tick <= 30; tick++) pvp_process_incoming_hits(&env, 1 - actor);
        CHECK("venom bypasses protection", defender->current_hitpoints == 93);
        CHECK("venom does not trigger vengeance or recoil", defender->veng_active &&
            defender->num_pending_hits == 0);
        CHECK("venom damage credited to actual inflictor", attacker->total_damage_dealt > 0);
        slot_equip_item(defender, GEAR_SLOT_HEAD, ITEM_SERPENTINE_HELM);
        CHECK("equipping Serp does not cure existing venom", defender->status.kind == OSRS_AFFLICTION_VENOM);
        pvp_apply_drink_one_dose_effect(defender, OSRS_CONSUMABLE_SANFEW);
        CHECK("NH Sanfew converts venom", defender->status.kind == OSRS_AFFLICTION_POISON);
        CHECK("NH Sanfew preserves pending damage", osrs_status_next_damage(&defender->status) == 8);
        pvp_apply_drink_one_dose_effect(defender, OSRS_CONSUMABLE_SANFEW);
        CHECK("second NH Sanfew cures poison", defender->status.kind == OSRS_AFFLICTION_NONE);
        queue_hit(0, actor, 1 - actor, attacker, defender, 10, ATTACK_STYLE_MELEE,
            0, 0, 1, 0, 0, 0, 0, 0);
        attacker->pending_hits[0].affliction = OSRS_AFFLICTION_VENOM;
        pvp_process_incoming_hits(&env, 1 - actor);
        CHECK("equipped Serp prevents new venom", defender->status.kind == OSRS_AFFLICTION_NONE);
        slot_equip_item(defender, GEAR_SLOT_HEAD, ITEM_NONE);
        osrs_status_apply_affliction(&defender->status, OSRS_AFFLICTION_VENOM, 6, 0, 30, actor);
        pvp_apply_drink_one_dose_effect(defender, OSRS_CONSUMABLE_ANTIVENOM_PLUS);
        CHECK("NH anti-venom+ cures", defender->status.kind == OSRS_AFFLICTION_NONE);
        CHECK("NH anti-venom+ immunity", defender->status.poison_immunity_ticks == 1500 &&
            defender->status.venom_immunity_ticks == 360);
        osrs_status_apply_affliction(&defender->status, OSRS_AFFLICTION_VENOM, 6, 0, 30, actor);
        CHECK("anti-venom+ blocks new venom", defender->status.kind == OSRS_AFFLICTION_NONE);
        CHECK("antivenom inventory kind accepted", pvp_drink_kind_available(defender,
            OSRS_CONSUMABLE_ANTIVENOM_PLUS));
        defender->status = (OsrsStatusState){0};
        osrs_status_apply_affliction(&defender->status, OSRS_AFFLICTION_VENOM, 10, 0, 7, actor);
        osrs_player_inventory_drink_effect(defender, OSRS_CONSUMABLE_SANFEW);
        CHECK("RF Sanfew converts venom", defender->status.kind == OSRS_AFFLICTION_POISON &&
            osrs_status_next_damage(&defender->status) == 10 && defender->status.damage_ticks == 7);
        osrs_player_inventory_drink_effect(defender, OSRS_CONSUMABLE_ANTIVENOM_PLUS);
        CHECK("RF anti-venom+ cures", defender->status.kind == OSRS_AFFLICTION_NONE &&
            defender->status.venom_immunity_ticks == 360);
        defender->status = (OsrsStatusState){0};
        osrs_status_apply_burn(&defender->status, 40);
        defender->prayer = PRAYER_PROTECT_RANGED;
        int hp = defender->current_hitpoints;
        for (int tick = 1; tick <= 40; tick++) pvp_process_incoming_hits(&env, 1 - actor);
        CHECK("conditioned burn bypasses ranged protection", defender->current_hitpoints == hp - 10);
        init_player(defender);
        CHECK("reset clears status and immunity", defender->status.kind == OSRS_AFFLICTION_NONE &&
            defender->status.poison_immunity_ticks == 0 && defender->status.burn_damage_ticks == 0);
    }
    const struct {
        uint8_t weapon;
        AttackStyle style;
        int special, magic_type;
        uint32_t seed;
        int accurate;
        OsrsAfflictionKind affliction;
        uint8_t head;
    } procs[] = {
        {ITEM_TOXIC_STAFF_OF_DEAD, ATTACK_STYLE_MELEE, 0, 0, 4, 1, OSRS_AFFLICTION_VENOM},
        {ITEM_TOXIC_STAFF_OF_DEAD, ATTACK_STYLE_MELEE, 0, 0, 1, 1, OSRS_AFFLICTION_NONE},
        {ITEM_TOXIC_STAFF_OF_DEAD, ATTACK_STYLE_MELEE, 0, 0, 12288, 0, OSRS_AFFLICTION_NONE},
        {ITEM_TOXIC_STAFF_OF_DEAD, ATTACK_STYLE_MAGIC, 0, 1, 4, 1, OSRS_AFFLICTION_VENOM},
        {ITEM_TOXIC_STAFF_OF_DEAD, ATTACK_STYLE_MAGIC, 0, 1, 1, 1, OSRS_AFFLICTION_NONE},
        {ITEM_TOXIC_STAFF_OF_DEAD, ATTACK_STYLE_MAGIC, 0, 1, 10368, 0, OSRS_AFFLICTION_NONE},
        {ITEM_NOXIOUS_HALBERD, ATTACK_STYLE_MELEE, 0, 0, 4, 1, OSRS_AFFLICTION_VENOM},
        {ITEM_NOXIOUS_HALBERD, ATTACK_STYLE_MELEE, 0, 0, 2, 1, OSRS_AFFLICTION_NONE},
        {ITEM_NOXIOUS_HALBERD, ATTACK_STYLE_MELEE, 0, 0, 13376, 0, OSRS_AFFLICTION_NONE},
        {ITEM_NOXIOUS_HALBERD, ATTACK_STYLE_MELEE, 0, 0, 3, 1, OSRS_AFFLICTION_NONE},
        {ITEM_NOXIOUS_HALBERD, ATTACK_STYLE_MELEE, 0, 0, 3, 1, OSRS_AFFLICTION_VENOM, ITEM_SERPENTINE_HELM},
        {ITEM_TOXIC_STAFF_OF_DEAD, ATTACK_STYLE_MELEE, 0, 0, 1, 1, OSRS_AFFLICTION_NONE, ITEM_SERPENTINE_HELM},
        {ITEM_DRAGON_DAGGER, ATTACK_STYLE_MELEE, 0, 0, 4, 1, OSRS_AFFLICTION_POISON},
        {ITEM_DRAGON_DAGGER, ATTACK_STYLE_MELEE, 0, 0, 1, 1, OSRS_AFFLICTION_NONE},
        {ITEM_DRAGON_DAGGER, ATTACK_STYLE_MELEE, 0, 0, 11264, 0, OSRS_AFFLICTION_NONE},
        {ITEM_DRAGON_DAGGER, ATTACK_STYLE_MELEE, 1, 0, 21, 1, OSRS_AFFLICTION_POISON},
    };
    for (int actor = 0; actor < 2; actor++) {
        for (size_t c = 0; c < sizeof(procs) / sizeof(*procs); c++) {
            memset(&env, 0, sizeof(env));
            env.rng_state = procs[c].seed;
            for (int side = 0; side < 2; side++) {
                init_player(&env.players[side]);
                memset(env.players[side].equipped, ITEM_NONE, sizeof(env.players[side].equipped));
                env.players[side].x = 3 + side;
                env.players[side].y = 3;
                osrs_refresh_player_equipment(&env.players[side]);
            }
            Player* attacker = &env.players[actor];
            Player* defender = &env.players[1 - actor];
            slot_equip_item(attacker, GEAR_SLOT_WEAPON, procs[c].weapon);
            slot_equip_item(attacker, GEAR_SLOT_HEAD, procs[c].head);
            perform_attack(&env, actor, 1 - actor, procs[c].style,
                procs[c].special, procs[c].magic_type, 1);
            CHECK("actual weapon accuracy outcome", attacker->pending_hits[0].hit_success == procs[c].accurate);
            CHECK("actual weapon affliction proc", attacker->pending_hits[0].affliction == procs[c].affliction);
            slot_equip_item(attacker, GEAR_SLOT_WEAPON, ITEM_WHIP);
            while (attacker->num_pending_hits > 0) pvp_process_incoming_hits(&env, 1 - actor);
            CHECK("launched status survives attacker weapon switch", defender->status.kind == procs[c].affliction);
            if (procs[c].special)
                CHECK("DDS retains normal special energy contract", attacker->special_energy == 75);
        }
        Player* defender = &env.players[1 - actor];
        init_player(defender);
        osrs_status_apply_affliction(&defender->status, OSRS_AFFLICTION_VENOM, 6, 0, 30, actor);
        defender->inventory_cells[0] = osrs_inventory_cell_from_raw_osrs_id(12913);
        int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
        actions[OSRS_HEAD_DRINK] = 1;
        execute_switches(&env, 1 - actor, actions, NULL);
        CHECK("NH inventory antivenom actually consumes dose", osrs_inventory_cell_raw_osrs_id(
            &defender->inventory_cells[0]) == 12915);
        CHECK("NH inventory antivenom actually cures", defender->status.kind == OSRS_AFFLICTION_NONE &&
            defender->status.venom_immunity_ticks == 360);
        init_player(defender);
        osrs_status_apply_affliction(&defender->status, OSRS_AFFLICTION_VENOM, 6, 0, 30, actor);
        defender->inventory_cells[0] = osrs_inventory_cell_from_raw_osrs_id(10925);
        OsrsInventoryUseState inventory_state = {0};
        OsrsTeleportState teleport = {0};
        CHECK("RF inventory Sanfew actually consumes dose", osrs_player_use_inventory(defender,
            &inventory_state, &teleport, 0, 1) == OSRS_INVENTORY_USE_CONSUMED &&
            osrs_inventory_cell_raw_osrs_id(&defender->inventory_cells[0]) == 10927);
        CHECK("RF inventory Sanfew actually converts venom", defender->status.kind == OSRS_AFFLICTION_POISON &&
            osrs_status_next_damage(&defender->status) == 6 && defender->status.poison_immunity_ticks == 600);
        init_player(defender);
        env.players[actor].num_pending_hits = 0;
        queue_hit(0, actor, 1 - actor, &env.players[actor], defender, 0, ATTACK_STYLE_MELEE,
            0, 0, 1, 0, 0, 0, 0, 0);
        env.players[actor].pending_hits[0].affliction = OSRS_AFFLICTION_POISON;
        pvp_process_incoming_hits(&env, 1 - actor);
        CHECK("accurate zero cannot inflict weapon poison", defender->status.kind == OSRS_AFFLICTION_NONE);
    }
    const RecordedStatusFixture recordings[] = {
        {"d016d958aa3fe8ece5de4d671ee2f87c6dc5d1967b43c4d38421ba5552453bbd",
            6, 4, {1978, 2008, 2038}, {6, 8, 10}, OSRS_HIT_VENOM, OSRS_AFFLICTION_VENOM, 6},
        {"d016d958aa3fe8ece5de4d671ee2f87c6dc5d1967b43c4d38421ba5552453bbd",
            3, 1, {1904, 1934, 1964}, {3, 3, 2}, OSRS_HIT_POISON, OSRS_AFFLICTION_POISON, 12},
        {"d016d958aa3fe8ece5de4d671ee2f87c6dc5d1967b43c4d38421ba5552453bbd",
            10, 15, {2012, 2016, 2020}, {1, 1, 1}, OSRS_HIT_BURN, OSRS_AFFLICTION_NONE, 0},
    };
    for (size_t i = 0; i < sizeof(recordings) / sizeof(*recordings); i++) {
        const RecordedStatusFixture* fixture = &recordings[i];
        memset(&env, 0, sizeof(env));
        init_player(&env.players[0]);
        init_player(&env.players[1]);
        env.players[0].status = (OsrsStatusState){.kind = fixture->affliction,
            .severity = fixture->severity, .damage_ticks = 1, .source = -1};
        if (fixture->hit_kind == OSRS_HIT_BURN) {
            osrs_status_apply_burn(&env.players[0].status, 40);
            env.players[0].status.burn_damage_ticks = 1;
        }
        RecordedStatusReplay replay = {.fixture = fixture};
        env.pvp_runtime.hit_observer = observe_recorded_status;
        env.pvp_runtime.hit_observer_context = &replay;
        for (env.tick = fixture->ticks[0]; env.tick <= fixture->ticks[2]; env.tick++)
            pvp_process_incoming_hits(&env, 0);
        CHECK("full recorded typed sequence reproduced", replay.events == 3);
        printf("recorded source=%s actor=%d presence=%d ticks=%d..%d\n",
            fixture->source_sha256, fixture->actor_trace_id, fixture->presence_id,
            fixture->ticks[0], fixture->ticks[2]);
    }
    return osrs_test_summary();
}
