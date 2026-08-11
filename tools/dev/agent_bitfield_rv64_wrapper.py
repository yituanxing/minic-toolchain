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

# parse_unary names the already-parsed operand expression id `operand`.
wrong_operand = 'minic_c0_expression_bit_field(parser->program, operand_id)'
if text.count(wrong_operand) != 1:
    raise SystemExit(f"expected one stale unary operand id, got {text.count(wrong_operand)}")
text = text.replace(wrong_operand,
                    'minic_c0_expression_bit_field(parser->program, operand)',
                    1)

# _Bool precision is one bit for width validation, but its storage/alignment boundary
# remains one byte. DataLayout must use storage bits for the boundary-crossing rule.
bool_storage_override = '''            if (minic_type_is_bool_integer(field->type)) {
                type_bits = 1U;
            }
'''
if text.count(bool_storage_override) != 1:
    raise SystemExit(f"expected one DataLayout bool storage override, got {text.count(bool_storage_override)}")
text = text.replace(bool_storage_override, "", 1)

path.write_text(text)
runpy.run_path(str(path), run_name="__main__")

# re.sub replacement strings interpret \0 specially. The generated C must contain the
# two-character escape sequence, never an embedded NUL byte.
record_path = Path(__file__).resolve().parents[2] / "src/frontend/parser_record.c"
data = record_path.read_bytes()
if b"\x00" in data:
    data = data.replace(b"'\x00'", b"'\\0'")
    record_path.write_bytes(data)
if b"\x00" in record_path.read_bytes():
    raise SystemExit("embedded NUL remains in generated parser_record.c")
