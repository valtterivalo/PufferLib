import importlib
import importlib.resources
import sys
import types


def import_pufferl_with_native_stub(monkeypatch):
    native_stub = types.SimpleNamespace(env_name=None, static_env_name=None)
    monkeypatch.setitem(sys.modules, "pufferlib._C", native_stub)
    sys.modules.pop("pufferlib.pufferl", None)
    return importlib.import_module("pufferlib.pufferl")


def test_load_config_includes_aurora_reference_defaults(monkeypatch):
    pufferl = import_pufferl_with_native_stub(monkeypatch)
    monkeypatch.setattr(sys, "argv", ["puffer"])

    args = pufferl.load_config("g2048")

    assert args["train"]["weight_decay"] == 0.0
    assert args["train"]["aurora_weight_decay"] == 0.025
    assert "aurora_target" not in args["train"]
    assert args["train"]["aurora_row_stats"] == 0


def test_aurora_optimizer_metadata_is_role_scoped():
    root = importlib.resources.files("pufferlib").parent
    muon_source = (root / "src" / "muon.cu").read_text()
    models_source = (root / "src" / "models.cu").read_text()

    assert "min(R, C) >= 2" in muon_source
    assert "e.role == OPT_PARAM_MINGRU" in muon_source
    assert "MUON_ROW_STAT_ROLE_OFFSET" in muon_source
    assert "OPT_PARAM_ENCODER" in models_source
    assert "OPT_PARAM_DECODER" in models_source
    assert "OPT_PARAM_LOGSTD" in models_source
    assert "OPT_PARAM_MINGRU" in models_source


def test_dashboard_shows_split_cuda_train_profile(monkeypatch, capsys):
    pufferl = import_pufferl_with_native_stub(monkeypatch)
    args = {
        "env_name": "g2048",
        "train": {"total_timesteps": 4096},
    }
    logs = {
        "agent_steps": 1024,
        "SPS": 2048,
        "epoch": 1,
        "uptime": 1.0,
        "perf/rollout": 0.1,
        "perf/train": 0.2,
        "perf/eval_gpu": 0.03,
        "perf/eval_env": 0.04,
        "perf/train_misc": 0.05,
        "perf/train_model": 0.06,
        "perf/train_muon": 0.07,
    }

    pufferl.print_dashboard(args, 1234, logs, idx=[0])

    out = capsys.readouterr().out
    assert "Model" in out
    assert "Muon" in out
