import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def verify_records(lines, expected_hash, candidates, actor_ids):
    first = min(hit['tick'] for candidate in candidates for hit in candidate['simulated_hits'])
    last = max(hit['tick'] for candidate in candidates for hit in candidate['simulated_hits'])
    bars = candidates[0].get('health_bars', [])
    if bars:
        first = min(first, min(bar['tick'] for bar in bars))
        last = max(last, max(bar['tick'] for bar in bars))
    observed_bars = {}
    digest = hashlib.sha256()
    observed = [[], []]
    deaths = []
    count = 0
    last_kind = None
    for sequence, line in enumerate(lines):
        digest.update(line)
        record = json.loads(line)
        if record['sequence'] != sequence:
            raise ValueError(f'Record sequence gap at {sequence}')
        count += 1
        last_kind = record['eventKind']
        tick = record['lastObservedGameTick']
        if not first <= tick <= last:
            continue
        payload = record['payload']
        state = payload.get('state', payload.get('initialState'))
        if state is not None:
            identity = payload.get('actor', payload).get('actorTraceId')
            if identity in actor_ids:
                actor = actor_ids.index(identity)
                observed_bars[tick, actor] = {**state['healthBar'], 'sequence': sequence}
        if last_kind not in ('HITSPLAT_APPLIED_OBSERVED', 'ACTOR_DEATH_OBSERVED'):
            continue
        identity = payload['actor']['actorTraceId']
        if identity not in actor_ids:
            continue
        actor = actor_ids.index(identity)
        event = {'tick': tick, 'target': actor, 'sequence': sequence,
                 'client_cycle': record['lastObservedClientCycle']}
        if last_kind == 'ACTOR_DEATH_OBSERVED':
            deaths.append(event)
        else:
            observed[actor].append({**event, 'damage': payload['amount']})
    if digest.hexdigest() != expected_hash:
        raise ValueError('Decompressed source hash mismatch')
    if last_kind != 'SESSION_ENDED':
        raise ValueError('Recording has no clean session end')
    for bar in bars:
        observed_bar = observed_bars[bar['tick'], bar['target']]
        if any(observed_bar[key] != bar[key] for key in ('ratio', 'scale')):
            raise ValueError(f'Recorded health bar disagrees: {bar} != {observed_bar}')
    for candidate in candidates:
        for actor in range(2):
            simulated = [(hit['tick'], hit['damage']) for hit in candidate['simulated_hits']
                         if hit['target'] == actor]
            recorded = [(hit['tick'], hit['damage']) for hit in observed[actor]]
            if simulated != recorded:
                raise ValueError(f"Recorded hits disagree with {candidate['window']} actor {actor}: "
                                 f'{simulated} != {recorded}')
    return {'window': candidates[0]['window'], 'source_sha256': expected_hash,
            'records': count, 'compatible_candidates': len(candidates),
            'actor_trace_ids': actor_ids, 'observed_hits': observed, 'death_callbacks': deaths,
            'health_bars': [{**bar, 'sequence': observed_bars[bar['tick'], bar['target']]['sequence']}
                            for bar in bars],
            'scope': 'Conditioned damage and queue phases, not recovered inputs or server PID'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('replay', type=Path)
    parser.add_argument('--window', required=True)
    parser.add_argument('--actor-ids', required=True, type=int, nargs=2)
    parser.add_argument('--recordings', type=Path,
                        default=Path.home() / '.light2/gameplay-recordings/spectator-combat')
    args = parser.parse_args()
    with args.replay.open() as stream:
        candidates = [row for line in stream if (row := json.loads(line))['window'] == args.window]
    if not candidates:
        raise ValueError(f'No compatible candidates for {args.window}')
    archives = {(row['archive'], row['source_sha256']) for row in candidates}
    if len(archives) != 1:
        raise ValueError('Replay candidates have inconsistent provenance')
    archive, source_hash = archives.pop()
    with subprocess.Popen(['zstd', '-dc', str(args.recordings / archive)], stdout=subprocess.PIPE) as process:
        result = verify_records(process.stdout, source_hash, candidates, args.actor_ids)
        if process.wait() != 0:
            raise RuntimeError(f'Archive decompression failed: {archive}')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
