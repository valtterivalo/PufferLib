"""Modal script to test upstream CUDA backend with OSRS Inferno.

Usage:
    modal run modal_inferno_cuda.py                        # smoke test (1M steps)
    modal run modal_inferno_cuda.py --steps 1000000000     # 1B run
    modal run modal_inferno_cuda.py --sweep                # single sweep
    modal run modal_inferno_cuda.py --sweep --parallel 4   # 4 parallel sweeps
"""

import modal

REPO_URL = "https://github.com/valtterivalo/PufferLib.git"
BRANCH = "inferno-encounter"

image = (
    modal.Image.from_registry(
        "nvidia/cuda:12.4.1-cudnn-devel-ubuntu22.04",
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
        "setuptools>=77",
        "wheel",
        "Cython",
        "shimmy[gym-v21]",
        "gymnasium>=0.29.1",
        "pettingzoo>=1.24.1",
        "gym==0.23",
        "torch",
        "gpytorch",
        "scikit-learn",
        "psutil",
    )
    .run_commands(
        f"git clone --branch {BRANCH} {REPO_URL} /root/pufferlib",
        "cd /root/pufferlib && python setup.py build_osrs_inferno --force 2>&1",
    )
    .env({"PYTHONPATH": "/root/pufferlib"})
)

app = modal.App("inferno-cuda-test", image=image)


@app.function(
    gpu="L4",
    timeout=7200,
    secrets=[modal.Secret.from_name("wandb-secret")],
)
def train_inferno(sweep: bool = False, steps: int = 1_000_000, worker_id: int = 0):
    """Run inferno training or sweep on L4."""
    import os
    import subprocess
    import time

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

    import glob as g
    so_files = g.glob("pufferlib/_C*.so")
    assert so_files, "no _C.so found"
    print(f"worker {worker_id}: using {so_files}")

    if sweep:
        # worker 0 runs normal sweep (2 baseline trials then suggestions).
        # workers 1+ randomize hyperparams from the start to avoid duplicates.
        import random as _rng
        _rng.seed(worker_id * 7919 + int(time.time()))

        cmd = [
            "python", "-m", "pufferlib.pufferl",
            "sweep", "puffer_osrs_inferno",
            "--sweep.gpus", "1",
            "--wandb",
            "--wandb-project", "inferno-cuda-stability",
            "--wandb-group", "l4-sweep",
            "--tag", f"sweep-worker-{worker_id}",
        ]

        if worker_id > 0:
            # override key hyperparams so each worker starts differently
            lr = 10 ** _rng.uniform(-4, -1)
            ent = 10 ** _rng.uniform(-4, -1)
            vf = _rng.uniform(0.1, 5.0)
            hs = _rng.choice([64, 128, 256, 512])
            nl = _rng.randint(1, 6)
            rr = _rng.uniform(0.1, 4.0)
            gamma = 1.0 - 10 ** _rng.uniform(-4, -1)
            cmd += [
                "--train.learning-rate", f"{lr:.6f}",
                "--train.ent-coef", f"{ent:.6f}",
                "--train.vf-coef", f"{vf:.4f}",
                "--policy.hidden-size", str(hs),
                "--policy.num-layers", str(nl),
                "--train.replay-ratio", f"{rr:.4f}",
                "--train.gamma", f"{gamma:.6f}",
            ]
    else:
        cmd = [
            "python", "-m", "pufferlib.pufferl",
            "train", "puffer_osrs_inferno",
            "--train.total-timesteps", str(steps),
            "--wandb",
            "--wandb-project", "inferno-cuda-stability",
            "--wandb-group", "l4-stability-test",
            "--tag", f"vanilla-cuda-{steps // 1_000_000}m",
        ]

    print(f"worker {worker_id}: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)


@app.local_entrypoint()
def main(sweep: bool = False, steps: int = 1_000_000, parallel: int = 1):
    if parallel > 1 and sweep:
        # fan out parallel independent sweeps
        handles = []
        for i in range(parallel):
            handles.append(train_inferno.spawn(sweep=True, worker_id=i))
        # wait for all to complete
        for h in handles:
            h.get()
    else:
        train_inferno.remote(sweep=sweep, steps=steps, worker_id=0)
