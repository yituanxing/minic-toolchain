#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_function.c")
source = path.read_text()

include_anchor = '#include "target/riscv64/codegen_internal.h"\n'
abi_include = '#include "target/riscv64/abi.h"\n'
if abi_include not in source:
    if include_anchor not in source:
        raise SystemExit("callee ABI include anchor missing")
    source = source.replace(include_anchor, include_anchor + abi_include, 1)

start_anchor = """    if (success) {\n        size_t parameter_index;\n        size_t integer_register_index;\n        size_t floating_register_index;\n        size_t stack_parameter_index;\n"""
end_anchor = """    if (success) {\n        success =\n            minic_riscv64_emit_block(file, program, function, function->body_block, label_counter);\n"""
start = source.find(start_anchor)
if start < 0:
    raise SystemExit("extended callee ABI start anchor missing")
if source.find(start_anchor, start + 1) >= 0:
    raise SystemExit("extended callee ABI start anchor is not unique")
end = source.find(end_anchor, start)
if end < 0:
    raise SystemExit("extended callee ABI end anchor missing")

replacement = r'''    if (success) {
        MinicRiscv64AbiCursor abi_cursor;
        size_t parameter_index;

        minic_riscv64_abi_cursor_initialize(&abi_cursor);
        for (parameter_index = 0U; success && parameter_index < function->parameter_count;
             ++parameter_index) {
            const MinicLocal *parameter;
            MinicLocalId local_id;
            MinicRiscv64AbiArgumentLocation location;

            local_id = function->local_begin + parameter_index;
            parameter = minic_c0_program_local(program, local_id);
            if (parameter == NULL || !minic_riscv64_abi_place_argument(
                                         program, parameter->type, true, &abi_cursor, &location)) {
                success = false;
                break;
            }

            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                if (location.integer_register_count != 0U ||
                    location.floating_register_count != 0U || location.stack_slot_count != 0U) {
                    success = false;
                }
                continue;
            }

            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT) {
                if (location.floating_register_count != 1U ||
                    location.floating_register_begin >= 8U ||
                    location.integer_register_count != 0U || location.stack_slot_count != 0U) {
                    success = false;
                    break;
                }
                success = fprintf(file,
                                  minic_type_is_double(parameter->type)
                                      ? "  fmv.x.d t0, fa%zu\n"
                                      : "  fmv.x.w t0, fa%zu\n",
                                  location.floating_register_begin) >= 0 &&
                          minic_riscv64_emit_object_store_register(
                              file, program, function, local_id, "t0");
                continue;
            }

            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                size_t byte_index;

                if (location.integer_register_count == 1U && location.stack_slot_count == 0U &&
                    location.integer_register_begin < 8U) {
                    success = fprintf(file,
                                      "  mv t0, %s\n",
                                      minic_riscv64_argument_registers
                                          [location.integer_register_begin]) >= 0;
                } else if (location.integer_register_count == 0U &&
                           location.stack_slot_count == 1U) {
                    size_t incoming_offset;

                    if (location.stack_slot_begin > (SIZE_MAX - frame_size) / 8U) {
                        success = false;
                        break;
                    }
                    incoming_offset = frame_size + location.stack_slot_begin * 8U;
                    success = minic_riscv64_emit_sp_load64(file, "t0", incoming_offset);
                } else {
                    success = false;
                }
                if (!success || !minic_riscv64_emit_object_address(file, program, function, local_id)) {
                    success = false;
                    break;
                }
                for (byte_index = 0U; success && byte_index < location.value.storage_size;
                     ++byte_index) {
                    if (byte_index <= 2047U) {
                        success = fprintf(file,
                                          "  lbu t1, %zu(t0)\n  sb t1, %zu(a0)\n",
                                          byte_index,
                                          byte_index) >= 0;
                    } else {
                        success = fprintf(file,
                                          "  li t2, %zu\n"
                                          "  add t3, t0, t2\n"
                                          "  lbu t1, 0(t3)\n"
                                          "  add t3, a0, t2\n"
                                          "  sb t1, 0(t3)\n",
                                          byte_index) >= 0;
                    }
                }
                continue;
            }

            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                size_t chunk_index;

                if (location.value.slot_count == 0U ||
                    location.value.slot_count != location.value.register_chunks ||
                    location.value.slot_count !=
                        location.integer_register_count + location.stack_slot_count ||
                    location.integer_register_begin > 8U ||
                    location.integer_register_count > 8U - location.integer_register_begin) {
                    success = false;
                    break;
                }
                for (chunk_index = 0U; success && chunk_index < location.value.slot_count;
                     ++chunk_index) {
                    const char *source_register;

                    source_register = "t0";
                    if (chunk_index < location.integer_register_count) {
                        source_register = minic_riscv64_argument_registers
                            [location.integer_register_begin + chunk_index];
                    } else {
                        size_t incoming_offset;
                        size_t stack_slot;

                        stack_slot = location.stack_slot_begin +
                                     (chunk_index - location.integer_register_count);
                        if (stack_slot > (SIZE_MAX - frame_size) / 8U) {
                            success = false;
                            break;
                        }
                        incoming_offset = frame_size + stack_slot * 8U;
                        success = minic_riscv64_emit_sp_load64(file, "t0", incoming_offset);
                    }
                    if (success) {
                        success = minic_riscv64_emit_integer_aggregate_local_chunk(
                            file, program, function, local_id, chunk_index, source_register);
                    }
                }
                continue;
            }

            if (location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
                location.floating_register_count != 0U) {
                success = false;
                break;
            }
            if (location.integer_register_count == 1U && location.stack_slot_count == 0U &&
                location.integer_register_begin < 8U) {
                success = minic_riscv64_emit_object_store_register(
                    file,
                    program,
                    function,
                    local_id,
                    minic_riscv64_argument_registers[location.integer_register_begin]);
                continue;
            }
            if (location.integer_register_count == 0U && location.stack_slot_count == 1U) {
                size_t incoming_offset;

                if (location.stack_slot_begin > (SIZE_MAX - frame_size) / 8U) {
                    success = false;
                    break;
                }
                incoming_offset = frame_size + location.stack_slot_begin * 8U;
                success = minic_riscv64_emit_sp_load64(file, "t0", incoming_offset) &&
                          minic_riscv64_emit_object_store_register(
                              file, program, function, local_id, "t0");
                continue;
            }
            success = false;
        }
    }
'''

source = source[:start] + replacement + source[end:]
path.write_text(source)
print("MATERIALIZED rv64-callee-abi-location-linux")
