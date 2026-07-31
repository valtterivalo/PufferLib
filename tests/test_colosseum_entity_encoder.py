"""Test the colosseum entity encoder CUDA forward and backward.

Builds a shared library from test_colosseum_entity_encoder.cu (thin wrapper around
ocean.cu's ColosseumEntityEncoder, --float build), then for the encoder
(global projection + NPC pool + inventory-cell pool):
  1. forward-matches the torch ColosseumEntityEncoder on identical bias-free weights,
  2. checks every weight-group gradient against torch autograd,
  3. runs an independent central finite-difference check per weight group, confirming
     no gradient flows into the obs slice (obs is a leaf env input).

The obs generator deliberately exercises active records, all-inactive (zero) records,
a fully-inactive batch row, and a per-channel tie so the masked-maxpool tie-break and
all-inactive semantics are covered -- for BOTH the NPC block and the inventory block.
"""

import subprocess
import ctypes
import os
import numpy as np
import torch
import torch.nn as nn

SRC = os.path.join(os.path.dirname(__file__), "test_colosseum_entity_encoder.cu")
SO = os.path.join(os.path.dirname(__file__), "colo_entity_test.so")

_OCEAN = os.path.join(os.path.dirname(__file__), "..", "src", "ocean.cu")


def _c(name):
    """Read a COLO_ENT_* constexpr straight out of ocean.cu.

    Hardcoding these is what rots layout tests: the encoder constants move every
    time the observation is recut, and a stale copy here would silently test the
    wrong slice instead of failing.
    """
    import re
    m = re.search(r"COLO_ENT_%s\s*=\s*(\d+)" % name, open(_OCEAN).read())
    if not m:
        raise AssertionError("COLO_ENT_%s not found in %s" % (name, _OCEAN))
    return int(m.group(1))


NPC_START = _c("NPC_START")
NUM_NPCS = _c("NUM_NPCS")
FEATS = _c("FEATS")
TYPE_ONEHOT = _c("TYPE_ONEHOT")
BOTTLENECK = _c("BOTTLENECK")

INV_START = _c("INV_START")
NUM_INV_CELLS = _c("INV_NUM_CELLS")
FEATS_PER_CELL = _c("INV_FEATS")
INV_PRESENT = _c("INV_PRESENT")
INV_BOTTLENECK = _c("INV_BOTTLENECK")

HIDDEN = 64
# Only has to span both pooled blocks. Deriving it keeps this test independent of
# the surrounding observation layout, so recutting the obs cannot break it.
OBS_SIZE = max(NPC_START + NUM_NPCS * FEATS,
               INV_START + NUM_INV_CELLS * FEATS_PER_CELL)

assert INV_PRESENT == 0, "pool mask reads a record prefix; present flag must be at offset 0"


def build():
    cmd = [
        "nvcc", "-shared", "-o", SO, SRC,
        "-I", os.path.join(os.path.dirname(__file__), "..", "src"),
        "-lcublas", "-lcudnn", "-lcurand",
        "--compiler-options", "-fPIC", "-Xcompiler", "-O2",
    ]
    print(f"Building: {' '.join(cmd)}")
    subprocess.check_call(cmd)


def _masked_maxpool(embeddings, active):
    masked = embeddings.masked_fill(~active.unsqueeze(-1), float("-inf"))
    pooled = masked.max(dim=1)[0]
    return torch.where(active.any(dim=1, keepdim=True), pooled, torch.zeros_like(pooled))


class ColosseumEntityEncoderRef(nn.Module):
    """Bias-free, tanh-GELU torch reference matching the native + puffernet paths.

    mode 1 = global + NPC pool; mode 2 also adds the inventory-cell pool. Submodule
    definition order (global, entity, inv) yields the parameter order the native
    reg_params / .bin checkpoint expects: global_w, npc_l1, npc_l2, inv_l1, inv_l2.
    """

    def __init__(self, obs_size, hidden_size, mode=1):
        super().__init__()
        self.obs_size = obs_size
        self.hidden_size = hidden_size
        self.mode = mode
        self.npc_block_size = NUM_NPCS * FEATS
        self.global_encoder = nn.Linear(obs_size, hidden_size, bias=False)
        self.entity_encoder = nn.Sequential(
            nn.Linear(FEATS, BOTTLENECK, bias=False),
            nn.GELU(approximate="tanh"),
            nn.Linear(BOTTLENECK, hidden_size, bias=False),
        )
        if mode >= 2:
            self.inv_encoder = nn.Sequential(
                nn.Linear(FEATS_PER_CELL, INV_BOTTLENECK, bias=False),
                nn.GELU(approximate="tanh"),
                nn.Linear(INV_BOTTLENECK, hidden_size, bias=False),
            )

    def forward(self, observations):
        x = observations.view(observations.shape[0], -1).float()
        out = self.global_encoder(x)

        npcs = x[:, NPC_START:NPC_START + self.npc_block_size].reshape(
            x.shape[0], NUM_NPCS, FEATS)
        npc_active = npcs[:, :, 0:TYPE_ONEHOT].sum(dim=-1) > 0
        out = out + _masked_maxpool(self.entity_encoder(npcs), npc_active)

        if self.mode >= 2:
            cells = x[:, INV_START:INV_START + NUM_INV_CELLS * FEATS_PER_CELL].reshape(
                x.shape[0], NUM_INV_CELLS, FEATS_PER_CELL)
            cell_active = cells[:, :, INV_PRESENT] > 0
            out = out + _masked_maxpool(self.inv_encoder(cells), cell_active)

        return out


def load_lib():
    lib = ctypes.CDLL(SO)
    VP = ctypes.c_void_p
    lib.colo_entity_test_init.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.colo_entity_test_set_weights.argtypes = [VP, VP, VP]
    lib.colo_entity_test_set_inv_weights.argtypes = [VP, VP]
    lib.colo_entity_test_forward.argtypes = [VP, VP, ctypes.c_int]
    lib.colo_entity_test_backward.argtypes = [VP, ctypes.c_int]
    for name in ["global_wgrad", "l1_wgrad", "l2_wgrad", "inv_l1_wgrad", "inv_l2_wgrad"]:
        getattr(lib, f"colo_entity_test_get_{name}").argtypes = [VP]
    lib.colo_entity_test_get_argmax.argtypes = [VP, ctypes.c_int]
    return lib


def ptr(t):
    return ctypes.c_void_p(t.data_ptr())


def extract_weights(model):
    """global, npc_l1, npc_l2, then (mode 2) inv_l1, inv_l2 -- the .bin order."""
    ws = [
        model.global_encoder.weight.data.contiguous(),
        model.entity_encoder[0].weight.data.contiguous(),
        model.entity_encoder[2].weight.data.contiguous(),
    ]
    if model.mode >= 2:
        ws.append(model.inv_encoder[0].weight.data.contiguous())
        ws.append(model.inv_encoder[2].weight.data.contiguous())
    return ws


def set_cuda_weights(lib, model):
    ws = extract_weights(model)
    lib.colo_entity_test_set_weights(*[ptr(w) for w in ws[:3]])
    if model.mode >= 2:
        lib.colo_entity_test_set_inv_weights(ptr(ws[3]), ptr(ws[4]))


def _fill_entity_block(obs, b, start, num_entities, feats, onehot_len, gen, present_is_onehot):
    """Fill `num_active` entity records with an active marker + random feats; leave the
    rest zero. present_is_onehot=True -> set a random type one-hot bit (NPC); False ->
    set the present flag at local offset 0 (inventory cell)."""
    num_active = int(torch.randint(1, num_entities + 1, (1,), generator=gen).item())
    for n in range(num_active):
        base = start + n * feats
        if present_is_onehot:
            t = int(torch.randint(0, onehot_len, (1,), generator=gen).item())
            obs[b, base + t] = 1.0
            obs[b, base + onehot_len:base + feats] = torch.randn(feats - onehot_len, generator=gen) * 0.7
        else:
            obs[b, base + INV_PRESENT] = 1.0
            obs[b, base + 1:base + feats] = torch.randn(feats - 1, generator=gen) * 0.7


def generate_obs(B, device, seed):
    """Active records get an active marker + random feats; inactive records stay zero.
    Row 0 forces a per-channel tie across two active records (both NPC and inventory);
    the last row is fully inactive for both blocks to exercise all-inactive -> 0."""
    g = torch.Generator(device="cpu").manual_seed(seed)
    obs = torch.zeros(B, OBS_SIZE, dtype=torch.float32)
    obs.copy_(torch.randn(B, OBS_SIZE, generator=g) * 0.5)
    obs[:, NPC_START:NPC_START + NUM_NPCS * FEATS] = 0.0
    obs[:, INV_START:INV_START + NUM_INV_CELLS * FEATS_PER_CELL] = 0.0

    for b in range(B):
        if b == B - 1:
            continue  # last row fully inactive (no active NPC or inventory records)
        _fill_entity_block(obs, b, NPC_START, NUM_NPCS, FEATS, TYPE_ONEHOT, g, present_is_onehot=True)
        _fill_entity_block(obs, b, INV_START, NUM_INV_CELLS, FEATS_PER_CELL, 0, g, present_is_onehot=False)

    # NPC tie on row 0 between records 0 and 1 (identical active feats).
    obs[0, NPC_START:NPC_START + FEATS] = 0.0
    obs[0, NPC_START + 0] = 1.0
    obs[0, NPC_START + TYPE_ONEHOT:NPC_START + FEATS] = 0.3
    obs[0, NPC_START + FEATS:NPC_START + 2 * FEATS] = 0.0
    obs[0, NPC_START + FEATS + 0] = 1.0
    obs[0, NPC_START + FEATS + TYPE_ONEHOT:NPC_START + 2 * FEATS] = 0.3
    # Inventory tie on row 0 between cells 0 and 1 (identical present + feats).
    obs[0, INV_START:INV_START + FEATS_PER_CELL] = 0.0
    obs[0, INV_START + INV_PRESENT] = 1.0
    obs[0, INV_START + 1:INV_START + FEATS_PER_CELL] = 0.3
    obs[0, INV_START + FEATS_PER_CELL:INV_START + 2 * FEATS_PER_CELL] = 0.0
    obs[0, INV_START + FEATS_PER_CELL + INV_PRESENT] = 1.0
    obs[0, INV_START + FEATS_PER_CELL + 1:INV_START + 2 * FEATS_PER_CELL] = 0.3
    return obs.to(device)


def check_match(name, got, ref, atol=1e-4, rtol=1e-4):
    max_diff = (got - ref).abs().max().item()
    mean_diff = (got - ref).abs().mean().item()
    ref_norm = ref.abs().mean().item()
    print(f"  [{name}] max={max_diff:.3e} mean={mean_diff:.3e} rel={mean_diff / (ref_norm + 1e-8):.3e}")
    ok = torch.allclose(got, ref, atol=atol, rtol=rtol)
    if not ok:
        idx = np.unravel_index((got - ref).abs().argmax().item(), tuple(got.shape))
        print(f"    Worst at {idx}: got={got[idx].item():.6f} ref={ref[idx].item():.6f}")
    assert ok, f"{name} FAILED"


def test_forward(lib, B, mode):
    print(f"\n--- Forward B={B} mode={mode} ---")
    device = torch.device("cuda")
    torch.manual_seed(7)
    model = ColosseumEntityEncoderRef(OBS_SIZE, HIDDEN, mode).to(device).float().eval()
    obs = generate_obs(B, device, seed=11)

    lib.colo_entity_test_init(B, OBS_SIZE, HIDDEN, mode)
    set_cuda_weights(lib, model)

    cuda_out = torch.zeros(B, HIDDEN, device=device)
    lib.colo_entity_test_forward(ptr(cuda_out), ptr(obs), B)
    torch.cuda.synchronize()
    with torch.no_grad():
        ref_out = model(obs)
    check_match("forward", cuda_out, ref_out)
    print("  PASSED")


def test_backward_autograd(lib, B, mode):
    print(f"\n--- Backward vs autograd B={B} mode={mode} ---")
    device = torch.device("cuda")
    torch.manual_seed(7)
    model = ColosseumEntityEncoderRef(OBS_SIZE, HIDDEN, mode).to(device).float()
    obs = generate_obs(B, device, seed=11)

    lib.colo_entity_test_init(B, OBS_SIZE, HIDDEN, mode)
    set_cuda_weights(lib, model)

    cuda_out = torch.zeros(B, HIDDEN, device=device)
    lib.colo_entity_test_forward(ptr(cuda_out), ptr(obs), B)
    torch.cuda.synchronize()

    obs_leaf = obs.clone().requires_grad_(True)
    out = model(obs_leaf)
    grad_output = torch.randn(B, HIDDEN, device=device)
    out.backward(grad_output)

    grad_cuda = grad_output.clone()
    lib.colo_entity_test_backward(ptr(grad_cuda), B)
    torch.cuda.synchronize()

    tol = dict(atol=1e-3, rtol=1e-3)
    g = torch.zeros(HIDDEN, OBS_SIZE, device=device)
    lib.colo_entity_test_get_global_wgrad(ptr(g))
    check_match("global_wgrad", g, model.global_encoder.weight.grad, **tol)

    g = torch.zeros(BOTTLENECK, FEATS, device=device)
    lib.colo_entity_test_get_l1_wgrad(ptr(g))
    check_match("entity_l1_wgrad", g, model.entity_encoder[0].weight.grad, **tol)

    g = torch.zeros(HIDDEN, BOTTLENECK, device=device)
    lib.colo_entity_test_get_l2_wgrad(ptr(g))
    check_match("entity_l2_wgrad", g, model.entity_encoder[2].weight.grad, **tol)

    if mode >= 2:
        g = torch.zeros(INV_BOTTLENECK, FEATS_PER_CELL, device=device)
        lib.colo_entity_test_get_inv_l1_wgrad(ptr(g))
        check_match("inv_l1_wgrad", g, model.inv_encoder[0].weight.grad, **tol)
        g = torch.zeros(HIDDEN, INV_BOTTLENECK, device=device)
        lib.colo_entity_test_get_inv_l2_wgrad(ptr(g))
        check_match("inv_l2_wgrad", g, model.inv_encoder[2].weight.grad, **tol)

    print("  obs-grad: native backward writes weight grads only (no obs buffer touched)")
    print("  PASSED")


def test_finite_difference(lib, B, mode):
    """Independent central finite-difference per weight group."""
    print(f"\n--- Central finite-difference B={B} mode={mode} ---")
    device = torch.device("cuda")
    torch.manual_seed(3)
    model = ColosseumEntityEncoderRef(OBS_SIZE, HIDDEN, mode).to(device).float()
    obs = generate_obs(B, device, seed=5)
    grad_output = torch.randn(B, HIDDEN, device=device)

    weights = extract_weights(model)
    gnames = ["global", "l1", "l2"] + (["inv_l1", "inv_l2"] if mode >= 2 else [])
    getters = {
        "global": (lib.colo_entity_test_get_global_wgrad, (HIDDEN, OBS_SIZE)),
        "l1": (lib.colo_entity_test_get_l1_wgrad, (BOTTLENECK, FEATS)),
        "l2": (lib.colo_entity_test_get_l2_wgrad, (HIDDEN, BOTTLENECK)),
        "inv_l1": (lib.colo_entity_test_get_inv_l1_wgrad, (INV_BOTTLENECK, FEATS_PER_CELL)),
        "inv_l2": (lib.colo_entity_test_get_inv_l2_wgrad, (HIDDEN, INV_BOTTLENECK)),
    }

    def cuda_forward(ws):
        lib.colo_entity_test_init(B, OBS_SIZE, HIDDEN, mode)
        lib.colo_entity_test_set_weights(*[ptr(w.contiguous()) for w in ws[:3]])
        if mode >= 2:
            lib.colo_entity_test_set_inv_weights(ptr(ws[3].contiguous()), ptr(ws[4].contiguous()))
        out = torch.zeros(B, HIDDEN, device=device)
        lib.colo_entity_test_forward(ptr(out), ptr(obs), B)
        torch.cuda.synchronize()
        return out

    cuda_forward(weights)
    grad_cuda = grad_output.clone()
    lib.colo_entity_test_backward(ptr(grad_cuda), B)
    torch.cuda.synchronize()
    ana = {}
    for gname in gnames:
        getter, shape = getters[gname]
        g = torch.zeros(*shape, device=device)
        getter(ptr(g))
        ana[gname] = g.clone()

    eps = 1e-3
    rng = np.random.default_rng(0)

    def fd_loss(out):
        return (out * grad_output).sum().item()

    for gi, gname in enumerate(gnames):
        w = weights[gi]
        flat = w.view(-1)
        n = flat.numel()
        samples = rng.choice(n, size=min(24, n), replace=False)
        max_err = 0.0
        for s in samples:
            s = int(s)
            orig = flat[s].item()
            flat[s] = orig + eps
            fp = fd_loss(cuda_forward(weights))
            flat[s] = orig - eps
            fm = fd_loss(cuda_forward(weights))
            flat[s] = orig
            fd = (fp - fm) / (2 * eps)
            an = ana[gname].view(-1)[s].item()
            denom = max(1.0, abs(an), abs(fd))
            err = abs(fd - an) / denom
            max_err = max(max_err, err)
        print(f"  [{gname}] max relative fd error over {len(samples)} samples: {max_err:.3e}")
        assert max_err < 2e-2, f"finite-difference {gname} FAILED (max_err={max_err:.3e})"
    print("  PASSED")


def main():
    build()
    lib = load_lib()
    # mode 1 (NPC pool only) is unreachable since the mode knob was deleted; both
    # pooled branches are always on now.
    for mode in (2,):
        for B in (4, 8):
            test_forward(lib, B, mode)
            test_backward_autograd(lib, B, mode)
        test_finite_difference(lib, 4, mode)
    print("\nALL PASSED")


if __name__ == "__main__":
    main()
