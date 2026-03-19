#!/usr/bin/env python3
"""pufferl — unified CLI for Metal RL training.

usage:
    python pufferl.py train <env> [args]     single training run
    python pufferl.py sweep <env> [args]     Protein hyperparameter sweep
    python pufferl.py results <env>          print sweep results

requires building the env first: python setup.py build_<env> --force

examples:
    python pufferl.py train breakout --total-timesteps 200000000
    python pufferl.py train osrs_inferno --hidden-size 256 --num-layers 2
    python pufferl.py sweep breakout --timeout 6
    python pufferl.py sweep osrs_pvp --timeout 8
    python pufferl.py results breakout
"""

import sys


def main():
    if len(sys.argv) < 3:
        print("usage: python pufferl.py [train|sweep|results] <env> [args]")
        print("  train   — single training run (was bench.py)")
        print("  sweep   — Protein hyperparameter sweep (was sweep_bench.py)")
        print("  results — print sweep results (was sweep_bench.py --results)")
        sys.exit(1)

    mode = sys.argv.pop(1)

    if mode == "train":
        from bench import main as train_main
        train_main()
    elif mode == "sweep":
        from sweep_bench import main as sweep_main
        sweep_main()
    elif mode == "results":
        env = sys.argv[1] if len(sys.argv) > 1 else None
        if not env:
            print("usage: python pufferl.py results <env>")
            sys.exit(1)
        sys.argv = [sys.argv[0], "--env", env, "--results"]
        from sweep_bench import main as sweep_main
        sweep_main()
    else:
        print(f"unknown mode: {mode}. use train, sweep, or results.")
        sys.exit(1)


if __name__ == "__main__":
    main()
