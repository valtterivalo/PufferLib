import configparser
import re
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).parents[1] / 'scripts'))
from run_nh_sweep import REWARD_CONTROLS, REWARD_FEATURE_BOUNDS, opponent_contract, sweep_contract

ROOT = Path(__file__).parents[1]
TYPES = '''typedef enum {
    OPP_NONE = 0, OPP_BAIT = 13, OPP_GENERALIST = 23,
    OPP_VENG = 27, OPP_BURST = 29, OPP_RANGE = 30,
    OPP_SELFPLAY = 31, OPP_MIXED_NH = 33,
    OPP_RECORDED_ADAPTIVE = 35, OPP_BLOOD = 36, OPP_FREEZE = 37,
} OpponentType;'''
CONTROLLERS = '''static const OpponentType MIXED_NH_POOL[] = {
    OPP_GENERALIST, OPP_RECORDED_ADAPTIVE, OPP_BLOOD, OPP_FREEZE,
};
static const int MIXED_NH_CUM_WEIGHTS[] = {25, 50, 75, 100};'''


class NhSweepTests(unittest.TestCase):
    def setUp(self):
        self.config = configparser.ConfigParser()
        self.config.optionxform = str
        self.config.read([ROOT / 'config/default.ini', ROOT / 'config/osrs_pvp.ini'])
        self.opponents, self.members, self.weights = opponent_contract(TYPES, CONTROLLERS)

    def contract(self, source='committed-source'):
        return sweep_contract(self.config, self.opponents, self.members, self.weights, source)

    def selfplay(self, history):
        self.config['env'].update(opponent_type='31', self_play='1', use_c_opponent='0')
        self.config['selfplay']['enabled'] = str(history)
        self.config['vec'].update(num_policies='2' if history else '1',
            hist_policy_percent='0.5' if history else '0')

    def test_canonical_sparse_ids_and_equal_sampler(self):
        self.assertEqual(set(self.opponents.values()), {0, 13, 23, 27, 29, 30, 31, 33, 35, 36, 37})
        self.assertEqual(self.members, [23, 35, 36, 37])
        self.assertEqual(self.weights, [25, 25, 25, 25])
        with self.assertRaises(AssertionError):
            opponent_contract(TYPES.replace('OPP_FREEZE = 37', 'OPP_FREEZE = 36'), CONTROLLERS)

    def test_default_is_fresh_with_independent_scripted_benchmark(self):
        result = self.contract()
        self.assertEqual(result['training_mode'], 'scripted')
        self.assertEqual(result['training_opponent_members'], [23, 35, 36, 37])
        self.assertEqual(result['eval_bots'], [23, 35, 36, 37])
        self.assertEqual(self.config.getint('selfplay', 'eval_bot_games'), 1024)
        self.assertEqual(self.config['sweep']['metric'], 'selfplay/bot_ladder_perf')
        self.assertEqual(result['self_play'], 0)
        self.assertEqual(result['historical_policies'], 0)

    def test_training_opponent_and_benchmark_are_independent(self):
        self.config['env']['opponent_type'] = '36'
        self.config['selfplay']['eval_bots'] = '23,35,37'
        result = self.contract()
        self.assertEqual(result['training_opponent_members'], [36])
        self.assertEqual(result['eval_bots'], [23, 35, 37])
        self.config['env']['opponent_type'] = '33'
        self.assertEqual(self.contract()['training_opponent_members'], [23, 35, 36, 37])

    def test_current_and_optional_history_modes(self):
        self.selfplay(0)
        current = self.contract()
        self.assertEqual(current['training_mode'], 'current_selfplay')
        self.assertEqual(current['training_opponent_members'], [])
        self.selfplay(1)
        history = self.contract()
        self.assertEqual(history['training_mode'], 'historical_selfplay')
        self.assertEqual(history['history_environment_fraction'], 0.5)
        self.assertNotEqual(current['objective_id'], history['objective_id'])
        for value in ['0', '1.1']:
            self.config['vec']['hist_policy_percent'] = value
            with self.assertRaises(AssertionError):
                self.contract()
        self.config['vec']['hist_policy_percent'] = '0.5'
        self.config['vec']['num_policies'] = '1'
        with self.assertRaises(AssertionError):
            self.contract()

    def test_rejects_removed_ids_mode_tokens_and_incompatible_modes(self):
        for bots in ['0', '31', '33', '24', '32', '34', '23,23']:
            self.config['selfplay']['eval_bots'] = bots
            with self.assertRaises(AssertionError):
                self.contract()
        self.config['selfplay']['eval_bots'] = '23,35,36,37'
        self.selfplay(0)
        self.config['env']['use_c_opponent'] = '1'
        with self.assertRaises(AssertionError):
            self.contract()
        self.config['env']['use_c_opponent'] = '0'
        self.config['env']['opponent_type'] = '36'
        with self.assertRaises(AssertionError):
            self.contract()

    def test_objective_changes_with_source_config_and_benchmark(self):
        original = self.contract()['objective_id']
        self.assertEqual(original, self.contract()['objective_id'])
        self.assertNotEqual(original, self.contract('different-source')['objective_id'])
        self.config['train']['learning_rate'] = '0.001'
        changed_config = self.contract()['objective_id']
        self.assertNotEqual(original, changed_config)
        self.config['selfplay']['eval_bots'] = '23,36,37'
        self.assertNotEqual(changed_config, self.contract()['objective_id'])
        self.assertTrue(original.startswith('nh-actual-wins-v2-'))
        self.config['sweep']['metric'] = 'env/episode_return'
        with self.assertRaises(AssertionError):
            self.contract()

    def test_rejects_warmstart_and_shaped_selection(self):
        for section, key, value in [
            ('base', 'load_model_path', '/old-policy.bin'),
            ('base', 'load_enemy_model_path', '/old-opponent.bin'),
            ('bot_eval', 'shaping_enabled', '1'),
        ]:
            original = self.config[section][key]
            self.config[section][key] = value
            with self.assertRaises(AssertionError):
                self.contract()
            self.config[section][key] = original

    def test_reward_search_covers_native_fields_and_preserves_headroom(self):
        source = (ROOT / 'ocean/osrs/osrs_types.h').read_text()
        registry = source[source.index('#define OSRS_PVP_REWARD_FLOAT_FIELDS'):]
        registry = registry[:registry.index('typedef struct')]
        fields = set(re.findall(r'X\("(\w+)",', registry))
        expected = REWARD_FEATURE_BOUNDS.keys() | REWARD_CONTROLS.keys()
        self.assertEqual(fields, expected)
        actual = {section.removeprefix('sweep.env.') for section in self.config.sections()
            if section.startswith('sweep.env.')}
        self.assertEqual(actual, expected)
        result = self.contract()
        self.assertAlmostEqual(result['maximum_absolute_step_reward'], 0.9465)
        for enabled in ('0', '1'):
            self.config['env']['shaping_enabled'] = enabled
            self.assertEqual(self.contract()['reward_search']['shaping_enabled']['distribution'],
                'int_uniform')

    def test_reward_search_rejects_saturation_and_fractional_gates(self):
        for section, key, value in [
            ('sweep.env.damage_dealt_coef', 'max', '0.01'),
            ('sweep.env.terminal_reward_coef', 'max', '1'),
            ('sweep.env.shaping_enabled', 'distribution', 'uniform'),
            ('env', 'shaping_enabled', '0.5'),
            ('sweep.env.click_penalty_threshold', 'max', '16.5'),
            ('sweep.env.damage_burst_threshold', 'min', '0'),
            ('env', 'gear_tier', '-1'),
        ]:
            original = self.config[section][key]
            self.config[section][key] = value
            with self.assertRaises(AssertionError):
                self.contract()
            self.config[section][key] = original
        self.config['sweep.env.opponent_type'] = {'distribution': 'int_uniform',
            'min': '23', 'max': '37', 'scale': 'auto'}
        with self.assertRaises(AssertionError):
            self.contract()

    def test_actual_source_catalogue_drives_active_mixture(self):
        opponents, members, weights = opponent_contract(
            (ROOT / 'ocean/osrs/osrs_types.h').read_text(),
            (ROOT / 'ocean/osrs/osrs_pvp_opponents.h').read_text())
        result = sweep_contract(self.config, opponents, members, weights, 'source')
        self.assertEqual(result['training_opponent_members'], members)
        self.assertEqual(result['training_opponent_weights'], weights)
        self.assertEqual(result['eval_bots'], [23, 35, 36, 37])


if __name__ == '__main__':
    unittest.main()
