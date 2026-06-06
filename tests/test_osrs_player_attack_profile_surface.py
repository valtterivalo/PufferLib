from pathlib import Path
import re


REPO = Path(__file__).resolve().parents[1]
OSRS = REPO / "ocean" / "osrs"

ALLOWED_FILES = {
    OSRS / "osrs_combat.h",
    OSRS / "osrs_player_attack_profile.h",
}

RAW_ATTACK_SPEED_PATTERNS = (
    re.compile(r"ITEM_DATABASE\[[^\n]+\]\.attack_speed"),
    re.compile(r"get_slot_gear_bonuses\([^\n]+->attack_speed"),
    re.compile(r"slot_bonuses->attack_speed"),
)


def test_player_attack_speed_uses_shared_profile_surface() -> None:
    offenders: list[str] = []
    for path in OSRS.rglob("*"):
        if path.suffix not in {".c", ".h", ".inc"}:
            continue
        if path in ALLOWED_FILES or "tests" in path.parts:
            continue
        for line_no, line in enumerate(path.read_text().splitlines(), start=1):
            if any(pattern.search(line) for pattern in RAW_ATTACK_SPEED_PATTERNS):
                offenders.append(f"{path.relative_to(REPO)}:{line_no}")

    assert offenders == []
