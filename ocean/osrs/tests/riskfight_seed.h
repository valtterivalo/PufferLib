#ifndef RISKFIGHT_SEED_H
#define RISKFIGHT_SEED_H
#include "riskfight_tape.h"

enum { ANIM_EAT = 829, ANIM_VW_SPEC = 11275, ANIM_MAUL_SPEC = 1667, ANIM_VENG_A = 8316, ANIM_VENG_B = 8317 };
enum { CONSUME_HARD, CONSUME_POTION, CONSUME_KARAMBWAN };
enum { HIS_SACKS = 27, HIS_PRAYER = 90, HIS_BOOST = 118, HIS_MAGIC = 99, RAW_ANGLERFISH = 13441, RAW_KARAMBWAN = 3144,
    ARRIVAL_WINDOW = 12, ARRIVAL_AGE = 100 };

typedef struct {
    float recoil_ring;
    int anglerfish, karambwan, brews, restores, combats;
} SeedPriors;

static const SeedPriors DEFAULT_PRIORS = {0.35f, 12, 4, 3, 2, 1};

typedef struct { uint8_t main, vw, maul, ammo; } HisKit;

static HisKit his_kit(const Tape* tape) {
    HisKit kit = {ITEM_WHIP, tape_has_anim(tape, ANIM_VW_SPEC), tape_has_anim(tape, ANIM_MAUL_SPEC), ITEM_NONE};
    if (tape_has_anim(tape, 1658)) kit.main = ITEM_WHIP;
    else if (tape_has_anim(tape, 7045)) kit.main = ITEM_AGS;
    else if (tape_has_anim(tape, 426)) { kit.main = ITEM_MAGIC_SHORTBOW_I; kit.ammo = ITEM_DRAGON_ARROWS; }
    else if (tape_has_anim(tape, 10172) || tape_has_anim(tape, 424)) kit.main = ITEM_SOULREAPER_AXE;
    else if (tape_has_anim(tape, 390)) kit.main = kit.vw ? ITEM_VOIDWAKER : ITEM_DRAGON_SCIMITAR;
    if (kit.main == ITEM_VOIDWAKER) kit.vw = 0;
    return kit;
}

static int consume_of_raw(int raw) {
    return raw == RAW_ANGLERFISH ? CONSUME_HARD : raw == RAW_KARAMBWAN ? CONSUME_KARAMBWAN : CONSUME_POTION;
}

static int consume_delay(int kind) {
    return kind == CONSUME_HARD ? 3 : kind == CONSUME_KARAMBWAN ? 2 : 0;
}

/** Attack timer after tick t from the swings (timer set at the tick of the swing) and consumes seen so far. */
static int replay_attack_timer(const Tape* tape, char side, int t) {
    int timer = -1;
    for (int x = tape->lo; x <= t; x++) {
        const TapeTick* tick = tape_tick(tape, x);
        if (timer >= 0) timer--;
        if (side == SIDE_US) {
            for (int i = 0; i < tape->action_count; i++) {
                const TapeAction* a = &tape->actions[i];
                if (a->tick == x && a->kind == ACTION_CLICK && timer > 0 &&
                        (a->raw_id == RAW_ANGLERFISH || a->raw_id == RAW_KARAMBWAN))
                    timer += consume_delay(consume_of_raw(a->raw_id));
            }
            if (tick->swing_us_tick == x) timer = tick->swing_us_speed - 1;
        } else {
            for (int i = 0; i < tape->anim_count; i++)
                if (tape->anims[i].tick == x && tape->anims[i].side == side && tape->anims[i].anim == ANIM_EAT && timer > 0)
                    timer += consume_delay(CONSUME_HARD);
            if (tick->swing_him_tick == x) timer = tick->swing_him_speed - 1;
        }
    }
    return timer;
}

typedef struct { int food, potion, karambwan; } Locks;

static Locks locks_after(const Tape* tape, char side, int t) {
    Locks locks = {0, 0, 0};
    for (int x = tape->lo; x <= t; x++) {
        int kind = -1;
        if (side == SIDE_US) {
            for (int i = 0; i < tape->action_count; i++) {
                const TapeAction* a = &tape->actions[i];
                if (a->tick == x && a->kind == ACTION_CLICK && (a->raw_id == RAW_ANGLERFISH || a->raw_id == RAW_KARAMBWAN ||
                        a->raw_id == 6685 || a->raw_id == 6687 || a->raw_id == 6689 || a->raw_id == 6691 ||
                        (a->raw_id >= 3024 && a->raw_id <= 3030) || (a->raw_id >= 12695 && a->raw_id <= 12701)))
                    kind = consume_of_raw(a->raw_id);
            }
        } else {
            for (int i = 0; i < tape->anim_count; i++)
                if (tape->anims[i].tick == x && tape->anims[i].side == side && tape->anims[i].anim == ANIM_EAT) kind = CONSUME_HARD;
        }
        if (locks.food > 0) locks.food--;
        if (locks.potion > 0) locks.potion--;
        if (locks.karambwan > 0) locks.karambwan--;
        if (kind < 0) continue;
        locks.food = 2;
        if (kind != CONSUME_HARD) locks.potion = 2;
        if (kind == CONSUME_KARAMBWAN) locks.karambwan = 1;
    }
    return locks;
}

static int last_anim_tick(const Tape* tape, char side, int t, const int* anims, int count) {
    int best = -1;
    for (int i = 0; i < tape->anim_count; i++) {
        const TapeAnim* a = &tape->anims[i];
        if (a->side != side || a->tick > t || a->tick < best) continue;
        for (int k = 0; k < count; k++) if (a->anim == anims[k]) best = a->tick;
    }
    return best;
}

static const int SWING_ANIMS[] = {390, 1658, 11275, 7045, 426, 10172, 10173, 424, 440, 1667, 7514, 7644, 4230, 1156, 3297, 5865, 5866, 8977, 860, 386};

static int armed_after(const Tape* tape, int t) {
    int last_spec_use = last_anim_tick(tape, SIDE_US, t, (const int[]){ANIM_VW_SPEC, ANIM_MAUL_SPEC}, 2);
    int clicks = 0;
    for (int i = 0; i < tape->action_count; i++)
        clicks += tape->actions[i].kind == ACTION_SPEC && tape->actions[i].tick > last_spec_use && tape->actions[i].tick <= t;
    return clicks & 1;
}

static OverheadPrayer overhead_of(const char* name) {
    return !strcmp(name, "SMITE") ? PRAYER_SMITE : !strcmp(name, "PROTECT_FROM_MELEE") ? PRAYER_PROTECT_MELEE :
        !strcmp(name, "PROTECT_FROM_MISSILES") ? PRAYER_PROTECT_RANGED : !strcmp(name, "PROTECT_FROM_MAGIC") ? PRAYER_PROTECT_MAGIC :
        !strcmp(name, "REDEMPTION") ? PRAYER_REDEMPTION : PRAYER_NONE;
}

static const int WORN_TO_GEAR[TAPE_WORN] = {GEAR_SLOT_HEAD, GEAR_SLOT_CAPE, GEAR_SLOT_NECK, GEAR_SLOT_WEAPON,
    GEAR_SLOT_BODY, GEAR_SLOT_SHIELD, -1, GEAR_SLOT_LEGS, -1, GEAR_SLOT_HANDS, GEAR_SLOT_FEET, -1, GEAR_SLOT_RING};

static float uniform(uint32_t* rng) { return (float)(xorshift32(rng) >> 8) / (float)(1 << 24); }

static int damage_dealt_before(const Tape* tape, char side, int t) {
    int total = 0;
    for (int i = 0; i < tape->hit_count; i++) {
        const TapeHit* h = &tape->hits[i];
        if (h->side == side && h->tick <= t && h->klass == 'A') total += h->amount / 10 + 1;
    }
    return total > RECOIL_MAX_CHARGES ? RECOIL_MAX_CHARGES : total;
}

static int swing_ago(int t, int swing_tick) { return t - swing_tick > ARRIVAL_AGE ? -1 : t - swing_tick; }

/** His hitpoints drawn inside the health bar band, widened by one serving per meal since the last hit on him. */
static int sample_his_hitpoints(const TapeTick* tick, uint32_t* rng) {
    int lo = 60, hi = 121;
    OsrsHealthBarRange range = osrs_health_bar_range(tick->bar_ratio, tick->bar_scale, 99, 121);
    if (range.kind == OSRS_HEALTH_BAR_KNOWN) {
        lo = range.lower;
        hi = range.upper + 24 * tick->meals_since;
        if (hi > 121) hi = 121;
    }
    return lo + (int)(uniform(rng) * (hi - lo + 1));
}

/** Snapshot of the tape at the end of tick t. Hidden values come from the stated priors and the rng. */
static void tape_snapshot(const Tape* tape, int t, const SeedPriors* priors, uint32_t* rng, RiskfightSnapshot* out) {
    memset(out, 0, sizeof(*out));
    const TapeTick* tick = tape_tick(tape, t);
    HisKit kit = his_kit(tape);

    RiskfightLoadout* us = &out->loadout[0];
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) us->equipment[i] = ITEM_NONE;
    for (int i = 0; i < TAPE_WORN; i++)
        if (WORN_TO_GEAR[i] >= 0 && tick->worn[i] >= 0) us->equipment[WORN_TO_GEAR[i]] = raw_id_to_item(tick->worn[i]);
    if (tick->recoil_used >= RECOIL_MAX_CHARGES) us->equipment[GEAR_SLOT_RING] = ITEM_NONE;
    for (int i = 0; i < TAPE_BAG; i++) us->inventory[i] = raw_id_to_content(tick->bag[i]);
    us->hitpoints = tick->hp;
    us->vengeance_sacks = tick->sacks;

    int his_last_swing = last_anim_tick(tape, SIDE_HIM, t, SWING_ANIMS, sizeof(SWING_ANIMS) / sizeof(int));
    uint8_t his_weapon = kit.main;
    for (int i = 0; i < tape->anim_count; i++) {
        const TapeAnim* a = &tape->anims[i];
        if (a->side != SIDE_HIM || a->tick != his_last_swing) continue;
        if (a->anim == ANIM_VW_SPEC) his_weapon = ITEM_VOIDWAKER;
        else if (a->anim == ANIM_MAUL_SPEC) his_weapon = ITEM_GRANITE_MAUL_ORNATE;
    }
    RiskfightLoadout* him = &out->loadout[1];
    memcpy(him->equipment, us->equipment, sizeof(him->equipment));
    him->equipment[GEAR_SLOT_WEAPON] = his_weapon;
    him->equipment[GEAR_SLOT_AMMO] = kit.ammo;
    int shield = us->equipment[GEAR_SLOT_SHIELD] != ITEM_NONE ? us->equipment[GEAR_SLOT_SHIELD] : ITEM_AVERNIC_DEFENDER;
    int two_handed = item_is_two_handed(his_weapon);
    him->equipment[GEAR_SLOT_SHIELD] = two_handed ? ITEM_NONE : shield;
    int recoil = tick->recoil_evidence > 0 || uniform(rng) < priors->recoil_ring;
    him->equipment[GEAR_SLOT_RING] = recoil ? ITEM_RING_OF_RECOIL : ITEM_NONE;
    int slot = 0;
    uint8_t carried[] = {kit.main, kit.vw ? ITEM_VOIDWAKER : ITEM_NONE, kit.maul ? ITEM_GRANITE_MAUL_ORNATE : ITEM_NONE,
        two_handed ? (uint8_t)shield : ITEM_NONE};
    for (size_t i = 0; i < sizeof(carried); i++)
        if (carried[i] != ITEM_NONE && carried[i] != his_weapon) him->inventory[slot++] = osrs_inventory_content_code_from_item(carried[i]);
    const struct { OsrsConsumableKind kind; int doses, count; } supplies[] = {
        {OSRS_CONSUMABLE_ANGLERFISH, 0, priors->anglerfish}, {OSRS_CONSUMABLE_KARAMBWAN, 0, priors->karambwan},
        {OSRS_CONSUMABLE_BREW, 4, priors->brews}, {OSRS_CONSUMABLE_SUPER_RESTORE, 4, priors->restores},
        {OSRS_CONSUMABLE_SUPER_COMBAT, 4, priors->combats}, {OSRS_CONSUMABLE_VENGEANCE_SACK, 0, 1}, {OSRS_CONSUMABLE_TELEPORT, 0, 1},
    };
    for (size_t k = 0; k < sizeof(supplies) / sizeof(*supplies); k++)
        for (int n = 0; n < supplies[k].count; n++) {
            assert(slot < OSRS_INVENTORY_SIZE);
            him->inventory[slot++] = riskfight_supply_code(supplies[k].kind, supplies[k].doses);
        }
    him->vengeance_sacks = HIS_SACKS;
    him->hitpoints = sample_his_hitpoints(tick, rng);

    Locks lu = locks_after(tape, SIDE_US, t), lh = locks_after(tape, SIDE_HIM, t);
    RiskfightSideSnapshot* su = &out->side[0];
    *su = (RiskfightSideSnapshot){
        .hitpoints = tick->hp, .prayer = tick->prayer, .attack = tick->attack, .strength = tick->strength,
        .defence = tick->defence, .magic = tick->magic, .special_energy = tick->spec_us, .special_regen_phase = tick->spec_phase_us,
        .spec_armed = armed_after(tape, t), .veng_active = tick->veng_us, .veng_cooldown = tick->veng_cooldown_us,
        .attack_timer = replay_attack_timer(tape, SIDE_US, t), .food_timer = lu.food, .potion_timer = lu.potion, .karambwan_timer = lu.karambwan,
        .recoil_damage_used = tick->recoil_used, .offensive_prayer = tick->piety ? OFFENSIVE_PRAYER_PIETY : OFFENSIVE_PRAYER_NONE,
        .overhead_prayer = overhead_of(tick->overhead), .x = 0, .y = 0,
        .interacting = tick->swing_us_tick >= t - ARRIVAL_WINDOW,
        .seen_last_attack_ago = swing_ago(t, tick->swing_us_tick), .seen_last_attack_speed = tick->swing_us_speed,
    };
    out->side[1] = (RiskfightSideSnapshot){
        .hitpoints = him->hitpoints, .prayer = HIS_PRAYER, .attack = HIS_BOOST, .strength = HIS_BOOST, .defence = HIS_BOOST,
        .magic = HIS_MAGIC, .special_energy = tick->spec_him, .special_regen_phase = tick->spec_phase_him,
        .veng_active = tick->veng_him, .veng_cooldown = tick->veng_cooldown_him,
        .attack_timer = replay_attack_timer(tape, SIDE_HIM, t), .food_timer = lh.food, .potion_timer = lh.potion, .karambwan_timer = lh.karambwan,
        .recoil_damage_used = damage_dealt_before(tape, SIDE_HIM, t), .offensive_prayer = OFFENSIVE_PRAYER_PIETY,
        .x = tick->ox - tick->ux, .y = tick->oy - tick->uy,
        .interacting = tick->swing_him_tick >= t - ARRIVAL_WINDOW,
        .seen_last_attack_ago = swing_ago(t, tick->swing_him_tick), .seen_last_attack_speed = tick->swing_him_speed,
    };
    out->pid_holder = tick->pid == 'L' || tick->pid == 'l' ? 0 : tick->pid == 'O' || tick->pid == 'o' ? 1 : (int)(xorshift32(rng) & 1);
    out->seed = xorshift32(rng) | 1;
}
#endif
