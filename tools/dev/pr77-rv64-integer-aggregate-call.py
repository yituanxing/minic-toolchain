#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text()

# Argument evaluation keeps the existing one-16-byte-temporary-per-C-argument
# scheme. An integer-class record occupies one temporary slot containing one or
# two XLEN chunks, so later register/stack assignment can consume chunks without
# inventing a second call path.
start = text.find('''        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            const MinicExpression *argument;
''')
end_marker = '''        {
            size_t integer_register_index;
            size_t floating_register_index;
'''
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("aggregate-call: cannot locate argument temporary loop")
argument_loop = r'''        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            const MinicExpression *argument;

            argument = minic_c0_program_expression(
                program, expression->value.call.arguments[argument_index]);
            if (argument == NULL) {
                return false;
            }
            if (argument_index < parameter_count &&
                minic_type_is_record(parameter_types[argument_index])) {
                size_t aggregate_size;
                size_t aggregate_chunks;

                if (!minic_type_is_record(argument->type) ||
                    argument->type.record_id != parameter_types[argument_index].record_id ||
                    argument->value_category != MINIC_VALUE_LVALUE ||
                    !minic_riscv64_integer_aggregate_abi(program,
                                                         parameter_types[argument_index],
                                                         &aggregate_size,
                                                         &aggregate_chunks) ||
                    !minic_riscv64_emit_lvalue_address(
                        file, program, function, expression->value.call.arguments[argument_index]) ||
                    !minic_riscv64_emit_stack_allocate(file, 16U) ||
                    fprintf(file,
                            "  mv t0, a0\n"
                            "  ld t1, 0(t0)\n"
                            "  sd t1, 0(sp)\n") < 0 ||
                    (aggregate_chunks == 2U &&
                     fprintf(file, "  ld t1, 8(t0)\n  sd t1, 8(sp)\n") < 0)) {
                    return false;
                }
                (void)aggregate_size;
                continue;
            }
            if (!minic_riscv64_emit_expression(
                    file, program, function, expression->value.call.arguments[argument_index])) {
                return false;
            }
            if (argument_index < parameter_count) {
                if (minic_type_is_integer(parameter_types[argument_index]) &&
                    !minic_riscv64_emit_integer_conversion(
                        file, parameter_types[argument_index], "a0")) {
                    return false;
                }
            } else if (!is_variadic ||
                       !minic_riscv64_emit_variadic_argument_conversion(file, argument->type)) {
                return false;
            }
            if (!minic_riscv64_emit_stack_allocate(file, 16U) ||
                fprintf(file, "  sd a0, 0(sp)\n") < 0) {
                return false;
            }
        }
'''
text = text[:start] + argument_loop + text[end:]

# Count integer ABI chunks, not source arguments. This keeps outgoing stack
# sizing coherent with the callee/frame classifier and naturally supports an
# aggregate split between the last integer register and the first stack slot.
start = text.find(end_marker, start + len(argument_loop))
end = text.find('''        if (stack_argument_count > (SIZE_MAX - 15U) / 8U) {
''', start)
if start < 0 or end < 0:
    raise SystemExit("aggregate-call: cannot locate register/stack counting block")
counting = r'''        {
            size_t integer_register_index;
            size_t floating_register_index;

            integer_register_index = 0U;
            floating_register_index = 0U;
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                bool fixed_floating;

                fixed_floating = argument_index < parameter_count &&
                                 (minic_type_is_double(parameter_types[argument_index]) ||
                                  minic_type_is_float(parameter_types[argument_index]));
                if (fixed_floating) {
                    if (floating_register_index >= 8U) {
                        return false;
                    }
                    floating_register_index += 1U;
                } else if (argument_index < parameter_count &&
                           minic_type_is_record(parameter_types[argument_index])) {
                    size_t aggregate_size;
                    size_t aggregate_chunks;
                    size_t chunk_index;

                    if (!minic_riscv64_integer_aggregate_abi(program,
                                                             parameter_types[argument_index],
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
                } else if (integer_register_index < 8U) {
                    integer_register_index += 1U;
                } else {
                    stack_argument_count += 1U;
                }
            }
        }
'''
text = text[:start] + counting + text[end:]

# Populate argument registers/outgoing stack from the temporary slots. Record
# chunks are loaded in little-endian XLEN order, matching the same a-register
# sequence consumed by the callee prologue.
start_marker = '''        {
            size_t integer_register_index;
            size_t floating_register_index;
            size_t stack_argument_index;
'''
start = text.find(start_marker, end + len(counting))
end = text.find('''        if (is_indirect) {
            if (fprintf(file,
''', start)
if start < 0 or end < 0:
    raise SystemExit("aggregate-call: cannot locate argument register materialization block")
materialize = r'''        {
            size_t integer_register_index;
            size_t floating_register_index;
            size_t stack_argument_index;

            integer_register_index = 0U;
            floating_register_index = 0U;
            stack_argument_index = 0U;
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                size_t offset;
                bool fixed_floating;

                offset = outgoing_stack_bytes + (argument_count - 1U - argument_index) * 16U;
                fixed_floating = argument_index < parameter_count &&
                                 (minic_type_is_double(parameter_types[argument_index]) ||
                                  minic_type_is_float(parameter_types[argument_index]));
                if (fixed_floating) {
                    if (floating_register_index >= 8U ||
                        fprintf(file,
                                minic_type_is_double(parameter_types[argument_index])
                                    ? "  ld t0, %zu(sp)\n  fmv.d.x fa%zu, t0\n"
                                    : "  ld t0, %zu(sp)\n  fmv.w.x fa%zu, t0\n",
                                offset,
                                floating_register_index) < 0) {
                        return false;
                    }
                    floating_register_index += 1U;
                } else if (argument_index < parameter_count &&
                           minic_type_is_record(parameter_types[argument_index])) {
                    size_t aggregate_size;
                    size_t aggregate_chunks;
                    size_t chunk_index;

                    if (!minic_riscv64_integer_aggregate_abi(program,
                                                             parameter_types[argument_index],
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
                                        "  ld a%zu, %zu(sp)\n",
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
                } else if (integer_register_index < 8U) {
                    if (fprintf(file, "  ld a%zu, %zu(sp)\n", integer_register_index, offset) < 0) {
                        return false;
                    }
                    integer_register_index += 1U;
                } else {
                    if (!minic_riscv64_emit_sp_load64(file, "t0", offset) ||
                        !minic_riscv64_emit_sp_store64(
                            file, "t0", stack_argument_index * 8U)) {
                        return false;
                    }
                    stack_argument_index += 1U;
                }
            }
        }
'''
text = text[:start] + materialize + text[end:]
path.write_text(text)

print("staged RV64 integer-class aggregate call arguments with shared register-chunk accounting")
