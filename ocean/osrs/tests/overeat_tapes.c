/* Over-eating of a recorded fighter against the sim's chance range (riskfight_chance_range): the sim's own definition read off recorded request streams.
 *
 *   cc -std=gnu11 -O2 -I. -Isrc -Wno-comment -o /tmp/overeat_tapes ocean/osrs/tests/overeat_tapes.c -lm -lpthread       (repository root; run with cwd = repository root)
 *   /tmp/overeat_tapes REQUESTS...            (scripts/measure_overeats.py builds it, writes the request streams and aggregates the rows)
 *
 * Every request file is replayed through the live server's own path (rt_parse, rt_decide with no net: the visible-opponent history, his swing slots of the threat block, his
 * estimated special energy and his Vengeance as the V lines show them). After each tick's state is built one row is written, tab separated:
 *   file fight tick hp range normal spec echo veng_tie veng_strict spec_est wielded his_meal ours_meals_eat ours_meals_brew ours_unclassified bar_lo bar_hi
 *   our_max our_spec_cost our_spec_max our_energy his_max our_veng
 * hp: our exact hitpoints this tick; range: riskfight_chance_range(&g_base.state, 0), asserted equal to the harness's own recomputation; normal: the largest normal max hit
 * of the weapons he wields or has swung (no special, no echo); spec: what the special energy estimate adds on top of normal; echo: what his Vengeance adds on top of both,
 * with the sim's test (`last_cast_tick >= last_proc_tick`: veng_tie, true at fight start when no cast and no proc was seen) and the strict test (veng_strict: cast after
 * proc); his_meal: he ate or drank this tick (V consuming); ours_meals_*: slots of our bag that held food/karambwan/pie (eat) or a brew whose content changed since the
 * previous tick of the same fight (what we ate during this tick); ours_unclassified: slots that changed from an item the content table does not know; bar_lo/bar_hi: the
 * hitpoints his health bar allows; our_max, our_spec_cost (energy), our_spec_max, our_energy and his_max: the other side's view for his meals (our wielded weapon against
 * his visible defence). The range of row N is what a meal executed during tick N + 1 is judged against, with the hitpoints of row N (the start of tick N + 1). */
#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"

static int slot_code(int raw) { return raw < 0 ? 0 : (int)raw_id_to_content((uint16_t)canonical(raw)); }

static int is_meal_code(int code) {
    if (code == 0) return 0;
    const OsrsItemContentMetadata* m = osrs_item_content_metadata((uint16_t)code);
    return m->click_action == OSRS_CLICK_EAT || m->consumable_kind == OSRS_CONSUMABLE_BREW;
}

int main(int argc, char** argv) {
    g_fight.net = NULL;
    g_fight.rng = 0x9e3779b9u;
    g_fight.hard = 1;
    riskfight_deployable = 1;
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99; g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    observer_register_items();
    sim_init(&g_base);
    static Request q;
    for (int a = 1; a < argc; a++) {
        FILE* in = fopen(argv[a], "r");
        if (!in) { perror(argv[a]); return 2; }
        RtSeen seen;
        int new_fight, fight_start, obs_only, fight_index = -1, last_start = -1, last_tick = -1000;
        int prev_raw[TAPE_BAG], prev_code[TAPE_BAG];
        while (rt_parse(in, &q, &seen, &new_fight, &fight_start, &obs_only)) {
            request_declare_priors(&q);
            const char* unavailable = server_unavailable(&q);
            if (unavailable) { /* the live server answers UNAVAILABLE and decides nothing on this tick: no row, counted by the caller */
                fprintf(stderr, "UNAVAILABLE %s %d %s\n", argv[a], q.tick, unavailable);
                continue;
            }
            RtRecord records[GS_WIRE_MAX_RECORDS];
            int count;
            rt_decide(&q, &seen, new_fight, fight_start, records, &count);
            RiskfightState* s = &g_base.state;
            const RiskfightVisibleOpponent* v = &s->visible[0];
            int same_fight = g_fight.start_tick == last_start && q.tick == last_tick + 1;
            if (g_fight.start_tick != last_start) fight_index++;
            RiskfightBagWeapons none = {{0}, 0};
            float threat[RF_THREAT_SIZE], bare[RF_THREAT_SIZE];
            riskfight_threat_write(&s->env.players[0], &none, &v->threat, v->health_bar, threat);
            memcpy(bare, threat, sizeof(bare));
            for (int i = 0; i < RF_THREAT_HIS_SLOTS; i++) bare[i * RF_THREAT_HIS_WIDTH + 9] = 0;
            int tie = v->last_cast_tick >= v->last_proc_tick, strict = v->last_cast_tick > v->last_proc_tick;
            int range = riskfight_chance_range(s, 0);
            assert(range == riskfight_chance_range_of(threat, v->spec_est, tie));
            int normal = riskfight_chance_range_of(bare, v->spec_est, 0);
            int noecho = riskfight_chance_range_of(threat, v->spec_est, 0);
            int echo = range - noecho;
            int wielded = 0;
            int his_max = 0;
            for (int i = 0; i < RF_THREAT_HIS_SLOTS; i++) {
                const float* o = threat + i * RF_THREAT_HIS_WIDTH;
                if (o[0] >= 1) { wielded++; int h = (int)lroundf(o[2] * RF_THREAT_DAMAGE_SCALE); if (h > his_max) his_max = h; }
            }
            const float* ours = threat + RF_THREAT_OURS_OFFSET;
            int our_max = (int)lroundf(ours[1] * RF_THREAT_DAMAGE_SCALE);
            int our_spec_cost = (int)lroundf(ours[4] * 100), our_spec_max = (int)lroundf(ours[5] * RF_THREAT_DAMAGE_SCALE);
            OsrsHealthBarRange bar = osrs_health_bar_range(v->health_bar, OSRS_PLAYER_HEALTH_BAR_SCALE, RF_THREAT_BASE_HITPOINTS, RF_THREAT_OVERHEAL_CAP);
            int bar_lo = bar.kind == OSRS_HEALTH_BAR_KNOWN ? bar.lower : 0, bar_hi = bar.kind == OSRS_HEALTH_BAR_KNOWN ? bar.upper : RF_THREAT_OVERHEAL_CAP;
            int eat = 0, brew = 0, unclassified = 0;
            for (int slot = 0; slot < TAPE_BAG; slot++) {
                int code = slot_code(q.bag[slot]);
                if (same_fight && q.bag[slot] != prev_raw[slot]) {
                    if (is_meal_code(prev_code[slot])) {
                        const OsrsItemContentMetadata* m = osrs_item_content_metadata((uint16_t)prev_code[slot]);
                        if (m->click_action == OSRS_CLICK_EAT) eat++; else brew++;
                    } else if (prev_raw[slot] >= 0 && prev_code[slot] == 0) unclassified++;
                }
                prev_raw[slot] = q.bag[slot];
                prev_code[slot] = code;
            }
            printf("%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", argv[a], fight_index, q.tick, s->env.players[0].current_hitpoints,
                range, normal, noecho - normal, echo, tie, strict, v->spec_est, wielded, seen.consuming, eat, brew, unclassified, bar_lo, bar_hi, our_max, our_spec_cost,
                our_spec_max, q.ours.spec_us, his_max, q.ours.veng_us);
            last_start = g_fight.start_tick;
            last_tick = q.tick;
        }
        fclose(in);
    }
    return 0;
}
