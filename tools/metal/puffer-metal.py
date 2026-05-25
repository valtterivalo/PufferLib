#!/usr/bin/env python
"""Run PufferLib through the local Metal overlay."""

from __future__ import annotations

import glob
import json
import os
import sys
from pathlib import Path


METAL_FLAGS = {
    "--metal-overlap": "PUFFER_METAL_OVERLAP",
    "--metal-cpu-inference": "PUFFER_METAL_CPU_INFERENCE",
    "--metal-train-fp16": "PUFFER_METAL_TRAIN_FP16",
}


def parse_flag_value(flag: str, value: str) -> str:
    if value not in {"0", "1"}:
        raise ValueError(f"{flag} must be 0 or 1")
    return value


def strip_metal_flags(argv: list[str]) -> list[str]:
    out = [argv[0]]
    index = 1
    while index < len(argv):
        arg = argv[index]
        if "=" in arg:
            flag, value = arg.split("=", 1)
            env_key = METAL_FLAGS.get(flag)
            if env_key:
                os.environ[env_key] = parse_flag_value(flag, value)
                index += 1
                continue

        env_key = METAL_FLAGS.get(arg)
        if env_key:
            if index + 1 >= len(argv):
                raise ValueError(f"{arg} requires 0 or 1")
            os.environ[env_key] = parse_flag_value(arg, argv[index + 1])
            index += 2
            continue

        out.append(arg)
        index += 1

    for env_key in METAL_FLAGS.values():
        os.environ.setdefault(env_key, "0")
    return out


def no_render_eval_requested(argv: list[str]) -> bool:
    if len(argv) < 3 or argv[1] != "eval":
        return False
    for index, arg in enumerate(argv):
        if arg == "--render-mode" and index + 1 < len(argv):
            return argv[index + 1] == "None"
        if arg == "--render-mode=None":
            return True
    return False


def arg_flag_present(argv: list[str], flag: str, min_prefix: str | None = None) -> bool:
    """Return whether a CLI flag is present in split, equals, or accepted abbreviated form."""
    for arg in argv:
        name = arg.split("=", 1)[0]
        if name == flag:
            return True
        if min_prefix and name.startswith(min_prefix) and flag.startswith(name):
            return True
    return False


def eval_total_agents(argv: list[str], env_name: str) -> int:
    """Parse eval config overrides and return total agent count."""
    from pufferlib.pufferl import load_config

    saved_argv = sys.argv
    try:
        sys.argv = [argv[0], *argv[3:]]
        args = load_config(env_name)
    finally:
        sys.argv = saved_argv
    return int(args["vec"]["total_agents"])


def ensure_eval_minibatch_size(argv: list[str]) -> None:
    """Keep interactive eval compatible with horizon=1."""
    if len(argv) < 3 or argv[1] != "eval":
        return
    if arg_flag_present(argv, "--train.minibatch-size", "--train.mini"):
        return
    total_agents = eval_total_agents(argv, argv[2])
    argv.extend(["--train.minibatch-size", str(total_agents)])


def run_no_render_eval(env_name: str) -> int:
    from pufferlib import _C as backend
    from pufferlib.pufferl import load_config, unroll_nested_dict

    args = load_config(env_name)
    args["reset_state"] = False
    args["train"]["horizon"] = 1
    args["train"]["minibatch_size"] = args["vec"]["total_agents"]

    pufferl = backend.create_pufferl(args)
    load_path = args.get("load_model_path")
    if load_path == "latest":
        pattern = os.path.join(
            args["checkpoint_dir"],
            args["env_name"],
            "**",
            "*.bin",
        )
        candidates = glob.glob(pattern, recursive=True)
        if not candidates:
            raise FileNotFoundError(
                f'No .bin checkpoints found in {args["checkpoint_dir"]}/{args["env_name"]}/'
            )
        load_path = max(candidates, key=os.path.getctime)

    if load_path:
        backend.load_weights(pufferl, load_path)
        print(f"Loaded weights from {load_path}")

    logs: dict[str, object] = {}
    while int(logs.get("env/n", 0)) < int(args["eval_episodes"]):
        backend.rollouts(pufferl)
        logs = dict(unroll_nested_dict(backend.eval_log(pufferl)))

    backend.close(pufferl)
    serializable = {
        key: float(value) if hasattr(value, "__float__") else value
        for key, value in logs.items()
    }
    print(json.dumps(serializable, sort_keys=True))
    return 0


def main() -> int:
    sys.argv = strip_metal_flags(sys.argv)
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root))
    if no_render_eval_requested(sys.argv):
        sys.argv.pop(1)
        env_name = sys.argv.pop(1)
        return run_no_render_eval(env_name)

    ensure_eval_minibatch_size(sys.argv)

    from pufferlib.pufferl import main as puffer_main

    return puffer_main()


if __name__ == "__main__":
    raise SystemExit(main())
