/* Replays an engine trace (light.engine.trace: <run>.requests) through gs_rl_server's exact decision path and prints, per request,
 * what the net saw and how sure it was of each deployable head: the live decisions of a session, reproduced in milliseconds.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/rl_explain ocean/osrs/tests/rl_explain.c -lm
 *   /tmp/rl_explain CHECKPOINT HIDDEN LAYERS RUN.requests [FROM [TO]]
 *   /tmp/rl_explain - 0 0 RUN.requests [FROM [TO]]      no net: only the observation each request produces, its threat block printed (what the net would be given)
 *
 * Run from a directory that resolves ocean/osrs/data (the repository root or ~/.light2/engine-release/home). */
#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"

static const char* rx_item(int item) { return item == ITEM_NONE ? "-" : ITEM_DATABASE[item].name; }
/* The slot's item, or its consumable kind for food and potions (their cells carry no item row), with the slot number. */
static const char* rx_cell(const Player* p, int slot) {
    static char text[4][64];
    static int turn;
    char* out = text[turn++ & 3];
    const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
    if (m->item_idx != ITEM_NONE) snprintf(out, 64, "%s@%d", rx_item(m->item_idx), slot);
    else snprintf(out, 64, "consumable%d@%d", m->consumable_kind, slot);
    return out;
}

/* Masked softmax of one head into p; returns the number of allowed actions. */
static int rx_probs(const RfNet* net, const unsigned char* mask, int head, float* p) {
    int at = RF_HEAD_OFFSET[head], n = RF_ACTION_DIMS[head], allowed = 0;
    float top = -1e30f, total = 0;
    for (int i = 0; i < n; i++) if (mask[at + i] && net->logits[at + i] > top) top = net->logits[at + i];
    for (int i = 0; i < n; i++) { p[i] = mask[at + i] ? expf(net->logits[at + i] - top) : 0; total += p[i]; allowed += mask[at + i]; }
    for (int i = 0; i < n; i++) p[i] /= total;
    return allowed;
}

/* A gear or meal head: the argmax with its probability, the summed probability of using the head at all (any slot), and of none and unequip. */
static void rx_gear_head(const RfNet* net, const unsigned char* mask, const Player* p, int head, const char* label, int chosen) {
    float pr[512];
    rx_probs(net, mask, head, pr);
    if (!chosen && pr[0] > 0.9f) return;
    int off = RF_GEAR_SLOT_BY_HEAD[head] >= 0 && mask[RF_HEAD_OFFSET[head] + RF_UNEQUIP];
    float use = 1 - pr[0] - (off ? pr[RF_UNEQUIP] : 0);
    printf(" | %s", label);
    if (chosen == RF_UNEQUIP) printf(" OFF");
    else if (chosen) printf(" %s", rx_cell(p, chosen - 1));
    printf(" (use %.2f none %.2f", use, pr[0]);
    if (off) printf(" off %.2f", pr[RF_UNEQUIP]);
    printf(")");
}

/* The threat block of an observation (riskfight_threat.h) as text: his weapon slots, then ours. */
static void rx_threat(const float* obs) {
    const float* t = obs + RF_THREAT_START;
    static const char* STYLE[] = {"?", "melee", "ranged", "magic"};
    for (int i = 0; i < RF_THREAT_HIS_SLOTS; i++) {
        const float* o = t + i * RF_THREAT_HIS_WIDTH;
        if (o[0] == 0) continue;
        int style = o[5] > 0 ? 1 : o[6] > 0 ? 2 : o[7] > 0 ? 3 : 0;
        printf("    his %d p %.2f%s max %.0f hit %.3f speed %.0f %s", i, o[0], o[1] > 0 ? " SHOWN" : "", o[2] * RF_THREAT_DAMAGE_SCALE, o[3], o[4] * RF_THREAT_SPEED_SCALE, STYLE[style]);
        if (o[8] > 0) printf(" | spec cost %.0f max %.0f hit %.3f speed %.0f", o[8] * 100, o[9] * RF_THREAT_DAMAGE_SCALE, o[10], o[11] * RF_THREAT_SPEED_SCALE);
        printf("\n");
    }
    for (int i = 0; i < RF_THREAT_OURS_SLOTS; i++) {
        const float* o = t + RF_THREAT_OURS_OFFSET + i * RF_THREAT_OURS_WIDTH;
        if (o[0] == 0) continue;
        printf("    ours %d max %.0f hit %.3f speed %.0f", i, o[1] * RF_THREAT_DAMAGE_SCALE, o[2], o[3] * RF_THREAT_SPEED_SCALE);
        if (o[4] > 0) printf(" | spec cost %.0f max %.0f hit %.3f", o[4] * 100, o[5] * RF_THREAT_DAMAGE_SCALE, o[6]);
        printf("\n");
    }
}

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: rl_explain CHECKPOINT HIDDEN LAYERS RUN.requests [FROM [TO]]\n"); return 2; }
    int from = argc > 5 ? atoi(argv[5]) : 0, to = argc > 6 ? atoi(argv[6]) : 1 << 30;
    g_fight.net = strcmp(argv[1], "-") ? rf_net_load(argv[1], RF_OBS_SIZE, atoi(argv[2]), atoi(argv[3]), RF_ACTION_DIMS, RF_HEADS) : NULL;
    g_fight.rng = 0x9e3779b9u;
    g_fight.hard = 1;
    riskfight_deployable = 1;
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99; g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    observer_register_items();
    sim_init(&g_base);
    FILE* in = fopen(argv[4], "r");
    if (!in) { perror(argv[4]); return 2; }
    static Request q;
    RtSeen seen;
    int new_fight, fight_start, obs_only;
    static const char* KIND[] = {"slot", "attack", "?2", "veng", "?4", "unequip", "?6", "?7", "?8", "?9", "?10", "?11", "item", "spec", "?14", "?15", "?16", "stop"};
    while (rt_parse(in, &q, &seen, &new_fight, &fight_start, &obs_only)) {
        request_declare_priors(&q);
        const char* unavailable = server_unavailable(&q);
        if (unavailable) { if (q.tick >= from && q.tick <= to) printf("%d UNAVAILABLE %s\n", q.tick, unavailable); continue; }
        RtRecord records[GS_WIRE_MAX_RECORDS];
        int count;
        rt_decide(&q, &seen, new_fight, fight_start, records, &count);
        if (q.tick < from || q.tick > to) continue;
        if (!g_fight.net) {
            float obs[RF_OBS_SIZE];
            riskfight_write_observation(&g_base.state, 0, obs);
            printf("%d %s his bar %d\n", q.tick, obs_only ? "OBS" : "DEC", g_fight.vis.health_bar);
            rx_threat(obs);
            continue;
        }
        /* rt_decide leaves the loaded state and the net's logits of this tick behind; the mask is recomputed from that state. */
        const RiskfightState* s = &g_base.state;
        const Player* us = &s->env.players[0];
        unsigned char mask[RF_MASK_SIZE];
        riskfight_write_action_mask(s, 0, mask);
        int actions[RF_HEADS] = {0};
        for (int h = 0; h < RF_HEADS; h++) {
            float pr[512];
            rx_probs(g_fight.net, mask, h, pr);
            for (int i = 1; i < RF_ACTION_DIMS[h]; i++) if (pr[i] > pr[actions[h]]) actions[h] = i;
        }
        riskfight_bag_to_physical(us, actions); /* the net's entries as the slots they click */
        if (new_fight) printf("== FIGHT %d\n", fight_start);
        printf("%d %s hp %d lock %d B %d-%d bar %d atk %d W %s S %s", q.tick, obs_only ? "OBS" : "DEC", us->current_hitpoints, us->pvp_overheal_block, q.belief.hp_low, q.belief.hp_high,
            g_fight.vis.health_bar, osrs_interaction_active(&us->interaction), rx_item(us->equipped[GEAR_SLOT_WEAPON]), rx_item(us->equipped[GEAR_SLOT_SHIELD]));
        OsrsHealthBarRange his_hp = osrs_health_bar_range(g_fight.vis.health_bar, OSRS_PLAYER_HEALTH_BAR_SCALE, RF_THREAT_BASE_HITPOINTS, RF_THREAT_OVERHEAL_CAP);
        printf(" spec us %d him~%d | range %d his hp %d-%d", us->special_energy, g_fight.vis.spec_est, riskfight_chance_range(s, 0),
            his_hp.kind == OSRS_HEALTH_BAR_KNOWN ? his_hp.lower : -1, his_hp.kind == OSRS_HEALTH_BAR_KNOWN ? his_hp.upper : -1);
        {
            /* The session block as the net received it, and the kosher input's predicates (riskfight_norms.h). */
            static float o[RF_OBS_SIZE];
            riskfight_write_observation(s, 0, o);
            const float* sb = o + RF_SESSION_START;
            const OsrsEscapeSupplies own = osrs_escape_supplies(us);
            printf(" | sess rep %.2f annoy %.2f margin %.2f kosher %.0f (no_spec %d no_food %d no_restore %d no_boost %d his_cheapest %d)", sb[4], sb[5], sb[6], sb[7],
                osrs_escape_no_special(&own), osrs_escape_no_hard_food(&own), osrs_escape_no_restore(&own), osrs_escape_no_boost(&own),
                riskfight_threat_cheapest_spec(o + RF_THREAT_START));
        }
        float pr[512];
        rx_probs(g_fight.net, mask, RF_PRIMARY, pr);
        printf(" | prim none %.2f atk %.2f stop %.2f tp %.2f", pr[0], pr[RF_ATTACK], pr[RF_STOP], pr[RF_TELEPORT]);
        rx_gear_head(g_fight.net, mask, us, RF_WEAPON, "weapon", actions[RF_WEAPON]);
        rx_gear_head(g_fight.net, mask, us, RF_SHIELD, "shield", actions[RF_SHIELD]);
        rx_gear_head(g_fight.net, mask, us, RF_RING, "ring", actions[RF_RING]);
        rx_gear_head(g_fight.net, mask, us, RF_FOOD, "food", actions[RF_FOOD]);
        rx_gear_head(g_fight.net, mask, us, RF_DRINK, "drink", actions[RF_DRINK]);
        rx_gear_head(g_fight.net, mask, us, RF_COMBO, "combo", actions[RF_COMBO]);
        rx_probs(g_fight.net, mask, RF_VENGEANCE, pr);
        if (mask[RF_HEAD_OFFSET[RF_VENGEANCE] + 1]) printf(" | veng %.2f", pr[1]);
        rx_probs(g_fight.net, mask, RF_SPECIAL, pr);
        printf(" | spec mask %d%d", mask[RF_HEAD_OFFSET[RF_SPECIAL] + 1], mask[RF_HEAD_OFFSET[RF_SPECIAL] + 2]);
        if (mask[RF_HEAD_OFFSET[RF_SPECIAL] + 1]) printf(" spec1 %.2f spec2 %.2f", pr[1], pr[2]);
        printf(" ->");
        if (!count) printf(" nothing");
        for (int i = 0; i < count; i++) {
            int k = records[i].kind;
            printf(" %s", k < (int)(sizeof(KIND) / sizeof(*KIND)) ? KIND[k] : "?");
            if (k == GS_WIRE_CLICK_SLOT) printf(":%s", rx_cell(us, records[i].arg));
        }
        printf("\n");
    }
    return 0;
}
