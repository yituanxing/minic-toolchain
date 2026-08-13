#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"{label} anchor not found")
    return text.replace(old, new, 1)


def replace_region(text: str, search_from: int, begin: str, end: str, replacement: str, label: str):
    start = text.find(begin, search_from)
    if start < 0:
        raise SystemExit(f"{label} begin not found")
    stop = text.find(end, start)
    if stop < 0:
        raise SystemExit(f"{label} end not found")
    return text[:start] + replacement + text[stop:], start + len(replacement)


expr_path = Path("src/target/riscv64/codegen_expression.c")
text = expr_path.read_text()
text = replace_once(
    text,
    '#include "target/riscv64/codegen_internal.h"\n',
    '#include "target/riscv64/codegen_internal.h"\n#include "target/riscv64/abi.h"\n',
    "expression ABI include",
)
text = replace_once(
    text,
    '''        MinicType abi_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t parameter_count;
        size_t argument_count;
        size_t argument_index;
        size_t outgoing_stack_bytes;
        size_t stack_argument_count;
        size_t temporary_bytes;
''',
    '''        MinicType abi_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
        MinicRiscv64AbiValue abi_values[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t argument_stage_end[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t parameter_count;
        size_t argument_count;
        size_t argument_index;
        size_t outgoing_stack_bytes;
        size_t stack_argument_count;
        size_t staged_bytes;
        size_t temporary_bytes;
''',
    "call locals",
)
text = replace_once(
    text,
    '''        outgoing_stack_bytes = 0U;
        stack_argument_count = 0U;
''',
    '''        outgoing_stack_bytes = 0U;
        stack_argument_count = 0U;
        staged_bytes = 0U;
        (void)memset(argument_stage_end, 0, sizeof(argument_stage_end));
''',
    "staging init",
)
text = replace_once(
    text,
    '''                fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") < 0) {
                return false;
            }
            parameter_types = indirect_type->parameter_types;
''',
    '''                fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") < 0) {
                return false;
            }
            staged_bytes = 16U;
            parameter_types = indirect_type->parameter_types;
''',
    "indirect callee stage",
)
text = replace_once(
    text,
    '''        for (argument_index = 0U; argument_index < parameter_count; ++argument_index) {
            if (!minic_c0_fixed_parameter_abi_type(program,
                                                   parameter_types[argument_index],
                                                   &abi_parameter_types[argument_index])) {
                return false;
            }
        }
''',
    '''        for (argument_index = 0U; argument_index < parameter_count; ++argument_index) {
            if (!minic_c0_fixed_parameter_abi_type(program,
                                                   parameter_types[argument_index],
                                                   &abi_parameter_types[argument_index]) ||
                !minic_riscv64_classify_abi_value(
                    program, abi_parameter_types[argument_index], &abi_values[argument_index])) {
                return false;
            }
        }
''',
    "parameter classify",
)

call_pos = text.find("    case MINIC_EXPRESSION_CALL:")
if call_pos < 0:
    raise SystemExit("CALL case not found")
loop_pos = text.find(
    "        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {",
    call_pos,
)
if loop_pos < 0:
    raise SystemExit("argument staging loop not found")
record_begin = '''            if (argument_index < parameter_count &&
                minic_type_is_record(abi_parameter_types[argument_index])) {
'''
scalar_begin = '''            if (!minic_riscv64_emit_expression(
'''
record_start = text.find(record_begin, loop_pos)
scalar_start = text.find(scalar_begin, record_start)
if record_start < 0 or scalar_start < 0:
    raise SystemExit("record staging semantic region not found")
record_new = '''            if (argument_index < parameter_count &&
                minic_type_is_record(abi_parameter_types[argument_index])) {
                const MinicRiscv64AbiValue *abi_value;
                size_t stage_size;

                abi_value = &abi_values[argument_index];
                if (!minic_type_is_record(argument->type) ||
                    argument->type.record_id != abi_parameter_types[argument_index].record_id) {
                    return false;
                }
                if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                    argument_stage_end[argument_index] = staged_bytes;
                    continue;
                }
                if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                    stage_size = 16U;
                } else if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                    if (abi_value->storage_size > SIZE_MAX - 15U) {
                        return false;
                    }
                    stage_size = (abi_value->storage_size + 15U) & ~(size_t)15U;
                } else {
                    return false;
                }
                if (stage_size == 0U || staged_bytes > SIZE_MAX - stage_size ||
                    !minic_c0_record_value_is_copy_source(
                        program, expression->value.call.arguments[argument_index]) ||
                    !minic_riscv64_emit_record_value_temporary(
                        file,
                        program,
                        function,
                        expression->value.call.arguments[argument_index],
                        abi_value->storage_size,
                        stage_size)) {
                    return false;
                }
                staged_bytes += stage_size;
                argument_stage_end[argument_index] = staged_bytes;
                continue;
            }
'''
text = text[:record_start] + record_new + text[scalar_start:]
text = replace_once(
    text,
    '''            if (!minic_riscv64_emit_stack_allocate(file, 16U) ||
                fprintf(file, "  sd a0, 0(sp)\\n") < 0) {
                return false;
            }
        }
''',
    '''            if (staged_bytes > SIZE_MAX - 16U ||
                !minic_riscv64_emit_stack_allocate(file, 16U) ||
                fprintf(file, "  sd a0, 0(sp)\\n") < 0) {
                return false;
            }
            staged_bytes += 16U;
            argument_stage_end[argument_index] = staged_bytes;
        }
''',
    "scalar staging",
)

count_marker = '''            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                bool fixed_floating;
'''
count_pos = text.find(count_marker, call_pos)
count_record = '''                } else if (argument_index < parameter_count &&
                           minic_type_is_record(abi_parameter_types[argument_index])) {
'''
count_start = text.find(count_record, count_pos)
count_end_marker = '''                } else if (integer_register_index < 8U) {
'''
count_end = text.find(count_end_marker, count_start)
if count_start < 0 or count_end < 0:
    raise SystemExit("record slot count semantic region not found")
count_new = '''                } else if (argument_index < parameter_count &&
                           minic_type_is_record(abi_parameter_types[argument_index])) {
                    const MinicRiscv64AbiValue *abi_value;
                    size_t integer_slots;
                    size_t slot_index;

                    abi_value = &abi_values[argument_index];
                    if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                        continue;
                    }
                    if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                        integer_slots = abi_value->register_chunks;
                    } else if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                        integer_slots = 1U;
                    } else {
                        return false;
                    }
                    for (slot_index = 0U; slot_index < integer_slots; ++slot_index) {
                        if (integer_register_index < 8U) {
                            integer_register_index += 1U;
                        } else {
                            stack_argument_count += 1U;
                        }
                    }
'''
text = text[:count_start] + count_new + text[count_end:]
text = replace_once(
    text,
    '''                offset = outgoing_stack_bytes + (argument_count - 1U - argument_index) * 16U;
''',
    '''                if (argument_stage_end[argument_index] > staged_bytes) {
                    return false;
                }
                offset = outgoing_stack_bytes + staged_bytes - argument_stage_end[argument_index];
''',
    "argument stage offset",
)

load_marker = '''            size_t stack_argument_index;

            integer_register_index = 0U;
'''
load_pos = text.find(load_marker, call_pos)
load_start = text.find(count_record, load_pos)
load_end_marker = '''                } else if (integer_register_index < 8U) {
                    if (fprintf(file, "  ld a%zu, %zu(sp)\\n", integer_register_index, offset) < 0) {
'''
load_end = text.find(load_end_marker, load_start)
if load_start < 0 or load_end < 0:
    raise SystemExit("record load semantic region not found")
load_new = '''                } else if (argument_index < parameter_count &&
                           minic_type_is_record(abi_parameter_types[argument_index])) {
                    const MinicRiscv64AbiValue *abi_value;

                    abi_value = &abi_values[argument_index];
                    if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                        continue;
                    }
                    if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                        const char *destination_register;

                        destination_register = integer_register_index < 8U ?
                                                   minic_riscv64_argument_registers[integer_register_index] :
                                                   "t0";
                        if (offset <= 2047U) {
                            if (fprintf(file,
                                        "  addi %s, sp, %zu\\n",
                                        destination_register,
                                        offset) < 0) {
                                return false;
                            }
                        } else if (fprintf(file,
                                           "  li t1, %zu\\n  add %s, sp, t1\\n",
                                           offset,
                                           destination_register) < 0) {
                            return false;
                        }
                        if (integer_register_index < 8U) {
                            integer_register_index += 1U;
                        } else {
                            if (!minic_riscv64_emit_sp_store64(
                                    file, "t0", stack_argument_index * 8U)) {
                                return false;
                            }
                            stack_argument_index += 1U;
                        }
                    } else if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                        size_t chunk_index;

                        for (chunk_index = 0U; chunk_index < abi_value->register_chunks; ++chunk_index) {
                            size_t chunk_offset;

                            chunk_offset = offset + chunk_index * 8U;
                            if (integer_register_index < 8U) {
                                if (fprintf(file,
                                            "  ld a%zu, %zu(sp)\\n",
                                            integer_register_index,
                                            chunk_offset) < 0) {
                                    return false;
                                }
                                integer_register_index += 1U;
                            } else {
                                if (!minic_riscv64_emit_sp_load64(file, "t0", chunk_offset) ||
                                    !minic_riscv64_emit_sp_store64(
                                        file, "t0", stack_argument_index * 8U)) {
                                    return false;
                                }
                                stack_argument_index += 1U;
                            }
                        }
                    } else {
                        return false;
                    }
'''
text = text[:load_start] + load_new + text[load_end:]
text = replace_once(
    text,
    '''        if (is_indirect) {
            if (fprintf(file, "  ld t0, %zu(sp)\\n", outgoing_stack_bytes + argument_count * 16U) <
                0) {
                return false;
            }
            temporary_bytes = (argument_count + 1U) * 16U;
        } else {
            temporary_bytes = argument_count * 16U;
        }
''',
    '''        if (is_indirect) {
            if (staged_bytes < 16U ||
                fprintf(file, "  ld t0, %zu(sp)\\n", outgoing_stack_bytes + staged_bytes - 16U) < 0) {
                return false;
            }
        }
        temporary_bytes = staged_bytes;
''',
    "temporary accounting",
)
expr_path.write_text(text)

support_path = Path("src/target/riscv64/codegen_support.c")
text = support_path.read_text()
frame_pos = text.find("bool minic_riscv64_frame_layout(")
if frame_pos < 0:
    raise SystemExit("frame layout function not found")
record_start = text.find("        if (minic_type_is_record(parameter->type)) {\n", frame_pos)
record_end_marker = "            continue;\n        }\n"
record_end = text.find(record_end_marker, record_start)
if record_start < 0 or record_end < 0:
    raise SystemExit("frame record semantic region not found")
record_end += len(record_end_marker)
frame_new = '''        if (minic_type_is_record(parameter->type)) {
            MinicRiscv64AbiValue abi_value;
            size_t integer_slots;

            if (!minic_riscv64_classify_abi_value(program, parameter->type, &abi_value)) {
                return false;
            }
            if (abi_value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                integer_slots = 0U;
            } else if (abi_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                integer_slots = abi_value.register_chunks;
            } else if (abi_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                integer_slots = 1U;
            } else {
                return false;
            }
            if (integer_parameter_count > SIZE_MAX - integer_slots) {
                return false;
            }
            integer_parameter_count += integer_slots;
            continue;
        }
'''
text = text[:record_start] + frame_new + text[record_end:]
support_path.write_text(text)

function_path = Path("src/target/riscv64/codegen_function.c")
text = function_path.read_text()
text = replace_once(
    text,
    '#include "target/riscv64/codegen.h"\n',
    '#include "target/riscv64/codegen.h"\n#include "target/riscv64/abi.h"\n',
    "function ABI include",
)
param_loop = text.find("        for (parameter_index = 0U; success && parameter_index < function->parameter_count;")
record_start = text.find("            if (minic_type_is_record(parameter->type)) {\n", param_loop)
record_end_marker = "                continue;\n            }\n"
record_end = text.find(record_end_marker, record_start)
if record_start < 0 or record_end < 0:
    raise SystemExit("function record parameter semantic region not found")
record_end += len(record_end_marker)
function_new = '''            if (minic_type_is_record(parameter->type)) {
                MinicRiscv64AbiValue abi_value;

                if (!minic_riscv64_classify_abi_value(program, parameter->type, &abi_value)) {
                    success = false;
                    break;
                }
                if (abi_value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                    continue;
                }
                if (abi_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                    size_t byte_index;

                    if (integer_register_index < 8U) {
                        if (fprintf(file,
                                    "  mv t0, %s\\n",
                                    minic_riscv64_argument_registers[integer_register_index]) < 0) {
                            success = false;
                            break;
                        }
                        integer_register_index += 1U;
                    } else {
                        size_t incoming_offset;

                        if (stack_parameter_index > (SIZE_MAX - frame_size) / 8U) {
                            success = false;
                            break;
                        }
                        incoming_offset = frame_size + stack_parameter_index * 8U;
                        success = minic_riscv64_emit_sp_load64(file, "t0", incoming_offset);
                        stack_parameter_index += 1U;
                        if (!success) {
                            break;
                        }
                    }
                    if (!minic_riscv64_emit_object_address(file, program, function, local_id)) {
                        success = false;
                        break;
                    }
                    for (byte_index = 0U; success && byte_index < abi_value.storage_size; ++byte_index) {
                        if (byte_index <= 2047U) {
                            success = fprintf(file,
                                              "  lbu t1, %zu(t0)\\n  sb t1, %zu(a0)\\n",
                                              byte_index,
                                              byte_index) >= 0;
                        } else {
                            success = fprintf(file,
                                              "  li t2, %zu\\n"
                                              "  add t3, t0, t2\\n"
                                              "  lbu t1, 0(t3)\\n"
                                              "  add t3, a0, t2\\n"
                                              "  sb t1, 0(t3)\\n",
                                              byte_index) >= 0;
                        }
                    }
                    continue;
                }
                if (abi_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                    size_t chunk_index;

                    for (chunk_index = 0U; success && chunk_index < abi_value.register_chunks; ++chunk_index) {
                        const char *source_register;

                        source_register = "t0";
                        if (integer_register_index < 8U) {
                            source_register = minic_riscv64_argument_registers[integer_register_index];
                            integer_register_index += 1U;
                        } else {
                            size_t incoming_offset;

                            if (stack_parameter_index > (SIZE_MAX - frame_size) / 8U) {
                                success = false;
                                break;
                            }
                            incoming_offset = frame_size + stack_parameter_index * 8U;
                            success = minic_riscv64_emit_sp_load64(file, "t0", incoming_offset);
                            stack_parameter_index += 1U;
                        }
                        if (success) {
                            success = minic_riscv64_emit_integer_aggregate_local_chunk(
                                file, program, function, local_id, chunk_index, source_register);
                        }
                    }
                    continue;
                }
                success = false;
                break;
            }
'''
text = text[:record_start] + function_new + text[record_end:]
function_path.write_text(text)
