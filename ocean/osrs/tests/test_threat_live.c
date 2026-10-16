/* gs_rl_server fills the threat block from a recorded live request stream (fixtures/live_swings.requests: 48 ticks of the live fight 20261016T141036 against Belle's
 * folly, trace replay-20261016T141036-swings.requests of the engine): the server's own path (rt_parse, rt_decide with no net), the observation read back.
 *   his weapon, Belle's folly (wiki: stab +100, slash +78, strength +102, speed 5), in his gear of the W/ARMOR/SHIELD lines:
 *     strength bonus 3 + 4 + 10 + 2 + 0 + 12 + 2 + 8 + 102 = 143 (neitiznot, fire cape, strength amulet, mixed hide top, rune platelegs, barrows gloves, rune boots,
 *     avernic defender, folly); strength 118 on Piety, aggressive: floor(118 * 1.23) + 3 + 8 = 156; max = floor(0.5 + 156 * (143 + 64) / 640) = floor(50.96) = 50.
 *     stab attack 100 + 4 (mixed hide top) + 30 (avernic defender) + 1 (fire cape) + 12 (gloves) = 147; roll (141 + 8) * (147 + 64) = 31439.
 *   our defence on the first request: 118 + 8 = 126; stab defence 31 + 12 + 62 + 25 + 12 + 16 = 158 (neitiznot, infernal cape, fighter torso, dragon defender,
 *     barrows gloves, dragon boots): roll 126 * (158 + 64) = 27972; hit = 1 - (27972 + 2) / (2 * (31439 + 1)) = 0.55510.
 * cc -std=c11 -O2 -I. -o /tmp/test_threat_live ocean/osrs/tests/test_threat_live.c -lm ; run from the repository root. */
#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"

static int near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

int main(void) {
    g_fight.net = NULL;
    g_fight.rng = 0x9e3779b9u;
    g_fight.hard = 1;
    riskfight_deployable = 1;
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99; g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    observer_register_items();
    sim_init(&g_base);
    FILE* in = fopen("ocean/osrs/tests/fixtures/live_swings.requests", "r");
    if (!in) { perror("fixtures/live_swings.requests"); return 2; }
    static Request q;
    RtSeen seen;
    int new_fight, fight_start, obs_only, requests = 0, swung = 0, first = 1, switched = 0;
    while (rt_parse(in, &q, &seen, &new_fight, &fight_start, &obs_only)) {
        request_declare_priors(&q);
        assert(!server_unavailable(&q));
        RtRecord records[GS_WIRE_MAX_RECORDS];
        int count;
        rt_decide(&q, &seen, new_fight, fight_start, records, &count);
        float obs[RF_OBS_SIZE];
        riskfight_write_observation(&g_base.state, 0, obs);
        const float* t = obs + RF_THREAT_START;
        requests++;
        swung |= seen.swing_raw != -1;
        uint8_t wielded = g_base.state.visible[0].equipment[GEAR_SLOT_WEAPON];
        if (wielded == ITEM_BELLES_FOLLY) {
            /* Slot 0 is his weapon: Belle's folly, a melee weapon of speed 5 with no special; shown from the tick of his first swing. */
            assert(t[0] == 1 && t[5] == 1 && t[6] == 0 && t[7] == 0 && near(t[4], 0.5f, 1e-6f) && t[8] == 0);
            assert(near(t[2] * RF_THREAT_DAMAGE_SCALE, 50, 1e-4f));
            assert(t[1] == (swung ? 1.0f : 0.0f));
            if (first) { assert(near(t[3], 0.55510f, 2e-4f)); first = 0; }
        } else {
            /* The last ticks: he has switched to the Ancient godsword (speed 6, special 50%). It leads, unshown; the folly he swung is shown in the next slot. */
            switched++;
            assert(wielded == ITEM_ANCIENT_GS && t[0] == 1 && t[1] == 0 && near(t[4], 0.6f, 1e-6f) && near(t[8], 0.5f, 1e-6f) && t[10] > 0);
            int folly = 0;
            for (int i = 1; i < RF_THREAT_HIS_SLOTS; i++) {
                const float* o = t + i * RF_THREAT_HIS_WIDTH;
                if (o[0] == 1 && o[1] == 1) { folly = 1; assert(near(o[4], 0.5f, 1e-6f) && near(o[2] * RF_THREAT_DAMAGE_SCALE, 50, 1e-4f)); }
            }
            assert(folly);
        }
        /* Ours: the Voidwaker in hand with its special, against his visible defence. */
        const float* ours = t + RF_THREAT_OURS_OFFSET;
        assert(ours[0] == 1 && ours[4] == 0.5f && ours[6] == 1.0f && ours[2] > 0 && ours[2] < 1);
        for (int i = 0; i < RF_OBS_SIZE; i++) assert(obs[i] >= -1 && obs[i] <= 1.0001f);
    }
    assert(requests == 48 && swung && switched > 0);
    /* His Soulreaper stacks from the V lines: swing_raw 28338 on five ticks, then a swing with the special animation (swing_spec), then 50 quiet ticks. */
    {
        RiskfightVisibleOpponent v;
        memset(&v, 0, sizeof(v));
        v.last_cast_tick = v.last_proc_tick = -1000; v.last_attack_tick = -1; v.pending_consume_tick = -1; v.health_bar = OSRS_PLAYER_HEALTH_BAR_SCALE;
        RtSeen axe = {28338, 1, 0, 0, 0, 0, -1, -1, 0, 0}, quiet = {-1, 0, 0, 0, 0, 0, -1, -1, 0, 0}, behead = {28338, 1, 0, 0, 0, 0, -1, -1, 0, 1};
        int n = 11; /* the fight tick after the one seen */
        for (int i = 0; i < 5; i++, n++) rt_observe(&v, &axe, n, &q);
        assert(riskfight_threat_stacks(&v.threat, n) == 5); /* read at the tick after the last swing */
        for (int i = 0; i < 48; i++, n++) rt_observe(&v, &quiet, n, &q);
        assert(riskfight_threat_stacks(&v.threat, n) == 5);  /* read at 64: 49 ticks after the swing of tick 14 */
        rt_observe(&v, &quiet, n++, &q);
        assert(riskfight_threat_stacks(&v.threat, n) == 4);
        rt_observe(&v, &behead, n++, &q);
        assert(riskfight_threat_stacks(&v.threat, n) == 0);
    }
    printf("live threat block: %d requests, his Belle's folly max 50 / hit 0.5551 as hand-computed, shown after the first swing, the Ancient godsword leads on his switch (%d ticks)\n", requests, switched);
    return 0;
}
