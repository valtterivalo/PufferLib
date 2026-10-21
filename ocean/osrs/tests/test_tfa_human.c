#include "../encounters/encounter_tfa.h"
#include "osrs_test_check.h"

static int far_reachable_tile(const TfaState* s, const TfaContext* ctx, int* tx, int* ty) {
    const Player* p = &s->players[0].player;
    int best = 0;
    for (int dy = -12; dy <= 12; dy++)
        for (int dx = -12; dx <= 12; dx++) {
            EncounterRouteInput in = {
                .topology = ctx->topology[s->room],
                .source_x = p->x, .source_y = p->y, .actor_size = 1,
                .target_x = p->x + dx, .target_y = p->y + dy, .target_size = 1,
                .target_kind = ENCOUNTER_ROUTE_TARGET_TILE,
                .movement_mode = ENCOUNTER_ROUTE_MOVEMENT_RUN,
                .cost_policy = ENCOUNTER_ROUTE_COST_SOUTH_FIRST_BFS,
            };
            EncounterRouteResult route = encounter_route_solve(&in);
            if (route.outcome != ROUTE_REACHED_TARGET || route.distance <= best) continue;
            best = route.distance;
            *tx = in.target_x, *ty = in.target_y;
        }
    return best >= 3;
}

int main(void) {
    const EncounterDef* def = encounter_find("tfa_guardian");
    for (int r = 0; r < TFA_NUM_IMPLEMENTED_ROOMS; r++) {
        int room = TFA_IMPLEMENTED_ROOMS[r];
        TfaState* s = calloc(1, sizeof(TfaState));
        TfaContext* ctx = calloc(1, sizeof(TfaContext));
        def->init_context((EncounterContext*)ctx);
        ctx->config.start_room = room;
        def->finalize_context((EncounterState*)s, (EncounterContext*)ctx);
        def->reset((EncounterState*)s, (EncounterContext*)ctx, 7);
        HumanInput hi;
        human_input_init(&hi);

        int tx, ty;
        CHECK("room has a far reachable tile", far_reachable_tile(s, ctx, &tx, &ty));
        human_input_queue_walk(&hi, tx, ty);
        int ticks = 0;
        while (hi.pending_move_x >= 0 && ticks < 20) {
            def->step_human_commands((EncounterState*)s, (EncounterContext*)ctx, &hi);
            ticks++;
        }
        const Player* p = &s->players[0].player;
        printf("room %d walk to (%d,%d): at (%d,%d) after %d ticks\n", room, tx, ty, p->x, p->y, ticks);
        CHECK("a walk click reaches the clicked tile", p->x == tx && p->y == ty);
        CHECK("an arrived walk clears the pending move", hi.pending_move_x < 0);
        CHECK("a walk takes more than one tick", ticks > 1);

        int slot = -1;
        for (int k = 0; k < TFA_NUM_TARGET_SLOTS && slot < 0; k++)
            if (tfa_target_slot_npc(s, k) >= 0) slot = k;
        if (slot >= 0) {
            human_input_queue_attack_npc(&hi, slot);
            def->step_human_commands((EncounterState*)s, (EncounterContext*)ctx, &hi);
            const OsrsInteraction* in = &s->players[0].player.interaction;
            CHECK("an attack click targets the clicked npc", osrs_interaction_active(in) && in->target_slot == slot);
        }
        human_input_destroy(&hi);
        def->destroy_context((EncounterContext*)ctx);
        free(s);
        free(ctx);
    }
    return osrs_test_summary();
}
