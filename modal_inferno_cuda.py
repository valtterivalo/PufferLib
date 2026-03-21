"""Modal script to test upstream CUDA backend with OSRS Inferno on A100.

Tests whether vanilla CUDA path (bf16, joint-ratio clipping, no stability fixes)
can train the 7-head inferno action space at scale without entropy collapse or NaN.

Usage:
    pip install modal && python3 -m modal setup   # one-time auth
    modal run modal_inferno_cuda.py                # smoke test (1M steps)
    modal run modal_inferno_cuda.py --steps 50000000  # medium run
    modal run modal_inferno_cuda.py --sweep        # Protein sweep
"""

import modal

REPO_URL = "https://github.com/valtterivalo/PufferLib.git"
BRANCH = "inferno-encounter"

# CUDA 12.4 devel has nvcc + headers for compiling kernels.cu
image = (
    modal.Image.from_registry(
        "nvidia/cuda:12.4.1-devel-ubuntu22.04",
        add_python="3.12",
    )
    .apt_install("git", "clang", "make", "wget", "tar", "binutils", "libomp-dev")
    .pip_install(
        "pybind11",
        "numpy<2.0",
        "pynvml",
        "rich",
        "rich-argparse",
        "wandb",
        "setuptools",
        "wheel",
        "Cython",
    )
    .run_commands(
        f"git clone --branch {BRANCH} {REPO_URL} /root/pufferlib",
        # install pufferlib without building _C.so (needs nvcc at correct path)
        "cd /root/pufferlib && NO_TRAIN=1 pip install -e . --no-build-isolation",
        # find nvcc and set CUDA_HOME, then build
        "export CUDA_HOME=$(dirname $(dirname $(which nvcc))) && "
        "cd /root/pufferlib && python setup.py build_osrs_inferno --force",
    )
)

app = modal.App("inferno-cuda-test", image=image)


@app.function(
    gpu="A100-40GB",
    timeout=7200,
    secrets=[modal.Secret.from_name("wandb-secret")],
)
def train_inferno(sweep: bool = False, steps: int = 1_000_000):
    """Run inferno training on A100."""
    import os
    import subprocess

    os.chdir("/root/pufferlib")

    # pull latest and rebuild if code changed
    result = subprocess.run(
        ["git", "pull", "origin", BRANCH],
        capture_output=True, text=True,
    )
    if "Already up to date" not in result.stdout:
        print("code updated, rebuilding...")
        subprocess.run(
            ["python", "setup.py", "build_osrs_inferno", "--force"],
            check=True,
        )

    # verify _C.so exists
    import glob as g
    so_files = g.glob("pufferlib/_C*.so")
    assert so_files, "no _C.so found"
    print(f"using: {so_files}")

    if sweep:
        cmd = [
            "python", "-m", "pufferlib.pufferl",
            "sweep", "puffer_osrs_inferno",
            "--wandb",
            "--wandb-project", "inferno-cuda-stability",
            "--wandb-group", "a100-sweep",
        ]
    else:
        cmd = [
            "python", "-m", "pufferlib.pufferl",
            "train", "puffer_osrs_inferno",
            "--train.total-timesteps", str(steps),
            "--wandb",
            "--wandb-project", "inferno-cuda-stability",
            "--wandb-group", "a100-stability-test",
            "--tag", f"vanilla-cuda-{steps // 1_000_000}m",
        ]

    print(f"=== {' '.join(cmd)} ===")
    subprocess.run(cmd, check=True)


@app.local_entrypoint()
def main(sweep: bool = False, steps: int = 1_000_000):
    train_inferno.remote(sweep=sweep, steps=steps)
