#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()

anchor = '''bool minic_riscv64_core_function_can_emit_basic_v0(const MinicCoreFunction *function) {
    return core_function_can_emit_basic_v0(NULL, function);
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"M169 tail trace: export anchor count={text.count(anchor)}")

helper = r'''/* M169_RV64_TAIL_TRACE: diagnostic-only mirror of the outer basic-v0
   preflight.  It does not change support decisions; it identifies the first
   target capability seam rejected by the canonical predicate. */
static void m169_trace_core_reject(const MinicC0Program *program,
                                   const MinicCoreFunction *function) {
    size_t index;

    if (function == NULL) {
        fprintf(stderr, "M169_PREFLIGHT reason=function-null\n");
        return;
    }
    if (!minic_core_function_verify(function)) {
        fprintf(stderr, "M169_PREFLIGHT function=%s reason=core-verify\n", function->name);
        return;
    }
    if (program == NULL) {
        fprintf(stderr, "M169_PREFLIGHT function=%s reason=program-null\n", function->name);
        return;
    }

    {
        MinicRiscv64AbiCursor cursor;
        MinicRiscv64AbiValue return_value;

        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &cursor, &return_value)) {
            fprintf(stderr, "M169_PREFLIGHT function=%s reason=return-classify\n", function->name);
            return;
        }
        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
            if (return_value.slot_count == 0U || return_value.slot_count > 2U) {
                fprintf(stderr,
                        "M169_PREFLIGHT function=%s reason=return-aggregate kind=%d size=%zu slots=%zu\n",
                        function->name, (int)return_value.kind,
                        return_value.storage_size, return_value.slot_count);
                return;
            }
        } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            if (!minic_type_is_record(function->return_type) ||
                return_value.storage_size <= 16U || return_value.slot_count != 1U) {
                fprintf(stderr,
                        "M169_PREFLIGHT function=%s reason=return-indirect kind=%d size=%zu slots=%zu\n",
                        function->name, (int)return_value.kind,
                        return_value.storage_size, return_value.slot_count);
                return;
            }
        } else if (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
                   return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER) {
            fprintf(stderr,
                    "M169_PREFLIGHT function=%s reason=return-kind kind=%d size=%zu slots=%zu\n",
                    function->name, (int)return_value.kind,
                    return_value.storage_size, return_value.slot_count);
            return;
        }

        for (index = 0U; index < function->parameter_count; ++index) {
            MinicRiscv64AbiArgumentLocation location;
            if (!minic_riscv64_abi_place_argument(
                    program, function->parameter_types[index], true, &cursor, &location)) {
                fprintf(stderr,
                        "M169_PREFLIGHT function=%s reason=parameter-place index=%zu\n",
                        function->name, index);
                return;
            }
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (!minic_type_is_record(function->parameter_types[index]) ||
                    location.value.storage_size == 0U || location.value.slot_count != 1U ||
                    location.floating_register_count != 0U ||
                    !((location.integer_register_count == 1U &&
                       location.integer_register_begin < 8U &&
                       location.stack_slot_count == 0U) ||
                      (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U))) {
                    fprintf(stderr,
                            "M169_PREFLIGHT function=%s reason=parameter-indirect index=%zu kind=%d size=%zu slots=%zu regs=%zu stack=%zu\n",
                            function->name, index, (int)location.value.kind,
                            location.value.storage_size, location.value.slot_count,
                            location.integer_register_count, location.stack_slot_count);
                    return;
                }
            } else if (location.value.kind != MINIC_RISCV64_ABI_VALUE_IGNORE &&
                       location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                       (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                        location.value.slot_count == 0U || location.value.slot_count > 2U)) {
                fprintf(stderr,
                        "M169_PREFLIGHT function=%s reason=parameter-kind index=%zu kind=%d size=%zu slots=%zu regs=%zu stack=%zu\n",
                        function->name, index, (int)location.value.kind,
                        location.value.storage_size, location.value.slot_count,
                        location.integer_register_count, location.stack_slot_count);
                return;
            }
        }
    }

    for (index = 0U; index < function->object_count; ++index) {
        size_t object_size;
        size_t object_alignment;
        MinicType object_type = function->objects[index].type;
        if ((!core_scalar_type(object_type) && !minic_type_is_record(object_type)) ||
            !minic_data_layout_type(minic_default_data_layout(), program, object_type,
                                    &object_size, &object_alignment) ||
            (object_size == 0U && !minic_type_is_record(object_type)) ||
            object_alignment == 0U || object_alignment > 16U) {
            fprintf(stderr,
                    "M169_PREFLIGHT function=%s reason=object index=%zu\n",
                    function->name, index);
            return;
        }
    }
    for (index = 0U; index < function->global_count; ++index) {
        if (function->globals[index].name == NULL ||
            function->globals[index].name_length == 0U ||
            !core_global_addressable_type(function->globals[index].type)) {
            fprintf(stderr,
                    "M169_PREFLIGHT function=%s reason=global index=%zu\n",
                    function->name, index);
            return;
        }
    }
    for (index = 0U; index < function->value_count; ++index) {
        if (!core_scalar_type(function->values[index].type)) {
            fprintf(stderr,
                    "M169_PREFLIGHT function=%s reason=value index=%zu\n",
                    function->name, index);
            return;
        }
    }
    for (index = 0U; index < function->instruction_count; ++index) {
        if (!core_instruction_supported(program, function, &function->instructions[index])) {
            fprintf(stderr,
                    "M169_PREFLIGHT function=%s reason=instruction index=%zu kind=%d\n",
                    function->name, index, (int)function->instructions[index].kind);
            return;
        }
    }
    fprintf(stderr, "M169_PREFLIGHT function=%s reason=unknown-after-mirror\n", function->name);
}

'''
text = text.replace(anchor, helper + anchor, 1)
old = '''bool minic_riscv64_core_function_can_emit_basic_v0_for_program(const MinicC0Program *program,
                                                               const MinicCoreFunction *function) {
    return program != NULL && core_function_can_emit_basic_v0(program, function);
}
'''
new = '''bool minic_riscv64_core_function_can_emit_basic_v0_for_program(const MinicC0Program *program,
                                                               const MinicCoreFunction *function) {
    bool supported = program != NULL && core_function_can_emit_basic_v0(program, function);
    if (!supported && program != NULL) {
        m169_trace_core_reject(program, function);
    }
    return supported;
}
'''
if text.count(old) != 1:
    raise SystemExit(f"M169 tail trace: for-program export count={text.count(old)}")
text = text.replace(old, new, 1)
PATH.write_text(text)
print("M169_TAIL_PREFLIGHT_TRACE_APPLIED")
