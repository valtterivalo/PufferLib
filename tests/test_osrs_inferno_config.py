import configparser
import itertools
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
RUN_76_DEFAULTS = {
    "base": {
        "async": "1",
    },
    "env": {
        "offensive_prayer_reward_coeff": "0.0692558885",
        "death_penalty_coeff": "0.26163137",
        "curriculum_frac_1": "0.186450824",
        "curriculum_frac_5": "0.0685265288",
        "zuk_untagged_healer_nonmagic_attack_bonus_coeff": "0.932179332",
    },
    "train": {
        "total_timesteps": "395922816",
        "learning_rate": "0.000352853211",
        "min_lr_ratio": "0.0223900452",
        "momentum": "0.996223032",
        "ent_coef": "4.47068851e-06",
        "gamma": "0.999429405",
        "gae_lambda": "0.833382428",
        "prio_alpha": "0.469595075",
        "clip_coef": "0.145622194",
        "vf_coef": "2.38069582",
        "vf_clip_coef": "0.214555308",
        "max_grad_norm": "0.571861565",
        "replay_ratio": "4.0840683",
    },
}
ASYNC_ARCHITECTURE_VALUES = {
    "policy.hidden_size": (256, 512, 1024),
    "policy.num_layers": (1, 2, 3),
    "vec.total_agents": (1024, 2048, 4096),
    "vec.num_buffers": (1, 2, 4),
    "train.horizon": (64, 128, 256, 512),
    "train.minibatch_size": (2048, 4096, 8192, 16384),
}


def read_inferno_config() -> configparser.ConfigParser:
    config = configparser.ConfigParser()
    loaded = config.read(
        [
            REPOSITORY_ROOT / "config/default.ini",
            REPOSITORY_ROOT / "config/osrs_inferno.ini",
        ]
    )
    assert len(loaded) == 2
    return config


def test_inferno_defaults_match_run_76_anchor() -> None:
    config = read_inferno_config()

    for section, expected_values in RUN_76_DEFAULTS.items():
        actual_values = {key: config[section][key] for key in expected_values}
        assert actual_values == expected_values


def test_inferno_sweep_contains_its_anchor() -> None:
    config = read_inferno_config()

    for sweep_section in (
        section for section in config.sections() if section.startswith("sweep.")
    ):
        target_section, key = sweep_section.removeprefix("sweep.").rsplit(".", 1)
        value = float(config[target_section][key])
        assert float(config[sweep_section]["min"]) <= value
        assert value <= float(config[sweep_section]["max"])


def test_async_architecture_sweep_values_are_trainer_compatible() -> None:
    config = read_inferno_config()

    for path, expected_values in ASYNC_ARCHITECTURE_VALUES.items():
        sweep_section = f"sweep.{path}"
        low = int(config[sweep_section]["min"])
        high = int(config[sweep_section]["max"])
        if config[sweep_section]["distribution"] == "uniform_pow2":
            actual_values = tuple(
                1 << exponent
                for exponent in range(low.bit_length() - 1, high.bit_length())
            )
        else:
            actual_values = tuple(range(low, high + 1))
        assert actual_values == expected_values

    for total_agents, num_buffers, horizon, minibatch_size in itertools.product(
        ASYNC_ARCHITECTURE_VALUES["vec.total_agents"],
        ASYNC_ARCHITECTURE_VALUES["vec.num_buffers"],
        ASYNC_ARCHITECTURE_VALUES["train.horizon"],
        ASYNC_ARCHITECTURE_VALUES["train.minibatch_size"],
    ):
        assert total_agents % num_buffers == 0
        assert minibatch_size % horizon == 0
        assert minibatch_size <= total_agents * horizon
