#ifndef GS_PLAN_H
#define GS_PLAN_H
/* AutoSpecialAttack.plan: the plans of a Voidwaker wielder with a maul in the bag, from what the client shows of both sides.
 * The press timing the Java planner takes from the hand model is the measured one: a lone Voidwaker spec is armed for the next
 * tick, a maul is processed two ticks on and a Voidwaker spec after it two ticks later (Light #175, #176). */
#include "gs_infer.h"

enum { GS_MAUL_PROCESSED_AFTER = 2, GS_SPEC_AFTER_MAUL = 2, GS_INVENTORY_SIZE = 28, GS_MAUL_STANCES = 3 };

typedef struct { int maul_id; int inventory_count, shield_worn; } GsOwn;

static int gs_protect(const GsClient* c, int damage, GsHeadIcon prayer) { return c->overhead == prayer ? (int)(damage * 0.6) : damage; }

static int gs_ornate_maul(int id) { return id == GS_ITEM_GRANITE_MAUL_PLUS || id == GS_ITEM_GRANITE_MAUL_PRETTY_PLUS; }

/* AutoSpecialAttack.nextSwing: the tick our next swing is processed on, 0 when unknown */
static int gs_next_swing(GsTimerState timer, int tick, int* out) {
    int exact;
    if (gs_timer_exact_next(timer, &exact)) { *out = imax(tick + 1, exact); return 1; }
    GsReading reading = gs_timer_reading(timer, tick);
    if (reading == GS_READING_UNKNOWN || reading == GS_READING_READY) { *out = tick + 1; return 1; }
    return 0;
}

static int gs_landing(int process, GsPid pid) { return gs_lands(process, GS_MELEE, 1, gs_local_lead(pid)); }

/* AutoSpecialAttack.maul: the maul spec's most and chance against his visible gear, empty without a held ornate maul */
static int gs_maul(const GsInfer* s, const GsOwn* own, int* most, double* accuracy) {
    const GsClient* c = &s->c;
    if (!own->maul_id || c->com_mode >= GS_MAUL_STANCES) return 0;
    const GsItem* ours[GS_WORN_SLOTS + 1];
    int count = 0;
    ours[count++] = gs_item(own->maul_id);
    GsBonuses strength = gs_bonuses_of(own->maul_id);
    for (int slot = 0; slot < GS_WORN_SLOTS; slot++) {
        if (c->worn[slot] <= 0 || slot == GS_WORN_WEAPON || slot == 5) continue;
        if (!gs_item(c->worn[slot])->has_stats) return 0;
        ours[count++] = gs_item(c->worn[slot]);
        strength = gs_bonuses_plus(strength, gs_bonuses_of(c->worn[slot]));
    }
    if (!gs_item(own->maul_id)->has_stats) return 0;
    const GsItem* theirs[GS_MAX_EQUIPMENT];
    int theirs_count = 0;
    for (int i = 0; i < c->equipment_count; i++)
        if (c->equipment[i] >= GS_ITEM_OFFSET) {
            if (!gs_item(c->equipment[i] - GS_ITEM_OFFSET)->has_stats) return 0;
            theirs[theirs_count++] = gs_item(c->equipment[i] - GS_ITEM_OFFSET);
        }
    int stance = c->com_mode == 0 ? 3 : 0;
    int prayer = gs_prayer_active(c, GS_PRAYER_PIETY) ? 120 : gs_prayer_active(c, GS_PRAYER_CHIVALRY) || gs_prayer_active(c, GS_PRAYER_INCREDIBLE_REFLEXES) ? 115
        : gs_prayer_active(c, GS_PRAYER_IMPROVED_REFLEXES) ? 110 : gs_prayer_active(c, GS_PRAYER_CLARITY_OF_THOUGHT) ? 105 : 100;
    int defence_bonus = 0;
    for (int i = 0; i < theirs_count; i++) defence_bonus += gs_damage_defence(GS_DAMAGE_CRUSH, theirs[i]);
    int attack_bonus = gs_sum_attack(GS_DAMAGE_CRUSH, ours, count);
    *accuracy = gs_chance(gs_roll(gs_effective_level(c->attack, prayer, stance), attack_bonus), gs_roll(gs_effective_level(120, 125, 3), defence_bonus));
    *most = gs_protect(c, gs_max_normal(GS_W_GRANITE_MAUL, GS_MELEE, gs_local_levels(c), strength), GS_HEAD_MELEE);
    return 1;
}

/* Builds the plans with the cast delaying everything by `extra` ticks, the plans of the Java build lambda. Returns the count. */
static GsSpecHit gs_spec_hit_blocked(Arena* arena, int lands, int least, int most, double accuracy, int immune_through) {
    if (lands > immune_through) return gs_spec_hit_of(arena, lands, least, most, accuracy);
    double* damage = arena_alloc(arena, sizeof(double));
    damage[0] = 1;
    return (GsSpecHit){lands, damage, 1, GS_ORIGIN_OURS};
}

static int gs_spec_plans(Arena* arena, const GsInfer* s, int tick, int extra, const GsOwn* own, GsSpecPlan* plans) {
    int immune = s->opponent.immune_through;
    const GsClient* c = &s->c;
    int energy = gs_spec_energy(c);
    int count = 0;
    GsPid pid = gs_pid_at(s, tick);
    int vw_most = gs_max_normal(GS_W_VOIDWAKER, GS_MELEE, gs_local_levels(c), gs_local_bonuses(c));
    int swing;
    int has_swing = gs_next_swing(s->local_timer, tick, &swing);
    int vw_process = has_swing ? swing : tick + 1;
    int maul_most;
    double maul_accuracy;
    int has_maul = gs_maul(s, own, &maul_most, &maul_accuracy);
    int wieldable = has_maul && (!own->shield_worn || own->inventory_count < GS_INVENTORY_SIZE);
    int cost = wieldable ? gs_weapon_spec_cost(own->maul_id) : 100;
    int vw_least = gs_protect(c, vw_most / 2, GS_HEAD_MAGIC), vw_top = gs_protect(c, vw_most * 3 / 2, GS_HEAD_MAGIC);
    if (has_swing && tick + 1 + extra <= vw_process) {
        GsSpecHit* hit = arena_alloc(arena, sizeof(GsSpecHit));
        *hit = gs_spec_hit_blocked(arena, gs_landing(vw_process, pid), vw_least, vw_top, 1.0, immune);
        plans[count++] = (GsSpecPlan){GS_CHOICE_VOIDWAKER, hit, 1};
    }
    if (wieldable) {
        int maul_process = tick + GS_MAUL_PROCESSED_AFTER + extra;
        GsSpecHit* hits = arena_alloc(arena, 2 * sizeof(GsSpecHit));
        hits[0] = gs_spec_hit_blocked(arena, gs_landing(maul_process, pid), 0, maul_most, maul_accuracy, immune);
        if (energy >= cost) plans[count++] = (GsSpecPlan){GS_CHOICE_MAUL, hits, 1};
        if (energy >= cost + 50 && has_swing) {
            GsSpecHit* both = arena_alloc(arena, 2 * sizeof(GsSpecHit));
            both[0] = hits[0];
            both[1] = gs_spec_hit_blocked(arena, gs_landing(imax(maul_process + GS_SPEC_AFTER_MAUL, swing), pid), vw_least, vw_top, 1.0, immune);
            plans[count++] = (GsSpecPlan){GS_CHOICE_MAUL_VOIDWAKER, both, 2};
        }
    }
    return count;
}
#endif
