from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text()


def test_state_set_curriculum_surface_matches_upstream_port() -> None:
    default_config = read("config/default.ini")
    bindings = read("src/bindings.cu")
    pufferlib = read("src/pufferlib.cu")

    for key in (
        "anneal_prio_beta",
        "anneal_cl",
        "state_checkpoint_interval",
        "explore_decay",
    ):
        assert key in default_config
        assert f'hypers.{key} = get_config(train_kwargs, "{key}")' in bindings
        assert f'.def_readwrite("{key}", &HypersT::{key})' in bindings

    assert (ROOT / "src/curriculum.cu").exists()
    assert '#include "curriculum.cu"' in pufferlib


def test_state_set_curriculum_uses_checkpoint_heap_replacement() -> None:
    curriculum = read("src/curriculum.cu")

    for symbol in (
        "candidate_states",
        "state_heap_insert",
        "state_heap_update_slot",
        "capture_curriculum_checkpoint",
        "compute_curriculum_checkpoint_scores",
    ):
        assert symbol in curriculum


def test_maze_exposes_deep_copy_state_snapshots() -> None:
    binding = read("ocean/maze/binding.c")
    maze = read("ocean/maze/maze.h")
    vecenv = read("src/vecenv.h")

    for symbol in (
        "PUFFER_STATE_T MazePufferState",
        "PUFFER_STATE_STORE",
        "PUFFER_STATE_LOAD",
        "PUFFER_STATE_REFRESH",
    ):
        assert symbol in binding

    assert "struct MazePufferState" in maze
    assert "maze_state_snapshot_store" in maze
    assert "maze_state_snapshot_load" in maze
    assert "PUFFER_STATE_STORE" in vecenv
    assert "PUFFER_STATE_LOAD" in vecenv
