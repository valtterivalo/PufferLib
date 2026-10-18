/* The training mix of a self-play env: who plays seat 1 and what he wears, and that the seats are treated alike.
 *
 *   mix       a fight is pure self-play (both seats the learning policy: tag 0), a frozen checkpoint (tag 1, net), or a pool kit played by OppFit or the script
 *             (tag 1, scripted_probability). The shares follow from vec.hist_policy_percent h and env.scripted_probability s: 1-h, h(1-s), h s. The kit of a
 *             net-played seat 1 is a pool kit with selfplay_pool_probability, else one of ours (live_loadout 3: the three Voidwaker kits; live_loadout 4: the Dharok
 *             kit half the time, else those three); the script and OppFit always take a pool kit.
 *   rematch   keeps seat 1's player, his kit and his price; a new opponent draws all three again.
 *   classes   kit_same_class: a new opponent's kit is of seat 0's kit class (Dharok's, Soulreaper, melee) that often, else drawn without a condition;
 *             learner_random_kit: seat 0 wears a pool kit it can play that often. In pure self-play both seats draw by that rule, at price 1. 100k draws each.
 *   symmetry  the observation and the mask are one function of a seat's own view: swapping the seats of a state swaps the two views, so a copy of one net
 *             in either seat is shown the mirrored fight. The session rules (rude teleport, rematch, regear, delays) treat both seats alike.
 * OppFit needs RF_OPPFIT_MODEL and RF_OPPFIT_EAT; the OppFit share is tested when they are set. */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static float observations[2][RF_OBS_SIZE];
static float actions[2][RF_HEADS];
static float rewards[2], terminals[2], resets[2];
static unsigned char masks[2][RF_MASK_SIZE];

static Env* make_env(const DictItem* extra, int extra_count, int tag, unsigned rng) {
    DictItem items[24] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1}, {.key = "session_ticks", .value = 3000},
        {.key = "curveball_probability", .value = 0}, {.key = "death_price", .value = 0}, {.key = "live_loadout", .value = 3},
        {.key = "opponent_pool", .value = 1}, {.key = "domain_randomization", .value = 1}, {.key = "deployable", .value = 1},
        {.key = "rep_start_probability", .value = 0}}; /* the shares below are those of a session without a reputation (a ragger is the script whatever the mix) */
    int count = 10;
    for (int i = 0; i < extra_count; i++) {
        int replaced = 0;
        for (int j = 0; j < count; j++)
            if (!strcmp(items[j].key, extra[i].key)) { items[j].value = extra[i].value; replaced = 1; }
        if (!replaced) items[count++] = extra[i];
    }
    Dict kwargs = {.items = items, .size = (size_t)count};
    Env* e = (Env*)calloc(1, sizeof(*e));
    e->rng = rng;
    puf_init(e, &kwargs);
    e->tag = tag;
    for (int i = 0; i < 2; i++) e->agents[i] = (Agent){
        .observations = observations[i], .actions = actions[i], .rewards = &rewards[i],
        .terminals = &terminals[i], .resets = &resets[i], .action_mask = masks[i], .policy = i};
    puf_reset(e);
    memset(actions, 0, sizeof(actions));
    return e;
}
static void destroy(Env* e) { puf_close(e); free(e); }

static int within(double count, double n, double p) {
    double sigma = sqrt(n * p * (1 - p)) + 1e-9;
    return fabs(count - n * p) <= 4.5 * sigma + 1;
}

typedef struct { int n, net, frozen_or_self_net, driven, pool, net_pool, driven_pool, net_live, net_vw, net_pies, oppfit; } Tally;
static void tally(Tally* t, const Env* e) {
    const RiskfightContext* c = &e->context;
    int net = c->self_play && !c->opp_driven;
    t->n++;
    t->net += net;
    t->driven += !net;
    t->pool += c->opp_kit != NULL;
    t->net_pool += net && c->opp_kit != NULL;
    t->driven_pool += !net && c->opp_kit != NULL;
    t->net_live += net && c->opp_kit == NULL && c->loadout[1] == &RISKFIGHT_LIVE_LOADOUT;
    t->net_vw += net && c->opp_kit == NULL && c->loadout[1] == &RISKFIGHT_VW_LOADOUT;
    t->net_pies += net && c->opp_kit == NULL && c->loadout[1] == &RISKFIGHT_VW_PIES_LOADOUT;
    t->oppfit += c->opp_hook != NULL;
    if (net) assert(c->opp_hook == NULL); /* a net in seat 1 has no hook */
    if (!net) assert(c->opp_kit != NULL);  /* the script and OppFit wear pool kits */
    if (net && c->opp_kit == NULL) assert(c->loadout[1] == &RISKFIGHT_LIVE_LOADOUT || c->loadout[1] == &RISKFIGHT_VW_LOADOUT || c->loadout[1] == &RISKFIGHT_VW_PIES_LOADOUT);
    if (c->opp_kit) assert(c->loadout[1] == &RISKFIGHT_POOL_LOADOUT[c->opp_kit - RISKFIGHT_POOL]);
}

static void test_mix_fractions(void) {
    const double h = 0.5, s = 0.4, q = 0.3;
    DictItem knobs[] = {{.key = "scripted_probability", .value = s}, {.key = "selfplay_pool_probability", .value = q},
        {.key = "scripted_omission_initial", .value = 0.8}, {.key = "scripted_omission_decay_ticks", .value = 150000}};
    const int envs = 400, draws = 40;
    Tally all = {0}, selfplay = {0}, teacher = {0};
    int frozen_start = envs - (int)(h * envs);
    for (int e = 0; e < envs; e++) {
        int tag = e < frozen_start ? 0 : 1;
        Env* env = make_env(knobs, 4, tag, 1000 + (unsigned)e);
        for (int d = 0; d < draws; d++) {
            tally(&all, env);
            tally(tag ? &teacher : &selfplay, env);
            if (!tag) assert(env->state.death_price == 1.0f && env->training.mirror); /* pure self-play: the even mirror, price 1 */
            puf_reset(env);
        }
        destroy(env);
    }
    /* The three-way mix: pure self-play 1-h, a frozen checkpoint h(1-s), a pool kit played by OppFit or the script h s. */
    int pure = selfplay.n, frozen = teacher.net, scripted = teacher.driven;
    assert(selfplay.net == selfplay.n && selfplay.driven == 0);
    assert(within(pure, all.n, 1 - h));
    assert(within(frozen, all.n, h * (1 - s)));
    assert(within(scripted, all.n, h * s));
    assert(pure + frozen + scripted == all.n);
    /* A frozen net wears a pool kit with probability q, else one of ours; the learning policy in seat 1 always wears one of ours (the mirror). Ours: a third each. */
    assert(selfplay.net_pool == 0 && within(teacher.net_pool, teacher.net, q));
    assert(within(all.net_live, all.net - all.net_pool, 1.0 / 3));
    assert(within(all.net_vw, all.net - all.net_pool, 1.0 / 3));
    assert(all.net_live + all.net_vw + all.net_pies == all.net - all.net_pool);
    assert(all.driven_pool == all.driven && all.driven > 0);
    printf("mix: self-play %.3f frozen %.3f scripted/oppfit %.3f (expected %.3f %.3f %.3f); net pool kit %.3f (%.2f), live %.3f of the three kits (live, vw, vw with pies)\n",
        (double)pure / all.n, (double)frozen / all.n, (double)scripted / all.n, 1 - h, h * (1 - s), h * s,
        (double)teacher.net_pool / teacher.net, q, (double)all.net_live / (all.net - all.net_pool));
}

/* live_loadout 4: our kit, drawn once per session, is the Dharok kit half the time and each Voidwaker kit a sixth; a net in seat 1 wearing one of ours draws the same way. */
static void test_dharok_mix_fractions(void) {
    DictItem knobs[] = {{.key = "live_loadout", .value = 4}, {.key = "scripted_probability", .value = 0}, {.key = "selfplay_pool_probability", .value = 0}};
    Env* env = make_env(knobs, 3, 0, 4242);
    assert(env->context.kit_mix == RF_KIT_MIX_DHAROK);
    const RiskfightLoadout* kits[4] = {&RISKFIGHT_DHAROK_LOADOUT, &RISKFIGHT_LIVE_LOADOUT, &RISKFIGHT_VW_LOADOUT, &RISKFIGHT_VW_PIES_LOADOUT};
    const double share[4] = {0.5, 1.0 / 6, 1.0 / 6, 1.0 / 6};
    enum { RESETS = 6000 };
    int ours[4] = {0}, his[4] = {0};
    for (int i = 0; i < RESETS; i++) {
        puf_reset(env);
        assert(env->context.opp_kit == NULL);
        for (int k = 0; k < 4; k++) { ours[k] += env->context.loadout[0] == kits[k]; his[k] += env->context.loadout[1] == kits[k]; }
        if (env->context.loadout[0] == &RISKFIGHT_DHAROK_LOADOUT) assert(env->state.env.players[0].equipped[GEAR_SLOT_BODY] == ITEM_DHAROKS_PLATEBODY);
    }
    for (int k = 0; k < 4; k++) assert(within(ours[k], RESETS, share[k]) && within(his[k], RESETS, share[k]));
    assert(ours[0] + ours[1] + ours[2] + ours[3] == RESETS && his[0] + his[1] + his[2] + his[3] == RESETS);
    destroy(env);
    printf("dharok mix: our kit Dharok %.3f, live %.3f, vw %.3f, vw pies %.3f (0.5 and a sixth each); seat 1 Dharok %.3f\n", (double)ours[0] / RESETS,
        (double)ours[1] / RESETS, (double)ours[2] / RESETS, (double)ours[3] / RESETS, (double)his[0] / RESETS);
}

/* The mix also holds across the opponents of one session, and a rematch keeps his player. */
static void test_new_opponents_and_rematch(void) {
    DictItem knobs[] = {{.key = "scripted_probability", .value = 0.4}, {.key = "selfplay_pool_probability", .value = 0.3},
        {.key = "scripted_omission_initial", .value = 0}, {.key = "scripted_omission_decay_ticks", .value = 0}};
    Env* env = make_env(knobs, 4, 1, 77);
    Tally t = {0};
    for (int i = 0; i < 6000; i++) {
        RiskfightTransition fresh = {.delay = 100};
        riskfight_training_next_fight(&env->training, &env->state, &env->context, &fresh, env->tag);
        tally(&t, env);
        /* A rematch keeps him: his kit, his price, who plays him. */
        const RiskfightKit* kit = env->context.opp_kit;
        const RiskfightLoadout* loadout = env->context.loadout[1];
        float price = env->state.death_price;
        int driven = env->context.opp_driven;
        RiskfightTrainingOpponent opponent = env->training.opponent;
        RiskfightTransition again = {.delay = 100, .rematch = 1};
        riskfight_training_next_fight(&env->training, &env->state, &env->context, &again, env->tag);
        assert(env->context.opp_kit == kit && env->context.loadout[1] == loadout && env->state.death_price == price);
        assert(env->context.opp_driven == driven && env->training.opponent == opponent);
    }
    assert(within(t.driven, t.n, 0.4));
    assert(within(t.net_pool, t.net, 0.3));
    assert(t.driven_pool == t.driven);
    /* Another price for every new opponent. */
    float seen = env->state.death_price;
    int changed = 0;
    for (int i = 0; i < 20; i++) {
        RiskfightTransition fresh = {.delay = 100};
        riskfight_training_next_fight(&env->training, &env->state, &env->context, &fresh, env->tag);
        changed += env->state.death_price != seen;
    }
    assert(changed >= 19);
    destroy(env);
    printf("session: %d new opponents, driven %.3f (0.4), net pool kit %.3f (0.3); a rematch keeps kit, price and player\n", t.n, (double)t.driven / t.n,
        (double)t.net_pool / t.net);
}

/* The mirror treats the seats alike: seat 1's kit is drawn with ours at the session's start and kept for its new opponents, as ours is kept, and domain randomization,
 * which weakens the side of an opponent, leaves the learning policy's seat 1 alone. */
static void test_mirror_keeps_kits_and_levels(void) {
    DictItem knobs[] = {{.key = "live_loadout", .value = 4}, {.key = "scripted_probability", .value = 0}, {.key = "selfplay_pool_probability", .value = 0}};
    Env* env = make_env(knobs, 3, 0, 515);
    for (int session = 0; session < 50; session++) {
        puf_reset(env);
        const RiskfightLoadout* kit = env->context.loadout[1];
        for (int i = 0; i < 20; i++) {
            RiskfightTransition fresh = {.delay = 100};
            riskfight_training_next_fight(&env->training, &env->state, &env->context, &fresh, env->tag);
            assert(env->training.mirror && env->context.loadout[1] == kit);
            const Player* us = &env->state.env.players[0];
            const Player* him = &env->state.env.players[1];
            assert(him->current_attack == us->current_attack && him->current_strength == us->current_strength && env->state.opp_lag == 0);
        }
    }
    destroy(env);
    printf("mirror: seat 1 keeps its kit for the session and its levels\n");
}

/* data_escapes: where both seats are nets (pure self-play, tag 0, and a frozen checkpoint, tag 1) neither may teleport by its own choice; when the scripts' escape
 * rule fires for a seat its mask is the no-op and the step teleports it. A seat in a Dharok kit follows RF_DHAROK_TAB_PM. Where seat 1 is the script (or in a
 * self_play 0 env) our teleport is free and the script leaves by its own rule. */
static void test_data_escapes(void) {
    int dharok = 0, other = 0;
    for (int tag = 0; tag < 2; tag++) {
        DictItem knobs[] = {{.key = "data_escapes", .value = 1}, {.key = "live_loadout", .value = 4}, {.key = "scripted_probability", .value = 0},
            {.key = "selfplay_pool_probability", .value = 0}};
        Env* env = make_env(knobs, 4, tag, 616 + (unsigned)tag);
        for (int i = 0; i < 200; i++) {
            puf_reset(env);
            assert(!env->context.opp_driven);
            for (int seat = 0; seat < 2; seat++) {
                const int kit_dharok = riskfight_player_dharok_pieces(&env->state.env.players[seat]) >= RF_DHAROK_KIT_PIECES;
                assert(env->state.data_escapes[seat] == (kit_dharok ? RF_DATA_ESCAPES_DHAROK : RF_DATA_ESCAPES_PROFILE));
                dharok += kit_dharok;
                other += !kit_dharok;
                assert(!masks[seat][RF_HEAD_OFFSET[RF_PRIMARY] + RF_TELEPORT]);
            }
        }
        /* Below 30 hitpoints out of heals the rule leaves at once: that seat's mask is the no-op, and a no-op step ends the fight by its teleport. */
        for (int seat = 0; seat < 2; seat++) {
            puf_reset(env);
            Player* p = &env->state.env.players[seat];
            p->current_hitpoints = 20;
            for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
                OsrsConsumableKind k = (OsrsConsumableKind)osrs_inventory_cell_metadata(&p->inventory_cells[slot])->consumable_kind;
                if (k != OSRS_CONSUMABLE_TELEPORT) p->inventory_cells[slot] = osrs_inventory_cell_from_content_code(0);
            }
            riskfight_native_observe(env);
            unsigned char wait[RF_MASK_SIZE];
            riskfight_write_wait_mask(wait);
            assert(riskfight_data_escape(&env->state, seat) && !memcmp(masks[seat], wait, RF_MASK_SIZE));
            const float ours = env->log.self_teleports, his = env->log.opponent_teleports;
            memset(actions, 0, sizeof(actions));
            puf_step(env);
            assert(env->log.self_teleports - ours == (seat == 0) && env->log.opponent_teleports - his == (seat == 1));
        }
        destroy(env);
    }
    assert(dharok > 0 && other > 0);
    /* A frozen-tagged env whose seat 1 is the script, and a self_play 0 env: our teleport is ours to choose, the script leaves by its own rule. */
    DictItem driven_knobs[] = {{.key = "data_escapes", .value = 1}, {.key = "scripted_probability", .value = 1}};
    DictItem lone_knobs[] = {{.key = "data_escapes", .value = 1}, {.key = "self_play", .value = 0}};
    for (int k = 0; k < 2; k++) {
        Env* env = k ? make_env(lone_knobs, 2, 0, 618) : make_env(driven_knobs, 2, 1, 617);
        assert(k ? !env->context.self_play : env->context.opp_driven);
        assert(env->state.data_escapes[0] == RF_DATA_ESCAPES_OFF && env->state.data_escapes[1] == RF_DATA_ESCAPES_OFF);
        assert(masks[0][RF_HEAD_OFFSET[RF_PRIMARY] + RF_TELEPORT]);
        destroy(env);
    }
    printf("data escapes: both net seats' own teleport masked (%d Dharok-kit and %d other seats), the escape tick a step of no choice; free against the script\n",
        dharok, other);
}

/* A ragger is the script, so he only steps into a seat 1 no learning policy plays: a self_play 0 env and a frozen (tag > 0) env draw him with the reputation's
 * probability, a pure self-play env (tag 0) never does, nor any other driven opponent: seat 1's rows there train the learning policy, which must play them. */
static void test_raggers_only_where_no_net_learns(void) {
    const struct { int self_play, tag; float share; } cases[3] = {{1, 0, 0}, {1, 1, RF_REP_RAG_CAP}, {0, 0, RF_REP_RAG_CAP}};
    for (int k = 0; k < 3; k++) {
        DictItem knobs[] = {{.key = "self_play", .value = cases[k].self_play}, {.key = "scripted_probability", .value = 0.5}};
        Env* env = make_env(knobs, 2, cases[k].tag, 900 + (unsigned)k);
        const int n = 3000;
        int ragged = 0, driven = 0;
        for (int i = 0; i < n; i++) {
            env->training.rep = 10; /* the cap: RF_REP_RAG_CAP */
            RiskfightTransition fresh = {.delay = 100};
            riskfight_training_next_fight(&env->training, &env->state, &env->context, &fresh, env->tag);
            ragged += env->state.ragger;
            driven += env->context.opp_driven;
        }
        if (cases[k].share == 0) assert(ragged == 0 && driven == 0);
        else assert(within(ragged, n, cases[k].share));
        destroy(env);
        printf("raggers: self_play %d tag %d: %.3f of new opponents (%.3f), driven %.3f\n", cases[k].self_play, cases[k].tag, (double)ragged / n,
            cases[k].share, (double)driven / n);
    }
}

/* OppFit plays the pool kits it was fitted on, the script the rest, in a self-play env too; a net never gets the hook. */
static void test_oppfit_in_self_play(void) {
    if (!getenv("RF_OPPFIT_MODEL") || !getenv("RF_OPPFIT_EAT")) { puts("oppfit share: skipped (RF_OPPFIT_MODEL / RF_OPPFIT_EAT not set)"); return; }
    DictItem knobs[] = {{.key = "scripted_probability", .value = 0.5}, {.key = "selfplay_pool_probability", .value = 0.5}, {.key = "oppfit_probability", .value = 0.5},
        {.key = "scripted_omission_initial", .value = 0}, {.key = "scripted_omission_decay_ticks", .value = 0}};
    Env* env = make_env(knobs, 5, 1, 5);
    int driven = 0, fitted_kit = 0, hooks = 0, hooks_on_unfitted = 0, net_hooks = 0;
    for (int i = 0; i < 3000; i++) {
        RiskfightTransition fresh = {.delay = 100};
        riskfight_training_next_fight(&env->training, &env->state, &env->context, &fresh, env->tag);
        riskfight_native_opponent_hooks(env, 1);
        const RiskfightContext* c = &env->context;
        if (c->opp_driven) {
            driven++;
            fitted_kit += c->opp_kit->oppfit_ok;
            hooks += c->opp_hook != NULL;
            hooks_on_unfitted += c->opp_hook != NULL && !c->opp_kit->oppfit_ok;
        } else net_hooks += c->opp_hook != NULL;
    }
    assert(net_hooks == 0 && hooks_on_unfitted == 0 && hooks > 0);
    assert(within(hooks, fitted_kit, 0.5));
    destroy(env);
    printf("oppfit: %d driven opponents, %d on fitted kits, %d played by OppFit (half of those)\n", driven, fitted_kit, hooks);
}

/* With the omission at 1 the driven opponent of a self-play env does nothing; a net in seat 1 is not touched by it. */
static void test_omission_only_for_the_driven(void) {
    DictItem knobs[] = {{.key = "scripted_probability", .value = 1}, {.key = "scripted_omission_initial", .value = 1}, {.key = "scripted_omission_decay_ticks", .value = 1000000}};
    Env* env = make_env(knobs, 3, 1, 3);
    assert(env->context.opp_driven);
    for (int tick = 0; tick < 20; tick++) {
        actions[0][RF_PRIMARY] = RF_ATTACK;
        actions[1][RF_PRIMARY] = RF_TELEPORT; /* ignored: the script plays him, and is omitted */
        puf_step(env);
        assert(env->context.opp_omit == 1 && !env->state.escaped[1] && env->state.env.players[1].attack_timer == 0);
        assert(env->state.env.players[1].current_hitpoints > 0);
    }
    assert(env->training.omissions == 20 && env->log.escapes == 0);
    destroy(env);
    DictItem plain[] = {{.key = "scripted_probability", .value = 0}, {.key = "scripted_omission_initial", .value = 1}, {.key = "scripted_omission_decay_ticks", .value = 1000000}};
    env = make_env(plain, 3, 0, 4);
    assert(!env->context.opp_driven);
    actions[0][RF_PRIMARY] = RF_ATTACK;
    actions[1][RF_PRIMARY] = RF_TELEPORT; /* a net's choice is played */
    puf_step(env);
    assert(env->context.opp_omit == 0 && env->log.escapes + (env->session_fights > 0) >= 1);
    destroy(env);
    puts("omission: spares the net, silences the driven opponent");
}

#define SWAP(a, b) do { __typeof__(a) tmp_ = (a); (a) = (b); (b) = tmp_; } while (0)
/** The state as seat 1 would see it were the seats exchanged: everything the observation and the mask read is per seat. The price is seat 0's, so the exchange
 * inverts it (exact for the powers of two the tests below use). */
static void swap_seats(RiskfightState* s) {
    SWAP(s->env.players[0], s->env.players[1]);
    SWAP(s->inventory_use[0], s->inventory_use[1]);
    SWAP(s->visible[0], s->visible[1]);
    SWAP(s->overeats[0], s->overeats[1]);
    SWAP(s->hand_busy[0], s->hand_busy[1]);
    SWAP(s->session_rep[0], s->session_rep[1]);
    SWAP(s->env.pvp_runtime.maul[0], s->env.pvp_runtime.maul[1]);
    SWAP(s->env.pvp_runtime.teleport[0], s->env.pvp_runtime.teleport[1]);
    s->death_price = riskfight_seat_price(s, 1);
}

static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

/** A legal random action for a seat: every head one of the actions its mask allows. */
static void random_actions(const unsigned char* mask, uint32_t* rng, int* out) {
    int offset = 0;
    for (int head = 0; head < RF_HEADS; head++) {
        int dim = RF_ACTION_DIMS[head];
        *rng = hash32(*rng);
        int pick = 0;
        if (head == RF_PRIMARY) pick = (*rng & 7) == 0 ? RF_STOP : RF_ATTACK;
        else if ((*rng & 3) == 0) pick = (int)((*rng >> 8) % (uint32_t)dim);
        if (!mask[offset + pick]) pick = 0;
        out[head] = pick;
        offset += dim;
    }
}

static void test_swapped_seats_see_mirrored_observations(void) {
    DictItem knobs[] = {{.key = "scripted_probability", .value = 0}, {.key = "selfplay_pool_probability", .value = 0.7}};
    int compared = 0, kits_differ = 0;
    for (int fight = 0; fight < 24; fight++) {
        Env* env = make_env(knobs, 2, 0, 500 + (unsigned)fight);
        RiskfightState* s = &env->state;
        s->death_price = 2.0f; /* seat 1's is 0.5: the swap inverts it exactly */
        kits_differ += memcmp(s->env.players[0].equipped, s->env.players[1].equipped, NUM_GEAR_SLOTS) != 0;
        uint32_t rng = 0x9e3779b9u + (uint32_t)fight;
        for (int tick = 0; tick < 70 && !s->env.episode_over; tick++) {
            int act[2 * RF_HEADS];
            for (int seat = 0; seat < 2; seat++) {
                unsigned char mask[RF_MASK_SIZE];
                riskfight_write_action_mask(s, seat, mask);
                random_actions(mask, &rng, act + seat * RF_HEADS);
            }
            riskfight_step((EncounterState*)s, (EncounterContext*)&env->context, act);
            if (s->env.episode_over) break;
            RiskfightState* swapped = (RiskfightState*)malloc(sizeof(*swapped));
            memcpy(swapped, s, sizeof(*swapped));
            swap_seats(swapped);
            for (int seat = 0; seat < 2; seat++) {
                float a[RF_OBS_SIZE], b[RF_OBS_SIZE];
                unsigned char ma[RF_MASK_SIZE], mb[RF_MASK_SIZE];
                riskfight_write_observation(s, seat, a);
                riskfight_write_observation(swapped, 1 - seat, b);
                assert(memcmp(a, b, sizeof(a)) == 0);
                riskfight_write_action_mask(s, seat, ma);
                riskfight_write_action_mask(swapped, 1 - seat, mb);
                assert(memcmp(ma, mb, sizeof(ma)) == 0);
                compared++;
            }
            free(swapped);
        }
        destroy(env);
    }
    assert(kits_differ > 0 && compared > 500);
    printf("symmetry: %d seat views of fights with different kits equal their mirror (observation %d floats, mask %d)\n", compared, RF_OBS_SIZE, RF_MASK_SIZE);
}

/* Both seats get the same start: the same fight with the kits exchanged shows seat 1 what seat 0 saw, but where the players stand. */
static void test_start_views_mirror(void) {
    DictItem knobs[] = {{.key = "scripted_probability", .value = 0}};
    Env* env = make_env(knobs, 1, 0, 9);
    RiskfightContext* c = &env->context;
    c->opponent_pool = 0;
    c->kit_mix = 0;
    const RiskfightLoadout* kits[2] = {&RISKFIGHT_LIVE_LOADOUT, &RISKFIGHT_VW_LOADOUT};
    RiskfightState a, b;
    for (int order = 0; order < 2; order++) {
        RiskfightState* s = order ? &b : &a;
        c->loadout[0] = kits[order];
        c->loadout[1] = kits[1 - order];
        riskfight_reset((EncounterState*)s, (EncounterContext*)c, 31337);
        s->death_price = order ? 0.5f : 2.0f; /* seat 0's price; seat 1's is its inverse, so the exchanged fight inverts it */
        s->session_ticks_left = 1234;
    }
    int differing = 0;
    for (int seat = 0; seat < 2; seat++) {
        float x[RF_OBS_SIZE], y[RF_OBS_SIZE];
        riskfight_write_observation(&a, seat, x);
        riskfight_write_observation(&b, 1 - seat, y);
        for (int i = 0; i < RF_OBS_SIZE; i++) {
            if (x[i] == y[i]) continue;
            differing++;
            /* Only where the players stand: the seats start a tile apart (own tile, and his tile and the distance between us). */
            assert(i == 21 || i == 22 || (i >= RF_OPPONENT_START + NUM_GEAR_SLOTS && i < RF_HISTORY_START));
        }
    }
    float mask_a[RF_MASK_SIZE];
    unsigned char ma[RF_MASK_SIZE], mb[RF_MASK_SIZE];
    (void)mask_a;
    for (int seat = 0; seat < 2; seat++) {
        riskfight_write_action_mask(&a, seat, ma);
        riskfight_write_action_mask(&b, 1 - seat, mb);
        assert(memcmp(ma, mb, sizeof(ma)) == 0);
    }
    destroy(env);
    printf("start views: kits exchanged, %d floats differ (all of them positions); masks equal\n", differing);
}

/* The session rules for seat 1 are the rules for seat 0, mirrored. */
static RiskfightState teleport_state(int leaver, int leaver_energy, int other_energy) {
    RiskfightState s;
    memset(&s, 0, sizeof(s));
    s.outcome[0] = s.outcome[1] = RISKFIGHT_ESCAPE;
    s.escaped[leaver] = 1;
    s.escape_supplies[leaver][1 - leaver].special_energy = other_energy;
    s.escape_supplies[leaver][1 - leaver].minimum_spec_cost = 50;
    s.escape_supplies[leaver][leaver].special_energy = leaver_energy;
    s.escape_supplies[leaver][leaver].minimum_spec_cost = 50;
    for (int side = 0; side < 2; side++) /* every supply class stocked: only the specials decide (riskfight_norms.h) */
        s.escape_supplies[leaver][side].hard_food = s.escape_supplies[leaver][side].restore_doses = s.escape_supplies[leaver][side].boost_doses = 1;
    return s;
}

static void test_session_rules_are_symmetric(void) {
    const int trials = 4000;
    int rematches[2][3] = {{0}}, rudes[2][3] = {{0}};
    const RiskfightRepRules rules = riskfight_rep_rules(RF_REP_SEARCH, RF_REP_RAG, RF_REP_HALF_LIFE_TICKS);
    const struct { int own, energy; } cases[3] = {{0, 100}, {100, 100}, {0, 0}}; /* rude (ours spent, his not), rude (nobody has specced), kosher (both spent) */
    for (int leaver = 0; leaver < 2; leaver++)
        for (int c = 0; c < 3; c++) {
            RiskfightState s = teleport_state(leaver, cases[c].own, cases[c].energy);
            uint32_t rng = 7 + 100 * (uint32_t)c;
            for (int i = 0; i < trials; i++) {
                RiskfightTransition t = riskfight_session_transition(&s, &rng, 0, &rules);
                rematches[leaver][c] += t.rematch;
                rudes[leaver][c] += t.rude;
                assert(t.delay >= 0);
                if (t.rematch) assert(t.delay >= RF_DELAY_REMATCH.ticks[0] && t.delay <= RF_DELAY_REMATCH.ticks[RF_DELAY_DECILES - 1]);
            }
        }
    for (int leaver = 0; leaver < 2; leaver++) {
        for (int c = 0; c < 2; c++) assert(rudes[leaver][c] == trials && rematches[leaver][c] == 0); /* a rude teleport loses the opponent, whoever leaves */
        assert(rudes[leaver][2] == 0 && within(rematches[leaver][2], trials, RF_REMATCH_PROBABILITY));
    }
    /* Both leave: rude if either was. */
    RiskfightState both = teleport_state(0, 0, 100);
    both.escaped[1] = 1;
    both.escape_supplies[1][0].special_energy = 100;
    both.escape_supplies[1][0].minimum_spec_cost = 50;
    for (int side = 0; side < 2; side++) both.escape_supplies[1][side].hard_food = both.escape_supplies[1][side].restore_doses = both.escape_supplies[1][side].boost_doses = 1;
    uint32_t rng = 3;
    for (int i = 0; i < 300; i++) {
        RiskfightTransition t = riskfight_session_transition(&both, &rng, 0, &rules);
        assert(t.rude && !t.rematch);
    }
    printf("session rules: rematch rate %.3f / %.3f after a polite teleport of seat 0 / seat 1 (%.2f); a rude one never rematches\n",
        (double)rematches[0][1] / trials, (double)rematches[1][1] / trials, RF_REMATCH_PROBABILITY);
}

/* Every fight starts after a regear, whichever seat left and whichever side stayed: both seats full supplies, special energy 100, 121 hitpoints. */
static int count_food(const Player* p) {
    int n = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        n += osrs_inventory_cell_metadata(&p->inventory_cells[slot])->click_action == OSRS_CLICK_EAT;
    return n;
}

static void test_every_fight_is_a_regear(void) {
    for (int rematch = 0; rematch < 2; rematch++)
        for (int leaver = 0; leaver < 2; leaver++) {
            DictItem knobs[] = {{.key = "scripted_probability", .value = 0}, {.key = "selfplay_pool_probability", .value = 0}, {.key = "domain_randomization", .value = 0}};
            Env* env = make_env(knobs, 3, 0, 61 + (unsigned)leaver);
            RiskfightState* s = &env->state;
            int full_food[2] = {count_food(&s->env.players[0]), count_food(&s->env.players[1])};
            for (int side = 0; side < 2; side++) {
                s->env.players[side].current_hitpoints = 50;
                s->env.players[side].special_energy = 30;
                s->env.players[side].current_attack = 90;
                s->env.players[side].current_prayer = 3;
                s->inventory_use[side].vengeance_sacks = 4;
                for (int slot = 0; slot < 3; slot++) s->env.players[side].inventory_cells[slot] = osrs_inventory_cell_from_content_code(0);
            }
            s->outcome[0] = s->outcome[1] = RISKFIGHT_ESCAPE;
            s->escaped[leaver] = 1;
            RiskfightTransition t = {.delay = 300, .rematch = rematch};
            riskfight_training_next_fight(&env->training, s, &env->context, &t, env->tag);
            for (int side = 0; side < 2; side++) {
                const Player* p = &s->env.players[side];
                /* Seat 1 wears whatever kit steps up (a pool kit may carry less food or start at 22 hitpoints); seat 0 is our own kit. */
                assert(p->special_energy == 100 && p->current_attack > 99 && p->current_prayer == RF_PRAYER_LEVEL);
                assert(side == 1 ? p->current_hitpoints <= 121 : p->current_hitpoints == 121 && count_food(p) == full_food[0] && s->inventory_use[0].vengeance_sacks > 4);
            }
            destroy(env);
        }
    puts("regear: both seats start every fight, rematch or not, at 121 hitpoints with full supplies, special energy and prayer");
}

/* ---- Kit classes (kit_same_class, learner_random_kit). */
static const char* const CLASS_NAME[RF_CLASS_COUNT] = {"any", "dharok", "soulreaper", "ranged", "melee"};

/** The pool's draw mass per class, over the kits `filter` keeps (RF_CLASS_ANY: all; RF_PICK_LEARNER: the kits seat 0 can wear), normalized. */
static void class_mass(int filter, double out[RF_CLASS_COUNT]) {
    double total = 0;
    memset(out, 0, sizeof(double) * RF_CLASS_COUNT);
    for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++) {
        if (filter == RF_PICK_LEARNER && !riskfight_kit_learner_ok(&RISKFIGHT_POOL[i])) continue;
        out[RISKFIGHT_POOL[i].kit_class] += RISKFIGHT_POOL[i].weight;
        total += RISKFIGHT_POOL[i].weight;
    }
    for (int c = 0; c < RF_CLASS_COUNT; c++) out[c] /= total;
}

/** The seat wears its loadout as drawn: every worn slot (a ring the kit does not show is drawn on), the bag's contents in any order, the kit's hitpoints. */
static void assert_wears(const Env* e, int seat) {
    const RiskfightLoadout* l = e->context.loadout[seat];
    const Player* p = &e->state.env.players[seat];
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++)
        if (slot != GEAR_SLOT_RING || l->equipment[GEAR_SLOT_RING] != ITEM_NONE) assert(p->equipped[slot] == l->equipment[slot]);
    int held[OSRS_ITEM_CONTENT_COUNT] = {0};
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) { held[l->inventory[slot]]++; held[p->inventory_cells[slot].content_code]--; }
    for (int code = 0; code < OSRS_ITEM_CONTENT_COUNT; code++) assert(held[code] == 0);
    assert(p->current_hitpoints == l->hitpoints);
}

/** A Dharok's kit as a player shows it: helm, platebody and platelegs worn, the greataxe wielded or in the bag. */
static int shows_dharok(const Player* p) {
    int axe = p->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) axe |= osrs_inventory_cell_metadata(&p->inventory_cells[slot])->item_idx == ITEM_DHAROKS_GREATAXE;
    return axe && p->equipped[GEAR_SLOT_HEAD] == ITEM_DHAROKS_HELM && p->equipped[GEAR_SLOT_BODY] == ITEM_DHAROKS_PLATEBODY &&
        p->equipped[GEAR_SLOT_LEGS] == ITEM_DHAROKS_PLATELEGS;
}

enum { CLASS_DRAWS = 100000 };
static const double SAME = 0.8, RANDOM_KIT = 0.25;

/* Seat 0 (live_loadout 4) wears one of ours or, with learner_random_kit, a pool kit it can play; the script's pool kit is of seat 0's class with kit_same_class,
 * else from the whole pool: P(his class d | ours c) = s [c = d] + (1 - s) mass(d). Every kit drawn is worn and carried as drawn; a Dharok's draw shows the set. */
static void test_kit_class_conditioning(void) {
    DictItem knobs[] = {{.key = "self_play", .value = 0}, {.key = "live_loadout", .value = 4}, {.key = "domain_randomization", .value = 0},
        {.key = "kit_same_class", .value = SAME}, {.key = "learner_random_kit", .value = RANDOM_KIT}};
    Env* env = make_env(knobs, 5, 0, 31337);
    double pool[RF_CLASS_COUNT], learner[RF_CLASS_COUNT];
    class_mass(RF_CLASS_ANY, pool);
    class_mass(RF_PICK_LEARNER, learner);
    static int pair[RF_CLASS_COUNT][RF_CLASS_COUNT];
    int ours_random = 0, dharok_seen = 0;
    memset(pair, 0, sizeof(pair));
    for (int i = 0; i < CLASS_DRAWS; i++) {
        riskfight_training_reset(&env->training, &env->state, &env->context, env->tag);
        const RiskfightContext* c = &env->context;
        assert(c->opp_kit && c->opp_driven == 0 && !c->self_play);
        const RiskfightKitClass mine = riskfight_loadout_class(c->loadout[0]);
        assert(mine != RF_CLASS_RANGED);
        if (c->our_kit) assert(riskfight_kit_learner_ok(c->our_kit) && c->our_kit->kit_class == (int)mine);
        ours_random += c->our_kit != NULL;
        pair[mine][c->opp_kit->kit_class]++;
        assert_wears(env, 0);
        assert_wears(env, 1);
        if (c->opp_kit->kit_class == RF_CLASS_DHAROK) { assert(shows_dharok(&env->state.env.players[1])); dharok_seen++; }
    }
    /* Seat 0: a pool kit in learner_random_kit of the sessions; Dharok's with 1/2 of our kits and the learner pool's Dharok mass of the rest. */
    int ours_n[RF_CLASS_COUNT] = {0};
    for (int a = 0; a < RF_CLASS_COUNT; a++) for (int b = 0; b < RF_CLASS_COUNT; b++) ours_n[a] += pair[a][b];
    assert(fabs((double)ours_random / CLASS_DRAWS - RANDOM_KIT) < 0.01);
    const double ours_dharok = (1 - RANDOM_KIT) * 0.5 + RANDOM_KIT * learner[RF_CLASS_DHAROK];
    assert(fabs((double)ours_n[RF_CLASS_DHAROK] / CLASS_DRAWS - ours_dharok) < 0.01);
    printf("kit classes, script opponents (live_loadout 4, kit_same_class %.2f, learner_random_kit %.2f, %d draws):\n", SAME, RANDOM_KIT, CLASS_DRAWS);
    for (int a = RF_CLASS_DHAROK; a < RF_CLASS_COUNT; a++) {
        if (!ours_n[a]) continue;
        printf("  ours %-10s %.4f:", CLASS_NAME[a], (double)ours_n[a] / CLASS_DRAWS);
        for (int b = RF_CLASS_DHAROK; b < RF_CLASS_COUNT; b++) {
            const double expected = SAME * (a == b) + (1 - SAME) * pool[b], seen = (double)pair[a][b] / ours_n[a];
            printf(" his %s %.4f (%.4f)", CLASS_NAME[b], seen, expected);
            /* Within 1% where seat 0's class has the draws to show it (sd under 0.002), within 4.5 sd elsewhere. */
            if (ours_n[a] >= 20000) assert(fabs(seen - expected) < 0.01);
            else assert(within(pair[a][b], ours_n[a], expected));
        }
        printf("\n");
    }
    printf("  dharok v dharok %.4f of the draws (expected %.4f); %d Dharok's opponents all show helm, platebody, platelegs and the greataxe\n",
        (double)pair[RF_CLASS_DHAROK][RF_CLASS_DHAROK] / CLASS_DRAWS, ours_dharok * (SAME + (1 - SAME) * pool[RF_CLASS_DHAROK]), dharok_seen);
    destroy(env);
}

/* Pure self-play (tag 0): seat 1 draws as seat 0 does, given seat 0's class with kit_same_class. The pair of classes is symmetric: seat 1's class has seat 0's
 * law and P(same class) = s + (1 - s) sum p(c)^2. Both at price 1; both wear what they drew. Also the Dharok's variants: the pool's Dharok rows. */
static void test_self_play_kit_symmetry(void) {
    DictItem knobs[] = {{.key = "live_loadout", .value = 4}, {.key = "domain_randomization", .value = 0}, {.key = "scripted_probability", .value = 0},
        {.key = "kit_same_class", .value = SAME}, {.key = "learner_random_kit", .value = RANDOM_KIT}};
    Env* env = make_env(knobs, 5, 0, 4711);
    static int pair[RF_CLASS_COUNT][RF_CLASS_COUNT];
    int seat_n[2][RF_CLASS_COUNT] = {{0}}, his_random = 0;
    memset(pair, 0, sizeof(pair));
    for (int i = 0; i < CLASS_DRAWS; i++) {
        riskfight_training_reset(&env->training, &env->state, &env->context, env->tag);
        const RiskfightContext* c = &env->context;
        assert(env->training.mirror && !c->opp_driven && env->state.death_price == 1.0f && riskfight_seat_price(&env->state, 1) == 1.0f);
        assert(c->opp_kit == c->opp_chosen && (!c->opp_kit || riskfight_kit_learner_ok(c->opp_kit)));
        const RiskfightKitClass a = riskfight_loadout_class(c->loadout[0]), b = riskfight_loadout_class(c->loadout[1]);
        pair[a][b]++;
        seat_n[0][a]++;
        seat_n[1][b]++;
        his_random += c->opp_kit != NULL;
        assert_wears(env, 0);
        assert_wears(env, 1);
        if (b == RF_CLASS_DHAROK) assert(shows_dharok(&env->state.env.players[1]));
    }
    double same_expected = SAME, same = 0;
    for (int k = RF_CLASS_DHAROK; k < RF_CLASS_COUNT; k++) {
        const double p = (double)seat_n[0][k] / CLASS_DRAWS;
        assert(fabs(p - (double)seat_n[1][k] / CLASS_DRAWS) < 0.01);
        same_expected += (1 - SAME) * p * p;
        same += (double)pair[k][k] / CLASS_DRAWS;
        for (int j = RF_CLASS_DHAROK; j < RF_CLASS_COUNT; j++) assert(fabs((double)(pair[k][j] - pair[j][k]) / CLASS_DRAWS) < 0.01);
    }
    assert(fabs(same - same_expected) < 0.01);
    assert(fabs((double)his_random / CLASS_DRAWS - RANDOM_KIT) < 0.01);
    /* The Dharok's variants a draw can produce: the pool's Dharok rows (distinct loadouts; a ring the kit does not show is drawn on top) and our own kit. */
    int variants = 0, setups = 0;
    for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++) {
        if (RISKFIGHT_POOL[i].kit_class != RF_CLASS_DHAROK) continue;
        int loadout_new = 1, setup_new = 1;
        for (int j = 0; j < i; j++) {
            if (RISKFIGHT_POOL[j].kit_class != RF_CLASS_DHAROK) continue;
            loadout_new &= memcmp(&RISKFIGHT_POOL_LOADOUT[i], &RISKFIGHT_POOL_LOADOUT[j], sizeof(RiskfightLoadout)) != 0;
            setup_new &= memcmp(RISKFIGHT_POOL[i].equipment, RISKFIGHT_POOL[j].equipment, NUM_GEAR_SLOTS) || RISKFIGHT_POOL[i].weapon_count != RISKFIGHT_POOL[j].weapon_count ||
                memcmp(RISKFIGHT_POOL[i].weapons, RISKFIGHT_POOL[j].weapons, RF_KIT_WEAPONS);
        }
        variants += loadout_new;
        setups += setup_new;
    }
    assert(riskfight_loadout_class(&RISKFIGHT_DHAROK_LOADOUT) == RF_CLASS_DHAROK && riskfight_loadout_class(&RISKFIGHT_VW_LOADOUT) == RF_CLASS_MELEE);
    assert(setups >= 50);
    printf("pure self-play kits: seat 0 dharok %.4f / seat 1 %.4f, same class %.4f (%.4f), seat 1 pool kit %.4f, dharok v dharok %.4f; "
        "%d Dharok's loadouts in the pool (%d gear and weapon setups, the rest the orb copies) plus our own\n",
        (double)seat_n[0][RF_CLASS_DHAROK] / CLASS_DRAWS, (double)seat_n[1][RF_CLASS_DHAROK] / CLASS_DRAWS, same, same_expected, (double)his_random / CLASS_DRAWS,
        (double)pair[RF_CLASS_DHAROK][RF_CLASS_DHAROK] / CLASS_DRAWS, variants, setups);
    destroy(env);
}

int main(void) {
    test_mix_fractions();
    test_dharok_mix_fractions();
    test_raggers_only_where_no_net_learns();
    test_new_opponents_and_rematch();
    test_oppfit_in_self_play();
    test_omission_only_for_the_driven();
    test_mirror_keeps_kits_and_levels();
    test_data_escapes();
    test_swapped_seats_see_mirrored_observations();
    test_start_views_mirror();
    test_session_rules_are_symmetric();
    test_every_fight_is_a_regear();
    test_kit_class_conditioning();
    test_self_play_kit_symmetry();
    puts("riskfight mix and seat symmetry passed");
    return 0;
}
