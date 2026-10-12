#!/usr/bin/env python3
"""EXACT instrumentation-only classification of why a CoreObject is permanent.

Applied after the audited runtime 43-patch profile and P1 frame instrumentation.
Only adds a string diagnostic member to CoreScalarObjectInterval and stderr
messages. No offset assignment, stack layout, asm, or instruction changes.
Three pinned C objects MUST remain identical at the SHA-256 byte level.
"""
from pathlib import Path

p=Path("src/target/riscv64/core_codegen.c")
s=p.read_text()
assert "P1_FRAME_DIAGNOSTIC_ONLY_V1" in s
assert "CORE_OBJECT_SLOT_REUSE_V0" in s
assert "P1_NONREUSABLE_REASON_AUDIT_V1" not in s

def replace_once(blob: str, old: str, new: str, label: str) -> str:
    n=blob.count(old)
    if n!=1:
        raise SystemExit(f"P1_NONREUSABLE_REASON=ERROR anchor={label} matched={n}")
    return blob.replace(old,new,1)

start=s.index("typedef struct CoreScalarObjectInterval {")
end=s.index("} CoreScalarObjectInterval;",start)
header=s[start:end]
header=replace_once(header, "    bool reusable;\n",
    "    bool reusable;\n"
    "    const char *nonreuse_reason; /* P1_NONREUSABLE_REASON_AUDIT_V1 */\n",
    "struct")
s=s[:start]+header+s[end:]

start=s.index("static bool core_scalar_object_interval(")
end=s.index("static bool core_scalar_object_intervals_overlap(",start)
f=s[start:end]
f=replace_once(
    f,
    "    interval->reusable = false;\n    interval->block_index = SIZE_MAX;",
    "    interval->reusable = false;\n"
    "    interval->nonreuse_reason = \"unreferenced_object\";\n"
    "    interval->block_index = SIZE_MAX;",
    "init")
f=replace_once(
    f,
    "        function->objects[object_id].explicit_alignment > 8U) {\n"
    "        return true;",
    "        function->objects[object_id].explicit_alignment > 8U) {\n"
    "        interval->nonreuse_reason = \"shape_or_alignment\";\n"
    "        return true;",
    "shape")
f=replace_once(
    f,
    "            block->terminator.return_object == object_id) {\n"
    "            interval->reusable = false;",
    "            block->terminator.return_object == object_id) {\n"
    "            interval->nonreuse_reason = \"return_aggregate\";\n"
    "            interval->reusable = false;",
    "return_object")
f=replace_once(
    f,
    "                 instruction->value.call.result_object == object_id)) {\n"
    "                interval->reusable = false;",
    "                 instruction->value.call.result_object == object_id)) {\n"
    "                interval->nonreuse_reason = \"record_or_call_result\";\n"
    "                interval->reusable = false;",
    "record_result")
f=replace_once(
    f,
    "                        argument->value.object_id == object_id) {\n"
    "                        interval->reusable = false;",
    "                        argument->value.object_id == object_id) {\n"
    "                        interval->nonreuse_reason = \"call_object_argument\";\n"
    "                        interval->reusable = false;",
    "call_arg")
f=replace_once(
    f,
    "                if (!core_scalar_object_note_position(interval, block_index, position)) {\n"
    "                    interval->reusable = false;",
    "                if (!core_scalar_object_note_position(interval, block_index, position)) {\n"
    "                    interval->nonreuse_reason = \"parameter_multiple_blocks\";\n"
    "                    interval->reusable = false;",
    "parameter")
f=replace_once(
    f,
    "                    !core_scalar_object_note_position(interval, block_index, position)) {\n"
    "                    interval->reusable = false;",
    "                    !core_scalar_object_note_position(interval, block_index, position)) {\n"
    "                    interval->nonreuse_reason = \"address_invalid_or_multiple_blocks\";\n"
    "                    interval->reusable = false;",
    "address_note")
f=replace_once(
    f,
    "                                    interval, use_block, use_position)) {\n"
    "                                interval->reusable = false;",
    "                                    interval, use_block, use_position)) {\n"
    "                                interval->nonreuse_reason = \"cross_block_direct_load_store\";\n"
    "                                interval->reusable = false;",
    "crossblock_load")
f=replace_once(
    f,
    "                if (total_uses.count != allowed_uses) {\n"
    "                    interval->reusable = false;",
    "                if (total_uses.count != allowed_uses) {\n"
    "                    interval->nonreuse_reason = \"address_derived_or_escaped\";\n"
    "                    interval->reusable = false;",
    "escaped")
f=replace_once(
    f,
    "    return true;\n}\n\n",
    "    if (interval->reusable) {\n"
    "        interval->nonreuse_reason = \"reusable\";\n"
    "    }\n"
    "    return true;\n}\n\n",
    "interval_final")
s=s[:start]+f+s[end:]

# Existing audited frame observer is in emit_core_function_with_symbol.
mark=(
    '                        life->last_position);\n'
    '                    if (life->reusable && life->block_index < function->block_count) {'
)
s=replace_once(s,mark,
    '                        life->last_position);\n'
    '                    (void)fprintf(stderr,\n'
    '                        "CORE_FRAME_REASON function=%s id=%zu reason=%s\\n",\n'
    '                        symbol_name, i,\n'
    '                        life->nonreuse_reason != NULL\n'
    '                          ? life->nonreuse_reason : "unset");\n'
    '                    if (life->reusable && life->block_index < function->block_count) {',
    "observer")
p.write_text(s)
print("P1_NONREUSABLE_REASON_AUDIT_V1=APPLIED observational_only=true")
