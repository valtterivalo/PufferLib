import argparse
import configparser
import hashlib
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path


def write_status(root, status):
    temporary = root / 'status.tmp'
    temporary.write_text(json.dumps(status) + '\n')
    temporary.replace(root / 'status.json')


def interrupt(signum, frame):
    raise SystemExit(128 + signum)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=7200)
    parser.add_argument('--max-suggestion-cost', type=int, default=600)
    parser.add_argument('--entity', default='valtterivalo-clock-cloud')
    parser.add_argument('--project', default='osrs-nh-pvp')
    args = parser.parse_args()
    signal.signal(signal.SIGINT, interrupt)
    signal.signal(signal.SIGTERM, interrupt)
    assert sys.platform == 'linux'
    assert args.seconds > 0 and args.max_suggestion_cost > 0
    repo = args.repo.resolve()
    root = args.root.resolve()
    binary_source = args.binary.resolve()
    subprocess.run(['pwd'], cwd=repo, check=True)
    dirty = subprocess.check_output(
        ['git', 'status', '--porcelain', '--untracked-files=no'], cwd=repo, text=True)
    assert not dirty, dirty
    git_sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
    branch = subprocess.check_output(
        ['git', 'branch', '--show-current'], cwd=repo, text=True).strip()
    config = configparser.ConfigParser()
    config.optionxform = str
    with (repo / 'config/default.ini').open() as stream:
        config.read_file(stream)
    with (repo / 'config/osrs_pvp.ini').open() as stream:
        config.read_file(stream)
    assert config['selfplay']['enabled'] == '0'
    assert config['env']['self_play'] == '0'
    assert config['env']['use_c_opponent'] == '1'
    assert config['env']['shaping_enabled'] == '0'
    assert config['vec']['num_policies'] == '1'
    assert float(config['vec']['hist_policy_percent']) == 0
    eval_bots = [int(bot) for bot in config['selfplay']['eval_bots'].split(',')]
    assert eval_bots == [int(config['env']['opponent_type'])]
    assert config['sweep']['metric'] == 'selfplay/bot_ladder_perf'
    assert float(config['sweep.train.total_timesteps']['min']) == 3e7
    assert float(config['sweep.train.total_timesteps']['max']) == 1e11
    assert not any(section.startswith(('sweep.selfplay.', 'sweep.env.', 'sweep.vec.hist_'))
        for section in config.sections())
    import wandb

    viewer = wandb.Api().viewer
    assert viewer.username == 'valtterivalo', viewer
    root.mkdir()
    for name in ('config', 'logs', 'checkpoints', 'wandb'):
        (root / name).mkdir()
    for name in ('default.ini', 'osrs_pvp.ini'):
        (root / 'config' / name).write_bytes((repo / 'config' / name).read_bytes())
    binary = root / 'nh-pvp'
    binary.write_bytes(binary_source.read_bytes())
    binary.chmod(0o755)
    objective_id = f'nh-bot-{eval_bots[0]}-actual-wins-terminal-reward-v1'
    command = [str(binary), 'sweep',
        f'--base.checkpoint_dir={root}/checkpoints', f'--base.log_dir={root}/logs',
        f'--sweep.max_suggestion_cost={args.max_suggestion_cost}', '--sweep.resume_dir=',
        f'--sweep.objective_id={objective_id}']
    start = time.time()
    manifest = {
        'git_sha': git_sha, 'branch': branch, 'repo': str(repo),
        'binary_source': str(binary_source),
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'command': command, 'started_at_unix': start,
        'suggestion_deadline_unix': start + args.seconds,
        'budget_seconds': args.seconds, 'max_suggestion_cost_seconds': args.max_suggestion_cost,
        'max_runs': int(config['sweep']['max_runs']),
        'eval_bots': eval_bots,
        'eval_bot_games': int(config['selfplay']['eval_bot_games']),
        'objective_id': objective_id,
        'score': 'Actual win rate against the single scripted training opponent',
        'self_play': 0, 'historical_policies': 0, 'shaping_enabled': 0,
        'environment_seed_offset': int(os.environ.get('PUFFER_ENV_SEED_OFFSET', '0')),
        'fight_seeding': 'osrs_env_seed(env index,0) plus reset_count*0x9E3779B9 per episode',
        'wandb_entity': args.entity, 'wandb_project': args.project, 'wandb_group': root.name,
        'stop_policy': 'Freeze sweep suggestions at the deadline and finish active trials',
        'checkpoint_policy': 'Retain all checkpoints',
    }
    (root / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    status = {'status': 'running', 'phase': 'suggesting'}
    write_status(root, status)
    processes = []
    with (root / 'logs/sweep.log').open('w') as log, \
            (root / 'logs/wandb-sync.log').open('w') as sync_log:
        try:
            native = subprocess.Popen(command, cwd=repo, stdout=log, stderr=subprocess.STDOUT,
                start_new_session=True)
            processes.append(native)
            sync = subprocess.Popen([sys.executable, str(repo / 'scripts/wandb_sync.py'), str(root),
                f'--entity={args.entity}', f'--project={args.project}'], cwd=repo,
                stdout=sync_log, stderr=subprocess.STDOUT, start_new_session=True)
            processes.append(sync)
            status['native_pid'] = native.pid
            status['sync_pid'] = sync.pid
            write_status(root, status)
            deadline = time.monotonic() + args.seconds
            while native.poll() is None and time.monotonic() < deadline:
                assert sync.poll() is None, f'W&B sync failed: {sync.returncode}'
                time.sleep(0.5)
            if native.poll() is None:
                os.kill(native.pid, signal.SIGSTOP)
                status['phase'] = 'finishing_active_trials'
                write_status(root, status)
                children_path = Path(f'/proc/{native.pid}/task/{native.pid}/children')
                while True:
                    assert sync.poll() is None, f'W&B sync failed: {sync.returncode}'
                    children = children_path.read_text().split()
                    states = [Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[0]
                        for pid in children]
                    if all(state == 'Z' for state in states):
                        break
                    time.sleep(0.5)
                os.kill(native.pid, signal.SIGKILL)
                native.wait()
                status['status'] = 'budget_complete'
            else:
                native.wait()
                assert native.returncode == 0, f'Native sweep failed: {native.returncode}'
                status['status'] = 'complete'
            status['phase'] = 'uploading_metrics'
            status['native_exit_code'] = native.returncode
            status['finished_at_unix'] = time.time()
            write_status(root, status)
            sync.wait()
            assert sync.returncode == 0, f'W&B sync failed: {sync.returncode}'
            trials = []
            for path in sorted((root / 'logs/osrs_pvp').glob('sweep_*.ini')):
                trial = configparser.ConfigParser()
                trial.optionxform = str
                with path.open() as stream:
                    trial.read_file(stream)
                lines = path.with_suffix('.jsonl').read_text().splitlines()
                finished = len(lines) > 0 and lines[-1] == '{"_finished":1}'
                row = {'run_id': path.stem, 'status': 'complete' if finished else 'failed',
                    'config': str(path), 'checkpoints': str(root / 'checkpoints/osrs_pvp' / path.stem)}
                if finished:
                    row['score'] = float(trial['metrics']['selfplay/bot_ladder_perf'].split(',')[-1])
                    row['cost'] = float(trial['metrics']['uptime'].split(',')[-1])
                    row['steps'] = float(trial['metrics']['agent_steps'].split(',')[-1])
                trials.append(row)
            (root / 'summary.json').write_text(json.dumps({
                'status': status['status'], 'trials': trials,
                'wall_seconds': status['finished_at_unix'] - start}, indent=2) + '\n')
            status['phase'] = 'finished'
            write_status(root, status)
        finally:
            for process in processes:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
            if status['phase'] != 'finished':
                status['status'] = 'failed'
                write_status(root, status)


if __name__ == '__main__':
    main()
