/* Throwaway: what the opponent (seat 1) truly does after our hit takes him to <= 20 hitpoints, against the net in seat 0, with the Dharok kit on both sides.
 *   probe_lowvictim CKPT HIDDEN LAYERS SESSIONS seed oppfit_probability
 * Per event (his first such hit of the fight): over the next 6 ticks, the first tick he truly eats (a food, karambwan, pie or brew leaves his bag), whether that meal
 * is shown (riskfight_meal_visible), his food timer at the hit, and whether he died within 10 ticks. Split by the opponent model (script / oppfit). */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include "rf_net.h"
#include <stdio.h>

static int meals_in_bag(const Player* p) {
    int n = 0;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        const OsrsInventoryCell* c = &p->inventory_cells[i];
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(c);
        if (m->click_action == OSRS_CLICK_EAT) n += 1 + (m->consumable_kind == OSRS_CONSUMABLE_SUMMER_PIE ? 0 : 0);
        else if (m->click_action == OSRS_CLICK_DRINK && m->consumable_kind == OSRS_CONSUMABLE_BREW) n += osrs_inventory_cell_dose_count(c);
    }
    return n;
}

int main(int argc, char** argv) {
    if (argc != 7) { fprintf(stderr, "usage: probe_lowvictim CKPT HIDDEN LAYERS SESSIONS seed oppfit_probability\n"); return 2; }
    int sessions = atoi(argv[4]);
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = 3000},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = 0}, {.key = "live_loadout", .value = 5},
        {.key = "deployable", .value = 1}, {.key = "motor", .value = 1}, {.key = "domain_randomization", .value = 1}, {.key = "oppfit_probability", .value = atof(argv[6])},
        {.key = "opponent_pool", .value = 1}, {.key = "fight_starts", .value = 1}, {.key = "price_table", .value = 1}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    unsigned seed = (unsigned)atoi(argv[5]);
    Env* env = (Env*)calloc(1, sizeof(*env));
    env->rng = seed;
    puf_init(env, &kwargs);
    RfNet* net = rf_net_load(argv[1], RF_OBS_SIZE, atoi(argv[2]), atoi(argv[3]), RF_ACTION_DIMS, RF_HEADS);
    static float obs[RF_OBS_SIZE], actions[RF_HEADS], reward, terminal, reset;
    static unsigned char mask[RF_MASK_SIZE];
    env->agents[0] = (Agent){.observations = obs, .actions = actions, .rewards = &reward, .terminals = &terminal, .resets = &reset, .action_mask = mask, .policy = 0};
    uint32_t rng = 0x9e3779b9u ^ seed;
    puf_reset(env);
    rf_net_reset(net);
    /* [model][first true meal tick 1..6, 0 none] , shown, timer>0 at hit, dead in 10 */
    long n[2] = {0}, first[2][8] = {{0}}, shown[2] = {0}, timer[2] = {0}, dead[2] = {0}, tabbed[2] = {0};
    int armed = 0, age = 0, model = 0, got = 0, fight_seen = 0, last_fights = -1;
    long ko_same_meal = 0, ko_meals_sum = 0, ko_nomeal = 0, ko_fewmeal = 0, spec_meals_sum = 0, spec_n = 0, spec_ko = 0, spec_locked = 0, spec_all_locked = 0, ko_locked = 0, ko_all_locked = 0, ko_hp_sum = 0, hp_hist[6] = {0}, hp_ko[6] = {0};
    while (env->log.n < sessions) {
        int chosen[RF_HEADS];
        rf_net_act(net, obs, mask, &rng, 1, RF_ACTION_DIMS, RF_HEADS, chosen);
        for (int h = 0; h < RF_HEADS; h++) actions[h] = (float)chosen[h];
        const int fighting = env->wait_left == 0 && riskfight_loadout_class(env->context.loadout[0]) == RF_CLASS_DHAROK &&
            riskfight_loadout_class(env->context.loadout[1]) == RF_CLASS_DHAROK;
        if (env->session_fights != last_fights) { last_fights = env->session_fights; fight_seen = 0; armed = 0; }
        const Player* him = &env->state.env.players[1];
        const int hp_before = him->current_hitpoints, bag_before = meals_in_bag(him);
        const int timer_before = him->food_timer, him_kara_before = him->karambwan_timer, him_pot_before = him->potion_timer;
        const int spec_before = env->state.specs_used[0];
        const Log before = env->log;
        puf_step(env);
        if (terminal || reset) rf_net_reset(net);
        if (!fighting) continue;
        const Player* h2 = &env->state.env.players[1];
        if (env->state.specs_used[0] > spec_before && hp_before <= 45 && hp_before > 0) {
            spec_n++;
            const int died = h2->current_hitpoints <= 0;
            spec_ko += died;
            const int locked = timer_before > 0, kara = him_kara_before > 0, pot = him_pot_before > 0;
            spec_locked += locked; spec_all_locked += locked && kara && pot;
            if (died) { ko_locked += locked; ko_all_locked += locked && kara && pot; ko_hp_sum += hp_before; ko_meals_sum += bag_before; ko_nomeal += bag_before == 0; ko_fewmeal += bag_before <= 3; ko_same_meal += meals_in_bag(h2) < bag_before; }
            spec_meals_sum += bag_before;
            hp_hist[hp_before / 10]++; if (died) hp_ko[hp_before / 10]++;
        }
        if (armed) {
            age++;
            if (!got && meals_in_bag(h2) < bag_before) { got = age; first[model][age]++; shown[model] += riskfight_meal_visible(&env->state, 1); }
            if (env->log.kills > before.kills && age <= 10) { dead[model]++; armed = 0; if (!got) first[model][0]++; continue; }
            if (env->log.escapes > before.escapes && age <= 10) tabbed[model]++;
            if (age >= 10 || env->session_fights != last_fights) { if (!got) first[model][0]++; armed = 0; }
            continue;
        }
        if (!fight_seen && h2->current_hitpoints > 0 && h2->current_hitpoints <= 20 && hp_before > h2->current_hitpoints) {
            fight_seen = 1; armed = 1; age = 0; got = 0;
            model = env->context.opp_hook != NULL;
            n[model]++;
            timer[model] += timer_before > 1;
        }
    }
    for (int m = 0; m < 2; m++) {
        if (!n[m]) continue;
        printf("%s: events %ld | first true meal by tick +1..+6: ", m ? "oppfit" : "script", n[m]);
        long cum = 0;
        for (int k = 1; k <= 6; k++) { cum += first[m][k]; printf("%.2f ", (double)cum / n[m]); }
        printf("| none in 10 %.2f | shown share of first meals %.2f | food timer > 1 at the hit %.2f | dead in 10 %.2f | tab in 10 %.2f\n",
            (double)first[m][0] / n[m], (double)shown[m] / (double)(n[m] - first[m][0] > 0 ? n[m] - first[m][0] : 1), (double)timer[m] / n[m], (double)dead[m] / n[m],
            (double)tabbed[m] / n[m]);
    }
    printf("our specials at his hp <= 45: %ld, KO on the tick %.3f | food-locked at the spec %.2f (all three timers %.2f) | of the KOs: food-locked %.2f, all three %.2f, mean hp %.1f\n",
        spec_n, (double)spec_ko / (spec_n ? spec_n : 1), (double)spec_locked / (spec_n ? spec_n : 1), (double)spec_all_locked / (spec_n ? spec_n : 1),
        (double)ko_locked / (spec_ko ? spec_ko : 1), (double)ko_all_locked / (spec_ko ? spec_ko : 1), (double)ko_hp_sum / (spec_ko ? spec_ko : 1));
    printf("meals left in his bag at our spec: mean %.1f; at the KOs mean %.1f, none left %.2f, <= 3 left %.2f\n", (double)spec_meals_sum / (spec_n ? spec_n : 1), (double)ko_meals_sum / (spec_ko ? spec_ko : 1), (double)ko_nomeal / (spec_ko ? spec_ko : 1), (double)ko_fewmeal / (spec_ko ? spec_ko : 1));
    printf("KOs where he ate on the same tick (his meal came after our damage): %.2f\n", (double)ko_same_meal / (spec_ko ? spec_ko : 1));
    printf("his true hp at our spec, by 10s 0-9..40-45: ");
    for (int b = 0; b < 5; b++) printf("%ld (KO %.2f) ", hp_hist[b], (double)hp_ko[b] / (hp_hist[b] ? hp_hist[b] : 1));
    printf("\n");
    return 0;
}
