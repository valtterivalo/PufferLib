import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / 'scripts'))
from check_pvp_damage_distribution import predictive_comparison, verify_observations
from check_pvp_recorded_constraints import verify_report


class RecordedDiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.recordings = Path(self.directory.name)
        rows = [
            {'eventKind': 'HITSPLAT_APPLIED_OBSERVED', 'payload': {
                'actor': {'actorTraceId': 7, 'presenceId': 3}, 'amount': 12}},
            {'eventKind': 'SESSION_ENDED', 'payload': {}},
        ]
        self.raw = b''.join((json.dumps({**row, 'sequence': sequence,
                                        'lastObservedGameTick': 20,
                                        'lastObservedClientCycle': 600}) + '\n').encode()
                            for sequence, row in enumerate(rows))
        self.archive = 'sample.jsonl.zst'
        (self.recordings / self.archive).write_bytes(
            subprocess.run(['zstd', '-q', '-c'], input=self.raw,
                           capture_output=True, check=True).stdout)
        self.digest = hashlib.sha256(self.raw).hexdigest()
        self.document = {
            'archive': self.archive, 'source_sha256': self.digest,
            'conditions': [{'field': 'source_attack', 'value': 'declared', 'basis': 'assumed'}],
            'observations': [{'sequence': 0, 'tick': 20, 'actor_trace_id': 7,
                              'presence_id': 3, 'damage': 12}],
        }

    def report(self):
        common = {'schema_version': 1, 'window': 'sample', 'archive': self.archive,
                  'source_sha256': self.digest, 'assumptions': 'declared source damage'}
        rows = [{**common, 'record_type': 'hypothesis', 'hp': [50, 50],
                 'first_processed_actor': first, 'status': 'compatible'} for first in (0, 1)]
        summary = {**common, 'record_type': 'window_summary', 'hp_bounds': [[50, 50], [50, 50]],
                   'actor_mapping': [0, 1],
                   'first_processed_actor_values': [0, 1], 'total_hypotheses': 2,
                   'survivors': 2, 'contradicted': 0, 'global_exhaustion_tick': None,
                   'recorded_hits': [{'tick': 20, 'target': 0, 'damage': 12}],
                   'health_bars': [], 'dead_actor': -1}
        return rows + [summary]

    def test_complete_report_binds_to_original_recording(self):
        result = verify_report(map(json.dumps, self.report()), [7, 8], self.recordings)
        self.assertEqual(result['status'], 'compatible_under_declared_inputs')

    def test_duplicate_padding_cannot_hide_missing_hypothesis(self):
        rows = self.report()
        rows[1] = rows[0]
        with self.assertRaisesRegex(ValueError, 'duplicated'):
            verify_report(map(json.dumps, rows), [7, 8], self.recordings)

    def test_mixed_source_hypothesis_rejected(self):
        rows = self.report()
        rows[0]['source_sha256'] = 'wrong'
        with self.assertRaisesRegex(ValueError, 'provenance'):
            verify_report(map(json.dumps, rows), [7, 8], self.recordings)

    def test_reduced_processing_order_space_cannot_claim_complete_enumeration(self):
        rows = self.report()
        rows[-1]['first_processed_actor_values'] = [0]
        with self.assertRaisesRegex(ValueError, 'Both processing orders'):
            verify_report(map(json.dumps, rows), [7, 8], self.recordings)

    def test_archive_disagrees_with_modified_expected_damage(self):
        rows = self.report()
        rows[-1]['recorded_hits'][0]['damage'] = 13
        with self.assertRaisesRegex(ValueError, 'Recorded hits disagree'):
            verify_report(map(json.dumps, rows), [7, 8], self.recordings)

    def test_damage_selection_requires_matching_presence_and_value(self):
        self.assertEqual(verify_observations(self.document, self.recordings), self.document['observations'])
        self.document['observations'][0]['presence_id'] = 4
        with self.assertRaisesRegex(ValueError, 'observation disagrees'):
            verify_observations(self.document, self.recordings)

    def test_distribution_rejects_impossible_value_without_fitting_seed(self):
        for seed in (1, 2):
            result = predictive_comparison([13], {12: 1}, 10, seed)
            self.assertEqual(result['status'], 'contradicted_under_declared_conditions')

    def test_predictive_replicates_preserve_observation_count(self):
        result = predictive_comparison([12, 12], {12: 1}, 10, 1)
        self.assertEqual(result['observed']['total_damage'], 24)
        self.assertEqual(result['observed']['zero_count'], 0)
        self.assertEqual(result['predictive_tail_counts']['total_damage'],
                         {'at_or_below': 10, 'at_or_above': 10})


if __name__ == '__main__':
    unittest.main()
