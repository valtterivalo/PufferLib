"""Build configuration for PufferLib C/CUDA extensions, raylib/box2d download, and static env builds."""

# Debug command:
#    DEBUG=1 python setup.py build_ext --inplace --force
#    CUDA_VISIBLE_DEVICES=None LD_PRELOAD=$(gcc -print-file-name=libasan.so) \
#        python3.12 -m pufferlib.clean_pufferl eval --train.device cpu

import glob
import os
import platform
import shutil
import tarfile
import urllib.request
import zipfile
from distutils.sysconfig import get_config_vars

import numpy
import pybind11
from setuptools import Extension, find_namespace_packages, find_packages, setup
from setuptools.command.build_ext import build_ext

# Build flags
DEBUG = os.getenv("DEBUG", "0") == "1"
NO_OCEAN = os.getenv("NO_OCEAN", "0") == "1"
NO_TRAIN = os.getenv("NO_TRAIN", "0") == "1"

# Torch is only needed for profiler builds (build_profile_torch, build_profiler).
# The main _C.so build is always torch-free.
HAS_TORCH = False
if not NO_TRAIN:
    try:
        from torch.utils.cpp_extension import CUDA_HOME

        HAS_TORCH = True
    except ImportError:
        pass

# Use ccache if available for faster rebuilds
if shutil.which("ccache"):
    os.environ.setdefault("CC", "ccache cc")
    os.environ.setdefault("CXX", "ccache c++")

# Build raylib for your platform
RAYLIB_URL = "https://github.com/raysan5/raylib/releases/download/5.5/"
RAYLIB_NAME = "raylib-5.5_macos" if platform.system() == "Darwin" else "raylib-5.5_linux_amd64"
RLIGHTS_URL = "https://raw.githubusercontent.com/raysan5/raylib/refs/heads/master/examples/shaders/rlights.h"


def download_raylib(platform, ext):
    """Download and extract a raylib release archive for the given platform.

    Args:
        platform: Platform string (e.g., 'raylib-5.5_macos').
        ext: Archive extension ('.zip' or '.tar.gz').

    """
    if not os.path.exists(platform):
        print(f"Downloading Raylib {platform}")
        urllib.request.urlretrieve(RAYLIB_URL + platform + ext, platform + ext)  # noqa: S310
        if ext == ".zip":
            with zipfile.ZipFile(platform + ext, "r") as zip_ref:
                zip_ref.extractall()  # noqa: S202
        else:
            with tarfile.open(platform + ext, "r") as tar_ref:
                tar_ref.extractall()  # noqa: S202

        os.remove(platform + ext)
        urllib.request.urlretrieve(RLIGHTS_URL, platform + "/include/rlights.h")  # noqa: S310


if not NO_OCEAN:
    download_raylib("raylib-5.5_webassembly", ".zip")
    download_raylib(RAYLIB_NAME, ".tar.gz")

BOX2D_URL = "https://github.com/capnspacehook/box2d/releases/latest/download/"
BOX2D_NAME = "box2d-macos-arm64" if platform.system() == "Darwin" else "box2d-linux-amd64"


def download_box2d(platform):
    """Download and extract a Box2D release archive for the given platform.

    Args:
        platform: Platform string (e.g., 'box2d-macos-arm64').

    """
    if not os.path.exists(platform):
        ext = ".tar.gz"

        print(f"Downloading Box2D {platform}")
        urllib.request.urlretrieve(BOX2D_URL + platform + ext, platform + ext)  # noqa: S310
        with tarfile.open(platform + ext, "r") as tar_ref:
            tar_ref.extractall()  # noqa: S202

        os.remove(platform + ext)


if not NO_OCEAN:
    download_box2d("box2d-web")
    download_box2d(BOX2D_NAME)

# Shared compile args for all platforms
extra_compile_args = [
    "-DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION",
    "-DPLATFORM_DESKTOP",
]
extra_link_args = [
    "-fwrapv",
    "-fopenmp",
]
cxx_args = [
    "-fdiagnostics-color=always",
    "-std=c++17",
    "-fopenmp",
]
nvcc_args = [
    "-Xcompiler=-D_GLIBCXX_USE_CXX11_ABI=1",
    "-std=c++17",
]

if DEBUG:
    extra_compile_args += [
        "-O0",
        "-g",
    ]
    extra_link_args += [
        "-g",
    ]
    cxx_args += [
        "-O0",
        "-g",
    ]
    nvcc_args += [
        "-O0",
        "-g",
    ]
else:
    extra_compile_args += [
        "-O2",
        "-flto=auto",
        "-fno-semantic-interposition",
        "-fvisibility=hidden",
    ]
    extra_link_args += [
        "-O2",
    ]
    cxx_args += [
        "-O2",
        "-fno-semantic-interposition",
        "-Wno-c++11-narrowing",
    ]
    nvcc_args += [
        "-O3",
    ]

system = platform.system()

# Platform detection: CUDA vs Metal vs CPU-only
CUDA_HOME = os.environ.get("CUDA_HOME") or os.environ.get("CUDA_PATH") or "/usr/local/cuda"
HAS_CUDA = os.path.exists(os.path.join(CUDA_HOME, "bin", "nvcc"))
BUILD_METAL = system == "Darwin" and not HAS_CUDA and not NO_TRAIN

if system == "Linux":
    extra_compile_args += [
        "-Wno-alloc-size-larger-than",
        "-fmax-errors=3",
    ]
    extra_link_args += [
        "-Bsymbolic-functions",
    ]
elif system == "Darwin":
    extra_compile_args += [
        "-Wno-error=int-conversion",
        "-Wno-error=incompatible-function-pointer-types",
        "-Wno-error=implicit-function-declaration",
    ]
    extra_link_args += [
        "-framework",
        "Cocoa",
        "-framework",
        "OpenGL",
        "-framework",
        "IOKit",
    ]
else:
    raise ValueError(f"Unsupported system: {system}")  # noqa: TRY003

# Default Gym/Gymnasium/PettingZoo versions
# Gym:
# - 0.26 still has deprecation warnings and is the last version of the package
# - 0.25 adds a breaking API change to reset, step, and render_modes
# - 0.24 is broken
# - 0.22-0.23 triggers deprecation warnings by calling its own functions
# - 0.21 is the most stable version
# - <= 0.20 is missing dict methods for gym.spaces.Dict
# - 0.18-0.21 require setuptools<=65.5.0

# Extensions
extnames = ["pufferlib._C"]


class BuildExt(build_ext):
    """Build extension that compiles _C.so (torch-free) then C ocean extensions."""

    user_options = [*build_ext.user_options, ("precision=", None, "Precision: float or bf16 (default: bf16)")]

    def initialize_options(self):
        """Initialize precision option alongside parent options."""
        super().initialize_options()
        self.precision = "bf16"

    def finalize_options(self):
        """Finalize options, delegating to parent."""
        super().finalize_options()

    def run(self):
        """Build _C.so then run the C ocean extension build."""
        # Propagate any build_ext options (e.g., --inplace, --force) to subcommands
        build_ext_opts = self.distribution.command_options.get("build_ext", {})
        if build_ext_opts:
            self.distribution.command_options["build_c"] = build_ext_opts.copy()

        # Build _C.so (always torch-free)
        if not NO_TRAIN:
            if BUILD_METAL:
                _build_metal_C(force=self.force)
            else:
                _build_notorch_C(force=self.force, precision=self.precision)
        self.run_command("build_c")


class CBuildExt(build_ext):
    """Build extension that compiles only C ocean extensions (excludes _C.so)."""

    def run(self, *args, **kwargs):
        """Run build, filtering out extensions already built by BuildExt."""
        self.extensions = [e for e in self.extensions if e.name not in extnames]
        super().run(*args, **kwargs)


# Define cmdclass outside of setup to add dynamic commands
cmdclass = {
    "build_ext": BuildExt,
    "build_c": CBuildExt,
}


INCLUDE = [f"{BOX2D_NAME}/include", f"{BOX2D_NAME}/src"]
RAYLIB_A = f"{RAYLIB_NAME}/lib/libraylib.a"
extension_kwargs = dict(
    include_dirs=INCLUDE,
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
    extra_objects=[RAYLIB_A],
)

# Find C extensions
c_extensions = []
c_extension_paths = []
if not NO_OCEAN:
    c_extension_paths = glob.glob("pufferlib/ocean/**/binding.c", recursive=True)
    c_extensions = [
        Extension(
            path.rstrip(".c").replace("/", "."),
            sources=[path],
            **extension_kwargs,
        )
        for path in c_extension_paths
        if "matsci" not in path
    ]
    c_extension_paths = [os.path.join(*path.split("/")[:-1]) for path in c_extension_paths]

    for c_ext in c_extensions:
        if "impulse_wars" in c_ext.name:
            print(f"Adding {c_ext.name} to extra objects")
            c_ext.extra_objects.append(f"{BOX2D_NAME}/libbox2d.a")
            # TODO: Figure out why this is necessary for some users
            impulse_include = "pufferlib/ocean/impulse_wars/include"
            if impulse_include not in c_ext.include_dirs:
                c_ext.include_dirs.append(impulse_include)

        if "matsci" in c_ext.name:
            c_ext.include_dirs.append("/usr/local/include")
            c_ext.extra_link_args.extend(["-L/usr/local/lib", "-llammps"])


class ProfileTorchBuildExt(build_ext):
    """Build libprofile_torch.a — static torch library (slow, compile once)."""

    user_options = build_ext.user_options + [
        ("precision=", None, "Precision: float or bf16 (default: float)"),
    ]

    def initialize_options(self):
        """Initialize precision option alongside parent options."""
        super().initialize_options()
        self.precision = "float"

    def finalize_options(self):
        """Finalize options, delegating to parent."""
        super().finalize_options()

    def run(self):
        """Compile profile_torch_lib.cu into a static .a library via nvcc."""
        import subprocess
        import sysconfig

        import torch.utils.cpp_extension as cpp_ext

        src = "profile_torch_lib.cu"
        obj = "profile_torch_lib.o"
        lib = "libprofile_torch.a"

        nvcc = cpp_ext._join_cuda_home("bin", "nvcc")
        arch = "-arch=sm_89"
        precision_flag = "-DPRECISION_FLOAT" if self.precision == "float" else ""

        # Step 1: Compile to object file
        cmd = [
            nvcc,
            "-c",
            "-O3",
            arch,
            "-DUSE_TORCH",
            "-DPUFFERLIB_TORCH",
            "-I.",
            "-Ipufferlib/src",
            "-Xcompiler",
            "-fPIC",
        ]
        if precision_flag:
            cmd.append(precision_flag)
        cmd += ["-I" + sysconfig.get_path("include")]
        cmd += ["-I" + p for p in cpp_ext.include_paths()]
        cmd += [src, "-o", obj]

        print(f"Building torch lib object: {' '.join(cmd)}")
        subprocess.check_call(cmd)

        # Step 2: Create static library
        ar_cmd = ["ar", "rcs", lib, obj]
        print(f"Creating static library: {' '.join(ar_cmd)}")
        subprocess.check_call(ar_cmd)
        print(f"Built: {lib}")


cmdclass["build_profile_torch"] = ProfileTorchBuildExt


class ProfilerBuildExt(build_ext):
    """Build profile binary — fast compile, links libprofile_torch.a if present."""

    user_options = build_ext.user_options + [
        ("no-torch", None, "Build profiler without torch support"),
        ("env=", None, "Static env to link (e.g., breakout, drive)"),
        ("precision=", None, "Precision: float or bf16 (default: float)"),
    ]

    def initialize_options(self):
        """Initialize no_torch, env, and precision options alongside parent options."""
        super().initialize_options()
        self.no_torch = False
        self.env = None
        self.precision = "float"

    def finalize_options(self):
        """Finalize options, delegating to parent."""
        super().finalize_options()

    def run(self):
        """Compile profile_kernels.cu into a profile binary via nvcc, optionally linking torch."""
        import subprocess
        import sysconfig

        import torch.utils.cpp_extension as cpp_ext

        src = "profile_kernels.cu"
        out = "profile"
        torch_lib = "libprofile_torch.a"

        nvcc = cpp_ext._join_cuda_home("bin", "nvcc")
        arch = "-arch=sm_89"

        precision_flag = "-DPRECISION_FLOAT" if self.precision == "float" else ""

        # Check if we should link torch
        use_torch = not self.no_torch and os.path.exists(torch_lib)
        if not self.no_torch and not os.path.exists(torch_lib):
            print(f"Warning: {torch_lib} not found. Build it with: python setup.py build_profile_torch")
            print("Building without torch support.")

        if not use_torch:
            # No-torch build: fast, kernel-only
            cmd = [nvcc, "-O3", arch, "-I."]
            if precision_flag:
                cmd.append(precision_flag)
            cmd += [src, "-o", out]
        else:
            out = "profile_torch"
            lib_paths = cpp_ext.library_paths()
            nvtx_lib_dir = os.path.join(cpp_ext.CUDA_HOME, "lib64")

            cmd = [
                nvcc,
                "-O3",
                arch,
                "-DUSE_TORCH",
                "-DPUFFERLIB_TORCH",
                "-I.",
                "-Ipufferlib/src",
            ]
            if precision_flag:
                cmd.append(precision_flag)

            # Optional static env for envspeed profiling
            if self.env:
                static_lib = f"pufferlib/src/libstatic_{self.env}.a"
                if not os.path.exists(static_lib):
                    raise RuntimeError(
                        f"Static library not found: {static_lib}\nBuild it first with: python setup.py build_{self.env}"
                    )
                cmd += [
                    f"-DENV_NAME={self.env}",
                    "-DUSE_STATIC_ENV",
                    f"-I./{RAYLIB_NAME}/include",
                ]

            cmd += ["-I" + sysconfig.get_path("include")]
            cmd += ["-I" + p for p in cpp_ext.include_paths()]
            cmd += [src]
            # Link the static torch lib
            cmd += [torch_lib]
            cmd += ["-L" + p for p in lib_paths]
            cmd += ["-L" + nvtx_lib_dir]
            cmd += ["-Xlinker", "-rpath," + ":".join(lib_paths)]
            cmd += ["-Xlinker", "--no-as-needed"]
            cmd += [
                "-lc10",
                "-lc10_cuda",
                "-ltorch",
                "-ltorch_cpu",
                "-ltorch_cuda",
                "-lcublas",
                "-lcusolver",
                "-lcurand",
                "-lnvToolsExt",
                "-ldl",
                "-lnccl",
            ]
            cmd += ["-Xlinker", "--unresolved-symbols=ignore-in-shared-libs"]
            cmd += ["-Xlinker", "--allow-multiple-definition"]
            if self.env:
                static_lib = f"pufferlib/src/libstatic_{self.env}.a"
                cmd += [static_lib, f"./{RAYLIB_NAME}/lib/libraylib.a", "-lGL"]
            cmd += ["-lomp5"]
            cmd += ["-o", out]

        print(f"Building profiler: {' '.join(cmd)}")
        subprocess.check_call(cmd)
        print(f"Built: {out}")


cmdclass["build_profiler"] = ProfilerBuildExt

# Static env builds: clang-compiled env + gcc/nvcc torch extension
# Discover envs by listing folders in pufferlib/ocean
OCEAN_DIR = "pufferlib/ocean"
STATIC_ENVS = [
    name
    for name in os.listdir(OCEAN_DIR)
    if os.path.isdir(os.path.join(OCEAN_DIR, name))
    and not name.startswith("__")
    and os.path.exists(f"pufferlib/ocean/{name}/binding.c")
]


def _build_static_lib(env_name, force=False):
    """Build a static .a library for a given env using clang."""
    import subprocess

    env_binding_src = f"pufferlib/ocean/{env_name}/binding.c"
    static_lib = f"pufferlib/src/libstatic_{env_name}.a"
    static_obj = f"pufferlib/src/libstatic_{env_name}.o"

    env_deps = [env_binding_src, "pufferlib/src/vecenv.h"]
    if not force and not _needs_rebuild(static_lib, env_deps):
        print(f"Static env up to date: {static_lib}")
        return static_lib

    clang_cmd = [
        "clang",
        "-c",
        "-O2",
        "-DNDEBUG",
        "-I.",
        "-Ipufferlib/src",
        f"-Ipufferlib/ocean/{env_name}",
        "-Ipufferlib/ocean/osrs",
        f"-I./{RAYLIB_NAME}/include",
        "-DPLATFORM_DESKTOP",
        "-fno-semantic-interposition",
        "-fvisibility=hidden",
        "-fPIC",
    ]
    if HAS_CUDA:
        clang_cmd += ["-I/usr/local/cuda/include"]
    if system == "Darwin":
        # macOS needs Homebrew libomp for OpenMP (vecenv.h includes omp.h)
        import subprocess as _sp
        try:
            omp_prefix = _sp.check_output(["brew", "--prefix", "libomp"], text=True).strip()
            clang_cmd += [f"-I{omp_prefix}/include", "-Xclang", "-fopenmp"]
        except Exception:
            clang_cmd += ["-Xclang", "-fopenmp"]
    else:
        clang_cmd += ["-fopenmp"]
    clang_cmd += [env_binding_src, "-o", static_obj]
    print(f"Building static env: {' '.join(clang_cmd)}")
    subprocess.check_call(clang_cmd)

    ar_cmd = ["ar", "rcs", static_lib, static_obj]
    print(f"Creating static library: {' '.join(ar_cmd)}")
    subprocess.check_call(ar_cmd)
    return static_lib


def _needs_rebuild(output, sources):
    """Check if output needs rebuilding based on source mtimes."""
    if not os.path.exists(output):
        return True
    out_mtime = os.path.getmtime(output)
    for src in sources:
        if os.path.exists(src) and os.path.getmtime(src) > out_mtime:
            return True
    return False


# Headers that affect the single compilation unit (for incremental builds)
_BINDINGS_CU_DEPS = [
    "pufferlib/src/bindings.cu",
    "pufferlib/src/pufferlib.cu",
    "pufferlib/src/models.cu",
    "pufferlib/src/kernels.cu",
    "pufferlib/src/vecenv.h",
]


def _build_notorch_C(static_lib=None, force=False, precision="bf16"):
    """Build _C.so via single nvcc compile (no torch dependency)."""
    import subprocess
    import sysconfig

    cuda_home = os.environ.get("CUDA_HOME") or os.environ.get("CUDA_PATH") or "/usr/local/cuda"
    nvcc = os.path.join(cuda_home, "bin", "nvcc")
    ext_suffix = sysconfig.get_config_var("EXT_SUFFIX")
    output = f"pufferlib/_C{ext_suffix}"

    bindings_cu = "pufferlib/src/bindings.cu"
    bindings_o = "pufferlib/src/bindings.o"

    precision_flag = "-DPRECISION_FLOAT" if precision == "float" else ""

    # Check what needs rebuilding
    need_compile = force or _needs_rebuild(bindings_o, _BINDINGS_CU_DEPS)
    need_link = need_compile or force or _needs_rebuild(output, [bindings_o] + ([static_lib] if static_lib else []))

    if not need_link:
        print(f"Up to date: {output}")
        return

    python_include = sysconfig.get_path("include")
    pybind_include = pybind11.get_include()
    nvcc_cmd = [
        nvcc,
        "-c",
        "-Xcompiler",
        "-fPIC",
        "-Xcompiler=-D_GLIBCXX_USE_CXX11_ABI=1",
        "-Xcompiler=-DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION",
        "-Xcompiler=-DPLATFORM_DESKTOP",
        "-std=c++17",
        "-I.",
        "-Ipufferlib/src",
        f"-I{python_include}",
        f"-I{pybind_include}",
        f"-I{numpy.get_include()}",
        f"-I{cuda_home}/include",
        f"-I{RAYLIB_NAME}/include",
        "-Xcompiler=-fopenmp",
    ]
    if precision_flag:
        nvcc_cmd.append(precision_flag)
    if DEBUG:
        nvcc_cmd += ["-O0", "-g"]
    else:
        nvcc_cmd += ["-O3"]
    nvcc_cmd += [bindings_cu, "-o", bindings_o]

    if need_compile:
        print(f"nvcc: {' '.join(nvcc_cmd)}")
        subprocess.check_call(nvcc_cmd)
    else:
        print(f"nvcc: up to date ({bindings_o})")

    # Link into shared library
    link_cmd = [
        "g++",
        "-shared",
        "-fPIC",
        "-fopenmp",
        bindings_o,
    ]
    if static_lib:
        link_cmd.append(static_lib)
    link_cmd += [RAYLIB_A]
    link_cmd += [
        f"-L{cuda_home}/lib64",
        "-lcudart",
        "-lnccl",
        "-lnvidia-ml",
        "-lcublas",
        "-lcusolver",
        "-lcurand",
        "-lnvToolsExt",
        "-lomp5",
    ]
    if DEBUG:
        link_cmd += ["-g"]
    else:
        link_cmd += ["-O2"]
    link_cmd += extra_link_args
    link_cmd += ["-o", output]
    print(f"link: {' '.join(link_cmd)}")
    subprocess.check_call(link_cmd)
    print(f"Built: {output}")


# Metal compilation units (compiled to separate .o files):
# metal_bindings.mm includes metal_pufferlib.mm which includes metal_kernels.mm
# metal_platform.mm is standalone (Metal infra, GEMM, CUDA compat stubs)
_METAL_SOURCES = [
    "pufferlib/src/metal_bindings.mm",
    "pufferlib/src/metal_platform.mm",
]

# All files that affect the Metal build (for incremental rebuild checks)
_METAL_BINDINGS_DEPS = _METAL_SOURCES + [
    "pufferlib/src/metal_pufferlib.mm",
    "pufferlib/src/metal_kernels.mm",
    "pufferlib/src/metal_shader_src.h",
    "pufferlib/src/metal_platform.h",
    "pufferlib/src/puf_types.h",
    "pufferlib/src/vecenv.h",
]

METAL_FRAMEWORKS = [
    "-framework",
    "Metal",
    "-framework",
    "Accelerate",
    "-framework",
    "Foundation",
]


def _build_metal_C(static_lib=None, force=False):
    """Build _C.so via clang++ with Metal backend (macOS Apple Silicon)."""
    import subprocess
    import sysconfig

    ext_suffix = sysconfig.get_config_var("EXT_SUFFIX")
    output = f"pufferlib/_C{ext_suffix}"

    metal_sources = _METAL_SOURCES
    missing = [s for s in _METAL_BINDINGS_DEPS if not os.path.exists(s)]
    if missing:
        print(f"Metal source files not found: {missing}")
        print("Skipping Metal build. Create the source files first.")
        return

    python_include = sysconfig.get_path("include")
    pybind_include = pybind11.get_include()

    obj_files = []
    for src in metal_sources:
        obj = src.replace(".mm", ".o")
        obj_files.append(obj)

        if not force and not _needs_rebuild(obj, _METAL_BINDINGS_DEPS):
            print(f"clang++: up to date ({obj})")
            continue

        compile_cmd = [
            "clang++",
            "-c",
            "-fPIC",
            "-std=c++17",
            "-ObjC++",
            "-fobjc-arc",
            "-DPRECISION_FLOAT",
            "-DWITH_METAL",
            "-DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION",
            "-DPLATFORM_DESKTOP",
            "-I.",
            "-Ipufferlib/src",
            f"-I{python_include}",
            f"-I{pybind_include}",
            f"-I{numpy.get_include()}",
            f"-I{RAYLIB_NAME}/include",
        ]
        # OpenMP header for vecenv.h (via Homebrew libomp on macOS)
        import subprocess as _sp
        try:
            omp_prefix = _sp.check_output(["brew", "--prefix", "libomp"], text=True).strip()
            compile_cmd += [f"-I{omp_prefix}/include", "-Xclang", "-fopenmp"]
        except Exception:
            pass
        if DEBUG:
            compile_cmd += ["-O0", "-g"]
        else:
            compile_cmd += ["-O2", "-fno-semantic-interposition", "-fvisibility=hidden"]
        compile_cmd += [src, "-o", obj]

        print(f"clang++: {' '.join(compile_cmd)}")
        subprocess.check_call(compile_cmd)

    need_link = force or _needs_rebuild(output, obj_files + ([static_lib] if static_lib else []))
    if not need_link:
        print(f"Up to date: {output}")
        return

    link_cmd = ["clang++", "-shared", "-fPIC", "-undefined", "dynamic_lookup"]
    link_cmd += obj_files
    if static_lib:
        link_cmd.append(static_lib)
    link_cmd += [RAYLIB_A]
    link_cmd += METAL_FRAMEWORKS
    # Raylib requires these macOS frameworks
    link_cmd += ["-framework", "Cocoa", "-framework", "OpenGL", "-framework", "IOKit",
                 "-framework", "CoreGraphics", "-framework", "CoreFoundation",
                 "-framework", "CoreVideo", "-framework", "CoreAudio",
                 "-framework", "AudioToolbox", "-framework", "UniformTypeIdentifiers"]
    # OpenMP runtime for vecenv.h threading.
    #
    # Prefer torch's bundled libomp when available so sweep_bench (which uses
    # torch/gpytorch) and pufferlib._C share one OpenMP runtime in-process.
    # Mixing Homebrew libomp + torch libomp has caused sweep-time segfaults.
    omp_runtime = None
    omp_source = None
    try:
        import torch as _torch  # noqa: PLC0415

        torch_lib_dir = os.path.join(os.path.dirname(_torch.__file__), "lib")
        torch_omp = os.path.join(torch_lib_dir, "libomp.dylib")
        if os.path.exists(torch_omp):
            omp_runtime = torch_lib_dir
            omp_source = "torch"
    except Exception:
        pass

    if omp_runtime is None:
        import subprocess as _sp  # noqa: PLC0415
        try:
            omp_prefix = _sp.check_output(["brew", "--prefix", "libomp"], text=True).strip()
            omp_runtime = f"{omp_prefix}/lib"
            omp_source = "homebrew"
        except Exception:
            omp_runtime = None

    if omp_runtime is not None:
        link_cmd += [f"-L{omp_runtime}", f"-Wl,-rpath,{omp_runtime}", "-lomp"]
        print(f"Metal link: using OpenMP runtime from {omp_source}: {omp_runtime}/libomp.dylib")
    if DEBUG:
        link_cmd += ["-g"]
    else:
        link_cmd += ["-O2"]
    link_cmd += ["-o", output]
    print(f"link: {' '.join(link_cmd)}")
    subprocess.check_call(link_cmd)

    # Normalize OpenMP linkage to @rpath so we don't pin _C to a different
    # absolute libomp path than torch in the same process.
    if omp_source == "torch":
        omp_install_names = [
            "/opt/homebrew/opt/libomp/lib/libomp.dylib",
            "/usr/local/opt/libomp/lib/libomp.dylib",
        ]
        for install_name in omp_install_names:
            try:
                subprocess.check_call(
                    [
                        "install_name_tool",
                        "-change",
                        install_name,
                        "@rpath/libomp.dylib",
                        output,
                    ]
                )
            except subprocess.CalledProcessError:
                # No-op when the install name is not present in this binary.
                pass

    print(f"Built: {output}")


def create_static_env_build_class(env_name):
    """Create a build class that compiles env with clang, then links _C.so."""

    class StaticEnvBuildExt(build_ext):
        user_options = build_ext.user_options + [
            ("precision=", None, "Precision: float or bf16 (default: bf16)"),
        ]

        def initialize_options(self):
            super().initialize_options()
            self.precision = "bf16"

        def finalize_options(self):
            super().finalize_options()

        def run(self):
            static_lib = _build_static_lib(env_name, force=self.force)
            if BUILD_METAL:
                _build_metal_C(static_lib, force=self.force)
            else:
                _build_notorch_C(static_lib, force=self.force, precision=self.precision)

    return StaticEnvBuildExt


# Add build_<env> for static-linked envs (always available, torch optional)
for env_name in STATIC_ENVS:
    cmdclass[f"build_{env_name}"] = create_static_env_build_class(env_name)

if not NO_OCEAN:

    def create_env_build_class(full_name):
        """Create a build class that compiles only the named env extension."""

        class EnvBuildExt(build_ext):
            def run(self):
                self.extensions = [e for e in self.extensions if e.name == full_name]
                super().run()

        return EnvBuildExt

    # Add a build_<env>_so command for each env (dynamic .so build)
    for c_ext in c_extensions:
        env_name = c_ext.name.split(".")[-2]
        cmdclass[f"build_{env_name}_so"] = create_env_build_class(c_ext.name)


# No torch extensions in the default build — _C.so is built via subprocess (torch-free).
# Use build_profile_torch / build_profiler for torch-based profiling.
torch_extensions = []

# Prevent Conda from injecting garbage compile flags

cfg_vars = get_config_vars()
for key in ("CC", "CXX", "LDSHARED"):
    if cfg_vars[key]:
        cfg_vars[key] = cfg_vars[key].replace("-B /root/anaconda3/compiler_compat", "")
        cfg_vars[key] = cfg_vars[key].replace("-pthread", "")
        cfg_vars[key] = cfg_vars[key].replace("-fno-strict-overflow", "")

for key, value in cfg_vars.items():
    if value and "-fno-strict-overflow" in str(value):
        cfg_vars[key] = value.replace("-fno-strict-overflow", "")

install_requires = [
    "setuptools",
    "numpy<2.0",
    "shimmy[gym-v21]",
    "gym==0.23",
    "gymnasium>=0.29.1",
    "pettingzoo>=1.24.1",
]

if not NO_TRAIN:
    install_requires += [
        "torch>=2.9",
        "psutil",
        "nvidia-ml-py",
        "rich",
        "rich_argparse",
        "imageio",
        "gpytorch",
        "scikit-learn",
        "heavyball>=2.2.0",  # contains relevant fixes compared to 1.7.2 and 2.1.1
        "neptune",
        "wandb",
    ]

setup(
    version="3.0.0",
    packages=find_namespace_packages() + find_packages() + c_extension_paths + ["pufferlib/src"],
    package_data={"pufferlib": [RAYLIB_NAME + "/lib/libraylib.a"]},
    include_package_data=True,
    install_requires=install_requires,
    ext_modules=c_extensions,
    cmdclass=cmdclass,
    include_dirs=[numpy.get_include(), RAYLIB_NAME + "/include"],
)
