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
