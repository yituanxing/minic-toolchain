#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()


def replace_region(start_marker: str, end_marker: str, new: str, label: str) -> None:
    global text
    start = text.find(start_marker)
    if start < 0:
        raise SystemExit(f"M168 {label}: start marker missing")
    end = text.find(end_marker, start)
    if end < 0:
        raise SystemExit(f"M168 {label}: end marker missing")
    text = text[:start] + new + text[end:]


# M163 reserved stack space only for direct calls. Generalize the exact same
# fixed-frame ownership to direct + indirect calls so sp remains stable.
helper_start = "/* M163_RV64_CALLER_ABI: reserve the maximum outgoing stack-argument area"
helper_end = "static bool core_frame_initialize(const MinicC0Program *program,"
helper = r'''/* M168_RV64_INDIRECT_CALL_STACK: reserve one fixed outgoing stack area
   shared by direct and indirect calls.  The ABI cursor remains the single owner
   of register/stack placement; Core keeps only VALUE/OBJECT arguments. */
static bool core_call_outgoing_stack_size(const MinicC0Program *program,
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
        const MinicType *parameter_types;
        size_t parameter_count;
        bool is_variadic;
        size_t argument_begin;
        size_t argument_count;
        MinicType return_type;
        MinicRiscv64AbiCursor cursor;
        MinicRiscv64AbiValue return_value;
        size_t argument_index;

        if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL) {
            const MinicCoreCallee *callee;
            if (instruction->value.call.callee_id >= function->callee_count ||
                instruction->value.call.argument_begin > function->call_argument_count ||
                instruction->value.call.argument_count >
                    function->call_argument_count - instruction->value.call.argument_begin) {
                return false;
            }
            callee = &function->callees[instruction->value.call.callee_id];
            parameter_types = callee->parameter_types;
            parameter_count = callee->parameter_count;
            is_variadic = callee->is_variadic;
            argument_begin = instruction->value.call.argument_begin;
            argument_count = instruction->value.call.argument_count;
            return_type = callee->return_type;
        } else if (instruction->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
            const MinicCoreCallSignature *signature;
            if (instruction->value.indirect_call.signature_id >= function->call_signature_count ||
                instruction->value.indirect_call.argument_begin > function->call_argument_count ||
                instruction->value.indirect_call.argument_count >
                    function->call_argument_count - instruction->value.indirect_call.argument_begin) {
                return false;
            }
            signature = &function->call_signatures[instruction->value.indirect_call.signature_id];
            parameter_types = signature->parameter_types;
            parameter_count = signature->parameter_count;
            is_variadic = signature->is_variadic;
            argument_begin = instruction->value.indirect_call.argument_begin;
            argument_count = instruction->value.indirect_call.argument_count;
            return_type = signature->return_type;
        } else {
            continue;
        }

        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, return_type, &cursor, &return_value)) {
            return false;
        }
        (void)return_value;
        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            const MinicCoreCallArgument *argument =
                &function->call_arguments[argument_begin + argument_index];
            MinicRiscv64AbiArgumentLocation location;
            MinicType argument_type;
            bool is_fixed_parameter = argument_index < parameter_count;

            if (is_fixed_parameter) {
                argument_type = parameter_types[argument_index];
            } else {
                if (!is_variadic || argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
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

'''
replace_region(helper_start, helper_end, helper, "outgoing-stack-owner")
count = text.count("core_direct_call_outgoing_stack_size(")
if count != 2:
    raise SystemExit(f"M168 outgoing stack call sites: expected 2, got {count}")
text = text.replace("core_direct_call_outgoing_stack_size(", "core_call_outgoing_stack_size(")

# Permit scalar indirect-call arguments to use either one integer argument
# register or one ABI stack slot. Aggregate support remains unchanged/fail-closed.
pre_start = text.index("static bool core_indirect_call_supported(")
pre_end = text.index("static bool core_instruction_supported(", pre_start)
pre = text[pre_start:pre_end]
old = '''        if (!minic_riscv64_abi_place_argument(
                program, argument_type, is_fixed_parameter, &cursor, &location) ||
            location.floating_register_count != 0U || location.stack_slot_count != 0U) {
            return false;
        }
        if (core_scalar_type(argument_type)) {
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count ||
                location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
                location.integer_register_count != 1U ||
                location.integer_register_begin >= 8U) {
                return false;
            }
'''
new = '''        if (!minic_riscv64_abi_place_argument(
                program, argument_type, is_fixed_parameter, &cursor, &location) ||
            location.floating_register_count != 0U) {
            return false;
        }
        if (core_scalar_type(argument_type)) {
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count ||
                location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
                !((location.integer_register_count == 1U &&
                   location.integer_register_begin < 8U &&
                   location.stack_slot_count == 0U) ||
                  (location.integer_register_count == 0U &&
                   location.stack_slot_count == 1U))) {
                return false;
            }
'''
if pre.count(old) != 1:
    raise SystemExit(f"M168 indirect preflight seam: expected 1, got {pre.count(old)}")
pre = pre.replace(old, new, 1)
text = text[:pre_start] + pre + text[pre_end:]

# Mirror M163 direct-call scalar stack emission for indirect calls.
emit_start = text.index("static bool emit_indirect_call(FILE *file,")
emit_end = text.index("static bool emit_field_address(FILE *file,", emit_start)
emit = text[emit_start:emit_end]
old = '''        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
            if (location.integer_register_count != 1U || location.integer_register_begin >= 8U ||
                !load_core_value(file,
                                 frame,
                                 argument->value.value_id,
                                 minic_core_rv64_argument_registers[location.integer_register_begin])) {
                return false;
            }
            continue;
        }
'''
new = '''        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
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
'''
if emit.count(old) != 1:
    raise SystemExit(f"M168 indirect emitter seam: expected 1, got {emit.count(old)}")
emit = emit.replace(old, new, 1)
text = text[:emit_start] + emit + text[emit_end:]

PATH.write_text(text)
print("M168_INDIRECT_CALL_STACK_APPLIED")
