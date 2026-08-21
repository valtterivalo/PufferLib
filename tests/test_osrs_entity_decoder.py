import ctypes
import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
CUDA_SOURCE = Path(__file__).with_name("test_osrs_entity_decoder_cuda.cu")
BATCH = 7
HIDDEN = 40
OUTPUT_DIM = 436
TARGET_START = 25
TARGET_SLOTS = 14
KEY_DIM = 16
LINEAR_DIM = OUTPUT_DIM + 1 - TARGET_SLOTS


def _pointer(tensor):
    return ctypes.c_void_p(tensor.data_ptr())


def _configure_library(path: Path):
    library = ctypes.CDLL(path)
    pointer = ctypes.c_void_p
    library.osrs_entity_decoder_test_init.argtypes = [ctypes.c_int, ctypes.c_int]
    library.osrs_entity_decoder_test_set_weights.argtypes = [
        pointer,
        pointer,
        pointer,
        pointer,
    ]
    library.osrs_entity_decoder_test_forward.argtypes = [pointer, pointer, pointer]
    library.osrs_entity_decoder_test_backward.argtypes = [pointer, pointer, pointer]
    library.osrs_entity_decoder_test_get_grad.argtypes = [ctypes.c_int, pointer]
    return library


@pytest.fixture(scope="session")
def cuda_library(tmp_path_factory):
    nvcc = shutil.which("nvcc")
    if nvcc is None:
        pytest.skip("nvcc is not installed")
    raylib_include = next(ROOT.glob("raylib-*/include"))
    raylib_archive = raylib_include.parent / "lib/libraylib.a"
    pytest.importorskip("torch")
    library_path = (
        tmp_path_factory.mktemp("osrs_entity_decoder") /
        "osrs_entity_decoder.so"
    )
    subprocess.run(
        [
            nvcc,
            "-shared",
            "-o",
            str(library_path),
            str(CUDA_SOURCE),
            "-I",
            str(ROOT / "src"),
            "-I",
            str(raylib_include),
            "-Xlinker",
            "--no-as-needed",
            "-lcublas",
            "-lcudnn",
            "-lcurand",
            "-lnccl",
            "-lnvidia-ml",
            "-lcusolver",
            str(raylib_archive),
            "-lGL",
            "-lm",
            "-lpthread",
            "-ldl",
            "-lrt",
            "--compiler-options",
            "-fPIC",
            "-Xcompiler",
            "-O2",
        ],
        cwd=ROOT,
        check=True,
    )
    return _configure_library(library_path)


def _reference_output(torch, hidden, keys, weights):
    linear_w, query_w, key_w, log_temperature = weights
    linear = torch.nn.functional.linear(hidden, linear_w)
    query = torch.nn.functional.linear(hidden, query_w)
    projected_keys = torch.nn.functional.linear(keys, key_w)
    dot = (query[:, None, :] * projected_keys).sum(dim=-1)
    denominator = torch.sqrt(
        torch.clamp(
            query.square().sum(dim=-1)[:, None]
            * projected_keys.square().sum(dim=-1),
            min=1.0e-12,
        )
    )
    target_logits = torch.exp(log_temperature[0]) * dot / denominator
    return torch.cat(
        [
            linear[:, :TARGET_START],
            target_logits,
            linear[:, TARGET_START:],
        ],
        dim=-1,
    )


def test_forward_backward_and_slot_permutation(cuda_library):
    torch = pytest.importorskip("torch")
    if not torch.cuda.is_available():
        pytest.skip("CUDA is not available")
    generator = torch.Generator(device="cuda").manual_seed(4187)
    hidden = torch.randn(
        (BATCH, HIDDEN), generator=generator, device="cuda", requires_grad=True
    )
    keys = torch.randn(
        (BATCH, TARGET_SLOTS, KEY_DIM),
        generator=generator,
        device="cuda",
        requires_grad=True,
    )
    with torch.no_grad():
        keys[:, -1].zero_()
    weights = [
        torch.randn(
            (LINEAR_DIM, HIDDEN),
            generator=generator,
            device="cuda",
            requires_grad=True,
        ),
        torch.randn(
            (KEY_DIM, HIDDEN),
            generator=generator,
            device="cuda",
            requires_grad=True,
        ),
        torch.randn(
            (KEY_DIM, KEY_DIM),
            generator=generator,
            device="cuda",
            requires_grad=True,
        ),
        torch.tensor([1.7], device="cuda", requires_grad=True),
    ]
    cuda_library.osrs_entity_decoder_test_init(BATCH, HIDDEN)
    cuda_library.osrs_entity_decoder_test_set_weights(
        *[_pointer(weight) for weight in weights]
    )

    expected = _reference_output(torch, hidden, keys, weights)
    actual = torch.empty_like(expected)
    cuda_library.osrs_entity_decoder_test_forward(
        _pointer(hidden), _pointer(keys), _pointer(actual)
    )
    torch.testing.assert_close(actual, expected, atol=3e-4, rtol=3e-4)
    assert torch.isfinite(actual).all()
    assert torch.count_nonzero(
        actual[:, TARGET_START + TARGET_SLOTS - 1]
    ).item() == 0

    output_gradient = torch.randn(
        expected.shape, generator=generator, device="cuda"
    )
    output_gradient[:, TARGET_START + TARGET_SLOTS - 1] = 0.0
    expected.backward(output_gradient)
    actual_hidden_grad = torch.empty_like(hidden)
    cuda_library.osrs_entity_decoder_test_backward(
        _pointer(output_gradient[:, :OUTPUT_DIM].contiguous()),
        _pointer(output_gradient[:, OUTPUT_DIM].contiguous()),
        _pointer(actual_hidden_grad),
    )
    torch.testing.assert_close(
        actual_hidden_grad, hidden.grad, atol=5e-3, rtol=5e-3
    )

    expected_gradients = [weight.grad for weight in weights] + [keys.grad]
    for index, expected_gradient in enumerate(expected_gradients):
        actual_gradient = torch.empty_like(expected_gradient)
        cuda_library.osrs_entity_decoder_test_get_grad(
            index, _pointer(actual_gradient)
        )
        torch.testing.assert_close(
            actual_gradient, expected_gradient, atol=5e-3, rtol=5e-3
        )

    permutation = torch.randperm(
        TARGET_SLOTS, generator=generator, device="cuda"
    )
    permuted_keys = keys.detach()[:, permutation].contiguous()
    permuted_output = torch.empty_like(actual)
    cuda_library.osrs_entity_decoder_test_forward(
        _pointer(hidden), _pointer(permuted_keys), _pointer(permuted_output)
    )
    torch.testing.assert_close(
        permuted_output[:, :TARGET_START],
        actual[:, :TARGET_START],
        atol=3e-4,
        rtol=3e-4,
    )
    torch.testing.assert_close(
        permuted_output[:, TARGET_START:TARGET_START + TARGET_SLOTS],
        actual[:, TARGET_START:TARGET_START + TARGET_SLOTS][:, permutation],
        atol=3e-4,
        rtol=3e-4,
    )
    torch.testing.assert_close(
        permuted_output[:, TARGET_START + TARGET_SLOTS:],
        actual[:, TARGET_START + TARGET_SLOTS:],
        atol=3e-4,
        rtol=3e-4,
    )
    target_mask = torch.rand(
        (BATCH, TARGET_SLOTS), generator=generator, device="cuda"
    ) > 0.4
    target_mask[:, 0] = True
    target_mask[:, -1] = False
    target_logits = actual[:, TARGET_START:TARGET_START + TARGET_SLOTS]
    selected_slots = target_logits.masked_fill(~target_mask, -torch.inf).argmax(
        dim=-1
    )
    permuted_target_mask = target_mask[:, permutation]
    permuted_target_logits = permuted_output[
        :, TARGET_START:TARGET_START + TARGET_SLOTS
    ]
    selected_permuted_slots = permuted_target_logits.masked_fill(
        ~permuted_target_mask, -torch.inf
    ).argmax(dim=-1)
    torch.testing.assert_close(
        permutation[selected_permuted_slots], selected_slots
    )
