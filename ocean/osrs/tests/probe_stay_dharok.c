/* At each teleport the net decides in a Dharok kit, what staying would have been worth with the spectated Dharok play: branch A leaves (0), branch B stays and
 * plays the rest of the fight as the Dharok script (riskfight_script with dharok_eating and a drawn persona: spectated meals, greataxe, orb and escapes), the
 * opponent as the env plays him. B's value is kill - price x death; each decision is cloned `rolls` times with the env's and both scripts' streams reseeded.
 *
 *   cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o probe_stay_dharok ocean/osrs/tests/probe_stay_dharok.c riskfight_oppfit_bridge.o -lm -lpthread
 *   RF_OPPFIT_MODEL=... RF_OPPFIT_EAT=... probe_stay_dharok CKPT HIDDEN LAYERS SESSIONS SEED ROLLS
 *
 * Prints `ROW session hp_band bar_band value kill death` per decision (our hitpoints <40 / 40-59 / 60-79 / 80+; his bar <12 / 12-23 / 24+ or none), the
 * value, kill and death averaged over the rolls; pool them by session. The env is session_probe's Dharok recipe without DR (live_loadout 5, OppFit 0.5,
 * recorded starts, live prices). B steps the state directly (riskfight_step), as probe_script_duels does, so the script's clicks skip the env's hand. */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include "rf_net.h"

static float obs[RF_OBS_SIZE], actions[RF_HEADS], reward, terminal, reset_flag;
static unsigned char mask[RF_MASK_SIZE];

int main(int argc, char** argv) {
    if (argc != 7) { fprintf(stderr, "usage: probe_stay_dharok CKPT HIDDEN LAYERS SESSIONS SEED ROLLS\n"); return 2; }
    const int sessions = atoi(argv[4]), rolls = atoi(argv[6]);
    const unsigned seed = (unsigned)atoi(argv[5]);
    assert(rolls > 0);
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = 3000},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = 0}, {.key = "price_table", .value = 1}, {.key = "live_loadout", .value = 5},
        {.key = "deployable", .value = 1}, {.key = "motor", .value = 1}, {.key = "domain_randomization", .value = 0}, {.key = "oppfit_probability", .value = 0.5},
        {.key = "opponent_pool", .value = 1}, {.key = "fight_starts", .value = 1}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    Env* env = calloc(1, sizeof(*env));
    env->rng = seed;
    puf_init(env, &kwargs);
    RfNet* net = rf_net_load(argv[1], RF_OBS_SIZE, atoi(argv[2]), atoi(argv[3]), RF_ACTION_DIMS, RF_HEADS);
    env->agents[0] = (Agent){.observations = obs, .actions = actions, .rewards = &reward, .terminals = &terminal, .resets = &reset_flag, .action_mask = mask, .policy = 0};
    uint32_t rng = 0x9e3779b9u ^ seed;
    puf_reset(env);
    rf_net_reset(net);
    static RiskfightState branch;
    static RiskfightContext branch_context;
    long decisions = 0;
    while (env->log.n < sessions) {
        int greedy[RF_HEADS];
        rf_net_act(net, obs, mask, &rng, 1, RF_ACTION_DIMS, RF_HEADS, greedy);
        const RiskfightState* s = &env->state;
        const Player* us = &s->env.players[0];
        if (env->wait_left == 0 && !s->env.episode_over && greedy[RF_PRIMARY] == RF_TELEPORT && riskfight_player_dharok_pieces(us) >= RF_DHAROK_KIT_PIECES) {
            const int hp = us->current_hitpoints, bar = s->visible[0].health_bar;
            double value = 0, kill = 0, death = 0;
            /* The clone shares the command queues' heap buffers: grow them first so no branch reallocates a buffer the env still points at. */
            for (int seat = 0; seat < 2; seat++) human_command_queue_reserve(&env->context.policy_commands[seat].commands, 256);
            for (int r = 0; r < rolls; r++) {
                memcpy(&branch, s, sizeof(branch));
                memcpy(&branch_context, &env->context, sizeof(branch_context));
                const uint32_t k = (uint32_t)(r + 1);
                branch.env.rng_state = osrs_lowbias32(s->env.rng_state ^ (0x5bd1e995u * k));
                branch.script_seed[0] = osrs_lowbias32(s->script_seed[0] ^ (0x27d4eb2fu * k));
                branch.script_seed[1] = osrs_lowbias32(s->script_seed[1] ^ (0x165667b1u * k));
                RiskfightProfile profile = *riskfight_pick_profile(RISKFIGHT_HUMAN, osrs_lowbias32(rng + k * 7919u));
                profile.dharok_eating = 1;
                profile.dharok_persona = riskfight_dharok_persona_draw(osrs_lowbias32(rng ^ (k * 104729u)));
                for (int t = 0; !branch.env.episode_over; t++) {
                    assert(t < 5000);
                    int commands[2 * RF_HEADS] = {0};
                    float script_obs[RF_OBS_SIZE];
                    riskfight_write_script_observation(&branch, 0, script_obs);
                    riskfight_script(script_obs, &profile, branch.script_seed[0], branch.consume_ticks[0], commands);
                    riskfight_step((EncounterState*)&branch, (EncounterContext*)&branch_context, commands);
                }
                const RiskfightOutcome out = branch.outcome[0];
                value += riskfight_outcome_reward(out, branch.death_price);
                kill += out == RISKFIGHT_KILL || out == RISKFIGHT_MUTUAL_DEATH;
                death += out == RISKFIGHT_DEATH || out == RISKFIGHT_MUTUAL_DEATH;
            }
            printf("ROW %d %d %d %.4f %.4f %.4f\n", (int)env->log.n, (hp >= 40) + (hp >= 60) + (hp >= 80), bar < 0 ? 2 : (bar >= 12) + (bar >= 24),
                value / rolls, kill / rolls, death / rolls);
            decisions++;
        }
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)greedy[h];
        puf_step(env);
        if (terminal || reset_flag) rf_net_reset(net);
    }
    fprintf(stderr, "%s, %d sessions, seed %u, %d rolls: %ld teleport decisions in a Dharok kit\n", argv[1], sessions, seed, rolls, decisions);
    puf_close(env);
    return 0;
}
