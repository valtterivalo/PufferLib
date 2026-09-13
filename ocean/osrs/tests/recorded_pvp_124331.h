#ifndef RECORDED_PVP_124331_H
#define RECORDED_PVP_124331_H

static const ConditionedAttack recorded_124331_lethal_attacks[] = {
    {19629, 1, 43, ATTACK_STYLE_MELEE, RECORDED_AFTER_SOURCE_PASS},
    {19630, 0, 68, ATTACK_STYLE_MELEE, RECORDED_AFTER_SOURCE_PASS},
};

static const RecordedHitsplat recorded_124331_lethal_hits[] = {
    {19629, 0, 43},
    {19630, 1, 5}, {19630, 1, 32}, {19631, 1, 68},
};

#define RECORDED_PVP_124331_LETHAL_WINDOW { \
    .name = "tentacle_vengeance_dharok_lethal", \
    .archive = "session_20260913T124331.199Z_20260913T093024Z.89354_b6f65a93-e103-4667-92ae-f1f750b5d101.jsonl.zst", \
    .source_sha256 = "eba48a5e968cb45084983cc40ccb883a19513450dc82d61853e006281bba443e", \
    .assumptions = "Actors lul-zuiger (0, traceId6, presence13) and 0004 (1, traceId3, presence14). Hitsplat sequences 10278,10327,10328,10373 and death callback 10372. Direct rolls 43 and 68 and source-pass launches conditioned on tentacle animation 1658 at 19629 and axe animation 2067 at 19630. No intervening healing or other damage assumed. Initial HP enumerated within the simulator 121-HP domain: lul survives 43, 0004 survives 37 then dies to 68. Only lul recoil and Vengeance active are assumed. Health bars and callback ordering do not establish exact HP or server PID.", \
    .start_tick = 19629, .end_tick = 19631, \
    .hp_min = {44, 38}, .hp_max = {121, 105}, \
    .vengeance = {1, 0}, .recoil = {1, 0}, \
    .attacks = recorded_124331_lethal_attacks, \
    .attack_count = sizeof(recorded_124331_lethal_attacks) / sizeof(recorded_124331_lethal_attacks[0]), \
    .hits = recorded_124331_lethal_hits, \
    .hit_count = sizeof(recorded_124331_lethal_hits) / sizeof(recorded_124331_lethal_hits[0]), \
    .dead_actor = 1, .expected_candidates = 5304, \
}

#endif
