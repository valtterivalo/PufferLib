import argparse
import configparser
import json
import re
import time
from pathlib import Path

import wandb


def config_value(value):
    if re.fullmatch(r'[+-]?\d+', value):
        return int(value)
    if re.fullmatch(r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?', value):
        return float(value)
    return value


def sync_trial(path, args):
    config = configparser.ConfigParser()
    config.optionxform = str
    config.read(path.with_suffix('.ini'))
    configuration = {
        section: {key: config_value(value) for key, value in config[section].items()}
        for section in config.sections() if section != 'metrics'
    }
    configuration['manifest'] = json.loads((args.root / 'manifest.json').read_text())
    run_id = f'{args.root.name}-{path.stem.rsplit("_", 1)[-1]}'
    with wandb.init(entity=args.entity, project=args.project, group=args.root.name,
                    id=run_id, name=run_id, resume='allow', config=configuration,
                    dir=str(args.root), save_code=False,
                    settings=wandb.Settings(disable_git=True, console='off')) as run:
        run.define_metric('agent_steps')
        run.define_metric('*', step_metric='agent_steps')
        previous_index = run.summary.get('_capture_index', -1)
        with path.open() as stream:
            index = 0
            while True:
                position = stream.tell()
                line = stream.readline()
                if not line.endswith('\n'):
                    stream.seek(position)
                    status = json.loads((args.root / 'status.json').read_text())
                    if status['status'] != 'running':
                        raise RuntimeError(f'Incomplete metric stream: {path}')
                    time.sleep(1)
                    continue
                row = json.loads(line)
                if '_finished' in row:
                    return
                if configuration['base']['env_name'] == 'osrs_riskfight':
                    original_bots = [f'selfplay/bot_{bot}_perf' for bot in (0, 1, 2)]
                    if all(key in row for key in original_bots):
                        original_sum = sum(row[key] for key in original_bots)
                        row['eval/three_bot_net_stake'] = original_sum / 3
                        if 'selfplay/bot_4_perf' in row:
                            row['eval/four_bot_net_stake'] = (original_sum + row['selfplay/bot_4_perf']) / 4
                if index > previous_index:
                    run.log({**row, '_capture_index': index})
                index += 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('--entity', required=True)
    parser.add_argument('--project', required=True)
    args = parser.parse_args()
    uploaded = set()
    while True:
        for path in sorted((args.root / 'logs').glob('*/*.jsonl')):
            if path not in uploaded:
                sync_trial(path, args)
                uploaded.add(path)
        status = json.loads((args.root / 'status.json').read_text())
        if status['status'] != 'running':
            return
        time.sleep(1)


if __name__ == '__main__':
    main()
