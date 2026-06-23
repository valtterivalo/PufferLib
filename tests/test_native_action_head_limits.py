import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _read(path: str) -> str:
    return (ROOT / path).read_text()


def _define_int(text: str, name: str) -> int:
    match = re.search(rf"^#define\s+{name}\s+([0-9]+)\b", text, re.MULTILINE)
    assert match, name
    return int(match.group(1))


def test_native_action_head_caps_cover_pvp_inventory_click_schema() -> None:
    osrs_types = _read("ocean/osrs/osrs_types.h")
    kernels = _read("src/kernels.cu")
    puffernet = _read("src/puffernet.h")
    visual = _read("ocean/osrs/osrs_visual.c")

    pvp_heads = _define_int(osrs_types, "OSRS_INVENTORY_SIZE") + 6

    assert pvp_heads == 34
    assert _define_int(kernels, "MAX_ATN_HEADS") >= pvp_heads
    assert _define_int(puffernet, "PUFFERNET_MAX_ACTION_HEADS") >= pvp_heads
    assert _define_int(visual, "VISUAL_POLICY_MAX_ACTION_HEADS") >= pvp_heads
