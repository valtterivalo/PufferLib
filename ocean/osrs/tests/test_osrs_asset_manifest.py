#!/usr/bin/env python3
"""Validate the OSRS asset install manifest."""

from __future__ import annotations

import json
import sys
import tempfile
from importlib import import_module
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
OSRS_ROOT = ROOT / "ocean" / "osrs"
sys.path.insert(0, str(OSRS_ROOT / "scripts"))

asset_manifest = import_module("osrs_asset_manifest")


def assert_true(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    manifest = asset_manifest.load_manifest(OSRS_ROOT / "asset_manifest.json")
    assert_true(manifest.format == asset_manifest.EXPECTED_FORMAT, "manifest format drifted")
    assert_true(
        manifest.asset_version == asset_manifest.EXPECTED_ASSET_VERSION,
        "asset version drifted",
    )
    assert_true(manifest.archive.url == asset_manifest.EXPECTED_ARCHIVE_URL, "asset URL drifted")
    assert_true(
        manifest.archive.sha256 == asset_manifest.EXPECTED_ARCHIVE_SHA256,
        "asset digest drifted",
    )
    assert_true(manifest.archive.strip_components == 1, "tar strip count drifted")

    group_names = {group.name for group in manifest.required_groups}
    assert_true(
        {
            "core",
            "inferno",
            "zulrah",
            "gui",
            "items",
            "headers",
            "combat_visuals",
            "wilderness",
            "pvp",
        } <= group_names,
        "required asset groups are incomplete",
    )

    for group in manifest.required_groups:
        assert_true(group.files, f"group {group.name} has no files")
        assert_true(len(group.files) == len(set(group.files)), f"group {group.name} repeats files")
        for asset_path in group.files:
            assert_true(asset_manifest.path_is_safe(asset_path), f"unsafe manifest path: {asset_path}")

    assert_true(
        not asset_manifest.path_is_safe("/tmp/osrs-data/equipment.models"),
        "absolute path accepted",
    )
    assert_true(not asset_manifest.path_is_safe("../equipment.models"), "parent path accepted")
    assert_true(
        not asset_manifest.path_is_safe("sprites//gui/compass.png"),
        "empty path part accepted",
    )
    assert_true(
        not asset_manifest.path_is_safe("sprites\\gui\\compass.png"),
        "backslash path accepted",
    )

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        empty_data = tmp_path / "empty-data"
        empty_data.mkdir()
        missing = asset_manifest.missing_required_files(manifest, empty_data)
        assert_true(missing[0] == ("core", "equipment.models"), "missing path order drifted")

        bad_manifest_path = tmp_path / "bad-digest.json"
        raw_manifest = json.loads((OSRS_ROOT / "asset_manifest.json").read_text(encoding="utf-8"))
        raw_manifest["archive"]["sha256"] = "0" * 64
        bad_manifest_path.write_text(json.dumps(raw_manifest), encoding="utf-8")
        try:
            asset_manifest.load_manifest(bad_manifest_path)
        except SystemExit as exc:
            assert_true("unexpected archive SHA256" in str(exc), "bad digest error drifted")
        else:
            raise AssertionError("bad digest accepted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
