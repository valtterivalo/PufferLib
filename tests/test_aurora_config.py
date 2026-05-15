import argparse
import importlib
import sys
import types


def import_pufferl_with_native_stub(monkeypatch):
    native_stub = types.SimpleNamespace(env_name=None, static_env_name=None)
    monkeypatch.setitem(sys.modules, "pufferlib._C", native_stub)
    formatter_stub = types.SimpleNamespace(RichHelpFormatter=argparse.ArgumentDefaultsHelpFormatter)
    monkeypatch.setitem(sys.modules, "rich_argparse", formatter_stub)
    sys.modules.pop("pufferlib.pufferl", None)
    return importlib.import_module("pufferlib.pufferl")


def test_load_config_includes_aurora_defaults(monkeypatch):
    pufferl = import_pufferl_with_native_stub(monkeypatch)
    monkeypatch.setattr(sys, "argv", ["puffer"])

    args = pufferl.load_config("osrs_inferno")

    assert args["train"]["weight_decay"] == 0.0
    assert args["train"]["aurora_weight_decay"] == 0.025
    assert "aurora_target" not in args["train"]
