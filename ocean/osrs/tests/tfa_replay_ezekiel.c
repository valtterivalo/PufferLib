/* Forced-damage replay of recorded Ezekiel fights: every recorded hit and heal on Ezekiel is applied on its recorded tick,
   each volley's slot outcome (weakened at its jump, heal, nothing) is taken from the recording, nobody else deals or takes
   damage and he casts no magic. The sim's volley sword counts, special-to-special gaps and finale ticks are compared with the
   recording: divergence is an HP threshold, rotation clock or finale trigger defect.
   usage: tfa_replay_ezekiel fixtures/tfa_ezekiel_events.tsv fixtures/tfa_fights.tsv */
#include "tfa_scripted.h"

#define MAX_FIGHTS 32
#define MAX_EVENTS 8192
#define MAX_VOLLEYS TFA_EZEKIEL_FORCED_VOLLEYS

typedef struct { int t; int amount; int heal; } Hit;
typedef struct { int t; int anim; } Anim;

typedef struct {
    char id[32];
    int party;
    Hit hits[MAX_EVENTS];
    int nhits;
    Anim anims[MAX_EVENTS];
    int nanims;
    int volley_t[MAX_VOLLEYS];
    int volley_swords[MAX_VOLLEYS];
    int nvolleys;
} Fight;

static Fight FIGHTS[MAX_FIGHTS];
static int NFIGHTS;

static Fight* fight_for(const char* id) {
    for (int i = 0; i < NFIGHTS; i++) if (strcmp(FIGHTS[i].id, id) == 0) return &FIGHTS[i];
    assert(NFIGHTS < MAX_FIGHTS);
    Fight* f = &FIGHTS[NFIGHTS++];
    snprintf(f->id, sizeof(f->id), "%s", id);
    return f;
}

static int is_attack_anim(int anim) {
    return anim == EZEKIEL_ANIM_MELEE || anim == EZEKIEL_ANIM_SPECIAL || anim == EZEKIEL_ANIM_HEAL ||
        anim == EZEKIEL_ANIM_WEAKEN || anim == EZEKIEL_ANIM_ROLL || anim == EZEKIEL_ANIM_FINALE || anim == EZEKIEL_ANIM_MAGIC;
}

static void load_events(const char* path) {
    FILE* fp = fopen(path, "r");
    assert(fp);
    char line[1024];
    assert(fgets(line, sizeof(line), fp));
    while (fgets(line, sizeof(line), fp)) {
        char* col[10] = {0};
        char* save = NULL;
        int n = 0;
        for (char* tok = strtok_r(line, "\t\n", &save); tok && n < 10; tok = strtok_r(NULL, "\t\n", &save)) col[n++] = tok;
        assert(n >= 5);
        Fight* f = fight_for(col[0]);
        int t = atoi(col[1]);
        if (strncmp(col[3], "16653#", 6) == 0 && strcmp(col[2], "NPC+") == 0) {
            int k = 0;
            while (k < f->nvolleys && f->volley_t[k] != t) k++;
            if (k == f->nvolleys) {
                assert(f->nvolleys < MAX_VOLLEYS);
                f->volley_t[f->nvolleys++] = t;
            }
            f->volley_swords[k]++;
            continue;
        }
        if (strncmp(col[3], "16652#", 6) != 0) continue;
        if (strcmp(col[2], "HIT") == 0) {
            int type = atoi(col[7]);
            if (type == 6 || type == 16 || type == 17 || type == 43) {
                assert(f->nhits < MAX_EVENTS);
                f->hits[f->nhits++] = (Hit){ t, atoi(col[4]), type == 6 };
            }
        } else if (strcmp(col[2], "ANIM") == 0) {
            int anim = atoi(col[4]);
            if (is_attack_anim(anim) || anim == EZEKIEL_ANIM_RECOVER || anim == EZEKIEL_ANIM_DEATH) {
                assert(f->nanims < MAX_EVENTS);
                f->anims[f->nanims++] = (Anim){ t, anim };
            }
        }
    }
    fclose(fp);
}

static void load_party(const char* path) {
    FILE* fp = fopen(path, "r");
    assert(fp);
    char line[4096];
    assert(fgets(line, sizeof(line), fp));
    while (fgets(line, sizeof(line), fp)) {
        char* col[16] = {0};
        char* save = NULL;
        int n = 0;
        for (char* tok = strtok_r(line, "\t\n", &save); tok && n < 16; tok = strtok_r(NULL, "\t\n", &save)) col[n++] = tok;
        for (int i = 0; i < NFIGHTS; i++)
            if (strcmp(FIGHTS[i].id, col[0]) == 0) FIGHTS[i].party = atoi(col[10]);
    }
    fclose(fp);
}

static int first_attack_t(const Fight* f) {
    for (int i = 0; i < f->nanims; i++)
        if (f->anims[i].anim == EZEKIEL_ANIM_MELEE || f->anims[i].anim == EZEKIEL_ANIM_MAGIC) return f->anims[i].t;
    return -1;
}

static int anim_after(const Fight* f, int anim, int t0, int window) {
    for (int i = 0; i < f->nanims; i++)
        if (f->anims[i].anim == anim && f->anims[i].t > t0 && f->anims[i].t <= t0 + window) return f->anims[i].t;
    return -1;
}

typedef struct { int swords_ok, swords_total, gap_ok, gap_total, finale_ok, finale_total, cast_ok; } Score;

static void replay(const Fight* f, Score* score) {
    TfaState* s = calloc(1, sizeof(TfaState));
    TfaContext* ctx = calloc(1, sizeof(TfaContext));
    tfa_init_context((EncounterContext*)ctx);
    ctx->config.team_size = f->party;
    ctx->config.start_room = TFA_ROOM_EZEKIEL;
    ctx->config.room_max_ticks = 100000;
    tfa_finalize_context((EncounterState*)s, (EncounterContext*)ctx);
    tfa_reset_typed(s, ctx, 1);
    TfaEzekielState* es = &s->boss.ezekiel;
    es->damage_source = TFA_EZEKIEL_DAMAGE_FORCED;
    int specials[MAX_VOLLEYS], nspecials = 0;
    for (int i = 0; i < f->nanims; i++) if (f->anims[i].anim == EZEKIEL_ANIM_SPECIAL) specials[nspecials++] = f->anims[i].t;
    for (int v = 0; v < nspecials && v < MAX_VOLLEYS; v++) {
        int next = v + 1 < nspecials ? specials[v + 1] : specials[v] + 30;
        int weaken = anim_after(f, EZEKIEL_ANIM_WEAKEN, specials[v], next - specials[v]);
        int heal = anim_after(f, EZEKIEL_ANIM_HEAL, specials[v], next - specials[v]);
        es->forced_slot[v] = weaken >= 0 ? TFA_EZEKIEL_SLOT_WEAKEN : heal >= 0 ? TFA_EZEKIEL_SLOT_HEAL : TFA_EZEKIEL_SLOT_NONE;
        es->forced_jump[v] = (int8_t)(weaken >= 0 ? weaken - specials[v] : 0);
    }
    int offset = TFA_EZEKIEL.first_attack - first_attack_t(f);
    int last_t = f->anims[f->nanims - 1].t + 2;
    int acts[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS] = {0};
    int sim_special[MAX_VOLLEYS], sim_swords[MAX_VOLLEYS], nsim = 0, sim_roll = -1;
    int h = 0;
    for (int t = -1 - offset; t + offset <= last_t + 1 && !s->episode_over; t++) {
        TfaNpc* x = tfa_npc_first(s, TFA_NPC_EZEKIEL);
        while (h < f->nhits && f->hits[h].t <= t) {
            if (x && x->attackable) {
                if (f->hits[h].heal) {
                    x->hp += f->hits[h].amount;
                    if (x->hp > x->max_hp) x->hp = x->max_hp;
                } else {
                    EncounterPendingHit hit = { .active = 1, .ticks_remaining = 1, .attack_style = ATTACK_STYLE_RANGED,
                        .source_npc_type = -1, .source_npc_slot = -1, .damage = (int16_t)f->hits[h].amount };
                    encounter_pending_hit_queue_push(&x->hits, hit, "replay", s->tick, 0, 0);
                }
            }
            h++;
        }
        for (int p = 0; p < s->team_size; p++) s->players[p].player.current_hitpoints = 10000;
        tfa_step_team(s, ctx, acts);
        x = tfa_npc_first(s, TFA_NPC_EZEKIEL);
        if (x && x->anim_tick == s->tick) {
            if (x->anim_id == EZEKIEL_ANIM_SPECIAL && nsim < MAX_VOLLEYS) {
                int swords = 0;
                for (int i = 0; i < TFA_MAX_NPCS; i++)
                    swords += s->npcs[i].active && s->npcs[i].kind == TFA_NPC_EZEKIEL_SWORD;
                sim_special[nsim] = t;
                sim_swords[nsim++] = swords;
            }
            if (x->anim_id == EZEKIEL_ANIM_ROLL && sim_roll < 0) sim_roll = t;
        }
    }
    for (int v = 0; v < nspecials && v < nsim; v++) {
        score->swords_total++;
        score->swords_ok += sim_swords[v] == f->volley_swords[v];
        if (v == 0) continue;
        score->gap_total++;
        score->gap_ok += sim_special[v] - sim_special[v - 1] == specials[v] - specials[v - 1];
    }
    int roll = -1;
    for (int i = 0; i < f->nanims; i++) if (f->anims[i].anim == EZEKIEL_ANIM_ROLL) { roll = f->anims[i].t; break; }
    if (roll >= 0) {
        score->finale_total++;
        score->finale_ok += sim_roll == roll;
        if (getenv("TFA_REPLAY_VERBOSE")) printf("  %s roll rec %d sim %d\n", f->id, roll, sim_roll);
    }
    printf("%-12s party %d  specials rec %d sim %d\n", f->id, f->party, nspecials, nsim);
    free(s);
    free(ctx);
}

int main(int argc, char** argv) {
    assert(argc == 3);
    load_events(argv[1]);
    load_party(argv[2]);
    Score score = {0};
    for (int i = 0; i < NFIGHTS; i++) {
        const Fight* f = &FIGHTS[i];
        int has_special = 0;
        for (int k = 0; k < f->nanims; k++) has_special |= f->anims[k].anim == EZEKIEL_ANIM_SPECIAL;
        if (has_special && f->party >= 2) replay(f, &score);
    }
    printf("volley sword counts exact %d/%d\n", score.swords_ok, score.swords_total);
    printf("special-to-special gaps exact %d/%d\n", score.gap_ok, score.gap_total);
    printf("finale roll tick exact %d/%d\n", score.finale_ok, score.finale_total);
    return !(score.swords_ok * 10 >= score.swords_total * 8 && score.gap_ok * 10 >= score.gap_total * 8 && score.finale_ok * 10 >= score.finale_total * 8);
}
