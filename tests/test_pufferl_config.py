import configparser
from pathlib import Path

import pytest


pufferl = pytest.importorskip("pufferlib.pufferl", exc_type=ImportError)


ROOT = Path(__file__).resolve().parents[1]


def test_load_config_prefers_exact_config_basename(monkeypatch: pytest.MonkeyPatch) -> None:
    parser = configparser.ConfigParser()
    parser.read(ROOT / "config/ocean/osrs_pvp.ini")
    monkeypatch.setattr("sys.argv", ["puffer"])

    args = pufferl.load_config("osrs_pvp")

    assert args["policy"]["hidden_size"] == parser.getint("policy", "hidden_size")
    assert args["policy"]["num_layers"] == parser.getint("policy", "num_layers")
    assert args["score_metric"] == parser.get("base", "score_metric")


def test_load_config_includes_state_curriculum_hypers(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("sys.argv", ["puffer"])

    args = pufferl.load_config("default")

    assert "state_lambda" in args["train"]
    assert "state_priority_decay" in args["train"]
    assert args["train"]["state_lambda"] == pytest.approx(args["train"]["gae_lambda"])
    assert args["train"]["state_priority_decay"] == pytest.approx(0.95)


def test_load_config_includes_rollout_eval_section(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("sys.argv", ["puffer"])

    args = pufferl.load_config("default")

    assert args["rollout_eval"]["mode"] == "off"
    assert args["rollout_eval"]["episodes"] == 4096
    assert args["rollout_eval"]["keep_weights"] == 1


def test_osrs_pvp_uses_static_scripted_rollout_eval(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("sys.argv", ["puffer"])

    args = pufferl.load_config("osrs_pvp")

    assert args["rollout_eval"]["mode"] == "pvp_static_mixed_panel"
    assert args["rollout_eval"]["scripted_weight"] == pytest.approx(1.0)
    assert args["rollout_eval"]["policy_weight"] == pytest.approx(0.0)
    assert len(args["rollout_eval"]["scripted_opponents"]) == 28


def test_load_config_includes_sweep_early_stop_metric(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr("sys.argv", ["puffer"])

    args = pufferl.load_config("osrs_pvp")

    assert args["sweep"]["early_stop_metric"] == "score"
