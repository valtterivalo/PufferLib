import importlib.util
import json
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('wandb_sync', Path(__file__).parents[1] / 'scripts/wandb_sync.py')
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)


class Run:
    def __init__(self):
        self.summary = {}
        self.rows = []
        self.exit_code = 0

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False

    def define_metric(self, *args, **kwargs):
        pass

    def log(self, row):
        self.rows.append(row)

    def finish(self, exit_code):
        self.exit_code = exit_code


class SyncTests(unittest.TestCase):
    def test_failed_trial_does_not_block_next_curve(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            logs = root / 'logs/osrs_riskfight'
            logs.mkdir(parents=True)
            (root / 'manifest.json').write_text('{}')
            (root / 'status.json').write_text('{"status":"complete"}')
            (root / 'logs/sweep.log').write_text('')
            streams = ['{"agent_steps":1,"loss/value":0.2}\n{"agent_steps":2,"loss/value":nan}\n',
                       '{"agent_steps":1,"env/net_stake":0.2}\n{"_finished":true}\n']
            runs = []
            for index, contents in enumerate(streams):
                path = logs / f'sweep_123_{index:04d}.jsonl'
                path.write_text(contents)
                path.with_suffix('.ini').write_text('[base]\nenv_name=osrs_riskfight\n')
                run = Run()
                fake = types.SimpleNamespace(init=lambda **kwargs: run, Settings=lambda **kwargs: None)
                with patch.dict(sys.modules, {'wandb': fake}):
                    sync.sync_trial(path, types.SimpleNamespace(root=root, entity='test', project='test'))
                runs.append(run)
            self.assertEqual([run.exit_code for run in runs], [1, 0])
            self.assertEqual(runs[0].summary['failure/agent_steps'], 2)
            self.assertEqual(len(runs[1].rows), 1)

    def test_failed_worker_finishes_without_global_sweep_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'logs').mkdir()
            (root / 'logs/sweep.log').write_text('sweep worker run=10 failed; marking sample bad\n')
            (root / 'status.json').write_text('{"status":"running"}')
            self.assertTrue(sync.trial_stopped(root, Path('sweep_123_0010.jsonl')))
            self.assertFalse(sync.trial_stopped(root, Path('sweep_123_0011.jsonl')))


if __name__ == '__main__':
    unittest.main()
