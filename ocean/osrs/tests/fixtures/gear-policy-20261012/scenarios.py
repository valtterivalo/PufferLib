#!/usr/bin/env python3
"""Generate 360 bounded server REQUESTs, plus a provenance/domain manifest.

No engine or future RNG truth. The baseline R is a SYNTHETIC_EXPLICIT_PLAN,
not a Java-production oracle. Train and holdout outputs must be evaluated with
separate opponent model/profile pools; the wire has no profile-pool selector.
"""
import argparse
import json
from pathlib import Path

HP = (25, 48, 65, 93, 99)
ENEMY_HP = (3, 35, 55, 85, 99)
READY = (-1, 1, 3)  # exact observed global attack clock; unknown deliberately excluded
WEAPONS = (27867, 27857, 24617)  # BH abyssal dagger(p++), BH dragon mace, blighted VLS
ARMOR = (10828, 21295, 1725, 10551, 4087, 7462, 11840)
# Exact controlled stakes ratios, paired across every one of the120 physical cases.
STAKES = ((53370, 1000000), (787200, 1000000), (1746000, 1000000))


def request(index, split, risk_index):
    # PID x local HP x readiness x four nuisance strata x three stakes =360 requests.
    pid = index // 60
    hp_index = (index // 12) % 5
    ready_index = (index // 4) % 3
    stratum = index % 4
    hp = HP[hp_index]
    stakes_him, stakes_us = STAKES[risk_index]
    enemy_hp = ENEMY_HP[(hp_index + ready_index + stratum) % 5]
    energy = (50, 100)[stratum % 2]
    enemy_energy = (0, 100)[(stratum // 2 + ready_index) % 2]
    weapon = WEAPONS[(ready_index + stratum + hp_index) % 3]
    veng = (hp_index + ready_index + stratum) % 3
    enemy_veng = (hp_index + stratum) % 2
    lockout = (stratum + hp_index) % 2
    pulse = (index // 2) % 3  # empty, known direct incoming, known reflection incoming
    tick = 100
    number = index * len(STAKES) + risk_index + (1 if split == "train" else 2001)
    cooldown = READY[ready_index]
    worn = [10828, 21295, 1725, 27690, 10551, 22322, -1, 4087, -1, 7462, 11840, -1, -1]
    bag = [13441, 3144, 6685, 3024, 12695, 24621] + [-1] * 22
    # Up means active; castable means down/zero cooldown; spent means down/positive cooldown.
    up, veng_cd = int(veng == 0), 0 if veng != 2 else 30
    lines = [f"LA {number} {tick}",
             "O " + " ".join(map(str, (hp, 90, 118, 118, 118, 99, energy, 0, 0,
                 up, veng_cd, 10, 0, cooldown, 2 if lockout else 0,
                 2 if lockout else 0, 1 if lockout else 0, 1, 0, 4, 4, 4, 4,
                 weapon, 0, 1, 0))) + f" OBSERVED {cooldown} {cooldown}",
             "SHIELD OBSERVED 22322", "ARMOR OBSERVED " + " ".join(map(str, ARMOR)),
             f"B {enemy_hp} {enemy_hp} {enemy_energy} {enemy_energy} {1 if enemy_veng else 2} {pid} 0 1 101 101 {103 if lockout else 101} 0 1 1 {(enemy_hp * 30 + 98) // 99}",
             f"K {stakes_him} {stakes_us}", f"F 1 {int(not lockout)} {int(veng == 1)} {energy}",
             "W " + " ".join(map(str, worn)), "G " + " ".join(map(str, bag)),
             # Immunity -54 guarantees at least1590 ticks, exceeds admitted15-tick horizon.
             "PS 0 IMMUNE -54", "PS 1 CLEAR_PRIOR", "RA OFF_PRIOR",
             f"P KNOWN {int(pulse != 0)}"]
    if pulse:
        # These are already launched observed distributions, not sampled future rolls.
        # Reflection is exact; direct damage remains unknown within observed launch support.
        if pulse == 1:
            lines.append("D 1 0 0 1 0 0 2 0 10 30 10000 0 0")
        else:
            lines.append("D 1 1 2 0 0 1 1 15 15 15 10000 0 0")
    commands = []
    if hp <= 48 and not lockout:
        commands.append("C 101 0 0 0")
    if veng == 1:
        commands.append("C 101 3 0 0")
    commands += ["C 101 17 0 0", "C 101 18 104 1", "C 101 2 0 0", "C 101 1 0 0"]
    lines += [f"R KNOWN {len(commands)}", *commands, "END"]
    return "\n".join(lines) + "\n", dict(id=number, pid=pid, hp=hp, enemy_hp=enemy_hp,
        local_global_clock=cooldown, energy=energy, enemy_energy=enemy_energy,
        vengeance=("up", "castable", "spent")[veng], enemy_vengeance="up" if enemy_veng else "spent",
        food_lockout=bool(lockout), pending=("empty", "direct_distribution", "known_reflection")[pulse],
        observed_enemy_weapon=weapon, baseline="SYNTHETIC_EXPLICIT_PLAN", split=split,
        physical_case=index, risk_stratum=risk_index, stakes_him=stakes_him,
        stakes_us=stakes_us, stakes_ratio=stakes_him / stakes_us,
        auto_retaliate="OFF_PRIOR_DECLARED_SYNTHETIC_COHORT")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--split", choices=("train", "holdout"), default="train")
    parser.add_argument("--append-exported-request", type=Path, action="append", default=[],
                        help="Append a parent-provided complete actual export unchanged, never transplant its plan")
    args = parser.parse_args()
    requests, cases = zip(*(request(i, args.split, risk)
                           for i in range(120) for risk in range(len(STAKES))))
    text = "".join(requests)
    supplied = []
    for path in args.append_exported_request:
        export = path.read_text()
        if not export.startswith("LA ") or not export.endswith("END\n"):
            parser.error(f"{path}: requires complete server REQUEST ending END newline")
        text += export
        supplied.append(dict(path=str(path), provenance="PARENT_SUPPLIED_ACTUAL_EXPORT_UNMODIFIED"))
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "requests.txt").write_text(text)
    manifest = dict(version=2, physical_case_count=120, synthetic_count=120 * len(STAKES),
        cases=cases, supplied_exports=supplied,
        opponent_profiles=[0, 35] if args.split == "train" else [36, 43],
        domain=dict(horizon_ticks=15, pid="both exact holders, flip0", local_hp=HP, enemy_hp=ENEMY_HP,
            local_global_ready=READY, unknown_local_clock="excluded", local_energy=[50, 100],
            enemy_energy=[0, 100], local_vengeance=["up", "castable", "spent"],
            enemy_vengeance=["up", "spent"], opponent_gear="full observed composition",
            stakes_ratios=[him / us for him, us in STAKES],
            auto_retaliate="explicit OFF_PRIOR synthetic cohort; not observed raw172",
            poison="local negative varp-54 horizon immunity; opponent declared clean initial prior",
            pending="explicit empty or already-launched observed damage support; no RNG future truth",
            design="full pairing of three stakes ratios with120 selected physical cases; not exhaustive factorial or independent holdout states"),
        assumptions=["Baseline is synthetic explicit plan, NOT actual Java closed-loop production policy",
            "C native opponent/actuator proxy; phase/information parity requires independent proof",
            "BH dagger admitted only against declared immunity lasting beyond the full horizon",
            "No positive-poison timer reconstruction; immunity phase is conservative, not exact",
            "Holdout requires separately selected profiles36..43/model; requests do not select profiles",
            "Exact enemy HP/energy are controlled belief supports, not claims of live observability",
            "Auto-retaliate OFF is a declared synthetic counterfactual cohort, not exported live truth",
            "Appended actual exports retain their original RA admission unchanged; UNKNOWN is never forced OFF",
            "Bounded15-tick current-plan decision experiment, not optimal policy or long-run certification"])
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
