#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_expression.c")
source = path.read_text()

include_anchor = '#include "target/riscv64/codegen_internal.h"\n'
abi_include = '#include "target/riscv64/abi.h"\n'
if abi_include not in source:
    if include_anchor not in source:
        raise SystemExit("caller ABI hybrid include anchor missing")
    source = source.replace(include_anchor, include_anchor + abi_include, 1)

decl_anchor = """        MinicType abi_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];\n        MinicRiscv64AbiValue abi_values[MINIC_MAX_FUNCTION_PARAMETERS];\n"""
decl_replacement = """        MinicType abi_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];\n        MinicRiscv64AbiValue abi_values[MINIC_MAX_FUNCTION_PARAMETERS];\n        MinicRiscv64AbiArgumentLocation argument_locations[MINIC_MAX_FUNCTION_PARAMETERS];\n        bool use_formal_location_path;\n"""
if decl_anchor not in source:
    raise SystemExit("caller ABI hybrid declaration anchor missing")
source = source.replace(decl_anchor, decl_replacement, 1)

count_anchor = """        {\n            size_t integer_register_index;\n            size_t floating_register_index;\n\n            integer_register_index = 0U;\n            floating_register_index = 0U;\n            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {\n                bool fixed_floating;\n"""
count_start = source.find(count_anchor)
if count_start < 0 or source.find(count_anchor, count_start + 1) >= 0:
    raise SystemExit("caller ABI hybrid count anchor missing/non-unique")
count_end_anchor = """        if (stack_argument_count > (SIZE_MAX - 15U) / 8U) {\n"""
count_end = source.find(count_end_anchor, count_start)
if count_end < 0:
    raise SystemExit("caller ABI hybrid count end anchor missing")
old_count = source[count_start:count_end]

emit_anchor = """        {\n            size_t integer_register_index;\n            size_t floating_register_index;\n            size_t stack_argument_index;\n\n            integer_register_index = 0U;\n            floating_register_index = 0U;\n            stack_argument_index = 0U;\n"""
emit_start = source.find(emit_anchor)
if emit_start < 0 or source.find(emit_anchor, emit_start + 1) >= 0:
    raise SystemExit("caller ABI hybrid emit anchor missing/non-unique")
emit_end_anchor = """\n        if (is_indirect) {\n"""
emit_end = source.find(emit_end_anchor, emit_start)
if emit_end < 0:
    raise SystemExit("caller ABI hybrid emit end anchor missing")
old_emit = source[emit_start:emit_end]

formal_count = r'''        if (use_formal_location_path) {
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
        } else {
'''
formal_count += old_count
formal_count += "        }\n"

source = source[:count_start] + formal_count + source[count_end:]

# Re-find emission block after the first replacement shifted offsets.
emit_start = source.find(emit_anchor)
if emit_start < 0 or source.find(emit_anchor, emit_start + 1) >= 0:
    raise SystemExit("caller ABI hybrid emit anchor lost after count rewrite")
emit_end = source.find(emit_end_anchor, emit_start)
if emit_end < 0:
    raise SystemExit("caller ABI hybrid emit end anchor lost after count rewrite")
old_emit = source[emit_start:emit_end]

formal_emit = r'''        if (use_formal_location_path) {
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                const MinicRiscv64AbiArgumentLocation *location;
                size_t offset;
                bool is_fixed_parameter;

                location = &argument_locations[argument_index];
                is_fixed_parameter = argument_index < parameter_count;
                if (argument_stage_end[argument_index] > staged_bytes) {
                    return false;
                }
                offset = outgoing_stack_bytes + staged_bytes - argument_stage_end[argument_index];

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

                    if (location->value.storage_size != 8U && location->value.storage_size != 16U) {
                        return false;
                    }
                    if (location->value.slot_count == 0U ||
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
        } else {
'''
formal_emit += old_emit
formal_emit += "        }\n"
source = source[:emit_start] + formal_emit + source[emit_end:]

# Decide once per call whether every argument is within the formal-v1 ABI capability.
# The decision is inserted immediately before the first placement/count block.
count_wrapper_marker = "        if (use_formal_location_path) {\n            MinicRiscv64AbiCursor abi_cursor;\n"
insert_at = source.find(count_wrapper_marker)
if insert_at < 0:
    raise SystemExit("caller ABI hybrid wrapper marker missing")
selector = r'''        use_formal_location_path = true;
        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            const MinicRiscv64AbiValue *value;

            value = &abi_values[argument_index];
            if (value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE ||
                value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT ||
                (value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
                 (argument_index >= parameter_count ||
                  (value->storage_size != 8U && value->storage_size != 16U)))) {
                use_formal_location_path = false;
                break;
            }
        }
'''
source = source[:insert_at] + selector + source[insert_at:]
path.write_text(source)
print("MATERIALIZED rv64-caller-abi-location-hybrid")
