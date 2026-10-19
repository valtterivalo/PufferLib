/* Bit-exactness of the trainer-facing env: steps trainer-shaped vectors of osrs_riskfight envs (riskfight_env_harness.h: the live-sweep kwargs, bf16 observations
 * unless -DTRACE_FLOAT, random-mask and scripted action streams) over hundreds of sessions and hashes everything the trainer reads after every step: the
 * observation buffer, the mask buffer, rewards, terminals; at the end each env's log, RNG states and session counters. The golden hashes below were taken on
 * the tree before the throughput work (c-main f28fd27c9); any optimisation of puf_step / the observation / the mask has to leave them unchanged.
 *
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o trace ocean/osrs/tests/test_riskfight_env_trace.c riskfight_oppfit_bridge.o -lm -lpthread
 *   RF_OPPFIT_MODEL=ocean/osrs/tests/fixtures/model.fit RF_OPPFIT_EAT=ocean/osrs/tests/fixtures/eat_mixture.fit trace [sweeps] [envs]   (prints hashes, checks the goldens at the default size)
 * The suite sets the two RF_OPPFIT_* variables itself only through this file's setenv defaults. */
#include "riskfight_env_harness.h"

enum { DEFAULT_SWEEPS = 20000, DEFAULT_ENVS = 48 };

typedef struct { uint64_t hash; long steps, waits, sessions, fights; } Trace;

/* The oracle: what the buffers must hold after a step is a full recomputation from the env's state, however the step got there (a shortcut that rewrites only
 * what changed has to agree with it byte for byte). The reference writers are the ones in ocean/osrs/encounters/riskfight/riskfight_policy.h. */
static void oracle_check(const Env* e, int sweep, int index) {
    for (int a = 0; a < e->num_agents; a++) {
        float f[RF_OBS_SIZE];
        riskfight_write_observation(&e->state, a, f);
        obs_t expect[RF_OBS_SIZE];
#ifdef RF_BF16_OBS
        uint16_t bits16[RF_OBS_SIZE];
        for (int j = 0; j < RF_OBS_SIZE; j++) { uint32_t b; memcpy(&b, &f[j], 4); bits16[j] = (uint16_t)((b + 0x7FFFu + ((b >> 16) & 1u)) >> 16); }
        memcpy(expect, bits16, sizeof(bits16));
#else
        memcpy(expect, f, sizeof(f));
#endif
        unsigned char mask[RF_MASK_SIZE];
        if (e->wait_left > 0) riskfight_write_wait_mask(mask); else riskfight_write_action_mask(&e->state, a, mask);
        if (memcmp(expect, e->agents[a].observations, sizeof(expect)) || memcmp(mask, e->agents[a].action_mask, sizeof(mask))) {
            for (int j = 0; j < RF_OBS_SIZE; j++) if (memcmp(&expect[j], &e->agents[a].observations[j], sizeof(obs_t)))
                fprintf(stderr, "obs[%d] differs (float %g)\n", j, f[j]);
            for (int j = 0; j < RF_MASK_SIZE; j++) if (mask[j] != e->agents[a].action_mask[j]) fprintf(stderr, "mask[%d] differs\n", j);
            fprintf(stderr, "oracle mismatch: sweep %d env %d agent %d wait_left %d\n", sweep, index, a, e->wait_left);
            abort();
        }
    }
}

static Trace run_trace(int self_play, HarnessPolicy policy, int sweeps, int envs, int seed_base) {
    HarnessVec v;
    harness_vec_create_variant(&v, envs, seed_base, self_play);
    uint64_t h = UINT64_C(1469598103934665603);
    Trace t = {0};
    h = harness_hash(h, v.obs, (size_t)v.agents * RF_OBS_SIZE * sizeof(obs_t));
    h = harness_hash(h, v.mask, (size_t)v.agents * RF_MASK_SIZE);
    for (int sweep = 0; sweep < sweeps; sweep++) {
        for (int i = 0; i < v.count; i++) harness_choose_actions(&v, i, policy);
        memset(v.rewards, 0, v.agents * sizeof(float));
        memset(v.terminals, 0, v.agents * sizeof(float));
        for (int i = 0; i < v.count; i++) { t.waits += v.envs[i].wait_left > 0; puf_step(&v.envs[i]); }
        t.steps += v.count;
        for (int i = 0; i < v.count; i++) oracle_check(&v.envs[i], sweep, i);
        h = harness_hash(h, v.obs, (size_t)v.agents * RF_OBS_SIZE * sizeof(obs_t));
        h = harness_hash(h, v.mask, (size_t)v.agents * RF_MASK_SIZE);
        h = harness_hash(h, v.rewards, v.agents * sizeof(float));
        h = harness_hash(h, v.terminals, v.agents * sizeof(float));
    }
    for (int i = 0; i < v.count; i++) {
        Env* e = &v.envs[i];
        h = harness_hash(h, &e->log, sizeof(e->log));
        h = harness_hash(h, &e->state.env.rng_state, sizeof(e->state.env.rng_state));
        h = harness_hash(h, &e->training.rng, sizeof(e->training.rng));
        h = harness_hash(h, &e->training.rep, sizeof(e->training.rep));
        h = harness_hash(h, &e->session_elapsed, sizeof(e->session_elapsed));
        h = harness_hash(h, &e->wait_left, sizeof(e->wait_left));
        t.sessions += (long)e->log.n;
        t.fights += (long)e->log.rounds;
    }
    t.hash = h;
    harness_vec_destroy(&v);
    return t;
}

/* The goldens depend on the compiler and libm (float contraction, lround): Apple clang arm64 and x86-64 GCC 13 -O2 (the trainer's host compiler) are kept; on
 * anything else the hashes are printed and only the oracle checks. Before and after the throughput work they are the same on both. */
#if defined(__clang__) && defined(__aarch64__)
#define PLATFORM 0
#elif defined(__x86_64__) && defined(__GNUC__) && !defined(__clang__)
#define PLATFORM 1
#else
#define PLATFORM -1
#endif
#ifndef TRACE_FLOAT
#define G(arm_bf16, arm_f32, x86_bf16, x86_f32) { UINT64_C(arm_bf16), UINT64_C(x86_bf16) }
#else
#define G(arm_bf16, arm_f32, x86_bf16, x86_f32) { UINT64_C(arm_f32), UINT64_C(x86_f32) }
#endif
typedef struct { const char* name; int self_play; HarnessPolicy policy; int seed; uint64_t golden[2]; } Case;
static const Case CASES[] = {
    {"selfplay_mixed", 1, POLICY_MIXED, 11, G(0xbb42be8dddbbff8d, 0x650bf3ed7c3d64eb, 0xa2839c5dee7a2c6d, 0x8e1fd7a965d06f41)},
    {"selfplay_script", 1, POLICY_SCRIPT, 23, G(0x8466801b74003999, 0xf2acc16bb5107090, 0xc7ca7442bb51234c, 0xbf868f9d5cd6a96d)},
    {"selfplay_random", 1, POLICY_RANDOM, 37, G(0xe2b40d8f7e6d3047, 0x7c421121cfa97c19, 0xe2b40d8f7e6d3047, 0xe9f1a62c6871951f)},
    {"botplay_script", 0, POLICY_SCRIPT, 41, G(0x3e4e514b9e520d8a, 0xea30850232f62fe8, 0x2b665f791acccffd, 0xaf156f1bc2316c59)},
    {"botplay_mixed", 0, POLICY_MIXED, 53, G(0xbfc7b966e15126ec, 0x36b440c475ad7ac5, 0xc266162f15388da4, 0x575034504adbe1e7)},
};


#ifndef TRACE_BASE /* -DTRACE_BASE builds the test against a tree from before the throughput work, to compare hashes across trees */
/* The memoized pool prior (riskfight_pool_prior_memo) against the scan of the pool, bit for bit: every kit's own gear, kits with a slot or the weapon swapped
 * for another kit's, asked repeatedly and with both caps in use. */
static void check_prior_memo(void) {
    uint32_t rng = 12345;
    long checked = 0;
    for (int round = 0; round < 3; round++) for (int k = 0; k < RISKFIGHT_POOL_COUNT; k++) for (int variant = 0; variant < 6; variant++) {
        uint8_t visible[NUM_GEAR_SLOTS];
        memcpy(visible, RISKFIGHT_POOL[k].equipment, NUM_GEAR_SLOTS);
        if (variant) {
            const RiskfightKit* other = &RISKFIGHT_POOL[xorshift32(&rng) % RISKFIGHT_POOL_COUNT];
            int slot = RF_POOL_VISIBLE_SLOTS[xorshift32(&rng) % (sizeof(RF_POOL_VISIBLE_SLOTS) / sizeof(*RF_POOL_VISIBLE_SLOTS))];
            visible[slot] = other->equipment[slot];
            if (variant & 1) visible[GEAR_SLOT_WEAPON] = other->equipment[GEAR_SLOT_WEAPON];
        }
        for (int cap = 1; cap <= RF_PRIOR_MEMO_CAP; cap += 4) {
            uint8_t i0[RF_PRIOR_MEMO_CAP], i1[RF_PRIOR_MEMO_CAP];
            float p0[RF_PRIOR_MEMO_CAP], p1[RF_PRIOR_MEMO_CAP];
            int n0 = riskfight_pool_prior(visible, i0, p0, cap), n1 = riskfight_pool_prior_memo(visible, i1, p1, cap);
            assert(n0 == n1 && !memcmp(i0, i1, n0) && !memcmp(p0, p1, n0 * sizeof(float)));
            checked++;
        }
    }
    printf("pool prior memo: %ld lookups equal the pool scan\n", checked);
}
#endif

int main(int argc, char** argv) {
    setenv("RF_OPPFIT_MODEL", "ocean/osrs/tests/fixtures/model.fit", 0);
    setenv("RF_OPPFIT_EAT", "ocean/osrs/tests/fixtures/eat_mixture.fit", 0);
#ifndef TRACE_BASE
    check_prior_memo();
#endif
    int sweeps = argc > 1 ? atoi(argv[1]) : DEFAULT_SWEEPS;
    int envs = argc > 2 ? atoi(argv[2]) : DEFAULT_ENVS;
    const int check = PLATFORM >= 0 && sweeps == DEFAULT_SWEEPS && envs == DEFAULT_ENVS;
    int failed = 0;
    for (size_t c = 0; c < sizeof(CASES) / sizeof(*CASES); c++) {
        Trace t = run_trace(CASES[c].self_play, CASES[c].policy, sweeps, envs, CASES[c].seed);
        printf("%-16s hash 0x%016llx steps %ld idle %.3f sessions %ld fights %ld%s\n", CASES[c].name, (unsigned long long)t.hash, t.steps,
            (double)t.waits / t.steps, t.sessions, t.fights, !check ? "" : t.hash == CASES[c].golden[PLATFORM < 0 ? 0 : PLATFORM] ? " ok" : " MISMATCH");
        if (check && t.hash != CASES[c].golden[PLATFORM]) failed = 1;
        assert(t.sessions >= envs); /* sessions of 3000 ticks complete */
    }
    return failed;
}
