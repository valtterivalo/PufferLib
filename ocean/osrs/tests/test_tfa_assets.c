/* The TFA viewer contract over scripted raids in every room, team sizes 1-4:
   - every model and animation it asks for exists in the caches it loads (a missing model aborts, a missing animation freezes);
   - the sim's floor lines up with the exported scene: every walkable tile reachable from the starts or stood on is inside the scene
     and walkable in its cache collision at the scene origin;
   - nobody leaves the floor: players stand on walkable tiles, NPC footprints on walkable tiles or tiles the live NPC of that id
     covered in the recordings (bosses on their platforms, the flying Guardian, pylons, pillars, swords). */
#include "../encounters/encounter_tfa.h"
#include "../osrs_anim.h"
#include "../osrs_collision.h"
#include "../osrs_npc_models.h"
#include "../osrs_spotanims.h"
#include "osrs_test_check.h"

#define TFA_ASSET_NO_ANIM 65535

typedef struct {
    uint32_t* ids;
    int count;
} ModelIds;

static void model_ids_read(ModelIds* set, const char* path) {
    FILE* f = osrs_asset_fopen(path, "rb");
    assert(f);
    uint32_t magic, count;
    osrs_read_exact(f, &magic, 4, 1, path, "magic");
    osrs_read_exact(f, &count, 4, 1, path, "count");
    uint32_t* offsets = malloc(count * sizeof(uint32_t));
    osrs_read_exact(f, offsets, 4, count, path, "offsets");
    set->ids = realloc(set->ids, (size_t)(set->count + (int)count) * sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i++) {
        osrs_seek_or_abort(f, (long)offsets[i], path);
        osrs_read_exact(f, &set->ids[set->count++], 4, 1, path, "model id");
    }
    free(offsets);
    fclose(f);
}

static int model_present(const ModelIds* set, uint32_t id) {
    for (int i = 0; i < set->count; i++) if (set->ids[i] == id) return 1;
    return 0;
}

static int anim_present(AnimCache* const* caches, int anim) {
    for (int i = 0; i < 3; i++) {
        AnimSequence* seq = anim_get_sequence(caches[i], (uint16_t)anim);
        if (seq && seq->frame_count > 0 && !(seq->frame_count == 1 && seq->frames[0].frame.kind == ANIM_FRAME_LEGACY
                && seq->frames[0].frame.framebase_id == 0xFFFF)) return 1;
    }
    return 0;
}

static int g_reported;

#define TFA_LIVE_MAX 20000

typedef struct { int npc_id, x, y; } LivePos;

static LivePos g_live[TFA_LIVE_MAX];
static int g_nlive;

static void live_positions_read(const char* room) {
    char path[256];
    snprintf(path, sizeof(path), "ocean/osrs/encounters/tfa/fixtures/tfa_%s_events.tsv", room);
    FILE* f = fopen(path, "r");
    g_nlive = 0;
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char* col[10] = {0};
        char* save = NULL;
        int n = 0;
        for (char* tok = strtok_r(line, "\t\n", &save); tok && n < 10; tok = strtok_r(NULL, "\t\n", &save)) col[n++] = tok;
        if (n < 7 || strcmp(col[2], "NPOS") != 0 || col[5][0] == '-') continue;
        assert(g_nlive < TFA_LIVE_MAX);
        g_live[g_nlive++] = (LivePos){ atoi(col[4]), atoi(col[5]), atoi(col[6]) };
    }
    fclose(f);
}

static int live_covered(const TfaNpc* n, int x, int y) {
    for (int i = 0; i < g_nlive; i++)
        if (g_live[i].npc_id == n->npc_id && x >= g_live[i].x && x < g_live[i].x + n->size && y >= g_live[i].y && y < g_live[i].y + n->size) return 1;
    return 0;
}

static int scene_walkable(const CollisionMap* m, const EncounterDef* def, int x, int y) {
    int wx = x + def->scene_origin_x, wy = y + def->scene_origin_y;
    return collision_map_get(m, collision_region_hash(wx, wy)) && !(collision_get_flags(m, 0, wx, wy) & COLLISION_BLOCKED);
}

static int g_floor_reported;

static void require_floor(int ok, const char* what, int x, int y, int tick, int room) {
    if (ok || g_floor_reported >= 20) return;
    g_floor_reported++;
    printf("  room %d: %s at %d,%d tick %d\n", room, what, x, y, tick);
    CHECK("on the floor", 0);
}

static void require(int ok, const char* what, uint32_t id, int room) {
    if (ok || g_reported >= 20) return;
    g_reported++;
    printf("  room %d: missing %s %u (0x%X)\n", room, what, id, id);
    CHECK("asset present", 0);
}

static void check_room(const EncounterDef* def, int room) {
    char path[256];
    ModelIds models = {0};
    snprintf(path, sizeof(path), "%s.models", def->npc_pack);
    model_ids_read(&models, OSRS_ASSET(path));
    model_ids_read(&models, OSRS_ASSET("equipment.models"));
    model_ids_read(&models, OSRS_ASSET("projectiles.models"));
    snprintf(path, sizeof(path), "%s.anims", def->npc_pack);
    AnimCache* anims[3] = { anim_cache_load(OSRS_ASSET(path)), anim_cache_load(OSRS_ASSET("equipment.anims")),
        anim_cache_load(OSRS_ASSET("projectiles.anims")) };

    TfaState* s = calloc(1, sizeof(TfaState));
    TfaContext* ctx = calloc(1, sizeof(TfaContext));
    def->init_context((EncounterContext*)ctx);
    tfa_finalize_context((EncounterState*)s, (EncounterContext*)ctx);
    EncounterOverlay* ov = calloc(1, sizeof(EncounterOverlay));
    RenderEntity ents[TFA_MAX_PLAYERS + TFA_MAX_NPCS];
    live_positions_read(def->name + 4);
    const CollisionMap* scene = collision_map_scene(def->scene);
    const TfaRoomLayout* layout = tfa_room_layout(room);
    uint8_t floor[TFA_ROOM_SIZE][TFA_ROOM_SIZE] = {{0}};
    int queue[TFA_ROOM_SIZE * TFA_ROOM_SIZE], head = 0, tail = 0;
    for (int k = 0; k < TFA_MAX_PLAYERS; k++) {
        if (floor[layout->start_y[k]][layout->start_x[k]]) continue;
        floor[layout->start_y[k]][layout->start_x[k]] = 1;
        queue[tail++] = layout->start_y[k] * TFA_ROOM_SIZE + layout->start_x[k];
    }
    while (head < tail) {
        int cx = queue[head] % TFA_ROOM_SIZE, cy = queue[head++] / TFA_ROOM_SIZE;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int nx = cx + dx, ny = cy + dy;
                if (tfa_layout_blocked(layout, nx, ny) || floor[ny][nx]) continue;
                floor[ny][nx] = 1;
                queue[tail++] = ny * TFA_ROOM_SIZE + nx;
            }
    }
    long floating = 0, projectiles = 0, ground = 0, ticks = 0;
    static OsrsSpotAnimSet* spotanims;
    if (!spotanims) spotanims = osrs_spotanims_load(OSRS_ASSET("spotanims.bin"));
    for (int e = 0; e < 20; e++) {
        ctx->config.team_size = 1 + e % TFA_MAX_PLAYERS;
        tfa_reset_typed(s, ctx, 500 + e);
        int acts[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS];
        while (!s->episode_over) {
            for (int p = 0; p < TFA_MAX_PLAYERS; p++) tfa_heuristic_actions(s, p, acts + p * TFA_NUM_ACTION_HEADS);
            tfa_step_team(s, ctx, acts);
            ticks++;
            for (int p = 0; p < s->team_size; p++) {
                if (s->players[p].life != TFA_LIFE_ALIVE) continue;
                int x = s->players[p].player.x, y = s->players[p].player.y;
                require_floor(!tfa_layout_blocked(layout, x, y), "player on a blocked tile", x, y, s->tick, room);
                floor[y][x] = 1;
            }
            for (int i = 0; i < TFA_MAX_NPCS; i++) {
                const TfaNpc* npc = &s->npcs[i];
                if (!npc->active) continue;
                for (int dx = 0; dx < npc->size; dx++)
                    for (int dy = 0; dy < npc->size; dy++) {
                        int x = npc->x + dx, y = npc->y + dy;
                        require_floor(x >= 0 && y >= 0 && x < TFA_ROOM_SIZE && y < TFA_ROOM_SIZE, "npc outside the room", x, y, s->tick, room);
                        int walkable = !tfa_layout_blocked(layout, x, y);
                        require_floor(walkable || live_covered(npc, x, y), "npc off the floor where the live npc never stood", x, y, s->tick, room);
                        if (walkable) floor[y][x] = 1;
                    }
            }
            int n = 0;
            tfa_fill_render_entities((EncounterState*)s, (EncounterContext*)ctx, ents, TFA_MAX_PLAYERS + TFA_MAX_NPCS, &n);
            for (int i = 0; i < n; i++) {
                if (ents[i].entity_type != ENTITY_NPC) continue;
                const NpcModelMapping* m = npc_model_lookup((uint16_t)ents[i].npc_def_id);
                require(m != NULL, "npc model mapping", (uint32_t)ents[i].npc_def_id, room);
                if (!m) continue;
                require(model_present(&models, m->model_id), "npc model", m->model_id, room);
                if (m->idle_anim != TFA_ASSET_NO_ANIM) require(anim_present(anims, (int)m->idle_anim), "npc idle anim", m->idle_anim, room);
                if (ents[i].npc_anim_id >= 0) require(anim_present(anims, ents[i].npc_anim_id), "npc anim", (uint32_t)ents[i].npc_anim_id, room);
            }
            tfa_render_post_tick((EncounterState*)s, (EncounterContext*)ctx, ov);
            for (int i = 0; i < ov->floating_model_count; i++) {
                require(model_present(&models, ov->floating_models[i].model_id), "floating model", ov->floating_models[i].model_id, room);
                if (ov->floating_models[i].anim_id >= 0)
                    require(anim_present(anims, ov->floating_models[i].anim_id), "floating anim", (uint32_t)ov->floating_models[i].anim_id, room);
            }
            for (int i = 0; i < ov->ground_spotanim_count; i++) {
                int gfx = ov->ground_spotanims[i].gfx_id;
                const OsrsSpotAnimDef* def_gfx = osrs_spotanim_find(spotanims, gfx);
                require(def_gfx != NULL, "ground spotanim definition", (uint32_t)gfx, room);
                require(model_present(&models, OSRS_SPOTANIM_MODEL_BASE | (uint32_t)gfx), "ground spotanim model", OSRS_SPOTANIM_MODEL_BASE | (uint32_t)gfx, room);
                if (def_gfx && def_gfx->animation_id >= 0) require(anim_present(anims, def_gfx->animation_id), "ground spotanim anim", (uint32_t)def_gfx->animation_id, room);
            }
            ground += ov->ground_spotanim_count;
            for (int i = 0; i < ov->projectile_count; i++) {
                require(model_present(&models, ov->projectiles[i].model_id), "projectile model", ov->projectiles[i].model_id, room);
                if (ov->projectiles[i].anim_id >= 0)
                    require(anim_present(anims, ov->projectiles[i].anim_id), "projectile anim", (uint32_t)ov->projectiles[i].anim_id, room);
            }
            floating += ov->floating_model_count;
            projectiles += ov->projectile_count;
        }
    }
    int floor_tiles = 0, aligned = 0;
    for (int y = 0; y < TFA_ROOM_SIZE; y++)
        for (int x = 0; x < TFA_ROOM_SIZE; x++) {
            if (!floor[y][x]) continue;
            floor_tiles++;
            aligned += scene_walkable(scene, def, x, y);
            require_floor(scene_walkable(scene, def, x, y), "sim floor not walkable in the scene", x, y, -1, room);
        }
    printf("%-14s ticks %ld floating %ld projectiles %ld ground %ld floor %d/%d aligned with the scene\n", def->name, ticks, floating, projectiles, ground, aligned, floor_tiles);
    CHECK("room drew hazards or projectiles", floating + projectiles + ground > 0);
    free(ov);
    free(s);
    free(ctx);
    free(models.ids);
}

int main(void) {
    const EncounterDef* defs[TFA_ROOM_COUNT] = {
        [TFA_ROOM_GUARDIAN] = &ENCOUNTER_TFA_guardian, [TFA_ROOM_BORK] = &ENCOUNTER_TFA_bork,
        [TFA_ROOM_EZEKIEL] = &ENCOUNTER_TFA_ezekiel, [TFA_ROOM_DREADNOTH] = &ENCOUNTER_TFA_dreadnoth,
        [TFA_ROOM_MOIA] = &ENCOUNTER_TFA_moia, [TFA_ROOM_XARTH] = &ENCOUNTER_TFA_xarth, [TFA_ROOM_DAGONHAI] = &ENCOUNTER_TFA_dagonhai,
        [TFA_ROOM_OCELLUS] = &ENCOUNTER_TFA_ocellus,
    };
    for (int r = 0; r < TFA_NUM_IMPLEMENTED_ROOMS; r++) check_room(defs[TFA_IMPLEMENTED_ROOMS[r]], TFA_IMPLEMENTED_ROOMS[r]);
    return osrs_test_summary();
}
