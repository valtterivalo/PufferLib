import configparser
import subprocess


def read_config(path):
    config = configparser.ConfigParser()
    config.optionxform = str
    assert config.read(str(path)) == [str(path)]
    return config


def ladder_args(binary, checkpoint, config, bots):
    args = [str(binary), 'ladder', f'--base.load_model_path={checkpoint}',
            f'--selfplay.eval_bots={bots}', '--env.self_play=0',
            '--env.damage_reward_coeff=0', '--env.teleport_penalty=0',
            '--env.chance_reward_coeff=0']
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
