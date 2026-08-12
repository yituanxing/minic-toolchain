from pathlib import Path

source = Path("tools/dev/pr111-materialize.py").read_text()
prefix, separator, _ = source.partition('run_sh = Path("tests/compiler/c0/run.sh")')
if not separator:
    raise SystemExit("cannot locate run.sh materializer tail")
exec(compile(prefix, "tools/dev/pr111-materialize.py", "exec"))

run_sh = Path("tests/compiler/c0/run.sh")
run_text = run_sh.read_text()
needle = 'MINIC="$minic" BUILD_DIR="$work/gnu-weak-function-symbol" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-gnu-weak-function-symbol.sh"\n'
if run_text.count(needle) != 1:
    raise SystemExit(f"run.sh final gate anchor mismatch: {run_text.count(needle)}")
run_text = run_text.replace(
    needle,
    needle
    + '\nMINIC="$minic" BUILD_DIR="$work/pragma-pack-record-layout" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-pragma-pack-record-layout.sh"\n',
    1,
)
run_sh.write_text(run_text)
