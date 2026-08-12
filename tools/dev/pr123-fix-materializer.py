from pathlib import Path

path = Path("tools/dev/pr123-materialize.py")
text = path.read_text()
old = '''end = text.find("\\nstatic bool minic_riscv64_emit_global_objects(", start)
if start < 0 or end < 0:
    raise SystemExit(f"global emitter region mismatch start={start} end={end}")
region = text[start:end]
if region.count("if (object->is_zero_initialized) {") != 1:
    raise SystemExit(
        f"global zero branch mismatch: {region.count('if (object->is_zero_initialized) {')}"
    )
region = region.replace(
    "if (object->is_zero_initialized) {",
    "if (object->is_zero_initialized || object->is_tentative) {",
    1,
)
'''
new = '''end = text.find("\\nstatic bool minic_riscv64_emit_function(", start)
if start < 0 or end < 0:
    raise SystemExit(f"global emitter region mismatch start={start} end={end}")
region = text[start:end]
if region.count("if (object->is_zero_initialized) {") != 2:
    raise SystemExit(
        f"global zero branch mismatch: {region.count('if (object->is_zero_initialized) {')}"
    )
region = region.replace(
    "if (object->is_zero_initialized) {",
    "if (object->is_zero_initialized || object->is_tentative) {",
)
'''
if text.count(old) != 1:
    raise SystemExit(f"PR123 codegen materializer patch mismatch: {text.count(old)}")
path.write_text(text.replace(old, new, 1))
