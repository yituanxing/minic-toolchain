from pathlib import Path
import runpy

path = Path(__file__).with_name("agent_bitfield_rv64_patch.py")
text = path.read_text()

# The production accessor currently has no null guard; let the generator match that old
# shape while still emitting the safer guarded accessor in the generated source.
old = '    "    if (program == NULL || expression_id >= program->expression_count) {\\n"\n'
new = '    "    if (expression_id >= program->expression_count) {\\n"\n'
if text.count(old) < 2:
    raise SystemExit(f"expected both old/new accessor anchors, got {text.count(old)}")
text = text.replace(old, new, 1)

# Simple and compound assignment intentionally contain the same scalar-store text.
# Preserve strict uniqueness for every other generator anchor, but let the first of
# these two known consumers replace the first occurrence; the second is then unique.
old_helper = '''def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact match, got {count}")
    return text.replace(old, new, 1)
'''
new_helper = '''def replace_once(text, old, new, label):
    count = text.count(old)
    if label == "bit-field simple assignment store":
        if count != 2:
            raise SystemExit(f"{label}: expected two ordered assignment-store matches, got {count}")
        return text.replace(old, new, 1)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact match, got {count}")
    return text.replace(old, new, 1)
'''
if text.count(old_helper) != 1:
    raise SystemExit("cannot locate strict replace_once helper")
text = text.replace(old_helper, new_helper, 1)

path.write_text(text)
runpy.run_path(str(path), run_name="__main__")
