import argparse
import configparser
import hashlib
import json
import re
import subprocess
from pathlib import Path


OBJECTIVE_ID = 'riskfight-four-bot-v1'


def read_config(path):
    config = configparser.ConfigParser()
    config.optionxform = str
    assert config.read(str(path)) == [str(path)]
    return config


def ladder_args(binary, checkpoint, config, bots):
    args = [str(binary), 'ladder', f'--base.load_model_path={checkpoint}',
            f'--selfplay.eval_bots={bots}', '--env.self_play=0',
            '--env.damage_reward_coeff=0', '--env.teleport_penalty=0']
    for section, keys in {
        'base': ('seed', 'reset_every_horizon', 'async', 'eval_agents'),
        'policy': ('hidden_size', 'num_layers'),
        'train': ('horizon',),
        'vec': ('num_buffers', 'num_threads'),
        'selfplay': ('eval_bot_games', 'eval_bot_envs', 'eval_bot_threads'),
    }.items():
        args.extend(f'--{section}.{key}={config[section][key]}' for key in keys)
    return args


def evaluate(args, repo, log_path):
    with log_path.open('w') as output:
        subprocess.run(args, cwd=repo, stdout=output, stderr=subprocess.STDOUT, check=True)
    rows = []
    for line in log_path.read_text().splitlines():
        if line.startswith('LADDER_EVAL bot='):
            values = dict(field.split('=', 1) for field in line.split()[1:])
            rows.append({key: int(value) if key in ('bot', 'games') else float(value)
                         for key, value in values.items()})
    assert rows, f'No ladder results in {log_path}'
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--repo', required=True, type=Path)
    parser.add_argument('--binary', required=True, type=Path)
    args = parser.parse_args()
    assert json.loads((args.source / 'status.json').read_text())['status'] == 'complete'
    args.destination.mkdir(exist_ok=False)
    resume = args.destination / 'resume'
    logs = args.destination / 'logs'
    resume.mkdir()
    logs.mkdir()
    source_logs = args.source / 'logs' / 'osrs_riskfight'
    completed = {int(value) for value in re.findall(r'^sweep run=(\d+) score=',
        (args.source / 'logs' / 'sweep.log').read_text(), re.M)}
    summary = []
    for path in sorted(source_logs.glob('sweep_*.ini')):
        if int(path.stem.rsplit('_', 1)[1]) not in completed:
            continue
        config = read_config(path)
        checkpoint = sorted((args.source / 'checkpoints' / 'osrs_riskfight' / path.stem).glob('*.bin'))[-1]
        original = float(config['metrics']['selfplay/bot_ladder_perf'])
        if not summary:
            gate = evaluate(ladder_args(args.binary, checkpoint, config, '0,1,2'),
                            args.repo, logs / 'three-bot-protocol-gate.log')
            assert [row['bot'] for row in gate] == [0, 1, 2]
            repeated = sum(row['perf'] for row in gate) / 3
            assert abs(repeated - original) < 1e-6, (original, repeated)
        command = ladder_args(args.binary, checkpoint, config, '0,1,2,4')
        rows = evaluate(command, args.repo, logs / f'{path.stem}.log')
        assert [row['bot'] for row in rows] == [0, 1, 2, 4]
        assert all(row['games'] >= int(config['selfplay']['eval_bot_games']) for row in rows)
        score = sum(row['perf'] for row in rows) / len(rows)
        legacy = sum(row['perf'] for row in rows[:3]) / 3
        assert abs(legacy - original) < 1e-6, (path.stem, original, legacy)
        record = {'run': path.stem, 'checkpoint': str(checkpoint),
                  'checkpoint_sha256': hashlib.sha256(checkpoint.read_bytes()).hexdigest(),
                  'original_three_bot_score': original, 'three_bot_score': legacy,
                  'four_bot_score': score, 'bots': rows, 'command': command}
        summary.append(record)
        config['selfplay']['eval_bots'] = '0,1,2,4'
        config['resume'] = {'objective_id': OBJECTIVE_ID, 'score': str(score),
                            'cost': config['metrics']['uptime']}
        with (resume / path.name).open('w') as output:
            config.write(output)
        (args.destination / 'summary.json').write_text(json.dumps(summary, indent=2))
        print(f'{path.stem}: three={legacy:.6f} four={score:.6f}', flush=True)
    assert len(summary) == len(completed)
    manifest = {'objective_id': OBJECTIVE_ID, 'source': str(args.source),
                'source_manifest': json.loads((args.source / 'manifest.json').read_text()),
                'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                'git_sha': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=args.repo, text=True).strip(),
                'trials': len(summary), 'bots': [0, 1, 2, 4], 'weights': [0.25] * 4,
                'training_cost': 'Original training uptime, excludes separate rescoring work'}
    (args.destination / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    (args.destination / 'status.json').write_text(json.dumps({'status': 'complete'}))


if __name__ == '__main__':
    main()
