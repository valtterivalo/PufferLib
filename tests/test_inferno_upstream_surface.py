from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text()


def test_mistaken_core_side_files_are_absent() -> None:
    for path in (
        "src/archive.h",
        "src/demostore.h",
        "src/phase2_curriculum.h",
    ):
        assert not (ROOT / path).exists()


def test_inferno_uses_upstream_state_copy_contract() -> None:
    binding = read("ocean/osrs_inferno/binding.c")
    render_snapshot = read(
        "ocean/osrs/encounters/inferno/encounter_inferno_render_snapshot.inc"
    )
    attack_tests = read("ocean/osrs/tests/test_inferno_attack_styles.c")

    for symbol in (
        "typedef InfernoState State;",
        "static inline void puffer_state_refresh(Env* env)",
        "inferno_env_refresh_after_state_load",
        "inferno_env_write_post_restore_state",
    ):
        assert symbol in binding

    for forbidden in (
        "PUFFER_STATE_T",
        "PUFFER_STATE_SIZE",
        "PUFFER_STATE_REFRESH",
        "src/archive.h",
        "src/demostore.h",
        "src/phase2_curriculum.h",
        "Phase2",
        "phase2",
        "Archive",
        "archive",
    ):
        assert forbidden not in binding

    for symbol in (
        "inf_refresh_after_state_load",
        "inf_rebuild_los(s)",
        "inf_rebuild_entity_collision_flags(s)",
        "inf_invalidate_los_cache(s)",
        "inf_refresh_current_obs_slots_ctx(s, ctx)",
    ):
        assert symbol in render_snapshot

    for symbol in (
        "test_inferno_state_assignment_copy_replays_trajectory",
        "test_inferno_refresh_after_state_load_rebuilds_derived_state",
        "test_inferno_snapshot_restore_round_trip",
    ):
        assert symbol in attack_tests


def test_inferno_config_keeps_upstream_state_curriculum_off() -> None:
    config = read("config/ocean/osrs_inferno.ini")

    for line in (
        "num_start_states = 0",
        "cl_frac = 0",
        "fresh_frac = 0",
    ):
        assert line in config

    for forbidden in (
        "phase2_",
        "phase2",
        "record_best_replay_path",
        "play_replay_path",
        "train.num_start_states",
        "train.cl_frac",
        "train.fresh_frac",
    ):
        assert forbidden not in config


def test_inferno_config_uses_upstream_muon_momentum() -> None:
    config = read("config/ocean/osrs_inferno.ini")

    assert "momentum = 0.9641862728826048" in config
    assert "beta1" not in config
    assert "beta2" not in config
    assert "state_curriculum_mode" not in config
