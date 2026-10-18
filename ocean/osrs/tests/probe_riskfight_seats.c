/* Seat symmetry of pure self-play: one policy on both seats of tag-0 self-play envs (both seats in our kits, price 1), teleports taken out of both seats'
 * actions, kills by seat 0 and by seat 1 counted over many fights. In a fair mirror they are equal within noise.
 *
 *   probe_riskfight_seats [fights] [policy 0 script / 1 uniform random over the mask] [key=value env kwargs ...]
 * RANDOM_HEADS=<bit mask of RF_ heads>: those heads random, the rest the script's; SEAT1_FIRST: seat 1 decides first. Prints the kills, seat 1's PID share and
 * each seat's share of fights in the Dharok kit. Build as the suite builds riskfight_env_harness.h users (cc -DTRACE_FLOAT ... riskfight_oppfit_bridge.o). */
#include "riskfight_env_harness.h"
#include <stdio.h>

static uint64_t splitmix64(uint64_t* x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

int main(int argc, char** argv) {
    const int fights = argc > 1 ? atoi(argv[1]) : 20000;
    const int random_policy = argc > 2 ? atoi(argv[2]) : 0;
    DictItem items[40] = {
        {.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1}, {.key = "live_loadout", .value = 4}, {.key = "opponent_pool", .value = 1},
        {.key = "selfplay_pool_probability", .value = 0}, {.key = "deployable", .value = 1}, {.key = "motor", .value = 1}, {.key = "death_price", .value = 0},
        {.key = "oppfit_probability", .value = 0}, {.key = "domain_randomization", .value = 1}, {.key = "scripted_probability", .value = 0},
        {.key = "fight_starts", .value = 1}, {.key = "session_ticks", .value = 3000}, {.key = "curveball_probability", .value = 0.1},
        {.key = "data_escapes", .value = 0}};
    int count = 15;
    for (int a = 3; a < argc; a++) {
        char* eq = strchr(argv[a], '=');
        assert(eq);
        *eq = 0;
        int replaced = 0;
        for (int j = 0; j < count; j++)
            if (!strcmp(items[j].key, argv[a])) { items[j].value = atof(eq + 1); replaced = 1; }
        if (!replaced) { assert(count < 40); items[count].value = atof(eq + 1); snprintf(items[count].key, PUF_DICT_MAX_KEY, "%s", argv[a]); count++; }
    }
    Dict kwargs = {.items = items, .size = (size_t)count};
    enum { ENVS = 64 };
    static Env envs[ENVS];
    static float obs[ENVS][2][RF_OBS_SIZE], act[ENVS][2][RF_HEADS], rew[ENVS][2], term[ENVS][2], res[ENVS][2];
    static unsigned char mask[ENVS][2][RF_MASK_SIZE];
    static uint64_t rng[ENVS][2]; /* splitmix64, one stream per seat: the seats' draws are independent */
    for (int i = 0; i < ENVS; i++) for (int s = 0; s < 2; s++) rng[i][s] = 0x9E3779B97F4A7C15ull * (uint64_t)(2 * i + s + 1);
    for (int i = 0; i < ENVS; i++) {
        envs[i].rng = 1000u + (unsigned)i;
        puf_init(&envs[i], &kwargs);
        envs[i].tag = 0;
        for (int s = 0; s < 2; s++) envs[i].agents[s] = (Agent){.observations = (obs_t*)obs[i][s], .actions = act[i][s], .rewards = &rew[i][s],
            .terminals = &term[i][s], .resets = &res[i][s], .action_mask = mask[i][s], .policy = 0};
        puf_reset(&envs[i]);
    }
    double done = 0, pid_ticks = 0, pid1 = 0, dharok_fights[2] = {0};
    const long random_heads = random_policy ? (1L << RF_HEADS) - 1 : getenv("RANDOM_HEADS") ? strtol(getenv("RANDOM_HEADS"), NULL, 0) : 0;
    while (done < fights) {
        done = 0;
        for (int i = 0; i < ENVS; i++) {
            Env* e = &envs[i];
            if (e->wait_left == 0)
                for (int seat = 0; seat < 2; seat++) {
                    const int s = getenv("SEAT1_FIRST") ? 1 - seat : seat;
                    /* the script, its teleport taken out and its choices the mask forbids (prayers, styles, gear the plugin keeps) dropped as the mask would;
                     * the heads of RANDOM_HEADS (a bit mask of RF_ heads; all with policy 1) drawn uniformly among the entries the mask allows instead */
                    const unsigned char* m = e->agents[s].action_mask;
                    float o[RF_OBS_SIZE];
                    int a[RF_HEADS];
                    riskfight_write_observation(&e->state, s, o);
                    riskfight_script(o, &RISKFIGHT_HUMAN_PROFILES[i % RISKFIGHT_HUMAN_TRAIN_PROFILES], e->state.script_seed[s], e->state.consume_ticks[s], a);
                    if (a[RF_PRIMARY] == RF_TELEPORT) a[RF_PRIMARY] = RF_ATTACK;
                    for (int h = 0; h < RF_HEADS; h++) act[i][s][h] = m[RF_HEAD_OFFSET[h] + a[h]] ? (float)a[h] : 0.0f;
                    for (int h = 0; h < RF_HEADS; h++) {
                        if (!((random_heads >> h) & 1)) continue;
                        const unsigned char* hm = m + RF_HEAD_OFFSET[h];
                        int allowed = 0;
                        for (int x = 0; x < RF_ACTION_DIMS[h]; x++) allowed += hm[x] && !(h == RF_PRIMARY && x == RF_TELEPORT);
                        int pick = allowed > 1 ? (int)(splitmix64(&rng[i][s]) % (uint64_t)allowed) : 0, chosen = 0;
                        for (int x = 0; x < RF_ACTION_DIMS[h]; x++) if (hm[x] && !(h == RF_PRIMARY && x == RF_TELEPORT) && pick-- == 0) { chosen = x; break; }
                        act[i][s][h] = (float)chosen;
                    }
                }
            if (e->wait_left == 0) { pid_ticks++; pid1 += e->state.env.pid_holder; }
            const int dharok[2] = {e->context.loadout[0] == &RISKFIGHT_DHAROK_LOADOUT, e->context.loadout[1] == &RISKFIGHT_DHAROK_LOADOUT};
            const float before = e->log.fights_selfplay;
            puf_step(e);
            if (e->log.fights_selfplay > before) for (int s = 0; s < 2; s++) dharok_fights[s] += dharok[s];
            done += e->log.fights_selfplay;
        }
    }
    double n = 0, k[2] = {0}, t[2] = {0}, mutual = 0, timeouts = 0;
    for (int i = 0; i < ENVS; i++) {
        n += envs[i].log.fights_selfplay;
        for (int s = 0; s < 2; s++) { k[s] += envs[i].log.sp_kills[s]; t[s] += envs[i].log.sp_teleports[s]; }
        mutual += envs[i].log.mutual_deaths;
        timeouts += envs[i].log.timeouts;
    }
    double sigma = sqrt(k[0] + k[1]);
    printf("fights %.0f kills seat0 %.0f seat1 %.0f (diff %.1f sigma) teleports %.0f %.0f mutual %.0f timeouts %.0f pid1 %.4f dharok kit %.3f %.3f\n", n, k[0], k[1],
        (k[0] - k[1]) / (sigma + 1e-9), t[0], t[1], mutual, timeouts, pid1 / pid_ticks, dharok_fights[0] / n, dharok_fights[1] / n);
    return 0;
}
