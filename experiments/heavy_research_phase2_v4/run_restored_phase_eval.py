"""Evaluate a checkpoint from phase-filtered Go-Explore restored starts."""

import argparse
import json
import os
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
PHASES = {
    "pre_healer": 1,
    "immediate_healer": 2,
    "partial_healer": 3,
    "post_healer": 4,
    "post_150": 5,
}


def phase_names(selected: str) -> list[str]:
    if selected == "all":
        return list(PHASES.keys())
    if selected not in PHASES:
        raise ValueError(f"unknown phase {selected}")
    return [selected]


def weighted_env_metrics(log: dict[str, object], n_this: float) -> dict[str, float]:
    weighted = {}
    for key, value in log.items():
        if not key.startswith("env/"):
            continue
        if not isinstance(value, (int, float)):
            continue
        weighted[key] = float(value) * n_this
    return weighted


def run_phase(args_cli: argparse.Namespace, name: str) -> dict[str, object]:
    sys.argv = [sys.argv[0]]
    if args_cli.config_file:
        os.environ["PUFFER_CONFIG_FILE"] = args_cli.config_file
    else:
        os.environ.pop("PUFFER_CONFIG_FILE", None)

    from pufferlib.pufferl import (
        _inferno_replay_env,
        _resolve_backend,
        load_config,
        unroll_nested_dict,
    )

    args = load_config(args_cli.env)
    args["train"]["total_timesteps"] = 10**12
    args["train"]["horizon"] = args_cli.horizon
    args["vec"]["total_agents"] = args_cli.total_agents
    args["vec"]["num_buffers"] = args_cli.num_buffers
    args["env"]["phase2_demo_dir"] = args_cli.demo_dir
    args["env"]["phase2_normal_start_frac"] = args_cli.normal_start_frac
    args["env"]["phase2_randomize_rng_frac"] = args_cli.randomize_rng_frac
    args["env"]["phase2_diagnostic_phase"] = float(PHASES[name])
    args["env"]["phase2_diagnostic_tries"] = float(args_cli.diagnostic_tries)
    args["env"]["oracle_mode"] = float(args_cli.oracle_mode)
    args["reset_state"] = False
    if args["train"]["minibatch_size"] > args_cli.total_agents:
        args["train"]["minibatch_size"] = args_cli.total_agents

    backend = _resolve_backend(args)
    print(
        f"phase={name} target={args_cli.target_episodes} "
        f"agents={args_cli.total_agents} horizon={args_cli.horizon}",
        flush=True,
    )

    with _inferno_replay_env(args):
        pufferl = backend.create_pufferl(args)
        backend.load_weights(pufferl, args_cli.checkpoint)
        loaded = backend.phase2_init(
            pufferl,
            demo_dir=args_cli.demo_dir,
            num_atns=args["env"].get("phase2_num_atns", 9),
            snapshot_stride=args["env"].get("phase2_snapshot_stride", 4),
            max_demos=args["env"].get("phase2_max_demos", 64),
            seed=args["env"].get("phase2_seed", 42),
            normal_start_frac=args_cli.normal_start_frac,
            randomize_rng_frac=args_cli.randomize_rng_frac,
            bc_coef=args["env"].get("phase2_bc_coef", 0.0),
            bc_demos_per_minibatch=args["env"].get("phase2_bc_demos_per_minibatch", 0),
            promote_rate=args["env"].get("phase2_promote_rate", 0.30),
            demote_rate=args["env"].get("phase2_demote_rate", 0.10),
            backstep_ticks=args["env"].get("phase2_backstep_ticks", 4),
            success_q_delta=args["env"].get("phase2_success_q_delta", 0.005),
        )
        backend.phase2_reset(pufferl)
        print(f"phase2 demos loaded={loaded}", flush=True)

        t0 = time.time()
        last_print_n = 0.0
        n_total = 0.0
        weighted_sum: dict[str, float] = {}
        rollout_idx = 0
        while n_total < args_cli.target_episodes:
            backend.rollouts(pufferl)
            rollout_idx += 1
            log = dict(unroll_nested_dict(backend.log(pufferl)))
            n_this = float(log.get("env/n", 0.0) or log.get("env/n_normal", 0.0))
            if n_this > 0.0:
                for key, value in weighted_env_metrics(log, n_this).items():
                    weighted_sum[key] = weighted_sum.get(key, 0.0) + value
                n_total += n_this
            if rollout_idx <= 5 or n_total - last_print_n >= 1000:
                elapsed = time.time() - t0
                rate = n_total / max(elapsed, 0.1)
                snapshot_frac = float(log.get("env/snapshot_frac", 0.0))
                print(
                    f"  rollout={rollout_idx} n_total={n_total:.0f} "
                    f"snapshot_frac={snapshot_frac:.3f} rate={rate:.1f}/s",
                    flush=True,
                )
                last_print_n = n_total

        final = {key: value / n_total for key, value in weighted_sum.items()}
        final["__n_total__"] = n_total
        final["__num_rollouts__"] = float(rollout_idx)
        backend.close(pufferl)

    elapsed = time.time() - t0
    return {
        "phase": name,
        "phase_id": PHASES[name],
        "checkpoint": args_cli.checkpoint,
        "demo_dir": args_cli.demo_dir,
        "target_episodes": args_cli.target_episodes,
        "elapsed_seconds": elapsed,
        "metrics": final,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", default="osrs_inferno")
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--demo-dir", required=True)
    parser.add_argument("--phase", default="all", choices=["all", *PHASES.keys()])
    parser.add_argument("--target-episodes", type=int, default=10000)
    parser.add_argument("--total-agents", type=int, default=256)
    parser.add_argument("--num-buffers", type=int, default=2)
    parser.add_argument("--horizon", type=int, default=128)
    parser.add_argument("--normal-start-frac", type=float, default=0.0)
    parser.add_argument("--randomize-rng-frac", type=float, default=0.0)
    parser.add_argument("--diagnostic-tries", type=int, default=256)
    parser.add_argument("--oracle-mode", type=int, default=0)
    parser.add_argument("--config-file", default="")
    parser.add_argument("--output", required=True)
    args_cli = parser.parse_args()

    results = [run_phase(args_cli, name) for name in phase_names(args_cli.phase)]

    Path(args_cli.output).parent.mkdir(parents=True, exist_ok=True)
    with open(args_cli.output, "w") as f:
        json.dump({"results": results}, f, indent=2)
    print(f"wrote {args_cli.output}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
