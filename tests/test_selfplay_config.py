from types import SimpleNamespace

import numpy as np
import pytest

from pufferlib import selfplay


class FakeBackend:
    def __init__(self, num_envs=4):
        self._num_envs = num_envs
        self.agent_perm = None
        self.env_tags = None
        self.scripted_opps = None
        self.train_mask = None
        self.pfsp_pool = None
        self.pfsp_cum_weights = None
        self.loaded_banks = []
        self.saved_weights = []

    def num_envs(self, pufferl):
        return self._num_envs

    def set_agent_perm(self, pufferl, perm):
        self.agent_perm = np.asarray(perm, dtype=np.int32)

    def set_env_tags(self, pufferl, tags):
        self.env_tags = np.asarray(tags, dtype=np.int32)

    def save_weights(self, pufferl, path):
        self.saved_weights.append(path)

    def load_frozen_bank(self, pufferl, bank_idx, path):
        self.loaded_banks.append((bank_idx, path))

    def set_env_scripted_opps(self, pufferl, scripted_opps):
        self.scripted_opps = np.asarray(scripted_opps, dtype=np.int32)

    def set_train_mask(self, pufferl, train_mask):
        self.train_mask = np.asarray(train_mask, dtype=np.uint8)

    def set_pfsp_weights(self, pufferl, pool, cum_weights):
        self.pfsp_pool = np.asarray(pool, dtype=np.int32)
        self.pfsp_cum_weights = np.asarray(cum_weights, dtype=np.int32)

    def get_pfsp_stats(self, pufferl):
        return {
            'pool_size': len(self.pfsp_pool),
            'wins': np.zeros(len(self.pfsp_pool), dtype=np.float32),
            'episodes': np.zeros(len(self.pfsp_pool), dtype=np.float32),
        }


def selfplay_args(tmp_path):
    return {
        'checkpoint_dir': str(tmp_path),
        'env_name': 'osrs_pvp',
        'vec': {
            'total_agents': 8,
            'num_buffers': 1,
            'num_frozen_banks': 1,
            'frozen_bank_pct': 0.25,
        },
        'selfplay': {
            'enabled': 1,
            'max_size': 8,
            'min_games': 1,
            'swap_winrate': 0.6,
            'scripted_opp_pool': '8,10,14',
            'scripted_opp_weights': '1,2,3',
            'scripted_sampling': 'pfsp_hard',
            'scripted_env_pct': 1.0,
            'scripted_env_schedule': 'adaptive',
            'scripted_env_floor_frac': 0.05,
            'scripted_env_solved_winrate': 0.9,
            'scripted_floor_weight': 0.001,
            'scripted_dispatcher_opp': selfplay.OPP_PFSP,
            'seed': 123,
        },
    }


def with_fake_c(fake_backend, fn):
    original = selfplay._C
    selfplay._C = fake_backend
    try:
        return fn()
    finally:
        selfplay._C = original


def test_adaptive_scripted_setup_assigns_pfsp_dispatcher(tmp_path):
    fake = FakeBackend()
    pufferl = SimpleNamespace(global_step=0)
    args = selfplay_args(tmp_path)

    def run():
        return selfplay.setup(pufferl, fake, args, 'run-id')

    pool_state = with_fake_c(fake, run)

    assert fake.pfsp_pool.tolist() == [8, 10, 14]
    assert fake.pfsp_cum_weights[-1] == 1000
    assert fake.env_tags.tolist() == [0, 0, 1, 1]
    assert fake.scripted_opps.tolist() == [
        selfplay.OPP_PFSP,
        selfplay.OPP_PFSP,
        -1,
        -1,
    ]
    assert fake.train_mask.tolist() == [1, 0, 1, 0, 1, 1, 1, 1]
    assert pool_state['scripted_target_envs'] == 2


def test_scripted_setup_fails_when_backend_hook_missing(tmp_path):
    class FakeBackendNoScripted:
        def __init__(self):
            self.impl = FakeBackend()

        def num_envs(self, pufferl):
            return self.impl.num_envs(pufferl)

        def set_agent_perm(self, pufferl, perm):
            self.impl.set_agent_perm(pufferl, perm)

        def set_env_tags(self, pufferl, tags):
            self.impl.set_env_tags(pufferl, tags)

        def save_weights(self, pufferl, path):
            self.impl.save_weights(pufferl, path)

        def load_frozen_bank(self, pufferl, bank_idx, path):
            self.impl.load_frozen_bank(pufferl, bank_idx, path)

        def set_pfsp_weights(self, pufferl, pool, cum_weights):
            self.impl.set_pfsp_weights(pufferl, pool, cum_weights)

        def get_pfsp_stats(self, pufferl):
            return self.impl.get_pfsp_stats(pufferl)

    fake = FakeBackendNoScripted()
    pufferl = SimpleNamespace(global_step=0)
    args = selfplay_args(tmp_path)

    def run():
        with pytest.raises(RuntimeError, match='set_env_scripted_opps'):
            selfplay.setup(pufferl, fake, args, 'run-id')

    with_fake_c(fake, run)


def test_scripted_setup_fails_when_train_mask_hook_missing(tmp_path):
    class FakeBackendNoTrainMask:
        def __init__(self):
            self.impl = FakeBackend()

        def num_envs(self, pufferl):
            return self.impl.num_envs(pufferl)

        def set_agent_perm(self, pufferl, perm):
            self.impl.set_agent_perm(pufferl, perm)

        def set_env_tags(self, pufferl, tags):
            self.impl.set_env_tags(pufferl, tags)

        def save_weights(self, pufferl, path):
            self.impl.save_weights(pufferl, path)

        def load_frozen_bank(self, pufferl, bank_idx, path):
            self.impl.load_frozen_bank(pufferl, bank_idx, path)

        def set_env_scripted_opps(self, pufferl, scripted_opps):
            self.impl.set_env_scripted_opps(pufferl, scripted_opps)

        def set_pfsp_weights(self, pufferl, pool, cum_weights):
            self.impl.set_pfsp_weights(pufferl, pool, cum_weights)

        def get_pfsp_stats(self, pufferl):
            return self.impl.get_pfsp_stats(pufferl)

    fake = FakeBackendNoTrainMask()
    pufferl = SimpleNamespace(global_step=0)
    args = selfplay_args(tmp_path)

    def run():
        with pytest.raises(RuntimeError, match='set_train_mask'):
            selfplay.setup(pufferl, fake, args, 'run-id')

    with_fake_c(fake, run)


def test_scripted_train_mask_preserves_pure_selfplay_rows():
    perm = np.asarray([0, 1, 2, 3, 6, 7, 4, 5], dtype=np.int32)
    scripted_envs = np.asarray([8, -1, -1, -1], dtype=np.int32)

    train_mask = selfplay.scripted_train_mask(
        total_agents=8,
        agents_per_env=2,
        perm=perm,
        scripted_envs=scripted_envs,
    )

    assert train_mask.tolist() == [1, 0, 1, 1, 1, 1, 1, 1]


def test_log_scripted_pfsp_emits_curriculum_keys():
    logs = {}
    pool_state = {
        'scripted_envs': np.asarray([16, 16, -1], dtype=np.int32),
        'scripted_target_envs': 2,
        'scripted_env_cap_pct': 0.8,
        'scripted_env_effective_pct': 0.4,
        'scripted_env_floor_frac': 0.05,
        'scripted_env_hardness': 0.5,
        'scripted_env_schedule': 'adaptive',
        'scripted_opps_list': [8, 10],
        'scripted_pfsp_enabled': True,
        'scripted_winrates': np.asarray([0.25, 0.75], dtype=np.float64),
        'scripted_weights': np.asarray([0.8, 0.2], dtype=np.float64),
        'scripted_games': np.asarray([12.0, 4.0], dtype=np.float64),
        'scripted_updates': 3,
    }

    selfplay.log_scripted_pfsp(pool_state, logs)

    assert logs['pool/scripted_envs'] == 2
    assert logs['pool/scripted_target_envs'] == 2
    assert logs['pool/scripted_env_adaptive'] == 1.0
    assert logs['pool/scripted_pfsp_enabled'] == 1.0
    assert logs['pool/scripted_opp_8_weight'] == 0.8
    assert logs['pool/scripted_opp_10_games'] == 4.0


def test_assign_scripted_envs_only_uses_eligible_envs():
    rng = np.random.default_rng(1)
    assignments = selfplay.assign_scripted_envs(
        6,
        np.asarray([0, 2, 4], dtype=np.int32),
        2,
        False,
        selfplay.OPP_PFSP,
        [8, 10],
        np.asarray([1.0, 0.0], dtype=np.float64),
        rng,
    )

    assert assignments.tolist() == [8, -1, 8, -1, -1, -1]
