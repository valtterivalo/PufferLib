"""PFSP (prioritized fictitious self-play) for osrs_pvp.

opponent pool definitions, weight initialization, and adaptive weight recomputation
based on per-opponent win rates. used by sweep trials to train against a diverse
pool of opponents with difficulty-proportional sampling.
"""

from __future__ import annotations

# opponent name -> enum value (from osrs_pvp_types.h OpponentType)
OPP_PFSP = 16  # special opponent type that samples from the pool

POOL = {
    "true_random": 1, "panicking": 2, "weak_random": 3, "semi_random": 4,
    "sticky_prayer": 5, "random_eater": 6, "prayer_rookie": 7, "improved": 8,
    "onetick": 11, "unpredictable_improved": 12, "unpredictable_onetick": 13,
    "novice_nh": 17, "apprentice_nh": 18, "competent_nh": 19,
    "intermediate_nh": 20, "advanced_nh": 21, "proficient_nh": 22,
    "expert_nh": 23, "master_nh": 24, "savant_nh": 25,
    "nightmare_nh": 26, "veng_fighter": 27, "blood_healer": 28,
    "gmaul_combo": 29,
}
POOL_NAMES = list(POOL.keys())
POOL_TYPES = list(POOL.values())

# PFSP tuning constants
WEIGHT_EXPONENT = 1.5       # (1-winrate)^p
WEIGHT_FLOOR = 0.02
UPDATE_INTERVAL = 2_000_000  # steps between weight recomputation
WARMUP_EPISODES = 50         # min episodes per opponent before reweighting


def init_pfsp(pufferl_handle: object, total_agents: int) -> dict:
    """Initialize PFSP pool with uniform weights. Returns PFSP state dict."""
    pool_size = len(POOL_TYPES)
    cum_weights = [int((i + 1) / pool_size * 1000) for i in range(pool_size)]
    cum_weights[-1] = 1000
    pufferl_handle.set_pfsp_weights(POOL_TYPES, cum_weights)
    return {
        "cum_episodes": [0.0] * pool_size,
        "last_update_step": 0,
    }


def update_pfsp(pufferl_handle: object, pfsp_state: dict, global_step: int) -> None:
    """Recompute PFSP weights based on per-opponent win rates."""
    if (global_step - pfsp_state["last_update_step"]) < UPDATE_INTERVAL:
        return

    wins_delta, episodes_delta = pufferl_handle.get_pfsp_stats()
    pool_size = len(POOL_TYPES)

    for i in range(pool_size):
        pfsp_state["cum_episodes"][i] += episodes_delta[i]

    if min(pfsp_state["cum_episodes"]) < WARMUP_EPISODES:
        pfsp_state["last_update_step"] = global_step
        return

    raw_weights = []
    for i in range(pool_size):
        wr = wins_delta[i] / max(episodes_delta[i], 1)
        raw_weights.append(max((1.0 - wr) ** WEIGHT_EXPONENT, WEIGHT_FLOOR))
    total_w = sum(raw_weights)
    cum_weights = [int(sum(raw_weights[:i + 1]) / total_w * 1000) for i in range(pool_size)]
    cum_weights[-1] = 1000
    pufferl_handle.set_pfsp_weights(POOL_TYPES, cum_weights)
    pfsp_state["last_update_step"] = global_step
