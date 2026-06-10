from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
OSRS = REPO / "ocean" / "osrs"

FORBIDDEN_CALLS = (
    "encounter_player_can_attack(",
    "entity_has_line_of_sight(",
    "collision_entity_has_line_of_sight(",
    "npc_has_line_of_sight(",
)

ALLOWED_FILES = {
    OSRS / "osrs_collision.h",
    OSRS / "osrs_attack_reach.h",
}


def test_attack_reach_is_the_shared_call_surface() -> None:
    offenders: list[str] = []
    for path in OSRS.rglob("*"):
        if path.suffix not in {".c", ".h", ".inc"}:
            continue
        if path in ALLOWED_FILES or "tests" in path.parts:
            continue
        text = path.read_text()
        for call in FORBIDDEN_CALLS:
            if call in text:
                offenders.append(f"{path.relative_to(REPO)}: {call}")

    assert offenders == []
