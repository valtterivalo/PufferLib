/* What a Dharok strip buys a trained net: at every step where the net (argmax) takes Dharok body or legs off, the env and the net are copied and the fight is
 * played on twice from the same state and random draws: A as chosen, B with the strip removed and no strip offered for the rest of the fight. Prints, per
 * branch, the next 10 steps' his swings, damage each way, his meals and teleports, our deaths, and the return to the fight's end; and the observation
 * floats that differ most between the branches one step after. With oppfit_probability > 0 (RF_OPPFIT_MODEL, RF_OPPFIT_EAT set) OppFit's history is copied too.
 *
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o strip_probe ocean/osrs/tests/strip_probe.c riskfight_oppfit_bridge.o -lm -lpthread
 *   strip_probe CHECKPOINT HIDDEN LAYERS SESSIONS [seed] [oppfit_probability] */
#include <malloc/malloc.h>
#include "../../osrs_riskfight/osrs_riskfight.h"
#include "rf_net.h"

enum { HORIZON = 10, MAX_FIGHT = 400, NB = 2 };
typedef struct { double his_swings, dmg_taken, dmg_dealt, his_eats, his_tp, our_tp, deaths, kills, ret10, ret, veng_procs, his_hits; } Branch;

static float obs[RF_OBS_SIZE], actions[RF_HEADS], reward, terminal, reset_flag;
static unsigned char mask[RF_MASK_SIZE];

static void forbid_strip(void) {
    mask[RF_HEAD_OFFSET[RF_BODY] + RF_UNEQUIP] = 0;
    mask[RF_HEAD_OFFSET[RF_LEGS] + RF_UNEQUIP] = 0;
}

/* Plays from the current state to the end of the fight; the first step takes `first`. */
static void play(Env* env, RfNet* net, uint32_t* rng, const int* first, int no_strip, Branch* b, float* obs_after) {
    const float n0 = env->log.n, rounds0 = env->log.rounds, otp0 = env->log.opponent_teleports, stp0 = env->log.self_teleports, d0 = env->log.deaths + env->log.mutual_deaths, k0 = env->log.kills;
    for (int t = 0; t < MAX_FIGHT; t++) {
        int chosen[RF_HEADS];
        if (t == 0) memcpy(chosen, first, sizeof(chosen));
        else {
            if (no_strip) forbid_strip();
            rf_net_act(net, obs, mask, rng, 1, RF_ACTION_DIMS, RF_HEADS, chosen);
        }
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)chosen[h];
        const Player *us = &env->state.env.players[0], *him = &env->state.env.players[1];
        const int our_hp = us->current_hitpoints, his_hp = him->current_hitpoints, his_veng = him->veng_active;
        puf_step(env);
        const int over = env->log.rounds != rounds0 || env->log.n != n0 || terminal || reset_flag;
        if (t == 0) memcpy(obs_after, obs, sizeof(obs));
        b->ret += reward;
        if (t < HORIZON) {
            b->ret10 += reward;
            if (!over) {
                const RiskfightState* st = &env->state;
                b->his_swings += st->visible[0].last_attack_tick == st->env.tick - 1;
                b->dmg_taken += our_hp > us->current_hitpoints ? our_hp - us->current_hitpoints : 0;
                b->dmg_dealt += his_hp > him->current_hitpoints ? his_hp - him->current_hitpoints : 0;
                b->his_eats += him->current_hitpoints > his_hp;
                b->veng_procs += his_veng && !him->veng_active;
            }
        }
        if (over) break;
    }
    b->his_tp += env->log.opponent_teleports - otp0;
    b->our_tp += env->log.self_teleports - stp0;
    b->deaths += env->log.deaths + env->log.mutual_deaths - d0;
    b->kills += env->log.kills - k0;
}

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: strip_probe CHECKPOINT HIDDEN LAYERS SESSIONS [seed] [oppfit_probability]\n"); return 2; }
    const int sessions = atoi(argv[4]);
    const unsigned seed = argc > 5 ? (unsigned)atoi(argv[5]) : 1;
    const double oppfit = argc > 6 ? atof(argv[6]) : 0;
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = 3000},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = 0}, {.key = "live_loadout", .value = 4}, {.key = "deployable", .value = 1},
        {.key = "motor", .value = 1}, {.key = "domain_randomization", .value = 1}, {.key = "oppfit_probability", .value = oppfit}, {.key = "opponent_pool", .value = 1},
        {.key = "fight_starts", .value = 1}, {.key = "price_table", .value = 1}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    Env* env = calloc(1, sizeof(*env));
    Env* saved = malloc(sizeof(*env));
    env->rng = seed;
    puf_init(env, &kwargs);
    /* OppFit's bridge (plain data, its own translation unit) is copied whole: its allocation's size bounds the struct. */
    const size_t bridge_bytes = env->oppfit ? malloc_size(env->oppfit) : 0;
    void* bridge_saved = bridge_bytes ? malloc(bridge_bytes) : NULL;
    RfNet* net = rf_net_load(argv[1], RF_OBS_SIZE, atoi(argv[2]), atoi(argv[3]), RF_ACTION_DIMS, RF_HEADS);
    RfNet net_saved;
    env->agents[0] = (Agent){.observations = obs, .actions = actions, .rewards = &reward, .terminals = &terminal, .resets = &reset_flag, .action_mask = mask, .policy = 0};
    uint32_t rng = 0x9e3779b9u ^ seed;
    puf_reset(env);
    /* The env is copied by bytes: its only heap buffers, the command queues, get a capacity no step outgrows, so no branch reallocates them. */
    for (int seat = 0; seat < 2; seat++) human_command_queue_reserve(&env->context.policy_commands[seat].commands, 1024);
    rf_net_reset(net);
    static float obs_saved[RF_OBS_SIZE], after[NB][RF_OBS_SIZE];
    static unsigned char mask_saved[RF_MASK_SIZE];
    static double obs_diff[RF_OBS_SIZE];
    Branch br[NB] = {0};
    long strips = 0, strip_veng = 0, strip_his_veng = 0, dharok_steps = 0, his_dharok = 0, strip_oppfit = 0;
    double strip_hp = 0, diff_sum = 0, diff_sq = 0;
    while (env->log.n < sessions) {
        int chosen[RF_HEADS];
        rf_net_act(net, obs, mask, &rng, 1, RF_ACTION_DIMS, RF_HEADS, chosen);
        const int dharok = env->context.loadout[0] == &RISKFIGHT_DHAROK_LOADOUT && env->wait_left == 0;
        dharok_steps += dharok;
        if (dharok && (chosen[RF_BODY] == RF_UNEQUIP || chosen[RF_LEGS] == RF_UNEQUIP)) {
            strips++;
            const Player* us = &env->state.env.players[0];
            strip_hp += us->current_hitpoints;
            strip_veng += us->veng_active;
            strip_his_veng += env->state.env.players[1].veng_active;
            his_dharok += riskfight_dharok_opponent(&env->state);
            strip_oppfit += env->oppfit_active;
            memcpy(saved, env, sizeof(*env));
            if (bridge_bytes) memcpy(bridge_saved, env->oppfit, bridge_bytes);
            net_saved = *net;
            memcpy(obs_saved, obs, sizeof(obs));
            memcpy(mask_saved, mask, sizeof(mask));
            const float r0 = reward, te0 = terminal, re0 = reset_flag;
            const double ret_a = br[0].ret, ret_b = br[1].ret;
            for (int b = 0; b < NB; b++) {
                memcpy(env, saved, sizeof(*env));
                if (bridge_bytes) memcpy(env->oppfit, bridge_saved, bridge_bytes);
                *net = net_saved;
                memcpy(obs, obs_saved, sizeof(obs));
                memcpy(mask, mask_saved, sizeof(mask));
                reward = r0; terminal = te0; reset_flag = re0;
                uint32_t branch_rng = rng;
                int first[RF_HEADS];
                memcpy(first, chosen, sizeof(first));
                if (b == 1) {
                    if (first[RF_BODY] == RF_UNEQUIP) first[RF_BODY] = 0;
                    if (first[RF_LEGS] == RF_UNEQUIP) first[RF_LEGS] = 0;
                }
                play(env, net, &branch_rng, first, b == 1, &br[b], after[b]);
            }
            for (int i = 0; i < RF_OBS_SIZE; i++) obs_diff[i] += fabsf(after[0][i] - after[1][i]);
            const double d = (br[0].ret - ret_a) - (br[1].ret - ret_b);
            diff_sum += d; diff_sq += d * d;
            /* The session goes on from the strip state with the net's own action. */
            memcpy(env, saved, sizeof(*env));
            if (bridge_bytes) memcpy(env->oppfit, bridge_saved, bridge_bytes);
            *net = net_saved;
            memcpy(obs, obs_saved, sizeof(obs));
            memcpy(mask, mask_saved, sizeof(mask));
            reward = r0; terminal = te0; reset_flag = re0;
        }
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)chosen[h];
        puf_step(env);
        if (terminal || reset_flag) rf_net_reset(net);
    }
    const double n = strips > 0 ? strips : 1;
    printf("sessions %d, Dharok fight steps %ld, strips %ld; at the strip: our hp %.1f, our veng up %.2f, his veng up %.2f, Dharok opponent %.2f, OppFit %.2f\n",
        sessions, dharok_steps, strips, strip_hp / n, strip_veng / n, strip_his_veng / n, his_dharok / n, strip_oppfit / n);
    const char* names[NB] = {"A strip", "B none "};
    printf("branch   | next %d steps: his_swings dmg_taken dmg_dealt his_eats veng_procs(his) ret | to fight end: ret his_tp our_tp deaths kills\n", HORIZON);
    for (int b = 0; b < NB; b++)
        printf("%s  | %.3f %.3f %.3f %.3f %.3f %.4f | %.4f %.3f %.3f %.3f %.3f\n", names[b], br[b].his_swings / n, br[b].dmg_taken / n, br[b].dmg_dealt / n,
            br[b].his_eats / n, br[b].veng_procs / n, br[b].ret10 / n, br[b].ret / n, br[b].his_tp / n, br[b].our_tp / n, br[b].deaths / n, br[b].kills / n);
    printf("return A - B to the fight's end: %.4f +- %.4f (s.e.)\n", diff_sum / n, sqrt((diff_sq / n - (diff_sum / n) * (diff_sum / n)) / n));
    printf("obs floats differing most one step after (mean |A-B|):");
    for (int k = 0; k < 15; k++) {
        int best = 0;
        for (int i = 1; i < RF_OBS_SIZE; i++) if (obs_diff[i] > obs_diff[best]) best = i;
        if (obs_diff[best] == 0) break;
        printf(" [%d] %.3f", best, obs_diff[best] / n);
        obs_diff[best] = -1;
    }
    printf("\n");
    return 0;
}
