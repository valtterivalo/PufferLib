/* Our own fights (live tapes, riskfight_tape.h) in the spectator.duels format, through riskfight_spectator.h's duel_from_tape (the rows fit_opponent reads), so the
 * readers of spectated duels (scripts/measure_risk_ledger.py --live) read live fights against real opponents the same way.
 *
 *   cc -std=c11 -O2 -I. -o export_tape_duels ocean/osrs/tests/export_tape_duels.c -lm
 *   export_tape_duels OUT tape...
 *
 * Side a is him, side b us (duel_from_tape's order). OUT.names: `id him us`. Our bar is our true hitpoints every tick (the tape knows them); his moves with his
 * hitsplats as a spectator sees it. The kit lines carry raw item ids. */
#define _POSIX_C_SOURCE 200809L
#include "riskfight_observe.h"
#include "riskfight_spectator.h"

static const char* spec_token(SpecKind k) {
    switch (k) {
        case SPEC_NONE: return "-";
        case SPEC_VW: return "VW";
        case SPEC_MAUL: return "MAUL";
        case SPEC_AGS: return "AGS";
        case SPEC_CLAWS: return "CLAWS";
        case SPEC_OTHER: return "SPEC";
    }
    fprintf(stderr, "export_tape_duels: unknown special kind %d\n", (int)k);
    exit(2);
}

static void print_side(FILE* out, const SpecSide* s) {
    char anims[128] = "";
    int n = 0;
    for (int i = 0; i < s->anim_count; i++) n += snprintf(anims + n, sizeof(anims) - n, "%s%d", i ? "|" : "", s->anims[i]);
    fprintf(out, " %d %d %d %d %s %d %d %d %d %s -1 %d %s", s->bar, s->wep, s->eat829, s->att, spec_token(s->spec), s->dmg, s->nhit, s->veng_cast, s->veng_proc,
        s->meal == MEAL_EAT ? "eat" : "-", s->dead, n ? anims : "-");
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: export_tape_duels OUT tape...\n"); return 2; }
    char path[1024];
    snprintf(path, sizeof(path), "%s.names", argv[1]);
    FILE* out = fopen(argv[1], "w");
    FILE* names = fopen(path, "w");
    if (!out || !names) { fprintf(stderr, "export_tape_duels: can't write %s\n", argv[1]); return 2; }
    static Tape tape;
    static SpecDuel duel;
    int written = 0;
    for (int a = 2; a < argc; a++) {
        if (!tape_load(&tape, argv[a])) { fprintf(stderr, "export_tape_duels: can't read %s\n", argv[a]); return 2; }
        duel_from_tape(&tape, &duel);
        if (duel.row_count == 0) { fprintf(stderr, "export_tape_duels: %s has no rows\n", argv[a]); return 2; }
        fprintf(out, "DUEL t%d.0 t%d %d %d 0 0 %d %d\n", written, written, duel.start, duel.end, duel.dharok[0], duel.dharok[1]);
        for (int e = 0; e < duel.eq_count; e++) {
            const SpecEq* q = &duel.eq[e];
            fprintf(out, "E %d %c", q->tick, q->side ? 'b' : 'a');
            for (int k = 0; k < KIT_SLOTS; k++) fprintf(out, " %d", q->kit[k] < 0 ? -1 : q->kit[k] - GS_ITEM_OFFSET);
            fprintf(out, "\n");
        }
        for (int i = 0; i < duel.row_count; i++) {
            const SpecRow* r = &duel.rows[i];
            fprintf(out, "R %d %d", r->tick, r->third);
            print_side(out, &r->s[0]);
            print_side(out, &r->s[1]);
            fprintf(out, "\n");
        }
        fprintf(names, "t%d.0 him us\n", written);
        written++;
        free(duel.rows);
        free(duel.eq);
        tape_free(&tape);
    }
    fclose(out);
    fclose(names);
    printf("tapes %d written\n", written);
    return 0;
}
