import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / 'scripts'))
from run_riskfight_sweep import OBJECTIVE_ID, read_config, stage_resume


class ResumeTests(unittest.TestCase):
    def test_preserves_successes_failures_and_previous_observations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            logs = source / 'logs/osrs_riskfight'
            logs.mkdir(parents=True)
            binary = root / 'binary'
            binary.write_bytes(b'fixed simulator')
            manifest = {'objective_id': OBJECTIVE_ID,
                        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
            (source / 'manifest.json').write_text(json.dumps(manifest))
            (source / 'status.json').write_text('{"status":"complete"}')
            (source / 'logs/sweep.log').write_text(
                'sweep run=0 score=0.25\nsweep worker run=1 failed; marking sample bad\n')
            (logs / 'sweep_123_0000.ini').write_text(
                '[metrics]\nselfplay/bot_ladder_perf=0.25\nuptime=30\n[policy]\nhidden_size=128\n')
            (logs / 'sweep_123_0001.ini').write_text('[policy]\nhidden_size=512\n')
            (logs / 'sweep_123_0001.jsonl').write_text('{"uptime":1}\n{"uptime":12}\n')
            prior = source / 'resume'
            prior.mkdir()
            (prior / 'sweep_100_0000.ini').write_text('[resume]\nstatus=success\n')
            destination = root / 'resume'
            self.assertEqual(stage_resume(source, destination, binary, OBJECTIVE_ID), 3)
            success = read_config(destination / 'sweep_123_0000.ini')
            failure = read_config(destination / 'sweep_123_0001.ini')
            self.assertEqual(success['resume']['score'], '0.25')
            self.assertEqual(success['policy']['hidden_size'], '128')
            self.assertEqual(failure['resume']['status'], 'failed')
            self.assertEqual(failure['resume']['cost'], '12')
            self.assertNotIn('score', failure['resume'])
            with self.assertRaises(AssertionError):
                stage_resume(source, root / 'different-objective', binary, 'supply-aware-exits-v2')
            self.assertFalse((root / 'different-objective').exists())
            binary.write_bytes(b'changed simulator')
            with self.assertRaises(AssertionError):
                stage_resume(source, root / 'mismatch', binary, OBJECTIVE_ID)
            self.assertFalse((root / 'mismatch').exists())


if __name__ == '__main__':
    unittest.main()
