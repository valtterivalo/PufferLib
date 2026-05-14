## puffer [train | eval | sweep] [env_name] [optional args] -- See https://puffer.ai for full details
# This is the same as python -m pufferlib.pufferl [train | eval | sweep] [env_name] [optional args]

import warnings
warnings.filterwarnings('error', category=RuntimeWarning)

import os
import sys
import glob
import json
import ast
import time
import argparse
import configparser
from contextlib import contextmanager
from collections import defaultdict
import multiprocessing as mp
import queue
from copy import deepcopy

import numpy as np

import torch
import pufferlib
try:
    from pufferlib import _C
except ImportError:
    raise ImportError('Failed to import PufferLib C++ backend. If you have non-default PyTorch, try installing with --no-build-isolation')

import rich
import rich.traceback
from rich.table import Table
from rich_argparse import RichHelpFormatter
rich.traceback.install(show_locals=False)

import signal # Aggressively exit on ctrl+c
signal.signal(signal.SIGINT, lambda sig, frame: os._exit(0))

def unroll_nested_dict(d):
    if not isinstance(d, dict):
        return d

    for k, v in d.items():
        if isinstance(v, dict):
            for k2, v2 in unroll_nested_dict(v):
                yield f"{k}/{k2}", v2
        else:
            yield k, v

def abbreviate(num, b2, c2):
    prefixes = ['', 'K', 'M', 'B', 'T']
    for i, prefix in enumerate(prefixes):
        if num < 1e3: break
        num /= 1e3

    return f'{b2}{num:.1f}{c2}{prefix}'

def duration(seconds, b2, c2):
    if seconds < 0: return f"{b2}0{c2}s"
    if seconds < 1: return f"{b2}{seconds*1000:.0f}{c2}ms"
    seconds = int(seconds)
    d = f'{b2}{seconds // 86400}{c2}d '
    h = f'{b2}{(seconds // 3600) % 24}{c2}h '
    m = f'{b2}{(seconds // 60) % 60}{c2}m '
    s = f'{b2}{seconds % 60}{c2}s'
    return d + h + m + s

def fmt_perf(name, color, delta_ref, elapsed, b2, c2):
    percent = 0 if delta_ref == 0 else int(100*elapsed/delta_ref - 1e-5)
    return f'{color}{name}', duration(elapsed, b2, c2), f'{b2}{percent:2d}{c2}%'

def print_dashboard(args, model_size, flat_logs, clear=False, idx=[0],
        c1='[cyan]', c2='[white]', b1='[bright_cyan]', b2='[bright_white]'):
    g = lambda k, d=0: flat_logs.get(k, d)
    console = rich.console.Console()
    dashboard = Table(box=rich.box.ROUNDED, expand=True,
        show_header=False, border_style='bright_cyan')
    table = Table(box=None, expand=True, show_header=False)
    dashboard.add_row(table)

    table.add_column(justify="left", width=30)
    table.add_column(justify="center", width=12)
    table.add_column(justify="center", width=18)
    table.add_column(justify="right", width=12)

    table.add_row(
        f'{b1}PufferLib {b2}4.0 {idx[0]*" "}:blowfish:',
        f'{c1}GPU: {b2}{g("util/gpu_percent"):.0f}{c2}%',
        f'{c1}VRAM: {b2}{g("util/vram_used_gb"):.1f}{c2}/{b2}{g("util/vram_total_gb"):.0f}{c2}G',
        f'{c1}RAM: {b2}{g("util/cpu_mem_gb"):.1f}{c2}G',
    )
    idx[0] = (idx[0] - 1) % 10

    s = Table(box=None, expand=True)
    remaining = f'{b2}A hair past a freckle{c2}'
    agent_steps = g('agent_steps')
    if g('SPS') != 0:
        remaining = duration((args['train']['total_timesteps']*args['train'].get('gpus', 1) - agent_steps)/g('SPS'), b2, c2)

    s.add_column(f"{c1}Summary", justify='left', vertical='top', width=10)
    s.add_column(f"{c1}Value", justify='right', vertical='top', width=14)
    s.add_row(f'{c2}Env', f'{b2}{args["env_name"]}')
    s.add_row(f'{c2}Params', abbreviate(model_size, b2, c2))
    s.add_row(f'{c2}Steps', abbreviate(agent_steps, b2, c2))
    s.add_row(f'{c2}SPS', abbreviate(g('SPS'), b2, c2))
    s.add_row(f'{c2}Epoch', f'{b2}{g("epoch")}')
    s.add_row(f'{c2}Uptime', duration(g('uptime'), b2, c2))
    s.add_row(f'{c2}Remaining', remaining)

    rollout = g('perf/rollout')
    train = g('perf/train')
    delta = rollout + train
    p = Table(box=None, expand=True, show_header=False)
    p.add_column(f"{c1}Performance", justify="left", width=10)
    p.add_column(f"{c1}Time", justify="right", width=8)
    p.add_column(f"{c1}%", justify="right", width=4)
    p.add_row(*fmt_perf('Evaluate', b1, delta, rollout, b2, c2))
    p.add_row(*fmt_perf('  GPU', b2, delta, g('perf/eval_gpu'), b2, c2))
    p.add_row(*fmt_perf('  Env', b2, delta, g('perf/eval_env'), b2, c2))
    p.add_row(*fmt_perf('Train', b1, delta, train, b2, c2))
    p.add_row(*fmt_perf('  Misc', b2, delta, g('perf/train_misc'), b2, c2))
    p.add_row(*fmt_perf('  Forward', b2, delta, g('perf/train_forward'), b2, c2))

    l = Table(box=None, expand=True)
    l.add_column(f'{c1}Losses', justify="left", width=16)
    l.add_column(f'{c1}Value', justify="right", width=8)
    for k, v in flat_logs.items():
        if k.startswith('loss/'):
            l.add_row(f'{b2}{k[5:]}', f'{b2}{v:.3f}')

    monitor = Table(box=None, expand=True, pad_edge=False)
    monitor.add_row(s, p, l)
    dashboard.add_row(monitor)

    table = Table(box=None, expand=True, pad_edge=False)
    dashboard.add_row(table)
    left = Table(box=None, expand=True)
    right = Table(box=None, expand=True)
    table.add_row(left, right)
    left.add_column(f"{c1}User Stats", justify="left", width=20)
    left.add_column(f"{c1}Value", justify="right", width=10)
    right.add_column(f"{c1}User Stats", justify="left", width=20)
    right.add_column(f"{c1}Value", justify="right", width=10)

    i = 0
    for k, v in flat_logs.items():
        if k.startswith('env/') and k != 'env/n':
            u = left if i % 2 == 0 else right
            u.add_row(f'{b2}{k[4:]}', f'{b2}{v:.3f}')
            i += 1
            if i == 30:
                break

    if clear:
        console.clear()

    with console.capture() as capture:
        console.print(dashboard)

    print('\033[0;0H' + capture.get())

def validate_config(args):
    minibatch_size = args['train']['minibatch_size']
    horizon = args['train']['horizon']
    total_agents = args['vec']['total_agents']
    assert (minibatch_size % horizon) == 0, \
        f'minibatch_size {minibatch_size} must be divisible by horizon {horizon}'
    assert minibatch_size <= horizon * total_agents, \
        f'minibatch_size {minibatch_size} > total_agents {total_agents} * horizon {horizon}'

def _resolve_backend(args):
    compiled_env = getattr(_C, 'env_name', None)
    static_env = getattr(_C, 'static_env_name', compiled_env)
    requested_env = args['env_name']
    if compiled_env is not None and compiled_env != requested_env:
        raise RuntimeError(f'build.sh was run for {compiled_env}, not {requested_env}')
    if static_env is not None and static_env != requested_env:
        raise RuntimeError(f'_C static env is {static_env}, not {requested_env}')
    if args.get('slowly'):
        from pufferlib.torch_pufferl import PuffeRL
        return PuffeRL
    return _C

@contextmanager
def _inferno_replay_env(args):
    if args.get('env_name') != 'osrs_inferno':
        yield
        return

    env_args = args.get('env', {})
    record_path = env_args.get('record_best_replay_path', '')
    play_path = env_args.get('play_replay_path', '')
    post_240_trace_dir = env_args.get('post_240_trace_dir', '')
    post_240_trace_max_episodes = env_args.get('post_240_trace_max_episodes', 40)
    post_240_trace_tick_cap = env_args.get('post_240_trace_tick_cap', 512)
    if record_path and play_path:
        raise ValueError('record_best_replay_path and play_replay_path cannot both be set')

    old_record = os.environ.get('RECORD_REPLAY')
    old_play = os.environ.get('PLAY_REPLAY')
    old_post_240_trace_dir = os.environ.get('POST240_TRACE_DIR')
    old_post_240_trace_max_episodes = os.environ.get('POST240_TRACE_MAX_EPISODES')
    old_post_240_trace_tick_cap = os.environ.get('POST240_TRACE_TICK_CAP')
    try:
        if record_path:
            os.environ['RECORD_REPLAY'] = record_path
        else:
            os.environ.pop('RECORD_REPLAY', None)
        if play_path:
            os.environ['PLAY_REPLAY'] = play_path
        else:
            os.environ.pop('PLAY_REPLAY', None)
        if post_240_trace_dir:
            os.environ['POST240_TRACE_DIR'] = post_240_trace_dir
            os.environ['POST240_TRACE_MAX_EPISODES'] = str(int(post_240_trace_max_episodes))
            os.environ['POST240_TRACE_TICK_CAP'] = str(int(post_240_trace_tick_cap))
        else:
            os.environ.pop('POST240_TRACE_DIR', None)
            os.environ.pop('POST240_TRACE_MAX_EPISODES', None)
            os.environ.pop('POST240_TRACE_TICK_CAP', None)
        yield
    finally:
        if old_record is None:
            os.environ.pop('RECORD_REPLAY', None)
        else:
            os.environ['RECORD_REPLAY'] = old_record
        if old_play is None:
            os.environ.pop('PLAY_REPLAY', None)
        else:
            os.environ['PLAY_REPLAY'] = old_play
        if old_post_240_trace_dir is None:
            os.environ.pop('POST240_TRACE_DIR', None)
        else:
            os.environ['POST240_TRACE_DIR'] = old_post_240_trace_dir
        if old_post_240_trace_max_episodes is None:
            os.environ.pop('POST240_TRACE_MAX_EPISODES', None)
        else:
            os.environ['POST240_TRACE_MAX_EPISODES'] = old_post_240_trace_max_episodes
        if old_post_240_trace_tick_cap is None:
            os.environ.pop('POST240_TRACE_TICK_CAP', None)
        else:
            os.environ['POST240_TRACE_TICK_CAP'] = old_post_240_trace_tick_cap

def _sweep_metric_key(args):
    return f'env/{args["sweep"]["metric"]}'

OSRS_INFERNO_WANDB_ENV_KEYS = frozenset({
    'env/behind_shield_pct',
    'env/blood_healed',
    'env/brews_remaining',
    'env/brews_remaining_after_240_death_normal',
    'env/brews_remaining_after_all_healers_dead_death_normal',
    'env/brews_remaining_normal_died',
    'env/brews_used',
    'env/damage_after_150_normal',
    'env/damage_after_240_normal',
    'env/damage_after_300_normal',
    'env/damage_dealt',
    'env/damage_received',
    'env/death_tick_normal',
    'env/deaths_to_jad',
    'env/episode_length',
    'env/episode_return',
    'env/episode_return_normal',
    'env/frac_all_zuk_healers_dead_normal',
    'env/frac_deaths_killed_by_bat_normal',
    'env/frac_deaths_killed_by_blob_normal',
    'env/frac_deaths_killed_by_heal_zuk_normal',
    'env/frac_deaths_killed_by_jad_normal',
    'env/frac_deaths_killed_by_mager_normal',
    'env/frac_deaths_killed_by_meleer_normal',
    'env/frac_deaths_killed_by_ranger_normal',
    'env/frac_deaths_killed_by_zuk_normal',
    'env/frac_died_after_240_all_healers_dead_normal',
    'env/frac_died_after_240_never_tagged_healer_normal',
    'env/frac_died_after_240_normal',
    'env/frac_died_after_240_some_healers_killed_normal',
    'env/frac_died_after_240_some_healers_tagged_normal',
    'env/frac_died_with_jad_alive_normal',
    'env/frac_died_with_set_alive_normal',
    'env/frac_died_with_zuk_healer_alive_normal',
    'env/frac_healer_spawned_normal',
    'env/frac_min_hp_le_150_normal',
    'env/frac_min_hp_le_240_normal',
    'env/frac_min_hp_le_300_normal',
    'env/frac_normal',
    'env/frac_reengaged_zuk_after_healers_normal',
    'env/frac_zuk_healers_attackable_ge_1_normal',
    'env/frac_zuk_healers_attacked_ge_1_normal',
    'env/frac_zuk_healers_killed_ge_4_normal',
    'env/frac_zuk_healers_tagged_ge_1_normal',
    'env/frac_zuk_healers_tagged_ge_4_normal',
    'env/frac_zuk_healers_targeted_ge_1_normal',
    'env/healer_resolve_normal',
    'env/hp_restored',
    'env/hp_restored_after_240_normal',
    'env/idle_ticks',
    'env/min_zuk_hp_normal',
    'env/min_zuk_hp_seen',
    'env/offshield_ticks_after_240_normal',
    'env/offshield_ticks_after_all_healers_dead_normal',
    'env/phase_reached_normal',
    'env/post_healer_objective_normal',
    'env/post_healer_survival_ticks_normal',
    'env/post_healer_zuk_damage_normal',
    'env/prayer_at_death_after_240_normal',
    'env/prayer_at_death_normal_died',
    'env/prayer_correct_rate',
    'env/restores_remaining',
    'env/restores_remaining_after_240_death_normal',
    'env/restores_remaining_after_all_healers_dead_death_normal',
    'env/restores_remaining_normal_died',
    'env/score',
    'env/score_normal',
    'env/spark_damage_after_240_normal',
    'env/ticks_240_to_all_healers_dead_normal',
    'env/ticks_240_to_all_healers_tagged_normal',
    'env/ticks_240_to_first_healer_attack_normal',
    'env/ticks_240_to_first_healer_tag_normal',
    'env/ticks_240_to_first_healer_target_normal',
    'env/ticks_after_150_normal',
    'env/ticks_after_240_normal',
    'env/ticks_after_300_normal',
    'env/ticks_all_healers_dead_to_first_zuk_hit_normal',
    'env/unavoidable_off_prayer_rate',
    'env/wave',
    'env/wins',
    'env/wins_normal',
    'env/zuk_healer_damage',
    'env/zuk_healer_target_attackable_ticks_normal',
    'env/zuk_healer_target_cannot_attack_ticks_normal',
    'env/zuk_healer_target_cooldown_ticks_normal',
    'env/zuk_healer_target_out_of_range_ticks_normal',
    'env/zuk_hp_max_after_healer_spawn_normal',
    'env/zuk_hp_remaining',
    'env/zuk_hp_when_all_healers_dead_normal',
    'env/zuk_objective_normal',
})

def _filter_wandb_payload(payload, env_name):
    if env_name != 'osrs_inferno':
        return payload

    filtered = {}
    for key, value in payload.items():
        if key.startswith('env/') and key not in OSRS_INFERNO_WANDB_ENV_KEYS:
            continue
        if key.startswith('eval/'):
            env_key = f'env/{key[5:]}'
            if env_key not in OSRS_INFERNO_WANDB_ENV_KEYS:
                continue
        filtered[key] = value
    return filtered

def _wandb_train_payload(fresh_logs, agent_steps, env_name=None):
    payload = dict(fresh_logs)
    payload.setdefault('agent_steps', agent_steps)
    return _filter_wandb_payload(payload, env_name)

def _wandb_eval_payload(flat_logs, agent_steps, env_name=None):
    payload = {'agent_steps': agent_steps}
    for key, value in flat_logs.items():
        if key.startswith('env/'):
            payload[f'eval/{key[4:]}'] = value
    return _filter_wandb_payload(payload, env_name)

def _resolve_checkpoint_load_path(args, load_path=None, allow_auto_latest=False,
        require_checkpoint=False):
    '''Resolve a checkpoint load request for train and eval entrypoints.'''
    load_path = load_path or args.get('load_model_path')
    checkpoint_dir = args['checkpoint_dir']
    pattern = os.path.join(checkpoint_dir, args['env_name'], '**', '*.bin')

    if load_path is None:
        if not allow_auto_latest:
            return None

        candidates = glob.glob(pattern, recursive=True)
        if candidates:
            return max(candidates, key=os.path.getctime)

        message = (f'No checkpoint found in {checkpoint_dir}/{args["env_name"]}/. '
                   f'Pass --load-model-path, --load-model-path latest, or train first '
                   f'with `puffer train {args["env_name"]}`.')
        if require_checkpoint:
            raise FileNotFoundError(message)

        print(f'WARNING: no checkpoint found in {checkpoint_dir}/{args["env_name"]}/ '
              f'- running with random weights. '
              f'Train first with `puffer train {args["env_name"]}`.', flush=True)
        return None

    if load_path == 'latest':
        candidates = glob.glob(pattern, recursive=True)
        if not candidates:
            raise FileNotFoundError(f'No .bin checkpoints found in {checkpoint_dir}/{args["env_name"]}/')
        return max(candidates, key=os.path.getctime)

    return load_path

def _phase2_init_if_configured(backend, pufferl, args):
    '''Initialize Go-Explore restored-start curriculum when configured.'''
    env_args = args.get('env', {})
    phase2_dir = env_args.get('phase2_demo_dir', '')
    if not phase2_dir:
        return 0

    n = backend.phase2_init(
        pufferl,
        demo_dir=phase2_dir,
        num_atns=env_args.get('phase2_num_atns', 9),
        snapshot_stride=env_args.get('phase2_snapshot_stride', 4),
        max_demos=env_args.get('phase2_max_demos', 64),
        seed=env_args.get('phase2_seed', 42),
        normal_start_frac=env_args.get('phase2_normal_start_frac', 0.25),
        randomize_rng_frac=env_args.get('phase2_randomize_rng_frac', 0.25),
        bc_coef=env_args.get('phase2_bc_coef', 0.0),
        bc_demos_per_minibatch=env_args.get('phase2_bc_demos_per_minibatch', 0),
        promote_rate=env_args.get('phase2_promote_rate', 0.30),
        demote_rate=env_args.get('phase2_demote_rate', 0.10),
        backstep_ticks=env_args.get('phase2_backstep_ticks', 4),
        success_q_delta=env_args.get('phase2_success_q_delta', 0.005),
    )
    print(f'phase2: loaded {n} demos from {phase2_dir}', flush=True)
    return n

def _restore_exact_match_config(args, sweep_obj):
    flat_args = dict(unroll_nested_dict(args))
    swept_keys = set(sweep_obj.hyperparameters.flat_spaces)
    exact_match = {}
    for key, value in flat_args.items():
        if key.startswith('sweep/'):
            continue
        if key in swept_keys or key in (
            'train/total_timesteps',
            'env/record_best_replay_path',
            'env/play_replay_path',
        ):
            continue

        if key in ('env_name', 'policy_name', 'rnn_name', 'score_metric'):
            exact_match[key] = value
            continue

        if key.startswith(('env/', 'vec/', 'policy/', 'train/', 'torch/')):
            exact_match[key] = value

    return exact_match

def _set_flat_config_key(args, key, value):
    parts = key.split('/')
    target = args
    for part in parts[:-1]:
        target = target.setdefault(part, {})
    target[parts[-1]] = value

def _is_sweep_observation_compatible(current_args, observation_args, sweep_obj):
    current_exact = _restore_exact_match_config(current_args, sweep_obj)
    observation_exact = _restore_exact_match_config(observation_args, sweep_obj)
    if current_exact != observation_exact:
        return False

    current_flat_args = dict(unroll_nested_dict(current_args))
    flat_args = dict(unroll_nested_dict(observation_args))
    for key, space in sweep_obj.hyperparameters.flat_spaces.items():
        if key not in flat_args:
            if key not in current_flat_args:
                return False
            flat_args[key] = current_flat_args[key]
            _set_flat_config_key(observation_args, key, current_flat_args[key])

        value = flat_args[key]
        if value < space.min or value > space.max:
            return False

    return True

def _restore_sweep_observations(env_name, args, sweep_obj):
    log_dir = os.path.join(args['log_dir'], env_name)
    if not os.path.isdir(log_dir):
        return 0, 0

    target_key = _sweep_metric_key(args)
    restored_runs = 0
    restored_points = 0
    pattern = os.path.join(log_dir, '*.json')
    for path in sorted(glob.glob(pattern)):
        try:
            with open(path) as f:
                logged_args = json.load(f)
        except (OSError, ValueError):
            continue

        if logged_args.get('env_name') != env_name:
            continue

        metrics = logged_args.get('metrics', {})
        scores = metrics.get(target_key)
        costs = metrics.get('uptime')
        timesteps = metrics.get('agent_steps')
        if not scores or not costs or not timesteps:
            continue

        restored_from_run = 0
        for score, cost, timestep in zip(scores, costs, timesteps):
            observation_args = deepcopy(logged_args)
            observation_args['train']['total_timesteps'] = timestep
            if not _is_sweep_observation_compatible(args, observation_args, sweep_obj):
                continue

            sweep_obj.observe(observation_args, score, cost, is_failure=False)
            restored_points += 1
            restored_from_run += 1

        if restored_from_run > 0:
            restored_runs += 1

    return restored_runs, restored_points

def _train_worker(args):
    backend = _resolve_backend(args)
    with _inferno_replay_env(args):
        pufferl = backend.create_pufferl(args)
        load_state_path = args.get('load_training_state_path')
        load_path = _resolve_checkpoint_load_path(args)
        if load_state_path and load_path:
            raise ValueError('load_training_state_path and load_model_path cannot both be set')
        if load_state_path:
            backend.load_training_state(pufferl, load_state_path)
            print(f'Loaded training state from {load_state_path}', flush=True)
        elif load_path is not None:
            backend.load_weights(pufferl, load_path)
            print(f'Loaded weights from {load_path}', flush=True)
        anchor_path = args.get('anchor_model_path')
        anchor_coef = args.get('anchor_coef', 0.0)
        parent_kl_coef = args['train'].get('parent_kl_coef', 0.0)
        parent_kl_log = args['train'].get('parent_kl_log', 0)
        if (parent_kl_coef > 0.0 or parent_kl_log) and not anchor_path:
            raise ValueError('parent KL requires anchor_model_path')
        if anchor_path:
            backend.load_anchor_weights(pufferl, anchor_path, anchor_coef)
            print(f'Loaded anchor weights from {anchor_path}', flush=True)
        args.pop('nccl_id', None)
        while pufferl.global_step < args['train']['total_timesteps']:
            backend.rollouts(pufferl)
            backend.train(pufferl)

        backend.close(pufferl)

def _train(env_name, args, sweep_obj=None, result_queue=None, verbose=False):
    '''Single-GPU training worker. Wraps the body so a crash still notifies the
    sweep main; without this, a python exception or C-level segfault that aborts
    before the normal queue.put leaves the sweep blocked on Queue.get forever.'''
    try:
        _train_body(env_name, args, sweep_obj=sweep_obj, result_queue=result_queue, verbose=verbose)
    except BaseException:
        if result_queue is not None:
            try:
                result_queue.put((args['gpu_id'], [], [], []))
            except Exception:
                pass
        raise


def _train_body(env_name, args, sweep_obj=None, result_queue=None, verbose=False):
    backend = _resolve_backend(args)
    rank = args['rank']
    run_id = str(int(1000*time.time()))
    if args['wandb']:
        import wandb
        run_id = wandb.util.generate_id()
        wandb.init(id=run_id, config=args,
            project=args['wandb_project'], group=args['wandb_group'],
            tags=[args['tag']] if args['tag'] is not None else [],
            settings=wandb.Settings(console="off"),
        )

    target_key = _sweep_metric_key(args)
    total_timesteps = args['train']['total_timesteps']
    all_logs = []

    checkpoint_dir = os.path.join(args['checkpoint_dir'], args['env_name'], run_id)
    os.makedirs(checkpoint_dir, exist_ok=True)

    log_dir = os.path.join(args['log_dir'], args['env_name'])
    os.makedirs(log_dir, exist_ok=True)

    # Write config-only stub at trial start so hung/crashed trials leave a
    # post-mortem trace. Overwritten with full {config, metrics} at trial end.
    # default=str handles bytes (nccl_id is b'' before being popped below) and
    # any numpy scalars that protein may have leaked into args.
    log_path = os.path.join(log_dir, run_id + '.json')
    with open(log_path, 'w') as f:
        json.dump({**args, 'metrics': {}, 'status': 'pending'}, f, default=str)

    with _inferno_replay_env(args):
        try:
            pufferl = backend.create_pufferl(args)
        except (RuntimeError, ValueError) as e:
            print(f'WARNING: {e}, skipping')
            if result_queue is not None:
                result_queue.put((args['gpu_id'], [], [], []))
            return

        args.pop('nccl_id', None)
        model_size = pufferl.num_params()

        load_state_path = args.get('load_training_state_path')
        load_path = _resolve_checkpoint_load_path(args)
        if load_state_path and load_path:
            raise ValueError('load_training_state_path and load_model_path cannot both be set')
        if load_state_path:
            backend.load_training_state(pufferl, load_state_path)
            print(f'Loaded training state from {load_state_path}', flush=True)
        elif load_path is not None:
            backend.load_weights(pufferl, load_path)
            print(f'Loaded weights from {load_path}', flush=True)
        anchor_path = args.get('anchor_model_path')
        anchor_coef = args.get('anchor_coef', 0.0)
        parent_kl_coef = args['train'].get('parent_kl_coef', 0.0)
        parent_kl_log = args['train'].get('parent_kl_log', 0)
        if (parent_kl_coef > 0.0 or parent_kl_log) and not anchor_path:
            raise ValueError('parent KL requires anchor_model_path')
        if anchor_path:
            backend.load_anchor_weights(pufferl, anchor_path, anchor_coef)
            print(f'Loaded anchor weights from {anchor_path}', flush=True)

        _phase2_init_if_configured(backend, pufferl, args)

        if verbose:
            flat_logs = dict(unroll_nested_dict(backend.log(pufferl)))
            print_dashboard(args, model_size, flat_logs, clear=True)

        model_path = ''
        flat_logs = {}
        train_epochs = int(total_timesteps // (args['vec']['total_agents'] * args['train']['horizon']))
        eval_epochs = train_epochs // 2
        for epoch in range(train_epochs + eval_epochs):
            backend.rollouts(pufferl)

            if epoch < train_epochs:
                backend.train(pufferl)

            # checkpoint_interval <= 0 disables periodic saves (still writes final epoch)
            interval = args['checkpoint_interval']
            should_save = (interval > 0 and epoch % interval == 0) or epoch == train_epochs - 1
            if should_save and sweep_obj is None:
                model_path = os.path.join(checkpoint_dir, f'{pufferl.global_step:016d}.bin')
                backend.save_weights(pufferl, model_path)
                if args['save_training_state']:
                    state_path = os.path.join(checkpoint_dir,
                        f'{pufferl.global_step:016d}.state')
                    backend.save_training_state(pufferl, state_path)

            # Rate limit, but always log for eval to maintain determinism
            if time.time() < pufferl.last_log_time + 0.6 and epoch < train_epochs - 1:
                continue

            logs = backend.eval_log(pufferl) if epoch >= train_epochs else backend.log(pufferl)
            fresh_logs = dict(unroll_nested_dict(logs))
            flat_logs = {**flat_logs, **fresh_logs}

            if verbose:
                print_dashboard(args, model_size, flat_logs)

            if args['wandb']:
                if epoch < train_epochs:
                    wandb.log(
                        _wandb_train_payload(
                            fresh_logs, pufferl.global_step, args['env_name']),
                        step=pufferl.global_step,
                    )

            if target_key not in flat_logs:
                continue

            if epoch < train_epochs:
                all_logs.append(flat_logs)

                if (sweep_obj is not None
                        and pufferl.global_step > min(0.20*total_timesteps, 100_000_000) and
                        sweep_obj.early_stop(logs, target_key)):
                    break
            elif flat_logs['env/n'] > args['eval_episodes']:
                if args['wandb']:
                    wandb.log(
                        _wandb_eval_payload(
                            flat_logs, pufferl.global_step, args['env_name']),
                        step=pufferl.global_step,
                    )
                break


        print_dashboard(args, model_size, flat_logs)
        backend.close(pufferl)

    if target_key not in flat_logs:
        if result_queue is not None:
            result_queue.put((args['gpu_id'], None, None, None))
        return

    # This version has the training perf logs and eval env logs
    all_logs.append(flat_logs)

    # Downsample results
    n = args['sweep']['downsample']
    metrics = {}
    last_metric_values = {}
    metric_bin_idx = 0
    logged_timesteps = all_logs[-1]['agent_steps']
    next_bin = logged_timesteps / (n - 1) if n > 1 else np.inf
    for log in all_logs:
        for k, v in log.items():
            if k not in metrics:
                metrics[k] = [[] for _ in range(metric_bin_idx + 1)]
            metrics[k][metric_bin_idx].append(v)
            last_metric_values[k] = v

        if log['agent_steps'] < next_bin:
            continue

        next_bin += logged_timesteps / (n - 1)
        for k in metrics:
            values = metrics[k][metric_bin_idx]
            metrics[k][metric_bin_idx] = (
                np.mean(values) if values else last_metric_values[k])
            metrics[k].append([])
        metric_bin_idx += 1

    for k in metrics:
        metrics[k][-1] = last_metric_values[k]

    # Save own log: config + downsampled results (overwrites pending stub)
    with open(log_path, 'w') as f:
        json.dump({**args, 'metrics': metrics, 'status': 'completed'}, f, default=str)

    if args['wandb']:
        if sweep_obj is None and model_path: # Don't spam uploads during sweeps
            artifact = wandb.Artifact(run_id, type='model')
            artifact.add_file(model_path)
            wandb.run.log_artifact(artifact)

        wandb.run.finish()

    if result_queue is not None:
        result_queue.put((args['gpu_id'], metrics[target_key], metrics['uptime'], metrics['agent_steps']))

def train(env_name, args=None, gpus=None, **kwargs):
    args = args or load_config(env_name)
    validate_config(args)

    subprocess = gpus is not None
    gpus = list(gpus or range(args['train']['gpus']))
    args['train']['total_timesteps'] //= len(gpus)
    args['world_size'] = len(gpus)
    args['nccl_id'] = _C.get_nccl_id() if len(gpus) > 1 and hasattr(_C, 'get_nccl_id') else b''

    if not subprocess:
        gpus = gpus[-1:] + gpus[:-1]  # Main process gets rank 0

    ctx = mp.get_context('spawn')
    for rank, gpu_id in reversed(list(enumerate(gpus))):
        worker_args = deepcopy(args)
        worker_args['rank'] = rank
        worker_args['gpu_id'] = gpu_id
        if rank == 0 and not subprocess:
            _train(env_name, worker_args, verbose=True)
        else:
            ctx.Process(target=_train, args=(env_name, worker_args),
                kwargs=kwargs).start()

def sweep(env_name, args=None, pareto=False):
    '''Train entry point. Handles single-GPU, multi-GPU DDP, and sweeps.'''
    args = args or load_config(env_name)
    exp_gpus = args['train']['gpus']
    sweep_gpus = args['sweep']['gpus'] or len(os.listdir('/proc/driver/nvidia/gpus'))
    concurrent_experiments = sweep_gpus // exp_gpus
    if concurrent_experiments < 1:
        raise ValueError(f'sweep.gpus={sweep_gpus} must be >= train.gpus={exp_gpus}')

    args['vec']['num_threads'] //= concurrent_experiments
    args['no_model_upload'] = True

    sweep_config = args['sweep']
    method = sweep_config.pop('method')
    import pufferlib.sweep
    try:
        sweep_cls = getattr(pufferlib.sweep, method)
    except:
        raise ValueError(f'Invalid sweep method {method}. See pufferlib.sweep')

    sweep_obj = sweep_cls(sweep_config)
    restored_runs, restored_points = _restore_sweep_observations(env_name, args, sweep_obj)
    if restored_runs:
        print(f'Restored {restored_points} observations from {restored_runs} prior sweep runs')

    max_runs = args['sweep']['max_runs']
    has_run_cap = max_runs > 0
    ts_default = args['train']['total_timesteps']
    ts_config = sweep_config.get('train', {}).get('total_timesteps', {'min': ts_default, 'max': ts_default})

    all_timesteps = np.geomspace(ts_config['min'], ts_config['max'], sweep_gpus)
    result_queue = mp.get_context('spawn').Queue()

    sweep_worker_timeout = float(os.environ.get('PUFFER_SWEEP_WORKER_TIMEOUT', '300'))

    active = {}
    completed = restored_runs
    launched = restored_runs
    while True:
        should_collect = active and (
            len(active) >= concurrent_experiments
            or (has_run_cap and launched >= max_runs)
        )
        if should_collect:
            try:
                gpu_id, scores, costs, timesteps = result_queue.get(timeout=sweep_worker_timeout)
            except queue.Empty:
                stuck = next(iter(active))
                print(f'WARNING: sweep worker gpu_id={stuck} silent for >{sweep_worker_timeout:.0f}s, treating as failure')
                done_args = active.pop(stuck)
                sweep_obj.observe(done_args, 0, 0, is_failure=True)
                continue
            if gpu_id not in active:
                continue
            done_args = active.pop(gpu_id)

            if not scores:
                sweep_obj.observe(done_args, 0, 0, is_failure=True)
            else:
                completed += 1

            for s, c, t in zip(scores, costs, timesteps):
                done_args['train']['total_timesteps'] = t
                sweep_obj.observe(done_args, s, c, is_failure=False)

        if has_run_cap and launched >= max_runs:
            if not active:
                break
            continue

        gpu_id = next(i for i in range(sweep_gpus) if i not in active)
        timestep_total = all_timesteps[gpu_id] if pareto else None
        if launched > 0: # Only the first overall experiment uses defaults
            sweep_obj.suggest(args, fixed_total_timesteps=timestep_total)

        try:
            validate_config(args)
        except (AssertionError, ValueError) as e:
            print(f'WARNING: {e}, skipping')
            sweep_obj.observe(args, 0, 0, is_failure=True)
            continue

        exp_args = deepcopy(args)
        active[gpu_id] = exp_args
        launched += 1
        early_stopper = sweep_obj.make_early_stopper()
        train(env_name, exp_args, range(gpu_id, gpu_id + exp_gpus),
            sweep_obj=early_stopper, result_queue=result_queue)

def eval(env_name, args=None, load_path=None):
    '''Evaluate a trained policy. Supports both native and --slowly torch backends.'''
    args = args or load_config(env_name)
    args['reset_state'] = False
    args['train']['horizon'] = 1
    # Eval batches are total_agents*1, so cap minibatch to that to satisfy
    # the divisibility check. Training-time minibatch may be larger.
    eval_batch = args['vec']['total_agents']
    if args['train']['minibatch_size'] > eval_batch:
        args['train']['minibatch_size'] = eval_batch

    backend = _resolve_backend(args)
    with _inferno_replay_env(args):
        pufferl = backend.create_pufferl(args)

        load_path = _resolve_checkpoint_load_path(
            args, load_path=load_path, allow_auto_latest=True,
            require_checkpoint=True)

        if load_path is not None:
            backend.load_weights(pufferl, load_path)
            print(f'Loaded weights from {load_path}', flush=True)

        _phase2_init_if_configured(backend, pufferl, args)

        while True:
            backend.render(pufferl, 0)
            backend.rollouts(pufferl)

    backend.close(pufferl)

def load_config(env_name):
    parser = argparse.ArgumentParser(formatter_class=RichHelpFormatter, add_help=False)
    parser.add_argument('--load-model-path', type=str, default=None,
        help='Path to a pretrained checkpoint')
    parser.add_argument('--load-training-state-path', type=str, default=None,
        help='Path to a native training-state checkpoint')
    parser.add_argument('--save-training-state', action='store_true',
        help='Save native optimizer state beside model checkpoints')
    parser.add_argument('--anchor-model-path', type=str, default=None,
        help='Path to raw .bin weights used as a parent anchor')
    parser.add_argument('--anchor-coef', type=float, default=0.0,
        help='Per-epoch weight blend coefficient toward anchor_model_path')
    parser.add_argument('--load-id', type=str,
        default=None, help='Kickstart/eval from from a finished Wandbrun')
    parser.add_argument('--render-mode', type=str, default='auto',
        choices=['auto', 'human', 'ansi', 'rgb_array', 'raylib', 'None'])
    parser.add_argument('--wandb', action='store_true', help='Use wandb for logging')
    parser.add_argument('--wandb-project', type=str, default='puffer4')
    parser.add_argument('--wandb-group', type=str, default='debug')
    parser.add_argument('--tag', type=str, default=None, help='Tag for experiment')
    parser.add_argument('--slowly', action='store_true', help='Use PyTorch training backend')
    parser.add_argument('--save-frames', type=int, default=0)
    parser.add_argument('--gif-path', type=str, default='eval.gif')
    parser.add_argument('--fps', type=float, default=15)
    parser.description = f':blowfish: PufferLib [bright_cyan]{pufferlib.__version__}[/]' \
        ' demo options. Shows valid args for your env and policy'

    repo_dir = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
    puffer_config_dir = os.path.join(repo_dir, 'config/**/*.ini')
    puffer_default_config = os.path.join(repo_dir, 'config/default.ini')

    config_file_override = os.environ.get('PUFFER_CONFIG_FILE', '').strip()

    #CC: Remove the default. Just raise an error on "puffer train" etc with no env (think we already do)
    if env_name == 'default':
        p = configparser.ConfigParser()
        p.read(puffer_default_config)
    elif config_file_override:
        p = configparser.ConfigParser()
        p.read([puffer_default_config, config_file_override])
        if env_name not in p.get('base', 'env_name', fallback='').split():
            raise ValueError(
                f'PUFFER_CONFIG_FILE={config_file_override} does not declare env_name={env_name}')
    else:
        for path in glob.glob(puffer_config_dir, recursive=True):
            p = configparser.ConfigParser()
            p.read([puffer_default_config, path])
            if env_name in p['base']['env_name'].split(): break
        else:
            raise ValueError('No config for env_name {}'.format(env_name))

    for section in p.sections():
        for key in p[section]:
            try:
                value = ast.literal_eval(p[section][key])
            except:
                value = p[section][key]

            #TODO: Can clean up with default sections in 3.13+
            fmt = f'--{key}' if section == 'base' else f'--{section}.{key}'
            dtype = type(value)
            parser.add_argument(
                fmt.replace('_', '-'), default=value,
                type=lambda v, t=dtype: v if v == 'auto' else t(v),
            )

    parser.add_argument('-h', '--help', default=argparse.SUPPRESS,
        action='help', help='Show this help message and exit')

    # Unpack to nested dict
    parsed = vars(parser.parse_args())
    args = defaultdict(dict)
    for key, value in parsed.items():
        nxt = args
        for subkey in key.split('.'):
            prev = nxt
            nxt = nxt.setdefault(subkey, {})

        prev[subkey] = value

    args['env_name'] = env_name
    for section in p.sections():
        args.setdefault(section, {})
    return dict(args)

def main():
    err = 'Usage: puffer [train, eval, sweep, paretosweep] [env_name] [optional args]. --help for more info'
    if len(sys.argv) < 3:
        raise ValueError(err)

    mode = sys.argv.pop(1)
    env_name = sys.argv.pop(1)
    args = load_config(env_name)

    if 'train' in mode:
        train(env_name=env_name, args=args)
    elif 'eval' in mode:
        eval(env_name=env_name, args=args)
    elif 'sweep' in mode:
        sweep(env_name=env_name, args=args, pareto='pareto' in mode)
    else:
        raise ValueError(err)

if __name__ == '__main__':
    main()
