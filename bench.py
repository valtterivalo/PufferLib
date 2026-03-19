"""Single-run Metal training via CLI args.

Requires building the env first: python setup.py build_<env> --force

Usage:
    python bench.py --env breakout --total-timesteps 5000000
    python bench.py --env osrs_inferno --hidden-size 256 --num-layers 2
"""

import argparse
import json
import time
from pathlib import Path

from train import ENV_DEFAULTS, build_configs, run_training


def parse_args():
    p = argparse.ArgumentParser(description="Metal training for simple envs")
    p.add_argument("--env", type=str, required=True,
                   choices=list(ENV_DEFAULTS.keys()))
    p.add_argument("--total-agents", type=int, default=2048)
    p.add_argument("--hidden-size", type=int, default=128)
    p.add_argument("--num-layers", type=int, default=1)
    p.add_argument("--horizon", type=int, default=32)
    p.add_argument("--total-timesteps", type=int, default=5_000_000)
    p.add_argument("--learning-rate", type=float, default=0.001)
    p.add_argument("--beta1", type=float, default=0.95)
    p.add_argument("--beta2", type=float, default=0.999)
    p.add_argument("--eps", type=float, default=1e-12)
    p.add_argument("--minibatch-size", type=int, default=4096)
    p.add_argument("--replay-ratio", type=float, default=0.25,
                   help="minibatch replays per rollout. values above ~0.5 cause catastrophic "
                        "policy drift in multi-head action spaces (7+ heads). breakout (1 head) "
                        "tolerates 1.9+, osrs_pvp (7 heads) needs 0.25-0.5.")
    p.add_argument("--ent-coef", type=float, default=0.01)
    p.add_argument("--gamma", type=float, default=0.99)
    p.add_argument("--gae-lambda", type=float, default=0.95)
    p.add_argument("--vtrace-rho-clip", type=float, default=1.0)
    p.add_argument("--vtrace-c-clip", type=float, default=1.0)
    p.add_argument("--prio-alpha", type=float, default=0.0)
    p.add_argument("--prio-beta0", type=float, default=0.4)
    p.add_argument("--clip-coef", type=float, default=0.2)
    p.add_argument("--vf-coef", type=float, default=0.5)
    p.add_argument("--vf-clip-coef", type=float, default=0.1)
    p.add_argument("--max-grad-norm", type=float, default=0.5)
    p.add_argument("--no-overlap", action="store_true")
    p.add_argument("--log-interval", type=int, default=10)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--trace-path", type=str, default="",
                   help="optional jsonl trace output path for parity debugging")
    p.add_argument("--trace-every", type=int, default=1,
                   help="write one trace row every N training iterations")
    p.add_argument("--num-buffers", type=int, default=1)
    p.add_argument("--profile", action="store_true",
                   help="GPU sync after each training phase for accurate per-kernel profiling")
    p.add_argument("--cpu-inference", action="store_true",
                   help="CPU forward pass during rollout (no GPU sync, uses Accelerate cblas)")
    p.add_argument("--fp16", action="store_true",
                   help="fp16 training activations/grads (rollout stays fp32)")
    p.add_argument("--ns-iters", type=int, default=5,
                   help="Newton-Schulz iterations in muon optimizer (1-5, default 5)")
    p.add_argument("--scaffolding-ratio", type=float, default=None,
                   help="Override env scaffolding_ratio (g2048 only)")
    p.add_argument("--min-lr-ratio", type=float, default=0.0,
                   help="minimum LR as ratio of initial (upstream default: 0.0)")
    return p.parse_args()


def main():
    args = parse_args()

    params = {
        "train": {
            "horizon": args.horizon,
            "learning_rate": args.learning_rate,
            "min_lr_ratio": args.min_lr_ratio,
            "beta1": args.beta1,
            "beta2": args.beta2,
            "eps": args.eps,
            "minibatch_size": args.minibatch_size,
            "replay_ratio": args.replay_ratio,
            "total_timesteps": args.total_timesteps,
            "max_grad_norm": args.max_grad_norm,
            "clip_coef": args.clip_coef,
            "vf_clip_coef": args.vf_clip_coef,
            "vf_coef": args.vf_coef,
            "ent_coef": args.ent_coef,
            "gamma": args.gamma,
            "gae_lambda": args.gae_lambda,
            "vtrace_rho_clip": args.vtrace_rho_clip,
            "vtrace_c_clip": args.vtrace_c_clip,
            "prio_alpha": args.prio_alpha,
            "prio_beta0": args.prio_beta0,
            "total_agents": args.total_agents,
            "num_buffers": args.num_buffers,
            "profile": 1 if args.profile else 0,
            "overlap": 0 if args.no_overlap else 1,
            "cpu_inference": 1 if args.cpu_inference else 0,
            "train_fp16": 1 if args.fp16 else 0,
            "ns_iters": args.ns_iters,
            "seed": args.seed,
        },
        "policy": {
            "hidden_size": args.hidden_size,
            "num_layers": args.num_layers,
        },
    }
    if args.scaffolding_ratio is not None:
        params["train"]["scaffolding_ratio"] = args.scaffolding_ratio

    config, vec_config, env_config, policy_config = build_configs(args.env, params)

    print(f"env={args.env}, agents={args.total_agents}, hidden={args.hidden_size}, "
          f"layers={args.num_layers}, horizon={args.horizon}, overlap={not args.no_overlap}, "
          f"cpu_infer={args.cpu_inference}, fp16={args.fp16}, seed={args.seed}")

    # optional trace output
    trace_file = None
    trace_every = max(int(args.trace_every), 1)
    if args.trace_path:
        trace_path = Path(args.trace_path).expanduser().resolve()
        trace_path.parent.mkdir(parents=True, exist_ok=True)
        trace_file = trace_path.open("w", encoding="utf-8")
        trace_file.write(json.dumps({
            "event": "meta", "env": args.env, "seed": args.seed,
            "total_agents": args.total_agents, "hidden_size": args.hidden_size,
            "num_layers": args.num_layers, "horizon": args.horizon,
            "total_timesteps": args.total_timesteps, "learning_rate": args.learning_rate,
            "optimizer": "muon", "trace_every": trace_every,
        }) + "\n")
        trace_file.flush()

    def on_log(iteration, global_step, sps, losses, env_stats):
        ent = losses.get("entropy", 0)
        pg = losses.get("pg_loss", 0)
        vf = losses.get("vf_loss", 0)
        ep_ret = env_stats.get("episode_return", 0)
        score = env_stats.get("score", ep_ret)
        ep_len = env_stats.get("episode_length", 0)
        print(f"[step={global_step:>10,} | SPS={sps:>10,.0f} | "
              f"ret={ep_ret:>8.2f} score={score:>8.2f} len={ep_len:>6.0f} | "
              f"ent={ent:.3f} pg={pg:.4f} vf={vf:.4f}]")

        if trace_file and iteration % trace_every == 0:
            from pufferlib import _C as _c
            trace_row = {
                "event": "tick", "iteration": iteration, "step": global_step,
                "sps": sps, "score": score, "episode_return": ep_ret,
                "episode_length": ep_len, "entropy": ent,
                "pg_loss": pg, "vf_loss": vf,
            }
            trace_file.write(json.dumps(trace_row) + "\n")

    print(f"model params: {int(config.get('total_timesteps', 0)):,} steps target")

    result = run_training(
        config, vec_config, env_config, policy_config,
        log_interval=args.log_interval,
        on_log=on_log,
    )

    print(f"\ndone. {result.steps:,} steps in {result.elapsed:.1f}s")
    print(f"avg SPS: {result.sps:,.0f}")
    for k, v in sorted(result.profile.items()):
        print(f"  {k}: {v:.3f}")

    if trace_file:
        trace_file.write(json.dumps({
            "event": "final", "step": result.steps,
            "total_time_seconds": result.elapsed, "avg_sps": result.sps,
            "profile": result.profile,
        }) + "\n")
        trace_file.close()


if __name__ == "__main__":
    main()
