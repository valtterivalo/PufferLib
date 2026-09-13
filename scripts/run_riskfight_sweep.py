import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

from rescore_riskfight_sweep import read_config


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--anchor', type=Path, required=True)
    args = parser.parse_args()
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
    command = [str(binary), 'sweep']
    for section in ('base', 'vec', 'selfplay', 'env', 'policy', 'train', 'bot_eval'):
        command.extend(f'--{section}.{key}={value}' for key, value in anchor[section].items())
    command.extend([
        f'--base.checkpoint_dir={args.root}/checkpoints', f'--base.log_dir={args.root}/logs',
        '--base.checkpoint_interval=64', '--base.eval_episodes=0', '--base.seed=73',
        '--vec.hist_policy_hidden_size=0', '--vec.hist_policy_num_layers=0',
        '--selfplay.eval_bots=0,1,2,4,5,6', '--selfplay.eval_bot_games=256',
        '--sweep.max_runs=24', '--sweep.max_suggestion_cost=300', '--sweep.resume_dir=',
        '--sweep.objective_id=riskfight-six-bot-consumption-v1'])
    manifest = {
        'git_sha': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=args.repo, text=True).strip(),
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'anchor': str(args.anchor), 'command': command, 'eval_bots': [0, 1, 2, 4, 5, 6],
        'objective_id': 'riskfight-six-bot-consumption-v1', 'selection_seed': 73,
        'heldout_bots': [7], 'heldout_seeds': [1009, 2027, 3037],
        'max_suggestion_cost_seconds': 300, 'trials': 24,
        'observation_schema': 2, 'observation_size': 349, 'action_heads': 20,
        'training': 'Current and historical policy self-play',
        'score': 'Equal-weight unshaped net stake over six selection bots',
        'historical_scores_imported': 0,
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
