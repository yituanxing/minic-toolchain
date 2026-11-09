#!/usr/bin/env python3
"""Exercise legal empty .s versus a successful compiler that creates no .s.

Kbuild can select an object whose active configuration emits zero definitions.
GNU as accepts the existing zero-byte file, but not a nonexistent input.
"""
import hashlib
import importlib.util
from pathlib import Path
import tempfile


ROOT = Path(__file__).resolve().parent


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fake_compiler(folder, mode):
    path = folder / ("fake-" + mode)
    action = {
        "empty": "out.touch()",
        "missing": "pass",
        "error": "sys.exit(19)",
    }[mode]
    path.write_text(
        "#!/usr/bin/env python3\n"
        "import sys\n"
        "from pathlib import Path\n"
        "out = Path(sys.argv[sys.argv.index('-o') + 1])\n"
        + action + "\n")
    path.chmod(0o755)
    return path


def main():
    first500 = load("first500", "linux-perf-first500-eligible-paired-v1.py")
    frozen = load("frozen", "linux-perf-frozen-corpus-differential-v1.py")
    full = load("full", "linux-perf-2064-paired-compile-v1.py")
    digest = hashlib.sha256(b"").hexdigest()
    with tempfile.TemporaryDirectory(prefix="minic-empty-asm-") as tmp:
        folder = Path(tmp)
        source = folder / "source.i"
        source.write_text("/* CONFIG disabled: empty translation unit */\n")
        for mode in ("empty", "missing", "error"):
            exe = fake_compiler(folder, mode)
            result = first500.compile_owner(exe, source, folder / (mode + "-500.s"))
            assert result["accepted"] == (mode == "empty"), (mode, result)
            assert result["returncode"] == (19 if mode == "error" else 0)
            if mode == "empty":
                assert result["empty_assembly"] and result["assembly_bytes"] == 0
                assert result["assembly_sha256"] == digest
            result = frozen.compile_one(exe, source, folder / (mode + "-frozen.s"))
            assert (result["status"] == "PASS") == (mode == "empty")
            if mode == "empty":
                assert result["bytes"] == 0 and result["sha256"] == digest
            try:
                _, asm_sha = full.run(
                    exe, source, folder / (mode + "-2064.s"),
                    folder / "stdout", folder / "stderr", "empty.o",
                    "baseline", [])
            except RuntimeError:
                assert mode != "empty"
            else:
                assert mode == "empty" and asm_sha == digest
    print("MINIC_LINUX_EMPTY_ASSEMBLY=PASS empty_valid=3 missing_rejected=3 rc_error_rejected=3")


if __name__ == "__main__":
    main()
