import subprocess
from pathlib import Path


def test_native_checkpoint_requires_exact_shape(tmp_path: Path) -> None:
    source = (Path(__file__).resolve().parents[1] / "src/pufferl.cu").read_text()
    start = source.index("void puf_load_weights_into(")
    end = source.index("    cudaMemcpy(dst.data, buf, nbytes", start)
    harness = tmp_path / "checkpoint_shape.c"
    harness.write_text(
        "#include <assert.h>\n#include <stdint.h>\n#include <stdio.h>\n"
        "#include <stdlib.h>\n#include <sys/stat.h>\n"
        "typedef struct { long shape[1]; } Float;\n"
        "typedef Float Prec;\ntypedef int cudaStream_t;\n"
        "static long numel(long* shape) { return shape[0]; }\n"
        + source[start:end]
        + "    free(buf);\n}\n"
        "int main(int argc, char** argv) {\n"
        "    assert(argc == 2);\n"
        "    puf_load_weights_into((Float){{4}}, (Prec){{4}}, 0, argv[1]);\n"
        "}\n"
    )
    executable = tmp_path / "checkpoint_shape"
    subprocess.run(["cc", "-std=gnu11", str(harness), "-o", str(executable)], check=True)
    for size in (12, 16, 20):
        checkpoint = tmp_path / f"weights-{size}.bin"
        checkpoint.write_bytes(bytes(size))
        result = subprocess.run([str(executable), str(checkpoint)], capture_output=True)
        assert (result.returncode == 0) == (size == 16)
        if size != 16:
            assert b"checkpoint shape mismatch" in result.stderr
