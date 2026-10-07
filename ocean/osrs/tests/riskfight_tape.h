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

/* Components of a rollout that an ablation replaces by what the recording shows, read from GS_ABL: hp=uniform|heals|oracle, eats, escape, pid, damage,
 * threat=P with timer=N (he eats with probability P when our swing lands within N ticks and his hitpoints are within our reach). */
typedef enum { HP_UNIFORM, HP_HEALS, HP_HIDDEN, HP_ORACLE } HpMode;
typedef struct { HpMode hp; int eats, escape, pid, damage, threat_timer, level_low, level_high, escape_model, his_defence, no_piety; double threat, appetite, panic; int panic_hp; } Ablation;
static Ablation g_ablation;
static const Tape* g_tape;
static int g_trace;
static long g_hit_hist[160], g_sim_hp_ticks[13];
static long g_sim_escapes, g_sim_rollouts, g_our_events, g_our_positive, g_his_events, g_his_positive;
static double g_spec_counted[3], g_spec_predicted[3], g_spec_realized[3];
static double g_cls[3][2];
static double g_pit_all[10], g_pit_low[10], g_pit_played[10], g_pit_n, g_pit_low_n, g_pit_played_n;
static double g_eb_n[6], g_eb_exp[6], g_eb_true[6], g_eb_low[6], g_eb_high[6];
static double g_eat_risk = 0.01;
static int g_thrown_only, g_vw_only, g_no_eat, g_plan_land, g_plan_last;
static double g_plan_mean;
static double g_cb_split[2][2];
static double g_react_sim[4][2], g_react_rec[4][2];
static double g_pc_specs[4], g_pc_n[4], g_pc_plan[4], g_pc_act[4], g_pc_land[4], g_pc_actland[4], g_pc_any[4], g_pc_chance[4], g_pc_dead[4];
static double g_fm[4][12];
static double g_dm_n[3], g_dm_lethal[3], g_dm_any[3], g_dm_dead[3], g_dm_ate[3], g_dm_sum[3];
static double g_diag_n[4][4], g_diag_exp[4][4], g_diag_true[4][4];
static double g_exp_counted[4], g_exp_predicted[4], g_exp_realized[4], g_exp_mean[4], g_exp_true[4];
static uint32_t g_rollout_seed;
static int (*g_unseen_ticks)(const Tape*, int tick);

static const TapeTick* tape_tick(const Tape* tape, int tick) {
    assert(tick >= tape->lo && tick <= tape->hi);
    const TapeTick* t = &tape->ticks[tick - tape->lo];
    assert(t->tick == tick);
    return t;
}

static int tape_his_anim_at(const Tape* tape, int tick, int anim) {
    for (int i = 0; i < tape->anim_count; i++)
        if (tape->anims[i].tick == tick && tape->anims[i].side == SIDE_HIM && tape->anims[i].anim == anim) return 1;
    return 0;
}

/** The tick he starts leaving the fight, a teleport tab or spell animation of his, INT_MAX when the recording shows none. */
static int tape_escape_tick(const Tape* tape) {
    int first = INT_MAX;
    for (int i = 0; i < tape->anim_count; i++) {
        const TapeAnim* a = &tape->anims[i];
        if (a->side == SIDE_HIM && (a->anim == 4069 || a->anim == 4071 || a->anim == 714) && a->tick < first) first = a->tick;
    }
    return first;
}

static int tape_damage_on_him(const Tape* tape, int tick) {
    int total = 0;
    for (int i = 0; i < tape->hit_count; i++) {
        const TapeHit* h = &tape->hits[i];
        if (h->tick == tick && h->side == SIDE_HIM && h->klass != 'B') total += h->amount;
    }
    return total;
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
