#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M163 trace {label}: expected 1 match, got {count}")
    text = text.replace(old, new, 1)


replace_once(
    '''    if (function == NULL || !minic_core_function_verify(function)) {
        return false;
    }
''',
    '''    if (function == NULL) {
        fprintf(stderr, "CORE_RV64_PREFLIGHT reason=null-function\\n");
        return false;
    }
    if (!minic_core_function_verify(function)) {
        fprintf(stderr, "CORE_RV64_PREFLIGHT reason=core-verify\\n");
        return false;
    }
''',
    "verify",
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
                program, function->return_type, &cursor, &return_value) ||
            (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
             return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
             (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
              return_value.slot_count == 0U || return_value.slot_count > 2U))) {
            fprintf(stderr,
                    "CORE_RV64_PREFLIGHT reason=return-abi kind=%d slots=%zu\\n",
                    (int)return_value.kind,
                    return_value.slot_count);
            return false;
        }
''',
    "return-abi",
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
                (location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                 (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                  location.value.slot_count == 0U || location.value.slot_count > 2U))) {
                fprintf(stderr,
                        "CORE_RV64_PREFLIGHT reason=parameter-abi index=%zu kind=%d slots=%zu regs=%zu stack=%zu\\n",
                        index,
                        (int)location.value.kind,
                        location.value.slot_count,
                        location.integer_register_count,
                        location.stack_slot_count);
                return false;
            }
''',
    "parameter-abi",
)

replace_once(
    '''        if ((!core_scalar_type(object_type) && !minic_type_is_record(object_type)) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    object_type,
                                    &object_size,
                                    &object_alignment) ||
            object_size == 0U || object_alignment == 0U || object_alignment > 16U) {
            return false;
        }
''',
    '''        if ((!core_scalar_type(object_type) && !minic_type_is_record(object_type)) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    object_type,
                                    &object_size,
                                    &object_alignment) ||
            object_size == 0U || object_alignment == 0U || object_alignment > 16U) {
            fprintf(stderr,
                    "CORE_RV64_PREFLIGHT reason=object index=%zu integer=%d pointer=%d record=%d array=%d size=%zu align=%zu\\n",
                    index,
                    minic_type_is_integer(object_type) ? 1 : 0,
                    minic_type_is_pointer(object_type) ? 1 : 0,
                    minic_type_is_record(object_type) ? 1 : 0,
                    minic_type_is_array(object_type) ? 1 : 0,
                    object_size,
                    object_alignment);
            return false;
        }
''',
    "object",
)

replace_once(
    '''        if (function->globals[index].name == NULL || function->globals[index].name_length == 0U ||
            !core_global_addressable_type(function->globals[index].type)) {
            return false;
        }
''',
    '''        if (function->globals[index].name == NULL || function->globals[index].name_length == 0U ||
            !core_global_addressable_type(function->globals[index].type)) {
            fprintf(stderr,
                    "CORE_RV64_PREFLIGHT reason=global index=%zu integer=%d pointer=%d record=%d array=%d void=%d\\n",
                    index,
                    minic_type_is_integer(function->globals[index].type) ? 1 : 0,
                    minic_type_is_pointer(function->globals[index].type) ? 1 : 0,
                    minic_type_is_record(function->globals[index].type) ? 1 : 0,
                    minic_type_is_array(function->globals[index].type) ? 1 : 0,
                    minic_type_is_void(function->globals[index].type) ? 1 : 0);
            return false;
        }
''',
    "global",
)

replace_once(
    '''        if (!core_scalar_type(function->values[index].type)) {
            return false;
        }
''',
    '''        if (!core_scalar_type(function->values[index].type)) {
            fprintf(stderr,
                    "CORE_RV64_PREFLIGHT reason=value index=%zu integer=%d pointer=%d record=%d array=%d void=%d\\n",
                    index,
                    minic_type_is_integer(function->values[index].type) ? 1 : 0,
                    minic_type_is_pointer(function->values[index].type) ? 1 : 0,
                    minic_type_is_record(function->values[index].type) ? 1 : 0,
                    minic_type_is_array(function->values[index].type) ? 1 : 0,
                    minic_type_is_void(function->values[index].type) ? 1 : 0);
            return false;
        }
''',
    "value",
)

replace_once(
    '''    case MINIC_CORE_INSTRUCTION_CALL:
        return core_direct_call_supported(program, function, instruction);
''',
    '''    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *trace_callee;
        MinicRiscv64AbiCursor trace_cursor;
        MinicRiscv64AbiValue trace_return = {0};
        bool trace_return_ok;
        size_t trace_argument_index;

        if (core_direct_call_supported(program, function, instruction)) {
            return true;
        }
        if (program == NULL || instruction->value.call.callee_id >= function->callee_count) {
            fprintf(stderr, "CORE_RV64_CALL_TRACE reason=invalid-program-or-callee\\n");
            return false;
        }
        trace_callee = &function->callees[instruction->value.call.callee_id];
        fprintf(stderr,
                "CORE_RV64_CALL_TRACE callee=%s argc=%zu fixed=%zu variadic=%d\\n",
                trace_callee->name != NULL ? trace_callee->name : "<null>",
                instruction->value.call.argument_count,
                trace_callee->parameter_count,
                trace_callee->is_variadic ? 1 : 0);
        trace_return_ok = minic_riscv64_abi_cursor_initialize_for_return(
            program, trace_callee->return_type, &trace_cursor, &trace_return);
        fprintf(stderr,
                "CORE_RV64_CALL_TRACE return_ok=%d kind=%d size=%zu chunks=%zu slots=%zu\\n",
                trace_return_ok ? 1 : 0,
                (int)trace_return.kind,
                trace_return.storage_size,
                trace_return.register_chunks,
                trace_return.slot_count);
        if (!trace_return_ok) {
            return false;
        }
        for (trace_argument_index = 0U;
             trace_argument_index < instruction->value.call.argument_count;
             ++trace_argument_index) {
            const MinicCoreCallArgument *trace_argument = &function->call_arguments[
                instruction->value.call.argument_begin + trace_argument_index];
            MinicRiscv64AbiArgumentLocation trace_location = {0};
            MinicType trace_argument_type;
            bool trace_fixed = trace_argument_index < trace_callee->parameter_count;
            bool trace_placed;

            if (trace_fixed) {
                trace_argument_type = trace_callee->parameter_types[trace_argument_index];
            } else if (trace_argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE &&
                       trace_argument->value.value_id < function->value_count) {
                trace_argument_type = function->values[trace_argument->value.value_id].type;
            } else {
                fprintf(stderr,
                        "CORE_RV64_CALL_TRACE arg=%zu fixed=%d core_kind=%d reason=no-type\\n",
                        trace_argument_index,
                        trace_fixed ? 1 : 0,
                        (int)trace_argument->kind);
                continue;
            }
            trace_placed = minic_riscv64_abi_place_argument(
                program, trace_argument_type, trace_fixed, &trace_cursor, &trace_location);
            fprintf(stderr,
                    "CORE_RV64_CALL_TRACE arg=%zu fixed=%d core_kind=%d placed=%d abi_kind=%d size=%zu slots=%zu ireg_begin=%zu iregs=%zu stack_begin=%zu stack=%zu integer=%d pointer=%d record=%d\\n",
                    trace_argument_index,
                    trace_fixed ? 1 : 0,
                    (int)trace_argument->kind,
                    trace_placed ? 1 : 0,
                    (int)trace_location.value.kind,
                    trace_location.value.storage_size,
                    trace_location.value.slot_count,
                    trace_location.integer_register_begin,
                    trace_location.integer_register_count,
                    trace_location.stack_slot_begin,
                    trace_location.stack_slot_count,
                    minic_type_is_integer(trace_argument_type) ? 1 : 0,
                    minic_type_is_pointer(trace_argument_type) ? 1 : 0,
                    minic_type_is_record(trace_argument_type) ? 1 : 0);
            if (!trace_placed) {
                break;
            }
        }
        return false;
    }
''',
    "direct-call",
)

replace_once(
    '''        if (!core_instruction_supported(program, function, &function->instructions[index])) {
            return false;
        }
''',
    '''        if (!core_instruction_supported(program, function, &function->instructions[index])) {
            fprintf(stderr,
                    "CORE_RV64_PREFLIGHT reason=instruction index=%zu kind=%d\\n",
                    index,
                    (int)function->instructions[index].kind);
            return false;
        }
''',
    "instruction",
)

PATH.write_text(text)
print("M163_PREFLIGHT_TRACE_APPLIED")
