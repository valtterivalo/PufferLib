#!/usr/bin/env python3
"""Split an Emscripten data package into browser-loadable static chunks."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def remove_existing_chunks(data_path: Path) -> None:
    manifest_path = data_path.with_name(f"{data_path.name}.chunks.json")
    if manifest_path.exists():
        manifest_path.unlink()
    for chunk_path in data_path.parent.glob(f"{data_path.name}.part*"):
        chunk_path.unlink()


def write_chunks(data_path: Path, chunk_size: int, remove_source: bool) -> None:
    if chunk_size <= 0:
        raise SystemExit("chunk size must be positive")
    if not data_path.is_file():
        raise SystemExit(f"missing data package: {data_path}")

    remove_existing_chunks(data_path)
    total_size = data_path.stat().st_size
    chunk_names: list[str] = []

    with data_path.open("rb") as source:
        index = 0
        while True:
            chunk = source.read(chunk_size)
            if not chunk:
                break
            chunk_name = f"{data_path.name}.part{index:02d}"
            data_path.with_name(chunk_name).write_bytes(chunk)
            chunk_names.append(chunk_name)
            index += 1

    manifest = {
        "size": total_size,
        "chunks": chunk_names,
    }
    manifest_path = data_path.with_name(f"{data_path.name}.chunks.json")
    manifest_path.write_text(json.dumps(manifest, separators=(",", ":")), encoding="utf-8")

    if remove_source:
        data_path.unlink()


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data_path", type=Path)
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=45 * 1024 * 1024,
    )
    parser.add_argument("--remove-source", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    write_chunks(args.data_path, args.chunk_size, args.remove_source)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
