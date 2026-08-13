#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    file = Path(path)
    text = file.read_text()
    if old in text:
        file.write_text(text.replace(old, new, 1))
        return
    if new not in text:
        raise SystemExit(f"unexpected {label} anchor")


replace_once(
    "src/target/riscv64/codegen_internal.h",
    """bool minic_riscv64_emit_integer_aggregate_local_chunk(FILE *file,
                                                      const MinicC0Program *program,
                                                      const MinicFunction *function,
                                                      MinicLocalId local_id,
                                                      size_t chunk_index,
                                                      const char *register_name);
""",
    """bool minic_riscv64_emit_integer_aggregate_local_chunk(FILE *file,
                                                      const MinicC0Program *program,
                                                      const MinicFunction *function,
                                                      MinicLocalId local_id,
                                                      size_t chunk_index,
                                                      const char *register_name);
bool minic_riscv64_emit_integer_aggregate_load_chunk(FILE *file,
                                                     const MinicC0Program *program,
                                                     MinicType type,
                                                     size_t chunk_index,
                                                     const char *destination_register,
                                                     const char *address_register);
""",
    "aggregate chunk declaration",
)

replace_once(
    "src/target/riscv64/codegen_support.c",
    """bool minic_riscv64_integer_aggregate_abi(const MinicC0Program *program,
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
""",
    """bool minic_riscv64_integer_aggregate_abi(const MinicC0Program *program,
                                         MinicType type,
                                         size_t *storage_size,
                                         size_t *register_chunks) {
    size_t alignment;
    size_t size;

    if (program == NULL || storage_size == NULL || register_chunks == NULL ||
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

bool minic_riscv64_emit_integer_aggregate_load_chunk(FILE *file,
                                                     const MinicC0Program *program,
                                                     MinicType type,
                                                     size_t chunk_index,
                                                     const char *destination_register,
                                                     const char *address_register) {
    size_t chunk_count;
    size_t chunk_offset;
    size_t chunk_size;
    size_t index;
    size_t storage_size;

    if (file == NULL || destination_register == NULL || address_register == NULL ||
        !minic_riscv64_integer_aggregate_abi(program, type, &storage_size, &chunk_count) ||
        chunk_index >= chunk_count || chunk_index > SIZE_MAX / 8U) {
        return false;
    }
    chunk_offset = chunk_index * 8U;
    chunk_size = storage_size - chunk_offset;
    if (chunk_size > 8U) {
        chunk_size = 8U;
    }
    if (chunk_size == 8U) {
        return fprintf(file, "  ld %s, %zu(%s)\n", destination_register, chunk_offset, address_register) >= 0;
    }
    if (chunk_size == 4U) {
        return fprintf(file, "  lwu %s, %zu(%s)\n", destination_register, chunk_offset, address_register) >= 0;
    }
    if (chunk_size == 2U) {
        return fprintf(file, "  lhu %s, %zu(%s)\n", destination_register, chunk_offset, address_register) >= 0;
    }
    if (chunk_size == 1U) {
        return fprintf(file, "  lbu %s, %zu(%s)\n", destination_register, chunk_offset, address_register) >= 0;
    }
    if (fprintf(file, "  li %s, 0\n", destination_register) < 0) {
        return false;
    }
    for (index = 0U; index < chunk_size; ++index) {
        if (fprintf(file, "  lbu t6, %zu(%s)\n", chunk_offset + index, address_register) < 0 ||
            (index != 0U && fprintf(file, "  slli t6, t6, %zu\n", index * 8U) < 0) ||
            fprintf(file, "  or %s, %s, t6\n", destination_register, destination_register) < 0) {
            return false;
        }
    }
    return true;
}
""",
    "aggregate ABI",
)

replace_once(
    "src/target/riscv64/codegen_support.c",
    """bool minic_riscv64_emit_integer_aggregate_local_chunk(FILE *file,
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
""",
    """bool minic_riscv64_emit_integer_aggregate_local_chunk(FILE *file,
                                                      const MinicC0Program *program,
                                                      const MinicFunction *function,
                                                      MinicLocalId local_id,
                                                      size_t chunk_index,
                                                      const char *register_name) {
    const MinicLocal *local;
    const char *instruction;
    size_t chunk_count;
    size_t chunk_offset;
    size_t chunk_size;
    size_t index;
    size_t storage_size;

    if (register_name == NULL || !minic_riscv64_local_object(program, function, local_id, &local) ||
        !minic_riscv64_integer_aggregate_abi(program, local->type, &storage_size, &chunk_count) ||
        chunk_index >= chunk_count || chunk_index > (SIZE_MAX - local->storage_offset) / 8U) {
        return false;
    }
    chunk_offset = local->storage_offset + chunk_index * 8U;
    chunk_size = storage_size - chunk_index * 8U;
    if (chunk_size > 8U) {
        chunk_size = 8U;
    }
    if (chunk_offset > function->local_storage_size ||
        chunk_size > function->local_storage_size - chunk_offset) {
        return false;
    }
    instruction = chunk_size == 8U   ? "sd"
                  : chunk_size == 4U ? "sw"
                  : chunk_size == 2U ? "sh"
                  : chunk_size == 1U ? "sb"
                                     : NULL;
    if (instruction != NULL) {
        return minic_riscv64_emit_s0_access(file, instruction, register_name, chunk_offset);
    }
    if (fprintf(file, "  mv t1, %s\n", register_name) < 0) {
        return false;
    }
    for (index = 0U; index < chunk_size; ++index) {
        if (!minic_riscv64_emit_s0_access(file, "sb", "t1", chunk_offset + index) ||
            (index + 1U < chunk_size && fprintf(file, "  srli t1, t1, 8\n") < 0)) {
            return false;
        }
    }
    return true;
}
""",
    "aggregate local chunk",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """                    !minic_riscv64_emit_lvalue_address(
                        file,
                        program,
                        function,
                        expression->value.call.arguments[argument_index]) ||
                    !minic_riscv64_emit_stack_allocate(file, 16U) ||
                    fprintf(file,
                            "  mv t0, a0\n"
                            "  ld t1, 0(t0)\n"
                            "  sd t1, 0(sp)\n") < 0 ||
                    (aggregate_chunks == 2U &&
                     fprintf(file, "  ld t1, 8(t0)\n  sd t1, 8(sp)\n") < 0)) {
""",
    """                    !minic_riscv64_emit_lvalue_address(
                        file,
                        program,
                        function,
                        expression->value.call.arguments[argument_index]) ||
                    !minic_riscv64_emit_stack_allocate(file, 16U) ||
                    fprintf(file, "  mv t0, a0\n") < 0 ||
                    !minic_riscv64_emit_integer_aggregate_load_chunk(
                        file, program, abi_parameter_types[argument_index], 0U, "t1", "t0") ||
                    fprintf(file, "  sd t1, 0(sp)\n") < 0 ||
                    (aggregate_chunks == 2U &&
                     (!minic_riscv64_emit_integer_aggregate_load_chunk(
                          file, program, abi_parameter_types[argument_index], 1U, "t1", "t0") ||
                      fprintf(file, "  sd t1, 8(sp)\n") < 0))) {
""",
    "aggregate call staging",
)

replace_once(
    "src/target/riscv64/codegen_statement.c",
    """            if (value->value_category == MINIC_VALUE_LVALUE) {
                if (!minic_riscv64_emit_lvalue_address(
                        file, program, function, statement->expression) ||
                    fprintf(file, "  mv t0, a0\n  ld a0, 0(t0)\n") < 0 ||
                    (aggregate_chunks == 2U && fprintf(file, "  ld a1, 8(t0)\n") < 0)) {
                    return false;
                }
""",
    """            if (value->value_category == MINIC_VALUE_LVALUE) {
                if (!minic_riscv64_emit_lvalue_address(
                        file, program, function, statement->expression) ||
                    fprintf(file, "  mv t0, a0\n") < 0 ||
                    !minic_riscv64_emit_integer_aggregate_load_chunk(
                        file, program, function->return_type, 0U, "a0", "t0") ||
                    (aggregate_chunks == 2U &&
                     !minic_riscv64_emit_integer_aggregate_load_chunk(
                         file, program, function->return_type, 1U, "a1", "t0"))) {
                    return false;
                }
""",
    "aggregate return load",
)
