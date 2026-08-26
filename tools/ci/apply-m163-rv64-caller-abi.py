#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if label == "direct-call-emitter" and count == 2:
        start = text.index("static bool emit_call(FILE *file,")
        end = text.index("static bool emit_indirect_call(FILE *file,", start)
        region = text[start:end]
        region_count = region.count(old)
        if region_count != 1:
            raise SystemExit(
                f"M163 caller ABI {label}: expected 1 emit_call match, got {region_count}"
            )
        text = text[:start] + region.replace(old, new, 1) + text[end:]
        return
    if count != 1:
        raise SystemExit(f"M163 caller ABI {label}: expected 1 match, got {count}")
    text = text.replace(old, new, 1)


frame_anchor = '''static bool core_frame_initialize(const MinicC0Program *program,
                                  const MinicCoreFunction *function,
                                  MinicRiscv64CoreFrame *frame) {
'''
frame_helpers = '''/* M163_RV64_CALLER_ABI: reserve the maximum outgoing stack-argument area
   in the fixed O0 frame.  Keeping sp stable across calls makes Core object/value
   offsets invariant while stack arguments remain at the psABI-mandated 0(sp). */
static bool core_direct_call_outgoing_stack_size(const MinicC0Program *program,
                                                 const MinicCoreFunction *function,
                                                 size_t *result) {
    size_t instruction_index;
    size_t maximum_stack_slots;

    if (program == NULL || function == NULL || result == NULL) {
        return false;
    }
    maximum_stack_slots = 0U;
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        const MinicCoreInstruction *instruction = &function->instructions[instruction_index];
        const MinicCoreCallee *callee;
        MinicRiscv64AbiCursor cursor;
        MinicRiscv64AbiValue return_value;
        size_t argument_index;

        if (instruction->kind != MINIC_CORE_INSTRUCTION_CALL) {
            continue;
        }
        if (instruction->value.call.callee_id >= function->callee_count ||
            instruction->value.call.argument_begin > function->call_argument_count ||
            instruction->value.call.argument_count >
                function->call_argument_count - instruction->value.call.argument_begin) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, callee->return_type, &cursor, &return_value)) {
            return false;
        }
        (void)return_value;
        for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument = &function->call_arguments[
                instruction->value.call.argument_begin + argument_index];
            MinicRiscv64AbiArgumentLocation location;
            MinicType argument_type;
            bool is_fixed_parameter = argument_index < callee->parameter_count;

            if (is_fixed_parameter) {
                argument_type = callee->parameter_types[argument_index];
            } else {
                if (!callee->is_variadic || argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                    argument->value.value_id >= function->value_count) {
                    return false;
                }
                argument_type = function->values[argument->value.value_id].type;
            }
            if (!minic_riscv64_abi_place_argument(
                    program, argument_type, is_fixed_parameter, &cursor, &location)) {
                return false;
            }
        }
        if (cursor.stack_slot_count > maximum_stack_slots) {
            maximum_stack_slots = cursor.stack_slot_count;
        }
    }
    if (maximum_stack_slots > SIZE_MAX / 8U) {
        return false;
    }
    *result = maximum_stack_slots * 8U;
    return true;
}

''' + frame_anchor
replace_once(frame_anchor, frame_helpers, "outgoing-stack-helper")

replace_once(
    '''    size_t required_size;

    if (function == NULL || frame == NULL) {
        return false;
    }
    storage_size = 0U;
''',
    '''    size_t required_size;
    size_t outgoing_argument_size;

    if (function == NULL || frame == NULL ||
        !core_direct_call_outgoing_stack_size(program, function, &outgoing_argument_size)) {
        return false;
    }
    storage_size = outgoing_argument_size;
''',
    "frame-outgoing-prefix",
)

replace_once(
    '''    if (function == NULL || offset == NULL || object_id >= function->object_count) {
        return false;
    }
    current_offset = 0U;
''',
    '''    if (function == NULL || offset == NULL || object_id >= function->object_count ||
        !core_direct_call_outgoing_stack_size(program, function, &current_offset)) {
        return false;
    }
''',
    "object-outgoing-prefix",
)

replace_once(
    '''        if (!minic_riscv64_abi_place_argument(
                program, argument_type, is_fixed_parameter, &cursor, &location) ||
            location.floating_register_count != 0U || location.stack_slot_count != 0U) {
            return false;
        }
        if (core_scalar_type(argument_type)) {
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
                location.integer_register_count != 1U || location.integer_register_begin >= 8U) {
                return false;
            }
        } else if (is_fixed_parameter && minic_type_is_record(argument_type)) {
            MinicCoreObjectId object_id;

            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT ||
                location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                location.value.slot_count == 0U || location.value.slot_count > 2U ||
                location.integer_register_count != location.value.slot_count ||
                location.integer_register_begin + location.integer_register_count > 8U) {
                return false;
            }
            object_id = argument->value.object_id;
            if (object_id >= function->object_count ||
                !minic_type_equal(function->objects[object_id].type, argument_type)) {
                return false;
            }
        } else {
            return false;
        }
''',
    '''        if (!minic_riscv64_abi_place_argument(
                program, argument_type, is_fixed_parameter, &cursor, &location) ||
            location.floating_register_count != 0U) {
            return false;
        }
        if (core_scalar_type(argument_type)) {
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
                !((location.integer_register_count == 1U &&
                   location.integer_register_begin < 8U &&
                   location.stack_slot_count == 0U) ||
                  (location.integer_register_count == 0U &&
                   location.stack_slot_count == 1U))) {
                return false;
            }
        } else if (is_fixed_parameter && minic_type_is_record(argument_type)) {
            MinicCoreObjectId object_id;

            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                return false;
            }
            object_id = argument->value.object_id;
            if (object_id >= function->object_count ||
                !minic_type_equal(function->objects[object_id].type, argument_type)) {
                return false;
            }
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                if (location.value.slot_count == 0U || location.value.slot_count > 2U ||
                    location.stack_slot_count != 0U ||
                    location.integer_register_count != location.value.slot_count ||
                    location.integer_register_begin + location.integer_register_count > 8U) {
                    return false;
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (location.value.slot_count != 1U || location.stack_slot_count != 0U ||
                    location.integer_register_count != 1U ||
                    location.integer_register_begin >= 8U) {
                    return false;
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
''',
    "direct-call-preflight",
)

replace_once(
    '''            if (!minic_riscv64_abi_place_argument(
                    program, function->parameter_types[index], true, &cursor, &location) ||
                (location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                 (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                  location.value.slot_count == 0U || location.value.slot_count > 2U))) {
                return false;
            }
''',
    '''            if (!minic_riscv64_abi_place_argument(
                    program, function->parameter_types[index], true, &cursor, &location) ||
                (location.value.kind != MINIC_RISCV64_ABI_VALUE_IGNORE &&
                 location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                 (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                  location.value.slot_count == 0U || location.value.slot_count > 2U))) {
                return false;
            }
''',
    "ignored-callee-parameter",
)

replace_once(
    '''        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
            if (location.integer_register_count != 1U || location.integer_register_begin >= 8U ||
                !load_core_value(file,
                                 frame,
                                 argument->value.value_id,
                                 minic_core_rv64_argument_registers[location.integer_register_begin])) {
                return false;
            }
            continue;
        }
        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            size_t chunk_index;
            size_t object_offset;

            if (!is_fixed_parameter ||
                !core_object_offset(program, function, argument->value.object_id, &object_offset)) {
                return false;
            }
            for (chunk_index = 0U; chunk_index < location.value.slot_count; ++chunk_index) {
                size_t chunk_offset = chunk_index * 8U;
                size_t chunk_size;
                size_t register_index = location.integer_register_begin + chunk_index;

                if (chunk_offset >= location.value.storage_size || register_index >= 8U ||
                    object_offset > SIZE_MAX - chunk_offset) {
                    return false;
                }
                chunk_size = location.value.storage_size - chunk_offset;
                if (chunk_size > 8U) {
                    chunk_size = 8U;
                }
                if (!emit_sp_load_chunk(file,
                                        minic_core_rv64_argument_registers[register_index],
                                        object_offset + chunk_offset,
                                        chunk_size)) {
                    return false;
                }
            }
            continue;
        }
''',
    '''        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
            if (location.integer_register_count == 1U && location.stack_slot_count == 0U) {
                if (location.integer_register_begin >= 8U ||
                    !load_core_value(
                        file,
                        frame,
                        argument->value.value_id,
                        minic_core_rv64_argument_registers[location.integer_register_begin])) {
                    return false;
                }
            } else if (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U) {
                size_t outgoing_offset;

                if (location.stack_slot_begin > SIZE_MAX / 8U ||
                    !load_core_value(file, frame, argument->value.value_id, "t0")) {
                    return false;
                }
                outgoing_offset = location.stack_slot_begin * 8U;
                if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                    return false;
                }
            } else {
                return false;
            }
            continue;
        }
        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            size_t chunk_index;
            size_t object_offset;

            if (!is_fixed_parameter ||
                !core_object_offset(program, function, argument->value.object_id, &object_offset)) {
                return false;
            }
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (location.integer_register_count != 1U || location.stack_slot_count != 0U ||
                    location.integer_register_begin >= 8U ||
                    !emit_sp_address(file,
                                     minic_core_rv64_argument_registers[
                                         location.integer_register_begin],
                                     object_offset)) {
                    return false;
                }
                continue;
            }
            if (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                return false;
            }
            for (chunk_index = 0U; chunk_index < location.value.slot_count; ++chunk_index) {
                size_t chunk_offset = chunk_index * 8U;
                size_t chunk_size;
                size_t register_index = location.integer_register_begin + chunk_index;

                if (chunk_offset >= location.value.storage_size || register_index >= 8U ||
                    object_offset > SIZE_MAX - chunk_offset) {
                    return false;
                }
                chunk_size = location.value.storage_size - chunk_offset;
                if (chunk_size > 8U) {
                    chunk_size = 8U;
                }
                if (!emit_sp_load_chunk(file,
                                        minic_core_rv64_argument_registers[register_index],
                                        object_offset + chunk_offset,
                                        chunk_size)) {
                    return false;
                }
            }
            continue;
        }
''',
    "direct-call-emitter",
)

PATH.write_text(text)
print("M163_CALLER_ABI_APPLIED")
