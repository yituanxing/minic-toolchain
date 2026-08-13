#!/usr/bin/env python3
from pathlib import Path

expr_path = Path("src/target/riscv64/codegen_expression.c")
text = expr_path.read_text()

old = '''        MinicType abi_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t parameter_count;
        size_t argument_count;
        size_t argument_index;
        size_t outgoing_stack_bytes;
        size_t stack_argument_count;
        size_t temporary_bytes;
'''
new = '''        MinicType abi_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
        MinicRiscv64AbiValue abi_values[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t argument_stage_end[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t parameter_count;
        size_t argument_count;
        size_t argument_index;
        size_t outgoing_stack_bytes;
        size_t stack_argument_count;
        size_t staged_bytes;
        size_t temporary_bytes;
'''
if old not in text:
    raise SystemExit("call local declarations anchor not found")
text = text.replace(old, new, 1)

old = '''        outgoing_stack_bytes = 0U;
        stack_argument_count = 0U;
'''
new = '''        outgoing_stack_bytes = 0U;
        stack_argument_count = 0U;
        staged_bytes = 0U;
        (void)memset(argument_stage_end, 0, sizeof(argument_stage_end));
'''
if old not in text:
    raise SystemExit("call staging initialization anchor not found")
text = text.replace(old, new, 1)

old = '''                fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") < 0) {
                return false;
            }
            parameter_types = indirect_type->parameter_types;
'''
new = '''                fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") < 0) {
                return false;
            }
            staged_bytes = 16U;
            parameter_types = indirect_type->parameter_types;
'''
if old not in text:
    raise SystemExit("indirect callee staging anchor not found")
text = text.replace(old, new, 1)

old = '''        for (argument_index = 0U; argument_index < parameter_count; ++argument_index) {
            if (!minic_c0_fixed_parameter_abi_type(program,
                                                   parameter_types[argument_index],
                                                   &abi_parameter_types[argument_index])) {
                return false;
            }
        }
'''
new = '''        for (argument_index = 0U; argument_index < parameter_count; ++argument_index) {
            if (!minic_c0_fixed_parameter_abi_type(program,
                                                   parameter_types[argument_index],
                                                   &abi_parameter_types[argument_index]) ||
                !minic_riscv64_classify_abi_value(
                    program, abi_parameter_types[argument_index], &abi_values[argument_index])) {
                return false;
            }
        }
'''
if old not in text:
    raise SystemExit("fixed parameter ABI classification anchor not found")
text = text.replace(old, new, 1)

old = '''            if (argument_index < parameter_count &&
                minic_type_is_record(abi_parameter_types[argument_index])) {
                size_t aggregate_size;
                size_t aggregate_chunks;

                if (!minic_type_is_record(argument->type) ||
                    argument->type.record_id != abi_parameter_types[argument_index].record_id ||
                    !minic_riscv64_integer_aggregate_abi(program,
                                                         abi_parameter_types[argument_index],
                                                         &aggregate_size,
                                                         &aggregate_chunks)) {
                    return false;
                }
                if (aggregate_chunks == 0U) {
                    continue;
                }
                if (!minic_c0_record_value_is_copy_source(
                        program, expression->value.call.arguments[argument_index]) ||
                    !minic_riscv64_emit_record_value_temporary(
                        file,
                        program,
                        function,
                        expression->value.call.arguments[argument_index],
                        aggregate_size,
                        16U)) {
                    return false;
                }
                continue;
            }
'''
new = '''            if (argument_index < parameter_count &&
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
if old not in text:
    raise SystemExit("materialized record argument staging anchor not found")
text = text.replace(old, new, 1)

old = '''            if (!minic_riscv64_emit_stack_allocate(file, 16U) ||
                fprintf(file, "  sd a0, 0(sp)\\n") < 0) {
                return false;
            }
        }
'''
new = '''            if (staged_bytes > SIZE_MAX - 16U ||
                !minic_riscv64_emit_stack_allocate(file, 16U) ||
                fprintf(file, "  sd a0, 0(sp)\\n") < 0) {
                return false;
            }
            staged_bytes += 16U;
            argument_stage_end[argument_index] = staged_bytes;
        }
'''
if old not in text:
    raise SystemExit("scalar argument staging anchor not found")
text = text.replace(old, new, 1)

old = '''                } else if (argument_index < parameter_count &&
                           minic_type_is_record(abi_parameter_types[argument_index])) {
                    size_t aggregate_size;
                    size_t aggregate_chunks;
                    size_t chunk_index;

                    if (!minic_riscv64_integer_aggregate_abi(program,
                                                             abi_parameter_types[argument_index],
                                                             &aggregate_size,
                                                             &aggregate_chunks)) {
                        return false;
                    }
                    (void)aggregate_size;
                    for (chunk_index = 0U; chunk_index < aggregate_chunks; ++chunk_index) {
                        if (integer_register_index < 8U) {
                            integer_register_index += 1U;
                        } else {
                            stack_argument_count += 1U;
                        }
                    }
'''
new = '''                } else if (argument_index < parameter_count &&
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
if old not in text:
    raise SystemExit("record argument slot counting anchor not found")
text = text.replace(old, new, 1)

old = '''                offset = outgoing_stack_bytes + (argument_count - 1U - argument_index) * 16U;
'''
new = '''                if (argument_stage_end[argument_index] > staged_bytes) {
                    return false;
                }
                offset = outgoing_stack_bytes + staged_bytes - argument_stage_end[argument_index];
'''
if old not in text:
    raise SystemExit("argument staged offset anchor not found")
text = text.replace(old, new, 1)

old = '''                } else if (argument_index < parameter_count &&
                           minic_type_is_record(abi_parameter_types[argument_index])) {
                    size_t aggregate_size;
                    size_t aggregate_chunks;
                    size_t chunk_index;

                    if (!minic_riscv64_integer_aggregate_abi(program,
                                                             abi_parameter_types[argument_index],
                                                             &aggregate_size,
                                                             &aggregate_chunks)) {
                        return false;
                    }
                    (void)aggregate_size;
                    for (chunk_index = 0U; chunk_index < aggregate_chunks; ++chunk_index) {
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
'''
new = '''                } else if (argument_index < parameter_count &&
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
if old not in text:
    raise SystemExit("record argument loading anchor not found")
text = text.replace(old, new, 1)

old = '''        if (is_indirect) {
            if (fprintf(file, "  ld t0, %zu(sp)\\n", outgoing_stack_bytes + argument_count * 16U) <
                0) {
                return false;
            }
            temporary_bytes = (argument_count + 1U) * 16U;
        } else {
            temporary_bytes = argument_count * 16U;
        }
'''
new = '''        if (is_indirect) {
            if (staged_bytes < 16U ||
                fprintf(file, "  ld t0, %zu(sp)\\n", outgoing_stack_bytes + staged_bytes - 16U) < 0) {
                return false;
            }
        }
        temporary_bytes = staged_bytes;
'''
if old not in text:
    raise SystemExit("call temporary byte accounting anchor not found")
text = text.replace(old, new, 1)
expr_path.write_text(text)

support_path = Path("src/target/riscv64/codegen_support.c")
text = support_path.read_text()
old = '''        if (minic_type_is_record(parameter->type)) {
            size_t aggregate_size;
            size_t aggregate_chunks;

            if (!minic_riscv64_integer_aggregate_abi(
                    program, parameter->type, &aggregate_size, &aggregate_chunks)) {
                const MinicRecord *record;
                size_t size;
                size_t alignment;
                size_t field_index;

                record = minic_c0_program_record(program, parameter->type.record_id);
                size = 0U;
                alignment = 0U;
                (void)minic_riscv64_type_layout(program, parameter->type, &size, &alignment);
                fprintf(stderr,
                        "CODEGEN_FRAME_AGG_REJECT param=%zu record=%zu size=%zu align=%zu complete=%d fields=%zu\\n",
                        parameter_index,
                        parameter->type.record_id,
                        size,
                        alignment,
                        record != NULL && record->is_complete ? 1 : 0,
                        record == NULL ? 0U : record->field_count);
                if (record != NULL) {
                    for (field_index = 0U; field_index < record->field_count; ++field_index) {
                        const MinicRecordField *field;
                        field = minic_c0_record_field(record, field_index);
                        fprintf(stderr,
                                "CODEGEN_FRAME_AGG_FIELD index=%zu kind=%d ptr=%u array=%d record=%zu count=%zu\\n",
                                field_index,
                                field == NULL ? -1 : (int)field->type.base_kind,
                                field == NULL ? 0U : field->type.pointer_depth,
                                field != NULL && minic_type_is_array(field->type) ? 1 : 0,
                                field == NULL ? SIZE_MAX : field->type.record_id,
                                field == NULL ? 0U : field->element_count);
                    }
                }
                return false;
            }
            if (integer_parameter_count > SIZE_MAX - aggregate_chunks) {
                return false;
            }
            (void)aggregate_size;
            integer_parameter_count += aggregate_chunks;
            continue;
        }
'''
new = '''        if (minic_type_is_record(parameter->type)) {
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
if old not in text:
    raise SystemExit("frame record ABI anchor not found")
text = text.replace(old, new, 1)
support_path.write_text(text)

function_path = Path("src/target/riscv64/codegen_function.c")
text = function_path.read_text()
old = '''            if (minic_type_is_record(parameter->type)) {
                size_t aggregate_size;
                size_t aggregate_chunks;
                size_t chunk_index;

                if (!minic_riscv64_integer_aggregate_abi(
                        program, parameter->type, &aggregate_size, &aggregate_chunks)) {
                    success = false;
                    break;
                }
                (void)aggregate_size;
                for (chunk_index = 0U; success && chunk_index < aggregate_chunks; ++chunk_index) {
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
'''
new = '''            if (minic_type_is_record(parameter->type)) {
                MinicRiscv64AbiValue abi_value;

                if (!minic_riscv64_classify_abi_value(program, parameter->type, &abi_value)) {
                    success = false;
                    break;
                }
                if (abi_value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                    continue;
                }
                if (abi_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                    const char *source_register;
                    size_t byte_index;

                    source_register = "t0";
                    if (integer_register_index < 8U) {
                        source_register = minic_riscv64_argument_registers[integer_register_index];
                        integer_register_index += 1U;
                        if (fprintf(file, "  mv t0, %s\\n", source_register) < 0) {
                            success = false;
                            break;
                        }
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
if old not in text:
    raise SystemExit("function record parameter materialization anchor not found")
text = text.replace(old, new, 1)
function_path.write_text(text)
