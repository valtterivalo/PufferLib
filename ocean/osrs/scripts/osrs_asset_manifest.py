#!/usr/bin/env python3
"""Load and validate the OSRS asset install manifest."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import sys


EXPECTED_FORMAT = "puffer-osrs-asset-manifest-v1"
EXPECTED_ASSET_VERSION = "osrs-assets-v21"
EXPECTED_ARCHIVE_NAME = "osrs-assets-v21.tar.gz"
EXPECTED_ARCHIVE_URL = (
    "https://github.com/valtterivalo/PufferLib/releases/download/"
    "osrs-assets-v21/osrs-assets-v21.tar.gz"
)
EXPECTED_ARCHIVE_SHA256 = "2f770543618a113fee806be6ec1217432904b0ed9f9d8b16872cd05274f81874"


@dataclass(frozen=True)
class AssetArchive:
    name: str
    url: str
    sha256: str
    strip_components: int


@dataclass(frozen=True)
class RequiredGroup:
    name: str
    files: tuple[str, ...]


@dataclass(frozen=True)
class AssetManifest:
    format: str
    asset_version: str
    archive: AssetArchive
    required_groups: tuple[RequiredGroup, ...]


def path_is_safe(path: str | None) -> bool:
    if not path:
        return False
    if path.startswith("/") or "\\" in path:
        return False
    return all(part not in ("", ".", "..") for part in path.split("/"))


def expect_str(raw: object, key: str, context: str) -> str:
    if not isinstance(raw, dict):
        raise SystemExit(f"setup-osrs-data: {context} must be an object")
    value = raw.get(key)
    if not isinstance(value, str) or not value:
        raise SystemExit(f"setup-osrs-data: {context}.{key} must be a string")
    return value


def expect_int(raw: object, key: str, context: str) -> int:
    if not isinstance(raw, dict):
        raise SystemExit(f"setup-osrs-data: {context} must be an object")
    value = raw.get(key)
    if not isinstance(value, int):
        raise SystemExit(f"setup-osrs-data: {context}.{key} must be an integer")
    return value


def load_manifest(path: Path) -> AssetManifest:
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise SystemExit("setup-osrs-data: manifest must be an object")

    manifest_format = expect_str(raw, "format", "manifest")
    if manifest_format != EXPECTED_FORMAT:
        raise SystemExit(f"setup-osrs-data: unsupported manifest format: {manifest_format}")
    asset_version = expect_str(raw, "asset_version", "manifest")
    if asset_version != EXPECTED_ASSET_VERSION:
        raise SystemExit(f"setup-osrs-data: unexpected asset version: {asset_version}")

    archive_raw = raw.get("archive")
    archive = AssetArchive(
        name=expect_str(archive_raw, "name", "archive"),
        url=expect_str(archive_raw, "url", "archive"),
        sha256=expect_str(archive_raw, "sha256", "archive"),
        strip_components=expect_int(archive_raw, "strip_components", "archive"),
    )
    if archive.name != EXPECTED_ARCHIVE_NAME:
        raise SystemExit(f"setup-osrs-data: unexpected archive name: {archive.name}")
    if archive.url != EXPECTED_ARCHIVE_URL:
        raise SystemExit(f"setup-osrs-data: unexpected archive URL: {archive.url}")
    if archive.sha256 != EXPECTED_ARCHIVE_SHA256:
        raise SystemExit(f"setup-osrs-data: unexpected archive SHA256: {archive.sha256}")
    if archive.strip_components < 0:
        raise SystemExit("setup-osrs-data: archive strip_components must be nonnegative")

    groups_raw = raw.get("required_groups")
    if not isinstance(groups_raw, list) or not groups_raw:
        raise SystemExit("setup-osrs-data: required_groups must be a nonempty list")

    groups: list[RequiredGroup] = []
    seen_group_names: set[str] = set()
    for group_raw in groups_raw:
        name = expect_str(group_raw, "name", "required_group")
        if name in seen_group_names:
            raise SystemExit(f"setup-osrs-data: repeated required group: {name}")
        seen_group_names.add(name)

        files_raw = group_raw.get("files") if isinstance(group_raw, dict) else None
        if not isinstance(files_raw, list) or not files_raw:
            raise SystemExit(f"setup-osrs-data: group {name} has no files")
        files: list[str] = []
        seen_files: set[str] = set()
        for file_raw in files_raw:
            if not isinstance(file_raw, str) or not path_is_safe(file_raw):
                raise SystemExit(f"setup-osrs-data: unsafe asset path in group {name}: {file_raw}")
            if file_raw in seen_files:
                raise SystemExit(f"setup-osrs-data: repeated asset path in group {name}: {file_raw}")
            seen_files.add(file_raw)
            files.append(file_raw)
        groups.append(RequiredGroup(name=name, files=tuple(files)))

    return AssetManifest(
        format=manifest_format,
        asset_version=asset_version,
        archive=archive,
        required_groups=tuple(groups),
    )


def missing_required_files(manifest: AssetManifest, data_dir: Path) -> list[tuple[str, str]]:
    missing: list[tuple[str, str]] = []
    for group in manifest.required_groups:
        for asset_path in group.files:
            if not (data_dir / asset_path).is_file():
                missing.append((group.name, asset_path))
    return missing


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="validate OSRS asset manifests")
    subparsers = parser.add_subparsers(dest="command", required=True)

    archive_parser = subparsers.add_parser("archive-tsv")
    archive_parser.add_argument("manifest", type=Path)

    missing_parser = subparsers.add_parser("missing-required")
    missing_parser.add_argument("manifest", type=Path)
    missing_parser.add_argument("data_dir", type=Path)

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("manifest", type=Path)

    args = parser.parse_args(argv)
    manifest = load_manifest(args.manifest)

    if args.command == "archive-tsv":
        archive = manifest.archive
        print(f"{archive.name}\t{archive.url}\t{archive.sha256}\t{archive.strip_components}")
        return 0

    if args.command == "missing-required":
        missing = missing_required_files(manifest, args.data_dir)
        for group_name, asset_path in missing:
            print(f"{group_name}\t{asset_path}")
        return 1 if missing else 0

    if args.command == "validate":
        return 0

    raise AssertionError(args.command)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
