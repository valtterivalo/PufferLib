"""Re-export Phase2Demo files from existing archive.bin blobs.

Reads each /tmp/inferno_phase2/seed_*/archive.bin and writes top-K demos
in the new Phase2Demo format (header + root snapshot + actions).
Replaces the old play-replay-only files in /tmp/inferno_phase2/curriculum_demos/
and /tmp/inferno_phase2/bc_demos/.
"""

import argparse
import glob
import os
import shutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive-glob',
        default='/tmp/inferno_phase2/seed_*/archive.bin')
    parser.add_argument('--output-dir',
        default='/tmp/inferno_phase2/curriculum_demos')
    parser.add_argument('--bc-output-dir',
        default='/tmp/inferno_phase2/bc_demos')
    parser.add_argument('--demos-per-archive', type=int, default=64)
    parser.add_argument('--bc-demos-per-archive', type=int, default=32)
    parser.add_argument('--max-replay-ticks', type=int, default=8192)
    parser.add_argument('--clean', action='store_true', default=True,
        help='wipe output dirs before writing')
    args = parser.parse_args()

    from pufferlib.pufferl import _resolve_backend, load_config
    backend_args = load_config('osrs_inferno')
    backend = _resolve_backend(backend_args)

    archives = sorted(glob.glob(args.archive_glob))
    if not archives:
        raise SystemExit(f'no archives found at {args.archive_glob}')

    if args.clean:
        for d in (args.output_dir, args.bc_output_dir):
            if os.path.isdir(d):
                shutil.rmtree(d)
            os.makedirs(d, exist_ok=True)

    total_curriculum = 0
    total_bc = 0
    for archive_path in archives:
        seed_label = os.path.basename(os.path.dirname(archive_path))
        per_archive_dir = os.path.join(args.output_dir, f'_{seed_label}')
        per_archive_bc_dir = os.path.join(args.bc_output_dir, f'_{seed_label}')
        os.makedirs(per_archive_dir, exist_ok=True)
        os.makedirs(per_archive_bc_dir, exist_ok=True)

        n_curr = backend.reexport_demos_from_archive(
            archive_path=archive_path,
            demo_export_dir=per_archive_dir,
            demo_max_count=args.demos_per_archive,
            demo_max_replay_ticks=args.max_replay_ticks,
        )
        n_bc = backend.reexport_demos_from_archive(
            archive_path=archive_path,
            demo_export_dir=per_archive_bc_dir,
            demo_max_count=args.bc_demos_per_archive,
            demo_max_replay_ticks=args.max_replay_ticks,
        )
        total_curriculum += n_curr
        total_bc += n_bc
        print(f'{seed_label}: curriculum={n_curr} bc={n_bc}', flush=True)

    flatten(args.output_dir)
    flatten(args.bc_output_dir)

    print(f'\ntotal curriculum demos: {total_curriculum}')
    print(f'total bc demos: {total_bc}')
    print(f'curriculum dir: {args.output_dir}')
    print(f'bc dir: {args.bc_output_dir}')


def flatten(parent_dir):
    """Move all per-archive demo files up to parent and rename uniquely."""
    seen = 0
    for sub in sorted(os.listdir(parent_dir)):
        sub_path = os.path.join(parent_dir, sub)
        if not os.path.isdir(sub_path):
            continue
        for name in sorted(os.listdir(sub_path)):
            src = os.path.join(sub_path, name)
            stem, ext = os.path.splitext(name)
            new_name = f'demo_{seen:04d}_{stem.split("_", 2)[-1]}{ext}'
            dst = os.path.join(parent_dir, new_name)
            shutil.move(src, dst)
            seen += 1
        os.rmdir(sub_path)


if __name__ == '__main__':
    main()
