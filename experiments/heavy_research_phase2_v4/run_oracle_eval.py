"""Oracle target-priority wrapper eval (heavy agent r4 step 1).

Runs N normal-start episodes per oracle_mode arm and reports D-deep metrics.

  E0  oracle_mode=0  raw policy
  E1  oracle_mode=1  Jad-only override @ zuk_hp <= 300
  E2  oracle_mode=2  full priority     @ zuk_hp <= 300
  E3  oracle_mode=3  full priority     @ zuk_hp <= 240

Run from repo root:
  python experiments/heavy_research_phase2_v4/run_oracle_eval.py \\
      --checkpoint checkpoints/osrs_inferno/p2k4szzs/0000000049971200.bin \\
      --oracle-mode 0 --target-episodes 20000 \\
      --output experiments/heavy_research_phase2_v4/oracle_eval/E0.json
"""

import argparse
import json
import os
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
DEFAULT_CFG = REPO / "experiments/heavy_research_phase2_v4/sweeps/inferno_c_base.ini"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--env", default="osrs_inferno")
    p.add_argument("--checkpoint", required=True)
    p.add_argument("--oracle-mode", type=int, default=0, choices=[0, 1, 2, 3])
    p.add_argument("--target-episodes", type=int, default=20000)
    p.add_argument("--total-agents", type=int, default=256)
    p.add_argument("--num-buffers", type=int, default=2)
    p.add_argument("--horizon", type=int, default=128)
    p.add_argument("--config-file", default=str(DEFAULT_CFG))
    p.add_argument("--output", required=True)
    args_cli = p.parse_args()

    sys.argv = [sys.argv[0]]
    os.environ["PUFFER_CONFIG_FILE"] = args_cli.config_file

    from pufferlib.pufferl import (
        load_config, _resolve_backend, _inferno_replay_env, unroll_nested_dict,
    )

    args = load_config(args_cli.env)
    args["train"]["total_timesteps"] = 10**12
    args["train"]["horizon"] = args_cli.horizon
    args["vec"]["total_agents"] = args_cli.total_agents
    args["vec"]["num_buffers"] = args_cli.num_buffers
    args["env"]["phase2_demo_dir"] = ""
    args["env"]["oracle_mode"] = float(args_cli.oracle_mode)
    args["reset_state"] = False
    if args["train"]["minibatch_size"] > args_cli.total_agents:
        args["train"]["minibatch_size"] = args_cli.total_agents

    backend = _resolve_backend(args)

    print(
        f"oracle_mode={args_cli.oracle_mode} target={args_cli.target_episodes} "
        f"total_agents={args_cli.total_agents} horizon={args_cli.horizon} "
        f"ckpt={args_cli.checkpoint}",
        flush=True,
    )

    Path(args_cli.output).parent.mkdir(parents=True, exist_ok=True)

    with _inferno_replay_env(args):
        pufferl = backend.create_pufferl(args)
        backend.load_weights(pufferl, args_cli.checkpoint)

        t0 = time.time()
        last_print_n = 0

        n_total = 0.0
        weighted_sum = {}
        rollout_idx = 0
        while True:
            backend.rollouts(pufferl)
            rollout_idx += 1
            log = dict(unroll_nested_dict(backend.log(pufferl)))
            n_this = float(log.get("env/n_normal", 0.0) or log.get("env/n", 0.0))
            if n_this > 0:
                for k, v in log.items():
                    if not k.startswith("env/"):
                        continue
                    if not isinstance(v, (int, float)):
                        continue
                    weighted_sum[k] = weighted_sum.get(k, 0.0) + float(v) * n_this
                n_total += n_this
            if rollout_idx <= 5 or n_total - last_print_n >= 1000:
                elapsed = time.time() - t0
                rate = n_total / max(elapsed, 0.1)
                print(
                    f"  rollout={rollout_idx} n_this={n_this:.0f} n_total={n_total:.0f} "
                    f"elapsed={elapsed:.1f}s rate={rate:.1f}/s",
                    flush=True,
                )
                last_print_n = n_total
            if n_total >= args_cli.target_episodes:
                break

        final = {k: v / n_total for k, v in weighted_sum.items()} if n_total > 0 else {}
        final["__n_total__"] = n_total
        final["__num_rollouts__"] = rollout_idx
        backend.close(pufferl)

    elapsed = time.time() - t0
    print(f"done. elapsed={elapsed:.0f}s n_normal={int(final.get('env/n_normal', 0))}",
          flush=True)

    serialisable = {
        k: float(v) if isinstance(v, (int, float)) else str(v)
        for k, v in final.items()
    }
    out = {
        "oracle_mode": args_cli.oracle_mode,
        "checkpoint": args_cli.checkpoint,
        "target_episodes": args_cli.target_episodes,
        "elapsed_seconds": elapsed,
        "metrics": serialisable,
    }
    with open(args_cli.output, "w") as f:
        json.dump(out, f, indent=2)
    print(f"wrote {args_cli.output}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
