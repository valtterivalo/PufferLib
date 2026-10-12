#ifndef OSRS_RISKFIGHT_SNAPSHOT_H
#define OSRS_RISKFIGHT_SNAPSHOT_H
#include <limits.h>
#include "riskfight_step.h"

enum { RISKFIGHT_LOAD_TICK = 256 };

typedef enum { RISKFIGHT_ATTACK_ABSENT, RISKFIGHT_ATTACK_OBSERVED, RISKFIGHT_ATTACK_LATENT, RISKFIGHT_ATTACK_UNKNOWN } RiskfightAttackHistory;
typedef struct {
    int present, observed_combat_level;
    int hitpoints, attack, strength, defence, ranged, magic, prayer;
} RiskfightActorStats;

static int riskfight_actor_combat_level(const RiskfightActorStats* stats) {
    int offense = stats->attack + stats->strength;
    int ranged = 3 * stats->ranged / 2, magic = 3 * stats->magic / 2;
    if (ranged > offense) offense = ranged;
    if (magic > offense) offense = magic;
    return (10 * (stats->defence + stats->hitpoints + stats->prayer / 2) + 13 * offense) / 40;
}

/* Maximize the established melee offense, retaining99s whenever compatible;
 * strength breaks offense ties, then prayer and defence. Secondary offense
 * stays as high as the same observed combat budget permits. */
static int riskfight_model_actor_stats(int combat, int hp, RiskfightActorStats* out) {
    if (hp < 10 || hp > 99 || combat < 0 || combat > 126 || (combat && combat < 3)) abort();
    RiskfightActorStats stats = {1, combat, hp, 99, 99, 99, 99, 99, 99};
    if (!combat) { *out = stats; return 1; }
    if (riskfight_actor_combat_level(&stats) < combat) return 0;
    int ceiling = 40 * combat + 39;
    for (int offense = 198; offense >= 2; offense--) {
        int budget = ceiling - 13 * offense;
        if (budget < 10 * (hp + 1)) continue;
        int prayer = 2 * (budget / 10 - hp - 1) + 1;
        stats.prayer = prayer < 99 ? prayer : 99;
        int defence = budget / 10 - hp - stats.prayer / 2;
        stats.defence = defence < 99 ? defence : 99;
        stats.strength = offense > 100 ? 99 : offense - 1;
        stats.attack = offense - stats.strength;
        int secondary = (2 * offense + 1) / 3;
        stats.ranged = stats.magic = secondary < 99 ? secondary : 99;
        if (riskfight_actor_combat_level(&stats) == combat) { *out = stats; return 1; }
    }
    return 0;
}

static void riskfight_apply_actor_stats(Player* player, const RiskfightActorStats* stats) {
    if (stats->present != 0 && stats->present != 1) abort();
    if (!stats->present) return;
    int levels[] = {stats->attack, stats->strength, stats->defence, stats->ranged, stats->magic, stats->prayer};
    if (stats->hitpoints < 10 || stats->hitpoints > 99) abort();
    for (int i = 0; i < 6; i++) if (levels[i] < 1 || levels[i] > 99) abort();
    if (stats->observed_combat_level && riskfight_actor_combat_level(stats) != stats->observed_combat_level) abort();
    player->base_hitpoints = stats->hitpoints;
    player->base_attack = stats->attack; player->base_strength = stats->strength;
    player->base_defence = stats->defence; player->base_ranged = stats->ranged;
    player->base_magic = stats->magic; player->base_prayer = stats->prayer;
    int ranged_ceiling = stats->ranged + osrs_ranging_boost_amount(stats->ranged);
    if (player->current_ranged > ranged_ceiling) player->current_ranged = ranged_ceiling;
}

typedef struct {
    RiskfightAttackHistory attack_history;
    int hitpoints, prayer, attack, strength, defence, magic;
    int special_energy, special_regen_phase, spec_armed;
    int veng_active, veng_cooldown;
    OsrsPoisonState poison;
    OsrsPoisonSource poison_source;
    int attack_timer, attack_timer_uncapped, food_timer, potion_timer, karambwan_timer;
    int recoil_damage_used;
    OffensivePrayer offensive_prayer;
    OverheadPrayer overhead_prayer;
    int x, y, interacting;
    int seen_health_bar, seen_last_attack_ago, seen_last_attack_speed;
    int seen_consume_food, seen_consume_potion, seen_consume_karam, seen_consume_delay;
} RiskfightSideSnapshot;

/** Fight state at the end of one tick. attack_timer is the Player value, -1 when the weapon is ready.
 * seen_* is what the other side has inferred about this side. pid_holder moves first.
 * pending_pid_active retains observer or native-request launch queues by concrete holder/source;
 * otherwise pending is the authoritative holder-independent fixed queue.
 * Metadata is paired by holder/source/slot; zero metadata retains ordinary raw-hit behavior.
 * Loading exposes the ordinary seed-holder queue; sampling consumes the retained hypotheses
 * after the common world's holder is chosen, before macro clones or ordinary rollout steps. */
typedef struct {
    RiskfightLoadout loadout[2];
    RiskfightSideSnapshot side[2];
    RiskfightActorStats actor[2];
    int pid_holder;
    int pending_known;
    int pending_count[2];
    PendingHit pending[2][MAX_PENDING_HITS];
    PendingHitMetadata pending_metadata[2][MAX_PENDING_HITS];
    int pending_pid_active;
    int pending_pid_count[2][2];
    PendingHit pending_pid[2][2][MAX_PENDING_HITS];
    PendingHitMetadata pending_pid_metadata[2][2][MAX_PENDING_HITS];
    uint32_t seed;
} RiskfightSnapshot;

static void riskfight_load_state(RiskfightState* s, RiskfightContext* ctx, const RiskfightSnapshot* snapshot) {
    if (!snapshot->pending_known || snapshot->pid_holder < 0 || snapshot->pid_holder > 1) {
        fprintf(stderr, "riskfight snapshot requires known pending queue and valid PID\n");
        abort();
    }
    int variants = snapshot->pending_pid_active ? 2 : 1;
    for (int i = 0; i < 2; i++) {
        if (!osrs_poison_state_valid(snapshot->side[i].poison) ||
            snapshot->side[i].poison_source < OSRS_POISON_ENGINE_STATE ||
            snapshot->side[i].poison_source > OSRS_POISON_CAUSAL_STATE ||
            snapshot->side[i].poison_source == OSRS_POISON_UNKNOWN_STATE ||
            (i != 0 && snapshot->side[i].poison_source == OSRS_POISON_CAUSAL_STATE)) {
            fprintf(stderr, "invalid snapshot poison state side=%d\n", i);
            abort();
        }
    }
    for (int pid = 0; pid < variants; pid++) for (int i = 0; i < 2; i++) {
        int count = snapshot->pending_pid_active ? snapshot->pending_pid_count[pid][i] : snapshot->pending_count[i];
        const PendingHit* hits = snapshot->pending_pid_active ? snapshot->pending_pid[pid][i] : snapshot->pending[i];
        const PendingHitMetadata* metadata = snapshot->pending_pid_active
            ? snapshot->pending_pid_metadata[pid][i] : snapshot->pending_metadata[i];
        if (count < 0 || count > MAX_PENDING_HITS) {
            fprintf(stderr, "riskfight snapshot pending count out of bounds\n");
            abort();
        }
        for (int j = 0; j < count; j++) {
            const PendingHit* h = &hits[j];
            const PendingHitMetadata* m = &metadata[j];
            if (h->ticks_until_hit < 0 || h->damage < 0 || m->damage_mode < 0 || m->damage_mode > 2 ||
                h->kind < OSRS_HIT_DIRECT || h->kind > OSRS_HIT_POISON ||
                h->attack_type < ATTACK_STYLE_NONE || h->attack_type > ATTACK_STYLE_MAGIC ||
                m->smite_snapshot < 0 || m->smite_snapshot > 2 ||
                (m->damage_mode == 2 && (m->damage_min < 0 || m->damage_max < m->damage_min ||
                    m->damage_max >= INT_MAX || m->accuracy_bp < 0 || m->accuracy_bp > 10000))) {
                fprintf(stderr, "invalid snapshot pending hit source=%d index=%d\n", i, j);
                abort();
            }
        }
    }
    const RiskfightLoadout* previous[2] = {ctx->loadout[0], ctx->loadout[1]};
    for (int i = 0; i < 2; i++) ctx->loadout[i] = &snapshot->loadout[i];
    riskfight_reset((EncounterState*)s, (EncounterContext*)ctx, snapshot->seed);
    for (int i = 0; i < 2; i++) ctx->loadout[i] = previous[i];
    s->env.tick = RISKFIGHT_LOAD_TICK;
    while (s->env.pid_holder != snapshot->pid_holder)
        pvp_reset_priority(&s->env, OSRS_PRIORITY_PVP_WORLD);
    s->pending_pid_active = snapshot->pending_pid_active;
    if (s->pending_pid_active) {
        memcpy(s->pending_pid_count, snapshot->pending_pid_count, sizeof(s->pending_pid_count));
        for (int pid = 0; pid < 2; pid++) for (int source = 0; source < 2; source++) {
            memcpy(s->pending_pid[pid][source], snapshot->pending_pid[pid][source],
                (size_t)s->pending_pid_count[pid][source] * sizeof(PendingHit));
            memcpy(s->pending_pid_metadata[pid][source], snapshot->pending_pid_metadata[pid][source],
                (size_t)s->pending_pid_count[pid][source] * sizeof(PendingHitMetadata));
        }
    }
    for (int i = 0; i < 2; i++) {
        const RiskfightSideSnapshot* side = &snapshot->side[i];
        Player* p = &s->env.players[i];
        riskfight_apply_actor_stats(p, &snapshot->actor[i]);
        p->num_pending_hits = snapshot->pending_pid_active
            ? snapshot->pending_pid_count[snapshot->pid_holder][i] : snapshot->pending_count[i];
        const PendingHit* hits = snapshot->pending_pid_active
            ? snapshot->pending_pid[snapshot->pid_holder][i] : snapshot->pending[i];
        memcpy(p->pending_hits, hits, (size_t)p->num_pending_hits * sizeof(PendingHit));
        memset(s->env.pending_hit_metadata[i], 0, sizeof(s->env.pending_hit_metadata[i]));
        const PendingHitMetadata* metadata = snapshot->pending_pid_active
            ? snapshot->pending_pid_metadata[snapshot->pid_holder][i] : snapshot->pending_metadata[i];
        memcpy(s->env.pending_hit_metadata[i], metadata,
            (size_t)p->num_pending_hits * sizeof(PendingHitMetadata));
        p->current_hitpoints = side->hitpoints;
        p->current_prayer = side->prayer;
        p->current_attack = side->attack;
        p->current_strength = side->strength;
        p->current_defence = side->defence;
        p->current_magic = side->magic;
        p->special_energy = side->special_energy;
        p->spec_regen_active = side->special_energy < 100;
        p->item_effect_state.special_regen_ticks = side->special_regen_phase;
        p->spec_armed = side->spec_armed;
        p->veng_active = side->veng_active;
        p->veng_cooldown = side->veng_cooldown;
        s->poison[i] = side->poison;
        s->poison_source[i] = side->poison_source;
        assert(side->attack_history != RISKFIGHT_ATTACK_UNKNOWN);
        p->has_attack_timer = side->attack_history != RISKFIGHT_ATTACK_ABSENT;
        p->attack_timer = side->attack_timer;
        p->attack_timer_uncapped = side->attack_timer_uncapped;
        p->food_timer = side->food_timer;
        p->potion_timer = side->potion_timer;
        p->karambwan_timer = side->karambwan_timer;
        p->offensive_prayer = side->offensive_prayer;
        p->prayer = side->overhead_prayer;
        p->x = FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH / 2 + side->x;
        p->y = FIGHT_AREA_BASE_Y + FIGHT_AREA_HEIGHT / 2 + side->y;
        p->dest_x = p->x;
        p->dest_y = p->y;
        if (side->interacting) osrs_interaction_set(&p->interaction, 1 - i);
        if (side->seen_last_attack_ago >= 0)
            s->env.pvp_runtime.maul[i] = osrs_granite_maul_record_attack(
                s->env.pvp_runtime.maul[i], RISKFIGHT_LOAD_TICK - side->seen_last_attack_ago, 1 - i);
        p->item_effect_state.recoil_damage_used = side->recoil_damage_used;
        if (p->equipment_effect_profile.recoil_source == OSRS_RECOIL_SOURCE_RING_OF_RECOIL)
            p->item_effect_state.recoil_charges = RECOIL_MAX_CHARGES - side->recoil_damage_used;
        s->inventory_use[i].divine_combat_ticks = 0;
    }
    for (int viewer = 0; viewer < 2; viewer++) {
        const RiskfightSideSnapshot* seen = &snapshot->side[1 - viewer];
        RiskfightVisibleOpponent* v = &s->visible[viewer];
        riskfight_observe_visible(s, viewer, 1);
        v->health_bar = seen->seen_health_bar;
        assert(seen->seen_last_attack_ago < RISKFIGHT_LOAD_TICK);
        v->last_attack_tick = seen->seen_last_attack_ago >= 0 ? RISKFIGHT_LOAD_TICK - seen->seen_last_attack_ago : -1;
        v->last_attack_speed = seen->seen_last_attack_speed;
        v->consume_food_est = seen->seen_consume_food;
        v->consume_potion_est = seen->seen_consume_potion;
        v->consume_karam_est = seen->seen_consume_karam;
        v->consume_delay_est = seen->seen_consume_delay;
    }
}
/* Initial launch hypotheses belong to one common world's PID, never to future live queues. */
static void riskfight_resolve_pending_pid(RiskfightState* s) {
    if (!s->pending_pid_active) return;
    int pid = s->env.pid_holder;
    for (int source = 0; source < 2; source++) {
        Player* p = &s->env.players[source];
        p->num_pending_hits = s->pending_pid_count[pid][source];
        memcpy(p->pending_hits, s->pending_pid[pid][source],
            (size_t)p->num_pending_hits * sizeof(PendingHit));
        memset(s->env.pending_hit_metadata[source], 0, sizeof(s->env.pending_hit_metadata[source]));
        memcpy(s->env.pending_hit_metadata[source], s->pending_pid_metadata[pid][source],
            (size_t)p->num_pending_hits * sizeof(PendingHitMetadata));
    }
    s->pending_pid_active = 0;
}

/* Resolve launched uncertainties once, before any macro clones this common world. */
static void riskfight_sample_pending(RiskfightState* s, uint32_t* rng) {
    riskfight_resolve_pending_pid(s);
    for (int source = 0; source < 2; source++) {
        Player* p = &s->env.players[source];
        for (int i = 0; i < p->num_pending_hits; i++) {
            PendingHit* h = &p->pending_hits[i];
            PendingHitMetadata* m = &s->env.pending_hit_metadata[source][i];
            if (m->damage_mode != 2) continue;
            h->hit_success = (int)(xorshift32(rng) % 10000) < m->accuracy_bp;
            h->damage = h->hit_success
                ? m->damage_min + (int)(xorshift32(rng) % (uint32_t)(m->damage_max - m->damage_min + 1)) : 0;
            m->damage_mode = 1;
        }
    }
}
#endif
