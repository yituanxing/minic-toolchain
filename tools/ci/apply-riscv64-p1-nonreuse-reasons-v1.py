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
# Disambiguate "address derived" (often ordinary pointer arithmetic from
# GNU macro expansion) from real escapes to callees, inline asm or return.
# This is READ-ONLY, and does not expand the accepted stack-slot set.
begin = "                    if (life->reusable && life->block_index < function->block_count) {"
assert s.count(begin)==1
extra = r'''                    if (life->nonreuse_reason != NULL &&
                        strcmp(life->nonreuse_reason, "address_derived_or_escaped") == 0) {
                        size_t definition_block;
                        for (definition_block = 0U; definition_block < function->block_count;
                             ++definition_block) {
                            const MinicCoreBlock *origin =
                                &function->blocks[definition_block];
                            size_t definition_position;
                            for (definition_position = 0U;
                                 definition_position < origin->instruction_count;
                                 ++definition_position) {
                                const MinicCoreInstruction *producer =
                                    &function->instructions[
                                        origin->instructions[definition_position]];
                                size_t use_block;
                                MinicCoreValueId address_value = producer->result;
                                if (producer->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
                                    producer->value.object_id != i ||
                                    address_value == MINIC_CORE_VALUE_INVALID) {
                                    continue;
                                }
                                for (use_block = 0U; use_block < function->block_count;
                                     ++use_block) {
                                    const MinicCoreBlock *consumer_block =
                                        &function->blocks[use_block];
                                    size_t pos;
                                    for (pos = 0U; pos < consumer_block->instruction_count;
                                         ++pos) {
                                        const MinicCoreInstruction *consumer =
                                            &function->instructions[
                                                consumer_block->instructions[pos]];
                                        CoreTargetValueUseCount hits;
                                        const char *category = "other_instruction";
                                        hits.target = address_value;
                                        hits.count = 0U;
                                        if (!core_visit_instruction_value_uses(
                                                function, consumer,
                                                core_count_target_value_use, &hits) ||
                                            hits.count == 0U) {
                                            continue;
                                        }
                                        if (consumer->kind == MINIC_CORE_INSTRUCTION_POINTER_OFFSET) {
                                            category = "pointer_offset";
                                        } else if (consumer->kind == MINIC_CORE_INSTRUCTION_FIELD_ADDRESS) {
                                            category = "field_address";
                                        } else if (consumer->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                                                   consumer->value.load.address == address_value) {
                                            category = "direct_load";
                                        } else if (consumer->kind == MINIC_CORE_INSTRUCTION_STORE &&
                                                   consumer->value.store.address == address_value) {
                                            category = "direct_store";
                                        } else if (consumer->kind == MINIC_CORE_INSTRUCTION_STORE &&
                                                   consumer->value.store.stored_value == address_value) {
                                            category = "stored_pointer_escape";
                                        } else if (consumer->kind == MINIC_CORE_INSTRUCTION_CALL ||
                                                   consumer->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
                                            category = "call_argument";
                                        } else if (consumer->kind == MINIC_CORE_INSTRUCTION_SCALAR_BITCAST ||
                                                   consumer->kind == MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION) {
                                            category = "cast";
                                        } else if (consumer->kind ==
                                                       MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM ||
                                                   consumer->kind ==
                                                       MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
                                            category = "inline_asm";
                                        }
                                        (void)fprintf(stderr,
                                            "CORE_FRAME_DERIVED_USE function=%s id=%zu "
                                            "category=%s kind=%u n=%zu\n",
                                            symbol_name, i, category,
                                            (unsigned int)consumer->kind, hits.count);
                                        (void)fprintf(stderr,
                                            "CORE_FRAME_USE_SPAN function=%s id=%zu "
                                            "kind=%u line=%zu offset=%zu\n",
                                            symbol_name, i, (unsigned int)consumer->kind,
                                            consumer->span.begin.line,
                                            consumer->span.begin.offset);
                                    }
                                    if (consumer_block->has_terminator) {
                                        CoreTargetValueUseCount hits;
                                        hits.target = address_value;
                                        hits.count = 0U;
                                        if (core_visit_terminator_value_uses(
                                                &consumer_block->terminator,
                                                core_count_target_value_use, &hits) &&
                                            hits.count != 0U) {
                                            (void)fprintf(stderr,
                                                "CORE_FRAME_DERIVED_USE function=%s id=%zu "
                                                "category=terminator kind=0 n=%zu\n",
                                                symbol_name, i, hits.count);
                                        }
                                    }
                                }
                            }
                        }
                    }
'''
s=s.replace(begin,extra+begin,1)

# Add source provenance for all CoreObjects; the preprocessed source uses
# generated .i line numbers because GNU -E -P removes #line markers.
object_source_marker = (
    '                    if (life->reusable && life->block_index < function->block_count) {'
)
assert s.count(object_source_marker) == 1
s=s.replace(
    object_source_marker,
    '                    (void)fprintf(stderr,\n'
    '                        "CORE_FRAME_OBJ_SPAN function=%s id=%zu "\n'
    '                        "line=%zu offset=%zu\\n",\n'
    '                        symbol_name, i, function->objects[i].span.begin.line,\n'
    '                        function->objects[i].span.begin.offset);\n'
    +object_source_marker,
    1
)

p.write_text(s)
print("P1_NONREUSABLE_REASON_AUDIT_V1=APPLIED observational_only=true")
