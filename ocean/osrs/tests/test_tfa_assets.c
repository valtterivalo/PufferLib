/* Every model and animation the TFA viewer asks for exists in the caches it loads: scripted raids per room,
   each tick's render entities, floating models and projectiles checked against the room pack, equipment and projectile caches.
   A missing model aborts the viewer, a missing animation silently freezes the model. */
#include "../encounters/encounter_tfa.h"
#include "../osrs_anim.h"
#include "../osrs_npc_models.h"
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
    long floating = 0, projectiles = 0, ticks = 0;
    for (int e = 0; e < 20; e++) {
        tfa_reset_typed(s, ctx, 500 + e);
        int acts[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS];
        while (!s->episode_over) {
            for (int p = 0; p < TFA_MAX_PLAYERS; p++) tfa_heuristic_actions(s, p, acts + p * TFA_NUM_ACTION_HEADS);
            tfa_step_team(s, ctx, acts);
            ticks++;
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
            for (int i = 0; i < ov->projectile_count; i++) {
                require(model_present(&models, ov->projectiles[i].model_id), "projectile model", ov->projectiles[i].model_id, room);
                if (ov->projectiles[i].anim_id >= 0)
                    require(anim_present(anims, ov->projectiles[i].anim_id), "projectile anim", (uint32_t)ov->projectiles[i].anim_id, room);
            }
            floating += ov->floating_model_count;
            projectiles += ov->projectile_count;
        }
    }
    printf("%-14s ticks %ld floating %ld projectiles %ld\n", def->name, ticks, floating, projectiles);
    CHECK("room drew hazards or projectiles", floating + projectiles > 0);
    free(ov);
    free(s);
    free(ctx);
    free(models.ids);
}

int main(void) {
    const EncounterDef* defs[TFA_ROOM_COUNT] = {
        [TFA_ROOM_GUARDIAN] = &ENCOUNTER_TFA_guardian, [TFA_ROOM_BORK] = &ENCOUNTER_TFA_bork,
        [TFA_ROOM_EZEKIEL] = &ENCOUNTER_TFA_ezekiel, [TFA_ROOM_DREADNOTH] = &ENCOUNTER_TFA_dreadnoth,
        [TFA_ROOM_MOIA] = &ENCOUNTER_TFA_moia, [TFA_ROOM_XARTH] = &ENCOUNTER_TFA_xarth,
        [TFA_ROOM_OCELLUS] = &ENCOUNTER_TFA_ocellus,
    };
    for (int r = 0; r < TFA_NUM_IMPLEMENTED_ROOMS; r++) check_room(defs[TFA_IMPLEMENTED_ROOMS[r]], TFA_IMPLEMENTED_ROOMS[r]);
    return osrs_test_summary();
}
