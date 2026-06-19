"""Colosseum sweep config invariants."""

import sys
from unittest.mock import patch

import pufferlib.pufferl as pufferl
import pufferlib.sweep as sweep


SWEEP_ONLY = (
    "total_timesteps",
    "learning_rate",
    "ent_coef",
    "gamma",
    "gae_lambda",
    "min_lr_ratio",
    "clip_coef",
    "vf_coef",
    "vf_clip_coef",
    "max_grad_norm",
    "replay_ratio",
    "prio_alpha",
    "prio_beta0",
    "vtrace_rho_clip",
    "vtrace_c_clip",
    "hidden_size",
    "num_layers",
    "total_agents",
    "num_buffers",
    "horizon",
    "state_buffer_size",
    "cl_frac",
    "state_checkpoint_interval",
    "explore_alpha",
    "explore_beta",
    "explore_decay",
    "damage_reward_coeff",
    "wave_clear_bonus",
    "avoided_damage_coeff",
    "death_penalty_coeff",
    "timeout_penalty",
    "curriculum_num_tiers",
    "curriculum_wave_1",
    "curriculum_frac_1",
    "curriculum_wave_2",
    "curriculum_frac_2",
    "curriculum_wave_3",
    "curriculum_frac_3",
)

SWEEP_SPACES = {
    "train/total_timesteps",
    "train/learning_rate",
    "train/ent_coef",
    "train/gamma",
    "train/gae_lambda",
    "train/min_lr_ratio",
    "train/clip_coef",
    "train/vf_coef",
    "train/vf_clip_coef",
    "train/max_grad_norm",
    "train/replay_ratio",
    "train/prio_alpha",
    "train/prio_beta0",
    "train/vtrace_rho_clip",
    "train/vtrace_c_clip",
    "policy/hidden_size",
    "policy/num_layers",
    "vec/total_agents",
    "vec/num_buffers",
    "train/horizon",
    "train/state_buffer_size",
    "train/cl_frac",
    "train/state_checkpoint_interval",
    "train/explore_alpha",
    "train/explore_beta",
    "train/explore_decay",
    "env/damage_reward_coeff",
    "env/wave_clear_bonus",
    "env/avoided_damage_coeff",
    "env/death_penalty_coeff",
    "env/timeout_penalty",
    "env/curriculum_num_tiers",
    "env/curriculum_wave_1",
    "env/curriculum_frac_1",
    "env/curriculum_wave_2",
    "env/curriculum_frac_2",
    "env/curriculum_wave_3",
    "env/curriculum_frac_3",
}


def load_colosseum_config():
    """Load Colosseum config without leaking test runner argv into CLI parsing."""
    with patch.object(sys, "argv", ["pytest"]):
        return pufferl.load_config("osrs_colosseum")


def test_colosseum_sweep_only_flat_leaf_keys():
    """Confirm sweep_only exposes the intended flat leaf keys once."""
    args = load_colosseum_config()
    keys = tuple(key.strip() for key in args["sweep"]["sweep_only"].split(","))

    assert keys == SWEEP_ONLY
    assert len(keys) == len(set(keys))


def test_colosseum_sweep_leaves_resolve_to_sectioned_spaces():
    """Confirm Colosseum sweep leaves map to valid sectioned spaces."""
    args = load_colosseum_config()
    hyperparameters = sweep.Hyperparameters(args["sweep"], verbose=False)

    assert set(hyperparameters.flat_spaces) == SWEEP_SPACES
    assert (
        hyperparameters.flat_spaces["train/state_buffer_size"].min
        >= args["train"]["warmup_states"]
    )
    assert hyperparameters.flat_spaces["train/cl_frac"].max <= 0.9


def test_colosseum_searchable_spaces_fill_create_pufferl_sections():
    """Confirm sampled sweep values land in sections consumed by create_pufferl."""
    args = load_colosseum_config()
    hyperparameters = sweep.Hyperparameters(args["sweep"], verbose=False)
    sampled = hyperparameters.to_dict(hyperparameters.search_centers, args)

    assert "hidden_size" in sampled["policy"]
    assert "num_layers" in sampled["policy"]
    assert "total_agents" in sampled["vec"]
    assert "horizon" in sampled["train"]
    assert "state_buffer_size" in sampled["train"]
    assert "cl_frac" in sampled["train"]
    assert "curriculum_num_tiers" in sampled["env"]
    assert "curriculum_wave_1" in sampled["env"]
