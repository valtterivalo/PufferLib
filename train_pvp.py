"""Train OSRS PVP on Metal backend with wandb logging.

Designed for fair comparison against PufferLib 4.0 C++ training.
Both backends use the same C env, same Muon optimizer, same model arch.
Supports PFSP (prioritized fictitious self-play) via --pfsp flag.
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

# opponent name -> enum value (from osrs_pvp_types.h OpponentType)
OPPONENT_TYPES = {
    "true_random": 1, "panicking": 2, "weak_random": 3, "semi_random": 4,
    "sticky_prayer": 5, "random_eater": 6, "prayer_rookie": 7, "improved": 8,
    "mixed_easy": 9, "mixed_medium": 10, "onetick": 11,
    "unpredictable_improved": 12, "unpredictable_onetick": 13,
    "mixed_hard": 14, "mixed_hard_balanced": 15,
    "novice_nh": 17, "apprentice_nh": 18, "competent_nh": 19,
    "intermediate_nh": 20, "advanced_nh": 21, "proficient_nh": 22,
    "expert_nh": 23, "master_nh": 24, "savant_nh": 25,
    "nightmare_nh": 26, "veng_fighter": 27, "blood_healer": 28,
    "gmaul_combo": 29,
}
OPP_PFSP = 16


def parse_args():
    p = argparse.ArgumentParser(description="Metal PVP training with wandb")
    p.add_argument("--total-agents", type=int, default=2048)
    p.add_argument("--hidden-size", type=int, default=512)
    p.add_argument("--num-layers", type=int, default=1)
    p.add_argument("--horizon", type=int, default=32)
    p.add_argument("--total-timesteps", type=int, default=50_000_000)
    p.add_argument("--learning-rate", type=float, default=0.00112)
    p.add_argument("--minibatch-size", type=int, default=4096)
    p.add_argument("--replay-ratio", type=float, default=0.25)
    p.add_argument("--ent-coef", type=float, default=0.0016)
    p.add_argument("--gamma", type=float, default=0.991)
    p.add_argument("--gae-lambda", type=float, default=0.845)
    p.add_argument("--prio-alpha", type=float, default=0.914)
    p.add_argument("--prio-beta0", type=float, default=0.218)
    p.add_argument("--clip-coef", type=float, default=0.32)
    p.add_argument("--vf-coef", type=float, default=2.5)
    p.add_argument("--vf-clip-coef", type=float, default=0.1)
    p.add_argument("--max-grad-norm", type=float, default=0.5)
    p.add_argument("--opponent-type", type=int, default=OPPONENT_TYPES["master_nh"])
    p.add_argument("--wandb-project", type=str, default="osrs-pvp-rl")
    p.add_argument("--experiment-name", type=str, default="metal-pvp-master-nh")
    p.add_argument("--log-interval", type=int, default=10, help="log every N iterations")
    p.add_argument("--save-interval", type=int, default=200, help="save weights every N iterations")
    p.add_argument("--save-dir", type=str, default="checkpoints")
    p.add_argument("--num-buffers", type=int, default=1)
    p.add_argument("--num-threads", type=int, default=1)
    p.add_argument("--no-overlap", action="store_true", help="disable async training overlap")
    p.add_argument("--optimizer", choices=["muon", "adam"], default="muon")
    p.add_argument("--no-wandb", action="store_true")
    p.add_argument("--shaping", action="store_true", help="enable reward shaping")
    p.add_argument("--arch", choices=["simple", "rich"], default="rich",
                   help="model arch: simple (upstream single-linear) or rich (3-layer MLP + LN decoder)")
    # PFSP args
    p.add_argument("--pfsp", type=str, default=None,
                   help="comma-separated opponent pool names for PFSP (e.g. improved,onetick,master_nh)")
    p.add_argument("--pfsp-p", type=float, default=1.5,
                   help="PFSP weight exponent: (1-winrate)^p")
    p.add_argument("--pfsp-update-interval", type=int, default=2_000_000,
                   help="steps between PFSP weight recomputation")
    p.add_argument("--pfsp-weight-floor", type=float, default=0.02,
                   help="minimum sampling probability per opponent")
    p.add_argument("--pfsp-warmup-episodes", type=int, default=50,
                   help="min cumulative episodes per opponent before reweighting")
    return p.parse_args()


def main():
    args = parse_args()

    # PFSP: parse pool names and override opponent_type
    pfsp_enabled = args.pfsp is not None
    pfsp_pool_names: list[str] = []
    pfsp_pool_types: list[int] = []
    if pfsp_enabled:
        pfsp_pool_names = [n.strip() for n in args.pfsp.split(",")]
        for name in pfsp_pool_names:
            assert name in OPPONENT_TYPES, f"unknown opponent: {name}"
        pfsp_pool_types = [OPPONENT_TYPES[n] for n in pfsp_pool_names]
        args.opponent_type = OPP_PFSP

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
        "use_adam": 1.0 if args.optimizer == "adam" else 0.0,
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
        "arch": 1.0 if args.arch == "simple" else 0.0,
    }
    env_config = {
        "opponent_type": float(args.opponent_type),
        "shaping_scale": 1.0 if args.shaping else 0.0,
        "shaping_enabled": 1.0 if args.shaping else 0.0,
        "mask_in_obs": 1.0,  # PVP embeds action mask in last 39 obs columns
    }

    # wandb
    opponent_label = f"pfsp({args.pfsp})" if pfsp_enabled else f"opp_{args.opponent_type}"
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
                "opponent_name": opponent_label,
                "pfsp_pool": args.pfsp,
                "pfsp_p": args.pfsp_p,
                **{k: v for k, v in config.items() if k not in ("env_name",)},
            },
        )

    # Create pufferl
    print(f"creating pufferl: agents={args.total_agents}, hidden={args.hidden_size}, "
          f"layers={args.num_layers}, horizon={args.horizon}")
    pufferl = _C.create_pufferl(config, vec_config, env_config, policy_config)
    print(f"model params: {pufferl.num_params():,}")

    # PFSP init: push pool types + uniform cumulative weights
    pfsp_pool_size = len(pfsp_pool_types)
    pfsp_cumulative_episodes: list[float] = [0.0] * pfsp_pool_size
    pfsp_last_update_step = 0
    if pfsp_enabled:
        cum_weights = [int((i + 1) / pfsp_pool_size * 1000) for i in range(pfsp_pool_size)]
        cum_weights[-1] = 1000  # clamp last to exactly 1000
        pufferl.set_pfsp_weights(pfsp_pool_types, cum_weights)
        print(f"PFSP enabled: pool={pfsp_pool_names}, initial weights=uniform")

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
            print(f"[step={global_step:>10,} | SPS={sps:>8,.0f} | "
                  f"ret={ep_ret:>6.2f} wins={wins:.2f} len={ep_len:.0f} | "
                  f"ent={ent:.3f} pg={pg:.4f} vf={vf:.4f}]")

        # PFSP weight recomputation
        if pfsp_enabled and (global_step - pfsp_last_update_step) >= args.pfsp_update_interval:
            wins_delta, episodes_delta = pufferl.get_pfsp_stats()
            pfsp_logs: dict[str, float] = {}
            total_eps = 0.0
            for i in range(pfsp_pool_size):
                pfsp_cumulative_episodes[i] += episodes_delta[i]
                total_eps += episodes_delta[i]
                wr = wins_delta[i] / max(episodes_delta[i], 1)
                pfsp_logs[f"pfsp/{pfsp_pool_names[i]}_win_rate"] = wr
                pfsp_logs[f"pfsp/{pfsp_pool_names[i]}_episodes"] = episodes_delta[i]

            min_cumulative = min(pfsp_cumulative_episodes)
            if min_cumulative < args.pfsp_warmup_episodes:
                wr_strs = []
                for i in range(pfsp_pool_size):
                    wr = wins_delta[i] / max(episodes_delta[i], 1)
                    ep = int(pfsp_cumulative_episodes[i])
                    wr_strs.append(f"{pfsp_pool_names[i]}:{wr:.0%}({ep}ep)")
                print(f"[pfsp step={global_step}] warmup (min_ep={int(min_cumulative)}/{args.pfsp_warmup_episodes}) {' | '.join(wr_strs)}")
            else:
                raw_weights = []
                for i in range(pfsp_pool_size):
                    wr = wins_delta[i] / max(episodes_delta[i], 1)
                    raw_weights.append(max((1.0 - wr) ** args.pfsp_p, args.pfsp_weight_floor))
                total_w = sum(raw_weights)
                cum_weights = []
                for i in range(pfsp_pool_size):
                    cum_weights.append(int(sum(raw_weights[:i + 1]) / total_w * 1000))
                cum_weights[-1] = 1000
                pufferl.set_pfsp_weights(pfsp_pool_types, cum_weights)

                wr_strs = []
                for i in range(pfsp_pool_size):
                    wr = wins_delta[i] / max(episodes_delta[i], 1)
                    ep = int(pfsp_cumulative_episodes[i])
                    wr_strs.append(f"{pfsp_pool_names[i]}={wr:.2f}")
                print(f"[pfsp step={global_step}] {' '.join(wr_strs)} ({int(total_eps)} eps since last update)")

            if wandb_run:
                wandb_run.log(pfsp_logs, step=global_step)
            pfsp_last_update_step = global_step

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
