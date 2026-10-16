/* Env-only throughput of osrs_riskfight as the trainer drives it (puf_step over a vector of self-play envs, trainer-shaped buffers; riskfight_env_harness.h).
 * Separates fight ticks from idle ticks (the wait between fights) by whether the env was waiting when stepped.
 *
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o bench_riskfight_env ocean/osrs/tests/bench_riskfight_env.c riskfight_oppfit_bridge.o -lm -lpthread
 *   RF_OPPFIT_MODEL=... RF_OPPFIT_EAT=... bench_riskfight_env [sweeps=20000] [envs=64] [random|script|mixed] [threads=1] [stages | replay [reps=5]]
 *
 * A "sweep" steps every env once, the way a buffer's worker loop does. Each thread owns `envs` envs. Policy time is outside every timer. */
#include "riskfight_env_harness.h"
#include <pthread.h>

typedef struct {
    int sweeps, envs, thread;
    HarnessPolicy policy;
    double kind_ns[5]; long kind_calls[5]; /* 0 wait, 1 wait that ends in a new fight, 2 fight, 3 fight that ends the fight, 4 step that starts a new session */
    double sweep_ns, fight_ns, wait_ns; /* sweep_ns: only the puf_step loops, two clock reads per sweep; fight_ns / wait_ns: per call, two reads each */
    long fight_calls, wait_calls, sweep_env_steps;
} Job;

static void* run(void* arg) {
    Job* j = arg;
    HarnessVec v;
    harness_vec_create(&v, j->envs, j->thread * 100003);
    for (int warm = 0; warm < 200; warm++) {
        for (int i = 0; i < v.count; i++) harness_choose_actions(&v, i, j->policy);
        memset(v.rewards, 0, v.agents * sizeof(float)); memset(v.terminals, 0, v.agents * sizeof(float));
        for (int i = 0; i < v.count; i++) puf_step(&v.envs[i]);
    }
    for (int sweep = 0; sweep < j->sweeps; sweep++) {
        for (int i = 0; i < v.count; i++) harness_choose_actions(&v, i, j->policy);
        memset(v.rewards, 0, v.agents * sizeof(float)); memset(v.terminals, 0, v.agents * sizeof(float));
        double t0 = harness_now_ns();
        for (int i = 0; i < v.count; i++) puf_step(&v.envs[i]);
        j->sweep_ns += harness_now_ns() - t0;
        j->sweep_env_steps += v.count;
        /* a second sweep, timed per call to split fight ticks from idle ticks */
        for (int i = 0; i < v.count; i++) harness_choose_actions(&v, i, j->policy);
        memset(v.rewards, 0, v.agents * sizeof(float)); memset(v.terminals, 0, v.agents * sizeof(float));
        for (int i = 0; i < v.count; i++) {
            Env* e = &v.envs[i];
            const int w = e->wait_left > 0, last_wait = e->wait_left == 1;
            const float sessions = e->log.n;
            double a = harness_now_ns();
            puf_step(e);
            double d = harness_now_ns() - a;
            if (w) { j->wait_ns += d; j->wait_calls++; } else { j->fight_ns += d; j->fight_calls++; }
            int kind = e->log.n != sessions ? 4 : w ? (last_wait ? 1 : 0) : (e->state.env.episode_over ? 3 : 2);
            j->kind_ns[kind] += d; j->kind_calls[kind]++;
        }
    }
    harness_vec_destroy(&v);
    return NULL;
}


/* Cost of the pieces of one fight tick, measured on live fight states: every 8th sweep each fighting env's observation / mask / step stages are repeated REPS
 * times (the observation and mask writers are pure). Clock reads are 41 ns ticks on
 * Apple silicon, so every interval spans a whole REPS loop. */
#define REPS 8
static void stages(int sweeps, int envs, HarnessPolicy policy) {
    HarnessVec v;
    harness_vec_create(&v, envs, 7);
    double obs = 0, obs_core = 0, threat = 0, session = 0, store = 0, mask = 0, wait_observe = 0;
    long fight_samples = 0, wait_samples = 0;
    float f[RF_OBS_SIZE];
    unsigned char m[RF_MASK_SIZE];
    for (int sweep = 0; sweep < sweeps; sweep++) {
        for (int i = 0; i < v.count; i++) harness_choose_actions(&v, i, policy);
        memset(v.rewards, 0, v.agents * sizeof(float)); memset(v.terminals, 0, v.agents * sizeof(float));
        if (sweep % 8 == 0) {
            for (int i = 0; i < v.count; i++) {
                Env* e = &v.envs[i];
                double t0, t1;
                if (e->wait_left > 0) {
                    t0 = harness_now_ns();
                    for (int r = 0; r < REPS; r++) riskfight_native_observe(e);
                    wait_observe += harness_now_ns() - t0;
                    wait_samples++;
                    continue;
                }
                fight_samples++;
                t0 = harness_now_ns();
                for (int r = 0; r < REPS; r++) for (int a = 0; a < 2; a++) riskfight_write_observation(&e->state, a, f);
                t1 = harness_now_ns(); obs += t1 - t0; t0 = t1;
                for (int r = 0; r < REPS; r++) for (int a = 0; a < 2; a++) riskfight_write_observation_core(&e->state, a, f, NULL);
                t1 = harness_now_ns(); obs_core += t1 - t0; t0 = t1;
                for (int r = 0; r < REPS; r++) for (int a = 0; a < 2; a++) { RiskfightBagWeapons bw = {{0}, 0}; riskfight_threat_write(&e->state.env.players[a], &bw, &e->state.visible[a].threat, e->state.visible[a].health_bar, f + RF_THREAT_START); }
                t1 = harness_now_ns(); threat += t1 - t0; t0 = t1;
                for (int r = 0; r < REPS; r++) for (int a = 0; a < 2; a++) riskfight_write_session_observation(&e->state, a, f);
                t1 = harness_now_ns(); session += t1 - t0; t0 = t1;
                for (int r = 0; r < REPS; r++) for (int a = 0; a < 2; a++) riskfight_obs_store(e->agents[a].observations, f);
                t1 = harness_now_ns(); store += t1 - t0; t0 = t1;
                for (int r = 0; r < REPS; r++) for (int a = 0; a < 2; a++) riskfight_write_action_mask(&e->state, a, m);
                t1 = harness_now_ns(); mask += t1 - t0; t0 = t1;
            }
        }
        for (int i = 0; i < v.count; i++) puf_step(&v.envs[i]);
    }
    double n = (double)fight_samples * REPS;
    printf("{\"stage_ns_per_fight_tick\":{\"observation_2_agents\":%.1f,\"obs_core\":%.1f,\"threat\":%.1f,\"session\":%.1f,"
        "\"bf16_store\":%.1f,\"mask_2_agents\":%.1f},\"idle_tick_observe_ns\":%.1f,\"fight_samples\":%ld,\"wait_samples\":%ld}\n",
        obs / n, obs_core / n, threat / n, session / n, store / n, mask / n, wait_observe / ((double)wait_samples * REPS), fight_samples, wait_samples);
    harness_vec_destroy(&v);
}

/* The sim is deterministic, so the actions of one scripted / random pass can be replayed against freshly created envs: the replay is a pure puf_step workload
 * (no policy code in it), for a profiler or for timing without the harness's own work. Prints the replay's ns per env step, fights and idle together. */
static void replay(int sweeps, int envs, HarnessPolicy policy, int reps) {
    unsigned char* actions = malloc((size_t)sweeps * envs * 2 * RF_HEADS);
    HarnessVec v;
    harness_vec_create(&v, envs, 7);
    for (int sweep = 0; sweep < sweeps; sweep++) {
        for (int i = 0; i < v.count; i++) harness_choose_actions(&v, i, policy);
        for (int i = 0; i < v.count; i++) for (int a = 0; a < v.envs[i].num_agents; a++) for (int h = 0; h < RF_HEADS; h++)
            actions[(((size_t)sweep * envs + i) * 2 + a) * RF_HEADS + h] = (unsigned char)v.envs[i].agents[a].actions[h];
        memset(v.rewards, 0, v.agents * sizeof(float)); memset(v.terminals, 0, v.agents * sizeof(float));
        for (int i = 0; i < v.count; i++) puf_step(&v.envs[i]);
    }
    harness_vec_destroy(&v);
    double ns = 0; long steps = 0;
    for (int r = 0; r < reps; r++) {
        harness_vec_create(&v, envs, 7);
        for (int sweep = 0; sweep < sweeps; sweep++) {
            for (int i = 0; i < v.count; i++) for (int a = 0; a < v.envs[i].num_agents; a++) for (int h = 0; h < RF_HEADS; h++)
                v.envs[i].agents[a].actions[h] = actions[(((size_t)sweep * envs + i) * 2 + a) * RF_HEADS + h];
            memset(v.rewards, 0, v.agents * sizeof(float)); memset(v.terminals, 0, v.agents * sizeof(float));
            double t0 = harness_now_ns();
            for (int i = 0; i < v.count; i++) puf_step(&v.envs[i]);
            ns += harness_now_ns() - t0;
            steps += v.count;
        }
        harness_vec_destroy(&v);
    }
    printf("{\"replay_ns_per_env_step\":%.1f,\"env_steps_per_s\":%.0f}\n", ns / steps, 1e9 * steps / ns);
    free(actions);
}

int main(int argc, char** argv) {
    int sweeps = argc > 1 ? atoi(argv[1]) : 20000;
    int envs = argc > 2 ? atoi(argv[2]) : 64;
    HarnessPolicy policy = POLICY_MIXED;
    if (argc > 3) policy = !strcmp(argv[3], "random") ? POLICY_RANDOM : !strcmp(argv[3], "script") ? POLICY_SCRIPT : POLICY_MIXED;
    int threads = argc > 4 ? atoi(argv[4]) : 1;
    if (argc > 5 && !strcmp(argv[5], "stages")) { stages(sweeps, envs, policy); return 0; }
    if (argc > 5 && !strcmp(argv[5], "replay")) { replay(sweeps, envs, policy, argc > 6 ? atoi(argv[6]) : 5); return 0; }
    assert(envs <= 4096 && threads >= 1 && threads <= 32);
    Job jobs[32] = {0};
    pthread_t pt[32];
    for (int t = 0; t < threads; t++) {
        jobs[t] = (Job){.sweeps = sweeps, .envs = envs, .thread = t, .policy = policy};
        pthread_create(&pt[t], NULL, run, &jobs[t]);
    }
    for (int t = 0; t < threads; t++) pthread_join(pt[t], NULL);
    double sweep_ns = 0, fight_ns = 0, wait_ns = 0;
    long fight_calls = 0, wait_calls = 0, sweep_steps = 0;
    for (int t = 0; t < threads; t++) {
        sweep_ns += jobs[t].sweep_ns; fight_ns += jobs[t].fight_ns; wait_ns += jobs[t].wait_ns;
        fight_calls += jobs[t].fight_calls; wait_calls += jobs[t].wait_calls; sweep_steps += jobs[t].sweep_env_steps;
    }
    long calls = fight_calls + wait_calls;
    static const char* names[5] = {"wait", "wait_end_new_fight", "fight", "fight_end", "session_reset"};
    double kns[5] = {0}; long kc[5] = {0};
    for (int t = 0; t < threads; t++) for (int k = 0; k < 5; k++) { kns[k] += jobs[t].kind_ns[k]; kc[k] += jobs[t].kind_calls[k]; }
    for (int k = 0; k < 5; k++) printf("  %-20s calls %9ld (%.2f%%) avg %9.1f ns\n", names[k], kc[k], 100.0 * kc[k] / calls, kc[k] ? kns[k] / kc[k] : 0);
    /* sweep_ns is the summed busy time of the threads, so 1e9 / (sweep_ns / sweep_steps) is env steps per second per thread */
    printf("{\"threads\":%d,\"envs_per_thread\":%d,\"sizeof_env\":%zu,\"policy\":\"%s\",\"idle_share\":%.3f,"
        "\"fight_ns_per_env_step\":%.1f,\"idle_ns_per_env_step\":%.1f,\"mixed_ns_per_env_step\":%.1f,"
        "\"env_steps_per_s_per_thread\":%.0f,\"agent_steps_per_s_per_thread\":%.0f}\n",
        threads, envs, sizeof(Env), policy == POLICY_RANDOM ? "random" : policy == POLICY_SCRIPT ? "script" : "mixed", (double)wait_calls / calls,
        fight_calls ? fight_ns / fight_calls : 0, wait_calls ? wait_ns / wait_calls : 0, sweep_ns / sweep_steps,
        1e9 * sweep_steps / sweep_ns, 2e9 * sweep_steps / sweep_ns);
    return 0;
}
