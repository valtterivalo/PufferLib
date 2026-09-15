import argparse
import hashlib
import itertools
import json
import statistics
import subprocess
import sys
from pathlib import Path

from prune_sweep_checkpoints import prune
from rescore_riskfight_sweep import evaluate, ladder_args, read_config


DISABLED = {
    'scripted_probability': 0,
    'scripted_omission_initial': 0,
    'scripted_omission_decay_ticks': 0,
    'midfight_probability': 0,
    'midfight_max_ticks': 0,
}
CONDITIONS = {
    'baseline': DISABLED,
    'scripted': {**DISABLED, 'scripted_probability': 0.2,
                 'scripted_omission_initial': 0.8, 'scripted_omission_decay_ticks': 150000},
    'midfight': {**DISABLED, 'midfight_probability': 0.5, 'midfight_max_ticks': 128},
}
SELECTION_BOTS = (0, 1, 2, 4, 5, 6, 8)
EVALUATION_SEEDS = (1009, 2027, 3037)


def selection_score(record):
    return statistics.mean(bot['perf'] for evaluation in record['evaluations']
                           if evaluation['seed'] in EVALUATION_SEEDS[:2]
                           for bot in evaluation['bots'] if bot['bot'] in SELECTION_BOTS)


def summary(results, failures):
    baseline = {row['training_seed']: selection_score(row) for row in results
                if row['condition'] == 'baseline'}
    output = {}
    for condition in CONDITIONS:
        rows = [row for row in results if row['condition'] == condition]
        scores = {row['training_seed']: selection_score(row) for row in rows}
        output[condition] = {
            'completed': len(rows),
            'failed': sum(row['condition'] == condition for row in failures),
            'selection_by_seed': scores,
            'paired_delta': {seed: score - baseline[seed] for seed, score in scores.items()
                             if seed in baseline},
        }
        if scores:
            output[condition]['mean_selection'] = statistics.mean(scores.values())
    return output


def main():
    import wandb

    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--anchor', type=Path, required=True)
    parser.add_argument('--seeds', default='73,101,211,317,419')
    args = parser.parse_args()
    assert wandb.Api().viewer.username == 'valtterivalo'
    config = read_config(args.anchor)
    seeds = [int(seed) for seed in args.seeds.split(',')]
    assert len(seeds) == len(set(seeds))
    args.root.mkdir()
    (args.root / 'logs').mkdir()
    (args.root / 'logs/sweep.log').touch()
    (args.root / 'anchor.ini').write_bytes(args.anchor.read_bytes())
    manifest = {
        'git_sha': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=args.repo, text=True).strip(),
        'repo': str(args.repo), 'binary': str(args.binary),
        'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        'anchor': str(args.anchor), 'anchor_sha256': hashlib.sha256(args.anchor.read_bytes()).hexdigest(),
        'training_seeds': seeds, 'conditions': CONDITIONS,
        'eval_bots': SELECTION_BOTS, 'heldout_bots': [7], 'evaluation_seeds': EVALUATION_SEEDS,
        'selection_seeds': EVALUATION_SEEDS[:2], 'heldout_seed': 3037,
        'training_steps': config['train']['total_timesteps'], 'checkpoint_interval': 64,
        'protocol': 'Seed-major matched curricula. Fixed anchor hyperparameters and rewards. '
                    'Full-fight unshaped evaluation. Script replacement only in frozen-policy slots. '
                    'Omission decay measured in per-environment learner interaction ticks, excluding prefixes.',
    }
    (args.root / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    status_path = args.root / 'status.json'
    status_path.write_text('{"status":"running"}')
    results, failures = [], []
    with (args.root / 'logs/wandb-sync.log').open('w') as sync_log:
        sync = subprocess.Popen([sys.executable, str(args.repo / 'scripts/wandb_sync.py'), str(args.root),
                                 '--entity=valtterivalo-clock-cloud', '--project=osrs-riskfight'],
                                stdout=sync_log, stderr=subprocess.STDOUT)
        try:
            for trial, (seed, condition) in enumerate(itertools.product(seeds, CONDITIONS)):
                run_id = f'sweep_curriculum_{trial:04d}'
                command = [str(args.binary), 'train']
                for section in ('base', 'vec', 'selfplay', 'env', 'policy', 'train', 'bot_eval'):
                    command.extend(f'--{section}.{key}={value}' for key, value in config[section].items())
                command.extend([
                    f'--base.run_id={run_id}', f'--base.seed={seed}',
                    f'--base.log_dir={args.root}/logs', f'--base.checkpoint_dir={args.root}/checkpoints',
                    '--base.load_model_path=None', '--base.load_enemy_model_path=None', '--base.result_fd=0',
                    '--base.checkpoint_interval=64', '--base.eval_episodes=0',
                    '--vec.hist_policy_hidden_size=0', '--vec.hist_policy_num_layers=0',
                    '--env.chance_reward_coeff=0', '--env.damage_reward_coeff=0',
                    '--bot_eval.chance_reward_coeff=0', '--bot_eval.damage_reward_coeff=0',
                    '--bot_eval.teleport_penalty=0', '--bot_eval.self_play=0',
                    '--selfplay.eval_bots=' + ','.join(map(str, SELECTION_BOTS)),
                ])
                command.extend(f'--env.{key}={value}' for key, value in CONDITIONS[condition].items())
                command.extend(f'--bot_eval.{key}={value}' for key, value in DISABLED.items())
                progress = {'trial': trial, 'seed': seed, 'condition': condition,
                            'phase': 'training', 'command': command}
                (args.root / 'progress.json').write_text(json.dumps(progress))
                log_path = args.root / 'logs' / f'{run_id}.log'
                with log_path.open('w') as log:
                    trained = subprocess.run(command, cwd=args.repo, stdout=log, stderr=subprocess.STDOUT)
                if trained.returncode:
                    failure = {**progress, 'exit_code': trained.returncode}
                    failures.append(failure)
                    (args.root / 'failures.json').write_text(json.dumps(failures, indent=2))
                    with (args.root / 'logs/sweep.log').open('a') as log:
                        log.write(f'sweep worker run={trial} failed; marking sample bad\n')
                    if 'nonfinite training loss:' in log_path.read_text():
                        continue
                    status_path.write_text(json.dumps({'status': 'failed', 'trial': trial}))
                    sync.wait()
                    raise SystemExit(trained.returncode)
                actual = read_config(args.root / 'logs/osrs_riskfight' / f'{run_id}.ini')
                with (args.root / 'logs/sweep.log').open('a') as log:
                    log.write(f'sweep run={trial} score=0\n')
                prune(args.root, True)
                checkpoint = sorted((args.root / 'checkpoints/osrs_riskfight' / run_id).glob('*.bin'))[-1]
                (args.root / 'progress.json').write_text(json.dumps({**progress, 'phase': 'evaluation'}))
                evaluations = []
                for eval_seed in EVALUATION_SEEDS:
                    actual['base']['seed'] = str(eval_seed)
                    command = ladder_args(args.binary, checkpoint, actual, '0,1,2,4,5,6,8,7')
                    command.extend(f'--env.{key}={value}' for key, value in DISABLED.items())
                    bots = evaluate(command, args.repo, args.root / 'logs' / f'eval-{trial}-{eval_seed}.log')
                    assert {bot['bot'] for bot in bots} == {*SELECTION_BOTS, 7} and len(bots) == 8
                    evaluations.append({'seed': eval_seed, 'bots': bots})
                results.append({'trial': trial, 'training_seed': seed, 'condition': condition,
                                'checkpoint': str(checkpoint), 'evaluations': evaluations})
                (args.root / 'results.json').write_text(json.dumps(results, indent=2))
                (args.root / 'summary.json').write_text(json.dumps(summary(results, failures), indent=2))
                print(json.dumps({'completed': len(results), 'failed': len(failures),
                                  'trial': trial, 'condition': condition}), flush=True)
            (args.root / 'summary.json').write_text(json.dumps(summary(results, failures), indent=2))
            status_path.write_text(json.dumps({'status': 'complete', 'completed': len(results), 'failed': len(failures)}))
        finally:
            if json.loads(status_path.read_text())['status'] == 'running':
                status_path.write_text('{"status":"failed"}')
            sync.wait()
        assert sync.returncode == 0


if __name__ == '__main__':
    main()
