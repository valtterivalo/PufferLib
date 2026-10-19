#ifndef RISKFIGHT_DUEL_WRITER_H
#define RISKFIGHT_DUEL_WRITER_H
/* Sim fights in the spectator.duels format (tools/spectator_export.py in the light repo), both seats, so the readers of real duels run on the sim unchanged
 * (scripts/measure_risk_ledger.py). Used by probe_risk_duels.c (a net against the env's opponents) and probe_script_duels.c (the scripts on both seats).
 *
 * Per side and tick, what a spectator sees: bar the bar the other side sees (it moves with hitsplats), weapon the raw id in hand, eat829 a meal or drink whose
 * animation shows (riskfight_meal_visible, the env's own visibility), spec VW / MAUL / AGS / SPEC on a special, dmg and nhit the tick's damage and whether a
 * hitsplat landed (the orb's included), veng_cast and veng_proc Vengeance turning on and being spent, dead, and the animations: a swing (2067 with the greataxe,
 * the special's animation on a special, else 390), 829 on a shown meal, 4071 on a teleport. att and meal_heal: 0 and -1, as in the export. Kit (E) lines carry the
 * worn composition (head cape neck weapon body shield arms legs hair hands feet jaw, raw ids, -1 empty) on the first row and on every change. */

enum { RD_MAX_ROWS = 4096 };
typedef struct { int tick; char side[2][160]; } RdRow;
typedef struct { int tick, side, comp[12]; } RdEq;

static RdRow rd_rows[RD_MAX_ROWS];
static RdEq rd_eqs[RD_MAX_ROWS * 2];
static int rd_row_count, rd_eq_count, rd_last_comp[2][12];

static void rd_begin(void) { rd_row_count = rd_eq_count = 0; }

static void rd_comp_of(const Player* p, int comp[12]) {
    static const int GEAR_OF_COMP[12] = {GEAR_SLOT_HEAD, GEAR_SLOT_CAPE, GEAR_SLOT_NECK, GEAR_SLOT_WEAPON, GEAR_SLOT_BODY, GEAR_SLOT_SHIELD, -1, GEAR_SLOT_LEGS, -1,
        GEAR_SLOT_HANDS, GEAR_SLOT_FEET, -1};
    for (int k = 0; k < 12; k++) comp[k] = GEAR_OF_COMP[k] < 0 || p->equipped[GEAR_OF_COMP[k]] == ITEM_NONE ? -1 : ITEM_DATABASE[p->equipped[GEAR_OF_COMP[k]]].item_id;
}

static int rd_weapon_raw(const Player* p) { return p->equipped[GEAR_SLOT_WEAPON] == ITEM_NONE ? -1 : ITEM_DATABASE[p->equipped[GEAR_SLOT_WEAPON]].item_id; }
static const char* rd_spec_token(uint8_t weapon) { return weapon == ITEM_VOIDWAKER ? "VW" : pvp_is_maul(weapon) ? "MAUL" : weapon == ITEM_AGS ? "AGS" : "SPEC"; }
static int rd_spec_anim(uint8_t weapon) { return weapon == ITEM_VOIDWAKER ? 11275 : pvp_is_maul(weapon) ? 1667 : weapon == ITEM_AGS ? 7644 : 1062; }

/** The row of the tick just stepped: `s` after the step, `pre` before it. */
static void rd_write_row(const RiskfightState* s, const RiskfightState* pre) {
    if (rd_row_count == RD_MAX_ROWS) { fprintf(stderr, "riskfight_duel_writer: a fight ran past %d ticks\n", RD_MAX_ROWS); exit(2); }
    const int tick = s->env.tick - 1;
    for (int i = 0; i < 2; i++) {
        int comp[12];
        rd_comp_of(&s->env.players[i], comp);
        if (rd_row_count > 0 && !memcmp(comp, rd_last_comp[i], sizeof(comp))) continue;
        rd_eqs[rd_eq_count] = (RdEq){.tick = tick, .side = i};
        memcpy(rd_eqs[rd_eq_count++].comp, comp, sizeof(comp));
        memcpy(rd_last_comp[i], comp, sizeof(comp));
    }
    RdRow* r = &rd_rows[rd_row_count++];
    r->tick = tick;
    for (int i = 0; i < 2; i++) {
        const Player* p = &s->env.players[i];
        const Player* q = &pre->env.players[i];
        const int spec = s->specs_used[i] > pre->specs_used[i];
        const int swing = spec || s->visible[1 - i].last_attack_tick == tick;
        const int shown_meal = riskfight_meal_visible(s, i);
        const int left = s->escaped[i] && !pre->escaped[i];
        const uint8_t hand = p->equipped[GEAR_SLOT_WEAPON];
        char anims[64] = "";
        int n = 0;
        if (swing) n += snprintf(anims + n, sizeof(anims) - n, "%s%d", n ? "|" : "", spec ? rd_spec_anim(hand) : hand == ITEM_DHAROKS_GREATAXE ? 2067 : 390);
        if (shown_meal) n += snprintf(anims + n, sizeof(anims) - n, "%s829", n ? "|" : "");
        if (left) n += snprintf(anims + n, sizeof(anims) - n, "%s4071", n ? "|" : "");
        snprintf(r->side[i], sizeof(r->side[i]), "%d %d %d 0 %s %d %d %d %d %s -1 %d %s", s->visible[1 - i].health_bar, rd_weapon_raw(p), shown_meal,
            spec ? rd_spec_token(hand) : "-", p->hit_damage > 0 ? p->hit_damage : 0, p->hit_landed_this_tick || p->hit_damage > 0,
            p->veng_active && !q->veng_active, !p->veng_active && q->veng_active, shown_meal ? "eat" : "-", p->current_hitpoints <= 0, n ? anims : "-");
    }
}

/** Writes the fight's rows as duel `number`, and its names line `id a b`. */
static void rd_flush(FILE* out, FILE* names, int number, const char* a, const char* b) {
    fprintf(out, "DUEL s%d.0 s%d %d %d 0 0 1 1\n", number, number, rd_rows[0].tick, rd_rows[rd_row_count - 1].tick);
    for (int e = 0; e < rd_eq_count; e++) {
        fprintf(out, "E %d %c", rd_eqs[e].tick, rd_eqs[e].side ? 'b' : 'a');
        for (int k = 0; k < 12; k++) fprintf(out, " %d", rd_eqs[e].comp[k]);
        fprintf(out, "\n");
    }
    for (int i = 0; i < rd_row_count; i++) fprintf(out, "R %d 0 %s %s\n", rd_rows[i].tick, rd_rows[i].side[0], rd_rows[i].side[1]);
    fprintf(names, "s%d.0 %s %s\n", number, a, b);
}
#endif
