#ifndef RISKFIGHT_SPECTATOR_H
#define RISKFIGHT_SPECTATOR_H
#include "riskfight_opp.h"

/* Spectated duels (tools/spectator export): both fighters of every duel seen from outside. Each fighter's features are built from what
 * the other side showed of him and of the other side, the same inputs HimInputs reads in the sim. */
enum { SP_EQ = 2048, SP_ANIMS = 8, SP_MAX_SESSIONS = 8 };
typedef enum { MEAL_NONE, MEAL_EAT, MEAL_DRINK, MEAL_HIDDEN } MealKind;

typedef struct {
    int bar, wep, eat829, att, vw, maul, other_spec, dmg, nhit, veng_cast, veng_proc, dead, anims[SP_ANIMS], anim_count;
    MealKind meal;
    double meal_heal;
} SpecSide;
typedef struct { int tick, third; SpecSide s[2]; } SpecRow;
typedef struct { int tick, side; int kit[KIT_SLOTS]; } SpecEq;
typedef struct { char id[40]; int session, start, end, dharok[2]; SpecRow* rows; int row_count, capacity; SpecEq* eq; int eq_count, eq_capacity; } SpecDuel;
typedef struct { SpecDuel* duels; int count, capacity; char sessions[SP_MAX_SESSIONS][24]; int session_count; } SpecCorpus;

static int spec_session(SpecCorpus* c, const char* name) {
    for (int i = 0; i < c->session_count; i++) if (!strcmp(c->sessions[i], name)) return i;
    assert(c->session_count < SP_MAX_SESSIONS);
    strcpy(c->sessions[c->session_count], name);
    return c->session_count++;
}

static void spec_load(SpecCorpus* c, const char* path) {
    FILE* f = fopen(path, "r");
    assert(f);
    static char line[4096];
    SpecDuel* d = NULL;
    memset(c, 0, sizeof(*c));
    while (fgets(line, sizeof(line), f)) {
        char* t[64];
        int n = 0;
        for (char* tok = strtok(line, " \n"); tok && n < 64; tok = strtok(NULL, " \n")) t[n++] = tok;
        if (!n) continue;
        if (!strcmp(t[0], "DUEL")) {
            if (c->count == c->capacity) { c->capacity = c->capacity ? c->capacity * 2 : 64; c->duels = realloc(c->duels, (size_t)c->capacity * sizeof(SpecDuel)); }
            d = &c->duels[c->count++];
            memset(d, 0, sizeof(*d));
            strcpy(d->id, t[1]);
            d->session = spec_session(c, t[2]);
            d->start = atoi(t[3]); d->end = atoi(t[4]); d->dharok[0] = atoi(t[7]); d->dharok[1] = atoi(t[8]);
        } else if (!strcmp(t[0], "E")) {
            assert(d && n == 15);
            if (d->eq_count == d->eq_capacity) { d->eq_capacity = d->eq_capacity ? d->eq_capacity * 2 : 8; d->eq = realloc(d->eq, (size_t)d->eq_capacity * sizeof(SpecEq)); }
            SpecEq* e = &d->eq[d->eq_count++];
            e->tick = atoi(t[1]); e->side = t[2][0] == 'b';
            for (int k = 0; k < KIT_SLOTS; k++) { int id = atoi(t[3 + k]); e->kit[k] = id < 0 ? -1 : id + GS_ITEM_OFFSET; }
        } else if (!strcmp(t[0], "R")) {
            assert(d && n == 3 + 2 * 13);
            if (d->row_count == d->capacity) { d->capacity = d->capacity ? d->capacity * 2 : 32; d->rows = realloc(d->rows, (size_t)d->capacity * sizeof(SpecRow)); }
            SpecRow* r = &d->rows[d->row_count++];
            memset(r, 0, sizeof(*r));
            r->tick = atoi(t[1]); r->third = atoi(t[2]);
            for (int side = 0; side < 2; side++) {
                char** u = &t[3 + 13 * side];
                SpecSide* s = &r->s[side];
                s->bar = atoi(u[0]); s->wep = atoi(u[1]); s->eat829 = atoi(u[2]); s->att = atoi(u[3]);
                s->vw = !strcmp(u[4], "VW"); s->maul = !strcmp(u[4], "MAUL"); s->other_spec = strcmp(u[4], "-") && !s->vw && !s->maul;
                s->dmg = atoi(u[5]); s->nhit = atoi(u[6]); s->veng_cast = atoi(u[7]); s->veng_proc = atoi(u[8]);
                s->meal = !strcmp(u[9], "eat") ? MEAL_EAT : !strcmp(u[9], "drink") ? MEAL_DRINK : !strcmp(u[9], "hidden") ? MEAL_HIDDEN : MEAL_NONE;
                s->meal_heal = atof(u[10]); s->dead = atoi(u[11]);
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

static int spec_is_vw(int w) { return w == 27690; }
static int spec_is_maul(int w) { return w == 4153 || w == 24225 || w == 20557 || w == 20559; }

/** One tick of a fighter's life as the viewer reads it, with the inputs at the end of the tick. */
typedef struct {
    Hist h;
    HimInputs in;
    int labels;
} SpecTick;

enum { SPEC_EAT = 1, SPEC_SWING = 2, SPEC_VW = 4, SPEC_MAUL = 8, SPEC_VENG = 16 };

static int eat_delay_of_heal(double heal) { return heal < 30 ? 3 : heal < 50 ? 4 : 5; }

/** Series of a fighter: after each row, his history and inputs, and what he did on that row. `out` has row_count entries. */
static void spec_series(const SpecDuel* d, int side, SpecTick* out) {
    Hist h = hist_start();
    int other = 1 - side;
    GsSpecEnergy energy = gs_energy_full(d->rows[0].tick);
    int next_attack = INT_MIN / 2, hp_ratio = 30, our_bar = 30, wielded_vw = 0, wielded_maul = 0, last_cast = INT_MIN / 2, cast_active = 0, ready_age = 0;
    for (int i = 0; i < d->row_count; i++) {
        const SpecRow* r = &d->rows[i];
        const SpecSide* x = &r->s[side];
        const SpecSide* y = &r->s[other];
        int tick = r->tick;
        HistInput in = {.ate_visible = x->eat829, .ate = x->meal == MEAL_EAT || x->meal == MEAL_HIDDEN, .hits = x->nhit > 0, .damage = x->dmg,
            .bar_ratio = x->bar, .bar_scale = 30, .weapon = y->wep, .swing = x->att, .vw = x->vw, .maul = x->maul};
        for (int k = 0; k < x->anim_count; k++) in.animated |= x->anims[k] != 829;
        if (x->bar >= 0) hp_ratio = x->bar;
        if (y->bar >= 0) our_bar = y->bar;
        const int* kit = spec_kit(d, other, tick);
        in.reach = assumed_reach(kit, y->wep, &in.has_spec);
        int labels = (in.ate ? SPEC_EAT : 0) | (x->att ? SPEC_SWING : 0) | (x->vw ? SPEC_VW : 0) | (x->maul ? SPEC_MAUL : 0) | (x->veng_cast ? SPEC_VENG : 0);
        int speed = gs_base_speed(x->wep) > 0 ? gs_base_speed(x->wep) : 4;
        if (in.ate && next_attack - (tick + 1) > 0) next_attack += eat_delay_of_heal(x->meal_heal);
        if (x->att) next_attack = tick + speed;
        in.ready = next_attack <= tick + 1;
        if (spec_is_vw(x->wep) || x->vw) wielded_vw = 1;
        if (spec_is_maul(x->wep) || x->maul) wielded_maul = 1;
        if (x->vw || x->maul || x->other_spec) energy = gs_energy_spec(energy, tick, 50);
        if (x->veng_cast) { last_cast = tick; cast_active = 1; }
        if (x->veng_proc) cast_active = 0;
        hist_update(&h, tick, &in);
        ready_age = in.ready ? ready_age + 1 : 0;
        h.ready_age = ready_age;
        GsEnergyRange e = gs_energy_range(&energy, tick);
        out[i].h = h;
        out[i].labels = labels;
        out[i].in = (HimInputs){.hp_ratio = hp_ratio, .our_bar_ratio = our_bar, .vw_ok = wielded_vw && e.high >= ENERGY_FOR_SPEC,
            .maul_ok = wielded_maul && e.high >= ENERGY_FOR_SPEC, .veng_ok = !cast_active && tick - last_cast >= 50};
    }
}
#endif
