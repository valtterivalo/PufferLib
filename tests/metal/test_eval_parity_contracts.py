from __future__ import annotations

import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def text(path: str) -> str:
    return (ROOT / path).read_text()


def load_parity_module():
    path = ROOT / "tools/metal/osrs_eval_parity.py"
    spec = importlib.util.spec_from_file_location("osrs_eval_parity", path)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_eval_action_mode_reaches_cuda_and_metal_samplers() -> None:
    assert "eval_action_mode" in text("pufferlib/pufferl.py")
    assert "hypers.eval_action_mode" in text("src/bindings.cu")
    assert "hypers.eval_action_mode" in text("src/metal/bindings.mm")
    assert "sp.action_mode == 1" in text("src/metal/shader_src.h")
    assert "action_mode == 1" in text("src/pufferlib.cu")
    assert "action_mode == 1" in text("src/metal/cpu_inference.h")


def test_parity_hashes_compare_semantic_backend_state() -> None:
    assert "parity_hashes" in text("src/bindings.cu")
    assert "parity_hashes" in text("src/metal/bindings.mm")
    assert "rollout_actions_i32" in text("src/bindings.cu")
    assert "rollout_actions_i32" in text("src/metal/bindings.mm")
    harness = text("tools/metal/osrs_eval_parity.py")
    assert "initial_hashes" in harness
    assert "rollout parity hash mismatch" in harness
    assert "policy_debug_sample" in text("src/bindings.cu")
    assert "policy_debug_sample" in text("src/metal/bindings.mm")
    assert "env_debug_sample" in text("src/bindings.cu")
    assert "env_debug_sample" in text("src/metal/bindings.mm")
    assert "policy_debug" in harness
    assert "initial_debug" in harness


def test_masked_sampler_fallback_uses_last_legal_action() -> None:
    cuda = text("src/pufferlib.cu")
    metal = text("src/metal/shader_src.h")
    cpu = text("src/metal/cpu_inference.h")
    assert "for (int a = A - 1; a >= 0; --a)" in cuda
    assert "for (int a = A - 1; a >= 0; a--)" in metal
    assert "for (int a = A - 1; a >= 0; a--)" in cpu
    assert "assert(false && \"no valid actions for discrete action head\")" in cuda
    assert "assert(has_valid_action && \"no valid actions for discrete action head\")" in cpu


def test_checkpoint_save_is_train_phase_gated_and_writes_metadata() -> None:
    source = text("pufferlib/pufferl.py")
    assert "train_phase = epoch < train_epochs" in source
    assert "and train_phase" in source
    assert "write_checkpoint_metadata(model_path, args, backend, pufferl, epoch, 'train')" in source


def test_cuda_close_handles_disabled_cudagraphs() -> None:
    source = text("src/pufferlib.cu")
    assert "if (pufferl.train_cudagraph)" in source
    assert "if (pufferl.fused_rollout_cudagraphs)" in source


def test_cuda_graph_capture_resets_recurrent_state() -> None:
    source = text("src/pufferlib.cu")
    assert "puf_zero(&pufferl->buffer_states[b], pufferl->default_stream)" in source
    assert "puf_zero(&bank->buffer_states[b], pufferl->default_stream)" in source


def test_metal_keeps_embedded_masks_out_of_sampler_by_default() -> None:
    source = text("src/metal/pufferlib.mm")
    runner = text("tools/metal/puffer-metal.py")
    assert "sample_mask_in_obs" in source
    assert "pufferl->has_mask = hypers.sample_mask_in_obs && mask_in_obs" in source
    assert "PUFFER_METAL_SAMPLE_MASK_IN_OBS requires env.mask_in_obs" in source
    assert '"--metal-sample-mask-in-obs": "PUFFER_METAL_SAMPLE_MASK_IN_OBS"' in runner


def test_parity_harness_merges_defaults_and_reports_filled_keys(tmp_path: Path) -> None:
    mod = load_parity_module()
    defaults = {
        "env_name": "osrs_inferno",
        "train": {"horizon": 1, "total_timesteps": 8},
        "env": {"start_wave": 1},
    }
    payload = {
        "train": {"horizon": 2},
    }
    config = tmp_path / "config.json"
    config.write_text(json.dumps(payload))

    original = mod.load_clean_default_config
    mod.load_clean_default_config = lambda env_name: defaults
    try:
        merged, filled = mod.load_config("osrs_inferno", config)
    finally:
        mod.load_clean_default_config = original

    assert merged["train"]["horizon"] == 2
    assert merged["train"]["total_timesteps"] == 8
    assert "train.total_timesteps" in filled
    assert "env.start_wave" in filled
