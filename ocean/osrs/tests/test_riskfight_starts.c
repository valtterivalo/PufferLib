/* Fight starts (riskfight_start.h) and the live price table (riskfight_training.h):
 *   table     the generated rows hold the numbers scripts/measure_fight_starts.py reported (161 fights, 63 at 121 hitpoints, who swung first, ...).
 *   mapping   a drawn row lands in the state exactly: hitpoints, levels, special energy, Vengeance, supplies as deficits of the kit, his bar and hitpoints inside it,
 *             the tiles between us, who swung (the sim's own swing bookkeeping), the hits in flight; for every kit, opponent and seat the training draws.
 *   marginals the starts a training env produces reproduce the measured marginals within sampling error (rematches from their rows, first meetings from theirs).
 *   roles     in a self-play env with a net in seat 1 the user's part falls to either seat; elsewhere to seat 0.
 *   price     death_price 0 with price_table 1 draws the live table; without it the price stays inside the wide [0.4, 2.5] draw.
 *   off       without fight_starts a fight is the regear: tick 0, 121 hitpoints, nothing swung. */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static float observations[2][RF_OBS_SIZE];
static float actions[2][RF_HEADS];
static float rewards[2], terminals[2];
static unsigned char masks[2][RF_MASK_SIZE];

static Env* make_env(int self_play, int fight_starts, double price, int price_table, unsigned rng) {
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = self_play}, {.key = "session_ticks", .value = 3000},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = price}, {.key = "live_loadout", .value = 3},
        {.key = "opponent_pool", .value = 1}, {.key = "domain_randomization", .value = 1}, {.key = "deployable", .value = 1},
        {.key = "fight_starts", .value = fight_starts}, {.key = "price_table", .value = price_table}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    Env* e = (Env*)calloc(1, sizeof(*e));
    e->rng = rng;
    puf_init(e, &kwargs);
    for (int i = 0; i < 2; i++) e->agents[i] = (Agent){
        .observations = observations[i], .actions = actions[i], .rewards = &rewards[i],
        .terminals = &terminals[i], .action_mask = masks[i], .policy = i};
    e->tag = self_play ? 1 : 0;
    puf_reset(e);
    memset(actions, 0, sizeof(actions));
    return e;
}
static void destroy(Env* e) { puf_close(e); free(e); }

/** Four and a half sigma of a binomial: a draw count within it of n p. */
static int within(double count, double n, double p) {
    double sigma = sqrt(n * p * (1 - p)) + 1e-9;
    return fabs(count - n * p) <= 4.5 * sigma + 1;
}

static int count_kind(const Player* p, OsrsConsumableKind kind) {
    int n = 0;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) n += osrs_inventory_cell_metadata(&p->inventory_cells[i])->consumable_kind == kind;
    return n;
}
static int doses_of(const Player* p, OsrsConsumableKind kind) {
    int n = 0;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[i]);
        if (m->consumable_kind == kind) n += m->dose_count;
    }
    return n;
}

/** The table against the report of scripts/measure_fight_starts.py. */
static void test_table(void) {
    int n = 0, rematch = 0, hp121 = 0, hp120 = 0, below_120 = 0, us_alone = 0, him_alone = 0, both = 0, bar_partial = 0, spec_him = 0, dist1 = 0;
    for (int i = 0; i < RF_START_RECORD_COUNT; i++) {
        const RiskfightStartRecord* r = &RF_START_RECORDS[i];
        n++; rematch += r->rematch;
        hp121 += r->hp == 121; hp120 += r->hp == 120; below_120 += r->hp < 120;
        us_alone += r->opener_us && !r->opener_him; him_alone += r->opener_him && !r->opener_us; both += r->opener_us && r->opener_him;
        bar_partial += r->bar >= 0 && r->bar < 30; spec_him += r->spec_him; dist1 += r->dist == 1;
        assert(r->opener_us || r->opener_him);
        assert(r->hp > 0 && r->hp <= 121 && r->e_spec <= 100 && r->h_spec <= 100);
    }
    assert(n == 161 && rematch == 44);
    assert(hp121 == 63 && hp120 == 59 && below_120 == 39);
    assert(us_alone == 80 && him_alone == 68 && both == 13);
    assert(bar_partial == 12 && spec_him == 14 && dist1 == 141);
    assert(RF_PRICE_TABLE_COUNT == 89);
    for (int i = 0; i < RF_PRICE_TABLE_COUNT; i++) assert(RF_PRICE_TABLE[i] >= 0.1f - 1e-6f && RF_PRICE_TABLE[i] <= 2.5f);
    printf("table: %d starts (%d rematches), 121 hp %d, 120 hp %d; opened us %d him %d both %d; %d prices\n", n, rematch, hp121, hp120, us_alone, him_alone, both, RF_PRICE_TABLE_COUNT);
}

typedef struct {
    long n;
    long hp121, hp120, below_120, opened_us, opened_him, opened_both, bar_partial, bar_hidden, spec_him, spec_us, veng_us, veng_him, dist1, food_short, hit_in_flight_us, hit_in_flight_him, hurt;
} Tally;

static long dharok_hidden_starts, dharok_hidden_low;
/** One start as the training draws it, on a fresh kit and opponent; checks the state against the row and tallies what the state shows. */
static void draw_and_check(Env* e, int rematch, Tally* t, long* seats) {
    RiskfightState* s = &e->state;
    RiskfightContext* c = &e->context;
    RiskfightTraining* tr = &e->training;
    riskfight_training_draw_opponent(tr, c, e->tag);
    riskfight_reset((EncounterState*)s, (EncounterContext*)c, 0);
    riskfight_training_new_opponent(tr, s, c, 0);
    /* What the kit holds before the start touches it. */
    Player kit_before[2] = {s->env.players[0], s->env.players[1]};
    RiskfightStartDraw draw = riskfight_training_start_fight(tr, s, c, rematch);
    const RiskfightStartRecord* r = draw.row;
    assert(r && r->rematch == rematch);
    const int us = draw.us, him = 1 - us;
    assert(us == 0 || (c->self_play && !c->opp_driven));
    seats[us]++;
    const Player* mine = &s->env.players[us];
    const Player* his = &s->env.players[him];
    assert(s->env.tick == 1 && !s->env.episode_over);

    /* Ours, exactly. */
    assert(mine->current_hitpoints == r->hp);
    assert(mine->current_attack == r->atk && mine->current_strength == r->str && mine->current_defence == r->df && mine->current_magic == r->mag);
    assert(mine->current_prayer == r->pray);
    assert(mine->special_energy == r->e_spec && mine->item_effect_state.special_regen_ticks == r->e_phase && mine->spec_regen_active == (r->e_spec < 100));
    assert(mine->veng_active == r->veng && mine->veng_cooldown == r->vcd);
    assert(s->inventory_use[us].vengeance_sacks == r->sacks);
    /* Supplies: the kit less the deficits, at most what it carries. */
    assert(count_kind(mine, OSRS_CONSUMABLE_ANGLERFISH) == count_kind(&kit_before[us], OSRS_CONSUMABLE_ANGLERFISH) - min_int(r->d_angler, count_kind(&kit_before[us], OSRS_CONSUMABLE_ANGLERFISH)));
    assert(count_kind(mine, OSRS_CONSUMABLE_KARAMBWAN) == count_kind(&kit_before[us], OSRS_CONSUMABLE_KARAMBWAN) - min_int(r->d_karam, count_kind(&kit_before[us], OSRS_CONSUMABLE_KARAMBWAN)));
    assert(doses_of(mine, OSRS_CONSUMABLE_BREW) == doses_of(&kit_before[us], OSRS_CONSUMABLE_BREW) - min_int(r->d_brew, doses_of(&kit_before[us], OSRS_CONSUMABLE_BREW)));
    assert(doses_of(mine, OSRS_CONSUMABLE_SUPER_RESTORE) == doses_of(&kit_before[us], OSRS_CONSUMABLE_SUPER_RESTORE) - min_int(r->d_restore, doses_of(&kit_before[us], OSRS_CONSUMABLE_SUPER_RESTORE)));
    assert(doses_of(mine, OSRS_CONSUMABLE_SUPER_COMBAT) == doses_of(&kit_before[us], OSRS_CONSUMABLE_SUPER_COMBAT) - min_int(r->d_combat, doses_of(&kit_before[us], OSRS_CONSUMABLE_SUPER_COMBAT)));
    assert(count_kind(mine, OSRS_CONSUMABLE_TELEPORT) == count_kind(&kit_before[us], OSRS_CONSUMABLE_TELEPORT) - min_int(r->d_tab, count_kind(&kit_before[us], OSRS_CONSUMABLE_TELEPORT)));
    /* His: inside his bar, the tape's estimates, the kit's supplies. A Dharok pool opponent with no bar in the row starts where spectated Dharok fighters do
     * (riskfight_start_dharok_hitpoints: a regear's 99-121 or a measured ten below 99), and the bar he shows follows it. */
    OsrsHealthBarRange range = osrs_health_bar_range(r->bar, OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 121);
    const int hurt_kit = kit_before[him].current_hitpoints < 99;
    const int dharok_hidden = !hurt_kit && range.kind == OSRS_HEALTH_BAR_MISSING && him == 1 && riskfight_dharok_opponent(s);
    dharok_hidden_starts += dharok_hidden;
    dharok_hidden_low += dharok_hidden && his->current_hitpoints < 99;
    if (hurt_kit) assert(his->current_hitpoints == kit_before[him].current_hitpoints);
    else if (range.kind == OSRS_HEALTH_BAR_KNOWN && range.upper < 121) assert(his->current_hitpoints >= range.lower && his->current_hitpoints <= range.upper);
    else if (dharok_hidden) assert(his->current_hitpoints >= 1 && his->current_hitpoints <= 121 && (his->current_hitpoints >= 99 || RF_START_DHAROK_LOW[his->current_hitpoints / 10] > 0));
    else assert(his->current_hitpoints >= 99 && his->current_hitpoints <= 121);
    assert(s->visible[us].health_bar == osrs_health_bar_ratio(his->current_hitpoints, his->base_hitpoints, 30));
    if (!hurt_kit && !dharok_hidden) assert(s->visible[us].health_bar == (r->bar >= 0 ? r->bar : OSRS_PLAYER_HEALTH_BAR_SCALE));
    assert(his->special_energy == r->h_spec && his->veng_active == r->h_veng);
    assert(s->visible[us].spec_est == r->h_spec && s->visible[him].spec_est == r->e_spec);
    assert(count_kind(his, OSRS_CONSUMABLE_ANGLERFISH) <= count_kind(&kit_before[him], OSRS_CONSUMABLE_ANGLERFISH));
    assert(his->current_hitpoints > 0);
    /* Between us. */
    assert(his->x - mine->x == r->dist && his->y == mine->y);
    /* Who swung: the sim's own bookkeeping on the viewer's side (a maul's instant special is no swing but counts as a special). */
    assert((s->visible[him].last_attack_tick == 0 || s->specs_used[us] > 0) == (r->opener_us != 0));
    assert((s->visible[us].last_attack_tick == 0 || s->specs_used[him] > 0) == (r->opener_him != 0));
    /* The hits in flight, with the measured final damage. */
    int in_flight[2] = {0, 0};
    for (int seat = 0; seat < 2; seat++) {
        const Player* attacker = &s->env.players[seat];
        const int role = seat == us ? 0 : 1;
        const int direct = role == 0 ? r->fl_him_direct : r->fl_us_direct; /* the attacker's hit lands on the other */
        const int veng = role == 0 ? r->fl_him_veng : r->fl_us_veng, recoil = role == 0 ? r->fl_him_recoil : r->fl_us_recoil;
        int queued = 0;
        for (int i = 0; i < attacker->num_pending_hits; i++) {
            const PendingHit* h = &attacker->pending_hits[i];
            assert(h->ticks_until_hit == 0);
            queued++;
            if (h->kind == OSRS_HIT_DIRECT) { assert(direct >= 0 && h->damage == direct && s->env.pending_hit_metadata[seat][i].damage_mode == 1); }
            else if (h->kind == OSRS_HIT_VENGEANCE) assert(veng >= 0 && h->damage == veng);
            else { assert(h->kind == OSRS_HIT_RECOIL && recoil >= 0 && h->damage == recoil); }
        }
        assert(queued == (direct >= 0) + (veng >= 0) + (recoil >= 0));
        in_flight[role] = queued > 0;
    }
    /* What landed on the first tick: the lock against an overheal and the landed flag the history shows. */
    assert((mine->pvp_overheal_block > 0) == (r->landed_us >= 0) && (his->pvp_overheal_block > 0) == (r->landed_him >= 0));

    t->n++;
    t->hp121 += mine->current_hitpoints == 121; t->hp120 += mine->current_hitpoints == 120; t->below_120 += mine->current_hitpoints < 120;
    const int swung_us = s->visible[him].last_attack_tick == 0 || s->specs_used[us] > 0, swung_him = s->visible[us].last_attack_tick == 0 || s->specs_used[him] > 0;
    t->opened_us += swung_us && !swung_him; t->opened_him += swung_him && !swung_us; t->opened_both += swung_us && swung_him;
    t->bar_partial += r->bar >= 0 && r->bar < 30; /* the shown bar is the row's: asserted above */ t->bar_hidden += r->bar < 0; t->hurt += hurt_kit;
    t->spec_him += his->special_energy < 100; t->spec_us += mine->special_energy < 100;
    t->veng_us += mine->veng_active; t->veng_him += his->veng_active;
    t->dist1 += his->x - mine->x == 1;
    t->food_short += r->d_angler > 0;
    t->hit_in_flight_us += in_flight[0]; t->hit_in_flight_him += in_flight[1];
}

/** The expected share of each tallied quantity, from the rows of one pool. */
static void expect(int rematch, double* out) {
    double n = 0;
    for (int k = 0; k < 14; k++) out[k] = 0;
    for (int i = 0; i < RF_START_RECORD_COUNT; i++) {
        const RiskfightStartRecord* r = &RF_START_RECORDS[i];
        if (r->rematch != rematch) continue;
        n++;
        out[0] += r->hp == 121; out[1] += r->hp == 120; out[2] += r->hp < 120;
        out[3] += r->opener_us && !r->opener_him; out[4] += r->opener_him && !r->opener_us; out[5] += r->opener_us && r->opener_him;
        out[6] += r->bar >= 0 && r->bar < 30; out[7] += r->bar < 0; out[8] += r->h_spec < 100; out[9] += r->e_spec < 100;
        out[10] += r->veng; out[11] += r->h_veng; out[12] += r->dist == 1; out[13] += r->d_angler > 0;
    }
    for (int k = 0; k < 14; k++) out[k] /= n;
}

static void test_marginals_and_mapping(void) {
    /* The bot-eval env of the sweep (one seat, the opponent a script or OppFit): the user is seat 0. 12000 draws of each pool across kits, pool opponents and randomization. */
    for (int rematch = 0; rematch < 2; rematch++) {
        Env* e = make_env(0, 1, 0, 1, 11 + (unsigned)rematch);
        Tally t = {0};
        long seats[2] = {0, 0};
        const int draws = 12000;
        for (int i = 0; i < draws; i++) draw_and_check(e, rematch, &t, seats);
        assert(seats[0] == draws && seats[1] == 0);
        double p[14];
        expect(rematch, p);
        const double n = (double)t.n;
        const long got[14] = {t.hp121, t.hp120, t.below_120, t.opened_us, t.opened_him, t.opened_both, t.bar_partial, t.bar_hidden, t.spec_him, t.spec_us, t.veng_us, t.veng_him, t.dist1, t.food_short};
        const char* names[14] = {"hp 121", "hp 120", "hp < 120", "we opened", "he opened", "both", "bar partial", "bar hidden", "his spec used", "our spec used", "our veng up", "his veng up", "distance 1", "food used"};
        for (int k = 0; k < 14; k++) {
            /* A row is drawn uniformly, so a count is binomial with the row share: four and a half sigma. */
            assert(within((double)got[k], n, p[k]));
            printf("  %s %-14s %.3f (measured %.3f)\n", rematch ? "rematch" : "first  ", names[k], got[k] / n, p[k]);
        }
        /* Both the bar and the hitpoints are one function of the row: a shown bar below full is the measured one. */
        destroy(e);
    }
    /* The bot-eval pool holds Dharok kits, so some hidden-bar starts are a Dharok opponent's, and some of those start below 99. */
    assert(dharok_hidden_starts > 0 && dharok_hidden_low > 0);
    printf("marginals: 12000 starts of each pool reproduce the measured marginals (4.5 sigma); %ld Dharok hidden-bar starts, %ld below 99\n", dharok_hidden_starts, dharok_hidden_low);
}

/** Self-play: the user's part is either seat when seat 1 is a net, seat 0 when the script or OppFit plays him. */
static void test_roles(void) {
    Env* e = make_env(1, 1, 0, 1, 31);
    long seats[2] = {0, 0}, net_seats[2] = {0, 0};
    Tally t = {0};
    const int draws = 6000;
    for (int i = 0; i < draws; i++) {
        const long before[2] = {seats[0], seats[1]};
        draw_and_check(e, 0, &t, seats);
        if (e->context.self_play && !e->context.opp_driven) { net_seats[0] += seats[0] - before[0]; net_seats[1] += seats[1] - before[1]; }
        else assert(seats[1] == before[1]); /* the script or OppFit in seat 1: the user's part is seat 0 */
    }
    assert(net_seats[0] + net_seats[1] > 1000 && within((double)net_seats[1], (double)(net_seats[0] + net_seats[1]), 0.5));
    destroy(e);
    printf("roles: user's part in seat 0/1 with a net opponent: %ld/%ld; a driven opponent never takes it\n", net_seats[0], net_seats[1]);
}

/** Sessions that begin and go on with starts drawn: random actions, no abort, the first fight starts at tick 1 with a swing, later fights too. */
static void test_sessions_run(void) {
    for (int self_play = 0; self_play < 2; self_play++) {
        Env* e = make_env(self_play, 1, 0, 1, 51 + (unsigned)self_play);
        uint32_t rng = 12345u + (unsigned)self_play;
        long fights_seen = 0, steps = 0;
        int last_fights = 0;
        for (int step = 0; step < 60000; step++) {
            /* A fight just began when the session counter moved or a session was reset: its state is a start. */
            if (e->wait_left == 0 && e->state.env.tick == 1 && !e->state.env.episode_over) {
                fights_seen++;
                assert(e->training.start == RF_START_FRESH);
            }
            for (int seat = 0; seat < 2; seat++) {
                const unsigned char* mask = masks[seat];
                int offset = 0;
                for (int head = 0; head < RF_HEADS; head++) {
                    int pick = 0, tries = 0;
                    do { rng = rng * 1664525u + 1013904223u; pick = (int)((rng >> 8) % (unsigned)RF_ACTION_DIMS[head]); } while (!mask[offset + pick] && ++tries < 64);
                    actions[seat][head] = (float)(mask[offset + pick] ? pick : 0);
                    offset += RF_ACTION_DIMS[head];
                }
            }
            puf_step(e);
            steps++;
            last_fights = e->session_fights;
        }
        /* Random play teleports rudely, and the reputation rules then stretch the searches, so the count of fights depends on those rules; 20 is enough
         * starts to exercise the draw. */
        assert(fights_seen > 20 && last_fights >= 0);
        destroy(e);
        printf("sessions: self_play %d, %ld steps, %ld fights began from a start without an abort\n", self_play, steps, fights_seen);
    }
}

/** The price: the live table when asked for, the wide draw otherwise. */
static void test_price(void) {
    const int draws = 20000;
    double table_sum = 0; long at_low = 0, at_high = 0, off_table = 0;
    Env* e = make_env(0, 0, 0, 1, 61);
    for (int i = 0; i < draws; i++) {
        riskfight_training_draw_opponent(&e->training, &e->context, e->tag);
        riskfight_reset((EncounterState*)&e->state, (EncounterContext*)&e->context, 0);
        riskfight_training_new_opponent(&e->training, &e->state, &e->context, 0);
        const float price = e->state.death_price;
        int on_table = 0;
        for (int k = 0; k < RF_PRICE_TABLE_COUNT; k++) on_table |= RF_PRICE_TABLE[k] == price;
        off_table += !on_table;
        table_sum += price; at_low += price <= 0.1f + 1e-6f; at_high += price >= 2.5f - 1e-6f;
    }
    destroy(e);
    double mean = 0; long low = 0, high = 0;
    for (int k = 0; k < RF_PRICE_TABLE_COUNT; k++) { mean += RF_PRICE_TABLE[k]; low += RF_PRICE_TABLE[k] <= 0.1f + 1e-6f; high += RF_PRICE_TABLE[k] >= 2.5f - 1e-6f; }
    mean /= RF_PRICE_TABLE_COUNT;
    assert(off_table == 0);
    assert(within((double)at_low, draws, (double)low / RF_PRICE_TABLE_COUNT) && within((double)at_high, draws, (double)high / RF_PRICE_TABLE_COUNT));
    assert(fabs(table_sum / draws - mean) < 0.05);
    printf("price table: mean %.3f (table %.3f), at 0.1 %.3f (%.3f), at 2.5 %.3f (%.3f), none off the table\n", table_sum / draws, mean, (double)at_low / draws,
        (double)low / RF_PRICE_TABLE_COUNT, (double)at_high / draws, (double)high / RF_PRICE_TABLE_COUNT);

    /* Without the table the draw stays log-uniform in [0.4, 2.5]. */
    e = make_env(0, 0, 0, 0, 62);
    float lo = 9, hi = 0;
    for (int i = 0; i < 4000; i++) {
        riskfight_training_draw_opponent(&e->training, &e->context, e->tag);
        riskfight_reset((EncounterState*)&e->state, (EncounterContext*)&e->context, 0);
        riskfight_training_new_opponent(&e->training, &e->state, &e->context, 0);
        lo = fminf(lo, e->state.death_price); hi = fmaxf(hi, e->state.death_price);
    }
    destroy(e);
    assert(lo >= 0.4f - 1e-4f && hi <= 2.5f + 1e-4f && lo < 0.45f && hi > 2.4f);
    printf("price wide draw: %.3f .. %.3f\n", lo, hi);
}

/** The same seed draws the same starts: two envs, the observation of every fresh session compared bit for bit, over sessions that play on. */
static void test_deterministic(void) {
    static float obs_a[RF_OBS_SIZE], obs_b[RF_OBS_SIZE];
    Env* a = make_env(1, 1, 0, 1, 81);
    Env* b = make_env(1, 1, 0, 1, 81);
    a->agents[0].observations = obs_a;
    b->agents[0].observations = obs_b;
    for (int session = 0; session < 300; session++) {
        puf_reset(a);
        puf_reset(b);
        assert(!memcmp(obs_a, obs_b, sizeof(obs_a)));
        assert(a->state.env.players[0].current_hitpoints == b->state.env.players[0].current_hitpoints && a->state.death_price == b->state.death_price);
        for (int step = 0; step < 5; step++) {
            puf_step(a);
            puf_step(b);
            assert(!memcmp(obs_a, obs_b, sizeof(obs_a)));
        }
    }
    destroy(a);
    destroy(b);
    puts("deterministic: the same seed draws the same starts");
}

/** Off by default: the regear. */
static void test_off(void) {
    Env* e = make_env(0, 0, 0, 0, 71);
    for (int i = 0; i < 50; i++) {
        riskfight_training_draw_opponent(&e->training, &e->context, e->tag);
        riskfight_reset((EncounterState*)&e->state, (EncounterContext*)&e->context, 0);
        riskfight_training_new_opponent(&e->training, &e->state, &e->context, 0);
        RiskfightStartDraw draw = riskfight_training_start_fight(&e->training, &e->state, &e->context, 0);
        assert(draw.row == NULL && e->state.env.tick == 0 && e->state.env.players[0].current_hitpoints == 121);
        assert(e->state.visible[0].last_attack_tick == -1 && e->state.visible[1].last_attack_tick == -1);
    }
    destroy(e);
    puts("off: without fight_starts a fight is the regear");
}

int main(void) {
    test_table();
    test_marginals_and_mapping();
    test_roles();
    test_price();
    test_off();
    test_deterministic();
    test_sessions_run();
    puts("riskfight starts passed");
    return 0;
}
