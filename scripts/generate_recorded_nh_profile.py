import argparse
import hashlib
import itertools
import json
from collections import Counter, defaultdict
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--fit", type=Path, required=True)
parser.add_argument("--fit-sha256", required=True)
parser.add_argument("--split", type=Path, required=True)
parser.add_argument("--split-sha256", required=True)
parser.add_argument("--source-sha256", required=True)
parser.add_argument("--output", type=Path, required=True)
arguments = parser.parse_args()
fit_bytes = arguments.fit.read_bytes()
assert hashlib.sha256(fit_bytes).hexdigest() == arguments.fit_sha256
assert hashlib.sha256(arguments.split.read_bytes()).hexdigest() == arguments.split_sha256
fit = json.loads(fit_bytes)
styles = {"NO_VISIBLE_ITEM": 0, "UNOBSERVED_IN_SEGMENT": 0, "MELEE": 1, "RANGED": 2, "MAGIC": 3}
prayers = {"NONE": 0, "MAGIC": 1, "RANGED": 2, "MELEE": 3, "SMITE": 4, "REDEMPTION": 5}
attacks = {"MAGIC_CAST": 0, "RANGED_BOW": 1, "RANGED_CROSSBOW": 1, "RANGED_ATLATL": 1, "MELEE_AXE": 2, "MELEE_SWEEP": 2, "MELEE_SWORD": 2, "VOIDWAKER_SPECIAL": 3, "AGS_SPECIAL": 3, "MAUL_SPECIAL": 3}
moves = {"CLOSER": 0, "SAME_DISTANCE": 1, "FARTHER": 2}
attack = defaultdict(Counter)
defense = defaultdict(Counter)
movement = defaultdict(Counter)
for row in fit["attack_cue_by_context"]:
    key = (row["distance"], styles[row["previous_own_damage_style_cue"]], prayers[row["target_visible_overhead"]])
    assert isinstance(key[0], int) and key[0] >= 0
    for cue, count in row["counts"].items():
        attack[key][attacks[cue]] += count
for row in fit["defensive_overhead_transition_with_weapon_history"]:
    key = (styles[row["target_older_visible_weapon_style"]], styles[row["target_previous_visible_weapon_style"]], prayers[row["own_previous_overhead"]])
    for prayer, count in row["counts"].items():
        defense[key][prayers[prayer]] += count
for row in fit["own_radial_movement"]:
    key = (row["previous_distance"], styles[row["own_previous_visible_weapon_style"]], styles[row["target_previous_visible_weapon_style"]])
    for move, count in row["counts"].items():
        movement[key][moves[move]] += count
attack_distance_count = max(key[0] for key in attack) + 1
move_distance_count = max(key[0] for key in movement) + 1

def marginalize(source, retained):
    result = defaultdict(Counter)
    for key, counts in source.items():
        result[tuple(key[index] for index in retained)].update(counts)
    return result

def table(name, source, dimensions):
    lines = ["static const uint32_t " + name + "[][RECORDED_NH_OUTCOME_COUNT] = {"]
    for key in itertools.product(*(range(size) for size in dimensions)):
        values = [source[key][outcome] for outcome in range(6)]
        assert sum(values) <= 2147483647
        lines.append("    {" + ", ".join(str(value) for value in values) + "},")
    lines.append("};")
    return "\n".join(lines)

header = """#ifndef OSRS_RECORDED_NH_PROFILE_H
#define OSRS_RECORDED_NH_PROFILE_H

#include <assert.h>
#include <stdint.h>
#include "osrs_types.h"

typedef enum {
    RECORDED_NH_MAGIC,
    RECORDED_NH_RANGED,
    RECORDED_NH_MELEE,
    RECORDED_NH_MELEE_SPECIAL,
    RECORDED_NH_ATTACK_COUNT,
} RecordedNhAttack;

typedef enum {
    RECORDED_NH_CLOSER,
    RECORDED_NH_SAME_DISTANCE,
    RECORDED_NH_FARTHER,
    RECORDED_NH_MOVE_COUNT,
} RecordedNhMove;

typedef enum {
    RECORDED_NH_EXACT,
    RECORDED_NH_DISTANCE_PREVIOUS,
    RECORDED_NH_DISTANCE,
    RECORDED_NH_CURRENT_WEAPON_PRAYER,
    RECORDED_NH_PREVIOUS_PRAYER,
    RECORDED_NH_DISTANCE_OWN_WEAPON,
    RECORDED_NH_GLOBAL_PRIOR,
} RecordedNhSupport;

enum {
    RECORDED_NH_STYLE_COUNT = ATTACK_STYLE_MAGIC + 1,
    RECORDED_NH_OUTCOME_COUNT = PRAYER_REDEMPTION + 1,
};

typedef struct {
    uint32_t counts[RECORDED_NH_OUTCOME_COUNT];
    uint32_t total;
    RecordedNhSupport support;
} RecordedNhWeights;
"""
header += f'\n#define RECORDED_NH_SOURCE_SHA256 "{arguments.source_sha256}"\n'
header += f'#define RECORDED_NH_FIT_SHA256 "{arguments.fit_sha256}"\n'
header += f'#define RECORDED_NH_SPLIT_SHA256 "{arguments.split_sha256}"\n'
header += f"enum {{ RECORDED_NH_ATTACK_DISTANCE_COUNT = {attack_distance_count}, RECORDED_NH_MOVE_DISTANCE_COUNT = {move_distance_count} }};\n\n"
tables = [
    ("RECORDED_NH_ATTACK_EXACT", attack, [attack_distance_count, 4, 6]),
    ("RECORDED_NH_ATTACK_DISTANCE_PREVIOUS", marginalize(attack, [0, 1]), [attack_distance_count, 4]),
    ("RECORDED_NH_ATTACK_DISTANCE", marginalize(attack, [0]), [attack_distance_count]),
    ("RECORDED_NH_ATTACK_PRIOR", marginalize(attack, []), []),
    ("RECORDED_NH_DEFENSE_EXACT", defense, [4, 4, 6]),
    ("RECORDED_NH_DEFENSE_CURRENT_PRAYER", marginalize(defense, [1, 2]), [4, 6]),
    ("RECORDED_NH_DEFENSE_PRAYER", marginalize(defense, [2]), [6]),
    ("RECORDED_NH_DEFENSE_PRIOR", marginalize(defense, []), []),
    ("RECORDED_NH_MOVE_EXACT", movement, [move_distance_count, 4, 4]),
    ("RECORDED_NH_MOVE_DISTANCE_OWN", marginalize(movement, [0, 1]), [move_distance_count, 4]),
    ("RECORDED_NH_MOVE_DISTANCE", marginalize(movement, [0]), [move_distance_count]),
    ("RECORDED_NH_MOVE_PRIOR", marginalize(movement, []), []),
]
header += "\n\n".join(table(name, source, dimensions) for name, source, dimensions in tables)
header += """

static inline RecordedNhWeights recorded_nh_weights(
    const uint32_t* counts, unsigned allowed, RecordedNhSupport support
) {
    RecordedNhWeights result = {{0}, 0, support};
    for (int outcome = 0; outcome < RECORDED_NH_OUTCOME_COUNT; outcome++) {
        if (!(allowed & (1u << outcome))) continue;
        result.counts[outcome] = counts[outcome];
        result.total += counts[outcome];
    }
    return result;
}

static inline int recorded_nh_sample(RecordedNhWeights weights, uint32_t draw) {
    assert(draw < weights.total);
    int outcome = 0;
    while (draw >= weights.counts[outcome]) {
        draw -= weights.counts[outcome];
        outcome++;
    }
    return outcome;
}

static inline RecordedNhWeights recorded_nh_attack_weights(
    int distance, AttackStyle previous, OverheadPrayer target, unsigned allowed
) {
    RecordedNhWeights result;
    if (distance < RECORDED_NH_ATTACK_DISTANCE_COUNT) {
        result = recorded_nh_weights(RECORDED_NH_ATTACK_EXACT[
            (distance * RECORDED_NH_STYLE_COUNT + previous) * RECORDED_NH_OUTCOME_COUNT + target], allowed, RECORDED_NH_EXACT);
        if (result.total) return result;
        result = recorded_nh_weights(RECORDED_NH_ATTACK_DISTANCE_PREVIOUS[
            distance * RECORDED_NH_STYLE_COUNT + previous], allowed, RECORDED_NH_DISTANCE_PREVIOUS);
        if (result.total) return result;
        result = recorded_nh_weights(RECORDED_NH_ATTACK_DISTANCE[distance],
            allowed, RECORDED_NH_DISTANCE);
        if (result.total) return result;
    }
    return recorded_nh_weights(RECORDED_NH_ATTACK_PRIOR[0], allowed,
        RECORDED_NH_GLOBAL_PRIOR);
}

static inline RecordedNhWeights recorded_nh_defense_weights(
    AttackStyle older, AttackStyle current, OverheadPrayer previous
) {
    unsigned allowed = (1u << RECORDED_NH_OUTCOME_COUNT) - 1u;
    RecordedNhWeights result = recorded_nh_weights(RECORDED_NH_DEFENSE_EXACT[
        (older * RECORDED_NH_STYLE_COUNT + current) * RECORDED_NH_OUTCOME_COUNT + previous], allowed, RECORDED_NH_EXACT);
    if (result.total) return result;
    result = recorded_nh_weights(RECORDED_NH_DEFENSE_CURRENT_PRAYER[
        current * RECORDED_NH_OUTCOME_COUNT + previous], allowed, RECORDED_NH_CURRENT_WEAPON_PRAYER);
    if (result.total) return result;
    result = recorded_nh_weights(RECORDED_NH_DEFENSE_PRAYER[previous],
        allowed, RECORDED_NH_PREVIOUS_PRAYER);
    if (result.total) return result;
    return recorded_nh_weights(RECORDED_NH_DEFENSE_PRIOR[0], allowed,
        RECORDED_NH_GLOBAL_PRIOR);
}

static inline RecordedNhWeights recorded_nh_move_weights(
    int distance, AttackStyle own, AttackStyle target, unsigned allowed
) {
    RecordedNhWeights result;
    if (distance < RECORDED_NH_MOVE_DISTANCE_COUNT) {
        result = recorded_nh_weights(RECORDED_NH_MOVE_EXACT[
            (distance * RECORDED_NH_STYLE_COUNT + own) * RECORDED_NH_STYLE_COUNT + target], allowed, RECORDED_NH_EXACT);
        if (result.total) return result;
        result = recorded_nh_weights(RECORDED_NH_MOVE_DISTANCE_OWN[
            distance * RECORDED_NH_STYLE_COUNT + own], allowed, RECORDED_NH_DISTANCE_OWN_WEAPON);
        if (result.total) return result;
        result = recorded_nh_weights(RECORDED_NH_MOVE_DISTANCE[distance],
            allowed, RECORDED_NH_DISTANCE);
        if (result.total) return result;
    }
    return recorded_nh_weights(RECORDED_NH_MOVE_PRIOR[0], allowed,
        RECORDED_NH_GLOBAL_PRIOR);
}

#endif
"""
arguments.output.write_text(header)
print(json.dumps({"fit_sha256": arguments.fit_sha256, "split_sha256": arguments.split_sha256, "header_sha256": hashlib.sha256(header.encode()).hexdigest(), "attack_observations": sum(sum(values.values()) for values in attack.values()), "transition_observations": sum(sum(values.values()) for values in defense.values()), "heldout_read": False}))
