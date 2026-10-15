#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M167d indirect record return {label}: expected 1 match, got {count}")
    text = text.replace(old, new, 1)


replace_once(
    '''    size_t return_address_offset;
    size_t varargs_offset;
''',
    '''    size_t return_address_offset;
    /* M167D_INDIRECT_RECORD_RETURN: psABI hidden result pointer is incoming
       state and must survive arbitrary calls before a Core RETURN. */
    size_t hidden_result_pointer_offset;
    size_t varargs_offset;
''',
    "frame-hidden-result-offset",
)

replace_once(
    '''    bool saves_return_address;
    bool has_variadic_argument_address;
''',
    '''    bool saves_return_address;
    bool has_hidden_result_pointer;
    bool has_variadic_argument_address;
''',
    "frame-hidden-result-flag",
)

replace_once(
    '''    frame->return_address_offset = 0U;
    if (frame->saves_return_address) {
        if (!align_up(storage_size, 8U, &frame->return_address_offset) ||
            frame->return_address_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->return_address_offset + 8U;
    }

    frame->has_variadic_argument_address =
''',
    '''    frame->return_address_offset = 0U;
    if (frame->saves_return_address) {
        if (!align_up(storage_size, 8U, &frame->return_address_offset) ||
            frame->return_address_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->return_address_offset + 8U;
    }

    frame->has_hidden_result_pointer = false;
    frame->hidden_result_pointer_offset = 0U;
    if (program != NULL) {
        MinicRiscv64AbiCursor return_cursor;
        MinicRiscv64AbiValue return_value;

        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &return_cursor, &return_value)) {
            return false;
        }
        (void)return_cursor;
        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            if (!minic_type_is_record(function->return_type) ||
                return_value.storage_size <= 16U || return_value.slot_count != 1U ||
                !align_up(storage_size, 8U, &frame->hidden_result_pointer_offset) ||
                frame->hidden_result_pointer_offset > SIZE_MAX - 8U) {
                return false;
            }
            storage_size = frame->hidden_result_pointer_offset + 8U;
            frame->has_hidden_result_pointer = true;
        }
    }

    frame->has_variadic_argument_address =
''',
    "frame-hidden-result-storage",
)

replace_once(
    '''        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &cursor, &return_value) ||
            (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
             return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
             (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
              return_value.slot_count == 0U || return_value.slot_count > 2U))) {
            return false;
        }
''',
    '''        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &cursor, &return_value)) {
            return false;
        }
        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
            if (return_value.slot_count == 0U || return_value.slot_count > 2U) {
                return false;
            }
        } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            if (!minic_type_is_record(function->return_type) ||
                return_value.storage_size <= 16U || return_value.slot_count != 1U) {
                return false;
            }
        } else if (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
                   return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER) {
            return false;
        }
''',
    "callee-return-preflight",
)

replace_once(
    '''    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, callee->return_type, &cursor, &return_value) ||
        (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
         return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
         (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
          return_value.slot_count == 0U || return_value.slot_count > 2U)) ||
        (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
         (!minic_type_is_record(callee->return_type) ||
          instruction->value.call.result_object >= function->object_count ||
          !minic_type_equal(
              function->objects[instruction->value.call.result_object].type,
              callee->return_type)))) {
        return false;
    }
''',
    '''    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, callee->return_type, &cursor, &return_value)) {
        return false;
    }
    if (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
        if (return_value.slot_count == 0U || return_value.slot_count > 2U ||
            !minic_type_is_record(callee->return_type) ||
            instruction->value.call.result_object >= function->object_count ||
            !minic_type_equal(
                function->objects[instruction->value.call.result_object].type,
                callee->return_type)) {
            return false;
        }
    } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        if (!minic_type_is_record(callee->return_type) || return_value.storage_size <= 16U ||
            return_value.slot_count != 1U ||
            instruction->value.call.result_object >= function->object_count ||
            !minic_type_equal(
                function->objects[instruction->value.call.result_object].type,
                callee->return_type)) {
            return false;
        }
    } else if (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
               return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER) {
        return false;
    }
''',
    "direct-call-return-preflight",
)

replace_once(
    '''    (void)return_value;
    for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
''',
    '''    if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        size_t result_offset;

        if (instruction->value.call.result_object >= function->object_count ||
            !core_object_offset(
                program, function, instruction->value.call.result_object, &result_offset) ||
            !emit_sp_address(file, "a0", result_offset)) {
            return false;
        }
    }
    for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
''',
    "direct-call-hidden-result-argument",
)

replace_once(
    '''    if (minic_type_is_record(instruction->type)) {
        size_t chunk_index;
        size_t object_offset;

        if (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
            return_value.slot_count == 0U || return_value.slot_count > 2U ||
''',
    '''    if (minic_type_is_record(instruction->type)) {
        size_t chunk_index;
        size_t object_offset;

        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            return return_value.storage_size > 16U && return_value.slot_count == 1U &&
                   instruction->value.call.result_object < function->object_count;
        }
        if (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
            return_value.slot_count == 0U || return_value.slot_count > 2U ||
''',
    "direct-call-indirect-result",
)

replace_once(
    '''    if (frame.saves_return_address &&
        !minic_riscv64_emit_sp_store64(file, "ra", frame.return_address_offset)) {
        return false;
    }
    if (frame.has_variadic_argument_address) {
''',
    '''    if (frame.saves_return_address &&
        !minic_riscv64_emit_sp_store64(file, "ra", frame.return_address_offset)) {
        return false;
    }
    if (frame.has_hidden_result_pointer &&
        !minic_riscv64_emit_sp_store64(file, "a0", frame.hidden_result_pointer_offset)) {
        return false;
    }
    if (frame.has_variadic_argument_address) {
''',
    "prologue-save-hidden-result",
)

replace_once(
    '''        if (minic_type_is_record(function->return_type)) {
            MinicRiscv64AbiValue return_value;
            size_t object_offset;

            if (program == NULL || terminator->return_object >= function->object_count ||
                !minic_type_equal(function->objects[terminator->return_object].type,
                                  function->return_type) ||
                !minic_riscv64_abi_classify_value(program, function->return_type, &return_value) ||
                return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                return_value.slot_count == 0U || return_value.slot_count > 2U ||
                !core_object_offset(program, function, terminator->return_object, &object_offset) ||
                !emit_sp_address(file, "t0", object_offset) ||
                !minic_riscv64_emit_integer_aggregate_load_chunk(
                    file, program, function->return_type, 0U, "a0", "t0") ||
                (return_value.slot_count == 2U &&
                 !minic_riscv64_emit_integer_aggregate_load_chunk(
                     file, program, function->return_type, 1U, "a1", "t0"))) {
                return false;
            }
''',
    '''        if (minic_type_is_record(function->return_type)) {
            MinicRiscv64AbiValue return_value;
            size_t object_offset;

            if (program == NULL || terminator->return_object >= function->object_count ||
                !minic_type_equal(function->objects[terminator->return_object].type,
                                  function->return_type) ||
                !minic_riscv64_abi_classify_value(program, function->return_type, &return_value) ||
                !core_object_offset(program, function, terminator->return_object, &object_offset) ||
                !emit_sp_address(file, "t0", object_offset)) {
                return false;
            }
            if (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                if (return_value.slot_count == 0U || return_value.slot_count > 2U ||
                    !minic_riscv64_emit_integer_aggregate_load_chunk(
                        file, program, function->return_type, 0U, "a0", "t0") ||
                    (return_value.slot_count == 2U &&
                     !minic_riscv64_emit_integer_aggregate_load_chunk(
                         file, program, function->return_type, 1U, "a1", "t0"))) {
                    return false;
                }
            } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                size_t copied;

                if (!frame->has_hidden_result_pointer || return_value.storage_size <= 16U ||
                    return_value.slot_count != 1U ||
                    !minic_riscv64_emit_sp_load64(
                        file, "t1", frame->hidden_result_pointer_offset)) {
                    return false;
                }
                copied = 0U;
                while (copied < return_value.storage_size) {
                    size_t chunk = return_value.storage_size - copied;
                    size_t offset;

                    if (chunk > 2048U) {
                        chunk = 2048U;
                    }
                    for (offset = 0U; offset < chunk; ++offset) {
                        if (fprintf(file,
                                    "  lbu t2, %zu(t0)\\n"
                                    "  sb t2, %zu(t1)\\n",
                                    offset,
                                    offset) < 0) {
                            return false;
                        }
                    }
                    copied += chunk;
                    if (copied < return_value.storage_size &&
                        fprintf(file,
                                "  addi t0, t0, 2047\\n"
                                "  addi t0, t0, 1\\n"
                                "  addi t1, t1, 2047\\n"
                                "  addi t1, t1, 1\\n") < 0) {
                        return false;
                    }
                }
            } else {
                return false;
            }
''',
    "callee-indirect-return-emitter",
)

PATH.write_text(text)
print("M167D_INDIRECT_RECORD_RETURN_APPLIED")
