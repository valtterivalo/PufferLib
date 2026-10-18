/* Seat 1's escapes, a net under data_escapes against the script itself: the per-tick teleport rate of seat 1 by the cells of the scripts' escape rule (a Dharok kit:
 * RF_DHAROK_TAB_PM's hitpoint bands; any other kit: tab_pm's supply band and hitpoints below 40), over ticks it holds a usable tab.
 *   net     pure self-play (tag 0, our kits, data_escapes 1): seat 1's own teleport masked, it leaves by riskfight_data_escape
 *   script  a frozen-tagged env whose seat 1 is the script in pool kits (scripted_probability 1)
 * Both seats play the script with its teleport taken out (the net's actions), so seat 1 leaves only by the escape rule (the script: its own rule).
 *
 *   probe_riskfight_escapes [fights]   (build as probe_riskfight_seats.c) */
#include "riskfight_env_harness.h"
#include <stdio.h>

enum { ENVS = 64, CELLS = 4 + RISKFIGHT_TAB_SUPPLY_BANDS * 2 + 1 }; /* the last cell: below 30 hitpoints out of heals, where the rule leaves at once */
typedef struct { double ticks[CELLS], escapes[CELLS], fights, offered, chosen, predicted, mismatched; } Tally;

/** The cell of seat 1's tick for the escape rule, -1 without a usable tab. */
static int escape_cell(const RiskfightState* s, int dharok) {
    const Player* p = &s->env.players[1];
    if (!osrs_teleport_allowed(s->env.pvp_runtime.teleport[1], s->env.tick)) return -1;
    int tab = 0, heals = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        OsrsConsumableKind k = (OsrsConsumableKind)osrs_inventory_cell_metadata(&p->inventory_cells[slot])->consumable_kind;
        tab |= k == OSRS_CONSUMABLE_TELEPORT;
        heals |= k == OSRS_CONSUMABLE_ANGLERFISH || k == OSRS_CONSUMABLE_KARAMBWAN || k == OSRS_CONSUMABLE_MARLIN || k == OSRS_CONSUMABLE_SUMMER_PIE ||
            k == OSRS_CONSUMABLE_HALIBUT || k == OSRS_CONSUMABLE_BREW;
    }
    if (!tab) return -1;
    const int hp = p->current_hitpoints;
    if (hp < 30 && !heals) return CELLS - 1;
    return dharok ? min_int(3, hp / 20) : 4 + 2 * min_int(s->consume_ticks[1] / RISKFIGHT_TAB_SUPPLY_TICKS, RISKFIGHT_TAB_SUPPLY_BANDS - 1) + (hp < 40);
}

static void run(int net, int fights, Tally* t) {
    DictItem items[] = {
        {.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1}, {.key = "live_loadout", .value = 4}, {.key = "opponent_pool", .value = 1},
        {.key = "selfplay_pool_probability", .value = 0}, {.key = "deployable", .value = 1}, {.key = "motor", .value = 1}, {.key = "death_price", .value = 0},
        {.key = "oppfit_probability", .value = 0}, {.key = "domain_randomization", .value = getenv("NO_DR") ? 0 : 1}, {.key = "scripted_probability", .value = net ? 0 : 1},
        {.key = "scripted_omission_initial", .value = 0}, {.key = "fight_starts", .value = 1}, {.key = "session_ticks", .value = 3000},
        {.key = "curveball_probability", .value = 0}, {.key = "rep_start_probability", .value = 0}, {.key = "data_escapes", .value = 1}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    static Env envs[ENVS];
    static float obs[ENVS][2][RF_OBS_SIZE], act[ENVS][2][RF_HEADS], rew[ENVS][2], term[ENVS][2], res[ENVS][2];
    static unsigned char mask[ENVS][2][RF_MASK_SIZE];
    memset(t, 0, sizeof(*t));
    for (int i = 0; i < ENVS; i++) {
        memset(&envs[i], 0, sizeof(envs[i]));
        envs[i].rng = 5000u + (unsigned)i;
        puf_init(&envs[i], &kwargs);
        envs[i].tag = !net;
        for (int s = 0; s < 2; s++) envs[i].agents[s] = (Agent){.observations = (obs_t*)obs[i][s], .actions = act[i][s], .rewards = &rew[i][s],
            .terminals = &term[i][s], .resets = &res[i][s], .action_mask = mask[i][s], .policy = 0};
        puf_reset(&envs[i]);
    }
    while (t->fights < fights) {
        for (int i = 0; i < ENVS; i++) {
            Env* e = &envs[i];
            RiskfightState* s = &e->state;
            int cell = -1;
            if (e->wait_left == 0) {
                for (int seat = 0; seat < 2; seat++) {
                    float o[RF_OBS_SIZE];
                    int a[RF_HEADS];
                    riskfight_write_observation(s, seat, o);
                    riskfight_script(o, &RISKFIGHT_HUMAN_PROFILES[i % RISKFIGHT_HUMAN_TRAIN_PROFILES], s->script_seed[seat], s->consume_ticks[seat], a);
                    if (a[RF_PRIMARY] == RF_TELEPORT) a[RF_PRIMARY] = RF_ATTACK;
                    const unsigned char* m = mask[i][seat];
                    for (int h = 0; h < RF_HEADS; h++) act[i][seat][h] = m[RF_HEAD_OFFSET[h] + a[h]] ? (float)a[h] : 0.0f;
                }
                if (net) {
                    assert(s->data_escapes[1] != RF_DATA_ESCAPES_OFF);
                    t->offered += mask[i][1][RF_HEAD_OFFSET[RF_PRIMARY] + RF_TELEPORT];
                    t->chosen += act[i][1][RF_PRIMARY] == RF_TELEPORT;
                    t->predicted += riskfight_data_escape(s, 1);
                }
                cell = escape_cell(s, net ? s->data_escapes[1] == RF_DATA_ESCAPES_DHAROK : riskfight_dharok_opponent(s));
            }
            const int predicted = net && e->wait_left == 0 && riskfight_data_escape(s, 1);
            const float tp = e->log.opponent_teleports, annoyed = e->log.annoyed_exits, fought = e->log.fights_selfplay + e->log.fights_script;
            puf_step(e);
            const int escaped = e->log.opponent_teleports - tp > e->log.annoyed_exits - annoyed;
            t->mismatched += net && e->log.fights_selfplay + e->log.fights_script > fought && predicted != escaped;
            if (cell >= 0) { t->ticks[cell]++; t->escapes[cell] += escaped; }
            t->fights += e->log.fights_selfplay + e->log.fights_script - fought;
        }
    }
    for (int i = 0; i < ENVS; i++) puf_close(&envs[i]);
}

int main(int argc, char** argv) {
    const int fights = argc > 1 ? atoi(argv[1]) : 20000;
    Tally tally[2];
    run(1, fights, &tally[0]);
    run(0, fights, &tally[1]);
    printf("net: teleport offered %.0f chosen %.0f, escapes predicted %.0f, fight ends that disagree with the prediction %.0f\n",
        tally[0].offered, tally[0].chosen, tally[0].predicted, tally[0].mismatched);
    for (int c = 0; c < CELLS; c++) {
        if (c == CELLS - 1) printf("hp<30 out of heals  ");
        else printf(c < 4 ? "dharok hp %2d-%-3d " : "supply %d hp<40 %d   ", c < 4 ? c * 20 : (c - 4) / 2, c < 4 ? c * 20 + 19 : (c - 4) % 2);
        for (int m = 0; m < 2; m++) {
            const Tally* t = &tally[m];
            const double p = t->ticks[c] ? t->escapes[c] / t->ticks[c] : 0;
            printf("  %s %7.0f ticks %5.0f esc %6.2f pm (+-%.2f)", m ? "script" : "net", t->ticks[c], t->escapes[c], 1000 * p,
                t->ticks[c] ? 1000 * sqrt(p * (1 - p) / t->ticks[c]) : 0);
        }
        printf("\n");
    }
    for (int m = 0; m < 2; m++) printf("%s fights %.0f\n", m ? "script" : "net", tally[m].fights);
    return 0;
}
