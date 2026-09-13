import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

from rescore_riskfight_sweep import read_config


OBJECTIVE_ID = 'riskfight-seven-bot-chance-v1'


def stage_resume(source, destination, binary):
    manifest = json.loads((source / 'manifest.json').read_text())
    assert json.loads((source / 'status.json').read_text())['status'] == 'complete'
    assert manifest['objective_id'] == OBJECTIVE_ID
    assert manifest['binary_sha256'] == hashlib.sha256(binary.read_bytes()).hexdigest()
    destination.mkdir()
    if (source / 'resume').exists():
        for path in (source / 'resume').glob('*.ini'):
            shutil.copyfile(path, destination / path.name)
    log = (source / 'logs/sweep.log').read_text()
    completed = {int(value) for value in re.findall(r'^sweep run=(\d+) score=', log, re.M)}
    failed = {int(value) for value in re.findall(r'^sweep worker run=(\d+) failed;', log, re.M)}
    assert not completed & failed
    imported = set()
    for path in sorted((source / 'logs/osrs_riskfight').glob('sweep_*.ini')):
        trial = int(path.stem.rsplit('_', 1)[1])
        assert trial in completed | failed, f'Unfinished trial: {path}'
        config = read_config(path)
        if trial in completed:
            config['resume'] = {'objective_id': OBJECTIVE_ID, 'status': 'success',
                'score': config['metrics']['selfplay/bot_ladder_perf'],
                'cost': config['metrics']['uptime']}
        else:
            last = None
            with path.with_suffix('.jsonl').open() as rows:
                for line in rows:
                    last = json.loads(line)
            assert last is not None and last['uptime'] > 0
            config['resume'] = {'objective_id': OBJECTIVE_ID, 'status': 'failed',
                'cost': str(last['uptime'])}
        target = destination / path.name
        assert not target.exists(), f'Duplicate resume observation: {target}'
        with target.open('w') as output:
            config.write(output)
        imported.add(trial)
    assert imported == completed | failed
    return len(list(destination.glob('*.ini')))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--anchor', type=Path, required=True)
    parser.add_argument('--trials', type=int, required=True)
    parser.add_argument('--resume-from', type=Path)
    args = parser.parse_args()
    assert args.trials > 0
    anchor = read_config(args.anchor)
    args.root.mkdir(exist_ok=False)
    for name in ('config', 'logs', 'checkpoints'):
        (args.root / name).mkdir()
    for name in ('default.ini', 'osrs_riskfight.ini'):
        shutil.copyfile(args.repo / 'config' / name, args.root / 'config' / name)
    shutil.copyfile(args.anchor, args.root / 'config/anchor.ini')
    binary = args.root / 'riskfight'
    shutil.copyfile(args.binary, binary)
    binary.chmod(0o755)
    imported = stage_resume(args.resume_from, args.root / 'resume', binary) if args.resume_from else 0
    command = [str(binary), 'sweep']
    for section in ('base', 'vec', 'selfplay', 'env', 'policy', 'train', 'bot_eval'):
        command.extend(f'--{section}.{key}={value}' for key, value in anchor[section].items())
    command.extend([
        f'--base.checkpoint_dir={args.root}/checkpoints', f'--base.log_dir={args.root}/logs',
        '--base.checkpoint_interval=64', '--base.eval_episodes=0', '--base.seed=73',
        '--vec.hist_policy_hidden_size=0', '--vec.hist_policy_num_layers=0',
        '--selfplay.eval_bots=0,1,2,4,5,6,8', '--selfplay.eval_bot_games=256',
        f'--sweep.max_runs={args.trials}', '--sweep.max_suggestion_cost=300',
        f'--sweep.resume_dir={args.root / "resume" if args.resume_from else ""}',
        f'--sweep.objective_id={OBJECTIVE_ID}'])
    manifest = {
        'git_sha': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=args.repo, text=True).strip(),
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'anchor': str(args.anchor), 'command': command, 'eval_bots': [0, 1, 2, 4, 5, 6, 8],
        'objective_id': OBJECTIVE_ID, 'selection_seed': 73,
        'heldout_bots': [7], 'heldout_seeds': [1009, 2027, 3037],
        'max_suggestion_cost_seconds': 300, 'trials': args.trials,
        'observation_schema': 2, 'observation_size': 349, 'action_heads': 20,
        'training': 'Current and historical policy self-play',
        'score': 'Equal-weight unshaped net stake over seven selection bots',
        'historical_scores_imported': imported,
        'resume_from': str(args.resume_from) if args.resume_from else None,
        'failed_resume_cost': 'Last logged training uptime before failure',
    }
    (args.root / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    (args.root / 'status.json').write_text('{"status":"running"}')
    with (args.root / 'logs/sweep.log').open('w') as log, \
         (args.root / 'logs/wandb-sync.log').open('w') as sync_log, \
         (args.root / 'logs/checkpoint-retention.log').open('w') as retention_log:
        sync = subprocess.Popen([sys.executable, str(args.repo / 'scripts/wandb_sync.py'), str(args.root),
            '--entity=valtterivalo-clock-cloud', '--project=osrs-riskfight'], stdout=sync_log, stderr=subprocess.STDOUT)
        retention = subprocess.Popen([sys.executable, str(args.repo / 'scripts/prune_sweep_checkpoints.py'),
            str(args.root), '--apply', '--watch'], stdout=retention_log, stderr=subprocess.STDOUT)
        result = subprocess.run(command, cwd=args.repo, stdout=log, stderr=subprocess.STDOUT)
        (args.root / 'status.json').write_text(json.dumps({
            'status': 'complete' if result.returncode == 0 else 'failed', 'exit_code': result.returncode}))
        sync.wait()
        retention.wait()
        assert result.returncode == sync.returncode == retention.returncode == 0
    (args.root / 'heldout-status.json').write_text('{"status":"running"}')
    subprocess.run([sys.executable, str(args.repo / 'scripts/evaluate_riskfight_finalists.py'),
        str(args.root), f'--repo={args.repo}'], check=True)


if __name__ == '__main__':
    main()
