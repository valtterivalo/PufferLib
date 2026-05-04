"""Stage D MVP runner: per-policy episode tail audit.

For each P-policy checkpoint:
  - Load weights into a fresh pufferl
  - Run rollouts until N normal-start episodes have terminated
  - Read aggregated log (means + new tail fractions)
  - Save JSON with per-policy stats

The new env metrics surfaced by my_log:
  frac_min_hp_le_300_normal, frac_min_hp_le_240_normal, frac_min_hp_le_150_normal
  frac_normal (n_normal / n_total)
The runner multiplies by total episodes to recover absolute counts.

Run from repo root:
  python experiments/heavy_research_phase2_v4/run_d_audit.py [--n-eps 20000]
"""

import argparse
import configparser
import json
import os
import sys
import time
from collections import defaultdict
from pathlib import Path

import pufferlib.pufferl as pl
from pufferlib import _C as backend


REPO = Path(__file__).resolve().parents[2]
DEFAULT_INI = REPO / "pufferlib/config/default.ini"
BASE_INI = REPO / "experiments/heavy_research_phase2_v4/sweeps/inferno_aprime_v3_base.ini"

POLICIES = [
    {"label": "P0", "id": "v3xzk1qs", "tag": "e3vyhuh9-s5", "expected_score": 0.7375},
    {"label": "P1", "id": "842dojh0", "tag": "e3vyhuh9-s1", "expected_score": 0.6284},
    {"label": "P2", "id": "t5k6svai", "tag": "kvuqyvh9-s4", "expected_score": 0.5914},
    {"label": "P3", "id": "fidtrqal", "tag": "fs4s6vst-s5", "expected_score": 0.5815},
    {"label": "P4", "id": "zpsbd3uq", "tag": "kvuqyvh9-s1", "expected_score": 0.5717},
]


def coerce(s):
    """ini values are strings; turn 'None' / 'True' / 'False' / '"x"' / numbers into Python."""
    if isinstance(s, str):
        st = s.strip()
        if st in ("None", "none"):
            return None
        if st in ("True", "true"):
            return True
        if st in ("False", "false"):
            return False
        if (st.startswith('"') and st.endswith('"')) or (st.startswith("'") and st.endswith("'")):
            return st[1:-1]
        try:
            return int(st)
        except ValueError:
            pass
        try:
            return float(st)
        except ValueError:
            pass
    return s


def load_args(load_path, total_agents=256, horizon=128):
    """Build pufferl args dict from ini files, no argparse."""
    p = configparser.ConfigParser()
    p.read([str(DEFAULT_INI), str(BASE_INI)])
    args = defaultdict(dict)
    for section in p.sections():
        for k, v in p[section].items():
            value = coerce(v)
            if section == "base":
                args[k] = value
            else:
                args[section][k] = value

    args["env_name"] = "osrs_inferno"
    args["load_model_path"] = load_path
    args["load_id"] = None
    args["render_mode"] = "None"
    args["wandb"] = False
    args["wandb_project"] = "puffer4"
    args["wandb_group"] = "debug"
    args["tag"] = None
    args["slowly"] = False
    args["save_frames"] = 0
    args["gif_path"] = "out.gif"
    args["fps"] = 15.0

    args["vec"]["total_agents"] = total_agents
    args["vec"]["num_buffers"] = 2
    args["vec"]["num_threads"] = 16
    args["train"]["horizon"] = horizon
    args["train"]["minibatch_size"] = total_agents * horizon
    args["train"]["total_timesteps"] = 10**12
    args["train"]["seed"] = 42
    args["reset_state"] = False
    args["rank"] = 0
    args["gpu_id"] = 0
    args["world_size"] = 1
    args["nccl_id"] = b""
    args["no_model_upload"] = True
    return dict(args)


def coerce_floats(d):
    out = {}
    for k, v in d.items():
        if isinstance(v, (int, float)):
            out[k] = float(v)
        elif isinstance(v, dict):
            out[k] = coerce_floats(v)
        else:
            try:
                out[k] = float(v)
            except (TypeError, ValueError):
                out[k] = v
    return out


def run_policy(label, run_id, ckpt_path, n_target, max_seconds=600.0):
    print(f"\n=== {label} {run_id} ckpt={ckpt_path} target={n_target} normal eps ===",
          flush=True)
    args = load_args(load_path=str(ckpt_path))
    if not Path(ckpt_path).exists():
        return {"label": label, "id": run_id, "error": f"ckpt missing: {ckpt_path}"}

    pl.validate_config(args)
    t0 = time.time()
    rollouts_done = 0
    last_n_normal = 0.0
    final_log = {}
    with pl._inferno_replay_env(args):
        pufferl = backend.create_pufferl(args)
        try:
            backend.load_weights(pufferl, str(ckpt_path))
            print(f"  loaded weights from {ckpt_path}", flush=True)

            while True:
                backend.rollouts(pufferl)
                rollouts_done += 1

                log = backend.eval_log(pufferl)
                env_log = log.get("env", {}) if isinstance(log, dict) else dict(log).get("env", {})
                env_log = dict(env_log) if not isinstance(env_log, dict) else env_log
                n_total = float(env_log.get("n", 0.0)) if env_log else 0.0
                frac_normal = float(env_log.get("frac_normal", 0.0)) if env_log else 0.0
                n_normal = n_total * frac_normal

                if rollouts_done % 10 == 0 or n_normal > last_n_normal + 500:
                    elapsed = time.time() - t0
                    print(f"  rollout {rollouts_done:4d}  n_total={n_total:6.0f}  "
                          f"n_normal={n_normal:6.0f}  elapsed={elapsed:5.1f}s",
                          flush=True)
                    last_n_normal = n_normal

                if n_normal >= n_target:
                    final_log = env_log
                    print(f"  reached target: n_normal={n_normal:.0f}", flush=True)
                    break
                if time.time() - t0 > max_seconds:
                    final_log = env_log
                    print(f"  timed out at n_normal={n_normal:.0f}", flush=True)
                    break
        finally:
            backend.close(pufferl)

    elapsed = time.time() - t0
    n_total = float(final_log.get("n", 0.0))
    frac_normal = float(final_log.get("frac_normal", 0.0))
    n_normal = n_total * frac_normal
    return {
        "label": label,
        "id": run_id,
        "ckpt": str(ckpt_path),
        "elapsed_seconds": elapsed,
        "rollouts": rollouts_done,
        "n_total": n_total,
        "n_normal": n_normal,
        "log": coerce_floats(dict(final_log)),
        "count_min_hp_le_300": float(final_log.get("frac_min_hp_le_300_normal", 0.0)) * n_normal,
        "count_min_hp_le_240": float(final_log.get("frac_min_hp_le_240_normal", 0.0)) * n_normal,
        "count_min_hp_le_150": float(final_log.get("frac_min_hp_le_150_normal", 0.0)) * n_normal,
        "count_wins": float(final_log.get("wins_normal", 0.0)) * n_normal,
        "mean_score": float(final_log.get("score_normal", 0.0)),
        "mean_min_hp": float(final_log.get("min_zuk_hp_normal", 0.0)),
        "mean_phase": float(final_log.get("phase_reached_normal", 0.0)),
        "mean_death_tick": float(final_log.get("death_tick_normal", 0.0)),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n-eps", type=int, default=20000,
                    help="target normal-start episodes per policy")
    ap.add_argument("--policies", default="all",
                    help="comma list of labels (e.g. P0,P1) or 'all'")
    ap.add_argument("--out", default=str(REPO / "experiments/heavy_research_phase2_v4/d_audit_results.json"))
    args = ap.parse_args()

    selected = POLICIES if args.policies == "all" else [
        p for p in POLICIES if p["label"] in args.policies.split(",")
    ]

    results = []
    for p in selected:
        ckpt_dir = REPO / "checkpoints/osrs_inferno" / p["id"]
        bins = sorted(ckpt_dir.glob("*.bin")) if ckpt_dir.exists() else []
        if not bins:
            print(f"  WARN: no checkpoint for {p['label']} {p['id']}, skipping", flush=True)
            results.append({"label": p["label"], "id": p["id"], "error": "no ckpt"})
            continue
        ckpt = bins[-1]
        res = run_policy(p["label"], p["id"], ckpt, args.n_eps)
        results.append(res)

        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        with open(out_path, "w") as f:
            json.dump({"policies": results}, f, indent=2)
        print(f"  wrote {out_path}", flush=True)

    print("\n=== D audit done ===")
    for r in results:
        if "error" in r:
            print(f"  {r['label']} {r['id']}: ERROR {r['error']}")
            continue
        print(f"  {r['label']} {r['id']:<10} n_normal={r['n_normal']:5.0f} "
              f"score={r['mean_score']:.3f} min_hp={r['mean_min_hp']:6.1f}  "
              f"<=300:{r['count_min_hp_le_300']:4.0f}  "
              f"<=240:{r['count_min_hp_le_240']:4.0f}  "
              f"<=150:{r['count_min_hp_le_150']:4.0f}  "
              f"wins:{r['count_wins']:.1f}")


if __name__ == "__main__":
    main()
