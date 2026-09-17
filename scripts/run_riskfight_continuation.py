import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('--repo', required=True, type=Path)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--rescore', required=True, type=Path)
    parser.add_argument('--additional-trials', type=int, default=24)
    args = parser.parse_args()
    prior = json.loads((args.rescore / 'manifest.json').read_text())
    assert json.loads((args.rescore / 'status.json').read_text())['status'] == 'complete'
    args.root.mkdir(exist_ok=False)
    for name in ('config', 'logs', 'checkpoints'):
        (args.root / name).mkdir()
    for name in ('default.ini', 'osrs_riskfight.ini'):
        shutil.copyfile(args.repo / 'config' / name, args.root / 'config' / name)
    binary = args.root / 'riskfight'
    shutil.copyfile(args.binary, binary)
    binary.chmod(0o755)
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == prior['binary_sha256']
    command = [str(binary), 'sweep',
        f'--base.checkpoint_dir={args.root}/checkpoints', f'--base.log_dir={args.root}/logs',
        '--base.eval_episodes=0', '--base.checkpoint_interval=64',
        '--selfplay.eval_bots=0,1,2,4',
        f'--sweep.max_runs={args.additional_trials}',
        f'--sweep.resume_dir={args.rescore}/resume',
        f"--sweep.objective_id={prior['objective_id']}"]
    manifest = {'git_sha': prior['git_sha'], 'binary_sha256': prior['binary_sha256'],
        'rescore': str(args.rescore), 'parent': prior['source'],
        'objective_id': prior['objective_id'], 'eval_bots': [0, 1, 2, 4],
        'score': 'Equal-weight mean unshaped net stake against trader, cautious, aggressive and tactician',
        'imported_trials': prior['trials'], 'additional_trials': args.additional_trials,
        'max_suggestion_cost_seconds': 300, 'command': command,
        'observation_schema': 3, 'action_heads': 20, 'action_mask_size': 461,
        'training': 'Unchanged current-policy and historical-policy self-play'}
    (args.root / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    (args.root / 'status.json').write_text(json.dumps({'status': 'running', 'stage': 'sweep'}))
    with (args.root / 'logs' / 'sweep.log').open('w') as log, \
         (args.root / 'logs' / 'wandb-sync.log').open('w') as sync_log, \
         (args.root / 'logs' / 'checkpoint-retention.log').open('w') as retention_log:
        python = '/puffertank/docker/.venv/bin/python'
        sync = subprocess.Popen([python, str(args.repo / 'scripts/wandb_sync.py'), str(args.root),
            '--entity=valtterivalo-clock-cloud', '--project=osrs-riskfight'],
            stdout=sync_log, stderr=subprocess.STDOUT)
        retention = subprocess.Popen([python, str(args.repo / 'scripts/prune_sweep_checkpoints.py'),
            str(args.root), '--apply', '--watch'], stdout=retention_log, stderr=subprocess.STDOUT)
        result = subprocess.run(command, cwd=args.repo, stdout=log, stderr=subprocess.STDOUT)
        (args.root / 'status.json').write_text(json.dumps({
            'status': 'complete' if result.returncode == 0 else 'failed',
            'exit_code': result.returncode}))
        sync.wait()
        retention.wait()
        assert result.returncode == 0
        assert sync.returncode == 0
        assert retention.returncode == 0


if __name__ == '__main__':
    main()
