#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main

static Sim sim;
static RiskfightState world, branch;
static Observer observer;
static TapeTick tick;
static RiskfightSnapshot snapshot;

/* A launch at N: melee may already have landed, while distance-one ranged
 * remains launched but lands at N+1 or N+2 according to the world's PID. */
static void launch_snapshot(int ranged) {
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.pid_holder = 1;
    snapshot.seed = 7;
    for (int source = 0; source < 2; source++) {
        RiskfightLoadout* loadout = &snapshot.loadout[source];
        loadout->hitpoints = 99;
        for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) loadout->equipment[slot] = ITEM_NONE;
        loadout->equipment[GEAR_SLOT_WEAPON] = ITEM_VOIDWAKER;
        snapshot.side[source] = (RiskfightSideSnapshot){.hitpoints = 99, .prayer = 99,
            .attack = 118, .strength = 118, .defence = 118, .magic = 99,
            .attack_history = RISKFIGHT_ATTACK_OBSERVED, .attack_timer = 10,
            .attack_timer_uncapped = 10, .seen_last_attack_ago = -1, .poison_source = OSRS_POISON_CLEAR_PRIOR};
    }
    gs_infer_init(&observer.inf);
    observer.inf.tracker_current = observer.inf.timed = 1;
    observer.inf.pid.owner = GS_PID_UNKNOWN;
    observer.inf.pending_launch_count = 1;
    observer.inf.pending_launches[0].by_pid[GS_PID_LOCAL] = (GsSwing){
        .lands = ranged ? 102 : 101, .least = 20, .most = 40, .accuracy = 1, .arrows = 1};
    observer.inf.pending_launches[0].by_pid[GS_PID_OPPONENT] = (GsSwing){
        .lands = ranged ? 101 : 100, .least = 20, .most = 40, .accuracy = 1, .arrows = 1};
    tick = (TapeTick){.tick = 100, .pending_known = 2};
    Tape tape = {.lo = 100, .hi = 100, .ticks = &tick, .observer_series = &observer};
    tape_pending_snapshot(&tape, 100, &snapshot);
    assert(snapshot.pending_known && snapshot.pending_pid_active);
    riskfight_load_state(&sim.state, &sim.context, &snapshot);
    assert(sim.state.pending_pid_active);
}

static LaBelief belief(void) {
    LaBelief b = {.hp_low = 99, .hp_high = 99, .energy_low = 0, .energy_high = 0,
        .pid = GS_PID_UNKNOWN, .vengeance = GS_VENGEANCE_DOWN,
        .gear = {.source = GS_GEAR_COMPOSITION_OBSERVED,
            .weapon = ITEM_DATABASE[ITEM_VOIDWAKER].item_id, .shield = -1}};
    for (int i = 0; i < GS_ARMOR_SLOTS; i++) b.gear.armor[i] = -1;
    return b;
}

static void test_common_world(int ranged) {
    launch_snapshot(ranged);
    LaBelief b = belief();
    int holders[2] = {0, 0};
    HumanCommandQueue idle = {0};
    for (uint32_t seed = 1; seed <= 64; seed++) {
        uint32_t rng = seed;
        Hist hist;
        la_world(&sim.state, &sim.hist, &b, 100, &rng, &world, &hist);
        int holder = world.env.pid_holder;
        holders[holder]++;
        assert(!world.pending_pid_active);
        Player* source = &world.env.players[1];
        int present = ranged || holder == 0;
        assert(source->num_pending_hits == present);
        int damage = present ? source->pending_hits[0].damage : 0;
        if (present) {
            assert(world.env.pending_hit_metadata[1][0].damage_mode == 1);
            assert(damage >= 20 && damage <= 40);
            assert(source->pending_hits[0].ticks_until_hit == (ranged && holder == 0));
        }
        /* Every macro starts from this resolved common world, not a new roll. */
        branch = world;
        uint32_t sampled = rng;
        riskfight_sample_pending(&branch, &rng);
        assert(rng == sampled);
        assert(!memcmp(branch.env.pending_hit_metadata, world.env.pending_hit_metadata,
            sizeof(world.env.pending_hit_metadata)));
        for (int step = 0; step < 3; step++) {
            riskfight_step_queues(&world, &sim.context, &idle, &idle);
            riskfight_step_queues(&branch, &sim.context, &idle, &idle);
            int landed = step > 0 || !ranged || holder == 1;
            assert(world.env.players[0].current_hitpoints == 99 - (landed ? damage : 0));
            assert(branch.env.players[0].current_hitpoints == world.env.players[0].current_hitpoints);
            assert(branch.env.players[1].num_pending_hits == world.env.players[1].num_pending_hits);
            riskfight_sample_pending(&world, &rng);
            assert(rng == sampled); /* No already-landed initial launch is regenerated. */
        }
        assert(world.env.players[1].num_pending_hits == 0);
    }
    assert(holders[0] && holders[1]);
    assert(sim.state.pending_pid_active); /* World resolution never consumes the reusable base. */
}

int main(void) {
    sim_init(&sim);
    test_common_world(0);
    test_common_world(1);
    puts("pending PID common worlds ok");
    return 0;
}
