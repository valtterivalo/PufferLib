#ifndef RECORDED_PVP_132330_H
#define RECORDED_PVP_132330_H

static const ConditionedAttack recorded_132330_strip_attacks[] = {
    {23808, 1, 4, ATTACK_STYLE_MELEE, RECORDED_AFTER_SOURCE_PASS},
    {23809, 0, 39, ATTACK_STYLE_MAGIC, RECORDED_AFTER_SOURCE_PASS},
};

static const RecordedHitsplat recorded_132330_strip_hits[] = {
    {23809, 0, 4}, {23810, 0, 4},
    {23809, 1, 1}, {23809, 1, 3}, {23809, 1, 39},
};

static const RecordedHealthBar recorded_132330_strip_bars[] = {
    {23809, 0, 23, 30}, {23809, 1, 16, 30},
};

#define RECORDED_PVP_132330_STRIP_WINDOW { \
    .name = "armour_strip_vengeance_voidwaker", \
    .archive = "session_20260913T132330.980Z_20260913T093024Z.89354_9eee9f77-3f50-426b-b7f4-a7867bad9c27.jsonl.zst", \
    .source_sha256 = "4675c9bd339ecc5cb7c082e01f7910f92370f9163490e9c4132f20fddd85f6e8", \
    .assumptions = "Actors Xmas Szn (0, traceId7) and SezeRS3 (1, traceId6). Visible helm/body/legs disappear at 23807 sequence12861 and return at 23813 sequence13129. Vengeance spot726 appears at 23808 sequence12869. Hitsplat sequences12917,12921,12922,12923,12989. Attack rolls4/39, source-pass phases, both recoil and only Xmas Vengeance are conditioned, not observed PID or hidden state. Initial HP bounds condition on base99 and fresh post-hit bars23/30 for Xmas and16/30 for SezeRS3 at 23809, adding back4 and43 damage respectively. Older pre-hit bars24/30 and22/30 are stale and do not constrain starting HP. No healing during the conditioned damage exchange is assumed. Eating animation829 at23810 is not a timestamped healing amount. Full final HP equivalence is not claimed.", \
    .start_tick = 23808, .end_tick = 23810, \
    .hp_min = {80, 95}, .hp_max = {82, 97}, \
    .vengeance = {1, 0}, .recoil = {1, 1}, \
    .attacks = recorded_132330_strip_attacks, \
    .attack_count = sizeof(recorded_132330_strip_attacks) / sizeof(recorded_132330_strip_attacks[0]), \
    .hits = recorded_132330_strip_hits, \
    .hit_count = sizeof(recorded_132330_strip_hits) / sizeof(recorded_132330_strip_hits[0]), \
    .dead_actor = -1, .expected_candidates = 9, \
    .health_bars = recorded_132330_strip_bars, \
    .health_bar_count = sizeof(recorded_132330_strip_bars) / sizeof(recorded_132330_strip_bars[0]), \
}

#endif
