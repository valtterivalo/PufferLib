"""Train OSRS Zulrah encounter on Metal backend with wandb logging.

Requires building with: python setup.py build_osrs_zulrah --inplace --force
"""

import argparse
import os
import sys
import time
from pathlib import Path

# Load .env for WANDB_API_KEY
for env_path in [Path(__file__).parent / ".env", Path.home() / "Projects/storm/storm/osrs-pvp-rl/pvp-c/.env"]:
    if env_path.exists():
        for line in env_path.read_text().strip().splitlines():
            if "=" in line and not line.startswith("#"):
                key, val = line.split("=", 1)
                os.environ.setdefault(key.strip(), val.strip())
        break

sys.path.insert(0, str(Path(__file__).parent))
from pufferlib import _C


def parse_args():
    p = argparse.ArgumentParser(description="Metal Zulrah training with wandb")
    p.add_argument("--total-agents", type=int, default=2048)
    p.add_argument("--hidden-size", type=int, default=256)
    p.add_argument("--num-layers", type=int, default=1)
    p.add_argument("--horizon", type=int, default=128)
    p.add_argument("--total-timesteps", type=int, default=500_000_000)
    p.add_argument("--learning-rate", type=float, default=0.001)
    p.add_argument("--minibatch-size", type=int, default=4096)
    p.add_argument("--replay-ratio", type=float, default=0.25)
    p.add_argument("--ent-coef", type=float, default=0.01)
    p.add_argument("--gamma", type=float, default=0.99)
    p.add_argument("--gae-lambda", type=float, default=0.95)
    p.add_argument("--prio-alpha", type=float, default=0.0)
    p.add_argument("--prio-beta0", type=float, default=0.0)
    p.add_argument("--clip-coef", type=float, default=0.2)
    p.add_argument("--vf-coef", type=float, default=0.5)
    p.add_argument("--vf-clip-coef", type=float, default=0.1)
    p.add_argument("--max-grad-norm", type=float, default=0.5)
    p.add_argument("--gear-tier", type=int, default=0, help="gear tier (0=budget, 1=mid, 2=BIS)")
    p.add_argument("--wandb-project", type=str, default="osrs-pvp-rl")
    p.add_argument("--experiment-name", type=str, default="metal-zulrah")
    p.add_argument("--log-interval", type=int, default=10, help="log every N iterations")
    p.add_argument("--save-interval", type=int, default=200, help="save weights every N iterations")
    p.add_argument("--save-dir", type=str, default="checkpoints")
    p.add_argument("--num-buffers", type=int, default=1)
    p.add_argument("--num-threads", type=int, default=1)
    p.add_argument("--no-overlap", action="store_true", help="disable async training overlap")
    p.add_argument("--no-wandb", action="store_true")
    p.add_argument("--arch", choices=["simple", "rich"], default="rich",
                   help="model arch: simple (upstream single-linear) or rich (3-layer MLP + LN decoder)")
    return p.parse_args()


def main():
    args = parse_args()

    config = {
        "horizon": args.horizon,
        "learning_rate": args.learning_rate,
        "min_lr_ratio": 0.1,
        "anneal_lr": 1.0,
        "beta1": 0.95,
        "beta2": 0.999,
        "eps": 1e-12,
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
        "vtrace_rho_clip": 1.0,
        "vtrace_c_clip": 1.0,
        "prio_alpha": args.prio_alpha,
        "prio_beta0": args.prio_beta0,
        "use_rnn": 1.0,
        "cudagraphs": -1.0,
        "kernels": 1.0,
        "profile": 0.0,
        "overlap": 0.0 if args.no_overlap else 1.0,
        "env_name": "osrs_zulrah",
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
    env_config = {
        "gear_tier": float(args.gear_tier),
        "mask_in_obs": 1.0,
    }

    # wandb
    wandb_run = None
    if not args.no_wandb:
        import wandb
        run_name = f"{args.experiment_name}_{int(time.time())}"
        wandb_run = wandb.init(
            project=args.wandb_project,
            name=run_name,
            config={
                "backend": "metal",
                "encounter": "zulrah",
                "total_agents": args.total_agents,
                "hidden_size": args.hidden_size,
                "num_layers": args.num_layers,
                "horizon": args.horizon,
                "total_timesteps": args.total_timesteps,
                "learning_rate": args.learning_rate,
                "minibatch_size": args.minibatch_size,
                "gear_tier": args.gear_tier,
                **{k: v for k, v in config.items() if k not in ("env_name",)},
            },
        )

    # Create pufferl
    print(f"creating pufferl: agents={args.total_agents}, hidden={args.hidden_size}, "
          f"layers={args.num_layers}, horizon={args.horizon}")
    pufferl = _C.create_pufferl(config, vec_config, env_config, policy_config)
    print(f"model params: {pufferl.num_params():,}")

    # Save dir
    save_dir = Path(args.save_dir) / (wandb_run.name if wandb_run else args.experiment_name)
    save_dir.mkdir(parents=True, exist_ok=True)

    # Warmup
    print("warming up...")
    _C.rollouts(pufferl)
    _C.train(pufferl)
    _C.log_losses(pufferl)  # discard warmup losses

    steps_per_iter = args.total_agents * args.horizon
    total_iters = args.total_timesteps // steps_per_iter
    global_step = 0
    log_interval = args.log_interval
    t_start = time.time()
    t_last_log = t_start

    print(f"training: {total_iters:,} iterations, {args.total_timesteps:,} total steps")
    print(f"batch_size={steps_per_iter}, minibatch={args.minibatch_size}, "
          f"replay_ratio={args.replay_ratio} -> {int(args.replay_ratio * steps_per_iter / args.minibatch_size)} minibatches/iter")

    for iteration in range(1, total_iters + 1):
        _C.rollouts(pufferl)
        _C.train(pufferl)
        global_step += steps_per_iter

        if iteration % log_interval == 0:
            now = time.time()
            elapsed_since_log = now - t_last_log
            sps = (log_interval * steps_per_iter) / elapsed_since_log
            t_last_log = now

            losses = _C.log_losses(pufferl)
            env_stats = _C.log_environments(pufferl)

            logs = {
                "sps": sps,
                "global_step": global_step,
                "elapsed_s": now - t_start,
                **{f"losses/{k}": v for k, v in losses.items()},
                **{f"env/{k}": v for k, v in env_stats.items()},
            }

            if wandb_run:
                wandb_run.log(logs, step=global_step)

            # Console output
            ent = losses.get("entropy", 0)
            pg = losses.get("pg_loss", 0)
            vf = losses.get("vf_loss", 0)
            ep_ret = env_stats.get("episode_return", 0)
            ep_len = env_stats.get("episode_length", 0)
            wins = env_stats.get("wins", 0)
            dmg = env_stats.get("damage_dealt", 0)
            print(f"[step={global_step:>10,} | SPS={sps:>8,.0f} | "
                  f"ret={ep_ret:>6.2f} wins={wins:.2f} len={ep_len:.0f} dmg={dmg:.0f} | "
                  f"ent={ent:.3f} pg={pg:.4f} vf={vf:.4f}]")

        if args.save_interval > 0 and iteration % args.save_interval == 0:
            path = str(save_dir / f"weights_{global_step}.bin")
            _C.save_weights(pufferl, path)
            print(f"  saved: {path}")

    # Final save
    final_path = str(save_dir / f"weights_final_{global_step}.bin")
    _C.save_weights(pufferl, final_path)
    print(f"\ntraining complete. final weights: {final_path}")
    print(f"total time: {time.time() - t_start:.1f}s, avg SPS: {global_step / (time.time() - t_start):,.0f}")

    # Profile summary
    profile = _C.log_profile(pufferl)
    if wandb_run:
        wandb_run.log({f"profile/{k}": v for k, v in profile.items()}, step=global_step)

    if wandb_run:
        wandb_run.finish()


if __name__ == "__main__":
    main()
