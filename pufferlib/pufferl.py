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
import hashlib
import argparse
import configparser
import subprocess as subprocess_lib
from contextlib import contextmanager
from collections import defaultdict
import multiprocessing as mp
from copy import deepcopy

import numpy as np

import torch
import pufferlib
try:
    from pufferlib import _C
except ImportError:
    raise ImportError('Failed to import PufferLib C++ backend. If you have non-default PyTorch, try installing with --no-build-isolation')
from pufferlib import selfplay

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
    stall_trace_dir = env_args.get('stall_trace_dir', '')
    stall_trace_max_episodes = env_args.get('stall_trace_max_episodes', 16)
    stall_trace_tick_cap = env_args.get('stall_trace_tick_cap', 512)
    stall_trace_min_ticks = env_args.get('stall_trace_min_ticks', 64)
    if record_path and play_path:
        raise ValueError('record_best_replay_path and play_replay_path cannot both be set')

    old_record = os.environ.get('RECORD_REPLAY')
    old_play = os.environ.get('PLAY_REPLAY')
    old_post_240_trace_dir = os.environ.get('POST240_TRACE_DIR')
    old_post_240_trace_max_episodes = os.environ.get('POST240_TRACE_MAX_EPISODES')
    old_post_240_trace_tick_cap = os.environ.get('POST240_TRACE_TICK_CAP')
    old_stall_trace_dir = os.environ.get('STALL_TRACE_DIR')
    old_stall_trace_max_episodes = os.environ.get('STALL_TRACE_MAX_EPISODES')
    old_stall_trace_tick_cap = os.environ.get('STALL_TRACE_TICK_CAP')
    old_stall_trace_min_ticks = os.environ.get('STALL_TRACE_MIN_TICKS')
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
        if stall_trace_dir:
            os.environ['STALL_TRACE_DIR'] = stall_trace_dir
            os.environ['STALL_TRACE_MAX_EPISODES'] = str(int(stall_trace_max_episodes))
            os.environ['STALL_TRACE_TICK_CAP'] = str(int(stall_trace_tick_cap))
            os.environ['STALL_TRACE_MIN_TICKS'] = str(int(stall_trace_min_ticks))
        else:
            os.environ.pop('STALL_TRACE_DIR', None)
            os.environ.pop('STALL_TRACE_MAX_EPISODES', None)
            os.environ.pop('STALL_TRACE_TICK_CAP', None)
            os.environ.pop('STALL_TRACE_MIN_TICKS', None)
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
        if old_stall_trace_dir is None:
            os.environ.pop('STALL_TRACE_DIR', None)
        else:
            os.environ['STALL_TRACE_DIR'] = old_stall_trace_dir
        if old_stall_trace_max_episodes is None:
            os.environ.pop('STALL_TRACE_MAX_EPISODES', None)
        else:
            os.environ['STALL_TRACE_MAX_EPISODES'] = old_stall_trace_max_episodes
        if old_stall_trace_tick_cap is None:
            os.environ.pop('STALL_TRACE_TICK_CAP', None)
        else:
            os.environ['STALL_TRACE_TICK_CAP'] = old_stall_trace_tick_cap
        if old_stall_trace_min_ticks is None:
            os.environ.pop('STALL_TRACE_MIN_TICKS', None)
        else:
            os.environ['STALL_TRACE_MIN_TICKS'] = old_stall_trace_min_ticks

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
    'env/damage_per_100_ticks',
    'env/damage_per_tick',
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
    'env/ticks_per_100_damage',
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
            payload[key] = value
    return _filter_wandb_payload(payload, env_name)

def _pvp_score_from_means(wins, damage_dealt, damage_received):
    dmg_diff = damage_dealt / 99.0 - damage_received / 99.0
    dmg_diff_score = min(1.0, max(0.0, 0.5 + 0.25 * dmg_diff))
    return 0.7 * wins + 0.3 * dmg_diff_score, dmg_diff_score

def _weighted_mean(values, weights):
    values = np.asarray(values, dtype=np.float64)
    weights = np.asarray(weights, dtype=np.float64)
    if values.shape != weights.shape:
        raise ValueError('weighted mean values and weights must have the same shape')
    if values.ndim != 1 or values.size == 0:
        raise ValueError('weighted mean requires a nonempty 1-D array')
    if np.any(weights < 0.0) or not np.all(np.isfinite(weights)) or weights.sum() <= 0.0:
        raise ValueError(f'weights must be finite nonnegative values: {weights!r}')
    return float(np.dot(values, weights / weights.sum()))

def _config_sequence(section, key, cast):
    return selfplay.parse_config_sequence(section.get(key, ''), cast, key)

def _fixed_eval_enabled(args):
    return bool(args.get('fixed_eval', {}).get('enabled', 0))

def _fixed_eval_args(args, opponent, seed):
    eval_args = deepcopy(args)
    eval_args['wandb'] = False
    eval_args['checkpoint_interval'] = 0
    eval_args['reset_state'] = True
    eval_args.setdefault('selfplay', {})['enabled'] = 0
    cfg = eval_args.get('fixed_eval', {})

    vec = eval_args.setdefault('vec', {})
    vec['total_agents'] = int(cfg.get('total_agents', min(512, vec['total_agents'])))
    vec['num_buffers'] = int(cfg.get('num_buffers', min(2, vec.get('num_buffers', 1))))
    if vec['total_agents'] % vec['num_buffers'] != 0:
        raise ValueError('fixed_eval.total_agents must be divisible by fixed_eval.num_buffers')

    train = eval_args.setdefault('train', {})
    train['horizon'] = int(cfg.get('horizon', train['horizon']))
    eval_batch_size = train['horizon'] * vec['total_agents']
    train['minibatch_size'] = int(cfg.get('minibatch_size', eval_batch_size))
    train['total_timesteps'] = eval_batch_size
    train['cpu_inference'] = 1

    env = eval_args.setdefault('env', {})
    env['opponent_type'] = int(opponent)
    env['use_rollout_opponent'] = 0
    env['seed'] = int(seed)
    return eval_args

def _collect_pvp_fixed_eval_opponent(backend, args, model_path, opponent, episodes, seed):
    eval_args = _fixed_eval_args(args, opponent, seed)
    pufferl = backend.create_pufferl(eval_args)
    backend.load_weights(pufferl, model_path)

    total_n = 0.0
    sums = defaultdict(float)
    while total_n < episodes:
        backend.rollouts(pufferl)
        flat = dict(unroll_nested_dict(backend.log(pufferl)))
        n = float(flat.get('env/n', 0.0))
        if n <= 0.0:
            continue
        total_n += n
        for key in (
                'env/wins',
                'env/damage_dealt',
                'env/damage_received',
                'env/episode_return',
                'env/episode_length',
                'env/prayer_correct_rate',
                'env/food_remaining',
                'env/karambwan_remaining',
                'env/brews_remaining',
                'env/spec_remaining',
                'env/attacks_landed',
                'env/off_prayer_hits'):
            if key in flat:
                sums[key] += float(flat[key]) * n

    backend.close(pufferl)
    means = {key: value / total_n for key, value in sums.items()}
    wins = means.get('env/wins', 0.0)
    damage_dealt = means.get('env/damage_dealt', 0.0)
    damage_received = means.get('env/damage_received', 0.0)
    score, dmg_diff_score = _pvp_score_from_means(wins, damage_dealt, damage_received)
    means['env/score'] = score
    means['env/dmg_diff_score'] = dmg_diff_score
    means['env/n'] = total_n
    return means

def _run_pvp_fixed_eval_suite(backend, args, model_path):
    cfg = args.get('fixed_eval', {})
    opponents = _config_sequence(cfg, 'opponents', int)
    if not opponents:
        raise ValueError('fixed_eval.enabled requires fixed_eval.opponents')
    weights = _config_sequence(cfg, 'opponent_weights', float)
    if weights and len(weights) != len(opponents):
        raise ValueError('fixed_eval.opponent_weights length must match opponents')
    if not weights:
        weights = [1.0] * len(opponents)

    episodes = int(cfg.get('episodes_per_opponent', 2048))
    if episodes <= 0:
        raise ValueError('fixed_eval.episodes_per_opponent must be positive')
    seed = int(cfg.get('seed', 424242))
    if seed <= 0:
        raise ValueError('fixed_eval.seed must be positive')

    started = time.time()
    logs = {}
    scores, wins, dmg_scores, ns = [], [], [], []
    for idx, opponent in enumerate(opponents):
        means = _collect_pvp_fixed_eval_opponent(
            backend, args, model_path, opponent, episodes, seed + 100_000 * idx)
        prefix = f'env/fixed_eval_opp_{opponent}'
        logs[f'{prefix}_score'] = means['env/score']
        logs[f'{prefix}_wins'] = means['env/wins']
        logs[f'{prefix}_dmg_diff_score'] = means['env/dmg_diff_score']
        logs[f'{prefix}_damage_dealt'] = means.get('env/damage_dealt', 0.0)
        logs[f'{prefix}_damage_received'] = means.get('env/damage_received', 0.0)
        logs[f'{prefix}_n'] = means['env/n']
        scores.append(means['env/score'])
        wins.append(means['env/wins'])
        dmg_scores.append(means['env/dmg_diff_score'])
        ns.append(means['env/n'])

    holdout_scores, holdout_wins = [], []
    for idx, opponent in enumerate(_config_sequence(cfg, 'holdout_opponents', int)):
        means = _collect_pvp_fixed_eval_opponent(
            backend, args, model_path, opponent, episodes, seed + 10_000_000 + 100_000 * idx)
        prefix = f'env/fixed_eval_holdout_opp_{opponent}'
        logs[f'{prefix}_score'] = means['env/score']
        logs[f'{prefix}_wins'] = means['env/wins']
        logs[f'{prefix}_dmg_diff_score'] = means['env/dmg_diff_score']
        logs[f'{prefix}_damage_dealt'] = means.get('env/damage_dealt', 0.0)
        logs[f'{prefix}_damage_received'] = means.get('env/damage_received', 0.0)
        logs[f'{prefix}_n'] = means['env/n']
        holdout_scores.append(means['env/score'])
        holdout_wins.append(means['env/wins'])

    logs['env/fixed_eval_score'] = _weighted_mean(scores, weights)
    logs['env/fixed_eval_wins'] = _weighted_mean(wins, weights)
    logs['env/fixed_eval_dmg_diff_score'] = _weighted_mean(dmg_scores, weights)
    logs['env/fixed_eval_score_unweighted'] = float(np.mean(scores))
    logs['env/fixed_eval_wins_unweighted'] = float(np.mean(wins))
    logs['env/fixed_eval_n'] = float(np.sum(ns))
    logs['env/fixed_eval_elapsed_sec'] = time.time() - started
    if holdout_scores:
        logs['env/fixed_eval_holdout_score'] = float(np.mean(holdout_scores))
        logs['env/fixed_eval_holdout_wins'] = float(np.mean(holdout_wins))
    return logs

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

def _checkpoint_sidecar_path(path):
    return path + '.json'

def _checkpoint_sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()

def _json_ready(value):
    if isinstance(value, dict):
        return {str(k): _json_ready(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json_ready(v) for v in value]
    if isinstance(value, np.generic):
        return value.item()
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return str(value)

def _repo_commit():
    repo_dir = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
    return subprocess_lib.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=repo_dir, text=True).strip()

def _native_env_contract():
    contract = {}
    if hasattr(_C, 'env_obs_size'):
        contract['obs_size'] = int(_C.env_obs_size())
    if hasattr(_C, 'env_num_action_heads'):
        contract['num_action_heads'] = int(_C.env_num_action_heads())
    if hasattr(_C, 'env_action_dims'):
        contract['action_dims'] = [int(v) for v in _C.env_action_dims()]
    return contract

def _zulrah_gear_config(args):
    if args['env_name'] != 'osrs_zulrah':
        return None
    env_args = args.get('env', {})
    return {
        'gear_tier': env_args.get('gear_tier'),
        'gear_tier_mode': env_args.get('gear_tier_mode'),
        'episode_mode': env_args.get('episode_mode'),
        'gear_tier_weight_0': env_args.get('gear_tier_weight_0'),
        'gear_tier_weight_1': env_args.get('gear_tier_weight_1'),
        'gear_tier_weight_2': env_args.get('gear_tier_weight_2'),
    }

def _zulrah_reward_config(args):
    if args['env_name'] != 'osrs_zulrah':
        return None
    env_args = args.get('env', {})
    return {
        'reward_win': env_args.get('reward_win'),
        'reward_loss_penalty': env_args.get('reward_loss_penalty'),
        'reward_damage_dealt': env_args.get('reward_damage_dealt'),
        'reward_correct_style': env_args.get('reward_correct_style'),
        'reward_damage_received_penalty': env_args.get('reward_damage_received_penalty'),
        'reward_cloud_occupancy_penalty': env_args.get('reward_cloud_occupancy_penalty'),
    }

def _checkpoint_file_info(path):
    digest = _checkpoint_sha256(path)
    return {
        'path': path,
        'size_bytes': os.path.getsize(path),
        'sha256': digest,
        'sha256_prefix': digest[:16],
    }

def _checkpoint_metadata(args, pufferl, path):
    info = _checkpoint_file_info(path)
    return _json_ready({
        'metadata_version': 1,
        'checkpoint': info,
        'env_name': args['env_name'],
        'num_params': int(pufferl.num_params()),
        'score_metric': args.get('score_metric'),
        'policy': args.get('policy', {}),
        'env_contract': _native_env_contract(),
        'zulrah_gear_config': _zulrah_gear_config(args),
        'zulrah_reward_config': _zulrah_reward_config(args),
        'git_commit': _repo_commit(),
    })

def _write_checkpoint_metadata(args, pufferl, path):
    sidecar = _checkpoint_sidecar_path(path)
    with open(sidecar, 'w') as f:
        json.dump(_checkpoint_metadata(args, pufferl, path), f,
            indent=2, sort_keys=True)

def _validate_checkpoint_metadata(args, pufferl, path):
    sidecar = _checkpoint_sidecar_path(path)
    if not os.path.exists(sidecar):
        return None
    with open(sidecar) as f:
        metadata = json.load(f)
    expected = _checkpoint_metadata(args, pufferl, path)
    fields = [
        'env_name',
        'num_params',
        'score_metric',
        'policy',
        'env_contract',
        'zulrah_gear_config',
        'zulrah_reward_config',
    ]
    mismatches = [
        key for key in fields
        if metadata.get(key) != expected.get(key)
    ]
    if metadata.get('checkpoint', {}).get('sha256') != expected['checkpoint']['sha256']:
        mismatches.append('checkpoint.sha256')
    if mismatches:
        raise ValueError(
            f'Checkpoint metadata mismatch for {path}: {", ".join(mismatches)}')
    return metadata

def _print_resolved_checkpoint(path, pufferl=None):
    info = _checkpoint_file_info(path)
    params = '' if pufferl is None else f' params={int(pufferl.num_params())}'
    print(
        f'Resolved checkpoint: path={info["path"]} '
        f'sha256={info["sha256_prefix"]} size={info["size_bytes"]}{params}',
        flush=True)
    return info

def _no_render_eval_summary(args, pufferl, checkpoint_info, flat_logs):
    env_args = args.get('env', {})
    return _json_ready({
        'checkpoint': checkpoint_info,
        'model': {
            'num_params': int(pufferl.num_params()),
        },
        'env': args['env_name'],
        'policy': args.get('policy', {}),
        'score_metric': args.get('score_metric'),
        'zulrah_reward_config': _zulrah_reward_config(args),
        'episode_mode': env_args.get('episode_mode'),
        'gear_tier_mode': env_args.get('gear_tier_mode'),
        'tier_weights': [
            env_args.get('gear_tier_weight_0'),
            env_args.get('gear_tier_weight_1'),
            env_args.get('gear_tier_weight_2'),
        ],
        'sampled_tier_fractions': {
            'tier_0': flat_logs.get('env/gear_tier_0_frac'),
            'tier_1': flat_logs.get('env/gear_tier_1_frac'),
            'tier_2': flat_logs.get('env/gear_tier_2_frac'),
        },
        'metrics': flat_logs,
    })

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
            _validate_checkpoint_metadata(args, pufferl, load_path)
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

        """Multi-bank PFSP self-play curriculum (no-op when selfplay.enabled=0).
        Ported from cheng_fork/selfplay; works on either backend (CUDA on box,
        Metal locally) via the _C symbols added in src/metal_bindings.mm."""
        pool_state = selfplay.setup(pufferl, backend, args, run_id)

        model_path = ''
        flat_logs = {}
        last_log_was_eval = False
        logged_eval_to_wandb = False
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
                _write_checkpoint_metadata(args, pufferl, model_path)
                if args['save_training_state']:
                    state_path = os.path.join(checkpoint_dir,
                        f'{pufferl.global_step:016d}.state')
                    backend.save_training_state(pufferl, state_path)

            # Rate limit, but always log for eval to maintain determinism
            if time.time() < pufferl.last_log_time + 0.6 and epoch < train_epochs - 1:
                continue

            is_eval_epoch = epoch >= train_epochs
            logs = backend.eval_log(pufferl) if is_eval_epoch else backend.log(pufferl)
            last_log_was_eval = is_eval_epoch
            fresh_logs = dict(unroll_nested_dict(logs))
            flat_logs = {**flat_logs, **fresh_logs}
            nan_loss_keys = [
                k for k, v in fresh_logs.items()
                if k.startswith('loss/')
                and isinstance(v, (float, np.floating))
                and np.isnan(v)
            ]
            if nan_loss_keys:
                if model_path and os.path.exists(model_path):
                    os.remove(model_path)
                    sidecar = _checkpoint_sidecar_path(model_path)
                    if os.path.exists(sidecar):
                        os.remove(sidecar)
                backend.close(pufferl)
                raise FloatingPointError(
                    f'NaN loss in {env_name}: {", ".join(nan_loss_keys)}')

            """Self-play pool maintenance: opponent Elo update, snapshot, swap.
            No-op when pool_state is None (selfplay disabled)."""
            if epoch < train_epochs:
                selfplay.step(pufferl, backend, pool_state, flat_logs, epoch)
                for key, value in flat_logs.items():
                    if (key.startswith('pool/')
                            or key.startswith('env/historical_winrate')
                            or key == 'env/elo'):
                        fresh_logs[key] = value

            if verbose:
                print_dashboard(args, model_size, flat_logs)

            if args['wandb']:
                if epoch < train_epochs:
                    wandb.log(
                        _wandb_train_payload(
                            fresh_logs, pufferl.global_step, args['env_name']),
                        step=pufferl.global_step,
                    )

            has_target_metric = target_key in flat_logs

            if epoch < train_epochs:
                all_logs.append(flat_logs)

                if (sweep_obj is not None
                        and has_target_metric
                        and pufferl.global_step > min(0.20*total_timesteps, 100_000_000) and
                        sweep_obj.early_stop(logs, target_key)):
                    break
            elif has_target_metric and flat_logs['env/n'] > args['eval_episodes']:
                if args['wandb']:
                    wandb.log(
                        _wandb_eval_payload(
                            flat_logs, pufferl.global_step, args['env_name']),
                        step=pufferl.global_step,
                    )
                    logged_eval_to_wandb = True
                break


        if args['wandb'] and last_log_was_eval and not logged_eval_to_wandb:
            wandb.log(
                _wandb_eval_payload(
                    flat_logs, pufferl.global_step, args['env_name']),
                step=pufferl.global_step,
            )

        fixed_eval_model_path = ''
        if _fixed_eval_enabled(args):
            fixed_eval_model_path = os.path.join(checkpoint_dir, 'fixed_eval_weights.bin')
            backend.save_weights(pufferl, fixed_eval_model_path)

        print_dashboard(args, model_size, flat_logs)
        backend.close(pufferl)

    if fixed_eval_model_path:
        fixed_eval_logs = _run_pvp_fixed_eval_suite(backend, args, fixed_eval_model_path)
        flat_logs = {**flat_logs, **fixed_eval_logs}
        if 'uptime' in flat_logs:
            flat_logs['uptime'] += fixed_eval_logs['env/fixed_eval_elapsed_sec']
        if args['wandb']:
            import wandb
            wandb.log(
                _wandb_train_payload(fixed_eval_logs, flat_logs.get('agent_steps', 0),
                    args['env_name']),
                step=flat_logs.get('agent_steps', 0),
            )
        if not args.get('fixed_eval', {}).get('keep_weights', 0):
            os.remove(fixed_eval_model_path)

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
    processes = []
    for rank, gpu_id in reversed(list(enumerate(gpus))):
        worker_args = deepcopy(args)
        worker_args['rank'] = rank
        worker_args['gpu_id'] = gpu_id
        if rank == 0 and not subprocess:
            _train(env_name, worker_args, verbose=True)
        else:
            process = ctx.Process(target=_train, args=(env_name, worker_args),
                kwargs=kwargs)
            process.start()
            processes.append(process)

    return processes

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

    active = {}
    completed = restored_runs
    launched = restored_runs
    while True:
        should_collect = active and (
            len(active) >= concurrent_experiments
            or (has_run_cap and launched >= max_runs)
        )
        if should_collect:
            gpu_id, scores, costs, timesteps = result_queue.get()
            if gpu_id not in active:
                continue
            done_args, processes = active.pop(gpu_id)
            for process in processes:
                process.join()

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
        launched += 1
        early_stopper = sweep_obj.make_early_stopper()
        processes = train(env_name, exp_args, range(gpu_id, gpu_id + exp_gpus),
            sweep_obj=early_stopper, result_queue=result_queue)
        active[gpu_id] = (exp_args, processes)

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
            checkpoint_info = _print_resolved_checkpoint(load_path, pufferl)
            _validate_checkpoint_metadata(args, pufferl, load_path)
            backend.load_weights(pufferl, load_path)
            print(f'Loaded weights from {load_path}', flush=True)
        else:
            checkpoint_info = None

        _phase2_init_if_configured(backend, pufferl, args)

        if args['render_mode'] == 'None':
            flat_logs = {}
            while flat_logs.get('env/n', 0) < args['eval_episodes']:
                backend.rollouts(pufferl)
                logs = backend.eval_log(pufferl)
                flat_logs = dict(unroll_nested_dict(logs))
            print(json.dumps(
                _no_render_eval_summary(args, pufferl, checkpoint_info, flat_logs),
                indent=2, sort_keys=True))
            backend.close(pufferl)
            return

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
        matches = []
        for path in sorted(glob.glob(puffer_config_dir, recursive=True)):
            candidate = configparser.ConfigParser()
            candidate.read([puffer_default_config, path])
            if env_name in candidate['base']['env_name'].split():
                matches.append((path, candidate))

        if not matches:
            raise ValueError('No config for env_name {}'.format(env_name))

        exact_config_name = f'{env_name}.ini'
        exact_matches = [
            (path, candidate) for path, candidate in matches
            if os.path.basename(path) == exact_config_name
        ]
        _, p = (exact_matches or matches)[0]

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
