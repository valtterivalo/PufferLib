/* Raid sequencing: path bits pick one room per pair, state carries between rooms, dead raiders stay dead. */
#include "tfa_scripted.h"
#include "osrs_test_check.h"

int main(void) {
    TfaState* s = calloc(1, sizeof(TfaState));
    TfaContext* ctx = calloc(1, sizeof(TfaContext));
    tfa_init_context((EncounterContext*)ctx);
    ctx->config.team_size = 3;
    ctx->config.episode_mode = TFA_EPISODE_RAID;
    ctx->config.path = 1;
    tfa_finalize_context((EncounterState*)s, (EncounterContext*)ctx);
    tfa_reset_typed(s, ctx, 3);
    ASSERT_INT_EQ("raid starts at the Guardian", s->room, TFA_ROOM_GUARDIAN);
    s->players[1].player.current_hitpoints = 40;
    tfa_kill_player(s, 2);
    tfa_room_cleared(s, ctx);
    ASSERT_INT_EQ("path bit 0 set picks Ezekiel", s->room, TFA_ROOM_EZEKIEL);
    ASSERT_INT_EQ("hitpoints carry into the next room", s->players[1].player.current_hitpoints, 40);
    ASSERT_INT_EQ("a dead raider stays dead", s->players[2].life, TFA_LIFE_DEAD);
    ASSERT_INT_EQ("living raiders stand on the room's start tiles", s->players[0].player.x, tfa_room_layout(TFA_ROOM_EZEKIEL)->start_x[0]);
    tfa_room_cleared(s, ctx);
    ASSERT_INT_EQ("path bit 1 clear picks Dreadnoth", s->room, TFA_ROOM_DREADNOTH);
    tfa_room_cleared(s, ctx);
    ASSERT_INT_EQ("path bit 2 clear picks Xarth", s->room, TFA_ROOM_XARTH);
    ASSERT_INT_EQ("rooms cleared so far", s->rooms_cleared, 3);
    CHECK("the raid is not over before the last room", !s->episode_over);
    ASSERT_INT_EQ("raid index before the last room", s->raid_index, 3);
    ASSERT_INT_EQ("the last room is Ocellus", tfa_raid_room(4, 1), TFA_ROOM_OCELLUS);
    ASSERT_INT_EQ("path 2 picks Moia", tfa_raid_room(2, 2), TFA_ROOM_MOIA);
    ASSERT_INT_EQ("path 4 picks the Dagon'hai", tfa_raid_room(3, 4), TFA_ROOM_DAGONHAI);
    free(s);
    free(ctx);
    return osrs_test_summary();
}
