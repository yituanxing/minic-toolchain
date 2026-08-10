#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


# Frontend: complete record return types are C language semantics. Target ABI
# classification is deliberately not embedded in the parser.
replace_once(
    "src/frontend/parser_function.c",
    '''    if (!minic_type_is_integer(return_type) && !minic_type_is_void(return_type) &&
        !minic_type_is_pointer(return_type) && !minic_type_is_double(return_type)) {
        minic_parser_error(parser, "unsupported function return type");
        return false;
    }
''',
    '''    if (!minic_type_is_integer(return_type) && !minic_type_is_void(return_type) &&
        !minic_type_is_pointer(return_type) && !minic_type_is_double(return_type) &&
        !minic_type_is_record(return_type)) {
        minic_parser_error(parser, "unsupported function return type");
        return false;
    }
    if (minic_type_is_record(return_type) &&
        !minic_parser_require_complete_object_type(
            parser, return_type, "function definition requires a complete record return type")) {
        return false;
    }
''',
    "record-return-frontend-acceptance",
)
replace_once(
    "src/frontend/parser_function.c",
    '''    if ((!minic_type_is_pointer(return_type) && !minic_type_is_double(return_type) &&
         !minic_parser_add_default_return(parser)) ||
''',
    '''    if ((!minic_type_is_pointer(return_type) && !minic_type_is_double(return_type) &&
         !minic_type_is_record(return_type) && !minic_parser_add_default_return(parser)) ||
''',
    "record-return-fallthrough",
)

# TargetABI bootstrap interface. Keep this target-specific and narrowly truthful:
# the current direct backend accepts complete integer-class records whose laid-out
# size is one or two XLEN chunks (8/16 bytes). Floating aggregates remain rejected
# until the hard-float flattening rules are implemented.
replace_once(
    "src/target/riscv64/codegen_internal.h",
    '''bool minic_riscv64_emit_object_store_register(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicFunction *function,
                                              MinicLocalId local_id,
                                              const char *register_name);
''',
    '''bool minic_riscv64_emit_object_store_register(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicFunction *function,
                                              MinicLocalId local_id,
                                              const char *register_name);
bool minic_riscv64_integer_aggregate_abi(const MinicC0Program *program,
                                         MinicType type,
                                         size_t *storage_size,
                                         size_t *register_chunks);
bool minic_riscv64_emit_integer_aggregate_local_chunk(FILE *file,
                                                       const MinicC0Program *program,
                                                       const MinicFunction *function,
                                                       MinicLocalId local_id,
                                                       size_t chunk_index,
                                                       const char *register_name);
''',
    "aggregate-abi-prototypes",
)

path = Path("src/target/riscv64/codegen_support.c")
text = path.read_text()
marker = '''static bool minic_riscv64_local_object(const MinicC0Program *program,
'''
helpers = r'''static bool minic_riscv64_integer_aggregate_member_type(const MinicC0Program *program,
                                                         MinicType type) {
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        return array_type != NULL &&
               minic_riscv64_integer_aggregate_member_type(program, array_type->element_type);
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *field;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL ||
                !minic_riscv64_integer_aggregate_member_type(program, field->type)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool minic_riscv64_integer_aggregate_abi(const MinicC0Program *program,
                                         MinicType type,
                                         size_t *storage_size,
                                         size_t *register_chunks) {
    size_t alignment;
    size_t size;

    if (program == NULL || storage_size == NULL || register_chunks == NULL ||
        !minic_type_is_record(type) ||
        !minic_riscv64_integer_aggregate_member_type(program, type) ||
        !minic_riscv64_type_layout(program, type, &size, &alignment) ||
        (size != 8U && size != 16U)) {
        return false;
    }
    (void)alignment;
    *storage_size = size;
    *register_chunks = size / 8U;
    return true;
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"aggregate-abi-helper-anchor: expected one marker, found {text.count(marker)}")
text = text.replace(marker, helpers + marker, 1)

# Add a local chunk store next to the existing scalar local access helpers so
# prologue ABI materialization can share the same large-offset handling.
marker = '''bool minic_riscv64_emit_object_store(FILE *file,
'''
helper = r'''bool minic_riscv64_emit_integer_aggregate_local_chunk(FILE *file,
                                                       const MinicC0Program *program,
                                                       const MinicFunction *function,
                                                       MinicLocalId local_id,
                                                       size_t chunk_index,
                                                       const char *register_name) {
    const MinicLocal *local;
    size_t chunks;
    size_t storage_size;
    size_t chunk_offset;

    if (register_name == NULL ||
        !minic_riscv64_local_object(program, function, local_id, &local) ||
        !minic_riscv64_integer_aggregate_abi(program, local->type, &storage_size, &chunks) ||
        chunk_index >= chunks || chunk_index > (SIZE_MAX - local->storage_offset) / 8U) {
        return false;
    }
    chunk_offset = local->storage_offset + chunk_index * 8U;
    if (chunk_offset > function->local_storage_size ||
        function->local_storage_size - chunk_offset < 8U) {
        return false;
    }
    return minic_riscv64_emit_s0_access(file, "sd", register_name, chunk_offset);
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"aggregate-local-store-anchor: expected one marker, found {text.count(marker)}")
text = text.replace(marker, helper + marker, 1)

# Frame accounting counts integer register *chunks*, not C parameters.
old = '''        if (minic_type_is_double(parameter->type) || minic_type_is_float(parameter->type)) {
            continue;
        }
        if (!minic_type_is_integer(parameter->type) && !minic_type_is_pointer(parameter->type)) {
            return false;
        }
        integer_parameter_count += 1U;
'''
new = '''        if (minic_type_is_double(parameter->type) || minic_type_is_float(parameter->type)) {
            continue;
        }
        if (minic_type_is_record(parameter->type)) {
            size_t aggregate_size;
            size_t aggregate_chunks;

            if (!minic_riscv64_integer_aggregate_abi(
                    program, parameter->type, &aggregate_size, &aggregate_chunks) ||
                integer_parameter_count > SIZE_MAX - aggregate_chunks) {
                return false;
            }
            (void)aggregate_size;
            integer_parameter_count += aggregate_chunks;
            continue;
        }
        if (!minic_type_is_integer(parameter->type) && !minic_type_is_pointer(parameter->type)) {
            return false;
        }
        integer_parameter_count += 1U;
'''
if text.count(old) != 1:
    raise SystemExit(f"aggregate-frame-accounting: expected one scalar accounting block, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

# Callee parameter materialization: replace the staged scalar/stack loop as one
# structural block. Record chunks consume the same integer-register sequence as
# scalar integer/pointer parameters and can split to incoming stack chunks later.
path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
start = text.find('''    if (success) {
        size_t parameter_index;
        size_t integer_register_index;
        size_t floating_register_index;
        size_t stack_parameter_index;
''')
end = text.find('''    if (success) {
        success =
            minic_riscv64_emit_block''', start)
if start < 0 or end < 0:
    raise SystemExit("aggregate-parameter-materialization: cannot locate staged parameter block")
replacement = r'''    if (success) {
        size_t parameter_index;
        size_t integer_register_index;
        size_t floating_register_index;
        size_t stack_parameter_index;

        integer_register_index = 0U;
        floating_register_index = 0U;
        stack_parameter_index = 0U;

        for (parameter_index = 0U; success && parameter_index < function->parameter_count;
             ++parameter_index) {
            const MinicLocal *parameter;
            MinicLocalId local_id;

            local_id = function->local_begin + parameter_index;
            parameter = minic_c0_program_local(program, local_id);
            if (parameter == NULL) {
                success = false;
                break;
            }
            if (minic_type_is_double(parameter->type) || minic_type_is_float(parameter->type)) {
                if (floating_register_index >= 8U) {
                    success = false;
                    break;
                }
                success = fprintf(file,
                                  minic_type_is_double(parameter->type) ? "  fmv.x.d t0, fa%zu\n"
                                                                        : "  fmv.x.w t0, fa%zu\n",
                                  floating_register_index) >= 0 &&
                          minic_riscv64_emit_object_store_register(
                              file, program, function, local_id, "t0");
                floating_register_index += 1U;
                continue;
            }
            if (minic_type_is_record(parameter->type)) {
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
            if (integer_register_index < 8U) {
                success = minic_riscv64_emit_object_store_register(
                    file,
                    program,
                    function,
                    local_id,
                    minic_riscv64_argument_registers[integer_register_index]);
                integer_register_index += 1U;
            } else {
                size_t incoming_offset;

                if (stack_parameter_index > (SIZE_MAX - frame_size) / 8U) {
                    success = false;
                    break;
                }
                incoming_offset = frame_size + stack_parameter_index * 8U;
                success = minic_riscv64_emit_sp_load64(file, "t0", incoming_offset) &&
                          minic_riscv64_emit_object_store_register(
                              file, program, function, local_id, "t0");
                stack_parameter_index += 1U;
            }
        }
    }
'''
path.write_text(text[:start] + replacement + text[end:])

# Return a one/two-XLEN integer-class record directly in a0/a1. An lvalue source
# is loaded from its object address; a record-valued call already leaves the same
# ABI register pair live and therefore needs no materialization.
path = Path("src/target/riscv64/codegen_statement.c")
text = path.read_text()
needle = '''        value = minic_c0_program_expression(program, statement->expression);
        if (minic_type_is_void(function->return_type) || value == NULL ||
            !minic_c0_assignment_compatible(
                program, function->return_type, statement->expression) ||
            !minic_riscv64_emit_expression(file, program, function, statement->expression)) {
            return false;
        }
        if (minic_type_is_integer(function->return_type) &&
'''
replacement = '''        value = minic_c0_program_expression(program, statement->expression);
        if (minic_type_is_void(function->return_type) || value == NULL ||
            !minic_c0_assignment_compatible(
                program, function->return_type, statement->expression)) {
            return false;
        }
        if (minic_type_is_record(function->return_type)) {
            size_t aggregate_size;
            size_t aggregate_chunks;

            if (!minic_type_is_record(value->type) ||
                value->type.record_id != function->return_type.record_id ||
                !minic_riscv64_integer_aggregate_abi(
                    program, function->return_type, &aggregate_size, &aggregate_chunks)) {
                return false;
            }
            (void)aggregate_size;
            if (value->value_category == MINIC_VALUE_LVALUE) {
                if (!minic_riscv64_emit_lvalue_address(
                        file, program, function, statement->expression) ||
                    fprintf(file, "  mv t0, a0\n  ld a0, 0(t0)\n") < 0 ||
                    (aggregate_chunks == 2U && fprintf(file, "  ld a1, 8(t0)\n") < 0)) {
                    return false;
                }
            } else if (value->kind != MINIC_EXPRESSION_CALL ||
                       !minic_riscv64_emit_expression(
                           file, program, function, statement->expression)) {
                return false;
            }
        } else if (!minic_riscv64_emit_expression(
                       file, program, function, statement->expression)) {
            return false;
        }
        if (minic_type_is_integer(function->return_type) &&
'''
if text.count(needle) != 1:
    raise SystemExit(f"aggregate-return-emitter: expected one return body, found {text.count(needle)}")
path.write_text(text.replace(needle, replacement, 1))

# A record-valued call's direct-AST result is the psABI return register pair. It
# can feed a record return immediately; later record-rvalue materialization will
# provide an addressable temporary for assignment/member-access consumers.
path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text()
old = '''        if (minic_type_is_double(expression->type)) {
            return fprintf(file, "  fmv.x.d a0, fa0\\n") >= 0;
        }
        return minic_type_is_pointer(expression->type) || minic_type_is_void(expression->type);
'''
new = '''        if (minic_type_is_double(expression->type)) {
            return fprintf(file, "  fmv.x.d a0, fa0\\n") >= 0;
        }
        if (minic_type_is_record(expression->type)) {
            size_t aggregate_size;
            size_t aggregate_chunks;

            return minic_riscv64_integer_aggregate_abi(
                program, expression->type, &aggregate_size, &aggregate_chunks);
        }
        return minic_type_is_pointer(expression->type) || minic_type_is_void(expression->type);
'''
if text.count(old) != 1:
    raise SystemExit(f"aggregate-call-result: expected one call result tail, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

print("staged RV64 integer-class record ABI: complete record returns, aggregate parameter chunks and a0/a1 return values")
