import hashlib
import importlib.util
import json
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('verify', Path(__file__).parents[1] / 'scripts/verify_pvp_recorded_replay.py')
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


class ProvenanceTests(unittest.TestCase):
    def setUp(self):
        rows = [
            {'eventKind': 'ACTOR_DEATH_OBSERVED', 'payload': {'actor': {'actorTraceId': 7}}},
            {'eventKind': 'HITSPLAT_APPLIED_OBSERVED', 'payload': {'actor': {'actorTraceId': 7}, 'amount': 61}},
            {'eventKind': 'SESSION_ENDED', 'payload': {}},
        ]
        self.lines = [(json.dumps({**row, 'sequence': i, 'lastObservedGameTick': 12,
                                  'lastObservedClientCycle': 360}) + '\n').encode()
                      for i, row in enumerate(rows)]
        self.digest = hashlib.sha256(b''.join(self.lines)).hexdigest()
        self.candidates = [{'window': 'death', 'simulated_hits': [{'tick': 12, 'target': 0, 'damage': 61}]}]

    def test_death_callback_before_hitsplat_preserved_without_server_order_claim(self):
        result = verify.verify_records(self.lines, self.digest, self.candidates, [7, 8])
        self.assertEqual(result['death_callbacks'][0]['sequence'], 0)
        self.assertEqual(result['observed_hits'][0][0]['sequence'], 1)

    def test_altered_hit_fails_even_with_valid_archive(self):
        self.candidates[0]['simulated_hits'][0]['damage'] = 60
        with self.assertRaisesRegex(ValueError, 'Recorded hits disagree'):
            verify.verify_records(self.lines, self.digest, self.candidates, [7, 8])

    def test_wrong_source_hash_fails(self):
        with self.assertRaisesRegex(ValueError, 'source hash mismatch'):
            verify.verify_records(self.lines, 'wrong', self.candidates, [7, 8])

    def test_incomplete_recording_fails(self):
        lines = self.lines[:-1]
        digest = hashlib.sha256(b''.join(lines)).hexdigest()
        with self.assertRaisesRegex(ValueError, 'clean session end'):
            verify.verify_records(lines, digest, self.candidates, [7, 8])

    def test_missing_record_fails(self):
        with self.assertRaisesRegex(ValueError, 'sequence gap'):
            verify.verify_records(self.lines[1:], self.digest, self.candidates, [7, 8])

    def test_health_bar_matches_original_record(self):
        row = json.loads(self.lines[0])
        row['payload']['state'] = {'healthBar': {'ratio': 14, 'scale': 30}}
        self.lines[0] = (json.dumps(row) + '\n').encode()
        digest = hashlib.sha256(b''.join(self.lines)).hexdigest()
        bars = [{'tick': 12, 'target': 0, 'ratio': 14, 'scale': 30}]
        self.candidates[0]['health_bars'] = bars
        result = verify.verify_records(self.lines, digest, self.candidates, [7, 8])
        self.assertEqual(result['health_bars'][0]['sequence'], 0)
        bars[0]['ratio'] = 15
        with self.assertRaisesRegex(ValueError, 'Recorded health bar disagrees'):
            verify.verify_records(self.lines, digest, self.candidates, [7, 8])


if __name__ == '__main__':
    unittest.main()
