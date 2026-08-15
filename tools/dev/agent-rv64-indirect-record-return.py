#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text()


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


# TargetABI: one canonical entry point prepares the explicit-argument cursor
# from the return ABI.  Only the actually-proven >2*XLEN indirect record case
# reserves a0; smaller unsupported record shapes stay fail-closed until the
# hard-float aggregate rules are implemented.
path = "src/target/riscv64/abi.h"
text = read(path)
text = replace_once(
    text,
    "void minic_riscv64_abi_cursor_initialize(MinicRiscv64AbiCursor *cursor);\n",
    "void minic_riscv64_abi_cursor_initialize(MinicRiscv64AbiCursor *cursor);\n"
    "bool minic_riscv64_abi_cursor_initialize_for_return(\n"
    "    const MinicC0Program *program,\n"
    "    MinicType return_type,\n"
    "    MinicRiscv64AbiCursor *cursor,\n"
    "    MinicRiscv64AbiValue *return_value);\n",
    "abi return cursor declaration",
)
write(path, text)

path = "src/target/riscv64/abi.c"
text = read(path)
anchor = '''void minic_riscv64_abi_cursor_initialize(MinicRiscv64AbiCursor *cursor) {
    if (cursor == NULL) {
        return;
    }
    cursor->integer_register_count = 0U;
    cursor->floating_register_count = 0U;
    cursor->stack_slot_count = 0U;
}
'''
insert = anchor + '''
bool minic_riscv64_abi_cursor_initialize_for_return(
    const MinicC0Program *program,
    MinicType return_type,
    MinicRiscv64AbiCursor *cursor,
    MinicRiscv64AbiValue *return_value) {
    MinicRiscv64AbiCursor next;
    MinicRiscv64AbiValue result;

    if (cursor == NULL || return_value == NULL ||
        !minic_riscv64_abi_classify_value(program, return_type, &result)) {
        return false;
    }
    minic_riscv64_abi_cursor_initialize(&next);
    if (result.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        if (!minic_type_is_record(return_type) || result.storage_size <= 16U) {
            return false;
        }
        next.integer_register_count = 1U;
    }
    *cursor = next;
    *return_value = result;
    return true;
}
'''
text = replace_once(text, anchor, insert, "abi return cursor definition")
write(path, text)

# Shared backend interface: frame layout remembers the hidden result pointer,
# and statement lowering can ask expression lowering to materialize a record
# return into that caller-owned destination.
path = "src/target/riscv64/codegen_internal.h"
text = read(path)
text = replace_once(
    text,
    '''typedef struct MinicRiscv64FrameLayout {
    size_t frame_size;
    size_t saved_ra_offset;
    size_t saved_s0_offset;
    size_t varargs_offset;
    size_t varargs_size;
    size_t integer_parameter_count;
} MinicRiscv64FrameLayout;
''',
    '''typedef struct MinicRiscv64FrameLayout {
    size_t frame_size;
    size_t saved_ra_offset;
    size_t saved_s0_offset;
    size_t varargs_offset;
    size_t varargs_size;
    size_t integer_parameter_count;
    bool has_indirect_return;
    size_t indirect_return_offset;
} MinicRiscv64FrameLayout;
''',
    "frame indirect return slot",
)
text = replace_once(
    text,
    '''bool minic_riscv64_emit_record_copy_value(FILE *file,
                                          const MinicC0Program *program,
                                          const MinicFunction *function,
                                          const MinicRiscv64FunctionLayout *function_layout,
                                          MinicExpressionId target_id,
                                          MinicExpressionId source_id,
                                          bool preserve_target_address);
''',
    '''bool minic_riscv64_emit_record_return_value(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicExpressionId source_id,
    size_t result_pointer_offset);
bool minic_riscv64_emit_record_copy_value(FILE *file,
                                          const MinicC0Program *program,
                                          const MinicFunction *function,
                                          const MinicRiscv64FunctionLayout *function_layout,
                                          MinicExpressionId target_id,
                                          MinicExpressionId source_id,
                                          bool preserve_target_address);
''',
    "record return helper declaration",
)
write(path, text)

# Frame layout: reserve a stable frame slot for the incoming hidden result
# pointer and place explicit parameters after its abstract a0 slot.
path = "src/target/riscv64/codegen_support.c"
text = read(path)
start = text.find("bool minic_riscv64_frame_layout_from_function_layout(\n")
if start < 0:
    raise SystemExit("frame layout function not found")
new_frame = r'''bool minic_riscv64_frame_layout_from_function_layout(
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicRiscv64FrameLayout *layout) {
    MinicRiscv64AbiCursor abi_cursor;
    MinicRiscv64AbiValue return_value;
    size_t hidden_return_size;
    size_t metadata_size;
    size_t parameter_index;
    size_t required_bytes;
    size_t varargs_size;

    if (program == NULL || function == NULL || function_layout == NULL || layout == NULL ||
        function->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        !minic_riscv64_abi_cursor_initialize_for_return(
            program, function->return_type, &abi_cursor, &return_value)) {
        return false;
    }

    for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
        const MinicLocal *parameter;
        MinicRiscv64AbiArgumentLocation location;

        parameter = minic_c0_program_local(program, function->local_begin + parameter_index);
        if (parameter == NULL || !minic_riscv64_abi_place_argument(
                                     program, parameter->type, true, &abi_cursor, &location)) {
            return false;
        }
    }
    if (function->is_variadic && abi_cursor.stack_slot_count != 0U) {
        return false;
    }

    hidden_return_size = return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT ? 8U : 0U;
    metadata_size = 16U + hidden_return_size;
    varargs_size = function->is_variadic ? (8U - abi_cursor.integer_register_count) * 8U : 0U;
    if (function_layout->local_storage_size > SIZE_MAX - metadata_size ||
        function_layout->local_storage_size + metadata_size > SIZE_MAX - varargs_size) {
        return false;
    }
    required_bytes = function_layout->local_storage_size + metadata_size + varargs_size;
    if (required_bytes > SIZE_MAX - 15U) {
        return false;
    }

    layout->frame_size = (required_bytes + 15U) & ~(size_t)15U;
    layout->varargs_size = varargs_size;
    layout->varargs_offset = layout->frame_size - varargs_size;
    if (layout->varargs_offset < metadata_size ||
        function_layout->local_storage_size > layout->varargs_offset - metadata_size) {
        return false;
    }
    layout->saved_ra_offset = layout->varargs_offset - 8U;
    layout->saved_s0_offset = layout->varargs_offset - 16U;
    layout->has_indirect_return = return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT;
    layout->indirect_return_offset =
        layout->has_indirect_return ? layout->varargs_offset - 24U : 0U;
    layout->integer_parameter_count = abi_cursor.integer_register_count;
    return true;
}
'''
text = text[:start] + new_frame
write(path, text)

# Callee entry: save the hidden result pointer before a0 is reused and start
# explicit parameter placement from the return-aware ABI cursor.
path = "src/target/riscv64/codegen_function.c"
text = read(path)
text = replace_once(
    text,
    '''    if (success) {
        success = minic_riscv64_emit_sp_store64(file, "ra", frame_layout.saved_ra_offset) &&
                  minic_riscv64_emit_sp_store64(file, "s0", frame_layout.saved_s0_offset) &&
                  fprintf(file, "  mv s0, sp\\n") >= 0;
    }
    if (success && function->is_variadic) {
''',
    '''    if (success) {
        success = minic_riscv64_emit_sp_store64(file, "ra", frame_layout.saved_ra_offset) &&
                  minic_riscv64_emit_sp_store64(file, "s0", frame_layout.saved_s0_offset) &&
                  fprintf(file, "  mv s0, sp\\n") >= 0;
    }
    if (success && frame_layout.has_indirect_return) {
        success = minic_riscv64_emit_sp_store64(file, "a0", frame_layout.indirect_return_offset);
    }
    if (success && function->is_variadic) {
''',
    "callee save indirect result pointer",
)
text = replace_once(
    text,
    '''    if (success) {
        MinicRiscv64AbiCursor abi_cursor;
        size_t parameter_index;

        minic_riscv64_abi_cursor_initialize(&abi_cursor);
        for (parameter_index = 0U; success && parameter_index < function->parameter_count;
''',
    '''    if (success) {
        MinicRiscv64AbiCursor abi_cursor;
        MinicRiscv64AbiValue return_value;
        size_t parameter_index;

        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &abi_cursor, &return_value) ||
            (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) !=
                frame_layout.has_indirect_return) {
            success = false;
        }
        for (parameter_index = 0U; success && parameter_index < function->parameter_count;
''',
    "callee return-aware argument cursor",
)
write(path, text)

# Expression lowering: indirect record calls receive caller-owned result storage.
path = "src/target/riscv64/codegen_expression.c"
text = read(path)
include_anchor = '#include <string.h>\n'
forward = r'''
static bool minic_riscv64_emit_expression_impl(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicExpressionId expression_id,
    size_t record_result_temporary_size);
'''
text = replace_once(text, include_anchor, include_anchor + forward, "expression impl declaration")
old_call_temp = r'''    if (source->kind == MINIC_EXPRESSION_CALL) {
        size_t aggregate_size;
        size_t aggregate_chunks;

        if (!minic_riscv64_integer_aggregate_abi(
                program, source->type, &aggregate_size, &aggregate_chunks) ||
            aggregate_size != storage_size ||
            !minic_riscv64_emit_expression(file, program, function, function_layout, source_id) ||
            !minic_riscv64_emit_stack_allocate(file, temporary_size) ||
            fprintf(file, "  sd a0, 0(sp)\n") < 0 ||
            (aggregate_chunks == 2U && fprintf(file, "  sd a1, 8(sp)\n") < 0)) {
            return false;
        }
        return aggregate_chunks == 1U || aggregate_chunks == 2U;
    }
'''
new_call_temp = r'''    if (source->kind == MINIC_EXPRESSION_CALL) {
        MinicRiscv64AbiValue value;

        if (!minic_riscv64_abi_classify_value(program, source->type, &value) ||
            value.storage_size != storage_size) {
            return false;
        }
        if (value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            return storage_size > 16U && temporary_size >= storage_size &&
                   (temporary_size & 15U) == 0U &&
                   minic_riscv64_emit_expression_impl(file,
                                                       program,
                                                       function,
                                                       function_layout,
                                                       source_id,
                                                       temporary_size);
        }
        if (value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE || value.slot_count == 0U ||
            value.slot_count > 2U ||
            !minic_riscv64_emit_expression(file, program, function, function_layout, source_id) ||
            !minic_riscv64_emit_stack_allocate(file, temporary_size) ||
            fprintf(file, "  sd a0, 0(sp)\n") < 0 ||
            (value.slot_count == 2U && fprintf(file, "  sd a1, 8(sp)\n") < 0)) {
            return false;
        }
        return true;
    }
'''
text = replace_once(text, old_call_temp, new_call_temp, "record call temporary")

copy_start = text.find("bool minic_riscv64_emit_record_copy_value(FILE *file,\n")
copy_end = text.find("static bool\nminic_riscv64_emit_record_assignment_expression", copy_start)
if copy_start < 0 or copy_end < 0:
    raise SystemExit("record copy function bounds not found")
new_copy = r'''static bool minic_riscv64_emit_record_temporary_to_address(FILE *file,
                                                            size_t storage_size,
                                                            size_t temporary_size,
                                                            const char *address_register,
                                                            bool preserve_address) {
    size_t index;

    if (file == NULL || address_register == NULL || storage_size == 0U ||
        temporary_size < storage_size ||
        (preserve_address && fprintf(file, "  mv t4, %s\n", address_register) < 0) ||
        fprintf(file, "  mv t2, sp\n  mv t3, %s\n", address_register) < 0) {
        return false;
    }
    for (index = 0U; index < storage_size; ++index) {
        if (fprintf(file,
                    "  lbu t0, 0(t2)\n"
                    "  sb t0, 0(t3)\n"
                    "  addi t2, t2, 1\n"
                    "  addi t3, t3, 1\n") < 0) {
            return false;
        }
    }
    if (!minic_riscv64_emit_stack_release(file, temporary_size)) {
        return false;
    }
    return !preserve_address || fprintf(file, "  mv a0, t4\n") >= 0;
}

bool minic_riscv64_emit_record_return_value(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicExpressionId source_id,
    size_t result_pointer_offset) {
    const MinicExpression *source;
    size_t storage_size;
    size_t temporary_size;

    source = minic_c0_program_expression(program, source_id);
    if (source == NULL || !minic_type_is_record(source->type) ||
        !minic_riscv64_type_layout(program, source->type, &storage_size, &temporary_size) ||
        storage_size <= 16U || storage_size > SIZE_MAX - 15U) {
        return false;
    }
    temporary_size = (storage_size + 15U) & ~(size_t)15U;
    return minic_riscv64_emit_record_value_temporary(file,
                                                     program,
                                                     function,
                                                     function_layout,
                                                     source_id,
                                                     storage_size,
                                                     temporary_size) &&
           minic_riscv64_emit_s0_load64(file, "t4", result_pointer_offset) &&
           minic_riscv64_emit_record_temporary_to_address(
               file, storage_size, temporary_size, "t4", false);
}

bool minic_riscv64_emit_record_copy_value(FILE *file,
                                          const MinicC0Program *program,
                                          const MinicFunction *function,
                                          const MinicRiscv64FunctionLayout *function_layout,
                                          MinicExpressionId target_id,
                                          MinicExpressionId source_id,
                                          bool preserve_target_address) {
    const MinicExpression *target;
    const MinicExpression *source;
    const MinicRecord *record;
    size_t storage_size;
    size_t temporary_size;

    target = minic_c0_program_expression(program, target_id);
    source = minic_c0_program_expression(program, source_id);
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(target->type) || !minic_type_is_record(target->type) ||
        !minic_type_is_record(source->type) || target->type.record_id != source->type.record_id ||
        !minic_c0_record_value_is_copy_source(program, source_id)) {
        return false;
    }
    record = minic_c0_program_record(program, target->type.record_id);
    if (record == NULL || !record->is_complete ||
        !minic_riscv64_type_layout(program, target->type, &storage_size, &temporary_size) ||
        storage_size == 0U || storage_size > SIZE_MAX - 15U) {
        return false;
    }
    temporary_size = (storage_size + 15U) & ~(size_t)15U;

    return minic_riscv64_emit_record_value_temporary(
               file, program, function, function_layout, source_id, storage_size, temporary_size) &&
           minic_riscv64_emit_lvalue_address(file, program, function, function_layout, target_id) &&
           minic_riscv64_emit_record_temporary_to_address(
               file, storage_size, temporary_size, "a0", preserve_target_address);
}

'''
text = text[:copy_start] + new_copy + text[copy_end:]

old_signature = r'''bool minic_riscv64_emit_expression(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicFunction *function,
                                   const MinicRiscv64FunctionLayout *function_layout,
                                   MinicExpressionId expression_id) {
'''
new_signature = r'''static bool minic_riscv64_emit_expression_impl(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicExpressionId expression_id,
    size_t record_result_temporary_size) {
'''
text = replace_once(text, old_signature, new_signature, "expression impl signature")

call_start = text.find("    case MINIC_EXPRESSION_CALL: {\n")
if call_start < 0:
    raise SystemExit("call case not found")
prefix = text[:call_start]
call_text = text[call_start:]
call_text = replace_once(
    call_text,
    '''        MinicRiscv64AbiValue abi_values[MINIC_MAX_FUNCTION_PARAMETERS];
        MinicRiscv64AbiArgumentLocation argument_locations[MINIC_MAX_FUNCTION_PARAMETERS];
        bool use_formal_location_path;
''',
    '''        MinicRiscv64AbiValue abi_values[MINIC_MAX_FUNCTION_PARAMETERS];
        MinicRiscv64AbiArgumentLocation argument_locations[MINIC_MAX_FUNCTION_PARAMETERS];
        MinicRiscv64AbiCursor initial_abi_cursor;
        MinicRiscv64AbiValue return_value;
        bool has_indirect_return;
        bool use_formal_location_path;
''',
    "call return ABI declarations",
)
call_text = replace_once(
    call_text,
    '''        staged_bytes = 0U;
        (void)memset(argument_stage_end, 0, sizeof(argument_stage_end));

        if (is_indirect) {
''',
    '''        staged_bytes = 0U;
        (void)memset(argument_stage_end, 0, sizeof(argument_stage_end));
        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, expression->type, &initial_abi_cursor, &return_value)) {
            return false;
        }
        has_indirect_return = return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT;
        if (has_indirect_return) {
            if (!minic_type_is_record(expression->type) || return_value.storage_size <= 16U ||
                record_result_temporary_size < return_value.storage_size ||
                (record_result_temporary_size & 15U) != 0U ||
                !minic_riscv64_emit_stack_allocate(file, record_result_temporary_size)) {
                return false;
            }
        } else if (record_result_temporary_size != 0U) {
            return false;
        }

        if (is_indirect) {
''',
    "call indirect result setup",
)
call_text = replace_once(
    call_text,
    '''            MinicRiscv64AbiCursor abi_cursor;

            minic_riscv64_abi_cursor_initialize(&abi_cursor);
''',
    '''            MinicRiscv64AbiCursor abi_cursor;

            abi_cursor = initial_abi_cursor;
''',
    "formal call argument cursor",
)
count = call_text.count("                integer_register_index = 0U;\n")
if count != 2:
    raise SystemExit(f"fallback integer register cursors: expected 2, found {count}")
call_text = call_text.replace(
    "                integer_register_index = 0U;\n",
    "                integer_register_index = has_indirect_return ? 1U : 0U;\n",
)
call_text = replace_once(
    call_text,
    '''        if (outgoing_stack_bytes == 0U && temporary_bytes != 0U &&
            !minic_riscv64_emit_stack_release(file, temporary_bytes)) {
            return false;
        }
        if (is_indirect) {
''',
    '''        if (outgoing_stack_bytes == 0U && temporary_bytes != 0U &&
            !minic_riscv64_emit_stack_release(file, temporary_bytes)) {
            return false;
        }
        if (has_indirect_return) {
            size_t result_offset;

            if (outgoing_stack_bytes == 0U) {
                result_offset = 0U;
            } else {
                if (outgoing_stack_bytes > SIZE_MAX - staged_bytes) {
                    return false;
                }
                result_offset = outgoing_stack_bytes + staged_bytes;
            }
            if (result_offset <= 2047U) {
                if (fprintf(file, "  addi a0, sp, %zu\\n", result_offset) < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  li t1, %zu\\n"
                               "  add a0, sp, t1\\n",
                               result_offset) < 0) {
                return false;
            }
        }
        if (is_indirect) {
''',
    "materialize hidden result pointer",
)
call_text = replace_once(
    call_text,
    '''        if (minic_type_is_record(expression->type)) {
            size_t aggregate_size;
            size_t aggregate_chunks;

            return minic_riscv64_integer_aggregate_abi(
                program, expression->type, &aggregate_size, &aggregate_chunks);
        }
''',
    '''        if (minic_type_is_record(expression->type)) {
            size_t aggregate_size;
            size_t aggregate_chunks;

            if (has_indirect_return) {
                return true;
            }
            return minic_riscv64_integer_aggregate_abi(
                program, expression->type, &aggregate_size, &aggregate_chunks);
        }
''',
    "call indirect record result",
)
text = prefix + call_text
wrapper = r'''

bool minic_riscv64_emit_expression(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicFunction *function,
                                   const MinicRiscv64FunctionLayout *function_layout,
                                   MinicExpressionId expression_id) {
    return minic_riscv64_emit_expression_impl(
        file, program, function, function_layout, expression_id, 0U);
}
'''
if text.rstrip().endswith("return false;\n}"):
    text = text.rstrip() + wrapper
else:
    raise SystemExit("expression implementation end not recognized")
write(path, text)

# Return lowering: small records retain register return; >16-byte records copy
# into the caller-owned hidden result buffer, then cleanup edges may run without
# needing to preserve a0/a1 as a return payload.
path = "src/target/riscv64/codegen_statement.c"
text = read(path)
text = replace_once(
    text,
    '#include "target/riscv64/codegen_internal.h"\n',
    '#include "target/riscv64/codegen_internal.h"\n#include "target/riscv64/abi.h"\n',
    "statement ABI include",
)
old_record_return = r'''        if (minic_type_is_record(function->return_type)) {
            size_t aggregate_size;
            size_t aggregate_chunks;

            if (!minic_type_is_record(value->type) ||
                value->type.record_id != function->return_type.record_id ||
                !minic_riscv64_integer_aggregate_abi(
                    program, function->return_type, &aggregate_size, &aggregate_chunks)) {
                return false;
            }
            (void)aggregate_size;
            if (minic_c0_record_value_is_address_backed(program, statement->expression)) {
                if (!minic_riscv64_emit_address_backed_record_value(
                        file, program, function, function_layout, statement->expression) ||
                    fprintf(file, "  mv t0, a0\n") < 0 ||
                    !minic_riscv64_emit_integer_aggregate_load_chunk(
                        file, program, function->return_type, 0U, "a0", "t0") ||
                    (aggregate_chunks == 2U &&
                     !minic_riscv64_emit_integer_aggregate_load_chunk(
                         file, program, function->return_type, 1U, "a1", "t0"))) {
                    return false;
                }
            } else if (value->kind != MINIC_EXPRESSION_CALL ||
                       !minic_riscv64_emit_expression(
                           file, program, function, function_layout, statement->expression)) {
                return false;
            }
        } else if (!minic_riscv64_emit_expression(
'''
new_record_return = r'''        if (minic_type_is_record(function->return_type)) {
            MinicRiscv64AbiValue return_value;

            if (!minic_type_is_record(value->type) ||
                value->type.record_id != function->return_type.record_id ||
                !minic_riscv64_abi_classify_value(program, function->return_type, &return_value)) {
                return false;
            }
            if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                MinicRiscv64FrameLayout frame_layout;

                if (return_value.storage_size <= 16U ||
                    !minic_riscv64_frame_layout_from_function_layout(
                        program, function, function_layout, &frame_layout) ||
                    !frame_layout.has_indirect_return ||
                    !minic_riscv64_emit_record_return_value(file,
                                                            program,
                                                            function,
                                                            function_layout,
                                                            statement->expression,
                                                            frame_layout.indirect_return_offset)) {
                    return false;
                }
            } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
                       return_value.slot_count != 0U && return_value.slot_count <= 2U) {
                if (minic_c0_record_value_is_address_backed(program, statement->expression)) {
                    if (!minic_riscv64_emit_address_backed_record_value(
                            file, program, function, function_layout, statement->expression) ||
                        fprintf(file, "  mv t0, a0\n") < 0 ||
                        !minic_riscv64_emit_integer_aggregate_load_chunk(
                            file, program, function->return_type, 0U, "a0", "t0") ||
                        (return_value.slot_count == 2U &&
                         !minic_riscv64_emit_integer_aggregate_load_chunk(
                             file, program, function->return_type, 1U, "a1", "t0"))) {
                        return false;
                    }
                } else if (value->kind != MINIC_EXPRESSION_CALL ||
                           !minic_riscv64_emit_expression(
                               file, program, function, function_layout, statement->expression)) {
                    return false;
                }
            } else {
                return false;
            }
        } else if (!minic_riscv64_emit_expression(
'''
text = replace_once(text, old_record_return, new_record_return, "indirect record return lowering")
write(path, text)

# Permanent TargetABI contract: a 24-byte record reserves the first integer
# argument slot for the hidden result pointer, while small unsupported record
# return classes remain fail-closed.
path = "tests/target/riscv64/abi_test.c"
text = read(path)
text = replace_once(
    text,
    '''static bool test_argument_placement(void) {
''',
    '''static bool test_return_argument_cursor(void) {
    MinicC0Program program;
    MinicType record24_fields[3];
    MinicType record_fp_field;
    MinicType record24;
    MinicType record_fp;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    MinicRiscv64AbiArgumentLocation location;

    minic_c0_program_initialize(&program);
    record24_fields[0] = minic_type_long();
    record24_fields[1] = minic_type_long();
    record24_fields[2] = minic_type_long();
    record_fp_field = minic_type_double();
    CHECK(add_record(&program, record24_fields, 3U, &record24));
    CHECK(add_record(&program, &record_fp_field, 1U, &record_fp));

    CHECK(minic_riscv64_abi_cursor_initialize_for_return(
        &program, record24, &cursor, &return_value));
    CHECK(return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT);
    CHECK(return_value.storage_size == 24U);
    CHECK(cursor.integer_register_count == 1U);
    CHECK(cursor.floating_register_count == 0U);
    CHECK(cursor.stack_slot_count == 0U);
    CHECK(minic_riscv64_abi_place_argument(
        &program, minic_type_long(), true, &cursor, &location));
    CHECK(location.integer_register_begin == 1U);
    CHECK(location.integer_register_count == 1U);

    minic_riscv64_abi_cursor_initialize(&cursor);
    CHECK(!minic_riscv64_abi_cursor_initialize_for_return(
        &program, record_fp, &cursor, &return_value));
    CHECK(cursor.integer_register_count == 0U);
    CHECK(cursor.floating_register_count == 0U);
    CHECK(cursor.stack_slot_count == 0U);

    minic_c0_program_destroy(&program);
    return true;
}

static bool test_argument_placement(void) {
''',
    "return cursor test",
)
text = replace_once(
    text,
    '''    if (!test_value_classification() || !test_argument_placement() ||
        !test_unsupported_argument_is_transactional()) {
''',
    '''    if (!test_value_classification() || !test_return_argument_cursor() ||
        !test_argument_placement() || !test_unsupported_argument_is_transactional()) {
''',
    "run return cursor test",
)
text = replace_once(
    text,
    'PASS rv64 abi classification+placement canonical owner',
    'PASS rv64 abi classification+return-cursor+placement canonical owner',
    "ABI PASS summary",
)
write(path, text)

# Extend the existing aggregate-return family with a 24-byte indirect result,
# a forwarded record-valued call, and the Linux-shaped cleanup local initializer.
path = "tests/compiler/c0/rv64_integer_aggregate_return.c"
text = read(path)
text += r'''

struct triple64 {
    long first;
    long second;
    long third;
};

static struct triple64 make_triple(long base) {
    struct triple64 result;

    result.first = base;
    result.second = base + 1;
    result.third = base + 2;
    return result;
}

static struct triple64 forward_triple(long base) {
    return make_triple(base);
}

static void cleanup_triple(struct triple64 *value) {
    value->third += 1;
}

static long cleanup_triple_call(long base) {
    struct triple64 value __attribute__((cleanup(cleanup_triple))) = make_triple(base);

    return value.first + value.second + value.third;
}
'''
write(path, text)

path = "tests/compiler/c0/run-rv64-integer-aggregate-return.sh"
text = read(path)
text = replace_once(
    text,
    '''grep -F 'ld a0, 0(t0)' "$assembly" >/dev/null
grep -F 'ld a1, 8(t0)' "$assembly" >/dev/null

printf '%s\\n' 'PASS compiler/c0/rv64_integer_aggregate_return size=16 class=integer callee-params=a0-a3 caller-chunks=1 return=a0-a1 record-local=1 record-call=1'
''',
    '''grep -F 'ld a0, 0(t0)' "$assembly" >/dev/null
grep -F 'ld a1, 8(t0)' "$assembly" >/dev/null
grep -F 'make_triple:' "$assembly" >/dev/null
grep -F 'forward_triple:' "$assembly" >/dev/null
grep -F 'cleanup_triple_call:' "$assembly" >/dev/null
grep -F 'call make_triple' "$assembly" >/dev/null
grep -F 'lbu t0, 0(t2)' "$assembly" >/dev/null
grep -F 'sb t0, 0(t3)' "$assembly" >/dev/null

printf '%s\\n' 'PASS compiler/c0/rv64_integer_aggregate_return direct=8,16 indirect=24 hidden-result=a0 explicit-args=a1+ record-local=1 record-call=1 cleanup=1'
''',
    "indirect aggregate return checks",
)
write(path, text)
