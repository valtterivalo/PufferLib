/* Remote-only proof: cc -std=c11 -O2 -I. ... -lm; executable [steps>=30000000] [seed].
 * Realistic built-in scripted riskfight actors; no uniform random action profile.
 * Compares every consumer transition after cloning, then replays each complete episode.
 */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState live, clone, replay, initial;
static RiskfightContext contexts[3];

static uint64_t bytes(uint64_t h, const void* p, size_t n) {
    const unsigned char* b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * UINT64_C(1099511628211);
    return h;
}
#define HASH(value) h = bytes(h, &(value), sizeof(value))
static uint64_t consumer_hash(RiskfightState* s) {
    uint64_t h = UINT64_C(1469598103934665603);
    HASH(s->env.tick); HASH(s->env.pid_holder); HASH(s->env.episode_over);
    HASH(s->env.winner); HASH(s->outcome); HASH(s->rewards); HASH(s->episode_returns);
    HASH(s->script_seed); HASH(s->consume_ticks); HASH(s->spec_voidwaker); HASH(s->spec_maul);
    HASH(s->env.pending_hit_metadata);
    HASH(s->env.rng_state);
    HASH(s->inventory_use);
    for (int side = 0; side < 2; side++) {
        float obs[RF_OBS_SIZE];
        unsigned char mask[RF_MASK_SIZE];
        riskfight_write_observation(s, side, obs);
        riskfight_write_action_mask(s, side, mask);
        h = bytes(h, obs, sizeof(obs)); h = bytes(h, mask, sizeof(mask));
        Player* p = &s->env.players[side];
        HASH(p->current_hitpoints); HASH(p->current_prayer); HASH(p->special_energy);
        HASH(p->attack_timer_uncapped); HASH(p->food_timer); HASH(p->potion_timer);
        HASH(p->karambwan_timer); HASH(p->veng_active); HASH(p->veng_cooldown);
        HASH(s->poison[side]); HASH(s->poison_source[side]);
        HASH(p->inventory_cells); HASH(p->equipped);
        HASH(p->num_pending_hits);
        h = bytes(h, p->pending_hits, p->num_pending_hits * sizeof(*p->pending_hits));
        HASH(s->visible[side]);
    }
    return h;
}
#undef HASH

static void scripted_step(RiskfightState* s, RiskfightContext* ctx, int profile) {
    int actions[2 * RF_HEADS] = {0};
    float obs[RF_OBS_SIZE];
    for (int side = 0; side < 2; side++) {
        riskfight_write_observation(s, side, obs);
        riskfight_script(obs, &RISKFIGHT_HUMAN_PROFILES[(profile + side) %
            (RISKFIGHT_HUMAN_TRAIN_PROFILES + RISKFIGHT_HUMAN_HELDOUT_PROFILES)],
            s->script_seed[side], s->consume_ticks[side], actions + side * RF_HEADS);
    }
    riskfight_step((EncounterState*)s, (EncounterContext*)ctx, actions);
}

static uint64_t number(const char* text) {
    char* end;
    errno = 0;
    unsigned long long n = strtoull(text, &end, 10);
    if (errno || !*text || *text == '-' || *end) { fprintf(stderr, "invalid integer: %s\n", text); exit(2); }
    return n;
}

int main(int argc, char** argv) {
    if (argc > 3) { fprintf(stderr, "usage: %s [steps>=30000000] [seed]\n", argv[0]); return 2; }
    uint64_t steps = argc > 1 ? number(argv[1]) : UINT64_C(30000000);
    uint64_t seed = argc > 2 ? number(argv[2]) : 20261012;
    if (steps < UINT64_C(30000000) || seed > UINT32_MAX) {
        fputs("requires >=30000000 primary transitions and a uint32 seed\n", stderr); return 2;
    }
    for (int i = 0; i < 3; i++) {
        riskfight_init_context((EncounterContext*)&contexts[i]);
        contexts[i].self_play = 1;
        riskfight_finalize_context((EncounterState*)&live, (EncounterContext*)&contexts[i]);
    }
    uint64_t done = 0, episodes = 0, digest = UINT64_C(1469598103934665603);
    static uint64_t transition_hashes[RISKFIGHT_ROUND_TICKS + 1];
    while (done < steps) {
        uint32_t episode_seed = osrs_lowbias32((uint32_t)seed ^ (uint32_t)episodes);
        riskfight_reset((EncounterState*)&live, (EncounterContext*)&contexts[0], episode_seed);
        initial = clone = live;
        int length = 0;
        int profile = episodes % (RISKFIGHT_HUMAN_TRAIN_PROFILES + RISKFIGHT_HUMAN_HELDOUT_PROFILES);
        while (!live.env.episode_over && done < steps) {
            if (length % 127 == 0) clone = live;
            scripted_step(&live, &contexts[0], profile);
            scripted_step(&clone, &contexts[1], profile);
            uint64_t a = consumer_hash(&live), b = consumer_hash(&clone);
            if (a != b) { fprintf(stderr, "clone divergence episode=%" PRIu64 " tick=%d\n", episodes, length); return 1; }
            digest = bytes(digest, &a, sizeof(a));
            assert(length < RISKFIGHT_ROUND_TICKS + 1);
            transition_hashes[length] = a;
            length++; done++;
        }
        replay = initial;
        for (int i = 0; i < length; i++) {
            scripted_step(&replay, &contexts[2], profile);
            if (consumer_hash(&replay) != transition_hashes[i]) {
                fprintf(stderr, "replay divergence episode=%" PRIu64 " tick=%d\n", episodes, i); return 1;
            }
        }
        episodes++;
    }
    for (int i = 0; i < 3; i++) riskfight_destroy_context((EncounterContext*)&contexts[i]);
    printf("steps=%" PRIu64 " episodes=%" PRIu64 " seed=%" PRIu64 " consumer_hash=%016" PRIx64 "\n", done, episodes, seed, digest);
    return 0;
}
