import argparse
import re
import struct
import sys
import tempfile
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "refs/RuneC/tools/cache_pipeline"))
from export_animations import export_animations_from_modern_cache


def animation_records(path):
    data = path.read_bytes()
    magic, version, size, bases, sequences, frames, flags = struct.unpack_from("<4sHHIIII", data)
    assert (magic, version, size) == (b"ANM2", 2, 24)
    pos = size
    base_rows, sequence_rows = {}, {}
    for _ in range(bases):
        start = pos
        key, slots = struct.unpack_from("<HB", data, pos)
        pos += 3 + slots
        for _ in range(slots):
            pos += 1 + data[pos]
        base_rows[key] = data[start:pos]
    for _ in range(sequences):
        start = pos
        key, count, interleave = struct.unpack_from("<HHB", data, pos)
        pos += 6 + interleave
        for _ in range(count):
            transforms = data[pos + 4]
            pos += 5 + 7 * transforms
        sequence_rows[key] = data[start:pos]
    assert pos == len(data)
    return base_rows, sequence_rows, frames, flags


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    configs = tomllib.loads((ROOT / "refs/rsmod-data/obj-enricher.toml").read_text())["config"]
    configs = {config["obj"]: config for config in configs}
    weapons = {4718: "barrows_dharok_weapon", 24225: "granite_maul_plus"}
    stances = {item: tuple(configs[name].get(field, 0xffffffff)
        for field in ("ready_anim", "walk_anim", "run_anim"))
        for item, name in weapons.items()}
    data_dir = ROOT / "ocean/osrs/data"
    target = data_dir / "equipment.anims"
    bases, sequences, frames, flags = animation_records(target)
    missing = {seq for stance in stances.values() for seq in stance
        if seq != 0xffffffff and seq not in sequences}
    if missing:
        with tempfile.TemporaryDirectory() as directory:
            exported = Path(directory) / "stances.anims"
            export_animations_from_modern_cache(args.cache, exported, missing)
            new_bases, new_sequences, new_frames, new_flags = animation_records(exported)
        assert new_sequences.keys() == missing and new_flags == flags
        for key in bases.keys() & new_bases.keys():
            assert bases[key] == new_bases[key]
        bases.update(new_bases)
        sequences.update(new_sequences)
        target.write_bytes(struct.pack("<4sHHIIII", b"ANM2", 2, 24,
            len(bases), len(sequences), frames + new_frames, flags)
            + b"".join(bases[key] for key in sorted(bases))
            + b"".join(sequences[key] for key in sorted(sequences)))
    header = data_dir / "item_models.h"
    content = header.read_text()
    for item, stance in stances.items():
        pattern = rf"(    \{{ {item}, )(.*?)( \}},)"
        match = re.search(pattern, content)
        assert match
        fields = match[2].split(", ")
        fields[-3:] = map(str, stance)
        content = content[:match.start()] + match[1] + ", ".join(fields) + match[3] + content[match.end():]
    header.write_text(content)
    print("Riskfight weapon stances exported and mapped")


if __name__ == "__main__":
    main()
