#ifndef RISKFIGHT_OBSERVE_H
#define RISKFIGHT_OBSERVE_H
#include "riskfight_opp.h"
#include "../gearswapper/gs_wire.h"
#include "../gearswapper/gs_plan.h"
#include "../gearswapper/gs_stakes.h"

/* The client's view of a sim step. Our side is read from the sim as the client reads it (our own state is certain), his side only as the
 * client shows it: animations with the priority that hides a meal or a cast, hitsplats, his health bar updated by hits and gone ten
 * ticks after the last, the visible gear without ring and ammunition. The events feed the ported plugin handlers. */
enum { BAR_VISIBLE_TICKS = 9, ANIM_BLOCK = 424, ANIM_EAT_ID = 829, ANIM_CAST_ID = 8317, ANIM_VW_SPEC_ID = 11275, ANIM_MAUL_SPEC_ID = 1667,
    SCALE_BAR = 30, HITSPLAT_DAMAGE = 16, HITSPLAT_BLOCK = 12, WORN_INDEX_COUNT = GS_WORN_SLOTS, MAX_HOLDINGS = 64 };

typedef struct { Observer seed; } ObserverSeed;

static const int WORN_OF_GEAR[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD] = 0, [GEAR_SLOT_CAPE] = 1, [GEAR_SLOT_NECK] = 2, [GEAR_SLOT_WEAPON] = 3, [GEAR_SLOT_BODY] = 4, [GEAR_SLOT_SHIELD] = 5,
    [GEAR_SLOT_LEGS] = 7, [GEAR_SLOT_HANDS] = 9, [GEAR_SLOT_FEET] = 10, [GEAR_SLOT_RING] = 12, [GEAR_SLOT_AMMO] = 13,
};

static int raw_of_item(uint8_t item) { return item == ITEM_NONE ? -1 : ITEM_DATABASE[item].item_id; }

static void observer_register_items(void) {
    static int done;
    if (done) return;
    done = 1;
    gs_register_java_items();
    for (int i = 0; i < NUM_ITEMS; i++) {
        const Item* item = &ITEM_DATABASE[i];
        if (GS_ITEMS[item->item_id].present) continue;
        int stats[14] = {item->attack_stab, item->attack_slash, item->attack_crush, item->attack_ranged, item->attack_magic,
            item->defence_stab, item->defence_slash, item->defence_crush, item->defence_ranged, item->defence_magic, item->melee_strength, item->ranged_strength, 0, 0};
        gs_item_register(item->item_id, item->name, 1, stats, item->magic_damage, item->attack_speed);
    }
}

static int consumable_channel(const OsrsItemContentMetadata* m) {
    switch ((OsrsConsumableKind)m->consumable_kind) {
        case OSRS_CONSUMABLE_BREW: case OSRS_CONSUMABLE_SUPER_RESTORE: case OSRS_CONSUMABLE_SANFEW: case OSRS_CONSUMABLE_SUPER_COMBAT:
        case OSRS_CONSUMABLE_DIVINE_COMBAT: case OSRS_CONSUMABLE_RANGING: case OSRS_CONSUMABLE_DIVINE_RANGING: case OSRS_CONSUMABLE_PRAYER_RESTORE: return 0;
        case OSRS_CONSUMABLE_KARAMBWAN: case OSRS_CONSUMABLE_HALIBUT: return 1;
        case OSRS_CONSUMABLE_ANGLERFISH: case OSRS_CONSUMABLE_MARLIN: case OSRS_CONSUMABLE_SUMMER_PIE: case OSRS_CONSUMABLE_SHARK_FOOD: return 2;
        default: return -1;
    }
}

static void consumable_units(const Player* p, int units[3]) {
    memset(units, 0, 3 * sizeof(int));
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
        int channel = consumable_channel(m);
        if (channel >= 0) units[channel] += channel == 0 ? m->dose_count : 1;
    }
}

static uint64_t prayer_mask(const Player* p) {
    uint64_t mask = 0;
    if (p->offensive_prayer == OFFENSIVE_PRAYER_PIETY) mask |= 1ull << GS_PRAYER_PIETY;
    if (p->offensive_prayer == OFFENSIVE_PRAYER_RIGOUR) mask |= 1ull << GS_PRAYER_RIGOUR;
    if (p->offensive_prayer == OFFENSIVE_PRAYER_AUGURY) mask |= 1ull << GS_PRAYER_AUGURY;
    if (p->prayer == PRAYER_PROTECT_MELEE) mask |= 1ull << GS_PRAYER_PROTECT_FROM_MELEE;
    if (p->prayer == PRAYER_PROTECT_RANGED) mask |= 1ull << GS_PRAYER_PROTECT_FROM_MISSILES;
    if (p->prayer == PRAYER_PROTECT_MAGIC) mask |= 1ull << GS_PRAYER_PROTECT_FROM_MAGIC;
    return mask;
}

static int fight_style_index(FightStyle style) {
    switch (style) {
        case FIGHT_STYLE_ACCURATE: return 0;
        case FIGHT_STYLE_AGGRESSIVE: return 1;
        case FIGHT_STYLE_CONTROLLED: return 2;
        default: return 3;
    }
}

static void observer_client(const RiskfightState* s, const Observer* ob, int tick, GsClient* c) {
    const Player* us = &s->env.players[0];
    const Player* him = &s->env.players[1];
    memset(c, 0, sizeof(*c));
    c->hitpoints = us->current_hitpoints; c->attack = us->current_attack; c->strength = us->current_strength; c->defence = us->current_defence;
    c->ranged = us->current_ranged; c->magic = us->current_magic; c->prayer = us->current_prayer;
    c->prayers = prayer_mask(us);
    c->spec_energy_varp = us->special_energy * 10;
    c->sa_attack = us->spec_armed;
    c->com_mode = fight_style_index(us->fight_style);
    c->lx = us->x; c->ly = us->y;
    int ours = osrs_interaction_active(&us->interaction), his = osrs_interaction_active(&him->interaction);
    c->local_interacting = ours ? 1 : -1;
    for (int i = 0; i < GS_WORN_SLOTS; i++) c->worn[i] = -1;
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) c->worn[WORN_OF_GEAR[slot]] = raw_of_item(us->equipped[slot]);
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        const OsrsInventoryCell* cell = &us->inventory_cells[slot];
        c->inventory[slot] = osrs_inventory_cell_is_empty(cell) ? -1 : (int)osrs_inventory_cell_metadata(cell)->raw_osrs_id;
    }
    c->player_count = 2;
    c->player_id[0] = 0; c->player_id[1] = 1;
    c->player_interacting[0] = c->local_interacting;
    c->player_interacting[1] = his ? 0 : -1;
    c->tracked = ob->inf.tracker_current;
    c->tracked_interacting = c->player_interacting[1];
    c->ox = him->x; c->oy = him->y;
    c->ratio = tick - ob->bar_hit_tick <= BAR_VISIBLE_TICKS ? ob->bar_ratio : -1;
    c->scale = c->ratio >= 0 ? SCALE_BAR : -1;
    c->oweapon = raw_of_item(him->equipped[GEAR_SLOT_WEAPON]);
    c->overhead = him->prayer == PRAYER_PROTECT_MELEE ? GS_HEAD_MELEE : him->prayer == PRAYER_PROTECT_RANGED ? GS_HEAD_RANGED : him->prayer == PRAYER_PROTECT_MAGIC ? GS_HEAD_MAGIC : GS_HEAD_NONE;
    static const int KIT[12] = {GEAR_SLOT_HEAD, GEAR_SLOT_CAPE, GEAR_SLOT_NECK, GEAR_SLOT_WEAPON, GEAR_SLOT_BODY, GEAR_SLOT_SHIELD, -1, GEAR_SLOT_LEGS, -1, GEAR_SLOT_HANDS, GEAR_SLOT_FEET, -1};
    c->equipment_count = 12;
    for (int k = 0; k < 12; k++) {
        int raw = KIT[k] < 0 ? -1 : raw_of_item(him->equipped[KIT[k]]);
        c->equipment[k] = raw < 0 ? -1 : raw + GS_ITEM_OFFSET;
    }
}

static void observer_sync(const RiskfightState* s, Observer* ob, int tick) { observer_client(s, ob, tick, &ob->inf.c); }

static void observer_rival_worn(Observer* ob) {
    if (ob->inf.tracker_current != ob->rival_tracked) { ob->rival_tracked = ob->inf.tracker_current; ob->rival_worn_count = 0; }
    if (ob->inf.tracker_current < 0) return;
    for (int i = 0; i < ob->inf.c.equipment_count; i++) {
        int id = ob->inf.c.equipment[i];
        if (id < GS_ITEM_OFFSET) continue;
        id -= GS_ITEM_OFFSET;
        int known = 0;
        for (int k = 0; k < ob->rival_worn_count; k++) known |= ob->rival_worn[k] == id;
        if (!known) { assert(ob->rival_worn_count < 48); ob->rival_worn[ob->rival_worn_count++] = id; }
    }
}

/* Refreshes the plugin's stakes from what it can price: everything we carry, everything he has been seen wearing, no skull on either,
 * item protection on for both. */
static void observer_stakes(Observer* ob) {
    GsHolding ours[OSRS_INVENTORY_SIZE + GS_WORN_SLOTS], his[48];
    int n = 0, m = 0;
    const GsClient* c = &ob->inf.c;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) if (c->inventory[i] > 0) ours[n++] = (GsHolding){gs_price(c->inventory[i]), 1};
    for (int i = 0; i < GS_WORN_SLOTS; i++) if (c->worn[i] > 0) ours[n++] = (GsHolding){gs_price(c->worn[i]), 1};
    for (int i = 0; i < ob->rival_worn_count; i++) his[m++] = (GsHolding){gs_price(ob->rival_worn[i]), 1};
    ob->stakes = gs_stakes_of(gs_stakes_lost(ours, n, gs_stakes_kept(0, 1)), gs_stakes_lost(his, m, gs_stakes_kept(0, 1)));
}

static int attack_animation(const Player* p) {
    uint8_t weapon = p->equipped[GEAR_SLOT_WEAPON];
    if (p->used_special_this_tick) {
        if (weapon == ITEM_VOIDWAKER) return ANIM_VW_SPEC_ID;
        if (pvp_is_maul(weapon)) return ANIM_MAUL_SPEC_ID;
        int special = osrs_combat_visual_weapon_attack_anim_for_fight_style(weapon, ATTACK_STYLE_MELEE, p->fight_style, 1, OSRS_COMBAT_VISUAL_NO_ANIMATION);
        if (special != OSRS_COMBAT_VISUAL_NO_ANIMATION) return special;
    }
    AttackStyle style = (AttackStyle)p->attack_style_this_tick;
    if (style == ATTACK_STYLE_MAGIC)
        return osrs_combat_visual_magic_attack_anim_for_fight_style(weapon, p->fight_style, p->used_special_this_tick, ANIM_BLOCK);
    return osrs_combat_visual_weapon_attack_anim_for_fight_style(weapon, style, p->fight_style, p->used_special_this_tick, OSRS_PLAYER_UNARMED_ATTACK_ANIM);
}

static int consumption_seen(const RiskfightState* s, int agent) {
    const Player* p = &s->env.players[agent];
    return osrs_consumption_animation_visible(p->current_hitpoints, p->attack_style_this_tick, p->ate_food_this_tick, p->ate_karambwan_this_tick,
        s->inventory_use[agent].potion_animation_tick_plus_one == s->env.tick) && !p->hit_landed_this_tick;
}

/* The animation a player shows this tick, -1 for none: an attack hides a meal, a meal hides a cast, a cast hides the block. */
static int animation_shown(const RiskfightState* s, int agent) {
    const Player* p = &s->env.players[agent];
    if (p->current_hitpoints <= 0) return -1;
    if (p->attack_style_this_tick != ATTACK_STYLE_NONE) return attack_animation(p);
    if (consumption_seen(s, agent)) return ANIM_EAT_ID;
    if (p->cast_veng_this_tick) return ANIM_CAST_ID;
    if (p->hit_landed_this_tick) return ANIM_BLOCK;
    return -1;
}

static void observer_after_tick(Observer* ob, int tick) {
    GsInfer* inf = &ob->inf;
    if (inf->timed >= 0 && inf->c.ratio != 0) gs_eater_observe(inf, tick);
    observer_rival_worn(ob);
    observer_stakes(ob);
}

static void observer_event(const RiskfightState* s, Observer* ob, int tick) { observer_sync(s, ob, tick); ob->events++; }

/* One sim step seen by the client: the events of tick `tick`, then its GameTick. */
static void observer_step(const RiskfightState* s, Observer* ob, int tick) {
    GsInfer* inf = &ob->inf;
    ob->events = 0;
    for (int agent = 0; agent < 2; agent++) {
        int active = osrs_interaction_active(&s->env.players[agent].interaction);
        if (active != ob->interacting[agent]) {
            ob->interacting[agent] = active;
            observer_event(s, ob, tick);
            gs_on_interacting(inf, agent, active ? 1 - agent : -1);
        }
    }
    int shown[2] = {animation_shown(s, 0), animation_shown(s, 1)};
    for (int agent = 0; agent < 2; agent++) {
        if (shown[agent] < 0 || shown[agent] == ANIM_BLOCK) continue;
        observer_event(s, ob, tick);
        gs_on_animation(inf, tick, agent, shown[agent]);
    }
    int our_damage_on_him = 0;
    for (int i = 0; i < ob->hit_count; i++) {
        const ObservedHit* h = &ob->hits[i];
        observer_event(s, ob, tick);
        gs_on_hitsplat(inf, tick, h->target, h->damage, h->damage > 0 ? HITSPLAT_DAMAGE : HITSPLAT_BLOCK, 1);
        if (h->target == 1) {
            ob->bar_hit_tick = tick;
            ob->bar_ratio = osrs_health_bar_ratio(s->env.players[1].current_hitpoints, s->env.players[1].base_hitpoints, SCALE_BAR);
            if (h->source == 0 && h->kind == OSRS_HIT_DIRECT) our_damage_on_him += h->damage;
        }
    }
    for (int agent = 0; agent < 2; agent++) {
        if (shown[agent] != ANIM_BLOCK) continue;
        observer_sync(s, ob, tick);
        gs_on_animation(inf, tick, agent, shown[agent]);
    }
    for (int agent = 0; agent < 2; agent++)
        if (s->env.players[agent].said_taste_vengeance_this_tick) { observer_event(s, ob, tick); gs_on_overhead_text(inf, tick, agent); }
    for (int agent = 0; agent < 2; agent++)
        if (s->env.players[agent].cast_veng_this_tick) { observer_event(s, ob, tick); gs_on_graphic(inf, tick, agent); }
    if (our_damage_on_him > 0) {
        ob->xp_tenths += 15 * our_damage_on_him;
        int xp = ob->xp_tenths / 10;
        if (xp > ob->hp_xp) {
            observer_event(s, ob, tick);
            gs_on_hitpoints_gain(inf, tick, xp - ob->hp_xp, GS_WIRE_COMBAT_LEVEL);
            ob->hp_xp = xp;
        }
    }
    int units[3];
    consumable_units(&s->env.players[0], units);
    if (memcmp(units, ob->units, sizeof(units))) {
        observer_event(s, ob, tick);
        memcpy(ob->units, units, sizeof(units));
        gs_on_inventory(inf, tick, units);
    }
    observer_sync(s, ob, tick);
    gs_on_game_tick(inf, tick);
    observer_after_tick(ob, tick);
    ob->hit_count = 0;
}

/* The client state of a seed from the recording, continued by the sim: our interaction, bag and bar as the sim holds them. */
static void observer_branch(const RiskfightState* s, Observer* ob) {
    for (int agent = 0; agent < 2; agent++) ob->interacting[agent] = osrs_interaction_active(&s->env.players[agent].interaction);
    consumable_units(&s->env.players[0], ob->units);
    memcpy(ob->inf.inventory_units, ob->units, sizeof(ob->units));
    ob->hit_count = 0;
    ob->events = 0;
}

typedef struct { char path[64][512]; long start[64], end[64]; int count; } WireIndex;

static void wire_index_build(const char* path, WireIndex* index) {
    FILE* f = fopen(path, "r");
    assert(f);
    static char line[1 << 16];
    long start = 0;
    index->count = 0;
    for (;;) {
        long at = ftell(f);
        if (!fgets(line, sizeof(line), f)) break;
        if (strncmp(line, "RECORDING ", 10)) continue;
        assert(index->count < 64);
        line[strcspn(line, "\n")] = 0;
        strcpy(index->path[index->count], line + 10);
        index->start[index->count] = start;
        index->end[index->count++] = at;
        start = ftell(f);
    }
    fclose(f);
}

/* The plugin's state after every GameTick of a fight, from the recording's own events: the seeds of the rollouts. */
static void observer_series_build(const char* wire_path, const WireIndex* index, const Tape* tape, Observer* out) {
    char key[64];
    snprintf(key, sizeof(key), "session_%s", tape->session);
    int found = -1;
    for (int i = 0; i < index->count; i++) if (strstr(index->path[i], key)) found = i;
    assert(found >= 0 && "no wire segment for the tape's session");
    FILE* f = fopen(wire_path, "r");
    assert(f);
    fseek(f, index->start[found], SEEK_SET);
    static char line[1 << 16];
    static Observer ob;
    GsWire wire = {0};
    memset(&ob, 0, sizeof(ob));
    gs_infer_init(&ob.inf);
    ob.bar_hit_tick = INT_MIN / 2;
    ob.rival_tracked = -1;
    while (ftell(f) < index->end[found] && fgets(line, sizeof(line), f)) {
        char applied = gs_wire_apply(&ob.inf, &wire, line);
        if (applied == 'H') {
            char copy[1 << 10];
            strncpy(copy, line, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = 0;
            char* t[GS_WIRE_TOKENS];
            gs_wire_split(copy, t);
            if (atoi(t[2]) == ob.inf.tracker_current) ob.bar_hit_tick = atoi(t[1]);
        }
        if (applied != 'T') continue;
        int tick = atoi(strchr(line, ' ') + 1);
        if (ob.inf.c.ratio >= 0) ob.bar_ratio = ob.inf.c.ratio;
        observer_after_tick(&ob, tick);
        if (tick < tape->lo || tick > tape->hi) continue;
        Observer* seed = &out[tick - tape->lo];
        *seed = ob;
        seed->hp_xp = wire.hp_xp;
        seed->xp_tenths = wire.hp_xp * 10;
        seed->interacting[0] = ob.inf.c.local_interacting >= 0;
        seed->interacting[1] = ob.inf.c.tracked >= 0 && ob.inf.c.tracked_interacting >= 0;
        memcpy(seed->units, ob.inf.inventory_units, sizeof(seed->units));
        if (seed->inf.tracker_current >= 0) seed->inf.tracker_current = 1;
        if (seed->inf.timed >= 0) seed->inf.timed = 1;
    }
    fclose(f);
}
#endif
