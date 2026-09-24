#include "../../osrs_riskfight/osrs_riskfight.h"

typedef struct {
    Env env;
    float observations[2][RF_OBS_SIZE], actions[2][RF_HEADS], rewards[2], terminals[2];
    unsigned char masks[2][RF_MASK_SIZE];
} Fixture;

static Fixture* fixture(Dict* config, int frozen) {
    Fixture* f = (Fixture*)calloc(1, sizeof(*f));
    f->env.rng = 123;
    puf_init(&f->env, config);
    f->env.tag = frozen;
    for (int i = 0; i < 2; i++) f->env.agents[i] = (Agent){
        .observations = f->observations[i], .actions = f->actions[i],
        .rewards = &f->rewards[i], .terminals = &f->terminals[i],
        .action_mask = f->masks[i], .policy = i};
    puf_reset(&f->env);
    return f;
}

static void destroy(Fixture* f) {
    puf_close(&f->env);
    free(f);
}

static void same_observations(const RiskfightState* a, const RiskfightState* b) {
    for (int i = 0; i < 2; i++) {
        float first[RF_OBS_SIZE], second[RF_OBS_SIZE];
        riskfight_write_observation(a, i, first);
        riskfight_write_observation(b, i, second);
        assert(memcmp(first, second, sizeof(first)) == 0);
    }
    assert(a->env.rng_state == b->env.rng_state);
    assert(a->env.tick == b->env.tick);
}

int main(void) {
    DictItem items[] = {
        {.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1},
        {.key = "scripted_probability", .value = 1},
        {.key = "scripted_omission_initial", .value = 1},
        {.key = "scripted_omission_decay_ticks", .value = 10},
        {.key = "midfight_probability", .value = 0},
        {.key = "midfight_max_ticks", .value = 64},
    };
    Dict config = {.items = items, .size = 7};
    Dict baseline = {.items = items, .size = 2};
    Fixture* plain = fixture(&baseline, 0);
    Fixture* protected_learner = fixture(&config, 0);
    assert(protected_learner->env.training.opponent == RF_TRAIN_LEARNED);
    for (int tick = 0; tick < 50; tick++) {
        for (int i = 0; i < 2; i++) {
            plain->actions[i][RF_PRIMARY] = RF_ATTACK;
            protected_learner->actions[i][RF_PRIMARY] = RF_ATTACK;
        }
        puf_step(&plain->env);
        puf_step(&protected_learner->env);
        same_observations(&plain->env.state, &protected_learner->env.state);
        assert(memcmp(plain->rewards, protected_learner->rewards, sizeof(plain->rewards)) == 0);
    }
    assert(protected_learner->env.log.scripted_ticks == 0);
    destroy(plain);
    destroy(protected_learner);

    Fixture* teacher = fixture(&config, 1);
    assert(teacher->env.training.opponent == RF_TRAIN_SCRIPTED);
    assert(teacher->env.training.script != RISKFIGHT_HELDOUT);
    int commands[2 * RF_HEADS] = {0};
    commands[RF_PRIMARY] = RF_STOP;
    commands[RF_HEADS + RF_PRIMARY] = RF_TELEPORT;
    assert(riskfight_training_actions(&teacher->env.training, &teacher->env.state, 1, commands) == 1);
    assert(commands[RF_PRIMARY] == RF_STOP);
    for (int i = RF_HEADS; i < 2 * RF_HEADS; i++) assert(commands[i] == 0);
    teacher->env.training.ticks = 5;
    assert(riskfight_training_omission(&teacher->env.training) == 0.5f);
    teacher->env.training.ticks = 10;
    int expected[RF_HEADS];
    riskfight_script(teacher->observations[1], teacher->env.training.script,
        teacher->env.state.script_seed[1], expected);
    assert(riskfight_training_actions(&teacher->env.training, &teacher->env.state, 1, commands) == 0);
    assert(memcmp(commands + RF_HEADS, expected, sizeof(expected)) == 0);
    assert(commands[RF_PRIMARY] == RF_STOP);
    teacher->actions[1][RF_PRIMARY] = RF_TELEPORT;
    puf_step(&teacher->env);
    assert(teacher->terminals[0] == 0 && teacher->env.training.ticks == 11);
    assert(teacher->env.log.n == 0 && teacher->env.log.scripted_ticks == 0);
    teacher->actions[0][RF_PRIMARY] = RF_TELEPORT;
    puf_step(&teacher->env);
    assert(teacher->terminals[0] == 1 && teacher->env.log.scripted_ticks == 2);
    destroy(teacher);

    Fixture* generated = fixture(&baseline, 1);
    Fixture* replay = fixture(&baseline, 1);
    RiskfightTrainingStart start = riskfight_training_prefix(&generated->env.state,
        &generated->env.context, RISKFIGHT_CAUTIOUS, RISKFIGHT_CAUTIOUS, 64);
    assert(start == RF_START_MIDFIGHT);
    for (int tick = 0; tick < 64; tick++) {
        for (int i = 0; i < 2; i++) {
            float obs[RF_OBS_SIZE];
            riskfight_write_observation(&replay->env.state, i, obs);
            riskfight_script(obs, RISKFIGHT_CAUTIOUS, 1, commands + i * RF_HEADS);
        }
        riskfight_step((EncounterState*)&replay->env.state,
            (EncounterContext*)&replay->env.context, commands);
    }
    same_observations(&generated->env.state, &replay->env.state);
    assert(generated->env.state.env.tick == 64);
    for (int i = 0; i < 2; i++) {
        assert(generated->env.state.episode_returns[i] == 0);
        assert(generated->env.state.direct_ko_chance_mass[i] == 0);
        assert(generated->env.state.visible[i].last_attack_tick >= 0);
    }
    generated->env.state.env.players[0].current_hitpoints = 0;
    start = riskfight_training_prefix(&generated->env.state, &generated->env.context,
        RISKFIGHT_CAUTIOUS, RISKFIGHT_CAUTIOUS, 1);
    assert(start == RF_START_PREFIX_TERMINAL);
    assert(generated->env.state.env.tick == 0 && !generated->env.state.env.episode_over);
    assert(generated->env.state.env.players[0].current_hitpoints == 121);
    destroy(generated);
    destroy(replay);

    items[2].value = 0;
    items[5].value = 1;
    Fixture* first = fixture(&config, 1);
    Fixture* second = fixture(&config, 1);
    assert(first->env.training.start_tick > 0);
    assert(first->env.training.ticks == 0);
    for (int episode = 0; episode < 25; episode++) {
        same_observations(&first->env.state, &second->env.state);
        for (int i = 0; i < 2; i++) {
            float mask[RF_MASK_SIZE];
            riskfight_write_action_mask(&first->env.state, i, mask);
            for (int j = 0; j < RF_MASK_SIZE; j++) assert(first->masks[i][j] == mask[j]);
            assert(first->rewards[i] == 0 && first->terminals[i] == 0);
        }
        puf_reset(&first->env);
        puf_reset(&second->env);
    }
    destroy(first);
    destroy(second);
    puts("Riskfight training isolation, omission, legal prefixes and replay passed");
}
