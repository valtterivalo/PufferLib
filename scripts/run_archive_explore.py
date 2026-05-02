"""Multi-seed offline archive exploration. Run from the inferno-sync repo root."""

import argparse
import os
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--env', default='osrs_inferno')
    parser.add_argument('--checkpoint', required=True)
    parser.add_argument('--output-dir', default='/tmp/inferno_phase2')
    parser.add_argument('--num-iterations', type=int, default=50)
    parser.add_argument('--archive-capacity', type=int, default=300000)
    parser.add_argument('--num-seeds', type=int, default=4)
    parser.add_argument('--start-seed', type=int, default=42)
    parser.add_argument('--demo-max-count', type=int, default=200)
    parser.add_argument('--demo-max-replay-ticks', type=int, default=8192)
    parser.add_argument('--action-chunk-pool-capacity-ints', type=int, default=20_000_000,
                        help='Pool size for action chunks. Bump if you hit drops.')
    own_args = parser.parse_args()

    sys.argv = [sys.argv[0]]

    from pufferlib.pufferl import load_config, _resolve_backend, _inferno_replay_env

    args = load_config(own_args.env)
    backend = _resolve_backend(args)

    os.makedirs(own_args.output_dir, exist_ok=True)

    summaries = []
    with _inferno_replay_env(args):
        pufferl = backend.create_pufferl(args)

        backend.load_weights(pufferl, own_args.checkpoint)
        print(f'Loaded weights from {own_args.checkpoint}', flush=True)

        for i in range(own_args.num_seeds):
            seed = own_args.start_seed + i
            seed_dir = os.path.join(own_args.output_dir, f'seed_{seed}')
            demo_dir = os.path.join(seed_dir, 'demos')
            os.makedirs(demo_dir, exist_ok=True)
            archive_path = os.path.join(seed_dir, 'archive.bin')

            print(f'\n--- seed {seed} (iter={own_args.num_iterations}, '
                  f'cap={own_args.archive_capacity}) ---', flush=True)

            stats = backend.archive_explore(
                pufferl,
                archive_capacity=own_args.archive_capacity,
                num_iterations=own_args.num_iterations,
                action_chunk_pool_capacity_ints=own_args.action_chunk_pool_capacity_ints,
                archive_seed=seed,
                archive_save_path=archive_path,
                demo_export_dir=demo_dir,
                demo_max_count=own_args.demo_max_count,
                demo_max_replay_ticks=own_args.demo_max_replay_ticks,
            )
            print(f'seed={seed}: {dict(stats)}', flush=True)
            summaries.append((seed, dict(stats)))

        backend.close(pufferl)

    print('\n=== summary ===', flush=True)
    for seed, stats in summaries:
        print(f'  seed={seed}: archive_size={stats["archive_size"]}, '
              f'new={stats["total_new_cells"]}, '
              f'demos={stats["demos_exported"]}, '
              f'dropped={stats["total_dropped"]}, '
              f'wall={stats["wall_seconds"]:.1f}s', flush=True)


if __name__ == '__main__':
    main()
