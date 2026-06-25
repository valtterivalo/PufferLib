"""OSRS asset manifest tooling invariants."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import tarfile


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = REPO_ROOT / "ocean/osrs/scripts/osrs_asset_manifest.py"


def load_tool():
    """Load the asset manifest tool as a test module."""
    spec = importlib.util.spec_from_file_location("osrs_asset_manifest", TOOL_PATH)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def test_generated_header_matches_manifest():
    """Confirm tracked C asset metadata is generated from the manifest."""
    tool = load_tool()
    manifest = tool.load_manifest(REPO_ROOT / "ocean/osrs/asset_manifest.json")
    header = (REPO_ROOT / "ocean/osrs/osrs_assets_generated.h").read_text(
        encoding="utf-8"
    )

    assert header == tool.generated_header_text(manifest)
    assert "npc_models_colosseum.h" in header


def test_package_archive_is_deterministic_and_excludes_local_junk(tmp_path):
    """Confirm package creation is stable and ignores local cache noise."""
    tool = load_tool()
    data_dir = tmp_path / "data"
    dist_dir = tmp_path / "dist"
    (data_dir / "dir").mkdir(parents=True)
    (data_dir / ".download").mkdir()
    (data_dir / "__MACOSX").mkdir()
    (data_dir / "a.bin").write_bytes(b"a")
    (data_dir / "dir/b.bin").write_bytes(b"b")
    (data_dir / ".download/cache.bin").write_bytes(b"cache")
    (data_dir / ".DS_Store").write_bytes(b"junk")
    (data_dir / "._appledouble").write_bytes(b"junk")
    (data_dir / "__MACOSX/ignored").write_bytes(b"junk")

    manifest_path = tmp_path / "manifest.json"
    manifest_path.write_text(
        json.dumps(
            {
                "format": "puffer-osrs-asset-manifest-v1",
                "asset_version": "osrs-assets-test",
                "archive": {
                    "name": "osrs-assets-test.tar.gz",
                    "url": "https://example.test/osrs-assets-test.tar.gz",
                    "sha256": "0" * 64,
                    "strip_components": 1,
                },
                "required_groups": [
                    {"name": "core", "files": ["a.bin", "dir/b.bin"]},
                ],
            }
        )
        + "\n",
        encoding="utf-8",
    )
    manifest = tool.load_manifest(manifest_path)

    archive = tool.package_archive(manifest, data_dir, dist_dir)
    first_sha = tool.sha256_file(archive)
    archive = tool.package_archive(manifest, data_dir, dist_dir)
    second_sha = tool.sha256_file(archive)

    assert first_sha == second_sha
    with tarfile.open(archive, "r:gz") as tar:
        names = set(tar.getnames())
    assert names == {
        "osrs-assets-test/a.bin",
        "osrs-assets-test/dir/b.bin",
    }
