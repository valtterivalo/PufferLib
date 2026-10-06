#ifndef RISKFIGHT_TAPE_H
#define RISKFIGHT_TAPE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../encounters/encounter_riskfight.h"

enum { TAPE_WORN = 13, TAPE_BAG = 28 };
enum { SIDE_US = 'U', SIDE_HIM = 'O' };
typedef enum { ACTION_CLICK, ACTION_ATTACK, ACTION_SPEC, ACTION_VENGEANCE } TapeActionKind;

typedef struct {
    int tick, hp, attack, strength, defence, magic, prayer, ux, uy, ox, oy;
    int spec_us, spec_phase_us, veng_us, veng_cooldown_us, sacks, recoil_used;
    int bar_ratio, bar_scale, last_bar_hit, meals_since;
    int spec_him, spec_phase_him, veng_him, veng_cooldown_him;
    int swing_him_tick, swing_him_speed, swing_us_tick, swing_us_speed, recoil_evidence, bar_now_ratio, bar_now_scale;
    char pid, overhead[16];
    int piety, worn[TAPE_WORN], bag[TAPE_BAG];
} TapeTick;

typedef struct { int tick; char side; int anim; } TapeAnim;
typedef struct { int tick; char side; int amount, type; char klass; } TapeHit;
typedef struct { int tick; TapeActionKind kind; int slot, raw_id; } TapeAction;

typedef struct {
    char name[64], session[32];
    int lo, hi, index;
    int kit_anims[64][2], kit_count;
    TapeTick* ticks;
    TapeAnim* anims; int anim_count;
    TapeHit* hits; int hit_count;
    TapeAction* actions; int action_count;
    void* hist_series;
    void* observer_series;
    uint8_t* his_flags;
} Tape;

static const TapeTick* tape_tick(const Tape* tape, int tick) {
    assert(tick >= tape->lo && tick <= tape->hi);
    const TapeTick* t = &tape->ticks[tick - tape->lo];
    assert(t->tick == tick);
    return t;
}

static int tape_load(Tape* tape, const char* path) {
    FILE* f = fopen(path, "r");
    assert(f);
    memset(tape, 0, sizeof(*tape));
    int lines = 0;
    char line[4096];
    while (fgets(line, sizeof(line), f)) lines++;
    rewind(f);
    tape->ticks = calloc(lines, sizeof(TapeTick));
    tape->anims = calloc(lines, sizeof(TapeAnim));
    tape->hits = calloc(lines, sizeof(TapeHit));
    tape->actions = calloc(lines, sizeof(TapeAction));
    int tick_count = 0;
    while (fgets(line, sizeof(line), f)) {
        char kind[8];
        int consumed;
        sscanf(line, "%7s %n", kind, &consumed);
        const char* rest = line + consumed;
        if (!strcmp(kind, "FIGHT")) {
            int r = sscanf(rest, "%d %31s %63s %d %d", &tape->index, tape->session, tape->name, &tape->lo, &tape->hi);
            assert(r == 5);
        } else if (!strcmp(kind, "KIT")) {
            char* p = (char*)rest;
            while (*p && *p != '\n') {
                int anim = (int)strtol(p, &p, 10);
                assert(*p == ':');
                int count = (int)strtol(p + 1, &p, 10);
                tape->kit_anims[tape->kit_count][0] = anim;
                tape->kit_anims[tape->kit_count++][1] = count;
                while (*p == ' ') p++;
            }
        } else if (!strcmp(kind, "A")) {
            TapeAnim* a = &tape->anims[tape->anim_count++];
            char side;
            int r = sscanf(rest, "%d %c %d", &a->tick, &side, &a->anim);
            assert(r == 3);
            a->side = side;
        } else if (!strcmp(kind, "H")) {
            TapeHit* h = &tape->hits[tape->hit_count++];
            char side, klass;
            int r = sscanf(rest, "%d %c %d %d %c", &h->tick, &side, &h->amount, &h->type, &klass);
            assert(r == 5);
            h->side = side;
            h->klass = klass;
        } else if (!strcmp(kind, "M")) {
            TapeAction* a = &tape->actions[tape->action_count++];
            char name[16];
            int r = sscanf(rest, "%d %15s %d %d", &a->tick, name, &a->slot, &a->raw_id);
            assert(r == 4);
            a->kind = !strcmp(name, "click") ? ACTION_CLICK : !strcmp(name, "attack") ? ACTION_ATTACK :
                !strcmp(name, "spec") ? ACTION_SPEC : ACTION_VENGEANCE;
        } else if (!strcmp(kind, "T")) {
            TapeTick* t = &tape->ticks[tick_count++];
            int* ints[] = {&t->tick, &t->hp, &t->attack, &t->strength, &t->defence, &t->magic, &t->prayer,
                &t->ux, &t->uy, &t->ox, &t->oy, &t->spec_us, &t->spec_phase_us, &t->veng_us, &t->veng_cooldown_us,
                &t->sacks, &t->recoil_used, &t->bar_ratio, &t->bar_scale, &t->last_bar_hit, &t->meals_since,
                &t->spec_him, &t->spec_phase_him, &t->veng_him, &t->veng_cooldown_him, &t->swing_him_tick,
                &t->swing_him_speed, &t->swing_us_tick, &t->swing_us_speed, &t->recoil_evidence,
                &t->bar_now_ratio, &t->bar_now_scale};
            char* p = (char*)rest;
            for (size_t i = 0; i < sizeof(ints) / sizeof(*ints); i++) *ints[i] = (int)strtol(p, &p, 10);
            while (*p == ' ') p++;
            t->pid = *p++;
            int n = 0;
            while (*p == ' ') p++;
            while (*p != ' ') t->overhead[n++] = *p++;
            t->piety = (int)strtol(p, &p, 10);
            for (int i = 0; i < TAPE_WORN; i++) t->worn[i] = (int)strtol(p, &p, 10);
            for (int i = 0; i < TAPE_BAG; i++) t->bag[i] = (int)strtol(p, &p, 10);
        }
    }
    fclose(f);
    assert(tick_count == tape->hi - tape->lo + 1);
    return tick_count;
}

static void tape_free(Tape* tape) {
    free(tape->hist_series); free(tape->his_flags);
    free(tape->ticks); free(tape->anims); free(tape->hits); free(tape->actions);
}

static uint8_t raw_id_to_item(int raw) {
    for (int i = 0; i < NUM_ITEMS; i++) if (ITEM_DATABASE[i].item_id == raw) return (uint8_t)i;
    fprintf(stderr, "no sim item for raw id %d\n", raw);
    abort();
}

static uint16_t raw_id_to_content(int raw) {
    return raw < 0 ? 0 : osrs_inventory_content_code_from_raw_osrs_id((uint16_t)raw);
}

static int tape_has_anim(const Tape* tape, int anim) {
    for (int i = 0; i < tape->kit_count; i++) if (tape->kit_anims[i][0] == anim) return 1;
    return 0;
}
#endif
