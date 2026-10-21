/* Plays scripted TFA rooms and writes sim events in the fixture TSV schema
   (fixtures/tfa_<boss>_events.tsv) so sim and recordings run through the same analysis.
   usage: tfa_dump_events <room> <team_size> <episodes> <seed> */
#include "tfa_scripted.h"

typedef struct {
    int active;
    int npc_id;
    uint32_t instance;
    int anim_tick;
} DumpNpc;

typedef struct {
    int kind;
    int gfx;
} DumpHazard;

static const char* const ROOM_NAMES[TFA_ROOM_COUNT] = {
    "guardian", "bork", "ezekiel", "dreadnoth", "moia", "xarth", "dagonhai", "ocellus",
};

static void prayers_string(const Player* pl, char* out) {
    const char* parts[2];
    int n = 0;
    if (pl->offensive_prayer == OFFENSIVE_PRAYER_RIGOUR) parts[n++] = "RIGOUR";
    else if (pl->offensive_prayer == OFFENSIVE_PRAYER_PIETY) parts[n++] = "PIETY";
    else if (pl->offensive_prayer == OFFENSIVE_PRAYER_AUGURY) parts[n++] = "AUGURY";
    if (pl->prayer == PRAYER_PROTECT_MELEE) parts[n++] = "PROT_MELEE";
    else if (pl->prayer == PRAYER_PROTECT_RANGED) parts[n++] = "PROT_RANGE";
    else if (pl->prayer == PRAYER_PROTECT_MAGIC) parts[n++] = "PROT_MAGIC";
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        if (i) strcat(out, ",");
        strcat(out, parts[i]);
    }
}

int main(int argc, char** argv) {
    assert(argc == 5);
    int room = atoi(argv[1]), team = atoi(argv[2]), episodes = atoi(argv[3]);
    uint32_t seed = (uint32_t)atoi(argv[4]);
    TfaState* s = calloc(1, sizeof(TfaState));
    TfaContext* ctx = calloc(1, sizeof(TfaContext));
    tfa_init_context((EncounterContext*)ctx);
    ctx->config.team_size = team;
    ctx->config.start_room = room;
    tfa_finalize_context((EncounterState*)s, (EncounterContext*)ctx);
    printf("fight_id\tt\tkind\tactor\tid\tx\ty\ta\tb\tc\n");
    for (int e = 0; e < episodes; e++) {
        char fid[64];
        snprintf(fid, sizeof(fid), "sim_%s_%03d", ROOM_NAMES[room], e);
        tfa_reset_typed(s, ctx, seed + (uint32_t)e);
        DumpNpc npcs[TFA_MAX_NPCS] = {0};
        DumpHazard hazards[TFA_MAX_HAZARDS] = {0};
        char pray[TFA_MAX_PLAYERS][64] = {{0}};
        int last_hp[TFA_MAX_PLAYERS] = {0};
        int acts[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS];
        int t = 0;
        for (;;) {
            for (int i = 0; i < TFA_MAX_NPCS; i++) {
                const TfaNpc* n = &s->npcs[i];
                DumpNpc* d = &npcs[i];
                if (d->active && (!n->active || n->instance_id != d->instance))
                    printf("%s\t%d\tNPC-\t%d#%d\t%d\t-\t-\t-\t-\t-\n", fid, t, d->npc_id, i, d->npc_id);
                if (n->active && (!d->active || n->instance_id != d->instance)) {
                    printf("%s\t%d\tNPC+\t%d#%d\t%d\t%d\t%d\t-1\t-1\t-\n", fid, t, n->npc_id, i, n->npc_id, n->x, n->y);
                    d->anim_tick = -1;
                } else if (n->active && n->npc_id != d->npc_id) {
                    printf("%s\t%d\tNPCID\t%d#%d\t%d\t%d\t%d\t%d\t-\t-\n", fid, t, n->npc_id, i, n->npc_id, n->x, n->y, d->npc_id);
                }
                if (n->active) {
                    printf("%s\t%d\tNPOS\t%d#%d\t%d\t%d\t%d\t-\t-\t-\n", fid, t, n->npc_id, i, n->npc_id, n->x, n->y);
                    if (n->anim_tick == s->tick && n->anim_id >= 0 && d->anim_tick != s->tick) {
                        printf("%s\t%d\tANIM\t%d#%d\t%d\t%d\t%d\t-\t-\t-\n", fid, t, n->npc_id, i, n->anim_id, n->x, n->y);
                        if (n->act != TFA_ACT_IDLE && n->target >= 0)
                            printf("%s\t%d\tTGT\t%d#%d\t%d\t-\t-\tP%d\t-\t-\n", fid, t, n->npc_id, i, n->anim_id, n->target);
                    }
                    for (int h = 0; h < n->hit_count_tick; h++)
                        printf("%s\t%d\tHIT\t%d#%d\t%d\t%d\t%d\t%d\t-\t-\n", fid, t, n->npc_id, i, n->hits_tick[h], n->x, n->y, n->hits_tick[h] > 0 ? 17 : 13);
                    if (n->max_hp > 0)
                        printf("%s\t%d\tNHP\t%d#%d\t%d\t-\t-\t100\t-\t-\n", fid, t, n->npc_id, i,
                            n->max_shield > 0 && n->shield > 0 ? 100 * n->shield / n->max_shield : 100 * n->hp / n->max_hp);
                }
                d->active = n->active;
                d->npc_id = n->npc_id;
                d->instance = n->instance_id;
                d->anim_tick = n->anim_tick;
            }
            for (int i = 0; i < TFA_MAX_HAZARDS; i++) {
                const TfaHazard* h = &s->hazards[i];
                DumpHazard* d = &hazards[i];
                int gfx = h->kind == TFA_HAZ_NONE ? -1 : h->gfx_id;
                if (d->gfx > 0 && d->gfx != gfx)
                    printf("%s\t%d\tOBJ-\t-\t%d\t%d\t%d\t1x1\t0\t-\n", fid, t, d->gfx, h->x, h->y);
                if (gfx > 0 && d->gfx != gfx)
                    printf("%s\t%d\tOBJ+\t-\t%d\t%d\t%d\t1x1\t0\t-\n", fid, t, gfx, h->x, h->y);
                d->gfx = gfx;
            }
            for (int p = 0; p < team; p++) {
                const TfaPlayer* tp = &s->players[p];
                if (tp->life != TFA_LIFE_ALIVE) continue;
                printf("%s\t%d\tPOS\tP%d\t0\t%d\t%d\t-\t-\t-\n", fid, t, p, tp->player.x, tp->player.y);
                for (int h = 0; h < tp->player.render_hit_count; h++)
                    printf("%s\t%d\tHIT\tP%d\t%d\t%d\t%d\t%d\t-\t-\n", fid, t, p, tp->player.render_hit_damage[h],
                        tp->player.x, tp->player.y, tp->player.render_hit_damage[h] > 0 ? 16 : 12);
                char now[64];
                prayers_string(&tp->player, now);
                if (strcmp(now, pray[p]) != 0 || t == 0)
                    printf("%s\t%d\tPRAY\tP%d\t0\t-\t-\t%s\t-\t-\n", fid, t, p, now);
                strcpy(pray[p], now);
                if (tp->player.current_hitpoints != last_hp[p])
                    printf("%s\t%d\tHP\tP%d\t%d\t-\t-\t%d\t-\t-\n", fid, t, p, tp->player.current_hitpoints, tp->player.base_hitpoints);
                last_hp[p] = tp->player.current_hitpoints;
            }
            if (s->episode_over) break;
            for (int p = 0; p < TFA_MAX_PLAYERS; p++) tfa_scripted_actions(s, p, acts + p * TFA_NUM_ACTION_HEADS);
            tfa_step_team(s, ctx, acts);
            t++;
        }
        fprintf(stderr, "%s outcome %d ticks %d\n", fid, s->outcome, s->tick);
    }
    free(s);
    free(ctx);
    return 0;
}
