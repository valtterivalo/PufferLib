#!/usr/bin/env python3
"""pufferl -- unified Metal RL training CLI.

usage:
    python pufferl.py train <env> [args]     single training run
    python pufferl.py sweep <env> [args]     Protein hyperparameter sweep
    python pufferl.py results <env>          print sweep results
    python pufferl.py eval <env> [args]      render trained agent

structure mirrors upstream PufferLib 4.0 pufferl.py: PuffeRL class wrapping
_C, _train_rank loop, train/sweep/eval entry points. Metal additions marked
with # --- Metal addition --- comments.
"""

from __future__ import annotations

import argparse
import ast
import configparser
import glob
import json
import math
import os
import signal
import sys
import time
from copy import deepcopy
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))

import rich
import rich.box
from rich.table import Table
from rich.console import Console

import pufferlib
from pufferlib import _C
from pufferlib.pufferl import downsample, abbreviate, duration
from pufferlib.sweep import Protein, pareto_points, prune_pareto_front

signal.signal(signal.SIGINT, lambda sig, frame: os._exit(0))

# keep logs live when piping through tee
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(line_buffering=True, write_through=True)
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(line_buffering=True, write_through=True)


# ============================================================================
# helpers (matches upstream pufferl.py formatting utilities)
# ============================================================================

def unroll_nested_dict(d):
    if not isinstance(d, dict):
        return d
    for k, v in d.items():
        if isinstance(v, dict):
            for k2, v2 in unroll_nested_dict(v):
                yield f"{k}/{k2}", v2
        else:
            yield k, v


def fmt_perf(name, color, delta_ref, elapsed, b2, c2):
    percent = 0 if delta_ref == 0 else int(100 * elapsed / delta_ref - 1e-5)
    return f'{color}{name}', duration(elapsed, b2, c2), f'{b2}{percent:2d}{c2}%'


# ============================================================================
# config loading (reads .ini files, builds argparse dynamically)
# ============================================================================

METAL_CONFIG_DIR = Path(__file__).parent / "config"
SWEEP_DIR_BASE = Path("runs/sweep_bench")


def _parse_ini_value(raw: str):
    """Parse a single .ini value into its Python type via ast.literal_eval.

    Safe: only parses Python literals (strings, numbers, booleans, None).
    Falls back to returning the raw string.
    """
    try:
        return ast.literal_eval(raw)
    except (ValueError, SyntaxError):
        return raw


def load_config(env_name: str) -> dict:
    """Load Metal config from default.ini + per-env .ini, merged via configparser.

    Returns a nested dict with sections as top-level keys. Sweep sections
    (sweep.train.*, sweep.policy.*) are restructured into {"sweep": {"train": {...}, ...}}.
    """
    default_ini = METAL_CONFIG_DIR / "default.ini"
    env_ini = METAL_CONFIG_DIR / "ocean" / f"{env_name}.ini"

    if not env_ini.exists():
        raise FileNotFoundError(f"no Metal config for env '{env_name}': {env_ini}")

    p = configparser.ConfigParser()
    p.read([str(default_ini), str(env_ini)])

    config = {}
    for section in p.sections():
        parsed_section = {}
        for key in p[section]:
            parsed_section[key] = _parse_ini_value(p[section][key])
        config[section] = parsed_section

    # restructure sweep.train.X / sweep.policy.X into nested sweep dict
    sweep = config.pop("sweep", {})
    sweep_params = {"train": {}, "policy": {}, "env": {}}
    sweep_sections_to_remove = []
    for section_name, section_data in list(config.items()):
        if section_name.startswith("sweep."):
            parts = section_name.split(".", 2)
            if len(parts) == 3:
                group, param = parts[1], parts[2]
                sweep_params.setdefault(group, {})[param] = section_data
            sweep_sections_to_remove.append(section_name)
    for key in sweep_sections_to_remove:
        config.pop(key)

    sweep_params = {k: v for k, v in sweep_params.items() if v}
    config["sweep"] = {**sweep, **sweep_params}

    return config


def apply_cli_overrides(config: dict, env_name: str) -> dict:
    """Build argparse from config keys, parse CLI, apply overrides."""
    parser = argparse.ArgumentParser(
        description=f"Metal training for {env_name}",
        add_help=True,
    )

    arg_registry = {}
    for section in ("train", "policy", "vec", "env", "base"):
        section_data = config.get(section, {})
        for key, value in section_data.items():
            cli_name = f"--{key.replace('_', '-')}"
            if cli_name in arg_registry:
                continue
            arg_registry[cli_name] = (section, key)
            if isinstance(value, bool):
                parser.add_argument(cli_name, type=lambda x: x.lower() in ("1", "true", "yes"),
                                    default=value)
            elif isinstance(value, int):
                parser.add_argument(cli_name, type=int, default=value)
            elif isinstance(value, float):
                parser.add_argument(cli_name, type=float, default=value)
            elif isinstance(value, str):
                parser.add_argument(cli_name, type=str, default=value)
            else:
                parser.add_argument(cli_name, type=type(value), default=value)

    def _add_if_new(name, **kwargs):
        try:
            parser.add_argument(name, **kwargs)
        except argparse.ArgumentError:
            pass

    _add_if_new("--no-overlap", action="store_true")
    _add_if_new("--fp16", action="store_true",
                help="fp16 training activations/grads (rollout stays fp32)")
    _add_if_new("--checkpoint-interval", type=int, default=200,
                help="save weights every N iterations")
    _add_if_new("--checkpoint-dir", type=str, default="",
                help="checkpoint directory (default: checkpoints/<env>/<run_id>)")
    _add_if_new("--load-model-path", type=str, default="latest",
                help="path to checkpoint for eval mode (default: latest)")
    # --- Metal addition ---
    _add_if_new("--trace-path", type=str, default="")
    _add_if_new("--trace-every", type=int, default=1)
    # sweep CLI
    _add_if_new("--timeout", type=float, default=4.0,
                help="max sweep hours (default: 4)")
    _add_if_new("--max-trials", type=int, default=None)
    _add_if_new("--results", action="store_true",
                help="print sweep results and exit")
    # wandb
    parser.add_argument("--wandb", action="store_true", help="log to wandb")
    parser.add_argument("--wandb-project", type=str, default="pufferlib-metal")
    parser.add_argument("--wandb-group", type=str, default="debug")
    parser.add_argument("--tag", type=str, default=None)

    args = parser.parse_args()

    parsed = vars(args)
    for cli_name, (section, key) in arg_registry.items():
        attr = cli_name.lstrip("-").replace("-", "_")
        if attr in parsed:
            config.setdefault(section, {})[key] = parsed[attr]

    if args.no_overlap:
        config.setdefault("train", {})["overlap"] = 0
    if args.fp16:
        config.setdefault("train", {})["train_fp16"] = 1

    config["_cli"] = {
        "checkpoint_interval": args.checkpoint_interval,
        "checkpoint_dir": args.checkpoint_dir,
        "load_model_path": args.load_model_path,
        "trace_path": args.trace_path,
        "trace_every": args.trace_every,
        "timeout": args.timeout,
        "max_trials": args.max_trials,
        "results": args.results,
        "wandb": args.wandb,
        "wandb_project": args.wandb_project,
        "wandb_group": args.wandb_group,
        "tag": args.tag,
    }

    return config


def build_config(env_name: str, config: dict) -> dict:
    """Convert loaded config dict to the single nested dict for _C.create_pufferl.

    Returns {"train": {...}, "vec": {...}, "env": {...}, "policy": {...}, "env_name": ...}
    matching the bindings create_pufferl API.
    """
    train = config.get("train", {})
    policy = config.get("policy", {})
    vec = config.get("vec", {})
    env = config.get("env", {})

    horizon = int(train.get("horizon", 64))
    total_agents = int(vec.get("total_agents", train.get("total_agents", 4096)))
    num_buffers = int(vec.get("num_buffers", train.get("num_buffers", 1)))
    minibatch_size = int(train.get("minibatch_size", 8192))

    batch_size = total_agents * horizon
    if minibatch_size > batch_size:
        minibatch_size = batch_size

    train_config = {
        "horizon": horizon,
        "learning_rate": train.get("learning_rate", 0.1),
        "min_lr_ratio": train.get("min_lr_ratio", 0.0),
        "anneal_lr": 1.0,
        "beta1": train.get("beta1", 0.73),
        "beta2": train.get("beta2", 0.9986),
        "eps": train.get("eps", 8.3e-5),
        "minibatch_size": minibatch_size,
        "replay_ratio": train.get("replay_ratio", 1.0),
        "total_timesteps": int(train.get("total_timesteps", 100_000_000)),
        "max_grad_norm": train.get("max_grad_norm", 1.5),
        "clip_coef": train.get("clip_coef", 0.2),
        "vf_clip_coef": train.get("vf_clip_coef", 0.2),
        "vf_coef": train.get("vf_coef", 2.0),
        "ent_coef": train.get("ent_coef", 0.001),
        "gamma": train.get("gamma", 0.995),
        "gae_lambda": train.get("gae_lambda", 0.90),
        "vtrace_rho_clip": train.get("vtrace_rho_clip", 2.0),
        "vtrace_c_clip": train.get("vtrace_c_clip", 1.1),
        "prio_alpha": train.get("prio_alpha", 0.8),
        "prio_beta0": train.get("prio_beta0", 0.2),
        "profile": float(int(train.get("profile", 0))),
        "overlap": float(int(train.get("overlap", 1))),
        "cpu_inference": float(int(train.get("cpu_inference",
            1 if env_name in ("breakout", "g2048") else 0))),
        "train_fp16": float(int(train.get("train_fp16", 0))),
        "ns_iters": float(int(train.get("ns_iters", 5))),
        "seed": float(int(train.get("seed", 42))),
    }
    vec_config = {
        "total_agents": float(total_agents),
        "num_buffers": float(num_buffers),
        "num_threads": float(vec.get("num_threads", num_buffers)),
    }
    policy_config = {
        "hidden_size": float(int(policy.get("hidden_size", 64))),
        "num_layers": float(int(policy.get("num_layers", 2))),
    }
    env_config = dict(env)

    if "scaffolding_ratio" in train:
        env_config["scaffolding_ratio"] = train["scaffolding_ratio"]

    return {
        "train": train_config,
        "vec": vec_config,
        "env": env_config,
        "policy": policy_config,
        "env_name": env_name,
    }


def validate_config(args):
    minibatch_size = int(args['train']['minibatch_size'])
    horizon = int(args['train']['horizon'])
    total_agents = int(args['vec']['total_agents'])
    assert (minibatch_size % horizon) == 0, \
        f'minibatch_size {minibatch_size} must be divisible by horizon {horizon}'
    assert minibatch_size <= horizon * total_agents, \
        f'minibatch_size {minibatch_size} > total_agents {total_agents} * horizon {horizon}'


# ============================================================================
# PuffeRL class (mirrors upstream _C pufferl lifecycle)
# ============================================================================

class PuffeRL:
    """Wraps _C.create_pufferl and owns the training loop lifecycle.

    Methods mirror upstream: evaluate, train, write_logs, print_dashboard,
    save_checkpoint, close. Single-GPU Metal only (no DDP).
    """

    def __init__(self, args):
        self.args = args
        self._pufferl = _C.create_pufferl(args)
        self.model_size = self._pufferl.num_params()
        self._flat_logs = {}
        self._console = Console()
        self._dash_idx = 0

    @property
    def global_step(self):
        return self._pufferl.global_step

    @property
    def epoch(self):
        return self._pufferl.epoch

    @property
    def last_log_time(self):
        return self._pufferl.last_log_time

    def evaluate(self):
        _C.rollouts(self._pufferl)

    def train(self):
        _C.train(self._pufferl)

    def write_logs(self):
        """Collect logs from _C.log() and merge into flat_logs. Returns flat dict."""
        logs = _C.log(self._pufferl)
        flat = dict(unroll_nested_dict(logs))

        # --- Metal addition: merge debug stats ---
        try:
            debug = _C.log_train_debug(self._pufferl)
            for k, v in debug.items():
                flat[f'debug/{k}'] = v
        except Exception:
            pass

        self._flat_logs = {**self._flat_logs, **flat}
        return self._flat_logs

    def print_dashboard(self, clear=False,
            c1='[cyan]', c2='[white]', b1='[bright_cyan]', b2='[bright_white]'):
        """Render Rich dashboard. Generic env stats (not hardcoded per env)."""
        g = lambda k, d=0: self._flat_logs.get(k, d)
        args = self.args

        dashboard = Table(box=rich.box.ROUNDED, expand=True,
            show_header=False, border_style='bright_cyan')
        table = Table(box=None, expand=True, show_header=False)
        dashboard.add_row(table)

        table.add_column(justify="left", width=30)
        table.add_column(justify="center", width=12)
        table.add_column(justify="center", width=18)
        table.add_column(justify="right", width=12)

        table.add_row(
            f'{b1}PufferLib Metal {b2}{args["env_name"]} {self._dash_idx * " "}:blowfish:',
            f'{c1}GPU: {b2}{g("util/gpu_percent"):.0f}{c2}%',
            f'{c1}VRAM: {b2}{g("util/vram_used_gb"):.1f}{c2}/{b2}{g("util/vram_total_gb"):.0f}{c2}G',
            f'{c1}RAM: {b2}{g("util/cpu_mem_gb"):.1f}{c2}G',
        )
        self._dash_idx = (self._dash_idx - 1) % 10

        # summary
        agent_steps = g('agent_steps')
        sps = g('SPS')
        total_ts = int(args['train']['total_timesteps'])
        remaining = duration((total_ts - agent_steps) / sps, b2, c2) if sps > 0 else f'{b2}--{c2}'

        s = Table(box=None, expand=True)
        s.add_column(f"{c1}Summary", justify='left', vertical='top', width=10)
        s.add_column(f"{c1}Value", justify='right', vertical='top', width=14)
        s.add_row(f'{c2}Env', f'{b2}{args["env_name"]}')
        s.add_row(f'{c2}Params', abbreviate(self.model_size, b2, c2))
        s.add_row(f'{c2}Steps', abbreviate(agent_steps, b2, c2))
        s.add_row(f'{c2}SPS', abbreviate(sps, b2, c2))
        s.add_row(f'{c2}Epoch', f'{b2}{g("epoch")}')
        s.add_row(f'{c2}Uptime', duration(g('uptime'), b2, c2))
        s.add_row(f'{c2}Remaining', remaining)

        # perf
        rollout_t = g('perf/rollout')
        train_t = g('perf/train')
        delta = rollout_t + train_t
        p = Table(box=None, expand=True, show_header=False)
        p.add_column(f"{c1}Performance", justify="left", width=10)
        p.add_column(f"{c1}Time", justify="right", width=8)
        p.add_column(f"{c1}%", justify="right", width=4)
        p.add_row(*fmt_perf('Evaluate', b1, delta, rollout_t, b2, c2))
        p.add_row(*fmt_perf('  GPU', b2, delta, g('perf/eval_gpu'), b2, c2))
        p.add_row(*fmt_perf('  Env', b2, delta, g('perf/eval_env'), b2, c2))
        p.add_row(*fmt_perf('Train', b1, delta, train_t, b2, c2))
        p.add_row(*fmt_perf('  Misc', b2, delta, g('perf/train_misc'), b2, c2))
        p.add_row(*fmt_perf('  Forward', b2, delta, g('perf/train_forward'), b2, c2))

        # losses
        l = Table(box=None, expand=True)
        l.add_column(f'{c1}Losses', justify="left", width=16)
        l.add_column(f'{c1}Value', justify="right", width=8)
        for k, v in self._flat_logs.items():
            if k.startswith('loss/'):
                l.add_row(f'{b2}{k[5:]}', f'{b2}{v:.3f}')
        # --- Metal addition: debug stats in losses column ---
        for dk in ('debug/grad_l2', 'debug/dec_policy_abs_max', 'debug/dec_value_abs_max'):
            if dk in self._flat_logs:
                l.add_row(f'{b2}{dk.split("/")[1]}', f'{b2}{self._flat_logs[dk]:.3f}')

        monitor = Table(box=None, expand=True, pad_edge=False)
        monitor.add_row(s, p, l)
        dashboard.add_row(monitor)

        # env stats (generic -- iterate all env/ keys)
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
        for k, v in self._flat_logs.items():
            if k.startswith('env/') and k != 'env/n':
                u = left if i % 2 == 0 else right
                u.add_row(f'{b2}{k[4:]}', f'{b2}{v:.3f}')
                i += 1
                if i == 30:
                    break

        if clear:
            self._console.clear()
        with self._console.capture() as capture:
            self._console.print(dashboard)
        print('\033[0;0H' + capture.get())

    def save_checkpoint(self, path):
        _C.save_weights(self._pufferl, path)

    def load_checkpoint(self, path):
        _C.load_weights(self._pufferl, path)

    def close(self):
        _C.close(self._pufferl)


# ============================================================================
# _train_rank: single training run (matches upstream _train)
# ============================================================================

def _train_rank(args, sweep_obj=None, verbose=True):
    """Single-GPU training rank. Creates PuffeRL, loops evaluate+train.

    Returns (pufferl_instance, all_flat_logs). Caller must close pufferl.
    """
    validate_config(args)
    pufferl = PuffeRL(args)

    target_key = f'env/{args.get("sweep", {}).get("metric", "score")}'
    total_timesteps = int(args['train']['total_timesteps'])
    all_logs = []

    # --- Metal addition: PFSP callback ---
    on_iteration = args.get('_metal', {}).get('on_iteration')

    # --- Metal addition: trace logging ---
    trace_file = None
    trace_every = max(int(args.get('_metal', {}).get('trace_every', 1)), 1)
    trace_path = args.get('_metal', {}).get('trace_path', '')
    if trace_path:
        tp = Path(trace_path).expanduser().resolve()
        tp.parent.mkdir(parents=True, exist_ok=True)
        trace_file = tp.open("w", encoding="utf-8")
        trace_file.write(json.dumps({"event": "meta", "env": args["env_name"]}) + "\n")
        trace_file.flush()

    # checkpoint setup
    checkpoint_dir = args.get('_metal', {}).get('checkpoint_dir', '')
    checkpoint_interval = int(args.get('_metal', {}).get('checkpoint_interval', 200))
    if not checkpoint_dir and sweep_obj is None:
        run_id = str(int(1000 * time.time()))
        checkpoint_dir = os.path.join("checkpoints", args["env_name"], run_id)
    if checkpoint_dir:
        os.makedirs(checkpoint_dir, exist_ok=True)

    # wandb
    wandb_run = None
    wandb_cfg = args.get('_metal', {}).get('wandb')
    if wandb_cfg:
        import wandb
        run_id = wandb.util.generate_id()
        run_name = wandb_cfg.get("tag") or f"{args['env_name']}-{run_id[:6]}"
        wandb_run = wandb.init(
            id=run_id, config=args,
            project=wandb_cfg.get("project", "pufferlib-metal"),
            group=wandb_cfg.get("group", "debug"),
            name=run_name,
            tags=[wandb_cfg["tag"]] if wandb_cfg.get("tag") else [args['env_name']],
            settings=wandb.Settings(console="off"),
        )

    if verbose:
        flat = pufferl.write_logs()
        pufferl.print_dashboard(clear=True)

    epoch = 0
    while pufferl.global_step < total_timesteps:
        pufferl.evaluate()
        pufferl.train()
        epoch += 1

        # checkpoint
        if checkpoint_dir and checkpoint_interval > 0 and (epoch % checkpoint_interval == 0):
            path = os.path.join(checkpoint_dir, f"{pufferl.global_step:016d}.bin")
            pufferl.save_checkpoint(path)

        # --- Metal addition: PFSP callback ---
        if on_iteration:
            on_iteration(pufferl._pufferl, pufferl.global_step)

        # rate-limit logging (upstream uses 0.6s)
        if time.time() < pufferl.last_log_time + 0.6:
            continue

        flat = pufferl.write_logs()

        # NaN guard
        for loss_name in ('loss/entropy', 'loss/policy', 'loss/value'):
            v = flat.get(loss_name)
            if v is not None and not math.isfinite(float(v)):
                raise RuntimeError(f"invalid loss {loss_name}={v} at step={pufferl.global_step}")

        if verbose:
            pufferl.print_dashboard()

        if target_key in flat:
            all_logs.append(dict(flat))

            if wandb_run:
                wandb_run.log(flat, step=flat.get('agent_steps', 0))

            # --- Metal addition: trace logging ---
            if trace_file and epoch % trace_every == 0:
                trace_file.write(json.dumps({
                    "event": "tick", "epoch": epoch,
                    "step": flat.get("agent_steps", 0),
                    "sps": flat.get("SPS", 0),
                }) + "\n")

            # sweep early stopping
            if (sweep_obj is not None
                    and pufferl.global_step > min(0.20 * total_timesteps, 100_000_000)):
                score = flat.get(target_key, 0)
                elapsed = flat.get('uptime', 0)
                if sweep_obj.should_stop(score, elapsed):
                    break

    # final log + dashboard
    flat = pufferl.write_logs()
    if verbose:
        pufferl.print_dashboard()

    # final checkpoint
    if checkpoint_dir and sweep_obj is None:
        path = os.path.join(checkpoint_dir, f"{pufferl.global_step:016d}.bin")
        pufferl.save_checkpoint(path)

    if trace_file:
        trace_file.write(json.dumps({
            "event": "final", "step": pufferl.global_step,
            "uptime": flat.get("uptime", 0),
        }) + "\n")
        trace_file.close()

    if wandb_run:
        import wandb as _wandb
        if checkpoint_dir:
            final_ckpt = os.path.join(checkpoint_dir, f"{pufferl.global_step:016d}.bin")
            if os.path.exists(final_ckpt):
                artifact = _wandb.Artifact(f"{args['env_name']}-model", type="model")
                artifact.add_file(final_ckpt)
                wandb_run.log_artifact(artifact)
        wandb_run.finish()

    return pufferl, all_logs


def train(env_name, args=None):
    """Train entry point. Upstream does DDP here; we're single GPU Metal."""
    config = args or load_config(env_name)
    if args is None:
        config = apply_cli_overrides(config, env_name)
    cli = config.pop("_cli", {})

    built = build_config(env_name, config)

    # pack Metal additions into _metal key
    built['_metal'] = {
        'checkpoint_dir': cli.get('checkpoint_dir', ''),
        'checkpoint_interval': cli.get('checkpoint_interval', 200),
        'trace_path': cli.get('trace_path', ''),
        'trace_every': cli.get('trace_every', 1),
    }
    if cli.get('wandb'):
        built['_metal']['wandb'] = {
            'project': cli.get('wandb_project', 'pufferlib-metal'),
            'group': cli.get('wandb_group', 'debug'),
            'tag': cli.get('tag'),
        }

    # keep sweep config for target_key resolution
    built['sweep'] = config.get('sweep', {})

    pufferl, _ = _train_rank(built, verbose=True)
    pufferl.close()


# ============================================================================
# sweep (Protein hyperparameter optimization)
# ============================================================================

# --- Metal addition: JSONL sweep persistence ---

def _save_observation(obs: dict, path: Path) -> None:
    """Append observation to JSONL persistence file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a") as f:
        f.write(json.dumps(obs) + "\n")


def _load_observations(path: Path) -> list[dict]:
    """Load all observations from JSONL persistence file."""
    if not path.exists():
        return []
    records = []
    with path.open() as f:
        for line in f:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


def sweep(env_name, args=None):
    """Protein sweep orchestrator. Calls _train_rank() per trial, collects results."""
    config = args or load_config(env_name)
    if args is None:
        config = apply_cli_overrides(config, env_name)
    cli = config.pop("_cli", {})

    if cli.get("results"):
        _print_results(SWEEP_DIR_BASE / env_name / "observations.jsonl")
        return

    sweep_dir = SWEEP_DIR_BASE / env_name
    sweep_dir.mkdir(parents=True, exist_ok=True)
    obs_path = sweep_dir / "observations.jsonl"

    # build Protein sweep config
    sweep_cfg = config.get("sweep", {})
    sweep_config = {
        "method": sweep_cfg.get("method", "Protein"),
        "metric": config.get("base", {}).get("score_metric", "score"),
        "metric_distribution": sweep_cfg.get("metric_distribution", "linear"),
        "goal": sweep_cfg.get("goal", "maximize"),
        "downsample": int(sweep_cfg.get("downsample", 5)),
        "use_gpu": False,
        "prune_pareto": sweep_cfg.get("prune_pareto", True),
        "early_stop_quantile": float(sweep_cfg.get("early_stop_quantile", 0.3)),
        "max_suggestion_cost": int(sweep_cfg.get("max_suggestion_cost", 1800)),
    }
    for group in ("train", "policy", "env", "vec"):
        if group in sweep_cfg and isinstance(sweep_cfg[group], dict):
            sweep_config[group] = sweep_cfg[group]

    default_params = {
        "train": dict(config.get("train", {})),
        "policy": dict(config.get("policy", {})),
    }
    if config.get("vec"):
        default_params["vec"] = dict(config["vec"])
    if config.get("env"):
        default_params["env"] = dict(config["env"])

    protein = Protein(sweep_config, use_gpu=False, prune_pareto=True)

    # replay existing observations
    existing_records = _load_observations(obs_path)
    existing_trials: set[int] = set()
    if existing_records:
        for r in existing_records:
            existing_trials.add(r["trial"])
            if "train" in r["params"]:
                r["params"]["train"].setdefault("ns_iters", 5)
            score = r.get("score", r.get("episode_return", 0))
            protein.observe(r["params"], score, r["cost"])
        print(f"replayed {len(existing_records)} observations from {len(existing_trials)} previous trials")

    # wandb config
    wandb_config = None
    if cli.get("wandb"):
        wandb_config = {
            "project": cli.get("wandb_project", "pufferlib-metal"),
            "group": cli.get("wandb_group", "debug"),
            "tag": cli.get("tag"),
        }

    trial_idx = max(existing_trials) + 1 if existing_trials else 0
    sweep_start = time.time()
    timeout_s = cli.get("timeout", 4.0) * 3600
    max_trials = cli.get("max_trials")
    score_key = config.get("base", {}).get("score_metric", "score")
    downsample_points = int(sweep_cfg.get("downsample", 5))

    n_params = len(dict(pufferlib.unroll_nested_dict(
        {k: v for k, v in sweep_config.items() if isinstance(v, dict)}
    )))
    print(f"protein sweep ({env_name}, metal)")
    print(f"  metric: {score_key}, {n_params} params, timeout: {cli.get('timeout', 4.0):.1f}h")

    trials_run = 0
    while True:
        if max_trials is not None and trials_run >= max_trials:
            print(f"\nreached max trials ({max_trials})")
            break
        if (time.time() - sweep_start) > timeout_s:
            print(f"\ntimeout reached ({cli.get('timeout', 4.0):.1f}h)")
            break

        if trial_idx == 0:
            params = deepcopy(default_params)
            print("\ntrial 0: default hyperparameters")
        else:
            fill = deepcopy(default_params)
            params, info = protein.suggest(fill)
            if info:
                print(f"\nprotein prediction: score={info.get('score', 0):.3f}, cost={info.get('cost', 0):.0f}s")

        # build trial args
        p_vec = params.get("vec", {})
        p_train = params.get("train", {})
        cfg_vec = config.get("vec", {})
        trial_config = {
            "train": p_train,
            "policy": params.get("policy", {}),
            "vec": {
                "total_agents": p_vec.get("total_agents",
                    p_train.get("total_agents", cfg_vec.get("total_agents", 2048))),
                "num_buffers": p_vec.get("num_buffers",
                    p_train.get("num_buffers", cfg_vec.get("num_buffers", 1))),
                "num_threads": cfg_vec.get("num_threads",
                    p_vec.get("num_buffers",
                        p_train.get("num_buffers", cfg_vec.get("num_buffers", 1)))),
            },
            "env": config.get("env", {}),
        }
        trial_args = build_config(env_name, trial_config)
        trial_args['sweep'] = sweep_config

        # --- Metal addition: PFSP callback for osrs_pvp ---
        pfsp_state = None
        try:
            from ocean.osrs_pvp.pfsp import OPP_PFSP, POOL_TYPES, init_pfsp, update_pfsp
            if env_name == "osrs_pvp" and trial_args["env"].get("opponent_type", 0) == float(OPP_PFSP):
                total_agents = int(trial_args["vec"]["total_agents"])
                pfsp_initialized = [False]
                pfsp_state = {"total_agents": total_agents}

                def _pfsp_callback(pufferl_c, global_step):
                    if not pfsp_initialized[0]:
                        init_pfsp(pufferl_c, total_agents)
                        pfsp_state["cum_episodes"] = [0.0] * len(POOL_TYPES)
                        pfsp_state["last_update_step"] = 0
                        pfsp_initialized[0] = True
                    update_pfsp(pufferl_c, pfsp_state, global_step)

                trial_args['_metal'] = {'on_iteration': _pfsp_callback}
        except ImportError:
            pass

        if wandb_config:
            trial_args.setdefault('_metal', {})['wandb'] = {
                **wandb_config,
                'tag': f"trial-{trial_idx}",
            }

        # print trial header
        flat = dict(pufferlib.unroll_nested_dict(params))
        total_steps = int(flat.get("train/total_timesteps", 0))
        print(f"\n{'='*70}")
        print(f"trial {trial_idx}  ({total_steps/1e6:.1f}M steps)")
        for key, value in sorted(flat.items()):
            short_key = key.split("/")[-1]
            fmt = f"{value:.6f}" if isinstance(value, float) else str(value)
            print(f"  {short_key:20s} = {fmt}")
        print(f"{'='*70}")

        # run trial
        trial_start = time.time()
        try:
            validate_config(trial_args)
            pufferl, all_logs = _train_rank(trial_args, sweep_obj=protein, verbose=True)
            elapsed = time.time() - trial_start
        except (AssertionError, ValueError, RuntimeError) as e:
            print(f"  FAILED: {e}")
            protein.observe(params, 0.0, 1.0, is_failure=True)
            _save_observation({
                "trial": trial_idx, "params": params,
                "score": 0.0, "cost": 1.0, "step": 0,
                "is_failure": True, "mean_sps": 0,
            }, obs_path)
            trial_idx += 1
            trials_run += 1
            continue

        # extract scores from logs, observe into Protein
        if not all_logs or f'env/{score_key}' not in all_logs[-1]:
            print(f"  FAILED: no metric entries ({elapsed:.0f}s)")
            protein.observe(params, 0.0, 1.0, is_failure=True)
            _save_observation({
                "trial": trial_idx, "params": params,
                "score": 0.0, "cost": 1.0, "step": 0,
                "is_failure": True, "mean_sps": 0,
            }, obs_path)
        else:
            scores = [lg.get(f'env/{score_key}', lg.get('env/episode_return', 0)) for lg in all_logs]
            steps = [lg.get('agent_steps', 0) for lg in all_logs]
            sps_vals = [lg.get('SPS', 0) for lg in all_logs]

            ds_scores = downsample(scores, downsample_points)
            ds_steps = downsample(steps, downsample_points)

            for score, step in zip(ds_scores, ds_steps):
                obs_params = deepcopy(params)
                obs_params["train"]["total_timesteps"] = step
                cost = elapsed * (step / max(steps[-1], 1))
                protein.observe(obs_params, score, cost)
                _save_observation({
                    "trial": trial_idx, "params": obs_params,
                    "score": score, "cost": cost, "step": step,
                    "mean_sps": sum(sps_vals) / len(sps_vals) if sps_vals else 0,
                }, obs_path)

            final_score = scores[-1]
            mean_sps = sum(sps_vals) / len(sps_vals) if sps_vals else 0
            print(f"  DONE  score={final_score:.2f}  sps={mean_sps:.0f}  "
                  f"steps={steps[-1]/1e6:.1f}M  wall={elapsed:.0f}s")

            # write constellation-compatible JSON
            constellation_dir = Path("logs") / f"puffer_{env_name}"
            constellation_dir.mkdir(parents=True, exist_ok=True)
            n_bins = min(200, len(all_logs))
            if n_bins > 0:
                metrics_raw = {}
                for lg in all_logs:
                    for k, v in lg.items():
                        if isinstance(v, (int, float)):
                            metrics_raw.setdefault(k, []).append(float(v))
                metrics_ds = {k: [float(x) for x in downsample(v, n_bins)]
                              for k, v in metrics_raw.items()}
                trial_json = {**params, "sweep": sweep_config, "metrics": metrics_ds,
                              "env_name": env_name, "log_dir": str(constellation_dir)}
                with (constellation_dir / f"trial_{trial_idx}.json").open("w") as f:
                    json.dump(trial_json, f)

        pufferl.close()
        trial_idx += 1
        trials_run += 1

    _print_results(obs_path)


def _print_results(obs_path: Path) -> None:
    """Print sweep results from persisted observations."""
    records = _load_observations(obs_path)
    if not records:
        print("no observations found")
        return

    trials: dict[int, list[dict]] = {}
    for r in records:
        trials.setdefault(r["trial"], []).append(r)

    trial_summaries = []
    for tid, obs_list in sorted(trials.items()):
        best = max(obs_list, key=lambda o: o["step"])
        score = best.get("score", best.get("episode_return", 0))
        trial_summaries.append({
            "trial": tid, "score": score, "cost": best["cost"],
            "step": best["step"], "params": best["params"],
            "mean_sps": best.get("mean_sps", 0), "output": score,
        })

    print(f"\n{'='*70}")
    print(f"sweep results: {len(trial_summaries)} trials")
    print(f"{'='*70}")

    valid = [t for t in trial_summaries if t["step"] > 0]
    pareto, _ = pareto_points(valid)
    pruned = prune_pareto_front(pareto)
    pareto_ids = {t["trial"] for t in pruned}

    if pruned:
        best = pruned[-1]
        print(f"\nbest on pareto front: #{best['trial']}")
        print(f"  score: {best['score']:.2f}")
        print(f"  steps: {best['step']/1e6:.1f}M")
        flat = dict(pufferlib.unroll_nested_dict(best["params"]))
        print("\n  hyperparameters:")
        for key, value in sorted(flat.items()):
            short_key = key.split("/")[-1]
            fmt = f"{value:.6f}" if isinstance(value, float) else str(value)
            print(f"    {short_key:20s} = {fmt}")

    if len(pruned) > 1:
        print(f"\npareto frontier ({len(pruned)} points):")
        for t in reversed(pruned):
            flat = dict(pufferlib.unroll_nested_dict(t["params"]))
            hz = int(flat.get("train/horizon", 0))
            lr = flat.get("train/learning_rate", 0)
            ent = flat.get("train/ent_coef", 0)
            hs = int(flat.get("policy/hidden_size", 0))
            nl = int(flat.get("policy/num_layers", 0))
            sps = t.get("mean_sps", 0)
            sps_str = f"  sps={sps/1e6:.2f}M" if sps > 0 else ""
            print(f"  #{t['trial']:3d}  score={t['score']:>8.2f}  "
                  f"steps={t['step']/1e6:>5.1f}M  wall={t['cost']:>5.0f}s{sps_str}  "
                  f"hz={hz:>3}  lr={lr:.4f}  ent={ent:.4f}  hs={hs}  L{nl}")

    by_score = sorted(trial_summaries, key=lambda t: t["score"], reverse=True)
    print("\ntop 15 by score:")
    for t in by_score[:15]:
        flat = dict(pufferlib.unroll_nested_dict(t["params"]))
        hz = int(flat.get("train/horizon", 0))
        lr = flat.get("train/learning_rate", 0)
        ent = flat.get("train/ent_coef", 0)
        hs = int(flat.get("policy/hidden_size", 0))
        nl = int(flat.get("policy/num_layers", 0))
        sps = t.get("mean_sps", 0)
        sps_str = f"  sps={sps/1e6:.2f}M" if sps > 0 else ""
        is_pareto = " *" if t["trial"] in pareto_ids else ""
        print(f"  #{t['trial']:3d}  score={t['score']:>8.2f}  "
              f"steps={t['step']/1e6:>5.1f}M  wall={t['cost']:>5.0f}s{sps_str}  "
              f"hz={hz:>3}  lr={lr:.4f}  ent={ent:.4f}  hs={hs}  L{nl}{is_pareto}")


# ============================================================================
# eval + CLI dispatcher
# ============================================================================

def run_eval(env_name, args=None):
    """Load a trained checkpoint and render the agent."""
    config = args or load_config(env_name)
    if args is None:
        config = apply_cli_overrides(config, env_name)
    cli = config.pop("_cli", {})

    built = build_config(env_name, config)

    load_path = cli.get("load_model_path", "latest")
    if load_path == "latest":
        pattern = os.path.join("checkpoints", env_name, "**", "*.bin")
        candidates = glob.glob(pattern, recursive=True)
        if not candidates:
            raise FileNotFoundError(f"no checkpoints found in checkpoints/{env_name}/")
        load_path = max(candidates, key=os.path.getctime)

    pufferl = PuffeRL(built)
    pufferl.load_checkpoint(load_path)
    print(f"loaded weights from {load_path}")
    print(f"rendering env 0. ctrl+c to stop.")

    while True:
        _C.render(pufferl._pufferl, 0)
        pufferl.evaluate()


def main():
    if len(sys.argv) < 3:
        print("usage: python pufferl.py [train|sweep|eval|results] <env> [args]")
        sys.exit(1)

    mode = sys.argv.pop(1)
    env_name = sys.argv.pop(1)

    if mode == "train":
        train(env_name)
    elif mode == "sweep":
        sweep(env_name)
    elif mode == "eval":
        run_eval(env_name)
    elif mode == "results":
        _print_results(SWEEP_DIR_BASE / env_name / "observations.jsonl")
    else:
        print(f"unknown mode: {mode}. use train, sweep, eval, or results.")
        sys.exit(1)


if __name__ == "__main__":
    main()
