"""Train OSRS PVP on Metal backend with wandb logging.

Designed for fair comparison against PufferLib 4.0 C++ training.
Both backends use the same C env, same Muon optimizer, same model arch.
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

# OPP_MASTER_NH = 24 (from osrs_pvp_types.h enum)
OPP_MASTER_NH = 24


def parse_args():
    p = argparse.ArgumentParser(description="Metal PVP training with wandb")
    p.add_argument("--total-agents", type=int, default=1024)
    p.add_argument("--hidden-size", type=int, default=512)
    p.add_argument("--num-layers", type=int, default=3)
    p.add_argument("--horizon", type=int, default=32)
    p.add_argument("--total-timesteps", type=int, default=50_000_000)
    p.add_argument("--learning-rate", type=float, default=0.00112)
    p.add_argument("--minibatch-size", type=int, default=4096)
    p.add_argument("--opponent-type", type=int, default=OPP_MASTER_NH)
    p.add_argument("--wandb-project", type=str, default="osrs-pvp-rl")
    p.add_argument("--experiment-name", type=str, default="metal-pvp-master-nh")
    p.add_argument("--log-interval", type=int, default=10, help="log every N iterations")
    p.add_argument("--save-interval", type=int, default=200, help="save weights every N iterations")
    p.add_argument("--save-dir", type=str, default="checkpoints")
    p.add_argument("--num-buffers", type=int, default=1)
    p.add_argument("--num-threads", type=int, default=1)
    p.add_argument("--no-wandb", action="store_true")
    return p.parse_args()


def main():
    args = parse_args()

    # Hyperparams matching osrs_pvp.ini production config
    config = {
        "horizon": args.horizon,
        "learning_rate": args.learning_rate,
        "min_lr_ratio": 0.1,
        "anneal_lr": 1.0,
        "beta1": 0.95,
        "beta2": 0.999,
        "eps": 1e-12,
        "minibatch_size": args.minibatch_size,
        "replay_ratio": 0.25,
        "total_timesteps": args.total_timesteps,
        "max_grad_norm": 0.5,
        "clip_coef": 0.32,
        "vf_clip_coef": 0.1,
        "vf_coef": 2.5,
        "ent_coef": 0.0016,
        "gamma": 0.991,
        "gae_lambda": 0.845,
        "vtrace_rho_clip": 1.0,
        "vtrace_c_clip": 1.0,
        "prio_alpha": 0.914,
        "prio_beta0": 0.218,
        "use_rnn": 1.0,
        "cudagraphs": -1.0,
        "kernels": 1.0,
        "profile": 0.0,
        "env_name": "osrs_pvp",
    }
    vec_config = {
        "total_agents": float(args.total_agents),
        "num_buffers": float(args.num_buffers),
        "num_threads": float(args.num_threads),
    }
    policy_config = {
        "hidden_size": float(args.hidden_size),
        "num_layers": float(args.num_layers),
    }
    env_config = {
        "opponent_type": float(args.opponent_type),
        "shaping_scale": 0.0,
        "shaping_enabled": 0.0,
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
                "total_agents": args.total_agents,
                "hidden_size": args.hidden_size,
                "num_layers": args.num_layers,
                "horizon": args.horizon,
                "total_timesteps": args.total_timesteps,
                "learning_rate": args.learning_rate,
                "minibatch_size": args.minibatch_size,
                "opponent_type": args.opponent_type,
                "opponent_name": "master_nh",
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
          f"replay_ratio=0.25 -> {int(0.25 * steps_per_iter / args.minibatch_size)} minibatches/iter")

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
            print(f"[step={global_step:>10,} | SPS={sps:>8,.0f} | "
                  f"ret={ep_ret:>6.2f} wins={wins:.2f} len={ep_len:.0f} | "
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

    # Profile summary (prints detailed breakdown to stderr, returns dict)
    profile = _C.log_profile(pufferl)
    if wandb_run:
        wandb_run.log({f"profile/{k}": v for k, v in profile.items()}, step=global_step)

    if wandb_run:
        wandb_run.finish()


if __name__ == "__main__":
    main()
