from pathlib import Path
import runpy

path = Path(__file__).with_name("agent_bitfield_rv64_patch.py")
text = path.read_text()
old = '    "    if (program == NULL || expression_id >= program->expression_count) {\\n"\n'
new = '    "    if (expression_id >= program->expression_count) {\\n"\n'
if text.count(old) < 2:
    raise SystemExit(f"expected both old/new accessor anchors, got {text.count(old)}")
text = text.replace(old, new, 1)
path.write_text(text)
runpy.run_path(str(path), run_name="__main__")
