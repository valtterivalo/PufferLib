#!/usr/bin/env python
"""Run comparable OSRS eval traces across CUDA and Metal."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


ACTION_MODES = {
    "sample": 0,
    "argmax": 1,
    "shared_sample": 2,
    "shared-sample": 2,
    "0": 0,
    "1": 1,
    "2": 2,
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def stable_json(data: Any) -> str:
    def normalize(value: Any) -> Any:
        if isinstance(value, defaultdict):
            value = dict(value)
        if isinstance(value, dict):
            return {str(k): normalize(v) for k, v in sorted(value.items())}
        if isinstance(value, (list, tuple)):
            return [normalize(v) for v in value]
        if isinstance(value, (str, int, float, bool)) or value is None:
            return value
        return repr(value)

    return json.dumps(normalize(data), sort_keys=True, separators=(",", ":"))


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def deep_merge(base: dict[str, Any], update: dict[str, Any]) -> dict[str, Any]:
    out = json.loads(json.dumps(base))
    for key, value in update.items():
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = deep_merge(out[key], value)
        else:
            out[key] = value
    return out


def missing_paths(defaults: dict[str, Any], update: dict[str, Any], prefix: str = "") -> list[str]:
    out: list[str] = []
    for key, value in defaults.items():
        path = f"{prefix}.{key}" if prefix else key
        if key not in update:
            if isinstance(value, dict):
                out.extend(missing_paths(value, {}, path))
            else:
                out.append(path)
            continue
        if isinstance(value, dict) and isinstance(update[key], dict):
            out.extend(missing_paths(value, update[key], path))
    return out


def load_clean_default_config(env_name: str) -> dict[str, Any]:
    from pufferlib.pufferl import load_config

    saved_argv = sys.argv[:]
    sys.argv = [saved_argv[0]]
    args = load_config(env_name)
    sys.argv = saved_argv
    return args


def extract_config_payload(raw: dict[str, Any]) -> dict[str, Any]:
    if "args" in raw and isinstance(raw["args"], dict):
        return raw["args"]
    if {"env", "policy", "train", "vec"}.issubset(raw.keys()):
        return {
            key: raw[key]
            for key in raw
            if key in {"base", "env", "policy", "train", "vec", "sweep"}
            or not isinstance(raw[key], dict)
        }
    return raw


def load_config(env_name: str, path: Path | None) -> tuple[dict[str, Any], list[str]]:
    defaults = load_clean_default_config(env_name)
    if path is None:
        return defaults, []

    raw = json.loads(path.read_text())
    payload = extract_config_payload(raw)
    filled = missing_paths(defaults, payload)
    return deep_merge(defaults, payload), filled


def env_source_hash(env_name: str) -> str:
    root = repo_root()
    files: list[Path] = []
    if env_name == "osrs_inferno":
        globs = [
            "ocean/osrs_inferno/*.c",
            "ocean/osrs/encounters/encounter_inferno.h",
            "ocean/osrs/encounters/inferno/*.inc",
            "ocean/osrs/osrs_encounter*.h",
        ]
    elif env_name == "osrs_pvp":
        globs = [
            "ocean/osrs_pvp/*.c",
            "ocean/osrs/osrs_pvp*.h",
            "ocean/osrs/osrs_encounter*.h",
        ]
    else:
        globs = [f"ocean/{env_name}/*.c"]

    for pattern in globs:
        files.extend(root.glob(pattern))

    h = hashlib.sha256()
    for path in sorted(set(files)):
        if path.is_file():
            rel = path.relative_to(root).as_posix()
            h.update(rel.encode())
            h.update(b"\0")
            h.update(path.read_bytes())
            h.update(b"\0")
    return h.hexdigest()


def flat_dict(data: dict[str, Any]) -> dict[str, Any]:
    from pufferlib.pufferl import unroll_nested_dict

    return dict(unroll_nested_dict(data))


def backend_parity_hashes(backend: Any, pufferl: Any) -> dict[str, Any]:
    return dict(backend.parity_hashes(pufferl)) if hasattr(backend, "parity_hashes") else {}


def backend_policy_debug_sample(backend: Any, pufferl: Any) -> dict[str, Any]:
    return dict(backend.policy_debug_sample(pufferl)) if hasattr(backend, "policy_debug_sample") else {}


def backend_env_debug_sample(backend: Any, pufferl: Any) -> dict[str, Any]:
    return dict(backend.env_debug_sample(pufferl)) if hasattr(backend, "env_debug_sample") else {}


def backend_env_obs_row_hashes(backend: Any, pufferl: Any, row_limit: int) -> list[str]:
    if row_limit <= 0 or not hasattr(backend, "env_obs_row_hashes"):
        return []
    return list(backend.env_obs_row_hashes(pufferl, row_limit))


def env_only_hashes(hashes: dict[str, Any]) -> dict[str, Any]:
    return {key: value for key, value in hashes.items() if key.startswith("env_")}


def run_rollout(args: argparse.Namespace) -> None:
    root = repo_root()
    sys.path.insert(0, str(root))

    from pufferlib import _C as backend

    cfg, filled = load_config(args.env_name, args.config)
    action_mode = ACTION_MODES[args.eval_action_mode]
    cfg["env_name"] = args.env_name
    cfg["render_mode"] = "None"
    cfg["wandb"] = False
    cfg["reset_state"] = False
    cfg["eval_action_mode"] = action_mode
    cfg["eval_episodes"] = int(args.episodes)
    cfg["train"]["horizon"] = 1
    cfg["train"]["minibatch_size"] = cfg["vec"]["total_agents"]
    if args.seed is not None:
        cfg["seed"] = int(args.seed)
        cfg["env"]["seed"] = int(args.seed)
    if action_mode == 2:
        cfg["cudagraphs"] = -1

    pufferl = backend.create_pufferl(cfg)
    backend.load_weights(pufferl, str(args.checkpoint))

    traces: list[dict[str, Any]] = []
    logs: dict[str, Any] = {}
    rollout_index = 0
    initial_hashes = env_only_hashes(backend_parity_hashes(backend, pufferl))
    initial_debug = backend_env_debug_sample(backend, pufferl)
    while (
        rollout_index < int(args.rollouts)
        if args.rollouts is not None
        else int(logs.get("env/n", 0)) < int(args.episodes)
    ):
        backend.rollouts(pufferl)
        logs = flat_dict(backend.eval_log(pufferl))
        if rollout_index < args.record_hashes:
            hashes = backend.rollout_hashes(pufferl) if hasattr(backend, "rollout_hashes") else {}
            traces.append({
                "rollout": rollout_index,
                "global_step": int(pufferl.global_step),
                "env_n": int(logs.get("env/n", 0)),
                "hashes": dict(hashes),
                "parity_hashes": backend_parity_hashes(backend, pufferl),
                "policy_debug": backend_policy_debug_sample(backend, pufferl),
                "env_obs_row_hashes": backend_env_obs_row_hashes(
                    backend, pufferl, args.record_row_hashes),
            })
        rollout_index += 1

    num_params = int(pufferl.num_params()) if hasattr(pufferl, "num_params") else None
    backend.close(pufferl)

    config_text = stable_json(cfg)
    result = {
        "kind": "osrs_eval_parity_rollout",
        "backend": args.backend_label,
        "compiled_env_name": getattr(backend, "env_name", "unknown"),
        "checkpoint_path": str(args.checkpoint.resolve()),
        "checkpoint_sha256": sha256_file(args.checkpoint),
        "config_path": str(args.config.resolve()) if args.config else None,
        "config_sha256": sha256_bytes(config_text.encode()),
        "filled_config_keys": filled,
        "env_source_hash": env_source_hash(args.env_name),
        "seed": cfg.get("seed"),
        "action_mode": action_mode,
        "action_mode_name": args.eval_action_mode,
        "episodes": int(args.episodes),
        "obs_size": int(backend.env_obs_size()) if hasattr(backend, "env_obs_size") else None,
        "action_dims": list(backend.env_action_dims()) if hasattr(backend, "env_action_dims") else None,
        "num_params": num_params,
        "logs": logs,
        "initial_hashes": initial_hashes,
        "initial_debug": initial_debug,
        "traces": traces,
    }
    args.write_json.parent.mkdir(parents=True, exist_ok=True)
    args.write_json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")


def compare_float(name: str, left: Any, right: Any, tolerance: float) -> None:
    l = float(left)
    r = float(right)
    if abs(l - r) > tolerance:
        raise AssertionError(f"{name}: {l} != {r} with tolerance {tolerance}")


def compare_outputs(args: argparse.Namespace) -> None:
    left = json.loads(args.left.read_text())
    right = json.loads(args.right.read_text())
    strict_keys = ["checkpoint_sha256", "config_sha256", "env_source_hash", "seed", "action_mode"]
    for key in strict_keys:
        if left.get(key) != right.get(key):
            raise AssertionError(f"{key}: {left.get(key)} != {right.get(key)}")

    for key in args.metric:
        if key not in left["logs"] and key not in right["logs"]:
            continue
        if key not in left["logs"] or key not in right["logs"]:
            raise AssertionError(f"missing metric {key}")
        compare_float(key, left["logs"][key], right["logs"][key], args.tolerance)

    if env_only_hashes(left.get("initial_hashes", {})) != env_only_hashes(right.get("initial_hashes", {})):
        raise AssertionError("initial parity hash mismatch")

    trace_count = min(len(left["traces"]), len(right["traces"]))
    for i in range(trace_count):
        left_hashes = left["traces"][i].get("parity_hashes") or left["traces"][i]["hashes"]
        right_hashes = right["traces"][i].get("parity_hashes") or right["traces"][i]["hashes"]
        if left_hashes != right_hashes:
            row_msg = ""
            left_rows = left["traces"][i].get("env_obs_row_hashes", [])
            right_rows = right["traces"][i].get("env_obs_row_hashes", [])
            for row_idx, (lrow, rrow) in enumerate(zip(left_rows, right_rows)):
                if lrow != rrow:
                    row_msg = f", first env obs row mismatch {row_idx}"
                    break
            raise AssertionError(f"rollout parity hash mismatch at trace {i}{row_msg}")

    print("OSRS eval parity outputs match")


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)

    rollout = sub.add_parser("rollout")
    rollout.add_argument("--env-name", required=True)
    rollout.add_argument("--checkpoint", type=Path, required=True)
    rollout.add_argument("--config", type=Path)
    rollout.add_argument("--episodes", type=int, default=4096)
    rollout.add_argument("--rollouts", type=int)
    rollout.add_argument("--eval-action-mode", choices=sorted(ACTION_MODES), default="argmax")
    rollout.add_argument("--seed", type=int)
    rollout.add_argument("--record-hashes", type=int, default=8)
    rollout.add_argument("--record-row-hashes", type=int, default=0)
    rollout.add_argument("--backend-label", default="unknown")
    rollout.add_argument("--write-json", type=Path, required=True)
    rollout.set_defaults(func=run_rollout)

    compare = sub.add_parser("compare")
    compare.add_argument("left", type=Path)
    compare.add_argument("right", type=Path)
    compare.add_argument("--metric", action="append", default=["env/n", "env/wins", "env/score"])
    compare.add_argument("--tolerance", type=float, default=1e-6)
    compare.set_defaults(func=compare_outputs)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
