/* The net sees the bag in canonical order (riskfight_bag.h): two physical layouts of the same bag give the same observation and mask, and every allowed bag action
 * of the net clicks a slot holding the same item (content code: item and dose) in both. Bags of 50 sessions (every kit, permuted by training), some doses drunk.
 *   ocean/osrs/tests/run_suite.sh test_riskfight_canonical_bag   (links the OppFit bridge osrs_riskfight.h needs) */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include <assert.h>
#include <stdio.h>

static float observations[2][RF_OBS_SIZE];
static float actions[2][RF_HEADS];
static float rewards[2], terminals[2], resets[2];
static unsigned char masks[2][RF_MASK_SIZE];

/** The content code of the slot every bag action of `actions` (net entries) clicks, through riskfight_bag_to_physical and riskfight_policy_commands as riskfight_step
 * takes them; -1 for a head whose action clicks nothing in the bag. */
static void clicked_codes(const RiskfightState* s, const int* net_actions, int* codes) {
    int physical[RF_HEADS];
    memcpy(physical, net_actions, sizeof(physical));
    riskfight_bag_to_physical(&s->env.players[0], physical);
    HumanInput hi;
    human_input_init(&hi);
    riskfight_policy_commands(s, 0, physical, &hi);
    int clicks = 0;
    for (int i = 0; i < hi.commands.count; i++) {
        const HumanCommand* c = &hi.commands.items[i];
        if (c->kind == HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK) codes[clicks++] = s->env.players[0].inventory_cells[c->inventory_slot].content_code;
    }
    assert(clicks == 1);
    free(hi.commands.items);
}

static void observe(const RiskfightState* s, float* obs, unsigned char* mask) {
    riskfight_write_observation(s, 0, obs);
    riskfight_write_action_mask(s, 0, mask);
}

int main(void) {
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = 3000},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = 0}, {.key = "live_loadout", .value = 3},
        {.key = "opponent_pool", .value = 0}, {.key = "domain_randomization", .value = 0}, {.key = "deployable", .value = 0}, {.key = "fight_starts", .value = 0}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    uint32_t rng = 7;
    int bags = 0, checked = 0;
    for (unsigned seed = 1; seed <= 50; seed++) {
        Env* e = (Env*)calloc(1, sizeof(*e));
        e->rng = seed;
        puf_init(e, &kwargs);
        for (int i = 0; i < 2; i++) e->agents[i] = (Agent){
            .observations = observations[i], .actions = actions[i], .rewards = &rewards[i], .terminals = &terminals[i], .resets = &resets[i], .action_mask = masks[i], .policy = i};
        puf_reset(e);
        RiskfightState* s = &e->state;
        OsrsInventoryCell* cells = s->env.players[0].inventory_cells;
        s->env.players[0].current_hitpoints = 50; /* wounded: the food heads are open */
        /* A dose drunk from some potions, so a bag holds one kind at several doses. */
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot += 3) {
            const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&cells[slot]);
            if (m->click_action == OSRS_CLICK_DRINK && m->dose_count > 1) cells[slot].content_code = m->next_content_code;
        }
        static float obs_a[RF_OBS_SIZE], obs_b[RF_OBS_SIZE];
        static unsigned char mask_a[RF_MASK_SIZE], mask_b[RF_MASK_SIZE];
        OsrsInventoryCell layout_a[OSRS_INVENTORY_SIZE], layout_b[OSRS_INVENTORY_SIZE];
        observe(s, obs_a, mask_a);
        memcpy(layout_a, cells, sizeof(layout_a));
        for (int slot = OSRS_INVENTORY_SIZE - 1; slot > 0; slot--) {
            int other = (int)(xorshift32(&rng) % (uint32_t)(slot + 1));
            OsrsInventoryCell held = cells[slot];
            cells[slot] = cells[other];
            cells[other] = held;
        }
        memcpy(layout_b, cells, sizeof(layout_b));
        assert(memcmp(layout_a, layout_b, sizeof(layout_a)) != 0);
        observe(s, obs_b, mask_b);
        assert(!memcmp(obs_a, obs_b, sizeof(obs_a)));
        assert(!memcmp(mask_a, mask_b, sizeof(mask_a)));
        /* Every allowed bag action of every bag head clicks the same item in both layouts. */
        for (int head = 0; head < RF_HEADS; head++) {
            if (!riskfight_bag_head(head)) continue;
            for (int action = 1; action <= OSRS_INVENTORY_SIZE; action++) {
                if (!mask_a[RF_HEAD_OFFSET[head] + action]) continue;
                int net_actions[RF_HEADS] = {0};
                net_actions[head] = action;
                int code_a, code_b;
                memcpy(cells, layout_a, sizeof(layout_a));
                clicked_codes(s, net_actions, &code_a);
                memcpy(cells, layout_b, sizeof(layout_b));
                clicked_codes(s, net_actions, &code_b);
                assert(code_a == code_b && code_a != 0);
                checked++;
            }
        }
        bags++;
        puf_close(e);
        free(e);
    }
    assert(checked > bags * 5);
    printf("canonical bag: %d bags in two layouts, same observation and mask, %d allowed bag actions click the same item in both\n", bags, checked);
    return 0;
}
