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


def stage_resume(source, destination, binary, objective_id, compatible_binary_sha256=None):
    manifest = json.loads((source / 'manifest.json').read_text())
    source_status = json.loads((source / 'status.json').read_text())['status']
    assert source_status in ('complete', 'stopped')
    assert manifest['objective_id'] == objective_id
    assert manifest['binary_sha256'] == hashlib.sha256(binary.read_bytes()).hexdigest() or (
        compatible_binary_sha256 is not None and compatible_binary_sha256 == manifest['binary_sha256'])
    destination.mkdir()
    if (source / 'resume').exists():
        for path in (source / 'resume').glob('*.ini'):
            shutil.copyfile(path, destination / path.name)
    log = (source / 'logs/sweep.log').read_text()
    completed = {int(value) for value in re.findall(r'^sweep run=(\d+) score=', log, re.M)}
    failed = {int(value) for value in re.findall(r'^sweep worker run=(\d+) failed;', log, re.M)}
    assert not completed & failed
    imported = set()
    interrupted = []
    for path in sorted((source / 'logs/osrs_riskfight').glob('sweep_*.ini')):
        trial = int(path.stem.rsplit('_', 1)[1])
        if trial not in completed | failed:
            assert source_status == 'stopped', f'Unfinished trial: {path}'
            interrupted.append(str(path))
            continue
        config = read_config(path)
        if trial in completed:
            config['resume'] = {'objective_id': objective_id, 'status': 'success',
                'score': config['metrics']['selfplay/bot_ladder_perf'],
                'cost': config['metrics']['uptime']}
        else:
            last = None
            with path.with_suffix('.jsonl').open() as rows:
                for line in rows:
                    last = json.loads(line)
            assert last is not None and last['uptime'] > 0
            config['resume'] = {'objective_id': objective_id, 'status': 'failed',
                'cost': str(last['uptime'])}
        target = destination / path.name
        assert not target.exists(), f'Duplicate resume observation: {target}'
        with target.open('w') as output:
            config.write(output)
        imported.add(trial)
    (destination / "interrupted.json").write_text(json.dumps(interrupted, indent=2))
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
    parser.add_argument('--objective-id', default=OBJECTIVE_ID)
    parser.add_argument('--resume-compatible-binary-sha256')
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
    imported = stage_resume(args.resume_from, args.root / 'resume', binary, args.objective_id, args.resume_compatible_binary_sha256) if args.resume_from else 0
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
        f'--sweep.objective_id={args.objective_id}'])
    manifest = {
        'git_sha': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=args.repo, text=True).strip(),
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'anchor': str(args.anchor), 'command': command, 'eval_bots': [0, 1, 2, 4, 5, 6, 8],
        'objective_id': args.objective_id, 'selection_seed': 73,
        'heldout_bots': [7], 'heldout_seeds': [1009, 2027, 3037],
        'max_suggestion_cost_seconds': 300, 'trials': args.trials,
        'observation_schema': 5, 'observation_size': 353, 'action_heads': 20,
        'training': 'Current and historical policy self-play with configurable scripted mixing and midfight starts',
        'score': 'Equal-weight unshaped net stake over seven selection bots',
        'historical_scores_imported': imported,
        'resume_from': str(args.resume_from) if args.resume_from else None,
        'resume_compatible_binary_sha256': args.resume_compatible_binary_sha256,
        'resume_cost_policy': 'Preserve measured historical runtimes without rescaling. New trials measure the new binary.',
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
