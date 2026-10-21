/* Forced-damage replay of recorded Guardian fights: the recorded hits on the Guardian (pylon
   conversions included, so the sim's pylons are removed) are applied on their recorded ticks, nobody else deals or takes damage, and the sim's knockdown, rise and
   breath ticks are compared with the recording. Divergence is a shield, HP, threshold or timer defect.
   usage: tfa_replay_guardian fixtures/tfa_guardian_events.tsv fixtures/tfa_fights.tsv */
#include "tfa_scripted.h"

#define MAX_FIGHTS 64
#define MAX_EVENTS 4096

typedef struct { int t; int amount; } Hit;
typedef struct { int t; int anim; } Anim;

typedef struct {
    char id[32];
    int party;
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
        int is_boss = strncmp(col[3], "16659#", 6) == 0 || strncmp(col[3], "16660#", 6) == 0;
        if (!is_boss) continue;
        Fight* f = fight_for(col[0]);
        int t = atoi(col[1]);
        if (strcmp(col[2], "HIT") == 0) {
            assert(f->nhits < MAX_EVENTS);
            f->hits[f->nhits++] = (Hit){ t, atoi(col[4]) };
        } else if (strcmp(col[2], "ANIM") == 0) {
            int anim = atoi(col[4]);
            if (anim == GUARDIAN_ANIM_LAND || anim == GUARDIAN_ANIM_RISE || anim == GUARDIAN_ANIM_BREATH) {
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

/* Fights joined mid-fight start with a partial shield; only fights whose first hit lands on a full bar replay. */
static int replay(const Fight* f, int* matched, int* total, int* off_by[3]) {
    TfaState* s = calloc(1, sizeof(TfaState));
    TfaContext* ctx = calloc(1, sizeof(TfaContext));
    tfa_init_context((EncounterContext*)ctx);
    ctx->config.team_size = f->party;
    ctx->config.room_max_ticks = 100000;
    tfa_finalize_context((EncounterState*)s, (EncounterContext*)ctx);
    tfa_reset_typed(s, ctx, 1);
    for (int i = 0; i < TFA_MAX_NPCS; i++)
        if (s->npcs[i].kind == TFA_NPC_GUARDIAN_PYLON) s->npcs[i].active = 0;
    int acts[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS] = {0};
    int last_t = f->anims[f->nanims - 1].t + 2;
    int sim_t[3][64], nsim[3] = {0};
    int h = 0;
    for (int t = 0; t <= last_t && !s->episode_over; t++) {
        TfaNpc* g = tfa_npc_first(s, TFA_NPC_GUARDIAN);
        while (h < f->nhits && f->hits[h].t <= t) {
            if (g) {
                EncounterPendingHit hit = { .active = 1, .ticks_remaining = 1, .attack_style = ATTACK_STYLE_RANGED,
                    .source_npc_type = -1, .source_npc_slot = -1, .damage = (int16_t)f->hits[h].amount };
                encounter_pending_hit_queue_push(&g->hits, hit, "replay", s->tick, 0, 0);
            }
            h++;
        }
        for (int p = 0; p < s->team_size; p++) s->players[p].player.current_hitpoints = 10000;
        tfa_step_team(s, ctx, acts);
        g = tfa_npc_first(s, TFA_NPC_GUARDIAN);
        if (g && getenv("TFA_REPLAY_TRACE") && strcmp(getenv("TFA_REPLAY_TRACE"), f->id) == 0)
            printf("    t %d shield %d hp %d phase %d\n", t, g->shield, g->hp, s->boss.guardian.phase);
        if (g && g->anim_tick == s->tick) {
            int k = g->anim_id == GUARDIAN_ANIM_LAND ? 0 : g->anim_id == GUARDIAN_ANIM_RISE ? 1 : g->anim_id == GUARDIAN_ANIM_BREATH ? 2 : -1;
            if (k >= 0 && nsim[k] < 64) sim_t[k][nsim[k]++] = t;
        }
    }
    int rec_n[3] = {0};
    for (int i = 0; i < f->nanims; i++) {
        int k = f->anims[i].anim == GUARDIAN_ANIM_LAND ? 0 : f->anims[i].anim == GUARDIAN_ANIM_RISE ? 1 : 2;
        int j = rec_n[k]++;
        total[k]++;
        if (j < nsim[k]) {
            int d = sim_t[k][j] - f->anims[i].t;
            if (d == 0) matched[k]++;
            if (d >= -2 && d <= 2) off_by[k][d + 2]++;
            if (d != 0 && getenv("TFA_REPLAY_VERBOSE"))
                printf("  %s anim %d #%d rec %d sim %d\n", f->id, f->anims[i].anim, j + 1, f->anims[i].t, sim_t[k][j]);
        }
    }
    printf("%-12s party %d  kd rec %d sim %d  rise rec %d sim %d  breath rec %d sim %d\n",
        f->id, f->party, rec_n[0], nsim[0], rec_n[1], nsim[1], rec_n[2], nsim[2]);
    free(s);
    free(ctx);
    return 0;
}

int main(int argc, char** argv) {
    assert(argc == 3);
    load_events(argv[1]);
    load_party(argv[2]);
    int matched[3] = {0}, total[3] = {0}, off[3][5] = {{0}};
    int* off_by[3] = { off[0], off[1], off[2] };
    for (int i = 0; i < NFIGHTS; i++) {
        const Fight* f = &FIGHTS[i];
        int first_land = -1;
        for (int k = 0; k < f->nanims; k++) if (f->anims[k].anim == GUARDIAN_ANIM_LAND) { first_land = f->anims[k].t; break; }
        int shield = 0;
        for (int k = 0; k < f->nhits && f->hits[k].t < first_land; k++) shield += f->hits[k].amount;
        int full = 550 + 250 * ((f->party < 2 ? 2 : f->party) - 2);
        if (f->nanims == 0 || first_land < 0 || shield != full) continue;
        replay(f, matched, total, off_by);
    }
    const char* names[3] = { "knockdown 14747", "rise 14748", "breath 14754" };
    int bad = 0;
    for (int k = 0; k < 3; k++) {
        printf("%-16s exact %d/%d  off-by -2..+2: %d %d %d %d %d\n", names[k], matched[k], total[k],
            off[k][0], off[k][1], off[k][2], off[k][3], off[k][4]);
        bad |= total[k] == 0 || matched[k] * 10 < total[k] * 8;
    }
    return bad;
}
