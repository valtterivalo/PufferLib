from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text()


def test_state_set_curriculum_surface_matches_upstream_port() -> None:
    default_config = read("config/default.ini")
    bindings = read("src/bindings.cu")
    pufferlib = read("src/pufferlib.cu")
    pufferl = read("pufferlib/pufferl.py")

    for key in (
        "anneal_prio_beta",
        "state_curriculum_mode",
        "state_buffer_size",
        "cl_frac",
        "anneal_cl",
        "warmup_states",
        "state_checkpoint_interval",
        "explore_alpha",
        "explore_beta",
        "explore_decay",
        "terminal_reset_state",
    ):
        assert key in default_config

    for key in (
        "anneal_prio_beta",
        "state_buffer_size",
        "cl_frac",
        "anneal_cl",
        "warmup_states",
        "state_checkpoint_interval",
        "explore_alpha",
        "explore_beta",
        "explore_decay",
        "terminal_reset_state",
    ):
        assert f'hypers.{key} = get_config(train_kwargs, "{key}")' in bindings
        assert f'.def_readwrite("{key}", &HypersT::{key})' in bindings

    assert 'int state_curriculum_mode = (int)get_config(train_kwargs, "state_curriculum_mode")' in bindings
    assert "state_curriculum_mode must be 0 or 1" in bindings
    assert "state_curriculum_mode=1 requires state_buffer_size > 0" in bindings
    assert "state_curriculum_mode=1 requires cl_frac > 0" in bindings
    assert "warmup_states must be <= state_buffer_size" in bindings
    assert "curriculum_rollout_begin(&pufferl)" in bindings
    assert (ROOT / "src/curriculum.cu").exists()
    assert '#define PUFFER_CURRICULUM_TYPES' in pufferlib
    assert '#define PUFFER_CURRICULUM_IMPL' in pufferlib
    assert "StateBuffer state_buf" in pufferlib
    assert "int curriculum_enabled" in pufferlib
    assert "curriculum_update_advantages(&pufferl, &advantages_puf, train_stream)" in pufferlib
    assert "capture_curriculum_checkpoint(pufferl, buf, t)" in pufferlib
    assert "compute_state_prio_normalize" in pufferlib
    assert "close_state_buffer(&pufferl.state_buf)" in pufferlib
    assert "warmup_states <= state_buffer_size" in pufferl
    assert "state_checkpoint_interval must be positive" in pufferl


def test_state_set_curriculum_preserves_terminal_reset_surface() -> None:
    bindings = read("src/bindings.cu")
    pufferlib = read("src/pufferlib.cu")

    assert "PrecisionTensor mb_terminals" in pufferlib
    assert "alloc_register(alloc, &bufs.mb_terminals)" in pufferlib
    assert "zero_terminal_recurrent_state_kernel" in pufferlib
    assert "hypers.terminal_reset_state" in pufferlib
    assert "graph.mb_terminals" in pufferlib
    assert "pufferl.train_cudagraph != nullptr" in pufferlib
    assert "pufferl.fused_rollout_cudagraphs != nullptr" in pufferlib
    assert '.def_readwrite("reset_state", &HypersT::reset_state)' in bindings
    assert '.def_readwrite("terminal_reset_state", &HypersT::terminal_reset_state)' in bindings


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


def test_pvp_exposes_state_snapshots_for_curriculum() -> None:
    binding = read("ocean/osrs_pvp/binding.c")

    for symbol in (
        "PvpStateSnapshot",
        "PUFFER_STATE_T PvpStateSnapshot",
        "PUFFER_STATE_STORE pvp_state_store",
        "PUFFER_STATE_LOAD pvp_state_load",
        "PUFFER_STATE_REFRESH pvp_state_refresh",
        "pvp_env_rewire_after_load",
        "pvp_env_rewire_internal_buffers",
        "pvp_env_rewire_rollout_buffers",
    ):
        assert symbol in binding

    assert "out->pvp = env->pvp" in binding
    assert "env->pvp = in->pvp" in binding
    assert "env->pvp.observations = env->pvp._obs_buf" in binding
    assert "env->pvp.ocean_io.agent_obs = env->obs_ptr[0]" in binding
    assert "ocean_write_obs(&env->pvp)" in binding
