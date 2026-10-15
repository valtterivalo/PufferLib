#ifndef RISKFIGHT_SPECTATOR_H
#define RISKFIGHT_SPECTATOR_H
#include "riskfight_opp.h"

/* Duels, own and spectated: both fighters of a duel seen from outside, one row per tick. Spectated duels are read from the text export of
 * tools/spectator_export.py (DUEL / E / R lines); own tapes are turned into the same rows, so that each fighter's decisions are labelled and
 * their inputs built by one function (fit_series), from what the other side showed of him and of itself, the inputs OppIn reads in the sim. */
enum { SP_EQ = 2048, SP_ANIMS = 8, SP_MAX_SESSIONS = 64 };
typedef enum { SPEC_NONE, SPEC_VW, SPEC_MAUL, SPEC_AGS, SPEC_CLAWS, SPEC_OTHER } SpecKind;
typedef enum { MEAL_NONE, MEAL_EAT } MealKind;

typedef struct {
    int bar, wep, eat829, att, dmg, nhit, veng_cast, veng_proc, dead, anims[SP_ANIMS], anim_count;
    SpecKind spec;
    MealKind meal;
} SpecSide;
typedef struct { int tick, third; SpecSide s[2]; } SpecRow;
typedef struct { int tick, side; int kit[KIT_SLOTS]; } SpecEq;
/** `session` is the recording a duel came from: the unit a fit is held out by. `source` 0 own tape, 1 spectator. */
typedef struct {
    char id[64];
    int session, source, start, end, dharok[2], veng0[2];
    SpecRow* rows; int row_count, capacity;
    SpecEq* eq; int eq_count, eq_capacity;
} SpecDuel;
typedef struct { SpecDuel* duels; int count, capacity; char sessions[SP_MAX_SESSIONS][48]; int session_count; } SpecCorpus;

static int spec_session(SpecCorpus* c, const char* name) {
    for (int i = 0; i < c->session_count; i++) if (!strcmp(c->sessions[i], name)) return i;
    assert(c->session_count < SP_MAX_SESSIONS);
    strncpy(c->sessions[c->session_count], name, sizeof(c->sessions[0]) - 1);
    return c->session_count++;
}

static SpecDuel* spec_add_duel(SpecCorpus* c) {
    if (c->count == c->capacity) { c->capacity = c->capacity ? c->capacity * 2 : 64; c->duels = realloc(c->duels, (size_t)c->capacity * sizeof(SpecDuel)); }
    SpecDuel* d = &c->duels[c->count++];
    memset(d, 0, sizeof(*d));
    return d;
}

static SpecRow* spec_add_row(SpecDuel* d) {
    if (d->row_count == d->capacity) { d->capacity = d->capacity ? d->capacity * 2 : 32; d->rows = realloc(d->rows, (size_t)d->capacity * sizeof(SpecRow)); }
    SpecRow* r = &d->rows[d->row_count++];
    memset(r, 0, sizeof(*r));
    return r;
}

static void spec_add_eq(SpecDuel* d, int tick, int side, const int kit[KIT_SLOTS]) {
    if (d->eq_count == d->eq_capacity) { d->eq_capacity = d->eq_capacity ? d->eq_capacity * 2 : 8; d->eq = realloc(d->eq, (size_t)d->eq_capacity * sizeof(SpecEq)); }
    SpecEq* e = &d->eq[d->eq_count++];
    e->tick = tick; e->side = side;
    for (int k = 0; k < KIT_SLOTS; k++) e->kit[k] = kit[k] < 0 ? -1 : kit[k] + GS_ITEM_OFFSET;
}

static SpecKind spec_kind_of(const char* token) {
    return !strcmp(token, "VW") ? SPEC_VW : !strcmp(token, "MAUL") ? SPEC_MAUL : !strcmp(token, "AGS") ? SPEC_AGS : !strcmp(token, "CLAWS") ? SPEC_CLAWS :
        !strcmp(token, "SPEC") ? SPEC_OTHER : SPEC_NONE;
}

/** Appends the duels of a spectator export; every duel's recording is a session. */
static void spec_load(SpecCorpus* c, const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open spectator corpus %s\n", path); abort(); }
    static char line[4096];
    SpecDuel* d = NULL;
    while (fgets(line, sizeof(line), f)) {
        char* t[64];
        int n = 0;
        for (char* tok = strtok(line, " \n"); tok && n < 64; tok = strtok(NULL, " \n")) t[n++] = tok;
        if (!n) continue;
        if (!strcmp(t[0], "DUEL")) {
            assert(n == 9);
            d = spec_add_duel(c);
            snprintf(d->id, sizeof(d->id), "%s", t[1]);
            d->session = spec_session(c, t[2]);
            d->source = 1;
            d->start = atoi(t[3]); d->end = atoi(t[4]); d->dharok[0] = atoi(t[7]); d->dharok[1] = atoi(t[8]);
        } else if (!strcmp(t[0], "E")) {
            assert(d && n == 15);
            int kit[KIT_SLOTS];
            for (int k = 0; k < KIT_SLOTS; k++) kit[k] = atoi(t[3 + k]);
            spec_add_eq(d, atoi(t[1]), t[2][0] == 'b', kit);
        } else if (!strcmp(t[0], "R")) {
            assert(d && n == 3 + 2 * 13);
            SpecRow* r = spec_add_row(d);
            r->tick = atoi(t[1]); r->third = atoi(t[2]);
            for (int side = 0; side < 2; side++) {
                char** u = &t[3 + 13 * side];
                SpecSide* s = &r->s[side];
                s->bar = atoi(u[0]); s->wep = atoi(u[1]); s->eat829 = atoi(u[2]); s->att = atoi(u[3]);
                s->spec = spec_kind_of(u[4]);
                s->dmg = atoi(u[5]); s->nhit = atoi(u[6]); s->veng_cast = atoi(u[7]); s->veng_proc = atoi(u[8]);
                s->meal = !strcmp(u[9], "eat") ? MEAL_EAT : MEAL_NONE;
                s->dead = atoi(u[11]);
                if (strcmp(u[12], "-")) for (char* a = strtok(u[12], "|"); a && s->anim_count < SP_ANIMS; a = strtok(NULL, "|")) s->anims[s->anim_count++] = atoi(a);
            }
        }
    }
    fclose(f);
}

static const int* spec_kit(const SpecDuel* d, int side, int tick) {
    const int* kit = NULL;
    for (int i = 0; i < d->eq_count; i++) if (d->eq[i].side == side && d->eq[i].tick <= tick) kit = d->eq[i].kit;
    if (!kit) for (int i = 0; i < d->eq_count; i++) if (d->eq[i].side == side) { kit = d->eq[i].kit; break; }
    static const int none[KIT_SLOTS] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    return kit ? kit : none;
}

/* ---------- own tapes as duels: side 0 is the opponent (his bar, his animations, the gear his composition shows), side 1 is us ---------- */

static SpecKind spec_of_animation(int a) {
    return a == ANIM_VW_SPEC ? SPEC_VW : a == ANIM_MAUL_SPEC ? SPEC_MAUL : a == 7644 || a == 7645 ? SPEC_AGS : a == 7514 ? SPEC_CLAWS :
        a == 1062 || a == 1060 || a == 3300 ? SPEC_OTHER : SPEC_NONE;
}

static int bar_of(int ratio, int scale) { return ratio < 0 || scale <= 0 ? -1 : scale == 30 ? ratio : (ratio * 30 + scale / 2) / scale; }

/** Dharok's greataxe, or its helm, platebody and platelegs, at any degradation. */
static int is_dharok_axe(int raw) { return raw == 4718 || (raw >= 4886 && raw <= 4890); }
static int is_dharok_set(const int* kit) {
    int helm = kit[0] == 4716 || (kit[0] >= 4880 && kit[0] <= 4884), body = kit[4] == 4720 || (kit[4] >= 4892 && kit[4] <= 4896);
    int legs = kit[7] == 4722 || (kit[7] >= 4898 && kit[7] <= 4902);
    return helm && body && legs;
}

static void duel_from_tape(const Tape* tape, SpecDuel* d) {
    memset(d, 0, sizeof(*d));
    snprintf(d->id, sizeof(d->id), "%s", tape->name);
    d->start = tape->lo; d->end = tape->hi;
    int his_weapon = -1, our_weapon = -1;
    int his_kit[KIT_SLOTS], our_kit[KIT_SLOTS], previous_his[KIT_SLOTS], previous_our[KIT_SLOTS];
    for (int k = 0; k < KIT_SLOTS; k++) his_kit[k] = our_kit[k] = previous_his[k] = previous_our[k] = -2;
    for (int x = tape->lo; x <= tape->hi; x++) {
        const TapeTick* t = tape_tick(tape, x);
        if (x == tape->lo) { d->veng0[1] = t->veng_us; d->veng0[0] = t->veng_him; }
        SpecRow* r = spec_add_row(d);
        r->tick = x;
        if (t->opponent_gear_source) {
            for (int k = 0; k < KIT_SLOTS; k++) his_kit[k] = k < t->opponent_gear_count ? t->opponent_gear[k] : -1;
            his_weapon = his_kit[3];
        }
        for (int k = 0; k < KIT_SLOTS; k++) our_kit[k] = k == 6 || k == 8 || k == 11 ? -1 : t->worn[k];
        our_weapon = our_kit[3];
        if (memcmp(his_kit, previous_his, sizeof(his_kit))) { spec_add_eq(d, x, 0, his_kit); memcpy(previous_his, his_kit, sizeof(his_kit)); }
        if (memcmp(our_kit, previous_our, sizeof(our_kit))) { spec_add_eq(d, x, 1, our_kit); memcpy(previous_our, our_kit, sizeof(our_kit)); }
        r->s[0].bar = bar_of(t->bar_now_ratio, t->bar_now_scale);
        r->s[1].bar = osrs_health_bar_ratio(t->hp, 99, 30);
        r->s[0].wep = his_weapon;
        r->s[1].wep = our_weapon;
        r->s[0].dead = r->s[0].bar == 0;
        r->s[1].dead = t->hp <= 0;
        d->dharok[0] = d->dharok[1] |= is_dharok_axe(his_weapon) || is_dharok_axe(our_weapon) || (t->opponent_gear_source && is_dharok_set(his_kit)) || is_dharok_set(our_kit);
    }
    for (int i = 0; i < tape->anim_count; i++) {
        const TapeAnim* a = &tape->anims[i];
        if (a->tick < tape->lo || a->tick > tape->hi || a->anim == -1) continue;
        SpecSide* s = &d->rows[a->tick - tape->lo].s[a->side == SIDE_HIM ? 0 : 1];
        if (s->anim_count < SP_ANIMS) s->anims[s->anim_count++] = a->anim;
        if (a->anim == ANIM_EAT) { s->eat829 = 1; s->meal = MEAL_EAT; }
        if (a->anim == ANIM_VENG_A || a->anim == ANIM_VENG_B) s->veng_cast = 1;
        SpecKind spec = spec_of_animation(a->anim);
        if (spec != SPEC_NONE) s->spec = spec;
    }
    for (int i = 0; i < tape->hit_count; i++) {
        const TapeHit* h = &tape->hits[i];
        if (h->tick < tape->lo || h->tick > tape->hi) continue;
        SpecRow* r = &d->rows[h->tick - tape->lo];
        SpecSide* s = &r->s[h->side == SIDE_HIM ? 0 : 1];
        s->nhit++;
        if (attack_hitsplat(h->type)) s->dmg += h->amount;
        /* A veng echo on a fighter is the echo of the other one's Vengeance: the other one shouts. */
        if (h->klass == 'V') r->s[h->side == SIDE_HIM ? 1 : 0].veng_proc = 1;
    }
}

/* ---------- the rows of one fighter: his decisions and what he read of the fight when he took them ---------- */

enum { FIT_SAMPLES = 4 };

typedef struct {
    int tick, session, source, style, third;
    Hist h;                       /* his history after the previous row */
    OppIn in;                     /* the inputs at the decision (his_hp is drawn per sample by the fit) */
    int hit_ratio, hit_scale, meals; /* his bar at his last hit (-1 none) and his meals since */
    int ate, swung, launch, wield, masked, swing_unknown, veng, bar, nhit, dmg;
} FitRow;

static SpecKind spec_of_weapon_kind(int k) { return k == WK_VW ? SPEC_VW : k == WK_AGS ? SPEC_AGS : k == WK_CLAWS ? SPEC_CLAWS : k == WK_MAUL ? SPEC_MAUL : SPEC_NONE; }

static int weapon_kind_of_raw(int raw) {
    if (raw < 0) return WK_NONE;
    int item = osrs_item_index_from_raw_id(raw);
    return item < 0 ? WK_NONE : weapon_kind_of_item(item);
}

static int spec_cost_of(SpecKind spec) { return spec == SPEC_OTHER ? 25 : 50; }

/** Series of a fighter: one FitRow per row after the first, from his history and what he and the other side showed up to the row before. `out` has row_count entries (entry 0 unused);
 * `final` is his history after the last row. */
static void fit_series(const SpecDuel* d, int side, FitRow* out, Hist* final) {
    Hist h = hist_start();
    int other = 1 - side;
    GsSpecEnergy energy = gs_energy_full(d->rows[0].tick);
    GsTimerState timer = {0}, our_timer = {0};
    int hp_ratio = 30, our_bar = 30, our_veng = d->veng0[other], hit_ratio = -1, hit_scale = 30, meals = 0;
    int cast_active = d->veng0[side], last_cast = INT_MIN / 2;
    for (int i = 0; i < d->row_count; i++) {
        const SpecRow* r = &d->rows[i];
        const SpecSide* x = &r->s[side];
        const SpecSide* y = &r->s[other];
        int tick = r->tick;
        int kit_unknown_swing = 0, attack_source = 0, swing = 0;
        FitRow* row = &out[i];
        if (i > 0) {
            memset(row, 0, sizeof(*row));
            GsEnergyRange e = gs_energy_range(&energy, tick);
            int warning = warning_since(&h.warning);
            row->tick = tick; row->session = d->session; row->source = d->source; row->style = d->dharok[side]; row->third = r->third;
            row->h = h;
            row->hit_ratio = hit_ratio; row->hit_scale = hit_scale; row->meals = meals;
            row->in = (OppIn){
                .his_bar_ratio = hp_ratio, .our_bar_ratio = our_bar, .our_hp = bar_hp(our_bar),
                .lag = lag_of(tick, warning), .warned = warning != WARNING_NONE,
                .since_eat = h.since_eat, .since_vw_spec = h.since_vw_spec,
                .ready = gs_timer_reading(timer, tick) == GS_READING_READY, .ready_age = h.ready_age, .on_time_streak = h.on_time_streak,
                .since_swing = tick - h.last_swing_tick,
                .our_tau = our_timer.seen ? opp_imax(0, our_timer.earliest - tick) : -1,
                .our_spec = h.warning.spec_wielded_since != WARNING_NONE, .our_veng = our_veng,
                .seen_mask = h.seen_mask, .wield_class = h.wield_class, .veng_ok = !cast_active && tick - last_cast >= 50,
            };
            for (int k = 0; k < WK_COUNT; k++) { row->in.energy_ok[k] = e.high >= WK_COST[k]; row->in.has[k] = 1; }
        }
        HistInput in = {.ate_visible = x->eat829, .ate = x->meal == MEAL_EAT, .hits = x->nhit > 0, .damage = x->dmg, .bar_ratio = x->bar, .bar_scale = 30,
            .weapon = y->wep, .launch_class = WK_NONE, .wield_class = WK_NONE};
        int unclassified = 0, other_animation = 0;
        for (int k = 0; k < x->anim_count; k++) {
            int animation = x->anims[k];
            if (animation == -1) continue;
            const GsAnimationRow* arow = gs_animation_row(animation);
            other_animation |= animation != ANIM_EAT;
            in.animated |= animation != ANIM_EAT;
            if (!arow) { unclassified = 1; continue; }
            GsKind kind = (GsKind)arow->kind;
            swing |= kind == GS_KIND_ATTACK;
            attack_source |= kind == GS_KIND_ATTACK || kind == GS_KIND_INSTANT_SPEC;
            GsSpeed speed = x->wep >= 0 || arow->spell_speed ? gs_speed_after(x->wep, animation, GS_PACE_UNKNOWN) : (GsSpeed){0};
            timer = gs_timer_observe(timer, kind, tick, speed);
        }
        kit_unknown_swing = x->att && !attack_source;
        unclassified |= kit_unknown_swing;
        for (int k = 0; k < y->anim_count; k++) {
            int animation = y->anims[k];
            if (animation == -1) continue;
            const GsAnimationRow* arow = gs_animation_row(animation);
            if (!arow) { our_timer = (GsTimerState){0}; continue; }
            GsSpeed speed = y->wep >= 0 || arow->spell_speed ? gs_speed_after(y->wep, animation, GS_PACE_UNKNOWN) : (GsSpeed){0};
            our_timer = gs_timer_observe(our_timer, (GsKind)arow->kind, tick, speed);
        }
        if (unclassified) timer = (GsTimerState){0};
        in.swing = swing;
        int wielded = weapon_kind_of_raw(x->wep);
        in.wield_class = wielded;
        SpecKind spec = x->spec;
        if (spec != SPEC_NONE && spec != SPEC_OTHER) in.launch_class = spec == SPEC_VW ? WK_VW : spec == SPEC_AGS ? WK_AGS : spec == SPEC_CLAWS ? WK_CLAWS : WK_MAUL;
        if (x->bar >= 0) hp_ratio = x->bar;
        if (y->bar >= 0) our_bar = y->bar;
        if (x->nhit > 0 && x->bar >= 0) { hit_ratio = x->bar; hit_scale = 30; meals = 0; }
        else if (x->eat829) meals++;
        const int* kit = spec_kit(d, other, tick);
        in.reach = assumed_reach(kit, y->wep, &in.has_spec);
        if (spec != SPEC_NONE) energy = gs_energy_spec(energy, tick, spec_cost_of(spec));
        if (y->veng_cast) our_veng = 1;
        if (y->veng_proc) our_veng = 0;
        if (x->veng_cast) { last_cast = tick; cast_active = 1; }
        if (x->veng_proc) cast_active = 0;
        if (i > 0) {
            row->ate = x->eat829; row->swung = swing; row->launch = in.launch_class; row->wield = wielded;
            row->masked = other_animation || x->nhit > 0;
            row->veng = x->veng_cast;
            row->bar = x->bar; row->nhit = x->nhit; row->dmg = x->dmg;
            row->swing_unknown = unclassified;
        }
        in.ready = gs_timer_reading(timer, tick + 1) == GS_READING_READY;
        hist_update(&h, tick, &in);
    }
    *final = h;
}

/** Hist after each tape tick lo..hi, from his animations, the hitsplats on him and the bar as shown. */
static void hist_series(const Tape* tape, Hist* out) {
    static SpecDuel duel;
    duel_from_tape(tape, &duel);
    FitRow* rows = malloc((size_t)duel.row_count * sizeof(FitRow));
    Hist final;
    fit_series(&duel, 0, rows, &final);
    for (int i = 0; i < duel.row_count; i++) out[i] = i + 1 < duel.row_count ? rows[i + 1].h : final;
    free(rows);
    free(duel.rows); free(duel.eq);
}
#endif
