/* The RL decision engine as a process, the `rl` policy of ENGINE_WIRE.md "Swappable decision policy": same transport and response shape as the lookahead server.
 *
 *   cc -std=c11 -O2 -I. -o gs_rl_server ocean/osrs/tests/gs_rl_server.c -lm
 *   gs_rl_server CHECKPOINT HIDDEN LAYERS [sample|argmax]      run from a directory where ocean/osrs/data/*.cmap resolve
 *   gs_rl_server CHECKPOINT HIDDEN LAYERS argmax bench N       per-tick latency over N warm requests, no READY
 *
 * READY <warm_micros> continue;no new clicks;rl POLICY rl:<checkpoint file> OBS
 * Request: `LA id tick [OBS]` ... `END`, the lookahead request (tags B K F O Q W G SHIELD ARMOR PS RA P R; R and Q are ignored) plus the tags
 *   FIGHT start_tick        a line before the first request of a fight: clears the visible-opponent history, the consume estimates and the recurrent state
 *   V swing_raw swing_style hit_landed hit_damage veng_cast consuming bar_ratio bar_scale veng_text [swing_spec]
 *       what the client showed of him on this tick. swing_raw: raw id of his weapon when his attack animation started this tick, else -1; swing_style: that
 *       attack's AttackStyle number (0 none, 1 melee, 2 ranged, 3 magic; 0 when swing_raw is -1); hit_landed: 1 when any hitsplat appeared on him this tick
 *       (damage or block, any source); hit_damage: the damage of those hitsplats added up; veng_cast: his Vengeance cast graphic this tick; consuming: a meal or
 *       potion animation of his this tick; bar_ratio/bar_scale: his health bar as displayed now (-1 -1 when hidden); veng_text: "Taste vengeance!" over his
 *       head this tick. swing_spec (optional, 0 when absent): the COUNT of special attacks he launched on this tick (0, 1 or 2). A double granite maul plays one animation
 *       (1667) and lands two hits in the same tick, so the count comes from his hits, not from the animation: it is 2 there. Every other special is 1, and a Behead
 *       (10173) is 1 (no energy, but it spends his Soulreaper stacks). The count may arrive on the swing's tick or on the next one; either way it is charged to his
 *       last swing. The server subtracts count x osrs_spec_cost(weapon) from the session observation's estimate of his special energy, which stays at 100 on a wire
 *       that does not send it. A count with no swing of his in this tick or the one before is a malformed wire and aborts.
 * Session block, live: [0] 1, [2] [3] 0, [1] his special energy from the V lines, [4] our reputation 0 (no client can measure it), [5] how annoyed he is: our overeats of this
 *   fight (rt_note_meals: servings that left the G line's bag between two consecutive requests while the earlier request's hitpoints were strictly above its chance range) over
 *   RF_OVEREAT_PATIENCE + 1, [6] hitpoints over his chance range, [7] whether a teleport now is kosher (riskfight_norms.h, from the G line's bag, our special energy, [1] and the threat block; the shared observation writer fills it). The counter starts at 0 at every FIGHT.
 * Every tick of a fight must reach the server with the full request body: the server owns the history ring, the consume estimates and the recurrent state.
 * A tick gap above 2 or a tick that does not advance starts a new fight. On a decision tick the response is `R id micros 2 <3 x "feasible kill death value worlds">
 * A n <n x "due kind arg timing">` (best_index 2 = replace the remainder with these clicks, due = tick + 1); on an `OBS` tick it is `O id`. UNAVAILABLE as in the
 * lookahead server. Movement, prayer, style and the armour heads other than weapon, shield, ring, body and legs are masked off (riskfight_deployable). */
#define main gs_lookahead_server_main
#include "gs_lookahead_server.c"
#undef main
#include "rf_net.h"

enum { RT_EVENT_WIDTH = RF_EVENT_WIDTH };

/* Servings of our bag by the delay clock that gates them (sim: osrs_player_food_timer, the brew's potion timer): hard food (anglerfish, shark, marlin, pie bites), the karambwan
 * clock (karambwan, halibut) and brew doses. A pie is two servings (pie, half a pie), a brew its doses: the chain of next_content_code of the item. */
typedef struct { int hard, karambwan, brew; } RtServings;

typedef struct {
    RiskfightVisibleOpponent vis;
    int active, start_tick, last_tick;
    RfNet* net;
    uint32_t rng;
    int hard;
    int land_pending[2], last_land; /* fight ticks his swings land on us (-1 none), and the last landing so far (-1 none): see rt_track_landing */
    /* Our overeats this fight (rt_note_meals): the bag, hitpoints and chance range of the previous request, which are the state the next tick starts from. */
    int have_previous, previous_hitpoints, previous_range;
    RtServings previous_bag;
    int meals, overeats, bag_unexplained;
} RtFight;

static RtFight g_fight;

typedef struct { int swing_raw, swing_style, hit_landed, hit_damage, veng_cast, consuming, bar_ratio, bar_scale, veng_text, swing_spec; RtServings servings; } RtSeen;

/* A fight starts with the bar the sim's first observation gives him (riskfight_start_apply): the bar the client shows on the fight's first request, hit or not
 * (the start row's bar), and the full bar when none shows (a start without a bar draws regeared hitpoints, which read 30). */
static void rt_fight_reset(RtFight* f, int start_tick, const RtSeen* seen) {
    RfNet* net = f->net;
    uint32_t rng = f->rng;
    int hard = f->hard;
    memset(f, 0, sizeof(*f));
    f->net = net; f->rng = rng; f->hard = hard;
    f->active = 1;
    f->start_tick = start_tick;
    f->last_tick = start_tick - 1;
    if (net) rf_net_reset(net); /* no net: the observation-only harness (rl_explain "-", test_threat_live) */
    RiskfightVisibleOpponent* v = &f->vis;
    v->last_cast_tick = v->last_proc_tick = -1000; v->veng_up = 1;
    v->last_attack_tick = -1;
    v->pending_consume_tick = -1;
    v->spec_est = 100;
    v->health_bar = seen->bar_scale == OSRS_PLAYER_HEALTH_BAR_SCALE && seen->bar_ratio >= 0 ? seen->bar_ratio : OSRS_PLAYER_HEALTH_BAR_SCALE;
    f->land_pending[0] = f->land_pending[1] = f->last_land = -1;
}

/* The sim's content code of a bag item as the client showed it: canonical() maps the blighted ids and the ids the server models (pies included); an id it has no row for
 * (a shark) may still have a content row of its own, which is what a meal of the sim's inventory would be. */
static int rt_content_code(int raw) {
    int c = canonical(raw);
    return raw_id_to_content(c == -2 ? raw : c);
}

enum { RT_MEAL_NONE, RT_MEAL_HARD, RT_MEAL_KARAMBWAN, RT_MEAL_BREW };

/* What counts as a meal for the overeat rule, as riskfight_execute_command classifies a click: an eat click (food, karambwan, pie), or a brew. A restore, a super combat or any other
 * potion is not a meal. */
static int rt_meal_class(int code) {
    if (code == 0) return RT_MEAL_NONE;
    const OsrsItemContentMetadata* m = osrs_item_content_metadata((uint16_t)code);
    if (m->consumable_kind == OSRS_CONSUMABLE_BREW) return RT_MEAL_BREW;
    if (m->click_action != OSRS_CLICK_EAT) return RT_MEAL_NONE;
    return m->consumable_kind == OSRS_CONSUMABLE_KARAMBWAN || m->consumable_kind == OSRS_CONSUMABLE_HALIBUT ? RT_MEAL_KARAMBWAN : RT_MEAL_HARD;
}

/** Servings of one item: the meals it still holds (a brew (4) is 4, half a pie 1, an anglerfish 1, a restore 0). */
static int rt_servings(int code) {
    int n = 0;
    while (rt_meal_class(code) != RT_MEAL_NONE) {
        n++;
        code = osrs_item_content_metadata((uint16_t)code)->next_content_code;
    }
    return n;
}

/** The servings of a bag as the wire sent it (raw ids, before request_declare_priors rewrites them). Totals per clock, not per slot: moving an item between slots changes nothing. */
static RtServings rt_bag_servings(const int* raw_bag) {
    RtServings total = {0, 0, 0};
    for (int slot = 0; slot < TAPE_BAG; slot++) {
        int code = rt_content_code(raw_bag[slot]);
        int servings = rt_servings(code);
        switch (rt_meal_class(code)) {
            case RT_MEAL_HARD: total.hard += servings; break;
            case RT_MEAL_KARAMBWAN: total.karambwan += servings; break;
            case RT_MEAL_BREW: total.brew += servings; break;
            default: break;
        }
    }
    return total;
}

/* Our overeats this fight, counted the way the sim counts them (riskfight_step.h: a meal eaten while the hitpoints the tick began with are strictly above his chance range).
 * The request of tick N carries the state after tick N, which is the state tick N + 1 begins with; the meals of tick N + 1 are the servings that left the bag between request N
 * and request N + 1, and they are judged against request N's hitpoints and chance range. Every meal of a tick shares that bar, so a triple-eat above the range is three overeats.
 * A meal is one serving of a class gone: each class has its own delay clock (hard food, karambwan, potion), so a class cannot be eaten twice on one tick; at most three meals a
 * tick, one per class. A class that lost two or more servings at once lost them some other way (dropped, banked): not a meal, counted in bag_unexplained.
 * The O line's timers cannot tell the meals of a tick apart: a karambwan sets all three clocks, so karambwan alone, with a hard food or with a brew jump the same timers,
 * a brew jumps the potion and food timers whether or not a hard food came with it, and a restore or super combat jumps what a brew does. The bag does. A tick missing from the
 * stream (a gap of 2) is judged against the last request seen. The first request of a fight has no previous state and counts nothing. */
static void rt_note_meals(RtFight* f, const RtServings* bag, int hitpoints, int range) {
    if (f->have_previous) {
        const int fallen[3] = {f->previous_bag.hard - bag->hard, f->previous_bag.karambwan - bag->karambwan, f->previous_bag.brew - bag->brew};
        int meals = 0;
        for (int i = 0; i < 3; i++) {
            if (fallen[i] == 1) meals++;
            else if (fallen[i] > 1) f->bag_unexplained++;
        }
        f->meals += meals;
        if (f->previous_hitpoints > f->previous_range) f->overeats += meals;
    }
    f->have_previous = 1;
    f->previous_bag = *bag;
    f->previous_hitpoints = hitpoints;
    f->previous_range = range;
}

/* What riskfight_observe_visible does after a sim tick, from what the client showed instead of the sim's players. `n` is the fight tick after this one. */
static void rt_observe(RiskfightVisibleOpponent* v, const RtSeen* seen, int n, const Request* q) {
    int bar_before = v->health_bar;
    riskfight_spec_est_regen(v);
    float* event = v->events[(n - 1) % RF_HISTORY_TICKS];
    memset(event, 0, RT_EVENT_WIDTH * sizeof(float));
    if (seen->swing_raw != -1 || seen->swing_style != 0) {
        int item = seen->swing_raw == -1 ? ITEM_NONE : raw_id_to_item(seen->swing_raw);
        event[0] = 1;
        event[1] = item == ITEM_NONE ? 0 : item;
        event[2] = seen->swing_style;
        v->last_swing_item = item;
        v->last_swing_tick = n - 1;
        riskfight_threat_note_swing(&v->threat, (uint8_t)item, n - 1, seen->swing_spec > 0);
        /* The maul special is instant: the sim logs the hits but does not move his swing clock. */
        if (item == ITEM_NONE || !pvp_is_maul(item))
            riskfight_his_swing_seen(v, n - 1, q->his_speed > 0 ? q->his_speed : item == ITEM_NONE ? 4 : ITEM_DATABASE[item].attack_speed);
    }
    if (seen->swing_spec > 0) {
        assert(seen->swing_spec <= 2 && n - 1 - v->last_swing_tick <= 1 && "special count with no swing of his in this tick or the one before");
        if (seen->swing_raw == -1 && seen->swing_style == 0) /* the count came a tick after its swing: the Behead of a Soulreaper axe is read now */
            riskfight_threat_note_swing(&v->threat, (uint8_t)v->last_swing_item, v->last_swing_tick, 1);
        if (v->last_swing_item != ITEM_NONE)
            for (int i = 0; i < seen->swing_spec; i++) riskfight_spec_est_seen(v, osrs_spec_cost(v->last_swing_item));
    }
    if (seen->hit_landed && seen->bar_scale == OSRS_PLAYER_HEALTH_BAR_SCALE && seen->bar_ratio >= 0) v->health_bar = seen->bar_ratio;
    riskfight_veng_seen(v, n, seen->veng_cast, seen->veng_text, seen->hit_landed ? seen->hit_damage : 0);
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
    if (seen->consuming) riskfight_meal_seen(v, n, bar_before);
    if (bar_event && v->pending_consume_tick >= 0) v->pending_damage_since += seen->hit_damage;
    if (bar_event && v->pending_consume_tick >= 0) riskfight_meals_resolve(v, n);
    else if (bar_event) v->pending_damage_since = 0;
}

/* The anglerfish overheal lock of our side (Player.pvp_overheal_block) from what the client showed of him. The snapshot's own derivation reads the O line's
 * his_last_ago, a plugin estimate from his attack timer that is wrong for many ticks (-1 or 0 right after a swing it did not see), so this server replaces it
 * with the swings of the V lines: his swing at fight tick T lands on us at T + 1 (melee) or T + 2 (ranged, magic: the shortest projectile flight). rt_track_landing
 * schedules the landing of the swing of the tick just seen (event n - 1, `n` being the fight tick after it) and books the ones that have come due; a landing
 * `landed_ago` ticks before the tick just seen leaves 16 - landed_ago ticks of lock. (The history ring holds 16 ticks, too few to read the lock off it.) */
static void rt_track_landing(RtFight* f, int n) {
    int seen = n - 1;
    const float* event = f->vis.events[seen % RF_HISTORY_TICKS];
    if (event[0] != 0) {
        int land = seen + ((int)event[2] == ATTACK_STYLE_MELEE ? 1 : 2);
        for (int i = 0; i < 2; i++) if (f->land_pending[i] < 0) { f->land_pending[i] = land; break; }
    }
    for (int i = 0; i < 2; i++)
        if (f->land_pending[i] >= 0 && f->land_pending[i] <= seen) {
            if (f->land_pending[i] > f->last_land) f->last_land = f->land_pending[i];
            f->land_pending[i] = -1;
        }
}

static int rt_overheal_block(const RtFight* f, int n) {
    int landed_ago = (n - 1) - f->last_land;
    return f->last_land >= 0 && landed_ago < OSRS_PVP_OVERHEAL_BLOCK_TICKS ? OSRS_PVP_OVERHEAL_BLOCK_TICKS - landed_ago : 0;
}

typedef struct { int kind, arg; } RtRecord;
static int rt_cmp(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return x < y ? -1 : x > y; }

static int rt_worn_index(int gear_slot) {
    for (int i = 0; i < TAPE_WORN; i++) if (WORN_TO_GEAR[i] == gear_slot) return i;
    return -1;
}

/* Head actions as the plugin's click records, in the order the sim queues them (riskfight_policy_commands): the gear heads in head order (weapon, shield, ring, body,
 * legs), the meals, the orb or the teleport (the sim clicks the tab when both are chosen), Vengeance, the special toggles (riskfight_spec_clicks: none when the
 * decision wields a granite maul whose knockout gate is shut, as in the sim's step), attack or stop. Bag actions name physical slots (riskfight_bag_to_physical of
 * the net's). Movement, prayer, style and the other armour heads are masked off by riskfight_deployable. */
static int rt_records(const int* actions, const RiskfightState* s, RtRecord* out) {
    int n = 0;
    static const int gear_heads[] = {RF_WEAPON, RF_SHIELD, RF_RING, RF_BODY, RF_LEGS};
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
    if (actions[RF_ORB] || actions[RF_PRIMARY] == RF_TELEPORT) {
        /* The first slot holding the tab or the orb (the mask offers either only when the bag holds it); the orb's left-click option is Feel. */
        OsrsConsumableKind kind = actions[RF_PRIMARY] == RF_TELEPORT ? OSRS_CONSUMABLE_TELEPORT : OSRS_CONSUMABLE_LOCATOR_ORB;
        int slot = 0;
        while (slot < OSRS_INVENTORY_SIZE && osrs_inventory_cell_metadata(&s->env.players[0].inventory_cells[slot])->consumable_kind != kind) slot++;
        assert(slot < OSRS_INVENTORY_SIZE);
        out[n++] = (RtRecord){GS_WIRE_CLICK_SLOT, slot};
    }
    if (actions[RF_VENGEANCE]) out[n++] = (RtRecord){GS_WIRE_CAST_VENGEANCE, 0};
    const int specials = riskfight_spec_clicks(s, 0, actions);
    for (int k = 0; k < specials; k++) out[n++] = (RtRecord){GS_WIRE_SPEC_TOGGLE, 0};
    int attacking = osrs_interaction_active(&s->env.players[0].interaction);
    if (actions[RF_PRIMARY] == RF_ATTACK && !attacking) out[n++] = (RtRecord){GS_WIRE_ATTACK, 0};
    if (actions[RF_PRIMARY] == RF_STOP && attacking) out[n++] = (RtRecord){GS_WIRE_STOP_ATTACK, 0};
    assert(n <= GS_WIRE_MAX_RECORDS);
    return n;
}

/* Centre-to-centre distance between two bag slots on the client (4 columns x 7 rows, slot = row * 4 + col, pitch 42 px across and 36 px down). */
static double rt_slot_distance(int a, int b) {
    return hypot(42.0 * (a % 4 - b % 4), 36.0 * (a / 4 - b / 4));
}

/* Which physical copy of a consumable the client clicks. The net picks a canonical bag entry, and among identical copies (same content code) the one it maps to is
 * arbitrary; the cursor has to travel to it, and a triple eat (hard food, potion, karambwan) must land all three clicks inside one tick. The sim's motor model does not
 * see slot geometry, so this is a live-only choice that leaves the net's decision (which item) alone and moves only the click (which copy), over the CLICK_SLOT records
 * of `records` that land on an eat or drink item:
 * - two or more CLICK_SLOT records in the batch: the copies that minimise the summed rt_slot_distance between consecutive CLICK_SLOT records in record order (gear,
 *   hard food, potion, karambwan, orb or teleport), searched exactly; gear, orb and teleport clicks stay where they are and anchor the path; no two clicks share a slot; ties go to
 *   the lowest slots;
 * - a single eat click (hard food, karambwan): the copy whose distance to the nearest drink slot (brew, restore, combat, any potion) is largest, keeping the copies next
 *   to the potions for the triple eats; ties go to the lowest slot;
 * - anything else (a lone potion, gear, the orb, a teleport, unequips) is unchanged. */
static void rt_choose_copies(const Player* p, RtRecord* records, int count) {
    enum { MOVABLE = 3 };
    const OsrsInventoryCell* cells = p->inventory_cells;
    int click[GS_WIRE_MAX_RECORDS], slot[GS_WIRE_MAX_RECORDS], clicks = 0;
    for (int i = 0; i < count; i++)
        if (records[i].kind == GS_WIRE_CLICK_SLOT) { click[clicks] = i; slot[clicks++] = records[i].arg; }
    int movable[GS_WIRE_MAX_RECORDS], moves = 0;
    for (int c = 0; c < clicks; c++) {
        int action = osrs_inventory_cell_metadata(&cells[slot[c]])->click_action;
        if (action == OSRS_CLICK_EAT || action == OSRS_CLICK_DRINK) movable[moves++] = c;
    }
    if (moves == 0) return;
    if (clicks == 1) {
        if (osrs_inventory_cell_metadata(&cells[slot[0]])->click_action != OSRS_CLICK_EAT) return;
        int best = -1;
        double best_gap = -1;
        for (int s = 0; s < OSRS_INVENTORY_SIZE; s++) {
            if (cells[s].content_code != cells[slot[0]].content_code) continue;
            double gap = INFINITY;
            for (int d = 0; d < OSRS_INVENTORY_SIZE; d++)
                if (osrs_inventory_cell_metadata(&cells[d])->click_action == OSRS_CLICK_DRINK) gap = fmin(gap, rt_slot_distance(s, d));
            if (gap > best_gap) { best_gap = gap; best = s; }
        }
        records[click[0]].arg = best;
        return;
    }
    assert(moves <= MOVABLE); /* one per meal head: food, drink, combo */
    int fixed[OSRS_INVENTORY_SIZE] = {0};
    for (int c = 0; c < clicks; c++) fixed[slot[c]] = 1;
    for (int m = 0; m < moves; m++) fixed[slot[movable[m]]] = 0;
    int candidates[MOVABLE][OSRS_INVENTORY_SIZE], options[MOVABLE];
    for (int m = 0; m < moves; m++) {
        options[m] = 0;
        for (int s = 0; s < OSRS_INVENTORY_SIZE; s++)
            if (!fixed[s] && cells[s].content_code == cells[slot[movable[m]]].content_code) candidates[m][options[m]++] = s;
    }
    int pick[MOVABLE] = {0}, best_slot[GS_WIRE_MAX_RECORDS];
    double best = INFINITY;
    for (;;) {
        int trial[GS_WIRE_MAX_RECORDS], distinct = 1;
        memcpy(trial, slot, sizeof(int) * (size_t)clicks);
        for (int m = 0; m < moves; m++) {
            trial[movable[m]] = candidates[m][pick[m]];
            for (int k = 0; k < m; k++) distinct &= trial[movable[k]] != trial[movable[m]];
        }
        if (distinct) {
            double path = 0;
            for (int c = 1; c < clicks; c++) path += rt_slot_distance(trial[c - 1], trial[c]);
            if (path < best) { best = path; memcpy(best_slot, trial, sizeof(int) * (size_t)clicks); }
        }
        int m = moves - 1;
        while (m >= 0 && ++pick[m] == options[m]) pick[m--] = 0;
        if (m < 0) break;
    }
    for (int c = 0; c < clicks; c++) records[click[c]].arg = best_slot[c];
}

/* One tick: snapshot, our exact state, the visible-opponent state this server owns, the net, the clicks. Returns micros. `obs_only`: an `OBS` request, a tick the
 * plugin does not decide because its run is committed or clicks are unlanded; that is the sim's busy hand (RiskfightState.hand_busy), so the net sees the tactics
 * block's hand_busy at 1 and a mask of no-ops, and its recurrent state steps as in training. */
static double rt_decide(Request* q, const RtSeen* seen, int new_fight, int fight_start, int obs_only, RtRecord* records, int* count) {
    double started = la_seconds();
    RtFight* f = &g_fight;
    if (new_fight) rt_fight_reset(f, fight_start, seen);
    else if (!f->active || q->tick <= f->last_tick || q->tick - f->last_tick > 2) rt_fight_reset(f, q->tick, seen);
    f->last_tick = q->tick;
    int n = q->tick - f->start_tick + 1;
    RiskfightSnapshot snap;
    snapshot_of(q, &snap);
    riskfight_load_state(&g_base.state, &g_base.context, &snap);
    RiskfightState* s = &g_base.state;
    RiskfightVisibleOpponent loaded = s->visible[0];
    rt_observe(&f->vis, seen, n, q);
    rt_track_landing(f, n);
    memcpy(f->vis.equipment, loaded.equipment, sizeof(f->vis.equipment));
    f->vis.interacting = loaded.interacting;
    /* The request's stat-priced item rows are rewritten every request (osrs_stat_items_clear), so item indices of an earlier tick may mean another item: rebuild. */
    f->vis.threat.valid = 0;
    riskfight_threat_refresh(&f->vis.threat, f->vis.equipment, n);
    Player* us = &s->env.players[0];
    us->x = FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH / 2;
    us->y = FIGHT_AREA_BASE_Y + FIGHT_AREA_HEIGHT / 2;
    f->vis.x = us->x + q->ours.ox;
    f->vis.y = us->y + q->ours.oy;
    us->pvp_overheal_block = (uint8_t)rt_overheal_block(f, n);
    s->visible[0] = f->vis;
    s->env.tick = n;
    /* Our overeats of this fight feed the session block's [5]; the chance range is the one the observation's [6] margin is taken against. */
    rt_note_meals(f, &seen->servings, us->current_hitpoints, riskfight_chance_range(s, 0));
    s->overeats[0] = f->overeats;
    /* Stakes: K him us in gp; the price of our death in units of his. */
    s->death_price = q->him > 0 && q->us > 0 ? fminf(RF_PRICE_MAX_X10 / 10.0f, fmaxf(0.1f, (float)((double)q->us / (double)q->him))) : 1.0f;
    s->inventory_use[0].divine_combat_ticks = OSRS_DIVINE_DURATION > n ? OSRS_DIVINE_DURATION - n : 0;
    /* A live session has no budget: the end of the session is never near (the session block reads 1 for "plenty of time left"). */
    s->session_ticks_left = RF_SESSION_OPEN;
    /* The wire does not carry our reputation (rude teleports of the session, decaying) and nothing on a client can measure it, so it reads as none: [4] is 0. */
    s->session_rep = 0;
    s->hand_busy[0] = obs_only;
    float obs[RF_OBS_SIZE];
    unsigned char mask[RF_MASK_SIZE];
    int actions[RF_HEADS];
    riskfight_write_observation(s, 0, obs);
    riskfight_write_action_mask(s, 0, mask);
    if (f->net) rf_net_act(f->net, obs, mask, &f->rng, f->hard, RF_ACTION_DIMS, RF_HEADS, actions);
    else memset(actions, 0, sizeof(actions));
    riskfight_bag_to_physical(us, actions); /* the net picks entries of the canonical bag; the client clicks the slot that holds it */
    *count = rt_records(actions, s, records);
    rt_choose_copies(us, records, *count); /* which copy of that item the client clicks: rt_choose_copies */
    return (la_seconds() - started) * 1e6;
}

static int rt_parse(FILE* in, Request* q, RtSeen* seen, int* new_fight, int* fight_start, int* obs_only) {
    static char text[1 << 16];
    size_t used = 0;
    char line[4096];
    *seen = (RtSeen){-1, 0, 0, 0, 0, 0, -1, -1, 0, 0};
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
            int fields = sscanf(line + 2, "%d %d %d %d %d %d %d %d %d %d", &seen->swing_raw, &seen->swing_style, &seen->hit_landed, &seen->hit_damage, &seen->veng_cast,
                &seen->consuming, &seen->bar_ratio, &seen->bar_scale, &seen->veng_text, &seen->swing_spec);
            assert(fields == 9 || fields == 10);
            assert(seen->swing_spec >= 0 && seen->swing_spec <= 2);
        } else {
            assert(header && "request must start with LA");
            used += (size_t)snprintf(text + used, sizeof(text) - used, "%s", line);
        }
        assert(used < sizeof(text) - sizeof(line));
        if (!strncmp(line, "END", 3)) {
            FILE* body = fmemopen(text, used, "r");
            read_request(body, q);
            seen->servings = rt_bag_servings(q->bag);
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
        double micros = rt_decide(&q, &seen, i == 0, q.tick, 0, records, &count);
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
        double micros = rt_decide(&q, &seen, new_fight, fight_start, obs_only, records, &count);
        rt_respond(stdout, &q, micros, records, count, obs_only);
        fflush(stdout);
    }
    return 0;
}
#endif
