import sys

import pytest

from pufferlib import selfplay
from pufferlib.pufferl import load_config


def test_selfplay_sequence_parser_accepts_literal_tuple_and_comma_text():
    assert selfplay.parse_config_sequence((1, 2, 3), int, 'scripted_opp_pool') == [1, 2, 3]
    assert selfplay.parse_config_sequence('1,2,3', int, 'scripted_opp_pool') == [1, 2, 3]
    assert selfplay.parse_config_sequence((1, 2), float, 'scripted_opp_weights') == [1.0, 2.0]


def test_scripted_pfsp_hard_prioritizes_low_winrate_opponents():
    weights, cum_weights = selfplay.scripted_pfsp_weights(
        base_weights=[1.0, 1.0, 1.0],
        winrates=[0.99, 0.50, 0.10],
        mode='pfsp_hard',
        floor=0.001,
    )

    assert weights[2] > weights[1] > weights[0]
    assert cum_weights.tolist()[-1] == 1000
    assert all(cum_weights[i] < cum_weights[i + 1] for i in range(len(cum_weights) - 1))


def test_scripted_pfsp_respects_static_priors():
    weights, _ = selfplay.scripted_pfsp_weights(
        base_weights=[1.0, 4.0],
        winrates=[0.5, 0.5],
        mode='pfsp_hard',
        floor=0.001,
    )

    assert weights[1] == 0.8


def test_adaptive_scripted_env_pct_decays_after_scripts_are_solved():
    neutral_hardness = selfplay.scripted_env_hardness(
        winrates=[0.5, 0.5],
        base_weights=[1.0, 1.0],
        neutral_winrate=0.5,
        solved_winrate=0.9,
    )
    solved_hardness = selfplay.scripted_env_hardness(
        winrates=[0.95, 0.95],
        base_weights=[1.0, 1.0],
        neutral_winrate=0.5,
        solved_winrate=0.9,
    )

    assert neutral_hardness == 1.0
    assert solved_hardness == 0.0
    assert selfplay.adaptive_scripted_env_pct(0.4, 0.05, neutral_hardness) == 0.4
    assert selfplay.adaptive_scripted_env_pct(
        0.4, 0.05, solved_hardness) == pytest.approx(0.02)


def test_scripted_env_assignment_uses_dispatcher_for_adaptive_pfsp():
    assignments = selfplay.assign_scripted_envs(
        num_envs=6,
        eligible_env_indices=[1, 2, 4, 5],
        target_count=2,
        scripted_pfsp_enabled=True,
        scripted_dispatcher_opp=selfplay.OPP_PFSP,
        scripted_opps_list=[8, 25],
        scripted_base_weights=[1.0, 1.0],
        rng=None,
    )

    assert assignments.tolist() == [-1, selfplay.OPP_PFSP, selfplay.OPP_PFSP, -1, -1, -1]


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
    assert args['selfplay']['scripted_sampling'] == 'pfsp_hard'
    assert args['selfplay']['scripted_dispatcher_opp'] == 16
    assert args['selfplay']['scripted_env_schedule'] == 'adaptive'
    assert args['selfplay']['scripted_env_floor_frac'] == 0.05
