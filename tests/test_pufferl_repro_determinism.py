from __future__ import annotations

from copy import deepcopy
from types import SimpleNamespace
import json

import pytest


pufferl = pytest.importorskip("pufferlib.pufferl", exc_type=ImportError)


class FakeTrainBackend:
    env_name = "fake_env"

    def __init__(self):
        self.log_calls = 0
        self.eval_log_calls = 0
        self.rollout_calls = 0
        self.train_calls = 0
        self.saved_weights = []
        self.closed = False

    def create_pufferl(self, args):
        return SimpleNamespace(
            global_step=0,
            last_log_time=0.0,
            last_log_step=0,
            num_params=lambda: 1,
        )

    def rollouts(self, pufferl_obj):
        self.rollout_calls += 1
        pufferl_obj.global_step += 1

    def train(self, pufferl_obj):
        self.train_calls += 1
        return None

    def log(self, pufferl_obj):
        self.log_calls += 1
        return {
            "agent_steps": pufferl_obj.global_step,
            "uptime": float(self.log_calls),
            "epoch": self.log_calls,
            "SPS": 1.0,
            "perf": {
                "rollout": 0.0,
                "train": 0.0,
            },
            "loss": {
                "policy": 0.0,
            },
            "env": {
                "score": 0.5,
                "wins": 0.5,
                "performance_score": 0.25 + 0.01 * self.log_calls,
                "n": 1.0,
            },
        }

    def eval_log(self, pufferl_obj):
        self.eval_log_calls += 1
        return {
            "agent_steps": pufferl_obj.global_step,
            "uptime": float(self.log_calls + self.eval_log_calls),
            "epoch": self.log_calls + self.eval_log_calls,
            "SPS": 1.0,
            "perf": {
                "rollout": 0.0,
                "train": 0.0,
            },
            "loss": {
                "policy": 0.0,
            },
            "env": {
                "score": 0.5,
                "performance_score": 0.5,
                "n": 1.0,
            },
        }

    def save_weights(self, pufferl_obj, path):
        self.saved_weights.append(path)

    def close(self, pufferl_obj):
        self.closed = True


def train_args(tmp_path):
    return {
        "env_name": "fake_env",
        "rank": 0,
        "gpu_id": 0,
        "wandb": False,
        "wandb_project": "tests",
        "wandb_group": "tests",
        "tag": None,
        "checkpoint_interval": 0,
        "checkpoint_dir": str(tmp_path / "checkpoints"),
        "log_dir": str(tmp_path / "logs"),
        "eval_episodes": 0,
        "fixed_eval": {
            "enabled": 0,
        },
        "rollout_eval": {
            "mode": "off",
            "episodes": 4096,
            "keep_weights": 0,
        },
        "sweep": {
            "metric": "score",
            "downsample": 4,
        },
        "vec": {
            "total_agents": 1,
            "num_buffers": 1,
        },
        "train": {
            "total_timesteps": 4,
            "horizon": 1,
            "minibatch_size": 1,
        },
        "selfplay": {
            "enabled": 1,
        },
    }


def run_fake_train(monkeypatch, tmp_path, times):
    backend = FakeTrainBackend()
    step_calls = []
    time_values = iter(times)
    last_time = times[-1]

    def fake_time():
        nonlocal time_values
        try:
            return next(time_values)
        except StopIteration:
            return last_time

    def fake_selfplay_step(pufferl_obj, backend_obj, pool_state, flat_logs, epoch):
        step_calls.append((epoch, pufferl_obj.global_step, flat_logs["agent_steps"]))

    monkeypatch.setattr(pufferl, "_resolve_backend", lambda args: backend)
    monkeypatch.setattr(pufferl.selfplay, "setup", lambda *args: object())
    monkeypatch.setattr(pufferl.selfplay, "step", fake_selfplay_step)
    monkeypatch.setattr(pufferl, "print_dashboard", lambda *args, **kwargs: None)
    monkeypatch.setattr(pufferl.time, "time", fake_time)

    pufferl._train(
        "fake_env",
        deepcopy(train_args(tmp_path)),
        sweep_obj=pufferl._ReproSweep(),
    )
    return step_calls, backend


def test_wall_clock_does_not_change_selfplay_step_sequence(monkeypatch, tmp_path):
    constant_steps, constant_backend = run_fake_train(
        monkeypatch,
        tmp_path / "constant",
        [0.0] * 32,
    )
    fast_steps, fast_backend = run_fake_train(
        monkeypatch,
        tmp_path / "fast",
        [float(i) for i in range(32)],
    )

    assert constant_steps == fast_steps
    assert constant_steps == [(0, 1, 1), (1, 2, 2), (2, 3, 3), (3, 4, 4)]
    assert constant_backend.log_calls == 4
    assert fast_backend.log_calls == 4


def test_sweep_early_stop_uses_configured_training_metric(monkeypatch, tmp_path):
    backend = FakeTrainBackend()
    args = train_args(tmp_path)
    args["train"]["total_timesteps"] = 10
    args["sweep"]["metric"] = "fixed_eval_performance_score"
    args["sweep"]["early_stop_metric"] = "performance_score"
    calls = []

    class FakeSweep:
        def early_stop(self, logs, target_key):
            calls.append((target_key, logs["env"]["performance_score"]))
            return True

    monkeypatch.setattr(pufferl, "_resolve_backend", lambda args: backend)
    monkeypatch.setattr(pufferl.selfplay, "setup", lambda *args: object())
    monkeypatch.setattr(pufferl.selfplay, "step", lambda *args: None)
    monkeypatch.setattr(pufferl, "print_dashboard", lambda *args, **kwargs: None)

    pufferl._train(
        "fake_env",
        args,
        sweep_obj=FakeSweep(),
    )

    assert calls
    assert calls[0][0] == "env/performance_score"
    assert backend.closed is True


def test_rollout_eval_runs_without_extra_train_or_selfplay(monkeypatch, tmp_path):
    backend = FakeTrainBackend()
    args = train_args(tmp_path)
    args["sweep"]["metric"] = "rollout_eval_performance_score"
    args["rollout_eval"] = {
        "mode": "pvp_training_distribution",
        "episodes": 2,
        "keep_weights": 1,
    }
    step_calls = []
    result_items = []

    monkeypatch.setattr(pufferl, "_resolve_backend", lambda args: backend)
    monkeypatch.setattr(pufferl.selfplay, "setup", lambda *args: object())
    monkeypatch.setattr(
        pufferl.selfplay,
        "step",
        lambda pufferl_obj, backend_obj, pool_state, flat_logs, epoch:
            step_calls.append(epoch),
    )
    monkeypatch.setattr(pufferl, "print_dashboard", lambda *args, **kwargs: None)

    pufferl._train(
        "fake_env",
        args,
        sweep_obj=pufferl._ReproSweep(),
        result_queue=SimpleNamespace(put=result_items.append),
    )

    assert backend.train_calls == 4
    assert backend.rollout_calls == 6
    assert step_calls == [0, 1, 2, 3]
    assert backend.saved_weights
    assert backend.saved_weights[-1].endswith("rollout_eval_weights.bin")
    assert backend.closed is True
    assert result_items
    assert result_items[-1][1] == [0.305, 0.305]


def test_rollout_eval_aggregates_metrics_by_episode_count():
    backend = FakeTrainBackend()
    pufferl_obj = backend.create_pufferl({})

    logs = pufferl._run_pvp_rollout_eval(
        backend,
        pufferl_obj,
        {
            "rollout_eval": {
                "mode": "pvp_training_distribution",
                "episodes": 2,
            },
        },
    )

    assert backend.rollout_calls == 2
    assert backend.train_calls == 0
    assert logs["env/rollout_eval_n"] == pytest.approx(2.0)
    assert logs["env/rollout_eval_wins"] == pytest.approx(0.5)
    assert logs["env/rollout_eval_performance_score"] == pytest.approx(0.265)


def test_rollout_eval_caps_final_batch_weight():
    backend = FakeTrainBackend()
    pufferl_obj = backend.create_pufferl({})

    def log(_pufferl_obj):
        value = 0.2 if backend.rollout_calls == 1 else 0.8
        return {
            "env": {
                "n": 5.0,
                "score": value,
                "wins": value,
                "performance_score": value,
                "expected_damage_score": value,
                "ko_supply_score": value,
                "damage_dealt": 0.0,
                "damage_received": 0.0,
            },
        }

    backend.log = log
    logs = pufferl._run_pvp_rollout_eval(
        backend,
        pufferl_obj,
        {
            "rollout_eval": {
                "mode": "pvp_training_distribution",
                "episodes": 7,
            },
        },
    )

    assert backend.rollout_calls == 2
    assert logs["env/rollout_eval_n"] == pytest.approx(7.0)
    assert logs["env/rollout_eval_performance_score"] == pytest.approx(
        (0.2 * 5.0 + 0.8 * 2.0) / 7.0,
    )


def test_rollout_eval_invalid_mode_fails_loud():
    with pytest.raises(ValueError, match="unknown rollout_eval.mode"):
        pufferl._rollout_eval_mode({"rollout_eval": {"mode": "mystery"}})


def test_checkpoint_interval_zero_disables_periodic_saves():
    assert not pufferl._should_save_checkpoint(None, 0, 0, False, False)
    assert pufferl._should_save_checkpoint(None, 0, 10, True, False)
    assert not pufferl._should_save_checkpoint(object(), 0, 10, True, False)
    assert pufferl._should_save_checkpoint(object(), 0, 10, True, True)


def test_deterministic_step_bins_ignore_display_timing():
    logs = [
        {"agent_steps": 1, "env/score": 0.0, "label": "a"},
        {"agent_steps": 2, "env/score": 1.0, "label": "b"},
        {"agent_steps": 3, "env/score": 2.0, "label": "c"},
        {"agent_steps": 4, "env/score": 3.0, "label": "d"},
    ]

    first = pufferl._deterministic_step_bin_metrics(logs, 4)
    second = pufferl._deterministic_step_bin_metrics(deepcopy(logs), 4)

    assert first == second
    assert len(first["agent_steps"]) == 4
    assert first["env/score"][-1] == 3.0
    assert first["label"][-1] == "d"


def test_prepare_repro_args_preserves_trial_config_and_strips_metrics(tmp_path):
    trial_path = tmp_path / "trial.json"
    output_dir = tmp_path / "out"
    trial = train_args(tmp_path)
    trial["checkpoint_interval"] = 0
    trial["checkpoint_dir"] = "/source/checkpoints"
    trial["log_dir"] = "/source/logs"
    trial["metrics"] = {"env/score": [0.5]}
    trial_path.write_text(json.dumps(trial))

    prepared = pufferl._prepare_repro_args(
        "fake_env",
        {
            "trial_json": str(trial_path),
            "output_dir": str(output_dir),
            "wandb": False,
        },
    )

    assert "metrics" not in prepared
    assert prepared["checkpoint_interval"] == 0
    assert prepared["checkpoint_dir"] == str(output_dir / "checkpoints")
    assert prepared["log_dir"] == str(output_dir / "logs")
    assert prepared["rank"] == 0
    assert prepared["gpu_id"] == 0
    assert prepared["world_size"] == 1
    assert prepared["no_model_upload"] is True
    assert pufferl._ReproSweep().early_stop({}, "env/score") is False
