import argparse
import hashlib
import json
import re
from pathlib import Path

from rescore_riskfight_sweep import evaluate, ladder_args, read_config


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('--repo', required=True, type=Path)
    args = parser.parse_args()
    manifest = json.loads((args.root / 'manifest.json').read_text())
    completed = set(map(int, re.findall(r'^sweep run=(\d+) score=',
        (args.root / 'logs/sweep.log').read_text(), re.M)))
    trials = []
    for path in (args.root / 'logs/osrs_riskfight').glob('*.ini'):
        if int(path.stem.rsplit('_', 1)[-1]) not in completed:
            continue
        config = read_config(path)
        score = float(config['metrics']['selfplay/bot_ladder_perf'])
        trials.append((score, path))
    trials.sort(key=lambda trial: (-trial[0], trial[1].name))
    assert trials, 'No successful sweep trials to evaluate'
    results = []
    evaluation_bots = manifest['eval_bots'] + manifest['heldout_bots']
    for rank, (score, path) in enumerate(trials[:3]):
        config = read_config(path)
        checkpoint = sorted((args.root / 'checkpoints/osrs_riskfight' / path.stem).glob('*.bin'))[-1]
        for seed in manifest['heldout_seeds']:
            config['base']['seed'] = str(seed)
            command = ladder_args(args.root / 'riskfight', checkpoint, config, ','.join(map(str, evaluation_bots)))
            bots = evaluate(command, args.repo, args.root / 'logs' / f'heldout-{path.stem}-{seed}.log')
            results.append({'selection_rank': rank, 'run': path.stem, 'selection_score': score,
                'checkpoint': str(checkpoint), 'checkpoint_sha256': hashlib.sha256(checkpoint.read_bytes()).hexdigest(),
                'seed': seed, 'bots': bots})
            (args.root / 'heldout.json').write_text(json.dumps(results, indent=2))
    import wandb

    api = wandb.Api()
    for rank, (_, path) in enumerate(trials[:3]):
        run_id = f'{args.root.name}-{path.stem.rsplit("_", 1)[-1]}'
        run = api.run(f'valtterivalo-clock-cloud/osrs-riskfight/{run_id}')
        run.summary['heldout/selection_rank'] = rank
        for bot_id in evaluation_bots:
            samples = [bot for result in results if result['run'] == path.stem
                       for bot in result['bots'] if bot['bot'] == bot_id]
            for metric in ('perf', 'kills_rate', 'deaths_rate', 'escapes_rate'):
                run.summary[f'heldout/bot_{bot_id}/{metric}'] = sum(bot[metric] for bot in samples) / len(samples)
        run.summary.update()
    (args.root / 'heldout-status.json').write_text('{"status":"complete"}')


if __name__ == '__main__':
    main()
