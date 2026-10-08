#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main

static void request(FILE* stream, int id, const char* history, int low, int high) {
    fprintf(stream, "LA %d 100\nO 99 90 118 118 118 99 100 0 0 0 0 27 0 -1 0 0 0 1 0 0 4 0 4 1215 0 0 1 %s %d %d\nEND\n", id, history, low, high);
}

int main(void) {
    FILE* stream = tmpfile();
    assert(stream);
    request(stream, 41, "UNKNOWN", 0, 0);
    request(stream, 42, "OBSERVED", -6, -3);
    request(stream, 43, "OBSERVED", -6, -6);
    rewind(stream);
    Request q;
    read_request(stream, &q);
    assert(q.id == 41 && q.attack_history == RISKFIGHT_ATTACK_UNKNOWN);
    read_request(stream, &q);
    assert(q.id == 42 && q.attack_history == RISKFIGHT_ATTACK_OBSERVED && q.attack_clock_low == -6 && q.attack_clock_high == -3);
    read_request(stream, &q);
    assert(q.id == 43 && q.attack_history == RISKFIGHT_ATTACK_OBSERVED && q.attack_clock_low == -6 && q.attack_clock_high == -6);
    fclose(stream);

    Player recent = {0}, idle = {0};
    recent.base_hitpoints = recent.current_hitpoints = idle.base_hitpoints = idle.current_hitpoints = 50;
    recent.current_hitpoints = idle.current_hitpoints = 20;
    recent.has_attack_timer = idle.has_attack_timer = 1;
    recent.attack_timer = idle.attack_timer = -1;
    recent.attack_timer_uncapped = -1;
    idle.attack_timer_uncapped = -6;
    LaBelief belief = {0};
    assert(la_key(&belief, &recent, 100, 100) != la_key(&belief, &idle, 100, 100));
    Player absent = idle;
    absent.has_attack_timer = 0;
    assert(la_key(&belief, &absent, 100, 100) != la_key(&belief, &idle, 100, 100));
    static RiskfightState a, b;
    a.env.players[1] = recent;
    b.env.players[1] = idle;
    assert(la_fingerprint(&a) != la_fingerprint(&b));
    a.env.players[1] = absent;
    assert(la_fingerprint(&a) != la_fingerprint(&b));
    assert(can_attack_now(&recent) && can_attack_now(&idle));
    osrs_player_eat_food_effects_with_clock(&recent, FOOD_SHARK, OSRS_FOOD_CONTINUOUS_CLOCK);
    osrs_player_eat_food_effects_with_clock(&idle, FOOD_SHARK, OSRS_FOOD_CONTINUOUS_CLOCK);
    assert(recent.attack_timer == 2 && recent.attack_timer_uncapped == 2 && !can_attack_now(&recent));
    assert(idle.attack_timer == -1 && idle.attack_timer_uncapped == -3 && can_attack_now(&idle));
    static Sim sim;
    sim_init(&sim);
    sim.state.env.players[1].base_hitpoints = 99;
    sim.obs.inf.opponent.timer = (GsTimerState){.seen = 1, .earliest = 104, .latest = 107};
    LaBelief known = la_belief_of(&sim, 110);
    assert(known.timer_seen && known.timer_first == 104 && known.timer_last == 107);
    known.hp_low = known.hp_high = 50;
    known.energy_low = known.energy_high = 100;
    known.pid = GS_PID_LOCAL;
    for (int deadline = 104; deadline <= 107; deadline++) {
        known.timer_first = known.timer_last = deadline;
        uint32_t rng = 99;
        Hist hist;
        la_world(&sim.state, &sim.hist, &known, 110, &rng, &a, &hist);
        Player* opponent = &a.env.players[1];
        assert(opponent->has_attack_timer && opponent->attack_timer == -1);
        assert(opponent->attack_timer_uncapped == deadline - 111);
        osrs_player_eat_food_effects_with_clock(opponent, FOOD_SHARK, OSRS_FOOD_CONTINUOUS_CLOCK);
        assert(opponent->attack_timer_uncapped == deadline - 108 && opponent->attack_timer == -1);
    }
    puts("Lookahead attack-clock contracts passed");
    return 0;
}
