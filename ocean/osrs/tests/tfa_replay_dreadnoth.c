/* Forced-damage replay of recorded Dreadnoth fights: the recorded hits and heals on the boss are applied on their
   recorded ticks, nobody else deals or takes damage, and the sim's slam, jump and death ticks are compared with the recording.
   Divergence is an HP, threshold or timer defect.
   usage: tfa_replay_dreadnoth fixtures/tfa_dreadnoth_events.tsv */
#include "tfa_scripted.h"

#define MAX_FIGHTS 32
#define MAX_EVENTS 8192

typedef struct { int t; int amount; } Hit;
typedef struct { int t; int anim; } Anim;

typedef struct {
    char id[32];
    Hit hits[MAX_EVENTS];
    int nhits;
    Anim anims[MAX_EVENTS];
    int nanims;
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
        if (strncmp(col[3], "16665#", 6) != 0) continue;
        Fight* f = fight_for(col[0]);
        int t = atoi(col[1]);
        if (strcmp(col[2], "HIT") == 0) {
            int type = atoi(col[7]);
            int amount = atoi(col[4]);
            if (type == 6) amount = -amount;
            else if (type != 16 && type != 17 && type != 43 && type != 74) continue;
            assert(f->nhits < MAX_EVENTS);
            f->hits[f->nhits++] = (Hit){ t, amount };
        } else if (strcmp(col[2], "ANIM") == 0) {
            int anim = atoi(col[4]);
            if (anim == DREAD_ANIM_SLAM || anim == DREAD_ANIM_JUMP || anim == DREAD_ANIM_DEATH) {
                assert(f->nanims < MAX_EVENTS);
                f->anims[f->nanims++] = (Anim){ t, anim };
            }
        }
    }
    fclose(fp);
}

static int kind_of(int anim) {
    return anim == DREAD_ANIM_SLAM ? 0 : anim == DREAD_ANIM_JUMP ? 1 : 2;
}

static void replay(const Fight* f, int* matched, int* total, int off[3][5]) {
    TfaState* s = calloc(1, sizeof(TfaState));
    TfaContext* ctx = calloc(1, sizeof(TfaContext));
    tfa_init_context((EncounterContext*)ctx);
    ctx->config.team_size = 3;
    ctx->config.start_room = TFA_ROOM_DREADNOTH;
    ctx->config.room_max_ticks = 100000;
    tfa_finalize_context((EncounterState*)s, (EncounterContext*)ctx);
    tfa_reset_typed(s, ctx, 1);
    s->boss.dreadnoth.no_heal = 1;
    int acts[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS] = {0};
    int t0 = f->hits[0].t;
    int last_t = f->anims[f->nanims - 1].t + 2;
    int sim_t[3][128], nsim[3] = {0};
    int h = 0;
    for (int t = t0; t <= last_t && !s->episode_over; t++) {
        TfaNpc* b = tfa_npc_first(s, TFA_NPC_DREADNOTH);
        while (h < f->nhits && f->hits[h].t <= t) {
            if (b) {
                EncounterPendingHit hit = { .active = 1, .ticks_remaining = 1, .attack_style = ATTACK_STYLE_RANGED,
                    .source_npc_type = -1, .source_npc_slot = -1, .damage = (int16_t)f->hits[h].amount };
                encounter_pending_hit_queue_push(&b->hits, hit, "replay", s->tick, 0, 0);
            }
            h++;
        }
        for (int p = 0; p < s->team_size; p++) s->players[p].player.current_hitpoints = 10000;
        tfa_step_team(s, ctx, acts);
        b = tfa_npc_first(s, TFA_NPC_DREADNOTH);
        if (b && getenv("TFA_REPLAY_TRACE") && strcmp(getenv("TFA_REPLAY_TRACE"), f->id) == 0)
            printf("    t %d hp %d/%d phase %d\n", t, b->hp, b->max_hp, s->boss.dreadnoth.phase);
        if (b && b->anim_tick == s->tick) {
            int k = b->anim_id == DREAD_ANIM_SLAM ? 0 : b->anim_id == DREAD_ANIM_JUMP ? 1 : b->anim_id == DREAD_ANIM_DEATH ? 2 : -1;
            if (k >= 0 && nsim[k] < 128) sim_t[k][nsim[k]++] = t;
        }
    }
    int rec_n[3] = {0};
    for (int i = 0; i < f->nanims; i++) {
        int k = kind_of(f->anims[i].anim);
        int j = rec_n[k]++;
        total[k]++;
        if (j < nsim[k]) {
            int d = sim_t[k][j] - f->anims[i].t;
            if (d == 0) matched[k]++;
            if (d >= -2 && d <= 2) off[k][d + 2]++;
            if (d != 0 && getenv("TFA_REPLAY_VERBOSE"))
                printf("  %s anim %d #%d rec %d sim %d\n", f->id, f->anims[i].anim, j + 1, f->anims[i].t, sim_t[k][j]);
        }
    }
    printf("%-12s slam rec %d sim %d  jump rec %d sim %d  death rec %d sim %d\n",
        f->id, rec_n[0], nsim[0], rec_n[1], nsim[1], rec_n[2], nsim[2]);
    free(s);
    free(ctx);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    load_events(argv[1]);
    int matched[3] = {0}, total[3] = {0}, off[3][5] = {{0}};
    for (int i = 0; i < NFIGHTS; i++)
        if (FIGHTS[i].nhits > 0 && FIGHTS[i].nanims > 0) replay(&FIGHTS[i], matched, total, off);
    const char* names[3] = { "slam 14777", "jump 14780", "death 14782" };
    int bad = 0;
    for (int k = 0; k < 3; k++) {
        printf("%-12s exact %d/%d  off-by -2..+2: %d %d %d %d %d\n", names[k], matched[k], total[k],
            off[k][0], off[k][1], off[k][2], off[k][3], off[k][4]);
        bad |= total[k] == 0 || matched[k] * 10 < total[k] * 8;
    }
    return bad;
}
