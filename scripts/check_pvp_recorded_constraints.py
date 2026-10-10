import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from verify_pvp_recorded_replay import verify_records


def verify_report(lines, actor_ids, recordings):
    if len(actor_ids) != 2 or actor_ids[0] == actor_ids[1]:
        raise ValueError('Two distinct recorded actor identities are required')
    hypotheses = []
    summaries = []
    for line in lines:
        row = json.loads(line)
        if row['record_type'] == 'hypothesis':
            hypotheses.append(row)
        elif row['record_type'] == 'window_summary':
            summaries.append(row)
        else:
            raise ValueError(f"Unknown replay record type: {row['record_type']}")
    if len(summaries) != 1:
        raise ValueError('Select exactly one replay window')
    summary = summaries[0]
    if summary['schema_version'] != 1:
        raise ValueError('Unsupported replay report schema')
    if summary['first_processed_actor_values'] != [0, 1] or summary['actor_mapping'] != [0, 1]:
        raise ValueError('Both processing orders and canonical actor mapping are required')
    if len(hypotheses) != summary['total_hypotheses']:
        raise ValueError('Replay report is incomplete')
    bounds = summary['hp_bounds']
    expected_grid = {(hp0, hp1, first)
                     for hp0 in range(bounds[0][0], bounds[0][1] + 1)
                     for hp1 in range(bounds[1][0], bounds[1][1] + 1)
                     for first in summary['first_processed_actor_values']}
    actual_grid = {(row['hp'][0], row['hp'][1], row['first_processed_actor']) for row in hypotheses}
    if len(actual_grid) != len(hypotheses) or actual_grid != expected_grid:
        raise ValueError('Replay hypotheses are duplicated or do not cover the declared grid')
    for row in hypotheses:
        if any(row[key] != summary[key] for key in
               ('schema_version', 'window', 'archive', 'source_sha256', 'assumptions')):
            raise ValueError('Replay hypothesis provenance disagrees with summary')
        if row['status'] not in ('compatible', 'contradicted'):
            raise ValueError('Unknown hypothesis outcome')
        if (row['status'] == 'contradicted') != ('first_failure' in row):
            raise ValueError('Contradicted hypotheses require a first failure')
    survivors = sum(row['status'] == 'compatible' for row in hypotheses)
    if survivors != summary['survivors'] or len(hypotheses) - survivors != summary['contradicted']:
        raise ValueError('Replay survivor count disagrees')
    exhaustion = None if survivors else max(row['first_failure']['tick'] for row in hypotheses)
    if exhaustion != summary['global_exhaustion_tick']:
        raise ValueError('Replay exhaustion tick disagrees with hypothesis failures')
    expected = [{'window': summary['window'], 'simulated_hits': summary['recorded_hits'],
                 'health_bars': summary['health_bars']}]
    archive = recordings / summary['archive']
    with subprocess.Popen(['zstd', '-dc', str(archive)], stdout=subprocess.PIPE) as process:
        with process.stdout as stream:
            verification = verify_records(stream, summary['source_sha256'], expected, actor_ids)
        if process.wait() != 0:
            raise RuntimeError(f'Archive decompression failed: {archive}')
    if summary['dead_actor'] >= 0 and not any(
        death['target'] == summary['dead_actor'] for death in verification['death_callbacks']
    ):
        raise ValueError('Recorded terminal death has no matching archive callback')
    return {
        'window': summary['window'],
        'status': 'compatible_under_declared_inputs' if survivors else 'contradicted_under_declared_inputs',
        'summary': summary,
        'archive_verification': verification,
        'interpretation': 'Survival establishes compatibility, not identification of hidden state or live parity.',
        'unverified': ['Submitted actions and attack availability', 'Accuracy and damage generation',
                       'Recorded-scene collision and movement', 'Hidden resource and prayer histories'],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('replay', type=Path)
    parser.add_argument('--actor-ids', type=int, nargs=2, required=True)
    parser.add_argument('--recordings', type=Path,
                        default=Path.home() / '.light2/gameplay-recordings/spectator-combat')
    args = parser.parse_args()
    with args.replay.open() as stream:
        result = verify_report(stream, args.actor_ids, args.recordings)
    result['report_sha256'] = hashlib.sha256(args.replay.read_bytes()).hexdigest()
    print(json.dumps(result, indent=2))
    return 0 if result['summary']['survivors'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
