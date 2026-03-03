"""Metal training for simple envs (breakout, g2048, etc).

Requires building the env first: python setup.py build_<env> --force

Usage:
    python bench.py --env breakout --total-timesteps 5000000
    python bench.py --env g2048 --total-timesteps 10000000
"""

import argparse
import json
import time
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pufferlib import _C

# Default env configs (kwargs passed to my_init via Dict)
ENV_DEFAULTS = {
    "breakout": {
        "frameskip": 4.0,
        "width": 576.0,
        "height": 330.0,
        "paddle_width": 62.0,
        "paddle_height": 8.0,
        "ball_width": 32.0,
        "ball_height": 32.0,
        "brick_width": 32.0,
        "brick_height": 12.0,
        "brick_rows": 6.0,
        "brick_cols": 18.0,
        "initial_ball_speed": 256.0,
        "max_ball_speed": 448.0,
        "paddle_speed": 620.0,
        "continuous": 0.0,
    },
    "g2048": {
        "scaffolding_ratio": 0.0,
    },
}


def parse_args():
    p = argparse.ArgumentParser(description="Metal training for simple envs")
    p.add_argument("--env", type=str, required=True, choices=list(ENV_DEFAULTS.keys()))
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
    p.add_argument("--replay-ratio", type=float, default=0.25)
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
    p.add_argument(
        "--trace-path",
        type=str,
        default="",
        help="optional jsonl trace output path for parity debugging",
    )
    p.add_argument(
        "--trace-every",
        type=int,
        default=1,
        help="write one trace row every N training iterations",
    )
    p.add_argument("--num-buffers", type=int, default=1)
    p.add_argument("--num-threads", type=int, default=1)
    p.add_argument("--arch", choices=["simple", "rich"], default="simple",
                   help="model arch: simple (upstream single-linear) or rich (3-layer MLP + LN decoder)")
    p.add_argument("--min-lr-ratio", type=float, default=0.0,
                   help="minimum LR as ratio of initial (upstream default: 0.0)")
    p.add_argument("--tf32-sim", action="store_true",
                   help="simulate CUDA TF32 precision by rounding GEMM inputs to 10-bit mantissa")
    p.add_argument("--profile", action="store_true",
                   help="GPU sync after each training phase for accurate per-kernel profiling")
    p.add_argument("--cpu-inference", action="store_true",
                   help="CPU forward pass during rollout (no GPU sync, uses Accelerate cblas)")
    return p.parse_args()


def main():
    args = parse_args()

    if args.tf32_sim:
        import os
        os.environ["PUFFERLIB_TF32_SIM"] = "1"

    config = {
        "horizon": args.horizon,
        "learning_rate": args.learning_rate,
        "min_lr_ratio": args.min_lr_ratio,
        "anneal_lr": 1.0,
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
        "use_rnn": 1.0,
        "cudagraphs": -1.0,
        "kernels": 1.0,
        "profile": 1.0 if args.profile else 0.0,
        "overlap": 0.0 if args.no_overlap else 1.0,
        "cpu_inference": 1.0 if args.cpu_inference else 0.0,
        "seed": float(args.seed),
        "env_name": args.env,
    }
    vec_config = {
        "total_agents": float(args.total_agents),
        "num_buffers": float(args.num_buffers),
        "num_threads": float(args.num_threads),
    }
    policy_config = {
        "hidden_size": float(args.hidden_size),
        "num_layers": float(args.num_layers),
        "arch": 1.0 if args.arch == "simple" else 0.0,
    }
    env_config = ENV_DEFAULTS[args.env]

    print(f"env={args.env}, agents={args.total_agents}, hidden={args.hidden_size}, "
          f"layers={args.num_layers}, horizon={args.horizon}, overlap={not args.no_overlap}, "
          f"arch={args.arch}, cpu_infer={args.cpu_inference}, seed={args.seed}")

    trace_file = None
    trace_every = max(int(args.trace_every), 1)
    if args.trace_path:
        trace_path = Path(args.trace_path).expanduser().resolve()
        trace_path.parent.mkdir(parents=True, exist_ok=True)
        trace_file = trace_path.open("w", encoding="utf-8")
        trace_file.write(json.dumps({
            "event": "meta",
            "env": args.env,
            "seed": args.seed,
            "total_agents": args.total_agents,
            "hidden_size": args.hidden_size,
            "num_layers": args.num_layers,
            "horizon": args.horizon,
            "total_timesteps": args.total_timesteps,
            "learning_rate": args.learning_rate,
            "optimizer": "muon",
            "minibatch_size": args.minibatch_size,
            "replay_ratio": args.replay_ratio,
            "ent_coef": args.ent_coef,
            "gamma": args.gamma,
            "gae_lambda": args.gae_lambda,
            "clip_coef": args.clip_coef,
            "vf_coef": args.vf_coef,
            "vf_clip_coef": args.vf_clip_coef,
            "max_grad_norm": args.max_grad_norm,
            "num_buffers": args.num_buffers,
            "num_threads": args.num_threads,
            "trace_every": trace_every,
        }) + "\n")
        trace_file.flush()

    pufferl = _C.create_pufferl(config, vec_config, env_config, policy_config)
    print(f"model params: {pufferl.num_params():,}")

    # warmup
    print("warming up...")
    _C.rollouts(pufferl)
    _C.train(pufferl)
    _C.log_losses(pufferl)

    steps_per_iter = args.total_agents * args.horizon
    total_iters = args.total_timesteps // steps_per_iter
    global_step = 0
    log_interval = args.log_interval
    t_start = time.time()
    t_last_log = t_start
    t_last_trace = t_start
    last_trace_iter = 0

    print(f"training: {total_iters:,} iters, {args.total_timesteps:,} steps")

    for iteration in range(1, total_iters + 1):
        _C.rollouts(pufferl)
        _C.train(pufferl)
        global_step += steps_per_iter

        should_log = iteration % log_interval == 0
        should_trace = trace_file is not None and iteration % trace_every == 0

        if should_log or should_trace:
            now = time.time()
            losses = _C.log_losses(pufferl)
            env_stats = _C.log_environments(pufferl)
            train_debug = _C.log_train_debug(pufferl) if should_trace else {}

        if should_log:
            elapsed = now - t_last_log
            sps = (log_interval * steps_per_iter) / elapsed
            t_last_log = now

            ent = losses.get("entropy", 0)
            pg = losses.get("pg_loss", 0)
            vf = losses.get("vf_loss", 0)
            ep_ret = env_stats.get("episode_return", 0)
            ep_len = env_stats.get("episode_length", 0)
            score = env_stats.get("score", ep_ret)
            # parseable format matching sweep_bench.py METRIC_PATTERN
            print(f"[step={global_step:>10,} | SPS={sps:>10,.0f} | "
                  f"ret={ep_ret:>8.2f} score={score:>8.2f} len={ep_len:>6.0f} | "
                  f"ent={ent:.3f} pg={pg:.4f} vf={vf:.4f}]")

        if should_trace and trace_file is not None:
            trace_elapsed = max(now - t_last_trace, 1e-9)
            trace_iters = max(iteration - last_trace_iter, 1)
            trace_sps = (trace_iters * steps_per_iter) / trace_elapsed
            t_last_trace = now
            last_trace_iter = iteration
            trace_row = {
                "event": "tick",
                "iteration": iteration,
                "step": global_step,
                "sps": trace_sps,
                "score": env_stats.get("score", env_stats.get("episode_return", 0)),
                "episode_return": env_stats.get("episode_return", 0),
                "episode_length": env_stats.get("episode_length", 0),
                "entropy": losses.get("entropy", 0),
                "pg_loss": losses.get("pg_loss", 0),
                "vf_loss": losses.get("vf_loss", 0),
                "total_loss": losses.get("total_loss", 0),
                "old_approx_kl": losses.get("old_approx_kl", 0),
                "approx_kl": losses.get("approx_kl", 0),
                "clipfrac": losses.get("clipfrac", 0),
            }
            trace_row.update(train_debug)
            trace_file.write(json.dumps(trace_row) + "\n")

    total_time = time.time() - t_start
    avg_sps = global_step / total_time
    print(f"\ndone. {global_step:,} steps in {total_time:.1f}s")
    print(f"avg SPS: {avg_sps:,.0f}")

    profile = _C.log_profile(pufferl)
    for k, v in sorted(profile.items()):
        print(f"  {k}: {v:.3f}")

    if trace_file is not None:
        trace_file.write(json.dumps({
            "event": "final",
            "step": global_step,
            "total_time_seconds": total_time,
            "avg_sps": avg_sps,
            "profile": profile,
        }) + "\n")
        trace_file.flush()
        trace_file.close()


if __name__ == "__main__":
    main()
