/* Plays scripted TFA rooms and writes sim events in the fixture TSV schema
   (fixtures/tfa_<boss>_events.tsv) so sim and recordings run through the same analysis.
   usage: tfa_dump_events <room> <team_size> <episodes> <seed> */
#include "tfa_scripted.h"

typedef struct {
    int active;
    int npc_id;
    uint32_t instance;
    int anim_tick;
    int overhead;
} DumpNpc;

typedef struct {
    int kind;
    int gfx;
    int x, y, orientation;
} DumpHazard;

typedef struct {
    int t, gfx, npc_id, slot, x, y, dst, flight;
} DumpProj;

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
    assert(argc == 5 || (argc == 6 && strcmp(argv[5], "invulnerable") == 0));
    int invulnerable = argc == 6;
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
        DumpNpc npcs[TFA_MAX_NPCS];
        for (int i = 0; i < TFA_MAX_NPCS; i++) npcs[i] = (DumpNpc){ .overhead = -1 };
        DumpHazard hazards[TFA_MAX_HAZARDS] = {0};
        char pray[TFA_MAX_PLAYERS][64] = {{0}};
        int last_hp[TFA_MAX_PLAYERS] = {0};
        static DumpProj pending[256];
        int npending = 0;
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
                        if (s->room == TFA_ROOM_OCELLUS && n->anim_id == OC_ANIM_QUADRANT_ATTACK)
                            printf("%s\t%d\tSPOT\t%d#%d\t%d\t-\t-\t1\t-\t-\n", fid, t, n->npc_id, i,
                                n->act == TFA_ACT_MELEE ? 4328 : n->act == TFA_ACT_RANGED ? 4330 : 4329);
                        if (n->act != TFA_ACT_IDLE && n->target >= 0)
                            printf("%s\t%d\tTGT\t%d#%d\t%d\t-\t-\tP%d\t-\t-\n", fid, t, n->npc_id, i, n->anim_id, n->target);
                    }
                    /* A splashed spell shows no hitsplat: P0's ice barrages never left a block splat (0 of 716). */
                    for (int h = 0; h < n->hit_count_tick; h++)
                        if (n->hits_style[h] != ATTACK_STYLE_MAGIC || n->hits_tick[h] > 0) printf("%s\t%d\tHIT\t%d#%d\t%d\t%d\t%d\t%d\t-\t-\n", fid, t, n->npc_id, i, n->hits_tick[h], n->x, n->y,
                            n->hits_attacker[h] == 0 ? (n->hits_tick[h] > 0 ? 16 : 12) : (n->hits_tick[h] > 0 ? 17 : 13));
                    if (n->max_hp > 0)
                        printf("%s\t%d\tNHP\t%d#%d\t%d\t-\t-\t100\t-\t-\n", fid, t, n->npc_id, i,
                            n->max_shield > 0 && n->shield > 0 ? 100 * n->shield / n->max_shield : 100 * n->hp / n->max_hp);
                }
                /* Overhead sprite ids as recorded: 0-2 the protections, 11 soul split, 15/16 Ocellus' P4 Deflect Melee/Range. */
                int p4 = s->room == TFA_ROOM_OCELLUS && n->kind == TFA_NPC_OCELLUS && s->boss.ocellus.phase == TFA_OCELLUS_P4;
                int sprite = p4 && s->boss.ocellus.soul_split ? 11 : p4 && s->boss.ocellus.deflect == ATTACK_STYLE_MELEE ? 15
                    : p4 && s->boss.ocellus.deflect == ATTACK_STYLE_RANGED ? 16
                    : n->overhead == PRAYER_PROTECT_MELEE ? 0 : n->overhead == PRAYER_PROTECT_RANGED ? 1 : n->overhead == PRAYER_PROTECT_MAGIC ? 2 : -1;
                if (n->active && sprite != d->overhead) {
                    if (sprite < 0) printf("%s\t%d\tOVH\t%d#%d\t-\t-\t-\t-\t-\t-\n", fid, t, n->npc_id, i);
                    else printf("%s\t%d\tOVH\t%d#%d\t%d\t-\t-\t-\t-\t-\n", fid, t, n->npc_id, i, sprite);
                }
                d->overhead = sprite;
                d->active = n->active;
                d->npc_id = n->npc_id;
                d->instance = n->instance_id;
                d->anim_tick = n->anim_tick;
            }
            for (int i = 0; i < TFA_MAX_HAZARDS; i++) {
                const TfaHazard* h = &s->hazards[i];
                DumpHazard* d = &hazards[i];
                int loc = h->kind == TFA_HAZ_NONE ? -1 : h->loc_id;
                int gfx = h->kind == TFA_HAZ_NONE ? -1 : h->gfx_id;
                if (d->gfx > 0 && d->gfx != loc)
                    printf("%s\t%d\tOBJ-\t-\t%d\t%d\t%d\t1x1\t%d\t-\n", fid, t, d->gfx, d->x, d->y, d->orientation);
                if (loc > 0 && d->gfx != loc)
                    printf("%s\t%d\tOBJ+\t-\t%d\t%d\t%d\t1x1\t%d\t-\n", fid, t, loc, h->x, h->y, h->orientation);
                if (gfx > 0 && d->kind != h->kind + 1000 * gfx)
                    printf("%s\t%d\tGFX\t-\t%d\t%d\t%d\t-\t-\t-\n", fid, t, gfx, h->x, h->y);
                d->gfx = loc;
                d->x = h->x;
                d->y = h->y;
                d->orientation = h->orientation;
                d->kind = h->kind == TFA_HAZ_NONE ? 0 : h->kind + 1000 * gfx;
            }
            for (int i = 0; i < s->fx_count; i++) {
                const TfaFx* fx = &s->fx[i];
                if (fx->kind == TFA_FX_GROUND) {
                    printf("%s\t%d\tGFX\t-\t%d\t%d\t%d\t%d\t64,64\t-\n", fid, t, fx->gfx_id, fx->x, fx->y, fx->delay);
                    continue;
                }
                const TfaNpc* src = &s->npcs[fx->src_slot];
                pending[npending++] = (DumpProj){ t + fx->delay, fx->gfx_id, src->npc_id, fx->src_slot, src->x, src->y, fx->dst_player, fx->duration };
            }
            for (int i = 0; i < npending; i++) {
                if (pending[i].t != t) continue;
                printf("%s\t%d\tPROJ\t%d#%d\t%d\t%d\t%d\tP%d\t%d\t-\n", fid, t, pending[i].npc_id, pending[i].slot,
                    pending[i].gfx, pending[i].x, pending[i].y, pending[i].dst, pending[i].flight);
                pending[i--] = pending[--npending];
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
            for (int p = 0; p < team && invulnerable; p++) s->players[p].player.current_hitpoints = 10000;
            tfa_step_team(s, ctx, acts);
            t++;
        }
        fprintf(stderr, "%s outcome %d ticks %d\n", fid, s->outcome, s->tick);
    }
    free(s);
    free(ctx);
    return 0;
}
