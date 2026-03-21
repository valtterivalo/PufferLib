"""Modal script to test upstream CUDA backend with OSRS Inferno on H200.

Tests whether vanilla CUDA path (bf16, joint-ratio clipping, no stability fixes)
can train the 7-head inferno action space at scale without entropy collapse or NaN.

Usage:
    pip install modal && python3 -m modal setup   # one-time auth
    modal run modal_inferno_cuda.py                # launch training
    modal run modal_inferno_cuda.py --sweep        # launch Protein sweep
"""

import modal

REPO_URL = "https://github.com/valtterivalo/PufferLib.git"
BRANCH = "inferno-encounter"

image = (
    modal.Image.debian_slim(python_version="3.12")
    .apt_install("git", "clang", "make", "wget", "tar", "binutils")
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
        "cd /root/pufferlib && NO_OCEAN=1 NO_TRAIN=1 pip install -e . --no-build-isolation",
    )
)

app = modal.App("inferno-cuda-test", image=image)


@app.function(
    gpu="A100-40GB",
    timeout=7200,
    secrets=[modal.Secret.from_name("wandb-secret")],
)
def train_inferno(sweep: bool = False):
    """Build inferno env and run training on H200."""
    import os
    import subprocess

    os.chdir("/root/pufferlib")

    # pull latest in case the image is cached with an older commit
    subprocess.run(["git", "pull", "origin", BRANCH], check=True)

    # build the inferno environment (static lib + _C.so with CUDA)
    print("=== building osrs_inferno ===")
    subprocess.run(
        ["python", "setup.py", "build_osrs_inferno", "--force"],
        check=True,
    )

    # verify _C.so was built
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
            "--wandb",
            "--wandb-project", "inferno-cuda-stability",
            "--wandb-group", "a100-stability-test",
            "--tag", "vanilla-cuda-500m",
        ]

    print(f"=== running: {' '.join(cmd)} ===")
    subprocess.run(cmd, check=True)


@app.local_entrypoint()
def main(sweep: bool = False):
    train_inferno.remote(sweep=sweep)
