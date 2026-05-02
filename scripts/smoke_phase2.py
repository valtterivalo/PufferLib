"""Smoke-test phase 2: load demos, build ladders, run rollouts with curriculum active."""

import argparse
import os
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--env', default='osrs_inferno')
    parser.add_argument('--checkpoint', required=True)
    parser.add_argument('--demo-dir', default='/tmp/inferno_phase2/curriculum_demos')
    parser.add_argument('--num-rollouts', type=int, default=3)
    parser.add_argument('--snapshot-stride', type=int, default=4)
    parser.add_argument('--max-demos', type=int, default=64)
    parser.add_argument('--normal-start-frac', type=float, default=0.25)
    parser.add_argument('--randomize-rng-frac', type=float, default=0.25)
    own_args = parser.parse_args()

    sys.argv = [sys.argv[0]]

    from pufferlib.pufferl import load_config, _resolve_backend, _inferno_replay_env

    args = load_config(own_args.env)
    backend = _resolve_backend(args)
    num_atns = 9

    with _inferno_replay_env(args):
        pufferl = backend.create_pufferl(args)
        backend.load_weights(pufferl, own_args.checkpoint)
        print(f'Loaded weights from {own_args.checkpoint}', flush=True)

        n_demos = backend.phase2_init(
            pufferl,
            demo_dir=own_args.demo_dir,
            num_atns=num_atns,
            snapshot_stride=own_args.snapshot_stride,
            max_demos=own_args.max_demos,
            seed=42,
            normal_start_frac=own_args.normal_start_frac,
            randomize_future_rng_frac=own_args.randomize_rng_frac,
        )
        print(f'phase2_init: {n_demos} demos', flush=True)

        for r in range(own_args.num_rollouts):
            backend.rollouts(pufferl)
            print(f'rollout {r}: ok', flush=True)

        backend.close(pufferl)
        print('smoke test passed', flush=True)


if __name__ == '__main__':
    main()
