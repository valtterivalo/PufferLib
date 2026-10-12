#ifndef RISKFIGHT_SEED_H
#define RISKFIGHT_SEED_H
#include "riskfight_tape.h"

enum { ANIM_EAT = 829, ANIM_VW_SPEC = 11275, ANIM_MAUL_SPEC = 1667, ANIM_VENG_A = 8316, ANIM_VENG_B = 8317 };
enum { CONSUME_HARD, CONSUME_POTION, CONSUME_KARAMBWAN };
enum { HIS_SACKS = 27, HIS_PRAYER = 90, HIS_BOOST = 118, HIS_MAGIC = 99, RAW_ANGLERFISH = 13441, RAW_KARAMBWAN = 3144,
    ARRIVAL_WINDOW = 12, ARRIVAL_AGE = 100 };

static int g_his_boost = 118;

typedef struct {
    float recoil_ring;
    int anglerfish, karambwan, brews, restores, combats;
} SeedPriors;

static const SeedPriors DEFAULT_PRIORS = {0.35f, 12, 4, 3, 2, 1};

typedef struct { uint8_t main, vw, maul, ammo; } HisKit;

static HisKit his_kit(const Tape* tape) {
    const TapeTick* first = tape_tick(tape, tape->lo);
    HisKit kit = {ITEM_NONE, tape_has_anim(tape, ANIM_VW_SPEC), tape_has_anim(tape, ANIM_MAUL_SPEC), ITEM_NONE};
    if (first->opponent_gear_source && first->opponent_gear[3] >= 0)
        kit.main = raw_id_to_item(first->opponent_gear[3]);
    if (kit.main == ITEM_MAGIC_SHORTBOW_I) kit.ammo = ITEM_DRAGON_ARROWS;
    if (kit.main == ITEM_VOIDWAKER) kit.vw = 0;
    return kit;
}

static int consume_of_raw(int raw) {
    return raw == RAW_ANGLERFISH ? CONSUME_HARD : raw == RAW_KARAMBWAN ? CONSUME_KARAMBWAN : CONSUME_POTION;
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

static int armed_after(const Tape* tape, int t) {
    int last_spec_use = -1;
    for (int i = 0; i < tape->anim_count; i++) {
        const TapeAnim* a = &tape->anims[i];
        if (a->side == SIDE_US && a->tick <= t && a->tick > last_spec_use &&
                (a->anim == ANIM_VW_SPEC || a->anim == ANIM_MAUL_SPEC)) last_spec_use = a->tick;
    }
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

/** The heal of one meal served as the opponent fit serves them: a single hard food, hard food and karambwan, hard food and brew, or all three. */
/* Chance a tick whose animation could have hidden a meal held one: Appetite.FITTED.hides. */
static double g_hidden_meal_chance = 0, g_seed_inert = 0.5, g_hidden_gated = 0, g_gate_low = 44, g_gate_high = 60;

static int serve_hitpoints(uint32_t* rng, int hp) {
    static const float shares[4] = {0.15f, 0.34f, 0.34f, 0.17f};
    static const int foods[4] = {1, 3, 5, 7}, heals[3] = {22, 18, 16}, caps[3] = {121, 99, 115};
    float u = (float)(xorshift32(rng) >> 8) / (float)(1 << 24);
    int k = u < shares[0] ? 0 : u < shares[0] + shares[1] ? 1 : u < shares[0] + shares[1] + shares[2] ? 2 : 3;
    for (int f = 0; f < 3; f++)
        if (foods[k] >> f & 1) hp = hp > caps[f] ? hp : (hp + heals[f] < caps[f] ? hp + heals[f] : caps[f]);
    return hp;
}

/** His hitpoints at the end of a tick: drawn inside the health bar band, which only a hit on him refreshes, then raised by one drawn serving for each
 * meal since (a hard food stops at 99, a brew at 115). Without a bar, uniform. The oracle reads them off the bar after the first recorded hit instead. */
static int sample_his_hitpoints(const Tape* tape, const TapeTick* tick, uint32_t* rng) {
    OsrsHealthBarRange bar = osrs_health_bar_range(tick->bar_ratio, tick->bar_scale, 99, 121);
    if (g_ablation.hp == HP_ORACLE) {
        for (int x = tick->tick + 1; x <= tick->tick + 10 && x <= tape->hi; x++) {
            int damage = tape_damage_on_him(tape, x);
            const TapeTick* after = tape_tick(tape, x);
            if (damage > 0 && after->bar_now_ratio > 0 && after->bar_now_scale > 1 && after->last_bar_hit == x) {
                OsrsHealthBarRange shown = osrs_health_bar_range(after->bar_now_ratio, after->bar_now_scale, 99, 121);
                int hp = (shown.lower + shown.upper) / 2 + damage;
                return hp > 121 ? 121 : hp;
            }
        }
    }
    if (bar.kind != OSRS_HEALTH_BAR_KNOWN) return 60 + (int)(uniform(rng) * 62);
    if (g_ablation.hp == HP_UNIFORM) {
        int hi = bar.upper + 24 * tick->meals_since;
        if (hi > 121) hi = 121;
        return bar.lower + (int)(uniform(rng) * (hi - bar.lower + 1));
    }
    int hp = bar.lower + (int)(uniform(rng) * (bar.upper - bar.lower + 1));
    int meals = tick->meals_since;
    for (int meal = 0; meal < meals; meal++) {
        int served = serve_hitpoints(rng, hp);
        if (uniform(rng) >= g_seed_inert) hp = served;
    }
    if (g_ablation.hp == HP_HIDDEN) {
        int masked = g_unseen_ticks(tape, tick->tick);
        for (int m = 0; m < masked; m++) {
            double wants = g_hidden_gated ? fmax(0, fmin(1, (g_gate_high - hp) / (g_gate_high - g_gate_low))) : 1;
            if (uniform(rng) < g_hidden_meal_chance * wants) hp = serve_hitpoints(rng, hp);
        }
    }
    return hp > 121 ? 121 : hp;
}

static int tape_pending_available(const Tape* tape, int t) {
    const TapeTick* tick = tape_tick(tape, t);
    if (tick->pending_known == 1) return 1;
    if (tick->pending_known != 2 || !tape->observer_series) return 0;
    const GsInfer* inf = &tape->observer_series[t - tape->lo].inf;
    if (inf->tracker_current < 0 || inf->timed < 0) return 0;
    return gs_pending_damage_known(inf, t, GS_PID_LOCAL) &&
        gs_pending_damage_known(inf, t, GS_PID_OPPONENT);
}

static void tape_pending_add(PendingHit* hits, PendingHitMetadata* metadata, int* count,
    int lands, int t, GsSwing swing, int exact) {
    if (lands <= t) return;
    if (*count == MAX_PENDING_HITS) {
        fprintf(stderr, "tape pending queue overflow at tick %d\n", t);
        abort();
    }
    int index = (*count)++;
    hits[index] = (PendingHit){.ticks_until_hit = lands - t - 1, .attack_type = ATTACK_STYLE_NONE,
        .hit_success = 1, .damage = exact ? swing.most : 0};
    metadata[index] = (PendingHitMetadata){.damage_mode = exact ? 1 : 2,
        .damage_min = swing.least, .damage_max = swing.most, .accuracy_bp = (int)lround(swing.accuracy * 10000)};
}

/* Translate the causal observation owner's retained queues; never read tape H/KIT
 * rows to infer a launch, damage roll, or reflection source. */
static void tape_pending_snapshot(const Tape* tape, int t, RiskfightSnapshot* out) {
    if (!tape_pending_available(tape, t)) return;
    out->pending_known = 1;
    if (tape_tick(tape, t)->pending_known == 1) return;
    const GsInfer* now = &tape->observer_series[t - tape->lo].inf;
    out->pending_pid_active = 1;
    for (int holder = 0; holder < 2; holder++) {
        GsPid pid = holder == 0 ? GS_PID_LOCAL : GS_PID_OPPONENT;
        for (int launch = 0; launch < now->local_launch_count; launch++) {
            const GsLocalLaunch* local = &now->local_launches[launch];
            GsSwing swing = local->by_pid[pid];
            for (int arrow = 0; arrow < swing.arrows; arrow++)
                tape_pending_add(out->pending_pid[holder][0], out->pending_pid_metadata[holder][0],
                    &out->pending_pid_count[holder][0], swing.lands + arrow, t, swing, local->exact);
        }
        for (int launch = 0; launch < now->pending_launch_count; launch++) {
            GsSwing swing = now->pending_launches[launch].by_pid[pid];
            for (int arrow = 0; arrow < swing.arrows; arrow++)
                tape_pending_add(out->pending_pid[holder][1], out->pending_pid_metadata[holder][1],
                    &out->pending_pid_count[holder][1], swing.lands + arrow, t, swing, 0);
        }
        if (pid == GS_PID_LOCAL) {
            for (int i = 0; i < now->reflection_count; i++) {
                GsSwing swing = now->reflection_swings[i];
                if (swing.lands <= t) continue;
                tape_pending_add(out->pending_pid[holder][1], out->pending_pid_metadata[holder][1],
                    &out->pending_pid_count[holder][1], swing.lands, t, swing, 1);
                out->pending_pid[holder][1][out->pending_pid_count[holder][1] - 1].kind = OSRS_HIT_VENGEANCE;
            }
        }
    }
}

static void source_actor_snapshot(const GsHealthHistory* health, int combat_level, int dead, uint32_t* rng, RiskfightActorStats* stats, int* hitpoints) {
    if (!health || !health->has_started) abort();
    int base = 0;
    for (int candidate = health->base; candidate >= 10; candidate--) {
        uint64_t alive = health->base_hp_support[candidate][0] & ~UINT64_C(1);
        if (!(alive || health->base_hp_support[candidate][1])) continue;
        if (riskfight_model_actor_stats(combat_level, candidate, stats)) { base = candidate; break; }
    }
    if (!base) { fprintf(stderr, "source health support contradicts observed combat level %d\n", combat_level); abort(); }
    if (dead) { *hitpoints = 0; return; }
    const GsTarget* target = &health->target;
    if (target->has_belief && base == target->base_hp) {
        double total = 0;
        for (int i = 0; i < target->belief_count; i++)
            for (int hp = 1; hp < target->belief[i].levels; hp++) total += target->belief[i].hp[hp];
        if (!(total > 0)) abort();
        double draw = uniform(rng) * total, cumulative = 0;
        for (int i = 0; i < target->belief_count; i++) for (int hp = 1; hp < target->belief[i].levels; hp++) {
            cumulative += target->belief[i].hp[hp];
            if (draw < cumulative) {
                if (!(health->base_hp_support[base][hp / 64] >> (hp % 64) & 1)) abort();
                *hitpoints = hp;
                return;
            }
        }
        abort();
    }
    int count = 0;
    for (int hp = 1; hp <= gs_health_cap(base); hp++) count += (int)(health->base_hp_support[base][hp / 64] >> (hp % 64) & 1);
    if (!count) abort();
    int selected = (int)(uniform(rng) * count);
    for (int hp = 1; hp <= gs_health_cap(base); hp++)
        if (health->base_hp_support[base][hp / 64] >> (hp % 64) & 1)
            if (selected-- == 0) { *hitpoints = hp; return; }
    abort();
}

static void source_actor_without_history(const GsClient* client, uint32_t* rng, RiskfightActorStats* stats, int* hitpoints) {
    for (int base = 99; base >= 10; base--) {
        if (!riskfight_model_actor_stats(client->opponent_combat_level, base, stats)) continue;
        int low = 1, high = gs_health_cap(base);
        if (client->ratio >= 0) {
            if (client->scale <= 1 || client->ratio > client->scale) abort();
            low = client->ratio == 0 ? 0 : client->ratio == 1 ? 1 : (base * (client->ratio - 1) + client->scale - 2) / (client->scale - 1);
            high = client->ratio == 0 ? 0 : client->ratio == client->scale ? gs_health_cap(base) : imin(base, (base * client->ratio - 1) / (client->scale - 1));
        }
        if (high < low) continue;
        *hitpoints = low == high ? low : low + (int)(uniform(rng) * (high - low + 1));
        return;
    }
    fprintf(stderr, "observed actor combat/bar have no compatible physical prior\n");
    abort();
}

static int actor_profile_boost(int preferred, int base) {
    int maximum = base + osrs_super_combat_boost_amount(base);
    return preferred < maximum ? preferred : maximum;
}

static int tape_poison_weapon(int raw) {
    return raw == 27867 || raw == 12904;
}

static int tape_local_poison_cohort(const Tape* tape, int tick) {
    for (int t = tape->lo; t <= tick; t++) {
        const TapeTick* row = tape_tick(tape, t);
        for (int i = 0; i < TAPE_WORN; i++) if (tape_poison_weapon(row->worn[i])) return 1;
        for (int i = 0; i < TAPE_BAG; i++) if (tape_poison_weapon(row->bag[i])) return 1;
    }
    return 0;
}


/* Construct actor/loadout state from validated exposed gear and declared hidden priors.
 * Poison provenance and launched queues are supplied by the owning source adapter. */
static int tape_snapshot_construct(const Tape* tape, int t, const SeedPriors* priors, uint32_t* rng, RiskfightSnapshot* out) {
    memset(out, 0, sizeof(*out));
    const TapeTick* tick = tape_tick(tape, t);
    if (!tick->opponent_gear_source) return 0;
    HisKit kit = his_kit(tape);

    RiskfightLoadout* us = &out->loadout[0];
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) us->equipment[i] = ITEM_NONE;
    for (int i = 0; i < TAPE_WORN; i++)
        if (WORN_TO_GEAR[i] >= 0 && tick->worn[i] >= 0) us->equipment[WORN_TO_GEAR[i]] = raw_id_to_item(tick->worn[i]);
    if (tick->recoil_used >= RECOIL_MAX_CHARGES) us->equipment[GEAR_SLOT_RING] = ITEM_NONE;
    for (int i = 0; i < TAPE_BAG; i++) us->inventory[i] = raw_id_to_content(tick->bag[i]);
    us->hitpoints = tick->hp;
    us->vengeance_sacks = tick->sacks;

    uint8_t his_weapon = tick->opponent_gear[3] < 0 ? ITEM_NONE : raw_id_to_item(tick->opponent_gear[3]);
    RiskfightLoadout* him = &out->loadout[1];
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) him->equipment[i] = ITEM_NONE;
    for (int i = 0; i < tick->opponent_gear_count; i++)
        if (WORN_TO_GEAR[i] >= 0 && tick->opponent_gear[i] >= 0)
            him->equipment[WORN_TO_GEAR[i]] = raw_id_to_item(tick->opponent_gear[i]);
    him->equipment[GEAR_SLOT_AMMO] = kit.ammo;
    int shield = him->equipment[GEAR_SLOT_SHIELD];
    int two_handed = item_is_two_handed(his_weapon);
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
    const GsInfer* source = tape->observer_series ? &tape->observer_series[t - tape->lo].inf : NULL;
    if (source) {
        if (!source->c.local_base_known) { fprintf(stderr, "source snapshot requires recorded local base skills\n"); abort(); }
        const int* base = source->c.local_base;
        out->actor[0] = (RiskfightActorStats){1, source->c.local_combat_level, base[0], base[1], base[2], base[3], base[4], base[5], base[6]};
    }
    if (source && source->health && source->health->has_started)
        source_actor_snapshot(source->health, source->c.opponent_combat_level, source->c.ratio == 0, rng, &out->actor[1], &him->hitpoints);
    else if (source) source_actor_without_history(&source->c, rng, &out->actor[1], &him->hitpoints);
    else him->hitpoints = sample_his_hitpoints(tape, tick, rng);

    Locks lu = locks_after(tape, SIDE_US, t), lh = locks_after(tape, SIDE_HIM, t);
    RiskfightSideSnapshot* su = &out->side[0];
    RiskfightAttackHistory our_history = RISKFIGHT_ATTACK_UNKNOWN;
    int our_clock = 0;
    if (tape->observer_series) {
        GsTimerState timer = tape->observer_series[t - tape->lo].inf.local_timer;
        if (timer.seen && timer.earliest == timer.latest) {
            our_history = RISKFIGHT_ATTACK_OBSERVED;
            our_clock = timer.earliest - t - 1;
        }
    }
    int his_clock = 0;
    if (tape->observer_series) {
        GsTimerState timer = tape->observer_series[t - tape->lo].inf.opponent.timer;
        if (timer.seen) {
            assert(timer.earliest <= timer.latest);
            int deadline = timer.earliest + (int)(uniform(rng) * (timer.latest - timer.earliest + 1));
            his_clock = deadline - t - 1;
        }
    }
    *su = (RiskfightSideSnapshot){
        .hitpoints = tick->hp, .prayer = tick->prayer, .attack = tick->attack, .strength = tick->strength,
        .defence = tick->defence, .magic = tick->magic, .special_energy = tick->spec_us, .special_regen_phase = tick->spec_phase_us,
        .spec_armed = armed_after(tape, t), .veng_active = tick->veng_us, .veng_cooldown = tick->veng_cooldown_us,
        .attack_history = our_history, .attack_timer = our_clock < -1 ? -1 : our_clock, .attack_timer_uncapped = our_clock, .food_timer = lu.food, .potion_timer = lu.potion, .karambwan_timer = lu.karambwan,
        .recoil_damage_used = tick->recoil_used, .offensive_prayer = tick->piety ? OFFENSIVE_PRAYER_PIETY : OFFENSIVE_PRAYER_NONE,
        .overhead_prayer = overhead_of(tick->overhead), .x = 0, .y = 0,
        .interacting = tick->swing_us_tick >= t - ARRIVAL_WINDOW,
        .seen_health_bar = osrs_health_bar_ratio(tick->hp, out->actor[0].present ? out->actor[0].hitpoints : MAXED_BASE_HITPOINTS, OSRS_PLAYER_HEALTH_BAR_SCALE),
        .seen_last_attack_ago = swing_ago(t, tick->swing_us_tick), .seen_last_attack_speed = tick->swing_us_speed,
    };
    out->side[1] = (RiskfightSideSnapshot){
        .hitpoints = him->hitpoints, .prayer = HIS_PRAYER, .attack = g_his_boost, .strength = g_his_boost, .defence = g_ablation.his_defence,
        .magic = HIS_MAGIC, .special_energy = tick->spec_him, .special_regen_phase = tick->spec_phase_him,
        .veng_active = tick->veng_him, .veng_cooldown = tick->veng_cooldown_him,
        .attack_history = RISKFIGHT_ATTACK_LATENT, .attack_timer = his_clock < -1 ? -1 : his_clock, .attack_timer_uncapped = his_clock, .food_timer = lh.food, .potion_timer = lh.potion, .karambwan_timer = lh.karambwan,
        .recoil_damage_used = damage_dealt_before(tape, SIDE_HIM, t), .offensive_prayer = g_ablation.no_piety ? OFFENSIVE_PRAYER_NONE : OFFENSIVE_PRAYER_PIETY,
        .x = tick->ox - tick->ux, .y = tick->oy - tick->uy,
        .interacting = tick->swing_him_tick >= t - ARRIVAL_WINDOW,
        .seen_health_bar = tick->bar_ratio,
        .seen_last_attack_ago = swing_ago(t, tick->swing_him_tick), .seen_last_attack_speed = tick->swing_him_speed,
    };
    if (out->actor[1].present) {
        const RiskfightActorStats* actor = &out->actor[1];
        RiskfightSideSnapshot* side = &out->side[1];
        side->attack = actor_profile_boost(g_his_boost, actor->attack);
        side->strength = actor_profile_boost(g_his_boost, actor->strength);
        int defence_boost = osrs_super_combat_boost_amount(actor->defence);
        int brew_boost = osrs_brew_defence_boost_amount(actor->defence);
        int defence_ceiling = actor->defence + (defence_boost > brew_boost ? defence_boost : brew_boost);
        side->defence = g_ablation.his_defence < defence_ceiling ? g_ablation.his_defence : defence_ceiling;
        int magic_ceiling = actor->magic + osrs_saturated_heart_magic_boost(actor->magic);
        side->magic = HIS_MAGIC < magic_ceiling ? HIS_MAGIC : magic_ceiling;
        side->prayer = HIS_PRAYER < actor->prayer ? HIS_PRAYER : actor->prayer;
    }
    /* Unknown observed PID chooses only a concrete prototype cohort. World draws
     * still use the caller's uncertain PID belief and retain both queue variants. */
    out->pid_holder = tick->pid == 'L' || tick->pid == 'l' ? 0 : tick->pid == 'O' || tick->pid == 'o' ? 1 : (int)(xorshift32(rng) & 1);
    out->seed = xorshift32(rng) | 1;
    return 1;
}

/** Snapshot of the tape at the end of tick t. Hidden values come from the stated priors and the rng. */
static int tape_snapshot(const Tape* tape, int t, const SeedPriors* priors, uint32_t* rng, RiskfightSnapshot* out) {
    const TapeTick* tick = tape_tick(tape, t);
    if (!tape_snapshot_construct(tape, t, priors, rng, out)) return 0;
    int local_poison = tape_local_poison_cohort(tape, t);
    out->side[0].poison_source = tape_local_poison(tick, &out->side[0].poison);
    out->side[1].poison_source = local_poison ? OSRS_POISON_UNKNOWN_STATE : OSRS_POISON_CLEAR_PRIOR;
    if (out->side[0].poison_source == OSRS_POISON_UNKNOWN_STATE ||
        out->side[1].poison_source == OSRS_POISON_UNKNOWN_STATE) return 0;
    tape_pending_snapshot(tape, t, out);
    return out->pending_known;
}
#endif
