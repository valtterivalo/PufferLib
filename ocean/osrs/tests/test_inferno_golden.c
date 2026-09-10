#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ocean/osrs/encounters/encounter_inferno.h"

#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

static inline uint64_t fnv_bytes(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= FNV_PRIME;
    }
    return h;
}

static inline uint64_t fnv_f32(uint64_t h, float v) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    return fnv_bytes(h, &bits, sizeof(bits));
}

static inline uint64_t fnv_i32(uint64_t h, int v) {
    int32_t w = (int32_t)v;
    return fnv_bytes(h, &w, sizeof(w));
}
static inline uint64_t fnv_u8(uint64_t h, uint8_t v) {
    return fnv_bytes(h, &v, sizeof(v));
}

static inline uint64_t fnv_u16(uint64_t h, uint16_t v) {
    return fnv_bytes(h, &v, sizeof(v));
}


static inline uint64_t splitmix64(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

#define TRACE_NPC_SLOTS 32

typedef struct {
    uint64_t state;
    uint64_t reward;
    uint64_t mask;
    uint64_t terminal;
    uint64_t observation;
} TraceHashes;

static uint64_t hash_core_state(uint64_t h, const InfernoState* s) {
    h = fnv_bytes(h, &s->player, offsetof(Player, interaction));
    h = fnv_i32(h, s->player.interaction.target_slot);
    h = fnv_bytes(
        h,
        &s->player.item_effect_state,
        sizeof(s->player) - offsetof(Player, item_effect_state));
    h = fnv_bytes(h, s->pillars, sizeof(s->pillars));
    h = fnv_bytes(h, s->npcs, TRACE_NPC_SLOTS * sizeof(s->npcs[0]));
    h = fnv_bytes(h, &s->player_pending_hits, sizeof(s->player_pending_hits));
    h = fnv_bytes(h, s->pending_sparks, sizeof(s->pending_sparks));
    for (int cell_idx = 0; cell_idx < OSRS_INVENTORY_SIZE; cell_idx++) {
        const OsrsInventoryCell* cell = &s->player.inventory_cells[cell_idx];
        h = fnv_u8(h, osrs_inventory_cell_item_index(cell));
        h = fnv_u16(h, osrs_inventory_cell_raw_osrs_id(cell));
        h = fnv_u8(h, osrs_inventory_cell_dose_count(cell));
    }
    h = fnv_bytes(h, s->dead_mobs, sizeof(s->dead_mobs));
    h = fnv_i32(h, s->wave);
    return fnv_i32(h, s->tick);
}

static uint64_t hash_semantic_mask(
    uint64_t h,
    const InfernoState* s,
    const float* mask
) {
    int offset = 0;
    for (int head = 0; head < INF_NUM_ACTION_HEADS; head++) {
        if (head != INF_HEAD_PRIMARY) {
            for (int action = 0; action < INF_ACTION_DIMS[head]; action++)
                h = fnv_f32(h, mask[offset + action]);
            offset += INF_ACTION_DIMS[head];
            continue;
        }

        for (int action = 0; action < OSRS_PRIMARY_MOVE_ACTIONS; action++)
            h = fnv_f32(h, mask[offset + action]);
        for (int npc_idx = 0; npc_idx < TRACE_NPC_SLOTS; npc_idx++) {
            int slot = inf_find_target_obs_slot(s, npc_idx);
            h = fnv_f32(h, slot >= 0
                ? mask[offset + inf_primary_attack_action_for_obs_slot(slot)]
                : 0.0f);
        }
        offset += INF_ACTION_DIMS[head];
    }
    return h;
}

static TraceHashes run_episode(int start_wave, uint32_t seed, int max_ticks) {
    InfernoContext context;
    InfernoState state_storage;
    inf_init_context_typed(&context);
    inf_init_state_typed(&state_storage, &context);
    inf_put_int_ctx(
        (EncounterState*)&state_storage,
        (EncounterContext*)&context,
        "start_wave",
        start_wave);
    inf_finalize_route_topology(&context);
    inf_reset_ctx(
        (EncounterState*)&state_storage,
        (EncounterContext*)&context,
        seed);

    InfernoState* s = &state_storage;
    static float obs[INF_NUM_OBS];
    static float mask[INF_ACTION_MASK_SIZE];
    int actions[INF_NUM_ACTION_HEADS];
    uint64_t arng = ((uint64_t)seed << 20) ^ (uint64_t)(start_wave + 1) ^
        0xD1B54A32D192ED03ULL;
    TraceHashes hashes = {
        .state = FNV_OFFSET,
        .reward = FNV_OFFSET,
        .mask = FNV_OFFSET,
        .terminal = FNV_OFFSET,
        .observation = FNV_OFFSET,
    };

    for (int t = 0; t < max_ticks; t++) {
        inf_refresh_current_obs_slots_ctx(s, &context);
        for (int head = 0; head < INF_NUM_ACTION_HEADS; head++) {
            uint64_t random_value = splitmix64(&arng);
            actions[head] =
                (int)(random_value % (uint64_t)INF_ACTION_DIMS[head]);
        }

        inf_step_ctx(
            (EncounterState*)s, (EncounterContext*)&context, actions);
        inf_write_obs_ctx(
            (EncounterState*)s, (EncounterContext*)&context, obs);
        inf_write_mask_ctx(
            (EncounterState*)s, (EncounterContext*)&context, mask);

        for (int npc_idx = TRACE_NPC_SLOTS; npc_idx < INF_MAX_NPCS; npc_idx++) {
            if (s->npcs[npc_idx].active) {
                fprintf(stderr, "golden trace activated NPC slot %d\n", npc_idx);
                abort();
            }
        }
        hashes.state = hash_core_state(hashes.state, s);
        hashes.reward = fnv_f32(hashes.reward, s->reward);
        hashes.mask = hash_semantic_mask(hashes.mask, s, mask);
        hashes.terminal = fnv_i32(hashes.terminal, s->episode_over);
        hashes.terminal = fnv_i32(hashes.terminal, s->winner);
        hashes.terminal = fnv_i32(hashes.terminal, s->wave);
        for (int i = 0; i < INF_NUM_OBS; i++)
            hashes.observation = fnv_f32(hashes.observation, obs[i]);

        if (s->episode_over) break;
    }

    inf_destroy_context((EncounterContext*)&context);
    return hashes;
}

typedef struct {
    const char* name;
    int start_wave;
    uint32_t seed;
} GoldenConfig;

static const GoldenConfig CONFIGS[] = {
    { "wave1_a",     1, 0x0000001u },
    { "wave1_b",     1, 0x0BADF00Du },
    { "wave1_c",     1, 0x1234567u },
    { "meleer_a",    9, 0x0000001u },
    { "meleer_b",    9, 0x0BADF00Du },
    { "ranger_a",   18, 0x0000001u },
    { "ranger_b",   18, 0x0BADF00Du },
    { "mager_a",    35, 0x0000001u },
    { "mager_b",    35, 0x0BADF00Du },
    { "jad_a",      67, 0x0000001u },
    { "jad_b",      67, 0x0BADF00Du },
    { "jad_c",      67, 0x1234567u },
    { "zuk_a",      69, 0x0000001u },
    { "zuk_b",      69, 0x0BADF00Du },
    { "zuk_c",      69, 0x1234567u },
};

#define NUM_CONFIGS ((int)(sizeof(CONFIGS) / sizeof(CONFIGS[0])))
#define EPISODE_TICKS 2000

/* Canonical inventory cells intentionally change the serialized player state hash. */
static const uint64_t EXPECTED_STATE[NUM_CONFIGS] = {
    0xc3c6ce9caedec932ULL, 0x5aefeff0eb53a8a0ULL, 0x1739fcfce20b0ac2ULL,
    0xb92f9a0ae51bf788ULL, 0xfdad7f55f7b7cb44ULL, 0xb8752e59eeff0240ULL,
    0x0eb8aa0167d94fdeULL, 0x7854ed938c91eb29ULL, 0x32c791f341d46705ULL,
    0xc562b4dc92c92dacULL, 0xa71848001975683aULL, 0xb3bb2650586b2fbeULL,
    0x2449187994da170aULL, 0x2f6ccb091a0432dcULL, 0x0600b75367c4e45fULL,
};

static const uint64_t EXPECTED_REWARD[NUM_CONFIGS] = {
    0xb089187e53857203ULL, 0xcd484e0aa89b231aULL, 0x9e5dcfd586cca42aULL,
    0x0368a3aa6f81109aULL, 0x51ad73340d19447aULL, 0xa84723357a2181a3ULL,
    0x6c34cdb27c0c1733ULL, 0xa11671953e858723ULL, 0xdce53c1df8560f83ULL,
    0x93d59ec47f5c8e53ULL, 0x4c54b1462ee3a663ULL, 0x3d09912556a163b3ULL,
    0x6c34cdb27c0c1733ULL, 0x47a1f9c7b1d3def3ULL, 0xe7f1c65d07654513ULL,
};

static const uint64_t EXPECTED_MASK[NUM_CONFIGS] = {
    0xc78a3cfc90fb040aULL, 0xcba9e085c6d856a3ULL, 0xb2ca79cd073b2ec3ULL,
    0x8a9246488843a2eaULL, 0xa353bc33b14bd0a3ULL, 0x0607f55b6567b7caULL,
    0x8b7bc0262228b5daULL, 0x9436e2cbac777d3aULL, 0x70a471b5f966653aULL,
    0xa692532e12d537c3ULL, 0xdf139831db54f453ULL, 0x77f977fac01526eaULL,
    0xdbe87f09bbc753a3ULL, 0xdff92be032bb7ecaULL, 0xa3c4b4613376930aULL,
};

static const uint64_t EXPECTED_TERMINAL[NUM_CONFIGS] = {
    0x45ce0c094429a073ULL, 0x26c550077ba89552ULL, 0xee8ee5c47541e453ULL,
    0xdfc4745cb07ec60bULL, 0x74622ccdf2f00ca2ULL, 0x323996e485ef6903ULL,
    0xda5efce8115c5862ULL, 0xce9f670c94cfbf73ULL, 0x565640d34d54c4f3ULL,
    0xc03f2da5d13a88e1ULL, 0x77852c52053de533ULL, 0x88ec0c389d9c42a1ULL,
    0x7c696af13bd0d307ULL, 0x12e57f8f9818c547ULL, 0xf92b462e21d67e67ULL,
};

static const uint64_t PRE_UNIFIED_OBSERVATION[NUM_CONFIGS] = {
    0xf2362a766a6212d1ULL, 0xa87ad065ca1666dcULL, 0xcac6b26329cadeafULL,
    0xda3602f8cf48fd1fULL, 0x9898fdcd480dc4e6ULL, 0x7c305c0b8123853fULL,
    0x9fb81d2ca3477306ULL, 0xb7e475f336034eadULL, 0x73f5ac59f227cb67ULL,
    0xda647dd6d85cfbe9ULL, 0x78ff2080d00d29c0ULL, 0x83cb4a3947e97146ULL,
    0xbf34279c73e947f4ULL, 0xdf399dc7aee3d3f3ULL, 0x989c8fc6b6aea06dULL,
};

static const uint64_t UNIFIED_OBSERVATION[NUM_CONFIGS] = {
    0x02416d3125af0312ULL, 0x81e238463f47c8acULL, 0xbe058a833eb51cceULL,
    0xdedcfee1bc8984a2ULL, 0x9b5e3e53f9bdbf84ULL, 0x9acbbc7b50a62331ULL,
    0x0424c0332c4929aeULL, 0x08121438552d14e6ULL, 0xdaa16046c108e993ULL,
    0x24cd9fd8fde43676ULL, 0x4089ce0317644e07ULL, 0x4b7cf5637f256071ULL,
    0x6c8db1b5cfab3a4eULL, 0x1e10c5bad008ede4ULL, 0x97e58a9ee67f1f66ULL,
};

static int check_hash(
    const char* config,
    const char* component,
    uint64_t actual,
    uint64_t expected
) {
    if (actual == expected) return 0;
    printf("  %-12s %-11s got 0x%016llx expected 0x%016llx\n",
        config,
        component,
        (unsigned long long)actual,
        (unsigned long long)expected);
    return 1;
}

int main(int argc, char** argv) {
    int print_mode = argc > 1 && strcmp(argv[1], "--print") == 0;
    inf_build_npc_stats();

    printf("inferno semantic golden (%d configs, <=%d ticks each)\n", NUM_CONFIGS,
        EPISODE_TICKS);
    printf("player contract obs%d primary%d heads%d\n\n",
        INF_NUM_OBS, INF_ACTION_DIMS[INF_HEAD_PRIMARY], INF_NUM_ACTION_HEADS);

    int failed = 0;
    for (int c = 0; c < NUM_CONFIGS; c++) {
        TraceHashes hashes =
            run_episode(CONFIGS[c].start_wave, CONFIGS[c].seed, EPISODE_TICKS);
        if (print_mode) {
            printf("%-12s %016llx %016llx %016llx %016llx %016llx\n",
                CONFIGS[c].name,
                (unsigned long long)hashes.state,
                (unsigned long long)hashes.reward,
                (unsigned long long)hashes.mask,
                (unsigned long long)hashes.terminal,
                (unsigned long long)hashes.observation);
            continue;
        }

        int config_failed = 0;
        config_failed += check_hash(
            CONFIGS[c].name, "state", hashes.state, EXPECTED_STATE[c]);
        config_failed += check_hash(
            CONFIGS[c].name, "reward", hashes.reward, EXPECTED_REWARD[c]);
        config_failed += check_hash(
            CONFIGS[c].name, "mask", hashes.mask, EXPECTED_MASK[c]);
        config_failed += check_hash(
            CONFIGS[c].name, "terminal", hashes.terminal, EXPECTED_TERMINAL[c]);
        config_failed += check_hash(
            CONFIGS[c].name, "observation", hashes.observation,
            UNIFIED_OBSERVATION[c]);
        if (PRE_UNIFIED_OBSERVATION[c] == UNIFIED_OBSERVATION[c]) {
            printf("  %-12s observation contract did not change\n", CONFIGS[c].name);
            config_failed++;
        }
        if (!config_failed) printf("  %-12s PASS\n", CONFIGS[c].name);
        failed += config_failed;
    }

    if (print_mode) return 0;
    printf("\n%d component mismatches\n", failed);
    return failed > 0 ? 1 : 0;
}
