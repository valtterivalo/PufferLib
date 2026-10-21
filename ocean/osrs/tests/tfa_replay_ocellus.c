/* Forced-damage replay of recorded Ocellus phase 1 fights: the recorded hits and heals on the boss are applied on their
   recorded ticks (sim tick = recorded tick + the first barrage delay), nobody else deals or takes damage, and the sim's
   middle teleport, expend and transformation ticks are compared with the recording. Divergence is an HP, threshold or timer defect.
   P1 HP is the recorded party's (tfa_fights.tsv party_size).
   usage: tfa_replay_ocellus fixtures/tfa_ocellus_events.tsv */
#include "tfa_scripted.h"

#define MAX_FIGHTS 16
#define MAX_EVENTS 4096
#define FULL_P1_NET 3000

typedef struct { int t; int amount; } Hit;
typedef struct { int t; int anim; } Anim;

typedef struct {
    char id[32];
    Hit hits[MAX_EVENTS];
    int nhits;
    Anim anims[MAX_EVENTS];
    int nanims;
    int party;
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
        if (strncmp(col[3], "16674#", 6) != 0) continue;
        Fight* f = fight_for(col[0]);
        int t = atoi(col[1]);
        if (strcmp(col[2], "HIT") == 0) {
            int type = atoi(col[7]), amount = atoi(col[4]);
            if (type == 6) amount = -amount;
            else if (type != 16 && type != 17 && type != 18 && type != 19 && type != 43 && type != 44 && type != 74) continue;
            assert(f->nhits < MAX_EVENTS);
            f->hits[f->nhits++] = (Hit){ t, amount };
        } else if (strcmp(col[2], "ANIM") == 0) {
            int anim = atoi(col[4]);
            if (anim == OC_ANIM_TP_OUT || anim == OC_ANIM_EXPEND || anim == OC_ANIM_P1_TRANSFORM) {
                assert(f->nanims < MAX_EVENTS);
                f->anims[f->nanims++] = (Anim){ t, anim };
            }
        }
    }
    fclose(fp);
}

static int kind_of(int anim) {
    return anim == OC_ANIM_TP_OUT ? 0 : anim == OC_ANIM_EXPEND ? 1 : 2;
}

static void replay(const Fight* f, int* matched, int* total, int off[3][5]) {
    TfaState* s = calloc(1, sizeof(TfaState));
    TfaContext* ctx = calloc(1, sizeof(TfaContext));
    tfa_init_context((EncounterContext*)ctx);
    ctx->config.team_size = 3;
    ctx->config.start_room = TFA_ROOM_OCELLUS;
    ctx->config.room_max_ticks = 100000;
    tfa_finalize_context((EncounterState*)s, (EncounterContext*)ctx);
    tfa_reset_typed(s, ctx, 1);
    s->boss.ocellus.no_line_heal = 1;
    s->npcs[0].hp = s->npcs[0].max_hp = tfa_party_hp(f->party, TFA_OC1.hp_base_p1, TFA_OC1.hp_trio_p1);
    int acts[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS] = {0};
    int last_t = f->anims[f->nanims - 1].t + TFA_OC1.first_barrage + 2;
    int sim_t[3][16], nsim[3] = {0};
    int h = 0;
    while (s->tick <= last_t && !s->episode_over) {
        TfaNpc* b = &s->npcs[0];
        while (h < f->nhits && f->hits[h].t + TFA_OC1.first_barrage + 1 <= s->tick + 1) {
            int amount = f->hits[h].amount;
            if (amount < 0) {
                oc_boss_heal(b, -amount);
            } else if (b->attackable) {
                EncounterPendingHit hit = { .active = 1, .ticks_remaining = 1, .attack_style = ATTACK_STYLE_RANGED,
                    .source_npc_type = -1, .source_npc_slot = -1, .damage = (int16_t)amount };
                encounter_pending_hit_queue_push(&b->hits, hit, "replay", s->tick, 0, 0);
            }
            h++;
        }
        for (int p = 0; p < s->team_size; p++) s->players[p].player.current_hitpoints = 10000;
        tfa_step_team(s, ctx, acts);
        b = &s->npcs[0];
        if (getenv("TFA_REPLAY_TRACE") && strcmp(getenv("TFA_REPLAY_TRACE"), f->id) == 0)
            printf("    t %d hp %d/%d middles %d\n", s->tick, b->hp, b->max_hp, s->boss.ocellus.middles);
        if (b->anim_tick == s->tick) {
            int k = b->anim_id == OC_ANIM_TP_OUT ? 0 : b->anim_id == OC_ANIM_EXPEND ? 1 : b->anim_id == OC_ANIM_P1_TRANSFORM ? 2 : -1;
            if (k >= 0 && nsim[k] < 16) sim_t[k][nsim[k]++] = s->tick;
        }
    }
    int rec_n[3] = {0};
    for (int i = 0; i < f->nanims; i++) {
        int k = kind_of(f->anims[i].anim);
        int j = rec_n[k]++;
        total[k]++;
        if (j < nsim[k]) {
            int d = sim_t[k][j] - (f->anims[i].t + TFA_OC1.first_barrage);
            if (d == 0) matched[k]++;
            if (d >= -2 && d <= 2) off[k][d + 2]++;
            if (d != 0 && getenv("TFA_REPLAY_VERBOSE"))
                printf("  %s anim %d #%d rec %d sim %d\n", f->id, f->anims[i].anim, j + 1, f->anims[i].t + TFA_OC1.first_barrage, sim_t[k][j]);
        }
    }
    printf("%-12s teleport rec %d sim %d  expend rec %d sim %d  transform rec %d sim %d\n",
        f->id, rec_n[0], nsim[0], rec_n[1], nsim[1], rec_n[2], nsim[2]);
    free(s);
    free(ctx);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    load_events(argv[1]);
    for (int i = 0; i < NFIGHTS; i++) FIGHTS[i].party = tfa_fixture_party(argv[1], FIGHTS[i].id);
    int matched[3] = {0}, total[3] = {0}, off[3][5] = {{0}};
    for (int i = 0; i < NFIGHTS; i++) {
        const Fight* f = &FIGHTS[i];
        int net = 0;
        for (int k = 0; k < f->nhits; k++) net += f->hits[k].amount;
        if (net >= FULL_P1_NET && f->nanims > 0) replay(f, matched, total, off);
    }
    const char* names[3] = { "teleport 14812", "expend 14810", "transform 14814" };
    int bad = 0;
    for (int k = 0; k < 3; k++) {
        printf("%-16s exact %d/%d  off-by -2..+2: %d %d %d %d %d\n", names[k], matched[k], total[k],
            off[k][0], off[k][1], off[k][2], off[k][3], off[k][4]);
        /* The kill tick at 5-6 raiders is only as good as the party hp: ocellus_08 needs hp 6764-6780 and ocellus_11 6778-6780 for an exact death
           teleport, the 0.419 formula gives 6772, so each lands one tick out in opposite directions. Anything beyond a tick is a broken rule. */
        int far = total[k] - off[k][1] - off[k][2] - off[k][3];
        bad |= total[k] == 0 || far > 0 || matched[k] * 4 < total[k] * 3;
    }
    return bad;
}
