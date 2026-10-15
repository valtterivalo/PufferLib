/* Engine wire v2 contracts of tests/gs_lookahead_server.c: every macro's script survives emit -> parse, the response A records echoed back are a valid R,
 * search-style literal clicks keep their order, and unsupported opponent gear is answered with priors, never UNAVAILABLE.
 *
 *   cc -std=c11 -O2 -I. -o test_gs_engine_protocol ocean/osrs/tests/test_gs_engine_protocol.c -lm      (cwd = repo root) */
#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static Request g_parsed;

static void parse_text(const char* text, Request* q) {
    FILE* in = fmemopen((void*)text, strlen(text), "r");
    CHECK(in != NULL);
    read_request(in, q);
    fclose(in);
}

/* The warm-up request 0 up to its pending queue, then `tail` (P, R, END). */
static void compose(char* out, size_t capacity, const char* tail) {
    const char* base = WARM_REQUESTS[0];
    const char* cut = strstr(base, "P KNOWN");
    CHECK(cut != NULL && (size_t)(cut - base) + strlen(tail) + 1 < capacity);
    memcpy(out, base, (size_t)(cut - base));
    strcpy(out + (cut - base), tail);
}

static void same_command(const HumanCommand* a, const HumanCommand* b) {
    CHECK(a->kind == b->kind);
    switch (a->kind) {
    case HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK: CHECK(a->inventory_slot == b->inventory_slot && a->item_osrs_id == b->item_osrs_id); break;
    case HUMAN_COMMAND_EQUIP_INVENTORY_ITEM: CHECK(a->item_osrs_id == b->item_osrs_id); break;
    case HUMAN_COMMAND_UNEQUIP: CHECK(a->gear_slot == b->gear_slot); break;
    case HUMAN_COMMAND_SPEC_TOGGLE: CHECK(a->spec_mode == b->spec_mode); break;
    default: break;
    }
}

/* parse(emit(script)) == script: same entries, order, due, timing, operands, candidates. */
static void same_script(const LaScript* a, const LaScript* b) {
    CHECK(a->n == b->n);
    for (int i = 0; i < a->n; i++) {
        CHECK(a->due[i] == b->due[i] && a->op[i] == b->op[i]);
        CHECK((a->production ? a->fixed[i] : 1) == b->fixed[i]);
        if (a->op[i] == 0) same_command(&a->command[i], &b->command[i]);
        if ((a->op[i] >= 3 && a->op[i] <= 6) || a->op[i] == 11) CHECK(a->arg[i] == b->arg[i]);
        CHECK(a->candidate_count[i] == b->candidate_count[i]);
        for (int c = 0; c < a->candidate_count[i]; c++) CHECK(a->candidates[i][c] == b->candidates[i][c]);
    }
}

/* R KNOWN n + C lines from emitted records, parsed back as a request. */
static void parse_with_plan(const LaScript* plan, Request* q) {
    static char text[1 << 16], tail[1 << 15];
    FILE* f = fmemopen(tail, sizeof(tail), "w");
    CHECK(f != NULL);
    fprintf(f, "P KNOWN 2\nD 1 4 0 1 0 1 0 20 0 30 8000 0 0\nD 1 7 0 1 0 1 0 20 0 30 8000 0 0\nR KNOWN %d\n", plan->n);
    for (int i = 0; i < plan->n; i++) { fputc('C', f); la_script_emit_record(f, plan, i); fputc('\n', f); }
    fputs("END\n", f);
    fclose(f);
    compose(text, sizeof(text), tail);
    parse_text(text, q);
}

static void test_macro_round_trip(void) {
    static char text[1 << 12];
    compose(text, sizeof(text), "P KNOWN 2\nD 1 4 0 1 0 1 0 20 0 30 8000 0 0\nD 1 7 0 1 0 1 0 20 0 30 8000 0 0\nR KNOWN 0\nEND\n");
    static Request q;
    parse_text(text, &q);
    CHECK(server_unavailable(&q) == NULL);
    RiskfightSnapshot snap;
    snapshot_of(&q, &snap);
    riskfight_load_state(&g_base.state, &g_base.context, &snap);
    LaScript scripts[LA_MACROS];
    int feasible[LA_MACROS];
    la_scripts(&g_base.state, q.tick, &q.feasible, &q.rules, scripts, feasible);
    int checked = 0, kinds_seen[GS_WIRE_KIND_COUNT] = {0};
    for (int m = 0; m < LA_MACROS; m++) {
        if (!feasible[m]) continue;
        parse_with_plan(&scripts[m], &g_parsed);
        same_script(&scripts[m], &g_parsed.rules);
        for (int i = 0; i < scripts[m].n; i++) { int kind, arg; CHECK(la_script_record(&scripts[m], i, &kind, &arg)); kinds_seen[kind]++; }
        checked++;
    }
    printf("round-tripped %d feasible macros\n", checked);
    CHECK(checked >= 14);
    CHECK(kinds_seen[GS_WIRE_STOP_ATTACK] && kinds_seen[GS_WIRE_ALIGNED_READY] && kinds_seen[GS_WIRE_WAIT_SPEC_LAUNCH]);
    CHECK(kinds_seen[GS_WIRE_EQUIP_RECIPE] && kinds_seen[GS_WIRE_COND_ENERGY] && kinds_seen[GS_WIRE_CLICK_SLOT] && kinds_seen[GS_WIRE_CAST_VENGEANCE]);
    CHECK(feasible[LA_EAT_STAGGER]);

    /* The response's A records, echoed back verbatim as R, are a valid request with the same plan. */
    for (int m = 0; m < LA_MACROS; m++) {
        if (!feasible[m]) continue;
        memset(&g_decision, 0, sizeof(g_decision));
        memcpy(g_decision.scripts, scripts, sizeof(scripts));
        memcpy(g_decision.feasible, feasible, sizeof(feasible));
        g_decision.best = (LaMacro)m;
        static char line[1 << 15];
        FILE* out = fmemopen(line, sizeof(line), "w");
        server_respond(out, &q, 12.0);
        fclose(out);
        char* a = strstr(line, " A ");
        CHECK(a != NULL);
        char* prior = strstr(a, " PRIOR ");
        CHECK(prior != NULL); /* the fixture wears a shield and armor the sim has no row for */
        int count, used;
        CHECK(sscanf(a, " A %d%n", &count, &used) == 1 && count == scripts[m].n);
        static char tail[1 << 15];
        int at = snprintf(tail, sizeof(tail), "P KNOWN 2\nD 1 4 0 1 0 1 0 20 0 30 8000 0 0\nD 1 7 0 1 0 1 0 20 0 30 8000 0 0\nR KNOWN %d\n", count);
        char* p = a + used;
        for (int i = 0; i < count; i++) {
            int fields[4], consumed;
            CHECK(sscanf(p, " %d %d %d %d%n", &fields[0], &fields[1], &fields[2], &fields[3], &consumed) == 4);
            p += consumed;
            at += snprintf(tail + at, sizeof(tail) - (size_t)at, "C %d %d %d %d", fields[0], fields[1], fields[2], fields[3]);
            if (fields[1] >= GS_WIRE_EAT_RECIPE && fields[1] <= GS_WIRE_UNEQUIP_RECIPE) {
                int candidates;
                CHECK(sscanf(p, " %d%n", &candidates, &consumed) == 1);
                p += consumed;
                at += snprintf(tail + at, sizeof(tail) - (size_t)at, " %d", candidates);
                for (int c = 0; c < candidates; c++) {
                    int raw;
                    CHECK(sscanf(p, " %d%n", &raw, &consumed) == 1);
                    p += consumed;
                    at += snprintf(tail + at, sizeof(tail) - (size_t)at, " %d", raw);
                }
            }
            at += snprintf(tail + at, sizeof(tail) - (size_t)at, "\n");
        }
        CHECK(p == prior); /* the A records end exactly where PRIOR starts */
        snprintf(tail + at, sizeof(tail) - (size_t)at, "END\n");
        compose(text, sizeof(text), tail);
        parse_text(text, &g_parsed);
        same_script(&scripts[m], &g_parsed.rules);
    }
}

/* Every meal click of every macro targets a carried item when it is pressed: single-dose food emptied by an earlier press of the same plan is never clicked
 * again (the staggered plan eats the hard food twice, on two different slots, and needs two carried). */
static void test_meals_never_click_empty(void) {
    static char text[1 << 12];
    compose(text, sizeof(text), "P KNOWN 2\nD 1 4 0 1 0 1 0 20 0 30 8000 0 0\nD 1 7 0 1 0 1 0 20 0 30 8000 0 0\nR KNOWN 0\nEND\n");
    static Request q;
    parse_text(text, &q);
    CHECK(server_unavailable(&q) == NULL && q.feasible.eat_ok);
    RiskfightSnapshot snap;
    snapshot_of(&q, &snap);
    riskfight_load_state(&g_base.state, &g_base.context, &snap);
    Player bag = g_base.state.env.players[0];
    int hard_total = 0;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) hard_total += osrs_inventory_cell_metadata(&bag.inventory_cells[i])->consumable_kind == OSRS_CONSUMABLE_ANGLERFISH;
    CHECK(hard_total >= 2 && slot_of_kind(&bag, OSRS_CONSUMABLE_KARAMBWAN) >= 0 && slot_of_kind(&bag, OSRS_CONSUMABLE_BREW) >= 0);
    for (int carry = 1; carry <= hard_total; carry++) {
        g_base.state.env.players[0] = bag;
        Player* us = &g_base.state.env.players[0];
        for (int i = 0, kept = 0; i < OSRS_INVENTORY_SIZE; i++)
            if (osrs_inventory_cell_metadata(&us->inventory_cells[i])->consumable_kind == OSRS_CONSUMABLE_ANGLERFISH && kept++ >= carry)
                us->inventory_cells[i] = osrs_inventory_cell_empty();
        LaScript scripts[LA_MACROS];
        int feasible[LA_MACROS];
        la_scripts(&g_base.state, q.tick, &q.feasible, &q.rules, scripts, feasible);
        CHECK(feasible[LA_EAT_TRIPLE] && feasible[LA_EAT_NOW]);
        CHECK(feasible[LA_EAT_STAGGER] == (carry >= 2));
        int stagger_hard[2] = {-1, -1}, stagger_meals = 0;
        for (int m = 0; m < LA_MACROS; m++) {
            if (!feasible[m]) continue;
            Player walk = *us;
            for (int i = 0; i < scripts[m].n; i++) {
                HumanCommand click = scripts[m].command[i];
                if (scripts[m].op[i] != 0 || click.kind != HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK) continue;
                int slot = click.inventory_slot;
                if (m == LA_EAT_STAGGER && osrs_inventory_cell_metadata(&walk.inventory_cells[slot])->consumable_kind == OSRS_CONSUMABLE_ANGLERFISH)
                    stagger_hard[stagger_meals++ & 1] = slot;
                CHECK(slot >= 0 && slot < OSRS_INVENTORY_SIZE && !osrs_inventory_cell_is_empty(&walk.inventory_cells[slot]));
                OsrsConsumableKind kind = osrs_inventory_cell_metadata(&walk.inventory_cells[slot])->consumable_kind;
                if (kind == OSRS_CONSUMABLE_ANGLERFISH || kind == OSRS_CONSUMABLE_KARAMBWAN) walk.inventory_cells[slot] = osrs_inventory_cell_empty();
            }
        }
        CHECK(stagger_meals == (carry >= 2 ? 2 : 0) && (carry < 2 || stagger_hard[0] != stagger_hard[1]));
    }
}

/* A search plan is literal ticked clicks: any order of clicks with due >= tick + 1, timing 1, list order kept for equal due. */
static void test_literal_plan_round_trip(void) {
    static const char* plan =
        "P KNOWN 0\nR KNOWN 11\n"
        "C 805 0 7 1\nC 805 1 0 1\nC 805 13 0 1\nC 806 3 0 1\nC 806 5 3 1\nC 808 17 0 1\nC 808 12 3144 1\nC 809 11 27690 1\nC 809 2 0 1\nC 809 4 0 1\nC 812 0 12 1\nEND\n";
    static char text[1 << 12];
    compose(text, sizeof(text), plan);
    static Request q;
    parse_text(text, &q);
    CHECK(q.rules.n == 11);
    static const int kind[11] = {GS_WIRE_CLICK_SLOT, GS_WIRE_ATTACK, GS_WIRE_SPEC_TOGGLE, GS_WIRE_CAST_VENGEANCE, GS_WIRE_UNEQUIP_SLOT, GS_WIRE_STOP_ATTACK,
        GS_WIRE_CLICK_ITEM, GS_WIRE_EQUIP_ITEM, GS_WIRE_SPEC_ENABLE, GS_WIRE_SPEC_DISARM, GS_WIRE_CLICK_SLOT};
    for (int i = 0; i < q.rules.n; i++) {
        int k, arg;
        CHECK(q.rules.fixed[i] == 1 && la_script_record(&q.rules, i, &k, &arg) && k == kind[i]);
    }
    parse_with_plan(&q.rules, &g_parsed);
    same_script(&q.rules, &g_parsed.rules);
    for (int i = 1; i < g_parsed.rules.n; i++) CHECK(g_parsed.rules.due[i] >= g_parsed.rules.due[i - 1]);
}

/* Unknown/unsupported opponent weapon, shield, armor: priors, a PRIOR triple per substitution, raw ids still observable. */
static void test_gear_priors(void) {
    static char text[1 << 13];
    const char* base = WARM_REQUESTS[0];
    /* weapon 1434 (dragon mace, unmapped), shield 8849 (adamant defender), body 1127 (rune platebody), gloves 99999 (id with no stats at all) */
    const char* shield = strstr(base, "SHIELD");
    const char* o = strstr(base, " 4587 0 0 -1 OBSERVED");
    CHECK(shield && o && o < shield);
    int n = (int)(o - base), m = (int)(shield - base);
    memcpy(text, base, (size_t)n);
    memcpy(text + n, " 1434", 5);                      /* the O record's observed weapon: an id with no sim row */
    memcpy(text + n + 5, o + 5, (size_t)(m - n - 5));
    n = n + (m - n);
    strcpy(text + n, "SHIELD OBSERVED 8849\nARMOR OBSERVED 10828 6570 1725 1127 7462 99999 4131\nPS 0 KNOWN 0 0 0\nPS 1 CLEAR_PRIOR\nRA OBSERVED 1\nP KNOWN 0\nR KNOWN 0\nEND\n");
    static Request q;
    parse_text(text, &q);
    CHECK(server_unavailable(&q) == NULL);
    CHECK(q.his_weapon == 1434); /* the observation keeps the raw id */
    CHECK(la_gear_wearable(q.belief.gear));
    CHECK(q.belief.gear.weapon != 1434 && la_item_of(q.belief.gear.weapon).kind == LA_ITEM_MAPPED);
    CHECK(get_item_attack_style((uint8_t)la_item_of(q.belief.gear.weapon).item) == ATTACK_STYLE_MELEE);
    CHECK(q.belief.gear.shield == 8850);
    CHECK(q.belief.gear.armor[3] == 10551); /* rune platebody -> nearest body */
    CHECK(q.belief.gear.armor[5] == -1);    /* an id the table has no stats for: bare */
    int weapon = 0, shield_triple = 0, body = 0, legs = 0;
    for (int i = 0; i < q.prior_count; i++) {
        weapon |= q.prior[i][0] == 1 && q.prior[i][1] == 0 && q.prior[i][2] == 1434;
        shield_triple |= q.prior[i][0] == 1 && q.prior[i][1] == 1 && q.prior[i][2] == 8849;
        body |= q.prior[i][0] == 1 && q.prior[i][1] == 2 + 3 && q.prior[i][2] == 1127;
        legs |= q.prior[i][0] == 1 && q.prior[i][1] == 2 + 5 && q.prior[i][2] == 99999;
    }
    CHECK(weapon && shield_triple && body && legs);
    RiskfightSnapshot snap;
    snapshot_of(&q, &snap); /* the sim can build the state: nothing unsupported is left */
    LaDecision* d = &g_decision;
    memset(d, 0, sizeof(*d));
    d->best = LA_CONTINUE;
    d->feasible[LA_CONTINUE] = 1;
    static char line[1 << 14];
    FILE* out = fmemopen(line, sizeof(line), "w");
    server_respond(out, &q, 1.0);
    fclose(out);
    CHECK(strstr(line, " PRIOR ") != NULL && strstr(line, " 1 0 1434") != NULL && strstr(line, " 1 1 8849") != NULL);
    /* nothing observed about shield/armor: bare, one triple per unobserved slot, still no UNAVAILABLE */
    compose(text, sizeof(text), "SHIELD UNKNOWN\nARMOR UNKNOWN\nPS 0 KNOWN 0 0 0\nPS 1 CLEAR_PRIOR\nRA OBSERVED 1\nP KNOWN 0\nR KNOWN 0\nEND\n");
}

int main(void) {
    g_ablation.hp = HP_UNIFORM;
    observer_register_items();
    sim_init(&g_base);
    test_macro_round_trip();
    test_meals_never_click_empty();
    test_literal_plan_round_trip();
    test_gear_priors();
    puts("Engine wire v2 contracts passed");
    return 0;
}
