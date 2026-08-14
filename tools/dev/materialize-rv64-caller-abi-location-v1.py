#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_expression.c")
source = path.read_text()

include_anchor = '#include "target/riscv64/codegen_internal.h"\n'
abi_include = '#include "target/riscv64/abi.h"\n'
if include_anchor not in source:
    raise SystemExit("caller ABI include anchor missing")
if abi_include not in source:
    source = source.replace(include_anchor, include_anchor + abi_include, 1)

declaration_anchor = """        MinicType abi_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];\n        size_t parameter_count;\n"""
declaration_replacement = """        MinicType abi_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];\n        MinicRiscv64AbiArgumentLocation argument_locations[MINIC_MAX_FUNCTION_PARAMETERS];\n        size_t parameter_count;\n"""
if declaration_anchor not in source:
    raise SystemExit("caller ABI location declaration anchor missing")
source = source.replace(declaration_anchor, declaration_replacement, 1)

first_start_anchor = """        {\n            size_t integer_register_index;\n            size_t floating_register_index;\n\n            integer_register_index = 0U;\n            floating_register_index = 0U;\n            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {\n                bool fixed_floating;\n"""
first_end_anchor = """        if (stack_argument_count > (SIZE_MAX - 15U) / 8U) {\n"""
first_start = source.find(first_start_anchor)
if first_start < 0:
    raise SystemExit("caller ABI count-pass start anchor missing")
if source.find(first_start_anchor, first_start + 1) >= 0:
    raise SystemExit("caller ABI count-pass start anchor is not unique")
first_end = source.find(first_end_anchor, first_start)
if first_end < 0:
    raise SystemExit("caller ABI count-pass end anchor missing")

first_replacement = r'''        {
            MinicRiscv64AbiCursor abi_cursor;

            minic_riscv64_abi_cursor_initialize(&abi_cursor);
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                const MinicExpression *argument;
                MinicType placement_type;
                bool is_fixed_parameter;

                argument = minic_c0_program_expression(
                    program, expression->value.call.arguments[argument_index]);
                if (argument == NULL) {
                    return false;
                }
                is_fixed_parameter = argument_index < parameter_count;
                placement_type = is_fixed_parameter ? abi_parameter_types[argument_index]
                                                    : argument->type;
                if (!minic_riscv64_abi_place_argument(program,
                                                       placement_type,
                                                       is_fixed_parameter,
                                                       &abi_cursor,
                                                       &argument_locations[argument_index])) {
                    return false;
                }
            }
            stack_argument_count = abi_cursor.stack_slot_count;
        }
'''
source = source[:first_start] + first_replacement + source[first_end:]

second_start_anchor = """        {\n            size_t integer_register_index;\n            size_t floating_register_index;\n            size_t stack_argument_index;\n\n            integer_register_index = 0U;\n            floating_register_index = 0U;\n            stack_argument_index = 0U;\n"""
second_end_anchor = """\n        if (is_indirect) {\n"""
second_start = source.find(second_start_anchor)
if second_start < 0:
    raise SystemExit("caller ABI emission-pass start anchor missing")
if source.find(second_start_anchor, second_start + 1) >= 0:
    raise SystemExit("caller ABI emission-pass start anchor is not unique")
second_end = source.find(second_end_anchor, second_start)
if second_end < 0:
    raise SystemExit("caller ABI emission-pass end anchor missing")

second_replacement = r'''        {
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                const MinicRiscv64AbiArgumentLocation *location;
                size_t offset;
                bool is_fixed_parameter;

                location = &argument_locations[argument_index];
                offset = outgoing_stack_bytes + (argument_count - 1U - argument_index) * 16U;
                is_fixed_parameter = argument_index < parameter_count;

                if (location->floating_register_count == 1U) {
                    if (!is_fixed_parameter ||
                        location->value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT ||
                        location->floating_register_begin >= 8U ||
                        location->integer_register_count != 0U || location->stack_slot_count != 0U ||
                        fprintf(file,
                                minic_type_is_double(abi_parameter_types[argument_index])
                                    ? "  ld t0, %zu(sp)\n  fmv.d.x fa%zu, t0\n"
                                    : "  ld t0, %zu(sp)\n  fmv.w.x fa%zu, t0\n",
                                offset,
                                location->floating_register_begin) < 0) {
                        return false;
                    }
                    continue;
                }

                if (location->value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                    size_t chunk_index;

                    if (!is_fixed_parameter || location->value.slot_count == 0U ||
                        location->value.slot_count !=
                            location->integer_register_count + location->stack_slot_count ||
                        location->integer_register_begin > 8U ||
                        location->integer_register_count > 8U - location->integer_register_begin) {
                        return false;
                    }
                    for (chunk_index = 0U; chunk_index < location->value.slot_count; ++chunk_index) {
                        size_t chunk_offset;

                        chunk_offset = offset + chunk_index * 8U;
                        if (chunk_index < location->integer_register_count) {
                            size_t register_index;

                            register_index = location->integer_register_begin + chunk_index;
                            if (fprintf(file, "  ld a%zu, %zu(sp)\n", register_index, chunk_offset) <
                                0) {
                                return false;
                            }
                        } else {
                            size_t stack_slot;

                            stack_slot = location->stack_slot_begin +
                                         (chunk_index - location->integer_register_count);
                            if (!minic_riscv64_emit_sp_load64(file, "t0", chunk_offset) ||
                                !minic_riscv64_emit_sp_store64(file, "t0", stack_slot * 8U)) {
                                return false;
                            }
                        }
                    }
                    continue;
                }

                if (location->floating_register_count != 0U ||
                    (location->value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                     location->value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT)) {
                    return false;
                }
                if (location->integer_register_count == 1U && location->stack_slot_count == 0U &&
                    location->integer_register_begin < 8U) {
                    if (fprintf(file,
                                "  ld a%zu, %zu(sp)\n",
                                location->integer_register_begin,
                                offset) < 0) {
                        return false;
                    }
                    continue;
                }
                if (location->integer_register_count == 0U && location->stack_slot_count == 1U) {
                    if (!minic_riscv64_emit_sp_load64(file, "t0", offset) ||
                        !minic_riscv64_emit_sp_store64(
                            file, "t0", location->stack_slot_begin * 8U)) {
                        return false;
                    }
                    continue;
                }
                return false;
            }
        }
'''
source = source[:second_start] + second_replacement + source[second_end:]
path.write_text(source)
print("MATERIALIZED rv64-caller-abi-location-v1")
