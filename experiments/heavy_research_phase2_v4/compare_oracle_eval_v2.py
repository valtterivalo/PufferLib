"""Compare round-5 oracle wrapper eval arms (E0/E4-E8).

Reads JSON outputs from run_oracle_eval.py and prints a side-by-side table.
"""

import json
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
OUT_DIR = REPO / "experiments/heavy_research_phase2_v4/oracle_eval_v2"

ARMS = [
    ("E0", "raw policy",                              0),
    ("E4", "target only @Jad-spawn",                  4),
    ("E5", "target + overhead @Jad-spawn",            5),
    ("E6", "target + gear + offensive @Jad-spawn",    6),
    ("E7", "FULL: t+g+offensive+overhead @Jad-spawn", 7),
    ("E8", "FULL: t+g+offensive+overhead @300",       8),
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
    ("env/frac_died_with_zuk_healer_alive_normal", "die_zuh"),
    ("env/frac_died_with_jad_healer_alive_normal", "die_jah"),
    ("env/frac_died_with_set_alive_normal", "die_set"),
    ("env/ticks_after_300_normal", "tk_a300"),
    ("env/damage_after_300_normal", "dmg_a300"),
    ("env/ticks_after_240_normal", "tk_a240"),
    ("env/damage_after_240_normal", "dmg_a240"),
    ("env/ticks_after_150_normal", "tk_a150"),
    ("env/damage_after_150_normal", "dmg_a150"),
    ("env/zuk_healer_damage", "heal_dmg"),
    ("env/deaths_to_jad", "p_dies_jad"),
    ("env/phase_reached_normal", "phase"),
    ("env/zuk_hp_remaining", "zhp_rem"),
    ("env/gear_switch_rate", "gear_sw"),
    ("env/prayer_correct_rate", "pray_ok"),
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
    print("ARMS:")
    for label, desc, mode in ARMS:
        print(f"  {label}  mode={mode}  n_normal={n_total_per[label]:>5}  {desc}")
    print()

    label_w = max(len(short) for _, short in KEYS) + 2
    arm_w = 9
    header = f"{'metric':<{label_w}}" + "".join(f"{label:>{arm_w}}" for label, _, _ in ARMS)
    print(header)
    print("-" * len(header))
    for full_key, short in KEYS:
        line = f"{short:<{label_w}}"
        for label, _, _ in ARMS:
            v = rows[label]["metrics"].get(full_key, None)
            if v is None:
                cell = "n/a"
            elif abs(v) >= 100:
                cell = f"{v:.1f}"
            elif abs(v) >= 1:
                cell = f"{v:.3f}"
            else:
                cell = f"{v:.4f}"
            line += f"{cell:>{arm_w}}"
        print(line)
    print()

    print("Heavy agent r5 gates (vs E0 raw):")
    e0 = rows["E0"]["metrics"]
    e0_n = n_total_per["E0"]
    e0_jad_kill_frac = 1.0 - e0.get("env/frac_died_with_jad_alive_normal", 1.0) - 0  # rough
    for label, desc, mode in ARMS[1:]:
        ev = rows[label]["metrics"]
        ev_n = n_total_per[label]
        score_d = ev.get("env/score_normal", 0) - e0.get("env/score_normal", 0)
        wins = int(ev.get("env/wins_normal", 0) * ev_n)
        ev_jad_kill_frac = 1.0 - ev.get("env/frac_died_with_jad_alive_normal", 1.0)
        die_jad = ev.get("env/frac_died_with_jad_alive_normal", 0)
        fr150 = ev.get("env/frac_min_hp_le_150_normal", 0)
        fr150_e0 = e0.get("env/frac_min_hp_le_150_normal", 0)
        fr240 = ev.get("env/frac_min_hp_le_240_normal", 0)
        print(f"  {label} ({desc}):")
        print(f"    jad_kills frac     = {ev_jad_kill_frac:.4f}   target >=0.25 {'PASS' if ev_jad_kill_frac >= 0.25 else 'fail'}")
        print(f"    die_jad            = {die_jad:.4f}   target <0.65  {'PASS' if die_jad < 0.65 else 'fail'}")
        print(f"    fr<=240            = {fr240:.4f}   target >=0.18 {'PASS' if fr240 >= 0.18 else 'fail'}")
        print(f"    fr<=150            = {fr150:.4f}   target >={fr150_e0*2:.4f} {'PASS' if fr150 >= fr150_e0 * 2 else 'fail'}")
        print(f"    score delta        = {score_d:+.4f}   target >=-0.03 {'PASS' if score_d >= -0.03 else 'fail (sharp drop)'}")
        print(f"    wins               = {wins}        target >0     {'PASS' if wins > 0 else 'fail'}")


if __name__ == "__main__":
    main()
