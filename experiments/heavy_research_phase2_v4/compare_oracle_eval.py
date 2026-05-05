"""Compare oracle wrapper eval arms (E0/E1/E2/E3).

Reads JSON outputs from run_oracle_eval.py and prints a side-by-side table of
the metrics that matter for heavy agent r4 step 1 gates.
"""

import json
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
OUT_DIR = REPO / "experiments/heavy_research_phase2_v4/oracle_eval"

ARMS = [
    ("E0", "raw policy", 0),
    ("E1", "Jad-only @300", 1),
    ("E2", "full priority @300", 2),
    ("E3", "full priority @240", 3),
]

KEYS = [
    ("env/score_normal", "score"),
    ("env/episode_return_normal", "ret_n"),
    ("env/wins_normal", "wins"),
    ("env/min_zuk_hp_normal", "min_zhp"),
    ("env/frac_min_hp_le_300_normal", "fr<=300"),
    ("env/frac_min_hp_le_240_normal", "fr<=240"),
    ("env/frac_min_hp_le_150_normal", "fr<=150"),
    ("env/frac_died_with_jad_alive_normal", "die_jad"),
    ("env/frac_died_with_healer_alive_normal", "die_heal"),
    ("env/frac_died_with_set_alive_normal", "die_set"),
    ("env/ticks_after_300_normal", "tk_a300"),
    ("env/damage_after_300_normal", "dmg_a300"),
    ("env/ticks_after_240_normal", "tk_a240"),
    ("env/damage_after_240_normal", "dmg_a240"),
    ("env/ticks_after_150_normal", "tk_a150"),
    ("env/damage_after_150_normal", "dmg_a150"),
    ("env/zuk_healer_damage", "heal_dmg"),
    ("env/deaths_to_jad", "jad_kills"),
    ("env/phase_reached_normal", "phase"),
    ("env/zuk_hp_remaining", "zhp_rem"),
]


def load_arm(label):
    path = OUT_DIR / f"{label}.json"
    if not path.exists():
        return None
    with open(path) as f:
        return json.load(f)


def main():
    rows = {label: load_arm(label) for label, _, _ in ARMS}
    missing = [label for label, data in rows.items() if data is None]
    if missing:
        print(f"missing: {missing}")
        sys.exit(1)

    n_total_per = {label: int(rows[label]["metrics"].get("__n_total__", 0)) for label, _, _ in ARMS}
    print(f"n_normal episodes: " +
          " ".join(f"{label}={n_total_per[label]}" for label, _, _ in ARMS))
    print()

    label_w = max(len(short) for _, short in KEYS) + 2
    arm_w = 12
    header = f"{'metric':<{label_w}}" + "".join(f"{label:>{arm_w}}" for label, _, _ in ARMS)
    print(header)
    print("-" * len(header))
    for full_key, short in KEYS:
        line = f"{short:<{label_w}}"
        for label, _, _ in ARMS:
            v = rows[label]["metrics"].get(full_key, None)
            if v is None:
                cell = "n/a"
            elif abs(v) > 100:
                cell = f"{v:.1f}"
            elif abs(v) > 1:
                cell = f"{v:.3f}"
            else:
                cell = f"{v:.4f}"
            line += f"{cell:>{arm_w}}"
        print(line)
    print()

    print("Heavy agent gates vs E0 (raw):")
    e0 = rows["E0"]["metrics"]
    for label, desc, mode in ARMS[1:]:
        ev = rows[label]["metrics"]
        score_d = ev.get("env/score_normal", 0) - e0.get("env/score_normal", 0)
        wins = int(ev.get("env/wins_normal", 0) * n_total_per[label])
        fr150 = ev.get("env/frac_min_hp_le_150_normal", 0)
        fr150_e0 = e0.get("env/frac_min_hp_le_150_normal", 0)
        fr150_x = (fr150 / fr150_e0) if fr150_e0 > 0 else 0
        die_jad_d = e0.get("env/frac_died_with_jad_alive_normal", 0) - ev.get("env/frac_died_with_jad_alive_normal", 0)
        die_jad_pct = (die_jad_d / e0.get("env/frac_died_with_jad_alive_normal", 1)) * 100 if e0.get("env/frac_died_with_jad_alive_normal", 0) > 0 else 0
        print(f"  {label} ({desc}):")
        print(f"    score    delta = {score_d:+.4f}    {'PASS (>=0.03)' if score_d >= 0.03 else 'fail'}")
        print(f"    wins     count = {wins}             {'PASS (>0)' if wins > 0 else 'fail'}")
        print(f"    fr<=150  ratio = {fr150_x:.2f}x       {'PASS (>=2x)' if fr150_x >= 2.0 else 'fail'}")
        print(f"    die_jad  drop  = {die_jad_pct:+.1f}%   {'PASS (>=30%)' if die_jad_pct >= 30 else 'fail'}")


if __name__ == "__main__":
    main()
