import argparse
import hashlib
import json
import math
import random
import subprocess
from pathlib import Path


def verify_observations(document, recordings):
    selected = {row['sequence']: row for row in document['observations']}
    if not selected or len(selected) != len(document['observations']):
        raise ValueError('Damage observations must be nonempty and have unique sequences')
    if not document['conditions']:
        raise ValueError('Declare attack conditions and their evidence before comparison')
    if any(row['basis'] not in ('observed', 'assumed') for row in document['conditions']):
        raise ValueError('Conditions must distinguish observed evidence from assumptions')
    digest = hashlib.sha256()
    matched = []
    last_kind = None
    archive = recordings / document['archive']
    with subprocess.Popen(['zstd', '-dc', str(archive)], stdout=subprocess.PIPE) as process:
        with process.stdout as stream:
            for sequence, line in enumerate(stream):
                digest.update(line)
                record = json.loads(line)
                if record['sequence'] != sequence:
                    raise ValueError(f'Record sequence gap at {sequence}')
                last_kind = record['eventKind']
                if sequence not in selected:
                    continue
                expected = selected[sequence]
                payload = record['payload']
                if last_kind != 'HITSPLAT_APPLIED_OBSERVED':
                    raise ValueError(f'Observation {sequence} is not a hitsplat')
                actual = {'sequence': sequence, 'tick': record['lastObservedGameTick'],
                          'actor_trace_id': payload['actor']['actorTraceId'],
                          'presence_id': payload['actor']['presenceId'], 'damage': payload['amount']}
                if actual != expected:
                    raise ValueError(f'Recorded observation disagrees: {actual} != {expected}')
                matched.append(actual)
        if process.wait() != 0:
            raise RuntimeError(f'Archive decompression failed: {archive}')
    if digest.hexdigest() != document['source_sha256']:
        raise ValueError('Decompressed source hash mismatch')
    if last_kind != 'SESSION_ENDED':
        raise ValueError('Recording has no clean session end')
    if len(matched) != len(selected):
        raise ValueError('Recording is missing selected observations')
    return matched


def predictive_comparison(damages, probabilities, replicates, seed):
    if replicates <= 0 or not probabilities or not damages:
        raise ValueError('Positive replicates, a probability distribution and observations are required')
    if any(value < 0 or probability < 0 for value, probability in probabilities.items()):
        raise ValueError('Invalid damage support or probability')
    if abs(sum(probabilities.values()) - 1) > 1e-9:
        raise ValueError('Damage probabilities do not sum to one')
    unsupported = sorted({value for value in damages if probabilities.get(value, 0) == 0})
    if unsupported:
        return {'status': 'contradicted_under_declared_conditions', 'unsupported_damage': unsupported}
    values = list(probabilities)
    weights = list(probabilities.values())
    observed = {'zero_count': damages.count(0), 'total_damage': sum(damages),
                'negative_log_likelihood': -sum(math.log(probabilities[value]) for value in damages)}
    tails = {name: {'at_or_below': 0, 'at_or_above': 0} for name in observed}
    rng = random.Random(seed)
    for _ in range(replicates):
        sample = rng.choices(values, weights=weights, k=len(damages))
        statistics = {'zero_count': sample.count(0), 'total_damage': sum(sample),
                      'negative_log_likelihood': -sum(math.log(probabilities[value]) for value in sample)}
        for name, value in statistics.items():
            tails[name]['at_or_below'] += value <= observed[name]
            tails[name]['at_or_above'] += value >= observed[name]
    return {'status': 'within_support_predictive_checks_reported', 'observed': observed,
            'predictive_tail_counts': tails, 'replicates': replicates, 'seed': seed,
            'observations': len(damages),
            'scope': 'Independent identical-condition draws. Tail counts are diagnostics, not a parity certificate.'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('assay', type=Path)
    parser.add_argument('observations', type=Path)
    parser.add_argument('--replicates', type=int, required=True)
    parser.add_argument('--seed', type=int, required=True)
    parser.add_argument('--recordings', type=Path,
                        default=Path.home() / '.light2/gameplay-recordings/spectator-combat')
    args = parser.parse_args()
    assay = json.loads(args.assay.read_text())
    document = json.loads(args.observations.read_text())
    if assay['schema'] != 2:
        raise ValueError('Damage comparison requires the independent-reference assay schema')
    if document['assay_inputs'] != assay['scenario']:
        raise ValueError('Declared observation conditions do not match assay inputs')
    observations = verify_observations(document, args.recordings)
    probabilities = {row['value']: row['reference_probability'] for row in assay['reference']['landed_damage']}
    if len(probabilities) != len(assay['reference']['landed_damage']):
        raise ValueError('Assay contains duplicate damage outcomes')
    comparison = predictive_comparison([row['damage'] for row in observations], probabilities,
                                      args.replicates, args.seed)
    result = {'comparison': comparison, 'conditions': document['conditions'],
              'unobserved_inputs': document['unobserved_inputs'], 'observations': observations,
              'source_sha256': document['source_sha256'],
              'assay_sha256': hashlib.sha256(args.assay.read_bytes()).hexdigest(),
              'observation_selection_sha256': hashlib.sha256(args.observations.read_bytes()).hexdigest(),
              'assay_inputs': assay['scenario'],
              'interpretation': 'Source attribution and hidden attack conditions remain declared assumptions.'}
    print(json.dumps(result, indent=2))
    return 1 if comparison['status'] == 'contradicted_under_declared_conditions' else 0


if __name__ == '__main__':
    raise SystemExit(main())
