import argparse
import configparser
import json
import re
import time
from pathlib import Path


def completed_trial_checkpoints(root):
    completed = {
        int(value) for value in re.findall(
            r'^sweep run=(\d+) score=', (root / 'logs' / 'sweep.log').read_text(), re.M
        )
    }
    for config_path in sorted((root / 'logs').glob('*/sweep_*.ini')):
        if int(config_path.stem.rsplit('_', 1)[1]) not in completed:
            continue
        config = configparser.ConfigParser()
        config.read(config_path)
        checkpoint_dir = root / 'checkpoints' / config_path.parent.name / config_path.stem
        checkpoints = sorted(checkpoint_dir.glob('*.bin'))
        assert len(checkpoints) >= 2, f'Missing initial or final checkpoint: {checkpoint_dir}'
        assert all(re.fullmatch(r'\d{16}\.bin', path.name) for path in checkpoints)
        final_steps = float(config['metrics']['agent_steps'].split(',')[-1])
        assert int(checkpoints[-1].stem) == final_steps, f'Final checkpoint mismatch: {checkpoint_dir}'
        yield from checkpoints[1:-1]


def prune(root, apply):
    removed_bytes = 0
    removed_count = 0
    for path in completed_trial_checkpoints(root):
        size = path.stat().st_size
        if apply:
            with (root / 'checkpoint-retention.jsonl').open('a') as manifest:
                manifest.write(json.dumps({'removed': str(path.relative_to(root)), 'bytes': size,
                    'retained': 'Initial and final checkpoint for every completed trial'}) + '\n')
            path.unlink()
        removed_bytes += size
        removed_count += 1
    if removed_count:
        print(json.dumps({'root': str(root), 'apply': apply, 'checkpoints': removed_count,
                          'bytes': removed_bytes}), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('roots', nargs='+', type=Path)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--watch', action='store_true')
    args = parser.parse_args()
    while True:
        for root in args.roots:
            prune(root, args.apply)
        if not args.watch or all(
            json.loads((root / 'status.json').read_text())['status'] != 'running'
            for root in args.roots
        ):
            return
        time.sleep(15)


if __name__ == '__main__':
    main()
