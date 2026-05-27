#!/usr/bin/env python3
"""Prepare the ignored OSRS web bundle for static hosting."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path


POLICIES = {
    "inferno": {
        "source": "osrs_inferno/xbeeukiz_repro/0000000160497664.bin",
        "target": "osrs_inferno_xbeeukiz.bin",
        "hidden_size": 512,
        "num_layers": 2,
        "encounter": "inferno",
        "default_wave": 69,
    },
    "zulrah": {
        "source": "osrs_zulrah/zulrah_best_1779475720116_repro/0000000038404096.bin",
        "target": "osrs_zulrah_best_1779475720116.bin",
        "hidden_size": 128,
        "num_layers": 2,
        "encounter": "zulrah",
        "default_tier": 0,
    },
    "pvp": {
        "source": "osrs_pvp/n46vl1vo_repro/latest.bin",
        "target": "osrs_pvp_n46vl1vo_repro.bin",
        "hidden_size": 2048,
        "num_layers": 2,
        "encounter": "pvp",
        "opponent_type": 26,
    },
}


def parse_args() -> argparse.Namespace:
    """Parse CLI arguments."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=Path("build_web/osrs_inferno"))
    parser.add_argument("--checkpoint-root", type=Path)
    parser.add_argument("--chunk-size", type=int, default=32 * 1024 * 1024)
    return parser.parse_args()


def default_checkpoint_root(repo_root: Path) -> Path:
    """Return the local checkpoint root used by this workstation."""
    candidates = []
    if value := os.environ.get("OSRS_CHECKPOINT_ROOT"):
        candidates.append(Path(value))
    candidates.append(repo_root / "checkpoints")
    candidates.append(Path.home() / "Projects/pufferlib-metal/checkpoints")
    for candidate in candidates:
        if all((candidate / str(spec["source"])).exists()
               for spec in POLICIES.values()):
            print(f"prepare-osrs-site: using checkpoint root {candidate}")
            return candidate
    roots = "\n".join(f"  {candidate}" for candidate in candidates)
    raise FileNotFoundError(f"missing selected policy checkpoints in:\n{roots}")


def file_sha256(path: Path) -> str:
    """Hash a file."""
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def remove_chunks(path: Path) -> None:
    """Remove stale chunk files for a split asset."""
    for chunk in path.parent.glob(f"{path.name}.part*"):
        chunk.unlink()
    manifest = path.with_name(f"{path.name}.chunks.json")
    if manifest.exists():
        manifest.unlink()


def split_binary_file(path: Path, chunk_size: int) -> list[Path]:
    """Split a binary file into cacheable chunks."""
    if chunk_size <= 0:
        raise ValueError("--chunk-size must be positive")
    remove_chunks(path)
    chunks = []
    size = path.stat().st_size
    with path.open("rb") as source:
        index = 0
        while data := source.read(chunk_size):
            chunk_path = path.with_name(f"{path.name}.part{index:02d}")
            chunk_path.write_bytes(data)
            chunks.append(chunk_path)
            index += 1
    manifest_path = path.with_name(f"{path.name}.chunks.json")
    manifest_path.write_text(json.dumps({
        "size": size,
        "chunks": [chunk.name for chunk in chunks],
    }, indent=2) + "\n")
    path.unlink()
    return [manifest_path, *chunks]


def copy_policy_models(
    out_dir: Path,
    checkpoint_root: Path,
    chunk_size: int,
) -> tuple[list[dict[str, object]], list[Path]]:
    """Copy selected policy checkpoints into the web bundle."""
    models_dir = out_dir / "models"
    models_dir.mkdir(parents=True, exist_ok=True)
    manifest = []
    version_paths = []
    for name, spec in POLICIES.items():
        source = checkpoint_root / str(spec["source"])
        if not source.exists():
            raise FileNotFoundError(source)
        target = models_dir / str(spec["target"])
        shutil.copy2(source, target)
        size = target.stat().st_size
        sha256 = file_sha256(target)
        if size > chunk_size:
            version_paths.extend(split_binary_file(target, chunk_size))
        else:
            remove_chunks(target)
            version_paths.append(target)
        row = {
            "name": name,
            "path": f"models/{target.name}",
            "bytes": size,
            "sha256": sha256,
            "hidden_size": spec["hidden_size"],
            "num_layers": spec["num_layers"],
            "encounter": spec["encounter"],
        }
        for key in ("default_wave", "default_tier", "opponent_type"):
            if key in spec:
                row[key] = spec[key]
        manifest.append(row)
    manifest_path = models_dir / "policies.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    version_paths.append(manifest_path)
    return manifest, version_paths


def split_data_file(out_dir: Path, chunk_size: int) -> list[Path]:
    """Split Emscripten's data package into cacheable chunks."""
    data_path = out_dir / "game.data"
    if not data_path.exists():
        paths = sorted(out_dir.glob("game.data.part*"))
        manifest = out_dir / "game.data.chunks.json"
        if manifest.exists():
            paths.append(manifest)
        return paths
    return split_binary_file(data_path, chunk_size)


def replace_asset_version(out_dir: Path, paths: list[Path]) -> str:
    """Stamp the shell with a content hash."""
    digest = hashlib.sha256()
    for path in sorted(paths):
        if path.exists() and path.name != "game.html":
            digest.update(path.name.encode())
            digest.update(file_sha256(path).encode())
    version = digest.hexdigest()[:16]
    html_path = out_dir / "game.html"
    html = html_path.read_text()
    html = html.replace("__OSRS_ASSET_VERSION__", version)
    html = html.replace('src="game.js"', f'src="game.js?v={version}"')
    html = html.replace("src='game.js'", f"src='game.js?v={version}'")
    html = html.replace("src=game.js", f"src=game.js?v={version}")
    html_path.write_text(html)
    return version


def write_rl_entrypoint(out_dir: Path) -> None:
    """Write the /rl/ static entrypoint."""
    html = (out_dir / "game.html").read_text()
    html = html.replace('src="game.js', 'src="../game.js')
    html = html.replace("src='game.js", "src='../game.js")
    html = html.replace("src=game.js", "src=../game.js")
    route_dir = out_dir / "rl"
    route_dir.mkdir(exist_ok=True)
    (route_dir / "index.html").write_text(html)


def main() -> None:
    """Run the preparation pipeline."""
    args = parse_args()
    repo_root = Path.cwd()
    out_dir = args.out
    checkpoint_root = args.checkpoint_root or default_checkpoint_root(repo_root)
    if not checkpoint_root.exists():
        raise FileNotFoundError(checkpoint_root)
    manifest, model_version_paths = copy_policy_models(
        out_dir, checkpoint_root, args.chunk_size)
    chunks = split_data_file(out_dir, args.chunk_size)
    version_paths = [
        out_dir / "game.js",
        out_dir / "game.wasm",
        out_dir / "game.wasm.map",
        *model_version_paths,
        *chunks,
    ]
    version = replace_asset_version(out_dir, version_paths)
    write_rl_entrypoint(out_dir)
    print(f"prepare-osrs-site: wrote {out_dir}")
    print(f"prepare-osrs-site: wrote {out_dir / 'rl/index.html'}")
    print(f"prepare-osrs-site: asset version {version}")


if __name__ == "__main__":
    main()
