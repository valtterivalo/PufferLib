"""
@fileoverview OSRS PvP training script with PFSP opponent scheduling and selfplay.

Wraps PufferLib 4.0's PuffeRL with a PFSP (prioritized fictitious self-play)
loop that periodically recomputes opponent sampling weights from win rates.
The C env handles per-env pool sampling; this script handles weight updates.

Selfplay: when "selfplay" appears in the PFSP pool, a frozen copy of the current
policy acts as player 1 in those envs. The frozen policy runs inside the C++ rollout
loop (net_callback_wrapper). Weights are copied from the main policy periodically.
"""

import os
import sys

# add pvp-c to path for osrs_pvp_constants
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pvp-c"))

from pufferlib.pufferl import PuffeRL, Logger, load_config
from pufferlib import _C

from osrs_pvp_constants import OPPONENT_CONFIGS


def train_pfsp():
    """Main training loop with PFSP weight updates between epochs."""
    args = load_config("osrs_pvp")
    train_config = args["train"]
    env_config = args["env"]

    # PFSP config from [train] section
    pfsp_pool_str = str(train_config.get("pfsp_pool", "improved,onetick,unpredictable_improved,unpredictable_onetick,veng_fighter"))
    pfsp_pool_names = [n.strip() for n in pfsp_pool_str.split(",")]
    pfsp_pool_types = [OPPONENT_CONFIGS[n] for n in pfsp_pool_names]
    pfsp_pool_size = len(pfsp_pool_types)
    pfsp_p = float(train_config.get("pfsp_p", 2.0))
    pfsp_update_interval = int(train_config.get("pfsp_update_interval", 1_000_000))
    pfsp_weight_floor = float(train_config.get("pfsp_weight_floor", 0.05))
    pfsp_warmup_episodes = int(train_config.get("pfsp_warmup_episodes", 200))

    # Selfplay config
    has_selfplay = "selfplay" in pfsp_pool_names
    selfplay_update_interval = int(train_config.get("selfplay_update_interval", 5_000_000))

    # tracking state
    pfsp_last_update_step = 0
    pfsp_cumulative_episodes = [0.0] * pfsp_pool_size
    selfplay_last_update_step = 0

    # create PuffeRL (C++ backend handles env creation, policy, optimizer)
    logger = Logger(args) if args.get("wandb") else None

    config = dict(**train_config)
    config["env_name"] = args["env_name"]
    pufferl = PuffeRL(config, args["vec"], env_config, args["policy"], logger)

    # initialize PFSP pool with uniform weights
    cum_weights = [int((i + 1) / pfsp_pool_size * 1000) for i in range(pfsp_pool_size)]
    cum_weights[-1] = 1000
    pufferl.pufferl_cpp.set_pfsp_weights(pfsp_pool_types, cum_weights)
    print(f"PFSP enabled: pool={pfsp_pool_names}, initial weights=uniform")

    # Enable selfplay if any pool entry is "selfplay"
    if has_selfplay:
        pufferl.pufferl_cpp.enable_selfplay()

    total_timesteps = config["total_timesteps"]

    while pufferl.global_step < total_timesteps:
        pufferl.evaluate()
        logs = pufferl.train()

        # Selfplay frozen policy update
        if has_selfplay and (pufferl.global_step - selfplay_last_update_step) >= selfplay_update_interval:
            pufferl.pufferl_cpp.update_selfplay_policy()
            selfplay_last_update_step = pufferl.global_step
            print(f"[Selfplay step={pufferl.global_step}] frozen policy updated")

        # PFSP weight recomputation
        if (pufferl.global_step - pfsp_last_update_step) >= pfsp_update_interval:
            wins, episodes = pufferl.pufferl_cpp.get_pfsp_stats()

            # accumulate lifetime episode counts (wins/episodes are per-window)
            for i in range(pfsp_pool_size):
                pfsp_cumulative_episodes[i] += episodes[i]

            min_cumulative = min(pfsp_cumulative_episodes)

            if min_cumulative < pfsp_warmup_episodes:
                # still warming up — log but keep uniform weights
                wr_strs = []
                for i in range(pfsp_pool_size):
                    wr = wins[i] / max(episodes[i], 1.0)
                    ep = int(pfsp_cumulative_episodes[i])
                    wr_strs.append(f"{pfsp_pool_names[i]}:{wr:.0%}({ep}ep)")
                print(f"[PFSP step={pufferl.global_step}] warmup (min_ep={int(min_cumulative)}/{pfsp_warmup_episodes}) {' | '.join(wr_strs)}")
            else:
                # recompute weights: weight = max((1 - win_rate)^p, floor)
                raw_weights = []
                for i in range(pfsp_pool_size):
                    wr = wins[i] / max(episodes[i], 1.0)
                    raw_weights.append(max((1.0 - wr) ** pfsp_p, pfsp_weight_floor))

                total = sum(raw_weights)
                cum = 0
                new_cum_weights = []
                for i in range(pfsp_pool_size):
                    cum += int(raw_weights[i] / total * 1000)
                    new_cum_weights.append(cum)
                new_cum_weights[-1] = 1000  # no rounding gap

                pufferl.pufferl_cpp.set_pfsp_weights(pfsp_pool_types, new_cum_weights)

                # log
                wr_strs = []
                for i in range(pfsp_pool_size):
                    wr = wins[i] / max(episodes[i], 1.0)
                    ep = int(episodes[i])
                    w_pct = int(raw_weights[i] / total * 100)
                    wr_strs.append(f"{pfsp_pool_names[i]}:{wr:.0%}({ep}ep,{w_pct}%w)")
                print(f"[PFSP step={pufferl.global_step}] {' | '.join(wr_strs)}")

                # log per-opponent win rates to wandb
                if logger:
                    pfsp_logs = {}
                    for i in range(pfsp_pool_size):
                        wr = wins[i] / max(episodes[i], 1.0)
                        pfsp_logs[f"pfsp/{pfsp_pool_names[i]}_winrate"] = wr
                        pfsp_logs[f"pfsp/{pfsp_pool_names[i]}_weight"] = raw_weights[i] / total
                    logger.log(pfsp_logs, pufferl.global_step)

            pfsp_last_update_step = pufferl.global_step

    # cleanup
    pufferl.print_dashboard()
    model_path = pufferl.close()
    if logger:
        logger.log_cost(pufferl.uptime)
        logger.close(model_path, early_stop=False)


if __name__ == "__main__":
    train_pfsp()
