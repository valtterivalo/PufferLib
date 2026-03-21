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

image = (
    modal.Image.debian_slim(python_version="3.12")
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
        # clone repo — setup.py downloads raylib/box2d at import time
        f"git clone --branch {BRANCH} {REPO_URL} /root/pufferlib",
        # install pufferlib (this runs setup.py which downloads raylib + box2d)
        "cd /root/pufferlib && pip install -e . --no-build-isolation",
    )
)

app = modal.App("inferno-cuda-test", image=image)


@app.function(
    gpu="A100-40GB",
    timeout=7200,
    secrets=[modal.Secret.from_name("wandb-secret")],
)
def train_inferno(sweep: bool = False, steps: int = 1_000_000):
    """Build inferno env and run training on A100."""
    import os
    import subprocess

    os.chdir("/root/pufferlib")

    # pull latest in case the image is cached
    subprocess.run(["git", "pull", "origin", BRANCH], check=True)

    # build osrs_inferno (static lib + _C.so with CUDA)
    print("=== building osrs_inferno ===")
    subprocess.run(
        ["python", "setup.py", "build_osrs_inferno", "--force"],
        check=True,
    )

    # verify _C.so exists
    import glob as g
    so_files = g.glob("pufferlib/_C*.so")
    assert so_files, "build failed: no _C.so found"
    print(f"built: {so_files}")

    # run training or sweep
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

    print(f"=== running: {' '.join(cmd)} ===")
    subprocess.run(cmd, check=True)


@app.local_entrypoint()
def main(sweep: bool = False, steps: int = 1_000_000):
    train_inferno.remote(sweep=sweep, steps=steps)
