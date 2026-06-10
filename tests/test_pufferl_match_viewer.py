from __future__ import annotations

import importlib
import sys
import argparse
import types
from copy import deepcopy
from types import SimpleNamespace

import numpy as np
import pytest


try:
    import pufferlib as pufferlib_pkg

    rich_argparse = types.ModuleType("rich_argparse")
    rich_argparse.RichHelpFormatter = argparse.ArgumentDefaultsHelpFormatter
    sys.modules.setdefault("rich_argparse", rich_argparse)
    setattr(pufferlib_pkg, "_C", SimpleNamespace(env_name="osrs_pvp"))
    sys.modules.pop("pufferlib.pufferl", None)
    pufferl = importlib.import_module("pufferlib.pufferl")
except ImportError as exc:
    pytest.skip(f"pufferlib.pufferl unavailable: {exc}", allow_module_level=True)


class FakeMatchBackend:
    env_name = "osrs_pvp"

    def __init__(self, stop_after: int | None = None) -> None:
        self.stop_after = stop_after
        self.created_args = None
        self.loaded_weights = []
        self.loaded_banks = []
        self.agent_perm = None
        self.render_calls = 0
        self.rollout_calls = 0
        self.closed = False

    def create_pufferl(self, args):
        self.created_args = deepcopy(args)
        return SimpleNamespace()

    def set_agent_perm(self, pufferl_obj, perm):
        self.agent_perm = np.asarray(perm, dtype=np.int32)

    def load_weights(self, pufferl_obj, path):
        self.loaded_weights.append(path)

    def load_frozen_bank(self, pufferl_obj, bank_idx, path):
        self.loaded_banks.append((bank_idx, path))

    def render(self, pufferl_obj, env_id):
        self.render_calls += 1

    def rollouts(self, pufferl_obj):
        self.rollout_calls += 1
        if self.stop_after is not None and self.rollout_calls > self.stop_after:
            raise StopIteration

    def eval_log(self, pufferl_obj):
        return {
            "env": {
                "n": self.rollout_calls,
                "slot_0_score": 0.75,
                "slot_1_score": 0.25,
                "draw_rate": 0.0,
            }
        }

    def close(self, pufferl_obj):
        self.closed = True


class FakeFixedEvalBackend:
    env_name = "osrs_pvp"

    def __init__(self) -> None:
        self.created_args = []
        self.loaded_weights = []
        self.loaded_banks = []
        self.agent_perms = []
        self.closed = 0

    def create_pufferl(self, args):
        copied = deepcopy(args)
        self.created_args.append(copied)
        return SimpleNamespace(args=copied, rollouts=0)

    def set_agent_perm(self, pufferl_obj, perm):
        self.agent_perms.append(np.asarray(perm, dtype=np.int32))

    def load_weights(self, pufferl_obj, path):
        self.loaded_weights.append(path)

    def load_frozen_bank(self, pufferl_obj, bank_idx, path):
        self.loaded_banks.append((bank_idx, path))

    def rollouts(self, pufferl_obj):
        pufferl_obj.rollouts += 1

    def log(self, pufferl_obj):
        env_args = pufferl_obj.args["env"]
        learned_policy_eval = bool(env_args["use_rollout_opponent"])
        performance_score = 1.0 if learned_policy_eval else 0.5
        wins = 1.0 if learned_policy_eval else 0.5
        return {
            "env": {
                "n": 1.0,
                "wins": wins,
                "expected_damage_score": performance_score,
                "ko_supply_score": 0.0,
                "performance_score": performance_score,
            }
        }

    def close(self, pufferl_obj):
        self.closed += 1


def match_args(render_mode: str = "auto") -> dict:
    return {
        "env_name": "osrs_pvp",
        "render_mode": render_mode,
        "checkpoint_dir": "checkpoints",
        "env": {
            "use_rollout_opponent": 0,
        },
        "vec": {
            "total_agents": 4096,
            "num_buffers": 5,
            "num_frozen_banks": 2,
            "frozen_bank_pct": 0.005,
        },
        "train": {
            "horizon": 8,
            "minibatch_size": 16384,
            "state_curriculum_mode": 1,
            "state_buffer_size": 16384,
            "cl_frac": 0.05,
            "warmup_states": 4096,
        },
        "sweep": {},
    }


def run_fake_match(monkeypatch, fake: FakeMatchBackend, args: dict, num_games: int):
    monkeypatch.setattr(pufferl, "_C", fake)
    return pufferl.match(
        env_name="osrs_pvp",
        policy_a_path="player0.bin",
        policy_b_path="past.bin",
        num_games=num_games,
        args=args,
        verbose=False,
    )


def test_match_pins_pvp_checkpoint_selfplay_routing(monkeypatch):
    fake = FakeMatchBackend()

    run_fake_match(monkeypatch, fake, match_args(), 1)

    args = fake.created_args
    assert args["reset_state"] is False
    assert args["env"]["use_rollout_opponent"] == 1
    assert args["vec"]["num_buffers"] == 2
    assert args["vec"]["total_agents"] == 8192
    assert args["vec"]["num_frozen_banks"] == 1
    assert args["vec"]["frozen_bank_pct"] == 0.5
    assert args["train"]["horizon"] == 1
    assert args["train"]["minibatch_size"] == 8192
    assert args["train"]["state_curriculum_mode"] == 0
    assert args["train"]["state_buffer_size"] == 0
    assert args["train"]["cl_frac"] == 0
    assert args["train"]["warmup_states"] == 0
    assert fake.loaded_weights == ["player0.bin"]
    assert fake.loaded_banks == [(0, "past.bin")]
    assert fake.agent_perm[0] == 0
    assert fake.agent_perm[1] == 2048
    assert fake.agent_perm[4096] == 4096
    assert fake.agent_perm[4097] == 6144
    assert fake.closed is True


def test_match_render_mode_human_calls_backend_render(monkeypatch):
    fake = FakeMatchBackend()

    run_fake_match(monkeypatch, fake, match_args("human"), 2)

    args = fake.created_args
    assert args["vec"]["num_buffers"] == 2
    assert args["vec"]["total_agents"] == 4
    assert args["train"]["minibatch_size"] == 4
    assert fake.agent_perm.tolist() == [0, 1, 2, 3]
    assert fake.render_calls == 2
    assert fake.rollout_calls == 2


def test_match_render_mode_auto_stays_stats_only(monkeypatch):
    fake = FakeMatchBackend()

    run_fake_match(monkeypatch, fake, match_args("auto"), 2)

    assert fake.render_calls == 0
    assert fake.rollout_calls == 2


def test_match_zero_games_runs_until_external_stop(monkeypatch):
    fake = FakeMatchBackend(stop_after=2)

    with pytest.raises(StopIteration):
        run_fake_match(monkeypatch, fake, match_args("human"), 0)

    assert fake.render_calls == 3
    assert fake.rollout_calls == 3


def fixed_eval_args(policy_path: str) -> dict:
    return {
        "env_name": "osrs_pvp",
        "wandb": False,
        "checkpoint_interval": 0,
        "reset_state": True,
        "fixed_eval": {
            "enabled": 1,
            "episodes_per_opponent": 2,
            "total_agents": 8,
            "num_buffers": 2,
            "horizon": 4,
            "seed": 424242,
            "opponents": [1],
            "opponent_weights": [3],
            "policy_opponent_path": policy_path,
            "policy_opponent_name": "latest selfplay",
            "policy_opponent_weight": 1,
            "policy_opponent_hidden_size": 2048,
            "policy_opponent_num_layers": 2,
            "holdout_opponents": [],
        },
        "selfplay": {
            "enabled": 1,
        },
        "env": {
            "seed": 73,
            "opponent_type": 0,
            "use_rollout_opponent": 1,
        },
        "vec": {
            "total_agents": 4096,
            "num_buffers": 2,
            "num_frozen_banks": 2,
            "frozen_bank_pct": 0.005,
        },
        "train": {
            "horizon": 8,
            "minibatch_size": 16384,
            "cpu_inference": 0,
        },
    }


def test_fixed_eval_can_include_one_learned_policy_opponent(tmp_path):
    policy_path = tmp_path / "policy.bin"
    policy_path.write_bytes(b"policy")
    fake = FakeFixedEvalBackend()

    logs = pufferl._run_pvp_fixed_eval_suite(
        fake,
        fixed_eval_args(str(policy_path)),
        "candidate.bin",
    )

    assert logs["env/fixed_eval_scripted_performance_score"] == pytest.approx(0.5)
    assert logs["env/fixed_eval_policy_latest_selfplay_performance_score"] == pytest.approx(1.0)
    assert logs["env/fixed_eval_policy_performance_score"] == pytest.approx(1.0)
    assert logs["env/fixed_eval_performance_score"] == pytest.approx(0.625)
    assert fake.loaded_weights == ["candidate.bin", "candidate.bin"]
    assert fake.loaded_banks == [(0, str(policy_path))]
    assert fake.created_args[0]["vec"]["num_frozen_banks"] == 0
    assert fake.created_args[0]["vec"]["frozen_bank_pct"] == 0.0
    assert fake.created_args[1]["vec"]["num_frozen_banks"] == 1
    assert fake.created_args[1]["vec"]["frozen_bank_pct"] == 0.5
    assert fake.closed == 2

    policy_args = fake.created_args[1]
    assert policy_args["selfplay"]["enabled"] == 0
    assert policy_args["env"]["use_rollout_opponent"] == 1
    assert policy_args["vec"]["num_frozen_banks"] == 1
    assert policy_args["vec"]["frozen_bank_pct"] == 0.5
    assert policy_args["vec"]["frozen_bank_hidden_size"] == 2048
    assert policy_args["vec"]["frozen_bank_num_layers"] == 2
    assert policy_args["train"]["cpu_inference"] == 1
    assert fake.agent_perms[0].tolist() == [0, 2, 1, 3, 4, 6, 5, 7]
