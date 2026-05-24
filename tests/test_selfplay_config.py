import sys

from pufferlib import selfplay
from pufferlib.pufferl import load_config


def test_selfplay_sequence_parser_accepts_literal_tuple_and_comma_text():
    assert selfplay.parse_config_sequence((1, 2, 3), int, 'scripted_opp_pool') == [1, 2, 3]
    assert selfplay.parse_config_sequence('1,2,3', int, 'scripted_opp_pool') == [1, 2, 3]
    assert selfplay.parse_config_sequence((1, 2), float, 'scripted_opp_weights') == [1.0, 2.0]


def test_osrs_pvp_v2_sweep_scripted_pool_config_survives_literal_eval(monkeypatch):
    monkeypatch.setattr(sys, 'argv', ['pytest'])
    monkeypatch.setenv('PUFFER_CONFIG_FILE', 'config/ocean/osrs_pvp_v2_sweep.ini')

    args = load_config('osrs_pvp')

    opponents = selfplay.parse_config_sequence(
        args['selfplay']['scripted_opp_pool'], int, 'scripted_opp_pool')
    weights = selfplay.parse_config_sequence(
        args['selfplay']['scripted_opp_weights'], float, 'scripted_opp_weights')

    assert opponents == [
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 27, 28, 29, 30,
    ]
    assert len(weights) == len(opponents)
