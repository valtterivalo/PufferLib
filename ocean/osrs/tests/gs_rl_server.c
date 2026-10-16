/* The RL decision engine as a process, the `rl` policy of ENGINE_WIRE.md "Swappable decision policy": same transport and response shape as the lookahead server.
 *
 *   cc -std=c11 -O2 -I. -o gs_rl_server ocean/osrs/tests/gs_rl_server.c -lm
 *   gs_rl_server CHECKPOINT HIDDEN LAYERS [sample|argmax]      run from a directory where ocean/osrs/data/*.cmap resolve
 *   gs_rl_server CHECKPOINT HIDDEN LAYERS argmax bench N       per-tick latency over N warm requests, no READY
 *
 * READY <warm_micros> continue;no new clicks;rl POLICY rl:<checkpoint file> OBS
 * Request: `LA id tick [OBS]` ... `END`, the lookahead request (tags B K F O Q W G SHIELD ARMOR PS RA P R; R and Q are ignored) plus the tags
 *   FIGHT start_tick        a line before the first request of a fight: clears the visible-opponent history, the consume estimates and the recurrent state
 *   V swing_raw swing_style hit_landed hit_damage veng_cast consuming bar_ratio bar_scale veng_text
 *       what the client showed of him on this tick. swing_raw: raw id of his weapon when his attack animation started this tick, else -1; swing_style: that
 *       attack's AttackStyle number (0 none, 1 melee, 2 ranged, 3 magic; 0 when swing_raw is -1); hit_landed: 1 when any hitsplat appeared on him this tick
 *       (damage or block, any source); hit_damage: the damage of those hitsplats added up; veng_cast: his Vengeance cast graphic this tick; consuming: a meal or
 *       potion animation of his this tick; bar_ratio/bar_scale: his health bar as displayed now (-1 -1 when hidden); veng_text: "Taste vengeance!" over his
 *       head this tick.
 * Every tick of a fight must reach the server with the full request body: the server owns the history ring, the consume estimates and the recurrent state.
 * A tick gap above 2 or a tick that does not advance starts a new fight. On a decision tick the response is `R id micros 2 <3 x "feasible kill death value worlds">
 * A n <n x "due kind arg timing">` (best_index 2 = replace the remainder with these clicks, due = tick + 1); on an `OBS` tick it is `O id`. UNAVAILABLE as in the
 * lookahead server. Movement, orb, prayer, style and armour-swap heads are masked off (riskfight_deployable). */
#define main gs_lookahead_server_main
#include "gs_lookahead_server.c"
#undef main
#include "rf_net.h"

enum { RT_EVENT_WIDTH = RF_EVENT_WIDTH };

typedef struct {
    RiskfightVisibleOpponent vis;
    int active, start_tick, last_tick;
    RfNet* net;
    uint32_t rng;
    int hard;
} RtFight;

static RtFight g_fight;

typedef struct { int swing_raw, swing_style, hit_landed, hit_damage, veng_cast, consuming, bar_ratio, bar_scale, veng_text; } RtSeen;

static void rt_fight_reset(RtFight* f, int start_tick) {
    RfNet* net = f->net;
    uint32_t rng = f->rng;
    int hard = f->hard;
    memset(f, 0, sizeof(*f));
    f->net = net; f->rng = rng; f->hard = hard;
    f->active = 1;
    f->start_tick = start_tick;
    f->last_tick = start_tick - 1;
    rf_net_reset(net);
    RiskfightVisibleOpponent* v = &f->vis;
    v->last_cast_tick = v->last_proc_tick = -1000;
    v->last_attack_tick = -1;
    v->pending_consume_tick = -1;
    v->health_bar = OSRS_PLAYER_HEALTH_BAR_SCALE; /* a fight starts at a full bar, as the sim's first observation */
}

/* What riskfight_observe_visible does after a sim tick, from what the client showed instead of the sim's players. `n` is the fight tick after this one. */
static void rt_observe(RiskfightVisibleOpponent* v, const RtSeen* seen, int n, const Request* q) {
    int bar_before = v->health_bar;
    float* event = v->events[(n - 1) % RF_HISTORY_TICKS];
    memset(event, 0, RT_EVENT_WIDTH * sizeof(float));
    if (seen->swing_raw != -1 || seen->swing_style != 0) {
        int item = seen->swing_raw == -1 ? ITEM_NONE : raw_id_to_item(seen->swing_raw);
        event[0] = 1;
        event[1] = item == ITEM_NONE ? 0 : item;
        event[2] = seen->swing_style;
        /* The maul special is instant: the sim logs the hits but does not move his swing clock. */
        if (item == ITEM_NONE || !pvp_is_maul(item)) {
            v->last_attack_tick = n - 1;
            v->last_attack_speed = q->his_speed > 0 ? q->his_speed : item == ITEM_NONE ? 4 : ITEM_DATABASE[item].attack_speed;
        }
    }
    if (seen->hit_landed && seen->bar_scale == OSRS_PLAYER_HEALTH_BAR_SCALE && seen->bar_ratio >= 0) v->health_bar = seen->bar_ratio;
    if (seen->veng_cast) v->last_cast_tick = n;
    if (seen->veng_text) v->last_proc_tick = n;
    if (v->consume_food_est > 0) v->consume_food_est--;
    if (v->consume_potion_est > 0) v->consume_potion_est--;
    if (v->consume_karam_est > 0) v->consume_karam_est--;
    if (v->consume_delay_est > 0) v->consume_delay_est--;
    event[3] = seen->hit_landed;
    event[4] = seen->hit_damage;
    event[5] = seen->veng_cast;
    event[6] = seen->consuming;
    event[7] = v->health_bar;
    int bar_event = seen->hit_landed;
    if (seen->consuming && v->pending_consume_tick < 0) {
        v->pending_consume_tick = n;
        v->pending_bar_before = bar_before;
        v->pending_damage_since = 0;
    }
    if (bar_event && v->pending_consume_tick >= 0) v->pending_damage_since += seen->hit_damage;
    if (bar_event && v->pending_consume_tick >= 0) {
        OsrsHealthBarRange before = osrs_health_bar_range(v->pending_bar_before, OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 121);
        OsrsHealthBarRange after = osrs_health_bar_range(v->health_bar, OSRS_PLAYER_HEALTH_BAR_SCALE, 99, 121);
        if (before.kind == OSRS_HEALTH_BAR_KNOWN && after.kind == OSRS_HEALTH_BAR_KNOWN) {
            OsrsHealthChangeRange ch = osrs_health_bar_unobserved_change(before, after, v->pending_damage_since);
            int f, po, k, d;
            riskfight_classify_consume_heal(ch.lower, ch.upper, &f, &po, &k, &d);
            int elapsed = n - v->pending_consume_tick;
            v->consume_food_est = f - elapsed > 0 ? f - elapsed : 0;
            v->consume_potion_est = po - elapsed > 0 ? po - elapsed : 0;
            v->consume_karam_est = k - elapsed > 0 ? k - elapsed : 0;
            v->consume_delay_est = d - elapsed > 0 ? d - elapsed : 0;
        }
        v->pending_consume_tick = -1;
        v->pending_damage_since = 0;
    } else if (bar_event) v->pending_damage_since = 0;
}

typedef struct { int kind, arg; } RtRecord;
static int rt_cmp(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return x < y ? -1 : x > y; }

static int rt_worn_index(int gear_slot) {
    for (int i = 0; i < TAPE_WORN; i++) if (WORN_TO_GEAR[i] == gear_slot) return i;
    return -1;
}

/* Head actions as the plugin's click records, in the order the sim queues them. Movement, orb, prayer and style heads are masked off by riskfight_deployable. */
static int rt_records(const int* actions, const RiskfightState* s, RtRecord* out) {
    int n = 0;
    static const int gear_heads[] = {RF_WEAPON, RF_SHIELD, RF_RING};
    for (size_t i = 0; i < sizeof(gear_heads) / sizeof(*gear_heads); i++) {
        int head = gear_heads[i], a = actions[head];
        if (!a) continue;
        if (a == RF_UNEQUIP) {
            int worn = rt_worn_index(RF_GEAR_SLOT_BY_HEAD[head]);
            assert(worn >= 0);
            out[n++] = (RtRecord){GS_WIRE_UNEQUIP_SLOT, worn};
        } else out[n++] = (RtRecord){GS_WIRE_CLICK_SLOT, a - 1};
    }
    for (int head = RF_FOOD; head <= RF_COMBO; head++)
        if (actions[head]) out[n++] = (RtRecord){GS_WIRE_CLICK_SLOT, actions[head] - 1};
    if (actions[RF_VENGEANCE]) out[n++] = (RtRecord){GS_WIRE_CAST_VENGEANCE, 0};
    for (int k = 0; k < actions[RF_SPECIAL]; k++) out[n++] = (RtRecord){GS_WIRE_SPEC_TOGGLE, 0};
    if (actions[RF_PRIMARY] == RF_TELEPORT) {
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
            if (osrs_inventory_cell_metadata(&s->env.players[0].inventory_cells[slot])->consumable_kind == OSRS_CONSUMABLE_TELEPORT) {
                out[n++] = (RtRecord){GS_WIRE_CLICK_SLOT, slot};
                break;
            }
    }
    int attacking = osrs_interaction_active(&s->env.players[0].interaction);
    if (actions[RF_PRIMARY] == RF_ATTACK && !attacking) out[n++] = (RtRecord){GS_WIRE_ATTACK, 0};
    if (actions[RF_PRIMARY] == RF_STOP && attacking) out[n++] = (RtRecord){GS_WIRE_STOP_ATTACK, 0};
    assert(n <= GS_WIRE_MAX_RECORDS);
    return n;
}

/* One tick: snapshot, our exact state, the visible-opponent state this server owns, the net, the clicks. Returns micros. */
static double rt_decide(Request* q, const RtSeen* seen, int new_fight, int fight_start, RtRecord* records, int* count) {
    double started = la_seconds();
    RtFight* f = &g_fight;
    if (new_fight) rt_fight_reset(f, fight_start);
    else if (!f->active || q->tick <= f->last_tick || q->tick - f->last_tick > 2) rt_fight_reset(f, q->tick);
    f->last_tick = q->tick;
    int n = q->tick - f->start_tick + 1;
    RiskfightSnapshot snap;
    snapshot_of(q, &snap);
    riskfight_load_state(&g_base.state, &g_base.context, &snap);
    RiskfightState* s = &g_base.state;
    RiskfightVisibleOpponent loaded = s->visible[0];
    rt_observe(&f->vis, seen, n, q);
    memcpy(f->vis.equipment, loaded.equipment, sizeof(f->vis.equipment));
    f->vis.interacting = loaded.interacting;
    Player* us = &s->env.players[0];
    us->x = FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH / 2;
    us->y = FIGHT_AREA_BASE_Y + FIGHT_AREA_HEIGHT / 2;
    f->vis.x = us->x + q->ours.ox;
    f->vis.y = us->y + q->ours.oy;
    s->visible[0] = f->vis;
    s->env.tick = n;
    /* Stakes: K him us in gp; the price of our death in units of his. */
    s->death_price = q->him > 0 && q->us > 0 ? fminf(RF_PRICE_MAX_X10 / 10.0f, fmaxf(0.1f, (float)((double)q->us / (double)q->him))) : 1.0f;
    s->inventory_use[0].divine_combat_ticks = OSRS_DIVINE_DURATION > n ? OSRS_DIVINE_DURATION - n : 0;
    float obs[RF_OBS_SIZE];
    unsigned char mask[RF_MASK_SIZE];
    int actions[RF_HEADS];
    riskfight_write_observation(s, 0, obs);
    riskfight_write_action_mask(s, 0, mask);
    rf_net_act(f->net, obs, mask, &f->rng, f->hard, RF_ACTION_DIMS, RF_HEADS, actions);
    *count = rt_records(actions, s, records);
    return (la_seconds() - started) * 1e6;
}

static int rt_parse(FILE* in, Request* q, RtSeen* seen, int* new_fight, int* fight_start, int* obs_only) {
    static char text[1 << 16];
    size_t used = 0;
    char line[4096];
    *seen = (RtSeen){-1, 0, 0, 0, 0, 0, -1, -1, 0};
    *new_fight = 0;
    *obs_only = 0;
    int header = 0;
    while (fgets(line, sizeof(line), in)) {
        assert(strchr(line, '\n') != NULL);
        if (!strncmp(line, "LA ", 3)) {
            assert(!header);
            header = 1;
            *obs_only = strstr(line, " OBS") != NULL;
            used += (size_t)snprintf(text + used, sizeof(text) - used, "%s", line);
        } else if (!strncmp(line, "FIGHT ", 6)) {
            *new_fight = 1;
            assert(sscanf(line + 6, "%d", fight_start) == 1);
        } else if (!strncmp(line, "V ", 2)) {
            assert(sscanf(line + 2, "%d %d %d %d %d %d %d %d %d", &seen->swing_raw, &seen->swing_style, &seen->hit_landed, &seen->hit_damage, &seen->veng_cast,
                &seen->consuming, &seen->bar_ratio, &seen->bar_scale, &seen->veng_text) == 9);
        } else {
            assert(header && "request must start with LA");
            used += (size_t)snprintf(text + used, sizeof(text) - used, "%s", line);
        }
        assert(used < sizeof(text) - sizeof(line));
        if (!strncmp(line, "END", 3)) {
            FILE* body = fmemopen(text, used, "r");
            read_request(body, q);
            fclose(body);
            return 1;
        }
    }
    assert(!header);
    return 0;
}

static void rt_respond(FILE* out, const Request* q, double micros, const RtRecord* records, int count, int obs_only) {
    if (obs_only) { fprintf(out, "O %d\n", q->id); return; }
    fprintf(out, "R %d %.0f 2 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 A %d", q->id, micros, count); /* plan 2 "rl" has no estimate: feasible 0, worlds 0 (ENGINE_WIRE.md) */
    for (int i = 0; i < count; i++) fprintf(out, " %d %d %d %d", q->tick + 1, records[i].kind, records[i].arg, 0);
    fprintf(out, "\n");
}

#ifndef GS_RL_SERVER_NO_MAIN
int main(int argc, char** argv) {
    assert(argc >= 4);
    static RfNet* net;
    net = rf_net_load(argv[1], RF_OBS_SIZE, atoi(argv[2]), atoi(argv[3]), RF_ACTION_DIMS, RF_HEADS);
    g_fight.net = net;
    g_fight.rng = 0x9e3779b9u;
    g_fight.hard = !(argc > 4 && !strcmp(argv[4], "sample"));
    riskfight_deployable = 1;
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99; g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    observer_register_items();
    sim_init(&g_base);
    const char* name = strrchr(argv[1], '/') ? strrchr(argv[1], '/') + 1 : argv[1];
    /* Warm-up: the lookahead's synthetic stream through the whole path, one fight, so page faults and lazy tables are paid before READY. */
    enum { WARM = 48 };
    double worst = 0;
    double bench[100000];
    int bench_n = 0, bench_target = argc > 6 && !strcmp(argv[5], "bench") ? atoi(argv[6]) : 0;
    for (int i = 0; i < (bench_target ? bench_target : WARM); i++) {
        const char* text = WARM_REQUESTS[i % WARM_REQUEST_COUNT];
        static Request q;
        FILE* in = fmemopen((void*)text, strlen(text), "r");
        read_request(in, &q);
        fclose(in);
        q.id = i;
        q.tick = 1000 + i;
        const char* unavailable = server_unavailable(&q);
        if (unavailable) { fprintf(stderr, "warm-up request %d is unavailable: %s\n", i % WARM_REQUEST_COUNT, unavailable); exit(2); }
        RtSeen seen = {-1, 0, i % 5 == 0, (i % 5 == 0) * 17, 0, i % 7 == 0, 20, 30, 0};
        RtRecord records[GS_WIRE_MAX_RECORDS];
        int count;
        double micros = rt_decide(&q, &seen, i == 0, q.tick, records, &count);
        if (i >= 2 && micros > worst) worst = micros;
        if (bench_target && bench_n < 100000) bench[bench_n++] = micros;
    }
    if (bench_target) {
        double total = 0;
        for (int i = 0; i < bench_n; i++) total += bench[i];
        qsort(bench, (size_t)bench_n, sizeof(double), rt_cmp);
        printf("rt_tick_micros n=%d mean=%.1f p50=%.1f p99=%.1f p999=%.1f max=%.1f\n", bench_n, total / bench_n, bench[bench_n / 2], bench[bench_n * 99 / 100],
            bench[bench_n * 999 / 1000], bench[bench_n - 1]);
        return 0;
    }
    printf("READY %.0f continue;no new clicks;rl POLICY rl:%s OBS\n", worst, name);
    fflush(stdout);
    static Request q;
    RtSeen seen;
    int new_fight, fight_start, obs_only;
    while (rt_parse(stdin, &q, &seen, &new_fight, &fight_start, &obs_only)) {
        request_declare_priors(&q);
        const char* unavailable = server_unavailable(&q);
        if (unavailable) {
            printf("UNAVAILABLE %d %s\n", q.id, unavailable);
            fflush(stdout);
            continue;
        }
        RtRecord records[GS_WIRE_MAX_RECORDS];
        int count;
        double micros = rt_decide(&q, &seen, new_fight, fight_start, records, &count);
        rt_respond(stdout, &q, micros, records, count, obs_only);
        fflush(stdout);
    }
    return 0;
}
#endif
