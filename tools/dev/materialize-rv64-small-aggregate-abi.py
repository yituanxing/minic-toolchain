#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]

support_path = root / 'src/target/riscv64/codegen_support.c'
support = support_path.read_text()
old_abi = '''    if (program == NULL || storage_size == NULL || register_chunks == NULL ||
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
new_abi = '''    if (program == NULL || storage_size == NULL || register_chunks == NULL ||
        !minic_type_is_record(type) ||
        !minic_riscv64_integer_aggregate_member_type(program, type) ||
        !minic_riscv64_type_layout(program, type, &size, &alignment) || size == 0U || size > 16U) {
        return false;
    }
    (void)alignment;
    *storage_size = size;
    *register_chunks = (size + 7U) / 8U;
    return true;
}

static bool minic_riscv64_integer_aggregate_chunk_size(size_t storage_size,
                                                       size_t chunk_index,
                                                       size_t *chunk_size) {
    size_t chunk_offset;
    size_t remaining;

    if (chunk_size == NULL || storage_size == 0U || storage_size > 16U || chunk_index >= 2U ||
        chunk_index > SIZE_MAX / 8U) {
        return false;
    }
    chunk_offset = chunk_index * 8U;
    if (chunk_offset >= storage_size) {
        return false;
    }
    remaining = storage_size - chunk_offset;
    *chunk_size = remaining < 8U ? remaining : 8U;
    return true;
}

bool minic_riscv64_emit_integer_aggregate_chunk_load(FILE *file,
                                                     size_t storage_size,
                                                     size_t chunk_index,
                                                     const char *destination_register,
                                                     const char *address_register) {
    size_t chunk_size;
    size_t chunk_offset;
    size_t byte_index;

    if (file == NULL || destination_register == NULL || address_register == NULL ||
        !minic_riscv64_integer_aggregate_chunk_size(storage_size, chunk_index, &chunk_size)) {
        return false;
    }
    chunk_offset = chunk_index * 8U;
    if (chunk_offset == 0U) {
        if (fprintf(file, "  mv t5, %s\\n", address_register) < 0) {
            return false;
        }
    } else if (chunk_offset <= 2047U) {
        if (fprintf(file, "  addi t5, %s, %zu\\n", address_register, chunk_offset) < 0) {
            return false;
        }
    } else if (fprintf(file,
                       "  li t6, %zu\\n"
                       "  add t5, %s, t6\\n",
                       chunk_offset,
                       address_register) < 0) {
        return false;
    }

    switch (chunk_size) {
    case 8U:
        return fprintf(file, "  ld %s, 0(t5)\\n", destination_register) >= 0;
    case 4U:
        return fprintf(file, "  lwu %s, 0(t5)\\n", destination_register) >= 0;
    case 2U:
        return fprintf(file, "  lhu %s, 0(t5)\\n", destination_register) >= 0;
    case 1U:
        return fprintf(file, "  lbu %s, 0(t5)\\n", destination_register) >= 0;
    default:
        break;
    }

    if (fprintf(file, "  li %s, 0\\n", destination_register) < 0) {
        return false;
    }
    for (byte_index = 0U; byte_index < chunk_size; ++byte_index) {
        if (fprintf(file, "  lbu t6, %zu(t5)\\n", byte_index) < 0 ||
            (byte_index != 0U && fprintf(file, "  slli t6, t6, %zu\\n", byte_index * 8U) < 0) ||
            fprintf(file, "  or %s, %s, t6\\n", destination_register, destination_register) < 0) {
            return false;
        }
    }
    return true;
}
'''
if support.count(old_abi) != 1:
    raise SystemExit('integer aggregate ABI anchor missing')
support = support.replace(old_abi, new_abi, 1)

old_store = '''bool minic_riscv64_emit_integer_aggregate_local_chunk(FILE *file,
                                                      const MinicC0Program *program,
                                                      const MinicFunction *function,
                                                      MinicLocalId local_id,
                                                      size_t chunk_index,
                                                      const char *register_name) {
    const MinicLocal *local;
    size_t chunks;
    size_t storage_size;
    size_t chunk_offset;

    if (register_name == NULL || !minic_riscv64_local_object(program, function, local_id, &local) ||
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
new_store = '''bool minic_riscv64_emit_integer_aggregate_local_chunk(FILE *file,
                                                      const MinicC0Program *program,
                                                      const MinicFunction *function,
                                                      MinicLocalId local_id,
                                                      size_t chunk_index,
                                                      const char *register_name) {
    const MinicLocal *local;
    size_t chunks;
    size_t storage_size;
    size_t chunk_size;
    size_t chunk_offset;
    size_t byte_index;

    if (register_name == NULL || !minic_riscv64_local_object(program, function, local_id, &local) ||
        !minic_riscv64_integer_aggregate_abi(program, local->type, &storage_size, &chunks) ||
        chunk_index >= chunks ||
        !minic_riscv64_integer_aggregate_chunk_size(storage_size, chunk_index, &chunk_size) ||
        chunk_index > (SIZE_MAX - local->storage_offset) / 8U) {
        return false;
    }
    chunk_offset = local->storage_offset + chunk_index * 8U;
    if (chunk_offset > function->local_storage_size ||
        function->local_storage_size - chunk_offset < chunk_size) {
        return false;
    }

    switch (chunk_size) {
    case 8U:
        return minic_riscv64_emit_s0_access(file, "sd", register_name, chunk_offset);
    case 4U:
        return minic_riscv64_emit_s0_access(file, "sw", register_name, chunk_offset);
    case 2U:
        return minic_riscv64_emit_s0_access(file, "sh", register_name, chunk_offset);
    case 1U:
        return minic_riscv64_emit_s0_access(file, "sb", register_name, chunk_offset);
    default:
        break;
    }

    if (fprintf(file, "  mv t6, %s\\n", register_name) < 0) {
        return false;
    }
    for (byte_index = 0U; byte_index < chunk_size; ++byte_index) {
        if (!minic_riscv64_emit_s0_access(file, "sb", "t6", chunk_offset + byte_index) ||
            (byte_index + 1U < chunk_size && fprintf(file, "  srli t6, t6, 8\\n") < 0)) {
            return false;
        }
    }
    return true;
}
'''
if support.count(old_store) != 1:
    raise SystemExit('aggregate local chunk store anchor missing')
support_path.write_text(support.replace(old_store, new_store, 1))

header_path = root / 'src/target/riscv64/codegen_internal.h'
header = header_path.read_text()
proto_anchor = '''bool minic_riscv64_integer_aggregate_abi(const MinicC0Program *program,
                                         MinicType type,
                                         size_t *storage_size,
                                         size_t *register_chunks);
'''
proto_new = proto_anchor + '''bool minic_riscv64_emit_integer_aggregate_chunk_load(FILE *file,
                                                     size_t storage_size,
                                                     size_t chunk_index,
                                                     const char *destination_register,
                                                     const char *address_register);
'''
if header.count(proto_anchor) != 1:
    raise SystemExit('aggregate ABI prototype anchor missing')
header_path.write_text(header.replace(proto_anchor, proto_new, 1))

expr_path = root / 'src/target/riscv64/codegen_expression.c'
expr = expr_path.read_text()
old_call_stage = '''                    !minic_riscv64_emit_lvalue_address(
                        file,
                        program,
                        function,
                        expression->value.call.arguments[argument_index]) ||
                    !minic_riscv64_emit_stack_allocate(file, 16U) ||
                    fprintf(file,
                            "  mv t0, a0\\n"
                            "  ld t1, 0(t0)\\n"
                            "  sd t1, 0(sp)\\n") < 0 ||
                    (aggregate_chunks == 2U &&
                     fprintf(file, "  ld t1, 8(t0)\\n  sd t1, 8(sp)\\n") < 0)) {
                    return false;
                }
                (void)aggregate_size;
                continue;
'''
new_call_stage = '''                    !minic_riscv64_emit_lvalue_address(
                        file,
                        program,
                        function,
                        expression->value.call.arguments[argument_index]) ||
                    !minic_riscv64_emit_stack_allocate(file, 16U) ||
                    fprintf(file, "  mv t0, a0\\n") < 0) {
                    return false;
                }
                {
                    size_t chunk_index;

                    for (chunk_index = 0U; chunk_index < aggregate_chunks; ++chunk_index) {
                        if (!minic_riscv64_emit_integer_aggregate_chunk_load(
                                file, aggregate_size, chunk_index, "t1", "t0") ||
                            fprintf(file, "  sd t1, %zu(sp)\\n", chunk_index * 8U) < 0) {
                            return false;
                        }
                    }
                }
                continue;
'''
if expr.count(old_call_stage) != 1:
    raise SystemExit('aggregate call staging anchor missing')
expr_path.write_text(expr.replace(old_call_stage, new_call_stage, 1))

stmt_path = root / 'src/target/riscv64/codegen_statement.c'
stmt = stmt_path.read_text()
old_return = '''                if (!minic_riscv64_emit_lvalue_address(
                        file, program, function, statement->expression) ||
                    fprintf(file, "  mv t0, a0\\n  ld a0, 0(t0)\\n") < 0 ||
                    (aggregate_chunks == 2U && fprintf(file, "  ld a1, 8(t0)\\n") < 0)) {
                    return false;
                }
'''
new_return = '''                if (!minic_riscv64_emit_lvalue_address(
                        file, program, function, statement->expression) ||
                    fprintf(file, "  mv t0, a0\\n") < 0 ||
                    !minic_riscv64_emit_integer_aggregate_chunk_load(
                        file, aggregate_size, 0U, "a0", "t0") ||
                    (aggregate_chunks == 2U &&
                     !minic_riscv64_emit_integer_aggregate_chunk_load(
                         file, aggregate_size, 1U, "a1", "t0"))) {
                    return false;
                }
'''
if stmt.count(old_return) != 1:
    raise SystemExit('aggregate return load anchor missing')
stmt_path.write_text(stmt.replace(old_return, new_return, 1))

source_path = root / 'tests/compiler/c0/rv64_integer_aggregate_return.c'
source = source_path.read_text()
source += '''\nstruct word32 {\n    unsigned int value;\n};\n\nstatic unsigned int unwrap_word(struct word32 input) {\n    return input.value;\n}\n\nstatic struct word32 return_word(struct word32 input) {\n    return input;\n}\n\nstatic unsigned int call_unwrap_word(void) {\n    struct word32 input;\n    input.value = 7U;\n    return unwrap_word(input);\n}\n'''
source_path.write_text(source)

run_path = root / 'tests/compiler/c0/run-rv64-integer-aggregate-return.sh'
run = run_path.read_text()
old_msg = "printf '%s\\n' 'PASS compiler/c0/rv64_integer_aggregate_return size=16 class=integer callee-params=a0-a3 caller-chunks=1 return=a0-a1 record-local=1 record-call=1'\n"
new_msg = '''grep -F 'unwrap_word:' "$assembly" >/dev/null\ngrep -F 'return_word:' "$assembly" >/dev/null\ngrep -F 'call unwrap_word' "$assembly" >/dev/null\ngrep -F '  sw a0,' "$assembly" >/dev/null\ngrep -F '  lwu t1, 0(t5)' "$assembly" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/rv64_integer_aggregate_return sizes=4,16 class=integer callee-partial=exact caller-partial=exact return-partial=exact record-call=1'\n'''
if run.count(old_msg) != 1:
    raise SystemExit('aggregate focused message anchor missing')
run_path.write_text(run.replace(old_msg, new_msg, 1))
