/* Room geometry over the 64x64 template region, flags indexed y * 64 + x with OSRS collision bits. */
#define TFA_ROOM_SIZE 64
typedef struct {
    const uint32_t* flags;
    int boss_x, boss_y;
    int start_x[TFA_MAX_PLAYERS], start_y[TFA_MAX_PLAYERS];
} TfaRoomLayout;

#include "tfa_rooms_generated.inc"

static const TfaRoomLayout* tfa_room_layout(int room) {
    assert(room >= 0 && room < TFA_ROOM_COUNT);
    return &TFA_ROOM_LAYOUTS[room];
}

static uint32_t tfa_layout_flags(const TfaRoomLayout* l, int x, int y) {
    if (x < 0 || y < 0 || x >= TFA_ROOM_SIZE || y >= TFA_ROOM_SIZE) return COLLISION_BLOCKED;
    return l->flags[y * TFA_ROOM_SIZE + x];
}

static int tfa_layout_blocked(const TfaRoomLayout* l, int x, int y) {
    return (tfa_layout_flags(l, x, y) & COLLISION_BLOCKED) != 0;
}

static uint32_t tfa_topology_flags(void* data, int x, int y) {
    return tfa_layout_flags((const TfaRoomLayout*)data, x, y);
}

static EncounterArenaTopology* TFA_TOPOLOGY_OWNER[TFA_ROOM_COUNT];

static void tfa_build_topologies(TfaContext* ctx) {
    for (int room = 0; room < TFA_ROOM_COUNT; room++) {
        const TfaRoomLayout* l = tfa_room_layout(room);
        EncounterArenaTopologyBuildSpec spec = {
            .origin_x = 0,
            .origin_y = 0,
            .width = TFA_ROOM_SIZE,
            .height = TFA_ROOM_SIZE,
            .max_footprint_size = ENCOUNTER_ARENA_TOPOLOGY_MAX_FOOTPRINT_SIZE,
            .revision = UINT64_C(0x5446410000000000) | (uint64_t)room,
            .tile_flags = tfa_topology_flags,
            .tile_flags_ctx = (void*)l,
            .los_tile_flags = tfa_topology_flags,
            .los_tile_flags_ctx = (void*)l,
            /* Moia's pocket is walled on walk flags only: P0 hit her from inside the ring in 12 of 12 attacks */
            .los_build_mode = room == TFA_ROOM_MOIA ? ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_OPEN : ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_TILE_BLOCKED,
        };
        if (!TFA_TOPOLOGY_OWNER[room]) {
            TFA_TOPOLOGY_OWNER[room] = encounter_arena_topology_build(&spec);
            encounter_arena_topology_finalize(TFA_TOPOLOGY_OWNER[room]);
        } else {
            encounter_arena_topology_require_spec(TFA_TOPOLOGY_OWNER[room], &spec, "tfa");
        }
        ctx->topology[room] = TFA_TOPOLOGY_OWNER[room];
    }
}

/* The room sequence of one raid: Guardian, then one of each pair by path bit, then Ocellus. */
static int tfa_raid_room(int index, int path) {
    static const int PAIRS[3][2] = {
        { TFA_ROOM_BORK, TFA_ROOM_EZEKIEL },
        { TFA_ROOM_DREADNOTH, TFA_ROOM_MOIA },
        { TFA_ROOM_XARTH, TFA_ROOM_DAGONHAI },
    };
    if (index == 0) return TFA_ROOM_GUARDIAN;
    if (index == TFA_RAID_LENGTH - 1) return TFA_ROOM_OCELLUS;
    return PAIRS[index - 1][(path >> (index - 1)) & 1];
}
