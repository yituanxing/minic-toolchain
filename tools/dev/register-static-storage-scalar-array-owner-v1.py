from pathlib import Path

path = Path(__file__).resolve().parents[2] / "tests/compiler/c0/run.sh"
text = path.read_text()
invocation = '''\nMINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\n  sh "$root/tests/compiler/c0/run-static-storage-scalar-array-owner.sh"\n'''
if "run-static-storage-scalar-array-owner.sh" not in text:
    text += invocation
path.write_text(text)
print("registered static-storage scalar-array owner gate")
