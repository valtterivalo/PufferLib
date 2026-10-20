#include "../../osrs_riskfight/osrs_riskfight.h"

typedef struct {
    Env env;
    float observations[2][RF_OBS_SIZE], actions[2][RF_HEADS], rewards[2], terminals[2], resets[2];
    unsigned char masks[2][RF_MASK_SIZE];
} Fixture;

static Fixture* fixture(Dict* config, int frozen) {
    Fixture* f = (Fixture*)calloc(1, sizeof(*f));
    f->env.rng = 123;
    puf_init(&f->env, config);
    f->env.tag = frozen;
    for (int i = 0; i < 2; i++) f->env.agents[i] = (Agent){
        .observations = f->observations[i], .actions = f->actions[i],
        .rewards = &f->rewards[i], .terminals = &f->terminals[i], .resets = &f->resets[i],
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

static const RiskfightProfile STEADY = {{1000, 1000, 1000, 1000, 1000}, 0, 0, {0}, 0, 1000, {{0}}};

int main(void) {
    DictItem items[] = {
        {.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1},
        {.key = "session_ticks", .value = 100000}, {.key = "curveball_probability", .value = 0},
        {.key = "scripted_probability", .value = 1},
        {.key = "scripted_omission_initial", .value = 1},
        {.key = "scripted_omission_decay_ticks", .value = 10},
        {.key = "midfight_probability", .value = 0},
        {.key = "midfight_max_ticks", .value = 64},
    };
    Dict config = {.items = items, .size = sizeof(items) / sizeof(*items)};
    Dict baseline = {.items = items, .size = 4};
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
    ptrdiff_t profile = teacher->env.state.opponent_profile - RISKFIGHT_HUMAN_PROFILES;
    assert(profile >= 0 && profile < RISKFIGHT_HUMAN_TRAIN_PROFILES);
    assert(teacher->env.context.opp_driven);
    int commands[2 * RF_HEADS] = {0};
    teacher->env.training.ticks = 0;
    assert(riskfight_training_omit(&teacher->env.training) == 1); /* the omission starts at 1: he does nothing */
    teacher->env.training.ticks = 5;
    assert(riskfight_training_omission(&teacher->env.training) == 0.5f);
    teacher->env.training.ticks = 10;
    assert(riskfight_training_omit(&teacher->env.training) == 0);
    teacher->actions[1][RF_PRIMARY] = RF_TELEPORT;
    puf_step(&teacher->env);
    assert(teacher->terminals[0] == 0 && teacher->env.training.ticks == 11);
    assert(teacher->env.log.n == 0 && teacher->env.log.scripted_ticks == 0);
    teacher->actions[0][RF_PRIMARY] = RF_TELEPORT;
    puf_step(&teacher->env);
    assert(teacher->terminals[0] == 0 && teacher->env.log.escapes == 1 && teacher->env.log.scripted_ticks == 2);
    destroy(teacher);

    Fixture* generated = fixture(&baseline, 1);
    Fixture* replay = fixture(&baseline, 1);
    RiskfightTrainingStart start = riskfight_training_prefix(&generated->env.state,
        &generated->env.context, &STEADY, &STEADY, 64, 1);
    assert(start == RF_START_MIDFIGHT);
    /* Both fixture kits are Dharok kits: the prefix plays them by the Dharok rules, seat 1 as the persona that steps up, seat 0 as one drawn from the roll. */
    RiskfightProfile dharok[2] = {STEADY, STEADY};
    for (int i = 0; i < 2; i++) {
        assert(riskfight_player_dharok_pieces(&replay->env.state.env.players[i]) >= RF_DHAROK_KIT_PIECES);
        dharok[i].dharok_eating = 1;
    }
    dharok[0].dharok_persona = riskfight_dharok_persona_draw(1);
    dharok[1].dharok_persona = replay->env.state.opponent_persona;
    for (int tick = 0; tick < 64; tick++) {
        for (int i = 0; i < 2; i++) {
            float obs[RF_OBS_SIZE];
            riskfight_write_script_observation(&replay->env.state, i, obs);
            riskfight_script(obs, &dharok[i], replay->env.state.script_seed[i], replay->env.state.consume_ticks[i],
                commands + i * RF_HEADS);
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
        &STEADY, &STEADY, 1, 1);
    assert(start == RF_START_PREFIX_TERMINAL);
    assert(generated->env.state.env.tick == 0 && !generated->env.state.env.episode_over);
    assert(generated->env.state.env.players[0].current_hitpoints == 121);
    destroy(generated);
    destroy(replay);

    items[4].value = 0;
    items[7].value = 1;
    Fixture* first = fixture(&config, 1);
    Fixture* second = fixture(&config, 1);
    assert(first->env.training.start_tick > 0);
    assert(first->env.training.ticks == 0);
    for (int episode = 0; episode < 25; episode++) {
        same_observations(&first->env.state, &second->env.state);
        for (int i = 0; i < 2; i++) {
            unsigned char mask[RF_MASK_SIZE];
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
